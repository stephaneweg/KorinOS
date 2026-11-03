//
// arabesque -- la composition algorithmique par fractales : un chemin calcule (un systeme de fonctions iterees) parcourt une
// image, et la couleur des points visites devient de la musique, jusqu'a dix parties General MIDI. L'idee est celle
// d'Art Song de David T. Strohbeen (1996) ; le programme est ecrit ici, il lit ses formats .ifs, .stl et .sca.
//
// Une fenetre : a gauche l'image et le chemin, dessous la sequence, a droite cinq onglets : Musique (trace, style, gamme,
// tonique), Style (les parties, leur orchestre, leur algorithme), Tracé (les transformations, qu'on tire a la souris sur
// l'image), Gammes, Clavier. Composer joue en composant : on change de gamme ou de style en route.
//
// Fichiers : SD0:/apps/arabesque.app/{traces,styles,default.sca}, et SD0:/home/arabesque pour ce qu'on enregistre.
// Exports : MIDI, FM Song (fmsout.h), WAV, MP3 (audio.h). Moteur sans UIKit : ifs.h, style.h, song.h, compose.h, et les
// images calculees (image.h). Le prereglage .arb (recipe.h, replay.h) garde de quoi recomposer un morceau a l'identique.
//
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <uikit.h>
#include "audio.h"
#include "replay.h"
#include "fmsout.h"

static struct ui_vars ui;
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3, J_ACCENT };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }

#define APPDIR		"SD0:/apps/arabesque.app/"
#define HOMEDIR		"SD0:/home/arabesque"
enum { W_WANT = 784, H_WANT = 540, PANEL_W = 356, TOPBAR = 40, ROLL_H = 62, FOOT = 24, MARGIN = 8, ROW = 32, NFILES = 96, IMG_SIZE = 512 };
enum { T_MUSIC, T_STYLE, T_TRACE, T_SCALES, T_KEYS };
enum { M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_XMID, M_XFMS, M_XWAV, M_XMP3, M_COPYRIGHT, M_QUIT,
       MC_COMPOSE, MC_PLAY, M_STOP, M_REWIND, M_KEEPSEED,
       M_STNEW, M_STOPEN, M_STSAVE, M_STSAVEAS, M_STDIR,
       M_TRNEW, M_TROPEN, M_TRSAVE, M_TRSAVEAS, M_TRDIR,
       M_IMGOPEN, M_IMGGEN, M_MARKER, M_ABOUT, M_PRESETOPEN, M_PRESETSAVE, M_GEN0 };	// M_GEN0 + sorte (image.h)

static const unsigned PART_COL[NPART] = { 0xE0524D, 0xEFA41B, 0x3FB950, 0x2FA8D8, 0x8C6BE8, 0xE85FA8, 0x20B8A6, 0xC8A020, 0x6C8CF0, 0xA8A8A8 };
static const char *const NOTE_FR[12] = { "Do", "Do♯", "Ré", "Mi♭", "Mi", "Fa", "Fa♯", "Sol", "La♭", "La", "Si♭", "Si" };
static const int UNIT_TICKS[9] = { 480, 240, 120, 80, 60, 40, 30, 20, 15 };

// ---- l'etat -----------------------------------------------------------------------------------------------------------
static long g_win, g_tabs, g_status, g_bRewind, g_bCompose, g_bPlay, g_bStop, g_lcd, g_slTempo, g_lTempo;
static long v_img, v_roll, v_trace, v_root, v_parts, v_scale, v_keys;
static long g_lsTrace, g_lsStyle, g_ddScale, g_lRoot;
static long g_segForm, g_fp[2];
static long f_name, f_chan, f_list, f_vol, f_pan, f_start, f_low, f_high, f_voices, f_chord, f_unit, f_rhythm, f_tie, f_den, f_phase, f_range, f_mp, f_md, f_mv, f_ma;
static long t_count, t_spin, t_add, t_del, t_test, t_auto, t_calc, t_sens, t_uni, t_rel, t_coef, t_op[12];
static long s_name, s_list, s_new, s_del, s_up, s_down, s_clear, s_save, s_load;
static long k_oct, k_list, k_vol;
struct Placed { long w; int x, y, ww, hh; };
static Placed g_pl[160]; static int g_npl;

static int g_part, g_selT, g_sens = 5, g_tab;
static bool g_autoLimit = true, g_testBig, g_keepSeed, g_dirty, g_quitNow;
static uint32_t g_seed = 1, g_imgSeed = 3;
static char g_imgKind[16] = "mandala";
static bool g_statusSet;		// un message plus utile que « Prêt » est affiche
static unsigned g_marker = 0xFFFFFF;
static ScaleList g_scales;
static int g_scale, g_root;
static char g_songPath[300], g_stylePath[300], g_tracePath[300], g_styleDir[260] = HOMEDIR, g_traceDir[260] = HOMEDIR;
static uint32_t *g_scaled; static int g_sw, g_sh;
static int g_lastMode = -1, g_keyHeld = -1, g_keyOct = 4, g_keyProg, g_keyVol = 100;
struct FileItem { char path[260]; };
static FileItem g_ftrace[NFILES], g_fstyle[NFILES];
static int g_ntrace, g_nstyle;

static inline const char *base_name (const char *p) { const char *b = strrchr (p, '/'); return b ? b + 1 : p; }
static void status (const char *s) { ui_w_set_text (g_status, (long) s); g_statusSet = true; }
static void lock () { pthread_mutex_lock (&g_lk); }
static void unlock () { pthread_mutex_unlock (&g_lk); }
static bool busy () { return g_mode != M_IDLE; }
static bool is_export () { return g_mode == M_EXPORT; }
static uint32_t horizon () { return (uint32_t) g_tick + LOOKAHEAD; }
static bool ends_with (const char *s, const char *e)
{
	size_t a = strlen (s), b = strlen (e);
	return a >= b && !strcasecmp (s + a - b, e);
}
static void text (const char *s, int x, int base, unsigned c, int px = 12, int bold = 0)
{
	ui_text ((long) s, x, base, c, px, bold ? (long) ui.font_b : (long) ui.font, 0);
}
static void after_dialog ()
{
	const long v[7] = { v_img, v_roll, v_trace, v_root, v_parts, v_scale, v_keys };
	for (int i = 0; i < 7; i++) ui_view_dirty (v[i]);
	ui_w_draw (0);
}

// ---- les pianos ---------------------------------------------------------------------------------------------------------
static const int WHITE_SEMI[7] = { 0, 2, 4, 5, 7, 9, 11 };
static const int BLACK_AFTER[5] = { 0, 1, 3, 4, 5 };		// la blanche a gauche de chaque noire
static const int BLACK_SEMI[5] = { 1, 3, 6, 8, 10 };
// colors[n] : la couleur de chaque touche (0 : naturelle)
static void draw_keys (int w, int h, int octaves, const unsigned *colors)
{
	int nw = 7 * octaves;
	ui_fill (0, 0, w, h, 0x2A3342);
	for (int i = 0; i < nw; i++)
	{
		int x0 = i * w / nw, x1 = (i + 1) * w / nw, s = i / 7 * 12 + WHITE_SEMI[i % 7];
		ui_fill (x0 + 1, 1, x1 - x0 - 1, h - 2, colors[s] ? colors[s] : 0xF4F5F7);
	}
	int bw = w / nw * 3 / 5, bh = h * 6 / 10;
	for (int o = 0; o < octaves; o++)
		for (int b = 0; b < 5; b++)
		{
			int i = o * 7 + BLACK_AFTER[b], cx = (i + 1) * w / nw, s = o * 12 + BLACK_SEMI[b];
			ui_fill (cx - bw / 2, 1, bw, bh, colors[s] ? colors[s] : 0x2A3342);
		}
}
static int key_at (int mx, int my, int w, int h, int octaves)
{
	int nw = 7 * octaves, bw = w / nw * 3 / 5, bh = h * 6 / 10;
	if (my < bh)
		for (int o = 0; o < octaves; o++)
			for (int b = 0; b < 5; b++)
			{
				int cx = (o * 7 + BLACK_AFTER[b] + 1) * w / nw;
				if (mx >= cx - bw / 2 && mx < cx - bw / 2 + bw) return o * 12 + BLACK_SEMI[b];
			}
	int i = mx * nw / w;
	if (i < 0 || i >= nw) return -1;
	return i / 7 * 12 + WHITE_SEMI[i % 7];
}

// ---- la gamme -----------------------------------------------------------------------------------------------------------
static unsigned cur_mask () { return g_scales.n ? g_scales.s[g_scale].mask : 0xAB5; }
static void scale_dropdown ()
{
	ui_list_clear (g_ddScale);
	for (int i = 0; i < g_scales.n; i++) ui_list_add (g_ddScale, (long) g_scales.s[i].name, i);
	ui_w_set_value (g_ddScale, g_scale);
}
static void apply_scale ()
{
	lock ();
	if (g_mode == M_COMPOSE) g_comp.change_scale (cur_mask (), g_root, horizon ());
	else { g_comp.mask = cur_mask (); g_comp.root = g_root; }
	unlock ();
	ui_w_set_text (g_lRoot, (long) NOTE_FR[g_root]);
	ui_view_dirty (v_root);
}
static long root_paint (long, long w, long h)
{
	unsigned col[12]; unsigned m = cur_mask ();
	for (int s = 0; s < 12; s++)
	{
		bool in = m >> ((s - g_root + 12) % 12) & 1;
		col[s] = s == g_root ? jet (J_ACCENT) : in ? (s == 1 || s == 3 || s == 6 || s == 8 || s == 10 ? 0x5F7894 : 0xC7D6E6) : 0;
	}
	draw_keys ((int) w, (int) h, 1, col);
	return 0;
}
static long root_mouse (long, long kind, long x, long y, long info)
{
	if (kind != 1 || !(info >> 8 & 0xFF)) return 0;
	long w = 0, h = 0; uikit_canvas_pixels (v_root, &w, &h);
	int s = key_at ((int) x, (int) y, (int) w, (int) h, 1);
	if (s >= 0) { g_root = s; apply_scale (); }
	return 0;
}
static void scale_list_fill ()
{
	ui_list_clear (s_list);
	for (int i = 0; i < g_scales.n; i++) ui_list_add (s_list, (long) g_scales.s[i].name, i);
	ui_w_set_value (s_list, g_scale);
	if (g_scales.n) ui_w_set_text (s_name, (long) g_scales.s[g_scale].name);
	ui_view_dirty (v_scale);
	scale_dropdown ();
}
static long scale_paint (long, long w, long h)
{
	unsigned col[12]; unsigned m = cur_mask ();
	for (int s = 0; s < 12; s++) col[s] = (m >> s & 1) ? jet (J_ACCENT) : 0;
	draw_keys ((int) w, (int) h, 1, col);
	return 0;
}
static long scale_mouse (long, long kind, long x, long y, long info)
{
	if (kind != 1 || !(info >> 8 & 0xFF) || !g_scales.n) return 0;
	long w = 0, h = 0; uikit_canvas_pixels (v_scale, &w, &h);
	int s = key_at ((int) x, (int) y, (int) w, (int) h, 1);
	if (s >= 0) { g_scales.s[g_scale].mask ^= 1u << s; apply_scale (); ui_view_dirty (v_scale); }
	return 0;
}

