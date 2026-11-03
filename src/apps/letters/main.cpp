//
// letters -- Onyx's word processor Letters on ARMPI (src/apps/letters/README.md), in the way of AbiWord / Word: the document
// laid out on pages (A4 by default) and drawn by the app in a ui_view with UIKit's fonts, at any zoom; two toolbars, a ruler
// (the margins, the paragraph's indents, a table's columns dragged), a status bar (the file, the page, the words, the zoom).
// Tables (rows, columns, merged cells, lines, shading, a heading row), headers and footers (the first page's own), page
// numbers and fields, tab stops with leaders, a table of contents, the pages kept whole (widows, orphans, headings with
// their text), a mail merge (a Cardfile's records into a letter).
//
// The pieces: doc.h (the document, its formats, tables, fields, stories, undo), layout.h (lines, tables, pages), font.h
// (UIKit's fonts, the advances kept), edit.h (the selection, the edits, the tables' changes, the clipboard, find), view.h
// (the pages drawn, the mouse, the keys), ui.h (the tools' row, the ruler, the status bar), office.h (files, DocumentKit and
// ArchiveKit, images), fileio.h (RTF, text, HTML), docx.h (Word), odt.h (OpenDocument), dialogs.h (Font, Paragraph, Tabs,
// Page Setup, Page Numbers, Insert Table, Table Properties, Field, Find & Replace, Special Character, Date and Time, Word
// Count), merge.h (the mail merge: its data, its fields, its documents).
//
// Files: .rtf (the default), .docx (Word), .odt (OpenDocument): read and written with the formats; .txt (plain text);
// anything else read as text; File > Export writes HTML or text. "letters SD0:/home/documents/a.odt" opens it; "letters --merge
// JOB" makes a mail merge's documents (Cardfile asks it: merge.h). Closed with unsaved changes, the document is kept in
// SD0:/home/letters/recovered.rtf and offered back at the next start.
//
#include <string.h>
#include <unistd.h>

#include "app.h"
#include "fileio.h"
#include "docx.h"
#include "odt.h"
#include "dialogs.h"
#include "merge.h"

