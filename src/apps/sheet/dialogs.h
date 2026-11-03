//
// dialogs.h -- the spreadsheet's dialogs (ui_dialog_*: a window of their own over the main one): Format
// Cells (Numbers: a category, its decimals, separator, red negatives, symbol, the date and time formats, the
// code itself and a preview; Font; Alignment; Borders: presets, each edge, the line, the colour; Fill),
// Insert Function (by category, each one's arguments and purpose), Sort (three keys), Find and Replace,
// Paste Special, Names (the defined names), Conditional Formatting (the sheet's rules), the AutoFilter's
// list (a column's values to show, sort), Chart (its type, title, legend, series, a live preview). A dialog
// is a function and a static state; what is drawn by hand in one is a ui_view (its function flushed from
// the dialog's function: ui_dialog_run does not).
//
#ifndef _sheet_dialogs_h
#define _sheet_dialogs_h

#include "bars.h"

namespace ss {

static int note (const char *title, const char *text, int buttons = UM_OK) { return (int) ui_message ((long) TR (title), (long) TR (text), buttons); }

static void lab (int x, int y, int w, const char *s, int fl = 0) { ui_label (x, y, w, ctl_h (), (long) s, fl); }
static long field (int x, int y, int w, const char *s) { long t = ui_textbox (x, y, w, ctl_h (), 0); ui_w_set_text (t, (long) s); return t; }
static const char *text_of (long w) { return (const char *) ui_w_get_text (w); }
static int  val (long w) { return (int) ui_w_get_value (w); }
static long check (int x, int y, int w, const char *s, bool on) { return ui_checkbox (x, y, w, 24, (long) TR (s), on); }
static long ok_cancel (long d, int W) { long e = ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, W - 12); ui_dialog_button (d, 1, (long) "Cancel", 0, 0, e); ui_dialog_cancel (d, 0); return e; }

// The colours: ten hues and their tints / shades (as an office suite's), the standard ones.
static unsigned g_cols[60];
static void make_palettes ()
{
	static const unsigned base[10] = { 0xFFFFFF, 0x000000, 0xE7E6E6, 0x44546A, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };
	static const unsigned std[10] = { 0xC00000, 0xFF0000, 0xFFC000, 0xFFFF00, 0x92D050, 0x00B050, 0x00B0F0, 0x0070C0, 0x002060, 0x7030A0 };
	for (int c = 0; c < 10; c++)
	{
		unsigned b = base[c];
		bool dark = c == 1;
		g_cols[c] = b;
		g_cols[10 + c] = c == 0 ? 0xF2F2F2 : dark ? 0x7F7F7F : mixc (b, 0xFFFFFF, 205);
		g_cols[20 + c] = c == 0 ? 0xD9D9D9 : dark ? 0x595959 : mixc (b, 0xFFFFFF, 154);
		g_cols[30 + c] = c == 0 ? 0xBFBFBF : dark ? 0x3F3F3F : mixc (b, 0xFFFFFF, 102);
		g_cols[40 + c] = c == 0 ? 0xA6A6A6 : dark ? 0x262626 : mixc (b, 0x000000, 64);
		g_cols[50 + c] = std[c];
	}
}

// ---- Format Cells ---------------------------------------------------------------------------------------
static const char *const NUM_CATS[] = { "General", "Number", "Currency", "Percent", "Scientific", "Fraction", "Date", "Time", "Text", "Custom" };
static const char *const DATE_FMTS[] = { "dd/mm/yyyy", "d/m/yy", "yyyy-mm-dd", "d mmm yyyy", "d mmmm yyyy", "dddd d mmmm yyyy", "mmm yy", "mmmm yyyy", "dd/mm/yyyy hh:mm", "d-mmm-yy" };
static const char *const TIME_FMTS[] = { "hh:mm", "hh:mm:ss", "h:mm AM/PM", "h:mm:ss AM/PM", "[h]:mm:ss", "mm:ss.0" };
static const char *const CURRENCIES[] = { "\xE2\x82\xAC", "$", "\xC2\xA3", "\xC2\xA5", "CHF" };
static const char *const CURRENCY_NAMES[] = { "Euro", "Dollar ($)", "Pound", "Yen", "Franc (CHF)" };
static const char *const HA_NAMES[] = { "General", "Left", "Centre", "Right", "Justify", "Fill" };
static const char *const VA_NAMES[] = { "Bottom", "Middle", "Top" };
static const char *const UNDER_NAMES[] = { "None", "Single", "Double" };
static const char *const FSTYLE_NAMES[] = { "Regular", "Italic", "Bold", "Bold Italic" };
static const char *const BS_NAMES[] = { "Thin", "Medium", "Thick", "Dashed", "Dotted", "Double", "Hair" };
static const int BS_VALS[] = { BS_THIN, BS_MEDIUM, BS_THICK, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_HAIR };

// The edges of a range the Borders page sets: each one on, off, or left as it is (-1).
struct BorderSet { int edge[6]; int style; unsigned color; };	// left, right, top, bottom, inner horizontal, inner vertical

struct FormatState
{
	Book *b; Style st, orig; double sample; bool sampleNum; char sampleText[64];
	BorderSet bset; bool mergeOn, mergeOrig;
	char code[160]; int cat, bstyleV; unsigned bcolV;
	long tabs, lcat, dec, thou, red, sym, dlist, codeBox, preview, ldec, lsym, ldt;
	long fam, fstyle, fsize, under, strike, fcol, fauto, sample2;
	long ha, va, ind, wrap, merge;
	long pres[4], bstyle, bcol, bview, nofill, fillv;
};
static FormatState fd;

