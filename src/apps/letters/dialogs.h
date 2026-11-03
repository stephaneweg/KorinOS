//
// dialogs.h -- Letters' dialogs on UIKit's (ui_dialog_begin / run / end): Font, Paragraph, Tabs, Page Setup, Word Count,
// Find and Replace, Special Character, Date and Time, Field, Page Numbers, Insert Table, Table Properties. A dialog is a
// window of its own: the page behind it does not repaint, so Find and Replace closes to show what it found, and comes
// back (dlg_find).
//
#ifndef _writer_dialogs_h
#define _writer_dialogs_h

#include "ui.h"

namespace wr {

static void fmt_len (char *b, int tw)		// twips -> "2.54" (cm or in)
{
	int unit = g_inches ? 1440 : 567;
	int v = (int) (((long long) (tw < 0 ? -tw : tw) * 100 + unit / 2) / unit);
	sprintf (b, "%s%d.%02d", tw < 0 ? "-" : "", v / 100, v % 100);
}
static bool parse_num (const char *s, long *hundredths)
{
	long v = 0; int frac = -1; bool neg = false, any = false;
	for (; *s; s++)
	{
		if (*s == '-') neg = true;
		else if (*s >= '0' && *s <= '9') { any = true; if (v > 100000000) return false; if (frac < 0) v = v * 10 + (*s - '0'); else if (frac < 2) { v = v * 10 + (*s - '0'); frac++; } }
		else if ((*s == '.' || *s == ',') && frac < 0) frac = 0;
		else if (*s != ' ') break;
	}
	if (!any) return false;
	if (frac < 0) frac = 0;
	while (frac < 2) { v *= 10; frac++; }
	*hundredths = neg ? -v : v;
	return true;
}
static bool parse_len (const char *s, int *tw)
{
	long h; if (!parse_num (s, &h)) return false;
	int unit = g_inches ? 1440 : 567;
	*tw = (int) (h * unit / 100);
	return true;
}
static inline const char *wtext (long w) { return (const char *) ui_w_get_text (w); }
static inline long wval (long w) { return ui_w_get_value (w); }

// ---- a dialog's controls ---------------------------------------------------------------------------------
static long d_label (int x, int y, int w, const char *t, long fl = 0) { return ui_label (x, y, w, ctl_h (), (long) t, fl); }
static long d_field (int x, int y, int w, const char *t) { long f = ui_textbox (x, y, w, ctl_h (), 0); ui_w_set_text (f, (long) t); return f; }
static long d_check (int x, int y, int w, const char *t, bool on) { return ui_checkbox (x, y, w, ctl_h (), (long) TR (t), on); }
static long d_radio (int x, int y, int w, const char *t, int group, bool on) { long r = ui_radio (x, y, w, ctl_h (), (long) TR (t), group); if (on) ui_w_set_value (r, 1); return r; }
static long d_drop (int x, int y, int w, const char *items, int sel) { long d = ui_dropdown (x, y, w, ctl_h (), (long) TR (items)); ui_w_set_value (d, sel); return d; }
static long d_button (int x, int y, int w, const char *t) { return ui_pushbutton (x, y, w, ctl_h (), (long) t, 0); }
static void d_ok_cancel (long d, int w) { long x = ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, w - 12); ui_dialog_button (d, 1, (long) "Cancel", 0, 0, x); ui_dialog_cancel (d, 0); }
static void d_close (long d, int w) { ui_dialog_button (d, 0, (long) "Close", 0, UB_DEFAULT, w - 12); ui_dialog_cancel (d, 0); }

// ---- Font ------------------------------------------------------------------------------------------------
static const char *const FONT_LIST[] = { "Liberation Serif", "Liberation Sans", "DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono", "Times New Roman", "Arial", "Calibri", "Cambria", "Courier New" };
enum { NFONTS = sizeof FONT_LIST / sizeof FONT_LIST[0] };
static const char *const STYLE_LIST[4] = { "Regular", "Italic", "Bold", "Bold Italic" };