// ---- le clavier -----------------------------------------------------------------------------------------------------------
enum { KEY_CH = 15 };
static long keys_paint (long, long w, long h)
{
	unsigned col[24]; memset (col, 0, sizeof col);
	if (g_keyHeld >= 0) col[g_keyHeld] = jet (J_ACCENT);
	draw_keys ((int) w, (int) h, 2, col);
	return 0;
}
static void key_release () { if (g_keyHeld >= 0) { syn (KEY_CH, 0x80, (g_keyOct + 1) * 12 + g_keyHeld, 0); g_keyHeld = -1; ui_view_dirty (v_keys); } }
static long keys_mouse (long, long kind, long x, long y, long info)
{
	int b = (int) (info >> 8) & 0xFF;
	if (kind == 2 || kind == 3 || !b) { key_release (); return 0; }
	long w = 0, h = 0; uikit_canvas_pixels (v_keys, &w, &h);
	int s = key_at ((int) x, (int) y, (int) w, (int) h, 2);
	if (s == g_keyHeld || s < 0) return 0;
	key_release ();
	g_keyHeld = s;
	syn (KEY_CH, 0xC0, g_keyProg, 0); syn (KEY_CH, 0xB0, 7, g_keyVol);
	syn (KEY_CH, 0x90, (g_keyOct + 1) * 12 + s, 100);
	ui_view_dirty (v_keys);
	return 0;
}

// ---- l'image et le trace --------------------------------------------------------------------------------------------------
static void scale_image (int w, int h)
{
	if (g_scaled && g_sw == w && g_sh == h) return;
	free (g_scaled);
	g_scaled = (uint32_t *) malloc (sizeof (uint32_t) * (size_t) w * (size_t) h);
	g_sw = w; g_sh = h;
	lock ();
	for (int y = 0; y < h; y++)
	{
		const uint32_t *s = g_img.px + (long) (y * g_img.h / h) * g_img.w;
		uint32_t *d = g_scaled + (long) y * w;
		for (int x = 0; x < w; x++) d[x] = s[x * g_img.w / w] & 0xFFFFFF;
	}
	unlock ();
}
static void image_changed () { free (g_scaled); g_scaled = 0; ui_view_dirty (v_img); }

// point du trace -> pixel de la vue
static void to_view (double x, double y, int w, int h, double &sx, double &sy)
{
	sx = (x - g_ifs.x0) / (g_ifs.x1 - g_ifs.x0) * w;
	sy = (1 - (y - g_ifs.y0) / (g_ifs.y1 - g_ifs.y0)) * h;
}
static void from_view (double sx, double sy, int w, int h, double &x, double &y)
{
	x = g_ifs.x0 + sx / w * (g_ifs.x1 - g_ifs.x0);
	y = g_ifs.y0 + (1 - sy / h) * (g_ifs.y1 - g_ifs.y0);
}
// les quatre coins d'une transformation : l'image du cadre
static void quad (int i, double *qx, double *qy, int w, int h)
{
	const Xform &m = g_ifs.t[i];
	double px[4] = { g_ifs.x0, g_ifs.x1, g_ifs.x1, g_ifs.x0 }, py[4] = { g_ifs.y0, g_ifs.y0, g_ifs.y1, g_ifs.y1 };
	for (int k = 0; k < 4; k++) to_view (m.a * px[k] + m.b * py[k] + m.e, m.c * px[k] + m.d * py[k] + m.f, w, h, qx[k], qy[k]);
}
static void dots (uint32_t *d, long stride, int w, int h, int count, unsigned color)
{
	Rng r (77);
	double x = 0, y = 0;
	lock ();
	g_ifs.prepare ();
	for (int i = 0; i < 32; i++) g_ifs.step (x, y, r);
	for (int i = 0; i < count; i++)
	{
		g_ifs.step (x, y, r);
		double u, v;
		g_ifs.to_unit (x, y, u, v);
		int px = (int) (u * (w - 1)), py = (int) (v * (h - 1));
		d[py * stride + px] = color;
	}
	unlock ();
}
static long img_paint (long v, long lw, long lh)
{
	int w = (int) lw, h = (int) lh;
	long pw, ph;
	uint32_t *d = uikit_canvas_pixels (v, &pw, &ph);
	if (!d) return 0;
	scale_image (w, h);
	if (g_tab == T_TRACE)
	{
		for (int y = 0; y < h; y++)
		{
			uint32_t *o = d + y * pw; const uint32_t *s = g_scaled + (long) y * w;
			for (int x = 0; x < w; x++) o[x] = (s[x] >> 3) & 0x1F1F1F;
		}
		if (g_selT < g_ifs.n)
		{
			double qx[4], qy[4];
			int pts[8];
			quad (g_selT, qx, qy, w, h);
			for (int k = 0; k < 4; k++) { pts[k * 2] = (int) (qx[k] * 16); pts[k * 2 + 1] = (int) (qy[k] * 16); }
			ui_vp_poly ((long) pts, 4, ui_mix (jet (J_ACCENT), 0x101418, 110));
		}
		dots (d, pw, w, h, g_testBig ? 60000 : 6000, 0xF0F4F8);
		for (int i = 0; i < g_ifs.n; i++)
		{
			double qx[4], qy[4];
			quad (i, qx, qy, w, h);
			unsigned c = i == g_selT ? jet (J_ACCENT) : 0x9AA8B8;
			for (int k = 0; k < 4; k++)
			{
				int n = (k + 1) & 3;
				ui_vp_line ((long) (qx[k] * 16), (long) (qy[k] * 16), (long) (qx[n] * 16), (long) (qy[n] * 16), i == g_selT ? 24 : 14, c);
			}
			if (i == g_selT) for (int k = 0; k < 4; k++) ui_vp_disc ((long) (qx[k] * 16), (long) (qy[k] * 16), 64, 0xFFFFFF);
		}
		return 0;
	}
	for (int y = 0; y < h; y++) memcpy (d + y * pw, g_scaled + (long) y * w, (size_t) w * 4);
	if (g_mode == M_COMPOSE)
	{
		lock ();
		for (int i = 0; i < NPART; i++)
		{
			const Voice &q = g_comp.v[i];
			if (!q.live || !q.tcount) continue;
			for (int k = 0; k < q.tcount; k++)
			{
				int at = (q.thead - 1 - k + TRAIL * 2) % TRAIL;
				long x = (long) (q.tu[at] * w * 16), y = (long) (q.tv[at] * h * 16);
				if (k == 0) { ui_vp_disc (x, y, 72, 0x101418); ui_vp_disc (x, y, 52, g_marker); ui_vp_disc (x, y, 32, PART_COL[i]); }
				else ui_vp_disc (x, y, 32 - k / 2, ui_mix (PART_COL[i], 0xFFFFFF, 256 - k * 4));
			}
		}
		unlock ();
	}
	return 0;
}

// ---- l'edition a la souris ----
static int g_dragMode, g_dragCorner;		// 0 rien, 1 deplacer, 2 un coin
static double g_dragX, g_dragY;
static void trace_tools_refresh ();
static void trace_edited (bool final)
{
	if (final && g_autoLimit) { lock (); g_ifs.limits (); unlock (); }
	g_testBig = false;
	ui_view_dirty (v_trace); ui_view_dirty (v_img);
	trace_tools_refresh ();
}
static bool inside (const double *qx, const double *qy, double x, double y)
{
	bool in = false;
	for (int i = 0, j = 3; i < 4; j = i++)
		if ((qy[i] > y) != (qy[j] > y) && x < (qx[j] - qx[i]) * (y - qy[i]) / (qy[j] - qy[i]) + qx[i]) in = !in;
	return in;
}
static long img_mouse (long, long kind, long mx, long my, long info)
{
	if (g_tab == T_TRACE)
	{
		long w = 0, h = 0; uikit_canvas_pixels (v_img, &w, &h);
		int b = (int) (info >> 8) & 0xFF;
		if (kind == 1 && b)
		{
			g_dragMode = 0;
			if (g_selT < g_ifs.n)
			{
				double qx[4], qy[4];
				quad (g_selT, qx, qy, (int) w, (int) h);
				for (int k = 0; k < 4; k++)
				{
					double dx = qx[k] - mx, dy = qy[k] - my;
					if (dx * dx + dy * dy < 100) { g_dragMode = 2; g_dragCorner = k; }
				}
			}
			if (!g_dragMode)
				for (int i = g_ifs.n - 1; i >= 0; i--)
				{
					double qx[4], qy[4];
					quad (i, qx, qy, (int) w, (int) h);
					if (inside (qx, qy, (double) mx, (double) my)) { g_selT = i; g_dragMode = 1; break; }
				}
			g_dragX = (double) mx; g_dragY = (double) my;
			trace_edited (false);
		}
		else if (kind == 0 && g_dragMode && b)
		{
			lock ();
			Xform &m = g_ifs.t[g_selT];
			double x, y, ox, oy;
			from_view ((double) mx, (double) my, (int) w, (int) h, x, y);
			from_view (g_dragX, g_dragY, (int) w, (int) h, ox, oy);
			if (g_dragMode == 1) { m.e += x - ox; m.f += y - oy; }
			else
			{
				// un coin : tourne et agrandit autour du coin oppose, qui ne bouge pas
				double px[4] = { g_ifs.x0, g_ifs.x1, g_ifs.x1, g_ifs.x0 }, py[4] = { g_ifs.y0, g_ifs.y0, g_ifs.y1, g_ifs.y1 };
				int o = (g_dragCorner + 2) & 3;
				double ax = m.a * px[o] + m.b * py[o] + m.e, ay = m.c * px[o] + m.d * py[o] + m.f;
				double vx = ox - ax, vy = oy - ay, ux = x - ax, uy = y - ay, n = vx * vx + vy * vy;
				if (n > 1e-12)
				{
					double re = (ux * vx + uy * vy) / n, im = (uy * vx - ux * vy) / n;
					double a = re * m.a - im * m.c, b2 = re * m.b - im * m.d, c = im * m.a + re * m.c, d = im * m.b + re * m.d;
					m.a = a; m.b = b2; m.c = c; m.d = d;
					m.e = ax - (m.a * px[o] + m.b * py[o]); m.f = ay - (m.c * px[o] + m.d * py[o]);
				}
			}
			g_dragX = (double) mx; g_dragY = (double) my;
			unlock ();
			trace_edited (false);
		}
		else if (kind == 2 && g_dragMode) { g_dragMode = 0; trace_edited (true); }
	}
	return 0;
}
static long mini_paint (long v, long lw, long lh)
{
	ui_fill (0, 0, lw, lh, jet (J_CREUX));
	long pw, ph; uint32_t *d = uikit_canvas_pixels (v, &pw, &ph);
	if (d && g_ifs.n) dots (d, pw, (int) lw, (int) lh, 3500, jet (J_ACCENT));
	return 0;
}

