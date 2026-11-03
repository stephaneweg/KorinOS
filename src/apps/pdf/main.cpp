//
// main.cpp -- Lecteur PDF, le port d'user/Apps/pdf d'Onyx (docs/pdf/README.md d'Onyx) sur l'UIKit d'ARMPI, sans pont : un
// onglet par document ; les pages en continu, une a une ou deux par deux ; le zoom (pleine largeur, page entiere, 50 a
// 400 %), la rotation ; le panneau de gauche (vignettes, sommaire, resultats de recherche) ; le texte choisi et copie, les
// liens ; les mots de passe ; les proprietes ; le plein ecran ; l'accueil (documents recents, repris ou on les a laisses).
//
// MuPDF dessine (engine.h, tools/pdf/build.sh). A cause de lui cette application est sous AGPL-3.0 (README.md).
// Fichiers : SD0:/home/pdf/settings.ini, SD0:/home/pdf/recent.tsv.
//
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <dirent.h>
#include <spawn.h>
#include <initializer_list>
#include "engine.h"
#include "ui.h"
#include <printerkit.h>

using namespace pdfv;

extern char **environ;

#define CONF_DIR "SD0:/home/pdf"
#define SETTINGS CONF_DIR "/settings.ini"
#define RECENT CONF_DIR "/recent.tsv"
#define WEB_PROGRAM "SD0:/apps/jet.app/main"
#define FILES_PROGRAM "SD0:/apps/files.app/main"

static inline const char *TR (const char *s) { return (const char *) ui_tr ((long) s); }

// ---- tailles, couleurs --------------------------------------------------------------------------------------------
enum { TABS_H = 34, TB_H = 46, SIDE_W = 224, MARGIN = 16, GAP = 14, SB_W = 14 };
static unsigned col_canvas () { return mix (jet (J_CREUX), C_TEXT, 60); }	// derriere les pages
enum { HID_ENTER = 0x28, HID_ESC = 0x29, HID_BKSP = 0x2A, HID_TAB = 0x2B, HID_SPACE = 0x2C, HID_F3 = 0x3C, HID_F9 = 0x42, HID_F5 = 0x3E,
       HID_HOME = 0x4A, HID_PGUP = 0x4B, HID_END = 0x4D, HID_PGDN = 0x4E, HID_RIGHT = 0x4F, HID_LEFT = 0x50, HID_DOWN = 0x51, HID_UP = 0x52 };

static inline unsigned ticks ()			// en centiemes de seconde
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 100 + t.tv_nsec / 10000000);
}

// ---- un onglet : un document et sa facon d'etre montre --------------------------------------------------------------
enum { L_SINGLE, L_SCROLL, L_TWO };
enum { Z_WIDTH, Z_PAGE, Z_CUSTOM };
enum { S_PAGES, S_CONTENTS, S_FIND };
struct Tab
{
	Doc *doc;				// 0 : l'accueil
	int layout, zoomMode; float zoom; int rot;
	int sx, sy;				// le defilement de la vue (px)
	int cur;				// la page courante (la plus visible)
	int side, thumbScroll, olScroll, findScroll;
	int selPage; float ax, ay, bx, by; bool sel;	// la selection (sur une page), ses bouts en points de la page
	char needle[200]; int opts, gen; SearchHit *hits; int nhits, capHits, searched; bool done; int hit;
	int pendingPage; float pendingY;	// (une page a montrer quand la mise en page sera connue)
};
enum { MAX_TABS = 12 };
static Tab g_tabs[MAX_TABS]; static int g_ntabs, g_cur;
static inline Tab &tab () { return g_tabs[g_cur]; }
static Worker g_worker;
static int g_sideOn = 1, g_defLayout = L_SCROLL, g_defZoom = Z_WIDTH, g_handOn;
static unsigned g_tick;

static long g_tabsW, g_vTb, g_vSide, g_view, g_vHome, g_pageBox, g_findBox, g_sideBox;
static bool g_tabsDirty = true;

static void refresh_all ();
static void open_path (const char *path, int page = -1, bool newTab = true);
static void save_settings ();
static void recent_note (Tab &t);
static void search_start (Tab &t, const char *needle);
static void goto_hit (Tab &t, int k);
static void show_page (Tab &t, int pg, float y = -1);
static void find_next (int dir);
static void side_follow ();

// ce qui ouvre un dialogue ou un menu attend la sortie de ui_w_event
enum { A_NONE, A_OPEN, A_ZOOM_MENU, A_MORE_MENU, A_FULL, A_CTX, A_RECENT, A_RECENT_MENU, A_FOLDER, A_LINK, A_THUMB, A_OL };
static int g_act, g_actA, g_actX, g_actY;
static inline void act (int what, int a = 0, int x = 0, int y = 0) { g_act = what; g_actA = a; g_actX = x; g_actY = y; }

// ---- les images des pages (celles de l'ouvrier), gardees tant qu'il y a la place ----------------------------------------
struct Bmp { int docId, page, skey, rot, cx, cy, cw, ch; bool thumb; unsigned *px; unsigned use; };
enum { MAX_BMP = 160 };
static Bmp g_bmp[MAX_BMP]; static int g_nbmp; static long g_bmpPx; static unsigned g_bmpClock;
static const long BMP_BUDGET = 18L << 20;	// pixels (x 4 octets)
static inline int skey (float s) { return (int) (s * 4096 + 0.5f); }
static void bmp_drop (int i) { g_bmpPx -= (long) g_bmp[i].cw * g_bmp[i].ch; free (g_bmp[i].px); g_bmp[i] = g_bmp[--g_nbmp]; }
static void bmp_forget_doc (int docId) { for (int i = g_nbmp - 1; i >= 0; i--) if (g_bmp[i].docId == docId) bmp_drop (i); }
static void bmp_add (const Job &j, unsigned *px)
{
	while (g_nbmp && (g_nbmp >= MAX_BMP || g_bmpPx + (long) j.cw * j.ch > BMP_BUDGET))
	{
		int old = 0; for (int i = 1; i < g_nbmp; i++) if (g_bmp[i].use < g_bmp[old].use) old = i;
		bmp_drop (old);
	}
	Bmp &b = g_bmp[g_nbmp++];
	b.docId = j.docId; b.page = j.page; b.skey = skey (j.scale); b.rot = j.rot; b.cx = j.cx; b.cy = j.cy; b.cw = j.cw; b.ch = j.ch;
	b.thumb = j.kind == J_THUMB; b.px = px; b.use = ++g_bmpClock;
	g_bmpPx += (long) j.cw * j.ch;
}
// la meilleure image d'une page : cette echelle couvrant la partie (x, y, l, h) ; sinon une page entiere d'une autre echelle
static Bmp *bmp_find (int docId, int page, float s, int rot, int x, int y, int w, int h, bool *exact, bool thumb = false)
{
	Bmp *best = 0; int k = skey (s); *exact = false;
	for (int i = 0; i < g_nbmp; i++)
	{
		Bmp &b = g_bmp[i];
		if (b.docId != docId || b.page != page || b.rot != rot) continue;
		if (b.skey == k && b.thumb == thumb && b.cx <= x && b.cy <= y && b.cx + b.cw >= x + w && b.cy + b.ch >= y + h) { b.use = ++g_bmpClock; *exact = true; return &b; }
		if (b.cx == 0 && b.cy == 0 && (!best || abs (b.skey - k) < abs (best->skey - k))) best = &b;
	}
	if (best) best->use = ++g_bmpClock;
	return best;
}
// l'image b (a son echelle bs) dans la vue : le coin de la page a l'echelle s est en (px, py), decoupe (clx, cly, clw, clh)
static void blit_page (const Bmp &b, float bs, float s, int px, int py, int clx, int cly, int clw, int clh)
{
	float r = s / bs;
	int dx0 = px + (int) (b.cx * r), dy0 = py + (int) (b.cy * r);
	int dw = (int) (b.cw * r + 0.5f), dh = (int) (b.ch * r + 0.5f);
	int x0 = dx0 > clx ? dx0 : clx, y0 = dy0 > cly ? dy0 : cly;
	int x1 = dx0 + dw < clx + clw ? dx0 + dw : clx + clw, y1 = dy0 + dh < cly + clh ? dy0 + dh : cly + clh;
	if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > g_pw) x1 = (int) g_pw; if (y1 > g_ph) y1 = (int) g_ph;
	if (x1 <= x0 || y1 <= y0) return;
	if (dw == b.cw && dh == b.ch)
	{
		for (int y = y0; y < y1; y++) memcpy (g_px + (size_t) y * g_pw + x0, b.px + (size_t) (y - dy0) * b.cw + (x0 - dx0), (size_t) (x1 - x0) * 4);
		return;
	}
	int fx = (int) (65536.0f / r);
	for (int y = y0; y < y1; y++)
	{
		int sy = (int) ((y - dy0) / r); if (sy >= b.ch) sy = b.ch - 1;
		const unsigned *src = b.px + (size_t) sy * b.cw;
		uint32_t *dst = g_px + (size_t) y * g_pw;
		int acc = (x0 - dx0) * fx;
		for (int x = x0; x < x1; x++, acc += fx) { int sx = acc >> 16; dst[x] = src[sx < b.cw ? sx : b.cw - 1]; }
	}
}

// ---- la mise en page d'un onglet ------------------------------------------------------------------------------------
struct PagePos { int x, y, w, h; };
static PagePos *g_pos; static int g_posCap;
static int g_layW, g_layH, g_layDoc = -1, g_layMode = -1, g_layRot = -1, g_layNp; static float g_layS = -1;
static int g_contentW, g_contentH;
static float g_scale;				// px par point
static float fit_scale (Tab &t, int W, int H, int zm)
{
	Doc *D = t.doc; float mw = 1, mh = 1;
	for (int i = 0; i < D->npages; i++) { float a = D->pw[i], b = D->ph[i]; if (t.rot % 180) { float x = a; a = b; b = x; } if (a > mw) mw = a; if (b > mh) mh = b; }
	int cols = t.layout == L_TWO ? 2 : 1;
	float sw = (W - 2 * MARGIN - (cols - 1) * GAP) / (cols * mw);
	if (zm == Z_WIDTH) return sw;
	float sh = (H - 2 * MARGIN) / mh;
	return sw < sh ? sw : sh;
}
static float tab_scale (Tab &t, int W, int H)
{
	if (t.zoomMode == Z_CUSTOM) return t.zoom;
	float s = fit_scale (t, W, H, t.zoomMode);
	return s < 0.05f ? 0.05f : s;
}
static void lay_out (Tab &t, int W, int H)
{
	Doc *D = t.doc; if (!D) return;
	float s = tab_scale (t, W, H);
	if (g_layDoc == D->id && g_layW == W && g_layH == H && g_layMode == t.layout && g_layRot == t.rot && g_layS == s && g_layNp == D->npages) { g_scale = s; return; }
	g_layDoc = D->id; g_layW = W; g_layH = H; g_layMode = t.layout; g_layRot = t.rot; g_layS = s; g_layNp = D->npages; g_scale = s;
	if (g_posCap < D->npages) { g_posCap = D->npages; g_pos = (PagePos *) realloc (g_pos, sizeof (PagePos) * g_posCap); }
	int y = MARGIN, maxw = 0;
	if (t.layout == L_TWO)
	{	// la premiere page seule (une couverture, a droite), puis deux par deux : celle de gauche finit au milieu
		int half = 0;
		for (int i = 0; i < D->npages; i++) { int w, h; page_px (D, i, s, t.rot, &w, &h); g_pos[i] = PagePos { 0, 0, w, h }; if (w > half) half = w; }
		int cw = 2 * half + GAP + 2 * MARGIN; if (cw < W) cw = W;
		int mid = cw / 2;
		for (int i = 0; i < D->npages; )
		{
			if (i == 0) { g_pos[0].y = y; g_pos[0].x = D->npages == 1 ? (cw - g_pos[0].w) / 2 : mid + GAP / 2; y += g_pos[0].h + GAP; i = 1; continue; }
			int b = i + 1 < D->npages ? i + 1 : -1, rowH = g_pos[i].h;
			g_pos[i].y = y; g_pos[i].x = mid - GAP / 2 - g_pos[i].w;
			if (b >= 0) { g_pos[b].y = y; g_pos[b].x = mid + GAP / 2; if (g_pos[b].h > rowH) rowH = g_pos[b].h; }
			y += rowH + GAP; i = b >= 0 ? b + 1 : i + 1;
		}
		g_contentW = cw;
	}
	else
	{
		for (int i = 0; i < D->npages; i++) { int w, h; page_px (D, i, s, t.rot, &w, &h); g_pos[i] = PagePos { 0, y, w, h }; if (w > maxw) maxw = w; y += h + GAP; }
		int cw = maxw + 2 * MARGIN; if (cw < W) cw = W;
		for (int i = 0; i < D->npages; i++) g_pos[i].x = (cw - g_pos[i].w) / 2;
		g_contentW = cw;
	}
	g_contentH = y - GAP + MARGIN;
}
static void invalidate_layout () { g_layDoc = -1; }
static void scroll_range (Tab &t, int H, int *y0, int *y1)
{
	if (t.layout == L_SINGLE && t.doc)
	{
		const PagePos &p = g_pos[t.cur];
		*y0 = p.y - MARGIN; *y1 = p.y + p.h + MARGIN - H; if (*y1 < *y0) { int c = p.y - (H - p.h) / 2; *y0 = *y1 = c; }
		return;
	}
	*y0 = 0; *y1 = g_contentH - H; if (*y1 < 0) *y1 = 0;
}
static void clamp_scroll (Tab &t, int W, int H)
{
	int y0, y1; scroll_range (t, H, &y0, &y1);
	if (t.sy < y0) t.sy = y0; if (t.sy > y1) t.sy = y1;
	int x1 = g_contentW - W; if (x1 < 0) x1 = 0;
	if (t.sx < 0) t.sx = 0; if (t.sx > x1) t.sx = x1;
}
static int page_at (Tab &t, int H)
{
	Doc *D = t.doc; int best = t.cur, bestA = -1;
	for (int i = 0; i < D->npages; i++)
	{
		int a = g_pos[i].y > t.sy ? g_pos[i].y : t.sy, b = g_pos[i].y + g_pos[i].h < t.sy + H ? g_pos[i].y + g_pos[i].h : t.sy + H;
		if (g_pos[i].y > t.sy + H) break;
		if (b - a > bestA) { bestA = b - a; best = i; }
	}
	return best;
}

// ---- le texte des pages cote fenetre (selection), les dernieres seulement ------------------------------------------------
struct StCache { int docId, page; fz_stext_page *t; unsigned use; };
static StCache g_st[4]; static unsigned g_stClock;
static fz_stext_page *text_of (Doc *D, int pg)
{
	int old = 0;
	for (int i = 0; i < 4; i++)
	{
		if (g_st[i].t && g_st[i].docId == D->id && g_st[i].page == pg) { g_st[i].use = ++g_stClock; return g_st[i].t; }
		if (!g_st[i].t || g_st[i].use < g_st[old].use) old = i;
	}
	if (g_st[old].t) fz_drop_stext_page (g_mu, g_st[old].t);
	g_st[old].t = page_text (g_mu, D, pg); g_st[old].docId = D->id; g_st[old].page = pg; g_st[old].use = ++g_stClock;
	return g_st[old].t;
}
static void text_forget (int docId) { for (int i = 0; i < 4; i++) if (g_st[i].t && g_st[i].docId == docId) { fz_drop_stext_page (g_mu, g_st[i].t); g_st[i].t = 0; } }