namespace wr {

#define W 1000
#define H 700
#define DOCK_H 96
static const char *RECOVER = APP_HOME "/recovered.rtf";
static const char *RECOVER_NAME = APP_HOME "/recovered.txt";

enum {	M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_EXPHTML, M_EXPTXT, M_EXPPDF, M_PAGESETUP, M_PRINT,
	M_UNDO = 20, M_REDO, M_CUT, M_COPY, M_PASTE, M_PASTEPLAIN, M_SELALL, M_FIND,
	M_ZOOM100 = 40, M_ZOOMW, M_ZOOMP, M_HF, M_MARKS, M_UNITS,
	M_PBREAK = 50, M_TABLE, M_IMAGE, M_SYMBOL, M_PNUM, M_DATE, M_FIELD, M_TOC,
	M_FONT = 60, M_PARA, M_TABS, M_BOLD, M_ITALIC, M_UNDER, M_STRIKE, M_SUPER, M_SUB, M_CLEARF, M_LEFT, M_CENTER, M_RIGHT, M_JUSTIFY, M_BULLETS, M_NUMBERS,
	M_AUTOCOL, M_NOHILITE, M_INDENT, M_OUTDENT,
	M_ROWSA = 90, M_ROWSB, M_COLSL, M_COLSR, M_DELR, M_DELC, M_DELT, M_MERGEC, M_SPLIT, M_EVEN, M_SELT, M_TPROPS,
	M_COUNT = 110, M_TOCUP, M_MAIL };

static const char MENUS[] =
	"MFile\n"
	"I1\tNew\tCtrl+N\n" "I2\tOpen...\tCtrl+O\n" "-\n" "I3\tSave\tCtrl+S\n" "I4\tSave As...\n" "I5\tExport as HTML...\n" "I6\tExport as Text...\n" "I7\tExport as PDF...\n" "-\n"
	"I8\tPage Setup...\n" "I9\tPrint...\tCtrl+P\n"
	"MEdit\n"
	"I20\tUndo\tCtrl+Z\n" "I21\tRedo\tCtrl+Y\n" "-\n" "I22\tCut\tCtrl+X\n" "I23\tCopy\tCtrl+C\n" "I24\tPaste\tCtrl+V\n" "I25\tPaste Unformatted\n" "-\n"
	"I26\tSelect All\tCtrl+A\n" "I27\tFind and Replace...\tCtrl+F\n"
	"MView\n"
	"I40\tActual Size (100%)\n" "I41\tPage Width\n" "I42\tWhole Page\n" "-\n" "I43\tHeader and Footer\n" "I44\tFormatting Marks\n" "I45\tRuler in Inches / Centimetres\n"
	"MInsert\n"
	"I50\tPage Break\n" "I51\tTable...\n" "I52\tImage...\n" "I53\tSpecial Character...\n" "-\n" "I54\tPage Numbers...\n" "I55\tDate and Time...\n" "I56\tField...\n" "I57\tTable of Contents\n"
	"MFormat\n"
	"I60\tFont...\tCtrl+D\n" "I61\tParagraph...\n" "I62\tTabs...\n" "-\n" "I63\tBold\tCtrl+B\n" "I64\tItalic\tCtrl+I\n" "I65\tUnderline\tCtrl+U\n" "I66\tStrikethrough\n"
	"I67\tSuperscript\n" "I68\tSubscript\n" "I69\tClear Formatting\n" "I76\tAutomatic Colour\n" "I77\tNo Highlight\n" "-\n"
	"I70\tAlign Left\tCtrl+L\n" "I71\tCentre\tCtrl+E\n" "I72\tAlign Right\tCtrl+R\n" "I73\tJustify\tCtrl+J\n" "-\n" "I74\tBullets\n" "I75\tNumbering\n" "I78\tIncrease Indent\n" "I79\tDecrease Indent\n"
	"MTable\n"
	"I90\tInsert Rows Above\n" "I91\tInsert Rows Below\n" "I92\tInsert Columns Left\n" "I93\tInsert Columns Right\n" "-\n" "I94\tDelete Rows\n" "I95\tDelete Columns\n" "I96\tDelete Table\n" "-\n"
	"I97\tMerge Cells\n" "I98\tSplit Cell\n" "I99\tDistribute Columns Evenly\n" "I100\tSelect Table\n" "I101\tTable Properties...\n"
	"MTools\n"
	"I110\tWord Count...\n" "I111\tUpdate Table of Contents\n" "I112\tMail Merge...\n";

static long g_tb1, g_tb2;
static long b_new, b_open, b_save, b_undo, b_redo, b_cut, b_copy, b_paste, b_find, b_marks, b_pbreak, b_table, b_symbol, b_image, b_zout, b_zoom, b_zin;
static long b_bold, b_italic, b_under, b_strike, b_super, b_sub, b_left, b_center, b_right, b_justify, b_bullets, b_numbers, b_outdent, b_indent;
static long g_style, g_font, g_size, g_color, g_hilite;
static Row  g_row1, g_row2;
static int  TB_H, g_rulerH;
static char g_path[200];			// "" : not saved yet
static unsigned g_saved;			// the document's edit count at its last save
static bool g_countDirty = true; static unsigned g_countT; static int g_words;
static bool g_fitWidth;				// the page follows the window's width (a small window; Page Width)
static char g_fontName[32][48]; static int g_nfontName;	// the font list's names
static int  s_fontShown = -1;				// (the font it shows)

static bool changed_doc () { return g_doc.changes != g_saved; }
static void focus_view () { if (g_view) ui_w_focus (g_view); }
static void after_edit () { if (g_view) { ensureVisible (); wake (); } refresh (); focus_view (); }
static void show_page () { refresh (); ensureVisible (); ui_view_dirty (g_view); bars_sync (); ui_view_flush (); }

static const char *base_name (const char *p)
{
	const char *b = p;
	for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1;
	return b;
}
static bool has_ext (const char *p, const char *ext)
{
	int n = slen (p), e = slen (ext);
	if (n < e) return false;
	for (int i = 0; i < e; i++) if (lower ((unsigned char) p[n - e + i]) != lower ((unsigned char) ext[i])) return false;
	return true;
}
static bool rich_ext (const char *p) { return has_ext (p, ".rtf") || has_ext (p, ".docx") || has_ext (p, ".odt"); }

// ---- the font list -----------------------------------------------------------------------------------------
static void fonts_fill ()
{
	g_nfontName = 0; s_fontShown = -1;
	ui_list_clear (g_font);
	auto add = [&] (const char *n) {
		for (int i = 0; i < g_nfontName; i++) if (!sicmp (g_fontName[i], n)) return;
		if (g_nfontName >= 32) return;
		scpy (g_fontName[g_nfontName++], n, 48);
		ui_list_add (g_font, (long) n, 0);
	};
	for (int i = 0; i < NFONTS; i++) add (FONT_LIST[i]);
	for (int i = 0; i < g_doc.nfont; i++) add (g_doc.fontName[i]);
}

// ---- files -----------------------------------------------------------------------------------------------
static void doc_loaded ()
{
	L.d = &g_doc;
	layout_reset_fonts ();
	doc_dirty_all (g_doc);
	g_caret = g_anchor = mkpos (0, 0); g_atEnd = false; g_typeCf = -1; g_goalX = -1;
	g_bodyCaret = g_bodyAnchor = mkpos (0, 0); L.hfPage = 0;
	g_relayout = true;
	g_saved = g_doc.changes;
	g_countDirty = true;
	merge_doc_loaded ();
	fonts_fill ();
	sx = sy = 0;
	if (g_view) ui_view_dirty (g_view);
	refresh ();
}
static bool load_path (const char *path)
{
	int n = 0; char *b = file_read (path, &n);
	if (!b) { say ("Open", "The file could not be read."); return false; }
	bool ok = doc_from_bytes (g_doc, b, n);
	delete[] b;
	if (!ok) { doc_new (g_doc); say ("Open", "The document could not be read."); }
	if (g_doc.n == 0 && g_doc.cur == SY_BODY) doc_new (g_doc);
	scpy (g_path, ok ? path : "", sizeof g_path);
	doc_loaded ();
	return ok;
}
static bool write_path (const char *path, bool asCopy = false)
{
	char *b = 0; unsigned n = 0;
	bool ok = doc_bytes (g_doc, path, &b, &n) && file_write (path, b, n);
	delete[] b;
	if (!ok) { say ("Save", "The file could not be written."); return false; }
	if (!asCopy) { scpy (g_path, path, sizeof g_path); g_saved = g_doc.changes; }
	refresh ();
	return true;
}
static void add_ext (char *path, int cap, const char *ext) { int k = slen (path); scpy (path + k, ext, cap - k); }
static void cmd_save_as ()
{
	char path[200] = "", def[80];
	scpy (def, g_path[0] ? base_name (g_path) : TR ("Untitled.rtf"), sizeof def);
	if (g_path[0] && !rich_ext (def) && !has_ext (def, ".txt"))
	{
		int n = slen (def); while (n > 0 && def[n - 1] != '.') n--;
		if (n > 0) def[n - 1] = 0;
		add_ext (def, sizeof def, ".rtf");
	}
	ui_file_filter ((long) "*.rtf;*.docx;*.odt;*.txt");
	long ok = ui_file_save ((long) DOCS_DIR, (long) def, (long) path, sizeof path);
	ui_file_filter (0);
	if (ok)
	{
		if (!rich_ext (path) && !has_ext (path, ".txt")) add_ext (path, sizeof path, ".rtf");
		write_path (path);
	}
	focus_view ();
}
static void cmd_save ()
{
	if (!g_path[0] || !(rich_ext (g_path) || has_ext (g_path, ".txt"))) { cmd_save_as (); return; }
	if (has_ext (g_path, ".txt"))					// (formats would be lost: only when plain)
	{
		bool plain = g_doc.ntbl == 0;
		CharFmt n0 = style_fmt (g_doc, ST_NORMAL);
		for (int i = 0; i < g_doc.nfmt && plain; i++) { const CharFmt &f = g_doc.fmt[i]; if (f.flags || f.color != AUTO || f.hilite != AUTO || (f.font != n0.font && f.size != n0.size)) plain = false; }
		if (!plain && ui_message ((long) TR ("Save"), (long) TR ("Plain text keeps no formats (bold, fonts, colours, tables...). Save as text anyway?"), UM_YESNO) != 1) { cmd_save_as (); return; }
	}
	write_path (g_path);
	focus_view ();
}
// Unsaved changes: saved, dropped, or the command given up.
static bool confirm ()
{
	if (!changed_doc ()) return true;
	char t[260]; snprintf (t, sizeof t, TR ("Save the changes to %s?"), g_path[0] ? base_name (g_path) : TR ("Untitled"));
	long r = ui_message ((long) TR ("Unsaved changes"), (long) t, UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { cmd_save (); return !changed_doc (); }
	return true;
}
static void cmd_new ()
{
	if (!confirm ()) { focus_view (); return; }
	doc_new (g_doc);
	g_path[0] = 0;
	doc_loaded ();
	focus_view ();
}
static void cmd_open ()
{
	if (!confirm ()) { focus_view (); return; }
	char path[200] = "";
	ui_file_filter ((long) "*.rtf;*.docx;*.odt;*.txt");
	long ok = ui_file_open ((long) DOCS_DIR, (long) path, sizeof path);
	ui_file_filter (0);
	if (ok) load_path (path);
	focus_view ();
}
static void cmd_export (const char *ext)
{
	char path[200] = "", def[80];
	scpy (def, g_path[0] ? base_name (g_path) : TR ("Untitled"), sizeof def);
	int n = slen (def), dot = n; while (dot > 0 && def[dot - 1] != '.') dot--;
	if (dot > 0) def[dot - 1] = 0;
	add_ext (def, sizeof def, ext);
	if (ui_file_save ((long) DOCS_DIR, (long) def, (long) path, sizeof path))
	{
		if (!has_ext (path, ext)) add_ext (path, sizeof path, ext);
		write_path (path, true);
	}
	focus_view ();
}
// ---- File > Export as PDF (pdfwrite.h: the pages at 100 %, UIKit's fonts embedded as subsets) ------------------------
static int utf8_put (char *o, unsigned c) { return tf_utf8 (c, o); }
// File > Print: the Print dialog (PrinterKit's, the same in every app), then the pages drawn into the job by the code that
// draws them into a PDF (pdfprint.h); the print service does the rest.
static void cmd_print ()
{
	if (g_doc.cur != SY_BODY) ed_story (SY_BODY);
	char name[200]; scpy (name, g_path[0] ? base_name (g_path) : TR ("Untitled"), sizeof name);
	long j = print_ask (name, L.npages, pageAt (viewH () / 2) + 1, L.d->page.w / 20.0f, L.d->page.h / 20.0f);
	if (!j) { focus_view (); return; }
	int zoom = L.zoom;					// (the pages at 100 %, as the PDF's)
	set_zoom (100); g_relayout = true; relayout ();
	{ PrintWriter w (j); exportPages (w, 0, L.npages - 1); }
	long id = print_end (j);
	set_zoom (zoom); g_relayout = true; relayout ();
	if (id < 0) say ("Print", "The document could not be put in the print queue.");
	focus_view ();
}
static void cmd_export_pdf ()
{
	if (g_doc.cur != SY_BODY) ed_story (SY_BODY);
	char def[200], title[120], author[80] = "", range[32] = "";
	scpy (def, g_path[0] ? base_name (g_path) : TR ("Untitled"), sizeof def);
	{ int n = slen (def), dot = n; while (dot > 0 && def[dot - 1] != '.') dot--; if (dot > 0) def[dot - 1] = 0; }
	scpy (title, def, sizeof title);
	add_ext (def, sizeof def, ".pdf");
	int cur = pageAt (viewH () / 2);
	enum { DW = 480, DH = 300 };
	long d = ui_dialog_begin (DW, DH, (long) "Export as PDF");
	int c = ctl_h () + 4, y = 12;
	char b[48]; snprintf (b, sizeof b, TR ("The current page (%d)"), cur + 1);
	long rAll = d_radio (16, y, 300, "All the pages", 1, true); y += c;
	long rCur = d_radio (16, y, 300, b, 1, false); y += c;
	long rRng = d_radio (16, y, 90, "Pages", 1, false); long tR = d_field (110, y, 120, ""); y += c;
	long cM = d_check (16, y, 340, "Bookmarks from the headings", true); y += c;
	d_label (16, y, 70, "Title:"); long tT = d_field (90, y, 374, title); y += c;
	d_label (16, y, 70, "Author:"); long tA = d_field (90, y, 374, "");
	d_ok_cancel (d, DW);
	(void) rAll;
	long r = ui_dialog_run (d, 0, 0);
	int mode = wval (rCur) ? 1 : wval (rRng) ? 2 : 0; bool marks = wval (cM) != 0;
	scpy (range, wtext (tR), sizeof range); scpy (title, wtext (tT), sizeof title); scpy (author, wtext (tA), sizeof author);
	ui_dialog_end (d);
	if (r != 1) { focus_view (); return; }
	int from = 0, to = 1 << 20;
	if (mode == 1) from = to = cur;
	if (mode == 2)
	{
		int a = atoi (range); const char *e = strchr (range, '-'); int z = e ? atoi (e + 1) : a;
		if (a < 1 || z < a) { say ("Export as PDF", "Type the pages as 2-5 (or one page: 3)."); focus_view (); return; }
		from = a - 1; to = z - 1;
	}
	char path[200] = "";
	ui_file_filter ((long) "*.pdf");
	long ok = ui_file_save ((long) DOCS_DIR, (long) def, (long) path, sizeof path);
	ui_file_filter (0);
	if (!ok) { focus_view (); return; }
	if (!has_ext (path, ".pdf")) add_ext (path, sizeof path, ".pdf");
	int zoom = L.zoom;					// (the pages at 100 %: a px is 0.75 point)
	set_zoom (100); g_relayout = true; relayout ();
	if (from >= L.npages) from = L.npages - 1;
	if (to >= L.npages) to = L.npages - 1;
	pdfw::Writer w;
	w.info (title, author, 0, "Letters (Korin OS)");
	exportPages (w, from, to);
	if (marks)
	{
		int n; Para **bp = story_p (g_doc, SY_BODY, &n);
		for (int i = 0; i < n; i++)
		{
			Para *q = bp[i];
			int lv = style_outline (q->pf.style);
			if (!lv || !q->nln || q->ln[0].page < from || q->ln[0].page > to) continue;
			char t[200]; int k = 0;
			for (int ci = 0; ci < q->len && k < 190; ci++) { unsigned ch = q->ch[ci]; if (ch < ' ' || ch == OBJ_CHAR || ch == FIELD_CHAR) ch = ' '; k += utf8_put (t + k, ch); }
			t[k] = 0;
			if (k) w.outline (lv - 1, t, q->ln[0].page - from, (q->ln[0].py64 >> 6) * PX2PT);
		}
	}
	unsigned len; unsigned char *pdf = w.finish (&len);
	set_zoom (zoom); g_relayout = true; relayout (); ui_view_dirty (g_view);
	if (!pdf || !file_write (path, pdf, len)) say ("Export as PDF", "The PDF could not be written there.");
	delete[] pdf;
	focus_view ();
}

// ---- edits, formats -----------------------------------------------------------------------------------------
static void cmd_select_all () { set_caret (mkpos (0, 0), false); set_caret (doc_end (g_doc), true); ui_view_dirty (g_view); focus_view (); }
static void toggle (unsigned short f) { ed_toggle (f); after_edit (); }
static void align (int a) { ed_para (pf_align, a); after_edit (); }
static void apply_color (unsigned c) { CfChange ch; ch.what = CH_COLOR; ch.color = c; ed_format (ch); after_edit (); }
static void apply_hilite (unsigned c) { CfChange ch; ch.what = CH_HILITE; ch.hilite = c; ed_format (ch); after_edit (); }
static void cmd_image ()
{
	char path[200] = "";
	ui_file_filter ((long) "*.png;*.bmp;*.jpg;*.jpeg");
	long ok = ui_file_open ((long) "SD0:/", (long) path, sizeof path);
	ui_file_filter (0);
	if (ok && !ed_insert_image (path, g_doc.page.w - g_doc.page.left - g_doc.page.right))
		say ("Insert Image", "That file is not an image Letters can read (PNG, BMP, JPEG).");
	after_edit ();
}
static void set_style (int st) { ed_para (pf_style, st); after_edit (); }
// The header / footer of the page at the caret edited (Esc, or a double click on the body: back).
static void cmd_header_footer ()
{
	if (g_doc.cur != SY_BODY) { ed_story (SY_BODY); after_edit (); return; }
	const Para *q = g_doc.p[g_caret.p];
	L.hfPage = q->nln ? q->ln[line_of (q, g_caret.o, g_atEnd)].page : 0;
	ed_story (hf_story (false, L.hfPage));
	after_edit ();
}
static bool in_tbl () { return g_doc.cur == SY_BODY && in_table (g_doc.p[sel_a ().p]); }
static void table_cmd (void (*fn) ()) { if (in_tbl ()) fn (); after_edit (); }
static void rows_above () { ed_table_rows (false); }
static void rows_below () { ed_table_rows (true); }
static void cols_left () { ed_table_cols (false); }
static void cols_right () { ed_table_cols (true); }
static void sel_table () { ed_table_select (SEL_TABLE); }

// ---- the zoom --------------------------------------------------------------------------------------------
static const int ZOOMS[] = { 25, 50, 75, 100, 125, 150, 200, 300, 400 };
static void zoom_to (int z)
{
	g_fitWidth = false;				// (a zoom chosen: kept)
	if (z == L.zoom) return;
	set_zoom (z);
	g_relayout = true;
	ensureVisible ();
	ui_view_dirty (g_view); ui_view_dirty (g_ruler); ui_view_dirty (g_status);
	refresh ();
	focus_view ();
}
static void zoom_by (int dir)
{
	int n = (int) (sizeof ZOOMS / sizeof ZOOMS[0]), z = L.zoom;
	if (dir > 0) { for (int i = 0; i < n; i++) if (ZOOMS[i] > z) { zoom_to (ZOOMS[i]); return; } }
	else { for (int i = n - 1; i >= 0; i--) if (ZOOMS[i] < z) { zoom_to (ZOOMS[i]); return; } }
}
static void cmd_zoom_width (bool keep = false) { int avail = viewW () - 2 * GAP; zoom_to (wclamp ((int) ((long long) avail * 1500 / g_doc.page.w), 10, 500)); g_fitWidth = keep; }
static void cmd_zoom_page ()
{
	int aw = viewW () - 2 * GAP, ah = viewH () - 2 * GAP;
	int zw = (int) ((long long) aw * 1500 / g_doc.page.w), zh = (int) ((long long) ah * 1500 / g_doc.page.h);
	zoom_to (wclamp (wmin (zw, zh), 10, 500));
}
static void pick_zoom ()
{
	static const char Z[] = "-\00025%\00050%\00075%\000100%\000125%\000150%\000200%\000300%\000400%\0";
	char T[200]; int n = snprintf (T, 80, "%s", TR ("Page Width")) + 1;
	n += snprintf (T + n, 80, "%s", TR ("Whole Page")) + 1;
	memcpy (T + n, Z, sizeof Z);
	long r = ui_popup_local (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y) + 8, (long) T, 130);
	if (r == 0) cmd_zoom_width (true); else if (r == 1) cmd_zoom_page (); else if (r >= 3) zoom_to (ZOOMS[r - 3]); else focus_view ();
}

// ---- the toolbar's lists -------------------------------------------------------------------------------------
static void pick_size (int hp) { CfChange c; c.what = CH_SIZE; c.size = (short) hp; ed_format (c); after_edit (); }
static void size_typed ()
{
	long h;
	if (parse_num (wtext (g_size), &h) && h >= 100 && h <= 163800) pick_size ((int) ((h * 2 + 50) / 100)); else focus_view ();
}

// ---- the commands ----------------------------------------------------------------------------------------
static void key_to_focus (int hid);
static void command (int id)
{
	switch (id)
	{
	case M_NEW: cmd_new (); break;
	case M_OPEN: cmd_open (); break;
	case M_SAVE: cmd_save (); break;
	case M_SAVEAS: cmd_save_as (); break;
	case M_EXPHTML: cmd_export (".html"); break;
	case M_EXPTXT: cmd_export (".txt"); break;
	case M_EXPPDF: cmd_export_pdf (); break;
	case M_PRINT: cmd_print (); break;
	case M_PAGESETUP: if (dlg_page_setup ()) { g_relayout = true; ui_view_dirty (g_view); ui_view_dirty (g_ruler); after_edit (); } else focus_view (); break;
	case M_UNDO: ed_undo (); after_edit (); break;
	case M_REDO: ed_redo (); after_edit (); break;
	case M_CUT: if (ui_w_get_focus () == g_view) { ed_cut (); after_edit (); } else key_to_focus (0x1B); break;
	case M_COPY: if (ui_w_get_focus () == g_view) { ed_copy (); focus_view (); } else key_to_focus (0x06); break;
	case M_PASTE: if (ui_w_get_focus () == g_view) { ed_paste (false); after_edit (); } else key_to_focus (0x19); break;
	case M_PASTEPLAIN: ed_paste (true); after_edit (); break;
	case M_SELALL: cmd_select_all (); break;
	case M_FIND: dlg_find (); after_edit (); break;
	case M_ZOOM100: zoom_to (100); break;
	case M_ZOOMW: cmd_zoom_width (true); break;
	case M_ZOOMP: cmd_zoom_page (); break;
	case M_HF: cmd_header_footer (); break;
	case M_MARKS: g_showMarks = !g_showMarks; ui_w_set_value (b_marks, g_showMarks); ui_menu_state (M_MARKS, g_showMarks ? UMS_CHECKED : 0); ui_view_dirty (g_view); focus_view (); break;
	case M_UNITS: g_inches = !g_inches; ui_view_dirty (g_ruler); focus_view (); break;
	case M_PBREAK: ed_page_break (); after_edit (); break;
	case M_TABLE: if (!in_tbl () && dlg_insert_table ()) after_edit (); else focus_view (); break;
	case M_IMAGE: cmd_image (); break;
	case M_SYMBOL: dlg_symbol (); after_edit (); break;
	case M_PNUM: if (dlg_page_numbers ()) { g_doc.changes++; after_edit (); } else focus_view (); break;
	case M_DATE: dlg_datetime (); after_edit (); break;
	case M_FIELD: dlg_field (); after_edit (); break;
	case M_TOC: ed_toc (false); after_edit (); break;
	case M_FONT: if (dlg_font ()) after_edit (); else focus_view (); break;
	case M_PARA: if (dlg_paragraph ()) after_edit (); else focus_view (); break;
	case M_TABS: if (dlg_tabs ()) after_edit (); else focus_view (); break;
	case M_BOLD: toggle (CF_BOLD); break;
	case M_ITALIC: toggle (CF_ITALIC); break;
	case M_UNDER: toggle (CF_UNDER); break;
	case M_STRIKE: toggle (CF_STRIKE); break;
	case M_SUPER: toggle (CF_SUPER); break;
	case M_SUB: toggle (CF_SUB); break;
	case M_CLEARF: ed_clear_format (); after_edit (); break;
	case M_AUTOCOL: apply_color (AUTO); break;
	case M_NOHILITE: apply_hilite (AUTO); break;
	case M_LEFT: align (AL_LEFT); break;
	case M_CENTER: align (AL_CENTER); break;
	case M_RIGHT: align (AL_RIGHT); break;
	case M_JUSTIFY: align (AL_JUSTIFY); break;
	case M_BULLETS: ed_para (pf_list, LS_BULLET); after_edit (); break;
	case M_NUMBERS: ed_para (pf_list, LS_NUMBER); after_edit (); break;
	case M_INDENT: ed_para (pf_indent, 1); after_edit (); break;
	case M_OUTDENT: ed_para (pf_indent, -1); after_edit (); break;
	case M_ROWSA: table_cmd (rows_above); break;
	case M_ROWSB: table_cmd (rows_below); break;
	case M_COLSL: table_cmd (cols_left); break;
	case M_COLSR: table_cmd (cols_right); break;
	case M_DELR: table_cmd (ed_table_del_rows); break;
	case M_DELC: table_cmd (ed_table_del_cols); break;
	case M_DELT: table_cmd (ed_table_delete); break;
	case M_MERGEC: table_cmd (ed_table_merge); break;
	case M_SPLIT: table_cmd (ed_table_split); break;
	case M_EVEN: table_cmd (ed_table_even); break;
	case M_SELT: table_cmd (sel_table); break;
	case M_TPROPS: if (in_tbl () && dlg_table_props ()) after_edit (); else focus_view (); break;
	case M_COUNT: dlg_word_count (); focus_view (); break;
	case M_TOCUP: ed_toc (true); after_edit (); break;
	case M_MAIL: dlg_mail_merge (); after_edit (); break;
	}
}
// (the menus' Cut, Copy, Paste for the field that has the focus)
static void key_to_focus (int hid)
{
	int e[8] = { WEV_KEY, 1, 0, 0, hid, 0, UI_MOD_CTRL, ui_ev_field (&ui, EV_WIN) };
	uikit_call3 ((long (*) ()) ui_w_event, (long) e, 0, 0, 0, 0, 0);
}

// ---- the context menu ------------------------------------------------------------------------------------
static void context_menu ()
{
	int a, b, head;
	bool tbl = in_tbl (), toc = g_doc.cur == SY_BODY && toc_find (&a, &b, &head) && sel_a ().p >= (head >= 0 ? head : a) && sel_a ().p < b;
	static int ids[24]; int nid = 0; char t[400]; int n = 0;
	auto add = [&] (const char *s, int id, bool on = true) {
		if (id && !on) t[n++] = 1;
		for (s = TR (s); *s; s++) t[n++] = *s;
		t[n++] = 0;
		ids[nid++] = id;
	};
	add ("Cut", M_CUT, has_sel ()); add ("Copy", M_COPY, has_sel ()); add ("Paste", M_PASTE); add ("-", 0);
	add ("Font...", M_FONT); add ("Paragraph...", M_PARA);
	if (tbl)
	{
		add ("-", 0);
		add ("Insert Row Below", M_ROWSB); add ("Insert Column Right", M_COLSR); add ("Delete Rows", M_DELR); add ("Delete Columns", M_DELC);
		add ("Merge Cells", M_MERGEC, has_sel ()); add ("Split Cell", M_SPLIT); add ("Table Properties...", M_TPROPS);
	}
	else if (toc) { add ("-", 0); add ("Update Table of Contents", M_TOCUP); }
	else if (g_doc.cur != SY_BODY) { add ("-", 0); add ("Close Header and Footer", M_HF); }
	else { add ("Bullets", M_BULLETS); add ("Numbering", M_NUMBERS); }
	add ("-", 0);
	add ("Select All", M_SELALL);
	t[n] = 0;
	long r = ui_popup_local (ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), (long) t, 210);
	if (r >= 0 && r < nid && ids[r]) command (ids[r]); else focus_view ();
}

