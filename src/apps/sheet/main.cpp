//
// sheet -- Onyx's Spreadsheet on ARMPI (src/apps/sheet/README.md), in the way of LibreOffice Calc and
// Gnumeric: workbooks of sheets up to 16 384 columns x 1 048 576 rows; formulas as Excel writes them (237
// functions: mathematics, statistics, logic, text, lookups, dates, finance; references relative and
// absolute, to other sheets, ranges and whole columns; arrays), recomputed at each change; number formats
// (Excel's codes), fonts, colours, borders, alignment, wrapping, merged cells; rows and columns inserted,
// deleted, sized, hidden; frozen panes; copy / cut / paste (the references following), the fill handle's
// series, sort, find and replace; defined names; conditional formatting (rules, colour scales, data bars);
// the AutoFilter; charts (column, bar, line, area, pie, scatter); undo. Files: Excel's .xlsx (read and
// written), LibreOffice's .ods (read), CSV (read and written).
//
//   the engine: core.h book.h formula.h eval.h numfmt.h input.h fn_core.h fn_more.h funcs.h ops.h undo.h
//   the files:  xlsx.h ods.h (+ CSV), through DocumentKit and ArchiveKit
//   the window: condfmt.h render.h chart.h grid.h bars.h dialogs.h main.cpp, on UIKit
//
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "dialogs.h"
#include <printerkit.h>

namespace ss {

#define APP_W 1000						// (fits a 1024 x 768 screen)
#define APP_H 640
#define HOME_DIR "SD0:/home/sheet"
static const char *RECOVER = HOME_DIR "/recovered.xlsx";
static const char *RECOVER_NAME = HOME_DIR "/recovered.txt";
static const char *LAST = HOME_DIR "/last.txt";			// the workbook opened or saved last
static const char *DOCS = "SD0:/home/documents";

static char g_path[256];
static unsigned g_gen = 1, g_saved = 1;
static unsigned g_textColor = 0xC00000, g_fillColor = 0xFFF2CC;
static int g_borderPreset = 1;					// the Borders button's last one
static char g_note[256];					// a message for after the event (a key's, a click's)
static struct { int where, x, y; } g_pend = { -1, 0, 0 };	// a menu or dialog asked from inside an event
static int g_pendChart = -1, g_pendCol = -1, g_pendX, g_pendY;
static bool g_changed = true;					// the state shown must follow (refresh)
static long g_flBefore;						// the focus at the last turn of the loop

static bool changed_doc () { return g_gen != g_saved; }
static void touched () { g_gen++; g_changed = true; }
static void refresh () { g_changed = true; }
static void refresh_now ();
static void show_now () { refresh_now (); gv_bars (); ui_view_flush (); }

static void now_clock (int *y, int *mo, int *d, int *h, int *mi, int *s)
{
	time_t t = time (0);
	struct tm *m = localtime (&t);
	if (!m) { *y = 2026; *mo = 1; *d = 1; *h = *mi = *s = 0; return; }
	*y = m->tm_year + 1900; *mo = m->tm_mon + 1; *d = m->tm_mday; *h = m->tm_hour; *mi = m->tm_min; *s = m->tm_sec;
}
static const char *base_name (const char *p)
{
	const char *b = p;
	for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1;
	return b;
}
static void message (const char *text) { note ("Spreadsheet", text); }
static void title_sync ()
{
	char t[320];
	long scr[4]; ui_screen ((long) scr);			// (the menu bar shows it: at 800 px the menus need the room)
	snprintf (t, sizeof t, scr[0] < 900 ? "%s%s" : TR ("%s%s - Spreadsheet"), changed_doc () ? "*" : "", g_path[0] ? base_name (g_path) : TR ("Untitled"));
	static char was[320];
	if (strcmp (t, was)) { scpy (was, t, sizeof was); ui_title ((long) t); }
}

// ---- the book's state -------------------------------------------------------------------------------------
static void book_fresh ()
{
	book_clear (g_b); book_init (g_b);
	book_add_sheet (g_b, TR ("Sheet1"));
	fonts_reset (); undo_clear (); clip_free ();
	g_copyMarquee = false; g_selChart = -1; gv_reset_sel_kind ();
	g_path[0] = 0;
	g_gen = g_saved = 1;
	gv_dirty ();
}
static void remember ()
{
	if (g_path[0]) write_file (LAST, g_path, (int) strlen (g_path));
}
static void loaded ()
{
	fonts_reset (); undo_clear ();
	g_copyMarquee = false; g_selChart = -1; gv_reset_sel_kind ();
	for (int i = 0; i < g_b.ns; i++) { Sheet *s = g_b.sh[i]; s->topR = imax (s->topR, s->freezeR); s->leftC = imax (s->leftC, s->freezeC); }
	tabs_sync ();
	g_gen = g_saved = 1;
	gv_dirty ();
	refresh ();
}
static bool load_path (const char *p)
{
	const char *why = 0;
	if (!book_load (g_b, p, &why)) { book_fresh (); tabs_sync (); refresh_now (); ui_view_flush (); message (why ? why : "The file cannot be opened."); return false; }
	scpy (g_path, p, sizeof g_path);
	remember ();
	loaded ();
	// (.ods and CSV say no height: the rows follow their fonts)
	if (!ends_with (p, ".xlsx"))
		for (int i = 0; i < g_b.ns; i++) { Sheet *s = g_b.sh[i]; sheet_bounds (s); for (int r = 0; r <= imin (s->maxR, 1000); r++) autofit_row (g_b, s, r); }
	return true;
}
static bool is_csv (const char *p) { return ends_with (p, ".csv") || ends_with (p, ".txt") || ends_with (p, ".tsv"); }
static bool save_to (const char *p)
{
	if (is_csv (p))
	{
		if (note ("Save as CSV", "A CSV file keeps only the values of the sheet shown -- no formats, no formulas, no other sheet. Save it anyway?", UM_OKCANCEL) != 1) return false;
	}
	const char *why = 0;
	if (!book_save (g_b, p, &why)) { message (why ? why : "The file cannot be written."); return false; }
	return true;
}
static void cmd_save_as ();
static void cmd_save ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	if (!g_path[0] || ends_with (g_path, ".ods")) { cmd_save_as (); return; }
	if (save_to (g_path)) { g_saved = g_gen; refresh (); }
}
static void cmd_save_as ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	char out[256], def[80];
	scpy (def, g_path[0] ? base_name (g_path) : TR ("Book1.xlsx"), sizeof def);
	char *dot = strrchr (def, '.'); if (dot && ends_with (def, ".ods")) scpy (dot, ".xlsx", (int) (sizeof def - (dot - def)));
	ui_file_filter ((long) "*.xlsx;*.csv;*.tsv");
	long ok = ui_file_save ((long) DOCS, (long) def, (long) out, sizeof out);
	ui_file_filter (0);
	if (!ok) return;
	if (!strrchr (base_name (out), '.')) { int n = (int) strlen (out); scpy (out + n, ".xlsx", (int) sizeof out - n); }
	if (!save_to (out)) return;
	if (!is_csv (out)) { scpy (g_path, out, sizeof g_path); g_saved = g_gen; remember (); }
	refresh ();
}
// Unsaved changes: Save, Don't Save, Cancel. false: stay.
static bool doc_confirm ()
{
	if (!changed_doc ()) return true;
	char m[320]; snprintf (m, sizeof m, TR ("Save the changes to %s?"), g_path[0] ? base_name (g_path) : TR ("Untitled"));
	int r = note ("Unsaved changes", m, UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { cmd_save (); return !changed_doc (); }
	return true;
}
static void cmd_new ()
{
	if (g_ed.on) gv_cancel_edit ();
	if (!doc_confirm ()) return;
	book_fresh ();
	tabs_sync ();
	refresh ();
}
static void cmd_open ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	if (!doc_confirm ()) return;
	char p[256];
	ui_file_filter ((long) "*.xlsx;*.ods;*.csv;*.tsv;*.txt");
	long ok = ui_file_open ((long) DOCS, (long) p, sizeof p);
	ui_file_filter (0);
	if (ok) load_path (p);
}
static void cmd_export_csv ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	char out[256], def[80];
	scpy (def, g_path[0] ? base_name (g_path) : TR ("Book1"), sizeof def);
	char *dot = strrchr (def, '.'); if (dot) *dot = 0;
	int n = (int) strlen (def); scpy (def + n, ".csv", (int) sizeof def - n);
	ui_file_filter ((long) "*.csv");
	long ok = ui_file_save ((long) DOCS, (long) def, (long) out, sizeof out);
	ui_file_filter (0);
	if (!ok) return;
	int len; char *d = csv_write (g_b, S (), ',', &len);
	if (!write_file (out, d, len)) message ("The file cannot be written.");
	free (d);
}

// ---- after a change ----------------------------------------------------------------------------------------
static void cmd_cut (); static void cmd_copy (); static void cmd_paste (); static void cmd_paste_special ();
static void cmd_ins_rows (); static void cmd_ins_cols (); static void cmd_del_rows (); static void cmd_del_cols ();
static void cmd_clear_contents (); static void cmd_format_cells (); static void cmd_chart (); static void cmd_col_width ();
static void cmd_row_height (); static void cmd_opt_width (); static void cmd_hide_rows (); static void cmd_hide_cols (); static void cmd_show_all ();
static void cmd_sort_asc (); static void cmd_sort_desc ();
static void chart_open (int i);
static void pick_sheet (int i);
static void grid_changed () { refresh (); }
static void grid_edited () { refresh (); }
static void grid_chart_open (int i) { g_pendChart = i; }
static void grid_filter (int col, int x, int y) { g_pendCol = col; g_pendX = x; g_pendY = y; }
static void grid_zoom (int dir);
static void after_change (bool rows = false);
static Rect used_sel ();
static bool grid_commit (int r, int c, const char *text)
{
	Sheet *s = book_sheet_by_id (g_b, g_ed.sheetId);
	if (!s) return true;
	Rect one = { r, c, r, c };
	undo_rect (g_b, s, one);
	const char *why = 0; int where = 0;
	if (!cell_input (g_b, s, r, c, text, &why, &where))
	{
		undo_step (g_b, false); ustack_clear (g_redo);		// (nothing changed: the step taken back)
		snprintf (g_note, sizeof g_note, TR ("This formula cannot be read: %s"), TR (why ? why : "?"));	// (shown when the event is over)
		g_ed.caret = g_ed.anchor = iclamp (where, 0, g_ed.t.n);
		return false;
	}
	recalc (g_b);
	autofit_row (g_b, s, r);
	touched ();
	return true;
}
static void cmd_cond_format ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S (); Rect r = used_sel ();
	char a1[24], a2[24], t[64]; cell_name (r.r0, r.c0, a1); cell_name (r.r1, r.c1, a2);
	if (r.r0 == r.r1 && r.c0 == r.c1) scpy (t, a1, sizeof t); else snprintf (t, sizeof t, "%s:%s", a1, a2);
	undo_sheet (g_b, s);
	if (!cond_dialog (&g_b, s, t)) undo_drop_last (); else touched ();
	cf_changed ();
	gv_dirty (); gv_focus ();
	refresh ();
}
static void cmd_export_pdf () { message ("To make a PDF: File > Print, and choose the PDF printer."); }