// ---- la sequence ----------------------------------------------------------------------------------------------------------
static uint32_t roll_span () { uint32_t e = g_song.length (), m = 16 * PPQ * BEATS; return e > m ? e + PPQ * BEATS : m; }
static long roll_paint (long v, long lw, long lh)
{
	int w = (int) lw, h = (int) lh;
	ui_fill (0, 0, w, h, jet (J_CREUX));
	long pw, ph; uint32_t *d = uikit_canvas_pixels (v, &pw, &ph);
	if (!d) return 0;
	lock ();
	uint32_t span = roll_span ();
	unsigned bar = jet (J_LIGNE);
	for (uint32_t b = 0; b < span; b += PPQ * BEATS * 4) { int x = (int) ((uint64_t) b * w / span); for (int y = 0; y < h; y++) d[y * pw + x] = bar; }
	for (int i = 0; i < NPART; i++)
	{
		const Track &t = g_song.tr[i];
		unsigned c = PART_COL[i];
		for (int k = 0; k < t.count; k++)
		{
			const Note &m = t.n[k];
			if (m.kind != NK_NOTE) continue;
			int x0 = (int) ((uint64_t) m.tick * w / span), x1 = (int) ((uint64_t) (m.tick + m.dur) * w / span);
			int y = h - 2 - (m.key > 24 ? (m.key < 108 ? m.key - 24 : 83) : 0) * (h - 3) / 84;
			if (x1 >= w) x1 = w - 1;
			uint32_t *o = d + (long) y * pw;
			for (int x = x0; x <= x1; x++) o[x] = c;
		}
	}
	int cx = (int) ((uint64_t) (uint32_t) g_tick * w / span);
	if (cx < w) for (int y = 0; y < h; y++) d[y * pw + cx] = jet (J_TEXTE1);
	unlock ();
	return 0;
}
static long roll_mouse (long, long kind, long mx, long, long info)
{
	if ((kind != 1 && kind != 0) || !(info >> 8 & 0xFF) || g_mode == M_COMPOSE || is_export ()) return 0;
	long w = 0, h = 0; uikit_canvas_pixels (v_roll, &w, &h);
	if (w <= 0 || mx < 0) return 0;
	lock (); uint32_t span = roll_span (); unlock ();
	cmd_seek ((uint32_t) ((uint64_t) mx * span / (uint64_t) w));
	ui_view_dirty (v_roll);
	return 0;
}

// ---- les parties ----------------------------------------------------------------------------------------------------------
static const int PR_H = 17;
static int g_dragPart = -1;
static void form_load ();
static long parts_paint (long, long lw, long lh)
{
	ui_fill (0, 0, lw, lh, jet (J_SURFACE));
	for (int i = 0; i < NPART; i++)
	{
		int y = i * PR_H;
		const Part &p = g_style.p[i];
		if (i == g_part) ui_fill (0, y, lw, PR_H, ui_mix (jet (J_ACCENT), jet (J_SURFACE), 60));
		char t[8]; snprintf (t, sizeof t, "%d", i + 1);
		ui_fill (4, y + 3, 22, PR_H - 6, p.on ? PART_COL[i] : jet (J_CREUX));
		text (t, 11 - (i == 9 ? 3 : 0), y + 14, p.on ? 0xFFFFFF : jet (J_TEXTE3), 11, 1);
		ui_fill (30, y + 3, 16, PR_H - 6, p.mute ? 0xE0524D : jet (J_CREUX));
		text ("M", 34, y + 14, p.mute ? 0xFFFFFF : jet (J_TEXTE3), 10);
		const char *nm = p.name[0] ? p.name : p.chan == 9 ? "Percussions" : GM_NAMES[p.patch & 127];
		ui_text_fit ((long) nm, 52, y + 14, p.on ? jet (J_TEXTE1) : jet (J_TEXTE3), 12, (long) ui.font, lw - 110);
		snprintf (t, sizeof t, "%d", g_song.tr[i].notes ());
		text (t, (int) lw - 52, y + 14, jet (J_TEXTE3), 11);
	}
	return 0;
}
static void part_copy (int from, int to)
{
	if (from == to) return;
	lock (); bool on = g_style.p[to].on; g_style.p[to] = g_style.p[from]; g_style.p[to].on = on; unlock ();
	g_part = to;
	form_load ();
}
static long parts_mouse (long, long kind, long mx, long my, long info)
{
	int row = (int) (my / PR_H);
	row = row < 0 ? 0 : row >= NPART ? NPART - 1 : row;
	if (kind == 1 && (info >> 8 & 0xFF))
	{
		int clicks = (int) info & 0xFF;
		if (mx < 28)
		{
			if (clicks >= 2)
			{
				lock ();
				if (g_mode == M_COMPOSE) g_comp.clear_track (row); else g_song.tr[row].clear ();
				g_style.p[row].on = !g_style.p[row].on;
				unlock ();
				status ("Partie effacée");
			}
			g_dragPart = row;
			lock (); g_style.p[row].on = !g_style.p[row].on; unlock ();
		}
		else if (mx < 48)
		{
			lock ();
			Part &p = g_style.p[row];
			p.mute = !p.mute;
			syn (p.chan, 0xB0, 7, p.mute ? 0 : p.vol);
			unlock ();
		}
		g_part = row;
		form_load ();
	}
	else if (kind == 2 && g_dragPart >= 0) { part_copy (g_dragPart, row); g_dragPart = -1; }
	ui_view_dirty (v_parts);
	return 0;
}
static void form_load ()
{
	const Part &p = g_style.p[g_part];
	ui_w_set_text (f_name, (long) p.name);
	ui_w_set_value (f_chan, p.chan + 1);
	ui_w_set_value (f_list, p.patch);
	ui_w_set_value (f_vol, p.vol); ui_w_set_value (f_pan, p.pan);
	ui_w_set_value (f_start, p.start); ui_w_set_value (f_low, p.low); ui_w_set_value (f_high, p.high);
	ui_w_set_value (f_voices, p.voices); ui_w_set_value (f_chord, p.chord);
	int u = 0;
	for (int i = 0; i < 9; i++) { int a = UNIT_TICKS[i] - p.unit, b = UNIT_TICKS[u] - p.unit; if ((a < 0 ? -a : a) < (b < 0 ? -b : b)) u = i; }
	ui_w_set_value (f_unit, u);
	ui_w_set_text (f_rhythm, (long) p.rhythm);
	ui_w_set_value (f_tie, p.tie); ui_w_set_value (f_den, p.density); ui_w_set_value (f_phase, p.phase);
	ui_w_set_value (f_range, p.range);
	ui_w_set_value (f_mp, p.mapP); ui_w_set_value (f_md, p.mapD); ui_w_set_value (f_mv, p.mapV); ui_w_set_value (f_ma, p.mapA);
	ui_view_dirty (v_parts);
	if (ui_w_get_value (g_tabs) == T_STYLE) ui_w_draw (0);
}
// un champ du formulaire a change -> true s'il etait a nous
static bool form_change (long w)
{
	Part &p = g_style.p[g_part];
	int v = (int) ui_w_get_value (w);
	lock ();
	bool hit = true;
	if (w == f_name) { strncpy (p.name, (const char *) ui_w_get_text (w), sizeof p.name - 1); p.name[sizeof p.name - 1] = 0; }
	else if (w == f_chan) { p.chan = v - 1; syn (p.chan, 0xC0, p.patch, 0); }
	else if (w == f_list) { p.patch = v; syn (p.chan, 0xC0, v, 0); song_retouch (g_song, g_part, NK_PROGRAM, v, 0); }
	else if (w == f_vol) { p.vol = v; syn (p.chan, 0xB0, 7, p.mute ? 0 : v); song_retouch (g_song, g_part, NK_CC, 7, v); }
	else if (w == f_pan) { p.pan = v; syn (p.chan, 0xB0, 10, v); song_retouch (g_song, g_part, NK_CC, 10, v); }
	else if (w == f_start) p.start = v;
	else if (w == f_low) { p.low = v; if (p.high < v) p.high = v; }
	else if (w == f_high) { p.high = v; if (p.low > v) p.low = v; }
	else if (w == f_voices) p.voices = v;
	else if (w == f_chord) p.chord = v;
	else if (w == f_unit) p.unit = UNIT_TICKS[v & 15];
	else if (w == f_rhythm) { strncpy (p.rhythm, (const char *) ui_w_get_text (w), sizeof p.rhythm - 1); p.rhythm[sizeof p.rhythm - 1] = 0; }
	else if (w == f_tie) p.tie = v != 0;
	else if (w == f_den) p.density = v;
	else if (w == f_phase) p.phase = v;
	else if (w == f_range) p.range = v;
	else if (w == f_mp) p.mapP = v;
	else if (w == f_md) p.mapD = v;
	else if (w == f_mv) p.mapV = v;
	else if (w == f_ma) p.mapA = v;
	else hit = false;
	unlock ();
	if (hit) ui_view_dirty (v_parts);
	return hit;
}

