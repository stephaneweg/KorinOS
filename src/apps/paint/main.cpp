//
// paint -- Onyx's Paint on ARMPI, without a bridge: a picture of transparent layers. The ribbon (Clipboard -- Image: select,
// crop, resize, rotate -- Tools: pencil, fill, text, eraser, colour picker, magnifier, gradient, adjust -- Brushes -- Shapes --
// Colours), the options bar of the current tool, the canvas (12 % - 3200 %), the layers' panel, the status bar: each a ui_view;
// the dialogs are UIKit's.
//
// Pieces: pdoc.h (the layers, their blend modes, undo by tiles), psel.h (the selection: rectangle, lasso, magic wand), raster.h
// (the shapes), pbrush.h (the brushes), pgrad.h (the gradients, GIMP's .ggr), ptext.h (the Text tool), padjust.h (the
// adjustments), pview.h (the canvas, its tools), plook.h / picons.h / pui.h (the drawing, the icons, the ribbon, the options, the
// layers, the status bar), pfile.h (OpenRaster, the pictures, the exports), image.cpp (the decoders).
//
// Files: File > Save writes OpenRaster (.ora: the layers, their blend modes -- GIMP and Krita open it); Open reads it, or a PNG,
// JPEG, BMP, GIF, WebP, PCX picture; File > Export writes what is shown, flattened, as PNG, JPEG, BMP or GIF. A picture named on
// the command line, or dropped on the window, is opened. Closed with unsaved changes, the picture is kept in
// SD0:/home/paint/recovered.ora and offered back at the next start. Your gradients: SD0:/home/paint/gradients/*.ggr.
//
// MIT licence (Onyx).
//
#include <time.h>
#include "pui.h"
#include "padjust.h"
#include "pfile.h"
#include <printerkit.h>

using namespace pd;

#define HOMEDIR "SD0:/home/paint"
#define PICTURES "SD0:/home/pictures"
static const char *RECOVER = HOMEDIR "/recovered.ora";
static const char *RECOVER_NAME = HOMEDIR "/recovered.txt";
static const char *IMAGE_MASK = "*.png;*.jpg;*.jpeg;*.jpe;*.gif;*.bmp;*.webp;*.pcx";

static CanvasView g_view;
static Ribbon g_ribbon;
static OptionsBar g_opts;
static LayersPanel g_layers;
static StatusBar g_status (&g_view);
static long g_win;				// the window
static char g_path[200];			// "" : not saved yet (a picture opened: its path, Save asks for an .ora)
static unsigned g_saved;
static unsigned *g_clip; static int g_clipW, g_clipH;	// the copied pixels (their alpha: the system's clipboard has none)
static bool g_quit;

// A press on a control ends in a popup or a dialog: done from the loop, after the event (UIKit is in it while the view's
// mouse is called).
struct Pending { void (*fn) (int, int, int, int, int); int a, b, c, d, e; };
static Pending g_pend;
static void later (void (*fn) (int, int, int, int, int), int a, int b = 0, int c = 0, int d = 0, int e = 0) { g_pend.fn = fn; g_pend.a = a; g_pend.b = b; g_pend.c = c; g_pend.d = d; g_pend.e = e; }

static bool changed_doc () { return D.changes != g_saved; }
static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static void focus_view () { g_view.setFocus (); }
static void refresh ()
{
	g_view.invalidate ();
	g_ribbon.invalidate ();
	g_opts.invalidate ();
	g_layers.sync ();
	g_status.invalidate ();
}
static void layout (int W, int H)
{
	bool wide = W >= RIBBON_WIDE;
	int rh = wide ? RIBBON_H : RIBBON_CH, y = rh + OPT_H, bh = H - y - STATUS_H, pw = g_showLayers ? (wide ? (int) PANEL_W : 236) : 0;
	g_ribbon.place (0, 0, W, rh);
	g_opts.place (0, rh, W, OPT_H);
	ui_w_show (g_layers.view, g_showLayers);
	g_layers.place (W - (pw ? pw : PANEL_W), y, pw ? pw : (int) PANEL_W, bh);
	g_status.place (0, H - STATUS_H, W, STATUS_H);
	g_view.width = W - pw; g_view.height = bh;
	ui_w_move (g_view.view, 0, y, W - pw, bh);
	g_view.clampScroll ();
	g_view.invalidate ();
}
static void on_resize (long w, long h) { layout ((int) w, (int) h); }
// (a dialog is open: what it changes in the picture shown at once)
static void show_main ()
{
	long was = ui_window_use (g_win);
	g_view.invalidate ();
	ui_view_flush ();
	ui_window_use (was);
}

// ---- dialogs ---------------------------------------------------------------------------------------------------
enum { CTL = 30 };
static long d_label (int x, int y, int w, const char *t, long fl = 0) { return ui_label (x, y, w, CTL, (long) TR (t), fl); }
static long d_field (int x, int y, int w, const char *t) { long f = ui_textbox (x, y, w, CTL, 0); ui_w_set_text (f, (long) t); return f; }
static long d_num (int x, int y, int w, int v) { char b[16]; fmt_int (b, v, 0); return d_field (x, y, w, b); }
static long d_radio (int x, int y, int w, const char *t, int group, bool on) { long r = ui_radio (x, y, w, CTL, (long) TR (t), group); if (on) ui_w_set_value (r, 1); return r; }
// a list "a|b|c" from names (translated)
static const char *d_items (const char *const *names, int n)
{
	static char b[1024]; int k = 0;
	for (int i = 0; i < n; i++) k += snprintf (b + k, sizeof b - (size_t) k, "%s%s", i ? "|" : "", TR (names[i]));
	return b;
}
static long d_drop (int x, int y, int w, const char *items, int sel) { long d = ui_dropdown (x, y, w, CTL, (long) items); ui_w_set_value (d, sel); return d; }
static void d_ok_cancel (long d, int w) { long x = ui_dialog_button (d, 0, (long) "OK", 1, UB_DEFAULT, w - 12); ui_dialog_button (d, 1, (long) TR ("Cancel"), 0, 0, x); ui_dialog_cancel (d, 0); }
static inline const char *wtext (long w) { return (const char *) ui_w_get_text (w); }
static long d_tick (long d) { (void) d; ui_view_flush (); return 0; }

// ---- files ------------------------------------------------------------------------------------------------
static void doc_loaded ()
{
	if (D.n == 1 && !strcmp (D.lay[0].name, "Background")) scpy (D.lay[0].name, TR ("Background"), sizeof D.lay[0].name);
	sel_clear ();
	g_saved = D.changes;
	g_view.sx = g_view.sy = 0;
	g_view.zoomFit ();
	refresh ();
}
static bool open_path (const char *path)
{
	if (!doc_open (path)) { ui_message ((long) TR ("Open"), (long) TR ("That file is not a picture Paint can read (PNG, JPEG, BMP, GIF, WebP, PCX, OpenRaster)."), UM_OK); return false; }
	scpy (g_path, path, sizeof g_path);
	doc_loaded ();
	return true;
}
static void with_ext (char *out, int cap, const char *name, const char *ext)
{
	scpy (out, name, cap);
	int n = slen (out), dot = n; while (dot > 0 && out[dot - 1] != '.') dot--;
	if (dot > 0) out[dot - 1] = 0;
	n = slen (out); scpy (out + n, ext, cap - n);
}
static void cmd_save_as ();
static void cmd_save ()
{
	settle ();
	if (!g_path[0] || !ends_with (g_path, ".ora")) { cmd_save_as (); return; }
	unsigned n; unsigned char *b = ora_save (&n);
	bool ok = save_file (g_path, b, n);
	delete[] b;
	if (ok) g_saved = D.changes; else ui_message ((long) TR ("Save"), (long) TR ("The file could not be written."), UM_OK);
	refresh (); focus_view ();
}
static void cmd_save_as ()
{
	settle ();
	char def[80], path[200];
	with_ext (def, sizeof def, g_path[0] ? base_name (g_path) : TR ("Untitled"), ".ora");
	ui_file_filter ((long) "*.ora");
	if (ui_file_save ((long) PICTURES, (long) def, (long) path, sizeof path))
	{
		if (!ends_with (path, ".ora")) { int k = slen (path); scpy (path + k, ".ora", (int) sizeof path - k); }
		scpy (g_path, path, sizeof g_path);
		cmd_save ();
		return;
	}
	focus_view ();
}
// Unsaved changes: save them, leave them, or stay -> false: stay
static bool guard ()
{
	settle ();
	if (!changed_doc ()) return true;
	char t[260]; snprintf (t, sizeof t, TR ("Save the changes to %s?"), g_path[0] ? base_name (g_path) : TR ("Untitled"));
	long r = ui_message ((long) TR ("Paint"), (long) t, UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1) { cmd_save (); return !changed_doc (); }
	return true;
}
// File > Print: the Print dialog (PrinterKit's: the printer, the paper...), then the picture -- as the screen shows it, its
// layers flattened -- on one page: at its size (96 pixels an inch), made smaller if it does not fit in what the printer
// prints, centred.
static void cmd_print ()
{
	settle ();
	if (!printerkit_kit_open ()) { ui_message ((long) TR ("Print"), (long) TR ("The print service is not installed."), UM_OK); focus_view (); return; }
	const char *name = g_path[0] ? base_name (g_path) : TR ("Untitled");
	PrintSetup ps; print_setup_default ((long) &ps);
	if (D.w > D.h) print_setup_paper ((long) &ps, 0, PRINT_LANDSCAPE);
	PrintDialogInfo di = { sizeof di, name, 1, 0, 0, 0, 0 };
	if (print_dialog ((long) &ps, (long) &di))
	{
		long j = print_begin ((long) &ps, (long) name);
		if (j && print_page (j, 0, 0))
		{
			float l = ps.margin_l > 18000 ? ps.margin_l / 1000.0f : 18, t = ps.margin_t > 18000 ? ps.margin_t / 1000.0f : 18;
			float r = ps.margin_r > 18000 ? ps.margin_r / 1000.0f : 18, b = ps.margin_b > 18000 ? ps.margin_b / 1000.0f : 18;
			float aw = ps.paper_w / 1000.0f - l - r, ah = ps.paper_h / 1000.0f - t - b, w = D.w * 0.75f, h = D.h * 0.75f;
			float k = aw / w < ah / h ? aw / w : ah / h;
			if (k < 1) { w *= k; h *= k; }
			unsigned *flat = flatten ();
			print_image_mode (j, PRINT_IMG_ALPHA);
			print_image (j, (long) flat, D.w, D.h, print_mpt (l + (aw - w) / 2), print_mpt (t + (ah - h) / 2), print_mpt (w), print_mpt (h));
			delete[] flat;
		}
		if (!j || print_end (j) < 0) ui_message ((long) TR ("Print"), (long) TR ("The picture could not be put in the print queue."), UM_OK);
	}
	focus_view ();
}
static void export_as (const char *ext)
{
	settle ();
	char def[80], path[200];
	with_ext (def, sizeof def, g_path[0] ? base_name (g_path) : TR ("Untitled"), ext);
	ui_file_filter ((long) (ext[1] == 'j' ? "*.jpg;*.jpeg" : ext[1] == 'b' ? "*.bmp" : ext[1] == 'g' ? "*.gif" : "*.png"));
	if (ui_file_save ((long) PICTURES, (long) def, (long) path, sizeof path))
	{
		if (!ends_with (path, ".png") && !ends_with (path, ".jpg") && !ends_with (path, ".jpeg") && !ends_with (path, ".bmp") && !ends_with (path, ".gif"))
		{ int k = slen (path); scpy (path + k, ext, (int) sizeof path - k); }
		unsigned n; unsigned char *b = export_bytes (path, &n);
		if (!save_file (path, b, n)) ui_message ((long) TR ("Export"), (long) TR ("The file could not be written."), UM_OK);
		delete[] b;
	}
	focus_view ();
}