// File > Print: the Print dialog (PrinterKit's: the printer, its paper, the pages...), then the sheet cut into pages as in
// Onyx. Each page is drawn at twice its size in a hidden canvas and given to the job as a picture (Onyx drew the cells as
// text and shapes: here the cells draw with ui_text, in pixels). The footer is text.
static void cmd_print ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	if (!printerkit_kit_open ()) { message ("The print service is not installed."); return; }
	char name[256]; snprintf (name, sizeof name, "%s", g_path[0] ? base_name (g_path) : TR ("Book1"));
	Sheet *s = S (); sheet_bounds (s);
	int maxR = s->maxR, maxC = s->maxC;
	for (int i = 0; i < s->nmerge; i++) { if (s->merges[i].r1 > maxR) maxR = s->merges[i].r1; if (s->merges[i].c1 > maxC) maxC = s->merges[i].c1; }
	for (int i = 0; i < s->ncharts; i++)		// (the charts: in the pages too)
	{
		Chart *c = s->charts[i];
		if (maxC < 0) maxC = 0;
		if (maxR < 0) maxR = 0;
		while (maxC < MAXC - 1 && col_x (s, maxC + 1) < c->x + c->w) maxC++;
		while (maxR < 100000 && row_y (s, maxR + 1) < c->y + c->h) maxR++;
	}
	if (maxR < 0 || maxC < 0) { message ("There is nothing in the sheet to print."); return; }
	long long uw = col_x (s, maxC + 1), uh = row_y (s, maxR + 1);
	PrintSetup ps; print_setup_default ((long) &ps);
	if (uw > uh && uw > 700) print_setup_paper ((long) &ps, 0, PRINT_LANDSCAPE);	// (a wide sheet: the paper lying, first)
	PrintDialogInfo di = { sizeof di, name, 0, 0, 0, 0, 0 };
	if (!print_dialog ((long) &ps, (long) &di)) { gv_focus (); return; }
	long j = print_begin ((long) &ps, (long) name);
	if (!j) { message ("The print queue cannot be written."); return; }
	// the margin: half an inch, and clear of what the printer cannot print on (the footer is under it)
	float PW = ps.paper_w / 1000.0f, PH = ps.paper_h / 1000.0f, FOOT = 18, scale = 0.75f;
	float M = ps.margin_l; if (ps.margin_t > M) M = ps.margin_t; if (ps.margin_r > M) M = ps.margin_r; if (ps.margin_b > M) M = ps.margin_b;
	M /= 1000.0f; M = M + 12 > 36 ? M + 12 : 36;
	int bandW = (int) ((PW - 2 * M) / scale), bandH = (int) ((PH - 2 * M - FOOT) / scale);
	static int cb[256], rb[2048]; int ncb = 0, nrb = 0;
	for (int c = 0; c <= maxC && ncb < 255; )
	{
		cb[ncb++] = c; int w = 0;
		while (c <= maxC && (w + col_w (s, c) <= bandW || w == 0)) { w += col_w (s, c); c++; }
	}
	cb[ncb] = maxC + 1;
	for (int r = 0; r <= maxR && nrb < 2047; )
	{
		rb[nrb++] = r; int h = 0;
		while (r <= maxR && (h + row_h (s, r) <= bandH || h == 0)) { h += row_h (s, r); r++; }
	}
	rb[nrb] = maxR + 1;
	enum { Z = 200 };
	int cw = bandW * 2, ch = bandH * 2;
	for (int i = 0; i < ncb; i++) { int w = (int) (col_x (s, cb[i + 1]) - col_x (s, cb[i])) * 2; if (w > cw) cw = w; }
	for (int i = 0; i < nrb; i++) { int h = (int) (row_y (s, rb[i + 1]) - row_y (s, rb[i])) * 2; if (h > ch) ch = h; }
	long cv = ui_canvas (0, 0, cw, ch);
	if (!cv) { print_abort (j); message ("There is not enough memory to print."); return; }
	ui_w_show (cv, 0);
	long gw, gh; uint32_t *cpx = uikit_canvas_pixels (cv, &gw, &gh);
	Pixels keep = g_cv;
	long font = print_font (j, (long) "DejaVu Sans", 0);
	int pageNo = 0;
	for (int i = 0; i < ncb; i++)
		for (int k = 0; k < nrb; k++)
		{
			pageNo++;
			if (!print_page (j, 0, 0)) continue;
			int bx0 = (int) col_x (s, cb[i]), by0 = (int) row_y (s, rb[k]);
			int pw = (int) (col_x (s, cb[i + 1]) - bx0), ph = (int) (row_y (s, rb[k + 1]) - by0);
			ui_canvas_begin (cv);
			g_cv.px = cpx; g_cv.w = (int) gw; g_cv.h = (int) gh;
			cv_fill (0, 0, pw * 2, ph * 2, 0xFFFFFF);
			PaneView pv;
			pane_layout (s, Z, cb[i], cb[i + 1], rb[k], rb[k + 1], 0, 0, pw * 2, ph * 2, pv);
			paint_pane (g_b, s, Z, pv, false);
			for (int q = 0; q < s->ncharts; q++)
			{
				Chart *c = s->charts[q];
				if (c->x < bx0 || c->x >= bx0 + pw || c->y < by0 || c->y >= by0 + ph || c->w <= 0 || c->h <= 0) continue;
				draw_chart (g_b, c, (c->x - bx0) * 2, (c->y - by0) * 2, c->w * 2, c->h * 2, Z, Rect { 0, 0, ph * 2 - 1, pw * 2 - 1 });
			}
			cv_end ();
			ui_canvas_end ();
			// (a picture no larger than the page's cells: its width is the canvas's, cropped by a copy line by line)
			unsigned *o = new unsigned[(size_t) pw * 2 * ph * 2];
			for (int y = 0; y < ph * 2; y++) memcpy (o + (size_t) y * pw * 2, cpx + (size_t) y * gw, (size_t) pw * 2 * 4);
			print_image (j, (long) o, pw * 2, ph * 2, print_mpt (M), print_mpt (M), print_mpt (pw * scale), print_mpt (ph * scale));
			delete[] o;
			char foot[120]; snprintf (foot, sizeof foot, "%s  \xE2\x80\x94  %d", s->name, pageNo);
			if (font >= 0)
			{
				long tw = print_text_width (j, font, 9000, (long) foot);
				print_text (j, font, 9000, (ps.paper_w - tw) / 2, print_mpt (PH - M + 12), (long) foot, 0x707070);
			}
		}
	g_cv = keep;
	ui_w_destroy (cv);
	if (print_end (j) < 0) message ("The sheet could not be put in the print queue.");
	gv_dirty (); gv_focus ();
}

// ---- the commands, as in Onyx --------------------------------------------------------------------------------
static void after_change (bool rows)
{
	recalc (g_b);
	touched ();
	if (rows) { Rect r = gv_sel (); Sheet *s = S (); for (int rr = r.r0; rr <= imin (r.r1, r.r0 + 500); rr++) autofit_row (g_b, s, rr); }
	gv_dirty ();
	refresh ();
}
static Rect used_sel ()						// the selection, clipped to the used area for whole rows / columns
{
	Rect r = gv_sel ();
	Sheet *s = S ();
	sheet_bounds (s);
	if (g_wholeCols) r.r1 = imax (r.r0, s->maxR);
	if (g_wholeRows) r.c1 = imax (r.c0, s->maxC);
	return r;
}
static void editor_copy (bool cut)
{
	EditLine &e = g_ed;
	if (e.s0 () == e.s1 ()) return;
	ui_clip_set ((long) (e.t.b + e.s0 ()), e.s1 () - e.s0 ());
	if (cut) { e.insert ("", 0); gv_changed (); }
}
static void cmd_copy ()
{
	if (g_ed.on) { editor_copy (false); return; }
	Sheet *s = S ();
	Rect r = used_sel ();
	if (g_wholeCols || g_wholeRows) copy_range (g_b, s, gv_sel (), false); else copy_range (g_b, s, r, false);
	char *t = clip_text (g_b, s, r);
	ui_clip_set ((long) t, -1);
	free (g_clip.text); g_clip.text = t;
	g_copyMarquee = true; g_copyRect = r; g_copySheet = s->id;
	gv_dirty ();
}
static void cmd_cut ()
{
	if (g_ed.on) { editor_copy (true); return; }
	cmd_copy ();
	g_clip.cut = true;
}
static void cmd_paste ()
{
	Sheet *s = S ();
	static char text[65536];
	int n = (int) ui_clip_get ((long) text, sizeof text);
	if (g_ed.on)
	{
		int k = 0; while (k < n && text[k] != '\n' && text[k] != '\r') k++;
		char *u = u8_valid (text, k) ? sdup (text, k) : latin1_to_u8 (text, k);
		g_ed.insert (u, (int) strlen (u)); free (u);
		gv_changed ();
		return;
	}
	bool ours = g_clip.full && g_clip.text && n > 0 && !strncmp (text, g_clip.text, n);	// (the clipboard may hold less than was copied)
	if (ours || (g_clip.full && n == 0))
	{
		if (g_clip.cut)
		{
			undo_book (g_b);
			paste_cut (g_b, s, s->curR, s->curC);
			g_copyMarquee = false;
			Rect r = { s->curR, s->curC, s->curR + g_clip.rows - 1, s->curC + g_clip.cols - 1 };
			gv_select_range (r);
		}
		else
		{
			Rect sl = gv_sel ();
			int R = g_clip.rows, C = g_clip.cols;
			Rect dst = { s->curR, s->curC, s->curR + R - 1, s->curC + C - 1 };
			if ((sl.r1 - sl.r0 + 1) % R == 0 && (sl.c1 - sl.c0 + 1) % C == 0 && !g_wholeCols && !g_wholeRows) dst = sl;
			if (g_clip.nmerge || g_clip.colW) undo_sheet (g_b, s); else undo_rect (g_b, s, dst);
			paste_range (g_b, s, s->curR, s->curC, sl);
			if (dst.r1 - dst.r0 + 1 == R && dst.c1 - dst.c0 + 1 == C) dst = Rect { s->curR, s->curC, s->curR + R - 1, s->curC + C - 1 };
			gv_select_range (dst);
		}
		after_change (true);
		return;
	}
	if (n <= 0) return;
	// text from another app: tab-separated lines
	char *u = u8_valid (text, n) ? sdup (text, n) : latin1_to_u8 (text, n);
	int rows = 1, cols = 1;
	for (const char *p = u; *p; p++) { if (*p == '\n' && p[1]) rows++; }
	undo_rect (g_b, s, Rect { s->curR, s->curC, imin (MAXR - 1, s->curR + rows), imin (MAXC - 1, s->curC + 200) });
	paste_text (g_b, s, s->curR, s->curC, u, &rows, &cols);
	free (u);
	gv_select_range (Rect { s->curR, s->curC, s->curR + imax (0, rows - 1), s->curC + imax (0, cols - 1) });
	after_change (true);
}
static void cmd_paste_special ()
{
	if (g_ed.on || !g_clip.full) { if (!g_clip.full) message ("Copy some cells first (Paste Special pastes what was copied here)."); return; }
	int what; bool tr;
	if (paste_dialog (&what, &tr) != 1) return;
	Sheet *s = S ();
	int R = tr ? g_clip.cols : g_clip.rows, C = tr ? g_clip.rows : g_clip.cols;
	Rect dst = { s->curR, s->curC, s->curR + R - 1, s->curC + C - 1 };
	undo_rect (g_b, s, dst);
	bool cut = g_clip.cut; g_clip.cut = false;
	paste_range (g_b, s, s->curR, s->curC, dst, what, tr);
	g_clip.cut = cut;
	gv_select_range (dst);
	after_change (true);
}
static void cmd_clear_contents ()
{
	if (g_ed.on) { g_ed.del (); gv_changed (); return; }
	Sheet *s = S ();
	Rect r = used_sel ();
	undo_rect (g_b, s, r);
	clear_range (g_b, s, r, CLR_CONTENT);
	after_change ();
}
static void cmd_undo ()
{
	if (g_ed.on) { gv_cancel_edit (); return; }
	Rect r;
	Sheet *s = undo_step (g_b, false, &r);
	if (!s) return;
	for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i] == s) g_b.active = i;
	fonts_reset (); recalc (g_b); touched ();
	g_selChart = -1; g_copyMarquee = false;
	tabs_sync ();
	gv_ensure_visible (s->curR, s->curC);
	refresh ();
}
static void cmd_redo ()
{
	if (g_ed.on) return;
	Rect r;
	Sheet *s = undo_step (g_b, true, &r);
	if (!s) return;
	for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i] == s) g_b.active = i;
	fonts_reset (); recalc (g_b); touched ();
	g_selChart = -1;
	tabs_sync ();
	gv_ensure_visible (s->curR, s->curC);
	refresh ();
}
static void cmd_fill_down ()
{
	if (g_ed.on) return;
	Sheet *s = S (); Rect r = used_sel ();
	undo_rect (g_b, s, r); fill_copy (g_b, s, r, true); after_change ();
}
static void cmd_fill_right ()
{
	if (g_ed.on) return;
	Sheet *s = S (); Rect r = used_sel ();
	undo_rect (g_b, s, r); fill_copy (g_b, s, r, false); after_change ();
}
static void cmd_select_all () { if (g_ed.on) { g_ed.anchor = 0; g_ed.caret = g_ed.t.n; gv_changed (); return; } gv_select_all (); }

