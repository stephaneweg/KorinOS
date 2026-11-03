//
// ui.h -- Courier's drawing helpers on UIKit, as Mail's ui.h: the theme's tokens (never theme.txt), the faces, text cut
// to fit, the few icons (their geometry drawn by ui_vp_*), what a hand-drawn view asks of the loop. Everything draws in
// the ui_view being painted, in its coordinates. Every widget is placed in window coordinates (UIKit's panels only
// show and hide what is in them).
//
#ifndef _courier_ui_h
#define _courier_ui_h

#include <math.h>
#include <stdint.h>
#include <initializer_list>
#include "tools.h"

namespace cr {

static struct ui_vars ui;

// ---- the theme ------------------------------------------------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned bg) { return (unsigned) ui_ink (bg) & 0xFFFFFF; }
static inline int luma (unsigned c) { return (54 * (int) (c >> 16 & 255) + 183 * (int) (c >> 8 & 255) + 19 * (int) (c & 255)) >> 8; }
#define C_FACE		jet (J_FACE)
#define C_FIELD		jet (J_SURFACE)
#define C_SIDE		jet (J_CREUX)
#define C_TEXT		jet (J_TEXTE1)
#define C_DIM		jet (J_TEXTE2)
#define C_FAINT		jet (J_TEXTE3)
#define C_LINE		jet (J_LIGNE)
#define C_ACCENT	jet (J_ACCENT)
// a colour of its own (a method, a status) that reads on the theme's ground
static inline unsigned on_field (unsigned c) { return luma (C_FACE) < 110 ? mix (c, 0xFFFFFF, 110) : c; }
static inline unsigned col_accent_text () { return luma (C_ACCENT) < 110 && luma (C_FACE) < 110 ? mix (C_ACCENT, C_TEXT, 120) : C_ACCENT; }
static const unsigned C_OK = 0x007F31, C_WARN = 0xAD7A03, C_BAD = 0xC0392B, C_BLUE = 0x0053B8;
static inline unsigned method_color (const char *m)
{
	static const unsigned C[M_COUNT] = { 0x007F31, 0xAD7A03, 0x0053B8, 0x623497, 0x8E1A10, 0x007F31, 0xA61468 };
	return on_field (C[method_index (m)]);
}
static inline unsigned status_color (int s)
{
	if (s < 0) return on_field (C_BAD);
	if (s < 300) return on_field (C_OK);
	if (s < 400) return on_field (C_BLUE);
	if (s < 500) return on_field (C_WARN);
	return on_field (C_BAD);
}
static inline int ctl_h () { static const int H[3] = { 32, 32, 28 }; long e = ui_echelle (); return H[e > 2 ? 2 : e]; }

// ---- a widget's text ------------------------------------------------------------------------------------------
static inline const char *tx (long w) { return (const char *) ui_w_get_text (w); }
static inline void set_text (long w, const char *s) { ui_w_set_text (w, (long) s); }
static inline void in_panel (long w, long panel) { if (panel) ui_w_set_parent (w, panel); }

// ---- the view being painted -----------------------------------------------------------------------------------
static uint32_t *g_px; static long g_pw, g_ph;
static inline void begin_paint (long view) { g_px = uikit_canvas_pixels (view, &g_pw, &g_ph); }
static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, c, c); }
#define V(a) ((a) * 16)

// ---- the faces ------------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_H2, F_H1, F_N };
static const int FACE_PX[F_N] = { 13, 11, 14, 17, 22 };
static inline long face (int style) { return style ? (long) ui.font_b : (long) ui.font; }
static inline int tw (const char *s, int f = F_UI, int style = 0) { return (int) ui_text_width ((long) s, FACE_PX[f], face (style)); }
static inline int fh (int f = F_UI) { return FACE_PX[f] * 5 / 4; }
static inline int btn_w (const char *s) { return tw (s, F_UI, 1) + 26; }		// a button that holds its word, translated
// s at x, the line's top y; cut with "..." past w (0: not cut)
static inline void text (int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	int px = FACE_PX[f];
	ui_text_fit ((long) s, x, y + px, c, px, face (style), w > 0 ? w : 4000);
}
static inline void text_v (int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_c (int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style); }
static inline int text_wrap (int x, int y, int w, const char *s, unsigned c, int f = F_UI)
{
	return (int) ui_text_wrap ((long) s, x, y, w, c, face (0), FACE_PX[f], 0);
}