// ---- the New dialog -------------------------------------------------------------------------------------------
static void cmd_new ()
{
	if (!guard ()) { focus_view (); return; }
	enum { W = 340, H = 196 };
	long d = ui_dialog_begin (W, H, (long) TR ("New Picture"));
	int y = 14;
	d_label (20, y, 90, "Width"); long tw2 = d_num (120, y, 100, 800); d_label (230, y, 40, "px", ULB_DIM);
	d_label (20, y + 38, 90, "Height"); long th = d_num (120, y + 38, 100, 560); d_label (230, y + 38, 40, "px", ULB_DIM);
	long white = d_radio (20, y + 82, 130, "White", 1, true);
	d_radio (160, y + 82, 160, "Transparent", 1, false);
	d_ok_cancel (d, W);
	if (ui_dialog_run (d, 0, 0) == 1)
	{
		int w = pclamp (parse_int (wtext (tw2)), 1, 8192), h = pclamp (parse_int (wtext (th)), 1, 8192);
		bool wh = ui_w_get_value (white) != 0;
		ui_dialog_end (d);
		doc_new (w, h, wh);
		undo_clear ();
		g_path[0] = 0;
		doc_loaded ();
	}
	else ui_dialog_end (d);
	focus_view ();
}
static void cmd_open ()
{
	if (!guard ()) { focus_view (); return; }
	char path[200], mask[80];
	snprintf (mask, sizeof mask, "%s;*.ora", IMAGE_MASK);
	ui_file_filter ((long) mask);
	if (ui_file_open ((long) PICTURES, (long) path, sizeof path)) open_path (path);
	focus_view ();
}

// ---- edits ----------------------------------------------------------------------------------------------------
static void cmd_undo () { settle (); if (undo ()) sel_clear (); refresh (); focus_view (); }
static void cmd_redo () { settle (); if (redo ()) sel_clear (); refresh (); focus_view (); }
static void set_tool (int t)
{
	if (t != T_SELECT) float_commit (false);
	if (t != g_tool) { grad_end (true); text_end (true); }
	if (t == T_PICKER && g_tool != T_PICKER) g_prevTool = g_tool;
	g_tool = t;
	refresh (); focus_view ();
}
static void set_select (int kind) { g_selKind = kind; set_tool (T_SELECT); }
// The selection's pixels (the floating ones, or the layer's inside the selection) -> g_clip, and the system's clipboard
// (which keeps no alpha: laid on white there).
static void copy_sel ()
{
	if (D.fl.px)
	{
		delete[] g_clip; g_clipW = D.fl.w; g_clipH = D.fl.h; g_clip = new unsigned[(unsigned) g_clipW * g_clipH];
		for (int i = 0; i < g_clipW * g_clipH; i++) g_clip[i] = D.fl.px[i];
	}
	else
	{
		Rect r = g_sel; r.clip (D.w, D.h);
		if (r.empty ()) return;
		delete[] g_clip; g_clipW = r.x1 - r.x0; g_clipH = r.y1 - r.y0; g_clip = new unsigned[(unsigned) g_clipW * g_clipH];
		for (int y = 0; y < g_clipH; y++)
			for (int x = 0; x < g_clipW; x++)
			{
				unsigned c = D.lay[D.cur].px[(unsigned) (r.y0 + y) * D.w + r.x0 + x]; int m = sel_at (r.x0 + x, r.y0 + y);
				g_clip[y * g_clipW + x] = m == 255 ? c : (c & 0xFFFFFF) | ((c >> 24) * (unsigned) m / 255) << 24;
			}
	}
	unsigned n = (unsigned) g_clipW * g_clipH;
	unsigned *rgb = new unsigned[n];
	for (unsigned i = 0; i < n; i++) rgb[i] = over (0xFFFFFFFFu, g_clip[i], 255) & 0xFFFFFF;
	ui_clip_set_image ((long) rgb, g_clipW, g_clipH);
	delete[] rgb;
}
// What Paste takes (new []): a picture file copied in Files, the system's picture (ours with its alpha, when it is still
// ours), or the pixels copied here
static unsigned *clip_pixels (int *w, int *h)
{
	char path[300]; long cut = 0;
	long n = uikit_call3 ((long (*) ()) ui_clip_get_files, (long) path, sizeof path - 1, 0, 0, &cut, 0);
	if (n > 0)
	{
		path[n] = 0;
		for (int i = 0; path[i]; i++) if (path[i] == '\n' || path[i] == '\r') { path[i] = 0; break; }
		if (img_is_image_name (path)) { unsigned *p = img_load (path, w, h); if (p) return p; }
	}
	long ch = 0, bytes = 0;
	long cw = uikit_call3 ((long (*) ()) ui_clip_get_image, 0, 0, 0, 0, &ch, &bytes);
	if (cw > 0 && ch > 0 && bytes >= cw * ch * 4)
	{
		unsigned *t = new unsigned[(unsigned long) bytes / 4];
		long c2 = 0, b2 = 0, w2 = uikit_call3 ((long (*) ()) ui_clip_get_image, (long) t, bytes, 0, 0, &c2, &b2);
		if (w2 == cw && c2 == ch)
		{
			bool mine = g_clip && cw == g_clipW && ch == g_clipH;
			for (long i = 0; mine && i < cw * ch; i++) if ((t[i] & 0xFFFFFF) != (over (0xFFFFFFFFu, g_clip[i], 255) & 0xFFFFFF)) mine = false;
			if (!mine)
			{
				for (long i = 0; i < cw * ch; i++) t[i] |= 0xFF000000u;
				*w = (int) cw; *h = (int) ch;
				return t;
			}
		}
		delete[] t;
	}
	if (!g_clip) return 0;
	*w = g_clipW; *h = g_clipH;
	unsigned *px = new unsigned[(unsigned) *w * *h];
	for (int i = 0; i < *w * *h; i++) px[i] = g_clip[i];
	return px;
}
static void cmd_copy () { copy_sel (); focus_view (); }
static void cmd_cut () { copy_sel (); g_view.deleteSelection (); refresh (); focus_view (); }
static void cmd_paste ()
{
	int w = 0, h = 0;
	unsigned *px = clip_pixels (&w, &h);
	if (!px) { focus_view (); return; }
	settle ();
	int x = pclamp (g_view.imX (0), 0, pmax (0, D.w - 1)), y = pclamp (g_view.imY (0), 0, pmax (0, D.h - 1));
	float_new (px, w, h, x, y);
	g_tool = T_SELECT;
	refresh (); focus_view ();
}
// Pixels (their own now) as a new layer above the current one, floating (moved, then put down by a
// click outside), centred on what the view shows.
static void layer_from_pixels (const char *name, unsigned *px, int w, int h)
{
	settle ();
	if (D.n >= MAXLAYERS) { delete[] px; ui_message ((long) TR ("Layers"), (long) TR ("The picture has as many layers as it can take."), UM_OK); return; }
	rec_whole ();
	for (int k = D.n; k > D.cur + 1; k--) D.lay[k] = D.lay[k - 1];
	layer_init (D.lay[D.cur + 1], name, new_px (D.w, D.h, 0));
	D.n++; D.cur++;
	compose_all ();
	int cx = g_view.imX (g_view.viewW () / 2), cy = g_view.imY (g_view.viewH () / 2);
	cx = pclamp (cx, 0, D.w); cy = pclamp (cy, 0, D.h);
	float_new (px, w, h, w >= D.w ? (D.w - w) / 2 : cx - w / 2, h >= D.h ? (D.h - h) / 2 : cy - h / 2);
	g_tool = T_SELECT;
}
static void cmd_paste_layer ()
{
	int w = 0, h = 0;
	unsigned *px = clip_pixels (&w, &h);
	if (px) layer_from_pixels (TR ("Pasted"), px, w, h);
	refresh (); focus_view ();
}
// A picture file opened as a new layer (its name the file's).
static void cmd_open_layer ()
{
	char path[200];
	ui_file_filter ((long) IMAGE_MASK);
	if (ui_file_open ((long) PICTURES, (long) path, sizeof path))
	{
		int w = 0, h = 0;
		unsigned *px = img_load (path, &w, &h);
		if (!px) ui_message ((long) TR ("Open as Layer"), (long) TR ("That file is not a picture Paint can read (PNG, JPEG, BMP, GIF, WebP, PCX, OpenRaster)."), UM_OK);
		else
		{
			char nm[32]; scpy (nm, base_name (path), sizeof nm);
			int n = slen (nm), dot = n; while (dot > 0 && nm[dot - 1] != '.') dot--;
			if (dot > 1) nm[dot - 1] = 0;
			layer_from_pixels (nm, px, w, h);
		}
	}
	refresh (); focus_view ();
}
static void cmd_delete () { g_view.deleteSelection (); refresh (); focus_view (); }
static void cmd_select_all () { settle (); g_tool = T_SELECT; sel_rect (mkrect (0, 0, D.w, D.h)); refresh (); focus_view (); }
static void cmd_deselect () { settle (); sel_clear (); refresh (); focus_view (); }
static void cmd_invert_sel () { settle (); sel_invert (); refresh (); focus_view (); }

// ---- the picture's changes ------------------------------------------------------------------------------------
static void new_size (int w, int h)
{
	D.w = w; D.h = h;
	delete[] D.ov.px; D.ov.px = new_px (w, h, 0); D.ov.r = norect ();
	sel_clear ();
	compose_all ();
}
static void cmd_crop ()
{
	float_commit (true);
	Rect r = g_sel; r.clip (D.w, D.h);
	if (r.empty ()) { ui_message ((long) TR ("Crop"), (long) TR ("Select the part to keep first (the Select tool)."), UM_OK); focus_view (); return; }
	settle ();
	rec_whole ();
	int w = r.x1 - r.x0, h = r.y1 - r.y0;
	for (int k = 0; k < D.n; k++)
	{
		unsigned *n = new unsigned[(unsigned) w * h];
		for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) n[y * w + x] = D.lay[k].px[(unsigned) (r.y0 + y) * D.w + r.x0 + x];
		delete[] D.lay[k].px; D.lay[k].px = n;
	}
	new_size (w, h);
	g_view.zoomFit (); refresh (); focus_view ();
}
// Rotate (1 right, -1 left, 2 half a turn) or flip (3 horizontal, 4 vertical): the selection, or the picture.
static void transform (int op)
{
	grad_end (true); text_end (true);
	if (has_sel () || D.fl.px)
	{
		if (!float_lift ()) return;
		Rect old = float_rect ();
		if (op == 3) flip_h (D.fl.px, D.fl.w, D.fl.h);
		else if (op == 4) flip_v (D.fl.px, D.fl.w, D.fl.h);
		else if (op == 2) { flip_h (D.fl.px, D.fl.w, D.fl.h); flip_v (D.fl.px, D.fl.w, D.fl.h); }
		else
		{
			unsigned *n = rotate (D.fl.px, D.fl.w, D.fl.h, op == 1);
			delete[] D.fl.px; D.fl.px = n;
			int cx = D.fl.x + D.fl.w / 2, cy = D.fl.y + D.fl.h / 2, t = D.fl.w; D.fl.w = D.fl.h; D.fl.h = t;
			D.fl.x = cx - D.fl.w / 2; D.fl.y = cy - D.fl.h / 2;
		}
		old.add (float_rect ()); compose (old);
		g_sel = float_rect ();
		g_tool = T_SELECT;
	}
	else
	{
		rec_whole ();
		int w = D.w, h = D.h;
		for (int k = 0; k < D.n; k++)
		{
			unsigned *p = D.lay[k].px;
			if (op == 3) flip_h (p, w, h);
			else if (op == 4) flip_v (p, w, h);
			else if (op == 2) { flip_h (p, w, h); flip_v (p, w, h); }
			else { D.lay[k].px = rotate (p, w, h, op == 1); delete[] p; }
		}
		if (op == 1 || op == -1) new_size (h, w);
		compose_all ();
		if (op == 1 || op == -1) g_view.zoomFit ();
	}
	refresh (); focus_view ();
}

