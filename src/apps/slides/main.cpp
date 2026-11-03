//
// slides -- Onyx's presentation program on ARMPI (src/apps/slides/README.md), in the way of PowerPoint / Impress. The
// window: two toolbars (the file, the slides, the objects to insert, Show; then the text: the font, the size, the colour,
// bold / italic / underline / strike / superscript / subscript, the alignment, the lists, the levels, the order), the
// slides at the left (sections, thumbnails), the slide in the middle (its layers composited: render.h), the speaker's
// notes under it, the sidebar at the right (Slide, Shape, Text, Animate), the status bar; the slide sorter; the slide
// show and the presenter's console (show.h).
//
// The pieces: model.h (the deck), master.h (the master view), text.h (the text's layout and drawing), render.h (the
// layers, the compositor), editor.h (the state, the commands, undo), view.h (the slide edited), panes.h (thumbnails,
// sorter, notes), sidebar.h (the four tabs), show.h (the show), find.h, odp.h (OpenDocument), pptx.h (PowerPoint): read
// and written, by DocumentKit (XML) and ArchiveKit (ZIP).
//
// Files: .odp (OpenDocument presentation: Slides' own; Impress reads it) and .pptx (PowerPoint's; saved as such when the
// file's name says so); File > Export as PDF (a page a slide, a picture each) and Export Slide as PNG. "slides
// SD0:/home/documents/a.odp" (or a.pptx) opens it. Closed with unsaved changes, the deck is kept in
// SD0:/home/slides/recovered.odp and offered back at the next start.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sidebar.h"
#include "show.h"
#include "master.h"
#include "find.h"
#include "pptx.h"
#include <printerkit.h>

using namespace sl;

#define W 1000
#define H 620
#define DOCK_H 96
#define HOME_DIR "SD0:/home/slides"
static const char *RECOVER = HOME_DIR "/recovered.odp";
static const char *RECOVER_NAME = HOME_DIR "/recovered.txt";
static const char *DOCS = "SD0:/home/documents";
static const char *const FONT_NAMES[3] = { "DejaVu Sans", "DejaVu Serif", "DejaVu Sans Mono" };

enum {	M_NEW = 1, M_OPEN, M_SAVE, M_SAVEAS, M_PDF, M_PNG, M_PRINT,
	M_UNDO = 10, M_REDO, M_CUT, M_COPY, M_PASTE, M_DUP, M_DEL, M_ALL, M_FIND,
	M_NORMAL = 20, M_SORTER, M_NOTES, M_MASTER, M_FIT, M_ZIN, M_ZOUT, M_THUMBS, M_SIDE,
	M_NEWSL = 30, M_NEWSLL, M_DUPSL, M_TEXT, M_PIC, M_TABLE, M_CHART, M_SHAPE, M_ARROW, M_LINE, M_FOOTER,
	M_BOLD = 45, M_ITALIC, M_UNDER, M_BULLET, M_NUMBER, M_FRONT, M_FORW, M_BACKW, M_BACK,
	M_LAYOUT = 55, M_DELSL, M_HIDE, M_ADDSEC, M_REMSEC,
	M_START = 60, M_HERE, M_PRESENTER };

static const char MENUS[] =
	"MFile\n"
	"I1\tNew\tCtrl+N\n" "I2\tOpen...\tCtrl+O\n" "-\n" "I3\tSave\tCtrl+S\n" "I4\tSave As...\n" "I5\tExport as PDF...\n" "I6\tExport Slide as PNG...\n" "-\n" "I7\tPrint...\tCtrl+P\n"
	"MEdit\n"
	"I10\tUndo\tCtrl+Z\n" "I11\tRedo\tCtrl+Y\n" "-\n" "I12\tCut\tCtrl+X\n" "I13\tCopy\tCtrl+C\n" "I14\tPaste\tCtrl+V\n" "I15\tDuplicate\tCtrl+D\n" "I16\tDelete\n" "I17\tSelect All\tCtrl+A\n" "-\n" "I18\tFind and Replace...\tCtrl+F\n"
	"MView\n"
	"I20\tNormal\n" "I21\tSlide Sorter\n" "I22\tNotes\n" "I27\tThumbnails\n" "I28\tSidebar\n" "I23\tMaster and Layouts\n" "-\n" "I24\tFit the Window\n" "I25\tZoom In\n" "I26\tZoom Out\n"
	"MInsert\n"
	"I30\tNew Slide\tCtrl+M\n" "I31\tNew Slide with a Layout...\n" "I32\tDuplicate Slide\n" "-\n" "I33\tText Box\n" "I34\tPicture...\n" "I35\tTable...\n" "I36\tChart...\n" "I37\tShape...\n" "I38\tArrow\n" "I39\tLine\n" "-\n" "I40\tHeader and Footer...\n"
	"MFormat\n"
	"I45\tBold\tCtrl+B\n" "I46\tItalic\tCtrl+I\n" "I47\tUnderline\tCtrl+U\n" "-\n" "I48\tBullets\n" "I49\tNumbering\n" "-\n" "I50\tBring to Front\n" "I51\tBring Forward\n" "I52\tSend Backward\n" "I53\tSend to Back\n"
	"MSlide\n"
	"I55\tLayout...\n" "I56\tDelete Slide\n" "I57\tHide / Show Slide\n" "I58\tAdd Section...\n" "I59\tRemove Section\n"
	"MShow\n"
	"I60\tFrom the Beginning\tF5\n" "I61\tFrom This Slide\n" "I62\tPresenter View\n";

static long g_tb1, g_tb2, g_tb2b, g_st, g_bShow, g_bFont, g_bSize, g_cText, g_lStatus, g_bNotes, g_bNormal, g_bSorter, g_bZm, g_bZoom, g_bZp;
enum { TB_NEW, TB_OPEN, TB_SAVE, TB_UNDO, TB_REDO, TB_CUT, TB_COPY, TB_PASTE, TB_NEWSL, TB_LAYOUT, TB_DUPSL, TB_DELSL, TB_TEXT, TB_PIC, TB_TABLE, TB_CHART, TB_SHAPES, TB_ARROW,
       TF_BOLD, TF_ITALIC, TF_UNDER, TF_STRIKE, TF_SUPER, TF_SUB, TF_LEFT, TF_CENTER, TF_RIGHT, TF_JUSTIFY, TF_BULLETS, TF_NUMBERS, TF_OUT, TF_IN, TF_SPACING, TF_FRONT, TF_BACK, TB_COUNT };
static long g_btn[TB_COUNT];
static int TB_H, ST_H, TH_W, SB_W, NOTES_H;
static bool g_showThumbs = true, g_showSide = true;
static char g_msg[120]; static unsigned g_msgT;

static void relayout ();
static void refresh ();
static void cmd_master ();
static void focus_view () { ui_w_focus (g_viewMode == VIEW_SORTER ? so : sv); }
static void status (const char *m) { scpy (g_msg, TR (m), sizeof g_msg); g_msgT = ticks (); g_ui = true; }
static int ask (const char *title, const char *text, int buttons) { return (int) ui_message ((long) TR (title), (long) TR (text), buttons); }