static void fd_layout_numbers ()
{
	int c = fd.cat;
	bool num = c >= 1 && c <= 4, cur = c == 2, dt = c == 6 || c == 7;
	ui_w_show (fd.dec, num); ui_w_show (fd.ldec, num);
	ui_w_show (fd.thou, c == 1 || c == 2); ui_w_show (fd.red, c == 1 || c == 2);
	ui_w_show (fd.sym, cur); ui_w_show (fd.lsym, cur); ui_w_show (fd.dlist, dt); ui_w_show (fd.ldt, dt);
	if (dt)
	{
		ui_list_clear (fd.dlist);
		const char *const *fm = c == 6 ? DATE_FMTS : TIME_FMTS; int n = c == 6 ? 10 : 6;
		for (int i = 0; i < n; i++) { Buf o; fmt_number (fm[i], fd.sampleNum && fd.sample > 0 ? fd.sample : 46294.5625, o); ui_list_add (fd.dlist, (long) o.str (), 0); }
		ui_w_set_text (fd.ldt, (long) TR (c == 6 ? "The date as:" : "The time as:"));
	}
}
static void fd_rebuild_code ()
{
	Buf c;
	int d = val (fd.dec);
	char z[20]; int k = imin (d, 15); for (int i = 0; i < k; i++) z[i] = '0'; z[k] = 0;
	int di = imax (0, val (fd.dlist));
	switch (fd.cat)
	{
	case 0: c.puts ("General"); break;
	case 1: case 2:
	{
		Buf base; base.puts (val (fd.thou) ? "#,##0" : "0"); if (d) { base.put ('.'); base.puts (z); }
		if (fd.cat == 2)
		{
			const char *s = CURRENCIES[iclamp (val (fd.sym), 0, 4)];
			Buf q; if (!strcmp (s, "$")) { q.put ('$'); q.puts (base.str ()); } else { q.puts (base.str ()); q.puts (" \""); q.puts (s); q.put ('"'); }
			base.clear (); base.puts (q.str ());
		}
		c.puts (base.str ());
		if (val (fd.red)) { c.puts (";[Red]-"); c.puts (base.str ()); }
		break;
	}
	case 3: c.put ('0'); if (d) { c.put ('.'); c.puts (z); } c.put ('%'); break;
	case 4: c.put ('0'); if (d) { c.put ('.'); c.puts (z); } c.puts ("E+00"); break;
	case 5: c.puts ("# ?/?"); break;
	case 6: c.puts (DATE_FMTS[imin (di, 9)]); break;
	case 7: c.puts (TIME_FMTS[imin (di, 5)]); break;
	case 8: c.puts ("@"); break;
	default: return;
	}
	scpy (fd.code, c.str (), sizeof fd.code);
	ui_w_set_text (fd.codeBox, (long) fd.code);
}
// (the views, in the dialog's own window)
static long fd_preview (long v, long w, long h)
{
	cv_begin (v);
	cv_fill (0, 0, (int) w, (int) h, 0xFFFFFF);
	Buf o; unsigned col = AUTO;
	if (fd.sampleNum) fmt_number (fd.code, fd.sample, o, &col, 30); else fmt_text (fd.code, fd.sampleText, o, &col);
	text_at (ui_font (13), 10, (int) h / 2 + 5, o.str (), col == AUTO ? 0 : col, 0, Rect { 0, 0, (int) h - 1, (int) w - 1 });
	cv_end ();
	return 0;
}
static long fd_sample (long v, long w, long h)
{
	cv_begin (v);
	cv_fill (0, 0, (int) w, (int) h, fd.st.fill == AUTO ? 0xFFFFFF : fd.st.fill);
	Look L; memset (&L, 0, sizeof L);
	Style s2 = fd.st;
	scpy (L.text, "AaBbCcYyZz 123", sizeof L.text); L.n = (int) strlen (L.text); L.f = style_font (*fd.b, s2, 100); L.st = &s2; L.ink = s2.color == AUTO ? 0 : s2.color;
	L.ha = HA_CENTER; L.va = VA_CENTER;
	draw_text (L, 0, 0, (int) w, (int) h, 0, (int) w, 100, Rect { 0, 0, (int) h - 1, (int) w - 1 });
	cv_end ();
	return 0;
}
enum { FB_M = 14 };
static long fd_borders (long v, long W, long H)
{
	cv_begin (v);
	int bx = FB_M, by = FB_M, bw = (int) W - 2 * FB_M, bh = (int) H - 2 * FB_M;
	cv_fill (0, 0, (int) W, (int) H, jet (J_FACE));
	cv_fill (bx, by, bw, bh, 0xFFFFFF);
	Rect cl = { 0, 0, (int) H - 1, (int) W - 1 };
	Font pf = ui_font (13);
	for (int i = 0; i < 3; i++) { hline (bx, bx + bw, by + i * bh / 2, 0xE0E0E0, BS_DOTTED, cl); vline (bx + i * bw / 2, by, by + bh, 0xE0E0E0, BS_DOTTED, cl); }
	for (int i = 0; i < 4; i++) text_at (pf, bx + (i & 1 ? 3 : 1) * bw / 4, by + (i & 2 ? 3 : 1) * bh / 4 + 5, TR ("Text"), 0x999999, 1, cl);
	int stl = BS_VALS[iclamp (val (fd.bstyle), 0, 6)];
	unsigned bc = (unsigned) val (fd.bcol) & 0xFFFFFF;
	int th = stl == BS_MEDIUM ? 2 : stl == BS_THICK ? 3 : 1;
	for (int e = 0; e < 6; e++)
	{
		bool on = fd.bset.edge[e] == 1;
		bool same = fd.bset.edge[e] == -1 && e < 4 && fd.orig.bs[e];
		if (!on && !same) continue;
		unsigned c = on ? bc : 0xA0A0A0;
		for (int t = 0; t < (on ? th : 1); t++)
		{
			if (e == 0) vline (bx + t, by, by + bh, c, on ? stl : BS_THIN, cl);
			if (e == 1) vline (bx + bw - t, by, by + bh, c, on ? stl : BS_THIN, cl);
			if (e == 2) hline (bx, bx + bw, by + t, c, on ? stl : BS_THIN, cl);
			if (e == 3) hline (bx, bx + bw, by + bh - t, c, on ? stl : BS_THIN, cl);
			if (e == 4) hline (bx, bx + bw, by + bh / 2 + t, c, on ? stl : BS_THIN, cl);
			if (e == 5) vline (bx + bw / 2 + t, by, by + bh, c, on ? stl : BS_THIN, cl);
		}
	}
	cv_end ();
	return 0;
}
static int s_bw, s_bh;
static long fd_borders_mouse (long v, long kind, long x, long y, long info)
{
	if (kind != 1) return 0;
	(void) info;
	int mx = (int) x, my = (int) y, bx = FB_M, by = FB_M, bw = s_bw - 2 * FB_M, bh = s_bh - 2 * FB_M, e = -1;
	if (abs (my - by) < 12) e = 2; else if (abs (my - (by + bh)) < 12) e = 3;
	else if (abs (mx - bx) < 12) e = 0; else if (abs (mx - (bx + bw)) < 12) e = 1;
	else if (abs (my - (by + bh / 2)) < 12) e = 4; else if (abs (mx - (bx + bw / 2)) < 12) e = 5;
	if (e >= 0) { fd.bset.edge[e] = fd.bset.edge[e] == 1 ? 0 : 1; ui_view_dirty (v); ui_view_flush (); }
	return 0;
}
enum { FP_CS = 22, FP_G = 5 };
static long fd_fill_paint (long v, long W, long H)
{
	cv_begin (v);
	cv_fill (0, 0, (int) W, (int) H, jet (J_FACE));
	for (int i = 0; i < 60; i++)
	{
		int x = 4 + (i % 10) * (FP_CS + FP_G), y = 4 + (i / 10) * (FP_CS + FP_G);
		cv_fill (x, y, FP_CS, FP_CS, g_cols[i]);
		ui_clip (0, 0, 0, 0);
		ui_frame (x, y, FP_CS, FP_CS, mixc (g_cols[i], 0, 60));
		if (fd.st.fill == g_cols[i]) { ui_frame (x - 2, y - 2, FP_CS + 4, FP_CS + 4, jet (J_ACCENT)); ui_frame (x - 3, y - 3, FP_CS + 6, FP_CS + 6, jet (J_ACCENT)); }
	}
	cv_end ();
	return 0;
}
static long fd_fill_mouse (long v, long kind, long x, long y, long info)
{
	(void) info;
	if (kind != 1) return 0;
	int c = ((int) x - 4) / (FP_CS + FP_G), r = ((int) y - 4) / (FP_CS + FP_G);
	if (x >= 4 && y >= 4 && c < 10 && r < 6) { fd.st.fill = g_cols[r * 10 + c]; ui_view_dirty (v); ui_view_dirty (fd.sample2); ui_view_flush (); }
	return 0;
}
static void fd_font_changed ()
{
	int f = val (fd.fam);
	if (f >= 0) fd.st.font = (unsigned short) book_font (*fd.b, (const char *) uikit_list_get (fd.fam, f, 0));
	int fs = val (fd.fstyle);
	fd.st.bold = fs >= 2; fd.st.italic = fs & 1;
	fd.st.size = (unsigned short) SIZE_VALS[iclamp (val (fd.fsize), 0, 22)];
	fd.st.under = (unsigned char) val (fd.under); fd.st.strike = val (fd.strike);
	fd.st.color = val (fd.fauto) ? AUTO : (unsigned) val (fd.fcol) & 0xFFFFFF;
	ui_view_dirty (fd.sample2);
}
static long fd_fn (long d, long w, long code)
{
	if (code == UN_ACTIVATE)
	{
		for (int i = 0; i < 4; i++)
			if (w == fd.pres[i])					// the borders' presets
			{
				for (int e = 0; e < 6; e++) fd.bset.edge[e] = i == 0 ? 0 : i == 1 ? (e < 4 ? 1 : 0) : i == 2 ? (e < 4 ? 0 : 1) : 1;
				if (i == 1) { fd.bset.edge[4] = fd.bset.edge[5] = -1; }
				if (i == 2) { for (int e = 0; e < 4; e++) fd.bset.edge[e] = -1; }
				ui_view_dirty (fd.bview);
			}
		if (w == fd.nofill) { fd.st.fill = AUTO; ui_view_dirty (fd.fillv); ui_view_dirty (fd.sample2); }
	}
	if (w == fd.lcat && code) { fd.cat = val (fd.lcat); if (fd.cat == 6 || fd.cat == 7) { ui_w_set_value (fd.dlist, 0); } fd_layout_numbers (); if (fd.cat == 6 || fd.cat == 7) ui_w_set_value (fd.dlist, 0); fd_rebuild_code (); ui_view_dirty (fd.preview); ui_w_draw (0); }
	if (code == UN_CHANGE)
	{
		if (w == fd.dec || w == fd.thou || w == fd.red || w == fd.sym || w == fd.dlist) { fd_rebuild_code (); ui_view_dirty (fd.preview); }
		if (w == fd.codeBox) { scpy (fd.code, text_of (fd.codeBox), sizeof fd.code); if (fd.cat != 9) { fd.cat = 9; ui_w_set_value (fd.lcat, 9); fd_layout_numbers (); ui_w_draw (0); } ui_view_dirty (fd.preview); }
		if (w == fd.fam || w == fd.fstyle || w == fd.fsize || w == fd.under || w == fd.strike || w == fd.fcol || w == fd.fauto) fd_font_changed ();
		if (w == fd.ha || w == fd.va || w == fd.ind || w == fd.wrap || w == fd.merge)
		{
			fd.st.ha = (unsigned char) val (fd.ha); fd.st.va = (unsigned char) val (fd.va); fd.st.indent = (unsigned char) val (fd.ind); fd.st.wrap = val (fd.wrap);
			fd.mergeOn = val (fd.merge);
		}
		if (w == fd.bstyle || w == fd.bcol) ui_view_dirty (fd.bview);
	}
	(void) d;
	ui_view_flush ();
	return 0;
}
// -> 1: OK (fd.st, fd.bset, fd.mergeOn, the code in fd.st.fmt)
static int format_dialog (Book *b, const Style &s0, double v, bool isNum, const char *txt, bool merged)
{
	memset (&fd, 0, sizeof fd);
	fd.b = b; fd.st = s0; fd.orig = s0; fd.sample = v; fd.sampleNum = isNum; fd.mergeOn = fd.mergeOrig = merged;
	scpy (fd.sampleText, txt, sizeof fd.sampleText);
	for (int i = 0; i < 6; i++) fd.bset.edge[i] = -1;
	scpy (fd.code, book_fmt_code (*b, fd.st.fmt), sizeof fd.code);
	int W = 620, H = 470, c = ctl_h ();
	long d = ui_dialog_begin (W, H, (long) "Format Cells");
	if (!d) return 0;
	fd.tabs = ui_tabs (10, 8, W - 20, H - 68, (long) TR ("Numbers|Font|Alignment|Borders|Fill"));
	int y = 52;
	auto in = [&] (long w, int page) { ui_w_set_parent (w, ui_tabs_page (fd.tabs, page)); return w; };
	auto lb = [&] (int x, int yy, int w, const char *s, int page) { return in (ui_label (x, yy, w, c, (long) s, 0), page); };
	// Numbers
	fd.lcat = in (ui_list (20, y, 150, 300, 0), 0); for (int i = 0; i < 10; i++) ui_list_add (fd.lcat, (long) TR (NUM_CATS[i]), 0);
	fd.ldec = lb (186, y + 2, 120, "Decimal places", 0);
	fd.dec = in (ui_numeric (320, y, 100, c, 0, 15, 2), 0);
	fd.thou = in (check (186, y + 36, 240, "Thousands separator", true), 0);
	fd.red = in (check (186, y + 64, 240, "Negative numbers in red", false), 0);
	fd.lsym = lb (186, y + 96, 120, "Symbol", 0);
	fd.sym = in (dd_make (320, y + 96, 150, c, CURRENCY_NAMES, 5, 0, true), 0);
	fd.ldt = lb (186, y, 200, "The date as:", 0);
	fd.dlist = in (ui_list (186, y + c + 2, 410, 130, 0), 0);
	lb (186, y + 192, 200, "Format code", 0);
	fd.codeBox = in (field (186, y + 192 + c, 410, fd.code), 0);
	fd.preview = in (ui_view (186, y + 192 + 2 * c + 8, 410, 40, 0), 0);
	ui_view_on (fd.preview, (long) fd_preview, 0, 0, 0);
	// Font
	fd.fam = in (ui_list (20, y, 220, 230, 0), 1);
	int cur = -1;
	const char *nowName = b->fonts[fd.st.font < b->nfonts ? fd.st.font : 0];
	for (int i = 0; i < NFONTNAMES; i++) { ui_list_add (fd.fam, (long) FONT_NAMES[i], 0); if (ci_eq (FONT_NAMES[i], nowName)) cur = i; }
	if (cur < 0) cur = (int) ui_list_add (fd.fam, (long) nowName, 0);
	ui_w_set_value (fd.fam, cur);
	lb (260, y, 90, "Style", 1); fd.fstyle = in (dd_make (360, y, 150, c, FSTYLE_NAMES, 4, (fd.st.bold ? 2 : 0) + (fd.st.italic ? 1 : 0), true), 1);
	int si = 4; for (int i = 0; i < 23; i++) if (SIZE_VALS[i] == fd.st.size) si = i;
	lb (260, y + 36, 90, "Size", 1); fd.fsize = in (dd_make (360, y + 36, 90, c, SIZE_NAMES, 23, si), 1);
	lb (260, y + 72, 90, "Underline", 1); fd.under = in (dd_make (360, y + 72, 150, c, UNDER_NAMES, 3, fd.st.under, true), 1);
	fd.strike = in (check (260, y + 108, 200, "Strikethrough", fd.st.strike), 1);
	lb (260, y + 142, 90, "Colour", 1);
	fd.fcol = in (ui_colorpicker (360, y + 142, 64, c, fd.st.color == AUTO ? 0 : fd.st.color), 1);
	fd.fauto = in (check (436, y + 144, 150, "Automatic", fd.st.color == AUTO), 1);
	fd.sample2 = ui_view (20, y + 244, W - 40, 60, 0);	// (a child of no page: moved by show below)
	ui_view_on (fd.sample2, (long) fd_sample, 0, 0, 0);
	in (fd.sample2, 1);
	// Alignment
	lb (40, y, 100, "Horizontal", 2); fd.ha = in (dd_make (150, y, 160, c, HA_NAMES, 6, fd.st.ha, true), 2);
	lb (40, y + 36, 100, "Vertical", 2); fd.va = in (dd_make (150, y + 36, 160, c, VA_NAMES, 3, fd.st.va, true), 2);
	lb (40, y + 72, 100, "Indent", 2); fd.ind = in (ui_numeric (150, y + 72, 100, c, 0, 15, fd.st.indent), 2);
	fd.wrap = in (check (40, y + 116, 300, "Wrap text in the cell", fd.st.wrap), 2);
	fd.merge = in (check (40, y + 146, 300, "Merge the cells", merged), 2);
	// Borders
	static const char *const PRE[4] = { "None", "Outline", "Inside", "All" };
	for (int i = 0; i < 4; i++) fd.pres[i] = in (ui_pushbutton (20 + i * 92, y, 86, c, (long) PRE[i], 0), 3);
	s_bw = 380; s_bh = 240;
	fd.bview = in (ui_view (20, y + 42, s_bw, s_bh, 0), 3);
	ui_view_on (fd.bview, (long) fd_borders, (long) fd_borders_mouse, 0, 0);
	lb (420, y + 44, 120, "Line", 3); fd.bstyle = in (dd_make (420, y + 44 + c, 130, c, BS_NAMES, 7, 0, true), 3);
	lb (420, y + 110, 120, "Colour", 3); fd.bcol = in (ui_colorpicker (420, y + 110 + c, 64, c, 0), 3);
	lb (420, y + 180, 180, "Click an edge to set it", 3);
	// Fill
	fd.nofill = in (ui_pushbutton (20, y, 110, c, (long) "No Fill", 0), 4);
	fd.fillv = in (ui_view (150, y, 10 * (FP_CS + FP_G) + 8, 6 * (FP_CS + FP_G) + 8, 0), 4);
	ui_view_on (fd.fillv, (long) fd_fill_paint, (long) fd_fill_mouse, 0, 0);
	ok_cancel (d, W);
	int k = fmt_kind (fd.code);
	int cc = k == FK_GENERAL ? 0 : k == FK_NUMBER ? 1 : k == FK_CURRENCY ? 2 : k == FK_PERCENT ? 3 : k == FK_SCI ? 4 : k == FK_FRACTION ? 5 : k == FK_DATE || k == FK_DATETIME ? 6 : k == FK_TIME ? 7 : k == FK_TEXT ? 8 : 9;
	ui_w_set_value (fd.lcat, cc); fd.cat = cc;
	int dc = fmt_decimals (fd.code); ui_w_set_value (fd.dec, dc < 0 ? 2 : dc);
	ui_w_set_value (fd.thou, strchr (fd.code, ',') != 0);
	ui_w_set_value (fd.red, strstr (fd.code, "[Red]") != 0);
	fd_layout_numbers ();
	int r = (int) ui_dialog_run (d, (long) fd_fn, 0);
	fd.bstyleV = val (fd.bstyle); fd.bcolV = (unsigned) val (fd.bcol) & 0xFFFFFF;
	if (r == 1) { scpy (fd.code, text_of (fd.codeBox), sizeof fd.code); fd.st.fmt = (unsigned short) book_fmt (*b, fd.code); }
	ui_dialog_end (d);
	return r;
}