static struct { long fam, sty, siz, u, s, sup, sub, col, colA, hil, hilN; } FD;
static long fd_fn (long, long w, long code)
{
	if (code == UN_CHANGE && w == FD.sup && wval (FD.sup) && wval (FD.sub)) ui_w_set_value (FD.sub, 0);
	if (code == UN_CHANGE && w == FD.sub && wval (FD.sub) && wval (FD.sup)) ui_w_set_value (FD.sup, 0);
	if (code == UN_CHANGE && w == FD.col) ui_w_set_value (FD.colA, 0);
	if (code == UN_CHANGE && w == FD.hil) ui_w_set_value (FD.hilN, 0);
	return 0;
}
static bool dlg_font ()
{
	CharFmt f = caret_fmt ();
	enum { W = 480, H = 330 };
	long d = ui_dialog_begin (W, H, (long) "Font");
	int c = ctl_h ();
	d_label (16, 12, 200, "Font:"); d_label (246, 12, 100, "Style:"); d_label (366, 12, 90, "Size:");
	FD.fam = ui_list (16, 12 + c, 220, 150, 0);
	const char *cur = g_doc.fontName[f.font]; int sel = -1;
	for (int i = 0; i < NFONTS; i++) { ui_list_add (FD.fam, (long) FONT_LIST[i], 0); if (!sicmp (FONT_LIST[i], cur)) sel = i; }
	if (sel < 0) { ui_list_add (FD.fam, (long) cur, 0); sel = NFONTS; }
	ui_w_set_value (FD.fam, sel);
	FD.sty = ui_list (246, 12 + c, 110, 150, 0);
	for (int i = 0; i < 4; i++) ui_list_add (FD.sty, (long) TR (STYLE_LIST[i]), 0);
	ui_w_set_value (FD.sty, (f.flags & CF_BOLD ? 2 : 0) + (f.flags & CF_ITALIC ? 1 : 0));
	char b[16]; sprintf (b, "%d%s", f.size / 2, f.size & 1 ? ".5" : "");
	FD.siz = d_field (366, 12 + c, 98, b);
	int y = 12 + c + 150 + 8;
	FD.u = d_check (16, y, 130, "Underline", f.flags & CF_UNDER);
	FD.s = d_check (16, y + c, 130, "Strikethrough", f.flags & CF_STRIKE);
	FD.sup = d_check (150, y, 120, "Superscript", f.flags & CF_SUPER);
	FD.sub = d_check (150, y + c, 120, "Subscript", f.flags & CF_SUB);
	d_label (276, y, 76, "Colour:"); d_label (276, y + c, 76, "Highlight:");
	FD.col = ui_colorpicker (352, y, 30, c - 2, f.color == AUTO ? 0 : f.color);
	FD.colA = d_check (388, y, 84, "Auto", f.color == AUTO);
	FD.hil = ui_colorpicker (352, y + c, 30, c - 2, f.hilite == AUTO ? 0xFFFF00 : f.hilite);
	FD.hilN = d_check (388, y + c, 84, "None", f.hilite == AUTO);
	d_ok_cancel (d, W);
	long r = ui_dialog_run (d, (long) fd_fn, 0);
	CfChange ch;
	if (r == 1)
	{
		ch.what = CH_FONT | CH_SIZE | CH_FLAGS | CH_COLOR | CH_HILITE;
		long v = wval (FD.fam);
		ch.font = (short) doc_font (g_doc, v >= 0 && v < NFONTS ? FONT_LIST[v] : g_doc.fontName[f.font]);
		long h; ch.size = f.size;
		if (parse_num (wtext (FD.siz), &h) && h >= 100 && h <= 163800) ch.size = (short) ((h * 2 + 50) / 100);
		int s = (int) wval (FD.sty);
		unsigned short fl = (unsigned short) ((s & 2 ? CF_BOLD : 0) | (s & 1 ? CF_ITALIC : 0));
		if (wval (FD.u)) fl |= CF_UNDER;
		if (wval (FD.s)) fl |= CF_STRIKE;
		if (wval (FD.sup)) fl |= CF_SUPER;
		if (wval (FD.sub)) fl |= CF_SUB;
		ch.setFlags = fl; ch.clearFlags = CF_BOLD | CF_ITALIC | CF_UNDER | CF_STRIKE | CF_SUPER | CF_SUB;
		ch.color = wval (FD.colA) ? AUTO : (unsigned) wval (FD.col) & 0xFFFFFF;
		ch.hilite = wval (FD.hilN) ? AUTO : (unsigned) wval (FD.hil) & 0xFFFFFF;
	}
	ui_dialog_end (d);
	if (r == 1) ed_format (ch);
	return r == 1;
}