// ---- the icons ---------------------------------------------------------------------------------------------------
enum { I_SEND, I_PLUS, I_CLOSE, I_CHEV_R, I_CHEV_D, I_GEAR, I_CHECK, I_DOT, I_HISTORY, I_FOLDER, I_COLL, I_ENV, I_GLOBE, I_MORE };
static void icon (int id, int x, int y, int s, unsigned c, unsigned hole = 0)
{
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
		void polyline (const int *xy, int n, int th, bool closed = false) const
		{
			for (int i = 0; i + 1 < n; i++) line (xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3], th);
			if (closed) line (xy[2 * n - 2], xy[2 * n - 1], xy[0], xy[1], th);
		}
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
	Pen p = { V (x), V (y), V (s) / 24, c };
	Pen h = p; h.c = hole;
	switch (id)
	{
	case I_SEND: { int t[] = { 3, 4, 22, 12, 3, 20, 6, 12 }; p.poly (t, 4); } break;
	case I_PLUS: p.rect (5, 11, 14, 2); p.rect (11, 5, 2, 14); break;
	case I_CLOSE: p.line (6, 6, 18, 18, 2); p.line (18, 6, 6, 18, 2); break;
	case I_CHEV_R: { int t[] = { 9, 5, 16, 12, 9, 19 }; p.polyline (t, 3, 2); } break;
	case I_CHEV_D: { int t[] = { 5, 9, 12, 16, 19, 9 }; p.polyline (t, 3, 2); } break;
	case I_GEAR:
		{ static const int D[8][2] = { { 0, -9 }, { 6, -6 }, { 9, 0 }, { 6, 6 }, { 0, 9 }, { -6, 6 }, { -9, 0 }, { -6, -6 } };
		  for (int k = 0; k < 8; k++) p.circle (12 + D[k][0], 12 + D[k][1], 2);
		  p.circle (12, 12, 8); h.circle (12, 12, 3); } break;
	case I_CHECK: { int t[] = { 4, 12, 10, 18, 20, 6 }; p.polyline (t, 3, 3); } break;
	case I_DOT: p.circle (12, 12, 5); break;
	case I_HISTORY: p.arc (12, 12, 9, 0, 360, 2); p.line (12, 12, 12, 6, 2); p.line (12, 12, 16, 14, 2); break;
	case I_FOLDER: p.rect (1, 6, 22, 15); p.rect (1, 3, 9, 5); break;
	case I_COLL: p.rect (2, 7, 20, 14); p.rect (2, 4, 9, 4); h.rect (6, 12, 12, 2); break;
	case I_ENV: p.arc (12, 12, 9, 0, 360, 2); p.circle (12, 12, 4); break;
	case I_GLOBE: p.arc (12, 12, 9, 0, 360, 2); p.line (3, 12, 21, 12, 2); p.arc (12, 12, 4, 0, 360, 2); break;
	case I_MORE: for (int k = 0; k < 3; k++) p.circle (5 + k * 7, 12, 2); break;
	}
}

// ---- what a view's click asks of the loop: a dialogue or a popup opens out of ui_w_event ---------------------------
enum { L_NONE, L_BAR, L_DOC, L_DOC_NEW, L_DOC_CLOSE, L_DOC_MENU, L_SIDE_HEAD, L_SIDE_RAIL, L_SIDE_ROW, L_SIDE_MENU };
static struct { int who, a, b, x, y; } g_later;
static inline void later (int who, int a = 0, int b = 0, int x = 0, int y = 0) { g_later.who = who; g_later.a = a; g_later.b = b; g_later.x = x; g_later.y = y; }

// the short name of a method (a badge)
static inline const char *method_short (const char *m)
{
	if (s_eqi (m, "DELETE")) return "DEL";
	if (s_eqi (m, "OPTIONS")) return "OPT";
	if (s_eqi (m, "PATCH")) return "PTCH";
	return m;
}

} // namespace cr

#endif
