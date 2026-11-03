//
// bars.h -- around the grid: the two toolbars (ui_toolbar; a button that does not fit the window hides, the
// menus have it all), the formula bar (the Name Box: the cell or range chosen, a reference typed there goes
// to it; the function wizard and AutoSum; while a cell is edited Cancel / Accept; the input line, a
// ui_textbox that holds the cell's content, edited there as in the cell), the sheets' tabs (ui_tabstrip: a
// click shows one, "+" adds one, the right button its menu), the status bar (a small view: the sheet, the
// mode, the sum / average / count of the selection, the zoom).
//
#ifndef _sheet_bars_h
#define _sheet_bars_h

#include "grid.h"

namespace ss {

// The commands: the menus' ids, the buttons'.
enum {	M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_EXPCSV, M_EXPPDF, M_PRINT,
	M_UNDO = 10, M_REDO, M_CUT, M_COPY, M_PASTE, M_PASTESP, M_CLEAR, M_FILLD, M_FILLR, M_SELALL, M_FIND, M_GOTO,
	M_ZOOMIN = 30, M_ZOOMOUT, M_ZOOM100, M_FREEZE, M_GRIDL, M_FORMULAS,
	M_INSROW = 40, M_INSCOL, M_FUNC, M_NAMES, M_SUM, M_CHART, M_INSSHEET, M_DATE, M_TIME,
	M_FORMAT = 50, M_COND, M_BOLD, M_ITALIC, M_UNDER, M_STRIKE, M_MERGE, M_WRAP, M_ROWH, M_COLW, M_OPTW, M_HIDER, M_HIDEC, M_SHOW, M_CLRFMT,
	M_NOFILL, M_AUTOCOL, M_LEFT, M_CENTER, M_RIGHT, M_VTOP, M_VMID, M_VBOT, M_CURRENCY, M_PERCENT, M_THOUSANDS, M_DECP, M_DECM, M_BORDERS,
	M_DELROW = 80, M_DELCOL, M_DELSHEET, M_RENSHEET, M_DUPSHEET, M_SHLEFT, M_SHRIGHT,
	M_SORTA = 90, M_SORTD, M_SORT, M_FILTER, M_RECALC,
	M_ACCEPT = 100, M_CANCEL, M_COUNT };

static const char MENUS[] =
	"MFile\n"
	"I1\tNew\tCtrl+N\n" "I2\tOpen...\tCtrl+O\n" "I3\tSave\tCtrl+S\n" "I4\tSave As...\n" "I5\tExport as CSV...\n" "I6\tExport as PDF...\n" "-\n" "I7\tPrint...\tCtrl+P\n"
	"MEdit\n"
	"I10\tUndo\tCtrl+Z\n" "I11\tRedo\tCtrl+Y\n" "-\n" "I12\tCut\tCtrl+X\n" "I13\tCopy\tCtrl+C\n" "I14\tPaste\tCtrl+V\n" "I15\tPaste Special...\n" "I16\tDelete Contents\tDel\n" "-\n"
	"I17\tFill Down\tCtrl+D\n" "I18\tFill Right\tCtrl+R\n" "I19\tSelect All\tCtrl+A\n" "I20\tFind and Replace...\tCtrl+F\n" "I21\tGo To...\tCtrl+G\n"
	"MView\n"
	"I30\tZoom In\n" "I31\tZoom Out\n" "I32\tActual Size (100%)\n" "-\n" "I33\tFreeze Panes\n" "I34\tGridlines\n" "I35\tFormulas\n"
	"MInsert\n"
	"I40\tRows Above\n" "I41\tColumns Before\n" "-\n" "I42\tFunction...\n" "I43\tNames...\n" "I44\tAutoSum\n" "I45\tChart...\n" "I46\tSheet\n" "-\n" "I47\tCurrent Date\n" "I48\tCurrent Time\n"
	"MFormat\n"
	"I50\tCells...\n" "I51\tConditional Formatting...\n" "-\n" "I52\tBold\tCtrl+B\n" "I53\tItalic\tCtrl+I\n" "I54\tUnderline\tCtrl+U\n" "I56\tMerge and Centre\n" "I57\tWrap Text\n" "-\n"
	"I58\tRow Height...\n" "I59\tColumn Width...\n" "I60\tOptimal Column Width\n" "I61\tHide Rows\n" "I62\tHide Columns\n" "I63\tShow Rows and Columns\n" "-\n"
	"I64\tClear Formatting\n" "I65\tNo Fill\n" "I66\tAutomatic Text Colour\n"
	"MSheet\n"
	"I80\tDelete Rows\n" "I81\tDelete Columns\n" "-\n" "I46\tInsert Sheet\n" "I82\tDelete Sheet\n" "I83\tRename Sheet...\n" "I84\tDuplicate Sheet\n" "I85\tMove Sheet Left\n" "I86\tMove Sheet Right\n"
	"MData\n"
	"I90\tSort Ascending\n" "I91\tSort Descending\n" "I92\tSort...\n" "I93\tAutoFilter\n" "I94\tRecalculate\tF9\n";

struct Tool { int id; const char *glyph; const char *tip; int fl; };	// (fl: UTB_*; id 0: only a space)
static const Tool TOOLS1[] = {
	{ M_NEW, "\xE2\x98\x90", "New workbook (Ctrl+N)", 0 }, { M_OPEN, "\xE2\x87\xA7", "Open... (Ctrl+O)", 0 }, { M_SAVE, "\xE2\x87\xA9", "Save (Ctrl+S)", 0 },
	{ M_UNDO, "\xE2\x86\xB6", "Undo (Ctrl+Z)", UTB_SEP }, { M_REDO, "\xE2\x86\xB7", "Redo (Ctrl+Y)", 0 },
	{ M_CUT, "\xE2\x9C\x82", "Cut (Ctrl+X)", UTB_SEP }, { M_COPY, "\xE2\x9D\x90", "Copy (Ctrl+C)", 0 }, { M_PASTE, "\xE2\x96\xA3", "Paste (Ctrl+V)", 0 },
	{ M_FIND, "⊙", "Find and Replace (Ctrl+F)", UTB_SEP }, { M_SORTA, "\xE2\x86\x93", "Sort ascending (by the cursor's column)", 0 }, { M_SORTD, "\xE2\x86\x91", "Sort descending", 0 },
	{ M_FILTER, "\xE2\x96\xBD", "AutoFilter: buttons on the table's headers to show some rows only", UTB_TOGGLE },
	{ M_INSROW, "\xE2\x8A\x9E", "Insert rows above", UTB_SEP }, { M_INSCOL, "\xE2\x8A\x9F", "Insert columns before", 0 }, { M_DELROW, "\xE2\x8A\xA0", "Delete rows", 0 }, { M_DELCOL, "\xE2\x8A\xA1", "Delete columns", 0 },
	{ M_CHART, "\xE2\x97\x95", "Insert a chart of the selection", UTB_SEP }, { M_FREEZE, "\xE2\x95\xAC", "Freeze the rows above and the columns left of the cursor", UTB_TOGGLE },
	{ M_ZOOMOUT, "\xE2\x88\x92", "Zoom out", UTB_SEP } };
static const Tool TOOLS2[] = {
	{ M_BOLD, "B", "Bold (Ctrl+B)", UTB_TOGGLE }, { M_ITALIC, "I", "Italic (Ctrl+I)", UTB_TOGGLE }, { M_UNDER, "U", "Underline (Ctrl+U)", UTB_TOGGLE }, { M_STRIKE, "S", "Strikethrough", UTB_TOGGLE },
	{ M_LEFT, "\xE2\x87\xA4", "Align left", UTB_SEP | UTB_TOGGLE }, { M_CENTER, "\xE2\x86\x94", "Centre", UTB_TOGGLE }, { M_RIGHT, "\xE2\x87\xA5", "Align right", UTB_TOGGLE },
	{ M_WRAP, "\xE2\x86\xA9", "Wrap text", UTB_SEP | UTB_TOGGLE }, { M_MERGE, "\xE2\x87\x94", "Merge and centre", UTB_TOGGLE },
	{ M_CURRENCY, "\xE2\x82\xAC", "Currency format", UTB_SEP }, { M_PERCENT, "%", "Percent format", 0 }, { M_THOUSANDS, ",", "Thousands' separator, two decimals", 0 },
	{ M_DECP, ".0", "Add a decimal", 0 }, { M_DECM, ".\xE2\x86\x90", "Take a decimal off", 0 },
	{ M_BORDERS, "\xE2\x96\xA6", "Borders", UTB_SEP }, { M_CLRFMT, "\xE2\x9C\x97", "Clear the formatting", 0 } };
static const Tool TOOLSF[] = {
	{ M_FUNC, "\xC6\x92", "Insert Function...", 0 }, { M_SUM, "\xCE\xA3", "AutoSum: the sum of the numbers above or at the left", 0 },
	{ M_CANCEL, "\xE2\x9C\x95", "Cancel (Esc)", 0 }, { M_ACCEPT, "\xE2\x9C\x93", "Accept (Enter)", 0 } };

enum { NT1 = sizeof TOOLS1 / sizeof TOOLS1[0], NT2 = sizeof TOOLS2 / sizeof TOOLS2[0] };
enum { TABS_H = 28, STATUS_H = 22 };
static long g_btn[M_COUNT];
static long g_tb1, g_tb2, g_tb2b, g_fbar, g_fbarb, g_font, g_size, g_zoom, g_cText, g_cFill, g_name, g_fline, g_tabs, g_status, g_zoomIn;
static int  g_tb1x[NT1 + 2], g_tb2x[NT2 + 2];			// (where each button ends: those past the window hide)
static int  g_bar2x;						// (where the second row's tools start)
static const int ZOOMS[] = { 25, 50, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400 };
enum { NZOOMS = 13 };
static const char *const SIZE_NAMES[] = { "6", "7", "8", "9", "10", "10.5", "11", "12", "14", "16", "18", "20", "22", "24", "26", "28", "32", "36", "40", "48", "54", "60", "72" };
static const int SIZE_VALS[] = { 60, 70, 80, 90, 100, 105, 110, 120, 140, 160, 180, 200, 220, 240, 260, 280, 320, 360, 400, 480, 540, 600, 720 };
// The fonts offered: names that map on the card's three families (the file keeps the name).
static const char *const FONT_NAMES[] = { "Calibri", "Arial", "Verdana", "Times New Roman", "Georgia", "Cambria", "Courier New", "Consolas" };
enum { NFONTNAMES = 8 };

// A list of names into a drop-down (tr: the app's words; not the fonts, nor the sheet's own texts).
static void dd_fill (long dd, const char *const *names, int n, bool tr = false)
{
	ui_list_clear (dd);
	for (int i = 0; i < n; i++) ui_list_add (dd, (long) (tr ? TR (names[i]) : names[i]), 0);
}
static long dd_make (int x, int y, int w, int h, const char *const *names, int n, int sel, bool tr = false)
{
	long dd = ui_dropdown (x, y, w, h, (long) "");
	dd_fill (dd, names, n, tr);
	ui_w_set_value (dd, sel);
	return dd;
}

static int tool_row (long bar, const Tool *t, int n, int *ends, int x0)
{
	int x = x0, step = ctl_h () + 2;
	for (int i = 0; i < n; i++)
	{
		if (t[i].fl & UTB_SEP) x += 12;
		long b = ui_toolbar_add (bar, (long) t[i].glyph, 0, t[i].fl);
		ui_w_tip (b, (long) t[i].tip);
		g_btn[t[i].id] = b;
		x += step;
		if (ends) ends[i] = x;
	}
	return x;
}
static void bars_create (int w)
{
	int H = ctl_h () + 6, c = ctl_h ();
	g_tb1 = ui_toolbar (0, 0, w, H);
	int x = tool_row (g_tb1, TOOLS1, NT1, g_tb1x, 8);
	char zn[NZOOMS][8]; const char *zp[NZOOMS];
	for (int i = 0; i < NZOOMS; i++) { snprintf (zn[i], sizeof zn[i], "%d%%", ZOOMS[i]); zp[i] = zn[i]; }
	g_zoom = dd_make (x, 3, 86, c, zp, NZOOMS, 4);
	ui_w_set_parent (g_zoom, g_tb1); ui_w_tip (g_zoom, (long) "Zoom");
	x += 88;
	{	// the "+" of the zoom after its box: a bar of its own
		long b2 = ui_toolbar (x, 0, 42, H); ui_w_set_parent (b2, g_tb1);
		Tool t = { M_ZOOMIN, "+", "Zoom in", 0 };
		tool_row (b2, &t, 1, 0, 8);
		g_zoomIn = b2;
	}
	g_tb2 = ui_toolbar (0, H, w, H);
	g_font = dd_make (8, H + 3, 128, c, FONT_NAMES, NFONTNAMES, 0);
	g_size = dd_make (140, H + 3, 62, c, SIZE_NAMES, 23, 6);
	g_cText = ui_colorpicker (206, H + 3, 34, c, 0xC00000);
	g_cFill = ui_colorpicker (242, H + 3, 34, c, 0xFFF2CC);
	ui_w_set_parent (g_font, g_tb2); ui_w_set_parent (g_size, g_tb2); ui_w_set_parent (g_cText, g_tb2); ui_w_set_parent (g_cFill, g_tb2);
	ui_w_tip (g_font, (long) "Font"); ui_w_tip (g_size, (long) "Font size"); ui_w_tip (g_cText, (long) "Font colour"); ui_w_tip (g_cFill, (long) "Fill colour");
	g_bar2x = 278;
	g_tb2b = ui_toolbar (g_bar2x, H, w - g_bar2x, H);
	ui_w_set_parent (g_tb2b, g_tb2);
	tool_row (g_tb2b, TOOLS2, NT2, g_tb2x, g_bar2x + 8);

	g_fbar = ui_toolbar (0, 2 * H, w, H);
	g_name = ui_textbox (6, 2 * H + 3, 100, c, 0);
	ui_w_set_parent (g_name, g_fbar);
	ui_w_tip (g_name, (long) "Name Box: the cell chosen; type a reference (B7, C2:F9, Sheet2!A1) and press Enter to go there");
	int fw = 4 * (c + 2) + 16;
	g_fbarb = ui_toolbar (108, 2 * H, fw, H);
	ui_w_set_parent (g_fbarb, g_fbar);
	tool_row (g_fbarb, TOOLSF, 4, 0, 8);
	g_fline = ui_textbox (108 + fw, 2 * H + 3, w - 114 - fw, c, 0);
	ui_w_set_parent (g_fline, g_fbar);
	ui_w_set_range (g_fline, 0, 32000, 0, 0);
}

static void bars_layout (int w, int h)
{
	int H = ctl_h () + 6, fw = 4 * (ctl_h () + 2) + 16;
	ui_w_move (g_tb1, 0, 0, w, H);
	ui_w_move (g_tb2, 0, H, w, H);
	ui_w_move (g_tb2b, g_bar2x, H, w - g_bar2x, H);
	ui_w_move (g_fbar, 0, 2 * H, w, H);
	ui_w_move (g_fline, 108 + fw, 2 * H + 3, w - 114 - fw, ctl_h ());
	int wt = w - 115;	// (the three window buttons keep the top right corner: FRAME_CAP_W)
	for (int i = 0; i < NT1; i++) ui_w_show (g_btn[TOOLS1[i].id], g_tb1x[i] <= wt);
	int zr = g_tb1x[NT1 - 1] + 88 + 42;
	ui_w_show (g_zoom, zr <= wt); ui_w_show (g_zoomIn, zr <= wt);
	for (int i = 0; i < NT2; i++) ui_w_show (g_btn[TOOLS2[i].id], g_tb2x[i] <= w);
	ui_w_move (g_tabs, 0, h - TABS_H - STATUS_H, w, TABS_H);
	ui_w_move (g_status, 0, h - STATUS_H, w, STATUS_H);
}

// ---- the formula bar's text --------------------------------------------------------------------------------
static char g_flShown[4096 * 2]; static bool g_flValid;
static bool fline_focused () { return ui_w_get_focus () == g_fline; }
// What the input line must show: the text being typed, else the cell's. (Not while it is the one typed in.)
static void fline_sync ()
{
	if (fline_focused ()) return;
	Sheet *s = S ();
	Buf t;
	if (g_ed.on) t.puts (g_ed.t.str ()); else cell_edit_text (g_b, s, s->cells.get (s->curR, s->curC), t);
	for (char *p = (char *) t.str (); *p; p++) if (*p == '\n') *p = ' ';
	if (g_flValid && !strncmp (t.str (), g_flShown, sizeof g_flShown - 1)) return;
	scpy (g_flShown, t.str (), sizeof g_flShown); g_flValid = true;
	ui_w_set_text (g_fline, (long) t.str ());
}
// The Name Box: the cell or range (a defined name for exactly this range: its name, as Excel does).
static void name_sync ()
{
	if (ui_w_get_focus () == g_name) return;
	Sheet *s = S ();
	Rect r = gv_sel ();
	char t[80];
	bool wc = g_wholeCols, wr = g_wholeRows;
	int mi = merge_at (s, r.r0, r.c0);
	if (g_ed.on) cell_name (g_ed.r, g_ed.c, t);
	else if (r.r0 == r.r1 && r.c0 == r.c1) cell_name (s->curR, s->curC, t);
	else if (mi >= 0 && s->merges[mi].r1 == r.r1 && s->merges[mi].c1 == r.c1) cell_name (r.r0, r.c0, t);
	else if (wc && !wr) { char a[8], c2[8]; col_name (r.c0, a); col_name (r.c1, c2); snprintf (t, sizeof t, "%s:%s", a, c2); }
	else if (wr && !wc) snprintf (t, sizeof t, "%d:%d", r.r0 + 1, r.r1 + 1);
	else if (wr && wc) scpy (t, "A1:XFD1048576", sizeof t);
	else { char a[20], c2[20]; cell_name (r.r0, r.c0, a); cell_name (r.r1, r.c1, c2); snprintf (t, sizeof t, "%s:%s", a, c2); }
	if (!g_ed.on)
		for (int i = 0; i < g_b.nnames; i++)
		{
			const DefName &d = g_b.names[i];
			if (!d.f || d.f->nd[d.f->root].k != N_TOK) continue;
			const Tok &k = d.f->tok[d.f->nd[d.f->root].tok];
			if ((k.t != TK_REF && k.t != TK_AREA) || (k.fl & TF_BAD) || (k.sheet ? k.sheet : d.scope) != s->id) continue;
			if (k.r0 == r.r0 && k.c0 == r.c0 && k.r1 == r.r1 && k.c1 == r.c1 && (!d.scope || d.scope == s->id)) { scpy (t, d.name, sizeof t); break; }
		}
	static char was[80];
	if (strcmp (t, was)) { scpy (was, t, sizeof was); ui_w_set_text (g_name, (long) t); }
}

// ---- the sheets' tabs ----------------------------------------------------------------------------------------
static void tabs_sync ()
{
	static char was[1024]; static int wasActive = -1;
	Buf k;
	for (int i = 0; i < g_b.ns; i++) { k.puts (g_b.sh[i]->name); k.put ('\n'); }
	if (strncmp (k.str (), was, sizeof was - 1))
	{
		scpy (was, k.str (), sizeof was);
		ui_list_clear (g_tabs);
		for (int i = 0; i < g_b.ns; i++) ui_tabstrip_add (g_tabs, (long) g_b.sh[i]->name, i);
		wasActive = -1;
	}
	if (wasActive != g_b.active) { wasActive = g_b.active; ui_w_set_value (g_tabs, g_b.active); ui_w_draw (g_tabs); }
}

// ---- the status bar ------------------------------------------------------------------------------------------
static char g_stL[64], g_stM[160]; static bool g_stForce;
static void (*g_stZoom) (int dir);				// -1, +1; 0: back to 100 %
static long st_paint (long v, long w, long h)
{
	cv_begin (v);
	int W = (int) w, Hh = (int) h, px = ui_px ();
	unsigned face = jet (J_FACE), ink = jet (J_TEXTE2), line = jet (J_LIGNE);
	cv_fill (0, 0, W, Hh, face);
	cv_fill (0, 0, W, 1, line);
	Font f = ui_font (px);
	int base = Hh / 2 + px * 37 / 100;
	text_at (f, 10, base, g_stL, ink, 0, Rect { 0, 0, Hh - 1, W / 3 });
	text_at (f, W - 110, base, g_stM, ink, 2, Rect { 0, W / 3, Hh - 1, W - 104 });
	char z[16]; snprintf (z, sizeof z, "%d%%", g_z);
	cv_fill (W - 102, 4, 1, Hh - 8, line);
	text_at (f, W - 88, base, "\xE2\x88\x92", ink, 1, Rect { 0, W - 100, Hh - 1, W - 76 });
	text_at (f, W - 50, base, z, ink, 1, Rect { 0, W - 76, Hh - 1, W - 24 });
	text_at (f, W - 13, base, "+", ink, 1, Rect { 0, W - 24, Hh - 1, W - 1 });
	cv_end ();
	return 0;
}
static long st_mouse (long v, long kind, long x, long y, long info)
{
	(void) v; (void) y; (void) info;
	if (kind != 1 || !g_stZoom) return 0;
	int mx = (int) x, W = (int) ui.w;
	if (mx >= W - 98 && mx < W - 76) g_stZoom (-1);
	else if (mx >= W - 24) g_stZoom (1);
	else if (mx >= W - 76) g_stZoom (0);
	return 0;
}
static void status_set (const char *l, const char *m)
{
	if (!strcmp (l, g_stL) && !strcmp (m, g_stM) && !g_stForce) return;
	g_stForce = false;
	scpy (g_stL, l, sizeof g_stL); scpy (g_stM, m, sizeof g_stM);
	ui_view_dirty (g_status);
}
static void bars_create2 (int w, int h)
{
	g_tabs = ui_tabstrip (0, h - TABS_H - STATUS_H, w, TABS_H, UTS_PLUS);
	g_status = ui_view (0, h - STATUS_H, w, STATUS_H, 0);
	ui_view_on (g_status, (long) st_paint, (long) st_mouse, 0, 0);
}

} // namespace ss

#endif
