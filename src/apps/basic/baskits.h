//
// baskits.h -- what #import gives a BASIC program on ARMPI (docs/BASIC.md, "Les kits"). The language's kits are Onyx's:
// programs written for it call UIKit.window, UIKit.label, FileKit.copy, ImageKit.formats, SystemKit.notify -- QBStudio's
// generated code too. Those names are made here, in the runtime, on ARMPI's kits; they are BASIC's interface, not a
// layer under an application. The table of "uikit" and "appkit" has these first, then the kit's own entries, so
// that a program can also call ui_window, ui_fill... by their C names; the kits Onyx did not have, or that have the
// same names (AudioKit, NetKit, GpuKit, GpioKit, MediaKit, ArchiveKit, DocumentKit) are opened as they are. The lists
// BASIC reads: src/lib/basic/bi (these) and tools/kits/abi2bas.py (the others), SD0:/lib/<kit>.bi.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _baskits_h
#define _baskits_h

// ---- UIKit: a window, its widgets and menus, what happens to them calling the program's SUBs ----------------------
typedef long (*BasFn) (long, long, long, long);
static bool bk_openUi ();				// (main.cpp: UIKit ready, with the runtime's variables)
static void bk_pad_event (long t);			// (main.cpp: WEV_PAD, a focus lost)
static long bk_win = 0;					// the program's window; none: the BASIC screen is used
static bool bk_closed = false, bk_redraw = false;
static long bk_resizeFn = 0;

struct BkWidget { long h; int w, h2; bool off, hid; };
enum { BK_MAX = 1024 };
static BkWidget bk_w[BK_MAX]; static int bk_n = 0;
static BkWidget *bk_find (long h) { for (int i = 0; i < bk_n; i++) if (bk_w[i].h == h) return &bk_w[i]; return 0; }
static long bk_add (long parent, long h, int w, int hh, long fn)
{
	if (!h) return 0;
	if (bk_n < BK_MAX) { BkWidget &b = bk_w[bk_n++]; b.h = h; b.w = w; b.h2 = hh; b.off = b.hid = false; }
	if (parent != bk_win && bk_find (parent)) ui_w_set_parent (h, parent);	// (made in a panel)
	if (fn) ui_w_on (h, fn, 0);
	bk_redraw = true;
	return h;
}

static long uk_window (const char *title, long w, long h, long flags)
{
	if (!bk_openUi ()) return 0;
	bas_utf8 t (title);
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	long win = flags & 1 ? ui_window_ex (0, scr[3], w, h, (long) t.s, WF_RESIZABLE) : ui_window (w, h, (long) t.s);
	if (win <= 0) return 0;
	ui_fill (0, 0, ui.w, ui.h, ((const unsigned *) ui_jetons ())[TJ_FACE / 4]);	// (the theme's face under the widgets)
	if (!bk_win) bk_win = win;
	bk_closed = false; bk_redraw = true;
	return win;
}
static void uk_window_close (long win) { ui_window_close (win); if (win == bk_win) bk_closed = true; }
static void uk_window_min_size (long, long w, long h) { ui_min_size (w, h); }
static long uk_window_width (long) { return ui.w; }
static long uk_window_height (long) { return ui.h; }
static long bk_resized (long w, long h)
{
	if (bk_resizeFn) ((BasFn) bk_resizeFn) (bk_win, w, h, 0);
	bk_redraw = true;
	return 0;
}
static void uk_window_on_resize (long, long fn) { bk_resizeFn = fn; ui_on_resize ((long) bk_resized, 0); }

// The menus: a text for ui_menu, written again at each new item
enum { BK_MENUS = 48 };
struct BkMenu { char title[32], item[64], key[16]; long fn; };
static BkMenu bk_menu[BK_MENUS]; static int bk_nmenu = 0;
static void uk_menu_item (long, const char *title, const char *item, const char *key, long fn)
{
	if (bk_nmenu >= BK_MENUS) return;
	BkMenu &m = bk_menu[bk_nmenu++];
	bas_cpy (m.title, title, sizeof m.title); bas_cpy (m.item, item, sizeof m.item); bas_cpy (m.key, key, sizeof m.key); m.fn = fn;
	static char text[BK_MENUS * 128]; int n = 0;
	bool done[BK_MENUS] = { false };
	for (int i = 0; i < bk_nmenu; i++)
	{
		if (done[i]) continue;
		bas_utf8 h (bk_menu[i].title);
		n += snprintf (text + n, sizeof text - n, "M%s\n", h.s);
		for (int j = i; j < bk_nmenu; j++)
		{
			if (done[j] || strcasecmp (bk_menu[j].title, bk_menu[i].title)) continue;
			done[j] = true;
			if (bk_menu[j].item[0] == '-' && !bk_menu[j].item[1]) { n += snprintf (text + n, sizeof text - n, "-\n"); continue; }
			bas_utf8 s (bk_menu[j].item);
			n += snprintf (text + n, sizeof text - n, "I%d\t%s%s%s\n", j + 1, s.s, bk_menu[j].key[0] ? "\t" : "", bk_menu[j].key);
		}
	}
	ui_menu ((long) text);
}
static void bk_choose (long id) { if (id >= 1 && id <= bk_nmenu && bk_menu[id - 1].fn) ((BasFn) bk_menu[id - 1].fn) (0, 0, 0, 0); }