// ---- files ---------------------------------------------------------------------------------------------------------
static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static bool has_ext (const char *p, const char *e)
{
	int n = (int) strlen (p), k = (int) strlen (e);
	if (n < k) return false;
	for (int i = 0; i < k; i++) { int a = p[n - k + i], b = e[i]; if (a >= 'A' && a <= 'Z') a += 32; if (a != b) return false; }
	return true;
}
static unsigned char *read_all (const char *path, unsigned *len)
{
	int fd = open (path, O_RDONLY);
	if (fd < 0) return 0;
	struct stat st;
	long sz = fstat (fd, &st) == 0 ? (long) st.st_size : 0;
	if (sz <= 0 || sz > (64L << 20)) { close (fd); return 0; }
	unsigned char *b = (unsigned char *) malloc ((size_t) sz + 1);
	long n = b ? read (fd, b, (size_t) sz) : -1;
	close (fd);
	if (n < 0) { free (b); return 0; }
	*len = (unsigned) n;
	return b;
}
static bool save_file (const char *path, const void *b, long n)
{
	int fd = open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return false;
	long w = n ? write (fd, b, (size_t) n) : 0;
	return close (fd) == 0 && w == n;
}
// The window's title (the menu bar shows it: on a small screen, the menus need the room; the status bar names the file).
static void show_title ()
{
	char t[220] = "Slides";
	if (g_path[0] && ui.w >= 900) { scat (t, " - ", sizeof t); scat (t, base_name (g_path), sizeof t); }
	ui_title ((long) t);
}
static void deck_loaded ()
{
	g_cur = 0; g_sel.clear (); end_edit (); undo_clear ();
	g_saved = g_deck.changes;
	g_thumbs.clear (); np_reset ();
	C.drop_all ();
	g_sorterSel.clear ();
	g_nfamCache = 0;
	sl_top = 0;
	after ();
	show_title ();
}
static bool load_path (const char *path)
{
	if (g_master) master_close ();
	unsigned n = 0;
	unsigned char *b = read_all (path, &n);
	if (!b) { ask ("Open", "The file could not be read.", UM_OK); return false; }
	bool ok = deck_load (g_deck, b, n);
	free (b);
	if (!ok) { deck_new (g_deck); ask ("Open", "That is not a presentation Slides can read (.odp, .pptx).", UM_OK); }
	scpy (g_path, ok ? path : "", sizeof g_path);
	deck_loaded ();
	return ok;
}
static bool write_path (const char *path)
{
	unsigned n = 0;
	unsigned char *b;
	if (g_master)
	{
		// the master view open: the deck as it will be (its slides, the view's changes)
		Deck *d = new Deck; master_real_deck (*d);
		b = has_ext (path, ".pptx") ? pptx_save (*d, &n) : odp_save (*d, &n);
		delete d;
	}
	else b = has_ext (path, ".pptx") ? pptx_save (g_deck, &n) : odp_save (g_deck, &n);
	bool ok = b && save_file (path, b, n);
	delete[] b;
	if (!ok) { ask ("Save", "The file could not be written.", UM_OK); return false; }
	scpy (g_path, path, sizeof g_path); g_saved = g_deck.changes;
	char m[220]; snprintf (m, sizeof m, TR ("Saved %s"), base_name (path)); status (m);
	show_title ();
	return true;
}
static void cmd_save_as ()
{
	char path[200], def[80];
	scpy (def, g_path[0] ? base_name (g_path) : TR ("Untitled.odp"), sizeof def);
	ui_file_filter ((long) "*.odp;*.pptx");
	if (ui_file_save ((long) DOCS, (long) def, (long) path, sizeof path))
	{
		// (.odp, or .pptx when the name says so -- or the file was one)
		if (!has_ext (path, ".odp") && !has_ext (path, ".pptx")) scat (path, has_ext (g_path, ".pptx") ? ".pptx" : ".odp", sizeof path);
		write_path (path);
	}
	ui_file_filter (0);
	focus_view ();
}
static void cmd_save () { if (!g_path[0] || (!has_ext (g_path, ".odp") && !has_ext (g_path, ".pptx"))) { cmd_save_as (); return; } write_path (g_path); focus_view (); }
// Unsaved changes: saved (Yes), dropped (No) or kept (Cancel -> false).
static bool confirm_discard ()
{
	if (!changed_doc ()) return true;
	char m[240]; snprintf (m, sizeof m, TR ("Save the changes to %s?"), g_path[0] ? base_name (g_path) : TR ("Untitled"));
	int r = ask ("Unsaved changes", m, UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { cmd_save (); return !changed_doc (); }
	return true;
}
static void cmd_new ()
{
	if (!confirm_discard ()) { focus_view (); return; }
	if (g_master) master_close ();
	deck_new (g_deck, theme_find (g_deck.theme.name) >= 0 ? theme_find (g_deck.theme.name) : 0);
	g_path[0] = 0;
	deck_loaded ();
	focus_view ();
}
static void cmd_open ()
{
	if (!confirm_discard ()) { focus_view (); return; }
	char path[200];
	ui_file_filter ((long) "*.odp;*.pptx");
	if (ui_file_open ((long) DOCS, (long) path, sizeof path)) load_path (path);
	ui_file_filter (0);
	focus_view ();
}
// File > Export as PDF: a page a slide, the slide flattened into a picture (deflated). The notes pages and handouts of
// Onyx, its fonts embedded and its JPEG writer are not here.
static void pdf_obj (Buf &o, Vec<int> &xref, int id) { while (xref.n <= id) xref.push (0); xref[id] = o.n; char t[32]; snprintf (t, sizeof t, "%d 0 obj\n", id); o.puts (t); }
static bool export_pdf (const char *path)
{
	int shown = 0; for (int i = 0; i < g_deck.slides.n; i++) if (!g_deck.slides[i]->hidden) shown++;
	if (!shown) return false;
	int pw = 1280, ph = pw * g_deck.sh / g_deck.sw;
	float ptW = g_deck.sw / 35.277778f, ptH = g_deck.sh / 35.277778f;
	unsigned *px = (unsigned *) malloc ((size_t) pw * ph * 4); unsigned char *rgb = (unsigned char *) malloc ((size_t) pw * ph * 3);
	if (!px || !rgb) { free (px); free (rgb); return false; }
	Buf o; Vec<int> xref; char t[200];
	o.puts ("%PDF-1.4\n");
	Buf kids;
	Compositor Cp;
	int obj = 3, ok = 1;
	for (int i = 0; i < g_deck.slides.n && ok; i++)
	{
		Slide &s = *g_deck.slides[i];
		if (s.hidden) continue;
		Cp.frame++;
		flatten_slide (Cp, g_deck, s, i, px, pw, ph, pw);
		Cp.sweep (0);
		for (long k = 0; k < (long) pw * ph; k++) { rgb[3 * k] = (unsigned char) (px[k] >> 16); rgb[3 * k + 1] = (unsigned char) (px[k] >> 8); rgb[3 * k + 2] = (unsigned char) px[k]; }
		long z = ar_deflate_new ((long) rgb, (long) pw * ph * 3, AR_ZLIB, 6);
		if (z <= 0) { ok = 0; break; }
		long zn = ar_bytes (z);
		int page = obj, cont = obj + 1, img = obj + 2; obj += 3;
		pdf_obj (o, xref, page);
		snprintf (t, sizeof t, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 %.2f %.2f]/Contents %d 0 R/Resources<</XObject<</I %d 0 R>>>>>>\nendobj\n", ptW, ptH, cont, img); o.puts (t);
		snprintf (t, sizeof t, "%d 0 R ", page); kids.puts (t);
		char c[80]; int cn = snprintf (c, sizeof c, "q %.2f 0 0 %.2f 0 0 cm /I Do Q\n", ptW, ptH);
		pdf_obj (o, xref, cont);
		snprintf (t, sizeof t, "<</Length %d>>\nstream\n", cn); o.puts (t); o.puts (c); o.puts ("endstream\nendobj\n");
		pdf_obj (o, xref, img);
		snprintf (t, sizeof t, "<</Type/XObject/Subtype/Image/Width %d/Height %d/ColorSpace/DeviceRGB/BitsPerComponent 8/Filter/FlateDecode/Length %ld>>\nstream\n", pw, ph, zn); o.puts (t);
		o.putn ((const char *) ar_data (z), (int) zn);
		o.puts ("\nendstream\nendobj\n");
		ar_free (z);
	}
	Cp.drop_all ();
	free (px); free (rgb);
	if (!ok) return false;
	pdf_obj (o, xref, 1); o.puts ("<</Type/Catalog/Pages 2 0 R>>\nendobj\n");
	pdf_obj (o, xref, 2); snprintf (t, sizeof t, "<</Type/Pages/Count %d/Kids[", shown); o.puts (t); o.puts (kids.str ()); o.puts ("]>>\nendobj\n");
	int xr = o.n;
	snprintf (t, sizeof t, "xref\n0 %d\n0000000000 65535 f \n", xref.n); o.puts (t);
	for (int i = 1; i < xref.n; i++) { snprintf (t, sizeof t, "%010d 00000 n \n", xref[i]); o.puts (t); }
	snprintf (t, sizeof t, "trailer\n<</Size %d/Root 1 0 R>>\nstartxref\n%d\n%%%%EOF\n", xref.n, xr); o.puts (t);
	return save_file (path, o.b, o.n);
}
static void def_name (char *def, int cap, const char *ext)
{
	scpy (def, g_path[0] ? base_name (g_path) : TR ("Untitled"), cap);
	char *dot = strrchr (def, '.'); if (dot) *dot = 0;
	scat (def, ext, cap);
}
static void cmd_export_pdf ()
{
	if (g_master) { master_close (); g_thumbs.clear (); after (); }
	char def[120], path[200];
	def_name (def, sizeof def, ".pdf");
	ui_file_filter ((long) "*.pdf");
	if (ui_file_save ((long) DOCS, (long) def, (long) path, sizeof path))
	{
		if (!has_ext (path, ".pdf")) scat (path, ".pdf", sizeof path);
		if (!export_pdf (path)) ask ("Export as PDF", "The PDF could not be written there.", UM_OK);
		else status ("Exported as PDF (a picture a page)");
	}
	ui_file_filter (0);
	focus_view ();
}
static void cmd_export_png ()
{
	if (g_master) { master_close (); g_thumbs.clear (); after (); }
	char def[120], path[200];
	snprintf (def, sizeof def, TR ("Slide %d.png"), g_cur + 1);
	ui_file_filter ((long) "*.png");
	if (ui_file_save ((long) DOCS, (long) def, (long) path, sizeof path))
	{
		if (!has_ext (path, ".png")) scat (path, ".png", sizeof path);
		int pw = 1920, ph = pw * g_deck.sh / g_deck.sw;
		unsigned *px = (unsigned *) malloc ((size_t) pw * ph * 4);
		unsigned n = 0; unsigned char *png = 0;
		if (px)
		{
			Compositor Cp; Cp.frame++;
			flatten_slide (Cp, g_deck, *cur_slide (), g_cur, px, pw, ph, pw);
			Cp.drop_all ();
			for (long k = 0; k < (long) pw * ph; k++) px[k] |= 0xFF000000u;
			png = png_encode (px, pw, ph, false, &n);
			free (px);
		}
		if (!png || !save_file (path, png, n)) ask ("Export", "The picture could not be written there.", UM_OK);
		delete[] png;
	}
	ui_file_filter (0);
	focus_view ();
}
// File > Print: the Print dialog (PrinterKit's), then a page a slide, flattened into a picture (about 200 pixels an inch on A4).
static void cmd_print ()
{
	char title[120]; scpy (title, g_path[0] ? base_name (g_path) : TR ("Untitled"), sizeof title);
	int shown = 0;
	for (int i = 0; i < g_deck.slides.n; i++) if (!g_deck.slides[i]->hidden) shown++;
	float ptW = g_deck.sw / 35.277778f, ptH = g_deck.sh / 35.277778f;
	if (!printerkit_kit_open ()) { ask ("Print", "The print service is not installed.", UM_OK); focus_view (); return; }
	PrintSetup ps; print_setup_default ((long) &ps);
	PrintDialogInfo di = { sizeof di, title, shown, 0, PRINT_DLG_OWN_PAPER, 0, 0 };
	if (!print_dialog ((long) &ps, (long) &di)) { focus_view (); return; }
	long j = print_begin ((long) &ps, (long) title);
	int pw = 2400, ph = pw * g_deck.sh / g_deck.sw;
	unsigned *px = (unsigned *) malloc ((size_t) pw * ph * 4);
	if (j && px)
	{
		Compositor Cp;
		for (int i = 0; i < g_deck.slides.n; i++)
		{
			Slide &s = *g_deck.slides[i];
			if (s.hidden) continue;
			if (!print_page (j, print_mpt (ptW), print_mpt (ptH))) continue;	// (not one of the pages to print: not drawn)
			Cp.frame++;
			flatten_slide (Cp, g_deck, s, i, px, pw, ph, pw);
			Cp.sweep (0);
			print_image (j, (long) px, pw, ph, 0, 0, print_mpt (ptW), print_mpt (ptH));
		}
		Cp.drop_all ();
	}
	free (px);
	if (!j || !px || print_end (j) < 0) ask ("Print", "The slides could not be put in the print queue.", UM_OK);
	focus_view ();
}

// ---- a few dialogs ---------------------------------------------------------------------------------------------------
static bool ask_line (const char *title, const char *q, char *out, int cap) { return ui_ask ((long) TR (title), (long) TR (q), (long) out, cap) == 1; }
static void d_ok_cancel (long d, int w) { long x = ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, w - 12); ui_dialog_button (d, 1, (long) "Cancel", 0, 0, x); ui_dialog_cancel (d, 0); }
static long d_label (int x, int y, int w, const char *t, long fl = 0) { return ui_label (x, y, w, ctl_h (), (long) t, fl); }
static long d_field (int x, int y, int w, const char *t) { long f = ui_textbox (x, y, w, ctl_h (), 0); ui_w_set_text (f, (long) t); return f; }
static const char *d_text (long w) { return (const char *) ui_w_get_text (w); }

// A grid of pictures to choose from (the shapes, the layouts): a dialog, a view in it. -> the cell, -1.
struct Gallery { int n, per, cw, ch; void (*draw) (int i, int x, int y, int w, int h, unsigned *cv, int cvw, int cvh); int hot; const char *foot; };
static Gallery G;
static long gl_view;
static void gl_paint (long v, long w, long h)
{
	long cw, chh; unsigned *cv = (unsigned *) uikit_canvas_pixels (v, &cw, &chh);
	ui_fill (0, 0, w, h, jet (J_SURFACE));
	for (int i = 0; i < G.n; i++)
	{
		int x = (i % G.per) * G.cw, y = (i / G.per) * G.ch;
		if (i == G.hot) ui_rbox (x + 1, y + 1, G.cw - 2, G.ch - 2, 5, jet (J_ACCENT_CLAIR), jet (J_ACCENT_CLAIR));
		G.draw (i, x, y, G.cw, G.ch, cv, (int) cw, (int) chh);
	}
}
static long gl_mouse (long, long kind, long mx, long my, long info)
{
	int c = (int) mx / G.cw, r = (int) my / G.ch, i = c >= 0 && c < G.per && my >= 0 ? r * G.per + c : -1;
	if (i >= G.n) i = -1;
	if (i != G.hot) { G.hot = i; ui_view_dirty (gl_view); ui_view_flush (); }
	(void) info;
	return kind == 2 && i >= 0 ? UN_ACTIVATE : 0;
}
static long gl_fn (long d, long w, long code) { if (w == gl_view && code == UN_ACTIVATE) ui_dialog_close (d, G.hot + 1); return 0; }
static int gallery (const char *title, const char *foot, int footAnswer)
{
	int rows = (G.n + G.per - 1) / G.per, W_ = G.per * G.cw + 16, H_ = rows * G.ch + 16 + ctl_h () + 16;
	long d = ui_dialog_begin (W_, H_, (long) title);
	if (!d) return -1;
	gl_view = ui_view (8, 8, G.per * G.cw, rows * G.ch, 0);
	ui_view_on (gl_view, (long) gl_paint, (long) gl_mouse, 0, 0);
	long x = ui_dialog_button (d, 0, (long) "Cancel", 0, 0, W_ - 12);
	if (foot) ui_dialog_button (d, 1, (long) foot, footAnswer + 1, 0, x);
	ui_dialog_cancel (d, 0);
	G.hot = -1;
	long r = ui_dialog_run (d, (long) gl_fn, 0);
	ui_dialog_end (d);
	return (int) r - 1;
}
static void shape_cell (int i, int x, int y, int w, int h, unsigned *cv, int cvw, int cvh)
{
	Surf s; s.px = cv; s.w = cvw; s.h = cvh; s.stride = cvw; s.pm = false; s.clip_all ();
	if (i >= SH_COUNT)
	{
		VPath p; p.line (Q (x + 8.0f), Q (y + h - 8.0f), Q (x + w - 8.0f), Q (y + 8.0f), 26);
		if (i == SH_COUNT + 1) p.arrowHead (Q (x + w - 6.0f), Q (y + 6.0f), 45, Q (8), Q (5));
		p.fill (s, jet (J_ACCENT_FONCE));
		return;
	}
	VPath p; shape_path (p, i, x + 7.0f, y + 8.0f, w - 14.0f, h - 16.0f, 160, 0, true); p.fill (s, jet (J_ACCENT));
	p.clear (); shape_path (p, i, x + 7.0f, y + 8.0f, w - 14.0f, h - 16.0f, 160, 1.2f, false); p.fill (s, jet (J_ACCENT_FONCE));
}
// -> SH_*, SH_COUNT: a line, SH_COUNT + 1: an arrow, -1
static int shape_pick (bool lines)
{
	G.n = SH_COUNT + (lines ? 2 : 0); G.per = 7; G.cw = 44; G.ch = 40; G.draw = shape_cell;
	return gallery ("Shapes", 0, 0);
}
static void layout_cell (int k, int cx, int cy, int cw, int chh, unsigned *, int, int)
{
	int x = cx + 8, y = cy + 6, w = cw - 16, h = chh - 30;
	ui_fill (x, y, w, h, 0xFFFFFF); ui_frame (x, y, w, h, 0xB4A8A0);
	unsigned bar = 0x4E8696, g = 0xD2CEC8;
	auto lines = [&] (int lx, int ly, int lw, int n) { for (int i = 0; i < n; i++) ui_fill (lx, ly + i * 7, lw * (i % 2 ? 9 : 10) / 10, 3, g); };
	switch (k)
	{
	case LY_TITLE: ui_fill (x + w * 15 / 100, y + h * 36 / 100, w * 70 / 100, 7, bar); ui_fill (x + w * 28 / 100, y + h * 56 / 100, w * 44 / 100, 4, g); break;
	case LY_SECTION: ui_fill (x + 1, y + 1, w - 2, h - 2, 0xD8E6EA); ui_fill (x + w * 12 / 100, y + h * 48 / 100, w * 60 / 100, 7, bar); break;
	case LY_BLANK: break;
	default:
		ui_fill (x + 6, y + 6, w * 55 / 100, 6, bar);
		if (k == LY_CONTENT) lines (x + 8, y + 20, w - 16, 4);
		else if (k == LY_TWO) { lines (x + 8, y + 20, w / 2 - 12, 4); lines (x + w / 2 + 4, y + 20, w / 2 - 12, 4); }
		else if (k == LY_COMPARE) { for (int j = 0; j < 2; j++) { int lx = x + 8 + j * (w / 2 - 2); ui_fill (lx, y + 19, w / 2 - 12, 4, 0xF4C8A0); lines (lx, y + 27, w / 2 - 12, 3); } }
		else if (k == LY_PICTURE) { ui_fill (x + 1, y + 1, w * 48 / 100, h - 2, 0x96BAC8); ui_fill (x + w * 55 / 100, y + 8, w * 38 / 100, 5, bar); lines (x + w * 55 / 100, y + 20, w * 38 / 100, 3); }
	}
	text_box (TR (g_deck.layout[k].name), cx + 4, cy + chh - 22, 20, jet (J_TEXTE1), 0, cw - 8);
}
// -> LY_*, 100: reset, -1
static int layout_pick (int cur, const char *title)
{
	(void) cur;
	G.n = LY_COUNT; G.per = 4; G.cw = 128; G.ch = 92; G.draw = layout_cell;
	int k = gallery (title, cur >= 0 ? "Reset the slide" : 0, 100);
	return k;
}

