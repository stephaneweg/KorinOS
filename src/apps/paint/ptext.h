//
// ptext.h -- Paint's Text tool: the text typed on the canvas, in one of the system's three families (UIKit's TrueType:
// DejaVu Sans, Serif, Sans Mono), its size, bold / italic / underline, aligned left / centred / right, its edges smooth or
// sharp, on colour 2 or on nothing. Each line is drawn white on black in a hidden ui_canvas, whose pixels are its coverage;
// it goes into the overlay of the current layer while it is typed (moved by its box), and on the layer when it is done.
//
// MIT licence (Onyx).
//
#ifndef _paint_ptext_h
#define _paint_ptext_h

#include "plook.h"

namespace pd {

enum { NFAM = 3 };
static const char *const g_tfamNames[NFAM] = { "DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono" };
static const int g_tnfam = NFAM;
static int g_tfam = 0, g_tsize = 32, g_talign = 0;		// family, px, 0 left 1 centre 2 right
static bool g_tbold, g_titalic, g_tunder, g_tsmooth = true, g_tback;

// The text being typed.
struct TextBox
{
	bool on;
	int x, y;				// its top-left in the picture
	char s[1024]; int n, caret;		// UTF-8
	int w, h;				// its size (the last layout)
};
static TextBox g_text;

static long text_font ()
{
	static const int F[NFAM][4] = { { UFN_SANS, UFN_ITALIC, UFN_BOLD, UFN_BOLDITALIC },
		{ UFN_SERIF, UFN_SERIFITALIC, UFN_SERIFBOLD, UFN_SERIFBOLDITALIC }, { UFN_MONO, UFN_MONO, UFN_MONOBOLD, UFN_MONOBOLD } };
	int fam = pclamp (g_tfam, 0, NFAM - 1), st = (g_titalic ? 1 : 0) + (g_tbold ? 2 : 0);
	static long got[NFAM][4];
	if (!got[fam][st]) got[fam][st] = ui_font_get (F[fam][st]);
	return got[fam][st] ? got[fam][st] : (long) ui.font;
}
static inline int line_w (long f, const char *s, int a, int b) { return (int) ui_text_prefix ((long) (s + a), b - a, g_tsize, f); }

static long g_tcv; static int g_tcw, g_tch;			// the hidden canvas (the main window's)
// The line s[a, b) drawn white on black -> its pixels (*stride a row), the base line at y = asc.
static const uint32_t *line_cover (long f, const char *s, int a, int b, int w, int h, int asc, long *stride)
{
	if (w > g_tcw || h > g_tch || !g_tcv)
	{
		if (g_tcv) ui_w_destroy (g_tcv);
		g_tcw = pmax (w + 64, g_tcw); g_tch = pmax (h + 16, g_tch);
		g_tcv = ui_canvas (0, 0, g_tcw, g_tch);
		ui_w_show (g_tcv, 0);
	}
	static char t[1024];
	int n = pmin (b - a, (int) sizeof t - 1);
	memcpy (t, s + a, (size_t) n); t[n] = 0;
	ui_canvas_begin (g_tcv);
	ui_fill (0, 0, w, h, 0);
	ui_text ((long) t, 2, asc, 0xFFFFFF, g_tsize, f, 0);
	ui_canvas_end ();
	long cw, ch;
	const uint32_t *p = uikit_canvas_pixels (g_tcv, &cw, &ch);
	*stride = cw;
	return p;
}

// Lay the text out: its box's size; draw it into buf (D.w x D.h, straight ARGB) when buf, colour c
// (back: colour 2 behind its box), and the caret's place (x, y0, y1 in the picture) when caretOut.
static void text_layout (unsigned *buf, Rect *drawn, unsigned c, unsigned back, int *caret)
{
	long f = text_font ();
	TextBox &t = g_text;
	int lh = g_tsize * 7 / 6, asc = g_tsize * 19 / 20;
	if (lh < 1) lh = 1;
	// the lines: their widths, the widest
	int starts[128], ends[128], nl = 0;
	for (int i = 0, a = 0; i <= t.n && nl < 128; i++)
		if (i == t.n || t.s[i] == '\n') { starts[nl] = a; ends[nl] = i; nl++; a = i + 1; }
	int wmax = 1;
	for (int l = 0; l < nl; l++) { int w = line_w (f, t.s, starts[l], ends[l]); if (w > wmax) wmax = w; }
	t.w = wmax + 2; t.h = nl * lh;
	if (caret) caret[0] = -1;
	if (buf && g_tback)
	{
		Rect r = mkrect (t.x, t.y, t.x + t.w, t.y + t.h); r.clip (D.w, D.h);
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) buf[(unsigned) y * D.w + x] = back | 0xFF000000u;
		if (drawn) drawn->add (r);
	}
	for (int l = 0; l < nl; l++)
	{
		int lw = line_w (f, t.s, starts[l], ends[l]);
		int x0 = t.x + (g_talign == 1 ? (wmax - lw) / 2 : g_talign == 2 ? wmax - lw : 0), y0 = t.y + l * lh;
		if (caret && t.caret >= starts[l] && t.caret <= ends[l]) { caret[0] = x0 + line_w (f, t.s, starts[l], t.caret); caret[1] = y0; caret[2] = y0 + lh; }
		if (!buf || lw <= 0) continue;
		int cw = lw + g_tsize / 2 + 8, ch = lh + g_tsize / 3;
		long st; const uint32_t *cv = line_cover (f, t.s, starts[l], ends[l], cw, ch, asc, &st);
		for (int j = 0; j < ch; j++)
		{
			int Y = y0 + j; if ((unsigned) Y >= (unsigned) D.h) continue;
			const uint32_t *row = cv + j * st;
			for (int i = 0; i < cw; i++)
			{
				int X = x0 + i - 2; if ((unsigned) X >= (unsigned) D.w) continue;
				int a = (int) ((row[i] >> 8) & 255);
				if (!a) continue;
				a = g_tsmooth ? a : a >= 128 ? 255 : 0;
				if (!a) continue;
				unsigned &d = buf[(unsigned) Y * D.w + X];
				d = over (d, c | 0xFF000000u, (unsigned) a);
				if (drawn) drawn->add (X, Y);
			}
		}
		if (g_tunder)
		{
			int uy = y0 + asc + pmax (1, g_tsize / 10), th = pmax (1, g_tsize / 14);
			for (int yy = uy; yy < uy + th; yy++)
				for (int xx = x0; xx < x0 + lw; xx++)
					if ((unsigned) xx < (unsigned) D.w && (unsigned) yy < (unsigned) D.h) { buf[(unsigned) yy * D.w + xx] = c | 0xFF000000u; if (drawn) drawn->add (xx, yy); }
		}
	}
}