// Find and Replace: a cell's text (its formula, or its value as shown) against what is looked for.
static bool cell_matches (Sheet *s, Cell *x, Buf &txt)
{
	txt.clear ();
	if (!x || x->kind == K_NONE) return false;
	if (g_find.formulas) cell_edit_text (g_b, s, x, txt);
	else { Shown sh; cell_shown (g_b, s, x, sh, 64); txt.puts (x->vt == V_STR ? x->str : sh.text); }
	char *f = sdup (g_find.find);
	int fl = (int) strlen (f);
	bool ok = false;
	if (g_find.whole) ok = g_find.matchCase ? !strcmp (txt.str (), f) : ci_cmp (txt.str (), txt.n, f, fl) == 0;
	else
		for (int i = 0; i + fl <= txt.n && !ok; i++)
			ok = g_find.matchCase ? !memcmp (txt.b + i, f, fl) : ci_cmp (txt.b + i, fl, f, fl) == 0;
	free (f);
	return ok;
}
static bool find_next (bool fromNext)
{
	for (int pass = 0; pass < (g_find.allSheets ? g_b.ns + 1 : 2); pass++)
	{
		Sheet *s = S ();
		sheet_bounds (s);
		int r = s->curR, c = s->curC + (fromNext ? 1 : 0);
		if (pass > 0) { r = 0; c = 0; }
		for (; r <= s->maxR; r++, c = 0)
			for (; c <= s->maxC; c++)
			{
				Buf t;
				if (cell_matches (s, s->cells.get (r, c), t)) { gv_select_cell (r, c, false); return true; }
			}
		if (g_find.allSheets && pass < g_b.ns) { pick_sheet ((g_b.active + 1) % g_b.ns); }
	}
	return false;
}
static int replace_in (Sheet *s, int r, int c)			// the cell's matches replaced; how many
{
	Cell *x = s->cells.get (r, c);
	Buf t;
	if (!cell_matches (s, x, t)) return 0;
	char *f = sdup (g_find.find), *rp = sdup (g_find.repl);
	int fl = (int) strlen (f), k = 0;
	Buf o;
	if (g_find.whole) { o.puts (rp); k = 1; }
	else
		for (int i = 0; i < t.n; )
		{
			bool m = i + fl <= t.n && (g_find.matchCase ? !memcmp (t.b + i, f, fl) : ci_cmp (t.b + i, fl, f, fl) == 0);
			if (m && fl) { o.puts (rp); i += fl; k++; } else o.put (t.b[i++]);
		}
	free (f); free (rp);
	if (!k) return 0;
	if (x->kind == K_STR && !g_find.formulas) cell_set_str (s, r, c, o.str ());
	else cell_input (g_b, s, r, c, o.str ());
	return k;
}
static void cmd_find ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	for (;;)
	{
		int r = find_dialog ();
		ui_view_flush ();
		if (r == 0 || !g_find.find[0]) return;
		Sheet *s = S ();
		if (r == 2) { if (!find_next (true)) { message ("Nothing more was found."); return; } show_now (); continue; }
		if (r == 3)
		{
			undo_rect (g_b, s, Rect { s->curR, s->curC, s->curR, s->curC });
			if (replace_in (s, s->curR, s->curC)) after_change ();
			if (!find_next (true)) { message ("Nothing more was found."); return; }
			show_now ();
			continue;
		}
		if (r == 4)
		{
			int total = 0;
			if (g_find.allSheets) undo_book (g_b); else undo_sheet (g_b, s);
			for (int i = 0; i < g_b.ns; i++)
			{
				Sheet *t = g_b.sh[i];
				if (!g_find.allSheets && t != s) continue;
				sheet_bounds (t);
				for (int rr = 0; rr <= t->maxR; rr++) for (int cc = 0; cc <= t->maxC; cc++) total += replace_in (t, rr, cc);
			}
			after_change ();
			char m[80]; snprintf (m, sizeof m, TR (total == 1 ? "%d replacement made." : "%d replacements made."), total);
			message (m);
			return;
		}
	}
}
// The selection as an absolute reference: "Sales!$B$5:$D$16".
static void sel_abs_ref (Buf &o)
{
	Sheet *s = S (); Rect r = gv_sel ();
	o.put ('='); put_sheet_name (o, s->name); o.put ('!');
	char a[24];
	col_name (r.c0, a); o.put ('$'); o.puts (a); o.put ('$'); o.puti (r.r0 + 1);
	if (r.r1 != r.r0 || r.c1 != r.c0) { col_name (r.c1, a); o.puts (":$"); o.puts (a); o.put ('$'); o.puti (r.r1 + 1); }
}
static void cmd_names ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Buf ref; sel_abs_ref (ref);
	undo_book (g_b);
	if (!names_dialog (&g_b, ref.str ())) undo_drop_last ();
	else { recalc (g_b); touched (); gv_dirty (); }
	gv_focus ();
	refresh ();
}
static void cmd_goto () { ui_w_focus (g_name); ui_textbox_select (g_name, 0, ui_w_text_len (g_name)); ui_w_draw (g_name); }
static void go_to (const char *t)
{
	// "B7", "C2:F9", "Sheet2!A1", "A:A", "3:3"
	Formula *f = formula_parse (g_b, t, -1);
	bool ok = false;
	if (f && f->nd[f->root].k == N_TOK)
	{
		const Tok &k = f->tok[f->nd[f->root].tok];
		if ((k.t == TK_REF || k.t == TK_AREA) && !(k.fl & TF_BAD))
		{
			if (k.sheet) for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i]->id == k.sheet) pick_sheet (i);
			if (k.t == TK_REF) gv_select_cell (k.r0, k.c0, false);
			else { gv_select_range (Rect { k.r0, k.c0, k.r1, k.c1 }); gv_ensure_visible (k.r0, k.c0); }
			ok = true;
		}
		else if (k.t == TK_NAME)
		{
			char nm[64]; scpy (nm, f->pool + k.s, imin (k.sn + 1, (int) sizeof nm));
			Sheet *s = S ();
			const DefName *d = name_find (g_b, nm, (int) strlen (nm), s->id);
			if (d && d->f)						// a name: its range shown
			{
				Buf e; e.put ('='); e.puts (nm);
				Val v = eval_text (g_b, s, s->curR, s->curC, e.str ());
				if (v.t == V_REF && v.sh)
				{
					for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i] == v.sh) pick_sheet (i);
					if (v.r0 == v.r1 && v.c0 == v.c1) gv_select_cell (v.r0, v.c0, false);
					else { gv_select_range (Rect { v.r0, v.c0, v.r1, v.c1 }); gv_ensure_visible (v.r0, v.c0); }
					ok = true;
				}
				else { message ("This name holds a value, not a range."); ok = true; }
			}
			else if (name_valid (nm))				// a new name: the selection's (as Excel does)
			{
				Buf ref; sel_abs_ref (ref);
				undo_book (g_b);
				if (name_set (g_b, nm, 0, ref.str ())) { recalc (g_b); touched (); gv_dirty (); }
				else undo_drop_last ();
				ok = true;
			}
		}
	}
	formula_free (f);
	if (!ok) message ("Type a cell or a range to go there (B7, C2:F9, Sheet2!A1), or a name for the selection.");
	gv_focus ();
	refresh ();
}

// ---- view ------------------------------------------------------------------------------------------------
static void set_zoom (int z) { g_z = iclamp (z, 25, 400); gv_dirty (); refresh (); }
static void zoom_step (int d)
{
	if (d == 0) { set_zoom (100); return; }
	int z = g_z, i = 0;
	while (i < NZOOMS && ZOOMS[i] < z) i++;
	if (d > 0) { if (i < NZOOMS && ZOOMS[i] == z) i++; set_zoom (ZOOMS[imin (i, NZOOMS - 1)]); }
	else set_zoom (ZOOMS[imax (0, i - 1)]);
}
static void grid_zoom (int dir) { zoom_step (dir); }
static void cmd_zoom_in () { zoom_step (1); }
static void cmd_zoom_out () { zoom_step (-1); }
static void cmd_zoom_100 () { set_zoom (100); }
static void cmd_freeze ()
{
	Sheet *s = S ();
	if (s->freezeR || s->freezeC) { s->freezeR = s->freezeC = 0; }
	else
	{
		int r = s->curR - (s->topR > s->curR ? 0 : 0), c = s->curC;
		if (r == 0 && c == 0) r = 1;				// (at A1: the first row)
		// the rows / columns frozen: those above / left of the cursor, from what is shown
		s->freezeR = r; s->freezeC = c;
		s->topR = imax (s->topR, r); s->leftC = imax (s->leftC, c);
	}
	touched ();
	gv_dirty (); refresh ();
}
static void cmd_gridlines () { Sheet *s = S (); s->grid = !s->grid; touched (); gv_dirty (); }
static void cmd_formulas () { g_showFormulas = !g_showFormulas; gv_dirty (); refresh (); }

