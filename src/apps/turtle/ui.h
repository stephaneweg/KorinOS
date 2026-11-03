//
// ui.h -- Turtle Quest's drawing helpers on UIKit: the theme's tokens, text, and the game's pieces (a star, a gem, a
// portal's pad, a ring), drawn by ui_vp_* in the view being painted. No colour comes from theme.txt: what is not a
// token of the theme is the game's own object (a coin, a gem, the pen's colours).
//
#ifndef _turtle_ui_h
#define _turtle_ui_h

#include <math.h>
#include <stdint.h>
#include <uikit.h>

static struct ui_vars ui;

// ---- the theme: UIKit's tokens ----------------------------------------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
#define C_BG		jet (J_FACE)
#define C_FIELD		jet (J_SURFACE)
#define C_TEXT		jet (J_TEXTE1)
#define C_DIM		jet (J_TEXTE2)
#define C_LINE		jet (J_LIGNE)
#define C_ACCENT	jet (J_ACCENT)
#define C_GOLD		jet (J_ALERTE)
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned c) { return (unsigned) ui_ink (c) & 0xFFFFFF; }
static inline int luma (unsigned c) { return (int) (((c >> 16 & 255) * 3 + (c >> 8 & 255) * 6 + (c & 255)) / 10); }
static inline bool dark_theme () { return luma (C_FIELD) < 128; }

static inline void rbox (int x, int y, int w, int h, int r, unsigned top, unsigned bot) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, top, bot); }
static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { rbox (x, y, w, h, r, c, c); }
static inline void panel (int x, int y, int w, int h, int r, unsigned fill, unsigned line) { rbox (x, y, w, h, r, line); rbox (x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, fill); }

// ---- text ----------------------------------------------------------------------------------------------------------
enum { PX_UI = 13, PX_SMALL = 11, PX_BIG = 18 };
static inline long f_ui () { return (long) ui.font; }
static inline long f_bold () { return (long) ui.font_b; }
static inline long f_mono () { return (long) ui.font_m; }
static inline int tw (const char *s, int px = PX_UI, long font = 0) { return (int) ui_text_width ((long) s, px, font ? font : f_ui ()); }
static inline int fh (int px = PX_UI) { return px * 5 / 4; }
// s at x, the line's top y, cut with "..." past w (0: not cut)
static inline void text (int x, int y, const char *s, unsigned c, int px = PX_UI, long font = 0, int w = 0)
{
	if (!font) font = f_ui ();
	if (w > 0) ui_text_fit ((long) s, x, y + px, c, px, font, w);
	else ui_text ((long) s, x, y + px, c, px, font, 0);
}
static inline void text_v (int x, int y, int h, const char *s, unsigned c, int px = PX_UI, long font = 0, int w = 0) { text (x, y + (h - fh (px)) / 2, s, c, px, font, w); }
static inline void text_c (int x, int y, int w, int h, const char *s, unsigned c, int px = PX_UI, long font = 0) { text (x + (w - tw (s, px, font)) / 2, y + (h - fh (px)) / 2, s, c, px, font); }
// wrapped at w, -> its height (measure: nothing drawn)
static inline int wrap (const char *s, int x, int y, int w, unsigned c, int px = PX_UI, long font = 0, bool measure = false)
{
	return (int) ui_text_wrap ((long) s, x, y, w, c, font ? font : f_ui (), px, measure ? 1 : 0);
}

// ---- pieces -----------------------------------------------------------------------------------------------------------
#define V(a) ((int) ((a) * 16))
static inline void disc (double cx, double cy, double r, unsigned c) { ui_vp_disc (V (cx), V (cy), V (r), c); }
static inline void seg (double a, double b, double c, double d, double th, unsigned col) { ui_vp_line (V (a), V (b), V (c), V (d), V (th), col); }
static const double DEG = 3.14159265358979 / 180;

