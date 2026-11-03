//
// ui.h -- le dessin du Lecteur PDF (d'apres user/Apps/pdf/ui.h d'Onyx, sur UIKit) : les jetons du theme, les tailles de
// texte, le texte qui tient, les icones (leur geometrie est celle d'Onyx, tracee par ui_vp_*), les listes de zones.
// Tout se dessine dans la vue en cours (ui_view) : begin_paint donne ses pixels a ce qu'UIKit ne fait pas.
//
#ifndef _pdf_ui_h
#define _pdf_ui_h

#include <math.h>
#include <uikit.h>

namespace pdfv {

static struct ui_vars ui;

// ---- le theme : les jetons d'UIKit ---------------------------------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
#define C_BG		jet (J_FACE)
#define C_FIELD		jet (J_SURFACE)
#define C_SIDE		jet (J_CREUX)
#define C_TEXT		jet (J_TEXTE1)
#define C_DIM		jet (J_TEXTE2)
#define C_FAINT		jet (J_TEXTE3)
#define C_LINE		jet (J_LIGNE)
#define C_ACCENT	jet (J_ACCENT)
#define C_ACCENT_L	jet (J_ACCENT_CLAIR)
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }
static inline unsigned ink_on (unsigned c) { return (unsigned) ui_ink (c) & 0xFFFFFF; }

// ---- la vue en cours ---------------------------------------------------------------------------------------------
static uint32_t *g_px; static long g_pw, g_ph;
static inline void begin_paint (long view) { g_px = uikit_canvas_pixels (view, &g_pw, &g_ph); }
// une couleur par-dessus les pixels, a sur 256
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
static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, c, c); }
static void panel (int x, int y, int w, int h, int r, unsigned fill, unsigned line)
{
	rbox (x, y, w, h, r, line); rbox (x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, fill);
}
#define V(a) ((a) * 16)

// ---- les textes ----------------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_H2, F_H1, F_N };
static const int FACE_PX[F_N] = { 13, 11, 15, 18, 26 };
static inline long face (int style) { return style == 2 ? (long) ui.font_b : (long) ui.font; }
static inline int tw (const char *s, int f = F_UI, int style = 0) { return (int) ui_text_width ((long) s, FACE_PX[f], face (style)); }
static inline int fh (int f = F_UI) { return FACE_PX[f] * 5 / 4; }
// s en x, haut de la ligne y, coupe par « … » au-dela de w (0 : pas coupe)
static inline void text (int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	int px = FACE_PX[f];
	if (w > 0) ui_text_fit ((long) s, x, y + px, c, px, face (style), w);
	else ui_text ((long) s, x, y + px, c, px, face (style), 0);
}
static inline void text_v (int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_r (int xr, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (xr - tw (s, f, style), y + (h - fh (f)) / 2, s, c, f, style); }
static inline void text_c (int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style); }

// ---- les listes de zones des parties dessinees a la main --------------------------------------------------------------
struct Hit { int x, y, w, h, kind, a; };
struct HitList
{
	Hit h[300]; int n;
	HitList () : n (0) {}
	void clear () { n = 0; }
	void add (int x, int y, int w, int hh, int kind, int a = 0) { if (n < 300) h[n++] = Hit { x, y, w, hh, kind, a }; }
	const Hit *at (int x, int y) const { for (int i = n - 1; i >= 0; i--) if (x >= h[i].x && y >= h[i].y && x < h[i].x + h[i].w && y < h[i].y + h[i].h) return &h[i]; return 0; }
};

// ---- les icones ---------------------------------------------------------------------------------------------------
enum { I_SIDEBAR, I_OPEN, I_UP, I_DOWN, I_MINUS, I_PLUS, I_SINGLE, I_SCROLL, I_TWO, I_ROTATE, I_FULL, I_SEARCH, I_MORE, I_CLOSE,
       I_HOME, I_FOLDER, I_RIGHT, I_HAND, I_CHECK };