// ---- les outils du trace ------------------------------------------------------------------------------------------------
static void trace_tools_refresh ()
{
	char t[200];
	if (g_selT >= g_ifs.n) g_selT = g_ifs.n ? g_ifs.n - 1 : 0;
	snprintf (t, sizeof t, "Transformations : %d", g_ifs.n);
	ui_w_set_text (t_count, (long) t);
	ui_w_set_range (t_spin, 1, g_ifs.n > 0 ? g_ifs.n : 1, 1, 1);
	ui_w_set_value (t_spin, g_selT + 1);
	if (g_ifs.n)
	{
		const Xform &m = g_ifs.t[g_selT];
		snprintf (t, sizeof t, "a %.3f  b %.3f  c %.3f  d %.3f\ne %.2f  f %.2f  p %.3f", m.a, m.b, m.c, m.d, m.e, m.f, m.p);
		ui_w_set_text (t_coef, (long) t);
	}
}
static void trace_op (int op)
{
	if (!g_ifs.n) return;
	lock ();
	Xform &m = g_ifs.t[g_selT];
	double sign = op & 1 ? 1 : -1;
	bool axisY = (op & 3) >= 2;
	int kind = op / 4;				// 0 tourner, 1 taille, 2 deplacer
	if (kind == 0)
	{
		double r = sign * g_sens * 3.14159265 / 180, cs = cos (r), sn = sin (r);
		if (!axisY) { double a = m.a * cs - m.c * sn, c = m.a * sn + m.c * cs; m.a = a; m.c = c; }
		else { double b = m.b * cs - m.d * sn, d = m.b * sn + m.d * cs; m.b = b; m.d = d; }
	}
	else if (kind == 1)
	{
		double f = 1 + sign * g_sens / 100.0;
		if (!axisY) { m.a *= f; m.c *= f; } else { m.b *= f; m.d *= f; }
	}
	else
	{
		if (!axisY) m.e += sign * g_sens / 100.0 * (g_ifs.x1 - g_ifs.x0);
		else m.f += sign * g_sens / 100.0 * (g_ifs.y1 - g_ifs.y0);
	}
	unlock ();
	trace_edited (true);
}

// ---- les listes de fichiers ------------------------------------------------------------------------------------------------
static int scan_dir (const char *dir, const char *ext, FileItem *out, int n, long list)
{
	DIR *d = opendir (dir);
	if (!d) return n;
	while (struct dirent *e = readdir (d))
	{
		if (n >= NFILES || !ends_with (e->d_name, ext)) continue;
		snprintf (out[n].path, sizeof out[n].path, "%s/%s", dir, e->d_name);
		char nm[64]; snprintf (nm, sizeof nm, "%s", e->d_name);
		char *dot = strrchr (nm, '.'); if (dot) *dot = 0;
		ui_list_add (list, (long) nm, n);
		n++;
	}
	closedir (d);
	return n;
}
static void fill_lists ()
{
	ui_list_clear (g_lsTrace); ui_list_clear (g_lsStyle);
	g_ntrace = scan_dir (APPDIR "traces", ".ifs", g_ftrace, 0, g_lsTrace);
	g_ntrace = scan_dir (g_traceDir, ".ifs", g_ftrace, g_ntrace, g_lsTrace);
	g_nstyle = scan_dir (APPDIR "styles", ".stl", g_fstyle, 0, g_lsStyle);
	g_nstyle = scan_dir (g_styleDir, ".stl", g_fstyle, g_nstyle, g_lsStyle);
}
static bool load_trace (const char *path)
{
	static Ifs t;
	if (!t.load (path)) { ui_message ((long) "Tracé", (long) "Ce fichier n'est pas un code IFS.", UM_OK); after_dialog (); return false; }
	lock (); g_ifs = t; unlock ();
	if (!g_ifs.name[0]) snprintf (g_ifs.name, sizeof g_ifs.name, "%s", base_name (path));
	snprintf (g_tracePath, sizeof g_tracePath, "%s", path);
	g_selT = 0;
	trace_edited (false);
	return true;
}
static bool load_style (const char *path)
{
	static Style s;
	if (!s.load (path)) return false;
	snprintf (s.name, sizeof s.name, "%s", base_name (path));
	lock ();
	if (g_mode == M_COMPOSE) g_comp.change_style (s, horizon ());
	else g_style = s;
	unlock ();
	snprintf (g_stylePath, sizeof g_stylePath, "%s", path);
	if (g_mode == M_COMPOSE) status ("Style suivant : à la prochaine mesure");
	else form_load ();
	ui_view_dirty (v_parts);
	return true;
}
// les octets d'un fichier image -> l'image (aussi pour relire celle d'un prereglage : replay.h)
static bool decode_image (const unsigned char *data, long size, Image &out)
{
	long im = ui_image_decode ((long) data, size);
	if (!im) return false;
	long h = 0, px = 0, w = uikit_image_size (im, &h, &px);
	image_alloc (out, (int) w, (int) h);
	const uint32_t *s = (const uint32_t *) px;
	for (long i = 0; i < w * h; i++) out.px[i] = s[i] & 0xFFFFFF;
	ui_image_free (im);
	return true;
}
// une image prete prend la place de l'image (elle est videe)
static void image_set (Image &im)
{
	lock ();
	image_free (g_img);
	g_img = im;
	im.px = 0; im.data = 0;
	unlock ();
	image_changed ();
}
static void load_image (const char *path)
{
	long size = 0;
	char *data = read_all (path, &size);
	Image im;
	if (!data || !decode_image ((const unsigned char *) data, size, im)) { free (data); ui_message ((long) "Image", (long) "Cette image ne se lit pas (BMP ou PNG).", UM_OK); after_dialog (); return; }
	image_source_file (im, base_name (path), data, size);
	free (data);
	image_set (im);
}
// une image calculee (image.h) : calculee hors du verrou, la musique continue
static void generate (const char *kind, uint32_t seed)
{
	snprintf (g_imgKind, sizeof g_imgKind, "%s", kind); g_imgSeed = seed;
	int k = img_kind_index (kind);
	char t[96]; snprintf (t, sizeof t, "Calcul de l'image : %s n° %u...", IMG_NAMES[k < 0 ? 0 : k], seed);
	status (t); ui_w_draw (g_status); ui_view_flush ();
	Image im;
	image_make (im, kind, IMG_SIZE, IMG_SIZE, seed);
	image_set (im);
	snprintf (t, sizeof t, "Image : %s n° %u (F9 : une autre)", IMG_NAMES[k < 0 ? 0 : k], seed);
	status (t);
}
static void song_load (const char *path)
{
	cmd_stop ();
	lock ();
	bool ok = song_load_midi (g_song, g_style, path);
	g_tick = 0;
	unlock ();
	if (!ok) { ui_message ((long) "Morceau", (long) "Ce fichier MIDI ne se lit pas.", UM_OK); after_dialog (); return; }
	snprintf (g_songPath, sizeof g_songPath, "%s", path);
	lock (); delete g_recipe; g_recipe = 0; unlock ();
	g_dirty = false;
	ui_w_set_value (g_slTempo, g_song.tempo);
	form_load ();
	ui_view_dirty (v_roll);
}
static bool confirm ();
static void preset_load (const char *path);
static void open_any (const char *path)
{
	if (ends_with (path, ".ifs")) load_trace (path);
	else if (ends_with (path, ".stl")) load_style (path);
	else if (ends_with (path, ".sca")) { if (g_scales.load (path)) { g_scale = 0; scale_list_fill (); apply_scale (); } }
	else if (ends_with (path, ".mid") || ends_with (path, ".midi")) song_load (path);
	else if (ends_with (path, ".arb")) { if (confirm ()) preset_load (path); }
	else load_image (path);
}

// ---- les commandes -----------------------------------------------------------------------------------------------------
static bool ask_save (const char *dir, const char *name, const char *filter, char *out, int cap)
{
	ui_file_filter ((long) filter);
	long r = ui_file_save ((long) dir, (long) name, (long) out, cap);
	ui_file_filter (0);
	after_dialog ();
	return r == 1;
}
static bool ask_open (const char *dir, const char *filter, char *out, int cap)
{
	ui_file_filter ((long) filter);
	long r = ui_file_open ((long) dir, (long) out, cap);
	ui_file_filter (0);
	after_dialog ();
	return r == 1;
}
static void msg (const char *title, const char *s) { ui_message ((long) title, (long) s, UM_OK); after_dialog (); }