// The Resize dialog: the picture scaled (by pixels or a percentage), or its canvas made bigger / smaller.
static struct { long mode, byPct, byPx, tw, th, keep, resample, anchor, ltw, lth, size; bool busy; int w0, h0; } RD;
static bool rd_pct () { return ui_w_get_value (RD.byPct) != 0; }
static int rd_w () { int v = parse_int (wtext (RD.tw)); return pclamp (rd_pct () ? (int) ((long long) RD.w0 * v / 100) : v, 1, 8192); }
static int rd_h () { int v = parse_int (wtext (RD.th)); return pclamp (rd_pct () ? (int) ((long long) RD.h0 * v / 100) : v, 1, 8192); }
static void rd_show ()
{
	char b[64]; snprintf (b, sizeof b, "%d x %d px", rd_w (), rd_h ());
	ui_w_set_text (RD.size, (long) b);
	ui_w_set_text (RD.ltw, (long) (rd_pct () ? "%" : "px")); ui_w_set_text (RD.lth, (long) (rd_pct () ? "%" : "px"));
	ui_w_draw (0);
}
static long rd_fn (long d, long w, long code)
{
	(void) d;
	if (code != UN_CHANGE) return 0;
	char b[16];
	if (w == RD.mode)
	{
		bool canvasMode = ui_w_get_value (RD.mode) == 1;
		ui_w_show (RD.resample, !canvasMode); ui_w_show (RD.anchor, canvasMode);
		if (canvasMode && rd_pct ()) { ui_w_set_value (RD.byPx, 1); ui_w_set_value (RD.byPct, 0); w = RD.byPx; }
	}
	if (w == RD.byPct || w == RD.byPx)
	{
		if (rd_pct ()) { ui_w_set_text (RD.tw, (long) "100"); ui_w_set_text (RD.th, (long) "100"); }
		else { fmt_int (b, RD.w0, 0); ui_w_set_text (RD.tw, (long) b); fmt_int (b, RD.h0, 0); ui_w_set_text (RD.th, (long) b); }
	}
	else if ((w == RD.tw || w == RD.th) && ui_w_get_value (RD.keep))
	{
		long o = w == RD.tw ? RD.th : RD.tw;
		int v = parse_int (wtext (w)), from = w == RD.tw ? RD.w0 : RD.h0, to = w == RD.tw ? RD.h0 : RD.w0;
		fmt_int (b, rd_pct () ? v : pmax (1, (int) ((long long) v * to / pmax (1, from))), 0);
		ui_w_set_text (o, (long) b);
	}
	rd_show ();
	return 0;
}
static void cmd_resize ()
{
	settle ();
	enum { W = 480, H = 330 };
	static const char *const RES[2] = { "Sharp (the nearest pixel)", "Smooth (averaged)" };
	static const char *const ANCH[3] = { "Top left", "Centre", "Bottom right" };
	long d = ui_dialog_begin (W, H, (long) TR ("Resize"));
	RD.w0 = D.w; RD.h0 = D.h;
	int y = 12;
	char seg[120]; snprintf (seg, sizeof seg, "%s|%s", TR ("Resize the picture"), TR ("Canvas size"));
	RD.mode = ui_segmented (20, y, 380, CTL, (long) seg);
	y += 40;
	RD.byPct = d_radio (20, y, 170, "By percentage", 1, false);
	RD.byPx = d_radio (200, y, 170, "By pixels", 1, true);
	y += 38;
	d_label (20, y, 86, "Width"); RD.tw = d_num (112, y, 100, D.w); RD.ltw = d_label (220, y, 30, "px", ULB_DIM);
	d_label (20, y + 36, 86, "Height"); RD.th = d_num (112, y + 36, 100, D.h); RD.lth = d_label (220, y + 36, 30, "px", ULB_DIM);
	RD.size = d_label (256, y + 18, 150, "", ULB_DIM);
	RD.keep = ui_checkbox (20, y + 74, 280, CTL, (long) TR ("Keep the proportions"), 1);
	d_label (20, y + 112, 134, "Resampling");
	RD.resample = d_drop (160, y + 112, 300, d_items (RES, 2), 1);
	RD.anchor = d_drop (160, y + 112, 300, d_items (ANCH, 3), 1);
	ui_w_show (RD.anchor, 0);
	d_ok_cancel (d, W);
	rd_show ();
	long r = ui_dialog_run (d, (long) rd_fn, 0);
	int w = rd_w (), h = rd_h (), res = (int) ui_w_get_value (RD.resample), anc = (int) ui_w_get_value (RD.anchor);
	bool canvasMode = ui_w_get_value (RD.mode) == 1;
	ui_dialog_end (d);
	if (r != 1 || (w == D.w && h == D.h)) { focus_view (); return; }
	rec_whole ();
	for (int k = 0; k < D.n; k++)
	{
		unsigned *p = D.lay[k].px, *n;
		if (!canvasMode) n = scale (p, D.w, D.h, w, h, res == 1);
		else
		{
			n = new_px (w, h, k == 0 && (p[0] >> 24) == 255 ? 0xFFFFFFFFu : 0);	// (an opaque background: grown white)
			int ox = anc == 1 ? (w - D.w) / 2 : anc == 2 ? w - D.w : 0, oy = anc == 1 ? (h - D.h) / 2 : anc == 2 ? h - D.h : 0;
			for (int y2 = 0; y2 < D.h; y2++) for (int x = 0; x < D.w; x++)
			{
				int X = x + ox, Y = y2 + oy;
				if (X >= 0 && Y >= 0 && X < w && Y < h) n[(unsigned) Y * w + X] = p[(unsigned) y2 * D.w + x];
			}
		}
		delete[] p; D.lay[k].px = n;
	}
	new_size (w, h);
	g_view.zoomFit (); refresh (); focus_view ();
}

// ---- layers ---------------------------------------------------------------------------------------------------
static void layer_insert (int at, const char *name, unsigned *px)
{
	for (int k = D.n; k > at; k--) D.lay[k] = D.lay[k - 1];
	layer_init (D.lay[at], name, px);
	D.n++;
}
static void next_name (char *out, int cap)
{
	for (int i = 2; i < 100; i++)
	{
		char b[32]; snprintf (b, sizeof b, "%s %d", TR ("Layer"), i);
		bool used = false; for (int k = 0; k < D.n; k++) if (!strcmp (b, D.lay[k].name)) used = true;
		if (!used) { scpy (out, b, cap); return; }
	}
	scpy (out, TR ("Layer"), cap);
}
static void cmd_layer_new ()
{
	settle ();
	if (D.n >= MAXLAYERS) return;
	rec_whole ();
	char nm[32]; next_name (nm, sizeof nm);
	layer_insert (D.cur + 1, nm, new_px (D.w, D.h, 0));
	D.cur++;
	compose_all (); refresh (); focus_view ();
}
static void cmd_layer_dup ()
{
	settle ();
	if (D.n >= MAXLAYERS) return;
	rec_whole ();
	const Layer s = D.lay[D.cur];
	unsigned *px = new unsigned[(unsigned) D.w * D.h];
	for (int i = 0; i < D.w * D.h; i++) px[i] = s.px[i];
	char nm[32]; snprintf (nm, sizeof nm, TR ("%s copy"), s.name);
	layer_insert (D.cur + 1, nm, px);
	D.cur++; D.lay[D.cur].visible = s.visible; D.lay[D.cur].opacity = s.opacity; D.lay[D.cur].blend = s.blend; D.lay[D.cur].clip = s.clip;
	compose_all (); refresh (); focus_view ();
}
static void cmd_layer_del ()
{
	settle ();
	if (D.n < 2) return;
	rec_whole ();
	delete[] D.lay[D.cur].px;
	for (int k = D.cur; k < D.n - 1; k++) D.lay[k] = D.lay[k + 1];
	D.n--;
	if (D.cur >= D.n) D.cur = D.n - 1;
	compose_all (); refresh (); focus_view ();
}
static void layer_move (int dir)
{
	settle ();
	int to = D.cur + dir;
	if (to < 0 || to >= D.n) return;
	rec_whole ();
	Layer t = D.lay[D.cur]; D.lay[D.cur] = D.lay[to]; D.lay[to] = t;
	D.cur = to;
	compose_all (); refresh (); focus_view ();
}
static void cmd_layer_up () { layer_move (1); }
static void cmd_layer_down () { layer_move (-1); }
// The layer merged into the one below, with its blend mode and opacity (a clip mask: the layer below
// keeps only what the mask lets through).
static void cmd_layer_merge ()
{
	settle ();
	if (D.cur == 0) return;
	rec_whole ();
	Layer &up = D.lay[D.cur], &lo = D.lay[D.cur - 1];
	if (up.visible)
		for (int y = 0; y < D.h; y++)
			for (int x = 0; x < D.w; x++)
			{
				unsigned i = (unsigned) y * D.w + x;
				if (is_clip_mask (D.cur)) { lo.px[i] = clipped_px (D.cur - 1, lo.px[i], x, y, false); continue; }
				unsigned s = premul (up.px[i]);
				if (up.blend == B_MASK) { lo.px[i] = unpremul (blend_px (s, premul (lo.px[i]), B_MASK)); continue; }
				if (up.opacity < 255) { unsigned o = (unsigned) up.opacity; s = ((((s >> 24) * o + 127) / 255) << 24) | (((((s >> 16) & 255) * o + 127) / 255) << 16) | (((((s >> 8) & 255) * o + 127) / 255) << 8) | (((s & 255) * o + 127) / 255); }
				if (!s) continue;
				lo.px[i] = unpremul (blend_px (s, premul (lo.px[i]), (unsigned) up.blend));
			}
	delete[] up.px;
	for (int k = D.cur; k < D.n - 1; k++) D.lay[k] = D.lay[k + 1];
	D.n--; D.cur--;
	compose_all (); refresh (); focus_view ();
}
static void cmd_flatten ()
{
	settle ();
	if (D.n < 2) return;
	rec_whole ();
	unsigned *f = flatten ();
	for (int k = 0; k < D.n; k++) delete[] D.lay[k].px;
	D.n = 1; D.cur = 0;
	layer_init (D.lay[0], TR ("Background"), f);
	compose_all (); refresh (); focus_view ();
}
// A layer's properties changed (no pixel): shown again.
static void props_changed () { D.changes++; compose_all (); refresh (); }