// ---- editing ----
static inline int u8_prev (const char *s, int i) { if (i > 0) i--; while (i > 0 && ((unsigned char) s[i] & 0xC0) == 0x80) i--; return i; }
static inline int u8_next (const char *s, int i, int n) { if (i < n) i++; while (i < n && ((unsigned char) s[i] & 0xC0) == 0x80) i++; return i; }
static int u8_put (unsigned c, char *o)
{
	if (c < 0x80) { o[0] = (char) c; return 1; }
	if (c < 0x800) { o[0] = (char) (0xC0 | c >> 6); o[1] = (char) (0x80 | (c & 63)); return 2; }
	if (c < 0x10000) { o[0] = (char) (0xE0 | c >> 12); o[1] = (char) (0x80 | ((c >> 6) & 63)); o[2] = (char) (0x80 | (c & 63)); return 3; }
	o[0] = (char) (0xF0 | c >> 18); o[1] = (char) (0x80 | ((c >> 12) & 63)); o[2] = (char) (0x80 | ((c >> 6) & 63)); o[3] = (char) (0x80 | (c & 63)); return 4;
}
static void text_insert (const char *u, int n)
{
	TextBox &t = g_text;
	if (t.n + n >= (int) sizeof t.s) return;
	for (int i = t.n; i >= t.caret; i--) t.s[i + n] = t.s[i];
	for (int i = 0; i < n; i++) t.s[t.caret + i] = u[i];
	t.n += n; t.caret += n;
}
static void text_back ()
{
	TextBox &t = g_text;
	if (t.caret <= 0) return;
	int p = u8_prev (t.s, t.caret), d = t.caret - p;
	for (int i = p; i + d <= t.n; i++) t.s[i] = t.s[i + d];
	t.n -= d; t.caret = p;
}
static void text_del ()
{
	TextBox &t = g_text;
	if (t.caret >= t.n) return;
	int d = u8_next (t.s, t.caret, t.n) - t.caret;
	for (int i = t.caret; i + d <= t.n; i++) t.s[i] = t.s[i + d];
	t.n -= d;
}

} // namespace pd

#endif