// One round of events, waiting up to ms -> 1, 0: the window is closed (the program's SUBs run from here)
static long bk_round (long ms)
{
	if (bk_closed || !bk_win) return 0;
	if (bk_redraw) { bk_redraw = false; ui_w_draw (0); ui_update_all (); }
	long t = ui_event (ms);
	if (t == WEV_CLOSE) { ui_window_close (0); bk_closed = true; return 0; }
	bk_pad_event (t);
	if (t == WEV_MENU) bk_choose (ui_ev_field (&ui, EV_CODE));
	else if (t > 0)
	{
		bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
		long code = 0, taken = 0;
		uikit_w_event (&code, &taken);
		if (press && !taken) { long id = ui_menu_key (0); if (id > 0) bk_choose (id); }
	}
	else ui_w_idle ();
	if (bk_redraw) { bk_redraw = false; ui_w_draw (0); ui_update_all (); }
	return !bk_closed;
}
static long uk_window_wait (long) { return bk_round (50); }
static long uk_window_step (long) { return bk_round (0); }
static void uk_window_run (long) { while (bk_round (100)) {} }

static long uk_label (long p, long x, long y, long w, long h, const char *s) { bas_utf8 t (s); return bk_add (p, ui_label (x, y, w, h, (long) t.s, 0), (int) w, (int) h, 0); }
static long uk_button (long p, long x, long y, long w, long h, const char *s, long fn) { bas_utf8 t (s); return bk_add (p, ui_pushbutton (x, y, w, h, (long) t.s, 0), (int) w, (int) h, fn); }
static long uk_checkbox (long p, long x, long y, long w, long h, const char *s, long on, long fn) { bas_utf8 t (s); return bk_add (p, ui_checkbox (x, y, w, h, (long) t.s, on != 0), (int) w, (int) h, fn); }
static long uk_textbox (long p, long x, long y, long w, long h, const char *s, long fn)
{
	bas_utf8 t (s);
	long b = ui_textbox (x, y, w, h, 0);
	if (b) ui_w_set_text (b, (long) t.s);
	return bk_add (p, b, (int) w, (int) h, fn);
}
static long uk_dropdown (long p, long x, long y, long w, long h, const char *items, long fn) { bas_utf8 t (items); return bk_add (p, ui_dropdown (x, y, w, h, (long) t.s), (int) w, (int) h, fn); }
static long uk_listbox (long p, long x, long y, long w, long h, const char *items, long fn)
{
	long l = ui_list (x, y, w, h, 0);
	if (l && items[0])
	{
		bas_utf8 t (items);
		for (char *q = t.s; ; )
		{
			char *e = strchr (q, '|');
			if (e) *e = 0;
			ui_list_add (l, (long) q, 0);
			if (!e) break;
			q = e + 1;
		}
	}
	return bk_add (p, l, (int) w, (int) h, fn);
}
static long uk_progress (long p, long x, long y, long w, long h, long max, long v) { return bk_add (p, ui_progress (x, y, w, h, max, v), (int) w, (int) h, 0); }
static long uk_slider (long p, long x, long y, long w, long h, long max, long v, long fn) { return bk_add (p, ui_slider (x, y, w, h, 0, max, v), (int) w, (int) h, fn); }
static long uk_panel (long p, long x, long y, long w, long h) { return bk_add (p, ui_panel (x, y, w, h, 0), (int) w, (int) h, 0); }
static void uk_set_parent (long w, long p) { ui_w_set_parent (w, p == bk_win ? 0 : p); bk_redraw = true; }
static void uk_add_item (long w, const char *s) { bas_utf8 t (s); ui_list_add (w, (long) t.s, 0); bk_redraw = true; }
static void uk_clear_items (long w) { ui_list_clear (w); bk_redraw = true; }
static long uk_item_count (long w) { return ui_list_count (w); }
static void uk_enable (long w, long on) { ui_w_enable (w, on != 0); BkWidget *b = bk_find (w); if (b) b->off = !on; bk_redraw = true; }
static long uk_enabled (long w) { BkWidget *b = bk_find (w); return b && !b->off; }
static void uk_show (long w, long on) { ui_w_show (w, on != 0); BkWidget *b = bk_find (w); if (b) b->hid = !on; bk_redraw = true; }
static long uk_shown (long w) { BkWidget *b = bk_find (w); return b && !b->hid; }
static void uk_focus (long w) { ui_w_focus (w); }
static void uk_move (long w, long x, long y, long ww, long h)
{
	ui_w_move (w, x, y, ww, h);
	BkWidget *b = bk_find (w); if (b) { b->w = (int) ww; b->h2 = (int) h; }
	bk_redraw = true;
}
static long uk_width (long w) { BkWidget *b = bk_find (w); return b ? b->w : 0; }
static long uk_height (long w) { BkWidget *b = bk_find (w); return b ? b->h2 : 0; }
static const char *uk_get_text (long w) { static char b[4096]; bas_latin1 (b, sizeof b, (const char *) ui_w_get_text (w)); return b; }
static void uk_set_text (long w, const char *s) { bas_utf8 t (s); ui_w_set_text (w, (long) t.s); bk_redraw = true; }
static long uk_get_value (long w) { return ui_w_get_value (w); }
static void uk_set_value (long w, long v) { ui_w_set_value (w, v); bk_redraw = true; }
static void uk_set_range (long w, long lo, long hi) { ui_w_set_range (w, lo, hi, 0, 0); bk_redraw = true; }
static long uk_message (const char *t, const char *m, long b) { if (!bk_openUi ()) return 0; bas_utf8 a (t), c (m); return ui_message ((long) a.s, (long) c.s, b); }
static const char *uk_ask_open (const char *dir)
{
	static char o[256]; char d[256]; bas_path (d, sizeof d, dir);
	if (!bk_openUi () || !ui_file_open ((long) d, (long) o, sizeof o)) o[0] = 0;
	return o;
}
static const char *uk_ask_save (const char *dir, const char *name)
{
	static char o[256]; char d[256]; bas_path (d, sizeof d, dir);
	if (!bk_openUi () || !ui_file_save ((long) d, (long) name, (long) o, sizeof o)) o[0] = 0;
	return o;
}
static const char *uk_tr (const char *s) { if (!bk_openUi ()) return s; return (const char *) ui_tr ((long) s); }
static const char *uk_lang () { if (!bk_openUi ()) return "en"; return (const char *) ui_lang (); }