// un stylo sur une boite de 24 unites
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
	void ring (int x, int y, int r, int th) const
	{
		for (int i = 0; i < 16; i++)
		{
			float a = i * 3.14159265f / 8, b = (i + 1) * 3.14159265f / 8;
			ui_vp_line (px (x) + (int) (r * u * cosf (a)), py (y) + (int) (r * u * sinf (a)), px (x) + (int) (r * u * cosf (b)), py (y) + (int) (r * u * sinf (b)), th * u, c);
		}
	}
};
// `hole` : la couleur sous l'icone
static void icon (int id, int x, int y, int s, unsigned c, unsigned hole)
{
	Pen p = { V (x), V (y), V (s) / 24, c };
	Pen h = p; h.c = hole;
	switch (id)
	{
	case I_SIDEBAR: p.rect (2, 4, 20, 16); h.rect (10, 6, 10, 12); break;
	case I_OPEN: case I_FOLDER: p.c = 0xD6AA5A; p.rect (1, 6, 22, 15); p.rect (1, 3, 9, 5); break;
	case I_UP: { int t[] = { 5, 15, 12, 8, 19, 15 }; p.polyline (t, 3, 3); } break;
	case I_DOWN: { int t[] = { 5, 9, 12, 16, 19, 9 }; p.polyline (t, 3, 3); } break;
	case I_RIGHT: { int t[] = { 9, 5, 16, 12, 9, 19 }; p.polyline (t, 3, 3); } break;
	case I_MINUS: p.rect (5, 11, 14, 3); break;
	case I_PLUS: p.rect (5, 11, 14, 3); p.rect (11, 5, 3, 14); break;
	case I_SINGLE: p.rect (6, 2, 12, 20); h.rect (8, 4, 8, 16); break;
	case I_SCROLL: p.rect (6, 0, 12, 10); p.rect (6, 13, 12, 10); h.rect (8, 2, 8, 6); h.rect (8, 15, 8, 6); break;
	case I_TWO: p.rect (1, 3, 10, 18); p.rect (13, 3, 10, 18); h.rect (3, 5, 6, 14); h.rect (15, 5, 6, 14); break;
	case I_ROTATE:
		for (int i = 0; i < 12; i++)
		{
			float a = (-200 + i * 22.5f) * 3.14159265f / 180, b = (-200 + (i + 1) * 22.5f) * 3.14159265f / 180;
			ui_vp_line (p.px (12) + (int) (8 * p.u * cosf (a)), p.py (13) + (int) (8 * p.u * sinf (a)), p.px (12) + (int) (8 * p.u * cosf (b)), p.py (13) + (int) (8 * p.u * sinf (b)), 3 * p.u, c);
		}
		{ int t[] = { 1, 7, 8, 7, 4, 14 }; p.poly (t, 3); }
		break;
	case I_FULL:
		{ int a[] = { 3, 9, 3, 3, 9, 3 }; p.polyline (a, 3, 3);
		  int b[] = { 15, 3, 21, 3, 21, 9 }; p.polyline (b, 3, 3);
		  int d[] = { 3, 15, 3, 21, 9, 21 }; p.polyline (d, 3, 3);
		  int e[] = { 15, 21, 21, 21, 21, 15 }; p.polyline (e, 3, 3); } break;
	case I_SEARCH: p.ring (10, 10, 6, 2); p.line (15, 15, 21, 21, 3); break;
	case I_MORE: for (int k = 0; k < 3; k++) p.circle (12, 5 + k * 7, 2); break;
	case I_CLOSE: p.line (6, 6, 18, 18, 2); p.line (18, 6, 6, 18, 2); break;
	case I_HOME: { int t[] = { 12, 2, 23, 12, 20, 12, 20, 22, 14, 22, 14, 15, 10, 15, 10, 22, 4, 22, 4, 12, 1, 12 }; p.poly (t, 11); } break;
	case I_HAND: for (int k = 0; k < 4; k++) p.rect (5 + k * 4, k == 0 || k == 3 ? 6 : 3, 3, 12); p.rect (5, 12, 15, 9); p.line (2, 11, 7, 17, 3); break;
	case I_CHECK: { int t[] = { 4, 12, 10, 18, 20, 6 }; p.polyline (t, 3, 3); } break;
	}
}

} // namespace pdfv

#endif
