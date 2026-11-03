//
// ui.h -- Media Player's drawing helpers (Onyx's Apps/media/ui.h, on UIKit): the theme's tokens, the faces (sizes of the
// kit's fonts), text that fits, the times, the icons (drawn from their geometry by ui_vp_*), a veil over the pixels.
// Everything draws in the view being painted (ui_view): begin_paint gives its pixels to what UIKit does not do.
//
#ifndef _media_ui_h
#define _media_ui_h

#include <math.h>
#include "sys.h"

namespace media {

static struct ui_vars ui;

// ---- the theme: UIKit's tokens, never theme.txt -------------------------------------------------------------
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
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned tone (unsigned c, int level) { return (unsigned) ui_tone (c, level) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned c) { return (unsigned) ui_ink (c) & 0xFFFFFF; }

// ---- the view being painted -----------------------------------------------------------------------------------
static uint32_t *g_px; static long g_pw, g_ph;
static inline void begin_paint (long view) { g_px = uikit_canvas_pixels (view, &g_pw, &g_ph); }
// a colour over the pixels, a of 256 (Onyx's fills with an alpha)
static void veil (int x, int y, int w, int h, unsigned c, int a)
{
	int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > g_pw ? (int) g_pw : x + w, y1 = y + h > g_ph ? (int) g_ph : y + h;
	unsigned rb = (c & 0xFF00FF) * (unsigned) a, gg = (c & 0xFF00) * (unsigned) a; int na = 256 - a;
	for (int j = y0; j < y1; j++)
	{
		uint32_t *p = g_px + (long) j * g_pw;
		for (int i = x0; i < x1; i++)
		{
			unsigned d = p[i];
			p[i] = ((((d & 0xFF00FF) * (unsigned) na + rb) >> 8) & 0xFF00FF) | (((d & 0xFF00) * (unsigned) na + gg) >> 8 & 0xFF00);
		}
	}
}
// the corners of a rectangle rounded: what is outside goes to bg (a square's picture on a card)
static void round_corners (int x, int y, int w, int h, int r, unsigned bg)
{
	if (r <= 0) return;
	for (int k = 0; k < 4; k++)
		for (int j = 0; j < r; j++)
			for (int i = 0; i < r; i++)
			{
				int dx = r - i, dy = r - j, d2 = dx * dx + dy * dy, in = (r - 1) * (r - 1), out = r * r;
				if (d2 <= in) continue;
				int a = d2 >= out ? 256 : (d2 - in) * 256 / (out - in);
				veil (k & 1 ? x + w - 1 - i : x + i, k & 2 ? y + h - 1 - j : y + j, 1, 1, bg, a);
			}
}
static inline void hline (int x, int y, int w, unsigned c) { ui_fill (x, y, w, 1, c); }
static inline void rbox (int x, int y, int w, int h, int r, unsigned top, unsigned bot) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, top, bot); }
static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { rbox (x, y, w, h, r, c, c); }
// a rounded box with its outline
static void panel (int x, int y, int w, int h, int r, unsigned fill, unsigned line)
{
	rbox (x, y, w, h, r, line); rbox (x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, fill);
}
#define V(a) ((a) * 16)
static inline void disc (int cx, int cy, int r, unsigned c) { ui_vp_disc (V (cx), V (cy), V (r), c); }

// ---- the faces --------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_BIG, F_H2, F_H1, F_N };
static const int FACE_PX[F_N] = { 13, 11, 15, 16, 20, 27 };
static inline long face (int style) { return style == 2 ? (long) ui.font_b : (long) ui.font; }
static inline int tw (const char *s, int f = F_UI, int style = 0) { return (int) ui_text_width ((long) s, FACE_PX[f], face (style)); }
static inline int fh (int f = F_UI) { return FACE_PX[f] * 5 / 4; }
// s at x, the line's top y, cut with "..." past w (0: not cut)
static inline void text (int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	int px = FACE_PX[f];
	if (w > 0) ui_text_fit ((long) s, x, y + px, c, px, face (style), w);
	else ui_text ((long) s, x, y + px, c, px, face (style), 0);
}
// centred vertically in a box of height h
static inline void text_v (int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_r (int xr, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (xr - tw (s, f, style), y + (h - fh (f)) / 2, s, c, f, style); }
static inline void text_c (int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style, w > 0 ? w : 0); }