// ---- Tabs ------------------------------------------------------------------------------------------------
static const char *const TAB_ALIGN_NAMES[4] = { "Left", "Centre", "Right", "Decimal" };
static const char *const TAB_LEAD_NAMES[4] = { "None", "Dots ......", "Dashes -----", "Line ______" };
static struct { ParaFmt pf; long list, pos, al[4], ld[4], bSet, bClr, bAll; } TD;
static void td_fill ()
{
	ui_list_clear (TD.list);
	for (int i = 0; i < TD.pf.ntab; i++)
	{
		char b[48], l[16]; fmt_len (l, TD.pf.tab[i].pos);
		snprintf (b, sizeof b, "%s  %s%s", l, TR (TAB_ALIGN_NAMES[TD.pf.tab[i].align & 3]), TD.pf.tab[i].leader == TL_DOT ? " ..." : TD.pf.tab[i].leader == TL_DASH ? " ---" : TD.pf.tab[i].leader ? " ___" : "");
		ui_list_add (TD.list, (long) b, 0);
	}
	ui_w_draw (TD.list);
}
static int td_of (long *r) { for (int i = 0; i < 4; i++) if (wval (r[i])) return i; return 0; }
static bool td_set ()
{
	int v; const char *t = wtext (TD.pos);
	if (!t[0] || !parse_len (t, &v) || v <= 0 || v > 30000) return false;
	pf_add_tab (TD.pf, v, td_of (TD.al), td_of (TD.ld));
	ui_w_set_text (TD.pos, (long) "");
	td_fill ();
	return true;
}
static long td_fn (long, long w, long code)
{
	if (code == UN_ACTIVATE && w == TD.bSet) td_set ();
	else if (code == UN_ACTIVATE && w == TD.bClr) { int i = (int) wval (TD.list); if (i >= 0 && i < TD.pf.ntab) { pf_del_tab (TD.pf, i); td_fill (); } }
	else if (code == UN_ACTIVATE && w == TD.bAll) { TD.pf.ntab = 0; td_fill (); }
	else if (code == UN_CHANGE && w == TD.list)
	{
		int i = (int) wval (TD.list);
		if (i < 0 || i >= TD.pf.ntab) return 0;
		char b[16]; fmt_len (b, TD.pf.tab[i].pos); ui_w_set_text (TD.pos, (long) b);
		ui_w_set_value (TD.al[TD.pf.tab[i].align & 3], 1); ui_w_set_value (TD.ld[TD.pf.tab[i].leader & 3], 1);
	}
	return 0;
}
static bool tabs_run (ParaFmt &pf)
{
	TD.pf = pf;
	enum { W = 460, H = 300 };
	long d = ui_dialog_begin (W, H, (long) "Tabs");
	int c = ctl_h ();
	d_label (16, 12, 160, g_inches ? "Position (in):" : "Position (cm):"); d_label (190, 12, 110, "Alignment:"); d_label (316, 12, 130, "Leader:");
	TD.pos = d_field (16, 12 + c, 150, "");
	TD.list = ui_list (16, 16 + 2 * c, 150, H - 90 - 2 * c, 0);
	for (int i = 0; i < 4; i++) TD.al[i] = d_radio (190, 12 + c + i * c, 110, TAB_ALIGN_NAMES[i], 1, i == 0);
	for (int i = 0; i < 4; i++) TD.ld[i] = d_radio (316, 12 + c + i * c, 130, TAB_LEAD_NAMES[i], 2, i == 0);
	int y = 20 + 5 * c;
	TD.bSet = d_button (190, y, 76, "Set"); TD.bClr = d_button (272, y, 76, "Clear"); TD.bAll = d_button (354, y, 90, "Clear All");
	d_label (190, y + c + 4, 256, "Default stops: every 1.25 cm.", ULB_DIM);
	d_ok_cancel (d, W);
	td_fill ();
	long r = ui_dialog_run (d, (long) td_fn, 0);
	if (r == 1) td_set ();
	ui_dialog_end (d);
	if (r == 1) { pf.ntab = TD.pf.ntab; for (int i = 0; i < TD.pf.ntab; i++) pf.tab[i] = TD.pf.tab[i]; }
	return r == 1;
}

// ---- Paragraph -------------------------------------------------------------------------------------------
static const short LINE_VALS[4] = { 100, 115, 150, 200 };
static struct { long tabs; ParaFmt pf; bool tabsSet; } PD;
static long pd_fn (long d, long w, long code) { if (code == UN_ACTIVATE && w == PD.tabs) ui_dialog_close (d, 2); return 0; }
static bool dlg_paragraph ()
{
	ParaFmt pf = g_doc.p[sel_a ().p]->pf;
	bool tabsSet = false;
	for (;;)
	{
		enum { W = 500, H = 340 };
		long d = ui_dialog_begin (W, H, (long) "Paragraph");
		int c = ctl_h () + 2;
		char b[16];
		const char *u = g_inches ? "in" : "cm";
		d_label (16, 12, 110, "Alignment:");
		long al = d_drop (130, 12, 150, "Left|Centred|Right|Justified", pf.align & 3);
		int y = 12 + c + 6;
		d_label (16, y, 110, "Indent left:"); fmt_len (b, pf.left); long tL = d_field (130, y, 80, b); d_label (214, y, 30, u);
		d_label (260, y, 70, "Special:");
		long sp = d_drop (334, y, 150, "(none)|First line|Hanging", pf.first > 0 ? 1 : pf.first < 0 ? 2 : 0);
		y += c;
		d_label (16, y, 110, "Indent right:"); fmt_len (b, pf.right); long tR = d_field (130, y, 80, b); d_label (214, y, 30, u);
		d_label (260, y, 70, "By:"); fmt_len (b, pf.first < 0 ? -pf.first : pf.first); long tBy = d_field (334, y, 80, b);
		y += c + 6;
		d_label (16, y, 110, "Space before:"); sprintf (b, "%d", pf.before / 20); long tB = d_field (130, y, 80, b); d_label (214, y, 30, "pt");
		d_label (260, y, 70, "Line:");
		int li = 0; for (int i = 0; i < 4; i++) if (LINE_VALS[i] == pf.line) li = i;
		long ln = d_drop (334, y, 150, "Single|1.15 lines|1.5 lines|Double", li);
		y += c;
		d_label (16, y, 110, "Space after:"); sprintf (b, "%d", pf.after / 20); long tA = d_field (130, y, 80, b); d_label (214, y, 30, "pt");
		y += c + 6;
		long c1 = d_check (16, y, 220, "Page break before", pf.pageBreak), c2 = d_check (250, y, 230, "Keep with next", pf.keepNext);
		y += c;
		long c3 = d_check (16, y, 220, "Keep lines together", pf.keepLines), c4 = d_check (250, y, 230, "Widow / orphan control", pf.widow);
		PD.tabs = d_button (16, H - c - 8, 90, "Tabs...");
		d_ok_cancel (d, W);
		long r = ui_dialog_run (d, (long) pd_fn, 0);
		if (r)
		{
			int v;
			pf.align = (unsigned char) wval (al);
			if (parse_len (wtext (tL), &v)) pf.left = (short) wclamp (v, 0, 20000);
			if (parse_len (wtext (tR), &v)) pf.right = (short) wclamp (v, 0, 20000);
			int by = 0; parse_len (wtext (tBy), &by); by = wclamp (by, 0, 20000);
			long s = wval (sp);
			pf.first = (short) (s == 1 ? by : s == 2 ? -by : 0);
			if (pf.first < -pf.left) pf.first = (short) -pf.left;
			long h;
			if (parse_num (wtext (tB), &h)) pf.before = (short) wclamp ((int) (h * 20 / 100), 0, 20000);
			if (parse_num (wtext (tA), &h)) pf.after = (short) wclamp ((int) (h * 20 / 100), 0, 20000);
			pf.line = LINE_VALS[wval (ln) & 3];
			pf.pageBreak = wval (c1) != 0; pf.keepNext = wval (c2) != 0; pf.keepLines = wval (c3) != 0; pf.widow = wval (c4) != 0;
		}
		ui_dialog_end (d);
		if (r == 0) return false;
		if (r == 2) { if (tabs_run (pf)) tabsSet = true; continue; }
		break;
	}
	g_pfSet = pf;
	ed_para (pf_set, PF_ALIGN | PF_LEFT | PF_RIGHT | PF_FIRST | PF_BEFORE | PF_AFTER | PF_LINE | PF_BREAK | PF_KEEP | PF_KEEPLINES | PF_WIDOW | (tabsSet ? PF_TABS : 0));
	return true;
}
static bool dlg_tabs ()
{
	ParaFmt pf = g_doc.p[sel_a ().p]->pf;
	if (!tabs_run (pf)) return false;
	g_pfSet = pf;
	ed_para (pf_set, PF_TABS);
	return true;
}