static bool song_save_as ()
{
	char p[300];
	if (!ask_save (HOMEDIR, g_songPath[0] ? base_name (g_songPath) : "arabesque.mid", "*.mid", p, sizeof p)) return false;
	lock (); bool ok = song_save_midi (g_song, g_style, p); unlock ();
	if (!ok) { msg ("Enregistrer", "Impossible d'écrire le fichier."); return false; }
	snprintf (g_songPath, sizeof g_songPath, "%s", p);
	g_dirty = false;
	return true;
}
static bool song_save ()
{
	if (!g_songPath[0]) return song_save_as ();
	lock (); bool ok = song_save_midi (g_song, g_style, g_songPath); unlock ();
	if (!ok) { msg ("Enregistrer", "Impossible d'écrire le fichier."); return false; }
	g_dirty = false;
	return true;
}
// des notes a garder ? -> false : la commande est abandonnee
static bool confirm ()
{
	if (!g_dirty) return true;
	long r = ui_message ((long) "Morceau", (long) "Enregistrer le morceau composé ?", UM_YESNOCANCEL);
	after_dialog ();
	if (r == 0) return false;
	if (r == 1) return song_save ();
	return true;
}
static bool song_ready ()
{
	if (is_export ()) { msg ("Exporter", "Un export est déjà en cours."); return false; }
	if (!g_song.empty ()) return true;
	msg ("Exporter", "Composez d'abord un morceau.");
	return false;
}
static void export_fms ()
{
	if (!song_ready ()) return;
	char p[300];
	if (!ask_save (HOMEDIR, "arabesque.fms", "*.fms", p, sizeof p)) return;
	cmd_stop ();
	FmsResult r;
	char title[32]; snprintf (title, sizeof title, "%s", base_name (p));
	char *dot = strrchr (title, '.'); if (dot) *dot = 0;
	lock (); bool ok = fms_export (g_song, g_style, p, title, r); unlock ();
	if (!ok) { msg ("FM Song", "Impossible d'écrire le fichier."); return; }
	char t[400]; int n = snprintf (t, sizeof t, "%d notes sur 8 canaux FM, %d motifs.", r.notes, r.patterns);
	if (r.dropped) n += snprintf (t + n, sizeof t - (size_t) n, "\n%d notes en trop perdues (une note à la fois par canal).", r.dropped);
	if (r.parts > 8) n += snprintf (t + n, sizeof t - (size_t) n, "\nSeules les huit premières parties sont gardées.");
	if (r.truncated) n += snprintf (t + n, sizeof t - (size_t) n, "\nLe morceau est coupé : il dépasse 64 motifs.");
	if (r.regular && (int) (r.tempo + 0.5) != g_song.tempo) n += snprintf (t + n, sizeof t - (size_t) n, "\nTempo ramené à %d (une ligne dure un multiple de 50 ms).", (int) (r.tempo + 0.5));
	if (!r.regular) n += snprintf (t + n, sizeof t - (size_t) n, "\nNotes placées à 25 ms près (grille de 50 ms).");
	snprintf (t + n, sizeof t - (size_t) n, "\nLes nuances ne passent pas en FM Song.");
	msg ("FM Song", t);
}
static void export_audio (int format, const char *name, const char *filter, int bits)
{
	if (!song_ready ()) return;
	char p[300];
	if (!ask_save (HOMEDIR, name, filter, p, sizeof p)) return;
	if (!g_synthOk) { msg ("Exporter", "Les sons se chargent encore : réessayez dans un instant."); return; }
	cmd_export (p, format, bits);
}
static void on_compose ()
{
	if (is_export ()) return;
	if (g_mode == M_COMPOSE) { cmd_stop (); status ("Composition arrêtée. Le morceau peut s'enregistrer ou s'exporter."); return; }
	if (!g_keepSeed) g_seed = (uint32_t) time (0) * 2654435761u ^ (uint32_t) clock ();
	bool any = false;
	for (int i = 0; i < NPART; i++) if (g_style.p[i].on) any = true;
	if (!any) { msg ("Composer", "Aucune partie n'est allumée dans ce style."); return; }
	if (g_ifs.n < 2) { msg ("Composer", "Le tracé doit avoir au moins deux transformations."); return; }
	cmd_stop ();
	lock (); g_comp.mask = cur_mask (); g_comp.root = g_root; unlock ();
	cmd_compose (g_seed);
	lock (); if (g_comp.rec) snprintf (g_comp.rec->scaleName, sizeof g_comp.rec->scaleName, "%s", g_scales.n ? g_scales.s[g_scale].name : ""); unlock ();
	g_dirty = true;
	char t[80]; snprintf (t, sizeof t, "Composition en cours (graine %u)", g_seed);
	status (t);
}
static void on_play ()
{
	if (is_export ()) return;
	if (g_mode == M_PLAY) { cmd_stop (); return; }
	if (g_mode == M_COMPOSE) cmd_stop ();
	if (g_song.empty ()) { msg ("Lire", "Il n'y a encore rien à lire : composez d'abord."); return; }
	cmd_play ();
}
// ---- le prereglage : de quoi recomposer le morceau ----
static void preset_save ()
{
	if (is_export ()) return;
	if (g_mode == M_COMPOSE) { cmd_stop (); status ("Composition arrêtée."); }
	char p[300], name[96];
	snprintf (name, sizeof name, "%s", g_songPath[0] ? base_name (g_songPath) : "arabesque.mid");
	char *dot = strrchr (name, '.'); if (dot) *dot = 0;
	strcat (name, ".arb");
	if (!ask_save (HOMEDIR, name, "*.arb", p, sizeof p)) return;
	static Recipe settings;
	lock ();
	bool song = g_recipe && !g_song.empty ();
	Recipe *r = song ? g_recipe : &settings;
	if (song) r->take_mix (g_song);
	else r->from_settings (g_seed, g_song.tempo, g_song.copyright, cur_mask (), g_root, g_scales.n ? g_scales.s[g_scale].name : "", g_style, g_ifs, g_img);
	r->tempo = g_song.tempo;
	snprintf (r->copyright, sizeof r->copyright, "%s", g_song.copyright);
	bool ok = r->save (p);
	unlock ();
	if (!ok) { msg ("Préréglage", "Impossible d'écrire le fichier."); return; }
	if (song) g_dirty = false;
	status (song ? "Préréglage enregistré : il recompose ce morceau." : "Préréglage enregistré : les réglages seuls (rien n'est composé).");
}
static void preset_load (const char *path)
{
	Recipe *r = new Recipe ();
	const char *err = r->load (path);
	if (err) { delete r; msg ("Préréglage", err); return; }
	cmd_stop ();
	g_decodeImage = decode_image;
	static Image first;
	bool imgOk = recipe_image (*r, r->ch[0].image, first);
	lock ();
	bool ok = recipe_replay (*r, g_song);
	g_style = *r->ch[0].style;
	g_ifs = *r->ch[0].ifs;
	image_free (g_img);
	g_img = first; first.px = 0; first.data = 0;
	g_comp.mask = r->mask; g_comp.root = r->root;
	uint32_t seed = r->seed;
	char sname[32]; snprintf (sname, sizeof sname, "%s", r->scaleName);
	delete g_recipe;
	g_recipe = r->length ? r : 0;
	g_tick = 0;
	unlock ();
	if (!g_recipe) delete r;
	if (!g_img.data) { snprintf (g_imgKind, sizeof g_imgKind, "%s", g_img.kind); g_imgSeed = g_img.seed; }
	// les reglages de depart dans la fenetre : Composer, a graine gardee, redonne le meme debut
	g_seed = seed;
	g_keepSeed = true; ui_menu_state (M_KEEPSEED, UMS_CHECKED);
	int tempo = g_song.tempo < 40 ? 40 : g_song.tempo > 240 ? 240 : g_song.tempo;
	ui_w_set_value (g_slTempo, tempo);
	char t[200]; snprintf (t, sizeof t, "Tempo %d", g_song.tempo); ui_w_set_text (g_lTempo, (long) t);
	unsigned mask = g_comp.mask;
	int si = -1;
	for (int i = 0; i < g_scales.n && si < 0; i++) if (g_scales.s[i].mask == mask && !strcmp (g_scales.s[i].name, sname)) si = i;
	for (int i = 0; i < g_scales.n && si < 0; i++) if (g_scales.s[i].mask == mask) si = i;
	if (si < 0) { g_scales.add (sname[0] ? sname : "Gamme du préréglage", mask); si = g_scales.n - 1; }
	g_scale = si < 0 ? 0 : si; g_root = g_comp.root;
	scale_list_fill (); apply_scale ();
	g_stylePath[0] = g_tracePath[0] = g_songPath[0] = 0;
	ui_w_set_value (g_lsTrace, -1); ui_w_set_value (g_lsStyle, -1);	// (ni l'un ni l'autre ne vient d'un fichier de la liste)
	g_selT = 0; g_part = 0;
	form_load ();
	trace_edited (false);
	image_changed ();
	ui_view_dirty (v_roll); ui_view_dirty (v_parts);
	g_dirty = false;
	int n = 0; lock (); for (int i = 0; i < NPART; i++) n += g_song.tr[i].notes (); unlock ();
	if (g_recipe) snprintf (t, sizeof t, "Préréglage %s : morceau recomposé, %d notes (graine %u).", base_name (path), n, seed);
	else snprintf (t, sizeof t, "Préréglage %s : réglages chargés (graine %u).", base_name (path), seed);
	status (t);
	ui_w_draw (0);
	if (!ok || !imgOk) msg ("Préréglage", "L'image gardée dans le préréglage ne se lit pas : un mandala la remplace.");
}