// (the places of src/lib/basic/bi/uikit.head)
static void *const bk_uikit[] = {
	(void *) uk_window, (void *) uk_window_close, (void *) uk_window_min_size, (void *) uk_window_width, (void *) uk_window_height,
	(void *) uk_window_on_resize, (void *) uk_window_wait, (void *) uk_window_step, (void *) uk_window_run, (void *) uk_menu_item,
	(void *) uk_label, (void *) uk_button, (void *) uk_checkbox, (void *) uk_textbox, (void *) uk_dropdown,
	(void *) uk_listbox, (void *) uk_progress, (void *) uk_slider, (void *) uk_panel, (void *) uk_set_parent,
	(void *) uk_add_item, (void *) uk_clear_items, (void *) uk_item_count, (void *) uk_enable, (void *) uk_enabled,
	(void *) uk_show, (void *) uk_shown, (void *) uk_focus, (void *) uk_move, (void *) uk_width,
	(void *) uk_height, (void *) uk_get_text, (void *) uk_set_text, (void *) uk_get_value, (void *) uk_set_value,
	(void *) uk_set_range, (void *) uk_message, (void *) uk_ask_open, (void *) uk_ask_save, (void *) uk_tr,
	(void *) uk_lang };

// ---- AppKit, FileKit, ImageKit, SystemKit: what the programs of Onyx ask of them ----------------------------------
static unsigned ak_abi_version () { return 1; }
static int ak_kernel_info (char *b, int cap) { return snprintf (b, cap, "Korin OS, an assembly OS for the Raspberry Pi 4 (AArch64)\n"); }
static void *const bk_appkit[] = { (void *) ak_abi_version, (void *) ak_kernel_info };

