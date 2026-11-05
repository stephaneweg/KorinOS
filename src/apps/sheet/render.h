//
// render.h -- the cells drawn: each style's font (ui_text: the file's font name mapped to sans, serif or
// mono, the name itself kept for the file), the value as its number format shows it (General: as many
// digits as the column holds; a number too wide: ####), aligned (numbers right, texts left, TRUE / errors
// centred, or as the style says; indented; top, middle, bottom), a text wrapped in its cell or flowing over
// the empty cells beside it (the grid's lines hidden under it), the fill, the borders, the merged cells
// drawn as one. Sizes: the sheet's pixels (at 100 %) times the zoom. The lines and fills go straight to the
// pixels of the view being drawn; the text through ui_text inside ui_clip.
//
// The sheet is a sheet of paper: white with its own inks, in every theme. What is around it follows the
// theme's tokens.
//
#ifndef _sheet_render_h
#define _sheet_render_h

#include <stdint.h>
#include <uikit.h>
#define TR(s)	((const char *) ui_tr ((long) (s)))	// (the dictionary: data/lang-fr.txt)
#include "condfmt.h"

namespace ss {

static struct ui_vars ui;

// ---- the theme: UIKit's tokens, never theme.txt ---------------------------------------------------------
enum { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
       J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
static const unsigned *s_jet;
static inline unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
static inline unsigned mixc (unsigned a, unsigned b, int amount) { return (unsigned) ui_mix (a, b, amount) & 0xFFFFFF; }	// (0..256: 256 all b)

// The heights of UIKit's scales (uk_mt_tab): a control's, the interface's text.
static int ctl_h () { return (int) ui_metric (UMT_CTL); }	// hauteur d'un controle : celle de UIKit (echelle, densite tactile)
static int ui_px () { return ui_echelle () >= 2 ? 12 : 13; }

// ---- the pixels being drawn ---------------------------------------------------------------------------------
struct Pixels { uint32_t *px; int w, h; };
static Pixels g_cv;
// (in a view's drawing function, first)
static void cv_begin (long view) { long w = 0, h = 0; g_cv.px = uikit_canvas_pixels (view, &w, &h); g_cv.w = (int) w; g_cv.h = (int) h; }
static inline void cv_end () { ui_clip (0, 0, 0, 0); }
static void cv_fill (int x, int y, int w, int h, unsigned c)
{
	int x1 = imin (x + w, g_cv.w), y1 = imin (y + h, g_cv.h);
	x = imax (x, 0); y = imax (y, 0);
	if (x1 <= x) return;
	for (; y < y1; y++) { uint32_t *p = g_cv.px + y * g_cv.w; for (int i = x; i < x1; i++) p[i] = c; }
}
static inline void cv_tint (int x, int y, int w, int h, unsigned c, int amount)	// (0..256)
{
	int x1 = imin (x + w, g_cv.w), y1 = imin (y + h, g_cv.h);
	x = imax (x, 0); y = imax (y, 0);
	unsigned cr = c >> 16 & 255, cg = c >> 8 & 255, cb = c & 255, k = (unsigned) amount, m = 256 - k;
	for (; y < y1; y++)
	{
		uint32_t *p = g_cv.px + y * g_cv.w;
		for (int i = x; i < x1; i++)
		{
			unsigned v = p[i];
			p[i] = ((v >> 16 & 255) * m + cr * k) >> 8 << 16 | ((v >> 8 & 255) * m + cg * k) >> 8 << 8 | ((v & 255) * m + cb * k) >> 8;
		}
	}
}

// ---- fonts -------------------------------------------------------------------------------------------------
// The card has DejaVu: a family (0 sans, 1 serif, 2 mono), bold, italic, a size in pixels. DejaVu is wider than the
// faces a workbook is made for (Calibri, Arial, Times): a little smaller, so that the columns hold what they were
// sized for.
enum { FAM_SANS, FAM_SERIF, FAM_MONO };
struct Font { long h; int px, asc, desc, lh; };
static long font_handle (int fam, bool bold, bool ital)
{
	static const int MAP[3][4] = { { UFN_SANS, UFN_BOLD, UFN_ITALIC, UFN_BOLDITALIC },
	                               { UFN_SERIF, UFN_SERIFBOLD, UFN_SERIFITALIC, UFN_SERIFBOLDITALIC },
	                               { UFN_MONO, UFN_MONOBOLD, UFN_MONO, UFN_MONOBOLD } };
	static long got[3][4];
	int k = (bold ? 1 : 0) + (ital ? 2 : 0);
	if (!got[fam][k]) got[fam][k] = ui_font_get (MAP[fam][k]);
	return got[fam][k];
}
static Font font_make (int fam, bool bold, bool ital, int px)
{
	Font f;
	f.h = font_handle (fam, bold, ital); f.px = imax (px, 4);
	f.asc = (f.px * 93 + 50) / 100; f.desc = (f.px * 24 + 50) / 100; f.lh = f.asc + f.desc;
	return f;
}
static Font ui_font (int px, bool bold = false) { return font_make (FAM_SANS, bold, false, px); }

static int g_famOf[256];					// the book's font -> a family (-1: not yet)
static void fonts_reset () { for (int i = 0; i < 256; i++) g_famOf[i] = -1; }
static int family_for (const char *name)
{
	char low[64]; int k = 0; for (const char *p = name; *p && k < 63; p++) low[k++] = (char) (*p >= 'A' && *p <= 'Z' ? *p + 32 : *p); low[k] = 0;
	static const char *const MONO[] = { "mono", "courier", "code", "consolas", "lucida console", "terminal", "fixed" };
	static const char *const SERIF[] = { "times", "cambria", "caladea", "georgia", "gelasio", "garamond", "palatino", "book antiqua", "century", "serif" };
	for (unsigned i = 0; i < sizeof MONO / sizeof MONO[0]; i++) if (strstr (low, MONO[i])) return FAM_MONO;
	if (strstr (low, "sans")) return FAM_SANS;
	for (unsigned i = 0; i < sizeof SERIF / sizeof SERIF[0]; i++) if (strstr (low, SERIF[i])) return FAM_SERIF;
	return FAM_SANS;
}
static int book_family (Book &b, int font)
{
	if (font < 0 || font >= b.nfonts) font = 0;
	if (font >= 256) return family_for (b.fonts[font]);
	if (g_famOf[font] < 0) g_famOf[font] = family_for (b.fonts[font]);
	return g_famOf[font];
}
// The style's font at the zoom (z: percent).
static Font style_font (Book &b, const Style &st, int z)
{
	int fam = book_family (b, st.font);
	int px = (int) (((long long) st.size * z * 4 + 1500) / 3000);	// (1/10 pt -> px at 96 dpi, zoomed)
	if (fam != FAM_MONO) px = (px * 7 + 4) / 8;
	return font_make (fam, st.bold, st.italic, px);
}

// ---- UTF-8 text through a font --------------------------------------------------------------------------
static int u8_width (const Font &f, const char *s, int n) { return n <= 0 ? 0 : (int) ui_text_prefix ((long) s, n, f.px, f.h); }
static int s_width (const Font &f, const char *s) { return u8_width (f, s, (int) strlen (s)); }
// x, baseline; only inside [cx0, cx1[ x [cy0, cy1[.
static void u8_draw (const Font &f, int x, int y, const char *s, int n, unsigned c, int cx0, int cy0, int cx1, int cy1)
{
	if (n <= 0 || cx1 <= cx0 || cy1 <= cy0) return;
	char t[512];
	if (n >= (int) sizeof t) n = (int) sizeof t - 1;
	memcpy (t, s, n); t[n] = 0;
	ui_clip (cx0, cy0, cx1 - cx0, cy1 - cy0);
	ui_text ((long) t, x, y, c, f.px, f.h, 0);
}
static void text_at (const Font &f, int x, int y, const char *s, unsigned c, int align, const Rect &clip)	// align 0 left, 1 centre, 2 right
{
	int n = (int) strlen (s), w = u8_width (f, s, n);
	u8_draw (f, align == 1 ? x - w / 2 : align == 2 ? x - w : x, y, s, n, c, clip.c0, clip.r0, clip.c1 + 1, clip.r1 + 1);
}
// The lines of a text wrapped at w (px): [start, end) byte ranges; the count.
static int wrap_lines (const Font &f, const char *s, int n, int w, int *st, int *en, int maxLines)
{
	int k = 0, i = 0;
	while (i <= n && k < maxLines)
	{
		int ls = i, lastSpace = -1, x = 0, j = i, l;
		bool broke = false;
		while (j < n)
		{
			if (s[j] == '\n') break;
			unsigned cp = u8_dec (s + j, n - j, &l);
			int a = u8_width (f, s + j, l);
			if (x + a > w && j > ls)
			{
				if (lastSpace > ls) { st[k] = ls; en[k] = lastSpace; k++; i = lastSpace + 1; }
				else { st[k] = ls; en[k] = j; k++; i = j; }
				broke = true;
				break;
			}
			if (cp == ' ') lastSpace = j;
			x += a; j += l;
		}
		if (broke) continue;
		st[k] = ls; en[k] = j; k++;
		if (j >= n) break;
		i = j + 1;					// (past the '\n')
	}
	return k;
}

// ---- a cell's look ------------------------------------------------------------------------------------------
struct Look
{
	char text[256]; int n;
	unsigned ink;
	Font f;
	int ha, va;						// HA_LEFT / CENTER / RIGHT (General resolved), VA_*
	bool wrap, number, spill;				// spill: may flow over the empty cells beside
	const Style *st;
	Style own;						// (the style under a conditional format's look)
};
static bool g_showFormulas;				// View > Formulas: a formula cell shows its formula
static void cell_look (Book &b, Sheet *s, Cell *x, int r, int c, int cellW, int z, Look &L)
{
	const Style *sp = &cell_style (b, s, x, r, c);
	CfLook cl; cflook_init (cl);
	if (s->ncf) { cf_cell (b, s, x, r, c, cl); if (cf_style (*sp, cl, L.own)) sp = &L.own; }
	const Style &st = *sp;
	L.st = &st;
	L.f = style_font (b, st, z);
	L.n = 0; L.text[0] = 0; L.ink = st.color == AUTO ? 0x000000 : st.color;
	L.wrap = st.wrap; L.va = st.va; L.number = false; L.spill = false;
	int digit = imax (1, u8_width (L.f, "0", 1));
	int maxc = imax (1, (cellW - 5) / digit);
	Shown sh; cell_shown (b, s, x, sh, maxc);
	scpy (L.text, sh.text, sizeof L.text);
	L.n = (int) strlen (L.text);
	if (sh.color != AUTO) L.ink = sh.color;
	if (cl.color != AUTO) L.ink = cl.color;				// (a rule's colour over the format's)
	L.number = sh.number;
	int ha = st.ha;
	if (ha == HA_GENERAL) ha = sh.number ? HA_RIGHT : (sh.err || sh.boolean) ? HA_CENTER : HA_LEFT;
	if (ha == HA_JUSTIFY || ha == HA_FILL) ha = HA_LEFT;
	L.ha = ha;
	L.spill = !L.wrap && !sh.number && !sh.err && L.n > 0;
	if (g_showFormulas && x && x->kind == K_FORM)
	{
		Buf ft; formula_print (b, x->f, ft);
		scpy (L.text, ft.str (), sizeof L.text); L.n = (int) strlen (L.text);
		L.ha = HA_LEFT; L.number = false; L.spill = true; L.ink = 0x000000;
		return;
	}
	// a number wider than its cell: ####
	if (sh.number && !L.wrap)
	{
		int w = u8_width (L.f, L.text, L.n);
		if (w > cellW - 4)
		{
			int hw = imax (1, u8_width (L.f, "#", 1));
			int k = imin ((int) sizeof L.text - 1, imax (1, (cellW - 4) / hw));
			for (int i = 0; i < k; i++) L.text[i] = '#';
			L.text[k] = 0; L.n = k;
		}
	}
}

// ---- borders ---------------------------------------------------------------------------------------------
static void hline (int x0, int x1, int y, unsigned c, int bs, const Rect &clip)
{
	if (y < clip.r0 || y > clip.r1 || y < 0 || y >= g_cv.h) return;
	x0 = imax (imax (x0, clip.c0), 0); x1 = imin (imin (x1, clip.c1), g_cv.w - 1);
	uint32_t *p = g_cv.px + y * g_cv.w;
	if (bs == BS_THIN || bs == BS_MEDIUM || bs == BS_THICK || bs == BS_DOUBLE) { for (int x = x0; x <= x1; x++) p[x] = c; return; }
	for (int x = x0; x <= x1; x++) if (!((bs == BS_DOTTED || bs == BS_HAIR) && (x & 1)) && !(bs == BS_DASHED && (x % 6) >= 4)) p[x] = c;
}
static void vline (int x, int y0, int y1, unsigned c, int bs, const Rect &clip)
{
	if (x < clip.c0 || x > clip.c1 || x < 0 || x >= g_cv.w) return;
	y0 = imax (imax (y0, clip.r0), 0); y1 = imin (imin (y1, clip.r1), g_cv.h - 1);
	for (int y = y0; y <= y1; y++) if (!((bs == BS_DOTTED || bs == BS_HAIR) && (y & 1)) && !(bs == BS_DASHED && (y % 6) >= 4)) g_cv.px[y * g_cv.w + x] = c;
}
// A cell's borders over its box (x, y, w, h) -- its edges on the grid's lines.
static void draw_borders (const Style &st, int x, int y, int w, int h, const Rect &clip)
{
	for (int sd = 0; sd < 4; sd++)
	{
		int bs = st.bs[sd];
		if (!bs) continue;
		unsigned c = st.bc[sd] == AUTO ? 0 : st.bc[sd];
		int th = bs == BS_MEDIUM ? 2 : bs == BS_THICK ? 3 : 1;
		switch (sd)
		{
		case B_LEFT:
			for (int t = 0; t < th; t++) vline (x - 1 + t - (th > 1 ? 1 : 0), y - 1, y + h - 1, c, bs, clip);
			if (bs == BS_DOUBLE) { vline (x - 1, y - 1, y + h - 1, c, BS_THIN, clip); vline (x + 1, y + 1, y + h - 3, c, BS_THIN, clip); }
			break;
		case B_RIGHT:
			for (int t = 0; t < th; t++) vline (x + w - 1 - t + (th > 1 ? 1 : 0), y - 1, y + h - 1, c, bs, clip);
			if (bs == BS_DOUBLE) { vline (x + w - 1, y - 1, y + h - 1, c, BS_THIN, clip); vline (x + w - 3, y + 1, y + h - 3, c, BS_THIN, clip); }
			break;
		case B_TOP:
			for (int t = 0; t < th; t++) hline (x - 1, x + w - 1, y - 1 + t - (th > 1 ? 1 : 0), c, bs, clip);
			if (bs == BS_DOUBLE) { hline (x - 1, x + w - 1, y - 1, c, BS_THIN, clip); hline (x + 1, x + w - 3, y + 1, c, BS_THIN, clip); }
			break;
		case B_BOTTOM:
			for (int t = 0; t < th; t++) hline (x - 1, x + w - 1, y + h - 1 - t + (th > 1 ? 1 : 0), c, bs, clip);
			if (bs == BS_DOUBLE) { hline (x - 1, x + w - 1, y + h - 1, c, BS_THIN, clip); hline (x + 1, x + w - 3, y + h - 3, c, BS_THIN, clip); }
			break;
		}
	}
}

// ---- a pane of the grid -----------------------------------------------------------------------------------
// The visible columns / rows of a pane: their sheet index, place and size on the canvas.
struct Span { int i, at, len; };
struct PaneView
{
	Span col[512]; int nc;
	Span row[512]; int nr;
	int x0, y0, x1, y1;					// the pane's box on the canvas
};
static void pane_layout (Sheet *s, int z, int c0, int cEnd, int r0, int rEnd, int x0, int y0, int x1, int y1, PaneView &pv)
{
	pv.x0 = x0; pv.y0 = y0; pv.x1 = x1; pv.y1 = y1;
	pv.nc = 0;
	for (int c = c0, x = x0; c < cEnd && x < x1 && pv.nc < 512; c++)
	{
		int w = col_w (s, c) * z / 100;
		if (w <= 0) continue;
		pv.col[pv.nc].i = c; pv.col[pv.nc].at = x; pv.col[pv.nc].len = w; pv.nc++;
		x += w;
	}
	pv.nr = 0;
	for (int r = r0, y = y0; r < rEnd && y < y1 && pv.nr < 512; r++)
	{
		int h = row_h (s, r) * z / 100;
		if (h <= 0) continue;
		pv.row[pv.nr].i = r; pv.row[pv.nr].at = y; pv.row[pv.nr].len = h; pv.nr++;
		y += h;
	}
}
// The x of a column's left edge in the pane (it may be off to the left / right: counted on).
static int pane_col_x (Sheet *s, int z, const PaneView &pv, int c)
{
	if (pv.nc == 0) return pv.x0;
	if (c >= pv.col[0].i && c <= pv.col[pv.nc - 1].i) { for (int i = 0; i < pv.nc; i++) if (pv.col[i].i >= c) return pv.col[i].at; }
	if (c < pv.col[0].i) return pv.col[0].at - (col_x (s, pv.col[0].i) - col_x (s, c)) * z / 100;
	return pv.col[pv.nc - 1].at + pv.col[pv.nc - 1].len + (col_x (s, c) - col_x (s, pv.col[pv.nc - 1].i + 1)) * z / 100;
}
static int pane_row_y (Sheet *s, int z, const PaneView &pv, int r)
{
	if (pv.nr == 0) return pv.y0;
	if (r >= pv.row[0].i && r <= pv.row[pv.nr - 1].i) { for (int i = 0; i < pv.nr; i++) if (pv.row[i].i >= r) return pv.row[i].at; }
	if (r < pv.row[0].i) return pv.row[0].at - (int) ((row_y (s, pv.row[0].i) - row_y (s, r)) * z / 100);
	return pv.row[pv.nr - 1].at + pv.row[pv.nr - 1].len + (int) ((row_y (s, r) - row_y (s, pv.row[pv.nr - 1].i + 1)) * z / 100);
}

static const unsigned GRIDLINE = 0xDADCE0;

// The text of a cell laid in its box (or the box its text flows over).
static void draw_text (Look &L, int x, int y, int w, int h, int tx0, int tx1, int z, const Rect &clip)
{
	if (!L.n) return;
	const Font &f = L.f;
	int pad = 3 * z / 100 + 1;
	int ind = L.st->indent * 9 * z / 100;
	int asc = f.asc, desc = f.desc, lh = f.lh;
	int cx0 = imax (clip.c0, tx0), cx1 = imin (clip.c1 + 1, tx1), cy0 = imax (clip.r0, y), cy1 = imin (clip.r1 + 1, y + h);
	if (cx1 <= cx0 || cy1 <= cy0) return;
	auto base_for = [&] (int lines) {
		int th = lines * lh;
		if (L.va == VA_TOP) return y + 2 + asc;
		if (L.va == VA_CENTER) return y + (h - th) / 2 + asc;
		return y + h - 2 - desc - (lines - 1) * lh;
	};
	Rect tc = { cy0, cx0, cy1 - 1, cx1 - 1 };
	auto one = [&] (const char *s, int n, int base) {
		int tw = u8_width (f, s, n);
		int xs;
		if (L.ha == HA_RIGHT) xs = x + w - pad - ind - tw;
		else if (L.ha == HA_CENTER) xs = x + (w - tw) / 2;
		else xs = x + pad + ind;
		// (flowing: the text keeps its place, the clip is the flow's box)
		u8_draw (f, xs, base, s, n, L.ink, cx0, cy0, cx1, cy1);
		int x1p = xs + tw;
		if (L.st->under)
		{
			int uy = base + imax (1, f.px / 8), ut = imax (1, f.px / 16);
			for (int k = 0; k < ut; k++) hline (imax (xs, cx0), imin (x1p, cx1 - 1), uy + k, L.ink, BS_THIN, tc);
			if (L.st->under == 2) hline (imax (xs, cx0), imin (x1p, cx1 - 1), uy + ut + 1, L.ink, BS_THIN, tc);
		}
		if (L.st->strike) hline (imax (xs, cx0), imin (x1p, cx1 - 1), base - f.px * 27 / 100, L.ink, BS_THIN, tc);
	};
	if (L.wrap || memchr (L.text, '\n', L.n))
	{
		int st[64], en[64];
		int k = L.wrap ? wrap_lines (f, L.text, L.n, imax (1, w - 2 * pad - ind), st, en, 64) : 0;
		if (!L.wrap)					// (line breaks only)
		{
			int i = 0;
			while (i <= L.n && k < 64) { int j = i; while (j < L.n && L.text[j] != '\n') j++; st[k] = i; en[k] = j; k++; i = j + 1; if (j >= L.n) break; }
		}
		int base = base_for (k);
		for (int i = 0; i < k; i++) one (L.text + st[i], en[i] - st[i], base + i * lh);
		return;
	}
	one (L.text, L.n, base_for (1));
}

// A pane painted: the fills, the grid's lines, the texts (flowing over their empty neighbours), the
// borders, the merged cells as one. clip: the pane's box.
static void paint_pane (Book &b, Sheet *s, int z, const PaneView &pv, bool gridlines)
{
	Rect clip = { pv.y0, pv.x0, pv.y1 - 1, pv.x1 - 1 };
	if (pv.nc == 0 || pv.nr == 0) return;
	int rc0 = pv.col[0].i, rc1 = pv.col[pv.nc - 1].i, rr0 = pv.row[0].i, rr1 = pv.row[pv.nr - 1].i;
	// the merges met here
	int mi[256]; int nm = 0;
	for (int i = 0; i < s->nmerge && nm < 256; i++)
	{
		const Rect &m = s->merges[i];
		if (m.r1 >= rr0 && m.r0 <= rr1 && m.c1 >= rc0 && m.c0 <= rc1) mi[nm++] = i;
	}
	auto merged = [&] (int r, int c) -> int { for (int i = 0; i < nm; i++) if (rect_has (s->merges[mi[i]], r, c)) return mi[i]; return -1; };
	// 1. the fills (the conditional formats' over the cells'; their data bars)
	for (int ri = 0; ri < pv.nr; ri++)
		for (int ci = 0; ci < pv.nc; ci++)
		{
			int r = pv.row[ri].i, c = pv.col[ci].i;
			Cell *x = s->cells.get (r, c);
			const Style &st = cell_style (b, s, x, r, c);
			unsigned fill = st.fill;
			CfLook cl; cflook_init (cl);
			if (s->ncf) { cf_cell (b, s, x, r, c, cl); if (cl.fill != AUTO) fill = cl.fill; }
			if (fill == AUTO && cl.bar < 0) continue;
			if (nm && merged (r, c) >= 0) continue;
			int X0 = imax (pv.col[ci].at, clip.c0), Y0 = imax (pv.row[ri].at, clip.r0);
			int X1 = imin (pv.col[ci].at + pv.col[ci].len, clip.c1 + 1), Y1 = imin (pv.row[ri].at + pv.row[ri].len, clip.r1 + 1);
			if (fill != AUTO && X1 > X0 && Y1 > Y0) cv_fill (X0, Y0, X1 - X0, Y1 - Y0, fill);
			if (cl.bar >= 0)					// a data bar: a gradient fading to the right, its edge
			{
				int bx = pv.col[ci].at + 2, by = pv.row[ri].at + 2, bh = pv.row[ri].len - 5;
				int bw = (int) ((pv.col[ci].len - 5) * cl.bar + 0.5);
				for (int xx = imax (bx, clip.c0); xx < imin (bx + bw, clip.c1 + 1); xx++)
				{
					unsigned col = mix_rgb (cl.barColor, 0xFFFFFF, bw > 1 ? 0.85 * (xx - bx) / (bw - 1) : 0);
					{ int ya = imax (by, clip.r0), yb = imin (by + bh, clip.r1 + 1); if (yb > ya) cv_fill (xx, ya, 1, yb - ya, col); }
				}
				if (bw > 0)
				{
					hline (bx, bx + bw - 1, by, cl.barColor, BS_THIN, clip); hline (bx, bx + bw - 1, by + bh - 1, cl.barColor, BS_THIN, clip);
					vline (bx, by, by + bh - 1, cl.barColor, BS_THIN, clip); vline (bx + bw - 1, by, by + bh - 1, cl.barColor, BS_THIN, clip);
				}
			}
		}
	// 2. the grid's lines
	if (gridlines)
	{
		for (int ci = 0; ci < pv.nc; ci++) vline (pv.col[ci].at + pv.col[ci].len - 1, pv.y0, pv.y1 - 1, GRIDLINE, BS_THIN, clip);
		for (int ri = 0; ri < pv.nr; ri++) hline (pv.x0, pv.x1 - 1, pv.row[ri].at + pv.row[ri].len - 1, GRIDLINE, BS_THIN, clip);
	}
	// 3. the merged cells: one box each (their inner lines covered)
	for (int k = 0; k < nm; k++)
	{
		const Rect &m = s->merges[mi[k]];
		int x0 = pane_col_x (s, z, pv, m.c0), x1 = pane_col_x (s, z, pv, m.c1 + 1);
		int y0 = pane_row_y (s, z, pv, m.r0), y1 = pane_row_y (s, z, pv, m.r1 + 1);
		Cell *x = s->cells.get (m.r0, m.c0);
		const Style &st = cell_style (b, s, x, m.r0, m.c0);
		unsigned fill = st.fill;
		if (s->ncf) { CfLook cl; cf_cell (b, s, x, m.r0, m.c0, cl); if (cl.fill != AUTO) fill = cl.fill; }
		int X0 = imax (x0, clip.c0), Y0 = imax (y0, clip.r0), X1 = imin (x1 - 1, clip.c1 + 1), Y1 = imin (y1 - 1, clip.r1 + 1);
		if (X1 > X0 && Y1 > Y0) cv_fill (X0, Y0, X1 - X0, Y1 - Y0, fill == AUTO ? 0xFFFFFF : fill);
		if (x && x->kind != K_NONE)
		{
			Look L; cell_look (b, s, x, m.r0, m.c0, x1 - x0, z, L);
			draw_text (L, x0, y0, x1 - x0, y1 - y0, x0, x1 - 1, z, clip);
		}
		draw_borders (st, x0, y0, x1 - x0, y1 - y0, clip);
	}
	// 4. the texts, row by row (a text flows over the empty cells beside it; the lines under it hidden)
	sheet_bounds (s);
	for (int ri = 0; ri < pv.nr; ri++)
	{
		int r = pv.row[ri].i, y = pv.row[ri].at, h = pv.row[ri].len;
		if (r > s->maxR) break;
		// cells of this row in view (and just outside: a text from beyond the edge may flow in)
		int left = rc0, right = rc1;
		for (int c = rc0 - 1; c >= 0 && c >= rc0 - 40; c--) { Cell *x = s->cells.get (r, c); if (x && x->kind != K_NONE) { left = c; break; } }
		for (int c = left; c <= imin (s->maxC, right + 40); c++)
		{
			Cell *x = s->cells.get (r, c);
			if (!x || x->kind == K_NONE) continue;
			if (nm && merged (r, c) >= 0) continue;
			int cx = pane_col_x (s, z, pv, c), cw = col_w (s, c) * z / 100;
			if (cw <= 0) continue;
			if (c > right && cx >= pv.x1) break;
			Look L; cell_look (b, s, x, r, c, cw, z, L);
			if (!L.n) continue;
			int fx0 = cx, fx1 = cx + cw - 1;
			if (L.spill)
			{
				int tw = u8_width (L.f, L.text, L.n) + 6 + L.st->indent * 9 * z / 100;
				if (tw > cw)
				{
					auto empty = [&] (int cc) { if (cc < 0 || cc >= MAXC) return false; Cell *y2 = s->cells.get (r, cc); return !(y2 && y2->kind != K_NONE) && !(nm && merged (r, cc) >= 0); };
					int need = tw - cw;
					if (L.ha == HA_LEFT || L.ha == HA_CENTER)
					{
						int more = L.ha == HA_CENTER ? need / 2 : need;
						for (int cc = c + 1; more > 0 && empty (cc); cc++) { int w2 = col_w (s, cc) * z / 100; fx1 += w2; more -= w2; if (gridlines) vline (fx1 - w2, y, y + h - 2, 0xFFFFFF, BS_THIN, clip); }
					}
					if (L.ha == HA_RIGHT || L.ha == HA_CENTER)
					{
						int more = L.ha == HA_CENTER ? need / 2 : need;
						for (int cc = c - 1; more > 0 && empty (cc); cc--) { int w2 = col_w (s, cc) * z / 100; fx0 -= w2; more -= w2; if (gridlines) vline (fx0 + w2 - 1, y, y + h - 2, 0xFFFFFF, BS_THIN, clip); }
					}
					// (the fills under the flow stay: only the lines go)
				}
			}
			if (fx1 < pv.x0 || fx0 >= pv.x1) continue;
			draw_text (L, cx, y, cw, h, fx0, fx1 + 1, z, clip);
		}
	}
	// 5. the borders
	for (int ri = 0; ri < pv.nr; ri++)
		for (int ci = 0; ci < pv.nc; ci++)
		{
			int r = pv.row[ri].i, c = pv.col[ci].i;
			Cell *x = s->cells.get (r, c);
			const Style &st = cell_style (b, s, x, r, c);
			if (!(st.bs[0] | st.bs[1] | st.bs[2] | st.bs[3])) continue;
			if (nm && merged (r, c) >= 0) continue;
			draw_borders (st, pv.col[ci].at, pv.row[ri].at, pv.col[ci].len, pv.row[ri].len, clip);
		}
}

// A row's height for its tallest font and wrapped text (the rows whose height was not set by hand).
static int fit_row_height (Book &b, Sheet *s, int r)
{
	int need = s->defRowH;
	sheet_bounds (s);
	for (int c = 0; c <= s->maxC; c++)
	{
		Cell *x = s->cells.get (r, c);
		if (!x) continue;
		const Style &st = b.styles.s[x->style];
		Font f = style_font (b, st, 100);
		int lh = f.lh;
		int h = lh + 2;
		if (x->kind != K_NONE && (st.wrap || (x->vt == V_STR && x->str && strchr (x->str, '\n'))))
		{
			Look L; cell_look (b, s, x, r, c, col_w (s, c), 100, L);
			int st2[64], en[64];
			int k = st.wrap ? wrap_lines (f, L.text, L.n, imax (1, col_w (s, c) - 8), st2, en, 64) : 1;
			if (!st.wrap) { k = 1; for (int i = 0; i < L.n; i++) if (L.text[i] == '\n') k++; }
			h = k * lh + 3;
		}
		if (x->kind != K_NONE || st.size > 100) need = imax (need, h);
	}
	return imin (need, 2000);
}
static void autofit_row (Book &b, Sheet *s, int r)
{
	RowInfo *ri = row_info (s, r);
	if (ri && (ri->fl & RF_CUSTOM)) return;
	int h = fit_row_height (b, s, r);
	if (h <= s->defRowH) { if (ri) { ri->fl &= ~RF_AUTO; row_drop_if_plain (s, r); } rows_changed (s); return; }
	ri = row_add (s, r);
	ri->fl |= RF_AUTO; ri->h = (unsigned short) h;
	rows_changed (s);
}
// A column as wide as its texts need (a double click on its edge).
static int fit_col_width (Book &b, Sheet *s, int c)
{
	int w = 0;
	sheet_bounds (s);
	for (int r = 0; r <= s->maxR; r++)
	{
		Cell *x = s->cells.get (r, c);
		if (!x || x->kind == K_NONE || merge_at (s, r, c) >= 0) continue;
		Look L; cell_look (b, s, x, r, c, 4000, 100, L);
		if (L.wrap) continue;
		int tw = u8_width (L.f, L.text, L.n) + 10 + L.st->indent * 9;
		if (tw > w) w = tw;
	}
	return w ? imin (w, 1200) : s->defColW;
}

} // namespace ss

#endif