// Find and Replace (find.h: the search)
static void show_hit (const FindAt &a, int n)
{
	if (a.slide != g_cur) go_slide (a.slide);
	Object *o = g_deck.slides[a.slide]->obj[a.obj];
	begin_edit (o->id, a.r, a.c);
	g_anchor = tpos (a.p, a.o); g_caret = tpos (a.p, a.o + n);
	after ();
}
static int word_of (unsigned *w, const char *s)
{
	int n = 0, L = (int) strlen (s);
	for (int i = 0; i < L && n < 255; ) { int l; w[n++] = ss::u8_dec (s + i, L - i, &l); i += l > 0 ? l : 1; }
	return n;
}
// (Enter in one of a dialog's fields: its default button; its own Close button. The kit's buttons have answered already.)
static long g_dClose, g_dField[64]; static int g_dFields;
static long field_fn (long d, long w, long code)
{
	if (code != UN_ACTIVATE) return 0;
	if (w == g_dClose) ui_dialog_close (d, 0);
	for (int i = 0; i < g_dFields; i++) if (w == g_dField[i]) ui_dialog_close (d, 1);
	return 0;
}
// (A dialog is a window of its own and the main one is only painted from the main loop: at a match the dialog closes, the
// slide is shown, and it opens again, the words kept.)
static void prepare ();
static void cmd_find ()
{
	if (g_master) return;
	static char sWhat[256], sWith[256]; static long sCase;
	char msgT[64] = "";
	if (has_tsel ()) { TextBody *tb = edit_body (); TPos a = tsel_a (), b = tsel_b (); if (tb && a.p == b.p) { Buf t; for (int i = a.o; i < b.o; i++) t.putu (tb->p[a.p]->ch[i]); scpy (sWhat, t.str (), sizeof sWhat); } }
	for (bool again = true; again; )
	{
		again = false;
		int DW = 440, DH = 200, c = ctl_h () + 8;
		long d = ui_dialog_begin (DW, DH, (long) "Find and Replace");
		if (!d) return;
		d_label (14, 12, 90, "Find"); long what = d_field (110, 12, 316, sWhat);
		d_label (14, 12 + c, 90, "Replace with"); long with = d_field (110, 12 + c, 316, sWith);
		long mc = ui_checkbox (110, 12 + 2 * c, 200, ctl_h (), (long) TR ("Match case"), sCase);
		long msg = d_label (14, 12 + 3 * c, 400, msgT, ULB_DIM);
		long x = ui_dialog_button (d, 0, (long) "Find Next", 1, UB_DEFAULT, DW - 90);
		x = ui_dialog_button (d, 1, (long) "Replace", 2, 0, x);
		ui_dialog_button (d, 2, (long) "Replace All", 3, 0, x);
		g_dClose = ui_pushbutton (DW - 84, DH - ctl_h () - 8, 74, ctl_h (), (long) "Close", 0);
		ui_dialog_cancel (d, 0);
		ui_w_focus (what);
		g_dFields = 2; g_dField[0] = what; g_dField[1] = with;
		for (;;)
		{
			long tag = ui_dialog_run (d, (long) field_fn, 0);
			scpy (sWhat, d_text (what), sizeof sWhat); scpy (sWith, d_text (with), sizeof sWith); sCase = ui_w_get_value (mc);
			if (tag == 0) break;
			unsigned w[256], r[256]; int n = word_of (w, sWhat), rn = word_of (r, sWith);
			bool cs = sCase != 0;
			ui_w_set_text (msg, (long) ""); msgT[0] = 0;
			if (!n) continue;
			if (tag == 3)
			{
				int count = replace_all (w, n, r, rn, cs);
				snprintf (msgT, sizeof msgT, count == 1 ? TR ("1 replacement.") : TR ("%d replacements."), count);
				after (); again = true;
				break;
			}
			if (tag == 2 && has_tsel ())
			{
				// the selection is a match: replaced
				TextBody *tb = edit_body ();
				TPos a = tsel_a (), b = tsel_b ();
				if (tb && a.p == b.p && b.o - a.o == n)
				{
					int m = 0; while (m < n && fold_ch (tb->p[a.p]->ch[a.o + m], cs) == fold_ch (w[m], cs)) m++;
					if (m == n)
					{
						begin_change ();
						CharFmt f = tb->p[a.p]->cf[a.o];
						tb_delete (*tb, a, b);
						if (rn) tb_insert (*tb, a, r, rn, f);
						g_caret = g_anchor = tpos (a.p, a.o + rn);
						done_change (); notify_slides ();
					}
				}
			}
			// the next match, after the caret (or from the current slide's start)
			FindAt at; at.slide = g_cur; at.obj = 0; at.r = -1; at.c = -1; at.p = 0; at.o = 0;
			if (g_edit)
			{
				Slide *s = cur_slide (); int oi = s ? s->index_of (g_edit) : -1;
				if (oi >= 0) { at.obj = oi; at.r = g_cellR; at.c = g_cellC; TPos c2 = tsel_b (); at.p = c2.p; at.o = c2.o; }
			}
			if (find_next (w, n, cs, at)) { show_hit (at, n); again = true; break; }
			ui_w_set_text (msg, (long) TR ("Not found."));
		}
		ui_dialog_end (d);
		after ();
		if (again) { sl_show_cur (); prepare (); ui_view_flush (); ui_update_all (); }
	}
	focus_view ();
}
// A chart's data: its title, its categories (up to 8 shown) and series (up to 4)
static bool chart_dialog (Chart &c)
{
	int DW = 706, c1 = ctl_h () + 6, DH = 12 + c1 + 8 + 5 * c1 + 30 + ctl_h () + 20;
	long d = ui_dialog_begin (DW, DH, (long) "Chart Data");
	if (!d) return false;
	d_label (14, 12, 80, "Title");
	long title = d_field (100, 12, 300, c.title);
	int y = 12 + c1 + 8;
	long cat[8], ser[4], val[4][8];
	for (int i = 0; i < 8; i++) cat[i] = d_field (150 + i * 68, y, 64, i < c.ncat ? c.cat[i] : "");
	for (int k = 0; k < 4; k++)
	{
		ser[k] = d_field (14, y + c1 + k * c1, 130, k < c.nser ? c.ser[k] : "");
		for (int i = 0; i < 8; i++) { char t[32] = ""; if (k < c.nser && i < c.ncat) snprintf (t, sizeof t, "%g", c.val[k][i]); val[k][i] = d_field (150 + i * 68, y + c1 + k * c1, 64, t); }
	}
	d_label (14, y + 5 * c1, 500, "A series a row, a category a column (empty: left out).", ULB_DIM);
	d_ok_cancel (d, DW);
	g_dClose = 0; g_dFields = 0;
	g_dField[g_dFields++] = title;
	for (int i = 0; i < 8; i++) g_dField[g_dFields++] = cat[i];
	for (int k = 0; k < 4; k++) { g_dField[g_dFields++] = ser[k]; for (int i = 0; i < 8; i++) g_dField[g_dFields++] = val[k][i]; }
	bool ok = false;
	for (;;)
	{
		if (ui_dialog_run (d, (long) field_fn, 0) != 1) break;
		int nc = 0; for (int i = 0; i < 8; i++) if (d_text (cat[i])[0]) nc = i + 1;
		int ns = 0; for (int k = 0; k < 4; k++) if (d_text (ser[k])[0]) ns = k + 1;
		if (nc == 0 || ns == 0) { ask ("Chart Data", "Name at least one category and one series.", UM_OK); continue; }
		scpy (c.title, d_text (title), sizeof c.title);
		c.ncat = nc; c.nser = c.type == CH_PIE ? 1 : ns;
		for (int i = 0; i < nc; i++) scpy (c.cat[i], d_text (cat[i]), 24);
		for (int k = 0; k < ns; k++) { scpy (c.ser[k], d_text (ser[k]), 32); for (int i = 0; i < nc; i++) c.val[k][i] = strtod (d_text (val[k][i]), 0); }
		ok = true;
		break;
	}
	ui_dialog_end (d);
	return ok;
}