// ---- insert -----------------------------------------------------------------------------------------------
static void cmd_ins_rows ()
{
	if (g_ed.on) return;
	Rect r = gv_sel ();
	int n = g_wholeCols ? 1 : r.r1 - r.r0 + 1;
	undo_book (g_b);
	insert_rows (g_b, S (), r.r0, n);
	after_change ();
}
static void cmd_ins_cols ()
{
	if (g_ed.on) return;
	Rect r = gv_sel ();
	int n = g_wholeRows ? 1 : r.c1 - r.c0 + 1;
	undo_book (g_b);
	insert_cols (g_b, S (), r.c0, n);
	after_change ();
}
static void cmd_del_rows ()
{
	if (g_ed.on) return;
	Rect r = gv_sel ();
	if (g_wholeCols) { message ("Choose rows (or cells in them) to delete."); return; }
	undo_book (g_b);
	delete_rows (g_b, S (), r.r0, r.r1 - r.r0 + 1);
	gv_select_cell (r.r0, S ()->curC, false);
	after_change ();
}
static void cmd_del_cols ()
{
	if (g_ed.on) return;
	Rect r = gv_sel ();
	if (g_wholeRows) { message ("Choose columns (or cells in them) to delete."); return; }
	undo_book (g_b);
	delete_cols (g_b, S (), r.c0, r.c1 - r.c0 + 1);
	gv_select_cell (S ()->curR, r.c0, false);
	after_change ();
}
static void cmd_function ()
{
	int fi = func_dialog ();
	if (fi < 0) { gv_focus (); return; }
	Buf o; o.puts (FNS[fi].name); o.put ('(');
	if (!g_ed.on) { Buf t; t.put ('='); t.puts (o.str ()); gv_begin_edit (true, t.str ()); }
	else g_ed.insert (o.str (), o.n);
	g_ed.typed = true;
	gv_focus ();
	gv_changed ();
}
// AutoSum: the numbers above the cell (else at its left) summed -- the formula proposed, being typed.
static void cmd_autosum ()
{
	Sheet *s = S ();
	if (g_ed.on) { g_ed.insert ("SUM(", 4); gv_changed (); return; }
	Rect sl = used_sel ();
	auto isnum = [&] (int r, int c) { Cell *x = s->cells.get (r, c); return x && (x->vt == V_NUM) && x->kind != K_NONE; };
	if (sl.r1 > sl.r0 || sl.c1 > sl.c0)				// a range: a total under each column
	{
		undo_rect (g_b, s, Rect { sl.r0, sl.c0, sl.r1 + 1, sl.c1 });
		for (int c = sl.c0; c <= sl.c1; c++)
		{
			char a[24], z[24], f[64]; cell_name (sl.r0, c, a); cell_name (sl.r1, c, z);
			snprintf (f, sizeof f, "=SUM(%s:%s)", a, z);
			cell_input (g_b, s, sl.r1 + 1, c, f);
		}
		after_change ();
		return;
	}
	int r = s->curR, c = s->curC;
	int r0 = r, c0 = c;
	while (r0 > 0 && isnum (r0 - 1, c)) r0--;
	char f[64];
	if (r0 < r) { char a[24], z[24]; cell_name (r0, c, a); cell_name (r - 1, c, z); snprintf (f, sizeof f, "=SUM(%s:%s)", a, z); }
	else
	{
		while (c0 > 0 && isnum (r, c0 - 1)) c0--;
		if (c0 < c) { char a[24], z[24]; cell_name (r, c0, a); cell_name (r, c - 1, z); snprintf (f, sizeof f, "=SUM(%s:%s)", a, z); }
		else scpy (f, "=SUM()", sizeof f);
	}
	gv_begin_edit (true, f);
	if (!strcmp (f, "=SUM()")) { g_ed.caret = g_ed.anchor = 5; }
	gv_focus ();
	gv_changed ();
}
// The block of data around the cursor (the current region: until empty rows and columns).
static Rect current_region (Sheet *s, int r, int c)
{
	Rect q = { r, c, r, c };
	auto filled = [&] (int rr, int cc) { if (rr < 0 || cc < 0 || rr >= MAXR || cc >= MAXC) return false; Cell *x = s->cells.get (rr, cc); return x && x->kind != K_NONE; };
	for (bool grew = true; grew; )
	{
		grew = false;
		for (int cc = q.c0 - 1; cc <= q.c1 + 1; cc++) { if (filled (q.r0 - 1, cc)) { q.r0--; grew = true; break; } }
		for (int cc = q.c0 - 1; cc <= q.c1 + 1; cc++) { if (filled (q.r1 + 1, cc)) { q.r1++; grew = true; break; } }
		for (int rr = q.r0 - 1; rr <= q.r1 + 1; rr++) { if (filled (rr, q.c0 - 1)) { q.c0--; grew = true; break; } }
		for (int rr = q.r0 - 1; rr <= q.r1 + 1; rr++) { if (filled (rr, q.c1 + 1)) { q.c1++; grew = true; break; } }
		if (q.r1 - q.r0 > 100000 || q.c1 - q.c0 > 1000) break;
	}
	return q;
}
static bool is_text_cell (Sheet *s, int r, int c) { Cell *x = s->cells.get (r, c); return x && x->vt == V_STR; }
static void cmd_chart ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = used_sel ();
	if (r.r0 == r.r1 && r.c0 == r.c1) r = current_region (s, r.r0, r.c0);
	Chart c; memset (&c, 0, sizeof c);
	c.type = CH_COLUMN; c.srcSheet = s->id; c.src = r;
	c.head = false; for (int cc = r.c0; cc <= r.c1; cc++) if (is_text_cell (s, r.r0, cc)) c.head = true;
	c.side = false; for (int rr = r.r0 + (c.head ? 1 : 0); rr <= r.r1; rr++) if (is_text_cell (s, rr, r.c0)) c.side = true;
	c.byRows = (r.c1 - r.c0) > (r.r1 - r.r0) && (r.r1 - r.r0) < 6;
	c.legend = LG_RIGHT; c.grid = true;
	c.w = 460; c.h = 290;
	c.x = col_x (s, r.c1 + 1) + 24; c.y = (int) row_y (s, r.r0);
	if (chart_dialog (&g_b, c, true) != 1) return;
	undo_sheet (g_b, s);
	Chart *nc = (Chart *) malloc (sizeof (Chart)); *nc = ch.c;
	chart_anchor (s, nc);
	s->charts = (Chart **) realloc (s->charts, (s->ncharts + 1) * sizeof (Chart *));
	s->charts[s->ncharts++] = nc;
	g_selChart = s->ncharts - 1;
	touched ();
	gv_dirty ();
}
static void chart_open (int i)
{
	Sheet *s = S ();
	if (i < 0 || i >= s->ncharts) return;
	if (chart_dialog (&g_b, *s->charts[i], false) != 1) return;
	undo_sheet (g_b, s);
	*s->charts[i] = ch.c;
	touched ();
	gv_dirty ();
}
static void cmd_insert_sheet ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	undo_book (g_b);
	char nm[64]; book_unique_sheet_name (g_b, nm, sizeof nm, TR ("Sheet"));
	book_add_sheet (g_b, nm, g_b.active + 1);
	g_b.active++;
	touched ();
	tabs_sync ();
	refresh ();
}
static void cmd_date_time (bool time)
{
	int y, mo, d, h, mi, se; now_clock (&y, &mo, &d, &h, &mi, &se);
	char t[40];
	if (time) snprintf (t, sizeof t, "%02d:%02d:%02d", h, mi, se); else snprintf (t, sizeof t, "%02d/%02d/%04d", d, mo, y);
	if (g_ed.on) { g_ed.insert (t, (int) strlen (t)); gv_changed (); return; }
	Sheet *s = S ();
	undo_rect (g_b, s, Rect { s->curR, s->curC, s->curR, s->curC });
	cell_input (g_b, s, s->curR, s->curC, t);
	after_change ();
}
static void cmd_date () { cmd_date_time (false); }
static void cmd_time () { cmd_date_time (true); }

