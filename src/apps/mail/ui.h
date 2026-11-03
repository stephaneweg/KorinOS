//
// ui.h -- Mail's drawing helpers on UIKit (Onyx's Apps/mail/ui.h): the theme's tokens, the faces, text cut to fit, the
// icons (their geometry is Onyx's, drawn by ui_vp_*), the round avatars with their initials, the hit lists of what is
// drawn by hand. Everything draws in the ui_view being painted, in its coordinates.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. See util.h for the full notice.
//
#ifndef _mail_ui_h
#define _mail_ui_h

#include <math.h>
#include <stdint.h>
#include <uikit.h>
#include "util.h"

namespace mailapp {

static struct ui_vars ui;

// ---- the theme: UIKit's tokens, never theme.txt -------------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned bg) { return (unsigned) ui_ink (bg) & 0xFFFFFF; }
static inline int luma (unsigned c) { return (54 * (int) (c >> 16 & 255) + 183 * (int) (c >> 8 & 255) + 19 * (int) (c & 255)) >> 8; }
#define C_FACE		jet (J_FACE)
#define C_FIELD		jet (J_SURFACE)
#define C_TEXT		jet (J_TEXTE1)
#define C_DIM		jet (J_TEXTE2)
#define C_FAINT		jet (J_TEXTE3)
#define C_LINE		jet (J_LIGNE)
#define C_ACCENT	jet (J_ACCENT)
#define C_STAR		jet (J_ALERTE)
static inline unsigned col_sel () { return mix (C_FIELD, C_ACCENT, 70); }
static inline unsigned col_hover () { return mix (C_FIELD, C_TEXT, 14); }
static inline unsigned col_accent_text () { return luma (C_ACCENT) < 110 && luma (C_FIELD) < 110 ? mix (C_ACCENT, C_TEXT, 120) : C_ACCENT; }
static inline unsigned col_danger_text () { return mix (jet (J_DANGER), C_TEXT, 80); }

static inline int imin (int a, int b) { return a < b ? a : b; }
static inline int imax (int a, int b) { return a > b ? a : b; }
static inline int ctl_h () { static const int H[3] = { 32, 32, 28 }; long e = ui_echelle (); return H[e > 2 ? 2 : e]; }

// ---- the view being painted: its pixels, for what UIKit does not do -----------------------------------------
static uint32_t *g_px; static long g_pw, g_ph;
static inline void begin_paint (long view) { g_px = uikit_canvas_pixels (view, &g_pw, &g_ph); }
static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, c, c); }
static void outline (int x, int y, int w, int h, int r, unsigned fill, unsigned line) { rbox (x, y, w, h, r, line); rbox (x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, fill); }
#define V(a) ((a) * 16)

// ---- the faces --------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_H2, F_H1, F_TINY, F_N };
static const int FACE_PX[F_N] = { 13, 11, 14, 17, 22, 10 };
static inline long face (int style) { return style ? (long) ui.font_b : (long) ui.font; }
static inline int tw (const char *s, int f = F_UI, int style = 0) { return (int) ui_text_width ((long) s, FACE_PX[f], face (style)); }
static inline int fh (int f = F_UI) { return FACE_PX[f] * 5 / 4; }
// s at x, the line's top y; cut with "..." past w (0: not cut)
static inline void text (int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	int px = FACE_PX[f];
	ui_text_fit ((long) s, x, y + px, c, px, face (style), w > 0 ? w : 4000);
}
static inline void text_v (int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_c (int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style); }

// ---- the hit lists ---------------------------------------------------------------------------------------------------------
struct Hit { int x, y, w, h, kind, a, b; };
struct HitList
{
	Hit h[300]; int n;
	HitList () : n (0) {}
	void clear () { n = 0; }
	void add (int x, int y, int w, int hh, int kind, int a = 0, int b = 0) { if (n < 300) h[n++] = Hit { x, y, w, hh, kind, a, b }; }
	const Hit *at (int x, int y) const { for (int i = n - 1; i >= 0; i--) if (x >= h[i].x && y >= h[i].y && x < h[i].x + h[i].w && y < h[i].y + h[i].h) return &h[i]; return 0; }
};