// ---- the commands ----------------------------------------------------------------------------------------------------
static bool notes_focus () { return ui_w_get_focus () == np_ta; }
static bool list_focus () { return ui_w_get_focus () == sl_v; }
// (a menu's Cut, Copy, Paste for a field of the sidebar: the key sent to it)
static bool field_focus ()
{
	long f = ui_w_get_focus ();
	return f && f != sv && f != sl_v && f != so && f != np_ta;
}
static void key_to_focus (int hid)
{
	int e[8] = { WEV_KEY, 1, 0, 0, hid, 0, UI_MOD_CTRL, ui_ev_field (&ui, EV_WIN) };
	uikit_call3 ((long (*) ()) ui_w_event, (long) e, 0, 0, 0, 0, 0);
}
static void ui_undo () { if (notes_focus ()) focus_view (); sl::cmd_undo (); g_thumbs.prune (); np_reset (); after (); }
static void ui_redo () { sl::cmd_redo (); g_thumbs.prune (); np_reset (); after (); }
static Buf g_clipText;
static void cmd_copy ()
{
	if (notes_focus ()) { ui_textarea_edit (np_ta, UTE_COPY); return; }
	if (field_focus ()) { key_to_focus (0x06); return; }
	if (g_edit) { Buf t; text_selected_utf8 (t); if (t.n) ui_clip_set ((long) t.str (), -1); return; }
	if (g_viewMode == VIEW_SORTER || list_focus ())
	{
		clip_clear_own ();
		if (g_sorterSel.n > 1) { for (int i = 0; i < g_deck.slides.n; i++) if (g_sorterSel.find (i) >= 0) g_clipSlides.push (slide_copy (g_deck.slides[i])); }
		else if (cur_slide ()) g_clipSlides.push (slide_copy (cur_slide ()));
		g_clipKind = 2;
		g_clipText.clear (); g_clipText.puts ("[Slides: slides]"); ui_clip_set ((long) g_clipText.str (), -1);
		return;
	}
	if (!g_sel.n) return;
	copy_objects ();
	g_clipText.clear ();
	char b[256]; if (ui_clip_get ((long) b, sizeof b) > 0) g_clipText.puts (b);
}
static void cmd_cut ()
{
	if (notes_focus ()) { ui_textarea_edit (np_ta, UTE_CUT); return; }
	if (field_focus ()) { key_to_focus (0x1B); return; }
	cmd_copy ();
	if (g_edit) { TextBody *tb = edit_body (); if (tb && has_tsel ()) { begin_change (); TPos a = tsel_a (); tb_delete (*tb, a, tsel_b ()); g_caret = g_anchor = a; done_change (); } after (); return; }
	if (g_viewMode == VIEW_SORTER || list_focus ()) { cmd_delete_slide (); after (); return; }
	cmd_delete_objects (); after ();
}
static bool own_clip ()
{
	char b[256]; long n = ui_clip_get ((long) b, sizeof b);
	return n > 0 && g_clipText.n && !strcmp (b, g_clipText.str ());
}
static void cmd_paste ()
{
	if (notes_focus ()) { ui_textarea_edit (np_ta, UTE_PASTE); return; }
	if (field_focus ()) { key_to_focus (0x19); return; }
	if (own_clip () && g_clipKind == 2 && g_clipSlides.n && (g_viewMode == VIEW_SORTER || list_focus () || !g_edit))
	{
		begin_change ();
		for (int i = 0; i < g_clipSlides.n; i++)
		{
			Slide *s = slide_copy (g_clipSlides[i]);
			for (int k = 0; k < s->obj.n; k++) { int old = s->obj[k]->id, nid = g_deck.nextId++; s->obj[k]->id = nid; for (int a = 0; a < s->anim.n; a++) if (s->anim[a].obj == old) s->anim[a].obj = nid; }
			g_deck.slides.insert (g_cur + 1 + i, s);
		}
		g_cur++; end_edit (); g_sel.clear ();
		done_change (); notify_slides (); after ();
		return;
	}
	if (own_clip () && g_clipKind == 1 && g_clipObj.n && !g_edit)
	{
		Slide *s = cur_slide (); if (!s) return;
		begin_change ();
		g_sel.clear ();
		for (int i = 0; i < g_clipObj.n; i++)
		{
			Object *o = obj_copy (g_clipObj[i]); o->id = g_deck.nextId++;
			o->x += 400; o->y += 400; g_clipObj[i]->x += 400; g_clipObj[i]->y += 400;
			if (o->ph != PH_NONE && o->kind == OB_TEXT) { bool has = false; for (int k = 0; k < s->obj.n; k++) if (s->obj[k]->ph == o->ph) has = true; if (has) o->ph = PH_NONE; }
			s->obj.push (o); g_sel.push (o->id);
		}
		done_change (); if (g_onSlides) g_onSlides (); after ();
		return;
	}
	// text: into the text edited, or a new text box
	char *t = (char *) malloc (65536); long n = t ? ui_clip_get ((long) t, 65536) : 0;
	if (n > 0)
	{
		unsigned *u = (unsigned *) malloc (sizeof (unsigned) * (n + 1)); int m = 0;
		for (int i = 0; i < n; ) { int l; u[m++] = ss::u8_dec (t + i, (int) n - i, &l); i += l > 0 ? l : 1; }
		if (g_edit) sv_type (u, m);
		else
		{
			Object *o = make_text_box (g_deck.sw / 10, g_deck.sh / 3, g_deck.sw * 8 / 10, 1500);
			o->tb.clear (); o->tb.ensure (); tb_insert (o->tb, tpos (0, 0), u, m, cf_inherit ());
			add_object (o);
		}
		free (u);
	}
	free (t);
	after ();
}
static void cmd_delete ()
{
	if (g_edit) { key_on (K_DEL, false, false); return; }
	if (g_viewMode == VIEW_SORTER || list_focus ()) { cmd_delete_slide (); after (); return; }
	cmd_delete_objects (); after ();
}
static void cmd_select_all ()
{
	if (notes_focus ()) { ui_textarea_edit (np_ta, UTE_ALL); return; }
	if (field_focus ()) { key_to_focus (0x04); return; }
	if (g_edit) { TextBody *tb = edit_body (); int lp = tb->p.n - 1; g_anchor = tpos (0, 0); g_caret = tpos (lp, tb->p[lp]->len); after (); return; }
	Slide *s = cur_slide (); if (!s) return;
	g_sel.clear (); for (int i = 0; i < s->obj.n; i++) g_sel.push (s->obj[i]->id);
	after (); focus_view ();
}
static void cmd_duplicate ()
{
	if (g_viewMode == VIEW_SORTER || list_focus () || !g_sel.n) { cmd_duplicate_slide (); after (); return; }
	Slide *s = cur_slide (); if (!s) return;
	begin_change ();
	Vec<int> nsel;
	for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (!o) continue; Object *c = obj_copy (o); c->id = g_deck.nextId++; c->x += 400; c->y += 400; if (c->ph != PH_NONE) c->ph = PH_NONE; s->obj.push (c); nsel.push (c->id); }
	g_sel.clear (); for (int i = 0; i < nsel.n; i++) g_sel.push (nsel[i]);
	done_change (); if (g_onSlides) g_onSlides (); after ();
}
static void cmd_new_slide_btn () { cmd_new_slide (); sl_show_cur (); after (); focus_view (); }
static void cmd_new_slide_layout ()
{
	int k = layout_pick (-1, "New slide");
	if (k >= 0 && k < LY_COUNT) { cmd_new_slide (k); sl_show_cur (); after (); }
	focus_view ();
}
static void cmd_layout ()
{
	Slide *s = cur_slide (); if (!s) return;
	int k = layout_pick (s->layout, "The slide's layout");
	if (k == 100) set_layout (s->layout);
	else if (k >= 0 && k < LY_COUNT) set_layout (k);
	after (); focus_view ();
}
static void cmd_dup_slide () { cmd_duplicate_slide (); sl_show_cur (); after (); }
static void cmd_del_slide () { cmd_delete_slide (); g_thumbs.prune (); after (); }
static void set_tool (int t) { g_tool = t; end_edit (); g_sel.clear (); after (); focus_view (); }
static void cmd_shape ()
{
	int k = shape_pick (true);
	if (k == SH_COUNT) set_tool (TOOL_LINE);
	else if (k == SH_COUNT + 1) set_tool (TOOL_ARROW);
	else if (k >= 0) { g_toolShape = k; set_tool (TOOL_SHAPE); }
	else focus_view ();
}
static void picture_into (Object *ph)
{
	char path[200];
	ui_file_filter ((long) "*.png;*.jpg;*.jpeg;*.bmp");
	bool got = ui_file_open ((long) DOCS, (long) path, sizeof path) != 0;
	ui_file_filter (0);
	if (!got) { focus_view (); return; }
	unsigned n = 0; unsigned char *b = read_all (path, &n);
	if (!b) { focus_view (); return; }
	Object *o = make_picture (base_name (path), b, n, ph);
	free (b);
	if (!o) { ask ("Insert Picture", "That file is not a picture Slides can read (PNG, JPEG, BMP).", UM_OK); focus_view (); return; }
	if (ph)
	{
		// the placeholder replaced by the picture (in its place in the order)
		Slide *s = cur_slide ();
		begin_change ();
		int i = s->index_of (ph->id);
		o->ph = PH_PICTURE;
		if (i >= 0) { delete s->obj[i]; s->obj[i] = o; } else s->obj.push (o);
		g_sel.clear (); g_sel.push (o->id);
		done_change (); if (g_onSlides) g_onSlides ();
	}
	else add_object (o);
	after (); focus_view ();
}
static void cmd_picture ()
{
	// an empty picture placeholder on the slide: filled
	Slide *s = cur_slide ();
	for (int i = 0; s && i < s->obj.n; i++) if (s->obj[i]->ph == PH_PICTURE && s->obj[i]->kind != OB_PICTURE) { picture_into (s->obj[i]); return; }
	picture_into (0);
}
static void cmd_table ()
{
	int DW = 290, c = ctl_h () + 8, DH = 12 + 2 * c + ctl_h () + 20;
	long d = ui_dialog_begin (DW, DH, (long) "Insert Table");
	if (!d) return;
	d_label (16, 12, 90, "Rows"); long rows_ = ui_numeric (120, 12, 110, ctl_h (), 1, 30, 4);
	d_label (16, 12 + c, 90, "Columns"); long cols = ui_numeric (120, 12 + c, 110, ctl_h (), 1, 12, 3);
	d_ok_cancel (d, DW);
	long a = ui_dialog_run (d, 0, 0);
	int r = iclamp ((int) ui_w_get_value (rows_), 1, 30), cc = iclamp ((int) ui_w_get_value (cols), 1, 12);
	ui_dialog_end (d);
	if (a == 1)
	{
		Object *o = make_table (r, cc);
		// the body placeholder's place, when the slide has an empty one
		Slide *s = cur_slide ();
		for (int i = 0; s && i < s->obj.n; i++)
			if ((s->obj[i]->ph == PH_BODY || s->obj[i]->ph == PH_BODY2) && s->obj[i]->tb.empty ())
			{
				Object *p = s->obj[i];
				o->x = p->x; o->y = p->y; for (int k = 0; k < cc; k++) o->tbl->colW[k] = p->w / cc; o->w = p->w;
				begin_change (); delete p; s->obj.erase (i); s->obj.push (o); g_sel.clear (); g_sel.push (o->id); done_change (); if (g_onSlides) g_onSlides (); after (); focus_view ();
				return;
			}
		add_object (o);
	}
	after (); focus_view ();
}
static void cmd_chart_type (int type)
{
	Object *o = make_chart (type);
	if (!chart_dialog (*o->chart)) { delete o; focus_view (); return; }
	add_object (o);
	after (); focus_view ();
}
static void cmd_chart ()
{
	int k = popup_list (120, TB_H, 160, CHART_NAMES, CH_COUNT);
	if (k >= 0) cmd_chart_type (k); else focus_view ();
}
static void chart_data (Object *o)
{
	if (!o || !o->chart) return;
	Chart c = *o->chart;
	if (chart_dialog (c)) { begin_change (); *o->chart = c; done_change (); after (); }
	focus_view ();
}
static void cmd_arr (int d) { cmd_arrange (d); after (); }
// the text's formats
static void flag (unsigned short f) { CharFmt c = shown_format (); apply_flag (f, !(c.flags & f)); after (); if (g_edit) focus_view (); }
static void para_cmd (ParaFn fn, int a) { apply_para (fn, a); after (); if (g_edit) focus_view (); }
static void cmd_bullets () { ParaFmt p = shown_para (); para_cmd (pf_bullet, p.bullet == BU_BULLET ? BU_NONE : BU_BULLET); }
static void cmd_numbers () { ParaFmt p = shown_para (); para_cmd (pf_bullet, p.bullet == BU_NUMBER ? BU_NONE : BU_NUMBER); }
static void set_anchor (int a)
{
	if (!g_sel.n && !g_edit) return;
	begin_change ();
	for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (o) o->tb.anchor = (signed char) a; }
	done_change (); after ();
}
static void cmd_spacing ()
{
	ParaFmt p = shown_para ();
	static const int sp[] = { 90, 100, 115, 150, 200 };
	int i = 0; for (int k = 0; k < 5; k++) if (p.spacing >= sp[k]) i = k;
	para_cmd (pf_spacing, sp[(i + 1) % 5]);
}
// the zoom
static const int ZOOMS[] = { 25, 33, 50, 66, 75, 100, 125, 150, 200, 300 };
static void zoom_to (int z) { sv_set_zoom (z); after (); }
static void cmd_zoom_in () { int z = sv_zoom_pct (); for (int i = 0; i < 10; i++) if (ZOOMS[i] > z + 1) { zoom_to (ZOOMS[i]); return; } }
static void cmd_zoom_out () { int z = sv_zoom_pct (); for (int i = 9; i >= 0; i--) if (ZOOMS[i] < z - 1) { zoom_to (ZOOMS[i]); return; } }
static void cmd_zoom_fit () { zoom_to (0); }
static void pick_font ()
{
	int k = popup_list (8, 2 * TB_H, 200, FONT_NAMES, 3);
	if (k >= 0) { FmtChange c; c.what = FC_FONT; c.font = (short) g_deck.font_index (FONT_NAMES[k]); apply_format (c); after (); }
	focus_view ();
}
static const char *const SIZE_ROWS[] = { "8", "10", "12", "14", "16", "18", "20", "24", "28", "32", "36", "40", "44", "48", "54", "60", "72", "88" };
static void pick_size ()
{
	int r = popup_list (140, 2 * TB_H, 70, SIZE_ROWS, 18);
	if (r >= 0) { FmtChange c; c.what = FC_SIZE; c.size = (short) (atoi (SIZE_ROWS[r]) * 10); apply_format (c); after (); }
	focus_view ();
}

// ---- the views, the show -------------------------------------------------------------------------------------------
static void set_view (int v) { g_viewMode = v; end_edit (); relayout (); after (); focus_view (); }
static void cmd_sorter () { if (g_master) return; g_sorterSel.clear (); set_view (VIEW_SORTER); }
// View > Master: the master and its layouts edited as slides (master.h)
static void cmd_master ()
{
	if (g_master) master_close (); else master_open ();
	g_thumbs.clear (); C.drop_all (); np_reset ();
	sl_top = 0;
	set_view (VIEW_NORMAL);
}
static void cmd_notes () { g_showNotes = !g_showNotes; relayout (); after (); }
static void open_slide (int i) { go_slide (i); set_view (VIEW_NORMAL); }
static Show g_show;
static void show (int from, bool presenter)
{
	if (g_master) { master_close (); g_thumbs.clear (); from = presenter || from ? g_cur : 0; }
	end_edit ();
	g_show.run (from, presenter);
	after ();
	focus_view ();
}
static void play_effects () { show (g_cur, false); }