static void command (int c)
{
	static char p[300];
	switch (c)
	{
	case M_NEW:
		if (!confirm ()) break;
		cmd_stop (); lock (); g_song.clear (); g_tick = 0; delete g_recipe; g_recipe = 0; unlock ();
		g_songPath[0] = 0; g_dirty = false; ui_view_dirty (v_roll); ui_view_dirty (v_parts);
		break;
	case M_OPEN: if (confirm () && ask_open (HOMEDIR, "*.mid;*.midi", p, sizeof p)) song_load (p); break;
	case M_SAVE: song_save (); break;
	case M_SAVEAS: case M_XMID: if (song_ready ()) song_save_as (); break;
	case M_XFMS: export_fms (); break;
	case M_XWAV: export_audio (AKF_WAV, "arabesque.wav", "*.wav", 0); break;
	case M_XMP3: export_audio (AKF_MP3, "arabesque.mp3", "*.mp3", 192000); break;
	case M_COPYRIGHT:
		snprintf (p, sizeof p, "%s", g_song.copyright);
		if (ui_ask ((long) "Copyright", (long) "Mention écrite dans le fichier MIDI :", (long) p, sizeof g_song.copyright)) { strcpy (g_song.copyright, p); g_dirty = true; }
		after_dialog ();
		break;
	case M_QUIT: if (confirm ()) g_quitNow = true; break;
	case MC_COMPOSE: on_compose (); break;
	case MC_PLAY: on_play (); break;
	case M_STOP: if (is_export ()) g_expCancel = true; else cmd_stop (); break;
	case M_REWIND: if (g_mode != M_COMPOSE && !is_export ()) { cmd_seek (0); ui_view_dirty (v_roll); } break;
	case M_KEEPSEED: g_keepSeed = !g_keepSeed; ui_menu_state (M_KEEPSEED, g_keepSeed ? UMS_CHECKED : 0); break;
	case M_STNEW:
		lock (); g_style.reset (); g_style.p[0].on = true; g_style.p[0].patch = 0; unlock ();
		g_stylePath[0] = 0; g_part = 0; form_load ();
		ui_w_set_value (g_tabs, T_STYLE); ui_w_draw (0);
		break;
	case M_STOPEN: if (ask_open (g_styleDir, "*.stl", p, sizeof p)) load_style (p); break;
	case M_STSAVE: if (g_stylePath[0] && strncmp (g_stylePath, APPDIR, strlen (APPDIR))) { g_style.save (g_stylePath); break; } [[fallthrough]];
	case M_STSAVEAS:
		if (ask_save (g_styleDir, g_stylePath[0] ? base_name (g_stylePath) : "style.stl", "*.stl", p, sizeof p))
		{
			if (!g_style.save (p)) { msg ("Style", "Impossible d'écrire le fichier."); break; }
			snprintf (g_stylePath, sizeof g_stylePath, "%s", p);
			fill_lists ();
		}
		break;
	case M_STDIR: if (ui_folder_choose ((long) g_styleDir, (long) p, sizeof g_styleDir) == 1) { strcpy (g_styleDir, p); fill_lists (); } after_dialog (); break;
	case M_TRNEW:
		lock ();
		g_ifs.n = 0; g_ifs.x0 = g_ifs.y0 = -1; g_ifs.x1 = g_ifs.y1 = 1;
		strcpy (g_ifs.name, "trace"); g_ifs.add (); g_ifs.add ();
		g_ifs.uniform ();
		unlock ();
		g_tracePath[0] = 0; g_selT = 0;
		ui_w_set_value (g_tabs, T_TRACE); g_tab = T_TRACE;
		trace_edited (false); ui_w_draw (0);
		break;
	case M_TROPEN: if (ask_open (g_traceDir, "*.ifs", p, sizeof p)) load_trace (p); break;
	case M_TRSAVE: if (g_tracePath[0] && strncmp (g_tracePath, APPDIR, strlen (APPDIR))) { g_ifs.save (g_tracePath); break; } [[fallthrough]];
	case M_TRSAVEAS:
		if (ask_save (g_traceDir, g_tracePath[0] ? base_name (g_tracePath) : "trace.ifs", "*.ifs", p, sizeof p))
		{
			if (!g_ifs.save (p)) { msg ("Tracé", "Impossible d'écrire le fichier."); break; }
			snprintf (g_tracePath, sizeof g_tracePath, "%s", p);
			fill_lists ();
		}
		break;
	case M_TRDIR: if (ui_folder_choose ((long) g_traceDir, (long) p, sizeof g_traceDir) == 1) { strcpy (g_traceDir, p); fill_lists (); } after_dialog (); break;
	case M_IMGOPEN: if (ask_open ("SD0:/home/pictures", "*.bmp;*.png", p, sizeof p)) load_image (p); break;
	case M_IMGGEN: generate (g_imgKind, g_imgSeed + 1); break;
	case M_PRESETOPEN: if (confirm () && ask_open (HOMEDIR, "*.arb", p, sizeof p)) preset_load (p); break;
	case M_PRESETSAVE: preset_save (); break;
	case M_MARKER:
	{
		long r = ui_color_dialog ((long) "Couleur du repère", g_marker);
		after_dialog ();
		if (r >= 0) g_marker = (unsigned) r & 0xFFFFFF;
		break;
	}
	default:
		if (c >= M_GEN0 && c < M_GEN0 + NKINDS) { const char *k = IMG_KINDS[c - M_GEN0]; generate (k, strcmp (k, g_imgKind) ? 1 : g_imgSeed + 1); }
		break;
	case M_ABOUT:
		msg ("Arabesque", "Un chemin fractal parcourt une image : la couleur des points visités devient de la musique.\n\n"
			"Choisissez un tracé, un style et une gamme, puis Composer. Pendant la composition, la gamme change au temps suivant, le style à la mesure suivante.\n\n"
			"D'après l'idée d'Art Song de David T. Strohbeen (1996). Lit ses fichiers .ifs, .stl et .sca.");
		break;
	}
}

// ---- les widgets --------------------------------------------------------------------------------------------------------------
static long g_pages[5];
static long PL (long w, long parent, int x, int y, int ww, int hh)
{
	ui_w_set_parent (w, parent);
	if (g_npl < 160) g_pl[g_npl++] = Placed { w, x, y, ww, hh };
	return w;
}
static long lab (long parent, int x, int y, int w, const char *s)
{
	return PL (ui_label (0, 0, w, 22, (long) s, ULB_DIM), parent, x, y + 4, w, 22);
}
static long drop (long parent, int x, int y, int w, const char *s) { return PL (ui_dropdown (0, 0, w, 28, (long) s), parent, x, y, w, 28); }
static long num (long parent, int x, int y, int w, int lo, int hi, int v) { return PL (ui_numeric (0, 0, w, 28, lo, hi, v), parent, x, y, w, 28); }
static long btn (long parent, int x, int y, int w, const char *s) { return PL (ui_pushbutton (0, 0, w, 28, (long) s, 0), parent, x, y, w, 28); }
static long view (long parent, int x, int y, int w, int h, long paint, long mouse)
{
	long v = ui_view (0, 0, w, h, 0);
	ui_view_on (v, paint, mouse, 0, 0);
	return PL (v, parent, x, y, w, h);
}

static void build_music (long p)
{
	const int W = PANEL_W - 16;
	lab (p, 0, 0, 120, "Tracé");
	g_lsTrace = PL (ui_list (0, 0, 10, 10, 0), p, 0, 24, W - 112, 104);
	v_trace = view (p, W - 104, 24, 104, 104, (long) mini_paint, 0);
	lab (p, 0, 132, 120, "Style");
	g_lsStyle = PL (ui_list (0, 0, 10, 10, 0), p, 0, 156, W, 104);
	lab (p, 0, 264, 60, "Tonique");
	g_lRoot = PL (ui_label (0, 0, 60, 22, (long) "Do", ULB_BOLD), p, 62, 268, 60, 22);
	lab (p, 130, 264, 60, "Gamme");
	g_ddScale = drop (p, 130, 290, W - 130, "Majeure");
	v_root = view (p, 0, 290, 122, 64, (long) root_paint, (long) root_mouse);
}
static void build_style (long p)
{
	const int W = PANEL_W - 16;
	v_parts = view (p, 0, 0, W, NPART * PR_H, (long) parts_paint, (long) parts_mouse);
	int y = NPART * PR_H + 3;
	g_segForm = PL (ui_segmented (0, 0, W - 76, 28, (long) "Orchestre|Algorithme"), p, 0, y, W - 76, 28);
	f_tie = PL (ui_checkbox (0, 0, 68, 24, (long) "Lier", 0), p, W - 68, y + 2, 68, 24);
	ui_w_tip (f_tie, (long) "Lier les notes égales qui se suivent");
	y += 31;
	int fh = 200;
	g_fp[0] = PL (ui_panel (0, 0, W, fh, 0), p, 0, y, W, fh);
	g_fp[1] = PL (ui_panel (0, 0, W, fh, 0), p, 0, y, W, fh);
	long a = g_fp[0];
	lab (a, 0, y, 60, "Nom"); f_name = PL (ui_textbox (0, 0, 150, 28, 0), a, 44, y, 150, 28);
	lab (a, 204, y, 50, "Canal"); f_chan = num (a, 250, y, W - 250, 1, 16, 1);
	f_list = PL (ui_list (0, 0, 10, 10, 0), a, 0, y + 30, W, 100);
	for (int i = 0; i < 128; i++) { char t[48]; snprintf (t, sizeof t, "%03d  %s", i, GM_NAMES[i]); ui_list_add (f_list, (long) t, i); }
	lab (a, 0, y + 134, 60, "Volume"); f_vol = PL (ui_slider (0, 0, 10, 24, 0, 127, 64), a, 64, y + 136, W - 64, 24);
	lab (a, 0, y + 164, 60, "Panor."); f_pan = PL (ui_slider (0, 0, 10, 24, 0, 127, 64), a, 64, y + 166, W - 64, 24);
	long b = g_fp[1];
	int c1 = 0, c2 = W / 2 + 4, lw = 64, fw = W / 2 - 4 - lw;
	static const char *const L1[7] = { "1re note", "Grave", "Aigu", "Voix", "Accord", "Unité", "Rythme" };
	static const char *const L2[7] = { "Densité %", "Phase", "Nuances", "Hauteur", "Durée", "Volume", "Articul." };
	for (int i = 0; i < 7; i++) { lab (b, c1, y + i * 28, lw, L1[i]); lab (b, c2, y + i * 28, lw, L2[i]); }
	f_start = num (b, c1 + lw, y, fw, 0, 127, 0);
	f_low = num (b, c1 + lw, y + 28, fw, 0, 127, 48);
	f_high = num (b, c1 + lw, y + 56, fw, 0, 127, 72);
	f_voices = num (b, c1 + lw, y + 84, fw, 1, 4, 1);
	f_chord = drop (b, c1 + lw, y + 112, fw, "Serré|Ouvert 1|Ouvert 2");
	f_unit = drop (b, c1 + lw, y + 140, fw, "Ronde|Blanche|Noire|Noire tr.|Croche|Croche tr.|Double|Double tr.|Triple");
	f_rhythm = PL (ui_textbox (0, 0, fw, 28, 0), b, c1 + lw, y + 168, fw, 28);
	ui_textbox_hint (f_rhythm, (long) "ex. 2112");
	f_den = num (b, c2 + lw, y, fw, 1, 100, 100);
	f_phase = num (b, c2 + lw, y + 28, fw, 0, 9999, 100);
	f_range = num (b, c2 + lw, y + 56, fw, 10, 80, 80);
	f_mp = drop (b, c2 + lw, y + 84, fw, "Rouge|Vert|Bleu");
	f_md = drop (b, c2 + lw, y + 112, fw, "Rouge|Vert|Bleu");
	f_mv = drop (b, c2 + lw, y + 140, fw, "Rouge|Vert|Bleu");
	f_ma = drop (b, c2 + lw, y + 168, fw, "Fixe|Rouge|Vert|Bleu");
	ui_w_show (g_fp[1], 0);
}
static void build_trace (long p)
{
	const int W = PANEL_W - 16;
	t_count = lab (p, 0, 0, 170, "Transformations : 0");
	t_spin = num (p, W - 110, 2, 110, 1, IFS_MAX, 1);
	int bw = (W - 16) / 3;
	t_add = btn (p, 0, 34, bw, "Ajouter"); t_del = btn (p, bw + 8, 34, bw, "Supprimer"); t_test = btn (p, 2 * (bw + 8), 34, bw, "Tester");
	t_auto = PL (ui_checkbox (0, 0, 130, 24, (long) "Cadre auto", 1), p, 0, 70, 130, 24);
	t_calc = btn (p, 134, 68, 76, "Calculer");
	t_sens = PL (ui_segmented (0, 0, 112, 28, (long) "10|5|1"), p, W - 112, 68, 112, 28);
	static const char *const R[3] = { "Tourner", "Taille", "Déplacer" };
	static const char *const OP[4] = { "-X", "+X", "-Y", "+Y" };
	for (int r = 0; r < 3; r++)
	{
		int y = 104 + r * 32;
		lab (p, 0, y, 76, R[r]);
		for (int o = 0; o < 4; o++) t_op[r * 4 + o] = btn (p, 80 + o * (W - 80) / 4, y, (W - 80) / 4 - 4, OP[o]);
	}
	lab (p, 0, 206, 100, "Probabilités");
	t_uni = PL (ui_radio (0, 0, 100, 24, (long) "Uniformes", 7), p, 104, 208, 104, 24);
	t_rel = PL (ui_radio (0, 0, 100, 24, (long) "Relatives", 7), p, 212, 208, 104, 24);
	t_coef = PL (ui_label (0, 0, W, 44, (long) "", ULB_DIM | ULB_WRAP), p, 0, 240, W, 44);
}
static void build_scales (long p)
{
	const int W = PANEL_W - 16, bw = 84;
	v_scale = view (p, 0, 0, W - bw - 8, 70, (long) scale_paint, (long) scale_mouse);
	s_name = PL (ui_textbox (0, 0, W - bw - 8, 28, 0), p, 0, 76, W - bw - 8, 28);
	s_list = PL (ui_list (0, 0, 10, 10, 0), p, 0, 110, W - bw - 8, 190);
	int x = W - bw;
	s_new = btn (p, x, 0, bw, "Nouvelle"); s_del = btn (p, x, 32, bw, "Supprimer");
	s_up = btn (p, x, 76, bw / 2 - 2, "▲"); s_down = btn (p, x + bw / 2 + 2, 76, bw / 2 - 2, "▼");
	s_clear = btn (p, x, 110, bw, "Vider"); s_save = btn (p, x, 142, bw, "Enregistrer"); s_load = btn (p, x, 174, bw, "Ouvrir");
}
static void build_keys (long p)
{
	const int W = PANEL_W - 16;
	v_keys = view (p, 0, 0, W, 84, (long) keys_paint, (long) keys_mouse);
	lab (p, 0, 92, 60, "Octave"); k_oct = num (p, 64, 92, 100, 1, 8, 4);
	lab (p, 176, 92, 60, "Volume"); k_vol = PL (ui_slider (0, 0, 10, 24, 0, 127, 100), p, 236, 94, W - 236, 24);
	k_list = PL (ui_list (0, 0, 10, 10, 0), p, 0, 126, W, 170);
	for (int i = 0; i < 128; i++) { char t[48]; snprintf (t, sizeof t, "%03d  %s", i, GM_NAMES[i]); ui_list_add (k_list, (long) t, i); }
}