// ---- the icons -----------------------------------------------------------------------------------------------------------------
enum { I_PEN, I_REPLY, I_REPLYALL, I_FORWARD, I_ARCHIVE, I_TRASH, I_JUNK, I_STAR, I_STAR_O, I_REFRESH, I_SEARCH, I_INBOX, I_SENT,
       I_DRAFTS, I_FOLDER, I_PERSON, I_GEAR, I_CLIP, I_SEND, I_CLOSE, I_CHEV_R, I_CHEV_D, I_PLUS, I_UNIFIED, I_DOT, I_PICTURE, I_FILE, I_CHECK,
       I_WARN, I_MORE, I_BACK };

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
// `hole`: the colour under the icon (the holes of a bin, of a box)
static void icon (int id, int x, int y, int s, unsigned c, unsigned hole = 0)
{
	Pen p = { V (x), V (y), V (s) / 24, c };
	Pen h = p; h.c = hole;
	switch (id)
	{
	case I_PEN: { int t[] = { 4, 20, 5, 15, 16, 4, 20, 8, 9, 19 }; p.poly (t, 5); } break;
	case I_REPLY: { int t[] = { 21, 19, 21, 15, 18, 12, 5, 12 }; p.polyline (t, 4, 2); int g[] = { 10, 6, 4, 12, 10, 18 }; p.polyline (g, 3, 2); } break;
	case I_REPLYALL: { int t[] = { 23, 19, 23, 15, 20, 12, 9, 12 }; p.polyline (t, 4, 2); int g[] = { 13, 6, 7, 12, 13, 18 }; p.polyline (g, 3, 2); int e[] = { 8, 6, 2, 12, 8, 18 }; p.polyline (e, 3, 2); } break;
	case I_FORWARD: { int t[] = { 3, 19, 3, 15, 6, 12, 19, 12 }; p.polyline (t, 4, 2); int g[] = { 14, 6, 20, 12, 14, 18 }; p.polyline (g, 3, 2); } break;
	case I_ARCHIVE: p.rect (2, 4, 20, 5); p.rect (4, 10, 16, 11); h.rect (9, 13, 6, 2); break;
	case I_TRASH: p.rect (3, 5, 18, 2); p.rect (9, 2, 6, 3); p.rect (5, 8, 14, 14); h.rect (9, 11, 2, 8); h.rect (13, 11, 2, 8); break;
	case I_JUNK: p.arc (12, 12, 9, 0, 360, 2); p.line (6, 18, 18, 6, 2); break;
	case I_STAR: case I_STAR_O:
	{
		static const int CS[10][2] = { { 0, -100 }, { 59, -81 }, { 95, -31 }, { 95, 31 }, { 59, 81 }, { 0, 100 }, { -59, 81 }, { -95, 31 }, { -95, -31 }, { -59, -81 } };
		int t[20];
		for (int k = 0; k < 10; k++) { int r = k & 1 ? 5 : 11; t[2 * k] = 12 + CS[k][0] * r / 100; t[2 * k + 1] = 13 + CS[k][1] * r / 100; }
		if (id == I_STAR)
		{
			p.circle (12, 13, 5);				// (ui_vp_poly: pair-impair, a star's point at a time)
			for (int k = 0; k < 10; k += 2) { int q[] = { t[2 * k], t[2 * k + 1], t[(2 * k + 2) % 20], t[(2 * k + 3) % 20], t[(2 * k + 18) % 20], t[(2 * k + 19) % 20] }; p.poly (q, 3); }
		}
		else p.polyline (t, 10, 2, true);
		break;
	}
	case I_REFRESH: p.arc (12, 12, 8, 30, 320, 2); { int t[] = { 16, 2, 22, 7, 15, 10 }; p.poly (t, 3); } break;
	case I_SEARCH: p.arc (10, 10, 6, 0, 360, 2); p.line (15, 15, 21, 21, 3); break;
	case I_INBOX:
		{ int t[] = { 2, 13, 7, 13, 9, 16, 15, 16, 17, 13, 22, 13 }; p.polyline (t, 6, 2);
		  int o[] = { 2, 13, 5, 4, 19, 4, 22, 13, 22, 20, 2, 20 }; p.polyline (o, 6, 2, true); } break;
	case I_SENT: { int t[] = { 2, 11, 22, 3, 15, 21, 11, 13 }; p.poly (t, 4); } break;
	case I_SEND: { int t[] = { 3, 4, 22, 12, 3, 20, 6, 12 }; p.poly (t, 4); } break;
	case I_DRAFTS:
		{ int t[] = { 4, 2, 14, 2, 20, 8, 20, 22, 4, 22 }; p.polyline (t, 5, 2, true);
		  p.line (8, 12, 16, 12, 2); p.line (8, 17, 14, 17, 2); } break;
	case I_FOLDER: p.rect (1, 6, 22, 15); p.rect (1, 3, 9, 5); break;
	case I_PERSON: p.circle (12, 8, 5); { int t[] = { 3, 22, 5, 16, 9, 14, 15, 14, 19, 16, 21, 22 }; p.poly (t, 6); } break;
	case I_GEAR:
		{ static const int D[8][2] = { { 0, -9 }, { 6, -6 }, { 9, 0 }, { 6, 6 }, { 0, 9 }, { -6, 6 }, { -9, 0 }, { -6, -6 } };
		  for (int k = 0; k < 8; k++) p.circle (12 + D[k][0], 12 + D[k][1], 2);
		  p.circle (12, 12, 8); h.circle (12, 12, 3); } break;
	case I_CLIP: p.line (9, 8, 9, 17, 2); p.arc (12, 17, 3, 0, 180, 2); p.line (15, 17, 15, 5, 2); p.arc (11, 5, 4, 180, 360, 2); p.line (7, 5, 7, 19, 2); p.arc (11, 19, 4, 0, 180, 2); break;
	case I_CLOSE: p.line (6, 6, 18, 18, 2); p.line (18, 6, 6, 18, 2); break;
	case I_CHEV_R: { int t[] = { 9, 5, 16, 12, 9, 19 }; p.polyline (t, 3, 2); } break;
	case I_CHEV_D: { int t[] = { 5, 9, 12, 16, 19, 9 }; p.polyline (t, 3, 2); } break;
	case I_BACK: { int t[] = { 15, 5, 8, 12, 15, 19 }; p.polyline (t, 3, 2); } break;
	case I_PLUS: p.rect (5, 11, 14, 2); p.rect (11, 5, 2, 14); break;
	case I_UNIFIED:
		{ int o[] = { 2, 13, 5, 4, 19, 4, 22, 13, 22, 20, 2, 20 }; p.poly (o, 6);
		  int t[] = { 4, 13, 8, 13, 10, 16, 14, 16, 16, 13, 20, 13 }; h.polyline (t, 6, 2); } break;
	case I_DOT: p.circle (12, 12, 5); break;
	case I_PICTURE: p.rect (2, 4, 20, 16); { int t[] = { 4, 18, 10, 10, 14, 15, 16, 13, 20, 18 }; h.poly (t, 5); h.circle (16, 8, 2); } break;
	case I_FILE: { int t[] = { 5, 2, 14, 2, 19, 7, 19, 22, 5, 22 }; p.poly (t, 5); } break;
	case I_CHECK: { int t[] = { 4, 12, 10, 18, 20, 6 }; p.polyline (t, 3, 3); } break;
	case I_WARN: { int t[] = { 12, 2, 23, 21, 1, 21 }; p.poly (t, 3); h.rect (11, 8, 2, 7); h.rect (11, 17, 2, 2); } break;
	case I_MORE: for (int k = 0; k < 3; k++) p.circle (5 + k * 7, 12, 2); break;
	}
}