// ---- the sections, the slide's menu ----------------------------------------------------------------------------------
static void cmd_add_section ()
{
	Slide *s = cur_slide (); if (!s) return;
	char n[48]; scpy (n, s->section[0] ? s->section : TR ("New section"), sizeof n);
	if (ask_line ("Section", "The section starting at this slide:", n, sizeof n)) { begin_change (); scpy (s->section, n[0] ? n : TR ("Section"), sizeof s->section); done_change (); notify_slides (); }
	after (); focus_view ();
}
static void cmd_remove_section () { Slide *s = cur_slide (); if (!s || !s->section[0]) return; begin_change (); s->section[0] = 0; done_change (); notify_slides (); after (); }
static void cmd_hide () { cmd_hide_slide (); after (); }
static void cmd_footer ()
{
	char t[96]; scpy (t, g_deck.footerText, sizeof t);
	if (ask_line ("Header and Footer", "The footer on every slide (empty: none):", t, sizeof t)) { begin_change (); scpy (g_deck.footerText, t, sizeof g_deck.footerText); g_deck.footer = t[0] != 0; done_change (); notify_slides (); }
	after (); focus_view ();
}
static void slide_menu (int x, int y)
{
	Slide *s = cur_slide ();
	bool sec = s && s->section[0];
	const char *M[12]; int id[12], n = 0;
	auto add = [&] (const char *t, int i) { M[n] = t; id[n] = i; n++; };
	add ("New Slide", 1); add ("Duplicate Slide", 2); add ("Delete Slide", 3); add ("-", 0);
	add (s && s->hidden ? "Show Slide" : "Hide Slide", 4); add ("Layout...", 5); add ("-", 0);
	add (sec ? "Rename Section..." : "Add Section Here...", 6);
	if (sec) add ("Remove Section", 7);
	add ("-", 0); add ("Copy", 8); add ("Paste", 9);
	int k = popup_list (x, y, 190, M, n);
	switch (k >= 0 ? id[k] : 0)
	{
	case 1: cmd_new_slide_btn (); break;
	case 2: cmd_dup_slide (); break;
	case 3: cmd_del_slide (); break;
	case 4: cmd_hide (); break;
	case 5: cmd_layout (); break;
	case 6: cmd_add_section (); break;
	case 7: cmd_remove_section (); break;
	case 8: ui_w_focus (sl_v); cmd_copy (); break;
	case 9: ui_w_focus (sl_v); cmd_paste (); break;
	}
}
static void view_menu (int x, int y)
{
	const char *M[16]; int id[16], n = 0;
	auto add = [&] (const char *t, int i) { M[n] = t; id[n] = i; n++; };
	if (g_sel.n > 0)
	{
		add ("Cut", 1); add ("Copy", 2); add ("Paste", 3); add ("Delete", 4); add ("-", 0);
		Object *o = sel_one ();
		if (o && (o->kind == OB_TEXT || o->kind == OB_SHAPE)) add ("Edit Text", 5);
		if (o && o->kind == OB_CHART) add ("Chart Data...", 12);
		add ("Bring to Front", 6); add ("Send to Back", 7); add ("-", 0);
		add ("Format...", 8); add ("Animate...", 9);
	}
	else { add ("Paste", 3); add ("-", 0); add ("New Slide", 10); add ("Layout...", 11); add ("Background...", 13); }
	int k = popup_list (x, y, 180, M, n);
	switch (k >= 0 ? id[k] : 0)
	{
	case 1: cmd_cut (); break; case 2: cmd_copy (); break; case 3: cmd_paste (); break; case 4: cmd_delete (); break;
	case 5: { Object *o = sel_one (); if (o) { begin_edit (o->id); after (); } break; }
	case 6: cmd_arr (2); break; case 7: cmd_arr (-2); break;
	case 8: g_sbTab = SB_SHAPE; ui_w_set_value (sb_seg, g_sbTab); after (); break; case 9: g_sbTab = SB_ANIM; ui_w_set_value (sb_seg, g_sbTab); after (); break;
	case 10: cmd_new_slide_btn (); break; case 11: cmd_layout (); break; case 12: chart_data (sel_one ()); break;
	case 13: g_sbTab = SB_SLIDE; ui_w_set_value (sb_seg, g_sbTab); after (); break;
	}
	focus_view ();
}