// ---- Insert Function ------------------------------------------------------------------------------------
static const char *const CAT_ALL[CAT_COUNT + 1] = { "All", "Mathematical", "Statistical", "Logical", "Text", "Lookup and Reference", "Date and Time", "Information", "Financial" };
static struct { long cat, list, sig, help, cn; int map[400], n; int chosen; } fn_d;
static void fn_fill ()
{
	ui_list_clear (fn_d.list); fn_d.n = 0;
	int c = val (fn_d.cat) - 1;
	for (char ch = 'A'; ch <= 'Z'; ch++)
		for (int i = 0; i < NFNS && fn_d.n < 400; i++)
			if (FNS[i].name[0] == ch && (c < 0 || FNS[i].cat == c)) { fn_d.map[fn_d.n++] = i; ui_list_add (fn_d.list, (long) FNS[i].name, 0); }
	ui_w_set_value (fn_d.list, 0);
}
static void fn_show ()
{
	int s = val (fn_d.list);
	if (s < 0 || s >= fn_d.n) return;
	const FnDef &d = FNS[fn_d.map[s]];
	Buf sig; sig.puts (d.name); sig.put ('('); sig.puts (TR (d.args)); sig.put (')');
	ui_w_set_text (fn_d.sig, (long) sig.str ());
	ui_w_set_text (fn_d.help, (long) TR (d.help));
	char t[80]; snprintf (t, sizeof t, TR ("Category: %s"), TR (CAT_NAMES[d.cat]));
	ui_w_set_text (fn_d.cn, (long) t);
}
static long fn_fn (long d, long w, long code)
{
	if (w == fn_d.cat && code == UN_CHANGE) { fn_fill (); fn_show (); ui_w_draw (fn_d.list); }
	if (w == fn_d.list && code == UN_CHANGE) fn_show ();
	if (w == fn_d.list && code == UN_ACTIVATE) ui_dialog_close (d, 1);
	return 0;
}
// -> the function chosen, -1
static int func_dialog ()
{
	int W = 560, H = 420;
	long d = ui_dialog_begin (W, H, (long) "Insert Function");
	if (!d) return -1;
	lab (16, 12, 80, "Category");
	fn_d.cat = dd_make (100, 12, 230, ctl_h (), CAT_ALL, CAT_COUNT + 1, 0, true);
	fn_d.list = ui_list (16, 52, 210, 300, 0);
	fn_d.sig = ui_label (240, 52, W - 256, 70, (long) "", ULB_WRAP | ULB_BOLD);
	fn_d.help = ui_label (240, 126, W - 256, 130, (long) "", ULB_WRAP);
	fn_d.cn = ui_label (240, 262, W - 256, 24, (long) "", ULB_DIM);
	ok_cancel (d, W);
	fn_fill (); fn_show ();
	ui_w_focus (fn_d.list);
	int r = (int) ui_dialog_run (d, (long) fn_fn, 0);
	int sel = val (fn_d.list);
	ui_dialog_end (d);
	return r == 1 && sel >= 0 && sel < fn_d.n ? fn_d.map[sel] : -1;
}