// ---- Page Setup ------------------------------------------------------------------------------------------
struct Paper { int w, h; };
static const Paper PAPERS[] = { { 11906, 16838 }, { 8391, 11906 }, { 16838, 23811 }, { 12240, 15840 }, { 12240, 20160 } };
static bool dlg_page_setup ()
{
	PageSetup ps = g_doc.page;
	enum { W = 440, H = 340 };
	long d = ui_dialog_begin (W, H, (long) "Page Setup");
	int c = ctl_h () + 2, pw = wmin (ps.w, ps.h), ph = wmax (ps.w, ps.h), sel = 0;
	for (int i = 0; i < 5; i++) if (PAPERS[i].w == pw && PAPERS[i].h == ph) sel = i;
	char b[16];
	const char *u = g_inches ? "(in)" : "(cm)";
	d_label (16, 12, 100, "Paper:");
	long paper = d_drop (120, 12, 210, "A4 (21 x 29.7 cm)|A5 (14.8 x 21 cm)|A3 (29.7 x 42 cm)|Letter (8.5 x 11 in)|Legal (8.5 x 14 in)", sel);
	long port = d_radio (120, 12 + c, 110, "Portrait", 1, ps.w <= ps.h), land = d_radio (230, 12 + c, 120, "Landscape", 1, ps.w > ps.h);
	int y = 16 + 2 * c;
	d_label (16, y, 100, "Top:"); fmt_len (b, ps.top); long tT = d_field (120, y, 80, b); d_label (204, y, 40, u);
	d_label (246, y, 90, "Bottom:"); fmt_len (b, ps.bottom); long tB = d_field (340, y, 80, b);
	y += c;
	d_label (16, y, 100, "Left:"); fmt_len (b, ps.left); long tL = d_field (120, y, 80, b); d_label (204, y, 40, u);
	d_label (246, y, 90, "Right:"); fmt_len (b, ps.right); long tR = d_field (340, y, 80, b);
	y += c;
	d_label (16, y, 100, "Header:"); fmt_len (b, ps.hdr); long tH = d_field (120, y, 80, b); d_label (204, y, 40, u);
	d_label (246, y, 90, "Footer:"); fmt_len (b, ps.ftr); long tF = d_field (340, y, 80, b);
	y += c;
	long first = d_check (16, y, 220, "Different first page", ps.titlePg);
	d_label (246, y, 90, "First number:"); sprintf (b, "%d", ps.start); long tS = d_field (340, y, 80, b);
	d_ok_cancel (d, W);
	long r = ui_dialog_run (d, 0, 0);
	if (r == 1)
	{
		const Paper &p = PAPERS[wval (paper) % 5];
		bool ls = wval (land) != 0; (void) port;
		ps.w = ls ? p.h : p.w; ps.h = ls ? p.w : p.h;
		int v;
		if (parse_len (wtext (tT), &v)) ps.top = wclamp (v, 0, ps.h / 3);
		if (parse_len (wtext (tB), &v)) ps.bottom = wclamp (v, 0, ps.h / 3);
		if (parse_len (wtext (tL), &v)) ps.left = wclamp (v, 0, ps.w / 3);
		if (parse_len (wtext (tR), &v)) ps.right = wclamp (v, 0, ps.w / 3);
		if (parse_len (wtext (tH), &v)) ps.hdr = wclamp (v, 0, ps.h / 3);
		if (parse_len (wtext (tF), &v)) ps.ftr = wclamp (v, 0, ps.h / 3);
		long h; if (parse_num (wtext (tS), &h)) ps.start = (int) wclamp (h / 100, 0L, 9999L);
		ps.titlePg = wval (first) != 0;
	}
	ui_dialog_end (d);
	if (r != 1) return false;
	g_doc.page = ps;
	g_doc.changes++;
	doc_dirty_all (g_doc);
	return true;
}