// ---- the sidebar's actions --------------------------------------------------------------------------------------------
static int sb_list (int x, int y, int w, const char *const *items, int n, int) { return popup_list (x, y, imax (w, 160), items, n); }
static void each_sel (void (*fn) (Object &, double, unsigned), double v, unsigned c)
{
	begin_change ();
	for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (o) fn (*o, v, c); }
	done_change (); if (g_onSlides) g_onSlides ();
}
void sl::sb_pick_list (SRow &r, int x, int y, int w)
{
	Slide *s = cur_slide (); if (!s) return;
	Object *o = g_sel.n ? obj_of (g_sel[0]) : 0;
	int k = -1;
	switch (r.id)
	{
	case I_LAYOUT: k = layout_pick (s->layout, "The slide's layout"); if (k == 100) set_layout (s->layout); else if (k >= 0 && k < LY_COUNT) set_layout (k); break;
	case I_FONT_MAJOR: case I_FONT_MINOR: case I_FONT:
	{
		k = popup_list (x, y, imax (w, 200), FONT_NAMES, 3);
		if (k < 0) break;
		if (r.id == I_FONT) { FmtChange c; c.what = FC_FONT; c.font = (short) g_deck.font_index (FONT_NAMES[k]); apply_format (c); }
		else { begin_change (); scpy (r.id == I_FONT_MAJOR ? g_deck.theme.major : g_deck.theme.minor, FONT_NAMES[k], 48); done_change (); notify_slides (); }
		break;
	}
	case I_LAYOUT_NAME:
	{
		char n[32]; scpy (n, TR (g_deck.layout[g_cur - 1].name), sizeof n);
		if (g_master && g_cur > 0 && ask_line ("Layout", "The layout's name:", n, sizeof n) && n[0]) { begin_change (); scpy (g_deck.layout[g_cur - 1].name, n, 32); done_change (); }
		break;
	}
	case I_BG_TYPE:
	{
		if (master_slide ())
		{
			static const char *T2[2] = { "Solid colour", "Gradient" };
			k = sb_list (x, y, w, T2, 2, g_deck.masterBg.type == FILL_GRADIENT);
			if (k < 0) break;
			begin_change ();
			s->bg = g_deck.masterBg; s->bg.type = k ? FILL_GRADIENT : FILL_SOLID;
			if (k && s->bg.c2 == s->bg.c1) s->bg.c2 = THEME | TC_LT2;
			done_change (); notify_slides ();
			break;
		}
		static const char *T[3] = { "The theme's", "Solid colour", "Gradient" };
		k = sb_list (x, y, w, T, 3, s->bg.type == FILL_INHERIT ? 0 : s->bg.type == FILL_GRADIENT ? 2 : 1);
		if (k < 0) break;
		begin_change ();
		Fill cur = s->bg.type == FILL_INHERIT ? g_deck.masterBg : s->bg;
		if (k == 0) s->bg.type = FILL_INHERIT;
		else { s->bg = cur; s->bg.type = k == 2 ? FILL_GRADIENT : FILL_SOLID; if (k == 2 && s->bg.c2 == s->bg.c1) s->bg.c2 = THEME | TC_LT2; }
		done_change (); notify_slides ();
		break;
	}
	case I_SIZE:
		k = sb_list (x, y, w, SIZE_NAMES, 4, -1);
		if (k >= 0)
		{
			begin_change ();
			double fx = (double) SIZE_W[k] / g_deck.sw, fy = (double) SIZE_H[k] / g_deck.sh;
			auto scale = [&] (Object *ob) { ob->x = (int) (ob->x * fx); ob->y = (int) (ob->y * fy); ob->w = (int) (ob->w * fx); ob->h = (int) (ob->h * fy); };
			for (int i = 0; i < g_deck.slides.n; i++) for (int j = 0; j < g_deck.slides[i]->obj.n; j++) scale (g_deck.slides[i]->obj[j]);
			for (int i = 0; i < g_deck.decor.n; i++) scale (g_deck.decor[i]);
			for (int l = 0; l < LY_COUNT; l++) for (int j = 0; j < g_deck.layout[l].ph.n; j++) scale (g_deck.layout[l].ph[j]);
			g_deck.sw = SIZE_W[k]; g_deck.sh = SIZE_H[k];
			g_thumbs.clear ();
			done_change (); notify_slides ();
		}
		break;
	case I_FILL_TYPE:
		k = sb_list (x, y, w, FILL_NAMES, 3, -1);
		if (k >= 0 && o) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (!q) continue; q->fill.type = (signed char) (k == 0 ? FILL_NONE : k == 1 ? FILL_SOLID : FILL_GRADIENT); if (k == 2 && q->fill.c2 == q->fill.c1) q->fill.c2 = THEME | TC_LT1; } done_change (); notify_slides (); }
		break;
	case I_LINE_TYPE:
		k = sb_list (x, y, w, LINE_NAMES, 4, -1);
		if (k >= 0 && o) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) q->line.type = (signed char) k; } done_change (); notify_slides (); }
		break;
	case I_HEAD0: case I_HEAD1:
		k = sb_list (x, y, w, HEAD_NAMES, 4, -1);
		if (k >= 0 && o) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) { if (r.id == I_HEAD0) q->line.head0 = (signed char) k; else q->line.head1 = (signed char) k; } } done_change (); notify_slides (); }
		break;
	case I_SHAPE:
		k = shape_pick (false);
		if (k >= 0 && o) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q && (q->kind == OB_SHAPE || q->kind == OB_PICTURE)) q->shape = (signed char) k; } done_change (); notify_slides (); }
		break;
	case I_TBL_ROWS:
	{
		static const char *T[6] = { "Insert a row below", "Insert a column at the right", "Delete the last row", "Delete the last column", "Columns evenly", "Edit the first cell" };
		k = sb_list (x, y, w, T, 6, -1);
		if (k < 0 || !o || !o->tbl) break;
		begin_change ();
		Table *t = o->tbl;
		int nr = t->rows + (k == 0) - (k == 2 && t->rows > 1), nc = t->cols + (k == 1) - (k == 3 && t->cols > 1);
		if (k <= 3 && (nr != t->rows || nc != t->cols))
		{
			Table *n = new Table (nr, nc);
			for (int c = 0; c < nc; c++) n->colW[c] = c < t->cols ? t->colW[c] : t->colW[t->cols - 1];
			for (int rr = 0; rr < nr; rr++) n->rowH[rr] = rr < t->rows ? t->rowH[rr] : t->rowH[t->rows - 1];
			for (int rr = 0; rr < nr; rr++) for (int c = 0; c < nc; c++) { if (rr < t->rows && c < t->cols) { n->at (rr, c).copy_from (t->at (rr, c)); n->cfill[rr * nc + c] = t->cfill[rr * t->cols + c]; } else { n->at (rr, c).inset[0] = n->at (rr, c).inset[2] = 200; n->at (rr, c).inset[1] = n->at (rr, c).inset[3] = 100; } }
			n->header = t->header; n->banded = t->banded;
			if (k == 1) { int sum = 0; for (int c = 0; c < nc; c++) sum += n->colW[c]; for (int c = 0; c < nc; c++) n->colW[c] = n->colW[c] * o->w / sum; }
			delete t; o->tbl = n;
		}
		else if (k == 4) { int sum = 0; for (int c = 0; c < t->cols; c++) sum += t->colW[c]; for (int c = 0; c < t->cols; c++) t->colW[c] = sum / t->cols; }
		done_change (); notify_slides ();
		if (k == 5) begin_edit (o->id, 0, 0);
		break;
	}
	case I_CHART_TYPE:
		k = sb_list (x, y, w, CHART_NAMES, CH_COUNT, o && o->chart ? o->chart->type : -1);
		if (k >= 0 && o && o->chart) { begin_change (); o->chart->type = k; if (k == CH_PIE) o->chart->nser = imin (o->chart->nser, 1); done_change (); notify_slides (); }
		break;
	case I_BULLET:
		k = sb_list (x, y, w, BULLET_NAMES, 3, -1);
		if (k >= 0) apply_para (pf_bullet, k);
		break;
	case I_FIT:
		k = sb_list (x, y, w, FIT_NAMES, 3, -1);
		if (k >= 0) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) q->tb.fit = (signed char) k; } done_change (); notify_slides (); }
		break;
	case I_TR_TYPE:
		k = sb_list (x, y, w, TR_NAMES, TR_COUNT, s->tr.type);
		if (k >= 0) { begin_change (); s->tr.type = (signed char) k; done_change (); notify_slides (); }
		break;
	case I_TR_DIR:
		k = sb_list (x, y, w, DIR_NAMES + 1, 4, s->tr.dir - 1);
		if (k >= 0) { begin_change (); s->tr.dir = (signed char) (k + 1); done_change (); }
		break;
	case I_TR_ADV:
		k = sb_list (x, y, w, ADV_NAMES, 2, s->tr.after >= 0);
		if (k >= 0) { begin_change (); s->tr.after = k ? (s->tr.after >= 0 ? s->tr.after : 5000) : -1; done_change (); notify_slides (); }
		break;
	case I_FX_ADD:
	{
		if (!g_sel.n) break;
		// the class, then the effect
		static const char *A[12] = { "Entrance: Appear", "Entrance: Fade", "Entrance: Fly in", "Entrance: Wipe", "Entrance: Zoom", "Entrance: Float in",
					     "Emphasis: Pulse", "Emphasis: Grow", "Emphasis: Spin", "Exit: Fade", "Exit: Fly out", "Exit: Zoom" };
		static const signed char CL[12] = { AC_ENTRANCE, AC_ENTRANCE, AC_ENTRANCE, AC_ENTRANCE, AC_ENTRANCE, AC_ENTRANCE, AC_EMPHASIS, AC_EMPHASIS, AC_EMPHASIS, AC_EXIT, AC_EXIT, AC_EXIT };
		static const signed char FX[12] = { FX_APPEAR, FX_FADE, FX_FLY, FX_WIPE, FX_ZOOM, FX_FLOAT, FX_PULSE, FX_GROW, FX_SPIN, FX_FADE, FX_FLY, FX_ZOOM };
		k = sb_list (x, y, w, A, 12, -1);
		if (k < 0) break;
		begin_change ();
		for (int i = 0; i < g_sel.n; i++)
		{
			Anim a; a.obj = g_sel[i]; a.cls = CL[k]; a.fx = FX[k]; a.start = (signed char) (i == 0 ? ST_CLICK : ST_WITH); a.dir = DIR_DOWN; a.byPara = false; a.delay = 0; a.dur = FX[k] == FX_APPEAR ? 0 : 500;
			s->anim.push (a);
		}
		g_animSel = s->anim.n - 1;
		done_change (); notify_slides ();
		break;
	}
	case I_FX_EFFECT: case I_FX_START: case I_FX_DIR:
	{
		if (g_animSel < 0 || g_animSel >= s->anim.n) break;
		Anim &a = s->anim[g_animSel];
		if (r.id == I_FX_EFFECT) { k = sb_list (x, y, w, FX_NAMES, FX_COUNT, a.fx); if (k >= 0) { begin_change (); s->anim[g_animSel].fx = (signed char) k; done_change (); } }
		else if (r.id == I_FX_START) { k = sb_list (x, y, w, START_NAMES, 3, a.start); if (k >= 0) { begin_change (); s->anim[g_animSel].start = (signed char) k; done_change (); } }
		else { k = sb_list (x, y, w, DIR_NAMES + 1, 4, a.dir - 1); if (k >= 0) { begin_change (); s->anim[g_animSel].dir = (signed char) (k + 1); done_change (); } }
		notify_slides ();
		break;
	}
	}
	after ();
}
static void set_fill_c1 (Object &o, double, unsigned c) { o.fill.c1 = c; if (o.fill.type == FILL_NONE) o.fill.type = FILL_SOLID; }
static void set_fill_c2 (Object &o, double, unsigned c) { o.fill.c2 = c; }
static void set_fill_angle (Object &o, double v, unsigned) { o.fill.angle = (short) v; }
static void set_fill_alpha (Object &o, double v, unsigned) { o.fill.alpha = (unsigned char) (255 - v * 255 / 100); }
static void set_line_c (Object &o, double, unsigned c) { o.line.color = c; if (o.line.type == LN_NONE) o.line.type = LN_SOLID; }
static void set_line_w (Object &o, double v, unsigned) { o.line.width = (short) (v * 35.278); }
static void set_radius (Object &o, double v, unsigned) { o.radius = (short) (v * 10); }
static void set_shadow (Object &o, double v, unsigned) { o.shadow = v != 0; }
static void set_x (Object &o, double v, unsigned) { o.x = (int) (v * 1000); }
static void set_y (Object &o, double v, unsigned) { o.y = (int) (v * 1000); }
static void set_w (Object &o, double v, unsigned) { o.w = imax (100, (int) (v * 1000)); }
static void set_h (Object &o, double v, unsigned) { o.h = imax (100, (int) (v * 1000)); }
static void set_rot (Object &o, double v, unsigned) { o.rot = (short) (((int) v % 360 + 360) % 360); }
static void set_flip (Object &o, double v, unsigned) { o.flipH = v != 0; }
static void set_crop (Object &o, double v, unsigned) { o.crop[0] = o.crop[2] = (short) (v * 10); }
static void set_wrap (Object &o, double v, unsigned) { o.tb.wrap = v != 0; }
void sl::sb_act (const SRow &r, int sub, double v)
{
	Slide *s = cur_slide (); if (!s) return;
	Object *o = g_sel.n ? obj_of (g_sel[0]) : 0;
	switch (r.id)
	{
	case I_RESET: set_layout (s->layout); break;
	case I_THEME:
	{
		begin_change ();
		char major[48], minor[48]; scpy (major, g_deck.theme.major, 48); scpy (minor, g_deck.theme.minor, 48);
		theme_set (g_deck.theme, sub);
		scpy (g_deck.theme.major, major, 48); scpy (g_deck.theme.minor, minor, 48);
		done_change (); notify_slides ();
		break;
	}
	case I_MASTER_VIEW: cmd_master (); return;
	case I_PH_TITLE: master_add_placeholder (PH_TITLE); break;
	case I_PH_TEXT: master_add_placeholder (PH_BODY); break;
	case I_PH_PIC: master_add_placeholder (PH_PICTURE); break;
	case I_BG_C1: case I_BG_C2:
		begin_change ();
		if (master_slide ()) s->bg = g_deck.masterBg;
		if (s->bg.type == FILL_INHERIT) { s->bg = g_deck.masterBg; s->bg.type = FILL_SOLID; }
		if (sub == 1) s->bg.type = FILL_INHERIT;
		else if (r.id == I_BG_C1) s->bg.c1 = r.color; else s->bg.c2 = r.color;
		done_change (); notify_slides ();
		break;
	case I_BG_ANGLE: begin_change (); if (master_slide ()) s->bg = g_deck.masterBg; s->bg.angle = (short) v; done_change (); notify_slides (); break;
	case I_MASTER_OBJ: begin_change (); s->masterObjects = v != 0; done_change (); notify_slides (); break;
	case I_NUMBER: begin_change (); g_deck.number = v != 0; done_change (); notify_slides (); break;
	case I_FOOTER: if (v != 0 && !g_deck.footerText[0]) cmd_footer (); else { begin_change (); g_deck.footer = v != 0; done_change (); notify_slides (); } break;
	case I_HIDDEN: begin_change (); s->hidden = v != 0; done_change (); notify_slides (); break;
	case I_FILL_C1: if (sub == 1) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) q->fill.type = FILL_NONE; } done_change (); } else each_sel (set_fill_c1, 0, r.color); break;
	case I_FILL_C2: each_sel (set_fill_c2, 0, r.color); break;
	case I_FILL_ANGLE: each_sel (set_fill_angle, v, 0); break;
	case I_FILL_ALPHA: each_sel (set_fill_alpha, v, 0); break;
	case I_LINE_C: if (sub == 1) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) q->line.type = LN_NONE; } done_change (); } else each_sel (set_line_c, 0, r.color); break;
	case I_LINE_W: each_sel (set_line_w, v, 0); break;
	case I_RADIUS: each_sel (set_radius, v, 0); break;
	case I_SHADOW: each_sel (set_shadow, v, 0); break;
	case I_CROP: each_sel (set_crop, v, 0); break;
	case I_X: each_sel (set_x, v, 0); break;
	case I_Y: each_sel (set_y, v, 0); break;
	case I_W: each_sel (set_w, v, 0); break;
	case I_H: each_sel (set_h, v, 0); break;
	case I_ROT: each_sel (set_rot, v, 0); break;
	case I_FLIP: each_sel (set_flip, v, 0); break;
	case I_ORDER: cmd_arrange (sub == 0 ? 2 : sub == 1 ? 1 : sub == 2 ? -1 : -2); break;
	case I_ALIGN: if (sub < 6) cmd_align (sub); else cmd_distribute (sub == 7); break;
	case I_TBL_HEAD: if (o && o->tbl) { begin_change (); o->tbl->header = v != 0; done_change (); notify_slides (); } break;
	case I_TBL_BAND: if (o && o->tbl) { begin_change (); o->tbl->banded = v != 0; done_change (); notify_slides (); } break;
	case I_CHART_LEGEND: if (o && o->chart) { begin_change (); o->chart->legend = v != 0; done_change (); notify_slides (); } break;
	case I_CHART_LABELS: if (o && o->chart) { begin_change (); o->chart->labels = v != 0; done_change (); notify_slides (); } break;
	case I_CHART_DATA: chart_data (o); break;
	case I_SIZE_T: { FmtChange c; c.what = FC_SIZE; c.size = (short) (v * 10); apply_format (c); break; }
	case I_COLOR: { FmtChange c; c.what = FC_COLOR; c.color = sub == 1 ? AUTO : r.color; apply_format (c); break; }
	case I_STYLE: { static const unsigned short F[6] = { CF_BOLD, CF_ITALIC, CF_UNDER, CF_STRIKE, CF_SUPER, CF_SUB }; CharFmt c = shown_format (); apply_flag (F[sub], !(c.flags & F[sub])); break; }
	case I_ALIGN_T: apply_para (pf_align, sub); break;
	case I_LEVEL: { ParaFmt p = shown_para (); apply_para (pf_level, (int) v - 1 - p.level); break; }
	case I_SPACING: apply_para (pf_spacing, (int) (v * 100 + 0.5)); break;
	case I_BEFORE: { static int sv; sv = (int) (v * 10); apply_para ([] (ParaFmt &p, int a) { p.before = (short) a; }, sv); break; }
	case I_AFTER: { static int sv; sv = (int) (v * 10); apply_para ([] (ParaFmt &p, int a) { p.after = (short) a; }, sv); break; }
	case I_ANCHOR: set_anchor (sub); break;
	case I_WRAP: each_sel (set_wrap, v, 0); break;
	case I_TR_DUR: begin_change (); s->tr.dur = (short) (v * 1000); done_change (); break;
	case I_TR_AFTER: begin_change (); s->tr.after = (int) (v * 1000); done_change (); notify_slides (); break;
	case I_TR_ALL: begin_change (); for (int i = 0; i < g_deck.slides.n; i++) g_deck.slides[i]->tr = s->tr; done_change (); notify_slides (); break;
	case I_FX_TOOLS:
		if (g_animSel < 0 || g_animSel >= s->anim.n) break;
		begin_change ();
		if (sub == 0) { s->anim.erase (g_animSel); if (g_animSel >= s->anim.n) g_animSel = s->anim.n - 1; }
		else
		{
			int to = g_animSel + (sub == 1 ? -1 : 1);
			if (to >= 0 && to < s->anim.n) { Anim t = s->anim[to]; s->anim[to] = s->anim[g_animSel]; s->anim[g_animSel] = t; g_animSel = to; }
		}
		done_change (); notify_slides ();
		break;
	case I_FX_DELAY: if (g_animSel >= 0 && g_animSel < s->anim.n) { begin_change (); s->anim[g_animSel].delay = (short) (v * 1000); done_change (); } break;
	case I_FX_DUR: if (g_animSel >= 0 && g_animSel < s->anim.n) { begin_change (); s->anim[g_animSel].dur = (short) (v * 1000); done_change (); } break;
	case I_FX_PARA: if (g_animSel >= 0 && g_animSel < s->anim.n) { begin_change (); s->anim[g_animSel].byPara = v != 0; done_change (); } break;
	case I_FX_PLAY: play_effects (); break;
	}
	after ();
}