// ---- Sort --------------------------------------------------------------------------------------------------
static struct { char names[64][40]; const char *ptrs[65]; int ncols; Rect r; Sheet *s; long hdr, key[3], ord[3]; int ok[3], dsc[3]; bool header; } sd;
static void sd_names ()
{
	sd.ncols = imin (64, sd.r.c1 - sd.r.c0 + 1);
	for (int i = 0; i < sd.ncols; i++)
	{
		int c = sd.r.c0 + i;
		char cn[8]; col_name (c, cn);
		Cell *x = sd.s->cells.get (sd.r.r0, c);
		if (sd.hdr && val (sd.hdr) && x && x->kind != K_NONE) { Shown sh; cell_shown (g_b, sd.s, x, sh, 30); scpy (sd.names[i], sh.text, sizeof sd.names[i]); }
		else snprintf (sd.names[i], sizeof sd.names[i], TR ("Column %s"), cn);
		sd.ptrs[i] = sd.names[i];
	}
	sd.ptrs[sd.ncols] = TR ("(none)");
}
static long sd_fn (long d, long w, long code)
{
	(void) d;
	if (w == sd.hdr && code == UN_CHANGE)
	{
		sd_names ();
		for (int k = 0; k < 3; k++) { int v = val (sd.key[k]); dd_fill (sd.key[k], sd.ptrs, sd.ncols + (k ? 1 : 0)); ui_w_set_value (sd.key[k], v); }
	}
	return 0;
}
// -> 1 OK: sd.ok[k] (the column's rank, -1), sd.dsc[k], sd.header
static int sort_dialog (Sheet *s, Rect r, bool header)
{
	int W = 460, H = 270, c = ctl_h ();
	sd.r = r; sd.s = s; sd.hdr = 0;
	long d = ui_dialog_begin (W, H, (long) "Sort");
	if (!d) return 0;
	sd.hdr = check (16, 12, 428, "The first row holds the column labels", header);
	sd_names ();
	static const char *const ORD[2] = { "Ascending", "Descending" };
	static const char *const L[3] = { "Sort by", "Then by", "Then by" };
	for (int k = 0; k < 3; k++)
	{
		lab (16, 48 + k * (c + 8), 90, L[k]);
		sd.key[k] = dd_make (110, 48 + k * (c + 8), 180, c, sd.ptrs, sd.ncols + (k ? 1 : 0), k ? sd.ncols : 0);
		sd.ord[k] = dd_make (304, 48 + k * (c + 8), 140, c, ORD, 2, 0, true);
	}
	ok_cancel (d, W);
	int rr = (int) ui_dialog_run (d, (long) sd_fn, 0);
	for (int k = 0; k < 3; k++) { int v = val (sd.key[k]); sd.ok[k] = v < sd.ncols ? v : -1; sd.dsc[k] = val (sd.ord[k]) == 1; }
	sd.header = val (sd.hdr);
	ui_dialog_end (d);
	return rr;
}