static void lay_out (long w, long h)
{
	long px = w - PANEL_W - 4, py = TOPBAR;
	ui_w_move (g_bRewind, MARGIN, 4, 40, 30);
	ui_w_move (g_bCompose, MARGIN + 44, 4, 112, 30);
	ui_w_move (g_bPlay, MARGIN + 160, 4, 84, 30);
	ui_w_move (g_bStop, MARGIN + 248, 4, 78, 30);
	ui_w_move (g_lcd, MARGIN + 334, 5, 84, 28);
	ui_w_move (g_lTempo, MARGIN + 430, 8, 74, 22);
	long sl = w - FRAME_CAP_W - 8 - (MARGIN + 508);
	ui_w_move (g_slTempo, MARGIN + 508, 8, sl > 40 ? sl : 40, 24);
	long iw = px - MARGIN - 8, ih = h - TOPBAR - ROLL_H - FOOT - 10;
	ui_w_move (v_img, MARGIN, TOPBAR, iw, ih);
	ui_w_move (v_roll, MARGIN, TOPBAR + ih + 4, iw, ROLL_H);
	ui_w_move (g_status, MARGIN, h - FOOT - 2, w - 2 * MARGIN, 22);
	ui_w_move (g_tabs, px, py, PANEL_W, h - py - FOOT - 6);
	// (ui_w_move ne deplace pas les pages des onglets : elles garderaient la place de leur creation et leur fond
	// se peindrait par-dessus l'image)
	for (int i = 0; i < 5; i++) ui_w_move (g_pages[i], px, py + 40, PANEL_W, h - py - FOOT - 46);
	long ox = px + 8, oy = py + 44;
	for (int i = 0; i < g_npl; i++) ui_w_move (g_pl[i].w, ox + g_pl[i].x, oy + g_pl[i].y, g_pl[i].ww, g_pl[i].hh);
	image_changed ();
	ui_view_dirty (v_roll);
}
static void on_resize (long w, long h) { ui_fill (0, 0, w, h, jet (J_FACE)); lay_out (w, h); ui_w_draw (0); }