// ---- Word Count ------------------------------------------------------------------------------------------
static void dlg_word_count ()
{
	Pos a = has_sel () ? sel_a () : mkpos (0, 0), b = has_sel () ? sel_b () : doc_end (g_doc);
	Counts c = doc_count (g_doc, a, b);
	int nl = 0;
	for (int p = a.p; p <= b.p; p++) nl += g_doc.p[p]->nln;
	char t[240];
	snprintf (t, sizeof t, TR ("Pages: %d\nWords: %d\nCharacters (no spaces): %d\nCharacters (with spaces): %d\nParagraphs: %d\nLines: %d"), L.npages, c.words, c.chars, c.charsSp, c.paras, nl);
	ui_message ((long) TR (has_sel () ? "Word Count (selection)" : "Word Count"), (long) t, UM_OK);
}

// ---- Find and Replace ------------------------------------------------------------------------------------
static char s_find[256], s_rep[256], s_msg[64]; static bool s_case;
static bool find_again ()
{
	unsigned pat[64]; int n = decode_text (s_find, slen (s_find), pat, 64);
	Pos a, b;
	if (n && find_next (pat, n, s_case, has_sel () ? sel_b () : g_caret, a, b)) { set_caret (a, false); set_caret (b, true); return true; }
	return false;
}
// Each step closes the dialog, shows the page, and opens it again; Close ends it.
static void show_page ();
static void dlg_find ()
{
	if (has_sel () && sel_a ().p == sel_b ().p && sel_b ().o - sel_a ().o < 60)	// (the selection: what is looked for)
	{
		unsigned u[64]; int n = doc_text (g_doc, sel_a (), sel_b (), u, 63);
		encode_text (u, n, s_find, sizeof s_find);
	}
	s_msg[0] = 0;
	for (;;)
	{
		enum { W = 470, H = 190 };
		long d = ui_dialog_begin (W, H, (long) "Find and Replace");
		int c = ctl_h () + 4;
		d_label (16, 12, 100, "Find what:"); long tF = d_field (120, 12, 334, s_find);
		d_label (16, 12 + c, 100, "Replace with:"); long tR = d_field (120, 12 + c, 334, s_rep);
		long cb = d_check (120, 12 + 2 * c, 160, "Match case", s_case);
		d_label (284, 12 + 2 * c, 170, s_msg, ULB_DIM);
		long x = ui_dialog_button (d, 0, (long) "Find Next", 2, UB_DEFAULT, W - 12);
		x = ui_dialog_button (d, 1, (long) "Replace", 3, 0, x);
		x = ui_dialog_button (d, 2, (long) "Replace All", 4, 0, x);
		ui_dialog_cancel (d, 0);
		ui_w_focus (tF); ui_textbox_select (tF, 0, slen (s_find));
		long r = ui_dialog_run (d, 0, 0);
		scpy (s_find, wtext (tF), sizeof s_find); scpy (s_rep, wtext (tR), sizeof s_rep); s_case = wval (cb) != 0;
		ui_dialog_end (d);
		if (r == 0) return;
		s_msg[0] = 0;
		unsigned pat[64], rep[64]; int n = decode_text (s_find, slen (s_find), pat, 64), rn = decode_text (s_rep, slen (s_rep), rep, 64);
		if (r == 3 && n && has_sel () && sel_a ().p == sel_b ().p && sel_b ().o - sel_a ().o == n)	// (the selection is an occurrence: replaced)
		{
			const Para *q = g_doc.p[sel_a ().p];
			int k = 0;
			while (k < n && (s_case ? q->ch[sel_a ().o + k] == pat[k] : fold (q->ch[sel_a ().o + k]) == fold (pat[k]))) k++;
			if (k == n) ed_type (rep, rn);
		}
		if (r == 4 && n)
		{
			int k = replace_all (pat, n, rep, rn, s_case);
			snprintf (s_msg, sizeof s_msg, TR (k == 1 ? "%d replacement." : "%d replacements."), k);
		}
		else if ((r == 2 || r == 3) && !find_again ()) scpy (s_msg, TR ("Not found."), sizeof s_msg);
		show_page ();
	}
}

// ---- Special Character -------------------------------------------------------------------------------------
struct Block { unsigned a, b; };
static const Block BLOCKS[] = { { 0xA1, 0xFF }, { 0x100, 0x17F }, { 0x384, 0x3CE }, { 0x400, 0x45F }, { 0x2010, 0x205E }, { 0x20A0, 0x20C0 },
				{ 0x2100, 0x218B }, { 0x2190, 0x21FF }, { 0x2200, 0x22FF }, { 0x2500, 0x25FF }, { 0x2600, 0x26FF }, { 0x2700, 0x27BF } };