// ---- format ---------------------------------------------------------------------------------------------
// A change applied to every cell of the selection (whole rows / columns: theirs too), one undo step.
static void apply_style (StyleFn fn, const void *arg)
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = gv_sel ();
	if (g_wholeCols || g_wholeRows) undo_sheet (g_b, s); else undo_rect (g_b, s, r);
	style_range (g_b, s, r, fn, arg);
	after_change (true);
}
static const Style &cur_style () { Sheet *s = S (); return cell_style (g_b, s, s->cells.get (s->curR, s->curC), s->curR, s->curC); }
static void f_bold (Style &st, const void *a) { st.bold = a != 0; }
static void f_italic (Style &st, const void *a) { st.italic = a != 0; }
static void f_under (Style &st, const void *a) { st.under = (unsigned char) (long) a; }
static void f_strike (Style &st, const void *a) { st.strike = a != 0; }
static void f_ha (Style &st, const void *a) { st.ha = (unsigned char) (long) a; }
static void f_va (Style &st, const void *a) { st.va = (unsigned char) (long) a; }
static void f_wrap (Style &st, const void *a) { st.wrap = a != 0; }
static void f_color (Style &st, const void *a) { st.color = (unsigned) (unsigned long) a; }
static void f_fill (Style &st, const void *a) { st.fill = (unsigned) (unsigned long) a; }
static void f_fmt (Style &st, const void *a) { st.fmt = (unsigned short) (long) a; }
static void f_font (Style &st, const void *a) { st.font = (unsigned short) (long) a; }
static void f_size (Style &st, const void *a) { st.size = (unsigned short) (long) a; }
static void f_indent (Style &st, const void *a) { st.indent = (unsigned char) iclamp (st.indent + (int) (long) a, 0, 15); }
static void f_clear (Style &st, const void *) { st.reset (); }
static void cmd_bold () { apply_style (f_bold, (const void *) (long) !cur_style ().bold); }
static void cmd_italic () { apply_style (f_italic, (const void *) (long) !cur_style ().italic); }
static void cmd_under () { apply_style (f_under, (const void *) (long) (cur_style ().under ? 0 : 1)); }
static void cmd_strike () { apply_style (f_strike, (const void *) (long) !cur_style ().strike); }
static void cmd_left () { apply_style (f_ha, (const void *) (long) (cur_style ().ha == HA_LEFT ? HA_GENERAL : HA_LEFT)); }
static void cmd_center () { apply_style (f_ha, (const void *) (long) (cur_style ().ha == HA_CENTER ? HA_GENERAL : HA_CENTER)); }
static void cmd_right () { apply_style (f_ha, (const void *) (long) (cur_style ().ha == HA_RIGHT ? HA_GENERAL : HA_RIGHT)); }
static void cmd_vtop () { apply_style (f_va, (const void *) (long) VA_TOP); }
static void cmd_vmid () { apply_style (f_va, (const void *) (long) VA_CENTER); }
static void cmd_vbot () { apply_style (f_va, (const void *) (long) VA_BOTTOM); }
static void cmd_wrap () { apply_style (f_wrap, (const void *) (long) !cur_style ().wrap); }
static void cmd_indent () { apply_style (f_indent, (const void *) (long) 1); }
static void cmd_outdent () { apply_style (f_indent, (const void *) (long) -1); }
static void cmd_text_color () { apply_style (f_color, (const void *) (unsigned long) g_textColor); }
static void cmd_fill_color () { apply_style (f_fill, (const void *) (unsigned long) g_fillColor); }
static void cmd_clear_format ()
{
	if (g_ed.on) return;
	Sheet *s = S (); Rect r = gv_sel ();
	if (g_wholeCols || g_wholeRows) undo_sheet (g_b, s); else undo_rect (g_b, s, r);
	style_range (g_b, s, r, f_clear, 0);
	after_change (true);
}
static void cmd_merge ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = gv_sel ();
	if (g_wholeCols || g_wholeRows) { message ("Merge a range of cells, not whole rows or columns."); return; }
	undo_sheet (g_b, s);
	int mi = merge_at (s, r.r0, r.c0);
	if (mi >= 0 && s->merges[mi].r0 == r.r0 && s->merges[mi].c0 == r.c0 && s->merges[mi].r1 == r.r1 && s->merges[mi].c1 == r.c1) unmerge_range (s, r);
	else
	{
		// (the other cells' content goes: asked first when there is some)
		bool more = false;
		for (int rr = r.r0; rr <= r.r1 && !more; rr++) for (int c = r.c0; c <= r.c1; c++) { if (rr == r.r0 && c == r.c0) continue; Cell *x = s->cells.get (rr, c); if (x && x->kind != K_NONE) { more = true; break; } }
		if (more && note ("Merge Cells", "Only the top left cell's content is kept. Merge anyway?", UM_OKCANCEL) != 1) { undo_step (g_b, false); ustack_clear (g_redo); return; }
		merge_range (g_b, s, r);
		Rect one = { r.r0, r.c0, r.r0, r.c0 };
		style_range (g_b, s, one, f_ha, (const void *) (long) HA_CENTER);
		style_range (g_b, s, one, f_va, (const void *) (long) VA_CENTER);
	}
	gv_select_range (r);
	after_change ();
}
// Number formats from the toolbar.
static void set_format (const char *code) { apply_style (f_fmt, (const void *) (long) book_fmt (g_b, code)); }
static const char *g_curSym = "\xE2\x82\xAC";
static void cmd_currency ()
{
	char c[64];
	if (!strcmp (g_curSym, "$")) scpy (c, "$#,##0.00;[Red]-$#,##0.00", sizeof c);
	else snprintf (c, sizeof c, "#,##0.00 \"%s\";[Red]-#,##0.00 \"%s\"", g_curSym, g_curSym);
	set_format (c);
}
static void cmd_percent () { set_format ("0%"); }
static void cmd_thousands () { set_format ("#,##0.00"); }
static void change_decimals (int d)
{
	Sheet *s = S ();
	Cell *x = s->cells.get (s->curR, s->curC);
	const char *code = book_fmt_code (g_b, cur_style ().fmt);
	int gen = 0;
	if (x && x->vt == V_NUM)				// (General: as many decimals as are shown now)
	{
		char t[40]; num_general (x->num, t, sizeof t, 11);
		char *dot = strchr (t, '.'); if (dot && !strchr (t, 'E')) gen = (int) strlen (dot + 1);
	}
	char out[160]; fmt_add_decimals (code, d, gen, out, sizeof out);
	set_format (out);
}
static void cmd_dec_plus () { change_decimals (1); }
static void cmd_dec_minus () { change_decimals (-1); }
// Borders from the toolbar: presets over the selection.
enum { BP_NONE, BP_ALL, BP_OUTLINE, BP_THICKOUT, BP_BOTTOM, BP_TOP, BP_LEFT, BP_RIGHT, BP_DBOTTOM, BP_THICKBOTTOM, BP_INSIDE };
static const char *const BP_NAMES[] = { "No Borders", "All Borders", "Outside Borders", "Thick Outside Borders", "Bottom Border", "Top Border", "Left Border", "Right Border", "Double Bottom Border", "Thick Bottom Border", "Inside Borders" };
static void apply_borders (int preset, int lineStyle, unsigned color, const BorderSet *custom = 0)
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = used_sel ();
	if ((long long) (r.r1 - r.r0 + 1) * (r.c1 - r.c0 + 1) > 1000000) return;
	undo_rect (g_b, s, Rect { imax (0, r.r0 - 1), imax (0, r.c0 - 1), imin (MAXR - 1, r.r1 + 1), imin (MAXC - 1, r.c1 + 1) });
	for (int rr = r.r0; rr <= r.r1; rr++)
		for (int c = r.c0; c <= r.c1; c++)
		{
			Cell *x = cell_make (s, rr, c);
			Style st = g_b.styles.s[x->style];
			bool top = rr == r.r0, bot = rr == r.r1, lef = c == r.c0, rig = c == r.c1;
			auto set = [&] (int side, bool on, int bs) { st.bs[side] = (unsigned char) (on ? bs : BS_NONE); st.bc[side] = on ? color : 0; };
			switch (preset)
			{
			case BP_NONE: for (int k = 0; k < 4; k++) set (k, false, 0); break;
			case BP_ALL: for (int k = 0; k < 4; k++) set (k, true, lineStyle); break;
			case BP_OUTLINE: case BP_THICKOUT:
			{
				int bs = preset == BP_THICKOUT ? BS_THICK : lineStyle;
				if (top) set (B_TOP, true, bs);
				if (bot) set (B_BOTTOM, true, bs);
				if (lef) set (B_LEFT, true, bs);
				if (rig) set (B_RIGHT, true, bs);
				break;
			}
			case BP_INSIDE:
				if (!top) set (B_TOP, true, lineStyle);
				if (!bot) set (B_BOTTOM, true, lineStyle);
				if (!lef) set (B_LEFT, true, lineStyle);
				if (!rig) set (B_RIGHT, true, lineStyle);
				break;
			case BP_BOTTOM: if (bot) set (B_BOTTOM, true, lineStyle); break;
			case BP_TOP: if (top) set (B_TOP, true, lineStyle); break;
			case BP_LEFT: if (lef) set (B_LEFT, true, lineStyle); break;
			case BP_RIGHT: if (rig) set (B_RIGHT, true, lineStyle); break;
			case BP_DBOTTOM: if (bot) set (B_BOTTOM, true, BS_DOUBLE); break;
			case BP_THICKBOTTOM: if (bot) set (B_BOTTOM, true, BS_THICK); break;
			case -1:
				if (custom)
				{
					const int *e = custom->edge;
					if (lef && e[0] >= 0) set (B_LEFT, e[0] == 1, lineStyle);
					if (rig && e[1] >= 0) set (B_RIGHT, e[1] == 1, lineStyle);
					if (top && e[2] >= 0) set (B_TOP, e[2] == 1, lineStyle);
					if (bot && e[3] >= 0) set (B_BOTTOM, e[3] == 1, lineStyle);
					if (!bot && e[4] >= 0) set (B_BOTTOM, e[4] == 1, lineStyle);	// (inside: horizontal)
					if (!top && e[4] >= 0) set (B_TOP, e[4] == 1, lineStyle);
					if (!rig && e[5] >= 0) set (B_RIGHT, e[5] == 1, lineStyle);	// (inside: vertical)
					if (!lef && e[5] >= 0) set (B_LEFT, e[5] == 1, lineStyle);
				}
				break;
			}
			x->style = (unsigned short) g_b.styles.intern (st);
			cell_drop_if_plain (s, x);
		}
	after_change ();
}
static void cmd_borders () { apply_borders (g_borderPreset, BS_THIN, 0); }
struct Diff { Style from, to; };
static void f_diff (Style &st, const void *a)
{
	const Diff *d = (const Diff *) a;
	const Style &f = d->from, &t = d->to;
	if (f.font != t.font) st.font = t.font;
	if (f.size != t.size) st.size = t.size;
	if (f.bold != t.bold) st.bold = t.bold;
	if (f.italic != t.italic) st.italic = t.italic;
	if (f.under != t.under) st.under = t.under;
	if (f.strike != t.strike) st.strike = t.strike;
	if (f.color != t.color) st.color = t.color;
	if (f.fill != t.fill) st.fill = t.fill;
	if (f.ha != t.ha) st.ha = t.ha;
	if (f.va != t.va) st.va = t.va;
	if (f.wrap != t.wrap) st.wrap = t.wrap;
	if (f.indent != t.indent) st.indent = t.indent;
	if (f.fmt != t.fmt) st.fmt = t.fmt;
}
static void cmd_row_height ()
{
	Sheet *s = S (); Rect r = gv_sel ();
	char t[32]; snprintf (t, sizeof t, "%g", row_h (s, s->curR) * 0.75);
	if (!ui_ask ((long) TR ("Row Height"), (long) TR ("Height (points):"), (long) t, sizeof t)) return;
	double pt = strtod (t, 0);
	if (pt <= 0 || pt > 1000) return;
	undo_sheet (g_b, s);
	for (int rr = r.r0; rr <= imin (r.r1, r.r0 + 100000); rr++) { RowInfo *ri = row_add (s, rr); ri->h = (unsigned short) (pt * 4 / 3 + 0.5); ri->fl |= RF_CUSTOM; ri->fl &= ~RF_HIDDEN; }
	rows_changed (s); touched (); gv_dirty ();
}
static void cmd_col_width ()
{
	Sheet *s = S (); Rect r = gv_sel ();
	char t[32]; snprintf (t, sizeof t, "%.2f", (col_w (s, s->curC) - 5) / 7.0);
	if (!ui_ask ((long) TR ("Column Width"), (long) TR ("Width (characters of the default font):"), (long) t, sizeof t)) return;
	double ch_ = strtod (t, 0);
	if (ch_ < 0 || ch_ > 255) return;
	undo_sheet (g_b, s);
	for (int c = r.c0; c <= r.c1; c++) { s->colW[c] = (unsigned short) (ch_ * 7 + 5 + 0.5); s->colFl[c] &= ~RF_HIDDEN; }
	cols_changed (s); touched (); gv_dirty ();
}
static void cmd_opt_width ()
{
	Sheet *s = S (); Rect r = gv_sel ();
	undo_sheet (g_b, s);
	for (int c = r.c0; c <= imin (r.c1, r.c0 + 500); c++) { s->colW[c] = (unsigned short) fit_col_width (g_b, s, c); s->colFl[c] &= ~RF_HIDDEN; }
	cols_changed (s); touched (); gv_dirty ();
}
static void cmd_hide_rows ()
{
	Sheet *s = S (); Rect r = gv_sel ();
	undo_sheet (g_b, s);
	for (int rr = r.r0; rr <= imin (r.r1, r.r0 + 100000); rr++) row_add (s, rr)->fl |= RF_HIDDEN;
	rows_changed (s); touched ();
	gv_select_cell (imin (MAXR - 1, r.r1 + 1), s->curC, false);
}
static void cmd_hide_cols ()
{
	Sheet *s = S (); Rect r = gv_sel ();
	undo_sheet (g_b, s);
	for (int c = r.c0; c <= r.c1; c++) s->colFl[c] |= RF_HIDDEN;
	cols_changed (s); touched ();
	gv_select_cell (s->curR, imin (MAXC - 1, r.c1 + 1), false);
}
static void cmd_show_all ()
{
	Sheet *s = S (); Rect r = gv_sel ();
	if (r.r0 == r.r1 && r.c0 == r.c1) { r.r0 = 0; r.r1 = MAXR - 1; r.c0 = 0; r.c1 = MAXC - 1; }
	undo_sheet (g_b, s);
	for (int c = imax (0, r.c0 - 1); c <= imin (MAXC - 1, r.c1 + 1); c++) s->colFl[c] &= ~RF_HIDDEN;
	for (int i = 0; i < s->nrows; i++) if (s->rows[i].r >= r.r0 - 1 && s->rows[i].r <= r.r1 + 1) s->rows[i].fl &= ~RF_HIDDEN;
	cols_changed (s); rows_changed (s); touched (); gv_dirty ();
}