// A ring (drawn by short lines: what is under it stays)
static void ring_line (double cx, double cy, double r, double th, unsigned c)
{
	const int N = 28;
	for (int i = 0; i < N; i++)
	{
		double a = i * 2 * 3.14159265358979 / N, b = (i + 1) * 2 * 3.14159265358979 / N;
		seg (cx + r * cos (a), cy + r * sin (a), cx + r * cos (b), cy + r * sin (b), th, c);
	}
}
static void star (int cx, int cy, int r, unsigned c, bool filled)
{
	int p[20];
	for (int i = 0; i < 10; i++)
	{
		double rr = i & 1 ? r * 2.0 / 5 : r, a = i * 36 * DEG;
		p[2 * i] = V (cx + sin (a) * rr); p[2 * i + 1] = V (cy - cos (a) * rr);
	}
	if (filled) { ui_vp_poly ((long) p, 10, c); return; }
	for (int i = 0; i < 10; i++) { int j = (i + 1) % 10; ui_vp_line (p[2 * i], p[2 * i + 1], p[2 * j], p[2 * j + 1], 20, c); }
}
// The gems' colours, 1 ... 9, their digit's ink (the game's objects)
static const unsigned GEM_C[10] = { 0, 0xD0342C, 0xE8822A, 0xF2C230, 0x2E9E44, 0x1E9FAF, 0x2F6FD0, 0x9C3FB5, 0xE07BEF, 0xF4F4F4 };
static const unsigned GEM_INK[10] = { 0, 0xFFFFFF, 0xFFFFFF, 0x4A3500, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0x5A1D66, 0x505050 };
// A gem: a cut stone (5 points, a darker rim, a lighter table), its number on it when the cell is big enough; k: its size
static void gem (int cx, int cy, int s, int n, double k = 1, bool digit = true)
{
	if (n < 1 || n > 9 || k < 0.02) return;
	double r = s * 0.34 * k;
	unsigned c = GEM_C[n];
	int a = V (r * 0.62), b = V (r * 0.12), R = V (r), X = V (cx), Y = V (cy);
	int P[10] = { X - a, Y - a, X + a, Y - a, X + R, Y - b, X, Y + R, X - R, Y - b };
	ui_vp_poly ((long) P, 5, mix (c, 0x000000, n == 9 ? 70 : 50));
	int Q[10] = { P[0] + 16, P[1] + 16, P[2] - 16, P[3] + 16, P[4] - 20, P[5], P[6], P[7] - 24, P[8] + 20, P[9] };	// (1 px inside)
	ui_vp_poly ((long) Q, 5, c);
	int tb = V (r * 0.42);
	int TB[8] = { Q[0], Q[1], Q[2], Q[3], X + tb, Y - b, X - tb, Y - b };
	ui_vp_poly ((long) TB, 4, mix (c, 0xFFFFFF, 110));
	if (digit && s >= 16 && k > 0.6) { char d[2] = { (char) ('0' + n), 0 }; text_c (cx - 8, cy - (int) (r * 0.5) - 1, 16, (int) (r * 1.2), d, GEM_INK[n], s >= 28 ? 13 : 11, f_bold ()); }
}
// The portals' pairs: T (pair 1) cyan, U (pair 2) magenta -- the ring, the pad's face
static const unsigned PAD_RING[2] = { 0x1E9FAF, 0x9C3FB5 }, PAD_FACE[2] = { 0x9FE9F1, 0xF0C2F7 };
// A portal's pad (q: the pair, 0 T or 1 U): a round pad in its pair's colour, a ring in it, one dot (pair 1) or two (pair
// 2) -- told apart not by the colour alone; glow (0 ... 1): a halo fading out over `floor` (the jump)
static void pad (int cx, int cy, int s, int q, double glow = 0, unsigned floor_ = 0)
{
	double r = s * 0.40;
	if (glow > 0) disc (cx, cy, r + s * 0.25 * glow, mix (PAD_FACE[q], floor_, (int) (56 + 200 * glow)));
	disc (cx, cy, r, PAD_RING[q]);
	disc (cx, cy, r - s / 9.0, PAD_FACE[q]);
	ring_line (cx, cy, r * 0.55, s / 20.0 + 0.5, mix (PAD_RING[q], PAD_FACE[q], 70));
	double dr = s / 16 + 1;
	if (q == 0) disc (cx, cy, dr, PAD_RING[q]);
	else { disc (cx - dr - 1, cy, dr, PAD_RING[q]); disc (cx + dr + 1, cy, dr, PAD_RING[q]); }
}

#endif