// The layer's properties: its name, its blend mode (a mask on the layer below only), its opacity, shown.
static struct { long name, blend, clip, op, oplb, hint; } LD;
static void ld_show ()
{
	int b = (int) ui_w_get_value (LD.blend);
	ui_w_enable (LD.clip, (b == B_MASK || b == B_CUTOUT) && D.cur != 0);
	ui_w_set_text (LD.hint, (long) (b == B_MASK ? TR ("Keeps what is under it where it is opaque.") : b == B_CUTOUT ? TR ("Removes what is under it where it is opaque.") : ""));
	char t[16]; fmt_int (t, (int) ui_w_get_value (LD.op), " %"); ui_w_set_text (LD.oplb, (long) t);
}
static long ld_fn (long, long, long code) { if (code == UN_CHANGE) { ld_show (); ui_w_draw (0); } return 0; }
static void cmd_layer_props ()
{
	settle ();
	enum { W = 420, H = 290 };
	const Layer &l = D.lay[D.cur];
	long d = ui_dialog_begin (W, H, (long) TR ("Layer Properties"));
	int y = 12;
	d_label (20, y, 90, "Name"); LD.name = d_field (120, y, 276, l.name);
	d_label (20, y + 38, 90, "Blend"); LD.blend = d_drop (120, y + 38, 276, d_items (BLEND_NAMES, NBLENDS), l.blend);
	LD.clip = ui_checkbox (120, y + 74, 280, CTL, (long) TR ("On the layer below only"), l.clip);
	d_label (20, y + 110, 90, "Opacity"); LD.op = ui_slider (120, y + 110, 220, CTL, 0, 100, (l.opacity * 100 + 127) / 255); LD.oplb = d_label (350, y + 110, 56, "");
	long vis = ui_checkbox (120, y + 146, 200, CTL, (long) TR ("Shown"), l.visible);
	LD.hint = d_label (20, y + 180, 380, "", ULB_DIM);
	d_ok_cancel (d, W);
	ld_show ();
	if (ui_dialog_run (d, (long) ld_fn, 0) == 1)
	{
		Layer &m = D.lay[D.cur];
		if (wtext (LD.name)[0]) scpy (m.name, wtext (LD.name), sizeof m.name);
		int b = (int) ui_w_get_value (LD.blend);
		m.opacity = (int) ui_w_get_value (LD.op) * 255 / 100; m.visible = ui_w_get_value (vis) != 0; m.blend = b;
		m.clip = ui_w_get_value (LD.clip) && (b == B_MASK || b == B_CUTOUT) && D.cur != 0;
		ui_dialog_end (d);
		props_changed ();
	}
	else ui_dialog_end (d);
	refresh (); focus_view ();
}
static void blend_popup (int x, int y)
{
	Layer &l = D.lay[D.cur];
	Popup m;
	for (int b = 0; b < NBLENDS; b++) { if (b == B_MASK) m.separator (); m.add (TR (BLEND_NAMES[b]), b + 1, true, l.blend == b); }
	m.separator ();
	m.add (TR ("On the layer below only"), 100, (l.blend == B_MASK || l.blend == B_CUTOUT) && D.cur > 0, l.clip);
	int r = m.run (x, y);
	if (r >= 1 && r <= NBLENDS) { l.blend = r - 1; props_changed (); }
	else if (r == 100) { l.clip = !l.clip; props_changed (); }
	refresh (); focus_view ();
}
static void layer_do (int cmd, int arg, int x, int y, int)
{
	switch (cmd)
	{
	case L_ADD: cmd_layer_new (); break;
	case L_DUP: cmd_layer_dup (); break;
	case L_DEL: cmd_layer_del (); break;
	case L_UP: cmd_layer_up (); break;
	case L_DOWN: cmd_layer_down (); break;
	case L_MERGE: cmd_layer_merge (); break;
	case L_PROPS: cmd_layer_props (); break;
	case L_RENAME: if (arg != D.cur) { settle (); D.cur = arg; } cmd_layer_props (); break;
	case L_BLEND: blend_popup (x, y); break;
	case L_MENU:
	{
		Popup m;
		m.add (TR ("New Layer"), 1); m.add (TR ("New Layer from a File..."), 9); m.add (TR ("Paste as New Layer"), 10); m.add (TR ("Duplicate"), 2); m.add (TR ("Delete"), 3, D.n > 1);
		m.separator ();
		m.add (TR ("Move Up"), 4, D.cur < D.n - 1); m.add (TR ("Move Down"), 5, D.cur > 0); m.add (TR ("Merge Down"), 6, D.cur > 0);
		m.separator ();
		m.add (D.lay[D.cur].visible ? TR ("Hide") : TR ("Show"), 7); m.add (TR ("Properties..."), 8);
		switch (m.run (x, y))
		{
		case 1: cmd_layer_new (); break; case 2: cmd_layer_dup (); break; case 3: cmd_layer_del (); break;
		case 4: cmd_layer_up (); break; case 5: cmd_layer_down (); break; case 6: cmd_layer_merge (); break;
		case 7: D.lay[D.cur].visible = !D.lay[D.cur].visible; props_changed (); focus_view (); break; case 8: cmd_layer_props (); break;
		case 9: cmd_open_layer (); break; case 10: cmd_paste_layer (); break;
		default: focus_view ();
		}
		break;
	}
	}
}
static void layer_cmd (int cmd, int arg, int x, int y)
{
	switch (cmd)
	{
	case L_SELECT: if (arg != D.cur) { settle (); D.cur = arg; compose_all (); } refresh (); focus_view (); break;
	case L_TOGGLE: D.lay[arg].visible = !D.lay[arg].visible; props_changed (); focus_view (); break;
	case L_OPACITY:
		if (D.lay[D.cur].opacity != arg * 255 / 100)
		{
			D.lay[D.cur].opacity = arg * 255 / 100; D.changes++;
			compose_all ();
			refresh ();
		}
		break;
	default: later (layer_do, cmd, arg, x, y);
	}
}