// ---- ce qu'on demande a l'ouvrier : les pages vues, les voisines, les vignettes, les documents recents ----------------------
static Job g_wantView[24]; static int g_nwantView;
static Job g_wantSide[24]; static int g_nwantSide;
static Job g_wantHome[12]; static int g_nwantHome;
static void send_wants ()
{
	Job all[WANT_MAX]; int n = 0;
	for (int i = 0; i < g_nwantView && n < WANT_MAX; i++) all[n++] = g_wantView[i];
	for (int i = 0; i < g_nwantHome && n < WANT_MAX; i++) all[n++] = g_wantHome[i];
	for (int i = 0; i < g_nwantSide && n < WANT_MAX; i++) all[n++] = g_wantSide[i];
	g_worker.set_wants (all, n);
}
static Job page_job (Tab &t, int pg, float s, int cx, int cy, int cw, int ch, int kind = J_PAGE)
{
	Job j; memset (&j, 0, sizeof j);
	j.kind = kind; j.doc = t.doc; j.docId = t.doc->id; j.page = pg; j.scale = s; j.rot = t.rot; j.cx = cx; j.cy = cy; j.cw = cw; j.ch = ch;
	return j;
}

// un rectangle en points d'une page -> la vue (coin de la page en (px, py))
static void quad_to_px (Doc *D, int pg, float s, int rot, Quad q, int px, int py, int *x, int *y, int *w, int *h)
{
	fz_rect r = fz_transform_rect (fz_make_rect (q.x0, q.y0, q.x1, q.y1), page_ctm (D, pg, s, rot));
	*x = px + (int) r.x0; *y = py + (int) r.y0; *w = (int) (r.x1 - r.x0 + 0.99f); *h = (int) (r.y1 - r.y0 + 0.99f);
}
static fz_point px_to_page (Doc *D, int pg, float s, int rot, int x, int y)
{
	return fz_transform_point (fz_make_point ((float) x, (float) y), fz_invert_matrix (page_ctm (D, pg, s, rot)));
}
static void bubble (int cx, int y, const char *s)
{
	int w = tw (s, F_UI, 2) + 24;
	rbox (cx - w / 2, y, w, 26, 13, 0x3A3634);
	text_c (cx - w / 2, y, w, 26, s, 0xFFFFFF, F_UI, 2);
}
// la poignee d'une barre de defilement : (taille, position) dans un rail de longueur rail
static void thumb_of (long total, int seen, long pos, int rail, int *y, int *h)
{
	if (total <= seen) { *y = 0; *h = 0; return; }
	int t = (int) ((long) rail * seen / total); if (t < 28) t = 28;
	long m = total - seen; *y = m ? (int) ((rail - t) * pos / m) : 0; *h = t;
}

// ---- la vue : les pages -------------------------------------------------------------------------------------------------
enum { TILE_LIMIT = 2600 * 2600 };
static unsigned g_pillT;			// quand la pastille de page a ete montree
static int g_drag;				// 0 rien, 1 selection, 2 barre verticale, 3 glisser les pages, 4 lien appuye, 5 barre horizontale
static int g_pressX, g_pressY, g_pressSy, g_pressSx, g_linkPage, g_linkIdx, g_pressBtn;
static unsigned g_lastClickT; static int g_clicks, g_lastCX, g_lastCY;
static int g_vwW, g_vwH, g_vwX;
static inline int vw () { return g_vwW - SB_W; }
static inline int vh () { return g_vwH; }

static long view_paint (long me, long, long)
{
	begin_paint (me);
	Tab &t = tab ();
	unsigned bg = col_canvas ();
	ui_fill (0, 0, g_pw, g_ph, bg);
	g_nwantView = 0;
	if (!t.doc) { send_wants (); return 0; }
	Doc *D = t.doc;
	int W = vw (), H = vh ();
	lay_out (t, W, H);
	clamp_scroll (t, W, H);
	float s = g_scale;
	for (int i = 0; i < D->npages; i++)
	{
		const PagePos &p = g_pos[i];
		if (t.layout == L_SINGLE && i != t.cur) continue;
		int x = p.x - t.sx, y = p.y - t.sy;
		if (y > H || y + p.h < 0 || x > W || x + p.w < 0) continue;
		veil (x + 2, y + 3, p.w, p.h, 0, 60);
		ui_fill (x, y, p.w, p.h, 0xFFFFFF);
		// l'image : la page entiere, ou (grande) la partie vue, sur une grille de 256 px, avec une marge
		int cx = 0, cy = 0, cw = p.w, ch = p.h;
		if ((long) p.w * p.h > TILE_LIMIT)
		{
			int vx0 = (t.sx - p.x) - 128, vy0 = (t.sy - p.y) - 128, vx1 = vx0 + W + 256, vy1 = vy0 + H + 256;
			vx0 = vx0 < 0 ? 0 : vx0 & ~255; vy0 = vy0 < 0 ? 0 : vy0 & ~255;
			vx1 = vx1 > p.w ? p.w : (vx1 + 255) & ~255; vy1 = vy1 > p.h ? p.h : (vy1 + 255) & ~255;
			if (vx1 > p.w) vx1 = p.w; if (vy1 > p.h) vy1 = p.h;
			cx = vx0; cy = vy0; cw = vx1 - vx0; ch = vy1 - vy0;
		}
		bool exact; Bmp *b = bmp_find (D->id, i, s, t.rot, cx, cy, cw, ch, &exact);
		if (b) blit_page (*b, b->skey / 4096.0f, s, x, y, x > 0 ? x : 0, y > 0 ? y : 0, W, H);
		if (!exact && g_nwantView < 24) g_wantView[g_nwantView++] = page_job (t, i, s, cx, cy, cw, ch);
		// les resultats de la recherche, la selection
		for (int k = 0; k < t.nhits; k++)
		{
			if (t.hits[k].page != i) continue;
			for (int q = 0; q < t.hits[k].nq; q++)
			{
				int hx, hy, hw, hh; quad_to_px (D, i, s, t.rot, t.hits[k].q[q], x, y, &hx, &hy, &hw, &hh);
				veil (hx - 1, hy - 1, hw + 2, hh + 2, k == t.hit ? 0xFF8C1E : 0xFFD640, k == t.hit ? 150 : 120);
			}
		}
		if (t.sel && t.selPage == i)
		{
			fz_stext_page *st = text_of (D, i);
			if (st)
			{
				static fz_quad qs[1024];
				int n = fz_highlight_selection (g_mu, st, fz_make_point (t.ax, t.ay), fz_make_point (t.bx, t.by), qs, 1024);
				for (int k = 0; k < n; k++)
				{
					int hx, hy, hw, hh; quad_to_px (D, i, s, t.rot, quad_box (qs[k]), x, y, &hx, &hy, &hw, &hh);
					veil (hx, hy, hw, hh + 1, C_ACCENT, 90);
				}
			}
		}
	}
	// les pages proches, d'avance (assez petites)
	int near[4] = { t.cur + 1, t.cur - 1, t.cur + 2, t.cur + 3 };
	for (int k = 0; k < 4; k++)
	{
		int i = near[k]; if (i < 0 || i >= D->npages || g_nwantView >= 24) continue;
		const PagePos &p = g_pos[i]; if ((long) p.w * p.h > TILE_LIMIT) continue;
		bool exact; bmp_find (D->id, i, s, t.rot, 0, 0, p.w, p.h, &exact);
		if (!exact) g_wantView[g_nwantView++] = page_job (t, i, s, 0, 0, p.w, p.h);
	}
	send_wants ();
	// les barres de defilement
	int y0, y1; scroll_range (t, H, &y0, &y1);
	long total = (long) (y1 - y0) + H, pos = t.sy - y0;
	ui_fill (W, 0, SB_W, H, mix (bg, C_TEXT, 30));
	int ty, th; thumb_of (total, H, pos, H - 4, &ty, &th);
	if (th) rbox (W + 3, 2 + ty, SB_W - 6, th, (SB_W - 6) / 2, C_DIM);
	if (g_contentW > W)
	{
		thumb_of (g_contentW, W, t.sx, W - 4, &ty, &th);
		veil (0, H - 11, W, 11, C_TEXT, 40);
		if (th) rbox (2 + ty, H - 9, th, 7, 3, C_DIM);
	}
	if (g_tick - g_pillT < 150)
	{
		char b[64]; snprintf (b, sizeof b, TR ("Page %d of %d"), t.cur + 1, D->npages);
		bubble (W / 2, H - 46, b);
	}
	return 0;
}

static int page_under (int mx, int my, fz_point *pt)
{
	Tab &t = tab (); Doc *D = t.doc;
	if (!D) return -1;
	for (int i = 0; i < D->npages; i++)
	{
		if (t.layout == L_SINGLE && i != t.cur) continue;
		const PagePos &p = g_pos[i];
		int x = p.x - t.sx, y = p.y - t.sy;
		if (mx >= x && my >= y && mx < x + p.w && my < y + p.h) { if (pt) *pt = px_to_page (D, i, g_scale, t.rot, mx - x, my - y); return i; }
	}
	return -1;
}
static int link_at (int pg, fz_point pt)
{
	Doc *D = tab ().doc;
	if (!D->links[pg].loaded) return -1;
	for (int k = 0; k < D->links[pg].n; k++) { Quad r = D->links[pg].l[k].r; if (pt.x >= r.x0 && pt.x <= r.x1 && pt.y >= r.y0 && pt.y <= r.y1) return k; }
	return -1;
}
static void scroll_by (int dx, int dy)
{
	Tab &t = tab (); if (!t.doc) return;
	int W = vw (), H = vh ();
	if (t.layout == L_SINGLE && dy)
	{	// une page a la fois : passe son bout, la suivante
		int y0, y1; scroll_range (t, H, &y0, &y1);
		if (dy > 0 && t.sy >= y1 && t.cur + 1 < t.doc->npages) { t.cur++; scroll_range (t, H, &y0, &y1); t.sy = y0; g_pillT = g_tick; refresh_all (); return; }
		if (dy < 0 && t.sy <= y0 && t.cur > 0) { t.cur--; scroll_range (t, H, &y0, &y1); t.sy = y1; g_pillT = g_tick; refresh_all (); return; }
	}
	t.sx += dx; t.sy += dy;
	clamp_scroll (t, W, H);
	int c = t.layout == L_SINGLE ? t.cur : page_at (t, H);
	g_pillT = g_tick;
	if (c != t.cur) { t.cur = c; side_follow (); refresh_all (); }
	else ui_view_dirty (g_view);
}
static void zoom_at (float f, int mx, int my)
{
	Tab &t = tab (); if (!t.doc) return;
	int W = vw (), H = vh ();
	float s0 = g_scale, s1 = s0 * f;
	if (s1 < 0.25f * 0.75f) s1 = 0.25f * 0.75f; if (s1 > 6.0f) s1 = 6.0f;
	float fx = (t.sx + mx) / s0, fy = (t.sy + my) / s0;	// le point sous la souris y reste
	t.zoomMode = Z_CUSTOM; t.zoom = s1;
	lay_out (t, W, H);
	t.sx = (int) (fx * g_scale) - mx; t.sy = (int) (fy * g_scale) - my;
	clamp_scroll (t, W, H);
	refresh_all ();
}
static void bar_to (int my)
{
	Tab &t = tab (); int H = vh ();
	int y0, y1, ty, th; scroll_range (t, H, &y0, &y1);
	long total = (long) (y1 - y0) + H;
	thumb_of (total, H, t.sy - y0, H - 4, &ty, &th);
	if (!th) return;
	int r = H - 4 - th; if (r < 1) r = 1;
	t.sy = y0 + (int) ((long) (my - 2 - th / 2) * (total - H) / r);
	scroll_by (0, 0);
}
static long view_mouse (long me, long kind, long lx, long ly, long info)
{
	Tab &t = tab ();
	if (!t.doc) return 0;
	int mx = (int) lx, my = (int) ly, W = vw (), H = vh (), mods = (int) (info >> 16 & 255);
	if (kind == 3) return 0;
	if (kind == 1)
	{
		int btn = (int) (info >> 8 & 255), cl = (int) (info & 255);
		g_pressBtn = btn; g_pressX = mx; g_pressY = my; g_pressSx = t.sx; g_pressSy = t.sy; g_drag = 0;
		if (btn & 2) { act (A_CTX, 0, mx, my); return 0; }
		if (btn & 4) { g_drag = 3; return 0; }
		if (mx >= W) { g_drag = 2; bar_to (my); return 0; }
		if (g_contentW > W && my >= H - 11) { g_drag = 5; return 0; }
		fz_point pt; int pg = page_under (mx, my, &pt);
		int lk = pg >= 0 ? link_at (pg, pt) : -1;
		if (lk >= 0) { g_drag = 4; g_linkPage = pg; g_linkIdx = lk; return 0; }
		// un clic, un double (un mot), un triple (une ligne)
		if (g_tick - g_lastClickT < 45 && abs (mx - g_lastCX) < 8 && abs (my - g_lastCY) < 8) g_clicks++; else g_clicks = 1;
		if (cl > g_clicks) g_clicks = cl;
		g_lastClickT = g_tick; g_lastCX = mx; g_lastCY = my;
		if (pg < 0) { if (t.sel) { t.sel = false; ui_view_dirty (me); } if (g_handOn) g_drag = 3; return 0; }
		if ((mods & UI_MOD_SHIFT) && t.sel && t.selPage == pg) { t.bx = pt.x; t.by = pt.y; g_drag = 1; ui_view_dirty (me); return 0; }
		if (g_clicks >= 2)
		{
			t.selPage = pg; t.sel = false;
			fz_stext_page *st = text_of (t.doc, pg);
			if (st) { fz_point a = pt, b = pt; fz_snap_selection (g_mu, st, &a, &b, g_clicks == 2 ? FZ_SELECT_WORDS : FZ_SELECT_LINES); t.ax = a.x; t.ay = a.y; t.bx = b.x; t.by = b.y; t.sel = true; }
			g_drag = g_handOn ? 3 : 0;
		}
		else if (g_handOn) { if (t.sel) t.sel = false; g_drag = 3; }
		else { t.selPage = pg; t.ax = t.bx = pt.x; t.ay = t.by = pt.y; t.sel = false; g_drag = 1; }
		ui_view_dirty (me);
		return 0;
	}
	if (kind == 0)
	{
		if (g_drag == 3) { t.sx = g_pressSx - (mx - g_pressX); t.sy = g_pressSy - (my - g_pressY); scroll_by (0, 0); }
		else if (g_drag == 2) bar_to (my);
		else if (g_drag == 5) { int r = W - 4; if (r < 1) r = 1; t.sx = (int) ((long) (mx - 2) * g_contentW / r) - W / 2; scroll_by (0, 0); }
		else if (g_drag == 1)
		{
			const PagePos &p = g_pos[t.selPage];
			fz_point pt = px_to_page (t.doc, t.selPage, g_scale, t.rot, mx - (p.x - t.sx), my - (p.y - t.sy));
			t.bx = pt.x; t.by = pt.y;
			if (abs (mx - g_pressX) > 2 || abs (my - g_pressY) > 2) t.sel = true;
			if (my < 0) scroll_by (0, -20); else if (my > H) scroll_by (0, 20); else ui_view_dirty (me);
		}
		else if (!g_drag)
		{	// le curseur : une main sur un lien, une barre de texte sinon
			fz_point pt; int pg = mx < W ? page_under (mx, my, &pt) : -1;
			static int shape = -1;
			int sh = pg < 0 ? UC_ARROW : link_at (pg, pt) >= 0 || g_handOn ? UC_HAND : UC_TEXT;
			if (sh != shape) { shape = sh; ui_w_cursor (me, sh); }
		}
		return 0;
	}
	// relache
	if (g_drag == 4)
	{
		fz_point pt; int pg = page_under (mx, my, &pt);
		if (pg == g_linkPage && link_at (pg, pt) == g_linkIdx) act (A_LINK, g_linkIdx, g_linkPage);
	}
	g_drag = 0;
	return 0;
}
static long view_wheel (long, long n)
{
	Tab &t = tab (); if (!t.doc) return 0;
	int mods = ui_ev_field (&ui, EV_MODS);
	if (mods & UI_MOD_CTRL) zoom_at (n > 0 ? 1.15f : 1 / 1.15f, g_pressX, g_pressY);
	else if (mods & UI_MOD_SHIFT) scroll_by ((int) -n * 60, 0);
	else scroll_by (0, (int) -n * 60);
	return 0;
}