// ---- Find and Replace ---------------------------------------------------------------------------------------
struct FindState { char find[64], repl[64]; bool matchCase, whole, formulas, allSheets; };
static FindState g_find = { "", "", false, false, false, false };
static struct { long fb, rb, mc, wh, fo, al, b2, b3, b4; } fi;
static long fi_fn (long d, long w, long code)
{
	if (code != UN_ACTIVATE) return 0;
	if (w == fi.fb || w == fi.b2) ui_dialog_close (d, 2);
	else if (w == fi.b3) ui_dialog_close (d, 3);
	else if (w == fi.b4) ui_dialog_close (d, 4);
	return 0;
}
// -> 0 closed, 2 find next, 3 replace, 4 replace all
static int find_dialog ()
{
	int W = 470, H = 230, c = ctl_h ();
	long d = ui_dialog_begin (W, H, (long) "Find and Replace");
	if (!d) return 0;
	lab (16, 12, 100, "Find"); fi.fb = field (120, 12, 330, g_find.find);
	lab (16, 48, 100, "Replace with"); fi.rb = field (120, 48, 330, g_find.repl);
	fi.mc = check (16, 88, 200, "Match case", g_find.matchCase); fi.wh = check (230, 88, 220, "Entire cells only", g_find.whole);
	fi.fo = check (16, 116, 200, "Search in formulas", g_find.formulas); fi.al = check (230, 116, 220, "All sheets", g_find.allSheets);
	int y = H - c - 12;
	fi.b2 = ui_pushbutton (16, y, 100, c, (long) "Find Next", UB_DEFAULT);
	fi.b3 = ui_pushbutton (122, y, 90, c, (long) "Replace", 0);
	fi.b4 = ui_pushbutton (218, y, 110, c, (long) "Replace All", 0);
	ui_dialog_button (d, 0, (long) "Close", 0, 0, W - 12); ui_dialog_cancel (d, 0);
	ui_w_focus (fi.fb);
	ui_textbox_select (fi.fb, 0, strlen (g_find.find));
	int r = (int) ui_dialog_run (d, (long) fi_fn, 0);
	scpy (g_find.find, text_of (fi.fb), sizeof g_find.find); scpy (g_find.repl, text_of (fi.rb), sizeof g_find.repl);
	g_find.matchCase = val (fi.mc); g_find.whole = val (fi.wh); g_find.formulas = val (fi.fo); g_find.allSheets = val (fi.al);
	ui_dialog_end (d);
	return r;
}

// ---- Paste Special ------------------------------------------------------------------------------------------
// -> 1 OK: *what (0 all, 1 values, 2 formats, 3 formulas), *transpose
static int paste_dialog (int *what, bool *transpose)
{
	int W = 360, H = 240;
	long d = ui_dialog_begin (W, H, (long) "Paste Special");
	if (!d) return 0;
	static const char *const N[4] = { "All", "Values only", "Formats only", "Formulas only (no formats)" };
	long rb[4];
	for (int i = 0; i < 4; i++) rb[i] = ui_radio (24, 12 + i * 30, 300, 24, (long) TR (N[i]), 1);
	ui_w_set_value (rb[0], 1);
	long tr = check (24, 136, 300, "Transpose (rows become columns)", false);
	ok_cancel (d, W);
	int r = (int) ui_dialog_run (d, 0, 0);
	*what = 0; for (int i = 0; i < 4; i++) if (val (rb[i])) *what = i;
	*transpose = val (tr);
	ui_dialog_end (d);
	return r;
}

// ---- Names (Insert > Names): the book's defined names -- added, changed, deleted -------------------------------
static struct
{
	Book *b; bool changed; long list, name, ref, scope, msg, bAdd, bDel;
	int idx[256], nidx, sc[34], nsc; char scName[34][72]; const char *scPtr[34];
} nd;
static void nd_fill ()
{
	ui_list_clear (nd.list); nd.nidx = 0;
	for (int i = 0; i < nd.b->nnames && nd.nidx < 256; i++)
	{
		const DefName &d = nd.b->names[i];
		Buf f; if (d.f) formula_print (*nd.b, d.f, f);
		char row[240];
		snprintf (row, sizeof row, "%s    %s", d.name, f.str ());
		if (d.scope) { Sheet *hs = book_sheet_by_id (*nd.b, d.scope); if (hs) { int n = (int) strlen (row); snprintf (row + n, sizeof row - n, "  (%s)", hs->name); } }
		ui_list_add (nd.list, (long) row, 0);
		nd.idx[nd.nidx++] = i;
	}
}
static long nd_fn (long d, long w, long code)
{
	(void) d;
	if (w == nd.list && code == UN_CHANGE)
	{
		int s = val (nd.list);
		if (s < 0 || s >= nd.nidx) return 0;
		const DefName &n = nd.b->names[nd.idx[s]];
		ui_w_set_text (nd.name, (long) n.name);
		Buf f; if (n.f) formula_print (*nd.b, n.f, f);
		ui_w_set_text (nd.ref, (long) f.str ());
		for (int i = 0; i < nd.nsc; i++) if (nd.sc[i] == n.scope) ui_w_set_value (nd.scope, i);
		ui_w_set_text (nd.msg, (long) "");
	}
	if (code != UN_ACTIVATE) return 0;
	if (w == nd.bAdd)						// Add (or change)
	{
		char nm[80]; scpy (nm, text_of (nd.name), sizeof nm);
		const char *why = 0;
		if (!name_set (*nd.b, nm, nd.sc[iclamp (val (nd.scope), 0, nd.nsc - 1)], text_of (nd.ref), &why)) ui_w_set_text (nd.msg, (long) TR (why ? why : "This formula cannot be read."));
		else { nd.changed = true; nd_fill (); ui_w_set_text (nd.msg, (long) ""); for (int i = 0; i < nd.nidx; i++) if (!strcmp (nd.b->names[nd.idx[i]].name, nm)) ui_w_set_value (nd.list, i); ui_w_draw (nd.list); }
	}
	else if (w == nd.bDel && val (nd.list) >= 0 && val (nd.list) < nd.nidx)
	{
		name_del (*nd.b, nd.idx[val (nd.list)]); nd.changed = true;
		nd_fill (); ui_w_set_text (nd.name, (long) ""); ui_w_draw (nd.list);
	}
	return 0;
}
static bool names_dialog (Book *b, const char *selRef)
{
	int W = 580, H = 390, c = ctl_h ();
	long d = ui_dialog_begin (W, H, (long) "Names");
	if (!d) return false;
	nd.b = b; nd.changed = false;
	nd.list = ui_list (16, 12, W - 32, 140, 0);
	int y = 164;
	lab (16, y, 110, "Name"); nd.name = field (130, y, 220, "");
	lab (16, y + 36, 110, "Refers to"); nd.ref = field (130, y + 36, W - 146, selRef);
	nd.nsc = 0; nd.sc[0] = 0; scpy (nd.scName[0], TR ("The whole workbook"), sizeof nd.scName[0]); nd.scPtr[0] = nd.scName[0]; nd.nsc = 1;
	for (int i = 0; i < b->ns && nd.nsc < 33; i++) { nd.sc[nd.nsc] = b->sh[i]->id; snprintf (nd.scName[nd.nsc], sizeof nd.scName[0], TR ("Sheet %s"), b->sh[i]->name); nd.scPtr[nd.nsc] = nd.scName[nd.nsc]; nd.nsc++; }
	lab (16, y + 72, 110, "Scope"); nd.scope = dd_make (130, y + 72, 220, c, nd.scPtr, nd.nsc, 0);
	nd.msg = ui_label (16, y + 110, W - 32, c, (long) "", 0);
	nd.bAdd = ui_pushbutton (16, H - c - 12, 90, c, (long) "Add", 0);
	nd.bDel = ui_pushbutton (112, H - c - 12, 90, c, (long) "Delete", 0);
	ui_dialog_button (d, 0, (long) "Close", 0, 0, W - 12); ui_dialog_cancel (d, 0);
	nd_fill ();
	ui_w_focus (nd.name);
	ui_dialog_run (d, (long) nd_fn, 0);
	ui_dialog_end (d);
	return nd.changed;
}