// ---- the controls' states --------------------------------------------------------------------------------
static void set_on (long w, bool on) { if ((wval (w) != 0) != on) ui_w_set_value (w, on); }
static void set_en (long w, bool on, int *was) { if (*was != (int) on) { *was = on; ui_w_enable (w, on); } }
static void refresh ()
{
	if (!g_view) return;
	relayout ();
	CharFmt f = caret_fmt ();
	const Para *q = g_doc.p[sel_a ().p];
	static int eU = -1, eR = -1, eC = -1, eK = -1, eT = -1, eMU = -1, eMR = -1, sSt = -1, sSz = -1, sZ = -1;
	set_on (b_bold, f.flags & CF_BOLD); set_on (b_italic, f.flags & CF_ITALIC); set_on (b_under, f.flags & CF_UNDER);
	set_on (b_strike, f.flags & CF_STRIKE); set_on (b_super, f.flags & CF_SUPER); set_on (b_sub, f.flags & CF_SUB);
	set_on (b_left, q->pf.align == AL_LEFT); set_on (b_center, q->pf.align == AL_CENTER);
	set_on (b_right, q->pf.align == AL_RIGHT); set_on (b_justify, q->pf.align == AL_JUSTIFY);
	set_on (b_bullets, q->pf.list == LS_BULLET); set_on (b_numbers, q->pf.list == LS_NUMBER);
	set_en (b_undo, g_doc.uptr > 0, &eU); set_en (b_redo, g_doc.uptr < g_doc.nundo, &eR);
	set_en (b_cut, has_sel (), &eC); set_en (b_copy, has_sel (), &eK);
	set_en (b_table, !in_tbl () && g_doc.cur == SY_BODY, &eT);
	if (eMU != (g_doc.uptr > 0)) { eMU = g_doc.uptr > 0; ui_menu_state (M_UNDO, eMU ? 0 : UMS_DISABLED); }
	if (eMR != (g_doc.uptr < g_doc.nundo)) { eMR = g_doc.uptr < g_doc.nundo; ui_menu_state (M_REDO, eMR ? 0 : UMS_DISABLED); }
	if (sSt != q->pf.style) { sSt = q->pf.style; ui_w_set_value (g_style, sSt); }
	if (s_fontShown != f.font)
	{
		s_fontShown = f.font;
		for (int i = 0; i < g_nfontName; i++) if (!sicmp (g_fontName[i], g_doc.fontName[f.font])) ui_w_set_value (g_font, i);
	}
	if (sSz != f.size && (sSz < 0 || ui_w_get_focus () == g_view))
	{
		sSz = f.size;
		char b[16]; sprintf (b, "%d%s", f.size / 2, f.size & 1 ? ".5" : "");
		ui_w_set_text (g_size, (long) b);
	}
	if (sZ != L.zoom) { sZ = L.zoom; char b[12]; sprintf (b, "%d%%", L.zoom); ui_w_set_text (b_zoom, (long) b); }
	ui_view_dirty (g_ruler);
	// the status bar: the file, the page, the words
	char l[220], m[100];
	snprintf (l, sizeof l, "%s%s%s", g_path[0] ? base_name (g_path) : TR ("Untitled"), changed_doc () ? "  " : "", changed_doc () ? TR ("(modified)") : "");
	int page = L.hfPage + 1;
	if (g_doc.cur == SY_BODY) { const Para *cq = g_doc.p[g_caret.p]; page = cq->nln ? cq->ln[line_of (cq, g_caret.o, g_atEnd)].page + 1 : 1; }
	snprintf (m, sizeof m, TR ("%s%sPage %d of %d     Words: %d"), g_doc.cur != SY_BODY ? TR (STORY_NAMES[g_doc.cur]) : "", g_doc.cur != SY_BODY ? " - " : "", page + g_doc.page.start - 1, L.npages, g_words);
	status_set (l, m);
	g_countDirty = true;
}