static void pick_sheet (int i)
{
	if (i < 0 || i >= g_b.ns || i == g_b.active) return;
	if (g_ed.on && !g_ed.pointing && !gv_commit (0, 0)) return;
	g_b.active = i;
	gv_reset_sel_kind (); g_selChart = -1;
	tabs_sync ();
	gv_dirty ();
	refresh ();
}
static void cmd_delete_sheet ()
{
	if (g_b.ns <= 1) { message ("A workbook keeps one sheet at least."); return; }
	char m[200]; snprintf (m, sizeof m, TR ("Delete the sheet \"%s\"? (Undo brings it back.)"), S ()->name);
	if (note ("Delete Sheet", m, UM_OKCANCEL) != 1) return;
	undo_book (g_b);
	delete_sheet (g_b, g_b.active);
	recalc (g_b); touched ();
	tabs_sync (); gv_dirty (); refresh ();
}
static void rename_sheet (int i)
{
	char cur[64]; scpy (cur, g_b.sh[i]->name, sizeof cur);
	if (!ui_ask ((long) TR ("Rename Sheet"), (long) TR ("The sheet's name:"), (long) cur, sizeof cur) || !cur[0]) return;
	for (char *p = cur; *p; p++) if (strchr ("[]:*?/\\", *p)) *p = '_';
	int k = book_sheet_index (g_b, cur);
	if (k >= 0 && k != i) { message ("Another sheet has this name."); return; }
	undo_book (g_b);
	scpy (g_b.sh[i]->name, cur, sizeof g_b.sh[i]->name);
	touched (); tabs_sync (); gv_dirty (); refresh ();
}
static void cmd_rename_sheet () { rename_sheet (g_b.active); }
static void cmd_duplicate_sheet () { undo_book (g_b); if (duplicate_sheet (g_b, g_b.active)) { g_b.active++; recalc (g_b); touched (); tabs_sync (); refresh (); } }
static void cmd_sheet_left () { if (g_b.active > 0) { undo_book (g_b); move_sheet (g_b, g_b.active, g_b.active - 1); g_b.active--; touched (); tabs_sync (); } }
static void cmd_sheet_right () { if (g_b.active < g_b.ns - 1) { undo_book (g_b); move_sheet (g_b, g_b.active, g_b.active + 1); g_b.active++; touched (); tabs_sync (); } }
static bool looks_header (Sheet *s, Rect r)			// the first row texts, the next one numbers somewhere
{
	if (r.r1 <= r.r0) return false;
	bool text = true, below = false;
	for (int c = r.c0; c <= r.c1; c++)
	{
		Cell *x = s->cells.get (r.r0, c);
		if (x && x->kind != K_NONE && x->vt != V_STR) text = false;
		Cell *y = s->cells.get (r.r0 + 1, c);
		if (y && y->vt != V_STR && y->kind != K_NONE) below = true;
	}
	return text && below;
}
static void sort_by (bool desc)
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = used_sel ();
	int keyCol = s->curC;
	if (r.r0 == r.r1 && r.c0 == r.c1) r = current_region (s, r.r0, r.c0);
	if (r.r1 <= r.r0) return;
	bool hdr = looks_header (s, r);
	undo_rect (g_b, s, r);
	SortKey k = { keyCol, desc };
	sort_range (g_b, s, r, &k, 1, hdr);
	after_change ();
}
static void cmd_sort_asc () { sort_by (false); }
static void cmd_sort_desc () { sort_by (true); }
static void cmd_sort ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = used_sel ();
	if (r.r0 == r.r1 && r.c0 == r.c1) r = current_region (s, r.r0, r.c0);
	if (r.c1 - r.c0 >= 64) r.c1 = r.c0 + 63;
	if (sort_dialog (s, r, looks_header (s, r)) != 1) return;
	SortKey keys[3]; int n = 0;
	for (int k = 0; k < 3; k++) if (sd.ok[k] >= 0) { keys[n].col = r.c0 + sd.ok[k]; keys[n].desc = sd.dsc[k]; n++; }
	if (!n) return;
	gv_select_range (r);
	undo_rect (g_b, s, r);
	sort_range (g_b, s, r, keys, n, sd.header);
	after_change ();
}
static void cmd_recalc () { recalc (g_b); gv_dirty (); refresh (); }
// Data > AutoFilter: the buttons put on the table's headers (the selection, else the table around the
// cursor), or taken off with the filters.
static void cmd_autofilter ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	undo_sheet (g_b, s);
	if (s->af.on) af_clear (s);
	else
	{
		Rect r = used_sel ();
		if (r.r0 == r.r1 && r.c0 == r.c1) r = current_region (s, r.r0, r.c0);
		if (r.r1 <= r.r0) { undo_drop_last (); message ("Choose a table first: its first row holds the headers, the rows under it the data."); return; }
		s->af.on = true; s->af.r = r;
	}
	touched (); gv_dirty (); refresh ();
}
static bool other_key (long hid, int mods)
{
	bool ctrl = mods & UI_MOD_CTRL;
	if (hid == KEY_DELETE) { cmd_clear_contents (); return true; }
	if (ctrl && hid == 0x1E) { cmd_format_cells (); return true; }			// Ctrl+1
	if (ctrl && hid == 0x33) { cmd_date (); return true; }				// Ctrl+;
	if (ctrl && hid == 0x37) { cmd_time (); return true; }				// Ctrl+.
	if (ctrl && (hid == 0x2E || hid == 0x57))					// Ctrl++
	{ if (g_wholeCols) cmd_ins_cols (); else cmd_ins_rows (); return true; }
	if (ctrl && (hid == 0x2D || hid == 0x56)) { if (g_wholeCols) cmd_del_cols (); else cmd_del_rows (); return true; }	// Ctrl+-
	if (ctrl && hid == 0x35) { cmd_formulas (); return true; }			// Ctrl+`
	return false;
}

// ---- the grid's requests ----------------------------------------------------------------------------------------
// where: 0..3 a menu (when the event is over); 4 the chart deleted; 5 another sheet (x: -1, +1); 6 the fill handle
// released; 7, 9 a column / a row fitted; 8 a size or a chart about to change: its undo step.
static void grid_context (int x, int y, int where)
{
	Sheet *s = S ();
	switch (where)
	{
	case 4:
		if (g_selChart >= 0 && g_selChart < s->ncharts)
		{
			undo_sheet (g_b, s);
			chart_free (s->charts[g_selChart]);
			memmove (s->charts + g_selChart, s->charts + g_selChart + 1, (s->ncharts - g_selChart - 1) * sizeof (Chart *));
			s->ncharts--; g_selChart = -1;
			touched (); gv_dirty ();
		}
		return;
	case 5: pick_sheet (iclamp (g_b.active + x, 0, g_b.ns - 1)); return;
	case 6:
	{
		Rect src = gv_sel (), to = g_fillRect;
		undo_rect (g_b, s, to);
		// (a smaller range than the source: the rest cleared, as the spreadsheets do)
		if (to.r1 < src.r1 || to.c1 < src.c1)
		{
			Rect clr = src;
			if (to.r1 < src.r1) clr.r0 = to.r1 + 1; else clr.c0 = to.c1 + 1;
			clear_range (g_b, s, clr, CLR_CONTENT);
		}
		else fill_range (g_b, s, src, to);
		gv_select_range (to.r1 < src.r1 || to.c1 < src.c1 ? Rect { src.r0, src.c0, to.r1, to.c1 } : to);
		after_change (true);
		return;
	}
	case 7:
	{
		undo_sheet (g_b, s);
		Rect r = gv_sel ();
		int c0 = g_fitCol, c1 = c0;
		if (g_wholeCols && c0 >= r.c0 && c0 <= r.c1) { c0 = r.c0; c1 = r.c1; }
		for (int c = c0; c <= c1; c++) { s->colW[c] = (unsigned short) fit_col_width (g_b, s, c); s->colFl[c] &= ~RF_HIDDEN; }
		cols_changed (s); touched (); gv_dirty ();
		return;
	}
	case 8: undo_sheet (g_b, s); touched (); return;
	case 9:
	{
		undo_sheet (g_b, s);
		RowInfo *ri = row_info (s, g_fitRow);
		if (ri) ri->fl &= ~(RF_CUSTOM | RF_HIDDEN);
		autofit_row (g_b, s, g_fitRow);
		touched (); gv_dirty ();
		return;
	}
	}
	g_pend.where = where; g_pend.x = x; g_pend.y = y;
}

// A menu at a point of the window: names, each ended by a 0, a last empty one; ids: what each gives.
static int popup (int x, int y, const char *labels, int width, const int *ids)
{
	static char t[1024];					// (the labels translated, one after the other)
	int k = 0;
	for (const char *p = labels; *p; p += strlen (p) + 1)
	{
		const char *s = TR (p);
		int n = (int) strlen (s);
		if (k + n + 2 > (int) sizeof t) break;
		memcpy (t + k, s, n + 1);
		k += n + 1;
	}
	t[k] = 0;
	long r = ui_popup_local (x, y, (long) t, width);
	return r < 0 ? 0 : ids[r];
}
static void command (int id);
static void context_menu (int x, int y, int where)
{
	int ax = x + g_vx, ay = y + g_vy;
	if (where == 3)
	{
		enum { CHP = 1000, CHD, CHF };
		static const int IDS[] = { CHP, CHD, CHF };
		int r = popup (ax, ay, "Chart Properties...\0Delete Chart\0Bring to Front\0", 200, IDS);
		if (r == CHP) chart_open (g_selChart);
		else if (r == CHD) grid_context (0, 0, 4);
		else if (r == CHF)
		{
			Sheet *s = S (); int i = g_selChart;
			if (i >= 0 && i < s->ncharts - 1) { Chart *c = s->charts[i]; memmove (s->charts + i, s->charts + i + 1, (s->ncharts - i - 1) * sizeof (Chart *)); s->charts[s->ncharts - 1] = c; g_selChart = s->ncharts - 1; touched (); }
			gv_dirty ();
		}
		return;
	}
	static const int ID0[] = { M_CUT, M_COPY, M_PASTE, M_PASTESP, 0, M_INSCOL, M_DELCOL, M_INSROW, M_DELROW, M_CLEAR, 0, M_SORTA, M_CHART, M_FORMAT };
	static const int ID1[] = { M_CUT, M_COPY, M_PASTE, M_PASTESP, 0, M_INSCOL, M_DELCOL, M_CLEAR, 0, M_COLW, M_OPTW, M_HIDEC, M_SHOW, M_FORMAT };
	static const int ID2[] = { M_CUT, M_COPY, M_PASTE, M_PASTESP, 0, M_INSROW, M_DELROW, M_CLEAR, 0, M_ROWH, M_HIDER, M_SHOW, M_FORMAT };
	int r;
	if (where == 1) r = popup (ax, ay, "Cut\0Copy\0Paste\0Paste Special...\0-\0Insert Columns Before\0Delete Columns\0Clear Contents\0-\0Column Width...\0Optimal Width\0Hide Columns\0Show Columns\0Format Cells...\0", 220, ID1);
	else if (where == 2) r = popup (ax, ay, "Cut\0Copy\0Paste\0Paste Special...\0-\0Insert Rows Above\0Delete Rows\0Clear Contents\0-\0Row Height...\0Hide Rows\0Show Rows\0Format Cells...\0", 220, ID2);
	else r = popup (ax, ay, "Cut\0Copy\0Paste\0Paste Special...\0-\0Insert Columns Before\0Delete Columns\0Insert Rows Above\0Delete Rows\0Clear Contents\0-\0Sort Ascending\0Insert Chart...\0Format Cells...\0", 220, ID0);
	if (r) command (r);
	gv_dirty ();
}
static void filter_drop (int col)
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	AfVal *vals = 0; int n = af_values (g_b, s, col, &vals);
	int k = af_index (s, col);
	int r = filter_dialog (vals, n, k >= 0 ? s->af.shown[k] : 0);
	if (r == 2 || r == 3)
	{
		undo_sheet (g_b, s);
		SortKey key = { col, r == 3 };
		sort_range (g_b, s, s->af.r, &key, 1, true);
		recalc (g_b);
		af_apply (g_b, s);
		touched (); gv_dirty (); refresh ();
	}
	else if (r == 1)
	{
		undo_sheet (g_b, s);
		if (fp_all ()) af_set (s, col, 0);
		else { Buf l; for (int i = 0; i < n; i++) if (fp.on[i]) { l.puts (vals[i].t); l.put ('\x1F'); } af_set (s, col, l.str ()); }
		af_apply (g_b, s);
		touched (); gv_dirty (); refresh ();
	}
	free (fp.on); fp.on = 0;
	for (int i = 0; i < n; i++) free (vals[i].t);
	free (vals);
	gv_focus ();
}
// What was asked from inside an event, now that it is over.
static void pending ()
{
	if (g_note[0]) { char t[256]; scpy (t, g_note, sizeof t); g_note[0] = 0; refresh_now (); ui_view_flush (); message (t); }
	if (g_pend.where >= 0) { int w = g_pend.where; g_pend.where = -1; refresh_now (); ui_view_flush (); context_menu (g_pend.x, g_pend.y, w); }
	if (g_pendChart >= 0) { int i = g_pendChart; g_pendChart = -1; chart_open (i); }
	if (g_pendCol >= 0) { int c = g_pendCol; g_pendCol = -1; filter_drop (c); }
}