// ---- le panneau de gauche : pages, sommaire, resultats --------------------------------------------------------------------
enum { SP_TAB = 1, SP_THUMB, SP_OL_TOGGLE, SP_OL, SP_CASE, SP_WORDS, SP_HIT };
static HitList g_sp; static int g_spHot = -1, g_spTop, g_spH, g_spContent;
static int g_sideW = SIDE_W;
static int &sp_scroll () { Tab &t = tab (); return t.side == S_PAGES ? t.thumbScroll : t.side == S_CONTENTS ? t.olScroll : t.findScroll; }
static float thumb_scale (Doc *D)
{
	float mw = 1; for (int i = 0; i < D->npages; i++) { float a = tab ().rot % 180 ? D->ph[i] : D->pw[i]; if (a > mw) mw = a; }
	return 96.0f / mw;
}
static void sp_segs ()
{
	const char *L[3] = { TR ("Pages"), TR ("Contents"), TR ("Find") };
	int x = 10, w = g_sideW - 20, sw = w / 3;
	panel (x, 8, w, 28, 6, C_FIELD, C_LINE);
	for (int k = 0; k < 3; k++)
	{
		bool on = tab ().side == k;
		if (on) rbox (x + 2 + k * sw, 10, sw - 4, 24, 5, C_ACCENT);
		text_c (x + k * sw, 8, sw, 28, L[k], on ? ink_on (C_ACCENT) : C_TEXT, F_SMALL);
		g_sp.add (x + k * sw, 8, sw, 28, SP_TAB, k);
	}
}
static bool ol_visible (Doc *D, int i) { for (int p = D->ol[i].parent; p >= 0; p = D->ol[p].parent) if (!D->ol[p].open) return false; return true; }
static int ol_current (Tab &t, Doc *D)
{	// la section lue : la derniere entree a la page courante ou avant
	int best = -1;
	for (int i = 0; i < D->nol; i++) if (D->ol[i].page >= 0 && D->ol[i].page <= t.cur) best = i;
	while (best >= 0 && !ol_visible (D, best)) best = D->ol[best].parent;
	return best;
}
static void sp_pages (Tab &t, Doc *D, int H)
{
	g_spTop = 44; g_spH = H - g_spTop;
	float s = thumb_scale (D);
	int y = g_spTop + 6 - t.thumbScroll, total = 6;
	for (int i = 0; i < D->npages; i++)
	{
		int w, h; page_px (D, i, s, t.rot, &w, &h);
		int x = (g_sideW - w) / 2, ih = h + 30;
		if (y + ih > g_spTop && y < H)
		{
			if (i == t.cur) panel (x - 6, y - 6, w + 12, h + 12, 6, mix (C_SIDE, C_ACCENT, 70), C_ACCENT);
			veil (x + 1, y + 2, w, h, 0, 50);
			ui_fill (x, y, w, h, 0xFFFFFF);
			bool exact; Bmp *b = bmp_find (D->id, i, s, t.rot, 0, 0, w, h, &exact, true);
			if (b) blit_page (*b, b->skey / 4096.0f, s, x, y, 0, g_spTop, g_sideW, g_spH);
			if (!exact && g_nwantSide < 24) g_wantSide[g_nwantSide++] = page_job (t, i, s, 0, 0, w, h, J_THUMB);
			char n[16]; snprintf (n, sizeof n, "%d", i + 1);
			text_c (0, y + h + 4, g_sideW, 18, n, i == t.cur ? C_ACCENT : C_DIM, F_SMALL, i == t.cur ? 2 : 0);
			g_sp.add (0, y - 4, g_sideW, ih, SP_THUMB, i);
		}
		y += ih; total += ih;
	}
	g_spContent = total;
	ui_fill (0, 0, g_sideW - 1, g_spTop, C_SIDE);		// (les onglets par-dessus la liste)
	send_wants ();
}
static void sp_contents (Tab &t, Doc *D, int H)
{
	g_spTop = 44; g_spH = H - g_spTop;
	if (!D->nol) { text_c (0, g_spTop + 20, g_sideW, 22, TR ("This document has no contents."), C_DIM, F_SMALL); g_spContent = 0; return; }
	int cur = ol_current (t, D);
	int y = g_spTop + 4 - t.olScroll, total = 8;
	for (int i = 0; i < D->nol; i++)
	{
		if (!ol_visible (D, i)) continue;
		OutlineItem &o = D->ol[i];
		if (y + 28 > g_spTop && y < H)
		{
			int ind = 24 + o.depth * 14;
			if (ind > g_sideW - 80) ind = g_sideW - 80;
			bool on = i == cur;
			if (on) rbox (ind - 4, y + 1, g_sideW - ind - 6, 26, 5, C_ACCENT);
			else if (g_sp.n == g_spHot) rbox (ind - 4, y + 1, g_sideW - ind - 6, 26, 5, mix (C_SIDE, C_ACCENT, 40));
			unsigned ink = on ? ink_on (C_ACCENT) : C_TEXT;
			if (o.kids)
			{
				icon (o.open ? I_DOWN : I_RIGHT, ind - 19, y + 7, 14, C_DIM, C_SIDE);
				g_sp.add (ind - 24, y, 22, 28, SP_OL_TOGGLE, i);
			}
			char pg[16] = ""; if (o.page >= 0) snprintf (pg, sizeof pg, "%d", o.page + 1);
			int pw = tw (pg, F_SMALL) + 18;
			text_v (ind + 2, y, 28, o.title, ink, F_UI, on ? 2 : 0, g_sideW - ind - 2 - pw - 6);
			text_r (g_sideW - 16, y, 28, pg, on ? ink_on (C_ACCENT) : C_DIM, F_SMALL);
			g_sp.add (ind - 4, y, g_sideW - ind - 6, 28, SP_OL, i);
		}
		y += 28; total += 28;
	}
	g_spContent = total;
	ui_fill (0, 0, g_sideW - 1, g_spTop, C_SIDE);
}
static void sp_check (int x, int y, const char *label, bool on, int kind)
{
	panel (x, y, 18, 18, 4, on ? C_ACCENT : C_FIELD, on ? C_ACCENT : jet (J_LIGNE_FORTE));
	if (on) icon (I_CHECK, x + 2, y + 2, 14, ink_on (C_ACCENT), C_ACCENT);
	text_v (x + 26, y - 2, 22, label, C_TEXT);
	g_sp.add (x, y - 4, tw (label) + 32, 26, kind);
}
static const char *ci_find (const char *s, const char *n)
{
	int ln = (int) strlen (n); if (!ln) return 0;
	for (; *s; s++) if (!strncasecmp (s, n, ln)) return s;
	return 0;
}
// une ligne de resultat, le mot en gras
static void snippet (const char *s, const char *needle, int x, int y, int w, bool on)
{
	const char *p = ci_find (s, needle);
	unsigned dim = on ? ink_on (C_ACCENT) : C_DIM, ink = on ? ink_on (C_ACCENT) : C_TEXT;
	int n = (int) strlen (needle), xe = x + w;
	char a[200];
	if (!p) { text_v (x, y, 22, s, dim, F_SMALL, 0, w); return; }
	int la = (int) (p - s); if (la > 190) la = 190;
	memcpy (a, s, la); a[la] = 0;
	text_v (x, y, 22, a, dim, F_SMALL); x += tw (a, F_SMALL);
	int lb = n < 190 ? n : 190; memcpy (a, p, lb); a[lb] = 0;
	if (x < xe) { text_v (x, y, 22, a, ink, F_SMALL, 2, xe - x); x += tw (a, F_SMALL, 2); }
	if (x < xe) text_v (x, y, 22, p + n, dim, F_SMALL, 0, xe - x);
}
static void sp_find (Tab &t, int H)
{
	char sum[96] = "";
	int pages = 0, last = -1; for (int k = 0; k < t.nhits; k++) if (t.hits[k].page != last) { pages++; last = t.hits[k].page; }
	if (t.needle[0])
	{
		if (!t.done) snprintf (sum, sizeof sum, t.nhits == 1 ? TR ("Searching... %d result") : TR ("Searching... %d results"), t.nhits);
		else if (!t.nhits) snprintf (sum, sizeof sum, "%s", TR ("No results"));
		else snprintf (sum, sizeof sum, t.nhits == 1 ? TR ("%d result on %d page") : pages == 1 ? TR ("%d results on %d page") : TR ("%d results on %d pages"), t.nhits, pages);
	}
	g_spTop = 164; g_spH = H - g_spTop;
	int y = g_spTop - t.findScroll, total = 0;
	for (int k = 0; k < t.nhits; k++)
	{
		SearchHit &h = t.hits[k];
		if (k == 0 || t.hits[k - 1].page != h.page)
		{
			int n = 0; for (int j = k; j < t.nhits && t.hits[j].page == h.page; j++) n++;
			if (y + 22 > g_spTop && y < H)
			{
				char a[32], b[16]; snprintf (a, sizeof a, TR ("Page %d"), h.page + 1); snprintf (b, sizeof b, "%d", n);
				text_v (12, y, 22, a, C_TEXT, F_SMALL, 2); text_r (g_sideW - 16, y, 22, b, C_DIM, F_SMALL);
			}
			y += 22; total += 22;
		}
		if (y + 24 > g_spTop && y < H)
		{
			bool on = k == t.hit;
			if (on) rbox (6, y, g_sideW - 14, 24, 5, C_ACCENT);
			else if (g_sp.n == g_spHot) rbox (6, y, g_sideW - 14, 24, 5, mix (C_SIDE, C_ACCENT, 40));
			snippet (h.ctx ? h.ctx : "", t.needle, 14, y + 1, g_sideW - 24, on);
			g_sp.add (6, y, g_sideW - 14, 24, SP_HIT, k);
		}
		y += 24; total += 24;
		if (k + 1 < t.nhits && t.hits[k + 1].page != h.page) { y += 4; total += 4; }
	}
	g_spContent = total + 8;
	ui_fill (0, 0, g_sideW - 1, g_spTop, C_SIDE);
	sp_check (12, 88, TR ("Match case"), (t.opts & 1) != 0, SP_CASE);
	sp_check (12, 114, TR ("Whole words"), (t.opts & 2) != 0, SP_WORDS);
	text (12, 142, sum, C_DIM, F_SMALL, 2, g_sideW - 24);
}
static long side_paint (long me, long, long)
{
	begin_paint (me);
	Tab &t = tab (); Doc *D = t.doc;
	int H = (int) g_ph;
	ui_fill (0, 0, g_pw, g_ph, C_SIDE);
	ui_fill (g_sideW - 1, 0, 1, H, C_LINE);
	g_sp.clear (); g_nwantSide = 0;
	if (!D) return 0;
	if (t.side == S_PAGES) sp_pages (t, D, H);
	else if (t.side == S_CONTENTS) sp_contents (t, D, H);
	else sp_find (t, H);
	sp_segs ();
	if (g_spContent > g_spH)
	{
		int ty, th; thumb_of (g_spContent, g_spH, sp_scroll (), g_spH - 4, &ty, &th);
		rbox (g_sideW - 9, g_spTop + 2 + ty, 6, th, 3, C_FAINT);
	}
	return 0;
}
static void sp_clamp () { int m = g_spContent - g_spH; if (m < 0) m = 0; if (sp_scroll () > m) sp_scroll () = m; if (sp_scroll () < 0) sp_scroll () = 0; }
static void sp_click (const Hit *ht)
{
	Tab &t = tab (); Doc *D = t.doc;
	switch (ht->kind)
	{
	case SP_TAB: t.side = ht->a; if (t.side == S_FIND) ui_w_focus (g_sideBox); refresh_all (); break;
	case SP_THUMB: show_page (t, ht->a); break;
	case SP_OL_TOGGLE: D->ol[ht->a].open = !D->ol[ht->a].open; ui_view_dirty (g_vSide); break;
	case SP_OL: if (D->ol[ht->a].page >= 0) show_page (t, D->ol[ht->a].page, D->ol[ht->a].y); if (D->ol[ht->a].kids && !D->ol[ht->a].open) D->ol[ht->a].open = true; refresh_all (); break;
	case SP_CASE: t.opts ^= 1; { char n[200]; scopy (n, t.needle, sizeof n); t.needle[0] = 0; search_start (t, n); } break;
	case SP_WORDS: t.opts ^= 2; { char n[200]; scopy (n, t.needle, sizeof n); t.needle[0] = 0; search_start (t, n); } break;
	case SP_HIT: goto_hit (t, ht->a); break;
	}
}
// la liste glisse au doigt ; un appui sans glisser choisit
static long side_mouse (long me, long kind, long x, long y, long)
{
	static int py, ps; static bool moved, down; static int hitKind, hitA;
	if (!tab ().doc) return 0;
	const Hit *ht = kind == 3 ? 0 : g_sp.at ((int) x, (int) y);
	int nh = ht ? (int) (ht - g_sp.h) : -1;
	if (nh != g_spHot) { g_spHot = nh; ui_view_dirty (me); }
	if (kind == 1) { py = (int) y; ps = sp_scroll (); moved = false; down = true; hitKind = ht ? ht->kind : 0; hitA = ht ? ht->a : 0; }
	else if (kind == 0 && down)
	{
		if (abs ((int) y - py) > 8) moved = true;
		if (moved && y >= g_spTop - 200) { sp_scroll () = ps - ((int) y - py); sp_clamp (); ui_view_dirty (me); }
	}
	else if (kind == 2 && down)
	{
		down = false;
		if (!moved && hitKind) { Hit h = { 0, 0, 0, 0, hitKind, hitA }; sp_click (&h); }
	}
	return 0;
}
static long side_wheel (long me, long n) { sp_scroll () -= (int) n * 48; sp_clamp (); ui_view_dirty (me); return 0; }
static void side_follow ()
{	// la page courante reste en vue dans la liste
	Tab &t = tab (); if (!t.doc || t.side != S_PAGES) return;
	float s = thumb_scale (t.doc); int y = 6;
	for (int i = 0; i < t.cur; i++) { int w, h; page_px (t.doc, i, s, t.rot, &w, &h); y += h + 30; }
	int w, h; page_px (t.doc, t.cur, s, t.rot, &w, &h);
	int lh = g_spH > 0 ? g_spH : 300;
	if (y < t.thumbScroll) t.thumbScroll = y - 8; else if (y + h + 30 > t.thumbScroll + lh) t.thumbScroll = y + h + 30 - lh;
	if (t.thumbScroll < 0) t.thumbScroll = 0;
}