// ---- the avatars: a coloured disc, the initials -----------------------------------------------------------------------------
// (Onyx's ten colours: hues, not the theme's; the initials take the ink that reads on them)
static unsigned avatar_colour (const char *key)
{
	static const unsigned C[10] = { 0xC5504B, 0x3C8DA8, 0x3D7A4B, 0x8E5BB5, 0xC77C2E, 0x2F6FB0, 0x8A6D3B, 0xB0466E, 0x4B7F86, 0x5E6B7A };
	unsigned h = 2166136261u; for (const char *p = key; p && *p; p++) h = (h ^ (unsigned char) (*p | 32)) * 16777619u;
	return C[h % 10];
}
// "Marie Dubois" -> "MD", "jonas.peeters@x" -> "JP"
static void initials (const char *name, char *out)
{
	int k = 0; bool start = true;
	for (const char *p = name; *p && k < 6; p++)
	{
		unsigned char c = (unsigned char) *p;
		if (c == '@') break;
		if (c == ' ' || c == '.' || c == '_' || c == '-' || c == '"' || c == '\'') { start = true; continue; }
		if (start)
		{
			if (c >= 'a' && c <= 'z') c -= 32;
			if (c >= 0xC0) { int l = c >= 0xE0 ? 3 : 2; for (int i = 0; i < l && p[i]; i++) out[k++] = p[i]; p += l - 1; if (l == 2 && (unsigned char) out[k - 2] == 0xC3 && (unsigned char) out[k - 1] >= 0xA0) out[k - 1] -= 0x20; }
			else out[k++] = (char) c;
			start = false;
			if ((k >= 2 && (unsigned char) out[0] < 0x80) || k >= 4) break;
		}
	}
	out[k] = 0;
}
static void avatar (int cx, int cy, int r, const char *name, const char *key)
{
	unsigned c = avatar_colour (key && *key ? key : name);
	ui_vp_disc (V (cx), V (cy), V (r), c);
	char in[8]; initials (name, in);
	text_c (cx - r, cy - r, 2 * r, 2 * r, in, ink_on (c), r >= 18 ? F_MID : F_SMALL, 1);
}

} // namespace mailapp

#endif