// ---- Conditional formatting (Format > Conditional Formatting): the sheet's rules ---------------------------------
static const char *const CF_TYPE_NAMES[CF_TYPES] = { "Cell value is", "Text", "Top / bottom", "Above / below average", "Duplicate / unique values", "Formula is true", "Colour scale", "Data bar" };
static const char *const CF_CELL_OPS[8] = { "greater than", "greater than or equal to", "less than", "less than or equal to", "equal to", "not equal to", "between", "not between" };
static const char *const CF_TEXT_OPS[4] = { "contains", "does not contain", "begins with", "ends with" };
static const char *const CF_TOP_OPS[4] = { "the top ... values", "the bottom ... values", "the top ... %", "the bottom ... %" };
static const char *const CF_AVG_OPS[2] = { "above the average", "below the average" };
static const char *const CF_DUP_OPS[2] = { "met more than once", "met once only" };
static const char *const CF_NO_OPS[1] = { "-" };
static void cf_describe (const CondFmt &c, char *o, int cap)
{
	char a1[24], a2[24], r[56]; cell_name (c.r.r0, c.r.c0, a1); cell_name (c.r.r1, c.r.c1, a2);
	if (c.r.r0 == c.r.r1 && c.r.c0 == c.r.c1) scpy (r, a1, sizeof r); else snprintf (r, sizeof r, "%s:%s", a1, a2);
	static const char *const SYM[8] = { ">", ">=", "<", "<=", "=", "<>", "between", "not between" };
	char w[160];
	switch (c.type)
	{
	case CF_CELL: if (c.op >= CO_BETWEEN) snprintf (w, sizeof w, TR ("Value %s %.60s and %.60s"), TR (SYM[iclamp (c.op, 0, 7)]), c.a, c.b); else snprintf (w, sizeof w, TR ("Value %s %.100s"), SYM[iclamp (c.op, 0, 7)], c.a); break;
	case CF_TEXT: snprintf (w, sizeof w, TR ("Text %s \"%s\""), TR (CF_TEXT_OPS[iclamp (c.op, 0, 3)]), c.a); break;
	case CF_TOP: snprintf (w, sizeof w, TR (c.op ? "Bottom %s%s" : "Top %s%s"), c.a, c.pct ? " %" : ""); break;
	case CF_AVERAGE: scpy (w, TR (c.op ? "Below the average" : "Above the average"), sizeof w); break;
	case CF_DUP: scpy (w, TR (c.op ? "Unique values" : "Duplicate values"), sizeof w); break;
	case CF_FORMULA: snprintf (w, sizeof w, TR ("Formula %s%s"), c.a[0] == '=' ? "" : "=", c.a); break;
	case CF_SCALE: snprintf (w, sizeof w, TR ("Colour scale (%d colours)"), c.op == 3 ? 3 : 2); break;
	default: scpy (w, TR ("Data bars"), sizeof w); break;
	}
	const char *look = "";
	if (c.type != CF_SCALE && c.type != CF_BAR)
	{
		look = TR ("its own look");
		for (int i = 0; i < NCF_LOOKS; i++) if (CF_LOOKS[i].fill == c.fill && CF_LOOKS[i].color == c.color && CF_LOOKS[i].bold == c.bold) look = TR (CF_LOOKS[i].name);
	}
	snprintf (o, cap, "%s    %s%s%s", r, w, look[0] ? " -- " : "", look);
}
static struct
{
	Book *b; Sheet *s; bool changed, hasOwn; CondFmt own;
	long list, range, type, cond, va, vb, look, msg, lval, land, lLook, bNew, bChange, bDel, bUp, bDown;
	const char *lookNames[NCF_LOOKS + 1]; const char *opt[8];
} cd;
static void cd_fill ()
{
	ui_list_clear (cd.list);
	for (int i = 0; i < cd.s->ncf; i++) { char t[240]; cf_describe (cd.s->cf[i], t, sizeof t); ui_list_add (cd.list, (long) t, 0); }
}
// The fields as the rule's type wants them.
static void cd_sync (bool keepLook)
{
	int t = val (cd.type);
	const char *const *ops = CF_NO_OPS; int nops = 1;
	switch (t)
	{
	case CF_CELL: ops = CF_CELL_OPS; nops = 8; break;
	case CF_TEXT: ops = CF_TEXT_OPS; nops = 4; break;
	case CF_TOP: ops = CF_TOP_OPS; nops = 4; break;
	case CF_AVERAGE: ops = CF_AVG_OPS; nops = 2; break;
	case CF_DUP: ops = CF_DUP_OPS; nops = 2; break;
	}
	static const char *const *was;
	if (was != ops) { was = ops; dd_fill (cd.cond, ops, nops, true); ui_w_set_value (cd.cond, 0); }
	ui_w_enable (cd.cond, nops > 1);
	bool sv = t == CF_CELL || t == CF_TEXT || t == CF_TOP || t == CF_FORMULA, sb = t == CF_CELL && val (cd.cond) >= CO_BETWEEN;
	ui_w_show (cd.va, sv); ui_w_show (cd.lval, sv); ui_w_show (cd.vb, sb); ui_w_show (cd.land, sb);
	ui_w_set_text (cd.lval, (long) TR (t == CF_FORMULA ? "Formula" : "Value"));
	ui_w_set_text (cd.lLook, (long) TR (t == CF_SCALE ? "Colours" : t == CF_BAR ? "Bars" : "Look"));
	if (!keepLook)
	{
		if (t == CF_SCALE) { for (int i = 0; i < NCF_SCALES; i++) cd.opt[i] = CF_SCALES[i].name; dd_fill (cd.look, cd.opt, NCF_SCALES, true); }
		else if (t == CF_BAR) { for (int i = 0; i < NCF_BARS; i++) cd.opt[i] = CF_BARS[i].name; dd_fill (cd.look, cd.opt, NCF_BARS, true); }
		else dd_fill (cd.look, cd.lookNames, NCF_LOOKS + (cd.hasOwn ? 1 : 0), true);
		ui_w_set_value (cd.look, 0);
	}
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_w_draw (0);
}
static bool cd_build (CondFmt &c)
{
	memset (&c, 0, sizeof c); c.fill = c.color = AUTO; c.bold = c.italic = -1;
	Rect r;
	char rt[64]; scpy (rt, text_of (cd.range), sizeof rt);
	if (parse_sqref (rt, &r, 1) != 1) { ui_w_set_text (cd.msg, (long) TR ("Type the cells the rule covers: B5:D16.")); return false; }
	c.r = r; c.type = val (cd.type);
	scpy (c.a, text_of (cd.va), sizeof c.a); scpy (c.b, text_of (cd.vb), sizeof c.b);
	int co = val (cd.cond);
	switch (c.type)
	{
	case CF_CELL: c.op = co; if (c.op < CO_BETWEEN) c.b[0] = 0; break;
	case CF_TEXT: c.op = co; c.b[0] = 0; break;
	case CF_TOP: c.op = co & 1; c.pct = co >= 2; if (atoi (c.a) < 1) scpy (c.a, "10", sizeof c.a); c.b[0] = 0; break;
	case CF_AVERAGE: case CF_DUP: c.op = co; c.a[0] = c.b[0] = 0; break;
	case CF_FORMULA: c.b[0] = 0; if (c.a[0] != '=') { char t[120]; t[0] = '='; scpy (t + 1, c.a, sizeof t - 1); scpy (c.a, t, sizeof c.a); } break;
	default: c.a[0] = c.b[0] = 0; break;
	}
	if ((c.type == CF_CELL || c.type == CF_TEXT || c.type == CF_FORMULA) && !c.a[0]) { ui_w_set_text (cd.msg, (long) TR ("Type the value the cells are compared with.")); return false; }
	if (c.type == CF_CELL && c.op >= CO_BETWEEN && !c.b[0]) { ui_w_set_text (cd.msg, (long) TR ("Type the second value (between ... and ...).")); return false; }
	for (int i = 0; i < 2; i++)				// (the formulas read)
	{
		const char *t = i ? c.b : c.a;
		if (t[0] != '=') continue;
		const char *why = 0;
		Formula *f = formula_parse (*cd.b, t + 1, (int) strlen (t + 1), &why);
		if (!f) { char m[240]; snprintf (m, sizeof m, TR ("This formula cannot be read: %s"), TR (why ? why : "?")); ui_w_set_text (cd.msg, (long) m); return false; }
		formula_free (f);
	}
	int ls = val (cd.look);
	if (c.type == CF_SCALE) { const CfScale &p = CF_SCALES[iclamp (ls, 0, NCF_SCALES - 1)]; c.op = p.n; c.c0 = p.c0; c.c1 = p.c1; c.c2 = p.c2; }
	else if (c.type == CF_BAR) c.c0 = CF_BARS[iclamp (ls, 0, NCF_BARS - 1)].c;
	else if (ls >= NCF_LOOKS && cd.hasOwn) { c.fill = cd.own.fill; c.color = cd.own.color; c.bold = cd.own.bold; c.italic = cd.own.italic; }
	else { const CfPreset &p = CF_LOOKS[iclamp (ls, 0, NCF_LOOKS - 1)]; c.fill = p.fill; c.color = p.color; c.bold = p.bold; }
	return true;
}
static void cd_done (int sel)
{
	cd.changed = true; cf_changed ();
	cd_fill ();
	if (sel >= 0) ui_w_set_value (cd.list, sel);
	ui_w_draw (cd.list);
}
static void cd_picked ()
{
	int i = val (cd.list);
	Sheet *s = cd.s;
	if (i < 0 || i >= s->ncf) return;
	const CondFmt &c = s->cf[i];
	char a1[24], a2[24], t[64]; cell_name (c.r.r0, c.r.c0, a1); cell_name (c.r.r1, c.r.c1, a2);
	if (c.r.r0 == c.r.r1 && c.r.c0 == c.r.c1) scpy (t, a1, sizeof t); else snprintf (t, sizeof t, "%s:%s", a1, a2);
	ui_w_set_text (cd.range, (long) t);
	cd.hasOwn = false;
	int li = -1;
	if (c.type != CF_SCALE && c.type != CF_BAR)
	{
		for (int k = 0; k < NCF_LOOKS; k++) if (CF_LOOKS[k].fill == c.fill && CF_LOOKS[k].color == c.color && CF_LOOKS[k].bold == c.bold) li = k;
		if (li < 0) { cd.hasOwn = true; cd.own = c; li = NCF_LOOKS; }
	}
	ui_w_set_value (cd.type, c.type);
	cd_sync (false);
	int op = c.type == CF_TOP ? c.op + (c.pct ? 2 : 0) : c.type == CF_FORMULA || c.type >= CF_SCALE ? 0 : c.op;
	ui_w_set_value (cd.cond, op);
	ui_w_set_text (cd.va, (long) c.a); ui_w_set_text (cd.vb, (long) c.b);
	if (c.type == CF_SCALE) { for (int k = 0; k < NCF_SCALES; k++) if (CF_SCALES[k].c0 == c.c0 && CF_SCALES[k].c2 == c.c2 && CF_SCALES[k].n == (c.op == 3 ? 3 : 2)) li = k; }
	else if (c.type == CF_BAR) { for (int k = 0; k < NCF_BARS; k++) if (CF_BARS[k].c == c.c0) li = k; }
	ui_w_set_value (cd.look, imax (li, 0));
	cd_sync (true);
	ui_w_set_text (cd.msg, (long) "");
}
static long cd_fn (long d, long w, long code)
{
	(void) d;
	Sheet *s = cd.s;
	if (code == UN_CHANGE)
	{
		if (w == cd.list) cd_picked ();
		else if (w == cd.type) cd_sync (false);
		else if (w == cd.cond) cd_sync (true);
		return 0;
	}
	if (code != UN_ACTIVATE) return 0;
	int sel = val (cd.list);
	if (w == cd.bNew || w == cd.bChange)
	{
		CondFmt c;
		if (cd_build (c))
		{
			ui_w_set_text (cd.msg, (long) "");
			if (w == cd.bChange && sel >= 0 && sel < s->ncf) s->cf[sel] = c;
			else { s->cf = (CondFmt *) realloc (s->cf, (s->ncf + 1) * sizeof (CondFmt)); s->cf[s->ncf++] = c; sel = s->ncf - 1; }
			cd_done (sel);
		}
	}
	else if (w == cd.bDel && sel >= 0 && sel < s->ncf)
	{
		memmove (s->cf + sel, s->cf + sel + 1, (s->ncf - sel - 1) * sizeof (CondFmt)); s->ncf--;
		cd_done (imin (sel, s->ncf - 1));
	}
	else if ((w == cd.bUp || w == cd.bDown) && sel >= 0 && sel < s->ncf)
	{
		int to = w == cd.bUp ? sel - 1 : sel + 1;
		if (to >= 0 && to < s->ncf) { CondFmt t = s->cf[sel]; s->cf[sel] = s->cf[to]; s->cf[to] = t; cd_done (to); }
	}
	return 0;
}
static bool cond_dialog (Book *b, Sheet *s, const char *rangeText)
{
	int W = 660, H = 440, c = ctl_h ();
	long d = ui_dialog_begin (W, H, (long) "Conditional Formatting");
	if (!d) return false;
	cd.b = b; cd.s = s; cd.changed = false; cd.hasOwn = false;
	cd.list = ui_list (16, 12, W - 32, 120, 0);
	int y = 142, st = c + 8;
	lab (16, y, 100, "Cells"); cd.range = field (130, y, 200, rangeText);
	lab (16, y + st, 100, "Rule"); cd.type = dd_make (130, y + st, 250, c, CF_TYPE_NAMES, CF_TYPES, 0, true);
	lab (16, y + 2 * st, 100, "Condition"); cd.cond = dd_make (130, y + 2 * st, 250, c, CF_CELL_OPS, 8, 0, true);
	cd.lval = ui_label (16, y + 3 * st, 100, c, (long) "Value", 0);
	cd.va = field (130, y + 3 * st, 200, "");
	cd.land = ui_label (346, y + 3 * st, 30, c, (long) "and", 0);
	cd.vb = field (380, y + 3 * st, 200, "");
	for (int i = 0; i < NCF_LOOKS; i++) cd.lookNames[i] = CF_LOOKS[i].name;
	cd.lookNames[NCF_LOOKS] = "Its own look (as read)";
	cd.lLook = ui_label (16, y + 4 * st, 100, c, (long) "Look", 0);
	cd.look = dd_make (130, y + 4 * st, 330, c, cd.lookNames, NCF_LOOKS, 0, true);
	cd.msg = ui_label (16, y + 5 * st, W - 32, c, (long) "", 0);
	int by = H - c - 12;
	cd.bNew = ui_pushbutton (16, by, 84, c, (long) "New", 0); cd.bChange = ui_pushbutton (106, by, 84, c, (long) "Change", 0); cd.bDel = ui_pushbutton (196, by, 84, c, (long) "Delete", 0);
	cd.bUp = ui_pushbutton (300, by, 80, c, (long) "Up", 0); cd.bDown = ui_pushbutton (386, by, 80, c, (long) "Down", 0);
	ui_dialog_button (d, 0, (long) "Close", 0, 0, W - 12); ui_dialog_cancel (d, 0);
	cd_fill ();
	cd_sync (true);
	ui_dialog_run (d, (long) cd_fn, 0);
	ui_dialog_end (d);
	return cd.changed;
}