static long fk_file_size (const char *p) { char q[256]; bas_path (q, sizeof q, p); struct stat s; return stat (q, &s) ? -1 : (long) s.st_size; }
static void fk_free (void *p) { free (p); }
static void fk_human_size (int n, char *o, int cap)
{
	if (n < 1024) snprintf (o, cap, "%d bytes", n);
	else if (n < 1024 * 1024) snprintf (o, cap, "%.1f KB", n / 1024.0);
	else snprintf (o, cap, "%.1f MB", n / 1048576.0);
}
static int fk_load (const char *p, char **out, int *n)
{
	char q[256]; bas_path (q, sizeof q, p);
	int len; char *b = bas_load (q, &len);
	if (!b) return -1;
	char *m = (char *) malloc (len + 1);
	memcpy (m, b, len + 1);
	delete [] b;
	*out = m; *n = len;
	return 0;
}
static int fk_remove (const char *p) { char q[256]; bas_path (q, sizeof q, p); return unlink (q) == 0 || rmdir (q) == 0 ? 0 : -1; }
static long fk_tree (const char *dir, int *files, int *folders)
{
	DIR *d = opendir (dir);
	if (!d) return 0;
	long total = 0; struct dirent *e;
	while ((e = readdir (d)))
	{
		if (!strcmp (e->d_name, ".") || !strcmp (e->d_name, "..")) continue;
		char f[320]; snprintf (f, sizeof f, "%s/%s", dir, e->d_name);
		struct stat s;
		if (stat (f, &s)) continue;
		if ((s.st_mode & S_IFMT) == S_IFDIR) { ++*folders; total += fk_tree (f, files, folders); }
		else { ++*files; total += s.st_size; }
	}
	closedir (d);
	return total;
}
static long fk_tree_size (const char *p, int *files, int *folders) { char q[256]; bas_path (q, sizeof q, p); *files = *folders = 0; return fk_tree (q, files, folders); }
// A copy that reports: cb (user, done, total, file name) -> 0 goes on.
static int fk_copy (const char *a, const char *b, long cb, long user)
{
	char p[256], q[256]; bas_path (p, sizeof p, a); bas_path (q, sizeof q, b);
	int in = open (p, O_RDONLY);
	if (in < 0) return -1;
	int out = open (q, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (out < 0) { close (in); return -1; }
	struct stat st; fstat (in, &st);
	static char buf[65536]; long done = 0; int rc = 0;
	for (;;)
	{
		long n = read (in, buf, sizeof buf);
		if (n <= 0 || write (out, buf, n) != n) { rc = n < 0 ? -1 : n ? -1 : 0; break; }
		done += n;
		if (cb && ((BasFn) cb) (user, done, st.st_size, (long) p)) { rc = -1; break; }
	}
	close (in); close (out);
	return rc;
}
static void *const bk_filekit[] = { (void *) fk_file_size, (void *) fk_free, (void *) fk_human_size, (void *) fk_load, (void *) fk_remove,
	(void *) fk_tree_size, (void *) fk_copy };

struct IkFormat { char name[12], ext[40]; int canRead, canWrite, alpha, animated; char pad[16]; };
static_assert (sizeof (IkFormat) == 84, "imagekit.bi");
static int ik_formats (IkFormat *o, int max)		// (what UIKit reads: ui_image_load)
{
	static const IkFormat f[] = { { "PNG", ".png", 1, 0, 1, 0, "" }, { "BMP", ".bmp", 1, 0, 0, 0, "" } };
	int n = max < 2 ? max : 2;
	for (int i = 0; i < n; i++) o[i] = f[i];
	return 2;
}
static void *const bk_imagekit[] = { (void *) ik_formats };

static void sk_notify (const char *t, const char *m);	// (main.cpp: as the statement NOTIFY)
static void *const bk_systemkit[] = { (void *) sk_notify };

// The table of a kit for BASIC -> its entries, 0: not one of these.
static void *const *bk_table (const char *name, void *const *&own, int &n)
{
	own = 0; n = 0;
#define BK(t, nm) if (!strcmp (name, nm)) { n = (int) (sizeof t / sizeof t[0]); own = t; }
	BK (bk_uikit, "uikit") BK (bk_appkit, "appkit") BK (bk_filekit, "filekit") BK (bk_imagekit, "imagekit") BK (bk_systemkit, "systemkit")
#undef BK
	return own;
}
static void *const *bk_open (const char *name, int minVersion, char *why, int cap)
{
	void *const *own; int n;
	if (!bk_table (name, own, n)) return bas_kit_open (name, minVersion, why, cap);
	if (strcmp (name, "uikit") && strcmp (name, "appkit")) return own;
	if (!strcmp (name, "uikit") && !bk_openUi ()) { bas_cpy (why, "no UIKit", cap); return 0; }
	void *k = armpi_kit_open (name, 1);
	unsigned nk = k ? armpi_kit_version (k) : 0;
	void **t = new void *[n + nk];				// (ours, then the kit's own)
	for (int i = 0; i < n; i++) t[i] = own[i];
	for (unsigned i = 0; i < nk; i++) t[n + i] = armpi_kit_fn (k, i);
	return t;
}

#endif