// ---- the adjustments --------------------------------------------------------------------------------------------
struct Spec { const char *label; int lo, hi, def; const char *suffix; };
static Spec aj_spec (int k, int i)
{
	static const Spec S[AJ_COUNT][3] = {
		{ { "Brightness", -100, 100, 0, "" }, { "Contrast", -100, 100, 0, "" }, { 0, 0, 0, 0, 0 } },
		{ { "Hue", -180, 180, 0, " deg" }, { "Saturation", -100, 100, 0, "" }, { "Lightness", -100, 100, 0, "" } },
		{ { "Amount", 0, 100, 100, " %" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
		{ { "Hue", 0, 360, 30, " deg" }, { "Saturation", 0, 100, 50, " %" }, { "Lightness", -100, 100, 0, "" } },
		{ { "Red from", 0, 4, 0, 0 }, { "Green from", 0, 4, 1, 0 }, { "Blue from", 0, 4, 2, 0 } },
		{ { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
		{ { "Amount", 0, 100, 100, " %" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
		{ { "Levels", 2, 16, 4, "" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
		{ { "Level", 0, 255, 128, "" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
		{ { "Radius", 1, 20, 2, " px" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
		{ { "Amount", 0, 300, 80, " %" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } },
		{ { "Block", 2, 64, 8, " px" }, { 0, 0, 0, 0, 0 }, { 0, 0, 0, 0, 0 } } };
	return S[k][i];
}
static struct { int kind, n, sc; long w[3], lb[3], scope; } AD;
static int ad_val (int i) { return i >= AD.n ? 0 : (int) ui_w_get_value (AD.w[i]); }
static void ad_apply ()
{
	for (int i = 0; i < AD.n; i++)
		if (AD.lb[i]) { char b[16]; fmt_int (b, ad_val (i), aj_spec (AD.kind, i).suffix); ui_w_set_text (AD.lb[i], (long) b); }
	adjust_apply (AD.kind, ad_val (0), ad_val (1), ad_val (2));
	show_main ();
}
static long ad_fn (long, long w, long code)
{
	if (code != UN_CHANGE) return 0;
	if (w == AD.scope)
	{
		adjust_restore (); adjust_shown ();
		AD.sc = (int) ui_w_get_value (AD.scope);
		if (AD.sc == SC_SELECTION && !has_sel ()) { AD.sc = SC_LAYER; ui_w_set_value (AD.scope, SC_LAYER); }
		adjust_begin (AD.sc);
	}
	ad_apply ();
	ui_w_draw (0);
	return 0;
}
static void run_adjust (int kind)
{
	settle ();
	int n = 0; while (n < 3 && aj_spec (kind, n).label) n++;
	bool note = kind == AJ_REMAP || kind == AJ_INVERT;
	int H = 18 + n * 44 + 44 + (note ? 28 : 0) + 56;
	enum { W = 460 };
	char title[48]; scpy (title, TR (ADJUST_NAMES[kind]), sizeof title); int tn = slen (title); if (tn > 3 && title[tn - 1] == '.') title[tn - 3] = 0;
	AD.kind = kind; AD.n = n; AD.sc = has_sel () ? SC_SELECTION : SC_LAYER;
	long d = ui_dialog_begin (W, H, (long) title);
	int y = 14;
	for (int i = 0; i < n; i++)
	{
		Spec p = aj_spec (kind, i);
		d_label (20, y + i * 44, 116, p.label);
		AD.lb[i] = 0;
		if (kind == AJ_REMAP) { AD.w[i] = d_drop (140, y + i * 44, 200, d_items (CHANNEL_NAMES, 5), p.def); continue; }
		AD.w[i] = ui_slider (140, y + i * 44, 220, CTL, p.lo, p.hi, p.def);
		AD.lb[i] = d_label (370, y + i * 44, 76, "");
	}
	d_label (20, y + n * 44 + 4, 116, "Apply to");
	AD.scope = d_drop (140, y + n * 44 + 4, 200, d_items (SCOPE_NAMES, 3), AD.sc);
	if (note) d_label (20, y + n * 44 + 40, 420, kind == AJ_REMAP ? "Each channel taken from another one (or black, white)." : "Each colour made its opposite.", ULB_DIM);
	d_ok_cancel (d, W);
	adjust_begin (AD.sc);
	ad_apply ();
	long r = ui_dialog_run (d, (long) ad_fn, 0);
	ui_dialog_end (d);
	if (r == 1) adjust_commit (); else adjust_cancel ();
	refresh (); focus_view ();
}
static void adjust_popup (int x, int y)
{
	Popup m;
	for (int i = 0; i < AJ_COUNT; i++) { if (i == AJ_INVERT || i == AJ_FIRST_FILTER) m.separator (); m.add (TR (ADJUST_NAMES[i]), i + 1); }
	int r = m.run (x, y);
	if (r > 0) run_adjust (r - 1); else focus_view ();
}

// ---- the brushes' gallery, the gradients' list ------------------------------------------------------------------
// A dialog of cells (a brush's stroke, a gradient's bar) -> the one touched, -1.
enum { G_BRUSHES, G_GRADIENTS, G_PATTERNS };
static struct { int kind, hot, cur; long dlg, view; int w, h; } GL;
static int gl_cell (int mx, int my, int *cx = 0, int *cy = 0, int *cw = 0, int *ch = 0)
{
	if (GL.kind == G_GRADIENTS)
	{
		int i = (my - 6) / 30;
		if (mx < 4 || mx >= GL.w - 4 || my < 6) return -1;
		if (i >= 0 && i < g_ngrads) { if (cx) { *cx = 4; *cy = 6 + i * 30; *cw = GL.w - 8; *ch = 28; } return i; }
		if (my >= 6 + g_ngrads * 30 + 6 && my < GL.h - 4) { if (cx) { *cx = 4; *cy = 6 + g_ngrads * 30 + 6; *cw = GL.w - 8; *ch = 28; } return 1000; }
		return -1;
	}
	int base = GL.kind == G_PATTERNS ? BR_FIRST_PATTERN : 0;
	for (int i = 0; i < (GL.kind == G_PATTERNS ? (int) NPATTERNS : (int) BR_COUNT); i++)
	{
		int k = base + i, row, col, y0;
		if (GL.kind == G_BRUSHES && k >= BR_FIRST_PATTERN) { int j = k - BR_FIRST_PATTERN; row = j / 3; col = j % 3; y0 = 30 + 3 * 62 + 38; }
		else { row = i / 3; col = i % 3; y0 = 30; }
		int x = 10 + col * 138, y = y0 + row * 62;
		if (mx >= x && mx < x + 132 && my >= y && my < y + 58) { if (cx) { *cx = x; *cy = y; *cw = 132; *ch = 58; } return k; }
	}
	return -1;
}
static long gl_paint (long v, long, long)
{
	begin_paint (v);
	unsigned bg = C_FIELD, dim = C_DIM;
	fill (0, 0, GL.w, GL.h, bg);
	if (GL.kind == G_GRADIENTS)
	{
		for (int i = 0; i < g_ngrads; i++)
		{
			int y = 6 + i * 30;
			if (i == GL.cur) rbox (4, y, GL.w - 8, 28, 4, C_ACCENT);
			else if (i == GL.hot) rbox (4, y, GL.w - 8, 28, 4, mix (bg, C_ACCENT, 40));
			grad_draw (12, y + 6, 100, 16, i);
			text_l (122, y, 28, TR (g_grads[i].name), i == GL.cur ? ink_on (C_ACCENT) : C_TEXT, 0, GL.w - 180);
			if (g_grads[i].user) text_l (GL.w - 50, y, 28, TR ("yours"), i == GL.cur ? ink_on (C_ACCENT) : dim);
		}
		int y = 6 + g_ngrads * 30;
		fill (10, y + 2, GL.w - 20, 1, C_LINE);
		if (GL.hot == 1000) rbox (4, y + 6, GL.w - 8, 28, 4, mix (bg, C_ACCENT, 40));
		text_l (14, y + 6, 28, TR ("Edit gradients..."), C_TEXT);
		text_l (GL.w - 80, y + 6, 28, "GIMP .ggr", dim);
		return 0;
	}
	text (14, 8, TR (GL.kind == G_BRUSHES ? "Brushes" : "Patterns"), dim, 2);
	if (GL.kind == G_BRUSHES)
	{
		int y = 30 + 3 * 62 + 4;
		fill (12, y, GL.w - 24, 1, C_LINE);
		text (14, y + 10, TR ("Patterns (the colour, on the picture's grid)"), dim, 2);
	}
	int base = GL.kind == G_PATTERNS ? BR_FIRST_PATTERN : 0;
	for (int i = 0; i < (GL.kind == G_PATTERNS ? (int) NPATTERNS : (int) BR_COUNT); i++)
	{
		int k = base + i, row, col, y0;
		if (GL.kind == G_BRUSHES && k >= BR_FIRST_PATTERN) { int j = k - BR_FIRST_PATTERN; row = j / 3; col = j % 3; y0 = 30 + 3 * 62 + 38; }
		else { row = i / 3; col = i % 3; y0 = 30; }
		int x = 10 + col * 138, y = y0 + row * 62, w = 132, h = 58;
		if (k == GL.cur) panel (x, y, w, h, 5, mix (bg, C_ACCENT, 60), mix (bg, C_ACCENT, 150));
		else if (k == GL.hot) rbox (x, y, w, h, 5, mix (bg, C_ACCENT, 30));
		brush_sample (x + 10, y + 4, w - 20, 32, k, C_TEXT);
		text_c (x, y + 36, w, 20, TR (BRUSH_NAMES[k]), C_TEXT);
	}
	return 0;
}
static long gl_mouse (long v, long kind, long x, long y, long)
{
	int h = kind == 3 ? -1 : gl_cell ((int) x, (int) y);
	if (h != GL.hot) { GL.hot = h; ui_view_dirty (v); }
	if (kind == 2 && h >= 0) ui_dialog_close (GL.dlg, h + 1);
	return 0;
}
static int gallery (int kind)
{
	GL.kind = kind; GL.hot = -1;
	GL.w = kind == G_GRADIENTS ? 340 : 430;
	if (kind == G_BRUSHES) { GL.h = 30 + 3 * 62 + 38 + 2 * 62 + 8; GL.cur = g_brush; }
	else if (kind == G_PATTERNS) { GL.h = 30 + 2 * 62 + 8; GL.cur = g_pattern; }
	else { GL.h = 10 + g_ngrads * 30 + 40; GL.cur = g_grad; }
	if (GL.h > ui.h - 40) GL.h = ui.h - 40;
	int top = kind == G_GRADIENTS ? 30 : 0;				// (the frame's buttons over its first row)
	GL.dlg = ui_dialog_begin (GL.w, GL.h + top + 52, (long) TR (kind == G_BRUSHES ? "Brushes" : kind == G_PATTERNS ? "Patterns" : "Gradients"));
	GL.view = ui_view (0, top, GL.w, GL.h, 0);
	ui_view_on (GL.view, (long) gl_paint, (long) gl_mouse, 0, 0);
	ui_dialog_button (GL.dlg, 0, (long) TR ("Cancel"), 0, 0, GL.w - 12);
	ui_dialog_cancel (GL.dlg, 0);
	ui_dialog_timer (GL.dlg, (long) d_tick, 40);
	long r = ui_dialog_run (GL.dlg, 0, 0);
	ui_dialog_end (GL.dlg);
	return (int) r;
}

// ---- the gradient editor ------------------------------------------------------------------------------------------
// The gradients' list (the presets, yours), the one chosen: its name, its bar with the stops under it (touch under the
// bar: a new stop; drag: move it, off the bar: remove it) and the midpoints above, the chosen stop's colour, position and
// opacity. A preset is copied to be changed. OK keeps yours on the card.
enum { GE_W = 680, GE_H = 452, GE_LW = 210, GE_Y = 14 };
static struct GradEd
{
	int gi, stop, drag; bool dragMid;
	long view, dlg, name, pos, col, op, oplb, bRev, bEven, bNew, bCopy, bDel;
	Gradient keep[GMAX]; int nkeep, keepCur;
} *GE;
static inline Gradient &ge_g () { return g_grads[GE->gi]; }
static inline int ge_barX () { return GE_LW + 32; }
static inline int ge_barY () { return GE_Y + 64; }
static inline int ge_barW () { return GE_W - GE_LW - 32 - 18; }
static void ge_sync ()
{
	Gradient &g = ge_g (); grad_dyn (g, g_col1, g_col2);
	GE->stop = pclamp (GE->stop, 0, g.n - 1);
	ui_w_set_text (GE->name, (long) (g.user ? g.name : TR (g.name)));
	char b[16]; fmt_int (b, g.pos[GE->stop] / 10, 0); ui_w_set_text (GE->pos, (long) b);
	int v = (int) ((g.col[GE->stop] >> 24) * 100 / 255);
	ui_w_set_value (GE->op, v);
	fmt_int (b, v, " %"); ui_w_set_text (GE->oplb, (long) b);
	bool ed = g.user;
	ui_w_enable (GE->name, ed); ui_w_enable (GE->pos, ed); ui_w_enable (GE->col, ed); ui_w_enable (GE->op, ed);
	ui_w_enable (GE->bRev, ed); ui_w_enable (GE->bEven, ed); ui_w_enable (GE->bDel, ed);
	ui_view_dirty (GE->view);
	ui_w_draw (0);
}
static void ge_add_copy (const Gradient &src, const char *nm)
{
	if (g_ngrads >= GMAX) return;
	Gradient &g = g_grads[g_ngrads] = src;
	grad_dyn (g, g_col1, g_col2);
	g.dyn = 0; g.user = true; g.file[0] = 0;
	scpy (g.name, nm, sizeof g.name);
	GE->gi = g_ngrads++; GE->stop = 0;
	ge_sync ();
}
static long ge_fn (long, long w, long code)
{
	Gradient &g = ge_g ();
	if (code == UN_CHANGE)
	{
		if (!g.user) return 0;
		if (w == GE->name) scpy (g.name, wtext (GE->name), sizeof g.name);
		else if (w == GE->pos && GE->stop > 0 && GE->stop < g.n - 1) g.pos[GE->stop] = pclamp (parse_int (wtext (GE->pos)) * 10, g.pos[GE->stop - 1], g.pos[GE->stop + 1]);
		else if (w == GE->op)
		{
			int v = (int) ui_w_get_value (GE->op);
			g.col[GE->stop] = (g.col[GE->stop] & 0xFFFFFF) | (unsigned) (v * 255 / 100) << 24;
			char b[12]; fmt_int (b, v, " %"); ui_w_set_text (GE->oplb, (long) b);
		}
		ui_view_dirty (GE->view);
		return 0;
	}
	if (code != UN_ACTIVATE) return 0;
	if (w == GE->col && g.user)
	{
		long c = ui_color_dialog ((long) TR ("Stop Colour"), g.col[GE->stop] & 0xFFFFFF);
		if (c >= 0) g.col[GE->stop] = (g.col[GE->stop] & 0xFF000000u) | ((unsigned) c & 0xFFFFFF);
		ui_view_dirty (GE->view);
	}
	else if (w == GE->bRev && g.user)
	{
		for (int a = 0, b = g.n - 1; a < b; a++, b--) { int p = g.pos[a]; g.pos[a] = 1000 - g.pos[b]; g.pos[b] = 1000 - p; unsigned c = g.col[a]; g.col[a] = g.col[b]; g.col[b] = c; }
		if (g.n & 1) g.pos[g.n / 2] = 1000 - g.pos[g.n / 2];
		for (int a = 0, b = g.n - 2; a < b; a++, b--) { int m = g.mid[a]; g.mid[a] = 1000 - g.mid[b]; g.mid[b] = 1000 - m; }
		if ((g.n - 1) & 1) g.mid[(g.n - 2) / 2] = 1000 - g.mid[(g.n - 2) / 2];
		ge_sync ();
	}
	else if (w == GE->bEven && g.user) { for (int i = 0; i < g.n; i++) { g.pos[i] = i * 1000 / (g.n - 1); g.mid[i] = 500; } ge_sync (); }
	else if (w == GE->bNew) ge_add_copy (g_grads[0], TR ("My gradient"));
	else if (w == GE->bCopy) { char nm[40]; snprintf (nm, sizeof nm, TR ("%s copy"), TR (g.name)); ge_add_copy (g, nm); }
	else if (w == GE->bDel && g.user) { grad_remove (GE->gi); GE->gi = pclamp (GE->gi - 1, 0, g_ngrads - 1); GE->stop = 0; ge_sync (); }
	return 0;
}
static long ge_paint (long v, long, long)
{
	begin_paint (v);
	unsigned bg = C_BG, dim = C_DIM;
	fill (0, 0, GE_W, GE_H, bg);
	// the list
	int lx = 16, ly = GE_Y, lh = GE_H - ly - 56;
	panel (lx, ly, GE_LW, lh, 5, C_FIELD, C_LINE);
	int rows = (lh - 8) / 34, top = pmax (0, GE->gi - rows + 1);
	for (int i = top; i < g_ngrads && i - top < rows; i++)
	{
		int y = ly + 4 + (i - top) * 34;
		if (i == GE->gi) rbox (lx + 3, y, GE_LW - 6, 32, 4, C_ACCENT);
		grad_draw (lx + 8, y + 4, 54, 24, i);
		text_l (lx + 70, y, 32, TR (g_grads[i].name), i == GE->gi ? ink_on (C_ACCENT) : C_TEXT, 0, GE_LW - 80);
	}
	// the gradient
	Gradient &g = ge_g (); grad_dyn (g, g_col1, g_col2);
	int X = ge_barX (), Y = ge_barY (), BW = ge_barW ();
	text_l (X, GE_Y, CTL, TR ("Name"), C_TEXT);
	grad_draw (X, Y + 14, BW, 52, GE->gi);
	for (int i = 0; i + 1 < g.n; i++)				// the midpoints
	{
		int mx = X + (g.pos[i] + (g.pos[i + 1] - g.pos[i]) * g.mid[i] / 1000) * (BW - 1) / 1000;
		int d[8] = { V (mx), V (Y + 1), V (mx + 5), V (Y + 7), V (mx), V (Y + 13), V (mx - 5), V (Y + 7) };
		ui_vp_poly ((long) d, 4, C_TEXT);
	}
	for (int i = 0; i < g.n; i++)					// the stops
	{
		int sx = X + g.pos[i] * (BW - 1) / 1000, sy = Y + 68;
		int d[10] = { V (sx), V (sy), V (sx + 8), V (sy + 10), V (sx + 8), V (sy + 24), V (sx - 8), V (sy + 24), V (sx - 8), V (sy + 10) };
		ui_vp_poly ((long) d, 5, i == GE->stop ? C_ACCENT : C_DIM);
		fill (sx - 5, sy + 11, 10, 10, g.col[i] & 0xFFFFFF); frame (sx - 5, sy + 11, 10, 10, 0x303030);
	}
	text_l (X, Y + 96, 20, TR (g.user ? "Touch under the bar: a new stop.  Drag a stop: move it (off the bar: remove it)." : "A preset: Copy it to change it."), dim, 0, BW);
	int sy = GE_Y + 200;
	text_l (X, sy, CTL, TR ("Colour"), C_TEXT);
	fill (X + 58, sy + 6, 20, CTL - 12, g.col[GE->stop] & 0xFFFFFF); frame (X + 58, sy + 6, 20, CTL - 12, C_LINE);
	text_l (X + 180, sy, CTL, TR ("Position"), C_TEXT);
	text_l (X + 324, sy, CTL, "%", dim);
	text_l (X, sy + 40, CTL, TR ("Opacity"), C_TEXT);
	return 0;
}
static long ge_mouse (long v, long kind, long lx, long ly, long info)
{
	int mx = (int) lx, my = (int) ly; bool bl = ((info >> 8) & 1) && kind != 2;
	Gradient &g = ge_g ();
	int X = ge_barX (), Y = ge_barY (), BW = ge_barW ();
	if (GE->drag >= 0)
	{
		int dr = GE->drag;
		if (!bl) { GE->drag = -1; return 0; }
		if (GE->dragMid) { int a = g.pos[dr], b = g.pos[dr + 1]; if (b > a) g.mid[dr] = pclamp (((mx - X) * 1000 / (BW - 1) - a) * 1000 / (b - a), 20, 980); ui_view_dirty (v); return 0; }
		if ((my > Y + 130 || my < Y - 20) && dr > 0 && dr < g.n - 1 && g.n > 2)		// (off the bar: removed)
		{
			for (int i = dr; i + 1 < g.n; i++) { g.pos[i] = g.pos[i + 1]; g.col[i] = g.col[i + 1]; g.mid[i] = g.mid[i + 1]; }
			g.n--; GE->drag = -1; GE->stop = 0; ge_sync (); return 0;
		}
		if (dr > 0 && dr < g.n - 1) g.pos[dr] = pclamp ((mx - X) * 1000 / (BW - 1), g.pos[dr - 1], g.pos[dr + 1]);
		char b[12]; fmt_int (b, g.pos[GE->stop] / 10, 0); ui_w_set_text (GE->pos, (long) b); ui_w_draw (GE->pos);
		ui_view_dirty (v);
		return 0;
	}
	if (kind != 1 || !bl) return 0;
	if (mx >= 16 && mx < 16 + GE_LW && my >= GE_Y && my < GE_H - 56)	// (the list)
	{
		int lh = GE_H - GE_Y - 56, rows = (lh - 8) / 34, top = pmax (0, GE->gi - rows + 1);
		int i = top + (my - GE_Y - 4) / 34;
		if (i >= 0 && i < g_ngrads) { GE->gi = i; GE->stop = 0; ge_sync (); }
		return 0;
	}
	if (g.user && mx >= X - 8 && mx < X + BW + 8)
	{
		if (my >= Y && my < Y + 14)				// (a midpoint)
			for (int i = 0; i + 1 < g.n; i++)
			{
				int m = X + (g.pos[i] + (g.pos[i + 1] - g.pos[i]) * g.mid[i] / 1000) * (BW - 1) / 1000;
				if (mx >= m - 8 && mx <= m + 8) { GE->drag = i; GE->dragMid = true; return 0; }
			}
		if (my >= Y + 66 && my < Y + 96)				// (a stop, or a new one)
		{
			int best = -1, bd = 12;
			for (int i = 0; i < g.n; i++) { int sx = X + g.pos[i] * (BW - 1) / 1000, d = mx > sx ? mx - sx : sx - mx; if (d < bd) { bd = d; best = i; } }
			if (best < 0 && g.n < GMAXSTOPS)
			{
				int p = pclamp ((mx - X) * 1000 / (BW - 1), 0, 1000), at = 1;
				while (at < g.n && g.pos[at] < p) at++;
				unsigned c = grad_at (g, p / 1000.0f);
				for (int i = g.n; i > at; i--) { g.pos[i] = g.pos[i - 1]; g.col[i] = g.col[i - 1]; g.mid[i] = g.mid[i - 1]; }
				g.pos[at] = p; g.col[at] = c; g.mid[at] = 500; g.mid[at - 1] = 500; g.n++;
				best = at;
			}
			if (best >= 0) { GE->stop = best; GE->drag = best; GE->dragMid = false; ge_sync (); }
		}
	}
	return 0;
}
static void cmd_gradients ()
{
	settle ();
	GE = new GradEd;
	GE->gi = pclamp (g_grad, 0, g_ngrads - 1); GE->stop = 0; GE->drag = -1; GE->dragMid = false; GE->nkeep = g_ngrads; GE->keepCur = g_grad;
	for (int i = 0; i < g_ngrads; i++) GE->keep[i] = g_grads[i];
	long d = GE->dlg = ui_dialog_begin (GE_W, GE_H, (long) TR ("Gradient Editor"));
	GE->view = ui_view (0, 0, GE_W, GE_H - 48, 0);
	ui_view_on (GE->view, (long) ge_paint, (long) ge_mouse, 0, 0);
	int X = GE_LW + 32, y = GE_Y;
	GE->name = ui_textbox (X + 60, y, GE_W - X - 78, CTL, 0);
	GE->col = ui_pushbutton (X + 84, y + 200, 90, CTL, (long) TR ("Change..."), 0);
	GE->pos = ui_textbox (X + 250, y + 200, 70, CTL, 0);
	GE->op = ui_slider (X + 84, y + 240, 200, CTL, 0, 100, 100);
	GE->oplb = ui_label (X + 296, y + 240, 60, CTL, (long) "", 0);
	GE->bRev = ui_pushbutton (X, y + 290, 150, CTL, (long) TR ("Reverse"), 0);
	GE->bEven = ui_pushbutton (X + 158, y + 290, 170, CTL, (long) TR ("Space evenly"), 0);
	int ly = GE_H - 90;
	GE->bNew = ui_pushbutton (16, ly, 80, CTL, (long) TR ("New"), 0);
	GE->bCopy = ui_pushbutton (100, ly, 62, CTL, (long) TR ("Copy"), 0);
	GE->bDel = ui_pushbutton (166, ly, 92, CTL, (long) TR ("Delete"), 0);
	d_ok_cancel (d, GE_W);
	ui_dialog_timer (d, (long) d_tick, 40);
	ge_sync ();
	long r = ui_dialog_run (d, (long) ge_fn, 0);
	ui_dialog_end (d);
	if (r != 1) { g_ngrads = GE->nkeep; for (int i = 0; i < GE->nkeep; i++) g_grads[i] = GE->keep[i]; g_grad = GE->keepCur; }
	else
	{
		for (int i = 0; i < g_ngrads; i++) if (g_grads[i].user) grad_save (g_grads[i]);
		g_grad = GE->gi;
	}
	delete GE; GE = 0;
	refresh (); focus_view ();
}

// ---- colours, the ribbon, the options ---------------------------------------------------------------------------
static void add_custom (unsigned c)
{
	c |= 0xFF000000u;
	for (int i = 0; i < 10; i++) if (g_custom[i] == c) return;
	for (int i = 9; i > 0; i--) g_custom[i] = g_custom[i - 1];
	g_custom[0] = c;
}
static void colours_changed () { if (g_text.on) text_preview (); if (g_gj.on) grad_preview (); }
static void cmd_edit_colours ()
{
	long c = ui_color_dialog ((long) TR ("Edit Colours"), (g_activeCol == 2 ? g_col2 : g_col1) & 0xFFFFFF);
	if (c >= 0)
	{
		if (g_activeCol == 2) g_col2 = (unsigned) c | 0xFF000000u; else g_col1 = (unsigned) c | 0xFF000000u;
		add_custom ((unsigned) c);
		colours_changed ();
	}
	refresh (); focus_view ();
}
static void cmd_swap_colours () { unsigned t = g_col1; g_col1 = g_col2; g_col2 = t; colours_changed (); refresh (); focus_view (); }
static void cmd_grid () { g_grid = !g_grid; if (g_grid && g_view.zoom < 300) g_view.setZoom (400); refresh (); focus_view (); }
static void cmd_fit () { g_view.zoomFit (); refresh (); focus_view (); }
static void cmd_zoom_in () { g_view.zoomStep (1); refresh (); focus_view (); }
static void cmd_zoom_out () { g_view.zoomStep (-1); refresh (); focus_view (); }
static void cmd_zoom_100 () { g_view.setZoom (100); refresh (); focus_view (); }

static void rotate_popup (int x, int y)
{
	Popup m;
	m.add (TR ("Rotate Right 90"), 1); m.add (TR ("Rotate Left 90"), 2); m.add (TR ("Rotate 180"), 3);
	m.separator ();
	m.add (TR ("Flip Vertical"), 4); m.add (TR ("Flip Horizontal"), 5);
	switch (m.run (x, y)) { case 1: transform (1); break; case 2: transform (-1); break; case 3: transform (2); break; case 4: transform (4); break; case 5: transform (3); break; default: focus_view (); }
}
static void select_popup (int x, int y)
{
	Popup m;
	m.add (TR ("Rectangle"), 1); m.add (TR ("Free-form (lasso)"), 2); m.add (TR ("Magic wand"), 3);
	m.separator ();
	m.add (TR ("Select All"), 4); m.add (TR ("Invert Selection"), 5); m.add (TR ("Deselect"), 6, has_sel ());
	m.separator ();
	m.add (TR ("Delete"), 7, has_sel () || D.fl.px); m.add (TR ("Crop to Selection"), 8, has_sel ());
	switch (m.run (x, y))
	{
	case 1: set_select (SK_RECT); break; case 2: set_select (SK_FREE); break; case 3: set_select (SK_WAND); break;
	case 4: cmd_select_all (); break; case 5: cmd_invert_sel (); break; case 6: cmd_deselect (); break;
	case 7: cmd_delete (); break; case 8: cmd_crop (); break;
	default: focus_view ();
	}
}
static void shapes_popup (int x, int y)
{
	Popup m;
	for (int k = 0; k < SH_COUNT; k++) m.add (TR (SHAPE_NAMES[k]), k + 1, true, g_tool == T_SHAPE && g_shape == k);
	int r = m.run (x, y);
	if (r > 0) { g_shape = r - 1; set_tool (T_SHAPE); } else focus_view ();
}
static void brushes_popup ()
{
	int r = gallery (G_BRUSHES);
	if (r > 0) { g_brush = r - 1; if (g_opac[T_BRUSH] == BRUSH_OPACITY[g_brush == BR_MARKER ? BR_BRUSH : BR_MARKER] || g_brush == BR_MARKER) g_opac[T_BRUSH] = BRUSH_OPACITY[g_brush]; }
	set_tool (T_BRUSH);
}
static void ribbon_do (int cmd, int arg, int btn, int x, int y)
{
	switch (cmd)
	{
	case C_PASTE:
		if (btn == 2) { Popup m; m.add (TR ("Paste"), 1); m.add (TR ("Paste as New Layer"), 2); m.add (TR ("Open as Layer..."), 3); int r = m.run (x, y); if (r == 1) cmd_paste (); else if (r == 2) cmd_paste_layer (); else if (r == 3) cmd_open_layer (); else focus_view (); }
		else cmd_paste ();
		break;
	case C_CUT: cmd_cut (); break;
	case C_COPY: cmd_copy (); break;
	case C_UNDO: cmd_undo (); break;
	case C_REDO: cmd_redo (); break;
	case C_SELECT: select_popup (x, y); break;
	case C_CROP: cmd_crop (); break;
	case C_RESIZE: cmd_resize (); break;
	case C_ROTATE: rotate_popup (x, y); break;
	case C_TOOL: set_tool (arg); break;
	case C_ADJUST: adjust_popup (x, y); break;
	case C_BRUSHES: brushes_popup (); break;
	case C_SHAPE: g_shape = arg; set_tool (T_SHAPE); break;
	case C_SHAPES: shapes_popup (x, y); break;
	case C_COL1: g_activeCol = 1; refresh (); focus_view (); break;
	case C_COL2: g_activeCol = 2; refresh (); focus_view (); break;
	case C_PALETTE: case C_CUSTOM:
	{
		unsigned c = cmd == C_PALETTE ? PALETTE[arg] : g_custom[arg];
		if (cmd == C_CUSTOM && !(c >> 24)) { cmd_edit_colours (); break; }
		c |= 0xFF000000u;
		if ((btn == 2) != (g_activeCol == 2)) g_col2 = c; else g_col1 = c;
		colours_changed ();
		refresh (); focus_view ();
		break;
	}
	case C_EDITCOL: cmd_edit_colours (); break;
	}
}
static void ribbon_cmd (int cmd, int arg, int btn, int x, int y) { later (ribbon_do, cmd, arg, btn, x, y); }
static void opt_do (int opt, int arg, int x, int y, int)
{
	switch (opt)
	{
	case O_BRUSHKIND: brushes_popup (); return;
	case O_SELRECT: set_select (SK_RECT); return;
	case O_SELFREE: set_select (SK_FREE); return;
	case O_SELWAND: set_select (SK_WAND); return;
	case O_SELNEW: g_selMode = SEL_REPLACE; break;
	case O_SELADD: g_selMode = SEL_ADD; break;
	case O_SELSUB: g_selMode = SEL_SUB; break;
	case O_CONTIG: g_contig = !g_contig; break;
	case O_ALLLAYERS: g_allLayers = !g_allLayers; break;
	case O_SELALL: cmd_select_all (); return;
	case O_INVERT: cmd_invert_sel (); return;
	case O_DESELECT: cmd_deselect (); return;
	case O_FILLCOL: g_fillMode = FM_COLOUR; break;
	case O_FILLGRAD: g_fillMode = FM_GRADIENT; break;
	case O_FILLPAT: g_fillMode = FM_PATTERN; break;
	case O_GRADLIST:
	{
		int r = gallery (G_GRADIENTS);
		if (r == 1001) { cmd_gradients (); return; }
		if (r > 0) { g_grad = r - 1; if (g_gj.on) grad_preview (); }
		break;
	}
	case O_GSHAPE: g_gshape = arg; if (g_gj.on) grad_preview (); break;
	case O_GREPEAT:
	{
		Popup m;
		for (int i = 0; i < 3; i++) m.add (TR (GREPEAT_NAMES[i]), i + 1, true, g_grepeat == i);
		int r = m.run (x, y);
		if (r > 0) { g_grepeat = r - 1; if (g_gj.on) grad_preview (); }
		break;
	}
	case O_GREVERSE: g_greverse = !g_greverse; if (g_gj.on) grad_preview (); break;
	case O_PATTERN: { int r = gallery (G_PATTERNS); if (r > 0) g_pattern = r - 1; break; }
	case O_GERASE: g_gradErase = !g_gradErase; break;
	case O_FONT:
	{
		Popup m;
		for (int i = 0; i < g_tnfam; i++) m.add (g_tfamNames[i], i + 1, true, i == g_tfam);
		int r = m.run (x, y);
		if (r > 0) g_tfam = r - 1;
		if (g_text.on) text_preview ();
		break;
	}
	case O_TSIZE:
	{
		static const int S[13] = { 8, 10, 12, 14, 16, 20, 24, 32, 40, 48, 64, 96, 144 };
		static const char *const N[13] = { "8", "10", "12", "14", "16", "20", "24", "32", "40", "48", "64", "96", "144" };
		Popup m;
		for (int i = 0; i < 13; i++) m.add (N[i], i + 1, true, g_tsize == S[i]);
		int r = m.run (x, y);
		if (r > 0) { g_tsize = S[r - 1]; if (g_text.on) text_preview (); }
		break;
	}
	case O_BOLD: g_tbold = !g_tbold; if (g_text.on) text_preview (); break;
	case O_ITALIC: g_titalic = !g_titalic; if (g_text.on) text_preview (); break;
	case O_UNDER: g_tunder = !g_tunder; if (g_text.on) text_preview (); break;
	case O_ALIGN: g_talign = arg; if (g_text.on) text_preview (); break;
	case O_SMOOTH: g_tsmooth = !g_tsmooth; if (g_text.on) text_preview (); break;
	case O_TBACK: g_tback = !g_tback; if (g_text.on) text_preview (); break;
	case O_OUTLINE: g_outline = !g_outline; if (!g_outline && !g_fillShape) g_fillShape = true; break;
	case O_FILLSH: g_fillShape = !g_fillShape; if (!g_outline && !g_fillShape) g_outline = true; break;
	}
	refresh (); focus_view ();
}
static void opt_cmd (int opt, int arg, int x, int y)
{
	if (opt <= O_TOL)						// (a slider: at once)
	{
		if (g_gj.on && opt == O_OPACITY) { g_gj.opacity = g_opac[g_tool] * 255 / 100; grad_preview (); }
		g_view.invalidate ();
		return;
	}
	later (opt_do, opt, arg, x, y);
}
static void zoom_do (int what, int, int, int, int)
{
	if (what == 2) { cmd_grid (); return; }
	if (what == 3) { cmd_fit (); return; }
	if (what == 4) { g_showLayers = !g_showLayers; layout ((int) ui.w, (int) ui.h); refresh (); focus_view (); return; }
	if (what) { g_view.zoomStep (what); refresh (); focus_view (); return; }
	static const int Z[8] = { 25, 50, 100, 200, 400, 800, 1600, 3200 };
	static const char *const N[8] = { "25 %", "50 %", "100 %", "200 %", "400 %", "800 %", "1600 %", "3200 %" };
	Popup m;
	m.add (TR ("Fit"), 1);
	for (int i = 0; i < 8; i++) m.add (N[i], i + 2);
	int r = m.run (g_status.popX, g_status.top - 9 * 26);
	if (r == 1) g_view.zoomFit (); else if (r > 1) g_view.setZoom (Z[r - 2]);
	refresh (); focus_view ();
}
static void zoom_cmd (int what)
{
	if (what >= 100) { g_view.setZoom (what - 100); refresh (); return; }
	later (zoom_do, what);
}
static void tool_changed () { refresh (); }
static void status_changed () { g_status.invalidate (); }

// ---- the menus --------------------------------------------------------------------------------------------------
enum { M_NEW = 1, M_OPEN, M_OPENLAYER, M_SAVE, M_SAVEAS, M_EXPNG, M_EXJPG, M_EXBMP, M_EXGIF, M_PRINT, M_QUIT,
       M_UNDO, M_REDO, M_CUT, M_COPY, M_PASTE, M_PASTELAYER, M_DELETE, M_SELALL, M_INVSEL, M_DESEL, M_CROP,
       M_ROTR, M_ROTL, M_ROT180, M_FLIPH, M_FLIPV, M_RESIZE,
       M_LNEW, M_LDUP, M_LDEL, M_LUP, M_LDOWN, M_LMERGE, M_LFLAT, M_LPROPS,
       M_EDITCOL, M_SWAP, M_GRADS, M_ZIN, M_ZOUT, M_Z100, M_ZFIT, M_GRID, M_LAYERS };
enum { M_AJ = 100 };
#define MA(k) (M_AJ + (int) (k))
static void build_menu ()
{
	static char b[4000]; int n = 0;
#define MENU(t) n += snprintf (b + n, sizeof b - (size_t) n, "M%s\n", TR (t))
#define ITEM(id, t, k) n += snprintf (b + n, sizeof b - (size_t) n, "I%d\t%s\t%s\n", (int) (id), TR (t), k)
#define SEP() n += snprintf (b + n, sizeof b - (size_t) n, "-\n")
	MENU ("File");
	ITEM (M_NEW, "New...", "Ctrl+N"); ITEM (M_OPEN, "Open...", "Ctrl+O"); ITEM (M_OPENLAYER, "Open as Layer...", ""); SEP ();
	ITEM (M_SAVE, "Save", "Ctrl+S"); ITEM (M_SAVEAS, "Save As...", ""); SEP ();
	ITEM (M_EXPNG, "Export as PNG...", "Ctrl+E"); ITEM (M_EXJPG, "Export as JPEG...", ""); ITEM (M_EXBMP, "Export as BMP...", ""); ITEM (M_EXGIF, "Export as GIF...", ""); SEP (); ITEM (M_PRINT, "Print...", "Ctrl+P"); SEP ();
	ITEM (M_QUIT, "Quit", "Ctrl+Q");
	MENU ("Edit");
	ITEM (M_UNDO, "Undo", "Ctrl+Z"); ITEM (M_REDO, "Redo", "Ctrl+Y"); SEP ();
	ITEM (M_CUT, "Cut", "Ctrl+X"); ITEM (M_COPY, "Copy", "Ctrl+C"); ITEM (M_PASTE, "Paste", "Ctrl+V"); ITEM (M_PASTELAYER, "Paste as New Layer", ""); ITEM (M_DELETE, "Delete", ""); SEP ();
	ITEM (M_SELALL, "Select All", "Ctrl+A"); ITEM (M_INVSEL, "Invert Selection", "Ctrl+I"); ITEM (M_DESEL, "Deselect", ""); ITEM (M_CROP, "Crop to Selection", "");
	MENU ("Image");
	ITEM (M_ROTR, "Rotate Right 90", ""); ITEM (M_ROTL, "Rotate Left 90", ""); ITEM (M_ROT180, "Rotate 180", ""); ITEM (M_FLIPH, "Flip Horizontal", ""); ITEM (M_FLIPV, "Flip Vertical", ""); SEP ();
	ITEM (M_RESIZE, "Resize...", "Ctrl+W");
	MENU ("Layers");
	ITEM (M_LNEW, "New Layer", "Ctrl+L"); ITEM (M_LDUP, "Duplicate Layer", ""); ITEM (M_LDEL, "Delete Layer", ""); SEP ();
	ITEM (M_LUP, "Move Up", ""); ITEM (M_LDOWN, "Move Down", ""); ITEM (M_LMERGE, "Merge Down", ""); ITEM (M_LFLAT, "Flatten", ""); SEP ();
	ITEM (M_LPROPS, "Layer Properties...", "");
	MENU ("Colours");
	ITEM (MA (AJ_BRIGHT), "Brightness / Contrast...", ""); ITEM (MA (AJ_HUE), "Hue / Saturation...", ""); ITEM (MA (AJ_DESAT), "Desaturate...", ""); ITEM (MA (AJ_COLORIZE), "Colorize...", ""); ITEM (MA (AJ_REMAP), "Remap the Channels...", ""); SEP ();
	ITEM (MA (AJ_INVERT), "Invert Colours...", ""); ITEM (MA (AJ_SEPIA), "Sepia...", ""); ITEM (MA (AJ_POSTER), "Posterize...", ""); ITEM (MA (AJ_THRESH), "Threshold...", ""); SEP ();
	ITEM (M_EDITCOL, "Edit Colours...", ""); ITEM (M_SWAP, "Swap Colours", "X"); ITEM (M_GRADS, "Gradients...", "");
	MENU ("Filters");
	ITEM (MA (AJ_BLUR), "Blur...", ""); ITEM (MA (AJ_SHARPEN), "Sharpen...", ""); ITEM (MA (AJ_PIXEL), "Pixelate...", "");
	MENU ("View");
	ITEM (M_ZIN, "Zoom In", "+"); ITEM (M_ZOUT, "Zoom Out", "-"); ITEM (M_Z100, "Actual Size (100%)", ""); ITEM (M_ZFIT, "Fit the Window", ""); SEP ();
	ITEM (M_GRID, "Grid", "Ctrl+G"); ITEM (M_LAYERS, "Layers' Panel", "");
#undef MENU
#undef ITEM
#undef SEP
	ui_menu ((long) b);
}
static void command (int id)
{
	if (id >= M_AJ) { run_adjust (id - M_AJ); return; }
	switch (id)
	{
	case M_NEW: cmd_new (); break;
	case M_OPEN: cmd_open (); break;
	case M_OPENLAYER: cmd_open_layer (); break;
	case M_SAVE: cmd_save (); break;
	case M_SAVEAS: cmd_save_as (); break;
	case M_EXPNG: export_as (".png"); break;
	case M_EXJPG: export_as (".jpg"); break;
	case M_EXBMP: export_as (".bmp"); break;
	case M_EXGIF: export_as (".gif"); break;
	case M_PRINT: cmd_print (); break;
	case M_QUIT: g_quit = true; break;
	case M_UNDO: cmd_undo (); break;
	case M_REDO: cmd_redo (); break;
	case M_CUT: cmd_cut (); break;
	case M_COPY: cmd_copy (); break;
	case M_PASTE: cmd_paste (); break;
	case M_PASTELAYER: cmd_paste_layer (); break;
	case M_DELETE: cmd_delete (); break;
	case M_SELALL: cmd_select_all (); break;
	case M_INVSEL: cmd_invert_sel (); break;
	case M_DESEL: cmd_deselect (); break;
	case M_CROP: cmd_crop (); break;
	case M_ROTR: transform (1); break;
	case M_ROTL: transform (-1); break;
	case M_ROT180: transform (2); break;
	case M_FLIPH: transform (3); break;
	case M_FLIPV: transform (4); break;
	case M_RESIZE: cmd_resize (); break;
	case M_LNEW: cmd_layer_new (); break;
	case M_LDUP: cmd_layer_dup (); break;
	case M_LDEL: cmd_layer_del (); break;
	case M_LUP: cmd_layer_up (); break;
	case M_LDOWN: cmd_layer_down (); break;
	case M_LMERGE: cmd_layer_merge (); break;
	case M_LFLAT: cmd_flatten (); break;
	case M_LPROPS: cmd_layer_props (); break;
	case M_EDITCOL: cmd_edit_colours (); break;
	case M_SWAP: cmd_swap_colours (); break;
	case M_GRADS: cmd_gradients (); break;
	case M_ZIN: cmd_zoom_in (); break;
	case M_ZOUT: cmd_zoom_out (); break;
	case M_Z100: cmd_zoom_100 (); break;
	case M_ZFIT: cmd_fit (); break;
	case M_GRID: cmd_grid (); break;
	case M_LAYERS: zoom_do (4, 0, 0, 0, 0); break;
	}
}

// ---- the window --------------------------------------------------------------------------------------------------
// the tools' letters (no control took the key)
static void root_key (unsigned ch, int mods)
{
	if (mods & (UI_MOD_CTRL | UI_MOD_ALT)) return;
	switch (ch)
	{
	case 's': case 'S': set_select (SK_RECT); break;
	case 'l': case 'L': set_select (SK_FREE); break;
	case 'w': case 'W': set_select (SK_WAND); break;
	case 'p': case 'P': set_tool (T_PENCIL); break;
	case 'b': case 'B': set_tool (T_BRUSH); break;
	case 'e': case 'E': set_tool (T_ERASER); break;
	case 'f': case 'F': set_tool (T_FILL); break;
	case 'g': case 'G': set_tool (T_GRADIENT); break;
	case 't': case 'T': set_tool (T_TEXT); break;
	case 'k': case 'K': set_tool (T_PICKER); break;
	case 'z': case 'Z': set_tool (T_ZOOM); break;
	case 'u': case 'U': set_tool (T_SHAPE); break;
	case 'x': case 'X': cmd_swap_colours (); break;
	case '+': cmd_zoom_in (); break;
	case '-': cmd_zoom_out (); break;
	}
}
static void on_drop ()
{
	char b[468]; long kind = 0;
	long n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES) return;
	b[n] = 0;
	for (int i = 0; b[i]; i++) if (b[i] == '\n' || b[i] == '\r') { b[i] = 0; break; }
	if (guard ()) open_path (b);
}
// The views' callbacks. (infos: clicks | buttons << 8 | mods << 16; the buttons of a release: none)
static inline int buttons (long kind, long info) { return kind == 2 ? 0 : (int) (info >> 8) & 7; }
static long canvas_paint (long, long, long) { g_view.paint (); return 0; }
static long canvas_mouse (long, long kind, long x, long y, long info)
{
	int b = buttons (kind, info);
	g_view.onMouse (kind == 3 ? -1 : (int) x, (int) y, b & 1, b & 2, b & 4, (unsigned) (info >> 16));
	return 0;
}
static long canvas_key (long, long hid, long ch, long mods) { return g_view.onKey ((int) hid, (unsigned) ch, (unsigned) mods) ? 0 : -1; }
static long canvas_wheel (long, long w) { g_view.wheel ((int) w, (unsigned) ui_ev_field (&ui, EV_MODS)); return 0; }
static long ribbon_paint (long v, long, long) { begin_paint (v); g_ribbon.paint (); return 0; }
static long ribbon_mouse (long, long kind, long x, long y, long info) { int b = (int) (info >> 8) & 7; g_ribbon.onMouse ((int) kind, (int) x, (int) y, b & 1, b & 2); return 0; }
static long opts_paint (long v, long, long) { begin_paint (v); g_opts.paint (); return 0; }
static long opts_mouse (long, long kind, long x, long y, long info) { g_opts.onMouse ((int) kind, (int) x, (int) y, buttons (kind, info) & 1); return 0; }
static long opts_wheel (long, long w) { g_opts.wheel ((int) w); return 0; }
static long layers_paint (long v, long, long) { begin_paint (v); g_layers.paint (); return 0; }
static long layers_mouse (long, long kind, long x, long y, long info) { int b = buttons (kind, info); g_layers.onMouse ((int) kind, (int) x, (int) y, b & 1, b & 2, (int) info & 255); return 0; }
static long layers_wheel (long, long w) { g_layers.wheel ((int) w); return 0; }
static long status_paint (long v, long, long) { begin_paint (v); g_status.paint (); return 0; }
static long status_mouse (long, long kind, long x, long y, long info) { g_status.onMouse ((int) kind, (int) x, (int) y, buttons (kind, info) & 1); return 0; }
static long make_view (long paint, long mouse, long key, long wheel, int flags = 0)
{
	long v = ui_view (0, 0, 20, 20, flags);
	ui_view_on (v, paint, mouse, key, wheel);
	return v;
}
static inline long now_ms () { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (long) t.tv_sec * 1000 + t.tv_nsec / 1000000; }

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/paint.app/lang/%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	mkdir ("SD0:/home", 0777); mkdir (HOMEDIR, 0777);
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int sw = (int) scr[0], avail = (int) (scr[1] - scr[3] - scr[2]) - (scr[2] ? 0 : 64);
	int W = pclamp (sw - 8, 640, 1180), H = pclamp (avail - 12, 400, 780);
	doc_new (800, 560, true);
	grad_presets (); grad_load_user ();
	g_col1 = 0xFF2B2B33;
	g_win = ui_window_ex ((sw - W) / 2, scr[3] + (avail - H) / 2, W, H, (long) TR ("Paint"), WF_RESIZABLE);
	if (g_win <= 0) return 1;
	ui_min_size (640, 400);
	ui_on_resize ((long) on_resize, 0);
	g_showLayers = W >= 960;
	g_ribbon.view = make_view ((long) ribbon_paint, (long) ribbon_mouse, 0, 0);
	g_opts.view = make_view ((long) opts_paint, (long) opts_mouse, 0, (long) opts_wheel);
	g_layers.view = make_view ((long) layers_paint, (long) layers_mouse, 0, (long) layers_wheel);
	g_status.view = make_view ((long) status_paint, (long) status_mouse, 0, 0);
	g_view.view = make_view ((long) canvas_paint, (long) canvas_mouse, (long) canvas_key, (long) canvas_wheel, UVW_FOCUS);
	ui_w_cursor (g_view.view, UC_CROSS);
	g_changed = refresh; g_toolChanged = tool_changed; g_statusChanged = status_changed;
	g_ribbonCmd = ribbon_cmd; g_layerCmd = layer_cmd; g_optCmd = opt_cmd; g_status.zoomCmd = zoom_cmd;
	build_menu ();
	layout (W, H);
	doc_loaded ();

	// A picture named on the command line, else the one kept at the last close.
	bool opened = argc > 1 && argv[1][0] && open_path (argv[1]);
	if (!opened && !access (RECOVER, F_OK))
	{
		if (ui_message ((long) TR ("Paint"), (long) TR ("Paint was closed with unsaved changes. Open the recovered picture?"), UM_YESNO) == 1 && doc_open (RECOVER))
		{
			g_path[0] = 0;
			unsigned n; unsigned char *b = read_all (RECOVER_NAME, &n);
			if (b) { int k = pmin ((int) n, (int) sizeof g_path - 1); for (int i = 0; i < k; i++) g_path[i] = (char) b[i]; g_path[k] = 0; delete[] b; }
			doc_loaded ();
			g_saved = D.changes + 1;
			opened = true;
		}
		unlink (RECOVER); unlink (RECOVER_NAME);
	}
	focus_view ();

	long wait = 40, last = 0;
	while (!g_quit)
	{
		long t = ui_event (wait);
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			unsigned ch = (unsigned) ui_ev_field (&ui, EV_CHAR); int mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0;
			long w = uikit_w_event (&code, &taken);
			(void) w;
			if (code == UN_DROP) on_drop ();
			if (press && !taken)
			{
				long id = ui_menu_key (0);
				if (id >= 0) command ((int) id);
				else root_key (ch, mods);
			}
		}
		else ui_w_idle ();
		if (g_pend.fn)
		{
			Pending p = g_pend; g_pend.fn = 0;
			p.fn (p.a, p.b, p.c, p.d, p.e);
		}
		// (the events first: the screen follows the pen, at most every 30 ms when they keep coming)
		wait = t > 0 ? 0 : 40;
		long now = now_ms ();
		if (t <= 0 || now - last >= 30) { ui_view_flush (); last = now; }
	}
	settle ();
	if (changed_doc ())
	{
		unsigned n; unsigned char *b = ora_save (&n);
		save_file (RECOVER, b, n);
		delete[] b;
		if (g_path[0]) save_file (RECOVER_NAME, g_path, (unsigned) slen (g_path));
	}
	return 0;
}