static inline void fmt_time (char *b, int cap, long long ms)
{
	if (ms < 0) ms = 0;
	int s = (int) (ms / 1000);
	if (s >= 3600) snprintf (b, cap, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60); else snprintf (b, cap, "%d:%02d", s / 60, s % 60);
}
static inline void fmt_long (char *b, int cap, long long ms)
{
	int m = (int) ((ms + 30000) / 60000);
	if (m >= 60) snprintf (b, cap, "%d h %02d min", m / 60, m % 60); else snprintf (b, cap, "%d min", m);
}

// ---- the icons --------------------------------------------------------------------------------------------
enum { I_HOME, I_PERSON, I_DISC, I_NOTE, I_TAG, I_FOLDER, I_HEART, I_HEART_O, I_CLOCK, I_LIST, I_PLUS, I_SEARCH, I_PLAY, I_PAUSE,
       I_PREV, I_NEXT, I_SHUFFLE, I_REPEAT, I_REPEAT1, I_VOLUME, I_MUTE, I_QUEUE, I_MINI, I_MORE, I_BACK, I_FWD, I_DOWN, I_EXPAND,
       I_CLOSE, I_GRID, I_ROWS, I_FILM, I_TV, I_FULL, I_UNFULL, I_BACK10, I_FWD10, I_CHECK };