// ---- the window ------------------------------------------------------------------------------------------
static void layout (int w, int h)
{
	int c = ctl_h ();
	ui_w_move (g_tb1, 0, 0, w, TB_H); ui_w_move (g_tb2, 0, TB_H, w, TB_H);
	row_place (g_row1, 4, c, w); row_place (g_row2, TB_H + 4, c, w);
	bool small = w < 720;
	g_rulerH = small ? 0 : RULER_H;
	ui_w_show (g_ruler, !small);
	int vy = 2 * TB_H + g_rulerH;
	ui_w_move (g_ruler, 0, 2 * TB_H, w, RULER_H);
	ui_w_move (g_view, 0, vy, w, h - vy - STATUS_H);
	ui_w_move (g_status, 0, h - STATUS_H, w, STATUS_H);
	s_x = 0; s_y = vy; s_w = w; s_h = h - vy - STATUS_H;
	ui_view_dirty (g_view); ui_view_dirty (g_ruler); ui_view_dirty (g_status);
}
static void on_resize (long w, long h)
{
	layout ((int) w, (int) h);
	if (g_fitWidth) cmd_zoom_width (true);
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_view_dirty (g_view);
	ui_w_draw (0);
	ui_update_all ();
}
static long tool (long bar, Row &r, const char *glyph, const char *tip, int fl, int prio)
{
	long b = ui_toolbar_add (bar, (long) glyph, 0, fl);
	ui_w_tip (b, (long) tip);
	row_add (r, b, ctl_h (), prio, fl & UTB_SEP ? 12 : 2);
	return b;
}
static void build_window ()
{
	s_jet = (const unsigned *) ui_jetons ();
	TB_H = ctl_h () + 8;
	int w = (int) ui.w, h = (int) ui.h;
	ui_fill (0, 0, w, h, jet (J_FACE));
	g_tb1 = ui_toolbar (0, 0, w, TB_H);
	g_tb2 = ui_toolbar (0, TB_H, w, TB_H);
	Row &r = g_row1, &s = g_row2;
	long t = g_tb1;
	b_new = tool (t, r, "\xE2\x98\x90", "New document (Ctrl+N)", 0, 3);
	b_open = tool (t, r, "\xE2\x87\xA7", "Open... (Ctrl+O)", 0, 2);
	b_save = tool (t, r, "\xE2\x87\xA9", "Save (Ctrl+S)", 0, 1);
	b_undo = tool (t, r, "\xE2\x86\xB6", "Undo (Ctrl+Z)", UTB_SEP, 1);
	b_redo = tool (t, r, "\xE2\x86\xB7", "Redo (Ctrl+Y)", 0, 2);
	b_cut = tool (t, r, "\xE2\x9C\x82", "Cut (Ctrl+X)", UTB_SEP, 3);
	b_copy = tool (t, r, "\xE2\x8A\x9E", "Copy (Ctrl+C)", 0, 3);
	b_paste = tool (t, r, "\xE2\x96\xA3", "Paste (Ctrl+V)", 0, 3);
	b_find = tool (t, r, "\xE2\x8C\x95", "Find and Replace (Ctrl+F)", UTB_SEP, 2);
	b_marks = tool (t, r, "\xC2\xB6", "Formatting marks", UTB_TOGGLE, 3);
	b_pbreak = tool (t, r, "\xE2\x86\xA1", "Page break", UTB_SEP, 3);
	b_table = tool (t, r, "\xE2\x96\xA6", "Insert a table", 0, 3);
	b_symbol = tool (t, r, "\xCE\xA9", "Special character...", 0, 3);
	b_image = tool (t, r, "\xE2\x96\xA8", "Insert an image...", 0, 3);
	b_zout = tool (t, r, "\xE2\x88\x92", "Zoom out", UTB_SEP, 2);
	b_zoom = ui_toolbar_add (t, 0, (long) "100%", 0); ui_w_tip (b_zoom, (long) "Zoom"); row_add (r, b_zoom, 60, 2);
	b_zin = tool (t, r, "+", "Zoom in", 0, 2);
	t = g_tb2;
	int c = ctl_h ();
	char items[300]; items[0] = 0;
	static const char *const NAMES[ST_COUNT] = { "Normal", "Heading 1", "Heading 2", "Heading 3", "Title", "Subtitle", "Quote", "Plain Text",
						     "Contents 1", "Contents 2", "Contents 3", "Contents Heading", "Header", "Footer" };
	for (int i = 0; i < ST_COUNT; i++) { if (i) strcat (items, "|"); strcat (items, TR (NAMES[i])); }
	g_style = ui_dropdown (0, 4, 130, c, (long) items); ui_w_set_parent (g_style, t); ui_w_tip (g_style, (long) "Paragraph style");
	row_add (s, g_style, 130, 1, 0, 88);
	g_font = ui_dropdown (0, 4, 170, c, (long) "Liberation Serif"); ui_w_set_parent (g_font, t); ui_w_tip (g_font, (long) "Font");
	row_add (s, g_font, 170, 2, 6, 100);
	g_size = ui_combobox (0, 4, 76, c, (long) "8|9|10|11|12|14|16|18|20|22|24|26|28|36|48|72"); ui_w_set_parent (g_size, t); ui_w_tip (g_size, (long) "Font size");
	row_add (s, g_size, 76, 1, 6);
	b_bold = tool (t, s, "B", "Bold (Ctrl+B)", UTB_TOGGLE | UTB_SEP, 1);
	b_italic = tool (t, s, "I", "Italic (Ctrl+I)", UTB_TOGGLE, 1);
	b_under = tool (t, s, "U", "Underline (Ctrl+U)", UTB_TOGGLE, 1);
	b_strike = tool (t, s, "S", "Strikethrough", UTB_TOGGLE, 3);
	b_super = tool (t, s, "x\xC2\xB2", "Superscript", UTB_TOGGLE, 4);
	b_sub = tool (t, s, "x\xE2\x82\x82", "Subscript", UTB_TOGGLE, 4);
	g_color = ui_colorpicker (0, 4, 30, c, 0xC00000); ui_w_set_parent (g_color, t); ui_w_tip (g_color, (long) "Text colour"); row_add (s, g_color, 30, 2, 12);
	g_hilite = ui_colorpicker (0, 4, 30, c, 0xFFFF00); ui_w_set_parent (g_hilite, t); ui_w_tip (g_hilite, (long) "Highlight"); row_add (s, g_hilite, 30, 3, 2);
	b_left = tool (t, s, "\xE2\x87\xA4", "Align left (Ctrl+L)", UTB_TOGGLE | UTB_SEP, 2);
	b_center = tool (t, s, "\xE2\x86\x94", "Centre (Ctrl+E)", UTB_TOGGLE, 2);
	b_right = tool (t, s, "\xE2\x87\xA5", "Align right (Ctrl+R)", UTB_TOGGLE, 3);
	b_justify = tool (t, s, "\xE2\x89\xA1", "Justify (Ctrl+J)", UTB_TOGGLE, 3);
	b_bullets = tool (t, s, "\xE2\x80\xA2", "Bullets", UTB_TOGGLE | UTB_SEP, 2);
	b_numbers = tool (t, s, "1.", "Numbering", UTB_TOGGLE, 3);
	b_outdent = tool (t, s, "\xC2\xAB", "Decrease indent", 0, 4);
	b_indent = tool (t, s, "\xC2\xBB", "Increase indent", 0, 4);

	g_ruler = ui_view (0, 2 * TB_H, w, RULER_H, 0);
	ui_view_on (g_ruler, (long) ruler_paint, (long) ruler_mouse, 0, 0);
	int vy = 2 * TB_H + RULER_H;
	pv_create (0, vy, w, h - vy - STATUS_H);
	g_status = ui_view (0, h - STATUS_H, w, STATUS_H, 0);
	ui_view_on (g_status, (long) status_paint, (long) status_mouse, 0, 0);
	g_zoomBy = zoom_by;
	g_onChange = refresh;
	g_mergeShow = show_page;
	ui_menu ((long) MENUS);
}
static void control (long w, long code, long evType)
{
	if (pv_event (w, code)) return;
	if (w == g_style && code == UN_CHANGE) { set_style ((int) wval (g_style)); return; }
	if (w == g_font && code == UN_CHANGE)
	{
		int i = (int) wval (g_font);
		if (i >= 0 && i < g_nfontName) { CfChange c; c.what = CH_FONT; c.font = (short) doc_font (g_doc, g_fontName[i]); ed_format (c); after_edit (); }
		return;
	}
	if (w == g_size && code == UN_CHANGE) { if (evType != WEV_KEY) size_typed (); return; }	// (picked; typed: Enter)
	if (w == g_color && code == UN_CHANGE) { apply_color ((unsigned) wval (g_color) & 0xFFFFFF); return; }
	if (w == g_hilite && code == UN_CHANGE) { apply_hilite ((unsigned) wval (g_hilite) & 0xFFFFFF); return; }
	if (code != UN_ACTIVATE && code != UN_CHANGE) return;
	static const struct { long *w; int id; } T[] = {
		{ &b_new, M_NEW }, { &b_open, M_OPEN }, { &b_save, M_SAVE }, { &b_undo, M_UNDO }, { &b_redo, M_REDO }, { &b_cut, M_CUT }, { &b_copy, M_COPY }, { &b_paste, M_PASTE },
		{ &b_find, M_FIND }, { &b_marks, M_MARKS }, { &b_pbreak, M_PBREAK }, { &b_table, M_TABLE }, { &b_symbol, M_SYMBOL }, { &b_image, M_IMAGE },
		{ &b_bold, M_BOLD }, { &b_italic, M_ITALIC }, { &b_under, M_UNDER }, { &b_strike, M_STRIKE }, { &b_super, M_SUPER }, { &b_sub, M_SUB },
		{ &b_left, M_LEFT }, { &b_center, M_CENTER }, { &b_right, M_RIGHT }, { &b_justify, M_JUSTIFY }, { &b_bullets, M_BULLETS }, { &b_numbers, M_NUMBERS },
		{ &b_indent, M_INDENT }, { &b_outdent, M_OUTDENT } };
	for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (w == *T[i].w) { if (w == b_cut || w == b_copy || w == b_paste) focus_view (); command (T[i].id); return; }
	if (w == b_zout) zoom_by (-1);
	else if (w == b_zin) zoom_by (1);
	else if (w == b_zoom) pick_zoom ();
}
static void word_count ()
{
	unsigned t = ticks ();
	if (!g_countDirty || t - g_countT <= 40) return;			// (the words counted when typing pauses: the body's)
	g_countDirty = false; g_countT = t;
	int n; Para **p = story_p (g_doc, SY_BODY, &n);
	Counts c = para_count (p, mkpos (0, 0), mkpos (n - 1, p[n - 1]->len));
	if (c.words != g_words) { g_words = c.words; refresh (); g_countDirty = false; }
}