// ---- the commands by number -------------------------------------------------------------------------------------------
static void command (int id)
{
	switch (id)
	{
	case M_NEW: cmd_new (); break;
	case M_OPEN: cmd_open (); break;
	case M_SAVE: cmd_save (); break;
	case M_SAVEAS: cmd_save_as (); break;
	case M_PDF: cmd_export_pdf (); break;
	case M_PNG: cmd_export_png (); break;
	case M_PRINT: cmd_print (); break;
	case M_UNDO: ui_undo (); break;
	case M_REDO: ui_redo (); break;
	case M_CUT: cmd_cut (); break;
	case M_COPY: cmd_copy (); break;
	case M_PASTE: cmd_paste (); break;
	case M_DUP: cmd_duplicate (); break;
	case M_DEL: cmd_delete (); break;
	case M_ALL: cmd_select_all (); break;
	case M_FIND: cmd_find (); break;
	case M_NORMAL: set_view (VIEW_NORMAL); break;
	case M_SORTER: cmd_sorter (); break;
	case M_NOTES: cmd_notes (); break;
	case M_THUMBS: g_showThumbs = !g_showThumbs; relayout (); after (); break;
	case M_SIDE: g_showSide = !g_showSide; relayout (); after (); break;
	case M_MASTER: cmd_master (); break;
	case M_FIT: cmd_zoom_fit (); break;
	case M_ZIN: cmd_zoom_in (); break;
	case M_ZOUT: cmd_zoom_out (); break;
	case M_NEWSL: cmd_new_slide_btn (); break;
	case M_NEWSLL: cmd_new_slide_layout (); break;
	case M_DUPSL: cmd_dup_slide (); break;
	case M_TEXT: set_tool (TOOL_TEXT); break;
	case M_PIC: cmd_picture (); break;
	case M_TABLE: cmd_table (); break;
	case M_CHART: cmd_chart (); break;
	case M_SHAPE: cmd_shape (); break;
	case M_ARROW: set_tool (TOOL_ARROW); break;
	case M_LINE: set_tool (TOOL_LINE); break;
	case M_FOOTER: cmd_footer (); break;
	case M_BOLD: flag (CF_BOLD); break;
	case M_ITALIC: flag (CF_ITALIC); break;
	case M_UNDER: flag (CF_UNDER); break;
	case M_BULLET: cmd_bullets (); break;
	case M_NUMBER: cmd_numbers (); break;
	case M_FRONT: cmd_arr (2); break;
	case M_FORW: cmd_arr (1); break;
	case M_BACKW: cmd_arr (-1); break;
	case M_BACK: cmd_arr (-2); break;
	case M_LAYOUT: cmd_layout (); break;
	case M_DELSL: cmd_del_slide (); break;
	case M_HIDE: cmd_hide (); break;
	case M_ADDSEC: cmd_add_section (); break;
	case M_REMSEC: cmd_remove_section (); break;
	case M_START: show (0, false); break;
	case M_HERE: show (g_cur, false); break;
	case M_PRESENTER: show (g_cur, true); break;
	}
}

// ---- the toolbars' state, the status bar ------------------------------------------------------------------------------
static void tool_enable (long w, bool on, int *was) { if (*was == (on ? 1 : 0)) return; *was = on ? 1 : 0; ui_w_enable (w, on); }
static void tool_on (int k, bool on)
{
	if (!g_btn[k] || (ui_w_get_value (g_btn[k]) != 0) == on) return;	// (a click flips the button: the state decides)
	ui_w_set_value (g_btn[k], on);
}
static void text_if (long w, char *was, int cap, const char *s) { if (!strcmp (was, s)) return; scpy (was, s, cap); ui_w_set_text (w, (long) s); }
static void refresh ()
{
	g_ui = false;
	np_load ();
	Slide *s = cur_slide ();
	CharFmt f = shown_format ();
	ParaFmt p = shown_para ();
	bool text = g_edit || (g_sel.n && obj_of (g_sel[0]) && obj_of (g_sel[0])->kind != OB_PICTURE && obj_of (g_sel[0])->kind != OB_LINE);
	tool_on (TF_BOLD, text && (f.flags & CF_BOLD)); tool_on (TF_ITALIC, text && (f.flags & CF_ITALIC));
	tool_on (TF_UNDER, text && (f.flags & CF_UNDER)); tool_on (TF_STRIKE, text && (f.flags & CF_STRIKE));
	tool_on (TF_SUPER, text && (f.flags & CF_SUPER)); tool_on (TF_SUB, text && (f.flags & CF_SUB));
	tool_on (TF_LEFT, text && p.align == AL_LEFT); tool_on (TF_CENTER, text && p.align == AL_CENTER);
	tool_on (TF_RIGHT, text && p.align == AL_RIGHT); tool_on (TF_JUSTIFY, text && p.align == AL_JUSTIFY);
	tool_on (TF_BULLETS, text && p.bullet == BU_BULLET); tool_on (TF_NUMBERS, text && p.bullet == BU_NUMBER);
	tool_on (TB_TEXT, g_tool == TOOL_TEXT); tool_on (TB_SHAPES, g_tool == TOOL_SHAPE || g_tool == TOOL_LINE);
	tool_on (TB_ARROW, g_tool == TOOL_ARROW);
	static int eUndo = -1, eRedo = -1, mUndo = -1, mRedo = -1;
	tool_enable (g_btn[TB_UNDO], g_undo.n > 0, &eUndo); tool_enable (g_btn[TB_REDO], g_redo.n > 0, &eRedo);
	if (mUndo != (g_undo.n > 0)) { mUndo = g_undo.n > 0; ui_menu_state (M_UNDO, mUndo ? 0 : UMS_DISABLED); }
	if (mRedo != (g_redo.n > 0)) { mRedo = g_redo.n > 0; ui_menu_state (M_REDO, mRedo ? 0 : UMS_DISABLED); }
	// the font, the size
	static char sFont[64], sSize[16], sZoom[16], sStatus[220];
	char t[220], u[64];
	snprintf (u, sizeof u, "%s \xE2\x96\xBE", g_deck.font_name (f.font)); text_if (g_bFont, sFont, sizeof sFont, u);
	if (f.size % 10) snprintf (u, sizeof u, "%.1f \xE2\x96\xBE", f.size / 10.0); else snprintf (u, sizeof u, "%d \xE2\x96\xBE", f.size / 10);
	text_if (g_bSize, sSize, sizeof sSize, u);
	static unsigned sColor = 0xFFFFFFFF; unsigned col = g_deck.rgb (f.color);
	if (col != sColor) { sColor = col; ui_w_set_value (g_cText, col); }
	// the status bar
	const char *sec = s ? section_of (g_cur) : "";
	if (g_msg[0] && ticks () - g_msgT < 400) scpy (t, g_msg, sizeof t);
	else if (g_master)
	{
		if (g_cur == 0) snprintf (t, sizeof t, TR ("Master view: the master     Theme: %s"), TR (g_deck.theme.name));
		else snprintf (t, sizeof t, TR ("Master view: layout %d of %d, %s"), g_cur, (int) LY_COUNT, TR (g_deck.layout[iclamp (g_cur - 1, 0, LY_COUNT - 1)].name));
	}
	else if (ui.w < 900) snprintf (t, sizeof t, TR ("Slide %d of %d     %s%s"), g_cur + 1, g_deck.slides.n, g_path[0] ? base_name (g_path) : TR ("Untitled"), changed_doc () ? " *" : "");
	else snprintf (t, sizeof t, TR ("Slide %d of %d%s%s%s     Theme: %s%s"), g_cur + 1, g_deck.slides.n, sec[0] ? "     " : "", sec, s && s->hidden ? TR ("  (hidden)") : "", TR (g_deck.theme.name), changed_doc () ? TR ("     (modified)") : "");
	text_if (g_lStatus, sStatus, sizeof sStatus, t);
	snprintf (u, sizeof u, "%d%%", sv_zoom_pct ()); text_if (g_bZoom, sZoom, sizeof sZoom, u);
	if (g_showSide && g_viewMode == VIEW_NORMAL) sb_refresh ();
}
static void on_slides () { g_thumbs.prune (); g_listDirty = true; }
static void on_wheel_slide (int dir) { go_slide (g_cur + dir); sl_show_cur (); after (); }
static void on_zoom () { g_ui = true; }
static void on_change () { after (); }

// The parts placed: the slides' column, the slide (or the sorter), the notes, the sidebar
static void place_right ()
{
	int cw = ui.w, sw = 96, x = cw - sw - 110;	// (left of the window's buttons)
	ui_w_move (g_bShow, x, (TB_H - ctl_h ()) / 2, sw, ctl_h ());
	// (the tools that would pass under it hide, the last first: all are in the menus)
	static const int LAST[] = { TB_ARROW, TB_SHAPES, TB_CHART, TB_TABLE, TB_PIC, TB_TEXT };
	int end = 8 + (TB_ARROW + 1) * (ctl_h () + 2) + 4 * 12;
	for (int k : LAST) { ui_w_show (g_btn[k], end <= x); end -= ctl_h () + 2; }
}
static void layout (int w, int h)
{
	int top = 2 * TB_H, bot = h - ST_H;
	bool normal = g_viewMode == VIEW_NORMAL;
	int listW = normal && g_showThumbs ? TH_W : 0, sideW = normal && g_showSide ? SB_W : 0;
	ui_w_move (g_tb1, 0, 0, w, TB_H); ui_w_move (g_tb2, 0, TB_H, w, TB_H);
	place_right ();
	ui_w_show (sl_v, listW > 0); ui_w_show (sv, normal); ui_w_show (so, !normal);
	ui_w_show (np_panel, normal && g_showNotes);
	ui_w_show (sbv, sideW > 0); ui_w_show (sb_seg, sideW > 0);
	if (listW) sl_move (0, top, TH_W, bot - top);
	if (sideW) sb_move (w - SB_W, top, SB_W, bot - top);
	int nh = normal && g_showNotes ? NOTES_H : 0;
	if (normal) sv_move (listW, top, w - listW - sideW, bot - top - nh);
	if (nh) np_move (listW, bot - nh, w - listW - sideW, nh);
	if (!normal) so_move (0, top, w, bot - top);
	// the status bar
	ui_w_move (g_st, 0, bot, w, ST_H);
	int c = ST_H - 4, y = bot + 2, x = w - 8;
	x -= 36; ui_w_move (g_bZp, x, y, 36, c); x -= 58; ui_w_move (g_bZoom, x, y, 58, c); x -= 36; ui_w_move (g_bZm, x, y, 36, c);
	x -= 76; ui_w_move (g_bSorter, x, y, 72, c); x -= 76; ui_w_move (g_bNormal, x, y, 72, c); x -= 66; ui_w_move (g_bNotes, x, y, 62, c);
	ui_w_move (g_lStatus, 10, y, imax (20, x - 16), c);
}
static void relayout ()
{
	layout ((int) ui.w, (int) ui.h);
	s_rowsHash = 0;
	after ();
	prepare ();
	ui_fill (0, 0, ui.w, ui.h, jet (J_FACE));
	ui_view_dirty (sv); ui_view_dirty (sl_v); ui_view_dirty (so); ui_view_dirty (sbv);
	ui_w_draw (0);
	ui_update_all ();
}
static void on_resize (long, long) { relayout (); }
// What must be ready before the views are painted (the main loop's end).
static void prepare ()
{
	if (g_ui) refresh ();
	if (g_svDirty && g_viewMode == VIEW_NORMAL) sv_compose ();
	if (g_listDirty) { sl_prepare (); so_prepare (); }
}