static struct { long blk, grid, bar; int block, n, top, sel; unsigned cps[512]; unsigned last; long d; } SD;
enum { SY_COLS = 16, SY_CELL = 28, SY_ROWS = 8 };
static void sd_fill ()
{
	const Block &b = BLOCKS[SD.block];
	SD.n = 0; SD.top = 0; SD.sel = 0;
	for (unsigned c = b.a; c <= b.b && SD.n < 512; c++) if (c != 0xAD) SD.cps[SD.n++] = c;
	int rows = (SD.n + SY_COLS - 1) / SY_COLS;
	ui_w_set_range (SD.bar, 0, wmax (rows, (int) SY_ROWS), SY_ROWS, 1);
	ui_w_set_value (SD.bar, 0);
}
static void sd_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, 0xFFFFFF);
	long f = ui_font_get (UFN_SANS);
	for (int r = 0; r < SY_ROWS; r++)
		for (int c = 0; c < SY_COLS; c++)
		{
			int i = (SD.top + r) * SY_COLS + c, x = c * SY_CELL, y = r * SY_CELL;
			ui_fill (x, y, SY_CELL, 1, 0xDDDDDD); ui_fill (x, y, 1, SY_CELL, 0xDDDDDD);
			if (i >= SD.n) continue;
			if (i == SD.sel) ui_fill (x + 1, y + 1, SY_CELL - 1, SY_CELL - 1, ui_mix (0xFFFFFF, jet (J_ACCENT), 90));
			char u[8]; tf_utf8 (SD.cps[i], u);
			int a = (int) ui_text_width ((long) u, 17, f);
			ui_text ((long) u, x + (SY_CELL - a) / 2, y + 21, 0, 17, f, 0);
		}
}
static long sd_mouse (long, long kind, long mx, long my, long info)
{
	if (kind != 1) return 0;
	int i = (SD.top + (int) my / SY_CELL) * SY_COLS + (int) mx / SY_CELL;
	if (i < SD.n)
	{
		if (i == SD.sel && (info & 255) >= 2) { unsigned c = SD.cps[i]; ed_type (&c, 1); }
		SD.sel = i;
		ui_view_dirty (SD.grid);
	}
	return 0;
}
static long sd_wheel (long, long n)
{
	int rows = (SD.n + SY_COLS - 1) / SY_COLS;
	SD.top = wclamp (SD.top - (int) n, 0, wmax (0, rows - (int) SY_ROWS));
	ui_w_set_value (SD.bar, SD.top); ui_view_dirty (SD.grid);
	return 0;
}
static long sd_fn (long, long w, long code)
{
	if (w == SD.blk && code == UN_CHANGE) { SD.block = (int) wval (SD.blk); sd_fill (); ui_view_dirty (SD.grid); }
	if (w == SD.bar && code == UN_CHANGE) { SD.top = (int) wval (SD.bar); ui_view_dirty (SD.grid); }
	ui_view_flush ();
	return 0;
}
static void dlg_symbol ()
{
	enum { W = SY_COLS * SY_CELL + 50, H = SY_ROWS * SY_CELL + 100 };
	long d = ui_dialog_begin (W, H, (long) "Special Character");
	d_label (16, 10, 90, "Subset:");
	SD.blk = d_drop (110, 10, 260, "Latin-1 Supplement|Latin Extended-A|Greek|Cyrillic|Punctuation|Currency|Letterlike, Numbers|Arrows|Mathematical|Box Drawing, Shapes|Symbols|Dingbats", SD.block);
	int y = 16 + ctl_h ();
	SD.grid = ui_view (16, y, SY_COLS * SY_CELL + 1, SY_ROWS * SY_CELL + 1, 0);
	ui_view_on (SD.grid, (long) sd_paint, (long) sd_mouse, 0, (long) sd_wheel);
	SD.bar = ui_scrollbar (18 + SY_COLS * SY_CELL, y, 14, SY_ROWS * SY_CELL, 0);
	sd_fill ();
	long x = ui_dialog_button (d, 0, (long) "Insert the Character", 1, UB_DEFAULT, W - 12);
	ui_dialog_button (d, 1, (long) "Close", 0, 0, x);
	ui_dialog_cancel (d, 0);
	long r = ui_dialog_run (d, (long) sd_fn, 0);
	ui_dialog_end (d);
	if (r == 1 && SD.sel < SD.n) { unsigned c = SD.cps[SD.sel]; ed_type (&c, 1); }
}