// ---- la barre d'outils ----------------------------------------------------------------------------------------------------
enum { TB_SIDE = 1, TB_OPEN, TB_PREV, TB_NEXT, TB_ZOUT, TB_ZOOM, TB_ZIN, TB_SINGLE, TB_SCROLL, TB_TWO, TB_ROT, TB_FULL, TB_HAND, TB_MORE };
static HitList g_tbHits; static int g_tbHot = -1, g_tbW;
static unsigned g_tipT; static bool g_tipOn;
static bool g_tbNarrow;
static int tb_btn (int x, int ic, int kind, bool on = false, bool dis = false)
{
	bool h = g_tbHits.n == g_tbHot && !dis;
	if (on) panel (x, 7, 32, 32, 6, mix (C_BG, C_ACCENT, 60), C_ACCENT);
	else if (h) panel (x, 7, 32, 32, 6, jet (J_ELEVE), C_LINE);
	icon (ic, x + 7, 14, 18, dis ? C_FAINT : C_TEXT, on ? mix (C_BG, C_ACCENT, 60) : h ? jet (J_ELEVE) : C_BG);
	if (!dis) g_tbHits.add (x, 7, 32, 32, kind);
	return x + 34;
}
static int tb_sep (int x) { ui_fill (x + 4, 10, 1, 26, C_LINE); return x + 10; }
static void zoom_label (Tab &t, char *z, int cap)
{
	if (t.zoomMode == Z_WIDTH) scopy (z, TR ("Fit width"), cap); else if (t.zoomMode == Z_PAGE) scopy (z, TR ("Fit page"), cap);
	else snprintf (z, cap, "%d %%", (int) (t.zoom * 72.0f / 96.0f * 100 + 0.5f));
}
static long tb_paint (long me, long, long)
{
	begin_paint (me);
	Tab &t = tab (); Doc *D = t.doc;
	int width = (int) g_pw, height = (int) g_ph;
	ui_fill (0, 0, width, height, C_BG);
	ui_fill (0, height - 1, width, 1, C_LINE);
	g_tbHits.clear ();
	int x = 8;
	x = tb_btn (x, I_SIDEBAR, TB_SIDE, g_sideOn != 0);
	x = tb_btn (x, I_OPEN, TB_OPEN);
	x = tb_sep (x);
	x = tb_btn (x, I_UP, TB_PREV, false, !D || t.cur <= 0);
	x = tb_btn (x, I_DOWN, TB_NEXT, false, !D || t.cur + 1 >= D->npages);
	x += 54;						// (le champ de la page, g_pageBox)
	char n[24]; snprintf (n, sizeof n, "/ %d", D ? D->npages : 0);
	text_v (x + 4, 7, 32, n, C_DIM); x += tw (n) + 14;
	x = tb_sep (x);
	x = tb_btn (x, I_MINUS, TB_ZOUT);
	{
		char z[32]; zoom_label (t, z, sizeof z);
		bool h = g_tbHits.n == g_tbHot; int zw = g_tbNarrow ? 84 : 132;
		panel (x + 2, 8, zw, 30, 5, h ? jet (J_ELEVE) : jet (J_ELEVE_BAS), C_LINE);
		text_v (x + 10, 8, 30, z, C_TEXT, g_tbNarrow ? F_SMALL : F_UI, 0, zw - 28);
		icon (I_DOWN, x + zw - 16, 16, 14, C_TEXT, C_BG);
		g_tbHits.add (x + 2, 8, zw, 30, TB_ZOOM);
		x += zw + 6;
	}
	x = tb_btn (x, I_PLUS, TB_ZIN);
	x = tb_sep (x);
	if (!g_tbNarrow)
	{
		x = tb_btn (x, I_SINGLE, TB_SINGLE, t.layout == L_SINGLE);
		x = tb_btn (x, I_SCROLL, TB_SCROLL, t.layout == L_SCROLL);
		x = tb_btn (x, I_TWO, TB_TWO, t.layout == L_TWO);
	}
	x = tb_btn (x, I_ROTATE, TB_ROT);
	x = tb_btn (x, I_FULL, TB_FULL);
	x = tb_btn (x, I_HAND, TB_HAND, g_handOn != 0);
	g_tbW = x;
	tb_btn (width - 42, I_MORE, TB_MORE);
	return 0;
}
static void set_layout (int l);
static void zoom_step (int dir);
static void rotate ();
static void toggle_side ();
static void tb_click (int kind)
{
	Tab &t = tab ();
	switch (kind)
	{
	case TB_SIDE: toggle_side (); break;
	case TB_OPEN: act (A_OPEN); break;
	case TB_PREV: if (t.doc && t.cur > 0) show_page (t, t.cur - 1); break;
	case TB_NEXT: if (t.doc && t.cur + 1 < t.doc->npages) show_page (t, t.cur + 1); break;
	case TB_ZOUT: zoom_step (-1); break;
	case TB_ZIN: zoom_step (1); break;
	case TB_ZOOM: act (A_ZOOM_MENU); break;
	case TB_SINGLE: set_layout (L_SINGLE); break;
	case TB_SCROLL: set_layout (L_SCROLL); break;
	case TB_TWO: set_layout (L_TWO); break;
	case TB_ROT: rotate (); break;
	case TB_FULL: act (A_FULL); break;
	case TB_HAND: g_handOn = !g_handOn; save_settings (); ui_view_dirty (g_vTb); break;
	case TB_MORE: act (A_MORE_MENU); break;
	}
}
static const char *tb_tip (int kind)
{
	switch (kind)
	{
	case TB_SIDE: return TR ("Side Panel");
	case TB_OPEN: return TR ("Open...");
	case TB_PREV: return TR ("Previous Page");
	case TB_NEXT: return TR ("Next Page");
	case TB_ZOUT: return TR ("Zoom out");
	case TB_ZIN: return TR ("Zoom in");
	case TB_SINGLE: return TR ("One Page at a Time");
	case TB_SCROLL: return TR ("Continuous");
	case TB_TWO: return TR ("Two Pages");
	case TB_ROT: return TR ("Rotate");
	case TB_FULL: return TR ("Full Screen");
	case TB_HAND: return TR ("Drag the pages (or select the text)");
	}
	return 0;
}
static long tb_mouse (long me, long kind, long x, long y, long)
{
	const Hit *ht = kind == 3 ? 0 : g_tbHits.at ((int) x, (int) y);
	int nh = ht ? (int) (ht - g_tbHits.h) : -1;
	if (nh != g_tbHot)
	{
		g_tbHot = nh; ui_view_dirty (me); g_tipT = nh >= 0 ? g_tick : 0;
		if (g_tipOn) { ui_tip_at (0, 0, 0); g_tipOn = false; }
	}
	if (kind == 1 && g_tipOn) { ui_tip_at (0, 0, 0); g_tipOn = false; }
	if (kind == 1) g_tipT = 0;
	if (kind == 2 && ht) tb_click (ht->kind);
	return 0;
}

// ---- les documents recents ------------------------------------------------------------------------------------------------
struct Recent { char path[300]; int page, npages; long long when; unsigned *px; int w, h; bool asked; };
enum { MAX_RECENT = 20 };
static Recent g_rec[MAX_RECENT]; static int g_nrec;
static long long now_stamp ()
{
	time_t t = time (0); struct tm m; localtime_r (&t, &m);
	return ((((long long) (m.tm_year + 1900) * 100 + m.tm_mon + 1) * 100 + m.tm_mday) * 100 + m.tm_hour) * 100 + m.tm_min;
}
static void recent_load ()
{
	char *buf = file_read (RECENT, 0);
	if (!buf) return;
	for (char *line = buf, *nx; line && *line && g_nrec < MAX_RECENT; line = nx)
	{
		nx = strchr (line, '\n'); if (nx) *nx++ = 0;
		if (line[0] == '#' || !line[0]) continue;
		char *f[4] = { line, 0, 0, 0 }; int k = 1;
		for (char *p = line; *p && k < 4; p++) if (*p == '\t') { *p = 0; f[k++] = p + 1; }
		if (!f[1]) continue;
		Recent &r = g_rec[g_nrec++]; memset (&r, 0, sizeof r);
		scopy (r.path, f[0], sizeof r.path); r.page = atoi (f[1]); r.npages = f[2] ? atoi (f[2]) : 0; r.when = f[3] ? atoll (f[3]) : 0;
	}
	free (buf);
}
static void recent_save ()
{
	mkdir (CONF_DIR, 0777);
	int cap = 200 + g_nrec * 360; char *b = (char *) malloc (cap); if (!b) return;
	int o = snprintf (b, cap, "# path\tpage\tpages\twhen (YYYYMMDDhhmm) -- the PDF Viewer's recent documents\n");
	for (int i = 0; i < g_nrec; i++) o += snprintf (b + o, cap - o, "%s\t%d\t%d\t%lld\n", g_rec[i].path, g_rec[i].page, g_rec[i].npages, g_rec[i].when);
	file_write (RECENT, b, o); free (b);
}
static int recent_find (const char *p) { for (int i = 0; i < g_nrec; i++) if (!strcmp (g_rec[i].path, p)) return i; return -1; }
static void recent_note (Tab &t)
{
	if (!t.doc) return;
	int k = recent_find (t.doc->path);
	Recent r; memset (&r, 0, sizeof r);
	if (k >= 0) { r = g_rec[k]; memmove (g_rec + k, g_rec + k + 1, sizeof (Recent) * (g_nrec - k - 1)); g_nrec--; }
	else if (g_nrec == MAX_RECENT) { free (g_rec[g_nrec - 1].px); g_nrec--; }
	scopy (r.path, t.doc->path, sizeof r.path); r.page = t.cur; r.npages = t.doc->npages; r.when = now_stamp ();
	memmove (g_rec + 1, g_rec, sizeof (Recent) * g_nrec); g_rec[0] = r; g_nrec++;
	recent_save ();
}
static void recent_remove (int k)
{
	if (k < 0 || k >= g_nrec) return;
	free (g_rec[k].px);
	memmove (g_rec + k, g_rec + k + 1, sizeof (Recent) * (g_nrec - k - 1)); g_nrec--;
	recent_save ();
}
static long days_of (long long ymd)
{
	int y = (int) (ymd / 10000), m = (int) (ymd / 100 % 100), d = (int) (ymd % 100);
	if (m < 3) { y--; m += 12; }
	return 365L * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + d;
}
static void fmt_when (long long w, char *b, int cap)
{
	const char *MO[12] = { TR ("Jan"), TR ("Feb"), TR ("Mar"), TR ("Apr"), TR ("May"), TR ("Jun"), TR ("Jul"), TR ("Aug"), TR ("Sep"), TR ("Oct"), TR ("Nov"), TR ("Dec") };
	if (!w) { b[0] = 0; return; }
	long long nw = now_stamp ();
	int mi = (int) (w % 100), h = (int) (w / 100 % 100), d = (int) (w / 10000 % 100), mo = (int) (w / 1000000 % 100);
	long dw = days_of (w / 10000), dn = days_of (nw / 10000);
	if (dw == dn) snprintf (b, cap, TR ("Today, %02d:%02d"), h, mi);
	else if (dn - dw == 1) snprintf (b, cap, "%s", TR ("Yesterday"));
	else snprintf (b, cap, "%d %s", d, MO[(mo + 11) % 12]);
}

// les dossiers de l'accueil : ceux qui existent, leurs PDF comptes
struct Folder { const char *name, *path; int count; bool there; };
static Folder g_folders[] = { { "Documents", "SD0:/home/documents", 0, false }, { "Downloads", "SD0:/home/downloads", 0, false },
			      { "Manuals", "SD0:/home/manuals", 0, false }, { "Second partition", "SD1:/", 0, false } };
enum { NFOLDERS = sizeof g_folders / sizeof g_folders[0] };
static int count_pdfs (const char *dir, int depth)
{
	DIR *d = opendir (dir);
	if (!d) return -1;
	int n = 0, seen = 0; struct dirent *e;
	while ((e = readdir (d)) && seen++ < 2000)
	{
		if (e->d_name[0] == '.') continue;
		int l = (int) strlen (e->d_name);
		if (l > 4 && !strcasecmp (e->d_name + l - 4, ".pdf")) { n++; continue; }
		if (depth < 3)
		{
			char p[400]; int dl = (int) strlen (dir); snprintf (p, sizeof p, "%s%s%s", dir, dl && dir[dl - 1] == '/' ? "" : "/", e->d_name);
			struct stat st;
			if (stat (p, &st) == 0 && S_ISDIR (st.st_mode)) { int k = count_pdfs (p, depth + 1); if (k > 0) n += k; }
		}
	}
	closedir (d);
	return n;
}
static void folders_count () { for (int i = 0; i < NFOLDERS; i++) { int n = count_pdfs (g_folders[i].path, 0); g_folders[i].there = n >= 0; g_folders[i].count = n > 0 ? n : 0; } }

