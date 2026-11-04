//
// arabesque -- la composition algorithmique par fractales : un chemin calcule (un systeme de fonctions iterees) parcourt une
// image, et la couleur des points visites devient de la musique, jusqu'a dix parties General MIDI. L'idee est celle
// d'Art Song de David T. Strohbeen (1996) ; le programme est ecrit ici, il lit ses formats .ifs, .stl et .sca.
//
// Une fenetre : a gauche l'image et le chemin, dessous la sequence et la grille d'accords, a droite cinq onglets :
// Musique (trace, style, gamme, tonique), Style (les parties, leur orchestre, leur algorithme), Tracé (les
// transformations, qu'on tire a la souris sur l'image), Gammes, Clavier. Composer joue en composant : on change de gamme
// ou de style en route. Deux fenetres de plus, a la demande : l'accord d'une mesure (un clic sur la grille) et les
// reglages de l'image calculee (Ctrl+I).
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
enum { W_WANT = 784, H_WANT = 560, PANEL_W = 356, TOPBAR = 40, ROLL_H = 56, CHORD_H = 38, FOOT = 24, MARGIN = 8, ROW = 32, NFILES = 96, IMG_SIZE = 512 };
enum { T_MUSIC, T_STYLE, T_TRACE, T_SCALES, T_KEYS };
enum { M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_XMID, M_XFMS, M_XWAV, M_XMP3, M_COPYRIGHT, M_QUIT,
       MC_COMPOSE, MC_PLAY, M_STOP, M_REWIND, M_KEEPSEED,
       M_STNEW, M_STOPEN, M_STSAVE, M_STSAVEAS, M_STDIR,
       M_TRNEW, M_TROPEN, M_TRSAVE, M_TRSAVEAS, M_TRDIR,
       M_IMGOPEN, M_IMGGEN, M_MARKER, M_ABOUT, M_PRESETOPEN, M_PRESETSAVE, M_GEN0,	// M_GEN0 + sorte (image.h)
       M_IMGSET = M_GEN0 + 8, M_CHORDEDIT, M_CHORDCLEAR };

static const unsigned PART_COL[NPART] = { 0xE0524D, 0xEFA41B, 0x3FB950, 0x2FA8D8, 0x8C6BE8, 0xE85FA8, 0x20B8A6, 0xC8A020, 0x6C8CF0, 0xA8A8A8 };
static const int UNIT_TICKS[9] = { 480, 240, 120, 80, 60, 40, 30, 20, 15 };

// ---- l'etat -----------------------------------------------------------------------------------------------------------
static long g_win, g_tabs, g_status, g_bRewind, g_bCompose, g_bPlay, g_bStop, g_lcd, g_slTempo, g_lTempo;
static long v_img, v_roll, v_trace, v_root, v_parts, v_scale, v_keys;
static long g_lsTrace, g_lsStyle, g_ddScale, g_lRoot;
static long g_segForm, g_fp[2];
static long f_name, f_chan, f_list, f_vol, f_pan, f_start, f_low, f_high, f_voices, f_chord, f_unit, f_rhythm, f_tie, f_den, f_phase, f_range, f_mp, f_md, f_mv, f_ma, f_harm;
static long v_chords;
static long t_count, t_spin, t_add, t_del, t_test, t_auto, t_calc, t_sens, t_uni, t_rel, t_coef, t_op[12];
static long s_name, s_list, s_new, s_del, s_up, s_down, s_clear, s_save, s_load;
static long k_oct, k_list, k_vol;
struct Placed { long w; int x, y, ww, hh; };
static Placed g_pl[160]; static int g_npl;

static int g_part, g_selT, g_sens = 5, g_tab;
static bool g_autoLimit = true, g_testBig, g_keepSeed, g_dirty, g_quitNow;
static uint32_t g_seed = 1, g_imgSeed = 3;
static char g_imgKind[16] = "mandala";
static ImgSet g_imgSet;			// les reglages de l'image calculee, donnes a la main
static void settings_refresh ();
static ChordGrid g_chords;		// la grille d'accords (sous le verrou : le compositeur la lit)
enum { VIEW_BARS = 8 };			// assez large pour lire les accords
static int g_viewBars = VIEW_BARS, g_viewFirst;	// la sequence et la grille : combien de mesures montrees (0 : tout), la premiere
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
	ui_w_set_text (g_lRoot, (long) NOTE_NAMES[g_root]);
	ui_view_dirty (v_root);
	ui_view_dirty (v_chords);		// (les noms des accords suivent la gamme)
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
static bool g_keep;			// a l'arret, l'image devoilee reste, jusqu'a un clic sur l'image
static void image_changed () { free (g_scaled); g_scaled = 0; g_keep = false; ui_view_dirty (v_img); }