// ---- the AutoFilter's dialog: sort the table by the column; the values shown, checked ---------------------------
// (a hand-drawn list: the rows' boxes in a view, the wheel and the bar move them)
enum { FP_RH = 22, FP_ROWS = 11 };
static struct { AfVal *v; int n, top; bool *on; long view, bar, bAsc, bDesc; } fp;
static bool fp_all () { for (int i = 0; i < fp.n; i++) if (!fp.on[i]) return false; return true; }
static long fp_paint (long v, long W, long H)
{
	cv_begin (v);
	cv_fill (0, 0, (int) W, (int) H, jet (J_SURFACE));
	Font f = ui_font (ui_px ());
	unsigned ink = jet (J_TEXTE1), dim = jet (J_TEXTE3), accent = jet (J_ACCENT);
	Rect cl = { 0, 0, (int) H - 1, (int) W - 16 };
	for (int r = 0; r < FP_ROWS; r++)
	{
		int i = fp.top + r, y = r * FP_RH;
		if (i > fp.n) break;
		bool on = i == 0 ? fp_all () : fp.on[i - 1];
		ui_clip (0, 0, 0, 0);
		ui_frame (8, y + 4, 14, 14, jet (J_LIGNE_FORTE));
		if (on) { ui_fill (9, y + 5, 12, 12, accent); text_at (f, 15, y + 16, "\xE2\x9C\x93", 0xFFFFFF, 1, cl); }
		const char *t = i == 0 ? TR ("(Select all)") : fp.v[i - 1].blank ? TR ("(Empty)") : fp.v[i - 1].t;
		text_at (f, 30, y + 16, t, i == 0 || fp.v[i - 1].blank ? dim : ink, 0, cl);
	}
	cv_end ();
	return 0;
}
static long fp_mouse (long v, long kind, long x, long y, long info)
{
	(void) x; (void) info;
	if (kind != 1) return 0;
	int i = fp.top + (int) y / FP_RH;
	if (i > fp.n) return 0;
	if (i == 0) { bool all = fp_all (); for (int k = 0; k < fp.n; k++) fp.on[k] = !all; }
	else fp.on[i - 1] = !fp.on[i - 1];
	ui_view_dirty (v);
	ui_view_flush ();
	return 0;
}
static long fp_wheel (long v, long n)
{
	fp.top = iclamp (fp.top - (int) n, 0, imax (0, fp.n + 1 - FP_ROWS));
	ui_w_set_value (fp.bar, fp.top);
	ui_view_dirty (v);
	ui_view_flush ();
	return 0;
}
static long fp_fn (long d, long w, long code)
{
	if (w == fp.bar && code == UN_CHANGE) { fp.top = val (fp.bar); ui_view_dirty (fp.view); ui_view_flush (); }
	if (code == UN_ACTIVATE) { if (w == fp.bAsc) ui_dialog_close (d, 2); if (w == fp.bDesc) ui_dialog_close (d, 3); }
	return 0;
}
// -> 0 cancelled, 1 OK (fp.on), 2 sort ascending, 3 descending; the caller frees fp.on.
static int filter_dialog (AfVal *vals, int n, const char *shown)
{
	int W = 300, c = ctl_h (), lh = FP_RH * FP_ROWS, H = 16 + 2 * (c + 6) + lh + 12 + c + 24;
	fp.v = vals; fp.n = n; fp.top = 0;
	fp.on = (bool *) malloc (imax (1, n) * sizeof (bool));
	for (int i = 0; i < n; i++) fp.on[i] = !shown || af_has (shown, vals[i].t);
	long d = ui_dialog_begin (W, H, (long) "AutoFilter");
	if (!d) return 0;
	fp.bAsc = ui_pushbutton (12, 8, W - 24, c, (long) "Sort ascending", 0);
	fp.bDesc = ui_pushbutton (12, 8 + c + 6, W - 24, c, (long) "Sort descending", 0);
	fp.view = ui_view (12, 16 + 2 * (c + 6), W - 24, lh, 0);
	ui_view_on (fp.view, (long) fp_paint, (long) fp_mouse, 0, (long) fp_wheel);
	fp.bar = ui_scrollbar (W - 12 - 12, 16 + 2 * (c + 6), 11, lh, 0);
	ui_w_set_parent (fp.bar, fp.view);
	ui_w_set_range (fp.bar, 0, n + 1, FP_ROWS, 1);
	ok_cancel (d, W);
	int r = (int) ui_dialog_run (d, (long) fp_fn, 0);
	ui_dialog_end (d);
	return r;
}