// ---- Date and Time ---------------------------------------------------------------------------------------
static const char *const DATE_PICS[7] = { "dd/MM/yyyy", "dddd d MMMM yyyy", "d MMMM yyyy", "MMMM d, yyyy", "yyyy-MM-dd", "HH:mm", "dd/MM/yyyy HH:mm" };
static void now_datetime (int *y, int *mo, int *d, int *h, int *mi, int *s)
{
	time_t t = time (0); struct tm *m = localtime (&t);
	if (m) { *y = m->tm_year + 1900; *mo = m->tm_mon + 1; *d = m->tm_mday; *h = m->tm_hour; *mi = m->tm_min; *s = m->tm_sec; }
	else { *y = 2026; *mo = 1; *d = 1; *h = 0; *mi = 0; *s = 0; }
}
static long dt_fn (long d, long w, long code) { (void) w; if (code == UN_ACTIVATE) ui_dialog_close (d, 1); return 0; }
static void dlg_datetime ()
{
	enum { W = 380, H = 300 };
	char items[7][64]; int y, mo, dd, h, mi, se;
	now_datetime (&y, &mo, &dd, &h, &mi, &se);
	long d = ui_dialog_begin (W, H, (long) "Date and Time");
	long list = ui_list (16, 12, W - 32, H - 110, 0);
	for (int i = 0; i < 7; i++)
	{
		unsigned u[64]; int n = date_text (DATE_PICS[i], y, mo, dd, h, mi, se, u, 63);
		encode_text (u, n, items[i], 64);
		ui_list_add (list, (long) items[i], 0);
	}
	ui_w_set_value (list, 0);
	long cb = d_check (16, H - 92, 320, "Update automatically (a field)", false);
	d_ok_cancel (d, W);
	long r = ui_dialog_run (d, (long) dt_fn, 0);
	int i = (int) wval (list); bool fld = wval (cb) != 0;
	ui_dialog_end (d);
	if (r != 1 || i < 0 || i > 6) return;
	if (fld) { ed_insert_field (i == 5 ? FK_TIME : FK_DATE, DATE_PICS[i]); return; }
	unsigned u[64]; int n = decode_text (items[i], slen (items[i]), u, 64);
	ed_type (u, n);
}

// ---- Field ------------------------------------------------------------------------------------------------
static const char *const TIME_PICS[3] = { "HH:mm", "HH:mm:ss", "h:mm AM/PM" };
static void dlg_field ()
{
	enum { W = 360, H = 170 };
	long d = ui_dialog_begin (W, H, (long) "Field");
	d_label (16, 12, 80, "Field:");
	long k = d_drop (100, 12, 244, "Page Number|Number of Pages|Date|Time", 0);
	d_label (16, 16 + ctl_h (), 80, "Format:");
	char items[200]; items[0] = 0;
	int y, mo, dd, h, mi, se; now_datetime (&y, &mo, &dd, &h, &mi, &se);
	for (int i = 0; i < 5; i++) { unsigned u[64]; char b[64]; int n = date_text (DATE_PICS[i], y, mo, dd, h, mi, se, u, 63); encode_text (u, n, b, 64); if (i) strcat (items, "|"); strncat (items, b, 39); }
	long p = d_drop (100, 16 + ctl_h (), 244, items, 0);
	long t = d_drop (100, 20 + 2 * ctl_h (), 244, "HH:mm|HH:mm:ss|h:mm AM/PM", 0);
	d_label (16, 20 + 2 * ctl_h (), 80, "Time:");
	d_ok_cancel (d, W);
	long r = ui_dialog_run (d, 0, 0);
	long kk = wval (k), pi = wval (p), ti = wval (t);
	ui_dialog_end (d);
	if (r != 1) return;
	if (kk == 0) ed_insert_field (FK_PAGE, "");
	else if (kk == 1) ed_insert_field (FK_PAGES, "");
	else if (kk == 2) ed_insert_field (FK_DATE, DATE_PICS[wclamp ((int) pi, 0, 4)]);
	else ed_insert_field (FK_TIME, TIME_PICS[wclamp ((int) ti, 0, 2)]);
}

// ---- Page Numbers --------------------------------------------------------------------------------------------
static bool dlg_page_numbers ()
{
	enum { W = 400, H = 210 };
	long d = ui_dialog_begin (W, H, (long) "Page Numbers");
	int c = ctl_h () + 4;
	d_label (16, 12, 100, "Position:"); long pos = d_drop (120, 12, 264, "Bottom of page (footer)|Top of page (header)", 0);
	d_label (16, 12 + c, 100, "Alignment:"); long al = d_drop (120, 12 + c, 264, "Left|Centre|Right", 1);
	d_label (16, 12 + 2 * c, 100, "Format:"); long fm = d_drop (120, 12 + 2 * c, 264, "1|Page 1|Page 1 of 3|1 / 3", 1);
	long first = d_check (16, 12 + 3 * c, 340, "Show the number on the first page", !g_doc.page.titlePg);
	d_ok_cancel (d, W);
	long r = ui_dialog_run (d, 0, 0);
	long p = wval (pos), a = wval (al), f = wval (fm), ff = wval (first);
	ui_dialog_end (d);
	if (r != 1) return false;
	static const int AL[3] = { AL_LEFT, AL_CENTER, AL_RIGHT };
	ed_page_numbers (p == 0, AL[a % 3], (int) f, ff != 0);
	return true;
}