// A file dropped on the window is opened, dropped text inserted.
static void dropped ()
{
	static char b[1 << 16];
	long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0) return;
	b[n] = 0;
	if (kind == UDT_TEXT)
	{
		unsigned *u = new unsigned[n + 1];
		int m = decode_text (b, (int) n, u, (int) n + 1);
		ed_type (u, m);
		delete[] u;
		after_edit ();
		return;
	}
	char *e = b; while (*e && *e != '\n') e++;
	*e = 0;
	if (kind == UDT_FILES && confirm ()) load_path (b);
	focus_view ();
}
static void now_hook (int *y, int *mo, int *d, int *h, int *mi, int *s) { now_datetime (y, mo, d, h, mi, s); }

static int run (int argc, char **argv)
{
	if (!uikit_open (&ui) || !documentkit_kit_open () || !archivekit_kit_open ()) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/letters.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = wmin (W, (int) scr[0] - 8), wh = wmin (H, (int) (scr[1] - scr[3] - scr[2]) - 32 - DOCK_H);
	if (ui_window_ex (0, scr[3], ww, wh, (long) "Letters", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (620, 420);
	ui_on_resize ((long) on_resize, 0);
	mkdir ("SD0:/home", 0755); mkdir (APP_HOME, 0755); mkdir (DOCS_DIR, 0755);
	g_now = now_hook;
	doc_init (g_doc); doc_new (g_doc);
	L.d = &g_doc; L.zoom = 100;
	build_window ();
	layout ((int) ui.w, (int) ui.h);
	doc_loaded ();
	if (L.pageW + 2 * GAP > viewW ()) cmd_zoom_width (true);
	ui_w_focus (g_view);
	ui_view_dirty (g_view);
	ui_w_draw (0);
	ui_update_all ();

	// A mail merge's documents, a file named on the command line, else the one kept at the last close.
	if (argc > 2 && !strcmp (argv[1], "--merge"))
	{
		int r = merge_job (argv[2], g_path, sizeof g_path);
		if (r == 2) load_path (g_path);
		else { if (r != 1) doc_new (g_doc); g_path[0] = 0; doc_loaded (); if (r == 1) g_saved = g_doc.changes + 1; }
	}
	else if (argc > 1 && argv[1][0]) load_path (argv[1]);
	else
	{
		int n = 0; char *b = file_read (RECOVER, &n);
		if (b)
		{
			if (n > 0 && rtf_is (b, n) && ui_message ((long) "Letters", (long) TR ("Letters was closed with unsaved changes. Open the recovered document?"), UM_YESNO) == 1)
			{
				rtf_load (g_doc, b, n);
				if (g_doc.n == 0) doc_new (g_doc);
				int nn = 0; char *nb = file_read (RECOVER_NAME, &nn);
				g_path[0] = 0;
				if (nb) { scpy (g_path, nb, sizeof g_path); delete[] nb; }
				doc_loaded ();
				g_saved = g_doc.changes + 1;			// (still unsaved)
			}
			delete[] b;
			unlink (RECOVER); unlink (RECOVER_NAME);
		}
	}
	refresh ();
	ui_view_dirty (g_view);
	ui_w_draw (0);
	ui_update_all ();
	focus_view ();

	unsigned last = 0;
	for (;;)
	{
		long t = ui_event (50);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) { command (ui_ev_field (&ui, EV_CODE)); ui_view_dirty (g_status); }
		else if (t == WEV_DROP) dropped ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE);
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) { control (w, code, t); ui_view_dirty (g_status); }
			if (g_ctxX >= 0) { g_ctxX = -1; context_menu (); }
			if (press && hid == KEY_ENTER && ui_w_get_focus () != g_view) size_typed ();
			if (press && !taken) { long id = ui_menu_key (0); if (id >= 0) command ((int) id); }
			if (t == WEV_KEY || t == WEV_MOUSE) refresh ();
		}
		else ui_w_idle ();
		unsigned now = ticks ();
		if (now - last >= 5) { last = now; pv_tick (); word_count (); }
		bars_sync ();
		ui_view_flush ();
	}

	// Closed with unsaved changes: the document kept for the next start.
	if (g_doc.cur != SY_BODY) doc_story (g_doc, SY_BODY);
	if (changed_doc () && !(g_doc.n == 1 && g_doc.p[0]->len == 0))
	{
		Out o; o.init ();
		rtf_save (g_doc, o);
		file_write (RECOVER, o.b, o.n);
		o.free ();
		if (g_path[0]) file_write (RECOVER_NAME, g_path, slen (g_path));
	}
	return 0;
}

} // namespace wr

int main (int argc, char **argv) { return wr::run (argc, argv); }
