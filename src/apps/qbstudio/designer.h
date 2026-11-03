//
// designer.h -- le concepteur de QBStudio. Dans Onyx, c'était les vrais widgets d'uikit sous une couche transparente ; ici
// c'est une ui_view : la fenêtre du formulaire, ses contrôles dessinés aux jetons du thème (aux places que donne la
// mise en page de form.h), ses boîtes de mise en page par-dessus (les conteneurs en tirets et nommés), le choix et ses
// poignées. Un clic choisit un élément, un glisser le déplace -- à une autre place de son conteneur ou dans un autre --,
// les contrôles de la boîte à outils y arrivent par glisser (un trait montre où ils vont : ni x, ni y à donner), le coin
// de la fenêtre et les bords d'un contrôle les redimensionnent. La boîte à outils (une ui_view aussi) : les éléments,
// par groupes. Tout changement est fait à l'arbre, puis dit à l'application (g_onFormEdited), qui réécrit le texte.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_designer_h
#define _qbstudio_designer_h

#include "app.h"

namespace qs {

// La grille : des points tous les GRID pixels de la fenêtre (View > Grid) ; les tailles glissées en sont des multiples,
// les enfants d'un Canvas s'y posent (View > Snap to Grid ; Alt tenue : non)
static const int GRID = 5;
static bool g_showGrid = true, g_snap = true, g_alt = false;
static inline int snap (int v) { return g_snap && !g_alt ? (v + GRID / 2) / GRID * GRID : v; }

// ---- les éléments : créés, nommés, trouvés (repris d'Onyx) -----------------------------------------------------------
static const char *default_base (int k)
{
	switch (k)
	{
	case K_BUTTON: return "button"; case K_TEXTBOX: return "text"; case K_CHECKBOX: return "check"; case K_LISTBOX: return "list";
	case K_DROPDOWN: return "choice"; case K_SLIDER: return "slider"; case K_PROGRESS: return "progress"; case K_STATUSBAR: return "status"; case K_HOST: return "host";
	default: return 0;
	}
}
static void unique_name (Form &f, const char *base, char *out, int cap)
{
	for (int i = 1; i < 1000; i++) { snprintf (out, cap, "%s%d", base, i); if (!f.named (out)) return; }
}
static El *new_element (Form &f, int kind)
{
	El *e = new El; e->kind = kind;
	const char *b = default_base (kind);
	if (b) unique_name (f, b, e->name, sizeof e->name);
	switch (kind)
	{
	case K_LABEL: cpy (e->text, "Label", sizeof e->text); e->hasText = true; break;
	case K_BUTTON: cpy (e->text, "Button", sizeof e->text); e->hasText = true; break;
	case K_TEXTBOX: e->hasText = true; break;
	case K_CHECKBOX: cpy (e->text, "CheckBox", sizeof e->text); e->hasText = true; break;
	case K_LISTBOX: case K_DROPDOWN: e->set ("items", "One|Two|Three"); break;
	case K_SLIDER: e->set ("max", "100"); break;
	case K_PROGRESS: e->set ("value", "40"); break;
	case K_STATUSBAR: cpy (e->text, "Ready", sizeof e->text); e->hasText = true; break;
	case K_GROUP: cpy (e->text, "Group", sizeof e->text); e->hasText = true; break;
	case K_ROW: case K_COLUMN: e->set ("gap", "8"); break;
	case K_GRID: e->set ("cols", "2"); e->set ("gap", "8"); break;
	case K_CANVAS: e->set ("size", "200x120"); break;
	case K_HOST: e->setFlag ("fill", true); e->set ("grow", "1"); break;
	case K_MENU:
	{
		El *t = new El; t->kind = K_MENUTITLE; cpy (t->text, "&File", sizeof t->text); t->hasText = true; e->add (t);
		El *q = new El; q->kind = K_MENUITEM; cpy (q->text, "&Quit", sizeof q->text); q->hasText = true; q->set ("key", "Ctrl+Q");
		char n[32]; unique_name (f, "mnuQuit", n, sizeof n); cpy (q->name, n, sizeof q->name);
		t->add (q);
		break;
	}
	}
	return e;
}
static bool is_inside (const El *e, const El *anc) { for (; e; e = e->parent) if (e == anc) return true; return false; }
// Le chemin d'un élément (les index depuis la fenêtre) et retour
static int path_of (const El *e, int *out, int cap)
{
	int tmp[32], n = 0;
	for (; e && e->parent && n < 32; e = e->parent) tmp[n++] = e->index ();
	int m = 0; while (n && m < cap) out[m++] = tmp[--n];
	return m;
}
static El *el_by_path (const Form &f, const int *p, int n)
{
	El *e = f.root;
	for (int i = 0; e && i < n; i++) e = p[i] >= 0 && p[i] < e->kids.n ? e->kids[p[i]] : 0;
	return e;
}
static El *el_at_line (El *e, int line)
{
	if (!e) return 0;
	if (e->line == line && e->kind != K_COMMENT) return e;
	for (int i = 0; i < e->kids.n; i++) { El *r = el_at_line (e->kids[i], line); if (r) return r; }
	return 0;
}
static El *child_of_kind (El *w, int k) { if (w) for (int i = 0; i < w->kids.n; i++) if (w->kids[i]->kind == k) return w->kids[i]; return 0; }
// Le titre d'un menu sans son '&'
static void plain (const char *s, char *o, int cap) { int k = 0; for (; *s && k < cap - 1; s++) if (*s != '&') o[k++] = *s; o[k] = 0; }
static void describe (const El *e, char *o, int cap)
{
	int k = snprintf (o, cap, "%s", KIND_NAMES[e->kind]);
	if (e->name[0]) k += snprintf (o + k, cap > k ? cap - k : 0, " %s", e->name);
	static const char *const P[] = { "padding", "gap", "align", "cols", 0 };
	for (int i = 0; P[i]; i++) { const char *v = e->get (P[i]); if (v && k < cap) k += snprintf (o + k, cap - k, "  %s %s", P[i], v); }
}

// ---- l'état du concepteur -----------------------------------------------------------------------------------------------
enum { DR_NONE, DR_PRESS, DR_MOVE, DR_NEW, DR_SIZE_WIN, DR_SIZE_EL };
enum { TITLE_H = 28, SBW = 10 };
struct Designer
{
	long view, vsb, hsb;
	int X, Y, W, H;					// sa place dans la fenêtre (les barres ont des coordonnées de fenêtre)
	Form *form;
	El *sel;
	int ox, oy;					// la zone client du formulaire, dans la vue
	int winW, winH;					// sa taille (celle du formulaire, sans le menu)
	int sx, sy, maxX, maxY;				// le défilement
	bool pressed;
	int drag, newKind, px, py, startW, startH, grabX, grabY, sizeFx, sizeFy;
	El *dropPar; int dropIdx; int dropLine[4]; bool dropOk; int dropAtX, dropAtY;
	int hoverX, hoverY;
};
static Designer g_des;
static Designer &D = g_des;

static int menu_h () { return D.form && D.form->root && child_of_kind (D.form->root, K_MENU) ? MENU_H : 0; }
static void des_dirty () { ui_view_dirty (D.view); }
// Les boîtes pour le concepteur (le générateur met le formulaire en page à d'autres tailles aussi)
static void des_relay () { if (D.form && D.form->root) form_layout (*D.form, D.winW, D.winH + menu_h (), true); }

// Où se dessine la fenêtre pour le défilement, et les barres : la place est la fenêtre, son titre, une marge autour et
// celle de l'étiquette de l'élément choisi sous elle
static void des_origin ()
{
	int mh = menu_h ();
	int cw = D.winW + 48, ch = TITLE_H + D.winH + mh + 24 + 44;
	for (int pass = 0; pass < 2; pass++)		// (une barre prend de la place : l'autre peut alors manquer)
	{
		int vw = D.W - (D.maxY > 0 ? SBW : 0), vh = D.H - (D.maxX > 0 ? SBW : 0);
		D.maxX = imax (0, cw - vw); D.maxY = imax (0, ch - vh);
	}
	D.sx = imax (0, imin (D.sx, D.maxX)); D.sy = imax (0, imin (D.sy, D.maxY));
	int vw = D.W - (D.maxY > 0 ? SBW : 0), vh = D.H - (D.maxX > 0 ? SBW : 0);
	D.ox = (D.maxX > 0 ? 24 : imax (24, (vw - D.winW) / 2)) - D.sx; D.oy = 24 + TITLE_H - D.sy;
	if (D.vsb)
	{
		ui_w_move (D.vsb, D.X + D.W - SBW, D.Y, SBW, imax (10, vh));
		ui_w_move (D.hsb, D.X, D.Y + D.H - SBW, imax (10, vw), SBW);
		ui_w_set_range (D.vsb, 0, ch, vh, 24); ui_w_set_value (D.vsb, D.sy);
		ui_w_set_range (D.hsb, 0, cw, vw, 24); ui_w_set_value (D.hsb, D.sx);
		ui_w_show (D.vsb, D.maxY > 0); ui_w_show (D.hsb, D.maxX > 0);
	}
}
// Le formulaire montré de nouveau : mis en page, redessiné
static void des_rebuild ()
{
	if (D.form && D.form->root)
	{
		form_size (*D.form, &D.winW, &D.winH);
		form_layout (*D.form, D.winW, D.winH + menu_h (), true);
	}
	else D.winW = D.winH = 0;
	des_origin ();
	des_dirty ();
}
static void des_set_form (Form *f) { D.form = f; D.sel = 0; des_rebuild (); }
static void des_select (El *e, bool tell = true) { D.sel = e; des_dirty (); if (tell && g_onSelect) g_onSelect (e); }
static void des_place (int x, int y, int w, int h) { D.X = x; D.Y = y; D.W = w; D.H = h; ui_w_move (D.view, x, y, w, h); des_rebuild (); }
static void des_scroll_by (int dx, int dy) { D.sx += dx; D.sy += dy; des_origin (); des_dirty (); }

// ---- les contrôles dessinés : le reflet de ce que UIKit dessinera ------------------------------------------------------
static void boxed (int x, int y, int w, int h, int r, unsigned line, unsigned top, unsigned bottom)
{
	ui_rbox (x, y, w, h, r, line, line);
	if (w > 2 && h > 2) ui_rbox (x + 1, y + 1, w - 2, h - 2, imax (0, r - 1), top, bottom);
}
static void draw_strip (const El *e, int x, int y, int w, int h)
{
	unsigned face = jet (TJ_FACE);
	if (e->kind == K_MENU)
	{
		ui_fill (x, y, w, h, jet (TJ_MENU));
		ui_fill (x, y + h - 1, w, 1, jet (TJ_LIGNE));
		int tx = x + 10;
		for (int i = 0; i < e->kids.n; i++)
		{
			if (e->kids[i]->kind != K_MENUTITLE) continue;
			char t[64]; plain (e->kids[i]->text, t, sizeof t);
			text_box (t, tx, y, h, ink_on (jet (TJ_MENU)));
			tx += tw (t) + 18;
		}
	}
	else if (e->kind == K_HOST)				// la place d'un contrôle utilisateur : son cadre, ce qu'elle montre
	{
		dashed_rect (x, y, w, h, jet (TJ_LIGNE_FORTE));
		char t[96]; const char *c = e->get ("content");
		if (c && c[0]) snprintf (t, sizeof t, "%s: %s", e->name[0] ? e->name : "Host", c);
		else snprintf (t, sizeof t, "%s (a user control goes here)", e->name[0] ? e->name : "Host");
		if (tw (t) + 12 > w) cpy (t, e->name[0] ? e->name : "Host", sizeof t);
		text_c (t, x, y, w, h, jet (TJ_TEXTE3));
	}
	else if (e->kind == K_TOOLBAR) { ui_fill (x, y, w, h, face); ui_fill (x, y + h - 1, w, 1, jet (TJ_LIGNE)); }
	else if (e->kind == K_STATUSBAR)
	{
		ui_fill (x, y, w, h, jet (TJ_CREUX));
		ui_fill (x, y, w, 1, jet (TJ_LIGNE));
		text_box (e->text, x + 8, y, h, jet (TJ_TEXTE1), false, w - 12);
	}
}
static void draw_items (const char *it, int x, int y, int w, int h, int rh)
{
	char t[64]; int k = 0, n = 0;
	for (const char *p = it; ; p++)
	{
		if (*p == '|' || !*p)
		{
			t[k] = 0; k = 0;
			if (y + (n + 1) * rh > h) break;
			text_box (t, x + 8, y + n * rh, rh, jet (TJ_TEXTE1), false, w - 16);
			n++;
			if (!*p) break;
		}
		else if (k < 63) t[k++] = *p;
	}
}
static void draw_element (const El *e, int ox, int oy)
{
	int x = ox + e->x, y = oy + e->y, w = imax (1, e->w), h = imax (1, e->h);
	bool off = e->flag ("disabled");
	unsigned ink = off ? jet (TJ_TEXTE3) : jet (TJ_TEXTE1);
	switch (e->kind)
	{
	case K_LABEL: text_box (e->text, x + 2, y, h, ink, false, w - 2); break;
	case K_BUTTON:
	{
		boxed (x, y, w, h, 6, jet (TJ_LIGNE), jet (TJ_ELEVE), jet (TJ_ELEVE_BAS));
		text_c (e->text, x, y, w, h, ink);
		break;
	}
	case K_TEXTBOX:
	{
		bool ro = off || e->flag ("readonly");		// (le code généré le grise : Enabled = 0)
		unsigned bg = ro ? jet (TJ_FACE) : jet (TJ_SURFACE);
		boxed (x, y, w, h, 5, jet (TJ_LIGNE), bg, bg);
		text_box (e->text, x + 8, y, h, ro ? jet (TJ_TEXTE3) : ink, false, w - 14);
		break;
	}
	case K_CHECKBOX:
		check_mark (x, y + (h - 18) / 2, 18, e->flag ("checked"));
		text_box (e->text, x + 26, y, h, ink, false, w - 26);
		break;
	case K_LISTBOX:
	{
		boxed (x, y, w, h, 5, jet (TJ_LIGNE), jet (TJ_SURFACE), jet (TJ_SURFACE));
		const char *it = e->get ("items");
		ui_clip (imax (0, x + 1), imax (0, y + 1), w - 2, h - 2);
		if (it) draw_items (it, x, y + 3, w, y + h, ui_px () + 9);
		ui_clip (0, 0, 0, 0);
		break;
	}
	case K_DROPDOWN:
	{
		boxed (x, y, w, h, 5, jet (TJ_LIGNE), jet (TJ_ELEVE), jet (TJ_ELEVE_BAS));
		const char *it = e->get ("items"); if (!it) it = e->text;
		char t[64]; int k = 0; while (it[k] && it[k] != '|' && k < 63) { t[k] = it[k]; k++; } t[k] = 0;
		text_box (t, x + 8, y, h, ink, false, w - 30);
		text_box ("\xE2\x96\xBE", x + w - 18, y, h, jet (TJ_TEXTE2));
		break;
	}
	case K_SLIDER:
	{
		int m = imax (1, e->num ("max", 100)), v = imax (0, imin (m, e->num ("value", 0)));
		int th = 16, rail = y + h / 2 - 2, kx = x + (w - th) * v / m;
		ui_rbox (x, rail, w, 4, 2, jet (TJ_CREUX), jet (TJ_CREUX));
		if (kx > x) ui_rbox (x, rail, kx - x + th / 2, 4, 2, jet (TJ_ACCENT), jet (TJ_ACCENT));
		boxed (kx, y + h / 2 - th / 2, th, th, th / 2, jet (TJ_LIGNE_FORTE), jet (TJ_ELEVE), jet (TJ_ELEVE));
		break;
	}
	case K_PROGRESS:
	{
		int v = imax (0, imin (100, e->num ("value", 0)));
		ui_rbox (x, y, w, h, 4, jet (TJ_CREUX), jet (TJ_CREUX));
		if (v) ui_rbox (x, y, imax (6, w * v / 100), h, 4, jet (TJ_ACCENT), jet (TJ_ACCENT));
		break;
	}
	case K_GROUP:
	{
		int ty = y + 8;
		ui_rbox (x, ty, w, imax (2, h - 8), 6, jet (TJ_LIGNE), jet (TJ_LIGNE));
		ui_rbox (x + 1, ty + 1, w - 2, imax (1, h - 10), 5, jet (TJ_FACE), jet (TJ_FACE));
		int t = tw (e->text, true);
		ui_fill (x + 10, y, t + 8, 16, jet (TJ_FACE));
		text_box (e->text, x + 14, y, 16, ink, true, w - 24);
		break;
	}
	case K_MENU: case K_TOOLBAR: case K_STATUSBAR: case K_HOST: draw_strip (e, x, y, w, h); break;
	default: break;
	}
	if (e->kind != K_MENU) for (int i = 0; i < e->kids.n; i++) if (e->kids[i]->kind != K_COMMENT) draw_element (e->kids[i], ox, oy);
}

// ---- par-dessus : les boîtes, le choix, le glisser ----------------------------------------------------------------------
static void draw_tag (int x, int y, const char *s, unsigned bg)
{
	int w = tw (s, true) + 8, h = ui_px () + 6;
	ui_fill (x, y, w, h, bg);
	text_box (s, x + 4, y, h, ink_on (bg), true);
}
static void draw_boxes (const El *e, int ox, int oy, const El *sel)
{
	if (e->kind == K_COMMENT || e->kind == K_MENU) return;
	unsigned bp = jet (TJ_ACCENT), face = jet (TJ_FACE);
	if (is_container (e->kind) && e->kind != K_WINDOW && e->w > 0)
	{
		dashed_rect (ox + e->x, oy + e->y, e->w, e->h, mix (bp, face, 60));
		// son nom : celui du conteneur de l'élément choisi seulement (les autres cacheraient les contrôles)
		if (e != sel && sel && sel->parent == e) { char d[96]; describe (e, d, sizeof d); draw_tag (ox + e->x, oy + e->y - ui_px () - 5, d, mix (bp, face, 70)); }
	}
	if (e->kind == K_SPACER && e->h > 4)
	{
		dashed_rect (ox + e->x + 2, oy + e->y + 2, e->w - 4, e->h - 4, mix (bp, face, 120));
		if (e->h > 20 && e == sel) { const char *t = "Spacer: the free room"; int w = tw (t, true) + 8; draw_tag (ox + e->x + (e->w - w) / 2, oy + e->y + (e->h - ui_px () - 6) / 2, t, mix (bp, face, 90)); }
	}
	for (int i = 0; i < e->kids.n; i++) draw_boxes (e->kids[i], ox, oy, sel);
}
static void des_grid (int ox, int oy, int W, int H)
{
	long pw = 0, ph = 0;
	uint32_t *p = uikit_canvas_pixels (D.view, &pw, &ph);
	if (!p) return;
	unsigned dot = mix (jet (TJ_FACE), jet (TJ_TEXTE1), 38), strong = mix (jet (TJ_FACE), jet (TJ_TEXTE1), 80);
	for (int y = 0; y < H; y += GRID)
	{
		int vy = oy + y; if (vy < 0 || vy >= ph) continue;
		for (int x = 0; x < W; x += GRID)
		{
			int vx = ox + x; if (vx < 0 || vx >= pw) continue;
			p[vy * pw + vx] = x % (GRID * 10) == 0 && y % (GRID * 10) == 0 ? strong : dot;
		}
	}
}
static long des_paint (long, long vw, long vh)
{
	unsigned desk = mix (jet (TJ_FACE), jet (TJ_TEXTE1), 20);
	ui_fill (0, 0, vw, vh, desk);
	long pw = 0, ph = 0;
	uint32_t *pix = uikit_canvas_pixels (D.view, &pw, &ph);
	if (pix)
	{
		unsigned dot = mix (desk, jet (TJ_TEXTE1), 50);
		for (int y = 8; y < ph; y += 16) for (int x = 8; x < pw; x += 16) pix[y * pw + x] = dot;
	}
	if (!D.form || !D.form->root) { text_c (TR ("The form has errors: see the text"), 0, 0, (int) vw, (int) vh, mix (desk, jet (TJ_TEXTE1), 150)); return 0; }
	des_relay ();
	int ox = D.ox, oy = D.oy, W = D.winW, H = D.winH + menu_h ();
	// la fenêtre : son ombre, son cadre, son titre
	unsigned edge = jet (TJ_LIGNE_FORTE);
	for (int i = 3; i >= 1; i--) ui_fill (ox - 1 - i + 2, oy - TITLE_H - 1 - i + 3, W + 2 + 2 * i - 2, H + TITLE_H + 2 + 2 * i - 2, mix (desk, jet (TJ_TEXTE1), 14 * (4 - i)));
	ui_fill (ox - 1, oy - TITLE_H - 1, W + 2, H + TITLE_H + 2, edge);
	unsigned tb = jet (TJ_TITRE_ACTIF);
	ui_fill (ox, oy - TITLE_H, W, TITLE_H, tb);
	const char *t = D.form->root->hasText ? D.form->root->text : D.form->root->name;
	text_c (t, ox + 8, oy - TITLE_H, W - 16, TITLE_H, ink_on (tb), true);
	char sz[32]; snprintf (sz, sizeof sz, "%d x %d", D.winW, D.winH);
	text_box (sz, ox + W - tw (sz), oy + H + 6, 18, mix (desk, jet (TJ_TEXTE1), 150));
	// la zone client
	ui_fill (ox, oy, W, H, jet (TJ_FACE));
	if (g_showGrid) des_grid (ox, oy, W, H);
	draw_element (D.form->root, ox, oy);
	// les boîtes, le choix
	draw_boxes (D.form->root, ox, oy, D.sel);
	unsigned bp = jet (TJ_ACCENT), hd = jet (TJ_SURFACE);
	El *s = D.sel;
	if (s)
	{
		int x, y, w, h;
		if (s == D.form->root) { x = ox; y = oy - TITLE_H; w = D.winW; h = D.winH + menu_h () + TITLE_H; }
		else { x = ox + s->x; y = oy + s->y; w = imax (4, s->w); h = imax (4, s->h); }
		ui_fill (x - 1, y - 1, w + 2, 1, bp); ui_fill (x - 1, y + h, w + 2, 1, bp);
		ui_fill (x - 1, y - 1, 1, h + 2, bp); ui_fill (x + w, y - 1, 1, h + 2, bp);
		int hx[8] = { x, x + w / 2, x + w, x + w, x + w, x + w / 2, x, x }, hy[8] = { y, y, y, y + h / 2, y + h, y + h, y + h, y + h / 2 };
		for (int i = 0; i < 8; i++) { ui_fill (hx[i] - 4, hy[i] - 4, 9, 9, bp); ui_fill (hx[i] - 3, hy[i] - 3, 7, 7, hd); }
		char d[96]; describe (s, d, sizeof d);
		int ty = y + h + 4; if (s == D.form->root) ty = y + h + 26;
		draw_tag (x + w - tw (d, true) - 8, imin (ty, (int) vh - ui_px () - 6), d, bp);
	}
	if (D.dropOk && (D.drag == DR_MOVE || D.newKind >= 0))
	{
		unsigned c = jet (TJ_DANGER);
		int *L = D.dropLine;
		ui_fill (ox + L[0], oy + L[1], L[2], L[3], c);
		if (L[2] > L[3]) { ui_fill (ox + L[0] - 3, oy + L[1] - 3, 6, 8, c); ui_fill (ox + L[0] + L[2] - 3, oy + L[1] - 3, 6, 8, c); }
		else { ui_fill (ox + L[0] - 3, oy + L[1] - 3, 8, 6, c); ui_fill (ox + L[0] - 3, oy + L[1] + L[3] - 3, 8, 6, c); }
	}
	if (D.newKind >= 0 && D.hoverX >= 0)
	{
		const char *n = KIND_NAMES[D.newKind];
		int w = tw (n) + 24, h = ui_px () + 12, x = D.hoverX + 8, y = D.hoverY - h / 2;
		boxed (x, y, w, h, 3, bp, jet (TJ_SURFACE), jet (TJ_SURFACE));
		text_box (n, x + 12, y, h, jet (TJ_TEXTE1));
	}
	return 0;
}

// ---- trouver ----------------------------------------------------------------------------------------------------------
// L'élément le plus profond à un point de la zone client
static El *des_hit (El *e, int cx, int cy)
{
	if (e->kind == K_COMMENT || e->kind == K_MENUTITLE || e->kind == K_MENUITEM || e->kind == K_SEP) return 0;
	bool in = cx >= e->x && cy >= e->y && cx < e->x + e->w && cy < e->y + e->h;
	if (!in && e->kind != K_WINDOW) return 0;
	for (int i = e->kids.n - 1; i >= 0; i--) { El *r = des_hit (e->kids[i], cx, cy); if (r) return r; }
	return in ? e : 0;
}
static El *des_at (int mx, int my)
{
	if (!D.form || !D.form->root) return 0;
	des_relay ();
	int cx = mx - D.ox, cy = my - D.oy;
	if (cx >= 0 && cy >= -TITLE_H && cx < D.winW && cy < 0) return D.form->root;	// (le titre)
	if (cx < 0 || cy < 0 || cx >= D.winW || cy >= D.winH + menu_h ()) return 0;
	return des_hit (D.form->root, cx, cy);
}
// Où va ce qu'on lâche en (mx, my) : son conteneur, son index, le trait qui le montre
static bool des_find_drop (int mx, int my, El *moving, int kind)
{
	D.dropOk = false; D.dropPar = 0;
	if (!D.form || !D.form->root) return false;
	des_relay ();
	El *win = D.form->root;
	int cx = mx - D.ox, cy = my - D.oy;
	int winW = D.winW, winH = D.winH, mh = menu_h ();
	if (cx < 0 || cy < 0 || cx >= winW || cy >= winH + mh) return false;
	if (kind == K_MENU || kind == K_TOOLBAR || kind == K_STATUSBAR)
	{
		if (child_of_kind (win, kind) && (!moving || moving->kind != kind)) return false;
		D.dropPar = win;
		D.dropIdx = kind == K_STATUSBAR ? win->kids.n : kind == K_MENU ? 0 : (child_of_kind (win, K_MENU) ? child_of_kind (win, K_MENU)->index () + 1 : 0);
		int y = kind == K_STATUSBAR ? winH + mh - 2 : kind == K_MENU ? 0 : mh;
		D.dropLine[0] = 0; D.dropLine[1] = y; D.dropLine[2] = winW; D.dropLine[3] = 2;
		D.dropOk = true; return true;
	}
	// le conteneur le plus profond sous le point
	El *c = des_hit (win, cx, cy);
	while (c && (!is_container (c->kind) || (moving && is_inside (c, moving)))) c = c->parent;
	if (!c) return false;
	if (c->kind == K_TOOLBAR && kind != K_BUTTON) c = win;
	D.dropPar = c;
	int pad = pad_of (c);
	if (c->kind == K_CANVAS)
	{
		D.dropIdx = c->kids.n; D.dropAtX = imax (0, snap (cx - c->x - (moving ? D.grabX : 0))); D.dropAtY = imax (0, snap (cy - c->y - (moving ? D.grabY : 0)));
		D.dropLine[0] = cx; D.dropLine[1] = cy; D.dropLine[2] = 8; D.dropLine[3] = 8;
		D.dropOk = true; return true;
	}
	bool across = c->kind == K_ROW || c->kind == K_TOOLBAR;
	bool grid = c->kind == K_GRID;
	D.dropIdx = c->kids.n;
	El *before = 0, *after = 0;
	for (int i = 0; i < c->kids.n; i++)
	{
		El *k = c->kids[i];
		if (k == moving || k->kind == K_COMMENT) continue;
		if (c == win && (k->kind == K_MENU || k->kind == K_TOOLBAR)) { after = k; continue; }
		bool goesBefore;
		if (c == win && k->kind == K_STATUSBAR) goesBefore = true;
		else if (across) goesBefore = cx < k->x + k->w / 2;
		else if (grid) goesBefore = cy < k->y || (cy < k->y + k->h && cx < k->x + k->w / 2);
		else goesBefore = cy < k->y + k->h / 2;
		if (goesBefore) { D.dropIdx = i; before = k; break; }
		after = k;
	}
	// le trait
	if (across || grid)
	{
		int x = before ? before->x - 4 : after ? after->x + after->w + 2 : c->x + pad;
		int y = before ? before->y : after ? after->y : c->y + pad, h = before ? before->h : after ? after->h : imax (20, c->h - 2 * pad);
		D.dropLine[0] = x; D.dropLine[1] = y; D.dropLine[2] = 2; D.dropLine[3] = h;
	}
	else
	{
		int y = before ? before->y - 4 : after ? after->y + after->h + 2 : c->y + pad + (c->kind == K_GROUP ? 18 : 0);
		if (c == win && !before && !after) y = mh + 4;
		D.dropLine[0] = c->x + pad; D.dropLine[1] = y; D.dropLine[2] = imax (10, c->w - 2 * pad); D.dropLine[3] = 2;
	}
	D.dropOk = true; return true;
}
// L'élément mis où des_find_drop l'a dit (moving : retiré d'abord de sa place)
static void des_do_drop (El *e, bool moving)
{
	if (!D.dropOk || !D.dropPar) return;
	if (moving)
	{
		El *op = e->parent; int oi = e->index ();
		if (op) { op->kids.erase (oi); if (op == D.dropPar && oi < D.dropIdx) D.dropIdx--; }
	}
	if (D.dropPar->kind == K_CANVAS) { char t[24]; snprintf (t, sizeof t, "%d,%d", D.dropAtX, D.dropAtY); e->set ("at", t); }
	else e->set ("at", 0);
	if (D.dropPar->kind != K_GRID) e->set ("cell", 0);
	D.dropPar->add (e, D.dropIdx);
	D.sel = e;
	if (g_onDesignHint) { char h[120]; snprintf (h, sizeof h, "%s %s %s", KIND_NAMES[e->kind], e->name, moving ? TR ("moved") : TR ("added")); g_onDesignHint (h); }
	if (g_onFormEdited) g_onFormEdited ();
}
static void des_remove_selected ()
{
	El *s = D.sel;
	if (!s || !D.form || s == D.form->root || !s->parent) return;
	El *p = s->parent; p->kids.erase (s->index ()); delete s;
	D.sel = p;
	if (g_onFormEdited) g_onFormEdited ();
}
// Depuis la boîte à outils : un glisser sur le concepteur (coordonnées du concepteur ; release : lâché)
static void des_tool_drag (int kind, int mx, int my, bool release)
{
	D.newKind = kind;
	bool ok = des_find_drop (mx, my, 0, kind);
	D.hoverX = mx; D.hoverY = my;
	if (release)
	{
		D.newKind = -1; D.hoverX = -1;
		if (ok) { El *e = new_element (*D.form, kind); des_do_drop (e, false); }
		D.dropOk = false;
	}
	else if (g_onDesignHint)
	{
		char h[120];
		if (ok) snprintf (h, sizeof h, "%s -> %s (%d)", KIND_NAMES[kind], KIND_NAMES[D.dropPar->kind], D.dropIdx + 1);
		else snprintf (h, sizeof h, "%s", TR ("Drop it into the window"));
		g_onDesignHint (h);
	}
	des_dirty ();
}
static void des_tool_leave () { D.newKind = -1; D.hoverX = -1; D.dropOk = false; des_dirty (); }
// Un double-clic dans la boîte à outils : ajouté après le choix (ou dans le conteneur choisi)
static void des_tool_add (int kind)
{
	if (!D.form || !D.form->root) return;
	El *win = D.form->root;
	if (kind == K_MENU || kind == K_TOOLBAR || kind == K_STATUSBAR)
	{
		if (child_of_kind (win, kind)) return;
		des_find_drop (D.ox + 2, D.oy + 2, 0, kind);
		El *e = new_element (*D.form, kind); des_do_drop (e, false); return;
	}
	El *s = D.sel ? D.sel : win;
	El *par; int idx;
	if (is_container (s->kind) && s->kind != K_TOOLBAR) { par = s; idx = s->kids.n; if (s == win) { El *sb = child_of_kind (win, K_STATUSBAR); if (sb) idx = sb->index (); } }
	else { par = s->parent ? s->parent : win; idx = s->index () + 1; }
	if (par == win) { El *body = 0; for (int i = 0; i < win->kids.n; i++) if (is_container (win->kids[i]->kind) && win->kids[i]->kind != K_TOOLBAR) body = win->kids[i]; if (body && s == win) { par = body; idx = body->kids.n; } }
	D.dropPar = par; D.dropIdx = idx; D.dropOk = true; D.dropAtX = 8; D.dropAtY = 8;
	El *e = new_element (*D.form, kind); des_do_drop (e, false);
	D.dropOk = false;
}

// ---- la souris, le clavier ----------------------------------------------------------------------------------------------
static long des_mouse (long, long kind, long lx, long ly, long info)
{
	int mx = (int) lx, my = (int) ly, clicks = (int) (info & 0xFF);
	g_alt = ((info >> 16) & UI_MOD_ALT) != 0;
	if (kind == 3 || !D.form || !D.form->root) return 0;		// (le pointeur est sorti)
	des_relay ();
	int W = D.winW, H = D.winH + menu_h ();
	int cx = mx - D.ox, cy = my - D.oy;
	El *s = D.sel;
	bool onCorner = cx >= W - 6 && cx <= W + 8 && cy >= H - 6 && cy <= H + 8;
	// Les poignées de l'élément choisi le dimensionnent -- la fenêtre, un contrôle, un conteneur : la plus proche à
	// HANDLE_R pixels, puis un bord (4 pixels de chaque côté de ses bords droit et bas). Elles passent avant ce qui est
	// dessous : un appui sur une poignée ne choisit jamais un autre élément.
	enum { HANDLE_R = 8 };
	int hfx = 1, hfy = 1; bool onHandle = false;
	bool root = s && s == D.form->root;
	bool sizable = s && (root || (s->parent && s->kind != K_MENU && s->kind != K_MENUTITLE && s->kind != K_MENUITEM && s->kind != K_SEP
				      && s->kind != K_COMMENT && s->kind != K_TOOLBAR && s->kind != K_STATUSBAR));
	if (sizable)
	{
		int x0 = root ? 0 : s->x, y0 = root ? -TITLE_H : s->y, w0 = root ? W : imax (4, s->w), h0 = root ? H + TITLE_H : imax (4, s->h);
		int hx[8] = { x0, x0 + w0 / 2, x0 + w0, x0 + w0, x0 + w0, x0 + w0 / 2, x0, x0 }, hy[8] = { y0, y0, y0, y0 + h0 / 2, y0 + h0, y0 + h0, y0 + h0, y0 + h0 / 2 };
		int best = -1, bd = 0;
		for (int i = 0; i < 8; i++)
		{
			int dx = abs (cx - hx[i]), dy = abs (cy - hy[i]);
			if (dx > HANDLE_R || dy > HANDLE_R) continue;
			if (best < 0 || dx + dy < bd) { best = i; bd = dx + dy; }
		}
		if (best >= 0)
		{
			onHandle = true;
			hfx = hx[best] == x0 ? -1 : hx[best] == x0 + w0 ? 1 : 0;
			hfy = hy[best] == y0 ? -1 : hy[best] == y0 + h0 ? 1 : 0;
		}
		else if (abs (cx - (x0 + w0)) <= 4 && cy > y0 && cy < y0 + h0) { onHandle = true; hfx = 1; hfy = 0; }	// le bord droit
		else if (abs (cy - (y0 + h0)) <= 4 && cx > x0 && cx < x0 + w0) { onHandle = true; hfx = 0; hfy = 1; }	// le bord bas
	}
	bool sizeWin = onHandle && root;
	if (onCorner && !onHandle) { onHandle = true; sizeWin = true; hfx = hfy = 1; }
	if (D.drag == DR_NONE && kind == 0)
		ui_w_cursor (D.view, onHandle ? (!hfy ? UC_HRESIZE : !hfx ? UC_VRESIZE : hfx == hfy ? UC_NWSE : UC_NESW) : UC_ARROW);
	if (kind == 1 && !D.pressed)
	{
		D.pressed = true;
		D.px = mx; D.py = my;
		if (onHandle && sizeWin) { D.drag = DR_SIZE_WIN; D.startW = W; D.startH = D.winH; D.sizeFx = hfx; D.sizeFy = hfy; return 0; }
		if (onHandle) { D.drag = DR_SIZE_EL; D.startW = s->w; D.startH = s->h; D.sizeFx = hfx; D.sizeFy = hfy; return 0; }
		El *e = des_at (mx, my);
		bool dbl = e && e == D.sel && clicks >= 2;
		des_select (e);
		if (e) { D.grabX = cx - e->x; D.grabY = cy - e->y; }
		if (dbl && g_onElementOpen) { D.pressed = false; g_onElementOpen (e); return 0; }
		D.drag = e && e != D.form->root ? DR_PRESS : DR_NONE;
		return 0;
	}
	if (kind == 0 && D.pressed)
	{
		int dx = mx - D.px, dy = my - D.py;
		switch (D.drag)
		{
		case DR_PRESS:
			if (abs (dx) + abs (dy) > 5) D.drag = DR_MOVE;
			else break;
			/* fall through */
		case DR_MOVE:
			des_find_drop (mx, my, D.sel, D.sel->kind);
			if (g_onDesignHint) g_onDesignHint (D.dropOk ? TR ("Drop it: it goes where the line is") : TR ("Not here"));
			des_dirty ();
			break;
		case DR_SIZE_WIN:
		{
			char t[24]; snprintf (t, sizeof t, "%dx%d", imax (120, snap (D.startW + dx * D.sizeFx)), imax (60, snap (D.startH + dy * D.sizeFy)));
			D.form->root->set ("size", t); des_rebuild ();
			break;
		}
		case DR_SIZE_EL:			// un contrôle, un conteneur : sa largeur, sa hauteur, par la poignée tenue
			if (s)
			{
				if (D.sizeFx && abs (dx) > 1) { s->setNum ("width", imax (GRID, snap (D.startW + dx * D.sizeFx))); s->setFlag ("fill", false); }
				if (D.sizeFy && abs (dy) > 1) s->setNum ("height", imax (GRID, snap (D.startH + dy * D.sizeFy)));
				des_rebuild ();
			}
			break;
		}
		return 0;
	}
	if (kind == 2 && D.pressed)
	{
		D.pressed = false;
		int dr = D.drag; D.drag = DR_NONE;
		if (dr == DR_MOVE && D.sel) { if (D.dropOk) des_do_drop (D.sel, true); D.dropOk = false; des_dirty (); }
		else if (dr == DR_SIZE_WIN || dr == DR_SIZE_EL) { if (g_onFormEdited) g_onFormEdited (); }
	}
	return 0;
}
static long des_key (long, long hid, long, long mods)
{
	if (!D.form || !D.form->root) return -1;
	if (hid == KEY_DELETE || hid == KEY_BACKSPACE) { des_remove_selected (); return 0; }
	if (hid == KEY_ESC) { if (D.sel && D.sel->parent) des_select (D.sel->parent); return 0; }
	El *s = D.sel;
	if ((hid == KEY_LEFT || hid == KEY_RIGHT || hid == KEY_UP || hid == KEY_DOWN) && s && s->parent && s->parent->kind == K_CANVAS)
	{
		int step = (mods & UI_MOD_SHIFT) ? 1 : GRID, x = 0, y = 0;
		s->size2 ("at", &x, &y);
		if (hid == KEY_LEFT) x -= step; else if (hid == KEY_RIGHT) x += step; else if (hid == KEY_UP) y -= step; else y += step;
		if (step > 1) { x = x / GRID * GRID; y = y / GRID * GRID; }
		char t[24]; snprintf (t, sizeof t, "%d,%d", imax (0, x), imax (0, y)); s->set ("at", t);
		if (g_onFormEdited) g_onFormEdited ();
		return 0;
	}
	return -1;
}
static long des_wheel (long, long n)
{
	if (g_alt) des_scroll_by ((int) -n * 24, 0); else des_scroll_by (0, (int) -n * 24);
	return 0;
}
static void des_bar (long w, long) { if (w == D.vsb) D.sy = (int) ui_w_get_value (w); else D.sx = (int) ui_w_get_value (w); des_origin (); des_dirty (); }
static void des_create ()
{
	D.newKind = -1; D.hoverX = -1; D.sizeFx = D.sizeFy = 1;
	D.view = ui_view (0, 0, 100, 100, UVW_FOCUS);
	ui_view_on (D.view, (long) des_paint, (long) des_mouse, (long) des_key, (long) des_wheel);
	D.vsb = ui_scrollbar (0, 0, SBW, 10, 0);
	D.hsb = ui_scrollbar (0, 0, 10, SBW, USB_HORIZONTAL);
	ui_w_set_parent (D.vsb, D.view); ui_w_set_parent (D.hsb, D.view);
	ui_w_on (D.vsb, (long) des_bar, 0); ui_w_on (D.hsb, (long) des_bar, 0);
	ui_w_show (D.vsb, 0); ui_w_show (D.hsb, 0);
}

// ---- la boîte à outils ----------------------------------------------------------------------------------------------------
struct ToolItem { int kind; const char *label; };
static const ToolItem TOOLS[] = {
	{ -1, "Layout" }, { K_COLUMN, "Column" }, { K_ROW, "Row" }, { K_GRID, "Grid" }, { K_GROUP, "Group" }, { K_SPACER, "Spacer" }, { K_CANVAS, "Canvas" },
	{ K_HOST, "Host" },
	{ -1, "Controls" }, { K_LABEL, "Label" }, { K_BUTTON, "Button" }, { K_TEXTBOX, "TextBox" }, { K_CHECKBOX, "CheckBox" }, { K_LISTBOX, "ListBox" },
	{ K_DROPDOWN, "DropDown" }, { K_SLIDER, "Slider" }, { K_PROGRESS, "Progress" },
	{ -1, "Window" }, { K_MENU, "Menu" }, { K_TOOLBAR, "ToolBar" }, { K_STATUSBAR, "StatusBar" },
};
static const int NTOOLS = (int) (sizeof TOOLS / sizeof TOOLS[0]);
struct Toolbox { long view; int X, Y, W, H; int hot, down, scroll, pressX, pressY; bool pressed, dragging; };
static Toolbox T;
// Une petite image de chaque genre
static void tool_icon (int kind, int x, int y, unsigned ink, unsigned acc)
{
	unsigned soft = mix (jet (TJ_FACE), ink, 110), field = jet (TJ_SURFACE);
	switch (kind)
	{
	case K_COLUMN: for (int i = 0; i < 3; i++) ui_fill (x + 1, y + 1 + i * 5, 12, 4, i == 1 ? acc : soft); break;
	case K_ROW: for (int i = 0; i < 3; i++) ui_fill (x + 1 + i * 5, y + 1, 4, 12, i == 1 ? acc : soft); break;
	case K_GRID: for (int i = 0; i < 2; i++) for (int j = 0; j < 2; j++) ui_fill (x + 1 + i * 7, y + 1 + j * 7, 5, 5, (i + j) % 2 ? acc : soft); break;
	case K_GROUP: dashed_rect (x + 1, y + 3, 12, 10, ink); ui_fill (x + 3, y + 1, 6, 3, acc); break;
	case K_SPACER: ui_fill (x + 1, y + 7, 12, 1, ink); ui_fill (x + 1, y + 4, 1, 7, ink); ui_fill (x + 12, y + 4, 1, 7, ink); break;
	case K_CANVAS: ui_fill (x + 1, y + 2, 12, 10, soft); ui_fill (x + 3, y + 8, 3, 3, acc); ui_fill (x + 8, y + 4, 3, 3, ink); break;
	case K_HOST: dashed_rect (x + 1, y + 2, 12, 10, ink); ui_fill (x + 4, y + 5, 6, 4, acc); break;
	case K_LABEL: text_box ("A", x + 2, y, 14, ink, true); break;
	case K_BUTTON: boxed (x, y + 3, 14, 9, 3, ink, soft, soft); break;
	case K_TEXTBOX: boxed (x, y + 3, 14, 9, 0, ink, field, field); ui_fill (x + 3, y + 5, 1, 5, ink); break;
	case K_CHECKBOX: check_mark (x + 1, y + 1, 12, true); break;
	case K_LISTBOX: boxed (x, y + 1, 14, 12, 0, ink, field, field); ui_fill (x + 2, y + 3, 10, 2, acc); ui_fill (x + 2, y + 7, 10, 1, ink); ui_fill (x + 2, y + 10, 10, 1, ink); break;
	case K_DROPDOWN: boxed (x, y + 3, 14, 9, 0, ink, field, field); ui_fill (x + 9, y + 6, 3, 1, ink); ui_fill (x + 10, y + 7, 1, 1, ink); break;
	case K_SLIDER: ui_fill (x, y + 7, 14, 2, ink); ui_fill (x + 5, y + 3, 4, 10, acc); break;
	case K_PROGRESS: boxed (x, y + 4, 14, 7, 0, ink, field, field); ui_fill (x + 1, y + 5, 7, 5, acc); break;
	case K_MENU: ui_fill (x, y + 2, 14, 3, ink); ui_fill (x + 2, y + 6, 8, 7, soft); break;
	case K_TOOLBAR: ui_fill (x, y + 3, 14, 8, soft); ui_fill (x + 2, y + 5, 3, 4, ink); ui_fill (x + 7, y + 5, 3, 4, acc); break;
	case K_STATUSBAR: ui_fill (x, y + 9, 14, 4, soft); ui_fill (x + 1, y + 10, 6, 2, ink); break;
	}
}
static int tool_rh () { return ui_px () + 9; }
// La boîte de l'élément i (2 colonnes ; les titres de groupe en travers)
static void tool_place (int i, int *x, int *y, int *w)
{
	int RH = tool_rh (), yy = 4 - T.scroll, col = 0, cw = (T.W - 12) / 2;
	for (int k = 0; k <= i; k++)
	{
		if (TOOLS[k].kind < 0) { if (col) { yy += RH; col = 0; } if (k == i) { *x = 6; *y = yy; *w = T.W - 12; return; } yy += RH; continue; }
		if (k == i) { *x = 6 + col * cw; *y = yy; *w = cw; return; }
		if (++col == 2) { col = 0; yy += RH; }
	}
}
static int tool_at (int mx, int my) { for (int i = 0; i < NTOOLS; i++) { if (TOOLS[i].kind < 0) continue; int x, y, w; tool_place (i, &x, &y, &w); if (mx >= x && mx < x + w && my >= y && my < y + tool_rh ()) return i; } return -1; }
static long tool_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (TJ_FACE));
	int RH = tool_rh ();
	for (int i = 0; i < NTOOLS; i++)
	{
		int x, y, iw; tool_place (i, &x, &y, &iw);
		if (y + RH < 0 || y > h) continue;
		if (TOOLS[i].kind < 0) { text_box (TR (TOOLS[i].label), x, y, RH, jet (TJ_TEXTE2), true); continue; }
		if (i == T.down || i == T.hot) { unsigned c = i == T.down ? jet (TJ_ACCENT_CLAIR) : mix (jet (TJ_FACE), jet (TJ_ACCENT), 40); ui_rbox (x, y + 1, iw - 2, RH - 2, 4, c, c); }
		tool_icon (TOOLS[i].kind, x + 4, y + (RH - 14) / 2, jet (TJ_TEXTE1), jet (TJ_ACCENT));
		text_box (TOOLS[i].label, x + 24, y, RH, jet (TJ_TEXTE1));
	}
	return 0;
}
static inline void tool_to_des (int mx, int my, int *dx, int *dy) { *dx = mx + T.X - D.X; *dy = my + T.Y - D.Y; }
static inline bool tool_in_des (int dx, int dy) { return dx >= 0 && dy >= 0 && dx < D.W && dy < D.H && D.form; }
static long tool_mouse (long, long kind, long lx, long ly, long info)
{
	int mx = (int) lx, my = (int) ly;
	if (kind == 3) { if (T.hot >= 0) { T.hot = -1; ui_view_dirty (T.view); } return 0; }
	bool inside = mx >= 0 && my >= 0 && mx < T.W && my < T.H;
	if (kind == 1 && !T.pressed)
	{
		if (!inside) return 0;
		T.down = tool_at (mx, my); T.pressX = mx; T.pressY = my; T.dragging = false;
		if (T.down >= 0 && (info & 0xFF) >= 2) { des_tool_add (TOOLS[T.down].kind); T.down = -1; }
		else T.pressed = T.down >= 0;
		ui_view_dirty (T.view);
		return 0;
	}
	if (kind == 0 && T.pressed && T.down >= 0)
	{
		if (!T.dragging && abs (mx - T.pressX) + abs (my - T.pressY) > 6) T.dragging = true;
		if (T.dragging) { int dx, dy; tool_to_des (mx, my, &dx, &dy); if (tool_in_des (dx, dy)) des_tool_drag (TOOLS[T.down].kind, dx, dy, false); else des_tool_leave (); }
		return 0;
	}
	if (kind == 2 && T.pressed)
	{
		T.pressed = false;
		if (T.dragging && T.down >= 0) { int dx, dy; tool_to_des (mx, my, &dx, &dy); if (tool_in_des (dx, dy)) des_tool_drag (TOOLS[T.down].kind, dx, dy, true); else des_tool_leave (); }
		T.dragging = false; T.down = -1; ui_view_dirty (T.view);
		return 0;
	}
	int h = inside ? tool_at (mx, my) : -1;
	if (h != T.hot) { T.hot = h; ui_view_dirty (T.view); }
	return 0;
}
static long tool_wheel (long, long n) { T.scroll = imax (0, T.scroll - (int) n * tool_rh ()); ui_view_dirty (T.view); return 0; }
static void tool_create ()
{
	T.hot = T.down = -1;
	T.view = ui_view (0, 0, 100, 100, 0);
	ui_view_on (T.view, (long) tool_paint, (long) tool_mouse, 0, (long) tool_wheel);
}
static void tool_place_view (int x, int y, int w, int h) { T.X = x; T.Y = y; T.W = w; T.H = h; ui_w_move (T.view, x, y, w, h); ui_view_dirty (T.view); }

} // namespace qs

#endif