// ---- l'accueil ----------------------------------------------------------------------------------------------------------------
enum { HM_OPEN = 1, HM_RECENT, HM_RMORE, HM_FOLDER };
static HitList g_hm; static int g_hmHot = -1, g_hmY;
static long home_paint (long me, long, long)
{
	begin_paint (me);
	int width = (int) g_pw, height = (int) g_ph;
	ui_fill (0, 0, width, height, C_FIELD);
	g_hm.clear (); g_nwantHome = 0;
	bool narrow = width < 640;
	int ax = narrow ? 16 : 40, ay = 20 - g_hmY;
	text (ax, ay, TR ("Recent documents"), C_TEXT, narrow ? F_H2 : F_H1, 2);
	{
		int bw = 170, bx = width - ax - bw; bool h = g_hm.n == g_hmHot;
		if (narrow) { bx = ax; ay += 34; }
		rbox (bx, ay, bw, 36, 6, h ? jet (J_ACCENT_VIF) : C_ACCENT);
		icon (I_OPEN, bx + 12, ay + 9, 18, 0, 0);
		text_v (bx + 40, ay, 36, TR ("Open a file..."), ink_on (C_ACCENT), F_UI, 2);
		g_hm.add (bx, ay, bw, 36, HM_OPEN);
	}
	int S = narrow ? 112 : 148, G = narrow ? 16 : 30, th = (int) (S * 1.414f);
	int cols = (width - 2 * ax + G) / (S + G); if (cols < 1) cols = 1;
	int y = ay + 58, shown = 0;
	if (!g_nrec) text_v (ax, y, 30, TR ("The documents you open will be here."), C_DIM, F_MID);
	int rows = height > th * 2 + 300 ? 2 : 1, maxShow = cols * rows;
	for (int k = 0; k < g_nrec && shown < maxShow; k++)
	{
		Recent &r = g_rec[k];
		int x = ax + (shown % cols) * (S + G), ry = y + (shown / cols) * (th + 96);
		int ph = r.px ? r.h * S / (r.w ? r.w : 1) : th; if (ph > th) ph = th;
		veil (x + 2, ry + 3, S, ph, 0, 50);
		ui_fill (x, ry, S, ph, 0xFFFFFF);
		if (r.px)
		{
			Bmp b; memset (&b, 0, sizeof b); b.px = r.px; b.cw = r.w; b.ch = r.h;
			blit_page (b, 1.0f, (float) S / r.w, x, ry, x, ry, S, ph);
		}
		else if (!r.asked && g_nwantHome < 12)
		{
			Job j; memset (&j, 0, sizeof j); j.kind = J_RECENT; j.cw = S; j.page = k; scopy (j.path, r.path, sizeof j.path);
			g_wantHome[g_nwantHome++] = j;
		}
		ui_frame (x, ry, S, ph, C_LINE);
		if (r.npages > 0) ui_fill (x, ry + ph - 4, S * (r.page + 1) / r.npages, 4, C_ACCENT);
		bool h = g_hm.n == g_hmHot || g_hm.n + 1 == g_hmHot;
		if (h) { veil (x, ry, S, ph, C_ACCENT, 40); ui_frame (x - 1, ry - 1, S + 2, ph + 2, C_ACCENT); }
		g_hm.add (x, ry, S, ph + 60, HM_RECENT, k);
		rbox (x + S - 30, ry + 6, 24, 24, 12, 0xF4F2F0);
		icon (I_MORE, x + S - 28, ry + 8, 20, 0x3A3634, 0xF4F2F0);
		g_hm.add (x + S - 34, ry + 2, 32, 32, HM_RMORE, k);
		const char *nm = r.path; for (const char *p = r.path; *p; p++) if (*p == '/' || *p == ':') nm = p + 1;
		text (x, ry + ph + 10, nm, C_TEXT, F_UI, 2, S);
		char b[64]; snprintf (b, sizeof b, r.npages ? TR ("Page %d of %d") : TR ("Page %d"), r.page + 1, r.npages);
		text (x, ry + ph + 28, b, C_DIM, F_SMALL);
		fmt_when (r.when, b, sizeof b); text (x, ry + ph + 44, b, C_FAINT, F_SMALL);
		shown++;
	}
	send_wants ();
	int usedRows = shown ? (shown + cols - 1) / cols : 0;
	int fy = y + (usedRows ? usedRows * (th + 96) : 50);
	if (fy + 90 < height)
	{
		text (ax, fy, TR ("Folders"), C_TEXT, F_H2, 2);
		int fx = ax, cw = narrow ? width - 2 * ax : 270;
		for (int i = 0; i < NFOLDERS; i++)
		{
			if (!g_folders[i].there) continue;
			if (fx + cw > width - 10) { fx = ax; fy += 64; if (fy + 90 > height) break; }
			bool h = g_hm.n == g_hmHot;
			panel (fx, fy + 34, cw, 56, 8, h ? mix (C_FIELD, C_ACCENT, 30) : jet (J_ELEVE), C_LINE);
			icon (I_FOLDER, fx + 14, fy + 48, 28, 0, 0);
			text (fx + 56, fy + 45, TR (g_folders[i].name), C_TEXT, F_UI, 2);
			char b[96]; snprintf (b, sizeof b, g_folders[i].count == 1 ? TR ("%s  \xC2\xB7  %d document") : TR ("%s  \xC2\xB7  %d documents"), g_folders[i].path, g_folders[i].count);
			text (fx + 56, fy + 65, b, C_DIM, F_SMALL, 0, cw - 66);
			g_hm.add (fx, fy + 34, cw, 56, HM_FOLDER, i);
			fx += cw + 20;
		}
	}
	return 0;
}
static long home_mouse (long me, long kind, long x, long y, long info)
{
	const Hit *ht = kind == 3 ? 0 : g_hm.at ((int) x, (int) y);
	int nh = ht ? (int) (ht - g_hm.h) : -1;
	if (nh != g_hmHot) { g_hmHot = nh; ui_view_dirty (me); }
	static int btn;
	if (kind == 1) btn = (int) (info >> 8 & 255);
	if (kind == 1 && (btn & 2) && ht && (ht->kind == HM_RECENT || ht->kind == HM_RMORE)) act (A_RECENT_MENU, ht->a, (int) x, (int) y + TABS_H);
	if (kind == 2 && (btn & 1) && ht)
	{
		if (ht->kind == HM_OPEN) act (A_OPEN);
		else if (ht->kind == HM_RMORE) act (A_RECENT_MENU, ht->a, (int) x, (int) y + TABS_H);
		else if (ht->kind == HM_RECENT) act (A_RECENT, ht->a);
		else if (ht->kind == HM_FOLDER) act (A_FOLDER, ht->a);
	}
	return 0;
}

// ---- les dialogues ---------------------------------------------------------------------------------------------------------
static long g_askBox;
static long ask_event (long d, long w, long code) { if (w == g_askBox && code == UN_ACTIVATE) ui_dialog_close (d, 1); return 0; }
// le mot de passe : tampon rempli, 1 ; 0 : abandon
static int ask_password (const char *msg, const char *err, char *out, int cap)
{
	int W = 440, H = 170;
	long d = ui_dialog_begin (W, H, (long) TR ("Password"));
	if (!d) return 0;
	ui_label (16, 14, W - 32, 22, (long) msg, 0);
	g_askBox = ui_textbox (16, 44, W - 32, 30, UTF_PASSWORD);
	ui_w_set_range (g_askBox, 0, 120, 0, 0);
	if (err && err[0]) ui_label (16, 80, W - 32, 22, (long) err, ULB_BOLD);
	long r = ui_dialog_button (d, 0, (long) TR ("Open"), 1, UB_DEFAULT, W - 16);
	ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, r);
	ui_dialog_cancel (d, 0);
	ui_w_focus (g_askBox);
	long a = ui_dialog_run (d, (long) ask_event, 0);
	if (a) scopy (out, (const char *) ui_w_get_text (g_askBox), cap);
	ui_dialog_end (d);
	return a == 1;
}
static void msg (const char *m) { ui_message ((long) TR ("PDF Viewer"), (long) m, UM_OK); }

static void pdf_date (const char *s, char *out, int cap)
{	// "D:20260929223430Z" -> "29 septembre 2026, 22:34"
	const char *MO[12] = { TR ("January"), TR ("February"), TR ("March"), TR ("April"), TR ("May"), TR ("June"), TR ("July"), TR ("August"), TR ("September"), TR ("October"), TR ("November"), TR ("December") };
	out[0] = 0;
	if (!strncmp (s, "D:", 2)) s += 2;
	int n = (int) strlen (s);
	if (n < 8) { scopy (out, s, cap); return; }
	int y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0'), mo = (s[4] - '0') * 10 + (s[5] - '0'), d = (s[6] - '0') * 10 + (s[7] - '0');
	if (mo < 1 || mo > 12) { scopy (out, s, cap); return; }
	if (n >= 12) snprintf (out, cap, "%d %s %d, %c%c:%c%c", d, MO[mo - 1], y, s[8], s[9], s[10], s[11]);
	else snprintf (out, cap, "%d %s %d", d, MO[mo - 1], y);
}
#define DASH "\xE2\x80\x94"
static void kv (long list, const char *k, const char *v)
{
	char l[420]; snprintf (l, sizeof l, "%s\t%s", k, v && v[0] ? v : DASH);
	for (char *p = l + strlen (k) + 1; *p; p++) if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
	ui_list_add (list, (long) l, 0);
}
// Proprietes : Description, Polices, Securite
static void properties ()
{
	Tab &t = tab (); Doc *D = t.doc; if (!D) return;
	int W = 540, H = 430;
	long scr[4] = { 1024, 768, 0, 0 }; ui_screen ((long) scr);
	if (H > scr[1] - 80) H = (int) scr[1] - 80;
	if (W > scr[0] - 20) W = (int) scr[0] - 20;
	long d = ui_dialog_begin (W, H, (long) TR ("Document Properties"));
	if (!d) return;
	char s[200]; snprintf (s, sizeof s, "%s|%s|%s", TR ("Description"), TR ("Fonts"), TR ("Security"));
	long tabs = ui_tabs (12, 10, W - 24, H - 66, (long) s);
	long lists[3];
	for (int k = 0; k < 3; k++)
	{
		long pg = ui_tabs_page (tabs, k);
		long l = ui_list (24, 52, W - 48, H - 124, 0);
		ui_list_column (l, (long) "", k == 1 ? 260 : 150, 0); ui_list_column (l, (long) "", k == 1 ? W - 330 : W - 220, 0);
		ui_w_set_parent (l, pg);
		lists[k] = l;
	}
	char b[220], c[220];
	long L = lists[0];
	char where[300]; scopy (where, D->path, sizeof where);
	{ char *e = where; for (char *p = where; *p; p++) if (*p == '/') e = p; if (e != where) *e = 0; }
	kv (L, TR ("File"), D->name); kv (L, TR ("Location"), where);
	long long sz = file_size (D->path); if (sz < 0) sz = 0;
	if (sz >= 1048576) snprintf (b, sizeof b, TR ("%.1f MB (%lld bytes)"), sz / 1048576.0, sz); else snprintf (b, sizeof b, TR ("%.0f KB (%lld bytes)"), sz / 1024.0, sz);
	kv (L, TR ("Size"), b);
	doc_meta (g_mu, D, FZ_META_INFO_TITLE, b, sizeof b); kv (L, TR ("Title"), b);
	doc_meta (g_mu, D, FZ_META_INFO_AUTHOR, b, sizeof b); kv (L, TR ("Author"), b);
	doc_meta (g_mu, D, FZ_META_INFO_SUBJECT, b, sizeof b); kv (L, TR ("Subject"), b);
	doc_meta (g_mu, D, FZ_META_INFO_KEYWORDS, b, sizeof b); kv (L, TR ("Keywords"), b);
	doc_meta (g_mu, D, FZ_META_INFO_CREATIONDATE, b, sizeof b); pdf_date (b, c, sizeof c); kv (L, TR ("Created"), c);
	doc_meta (g_mu, D, FZ_META_INFO_MODIFICATIONDATE, b, sizeof b); pdf_date (b, c, sizeof c); kv (L, TR ("Modified"), c);
	doc_meta (g_mu, D, FZ_META_INFO_CREATOR, b, sizeof b); kv (L, TR ("Application"), b);
	doc_meta (g_mu, D, FZ_META_INFO_PRODUCER, b, sizeof b); kv (L, TR ("PDF producer"), b);
	doc_meta (g_mu, D, FZ_META_FORMAT, b, sizeof b); kv (L, TR ("PDF version"), strncmp (b, "PDF ", 4) ? b : b + 4);
	{
		float w = D->pw[0] / 72 * 25.4f, h = D->ph[0] / 72 * 25.4f;
		const char *nm = "";
		if (fabsf (w - 210) < 2 && fabsf (h - 297) < 2) nm = "A4, "; else if (fabsf (w - 216) < 2 && fabsf (h - 279) < 2) nm = "@@";
		else if (fabsf (w - 148) < 2 && fabsf (h - 210) < 2) nm = "A5, "; else if (fabsf (w - 297) < 2 && fabsf (h - 420) < 2) nm = "A3, ";
		char lt[32]; if (nm[0] == '@') { snprintf (lt, sizeof lt, "%s, ", TR ("Letter")); nm = lt; }
		snprintf (b, sizeof b, TR ("%d  \xC2\xB7  %s%.0f \xC3\x97 %.0f mm"), D->npages, nm, w, h); kv (L, TR ("Pages"), b);
	}
	static FontInfo f[64];
	int nf = doc_fonts (g_mu, D, f, 64);
	if (!nf) kv (lists[1], TR ("No fonts (or not a PDF)."), "");
	for (int i = 0; i < nf; i++)
	{
		snprintf (b, sizeof b, "%s  \xC2\xB7  %s", f[i].type, f[i].embedded ? (f[i].subset ? TR ("Embedded subset") : TR ("Embedded")) : TR ("Not embedded"));
		kv (lists[1], f[i].name, b);
	}
	L = lists[2];
	doc_meta (g_mu, D, FZ_META_ENCRYPTION, b, sizeof b);
	kv (L, TR ("Encryption"), b[0] && strcmp (b, "None") ? b : TR ("None"));
	kv (L, TR ("Printing"), doc_allows (g_mu, D, FZ_PERMISSION_PRINT) ? TR ("Allowed") : TR ("Not allowed"));
	kv (L, TR ("Copying text and images"), doc_allows (g_mu, D, FZ_PERMISSION_COPY) ? TR ("Allowed") : TR ("Not allowed"));
	kv (L, TR ("Changing the document"), doc_allows (g_mu, D, FZ_PERMISSION_EDIT) ? TR ("Allowed") : TR ("Not allowed"));
	kv (L, TR ("Comments, filling forms"), doc_allows (g_mu, D, FZ_PERMISSION_ANNOTATE) ? TR ("Allowed") : TR ("Not allowed"));
	ui_dialog_button (d, 0, (long) TR ("Close"), 1, 0, W - 16);
	ui_dialog_cancel (d, 0);
	ui_dialog_run (d, 0, 0);
	ui_dialog_end (d);
}

