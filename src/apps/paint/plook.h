//
// plook.h -- Paint's drawing helpers on UIKit: the theme's tokens, the text, the rounded boxes, the view being painted, a
// path of shapes (the icons' geometry is Onyx's: ui_vp_* draws it, in 1/16 px), the scroll bars' arithmetic, a popup menu.
// Everything draws in the view being painted (ui_view): begin_paint gives its pixels to what UIKit does not do.
//
#ifndef _paint_plook_h
#define _paint_plook_h

#include <uikit.h>
#include "pdoc.h"

#define TR(s) ((const char *) ui_tr ((long) (s)))

namespace pd {

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
static inline unsigned ink_on (unsigned c) { return (unsigned) ui_ink (c) & 0xFFFFFF; }
static inline int bright (unsigned c) { return (int) (((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255)) / 10; }

// ---- the view being painted -----------------------------------------------------------------------------------
static uint32_t *g_px; static long g_pw, g_ph;
static inline void begin_paint (long view) { g_px = uikit_canvas_pixels (view, &g_pw, &g_ph); }
static inline void put_px (int x, int y, unsigned c) { if ((unsigned long) x < (unsigned long) g_pw && (unsigned long) y < (unsigned long) g_ph) g_px[y * g_pw + x] = c; }
static inline void blend_view (int x, int y, unsigned c, int a)		// (c over the pixel, a of 255)
{
	if ((unsigned long) x >= (unsigned long) g_pw || (unsigned long) y >= (unsigned long) g_ph) return;
	uint32_t &d = g_px[y * g_pw + x];
	d = mix (d, c, (a + 1) * 256 / 256);
}
static inline void fill (int x, int y, int w, int h, unsigned c) { if (w > 0 && h > 0) ui_fill (x, y, w, h, c); }
static inline void frame (int x, int y, int w, int h, unsigned c) { if (w > 0 && h > 0) ui_frame (x, y, w, h, c); }
static inline void rbox (int x, int y, int w, int h, int r, unsigned c) { if (w > 0 && h > 0) ui_rbox (x, y, w, h, r, c, c); }
// a rounded box with its outline
static void panel (int x, int y, int w, int h, int r, unsigned bg, unsigned line) { rbox (x, y, w, h, r, line); rbox (x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : r, bg); }
#define V(a) ((a) * 16)
static inline void disc (int cx, int cy, int r, unsigned c) { ui_vp_disc (V (cx), V (cy), V (r), c); }
static inline void chevron (int cx, int cy, unsigned c) { int t[6] = { V (cx - 4), V (cy - 2), V (cx + 4), V (cy - 2), V (cx), V (cy + 3) }; ui_vp_poly ((long) t, 3, c); }

// ---- the text: the interface's size, 13 px -------------------------------------------------------------------
enum { TPX = 13, TLH = 16 };
static inline long face (int style) { return style == 2 ? (long) ui.font_b : style == 1 ? ui_font_get (UFN_ITALIC) : (long) ui.font; }
static inline int tw (const char *s, int style = 0) { return (int) ui_text_width ((long) s, TPX, face (style)); }
static inline void text (int x, int y, const char *s, unsigned c, int style = 0) { ui_text ((long) s, x, y + TPX, c, TPX, face (style), 0); }
// centred vertically in a box of height h; cut with "..." past w (0: not cut)
static inline void text_l (int x, int y, int h, const char *s, unsigned c, int style = 0, int w = 0)
{
	int base = y + (h - TLH) / 2 + TPX;
	if (w > 0) ui_text_fit ((long) s, x, base, c, TPX, face (style), w); else ui_text ((long) s, x, base, c, TPX, face (style), 0);
}
static inline void text_c (int x, int y, int w, int h, const char *s, unsigned c, int style = 0) { text_l (x + (w - tw (s, style)) / 2, y, h, s, c, style); }

// ---- a path: shapes gathered, then filled in one colour ----------------------------------------------------------
enum { P_POLY, P_DISC, P_RING, P_LINE, PATHMAX = 48, PATHPTS = 72 };
struct Path
{
	struct Item { int kind, n, a, b, c, d, w; int xy[PATHPTS * 2]; };
	Item it[PATHMAX]; int n;
	Path () : n (0) {}
	void clear () { n = 0; }
	Item *add (int kind) { if (n >= PATHMAX) return it + PATHMAX - 1; it[n].kind = kind; it[n].n = 0; return &it[n++]; }
	void rect (int x, int y, int w, int h) { Item *i = add (P_POLY); int t[8] = { x, y, x + w, y, x + w, y + h, x, y + h }; i->n = 4; for (int k = 0; k < 8; k++) i->xy[k] = t[k]; }
	void poly (const int *xy, int k) { Item *i = add (P_POLY); if (k > PATHPTS) k = PATHPTS; i->n = k; for (int j = 0; j < 2 * k; j++) i->xy[j] = xy[j]; }
	void circle (int cx, int cy, int r) { Item *i = add (P_DISC); i->a = cx; i->b = cy; i->c = r; }
	void hole (int cx, int cy, int r) { (void) cx; (void) cy; if (n && it[n - 1].kind == P_DISC) { it[n - 1].kind = P_RING; it[n - 1].d = r; } }
	void ellipse (int cx, int cy, int rx, int ry)
	{
		Item *i = add (P_POLY); i->n = 32;
		for (int k = 0; k < 32; k++) { i->xy[2 * k] = cx + rx * icos (k * 360 / 32) / 16384; i->xy[2 * k + 1] = cy - ry * isin (k * 360 / 32) / 16384; }
	}
	void rrect (int x, int y, int w, int h, int r)
	{
		Item *i = add (P_POLY); i->n = 0;
		int cx[4] = { x + w - r, x + r, x + r, x + w - r }, cy[4] = { y + r, y + r, y + h - r, y + h - r };
		for (int q = 0; q < 4; q++)
			for (int k = 0; k <= 4; k++)
			{
				int d = q * 90 + k * 90 / 4;
				i->xy[2 * i->n] = cx[q] + r * icos (d) / 16384; i->xy[2 * i->n + 1] = cy[q] - r * isin (d) / 16384; i->n++;
			}
	}
	void line (int x0, int y0, int x1, int y1, int w) { Item *i = add (P_LINE); i->a = x0; i->b = y0; i->c = x1; i->d = y1; i->w = w; }
	void polyline (const int *xy, int k, int w, bool closed = false)
	{
		for (int j = 0; j + 1 < k + (closed ? 1 : 0); j++) { int e = (j + 1) % k; line (xy[2 * j], xy[2 * j + 1], xy[2 * e], xy[2 * e + 1], w); }
	}
	// from a0 to a1 degrees (counter-clockwise from 3 o'clock, as on paper)
	void arc (int cx, int cy, int r, int a0, int a1, int w)
	{
		int s = 8;
		for (int k = 0; k < s; k++)
		{
			int d0 = a0 + (a1 - a0) * k / s, d1 = a0 + (a1 - a0) * (k + 1) / s;
			line (cx + r * icos (d0) / 16384, cy - r * isin (d0) / 16384, cx + r * icos (d1) / 16384, cy - r * isin (d1) / 16384, w);
		}
	}
	// an arrow head at (x, y) pointing toward deg, len long, half wide each side
	void arrowHead (int x, int y, int deg, int len, int half)
	{
		int c = icos (deg), s = isin (deg);
		int bx = x - len * c / 16384, by = y + len * s / 16384;
		int t[6] = { x, y, bx - half * s / 16384, by - half * c / 16384, bx + half * s / 16384, by + half * c / 16384 };
		poly (t, 3);
	}
	void fill (unsigned c) const
	{
		for (int k = 0; k < n; k++)
		{
			const Item &i = it[k];
			switch (i.kind)
			{
			case P_POLY: ui_vp_poly ((long) i.xy, i.n, c); break;
			case P_DISC: ui_vp_disc (i.a, i.b, i.c, c); break;
			case P_LINE: ui_vp_line (i.a, i.b, i.c, i.d, i.w, c); break;
			case P_RING:
			{
				int r = (i.c + i.d) / 2, w = i.c - i.d;
				for (int q = 0; q < 24; q++)
					ui_vp_line (i.a + r * icos (q * 15) / 16384, i.b - r * isin (q * 15) / 16384, i.a + r * icos ((q + 1) * 15) / 16384, i.b - r * isin ((q + 1) * 15) / 16384, w, c);
				break;
			}
			}
		}
	}
};

// ---- a scroll bar's arithmetic ---------------------------------------------------------------------------------
struct Thumb { bool show; int y, h; };
static Thumb thumb_of (long total, long view, long pos, int track)
{
	Thumb t = { false, 0, track };
	if (total <= view || track <= 0) return t;
	t.show = true;
	t.h = (int) ((long long) track * view / total); if (t.h < 24) t.h = 24;
	if (t.h > track) t.h = track;
	t.y = (int) ((long long) (track - t.h) * pos / (total - view));
	return t;
}
static long thumb_pos (int m, int track, long total, long view, int h)
{
	if (track <= h) return 0;
	return (long long) (m - h / 2) * (total - view) / (track - h);
}

// ---- a popup menu ----------------------------------------------------------------------------------------------
// (ui_popup_local: at most what fits; an entry that cannot be chosen is left out; a check mark before the current one)
struct Popup
{
	char b[2400]; int n, ids[64], k, wmax;
	Popup () : n (0), k (0), wmax (80) {}
	void put (const char *s) { int l = slen (s); if (n + l + 2 < (int) sizeof b) { memcpy (b + n, s, (size_t) l + 1); n += l + 1; } }
	void add (const char *label, int id, bool on = true, bool check = false)
	{
		if (!on || k >= 63) return;
		char t[100]; snprintf (t, sizeof t, "%s%s", check ? "\xE2\x9C\x93 " : "", label);
		put (t); ids[k++] = id;
		int w = tw (t) + 36; if (w > wmax) wmax = w;
	}
	void separator () { if (k && k < 63) { put ("-"); ids[k++] = -1; } }
	int run (int x, int y)
	{
		while (k && ids[k - 1] < 0) { k--; n -= 2; }		// (no separator at the end)
		if (!k) return -1;
		b[n] = 0;
		long r = ui_popup_local (x, y, (long) b, wmax);
		return r >= 0 && r < k ? ids[r] : -1;
	}
};

} // namespace pd

#endif