// pendant la lecture et la composition, l'image est sombre : chaque note jouee en devoile pour de bon les couleurs autour
// de son point, et la fractale apparait peu a peu. Les notes se reprennent de zero si la lecture recule.
static uint8_t *g_seen; static int g_seenW, g_seenH, g_seenCur[NPART];
static double g_seenT = -1;
static void stamp (int w, int h, double cx, double cy, double r)	// un disque aux bords doux
{
	int x0 = (int) (cx - r), x1 = (int) (cx + r), y0 = (int) (cy - r), y1 = (int) (cy + r);
	if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > w - 1) x1 = w - 1; if (y1 > h - 1) y1 = h - 1;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			double d = sqrt ((x - cx) * (x - cx) + (y - cy) * (y - cy)) / r;
			if (d >= 1) continue;
			int a = d < 0.6 ? 255 : (int) (255 * (1 - d) / 0.4);
			uint8_t &p = g_seen[y * w + x];
			if (a > p) p = (uint8_t) a;
		}
}
static void reveal (uint32_t *d, long pw, int w, int h, bool run)
{
	if (!g_seen || g_seenW != w || g_seenH != h)
	{
		free (g_seen);
		g_seen = (uint8_t *) calloc ((size_t) w * h, 1);
		g_seenW = w; g_seenH = h; g_seenT = -1;
	}
	if (run)
	{
		lock ();
		double t = g_tick;
		if (t < g_seenT || g_seenT < 0) { memset (g_seen, 0, (size_t) w * h); memset (g_seenCur, 0, sizeof g_seenCur); }
		g_seenT = t;
		double rad = w / 160.0 > 1.5 ? w / 160.0 : 1.5;
		for (int i = 0; i < NPART; i++)
		{
			const Track &tr = g_song.tr[i];
			int &c = g_seenCur[i];
			if (c > tr.count) c = 0;
			for (; c < tr.count && tr.n[c].tick <= t; c++)
			{
				const Note &m = tr.n[c];
				if (m.kind == NK_NOTE && m.pu) stamp (w, h, (m.pu - 1) / 65534.0 * (w - 1), (m.pv - 1) / 65534.0 * (h - 1), rad * (0.7 + m.vel / 127.0 * 0.6));
			}
		}
		unlock ();
	}
	for (int y = 0; y < h; y++)
	{
		uint32_t *o = d + y * pw; const uint32_t *s = g_scaled + (long) y * w; const uint8_t *seen = g_seen + (long) y * w;
		for (int x = 0; x < w; x++)
		{
			uint32_t c = s[x], a = seen[x], k = (c >> 2) & 0x3F3F3F;		// (le fond : un quart de la lumiere)
			if (!a) { o[x] = k; continue; }
			uint32_t r = (((c >> 16) & 255) * a + ((k >> 16) & 255) * (255 - a)) / 255, g = (((c >> 8) & 255) * a + ((k >> 8) & 255) * (255 - a)) / 255, b = ((c & 255) * a + (k & 255) * (255 - a)) / 255;
			o[x] = r << 16 | g << 8 | b;
		}
	}
}
// un disque de couleur pose avec sa transparence (a : 0..1), le bord adouci
static void glow_disc (uint32_t *d, long pw, int w, int h, double cx, double cy, double r, unsigned col, double a)
{
	int x0 = (int) (cx - r - 1), x1 = (int) (cx + r + 1), y0 = (int) (cy - r - 1), y1 = (int) (cy + r + 1);
	if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 > w - 1) x1 = w - 1; if (y1 > h - 1) y1 = h - 1;
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++)
		{
			double e = r + 0.5 - sqrt ((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
			if (e <= 0) continue;
			int k = (int) (256 * a * (e < 1 ? e : 1));
			uint32_t &p = d[y * pw + x];
			p = ui_mix (col, p, k) & 0xFFFFFF;
		}
}
// les notes qui sonnent s'allument au point de l'image d'ou elles viennent : un eclat a l'attaque, la lueur tant que la
// note tient, puis elle s'eteint en une noire
static void glow (uint32_t *d, long pw, int w, int h)
{
	const uint32_t FADE = PPQ, BACK = 4 * PPQ * BEATS;
	lock ();
	double t = g_tick;
	for (int i = 0; i < NPART; i++)
	{
		if (g_style.p[i].mute) continue;
		const Track &tr = g_song.tr[i];
		for (int k = tr.find (t > BACK ? (uint32_t) t - BACK : 0); k < tr.count && tr.n[k].tick <= t; k++)
		{
			const Note &m = tr.n[k];
			if (m.kind != NK_NOTE || !m.pu) continue;
			double end = (double) m.tick + m.dur, age = t - m.tick, a, r;
			bool on = t < end;
			if (on)
			{
				double attack = 1 - age / (PPQ / 2.0);
				a = 1; r = 2.5 + m.vel / 127.0 * 3 + (attack > 0 ? attack : 0) * 5;
			}
			else
			{
				double f = (t - (end > m.tick ? end : m.tick)) / FADE;
				if (f >= 1) continue;
				a = 1 - f; r = 2 + 2 * a;
			}
			double x = (m.pu - 1) / 65534.0 * w, y = (m.pv - 1) / 65534.0 * h;
			glow_disc (d, pw, w, h, x, y, r * 2.2, PART_COL[i], a * 90 / 255);
			glow_disc (d, pw, w, h, x, y, r, PART_COL[i], a);
			if (on) glow_disc (d, pw, w, h, x, y, r * 0.4, 0xFFFFFF, 1);
		}
	}
	unlock ();
}

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
	if (g_mode != M_IDLE && !is_export ()) { reveal (d, pw, w, h, true); g_keep = true; }
	else if (g_keep) reveal (d, pw, w, h, false);
	else for (int y = 0; y < h; y++) memcpy (d + y * pw, g_scaled + (long) y * w, (size_t) w * 4);
	if (g_mode != M_IDLE && !is_export ()) glow (d, pw, w, h);
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
	if (g_tab != T_TRACE && kind == 1 && g_keep && g_mode == M_IDLE) { g_keep = false; ui_view_dirty (v_img); return 0; }
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
// la sequence et la grille d'accords, a la meme echelle : g_viewBars mesures a partir de g_viewFirst (0 : tout le morceau)
static const uint32_t BAR = PPQ * BEATS;
// le morceau, la grille et une mesure de plus, au moins seize mesures (sous le verrou)
static uint32_t roll_span ()
{
	uint32_t e = g_song.length (), c = (uint32_t) g_chords.length * BAR;
	if (c > e) e = c;
	uint32_t bars = (e + BAR - 1) / BAR + 1;
	return (bars < 16 ? 16 : bars) * BAR;
}
static void view_range (uint32_t &start, uint32_t &len)		// (sous le verrou)
{
	uint32_t span = roll_span ();
	if (g_viewBars <= 0 || (uint32_t) g_viewBars * BAR >= span) { start = 0; len = span; return; }
	len = (uint32_t) g_viewBars * BAR;
	int last = (int) ((span - len) / BAR);
	g_viewFirst = g_viewFirst < 0 ? 0 : g_viewFirst > last ? last : g_viewFirst;
	start = (uint32_t) g_viewFirst * BAR;
}
static void timeline () { ui_view_dirty (v_roll); ui_view_dirty (v_chords); }
static int tl_x (uint32_t tick, uint32_t start, uint32_t len, int w) { return (int) (((int64_t) tick - start) * w / (int64_t) len); }
// la mesure sous ce point de la vue
static int tl_bar (long mx, long w)
{
	lock (); uint32_t start, len; view_range (start, len); unlock ();
	if (mx < 0) mx = 0; if (mx >= w) mx = w - 1;
	return (int) ((start + (uint64_t) mx * len / (uint64_t) w) / BAR);
}
// molette : zoom autour de la souris ; Maj : defiler
static long tl_wheel (long v, long notches)
{
	long w = 0, h = 0; uikit_canvas_pixels (v, &w, &h);
	if (w <= 0 || !notches) return 0;
	lock ();
	uint32_t start, len; view_range (start, len);
	int bars = (int) (len / BAR), total = (int) (roll_span () / BAR), first = (int) (start / BAR);
	unlock ();
	if (ui_ev_field (&ui, EV_MODS) & UI_MOD_SHIFT)
	{
		if (g_viewBars <= 0) return 0;
		g_viewFirst = first - (notches > 0 ? 1 : -1) * (bars / 4 > 1 ? bars / 4 : 1);
	}
	else
	{
		double frac = (double) ui_ev_field (&ui, EV_X) / w;
		frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
		double at = first + frac * bars;
		int nb = notches > 0 ? (bars * 2 / 3 > 4 ? bars * 2 / 3 : 4) : bars * 3 / 2 + 1;
		if (nb >= total) { g_viewBars = 0; g_viewFirst = 0; }
		else { g_viewBars = nb; g_viewFirst = (int) floor (at - frac * nb + 0.5); }
	}
	timeline ();
	return 0;
}
static void ensure_visible (int bar)
{
	lock (); uint32_t start, len; view_range (start, len); unlock ();
	uint32_t t = (uint32_t) bar * BAR;
	if (t >= start && t + BAR <= start + len) return;
	g_viewFirst = bar - (int) (len / BAR) / 2;
	timeline ();
}
static long roll_paint (long v, long lw, long lh)
{
	int w = (int) lw, h = (int) lh;
	ui_fill (0, 0, w, h, jet (J_CREUX));
	long pw, ph; uint32_t *d = uikit_canvas_pixels (v, &pw, &ph);
	if (!d || w <= 0) return 0;
	lock ();
	uint32_t start, len; view_range (start, len);
	unsigned faint = ui_mix (jet (J_LIGNE), jet (J_CREUX), 110), strong = jet (J_LIGNE);
	bool every = (int64_t) w * BAR / len >= 8;
	for (uint32_t b = (start + BAR - 1) / BAR * BAR; b < start + len; b += BAR)
	{
		if (!every && b / BAR % 4) continue;
		int x = tl_x (b, start, len, w);
		unsigned c = b / BAR % 4 ? faint : strong;
		if (x > 0 && x < w) for (int y = 0; y < h; y++) d[y * pw + x] = c;
	}
	for (int i = 0; i < NPART; i++)
	{
		const Track &t = g_song.tr[i];
		unsigned c = PART_COL[i];
		for (int k = t.find (start > 8 * BAR ? start - 8 * BAR : 0); k < t.count && t.n[k].tick < start + len; k++)
		{
			const Note &m = t.n[k];
			if (m.kind != NK_NOTE || m.tick + m.dur < start) continue;
			int x0 = tl_x (m.tick, start, len, w), x1 = tl_x (m.tick + m.dur, start, len, w);
			int y = h - 2 - (m.key > 24 ? (m.key < 108 ? m.key - 24 : 83) : 0) * (h - 3) / 84;
			if (x0 < 0) x0 = 0;
			if (x1 >= w) x1 = w - 1;
			if (x1 == x0 + 1 || x1 == x0) x1 = x0 + (every ? 1 : 0);	// (un trait visible, meme court)
			uint32_t *o = d + (long) y * pw;
			for (int x = x0; x <= x1 && x < w; x++) o[x] = c;
		}
	}
	int cx = tl_x ((uint32_t) g_tick, start, len, w);
	if (cx >= 0 && cx < w) for (int y = 0; y < h; y++) d[y * pw + cx] = jet (J_TEXTE1);
	unlock ();
	return 0;
}
static long roll_mouse (long, long kind, long mx, long, long info)
{
	if ((kind != 1 && kind != 0) || !(info >> 8 & 0xFF) || g_mode == M_COMPOSE || is_export ()) return 0;
	long w = 0, h = 0; uikit_canvas_pixels (v_roll, &w, &h);
	if (w <= 0 || mx < 0) return 0;
	lock (); uint32_t start, len; view_range (start, len); unlock ();
	if (mx >= w) mx = w - 1;
	cmd_seek (start + (uint32_t) ((uint64_t) mx * len / (uint64_t) w));
	timeline ();
	return 0;
}

// ---- la grille d'accords, sous la sequence : une case par mesure. Un clic ouvre l'accord de la mesure ; le trait de
// couleur marque la fin de la grille (a tirer) ----
static void chord_edit (int bar);
static int g_edBar = -1;		// la mesure dans l'editeur d'accord
static long chords_paint (long v, long lw, long lh)
{
	int w = (int) lw, h = (int) lh;
	ui_fill (0, 0, w, h, jet (J_CREUX));
	if (w <= 0) return 0;
	static ChordGrid g;
	lock ();
	uint32_t start, len; view_range (start, len);
	unsigned mask = g_comp.mask; int root = g_comp.root;
	g = g_chords;
	double tick = g_tick;
	unlock ();
	double cw = (double) w * BAR / len;
	int b0 = (int) (start / BAR), b1 = (int) ((start + len + BAR - 1) / BAR);
	for (int b = b0; b < b1; b++)
	{
		int x0 = tl_x ((uint32_t) b * BAR, start, len, w), x1 = tl_x ((uint32_t) (b + 1) * BAR, start, len, w);
		int s = g.slot (b);
		bool inGrid = b < g.length, ghost = !inGrid && s >= 0;
		unsigned fill = b == g_edBar ? ui_mix (jet (J_ACCENT), jet (J_CREUX), 90) : inGrid ? jet (J_ELEVE) : ghost ? jet (J_SURFACE) : 0;
		if (fill && x1 - x0 > 2) ui_rbox (x0 + 1, 2, x1 - x0 - 2, h - 4, cw >= 16 ? 4 : 1, fill, fill);
		if (b % 4 == 0 && b > 0) ui_fill (x0, 6, 1, h - 12, jet (J_LIGNE_FORTE));
		Chord c; int at;
		if (g.find (b, c, at))
		{
			if (at == s)
			{
				char t[64];
				chord_roman (c, mask, root, t, sizeof t);
				ui_text_fit ((long) t, x0 + 5, h > 30 ? 17 : h / 2 + 5, ghost ? jet (J_TEXTE3) : jet (J_TEXTE1), 12, (long) ui.font_b, x1 - x0 - 8);
				if (h > 30) { chord_notes (c, mask, root, t, sizeof t); ui_text_fit ((long) t, x0 + 5, 31, jet (J_TEXTE3), 10, (long) ui.font, x1 - x0 - 8); }
			}
			else if (x1 - x0 > 12) ui_fill (x0 + 5, h / 2, x1 - x0 - 10, 2, jet (J_LIGNE_FORTE));	// l'accord d'avant continue
		}
		else if (s >= 0 && g.c[s].deg == CHORD_NC) ui_text_fit ((long) "N.C.", x0 + 5, h / 2 + 5, jet (J_TEXTE3), 11, (long) ui.font, x1 - x0 - 8);
	}
	if (g.length > 0)
	{
		int xe = tl_x ((uint32_t) g.length * BAR, start, len, w);
		if (xe >= 0 && xe <= w)
		{
			ui_fill (xe - 1, 0, 2, h, jet (J_ACCENT));
			if (g.loop) text ("↺", xe + 4, h - 6, jet (J_ACCENT), 12, 1);
		}
	}
	int cx = tl_x ((uint32_t) tick, start, len, w);
	if (cx >= 0 && cx < w) ui_fill (cx, 0, 1, h, jet (J_TEXTE1));
	return 0;
}
static int g_chPressed = -1;
static bool g_chDragEnd;
static bool near_end (long mx, long w)
{
	lock ();
	int n = g_chords.length;
	uint32_t start, len; view_range (start, len);
	unlock ();
	if (!n) return false;
	int x = tl_x ((uint32_t) n * BAR, start, len, (int) w);
	return mx > x - 6 && mx < x + 6;
}
static long chords_mouse (long, long kind, long mx, long, long info)
{
	long w = 0, h = 0; uikit_canvas_pixels (v_chords, &w, &h);
	if (w <= 0) return 0;
	bool down = (info >> 8 & 0xFF) != 0;
	if (kind == 1 && down)
	{
		if (near_end (mx, w)) { g_chDragEnd = true; return 0; }
		g_chPressed = tl_bar (mx, w);
	}
	else if (kind == 0 && g_chDragEnd && down)
	{
		lock ();
		uint32_t start, len; view_range (start, len);
		int n = (int) floor ((start + (double) (mx < 0 ? 0 : mx) / w * len) / BAR + 0.5);
		g_chords.length = n < 1 ? 1 : n > CHORDS_MAX ? CHORDS_MAX : n;
		unlock ();
		timeline ();
	}
	else if (kind == 2)
	{
		// (l'editeur s'ouvre au relache)
		if (g_chDragEnd) g_chDragEnd = false;
		else if (g_chPressed >= 0 && tl_bar (mx, w) == g_chPressed) chord_edit (g_chPressed);
		g_chPressed = -1;
	}
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
	ui_w_set_value (f_harm, p.harm & 3);
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
	else if (w == f_harm) p.harm = v < 0 ? 0 : v & 3;
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
	settings_refresh ();
}
// une image calculee (image.h) : calculee hors du verrou, la musique continue. set : ses reglages (0 : la graine
// choisit tout) ; quiet : sans message pendant le calcul (un curseur des reglages bouge)
static void settings_refresh ();
static void generate (const char *kind, uint32_t seed, const ImgSet *set = 0, bool quiet = false)
{
	snprintf (g_imgKind, sizeof g_imgKind, "%s", kind); g_imgSeed = seed;
	if (set) g_imgSet = *set; else g_imgSet.n = 0;
	int k = img_kind_index (kind);
	char t[96];
	if (!quiet) { snprintf (t, sizeof t, "Calcul de l'image : %s n° %u...", IMG_NAMES[k < 0 ? 0 : k], seed); status (t); ui_w_draw (g_status); ui_view_flush (); }
	Image im;
	image_make (im, kind, IMG_SIZE, IMG_SIZE, seed, &g_imgSet);
	image_set (im);
	snprintf (t, sizeof t, "Image : %s n° %u%s (F9 : une autre)", IMG_NAMES[k < 0 ? 0 : k], seed, g_imgSet.n ? ", réglée" : "");
	status (t);
	settings_refresh ();
}

// ---- les reglages de l'image calculee : une fenetre a part (Ctrl+I), une ligne par reglage de la sorte. Un reglage
// « a la graine » montre la valeur que la graine a tiree ; le bouger le regle a la main. L'image se recalcule un instant
// apres le dernier mouvement ----
enum { SET_ROWS = 5, SET_W = 380, SET_ROW_H = 64 };
static long g_setWin, s_head, s_another, s_reset, s_hint, s_lab[SET_ROWS], s_sl[SET_ROWS], s_val[SET_ROWS], s_seed[SET_ROWS], s_lo[SET_ROWS], s_hi[SET_ROWS];
static long g_setDue;			// le moment du recalcul demande (ms), 0 : rien
static long now_ms () { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static const ImgParam *set_params (int &n)
{
	const ImgParam *d = img_params (g_img.data ? "" : g_img.kind, n);
	while (n > 0 && d[n - 1].hidden) n--;		// (le cadrage se regle a la souris, pas ici)
	return d;
}
// le curseur <-> la valeur : les entiers tels quels, les autres en mille pas
static double set_value (const ImgParam &d, long s) { return d.isInt ? (double) s : nearbyint ((d.min + s / 1000.0 * (d.max - d.min)) * 1000) / 1000; }
static long set_slider (const ImgParam &d, double v) { return d.isInt ? (long) nearbyint (v) : (long) nearbyint ((v - d.min) / (d.max - d.min) * 1000); }
static void set_show_value (int i, const ImgParam &d, double v)
{
	char t[24];
	if (d.isInt) snprintf (t, sizeof t, "%d", (int) nearbyint (v));
	else if (d.min < 0) snprintf (t, sizeof t, v == 0 ? "0" : "%+.2f", v);
	else snprintf (t, sizeof t, "%.2f", v);
	ui_w_set_text (s_val[i], (long) t);
}
// la fenetre suit l'image : sa sorte, et les valeurs que la graine a prises
static void settings_refresh ()
{
	if (!g_setWin) return;
	long was = ui_window_use (g_setWin);
	int n; const ImgParam *defs = set_params (n);
	char t[96];
	if (g_img.data) snprintf (t, sizeof t, "Image ouverte : %s", g_img.file);
	else { int k = img_kind_index (g_img.kind); snprintf (t, sizeof t, "%s  n° %u", IMG_NAMES[k < 0 ? 0 : k], g_img.seed); }
	ui_w_set_text (s_head, (long) t);
	ui_w_set_text (s_hint, (long) (g_img.data ? "Une image ouverte se prend telle quelle : elle n'a pas de réglages." : "Cette sorte n'a pas encore de réglages : sa graine décide de tout. F9 en donne une autre."));
	ui_w_show (s_hint, n == 0);
	ui_w_enable (s_another, !g_img.data); ui_w_enable (s_reset, n > 0);
	for (int i = 0; i < SET_ROWS; i++)
	{
		bool on = i < n;
		long ws[6] = { s_lab[i], s_sl[i], s_val[i], s_seed[i], s_lo[i], s_hi[i] };
		for (int k = 0; k < 6; k++) ui_w_show (ws[k], on && (k < 4 || defs[i].low));
		if (!on) continue;
		const ImgParam &d = defs[i];
		double v, u;
		bool given = g_imgSet.get (d.key, v);
		if (!given) v = g_img.used.get (d.key, u) ? u : d.def;
		ui_w_set_text (s_lab[i], (long) d.label);
		ui_w_set_text (s_seed[i], (long) (d.fixed ? "Aucun" : "Graine"));
		if (d.low) { ui_w_set_text (s_lo[i], (long) d.low); ui_w_set_text (s_hi[i], (long) d.high); }
		if (d.isInt) ui_w_set_range (s_sl[i], (long) d.min, (long) d.max, 1, 1); else ui_w_set_range (s_sl[i], 0, 1000, 100, 1);
		ui_w_set_value (s_sl[i], set_slider (d, v));
		ui_w_set_value (s_seed[i], !given);
		set_show_value (i, d, v);
	}
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_w_draw (0);
	ui_update_all ();
	ui_window_use (was);
}
static void settings_open ()
{
	if (g_setWin) { ui_window_use (g_setWin); ui_w_focus (s_another); ui_window_use (g_win); settings_refresh (); return; }
	long scr[4] = { 1024, 768, 0, 0 }; ui_screen ((long) scr);
	int hgt = 100 + SET_ROWS * SET_ROW_H;
	if (hgt > scr[1] - scr[2] - scr[3] - 40) hgt = (int) (scr[1] - scr[2] - scr[3] - 40);
	long x = scr[0] - SET_W - 24; if (x < 0) x = 0;
	g_setWin = ui_window_ex (x, scr[3] + 60, SET_W, hgt, (long) "Réglages de l'image", 0);
	if (g_setWin <= 0) { g_setWin = 0; ui_window_use (g_win); return; }
	const int X = 16, SW = 210;
	s_head = ui_label (X, 14, SET_W - FRAME_CAP_W - X - 8, 24, (long) "", ULB_BOLD);
	s_another = ui_pushbutton (X, 46, 130, 28, (long) "Une autre (F9)", 0);
	s_reset = ui_pushbutton (X + 138, 46, 140, 28, (long) "Tout à la graine", 0);
	s_hint = ui_label (X, 90, SET_W - 2 * X, 44, (long) "", ULB_DIM | ULB_WRAP);
	for (int i = 0; i < SET_ROWS; i++)
	{
		int y = 90 + i * SET_ROW_H;
		s_lab[i] = ui_label (X, y, SW, 20, (long) "", ULB_DIM);
		s_sl[i] = ui_slider (X, y + 22, SW, 24, 0, 1000, 0);
		s_val[i] = ui_label (X + SW + 8, y + 24, 54, 20, (long) "", 0);
		s_seed[i] = ui_checkbox (X + SW + 64, y + 22, 84, 24, (long) "Graine", 1);
		s_lo[i] = ui_label (X, y + 44, SW / 2, 16, (long) "", ULB_DIM);
		s_hi[i] = ui_label (X + SW / 2, y + 44, SW / 2, 16, (long) "", ULB_DIM | ULB_RIGHT);
	}
	ui_window_use (g_win);
	settings_refresh ();
}
static void settings_close () { if (!g_setWin) return; ui_window_close (g_setWin); g_setWin = 0; g_setDue = 0; ui_window_use (g_win); }
// un widget des reglages -> true s'il etait a eux
static bool settings_widget (long w, long code)
{
	if (!g_setWin) return false;
	if (w == s_another && code == UN_ACTIVATE) { generate (g_imgKind, g_imgSeed + 1, &g_imgSet); return true; }
	if (w == s_reset && code == UN_ACTIVATE) { g_setDue = 0; generate (g_imgKind, g_imgSeed, 0); return true; }
	int n; const ImgParam *defs = set_params (n);
	for (int i = 0; i < n && i < SET_ROWS; i++)
	{
		const ImgParam &d = defs[i];
		if (w == s_sl[i] && code == UN_CHANGE)
		{
			double v = set_value (d, ui_w_get_value (w));
			g_imgSet.put (d.key, v);
			long was = ui_window_use (g_setWin);
			set_show_value (i, d, v); ui_w_draw (s_val[i]);
			if (ui_w_get_value (s_seed[i])) { ui_w_set_value (s_seed[i], 0); ui_w_draw (s_seed[i]); }
			ui_window_use (was);
			g_setDue = now_ms () + 60;
			return true;
		}
		if (w == s_seed[i] && code == UN_CHANGE)
		{
			if (ui_w_get_value (w)) g_imgSet.remove (d.key);
			else g_imgSet.put (d.key, set_value (d, ui_w_get_value (s_sl[i])));
			g_setDue = now_ms ();
			return true;
		}
	}
	return w == s_hint || w == s_head;
}
// (dans la boucle) le recalcul demande, quand le curseur s'est arrete un instant
static void settings_tick ()
{
	if (!g_setDue || now_ms () < g_setDue) return;
	g_setDue = 0;
	ImgSet s = g_imgSet;
	generate (g_imgKind, g_imgSeed, &s, true);
}
// ---- l'accord au piano, une seconde : la basse, puis l'accord autour du do du milieu ----
enum { CHORD_CH = 14 };
static int g_heard[12], g_nheard; static long g_hearEnd;
static void hear_off () { for (int i = 0; i < g_nheard; i++) syn (CHORD_CH, 0x80, g_heard[i], 0); g_nheard = 0; g_hearEnd = 0; }
static void audition (const Chord &c)
{
	if (c.deg < 1 || c.deg > 7) return;
	lock (); ChordTones t = chord_tones (c, g_comp.mask, g_comp.root); unlock ();
	hear_off ();
	syn (CHORD_CH, 0xC0, 0, 0); syn (CHORD_CH, 0xB0, 7, 100);
	g_heard[g_nheard++] = 36 + t.bass;
	int r = 55 + (t.root + 5) % 12;			// la fondamentale entre sol 3 et fa# 4
	for (int i = 0; i < t.n; i++) g_heard[g_nheard++] = r + t.iv[i] + (c.open && i == 1 ? 12 : 0);	// (open : la tierce a l'octave)
	for (int i = 0; i < g_nheard; i++) syn (CHORD_CH, 0x90, g_heard[i], 84);
	g_hearEnd = now_ms () + 1100;
}

// ---- l'accord d'une mesure : une fenetre a part. Chaque clic change l'accord et le fait entendre ----
static long g_edWin, e_title, e_name, e_deg, e_acc, e_qual, e_sus, e_sev, e_ext[9], e_inv, e_open, e_prev, e_next, e_hear, e_nc, e_empty, e_ok,
	e_copy, e_paste, e_ins, e_del, e_end, e_loop;
static Chord g_edChord, g_clip; static bool g_hasClip;
// l'accord de la case ; une case vide montre l'accord qui continue (rien n'est ecrit avant un clic)
static void ed_load ()
{
	lock ();
	g_edChord = g_chords.c[g_edBar];
	if (g_edChord.empty ()) { Chord p; int at; if (g_chords.find (g_edBar, p, at)) g_edChord = p; else { memset (&g_edChord, 0, sizeof g_edChord); g_edChord.deg = 1; } }
	if (g_edChord.deg == CHORD_NC) g_edChord.deg = 0;
	unlock ();
}
static void ed_show ()
{
	if (!g_edWin) return;
	long was = ui_window_use (g_edWin);
	lock ();
	Chord here = g_chords.c[g_edBar]; unsigned mask = g_comp.mask; int root = g_comp.root; bool loop = g_chords.loop;
	unlock ();
	const Chord &c = g_edChord;
	bool written = !here.empty ();
	char rn[64], nn[64], t[160];
	chord_roman (c, mask, root, rn, sizeof rn); chord_notes (c, mask, root, nn, sizeof nn);
	snprintf (t, sizeof t, "Mesure %d : %s", g_edBar + 1, here.deg == CHORD_NC ? "sans accord" : written ? rn : "vide");
	ui_w_set_text (e_title, (long) t);
	if (here.deg == CHORD_NC) snprintf (t, sizeof t, "La composition y est libre.");
	else if (written) snprintf (t, sizeof t, "%s", nn);
	else if (c.deg > 0) snprintf (t, sizeof t, "L'accord d'avant continue (%s) : un clic en écrit un ici.", rn);
	else snprintf (t, sizeof t, "Choisissez un degré.");
	ui_w_set_text (e_name, (long) t);
	ui_w_set_value (e_deg, written && here.deg != CHORD_NC ? c.deg - 1 : -1);
	ui_w_set_value (e_acc, c.acc + 1); ui_w_set_value (e_qual, c.qual); ui_w_set_value (e_sus, c.sus);
	ui_w_set_value (e_sev, c.sev); ui_w_set_value (e_inv, c.inv); ui_w_set_value (e_open, c.open);
	for (int i = 0; i < 9; i++) ui_w_set_value (e_ext[i], (c.ext >> i) & 1);
	ui_w_set_value (e_loop, loop);
	ui_w_enable (e_paste, g_hasClip); ui_w_enable (e_copy, written && here.deg != CHORD_NC);
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_w_draw (0);
	ui_update_all ();
	ui_window_use (was);
	timeline ();
}
// l'accord change : il s'ecrit dans la case et s'entend
static void ed_write (const Chord &x)
{
	Chord y = x;
	if (y.deg == 0) y.deg = 1;
	lock (); g_chords.set (g_edBar, y); unlock ();
	if (y.deg != CHORD_NC) { g_edChord = y; audition (y); }
	if (g_mode == M_COMPOSE) g_dirty = true;
	ed_show ();
}
static void ed_clear () { lock (); memset (&g_chords.c[g_edBar], 0, sizeof (Chord)); unlock (); ed_load (); ed_show (); }
static void chord_edit (int bar)
{
	g_edBar = bar < 0 ? 0 : bar >= CHORDS_MAX ? CHORDS_MAX - 1 : bar;
	ed_load ();
	if (!g_edWin)
	{
		enum { EW = 640, EH = 432 };
		long scr[4] = { 1024, 768, 0, 0 }; ui_screen ((long) scr);
		long w = EW < scr[0] ? EW : scr[0], h = EH < scr[1] - scr[2] - scr[3] ? EH : scr[1] - scr[2] - scr[3];
		g_edWin = ui_window_ex ((scr[0] - w) / 2, scr[1] - scr[2] - h - 8 > scr[3] ? scr[1] - scr[2] - h - 8 : scr[3], w, h, (long) "Accord", 0);
		if (g_edWin <= 0) { g_edWin = 0; ui_window_use (g_win); return; }
		const int X = 16, C = 104;
		e_title = ui_label (X, 12, EW - FRAME_CAP_W - X - 8, 24, (long) "", ULB_TITLE);
		e_name = ui_label (X, 36, EW - 2 * X, 20, (long) "", ULB_DIM);
		static const char *const L[7] = { "Degré", "Altération", "Qualité", "Sus", "Septième", "Couleur", "Basse" };
		for (int i = 0; i < 7; i++) ui_label (X, 68 + i * 34, C - 8, 22, (long) L[i], ULB_DIM);
		e_deg = ui_segmented (C, 64, 0, 28, (long) "I|II|III|IV|V|VI|VII");
		e_acc = ui_segmented (C, 98, 0, 28, (long) "♭|♮|♯");
		e_qual = ui_segmented (C, 132, 0, 28, (long) "De la gamme|Majeur|Mineur|Diminué|Augmenté|Quinte");
		e_sus = ui_segmented (C, 166, 0, 28, (long) "—|sus2|sus4");
		e_sev = ui_segmented (C, 200, 0, 28, (long) "—|De la gamme|7|maj7|6|°7");
		for (int i = 0; i < 9; i++) e_ext[i] = ui_checkbox (C + i * 58, 236, 56, 24, (long) EXT_NAMES[i], 0);
		e_inv = ui_segmented (C, 268, 0, 28, (long) "Fondamentale|1er renv.|2e renv.|3e renv.|Voice leading");
		e_open = ui_checkbox (C, 302, 240, 24, (long) "Open voicing (les voix écartées)", 0);
		ui_w_tip (e_inv, (long) "Voice leading : la basse et les voix de chaque partie vont à la note de l'accord la plus proche");
		int y = 340;
		e_prev = ui_pushbutton (X, y, 36, 28, (long) "◀", 0); ui_w_tip (e_prev, (long) "Mesure précédente (←)");
		e_next = ui_pushbutton (X + 40, y, 36, 28, (long) "▶", 0); ui_w_tip (e_next, (long) "Mesure suivante (→)");
		e_hear = ui_pushbutton (X + 90, y, 84, 28, (long) "Écouter", 0); ui_w_tip (e_hear, (long) "Faire entendre l'accord (Espace)");
		e_nc = ui_pushbutton (X + 180, y, 110, 28, (long) "Sans accord", 0); ui_w_tip (e_nc, (long) "La composition redevient libre à partir de cette mesure");
		e_empty = ui_pushbutton (X + 296, y, 70, 28, (long) "Vider", 0); ui_w_tip (e_empty, (long) "La mesure prolonge l'accord d'avant (Suppr)");
		e_ok = ui_pushbutton (EW - X - 80, y, 80, 28, (long) "OK", 0); ui_w_style (e_ok, UB_DEFAULT);
		y += 36;
		e_copy = ui_pushbutton (X, y, 70, 28, (long) "Copier", 0);
		e_paste = ui_pushbutton (X + 74, y, 70, 28, (long) "Coller", 0);
		e_ins = ui_pushbutton (X + 148, y, 76, 28, (long) "Insérer", 0); ui_w_tip (e_ins, (long) "Insérer une mesure vide ici");
		e_del = ui_pushbutton (X + 228, y, 90, 28, (long) "Supprimer", 0); ui_w_tip (e_del, (long) "Supprimer la mesure de la grille");
		e_end = ui_pushbutton (X + 322, y, 92, 28, (long) "Finir ici", 0); ui_w_tip (e_end, (long) "La grille finit après cette mesure");
		e_loop = ui_checkbox (X + 424, y + 2, 150, 24, (long) "Boucler la grille", 1);
		ui_label (X, y + 34, EW - 2 * X, 18, (long) "Clavier : 1 à 7 le degré, ← → les mesures, Suppr vide, Espace écoute.", ULB_DIM);
		ui_window_use (g_win);
	}
	ensure_visible (g_edBar);
	ed_show ();
}
static void ed_close () { if (!g_edWin) return; ui_window_close (g_edWin); g_edWin = 0; g_edBar = -1; ui_window_use (g_win); timeline (); }
static void ed_move (int d) { int b = g_edBar + d; if (b >= 0 && b < CHORDS_MAX) chord_edit (b); }
// un widget de l'editeur -> true s'il etait a lui
static bool ed_widget (long w, long code)
{
	if (!g_edWin) return false;
	Chord x = g_edChord;
	int v = (int) ui_w_get_value (w);
	if (code == UN_CHANGE)
	{
		if (w == e_deg) { if (v >= 0) { x.deg = v + 1; ed_write (x); } }
		else if (w == e_acc) { x.acc = v - 1; ed_write (x); }
		else if (w == e_qual) { x.qual = v; ed_write (x); }
		else if (w == e_sus) { x.sus = v; ed_write (x); }
		else if (w == e_sev) { x.sev = v; ed_write (x); }
		else if (w == e_inv) { x.inv = v; ed_write (x); }
		else if (w == e_open) { x.open = v != 0; ed_write (x); }
		else if (w == e_loop) { lock (); g_chords.loop = v != 0; unlock (); timeline (); }
		else
		{
			for (int i = 0; i < 9; i++)
				if (w == e_ext[i])
				{
					int bit = 1 << i;
					x.ext ^= bit;
					// une seule neuvieme, une seule onzieme, une seule treizieme, une seule quinte alteree
					static const int GROUPS[4][3] = { { EXT_9, EXT_B9, EXT_S9 }, { EXT_11, EXT_S11, 0 }, { EXT_13, EXT_B13, 0 }, { EXT_B5, EXT_S5, 0 } };
					for (int g = 0; g < 4; g++)
						if (GROUPS[g][0] == bit || GROUPS[g][1] == bit || GROUPS[g][2] == bit)
							for (int k = 0; k < 3; k++) if (GROUPS[g][k] && GROUPS[g][k] != bit) x.ext &= ~GROUPS[g][k];
					ed_write (x);
					return true;
				}
			return false;
		}
		return true;
	}
	if (code != UN_ACTIVATE) return false;
	int b = g_edBar;
	if (w == e_prev) ed_move (-1);
	else if (w == e_next) ed_move (1);
	else if (w == e_hear) audition (g_edChord);
	else if (w == e_nc) { Chord n; memset (&n, 0, sizeof n); n.deg = CHORD_NC; ed_write (n); }
	else if (w == e_empty) ed_clear ();
	else if (w == e_ok) ed_close ();
	else if (w == e_copy) { lock (); g_clip = g_chords.c[b]; unlock (); g_hasClip = true; ed_show (); }
	else if (w == e_paste) { if (g_hasClip) ed_write (g_clip); }
	else if (w == e_ins || w == e_del)
	{
		lock ();
		ChordGrid &g = g_chords;
		if (b < g.length)
		{
			if (w == e_ins)
			{
				memmove (&g.c[b + 1], &g.c[b], sizeof (Chord) * (size_t) (CHORDS_MAX - b - 1));
				memset (&g.c[b], 0, sizeof (Chord));
				if (g.length < CHORDS_MAX) g.length++;
			}
			else
			{
				memmove (&g.c[b], &g.c[b + 1], sizeof (Chord) * (size_t) (CHORDS_MAX - b - 1));
				memset (&g.c[CHORDS_MAX - 1], 0, sizeof (Chord));
				g.length--;
			}
		}
		unlock ();
		ed_load (); ed_show ();
	}
	else if (w == e_end) { lock (); g_chords.length = b + 1; unlock (); ed_show (); }
	else return false;
	return true;
}
// une touche dans l'editeur -> true si prise
static bool ed_key (int hid)
{
	if (hid >= 0x1E && hid <= 0x24) { Chord x = g_edChord; x.deg = hid - 0x1E + 1; ed_write (x); }	// 1 a 7 (rangee du haut)
	else if (hid >= 0x59 && hid <= 0x5F) { Chord x = g_edChord; x.deg = hid - 0x59 + 1; ed_write (x); }	// pave numerique
	else if (hid == KEY_LEFT) ed_move (-1);
	else if (hid == KEY_RIGHT) ed_move (1);
	else if (hid == KEY_DELETE || hid == KEY_BACKSPACE) ed_clear ();
	else if (hid == 0x2C) audition (g_edChord);				// espace
	else if (hid == KEY_ENTER || hid == KEY_ESC) ed_close ();
	else return false;
	return true;
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
// la forme du prereglage ouvert (0 : libre, une seule coulee) ; elle se garde pour recomposer et s'enregistre avec lui
static MusicForm *g_form;
static void on_compose ()
{
	if (is_export ()) return;
	if (g_mode == M_COMPOSE) { cmd_stop (); status ("Composition arrêtée. Le morceau peut s'enregistrer ou s'exporter."); return; }
	if (!g_keepSeed) g_seed = (uint32_t) time (0) * 2654435761u ^ (uint32_t) clock ();
	bool any = false;
	for (int i = 0; i < NPART; i++) if (g_style.p[i].on) any = true;
	if (g_form) for (int l = 0; l < 26; l++) if (g_form->def[l].style && g_form->uses (l)) for (int i = 0; i < NPART; i++) if (g_form->def[l].style->p[i].on) any = true;
	if (!any) { msg ("Composer", "Aucune partie n'est allumée dans ce style."); return; }
	if (g_ifs.n < 2) { msg ("Composer", "Le tracé doit avoir au moins deux transformations."); return; }
	cmd_stop ();
	lock (); g_comp.mask = cur_mask (); g_comp.root = g_root; unlock ();
	cmd_compose (g_seed);
	lock (); if (g_comp.rec) snprintf (g_comp.rec->scaleName, sizeof g_comp.rec->scaleName, "%s", g_scales.n ? g_scales.s[g_scale].name : ""); unlock ();
	g_dirty = true;
	char t[MusicForm::MAX_SECTIONS * 12 + 120], ft[MusicForm::MAX_SECTIONS * 12];
	if (g_form) { g_form->text (ft, sizeof ft); snprintf (t, sizeof t, "Composition en cours (graine %u) : forme %s, %u mesures", g_seed, ft, g_form->length (PPQ * BEATS) / (PPQ * BEATS)); }
	else snprintf (t, sizeof t, "Composition en cours (graine %u)", g_seed);
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
	else r->from_settings (g_seed, g_song.tempo, g_song.copyright, cur_mask (), g_root, g_scales.n ? g_scales.s[g_scale].name : "", g_style, g_ifs, g_img, &g_chords, g_form);
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
	if (r->ch[0].chords) g_chords = *r->ch[0].chords; else { g_chords.clear (); g_chords.loop = true; }
	image_free (g_img);
	g_img = first; first.px = 0; first.data = 0;
	g_comp.mask = r->mask; g_comp.root = r->root;
	delete g_form; g_form = r->form ? r->form->clone () : 0;
	g_comp.formIn = g_form;
	uint32_t seed = r->seed;
	char sname[32]; snprintf (sname, sizeof sname, "%s", r->scaleName);
	delete g_recipe;
	g_recipe = r->length ? r : 0;
	g_tick = 0;
	unlock ();
	if (!g_recipe) delete r;
	if (!g_img.data) { snprintf (g_imgKind, sizeof g_imgKind, "%s", g_img.kind); g_imgSeed = g_img.seed; g_imgSet = g_img.set; }
	g_viewBars = VIEW_BARS; g_viewFirst = 0;
	settings_refresh ();
	if (g_edWin) { if (g_edBar < 0) g_edBar = 0; ed_load (); ed_show (); }
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
	timeline (); ui_view_dirty (v_parts);
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
	case M_REWIND: if (g_mode != M_COMPOSE && !is_export ()) { cmd_seek (0); g_viewFirst = 0; timeline (); } break;
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
	case M_IMGGEN: generate (g_imgKind, g_imgSeed + 1, &g_imgSet); break;
	case M_IMGSET: settings_open (); break;
	case M_CHORDEDIT: { int b = (int) ((uint32_t) g_tick / BAR); chord_edit (b); break; }
	case M_CHORDCLEAR:
		if (!g_chords.length) break;
		if (ui_message ((long) "Accords", (long) "Effacer toute la grille d'accords ?", UM_YESNO) == 1)
		{
			lock (); g_chords.clear (); unlock ();
			if (g_edWin) { ed_load (); ed_show (); }
		}
		after_dialog ();
		timeline ();
		break;
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
		if (c >= M_GEN0 && c < M_GEN0 + NKINDS)
		{
			const char *k = IMG_KINDS[c - M_GEN0];
			bool same = !strcmp (k, g_imgKind);
			generate (k, same ? g_imgSeed + 1 : 1, same ? &g_imgSet : 0);
		}
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
	int fh = 228;
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
	lab (b, c1, y + 197, lw, "Harmonie");
	f_harm = drop (b, c1 + lw, y + 197, W - lw, "L'accord aux temps, passages entre|Les notes de l'accord seulement|La basse de l'accord aux temps|Libre : ignore les accords");
	ui_w_tip (f_harm, (long) "Comment la partie suit la grille d'accords (sous la séquence)");
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
	long iw = px - MARGIN - 8, ih = h - TOPBAR - ROLL_H - CHORD_H - FOOT - 14;
	ui_w_move (v_img, MARGIN, TOPBAR, iw, ih);
	ui_w_move (v_roll, MARGIN, TOPBAR + ih + 4, iw, ROLL_H);
	ui_w_move (v_chords, MARGIN, TOPBAR + ih + 4 + ROLL_H + 3, iw, CHORD_H);
	ui_w_move (g_status, MARGIN, h - FOOT - 2, w - 2 * MARGIN, 22);
	ui_w_move (g_tabs, px, py, PANEL_W, h - py - FOOT - 6);
	// (ui_w_move ne deplace pas les pages des onglets : elles garderaient la place de leur creation et leur fond
	// se peindrait par-dessus l'image)
	for (int i = 0; i < 5; i++) ui_w_move (g_pages[i], px, py + 40, PANEL_W, h - py - FOOT - 46);
	long ox = px + 8, oy = py + 44;
	for (int i = 0; i < g_npl; i++) ui_w_move (g_pl[i].w, ox + g_pl[i].x, oy + g_pl[i].y, g_pl[i].ww, g_pl[i].hh);
	image_changed ();
	timeline ();
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
		else
		{
			ui_view_dirty (v_img);
			// (la lecture sort de la vue : la vue la suit)
			lock ();
			uint32_t start, len; view_range (start, len);
			if (g_viewBars > 0 && (t < start || t >= start + len)) { g_viewFirst = (int) (t / BAR); ui_view_dirty (v_chords); }
			unlock ();
		}
		if (++frame % 3 == 0) { timeline (); if (g_mode == M_COMPOSE && ui_w_get_value (g_tabs) == T_STYLE) ui_view_dirty (v_parts); }
	}
	if (g_hearEnd && now_ms () >= g_hearEnd) hear_off ();
	settings_tick ();
	bool pend = g_mode == M_COMPOSE && (g_comp.stylePending || g_comp.scalePending);
	if (wasPending && !pend) { form_load (); }
	wasPending = pend;
}
static void on_widget (long w, long code)
{
	if (ed_widget (w, code) || settings_widget (w, code)) return;
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
	ui_min_size (720, 540);
	ui_menu ((long)
		"MMorceau\n" "I1\tNouveau\tCtrl+N\n" "I2\tOuvrir...\tCtrl+O\n" "I3\tEnregistrer\tCtrl+S\n" "I4\tEnregistrer sous...\n" "-\n"
		"I5\tExporter en MIDI...\n" "I6\tExporter en FM Song...\n" "I7\tExporter en WAV...\n" "I8\tExporter en MP3...\n" "-\n"
		"I30\tOuvrir un préréglage...\tCtrl+Maj+O\n" "I31\tEnregistrer le préréglage...\tCtrl+Maj+S\n" "-\n"
		"I9\tCopyright...\n" "-\n" "I10\tQuitter\tCtrl+Q\n"
		"MComposition\n" "I11\tComposer\tF5\n" "I12\tLire\tF6\n" "I13\tArrêter\tF7\n" "I14\tAu début\tF8\n" "-\n" "I15\tGarder la même graine\n" "-\n"
		"I41\tAccord de la mesure...\tF2\n" "I42\tEffacer la grille d'accords\n"
		"MStyle\n" "I16\tNouveau\tF3\n" "I17\tOuvrir...\n" "I18\tEnregistrer\n" "I19\tEnregistrer sous...\n" "I20\tDossier des styles...\n"
		"MTracé\n" "I21\tNouveau\tF4\n" "I22\tOuvrir...\n" "I23\tEnregistrer\n" "I24\tEnregistrer sous...\n" "I25\tDossier des tracés...\n"
		"MImage\n" "I26\tOuvrir...\n" "-\n"
		"I32\tMandala\n" "I33\tMandelbrot\n" "I34\tJulia\n" "I35\tBurning Ship\n" "I36\tLyapunov\n" "I37\tNewton\n" "I38\tRayons\n"
		"I27\tUne autre de la même sorte\tF9\n" "I40\tRéglages de l'image...\tCtrl+I\n" "-\n" "I28\tCouleur du repère...\n"
		"MAide\n" "I29\tÀ propos d'Arabesque\n");
	mkdir (HOMEDIR, 0755);

	g_comp.song = &g_song; g_comp.style = &g_style; g_comp.ifs = &g_ifs; g_comp.img = &g_img; g_comp.chords = &g_chords;
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
	v_roll = ui_view (0, 0, 10, 10, 0); ui_view_on (v_roll, (long) roll_paint, (long) roll_mouse, 0, (long) tl_wheel);
	v_chords = ui_view (0, 0, 10, 10, 0); ui_view_on (v_chords, (long) chords_paint, (long) chords_mouse, 0, (long) tl_wheel);
	ui_w_tip (v_chords, (long) "Les accords, mesure par mesure : un clic ouvre l'accord de la mesure. Molette : zoom, Maj+molette : défiler.");
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
		long ew = ui_ev_field (&ui, EV_WIN);
		if (t == WEV_CLOSE)
		{
			if (g_edWin && ew == g_edWin) ed_close ();
			else if (g_setWin && ew == g_setWin) settings_close ();
			else if (confirm ()) break;
			continue;
		}
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_DROP) on_drop ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE);
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) on_widget (w, code);
			if (press && !taken)
			{
				// (les deux fenetres a part ont leurs touches ; les autres sont celles des menus)
				bool done = false;
				if (g_edWin && ew == g_edWin) done = ed_key (hid);
				else if (g_setWin && ew == g_setWin && hid == KEY_ESC) { settings_close (); done = true; }
				if (!done)
				{
					long id = ui_menu_key (0);
					if (id >= 0) command ((int) id);
				}
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