// ---- Chart ------------------------------------------------------------------------------------------------
static const char *const LEGEND_NAMES[4] = { "None", "Right", "Bottom", "Top" };
static struct { Book *b; Chart c; long type, title, legend, rows, rows2, head, side, stack, grid, view; } ch;
static long ch_paint (long v, long w, long h)
{
	cv_begin (v);
	draw_chart (*ch.b, &ch.c, 0, 0, (int) w, (int) h, 100, Rect { 0, 0, (int) h - 1, (int) w - 1 });
	cv_end ();
	return 0;
}
static void ch_read ()
{
	Chart &c = ch.c;
	c.type = val (ch.type);
	scpy (c.title, text_of (ch.title), sizeof c.title);
	c.legend = val (ch.legend); c.byRows = val (ch.rows2); c.head = val (ch.head); c.side = val (ch.side); c.stacked = val (ch.stack); c.grid = val (ch.grid);
}
static long ch_fn (long d, long w, long code)
{
	(void) d; (void) w;
	if (code == UN_CHANGE) { ch_read (); ui_view_dirty (ch.view); ui_view_flush (); }
	return 0;
}
// -> 1 OK: ch.c
static int chart_dialog (Book *b, const Chart &c0, bool isNew)
{
	int W = 720, H = 440, c = ctl_h ();
	ch.b = b; ch.c = c0;
	long d = ui_dialog_begin (W, H, (long) (isNew ? "Insert Chart" : "Chart"));
	if (!d) return 0;
	int y = 12, st = c + 8;
	lab (16, y, 76, "Type"); ch.type = dd_make (96, y, 190, c, CHART_NAMES, CH_COUNT, c0.type, true); y += st;
	lab (16, y, 76, "Title"); ch.title = field (96, y, 190, c0.title); y += st;
	lab (16, y, 76, "Legend"); ch.legend = dd_make (96, y, 190, c, LEGEND_NAMES, 4, c0.legend, true); y += st;
	lab (16, y, 76, "Series");
	ch.rows = ui_radio (96, y, 100, 24, (long) TR ("Columns"), 2); ch.rows2 = ui_radio (200, y, 90, 24, (long) TR ("Rows"), 2);
	ui_w_set_value (c0.byRows ? ch.rows2 : ch.rows, 1); y += st;
	ch.head = check (16, y, 280, "The first row names", c0.head); y += 28;
	ch.side = check (16, y, 280, "The first column names", c0.side); y += 28;
	ch.stack = check (16, y, 140, "Stacked", c0.stacked);
	ch.grid = check (156, y, 140, "Gridlines", c0.grid);
	ch.view = ui_view (316, 12, W - 332, H - 64 - 12, 0);
	ui_view_on (ch.view, (long) ch_paint, 0, 0, 0);
	ok_cancel (d, W);
	int r = (int) ui_dialog_run (d, (long) ch_fn, 0);
	ch_read ();
	ui_dialog_end (d);
	return r;
}

} // namespace ss

#endif