// a pen on a box of 24 units (the geometry is Onyx's)
struct Pen
{
	int X, Y, u; unsigned c;
	int px (int a) const { return X + a * u; }
	int py (int b) const { return Y + b * u; }
	void poly (const int *xy, int n) const
	{
		int t[40]; if (n > 20) n = 20;
		for (int i = 0; i < n; i++) { t[2 * i] = px (xy[2 * i]); t[2 * i + 1] = py (xy[2 * i + 1]); }
		ui_vp_poly ((long) t, n, c);
	}
	void rect (int x, int y, int w, int h) const { int t[] = { x, y, x + w, y, x + w, y + h, x, y + h }; poly (t, 4); }
	void line (int a, int b, int d, int e, int th) const { ui_vp_line (px (a), py (b), px (d), py (e), th * u, c); }
	void polyline (const int *xy, int n, int th) const { for (int i = 0; i + 1 < n; i++) line (xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], th); }
	void circle (int x, int y, int r) const { ui_vp_disc (px (x), py (y), r * u, c); }
	void arc (int x, int y, int r, int a0, int a1, int th) const
	{	// (in pieces of 30 degrees)
		float d = 3.14159265f / 180;
		int n = (a1 - a0) / 30 + 1;
		for (int i = 0; i < n; i++)
		{
			float s = (a0 + (a1 - a0) * i / n) * d, e = (a0 + (a1 - a0) * (i + 1) / n) * d;
			ui_vp_line (px (x) + (int) (r * u * cosf (s)), py (y) + (int) (r * u * sinf (s)), px (x) + (int) (r * u * cosf (e)), py (y) + (int) (r * u * sinf (e)), th * u, c);
		}
	}
};
// `hole`: the colour under the icon (the holes of a tag, of a film)
static void icon (int id, int x, int y, int s, unsigned c, unsigned hole = 0)
{
	Pen p = { V (x), V (y), V (s) / 24, c };
	Pen h = p; h.c = hole;
	switch (id)
	{
	case I_HOME: { int t[] = { 12, 2, 23, 12, 20, 12, 20, 22, 14, 22, 14, 15, 10, 15, 10, 22, 4, 22, 4, 12, 1, 12 }; p.poly (t, 11); } break;
	case I_PERSON: p.circle (12, 7, 5); { int t[] = { 4, 22, 5, 16, 9, 14, 15, 14, 19, 16, 20, 22 }; p.poly (t, 6); } break;
	case I_DISC: p.arc (12, 12, 9, 0, 360, 3); p.circle (12, 12, 3); break;
	case I_NOTE: p.circle (8, 18, 4); p.rect (10, 3, 2, 15); { int t[] = { 10, 3, 20, 6, 20, 10, 12, 7 }; p.poly (t, 4); } break;
	case I_TAG: { int t[] = { 2, 2, 13, 2, 22, 12, 12, 22, 2, 13 }; p.poly (t, 5); h.circle (7, 7, 2); } break;
	case I_FOLDER: p.c = 0xD6AA5A; p.rect (1, 6, 22, 15); p.rect (1, 3, 9, 5); break;
	case I_HEART: p.circle (8, 9, 6); p.circle (16, 9, 6); { int t[] = { 3, 12, 21, 12, 12, 21 }; p.poly (t, 3); } break;
	case I_HEART_O: p.arc (8, 9, 5, 180, 340, 2); p.arc (16, 9, 5, 200, 360, 2); p.line (3, 10, 12, 20, 2); p.line (21, 10, 12, 20, 2); break;
	case I_CLOCK: p.arc (12, 12, 9, 0, 360, 2); { int t[] = { 12, 6, 12, 12, 16, 15 }; p.polyline (t, 3, 2); } break;
	case I_LIST: for (int k = 0; k < 3; k++) { p.rect (8, 5 + k * 6, 14, 2); p.circle (4, 6 + k * 6, 2); } break;
	case I_PLUS: p.rect (11, 4, 2, 16); p.rect (4, 11, 16, 2); break;
	case I_SEARCH: p.arc (10, 10, 6, 0, 360, 2); p.line (15, 15, 21, 21, 3); break;
	case I_PLAY: { int t[] = { 7, 4, 20, 12, 7, 20 }; p.poly (t, 3); } break;
	case I_PAUSE: p.rect (6, 4, 4, 16); p.rect (14, 4, 4, 16); break;
	case I_PREV: p.rect (4, 5, 3, 14); { int t[] = { 20, 5, 20, 19, 8, 12 }; p.poly (t, 3); } break;
	case I_NEXT: p.rect (17, 5, 3, 14); { int t[] = { 4, 5, 4, 19, 16, 12 }; p.poly (t, 3); } break;
	case I_SHUFFLE:
		{ int a[] = { 2, 7, 8, 7, 15, 17, 19, 17 }; p.polyline (a, 4, 2);
		  int b[] = { 2, 17, 8, 17, 15, 7, 19, 7 }; p.polyline (b, 4, 2);
		  int c1[] = { 18, 4, 23, 7, 18, 10 }; p.poly (c1, 3); int c2[] = { 18, 14, 23, 17, 18, 20 }; p.poly (c2, 3); } break;
	case I_REPEAT: case I_REPEAT1:
		{ int a[] = { 4, 12, 4, 7, 19, 7 }; p.polyline (a, 3, 2);
		  int b[] = { 20, 12, 20, 17, 5, 17 }; p.polyline (b, 3, 2);
		  int c1[] = { 18, 4, 23, 7, 18, 10 }; p.poly (c1, 3); int c2[] = { 6, 14, 1, 17, 6, 20 }; p.poly (c2, 3); }
		if (id == I_REPEAT1) p.rect (11, 9, 2, 6);
		break;
	case I_VOLUME: case I_MUTE:
		{ int t[] = { 2, 9, 7, 9, 13, 3, 13, 21, 7, 15, 2, 15 }; p.poly (t, 6); }
		if (id == I_VOLUME) { p.arc (13, 12, 5, -45, 45, 2); p.arc (13, 12, 9, -45, 45, 2); }
		else { p.line (16, 8, 23, 16, 2); p.line (23, 8, 16, 16, 2); }
		break;
	case I_QUEUE: for (int k = 0; k < 3; k++) p.rect (2, 5 + k * 6, 12, 2); { int t[] = { 16, 11, 23, 16, 16, 21 }; p.poly (t, 3); } break;
	case I_MINI: p.rect (2, 4, 20, 2); p.rect (2, 18, 20, 2); p.rect (2, 4, 2, 16); p.rect (20, 4, 2, 16); p.rect (11, 11, 8, 6); break;
	case I_MORE: for (int k = 0; k < 3; k++) p.circle (5 + k * 7, 12, 2); break;
	case I_BACK: { int t[] = { 15, 4, 7, 12, 15, 20 }; p.polyline (t, 3, 3); } break;
	case I_FWD: { int t[] = { 9, 4, 17, 12, 9, 20 }; p.polyline (t, 3, 3); } break;
	case I_DOWN: { int t[] = { 4, 8, 12, 16, 20, 8 }; p.polyline (t, 3, 3); } break;
	case I_EXPAND: p.rect (3, 3, 18, 2); p.rect (3, 19, 18, 2); p.rect (3, 3, 2, 18); p.rect (19, 3, 2, 18); break;
	case I_CLOSE: p.line (5, 5, 19, 19, 3); p.line (19, 5, 5, 19, 3); break;
	case I_GRID: for (int k = 0; k < 4; k++) p.rect (3 + (k % 2) * 10, 3 + (k / 2) * 10, 8, 8); break;
	case I_ROWS: for (int k = 0; k < 3; k++) p.rect (3, 4 + k * 7, 18, 3); break;
	case I_FILM:
		p.rect (2, 4, 20, 16);
		for (int k = 0; k < 4; k++) { h.circle (5, 7 + k * 3 + (k > 1 ? 1 : 0), 1); h.circle (19, 7 + k * 3 + (k > 1 ? 1 : 0), 1); }
		break;
	case I_TV: p.rect (2, 5, 20, 2); p.rect (2, 17, 20, 2); p.rect (2, 5, 2, 14); p.rect (20, 5, 2, 14); p.line (8, 1, 12, 5, 2); p.line (16, 1, 12, 5, 2); p.rect (8, 20, 8, 2); break;
	case I_FULL: case I_UNFULL:
		for (int k = 0; k < 4; k++)
		{
			int cx = k % 2 ? 21 : 3, cy = k / 2 ? 21 : 3, dx = k % 2 ? -1 : 1, dy = k / 2 ? -1 : 1;
			if (id == I_UNFULL) { cx = k % 2 ? 15 : 9; cy = k / 2 ? 15 : 9; dx = -dx; dy = -dy; }
			int t[] = { cx, cy + 6 * dy, cx, cy, cx + 6 * dx, cy }; p.polyline (t, 3, 2);
		}
		break;
	case I_BACK10: { int t[] = { 12, 5, 12, 19, 2, 12 }; p.poly (t, 3); int q[] = { 22, 5, 22, 19, 12, 12 }; p.poly (q, 3); } break;
	case I_FWD10: { int t[] = { 2, 5, 2, 19, 12, 12 }; p.poly (t, 3); int q[] = { 12, 5, 12, 19, 22, 12 }; p.poly (q, 3); } break;
	case I_CHECK: { int t[] = { 4, 12, 10, 18, 20, 6 }; p.polyline (t, 3, 3); } break;
	}
}
// a few bars moving (the song playing)
static void eq_bars (int x, int y, int s, unsigned c, unsigned t)
{
	for (int k = 0; k < 4; k++)
	{
		float v = sinf ((float) (t * 37 + k * 71) * 3.14159265f / 180);
		int h = s * (30 + (int) ((v + 1) * 35)) / 100;
		ui_fill (x + k * s * 26 / 100, y + s - h, s * 18 / 100 + 1, h, c);
	}
}

} // namespace media

#endif