// ---- Tables -----------------------------------------------------------------------------------------------------
static void table_heading (bool on)				// (the table at the caret: its heading row)
{
	int ta, tb; Table *t = table_at (sel_a ().p, &ta, &tb);
	if (!t || t->header == on) return;
	Table *nt = table_copy (t); nt->header = on;
	ed_table_set (sel_a ().p, nt);
}
static bool dlg_insert_table ()
{
	enum { W = 400, H = 180 };
	long d = ui_dialog_begin (W, H, (long) "Insert Table");
	int c = ctl_h () + 4;
	d_label (16, 12, 150, "Columns:"); long tC = d_field (170, 12, 70, "3");
	d_label (16, 12 + c, 150, "Rows:"); long tR = d_field (170, 12 + c, 70, "2");
	long head = d_check (16, 12 + 2 * c, 360, "Repeat the heading row on each page", false);
	d_ok_cancel (d, W);
	long r = ui_dialog_run (d, 0, 0), rows = 0, cols = 0;
	bool ok = r == 1 && parse_num (wtext (tR), &rows) && parse_num (wtext (tC), &cols), hd = wval (head) != 0;
	ui_dialog_end (d);
	if (!ok) return false;
	ed_insert_table ((int) wclamp (rows / 100, 1L, 500L), (int) wclamp (cols / 100, 1L, (long) MAXCOLS));
	if (hd) table_heading (true);
	return true;
}
static const unsigned char BW_VALS[6] = { 4, 6, 8, 12, 18, 24 };
static bool dlg_table_props ()
{
	int r0, c0, r1, c1, ta, tb;
	Table *src = table_at (sel_a ().p, &ta, &tb);
	if (!src || !sel_cells (&r0, &c0, &r1, &c1)) return false;
	Table *t = table_copy (src);
	enum { W = 470, H = 330 };
	long d = ui_dialog_begin (W, H, (long) "Table Properties");
	int c = ctl_h () + 4, y = 12;
	char b[16];
	d_label (16, y, 130, "Lines:"); long bd = d_drop (150, y, 160, "All lines|None|Outline only|Rows only", t->border & 3); y += c;
	int bsel = 0; for (int i = 0; i < 6; i++) if (BW_VALS[i] <= t->bw) bsel = i;
	d_label (16, y, 130, "Width:"); long bw = d_drop (150, y, 160, "1/2 pt|3/4 pt|1 pt|1 1/2 pt|2 1/4 pt|3 pt", bsel);
	d_label (320, y, 60, "Colour:"); long bc = ui_colorpicker (384, y, 40, ctl_h () - 2, t->bcolor == AUTO ? 0 : t->bcolor); y += c;
	d_label (16, y, 130, "On the page:"); long al = d_drop (150, y, 160, "Left|Centre|Right", t->align == AL_CENTER ? 1 : t->align == AL_RIGHT ? 2 : 0);
	d_label (320, y, 60, "Indent:"); fmt_len (b, t->indent); long tI = d_field (384, y, 70, b); y += c;
	long hd = d_check (16, y, 360, "Repeat the first row on each page", t->header); y += c + 4;
	d_label (16, y, 200, "The selected cells:", ULB_BOLD); y += c;
	int w = 0; for (int k = c0; k <= c1; k++) w += t->colW[k];
	d_label (16, y, 130, "Column width:"); fmt_len (b, w / (c1 - c0 + 1)); long tW = d_field (150, y, 80, b); y += c;
	d_label (16, y, 130, "Row height:"); fmt_len (b, t->rowH[r0]); long tH = d_field (150, y, 80, b);
	d_label (250, y, 60, "Shading:"); unsigned f0 = tcell (t, r0, c0).fill;
	long fl = ui_colorpicker (316, y, 40, ctl_h () - 2, f0 == AUTO ? 0xFFFFFF : f0);
	long fn = d_check (362, y, 90, "None", f0 == AUTO);
	d_ok_cancel (d, W);
	long r = ui_dialog_run (d, 0, 0);
	if (r == 1)
	{
		t->border = (unsigned char) wval (bd); t->bw = BW_VALS[wval (bw) % 6]; t->bcolor = (unsigned) wval (bc) & 0xFFFFFF;
		long a = wval (al);
		t->align = (unsigned char) (a == 1 ? AL_CENTER : a == 2 ? AL_RIGHT : AL_LEFT);
		int v;
		if (parse_len (wtext (tI), &v)) t->indent = wclamp (v, -5000, 20000);
		t->header = wval (hd) != 0;
		int w0 = 0; for (int k = c0; k <= c1; k++) w0 += t->colW[k];
		if (parse_len (wtext (tW), &v) && v >= 120 && v / 10 != w0 / (c1 - c0 + 1) / 10) for (int k = c0; k <= c1; k++) t->colW[k] = wmin (v, 30000);
		if (parse_len (wtext (tH), &v)) for (int k = r0; k <= r1; k++) t->rowH[k] = (short) wclamp (v, 0, 20000);
		unsigned fill = wval (fn) ? AUTO : (unsigned) wval (fl) & 0xFFFFFF;
		for (int k = r0; k <= r1; k++) for (int j = c0; j <= c1; j++) tcell (t, k, j).fill = fill;
	}
	ui_dialog_end (d);
	if (r != 1) { table_free (t); return false; }
	ed_table_set (sel_a ().p, t);
	return true;
}

} // namespace wr

#endif