// Format Cells: what was changed set over the selection.
static void cmd_format_cells ()
{
	if (g_ed.on && !gv_commit (0, 0)) return;
	Sheet *s = S ();
	Cell *x = s->cells.get (s->curR, s->curC);
	Rect r = gv_sel ();
	int mi = merge_at (s, r.r0, r.c0);
	bool merged = mi >= 0 && s->merges[mi].r1 == r.r1 && s->merges[mi].c1 == r.c1 && (r.r1 > r.r0 || r.c1 > r.c0);
	if (format_dialog (&g_b, cur_style (), x && x->vt == V_NUM ? x->num : 1234.5678, !x || x->vt == V_NUM || x->kind == K_NONE, x && x->vt == V_STR ? x->str : "", merged) != 1) { gv_focus (); return; }
	undo_sheet (g_b, s);
	Diff df; df.from = fd.orig; df.to = fd.st;
	style_range (g_b, s, r, f_diff, &df);
	bool anyEdge = false; for (int k = 0; k < 6; k++) if (fd.bset.edge[k] >= 0) anyEdge = true;
	if (anyEdge)
	{
		ustack_clear (g_redo);
		BorderSet bs = fd.bset;
		// (in the same undo step: the sheet was written down already)
		int n = g_undo.n;
		apply_borders (-1, BS_VALS[iclamp (fd.bstyleV, 0, 6)], fd.bcolV, &bs);
		while (g_undo.n > n) { g_undo.n--; free (g_undo.s[g_undo.n].data); }
	}
	if (fd.mergeOn != fd.mergeOrig && !g_wholeCols && !g_wholeRows)
	{
		if (fd.mergeOn) merge_range (g_b, s, r); else unmerge_range (s, r);
	}
	after_change (true);
	gv_focus ();
}
static void drop_borders ()
{
	static const int IDS[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
	int r = popup (g_vx + 300, g_vy, "No Borders\0All Borders\0Outside Borders\0Thick Outside Borders\0Bottom Border\0Top Border\0Left Border\0Right Border\0Double Bottom Border\0Thick Bottom Border\0Inside Borders\0", 220, IDS);
	if (r > 0) { g_borderPreset = r - 1; apply_borders (g_borderPreset, BS_THIN, 0); }
}
static void drop_currency ()
{
	static const int IDS[] = { 1, 2, 3, 4, 5 };
	int r = popup (g_vx + 200, g_vy, "Euro\0Dollar ($)\0Pound\0Yen\0Franc (CHF)\0", 160, IDS);
	if (r > 0) { g_curSym = CURRENCIES[r - 1]; cmd_currency (); }
}
static void tab_menu (int x, int y)
{
	static const int IDS[] = { M_INSSHEET, M_DELSHEET, M_RENSHEET, M_DUPSHEET, 0, M_SHLEFT, M_SHRIGHT };
	int r = popup (x, y, "Insert Sheet\0Delete Sheet\0Rename...\0Duplicate\0-\0Move Left\0Move Right\0", 180, IDS);
	if (r) command (r);
}

// ---- the state shown -----------------------------------------------------------------------------------------
static void tool_on (int id, bool v) { long b = g_btn[id]; if (b && (ui_w_get_value (b) != 0) != v) { ui_w_set_value (b, v); ui_w_draw (b); } }
static void tool_enable (int id, bool on)
{
	static signed char was[M_COUNT];
	if (was[id] == (on ? 1 : 2)) return;
	was[id] = on ? 1 : 2;
	ui_w_enable (g_btn[id], on);
}
static void menu_enable (int id, bool on)
{
	static signed char was[M_COUNT];
	if (was[id] == (on ? 1 : 2)) return;
	was[id] = on ? 1 : 2;
	ui_menu_state (id, on ? 0 : UMS_DISABLED);
}
static void dd_show (long dd, int v) { if (val (dd) != v) { ui_w_set_value (dd, v); ui_w_draw (dd); } }
static void font_show ()
{
	const Style &st = cur_style ();
	const char *nm = g_b.fonts[st.font < g_b.nfonts ? st.font : 0];
	int n = (int) ui_list_count (g_font), at = -1;
	for (int i = 0; i < n; i++) if (ci_eq ((const char *) uikit_list_get (g_font, i, 0), nm)) at = i;
	if (at < 0) at = (int) ui_list_add (g_font, (long) nm, 0);
	dd_show (g_font, at);
	int si = -1; for (int i = 0; i < 23; i++) if (SIZE_VALS[i] == st.size) si = i;
	if (si >= 0) dd_show (g_size, si);
	int zi = -1; for (int i = 0; i < NZOOMS; i++) if (ZOOMS[i] == g_z) zi = i;
	if (zi >= 0) dd_show (g_zoom, zi);
}
static void refresh_now ()
{
	g_changed = false;
	Sheet *s = S ();
	const Style &st = cur_style ();
	tool_on (M_BOLD, st.bold); tool_on (M_ITALIC, st.italic); tool_on (M_UNDER, st.under != 0); tool_on (M_STRIKE, st.strike);
	tool_on (M_LEFT, st.ha == HA_LEFT); tool_on (M_CENTER, st.ha == HA_CENTER); tool_on (M_RIGHT, st.ha == HA_RIGHT);
	tool_on (M_WRAP, st.wrap); tool_on (M_MERGE, merge_at (s, s->curR, s->curC) >= 0); tool_on (M_FREEZE, s->freezeR || s->freezeC);
	tool_on (M_FILTER, s->af.on);
	// (grey only when there is nothing to do, the typing included: a grey entry has no shortcut)
	tool_enable (M_UNDO, g_undo.n != 0 || g_ed.on); tool_enable (M_REDO, g_redo.n != 0);
	menu_enable (M_UNDO, g_undo.n != 0 || g_ed.on); menu_enable (M_REDO, g_redo.n != 0);
	tool_enable (M_CANCEL, g_ed.on); tool_enable (M_ACCEPT, g_ed.on);
	font_show ();
	name_sync (); fline_sync (); tabs_sync (); title_sync ();
	// the status bar: the sheet, the mode, the selection's figures
	char l[64], m[160];
	static const char *const MODES[4] = { "Ready", "Enter", "Edit", "Point" };
	snprintf (l, sizeof l, TR ("Sheet %d of %d    %s"), g_b.active + 1, g_b.ns, TR (MODES[gv_mode ()]));
	if (changed_doc ()) { int n = (int) strlen (l); snprintf (l + n, sizeof l - n, "    %s", TR ("(modified)")); }
	m[0] = 0;
	Rect r = used_sel ();
	int mi = merge_at (s, r.r0, r.c0);
	bool oneCell = mi >= 0 && s->merges[mi].r1 == r.r1 && s->merges[mi].c1 == r.c1;	// (a merged cell)
	if (!g_ed.on && !oneCell && (r.r1 > r.r0 || r.c1 > r.c0))
	{
		double sum = 0; int cnt = 0, cnta = 0;
		int fmt = -1; bool mixed = false;				// (the numbers' format, when they share one)
		long long area = (long long) (r.r1 - r.r0 + 1) * (r.c1 - r.c0 + 1);
		if (area <= 2000000)
			for (int k = 0; k < s->cells.cap; k++)
			{
				Cell *x = s->cells.t[k];
				if (!x || !rect_has (r, x->r, x->c) || x->kind == K_NONE) continue;
				cnta++;
				if (x->vt != V_NUM) continue;
				sum += x->num; cnt++;
				int f = g_b.styles.s[x->style].fmt;
				if (fmt < 0) fmt = f; else if (f != fmt) mixed = true;
			}
		if (cnta)
		{
			// (the figures in the numbers' format when they all have the same one: 1,234.50 €)
			const char *code = mixed || fmt < 0 ? "General" : book_fmt_code (g_b, fmt);
			int fk = fmt_kind (code);
			if (fk == FK_TEXT || fk == FK_DATE || fk == FK_TIME || fk == FK_DATETIME) code = "General";
			Buf a, a2;
			fmt_number (code, sum, a, 0, 16); fmt_number (code, cnt ? sum / cnt : 0, a2, 0, 16);
			if (cnt) snprintf (m, sizeof m, TR ("Sum: %s    Average: %s    Count: %d"), a.str (), a2.str (), cnta);
			else snprintf (m, sizeof m, TR ("Count: %d"), cnta);
		}
	}
	if (!m[0] && g_path[0]) snprintf (m, sizeof m, "%s", base_name (g_path));
	status_set (l, m);
	ui_view_dirty (g_status);
}

// ---- the commands by number ------------------------------------------------------------------------------------
static void command (int id)
{
	switch (id)
	{
	case M_NEW: cmd_new (); break; case M_OPEN: cmd_open (); break; case M_SAVE: cmd_save (); break; case M_SAVEAS: cmd_save_as (); break;
	case M_EXPCSV: cmd_export_csv (); break; case M_EXPPDF: cmd_export_pdf (); break; case M_PRINT: cmd_print (); break;
	case M_UNDO: cmd_undo (); break; case M_REDO: cmd_redo (); break; case M_CUT: cmd_cut (); break; case M_COPY: cmd_copy (); break; case M_PASTE: cmd_paste (); break;
	case M_PASTESP: cmd_paste_special (); break; case M_CLEAR: cmd_clear_contents (); break; case M_FILLD: cmd_fill_down (); break; case M_FILLR: cmd_fill_right (); break;
	case M_SELALL: cmd_select_all (); break; case M_FIND: cmd_find (); break; case M_GOTO: cmd_goto (); return;
	case M_ZOOMIN: cmd_zoom_in (); break; case M_ZOOMOUT: cmd_zoom_out (); break; case M_ZOOM100: cmd_zoom_100 (); break;
	case M_FREEZE: cmd_freeze (); break; case M_GRIDL: cmd_gridlines (); break; case M_FORMULAS: cmd_formulas (); break;
	case M_INSROW: cmd_ins_rows (); break; case M_INSCOL: cmd_ins_cols (); break; case M_FUNC: cmd_function (); return; case M_NAMES: cmd_names (); break;
	case M_SUM: cmd_autosum (); return; case M_CHART: cmd_chart (); break; case M_INSSHEET: cmd_insert_sheet (); break; case M_DATE: cmd_date (); break; case M_TIME: cmd_time (); break;
	case M_FORMAT: cmd_format_cells (); break; case M_COND: cmd_cond_format (); break; case M_BOLD: cmd_bold (); break; case M_ITALIC: cmd_italic (); break;
	case M_UNDER: cmd_under (); break; case M_STRIKE: cmd_strike (); break; case M_MERGE: cmd_merge (); break; case M_WRAP: cmd_wrap (); break;
	case M_ROWH: cmd_row_height (); break; case M_COLW: cmd_col_width (); break; case M_OPTW: cmd_opt_width (); break; case M_HIDER: cmd_hide_rows (); break; case M_HIDEC: cmd_hide_cols (); break;
	case M_SHOW: cmd_show_all (); break; case M_CLRFMT: cmd_clear_format (); break;
	case M_NOFILL: apply_style (f_fill, (const void *) (unsigned long) AUTO); break; case M_AUTOCOL: apply_style (f_color, (const void *) (unsigned long) AUTO); break;
	case M_LEFT: cmd_left (); break; case M_CENTER: cmd_center (); break; case M_RIGHT: cmd_right (); break;
	case M_CURRENCY: drop_currency (); break; case M_PERCENT: cmd_percent (); break; case M_THOUSANDS: cmd_thousands (); break; case M_DECP: cmd_dec_plus (); break; case M_DECM: cmd_dec_minus (); break;
	case M_BORDERS: drop_borders (); break;
	case M_DELROW: cmd_del_rows (); break; case M_DELCOL: cmd_del_cols (); break; case M_DELSHEET: cmd_delete_sheet (); break; case M_RENSHEET: cmd_rename_sheet (); break;
	case M_DUPSHEET: cmd_duplicate_sheet (); break; case M_SHLEFT: cmd_sheet_left (); break; case M_SHRIGHT: cmd_sheet_right (); break;
	case M_SORTA: cmd_sort_asc (); break; case M_SORTD: cmd_sort_desc (); break; case M_SORT: cmd_sort (); break; case M_FILTER: cmd_autofilter (); break; case M_RECALC: cmd_recalc (); break;
	case M_ACCEPT: gv_commit (0, 0); break;
	case M_CANCEL: gv_cancel_edit (); break;
	}
	if (ui_w_get_focus () != g_name) gv_focus ();
	refresh ();
}

// ---- the formula bar's line, the Name Box --------------------------------------------------------------------
// The focus came to the line: the cell is edited there.
static void focus_turn ()
{
	long f = ui_w_get_focus ();
	if (f == g_flBefore) return;
	long was = g_flBefore;
	g_flBefore = f;
	if (f == g_fline && !g_ed.on) { gv_begin_edit (false, 0); g_ed.typed = false; }
	if (f == g_name) ui_textbox_select (g_name, 0, ui_w_text_len (g_name));
	if (was == g_fline || f == g_fline) ui_view_dirty (g_view);
	if (was == g_name) refresh ();
}
// The line's caret and selection into the editor.
static void fline_caret ()
{
	if (!fline_focused () || !g_ed.on) return;
	long e = 0, a = uikit_textbox_selection (g_fline, &e);
	if ((int) a == g_ed.s0 () && (int) e == g_ed.s1 ()) return;
	g_ed.anchor = iclamp ((int) a, 0, g_ed.t.n); g_ed.caret = iclamp ((int) e, 0, g_ed.t.n); g_ed.pointing = false;
	ui_view_dirty (g_view);
}
static void fline_typed ()
{
	if (!g_ed.on) { gv_begin_edit (false, 0); g_ed.typed = false; }
	g_ed.t.clear (); g_ed.t.puts (text_of (g_fline));
	g_ed.caret = g_ed.anchor = g_ed.t.n; g_ed.pointing = false;
	fline_caret ();
	g_acStart = -1;
	g_flValid = false;
	ui_view_dirty (g_view);
	refresh ();
}

// A control's event, out of ui_w_event.
static void control (long w, long code)
{
	if (gv_control (w, code)) return;
	if (w == g_fline)
	{
		if (code == UN_CHANGE) fline_typed ();
		else if (code == UN_ACTIVATE) { fline_caret (); if (gv_commit (1, 0)) gv_focus (); refresh (); }
		return;
	}
	if (w == g_name) { if (code == UN_ACTIVATE) { char t[80]; scpy (t, text_of (g_name), sizeof t); go_to (t); } return; }
	if (w == g_tabs)
	{
		if (code == UN_CHANGE) pick_sheet (val (g_tabs));
		else if (code == UN_NEW) { cmd_insert_sheet (); refresh (); }
		return;
	}
	if (code == UN_CHANGE)
	{
		if (w == g_font) { apply_style (f_font, (const void *) (long) book_font (g_b, (const char *) uikit_list_get (g_font, val (g_font), 0))); gv_focus (); return; }
		if (w == g_size) { apply_style (f_size, (const void *) (long) SIZE_VALS[iclamp (val (g_size), 0, 22)]); gv_focus (); return; }
		if (w == g_zoom) { set_zoom (ZOOMS[iclamp (val (g_zoom), 0, NZOOMS - 1)]); gv_focus (); return; }
		if (w == g_cText) { g_textColor = (unsigned) val (g_cText) & 0xFFFFFF; cmd_text_color (); gv_focus (); return; }
		if (w == g_cFill) { g_fillColor = (unsigned) val (g_cFill) & 0xFFFFFF; cmd_fill_color (); gv_focus (); return; }
	}
	if (code == UN_ACTIVATE) for (int i = 1; i < M_COUNT; i++) if (g_btn[i] == w) { command (i); return; }
}
// A key no widget took: the menus' shortcuts came first.
static void key (int hid, int mods)
{
	if (hid >= 0xE0) return;
	long f = ui_w_get_focus ();
	if (hid == KEY_ESC)
	{
		if (f == g_fline) { gv_cancel_edit (); g_flValid = false; gv_focus (); refresh (); }
		else if (f == g_name) { g_flBefore = 0; gv_focus (); refresh (); }
		return;
	}
	if (f == g_view && other_key (hid, mods)) return;
	if (hid == 0x42) cmd_recalc ();					// F9 (a shortcut of the menus too)
}

// ---- the window ------------------------------------------------------------------------------------------
static int g_gy;
static void layout (int w, int h)
{
	bars_layout (w, h);
	gv_move (0, g_gy, w, h - g_gy - TABS_H - STATUS_H);
}
static long on_resize (long w, long h)
{
	layout ((int) w, (int) h);
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_view_dirty (g_status);
	ui_w_draw (0);
	ui_update_all ();
	return 0;
}
static void build_window ()
{
	long scr[4];
	ui_screen ((long) scr);
	int w = imin (APP_W, (int) scr[0] - 8), h = imin (APP_H, (int) (scr[1] - scr[3] - scr[2]) - 32 - 96);
	ui_window_ex (0, scr[3], w, h, (long) TR ("Spreadsheet"), WF_RESIZABLE);
	ui_min_size (620, 420);
	ui_on_resize ((long) on_resize, 0);
	s_jet = (const unsigned *) ui_jetons ();
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	bars_create (w);
	g_gy = 3 * (ctl_h () + 6);
	gv_create (0, g_gy, w, h - g_gy - TABS_H - STATUS_H);
	bars_create2 (w, h);
	g_stZoom = grid_zoom;
	ui_menu ((long) MENUS);
	layout (w, h);
	tabs_sync ();
	gv_focus ();
	g_flBefore = g_view;
	refresh_now ();
	gv_bars ();
	gv_dirty ();
	ui_w_draw (0);
	ui_update_all ();
}

static int app_main (int ac, char **av)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/sheet.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	if (!documentkit_kit_open () || !archivekit_kit_open ()) return 1;
	mkdir ("SD0:/home", 0777); mkdir (HOME_DIR, 0777); mkdir (DOCS, 0777);
	g_now = now_clock;
	fn_init ();
	fonts_reset ();
	book_init (g_b);
	book_add_sheet (g_b, TR ("Sheet1"));
	build_window ();
	make_palettes ();

	// a file named on the command line, else the one kept at the last close, else the last one
	int n; char *b;
	if (ac > 1 && av[1][0]) load_path (av[1]);
	else if ((b = read_file (RECOVER, &n)) != 0)
	{
		free (b);
		if (note ("Spreadsheet", "The spreadsheet was closed with unsaved changes. Open the recovered workbook?", UM_YESNO) == 1)
		{
			const char *why = 0;
			if (book_load (g_b, RECOVER, &why))
			{
				int nl; char *nm = read_file (RECOVER_NAME, &nl);
				g_path[0] = 0;
				if (nm) { scpy (g_path, nm, sizeof g_path); free (nm); }
				loaded ();
				g_saved = g_gen + 1;				// (still unsaved)
			}
			else { book_fresh (); }
		}
		unlink (RECOVER); unlink (RECOVER_NAME);
	}
	else if ((b = read_file (LAST, &n)) != 0)
	{
		char p[256]; scpy (p, b, sizeof p); free (b);
		struct stat st;
		if (stat (p, &st) == 0) load_path (p);
	}
	refresh_now ();
	gv_focus ();

	for (;;)
	{
		long t = ui_event (40);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), mods = ui_ev_field (&ui, EV_MODS);
			// the right button on the sheets' tabs: their menu (no such thing in ui_tabstrip)
			bool tabMenu = t == WEV_MOUSE && ui_ev_field (&ui, EV_KIND) == 1 && (hid & 2) && ui_ev_field (&ui, EV_Y) >= ui.h - TABS_H - STATUS_H && ui_ev_field (&ui, EV_Y) < ui.h - STATUS_H;
			int tx = ui_ev_field (&ui, EV_X), ty = ui_ev_field (&ui, EV_Y);
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (tabMenu) tab_menu (tx, ty);
			pending ();
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id); else key (hid, mods);
			}
			if (t == WEV_DROP && !taken)
			{
				static char path[256]; long kind = 0;
				long dn = uikit_drop_get (path, sizeof path - 1, &kind);
				if (dn > 0 && kind == UDT_FILES)
				{
					path[dn] = 0;
					char *e = strchr (path, '\n'); if (e) *e = 0;
					struct stat st;
					if (stat (path, &st) == 0 && !S_ISDIR (st.st_mode) && doc_confirm ()) load_path (path);
				}
			}
		}
		else ui_w_idle ();
		gv_tick ();
		focus_turn ();
		fline_caret ();
		if (g_changed) { refresh_now (); }
		gv_bars ();
		ui_view_flush ();
	}

	// closed with unsaved changes: the workbook kept for the next start
	if (changed_doc ())
	{
		if (g_ed.on) gv_commit (0, 0);
		int nb; char *d = xlsx_write (g_b, &nb);
		if (d) { write_file (RECOVER, d, nb); free (d); }
		if (g_path[0]) write_file (RECOVER_NAME, g_path, (int) strlen (g_path));
	}
	return 0;
}

} // namespace ss

int main (int ac, char **av) { return ss::app_main (ac, av); }