// ---- ouvrir, fermer -------------------------------------------------------------------------------------------------------------
static void tab_init (Tab &t, Doc *D)
{
	memset (&t, 0, sizeof t);
	t.doc = D; t.layout = g_defLayout; t.zoomMode = g_defZoom; t.zoom = 96.0f / 72.0f; t.side = S_PAGES; t.hit = -1; t.pendingPage = -1; t.selPage = -1;
}
static int tab_of_doc (Doc *D) { for (int k = 0; k < g_ntabs; k++) if (g_tabs[k].doc == D) return k; return -1; }
static void open_path (const char *path, int page, bool newTab)
{
	for (int k = 0; k < g_ntabs; k++)
		if (g_tabs[k].doc && !strcmp (g_tabs[k].doc->path, path)) { g_cur = k; if (page >= 0) show_page (tab (), page); g_tabsDirty = true; refresh_all (); return; }
	bool needPw; char err[200];
	Doc *D = doc_open (g_mu, path, &needPw, err, sizeof err);
	const char *nm = path; for (const char *p = path; *p; p++) if (*p == '/' || *p == ':') nm = p + 1;
	if (!D)
	{
		char m[400]; snprintf (m, sizeof m, "%s", TR ("It is not a PDF document, or it is damaged."));
		char l[500]; snprintf (l, sizeof l, TR ("\xE2\x80\x9C%s\xE2\x80\x9D cannot be opened: %s"), nm, m);
		msg (l); return;
	}
	if (needPw)
	{
		char m[300], e[120] = "", pw[128]; snprintf (m, sizeof m, TR ("\xE2\x80\x9C%s\xE2\x80\x9D is protected. Its password:"), nm);
		bool ok = false;
		while (ask_password (m, e, pw, sizeof pw))
		{
			if (doc_auth (g_mu, D, pw)) { ok = true; break; }
			scopy (e, TR ("That is not the password."), sizeof e);
		}
		if (!ok) { doc_free (g_mu, D); return; }
	}
	if (!doc_load (g_mu, D, err, sizeof err))
	{
		char m[400]; snprintf (m, sizeof m, TR ("\xE2\x80\x9C%s\xE2\x80\x9D cannot be read: %s"), nm, err);
		msg (m); doc_free (g_mu, D); return;
	}
	int k;
	if (!newTab && !tab ().doc) k = g_cur;				// l'accueil : remplace
	else if (g_ntabs < MAX_TABS) k = g_ntabs++;
	else { msg (TR ("Too many documents are open: close one first.")); doc_free (g_mu, D); return; }
	tab_init (g_tabs[k], D);
	if (page < 0) { int r = recent_find (D->path); if (r >= 0) page = g_rec[r].page; }
	if (page > 0 && page < D->npages) { g_tabs[k].pendingPage = page; g_tabs[k].pendingY = -1; g_tabs[k].cur = page; }
	g_cur = k;
	invalidate_layout ();
	recent_note (g_tabs[k]);
	g_tabsDirty = true;
	refresh_all ();
}
static void new_home_tab ()
{
	g_tabsDirty = true;
	for (int k = 0; k < g_ntabs; k++) if (!g_tabs[k].doc) { g_cur = k; refresh_all (); return; }
	if (g_ntabs >= MAX_TABS) return;
	tab_init (g_tabs[g_ntabs], 0); g_cur = g_ntabs++;
	folders_count ();
	refresh_all ();
}
static void close_tab (int k)
{
	if (k < 0 || k >= g_ntabs) return;
	Tab &t = g_tabs[k];
	if (t.doc)
	{
		recent_note (t);
		g_worker.forget (t.doc);
		bmp_forget_doc (t.doc->id); text_forget (t.doc->id);
		for (int i = 0; i < t.nhits; i++) free (t.hits[i].ctx);
		free (t.hits);
		doc_free (g_mu, t.doc);
	}
	memmove (g_tabs + k, g_tabs + k + 1, sizeof (Tab) * (g_ntabs - k - 1)); g_ntabs--;
	if (!g_ntabs) { tab_init (g_tabs[0], 0); g_ntabs = 1; folders_count (); }
	if (g_cur >= g_ntabs) g_cur = g_ntabs - 1; else if (g_cur > k) g_cur--;
	invalidate_layout ();
	g_tabsDirty = true;
	refresh_all ();
}

// ---- se deplacer -----------------------------------------------------------------------------------------------------------------
static void show_page (Tab &t, int pg, float y)
{
	if (!t.doc || pg < 0 || pg >= t.doc->npages) return;
	int W = vw (), H = vh ();
	lay_out (t, W, H);
	t.cur = pg;
	int yy = g_pos[pg].y - MARGIN / 2;
	if (y > 0 && t.rot == 0) yy = g_pos[pg].y + (int) (y * g_scale) - 24;
	t.sy = yy;
	if (t.layout == L_SINGLE) { int y0, y1; scroll_range (t, H, &y0, &y1); t.sy = y > 0 ? yy : y0; }
	clamp_scroll (t, W, H);
	g_pillT = g_tick;
	side_follow ();
	refresh_all ();
}
static void set_layout (int l)
{
	Tab &t = tab (); if (!t.doc) return;
	int pg = t.cur; t.layout = l; g_defLayout = l; save_settings ();
	invalidate_layout (); show_page (t, pg);
}
static const float ZOOMS[] = { 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 2.0f, 3.0f, 4.0f };	// (x 96/72 : le « 100 % » est la taille reelle de la page)
static void set_zoom (int mode, float z)
{
	Tab &t = tab (); if (!t.doc) return;
	int pg = t.cur; t.zoomMode = mode; if (mode == Z_CUSTOM) t.zoom = z;
	g_defZoom = mode == Z_CUSTOM ? Z_WIDTH : mode; save_settings ();
	invalidate_layout (); show_page (t, pg);
}
static void zoom_step (int dir)
{
	Tab &t = tab (); if (!t.doc) return;
	float cur = g_scale * 72.0f / 96.0f, nz = cur;
	if (dir > 0) { nz = 4.0f; for (unsigned i = 0; i < sizeof ZOOMS / sizeof ZOOMS[0]; i++) if (ZOOMS[i] > cur + 0.01f) { nz = ZOOMS[i]; break; } }
	else { nz = 0.5f; for (int i = (int) (sizeof ZOOMS / sizeof ZOOMS[0]) - 1; i >= 0; i--) if (ZOOMS[i] < cur - 0.01f) { nz = ZOOMS[i]; break; } }
	set_zoom (Z_CUSTOM, nz * 96.0f / 72.0f);
}
static void rotate () { Tab &t = tab (); if (!t.doc) return; int pg = t.cur; t.rot = (t.rot + 90) % 360; invalidate_layout (); show_page (t, pg); }
static void toggle_side () { g_sideOn = !g_sideOn; save_settings (); invalidate_layout (); refresh_all (); }
// un menu en un point de la fenetre : libelles, 0 en fin -> celui choisi, -1. « - » : un trait (il compte) ; premier octet 1 : grise
static long popup (int x, int y, std::initializer_list<const char *> items, int width = 200)
{
	char t[1200]; int n = 0;
	for (const char *s : items)
	{
		int l = (int) strlen (s);
		if (n + l + 2 >= (int) sizeof t) break;
		memcpy (t + n, s, (size_t) l + 1); n += l + 1;
	}
	t[n] = 0;
	return ui_popup_local (x, y, (long) t, width);
}
static void zoom_menu ()
{
	Tab &t = tab (); if (!t.doc) return;
	char a[48], b[48], lab[8][24];
	snprintf (a, sizeof a, "%s%s", TR ("Fit page"), t.zoomMode == Z_PAGE ? "  \xE2\x9C\x93" : "");
	snprintf (b, sizeof b, "%s%s", TR ("Fit width"), t.zoomMode == Z_WIDTH ? "  \xE2\x9C\x93" : "");
	for (int i = 0; i < 8; i++)
	{
		bool on = t.zoomMode == Z_CUSTOM && fabsf (t.zoom - ZOOMS[i] * 96.0f / 72.0f) < 0.01f;
		snprintf (lab[i], sizeof lab[i], on ? "%d %%  \xE2\x9C\x93" : "%d %%", (int) (ZOOMS[i] * 100));
	}
	long r = popup (276, TABS_H + 40, { a, b, TR ("Actual size"), "-", lab[0], lab[1], lab[2], lab[3], lab[4], lab[5], lab[6], lab[7] }, 180);
	if (r == 0) set_zoom (Z_PAGE, 0); else if (r == 1) set_zoom (Z_WIDTH, 0); else if (r == 2) set_zoom (Z_CUSTOM, 96.0f / 72.0f);
	else if (r >= 4) set_zoom (Z_CUSTOM, ZOOMS[r - 4] * 96.0f / 72.0f);
}

// ---- la recherche --------------------------------------------------------------------------------------------------------------
static void search_clear (Tab &t)
{
	for (int i = 0; i < t.nhits; i++) free (t.hits[i].ctx);
	free (t.hits); t.hits = 0; t.nhits = t.capHits = 0; t.hit = -1; t.done = false; t.searched = 0;
}
static void search_start (Tab &t, const char *needle)
{
	if (!t.doc || !strcmp (needle, t.needle)) return;
	search_clear (t);
	scopy (t.needle, needle, sizeof t.needle);
	t.gen++;
	if (!needle[0]) { g_worker.stop_search (); t.done = true; refresh_all (); return; }
	// mots entiers : une expression reguliere ; sinon les mots tels quels (casse ignoree sauf demande)
	char q[440]; int opts = (t.opts & 1) ? 0 : FZ_SEARCH_IGNORE_CASE;
	if (t.opts & 2)
	{
		int o = 0; o += snprintf (q, sizeof q, "\\b");
		for (const char *p = needle; *p && o < (int) sizeof q - 8; p++) { if (strchr ("\\^$.|?*+()[]{}", *p)) q[o++] = '\\'; q[o++] = *p; }
		snprintf (q + o, sizeof q - o, "\\b"); opts |= FZ_SEARCH_REGEXP;
	}
	else scopy (q, needle, sizeof q);
	g_worker.start_search (t.doc, q, opts, t.gen);
	refresh_all ();
}
static void on_hits (HitPack *h)
{
	int k = tab_of_doc (h->doc);
	if (k >= 0 && g_tabs[k].gen == h->gen)
	{
		Tab &t = g_tabs[k];
		if (t.nhits + h->n > t.capHits) { t.capHits = (t.nhits + h->n) * 2 + 16; t.hits = (SearchHit *) realloc (t.hits, sizeof (SearchHit) * t.capHits); }
		memcpy (t.hits + t.nhits, h->h, sizeof (SearchHit) * h->n);
		int was = t.nhits; t.nhits += h->n; t.searched = h->page + 1; if (h->last) t.done = true;
		if (t.hit < 0) for (int i = was; i < t.nhits; i++) if (t.hits[i].page >= t.cur) { if (k == g_cur) goto_hit (t, i); else t.hit = i; break; }
		if (t.done && t.hit < 0 && t.nhits) { if (k == g_cur) goto_hit (t, 0); else t.hit = 0; }
		if (k == g_cur) refresh_all ();
	}
	else for (int i = 0; i < h->n; i++) free (h->h[i].ctx);
	free (h->h); free (h);
}
static void goto_hit (Tab &t, int k)
{
	if (!t.doc || k < 0 || k >= t.nhits) return;
	t.hit = k;
	SearchHit &h = t.hits[k];
	int W = vw (), H = vh ();
	lay_out (t, W, H);
	if (t.layout == L_SINGLE) t.cur = h.page;
	const PagePos &p = g_pos[h.page];
	int hx, hy, hw, hh; quad_to_px (t.doc, h.page, g_scale, t.rot, h.q[0], p.x, p.y, &hx, &hy, &hw, &hh);
	if (hy < t.sy + 20 || hy + hh > t.sy + H - 20) t.sy = hy - H / 3;
	if (hx < t.sx || hx + hw > t.sx + W) t.sx = hx - W / 3;
	clamp_scroll (t, W, H);
	t.cur = t.layout == L_SINGLE ? h.page : page_at (t, H);
	int y = 0; for (int i = 0; i <= k; i++) { if (i == 0 || t.hits[i - 1].page != t.hits[i].page) y += 22 + (i ? 4 : 0); if (i < k) y += 24; }
	int lh = g_spH > 0 ? g_spH : 300;
	if (y < t.findScroll) t.findScroll = y - 30; else if (y + 24 > t.findScroll + lh) t.findScroll = y + 24 - lh + 30;
	if (t.findScroll < 0) t.findScroll = 0;
	refresh_all ();
}
static void find_next (int dir)
{
	Tab &t = tab (); if (!t.doc || !t.nhits) return;
	int k = t.hit + dir;
	if (k >= t.nhits) k = 0; if (k < 0) k = t.nhits - 1;
	goto_hit (t, k);
}
static unsigned g_queryT;
static void search_enter (long box)
{
	Tab &t = tab ();
	const char *s = (const char *) ui_w_get_text (box);
	if (strcmp (s, t.needle)) { char q[200]; scopy (q, s, sizeof q); search_start (t, q); return; }
	find_next (ui_ev_field (&ui, EV_MODS) & UI_MOD_SHIFT ? -1 : 1);
}
static void page_field_enter ()
{
	Tab &t = tab (); if (!t.doc) return;
	int n = atoi ((const char *) ui_w_get_text (g_pageBox));
	if (n >= 1 && n <= t.doc->npages) show_page (t, n - 1);
	ui_w_focus (g_view);
	refresh_all ();
}