static void sync_transport ()
{
	int m = g_mode;
	if (m == g_lastMode) return;
	if (g_lastMode == M_EXPORT)
	{
		char t[360];
		if (g_expState == EXPORT_DONE) { snprintf (t, sizeof t, "Exporté : %s", g_expPath); status (t); }
		else if (g_expState == EXPORT_CANCEL) status ("Export abandonné.");
		else msg ("Exporter", "Impossible d'écrire le fichier.");
	}
	g_lastMode = m;
	ui_w_set_text (g_bCompose, (long) (m == M_COMPOSE ? "■ Composition" : "● Composer"));
	ui_w_set_text (g_bPlay, (long) (m == M_PLAY ? "❚❚ Lecture" : "▶ Lire"));
	ui_w_style (g_bCompose, m == M_COMPOSE ? UB_DEFAULT : 0);
	ui_w_style (g_bPlay, m == M_PLAY ? UB_DEFAULT : 0);
	ui_w_enable (g_bCompose, m != M_EXPORT); ui_w_enable (g_bPlay, m != M_EXPORT);
	ui_w_draw (g_bCompose); ui_w_draw (g_bPlay);
	if (m == M_IDLE) ui_view_dirty (v_parts);
	ui_view_dirty (v_img);
}
static void tick_ui ()
{
	static bool wasPending, wasLoaded;
	static uint32_t lastT = 0xFFFFFFFF;
	static int frame;
	sync_transport ();
	if (g_synthOk != wasLoaded) { wasLoaded = g_synthOk; if (!g_statusSet) status ("Prêt : choisissez un tracé et un style, puis Composer."); }
	uint32_t t = (uint32_t) g_tick;
	if (t / (PPQ / 4) != lastT)
	{
		lastT = t / (PPQ / 4);
		char s[24];
		snprintf (s, sizeof s, "%d:%d", (int) (t / (PPQ * BEATS)) + 1, (int) (t / PPQ % BEATS) + 1);
		ui_w_set_text (g_lcd, (long) s);
		ui_w_draw (g_lcd);
	}
	if (busy ())
	{
		if (is_export ())
		{
			char s[80]; snprintf (s, sizeof s, "Export en cours : %d %%   (Arrêt pour abandonner)", g_expPct);
			status (s);
		}
		else if (g_mode == M_COMPOSE) ui_view_dirty (v_img);
		if (++frame % 3 == 0) { ui_view_dirty (v_roll); if (g_mode == M_COMPOSE && ui_w_get_value (g_tabs) == T_STYLE) ui_view_dirty (v_parts); }
	}
	bool pend = g_mode == M_COMPOSE && (g_comp.stylePending || g_comp.scalePending);
	if (wasPending && !pend) { form_load (); }
	wasPending = pend;
}
static void on_widget (long w, long code)
{
	if (code == UN_ACTIVATE)
	{
		if (w == g_bRewind) command (M_REWIND);
		else if (w == g_bCompose) command (MC_COMPOSE);
		else if (w == g_bPlay) command (MC_PLAY);
		else if (w == g_bStop) command (M_STOP);
		else if (w == t_add) { lock (); g_ifs.add (); unlock (); g_selT = g_ifs.n - 1; trace_edited (true); }
		else if (w == t_del) { if (g_ifs.n > 2) { lock (); g_ifs.remove (g_selT); unlock (); trace_edited (true); } }
		else if (w == t_test) { g_testBig = true; ui_view_dirty (v_img); }
		else if (w == t_calc) { lock (); g_ifs.limits (); unlock (); trace_edited (false); }
		else if (w == s_new) { g_scales.add ("Nouvelle gamme", 1); g_scale = g_scales.n - 1; scale_list_fill (); apply_scale (); }
		else if (w == s_del) { if (g_scales.n > 1) { memmove (g_scales.s + g_scale, g_scales.s + g_scale + 1, sizeof (Scale) * (size_t) (g_scales.n - 1 - g_scale)); g_scales.n--; if (g_scale >= g_scales.n) g_scale = g_scales.n - 1; scale_list_fill (); apply_scale (); } }
		else if (w == s_up || w == s_down)
		{
			int o = g_scale + (w == s_up ? -1 : 1);
			if (o >= 0 && o < g_scales.n) { Scale t = g_scales.s[o]; g_scales.s[o] = g_scales.s[g_scale]; g_scales.s[g_scale] = t; g_scale = o; scale_list_fill (); }
		}
		else if (w == s_clear)
		{
			long r = ui_message ((long) "Gammes", (long) "Effacer toutes les gammes ?", UM_YESNO);
			after_dialog ();
			if (r == 1) { g_scales.n = 0; g_scales.add ("Majeure", 0xAB5); g_scale = 0; scale_list_fill (); apply_scale (); }
		}
		else if (w == s_save) { char p[300]; if (ask_save (HOMEDIR, "gammes.sca", "*.sca", p, sizeof p)) g_scales.save (p); }
		else if (w == s_load) { char p[300]; if (ask_open (HOMEDIR, "*.sca", p, sizeof p) && g_scales.load (p)) { g_scale = 0; scale_list_fill (); apply_scale (); } }
		else
		{
			for (int i = 0; i < 12; i++) if (w == t_op[i]) trace_op (i);
		}
		return;
	}
	if (code != UN_CHANGE) return;
	if (w == g_tabs) { g_tab = (int) ui_w_get_value (w); key_release (); ui_view_dirty (v_img); ui_view_dirty (v_parts); ui_view_dirty (v_trace); ui_view_dirty (v_root); ui_view_dirty (v_scale); ui_view_dirty (v_keys); trace_tools_refresh (); }
	else if (w == g_slTempo)
	{
		int v = (int) ui_w_get_value (w);
		lock (); g_song.tempo = v; unlock ();
		char t[24]; snprintf (t, sizeof t, "Tempo %d", v);
		ui_w_set_text (g_lTempo, (long) t);
	}
	else if (w == g_lsTrace) { int i = (int) ui_w_get_value (w); if (i >= 0 && i < g_ntrace) load_trace (g_ftrace[i].path); }
	else if (w == g_lsStyle) { int i = (int) ui_w_get_value (w); if (i >= 0 && i < g_nstyle) load_style (g_fstyle[i].path); }
	else if (w == g_ddScale) { g_scale = (int) ui_w_get_value (w); scale_list_fill (); apply_scale (); }
	else if (w == s_list) { int i = (int) ui_w_get_value (w); if (i >= 0 && i < g_scales.n) { g_scale = i; ui_w_set_text (s_name, (long) g_scales.s[i].name); ui_w_set_value (g_ddScale, i); ui_view_dirty (v_scale); apply_scale (); } }
	else if (w == s_name) { if (g_scales.n) { snprintf (g_scales.s[g_scale].name, sizeof g_scales.s[g_scale].name, "%s", (const char *) ui_w_get_text (w)); ui_list_set (s_list, g_scale, (long) g_scales.s[g_scale].name); } }
	else if (w == g_segForm) { int v = (int) ui_w_get_value (w); ui_w_show (g_fp[0], v == 0); ui_w_show (g_fp[1], v == 1); ui_w_draw (0); }
	else if (w == t_spin) { g_selT = (int) ui_w_get_value (w) - 1; trace_edited (false); }
	else if (w == t_auto) { g_autoLimit = ui_w_get_value (w) != 0; if (g_autoLimit) { lock (); g_ifs.limits (); unlock (); trace_edited (false); } }
	else if (w == t_sens) { static const int S[3] = { 10, 5, 1 }; g_sens = S[ui_w_get_value (w) & 3]; }
	else if (w == t_uni) { lock (); g_ifs.uniform (); unlock (); trace_edited (true); }
	else if (w == t_rel) { lock (); g_ifs.relative (); unlock (); trace_edited (true); }
	else if (w == k_oct) { key_release (); g_keyOct = (int) ui_w_get_value (w); }
	else if (w == k_vol) g_keyVol = (int) ui_w_get_value (w);
	else if (w == k_list) g_keyProg = (int) ui_w_get_value (w);
	else form_change (w);
}
static void on_drop ()
{
	static char b[1024];
	long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES) return;
	b[n] = 0;
	char *e = b; while (*e && *e != '\n' && *e != '\r') e++;
	*e = 0;
	open_any (b);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui) || !audiokit_kit_open ()) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	long sw = W_WANT, sh = H_WANT;
	{ long scr[4]; ui_screen ((long) scr); if (scr[0] > 0 && sw > scr[0] - 16) sw = scr[0] - 16; if (scr[1] > 0 && sh > scr[1] - scr[2] - scr[3] - 16) sh = scr[1] - scr[2] - scr[3] - 16; }
	g_win = ui_window_ex (-1, -1, sw, sh, (long) "Arabesque", WF_RESIZABLE);
	if (g_win <= 0) return 1;
	ui_min_size (720, 510);
	ui_menu ((long)
		"MMorceau\n" "I1\tNouveau\tCtrl+N\n" "I2\tOuvrir...\tCtrl+O\n" "I3\tEnregistrer\tCtrl+S\n" "I4\tEnregistrer sous...\n" "-\n"
		"I5\tExporter en MIDI...\n" "I6\tExporter en FM Song...\n" "I7\tExporter en WAV...\n" "I8\tExporter en MP3...\n" "-\n"
		"I30\tOuvrir un préréglage...\tCtrl+Maj+O\n" "I31\tEnregistrer le préréglage...\tCtrl+Maj+S\n" "-\n"
		"I9\tCopyright...\n" "-\n" "I10\tQuitter\tCtrl+Q\n"
		"MComposition\n" "I11\tComposer\tF5\n" "I12\tLire\tF6\n" "I13\tArrêter\tF7\n" "I14\tAu début\tF8\n" "-\n" "I15\tGarder la même graine\n"
		"MStyle\n" "I16\tNouveau\tF3\n" "I17\tOuvrir...\n" "I18\tEnregistrer\n" "I19\tEnregistrer sous...\n" "I20\tDossier des styles...\n"
		"MTracé\n" "I21\tNouveau\tF4\n" "I22\tOuvrir...\n" "I23\tEnregistrer\n" "I24\tEnregistrer sous...\n" "I25\tDossier des tracés...\n"
		"MImage\n" "I26\tOuvrir...\n" "-\n"
		"I32\tMandala\n" "I33\tMandelbrot\n" "I34\tJulia\n" "I35\tBurning Ship\n" "I36\tLyapunov\n" "I37\tNewton\n"
		"I27\tUne autre de la même sorte\tF9\n" "-\n" "I28\tCouleur du repère...\n"
		"MAide\n" "I29\tÀ propos d'Arabesque\n");
	mkdir (HOMEDIR, 0755);

	g_comp.song = &g_song; g_comp.style = &g_style; g_comp.ifs = &g_ifs; g_comp.img = &g_img;
	image_make (g_img, "mandala", IMG_SIZE, IMG_SIZE, g_imgSeed);
	g_scales.defaults ();

	g_bRewind = ui_pushbutton (0, 0, 40, 30, (long) "|◀", 0);
	ui_w_tip (g_bRewind, (long) "Au début (F8)");
	g_bCompose = ui_pushbutton (0, 0, 104, 30, (long) "● Composer", 0);
	g_bPlay = ui_pushbutton (0, 0, 74, 30, (long) "▶ Lire", 0);
	g_bStop = ui_pushbutton (0, 0, 74, 30, (long) "■ Arrêt", 0);
	g_lcd = ui_lcd (0, 0, 84, 28, (long) "1:1");
	g_lTempo = ui_label (0, 0, 74, 22, (long) "Tempo 120", ULB_DIM);
	g_slTempo = ui_slider (0, 0, 100, 24, 40, 240, 120);
	v_img = ui_view (0, 0, 10, 10, 0); ui_view_on (v_img, (long) img_paint, (long) img_mouse, 0, 0);
	v_roll = ui_view (0, 0, 10, 10, 0); ui_view_on (v_roll, (long) roll_paint, (long) roll_mouse, 0, 0);
	g_status = ui_label (0, 0, 100, 22, (long) "Chargement des sons...", ULB_DIM);
	g_tabs = ui_tabs (0, 0, PANEL_W, 400, (long) "Musique|Style|Tracé|Gammes|Clavier");
	for (int i = 0; i < 5; i++) g_pages[i] = ui_tabs_page (g_tabs, i);
	build_music (g_pages[T_MUSIC]);
	build_style (g_pages[T_STYLE]);
	build_trace (g_pages[T_TRACE]);
	build_scales (g_pages[T_SCALES]);
	build_keys (g_pages[T_KEYS]);
	ui_w_set_value (t_rel, 1);
	ui_w_set_value (t_sens, 1);

	scale_list_fill ();
	g_style.p[0].on = true;
	form_load ();
	apply_scale ();
	ui_on_resize ((long) on_resize, 0);
	lay_out (ui.w, ui.h);
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	trace_tools_refresh ();
	ui_w_draw (0);
	ui_view_flush ();
	ui_update_all ();
	// (la fenetre d'abord : un disque occupe ne la laisse pas vide)
	if (g_scales.load (APPDIR "default.sca")) scale_list_fill ();
	fill_lists ();
	if (g_ntrace) { load_trace (g_ftrace[0].path); ui_w_set_value (g_lsTrace, 0); }
	if (g_nstyle) { load_style (g_fstyle[0].path); ui_w_set_value (g_lsStyle, 0); }
	if (argc > 1 && argv[1][0]) open_any (argv[1]);
	audio_start ();
	ui_w_draw (0);
	while (!g_quitNow)
	{
		long t = ui_event (busy () ? 33 : 150);
		if (t == WEV_CLOSE) { if (confirm ()) break; continue; }
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP) on_drop ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) on_widget (w, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
			}
		}
		else ui_w_idle ();
		tick_ui ();
		ui_view_flush ();
	}
	cmd_stop ();
	audio_stop ();
	return 0;
}