static long tool (long bar, int k, const char *glyph, const char *tip, long fl = 0)
{
	long b = ui_toolbar_add (bar, (long) glyph, 0, fl);
	ui_w_tip (b, (long) tip);
	if (k >= 0) g_btn[k] = b;
	return b;
}
static void build_window ()
{
	s_jet = (const unsigned *) ui_jetons ();
	bool narrow = ui.w < 900;
	TB_H = ctl_h () + 6; ST_H = ctl_h () - 2; TH_W = narrow ? 120 : 164; SB_W = narrow ? 206 : 236; NOTES_H = 84;
	if (ui.h < 520) g_showNotes = false;
	int w = (int) ui.w, h = (int) ui.h;
	ui_fill (0, 0, w, h, jet (J_FACE));
	mask_create ();

	g_tb1 = ui_toolbar (0, 0, w, TB_H);
	tool (g_tb1, TB_NEW, "\xE2\x98\x90", "New presentation (Ctrl+N)");
	tool (g_tb1, TB_OPEN, "\xE2\x87\xA7", "Open... (Ctrl+O)");
	tool (g_tb1, TB_SAVE, "\xE2\x87\xA9", "Save (Ctrl+S)");
	tool (g_tb1, TB_UNDO, "\xE2\x86\xB6", "Undo (Ctrl+Z)", UTB_SEP);
	tool (g_tb1, TB_REDO, "\xE2\x86\xB7", "Redo (Ctrl+Y)");
	tool (g_tb1, TB_CUT, "\xE2\x9C\x82", "Cut (Ctrl+X)", UTB_SEP);
	tool (g_tb1, TB_COPY, "\xE2\x9D\x90", "Copy (Ctrl+C)");
	tool (g_tb1, TB_PASTE, "\xE2\x96\xA4", "Paste (Ctrl+V)");
	tool (g_tb1, TB_NEWSL, "+", "New slide (Ctrl+M)", UTB_SEP);
	tool (g_tb1, TB_LAYOUT, "\xE2\x96\xA5", "The slide's layout");
	tool (g_tb1, TB_DUPSL, "\xE2\x8A\x9E", "Duplicate the slide");
	tool (g_tb1, TB_DELSL, "\xE2\x9C\x95", "Delete the slide");
	tool (g_tb1, TB_TEXT, "T", "Text box (draw it on the slide)", UTB_SEP | UTB_TOGGLE);
	tool (g_tb1, TB_PIC, "\xE2\x96\xA8", "Insert a picture...");
	tool (g_tb1, TB_TABLE, "\xE2\x96\xA6", "Insert a table...");
	tool (g_tb1, TB_CHART, "\xE2\x97\x94", "Insert a chart...");
	tool (g_tb1, TB_SHAPES, "\xE2\x97\x86", "Shapes...", UTB_TOGGLE);
	tool (g_tb1, TB_ARROW, "\xE2\x86\x97", "Arrow (draw it)", UTB_TOGGLE);
	g_bShow = ui_pushbutton (w - 206, 3, 96, ctl_h (), (long) TR ("\xE2\x96\xB6 Show"), UB_DEFAULT);
	ui_w_set_parent (g_bShow, g_tb1);
	ui_w_tip (g_bShow, (long) "Start the show (F5)");

	g_tb2 = ui_toolbar (0, TB_H, w, TB_H);
	int cy = TB_H + (TB_H - ctl_h ()) / 2, fw = narrow ? 104 : 130;
	g_bFont = ui_pushbutton (8, cy, fw, ctl_h (), (long) "", UB_GHOST); ui_w_set_parent (g_bFont, g_tb2); ui_w_tip (g_bFont, (long) "Font");
	g_bSize = ui_pushbutton (8 + fw + 2, cy, 54, ctl_h (), (long) "", UB_GHOST); ui_w_set_parent (g_bSize, g_tb2); ui_w_tip (g_bSize, (long) "Size");
	g_cText = ui_colorpicker (8 + fw + 58, cy + 2, 32, ctl_h () - 4, 0xC00000); ui_w_set_range (g_cText, 0, 0xFFFFFF, 0, 1); ui_w_set_parent (g_cText, g_tb2); ui_w_tip (g_cText, (long) "Text colour");
	int x2 = 8 + fw + 94;
	g_tb2b = ui_toolbar (x2, TB_H, w - x2, TB_H);
	ui_w_set_parent (g_tb2b, g_tb2);
	tool (g_tb2b, TF_BOLD, "B", "Bold (Ctrl+B)", UTB_TOGGLE);
	tool (g_tb2b, TF_ITALIC, "I", "Italic (Ctrl+I)", UTB_TOGGLE);
	tool (g_tb2b, TF_UNDER, "U", "Underline (Ctrl+U)", UTB_TOGGLE);
	tool (g_tb2b, TF_STRIKE, "S", "Strikethrough", UTB_TOGGLE);
	if (!narrow)
	{
		tool (g_tb2b, TF_SUPER, "x\xC2\xB2", "Superscript", UTB_TOGGLE);
		tool (g_tb2b, TF_SUB, "x\xE2\x82\x82", "Subscript", UTB_TOGGLE);
	}
	tool (g_tb2b, TF_LEFT, "\xE2\x87\xA4", "Align left", UTB_SEP | UTB_TOGGLE);
	tool (g_tb2b, TF_CENTER, "\xE2\x86\x94", "Centre", UTB_TOGGLE);
	tool (g_tb2b, TF_RIGHT, "\xE2\x87\xA5", "Align right", UTB_TOGGLE);
	tool (g_tb2b, TF_JUSTIFY, "\xE2\x89\xA1", "Justify", UTB_TOGGLE);
	tool (g_tb2b, TF_BULLETS, "\xE2\x80\xA2", "Bullets", UTB_SEP | UTB_TOGGLE);
	tool (g_tb2b, TF_NUMBERS, "1.", "Numbering", UTB_TOGGLE);
	tool (g_tb2b, TF_OUT, "\xE2\x86\x90", "Decrease the level (Shift+Tab)");
	tool (g_tb2b, TF_IN, "\xE2\x86\x92", "Increase the level (Tab)");
	tool (g_tb2b, TF_SPACING, "\xE2\x86\x95", "Line spacing");
	tool (g_tb2b, TF_FRONT, "\xE2\x87\x88", "Bring to front", UTB_SEP);
	tool (g_tb2b, TF_BACK, "\xE2\x87\x8A", "Send to back");

	// the parts
	int top = 2 * TB_H, vh = h - top - ST_H;
	sl_create (0, top, TH_W, vh);
	so_create (0, top, w, vh);
	sv_create (TH_W, top, w - TH_W - SB_W, vh - NOTES_H);
	np_create (TH_W, h - ST_H - NOTES_H, w - TH_W - SB_W, NOTES_H);
	sb_create (w - SB_W, top, SB_W, vh);

	// the status bar
	g_st = ui_toolbar (0, h - ST_H, w, ST_H);
	g_lStatus = ui_label (10, h - ST_H, 300, ST_H, (long) "", 0); ui_w_set_parent (g_lStatus, g_st);
	struct { long *w; const char *t; const char *tip; } B[] = {
		{ &g_bNotes, "Notes", "Notes under the slide" }, { &g_bNormal, "Normal", "The normal view" }, { &g_bSorter, "Sorter", "The slide sorter" },
		{ &g_bZm, "\xE2\x88\x92", "Zoom out" }, { &g_bZoom, "100%", "Fit the window" }, { &g_bZp, "+", "Zoom in" } };
	for (unsigned i = 0; i < sizeof B / sizeof B[0]; i++) { *B[i].w = ui_pushbutton (0, h - ST_H, 40, ST_H, (long) B[i].t, UB_GHOST); ui_w_set_parent (*B[i].w, g_st); ui_w_tip (*B[i].w, (long) B[i].tip); }

	ui_menu ((long) MENUS);
}

// A control's news.
static void on_drop ()
{
	char b[468]; long kind = 0;
	long n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES) return;
	b[n] = 0;
	for (int i = 0; b[i]; i++) if (b[i] == '\n' || b[i] == '\r') { b[i] = 0; break; }
	if (has_ext (b, ".odp") || has_ext (b, ".pptx")) { if (confirm_discard ()) load_path (b); return; }
	if (has_ext (b, ".png") || has_ext (b, ".jpg") || has_ext (b, ".jpeg") || has_ext (b, ".bmp"))
	{
		unsigned sz = 0; unsigned char *d = read_all (b, &sz);
		if (d) { Object *o = make_picture (base_name (b), d, sz); free (d); if (o) add_object (o); after (); }
	}
}
static void control (long w, long code)
{
	if (code == UN_DROP) { on_drop (); return; }
	if (w == np_ta) { if (code == UN_CHANGE) np_changed (); return; }
	if (w == sl_bar) { if (code == UN_CHANGE) { sl_top = (int) ui_w_get_value (sl_bar); g_listDirty = true; } return; }
	if (w == so_bar) { if (code == UN_CHANGE) { so_top = (int) ui_w_get_value (so_bar); g_listDirty = true; } return; }
	if (w == g_cText) { if (code == UN_CHANGE) { FmtChange ch; ch.what = FC_COLOR; ch.color = palette_code (ui_w_get_value (g_cText)); apply_format (ch); after (); if (g_edit) focus_view (); } return; }
	if (code == UN_ACTIVATE)
	{
		static const struct { int k, id; } T[] = {
			{ TB_NEW, M_NEW }, { TB_OPEN, M_OPEN }, { TB_SAVE, M_SAVE }, { TB_UNDO, M_UNDO }, { TB_REDO, M_REDO }, { TB_CUT, M_CUT }, { TB_COPY, M_COPY }, { TB_PASTE, M_PASTE },
			{ TB_NEWSL, M_NEWSL }, { TB_LAYOUT, M_LAYOUT }, { TB_DUPSL, M_DUPSL }, { TB_DELSL, M_DELSL }, { TB_TEXT, M_TEXT }, { TB_PIC, M_PIC }, { TB_TABLE, M_TABLE }, { TB_CHART, M_CHART },
			{ TB_SHAPES, M_SHAPE }, { TB_ARROW, M_ARROW }, { TF_BOLD, M_BOLD }, { TF_ITALIC, M_ITALIC }, { TF_UNDER, M_UNDER }, { TF_BULLETS, M_BULLET }, { TF_NUMBERS, M_NUMBER },
			{ TF_FRONT, M_FRONT }, { TF_BACK, M_BACK } };
		for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (w == g_btn[T[i].k]) { command (T[i].id); return; }
		if (w == g_btn[TF_STRIKE]) { flag (CF_STRIKE); return; }
		if (g_btn[TF_SUPER] && w == g_btn[TF_SUPER]) { flag (CF_SUPER); return; }
		if (g_btn[TF_SUB] && w == g_btn[TF_SUB]) { flag (CF_SUB); return; }
		if (w == g_btn[TF_LEFT]) { para_cmd (pf_align, AL_LEFT); return; }
		if (w == g_btn[TF_CENTER]) { para_cmd (pf_align, AL_CENTER); return; }
		if (w == g_btn[TF_RIGHT]) { para_cmd (pf_align, AL_RIGHT); return; }
		if (w == g_btn[TF_JUSTIFY]) { para_cmd (pf_align, AL_JUSTIFY); return; }
		if (w == g_btn[TF_OUT]) { para_cmd (pf_level, -1); return; }
		if (w == g_btn[TF_IN]) { para_cmd (pf_level, 1); return; }
		if (w == g_btn[TF_SPACING]) { cmd_spacing (); return; }
		if (w == g_bShow) { show (0, false); return; }
		if (w == g_bFont) { pick_font (); return; }
		if (w == g_bSize) { pick_size (); return; }
		if (w == g_bNotes) { focus_view (); cmd_notes (); return; }
		if (w == g_bNormal) { set_view (VIEW_NORMAL); return; }
		if (w == g_bSorter) { cmd_sorter (); return; }
		if (w == g_bZm) { cmd_zoom_out (); focus_view (); return; }
		if (w == g_bZoom) { cmd_zoom_fit (); focus_view (); return; }
		if (w == g_bZp) { cmd_zoom_in (); focus_view (); return; }
	}
	sb_event (w, code);
}
// A key no control took.
static void key (int hid, int mods)
{
	if (hid == 0x3E) { if (mods & UI_MOD_SHIFT) show (g_cur, false); else show (0, false); }
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/slides.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	if (!documentkit_kit_open () || !archivekit_kit_open ()) return 1;
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int ww = imin (W, (int) scr[0] - 8), wh = imin (H, (int) (scr[1] - scr[3] - scr[2]) - 32 - DOCK_H);
	if (ui_window_ex (0, scr[3], ww, wh, (long) "Slides", WF_RESIZABLE) <= 0) return 1;
	ui_min_size (640, 400);
	ui_on_resize ((long) on_resize, 0);
	mkdir ("SD0:/home", 0755);
	mkdir (HOME_DIR, 0755);
	mkdir (DOCS, 0755);
	deck_new (g_deck);
	build_window ();
	g_onChange = on_change; g_onSlides = on_slides;
	g_onDone = master_sync; g_accepts = master_accepts; g_onRefuse = [] (const char *m) { ask ("Master view", m, UM_OK); };
	g_onOpenSlide = open_slide; g_onTool = on_zoom;
	g_onWheelSlide = on_wheel_slide; g_onZoom = on_zoom;
	relayout ();

	// a file named on the command line, else the one kept at the last close
	if (argc > 1 && argv[1][0]) load_path (argv[1]);
	else
	{
		unsigned n = 0; unsigned char *b = read_all (RECOVER, &n);
		bool recovered = false;
		if (b)
		{
			if (n > 0 && ask ("Slides", "Slides was closed with unsaved changes. Open the recovered presentation?", UM_YESNO) == 1 && deck_load (g_deck, b, n))
			{
				unsigned nn = 0; unsigned char *nb = read_all (RECOVER_NAME, &nn);
				g_path[0] = 0;
				if (nb) { int k = imin ((int) nn, 199); memcpy (g_path, nb, k); g_path[k] = 0; free (nb); }
				deck_loaded ();
				g_saved = g_deck.changes + 1;
				recovered = true;
			}
			free (b);
			unlink (RECOVER); unlink (RECOVER_NAME);
		}
		if (!recovered) deck_loaded ();
	}
	focus_view ();
	relayout ();

	unsigned tick = 0;
	for (;;)
	{
		long t = ui_event (g_edit ? 25 : 50);
		if (t == WEV_CLOSE)
		{
			break;
		}
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			if (code) control (w, code);
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
				else key (hid, mods);
			}
		}
		else ui_w_idle ();
		if (g_wantContext) { g_wantContext = false; prepare (); ui_view_flush (); view_menu (g_ctxX, g_ctxY); }
		if (g_wantSlideMenu) { g_wantSlideMenu = false; prepare (); ui_view_flush (); slide_menu (g_smX, g_smY); }
		if (g_wantPic) { g_wantPic = false; picture_into (g_wantPicture); }
		sv_tick ();
		unsigned now = ticks ();
		if (now - tick >= 25) { tick = now; if (g_msg[0] && now - g_msgT >= 400) { g_msg[0] = 0; g_ui = true; } }
		prepare ();
		ui_view_flush ();
	}

	if (g_master) master_close ();
	if (changed_doc ())
	{
		unsigned n = 0; unsigned char *b = odp_save (g_deck, &n);
		if (b) { save_file (RECOVER, b, n); delete[] b; }
		if (g_path[0]) save_file (RECOVER_NAME, g_path, (long) strlen (g_path));
	}
	return 0;
}