// ---- la selection, les liens ------------------------------------------------------------------------------------------------------
static char *selection_text ()
{
	Tab &t = tab (); if (!t.doc || !t.sel) return 0;
	fz_stext_page *st = text_of (t.doc, t.selPage); if (!st) return 0;
	char *s = 0;
	fz_try (g_mu) s = fz_copy_selection (g_mu, st, fz_make_point (t.ax, t.ay), fz_make_point (t.bx, t.by), 0);
	fz_catch (g_mu) s = 0;
	return s;
}
static void copy_selection ()
{
	char *s = selection_text ();
	if (!s) return;
	if (!doc_allows (g_mu, tab ().doc, FZ_PERMISSION_COPY)) { msg (TR ("This document does not allow copying its text.")); fz_free (g_mu, s); return; }
	ui_clip_set ((long) s, -1); fz_free (g_mu, s);
}
static void select_all ()
{
	Tab &t = tab (); if (!t.doc) return;
	t.selPage = t.cur; t.ax = -1e4f; t.ay = -1e4f; t.bx = 1e5f; t.by = 1e5f; t.sel = true; refresh_all ();
}
static bool spawn_app (const char *path, const char *arg)
{
	char *argv[] = { (char *) path, (char *) arg, 0 };
	pid_t p;
	return posix_spawn (&p, path, 0, 0, argv, environ) == 0;
}
static void url_open (const char *uri)
{
	if (!strncmp (uri, "http://", 7) || !strncmp (uri, "https://", 8)) { spawn_app (WEB_PROGRAM, uri); return; }
	if (!strncmp (uri, "file:", 5) || strstr (uri, ".pdf")) { const char *p = uri; if (!strncmp (p, "file://", 7)) p += 7; open_path (p); return; }
	char m[400]; snprintf (m, sizeof m, TR ("This link (%.300s) cannot be opened here."), uri);
	msg (m);
}
static void look_up (const char *s)
{
	char u[600]; int o = snprintf (u, sizeof u, "https://duckduckgo.com/?q=");
	for (const unsigned char *p = (const unsigned char *) s; *p && o < (int) sizeof u - 4; p++)
	{
		if (isalnum (*p) || *p == '-' || *p == '.' || *p == '_') u[o++] = (char) *p;
		else if (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') u[o++] = '+';
		else o += snprintf (u + o, sizeof u - o, "%%%02X", *p);
	}
	u[o] = 0;
	spawn_app (WEB_PROGRAM, u);
}
static void context_menu (int mx, int my)
{
	Tab &t = tab (); if (!t.doc) return;
	char *s = selection_text ();
	char lab[80]; snprintf (lab, sizeof lab, "%s%s", s ? "" : "\x01", TR ("Find the Selection"));
	if (s) { char w[40]; int n = 0; for (const char *p = s; *p && n < 24; p++) w[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p; w[n] = 0; snprintf (lab, sizeof lab, TR ("Find \xE2\x80\x9C%s%s\xE2\x80\x9D"), w, strlen (s) > 24 ? "\xE2\x80\xA6" : ""); }
	char copy[40], look[60]; snprintf (copy, sizeof copy, "%s%s", s ? "" : "\x01", TR ("Copy")); snprintf (look, sizeof look, "%s%s", s ? "" : "\x01", TR ("Look Up in Jet Browser"));
	long r = popup (mx + g_vwX, my + TABS_H + TB_H, { copy, TR ("Select All"), "-", lab, look }, 260);
	if (r == 0) copy_selection ();
	else if (r == 1) select_all ();
	else if ((r == 3 || r == 4) && s)
	{
		char q[200]; int n = 0; for (const char *p = s; *p && n < 190; p++) q[n++] = (*p == '\n' || *p == '\r') ? ' ' : *p; q[n] = 0;
		while (n && q[n - 1] == ' ') q[--n] = 0;
		if (r == 3) { ui_w_set_text (g_findBox, (long) q); ui_w_set_text (g_sideBox, (long) q); t.side = S_FIND; if (!g_sideOn) toggle_side (); search_start (t, q); }
		else look_up (q);
	}
	if (s) fz_free (g_mu, s);
}

// ---- le menu du bouton ⋯, les fichiers -----------------------------------------------------------------------------------------
static void save_copy ()
{
	Tab &t = tab (); if (!t.doc) return;
	char out[300];
	ui_file_filter ((long) "*.pdf");
	if (!ui_file_save ((long) "SD0:/home/documents", (long) t.doc->name, (long) out, sizeof out)) return;
	int a = open (t.doc->path, O_RDONLY), b = a >= 0 ? open (out, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
	bool ok = a >= 0 && b >= 0;
	if (ok) { static char buf[32768]; long n; while ((n = read (a, buf, sizeof buf)) > 0) if (write (b, buf, (size_t) n) != n) { ok = false; break; } }
	if (a >= 0) close (a); if (b >= 0) close (b);
	if (!ok) msg (TR ("The copy could not be written."));
}
static void show_in_files ()
{
	Tab &t = tab (); if (!t.doc) return;
	char d[300]; scopy (d, t.doc->path, sizeof d);
	char *e = 0; for (char *p = d; *p; p++) if (*p == '/') e = p;
	if (e) *e = 0;
	spawn_app (FILES_PROGRAM, d);
}
static void more_menu ()
{
	Tab &t = tab ();
	char a[60], b[60], c[60], d[60];
	const char *g = t.doc ? "" : "\x01";
	snprintf (a, sizeof a, "%s%s", g, TR ("Properties...")); snprintf (b, sizeof b, "%s%s", g, TR ("Save a Copy...")); snprintf (c, sizeof c, "%s%s", g, TR ("Show in the File Viewer"));
	snprintf (d, sizeof d, "%s%s", t.sel ? "" : "\x01", TR ("Copy"));
	long r = popup ((int) ui.w - 230, TABS_H + 40, { d, "-", a, b, c, "-", TR ("Close the Tab") }, 220);
	if (r == 0) copy_selection (); else if (r == 2) properties (); else if (r == 3) save_copy (); else if (r == 4) show_in_files (); else if (r == 6) close_tab (g_cur);
}
static void m_open ()
{
	char p[300], dir[300];
	scopy (dir, tab ().doc ? tab ().doc->path : "SD0:/home/documents", sizeof dir);
	{ char *e = 0; for (char *q = dir; *q; q++) if (*q == '/') e = q; if (e && tab ().doc) *e = 0; }
	ui_file_filter ((long) "*.pdf");
	if (ui_file_open ((long) dir, (long) p, sizeof p)) open_path (p, -1, tab ().doc != 0);
}

// ---- plein ecran : les pages seules, sur du noir (une presentation) ---------------------------------------------------------------
static void full_screen ()
{
	Tab &t = tab (); if (!t.doc) return;
	long scr[4] = { 0, 0, 0, 0 };
	ui_screen ((long) scr);
	ui_dialog_at (0, 0);
	long dlg = ui_dialog_begin (scr[0], scr[1], (long) TR ("PDF Viewer"));
	if (!dlg) return;
	long win = ui.win;
	int W = (int) ui.w, H = (int) ui.h;
	Doc *D = t.doc;
	int pg = t.cur, shown = -1;
	bool quit = false;
	while (!quit)
	{
		if (pg != shown)
		{
			float s = W / (t.rot % 180 ? D->ph[pg] : D->pw[pg]), s2 = H / (t.rot % 180 ? D->pw[pg] : D->ph[pg]); if (s2 < s) s = s2;
			int w, h; page_px (D, pg, s, t.rot, &w, &h);
			unsigned *px = render_page (g_mu, D, pg, s, t.rot, 0, 0, w, h);
			ui_fill (0, 0, W, H, 0);
			if (px)
			{
				int x0 = (W - w) / 2, y0 = (H - h) / 2;
				for (int y = 0; y < h && y0 + y < H; y++) memcpy (ui.surf + (size_t) (y0 + y) * ui.w + (x0 > 0 ? x0 : 0), px + (size_t) y * w, (size_t) (w < W ? w : W) * 4);
				free (px);
			}
			ui_update_all ();
			shown = pg;
		}
		long e = ui_event (30);
		if (e == WEV_CLOSE) break;
		if (e <= 0 || ui_ev_field (&ui, EV_WIN) != win) continue;
		if (e == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0)
		{
			int k = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR);
			if (k == HID_ESC || k == HID_F5 || ch == 'q') quit = true;
			else if (k == HID_RIGHT || k == HID_DOWN || k == HID_PGDN || k == HID_SPACE || k == HID_ENTER) { if (pg + 1 < D->npages) pg++; }
			else if (k == HID_LEFT || k == HID_UP || k == HID_PGUP || k == HID_BKSP) { if (pg > 0) pg--; }
			else if (k == HID_HOME) pg = 0; else if (k == HID_END) pg = D->npages - 1;
		}
		else if (e == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1)
		{	// au toucher : a gauche on recule, a droite on avance, en haut a droite on sort
			int x = ui_ev_field (&ui, EV_X), y = ui_ev_field (&ui, EV_Y);
			if (x > W - 80 && y < 80) quit = true;
			else if (x < W / 3) { if (pg > 0) pg--; } else if (pg + 1 < D->npages) pg++; else quit = true;
		}
		else if (e == WEV_WHEEL) { if (ui_ev_field (&ui, EV_CODE) > 0) { if (pg > 0) pg--; } else if (pg + 1 < D->npages) pg++; }
	}
	ui_dialog_end (dlg);
	show_page (t, pg);
}

// ---- reglages ------------------------------------------------------------------------------------------------------------------------
static void load_settings ()
{
	char *buf = file_read (SETTINGS, 0);
	if (!buf) return;
	for (char *l = buf, *nx; l && *l; l = nx)
	{
		nx = strchr (l, '\n'); if (nx) *nx++ = 0;
		char k[64]; int v;
		if (sscanf (l, " %63[a-z] = %d", k, &v) != 2) continue;
		if (!strcmp (k, "side")) g_sideOn = v != 0;
		else if (!strcmp (k, "layout") && v >= 0 && v <= 2) g_defLayout = v;
		else if (!strcmp (k, "zoom") && v >= 0 && v <= 1) g_defZoom = v;
		else if (!strcmp (k, "hand")) g_handOn = v != 0;
	}
	free (buf);
}
static void save_settings ()
{
	mkdir (CONF_DIR, 0777);
	char b[400];
	int n = snprintf (b, sizeof b, "# PDF Viewer\n# side: the side panel shown; layout: 0 one page, 1 continuous, 2 two pages; zoom: 0 fit the width, 1 the page; hand: a drag moves the pages\n"
			  "side = %d\nlayout = %d\nzoom = %d\nhand = %d\n", g_sideOn, g_defLayout, g_defZoom, g_handOn);
	file_write (SETTINGS, b, n);
}

// ---- la fenetre --------------------------------------------------------------------------------------------------------------------------
static void show_part (long w, bool on) { ui_w_show (w, on); }
static void layout_parts ()
{
	int W = (int) ui.w, H = (int) ui.h;
	Tab &t = tab ();
	bool doc = t.doc != 0;
	ui_w_move (g_tabsW, 0, 0, W - FRAME_CAP_W - 16, TABS_H);
	show_part (g_vTb, doc); show_part (g_view, doc); show_part (g_vHome, !doc);
	int y = TABS_H;
	if (doc) { ui_w_move (g_vTb, 0, y, W, TB_H); y += TB_H; }
	bool over = W < 640;					// (fenetre etroite : le panneau passe par-dessus les pages)
	int sw = doc && g_sideOn ? SIDE_W : 0;
	g_sideW = SIDE_W;
	show_part (g_vSide, sw != 0);
	if (sw) ui_w_move (g_vSide, 0, y, SIDE_W, H - y);
	int vx = over ? 0 : sw;
	g_vwW = W - vx; g_vwH = H - y;
	g_vwX = vx;
	ui_w_move (g_view, vx, y, g_vwW, g_vwH);
	ui_w_move (g_vHome, 0, TABS_H, W, H - TABS_H);
	// la barre d'outils : le champ de la page, la recherche
	g_tbNarrow = W < 900;
	int by = TABS_H + 9;
	show_part (g_pageBox, doc);
	if (doc) ui_w_move (g_pageBox, 8 + 68 + 10 + 68 + 4, by, 46, 28);
	char n[24]; snprintf (n, sizeof n, "/ %d", doc ? t.doc->npages : 0);
	int x = 8 + 68 + 10 + 68 + 54 + tw (n) + 14 + 10 + 34 + (g_tbNarrow ? 90 : 138) + 34 + 10 + (g_tbNarrow ? 0 : 102) + 102 + 8;
	int sr = W - 48, swf = sr - x; if (swf > 240) swf = 240;
	bool fb = doc && swf >= 120;
	show_part (g_findBox, fb);
	if (fb) ui_w_move (g_findBox, sr - swf, by, swf, 28);
	show_part (g_sideBox, doc && sw && t.side == S_FIND);
	if (sw) ui_w_move (g_sideBox, 10, y + 50, SIDE_W - 20, 28);
}
static void sync_page_box ()
{
	Tab &t = tab ();
	if (ui_w_get_focus () == g_pageBox) return;
	char p[16]; snprintf (p, sizeof p, "%d", t.doc ? t.cur + 1 : 0);
	if (strcmp (p, (const char *) ui_w_get_text (g_pageBox))) ui_w_set_text (g_pageBox, (long) p);
}
static void tabs_sync ()
{
	if (!g_tabsDirty) return;
	g_tabsDirty = false;
	ui_list_clear (g_tabsW);
	for (int k = 0; k < g_ntabs; k++) ui_tabstrip_add (g_tabsW, (long) (g_tabs[k].doc ? g_tabs[k].doc->name : TR ("Home")), k);
	ui_w_set_value (g_tabsW, g_cur);
}
static bool g_paintAll;					// des widgets montres, caches ou deplaces : tout se redessine
static void refresh_all ()
{
	if (!g_view) return;
	static int last = -1;
	Tab &t = tab ();
	int k = (t.doc ? 1 : 0) | (g_sideOn ? 2 : 0) | (t.side == S_FIND ? 4 : 0) | (g_tabsDirty ? 8 : 0);
	if (k != last || g_tabsDirty) { last = k & 7; g_paintAll = true; }
	layout_parts ();
	tabs_sync ();
	sync_page_box ();
	for (long v : { g_vTb, g_vSide, g_view, g_vHome }) ui_view_dirty (v);
}
static void paint_all ()
{
	g_paintAll = false;
	layout_parts ();
	ui_fill (0, 0, ui.w, ui.h, C_BG);
	ui_fill (0, 0, ui.w, TABS_H - 1, C_SIDE);
	ui_fill (0, TABS_H - 1, ui.w, 1, C_LINE);
	refresh_all ();
	g_paintAll = false;
	ui_w_draw (0); ui_update_all ();
}
static void on_resize (long, long) { invalidate_layout (); paint_all (); }
static void on_done (Done *d)
{
	if (d->job.kind == J_RECENT)
	{
		int k = recent_find (d->job.path);
		if (k >= 0) { g_rec[k].asked = true; if (d->px) { free (g_rec[k].px); g_rec[k].px = d->px; g_rec[k].w = d->w; g_rec[k].h = d->h; d->px = 0; if (d->npages) g_rec[k].npages = d->npages; } }
		else free (d->px);
		ui_view_dirty (g_vHome);
		free (d); return;
	}
	bool live = false;
	for (int k = 0; k < g_ntabs; k++) if (g_tabs[k].doc == d->job.doc && g_tabs[k].doc->id == d->job.docId) live = true;
	if (live && d->px)
	{
		bmp_add (d->job, d->px);
		if (d->job.doc == tab ().doc) { if (d->job.kind == J_THUMB) ui_view_dirty (g_vSide); else ui_view_dirty (g_view); }
	}
	else free (d->px);
	free (d);
}
static void pump ()
{
	Post *p;
	while ((p = g_worker.take ())) { if (p->d) on_done (p->d); if (p->h) on_hits (p->h); free (p); }
}

// ---- les menus -----------------------------------------------------------------------------------------------------------------------------
// File > Print: the Print dialog (PrinterKit's), then the pages drawn by MuPDF at the printer's 300 dots an inch, each a picture of
// the job's page (the document's own page size: fitted on the paper). To the PDF printer with every page: the file itself is
// copied -- it is already what is asked for.
static void print_doc ()
{
	Tab &t = tab (); if (!t.doc) return;
	Doc *D = t.doc;
	if (!doc_allows (g_mu, D, FZ_PERMISSION_PRINT)) { msg (TR ("This document does not allow printing.")); return; }
	if (!printerkit_kit_open ()) { msg (TR ("The print service is not installed.")); return; }
	PrintSetup ps; print_setup_default ((long) &ps);
	PrintDialogInfo di = { sizeof di, D->name, D->npages, t.cur + 1, PRINT_DLG_OWN_PAPER, 0, 0 };
	if (!print_dialog ((long) &ps, (long) &di)) return;
	if (!strcmp (ps.printer, "PDF") && ps.from <= 0)
	{
		int a = open (D->path, O_RDONLY), b = a >= 0 ? open (ps.output, O_WRONLY | O_CREAT | O_TRUNC, 0666) : -1;
		bool ok = a >= 0 && b >= 0;
		if (ok) { static char buf[32768]; long n; while ((n = read (a, buf, sizeof buf)) > 0) if (write (b, buf, (size_t) n) != n) { ok = false; break; } }
		if (a >= 0) close (a);
		if (b >= 0) close (b);
		if (!ok) msg (TR ("The copy could not be written."));
		return;
	}
	long j = print_begin ((long) &ps, (long) D->name);
	for (int pg = 0; j && pg < D->npages; pg++)
	{
		if (!print_page (j, print_mpt (D->pw[pg]), print_mpt (D->ph[pg]))) continue;	// (not in the pages to print: not rendered)
		float s = 300.0f / 72.0f;
		if (D->pw[pg] * s > 3600 || D->ph[pg] * s > 3600) s = 3600 / (D->pw[pg] > D->ph[pg] ? D->pw[pg] : D->ph[pg]);	// (a poster: fewer dots)
		int w, h; page_px (D, pg, s, 0, &w, &h);
		unsigned *px = render_page (g_mu, D, pg, s, 0, 0, 0, w, h);
		if (!px) continue;
		print_image (j, (long) px, w, h, 0, 0, print_mpt (D->pw[pg]), print_mpt (D->ph[pg]));
		free (px);
	}
	if (!j || print_end (j) < 0) msg (TR ("The document could not be put in the print queue."));
}
enum { C_PRINT = 100, C_OPEN = 1, C_HOME, C_CLOSE, C_SAVE, C_FILES, C_PROPS, C_COPY, C_SELALL, C_FIND, C_NEXT, C_PREV, C_SIDEP, C_FITP, C_FITW, C_ACTUAL, C_ZIN, C_ZOUT,
       C_SINGLE, C_CONT, C_TWO, C_ROT, C_FULL, C_PGNEXT, C_PGPREV, C_FIRST, C_LAST, C_GOTO, C_NEXTTAB };
static void menus ()
{
	static char b[2200]; int n = 0;
	auto M = [&] (const char *t) { n += snprintf (b + n, sizeof b - n, "M%s\n", t); };
	auto I = [&] (int id, const char *t, const char *key) { n += snprintf (b + n, sizeof b - n, "I%d\t%s%s%s\n", id, t, key[0] ? "\t" : "", key); };
	auto S = [&] () { n += snprintf (b + n, sizeof b - n, "-\n"); };
	M (TR ("File"));
	I (C_OPEN, TR ("Open..."), "Ctrl+O"); I (C_HOME, TR ("Home"), ""); I (C_CLOSE, TR ("Close the Tab"), "Ctrl+W"); S ();
	I (C_SAVE, TR ("Save a Copy..."), ""); I (C_PRINT, TR ("Print..."), "Ctrl+P"); I (C_FILES, TR ("Show in the File Viewer"), ""); S ();
	I (C_PROPS, TR ("Properties..."), "Ctrl+D");
	M (TR ("Edit"));
	I (C_COPY, TR ("Copy"), "Ctrl+C"); I (C_SELALL, TR ("Select All (the page)"), "Ctrl+A"); S ();
	I (C_FIND, TR ("Find..."), "Ctrl+F"); I (C_NEXT, TR ("Find Next"), "F3"); I (C_PREV, TR ("Find Previous"), "Shift+F3");
	M (TR ("View"));
	I (C_SIDEP, TR ("Side Panel"), "F9"); S ();
	I (C_FITP, TR ("Fit Page"), "Ctrl+0"); I (C_FITW, TR ("Fit Width"), ""); I (C_ACTUAL, TR ("Actual Size"), "Ctrl+1"); I (C_ZIN, TR ("Zoom In"), ""); I (C_ZOUT, TR ("Zoom Out"), ""); S ();
	I (C_SINGLE, TR ("One Page at a Time"), ""); I (C_CONT, TR ("Continuous"), ""); I (C_TWO, TR ("Two Pages"), ""); S ();
	I (C_ROT, TR ("Rotate"), "Ctrl+R"); I (C_FULL, TR ("Full Screen"), "F5");
	M (TR ("Go"));
	I (C_PGNEXT, TR ("Next Page"), ""); I (C_PGPREV, TR ("Previous Page"), ""); I (C_FIRST, TR ("First Page"), ""); I (C_LAST, TR ("Last Page"), "");
	I (C_GOTO, TR ("Go to Page..."), "Ctrl+G"); S (); I (C_NEXTTAB, TR ("Next Tab"), "");
	ui_menu ((long) b);
}
static void m_find ()
{
	Tab &t = tab (); if (!t.doc) return;
	t.side = S_FIND; if (!g_sideOn) { g_sideOn = 1; save_settings (); invalidate_layout (); }
	refresh_all ();
	ui_w_focus (g_sideBox);
}
static void command (int id)
{
	Tab &t = tab ();
	switch (id)
	{
	case C_OPEN: m_open (); break;
	case C_HOME: new_home_tab (); break;
	case C_CLOSE: close_tab (g_cur); break;
	case C_SAVE: save_copy (); break;
	case C_PRINT: print_doc (); break;
	case C_FILES: show_in_files (); break;
	case C_PROPS: properties (); break;
	case C_COPY: copy_selection (); break;
	case C_SELALL: select_all (); break;
	case C_FIND: m_find (); break;
	case C_NEXT: find_next (1); break;
	case C_PREV: find_next (-1); break;
	case C_SIDEP: toggle_side (); break;
	case C_FITP: set_zoom (Z_PAGE, 0); break;
	case C_FITW: set_zoom (Z_WIDTH, 0); break;
	case C_ACTUAL: set_zoom (Z_CUSTOM, 96.0f / 72.0f); break;
	case C_ZIN: zoom_step (1); break;
	case C_ZOUT: zoom_step (-1); break;
	case C_SINGLE: set_layout (L_SINGLE); break;
	case C_CONT: set_layout (L_SCROLL); break;
	case C_TWO: set_layout (L_TWO); break;
	case C_ROT: rotate (); break;
	case C_FULL: full_screen (); break;
	case C_PGNEXT: if (t.doc && t.cur + 1 < t.doc->npages) show_page (t, t.cur + 1); break;
	case C_PGPREV: if (t.doc && t.cur > 0) show_page (t, t.cur - 1); break;
	case C_FIRST: if (t.doc) show_page (t, 0); break;
	case C_LAST: if (t.doc) show_page (t, t.doc->npages - 1); break;
	case C_GOTO: if (t.doc) { ui_w_focus (g_pageBox); ui_textbox_select (g_pageBox, 0, strlen ((const char *) ui_w_get_text (g_pageBox))); } break;
	case C_NEXTTAB: if (g_ntabs > 1) { g_cur = (g_cur + 1) % g_ntabs; g_tabsDirty = true; refresh_all (); } break;
	}
}
static void do_act ()
{
	int w = g_act, a = g_actA, x = g_actX, y = g_actY;
	g_act = A_NONE;
	Tab &t = tab ();
	switch (w)
	{
	case A_OPEN: m_open (); break;
	case A_ZOOM_MENU: zoom_menu (); break;
	case A_MORE_MENU: more_menu (); break;
	case A_FULL: full_screen (); break;
	case A_CTX: context_menu (x, y); break;
	case A_LINK:
		if (t.doc)
		{
			LinkInfo &li = t.doc->links[x].l[a];
			if (li.uri) url_open (li.uri); else if (li.page >= 0) show_page (t, li.page, li.y);
		}
		break;
	case A_RECENT:
	{
		Recent &r = g_rec[a];
		if (!file_exists (r.path)) { msg (TR ("This document is no longer there (moved or deleted). It is taken off the list.")); recent_remove (a); ui_view_dirty (g_vHome); break; }
		char p[300]; scopy (p, r.path, sizeof p);
		open_path (p, r.page, false);
		break;
	}
	case A_RECENT_MENU:
	{
		long r = popup (x, y, { TR ("Open"), TR ("Open in a New Tab"), "-", TR ("Remove from the List") }, 220);
		if (r == 0 || r == 1) { char p[300]; scopy (p, g_rec[a].path, sizeof p); open_path (p, g_rec[a].page, r == 1); }
		else if (r == 3) { recent_remove (a); ui_view_dirty (g_vHome); }
		break;
	}
	case A_FOLDER:
	{
		char p[300];
		ui_file_filter ((long) "*.pdf");
		if (ui_file_open ((long) g_folders[a].path, (long) p, sizeof p)) open_path (p, -1, false);
		break;
	}
	}
}
static void control (long w, long code)
{
	if (w == g_tabsW)
	{
		if (code == UN_CHANGE) { int k = (int) ui_w_get_value (w); if (k >= 0 && k < g_ntabs && k != g_cur) { g_cur = k; invalidate_layout (); refresh_all (); } }
		else if (code == UN_CLOSE) close_tab ((int) ui_tabstrip_closing (w));
		else if (code == UN_NEW) new_home_tab ();
	}
	else if (w == g_findBox || w == g_sideBox) { if (code == UN_ACTIVATE) search_enter (w); }
	else if (w == g_pageBox) { if (code == UN_ACTIVATE) page_field_enter (); }
}
static void key (int hid, int ch, int mods)
{
	Tab &t = tab ();
	bool ctrl = (mods & UI_MOD_CTRL) != 0;
	if (hid >= 0xE0) return;				// (un modificateur seul)
	if (ctrl && (ch == '+' || ch == '=')) { zoom_step (1); return; }
	if (ctrl && ch == '-') { zoom_step (-1); return; }
	if (hid == HID_TAB && ctrl) { command (C_NEXTTAB); return; }
	if (!t.doc) return;
	long f = ui_w_get_focus ();
	if (f == g_findBox || f == g_sideBox || f == g_pageBox)
	{
		if (hid == HID_ESC) { ui_w_focus (g_view); refresh_all (); }
		return;
	}
	int H = vh ();
	switch (hid)
	{
	case HID_DOWN: scroll_by (0, 50); break;
	case HID_UP: scroll_by (0, -50); break;
	case HID_RIGHT: if (g_contentW > vw ()) scroll_by (50, 0); else command (C_PGNEXT); break;
	case HID_LEFT: if (g_contentW > vw ()) scroll_by (-50, 0); else command (C_PGPREV); break;
	case HID_PGDN: scroll_by (0, H - 40); break;
	case HID_SPACE: scroll_by (0, (mods & UI_MOD_SHIFT) ? -(H - 40) : H - 40); break;
	case HID_PGUP: scroll_by (0, -(H - 40)); break;
	case HID_HOME: command (C_FIRST); break;
	case HID_END: command (C_LAST); break;
	case HID_ESC:
		if (t.sel) { t.sel = false; refresh_all (); }
		else if (t.needle[0]) { ui_w_set_text (g_findBox, (long) ""); ui_w_set_text (g_sideBox, (long) ""); }
		break;
	}
}
static long view_key (long, long, long, long) { return -1; }
static void tick ()
{
	g_tick = ticks ();
	Tab &t = tab ();
	// (hors du dessin d'une vue : layout_parts dessine en montrant et en cachant, et le dessin dans une vue ne s'imbrique pas)
	if (t.doc && t.pendingPage >= 0) { int p = t.pendingPage; float y = t.pendingY; t.pendingPage = -1; show_page (t, p, y); }
	// ou on lit est garde (le Pi est souvent simplement eteint) : toutes les 10 s si la page a change
	static unsigned lastNote; static int notedPage = -1, notedDoc = -1;
	if (t.doc && g_tick - lastNote > 1000 && (t.cur != notedPage || t.doc->id != notedDoc)) { lastNote = g_tick; notedPage = t.cur; notedDoc = t.doc->id; recent_note (t); }
	// la pastille de page s'en va
	static bool pill;
	bool p = g_tick - g_pillT < 150;
	if (p != pill) { pill = p; ui_view_dirty (g_view); }
	// la recherche, a mesure de la frappe (les deux champs restent pareils)
	if (t.doc)
	{
		static char lastTb[200], lastSide[200];
		const char *a = (const char *) ui_w_get_text (g_findBox), *b = (const char *) ui_w_get_text (g_sideBox);
		char ca[200], cb[200]; scopy (ca, a, sizeof ca); scopy (cb, b, sizeof cb);
		if (strcmp (ca, lastTb)) { scopy (lastTb, ca, sizeof lastTb); if (strcmp (cb, ca)) { ui_w_set_text (g_sideBox, (long) ca); } scopy (lastSide, ca, sizeof lastSide); g_queryT = g_tick; }
		else if (strcmp (cb, lastSide)) { scopy (lastSide, cb, sizeof lastSide); ui_w_set_text (g_findBox, (long) cb); scopy (lastTb, cb, sizeof lastTb); g_queryT = g_tick; }
		if (g_queryT && g_tick - g_queryT > 35)
		{
			g_queryT = 0;
			if (strcmp (lastTb, t.needle)) { if (lastTb[0] && t.side != S_FIND) { t.side = S_FIND; refresh_all (); } search_start (t, lastTb); }
		}
	}
	// l'infobulle d'un outil
	if (g_tipT && !g_tipOn && g_tick - g_tipT > 60 && g_tbHot >= 0 && g_tbHot < g_tbHits.n)
	{
		const Hit &h = g_tbHits.h[g_tbHot];
		const char *s = tb_tip (h.kind);
		if (s) { ui_tip_at (h.x, TABS_H + TB_H + 2, (long) s); g_tipOn = true; }
		g_tipT = 0;
	}
	sync_page_box ();
}
static void on_drop ()
{
	char data[480]; long kind = 0;
	long len = uikit_drop_get (data, sizeof data, &kind);
	if (kind != UDT_FILES) return;
	char path[300]; int o = 0;
	for (long i = 0; i <= len; i++)
	{
		char c = i < len ? data[i] : 0;
		if (c == '\n' || c == 0) { path[o] = 0; if (o > 4 && !strcasecmp (path + o - 4, ".pdf")) open_path (path, -1, tab ().doc != 0); o = 0; }
		else if (o < (int) sizeof path - 1) path[o++] = c;
	}
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/pdf.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	mkdir ("SD0:/home", 0777); mkdir (CONF_DIR, 0777);
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	g_handOn = scr[0] <= 800;				// (l'ecran tactile du PocketTerm : le doigt fait glisser les pages)
	if (!engine_init ()) { ui_message ((long) "PDF", (long) TR ("Not enough memory to start."), UM_OK); return 1; }
	load_settings ();
	recent_load ();
	folders_count ();
	tab_init (g_tabs[0], 0); g_ntabs = 1; g_cur = 0;

	int w = scr[0] - 8 < 1000 ? (int) scr[0] - 8 : 1000, h = (int) (scr[1] - scr[3] - (scr[2] > 96 ? scr[2] : 96) - 32); if (h > 680) h = 680;
	ui_window_ex (0, scr[3], w, h, (long) TR ("PDF Viewer"), WF_RESIZABLE);
	ui_min_size (480, 360);
	ui_on_resize ((long) on_resize, 0);

	g_view = ui_view (0, 0, 10, 10, UVW_FOCUS); ui_view_on (g_view, (long) view_paint, (long) view_mouse, (long) view_key, (long) view_wheel);
	g_vSide = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vSide, (long) side_paint, (long) side_mouse, 0, (long) side_wheel);
	g_vTb = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vTb, (long) tb_paint, (long) tb_mouse, 0, 0);
	g_vHome = ui_view (0, 0, 10, 10, 0); ui_view_on (g_vHome, (long) home_paint, (long) home_mouse, 0, 0);
	g_tabsW = ui_tabstrip (0, 0, 100, TABS_H, UTS_CLOSE | UTS_PLUS);
	g_pageBox = ui_textbox (0, 0, 46, 28, UTF_RIGHT); ui_w_set_range (g_pageBox, 0, 6, 0, 0); ui_w_set_parent (g_pageBox, g_vTb);
	g_findBox = ui_textbox (0, 0, 100, 28, 0); ui_textbox_hint (g_findBox, (long) TR ("Find in the document")); ui_w_set_range (g_findBox, 0, 190, 0, 0); ui_w_set_parent (g_findBox, g_vTb);
	g_sideBox = ui_textbox (0, 0, 100, 28, 0); ui_textbox_hint (g_sideBox, (long) TR ("Find in the document")); ui_w_set_range (g_sideBox, 0, 190, 0, 0); ui_w_set_parent (g_sideBox, g_vSide);
	g_worker.start ();
	menus ();
	g_tick = ticks ();
	paint_all ();

	if (argc > 1 && argv[1][0]) open_path (argv[1], -1, false);

	for (;;)
	{
		long t = ui_event (g_worker.idle () ? 30 : 8);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP) on_drop ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0, wd = uikit_w_event (&code, &taken);
			if (code) control (wd, code);
			if (press && !taken) { long id = ui_menu_key (0); if (id >= 0) command ((int) id); else key (hid, ch, mods); }
		}
		else ui_w_idle ();
		if (g_act) do_act ();
		pump ();
		tick ();
		if (g_paintAll) paint_all (); else ui_view_flush ();
	}

	for (int k = 0; k < g_ntabs; k++) recent_note (g_tabs[k]);
	g_worker.stop ();
	return 0;
}
