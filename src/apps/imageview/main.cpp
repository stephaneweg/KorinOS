//
// imageview -- the Onyx image viewer (user/Apps/imageview) on Korin OS's UIKit, without a bridge: BMP, GIF (animated), PNG,
// JPEG, PCX and WebP, read by Paint's decoders (stb_image, simplewebp, our PCX: ../paint/image.cpp, as Photos does); a
// JPEG is turned the way the camera says (EXIF's orientation: ../photos/exif.h, ../photos/imgops.h).
//
//   * Opens the file given as argument (Files, a double click: it is the system's image viewer), File > Open... (Ctrl+O),
//     or a file dropped on the window.
//   * Fit to the window by default (never enlarged); 1 = actual size, + / - (or the wheel, at the pointer) zoom, 0 = fit
//     again, a double click switches between the two. When the image is larger than the window, drag it to pan.
//   * Left / Right (or Page Up / Page Down, Backspace / Space): previous / next image of the same folder; Home / End:
//     first / last.
//   * Transparent pixels are shown over a checkerboard; animated GIFs play. Edit > Copy puts the picture on the clipboard.
//
// Onyx's "imageview --background" is not there: Korin OS's desktop background is not a picture.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <spawn.h>
#include <time.h>
#include <initializer_list>
#include <uikit.h>
#include "../paint/imgload.h"
#include "../photos/exif.h"
#include "../photos/imgops.h"

extern char **environ;

static struct ui_vars ui;
static const unsigned *s_jet;
static inline unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }
static inline const char *TR (const char *s) { return (const char *) ui_tr ((long) s); }

#define PAINT_PROGRAM	"SD0:/apps/paint.app/main"
#define FILES_PROGRAM	"SD0:/apps/files.app/main"
#define PICTURES	"SD0:/home/pictures"
#define FILTER		"*.png;*.jpg;*.jpeg;*.jpe;*.gif;*.bmp;*.webp;*.pcx"
#define MAXF		1024		// images listed in the folder
#define ST_H		26		// the status strip, at the bottom of the view

static unsigned ms_now ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static const char *base_name (const char *p) { const char *s = strrchr (p, '/'); if (!s) s = strrchr (p, ':'); return s ? s + 1 : p; }
// the folder of a path, with its last '/': "SD0:/home/pictures/"
static void dir_of (const char *p, char *out, int cap)
{
	scpy (out, p, cap);
	char *s = strrchr (out, '/'); if (!s) s = strrchr (out, ':');
	if (s) s[1] = 0; else out[0] = 0;
}

// ---- the picture shown: its frames (an animated GIF), turned the way the camera says ---------------------------------
static struct { unsigned *px; int w, h, n; int *delay; bool alpha; const char *format; } g_im;
static int  g_frame;
static unsigned g_frameT;				// ms: when the current frame was shown
static char g_path[400];
static char g_err[480];
static bool g_fit = true;
static double g_zoom = 1;				// screen pixels per image pixel
static int  g_ox, g_oy;					// where the image's corner is in the view
static char g_files[MAXF][96]; static int g_nfiles, g_cur = -1;

static long g_tb, g_view, g_bOpen, g_bPrev, g_bNext, g_bOut, g_bIn, g_bFit, g_bActual;
static int  TB_H = 48;
static bool g_quit;

static void close_image ()
{
	delete[] g_im.px; delete[] g_im.delay;
	g_im.px = 0; g_im.delay = 0; g_im.n = g_im.w = g_im.h = 0; g_im.alpha = false; g_im.format = "";
}
// the format, by the file's first bytes
static const char *format_of (const char *path)
{
	unsigned char b[16] = { 0 };
	FILE *f = fopen (path, "rb");
	if (!f) return "";
	size_t n = fread (b, 1, sizeof b, f); fclose (f);
	if (n >= 2 && b[0] == 'B' && b[1] == 'M') return "BMP";
	if (n >= 4 && !memcmp (b, "GIF8", 4)) return "GIF";
	if (n >= 4 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') return "PNG";
	if (n >= 2 && b[0] == 0xFF && b[1] == 0xD8) return "JPEG";
	if (n >= 12 && !memcmp (b, "RIFF", 4) && !memcmp (b + 8, "WEBP", 4)) return "WebP";
	if (n >= 3 && b[0] == 0x0A && b[2] == 1) return "PCX";
	return "";
}
static bool load_image (const char *path)
{
	close_image ();
	int w = 0, h = 0, n = 0; int *dl = 0;
	unsigned *px = img_load_frames (path, &w, &h, &n, &dl);
	if (!px || w <= 0 || h <= 0 || n <= 0) { delete[] px; delete[] dl; return false; }
	g_im.px = px; g_im.w = w; g_im.h = h; g_im.n = n; g_im.delay = dl;
	g_im.format = format_of (path);
	photos::PicInfo inf;
	if (n == 1 && photos::pic_info (path, inf) && inf.orient > 1)		// a photo taken with the camera turned
	{
		photos::Pix p; p.px = g_im.px; p.w = w; p.h = h;
		photos::orient (p, inf.orient);
		g_im.px = p.px; g_im.w = p.w; g_im.h = p.h; p.px = 0;
	}
	for (long i = 0, k = (long) g_im.w * g_im.h * g_im.n; i < k && !g_im.alpha; i++) if ((g_im.px[i] >> 24) != 255) g_im.alpha = true;
	return true;
}
static const unsigned *frame_px () { return g_im.px + (long) g_frame * g_im.w * g_im.h; }

// ---- the view: the picture fitted or zoomed, the status strip underneath ----------------------------------------------
static int g_vw = 100, g_vh = 100;			// the view's size (the picture's area: g_vh - ST_H)
static int area_h () { return g_vh - ST_H > 1 ? g_vh - ST_H : 1; }
static double fit_zoom ()
{
	if (!g_im.n) return 1;
	double a = (double) g_vw / g_im.w, b = (double) area_h () / g_im.h, s = a < b ? a : b;
	return s > 1 ? 1 : s;
}
static double zoom () { return g_fit ? fit_zoom () : g_zoom; }
static int disp_w () { int d = (int) (g_im.w * zoom () + 0.5); return d < 1 ? 1 : d; }
static int disp_h () { int d = (int) (g_im.h * zoom () + 0.5); return d < 1 ? 1 : d; }
static void clamp_pan ()
{
	int dw = disp_w (), dh = disp_h (), ah = area_h ();
	if (dw <= g_vw) g_ox = (g_vw - dw) / 2; else { if (g_ox > 0) g_ox = 0; if (g_ox < g_vw - dw) g_ox = g_vw - dw; }
	if (dh <= ah) g_oy = (ah - dh) / 2; else { if (g_oy > 0) g_oy = 0; if (g_oy < ah - dh) g_oy = ah - dh; }
}
static bool larger () { return g_im.n && (disp_w () > g_vw || disp_h () > area_h ()); }

// the picture made smaller (each pixel the average of those it covers, at most ~4 x 4 looked at, by their alpha):
// kept while the frame and the size stay
static unsigned *g_small; static int g_sw, g_sh, g_sFrame = -1; static const unsigned *g_sSrc;
static void shrink (const unsigned *src, int sw, int sh, unsigned *dst, int dw, int dh)
{
	for (int y = 0; y < dh; y++)
	{
		int y0 = (int) ((long long) y * sh / dh), y1 = (int) ((long long) (y + 1) * sh / dh); if (y1 <= y0) y1 = y0 + 1;
		int ys = 1 + (y1 - y0) / 4;
		unsigned *o = dst + (long) y * dw;
		for (int x = 0; x < dw; x++)
		{
			int x0 = (int) ((long long) x * sw / dw), x1 = (int) ((long long) (x + 1) * sw / dw); if (x1 <= x0) x1 = x0 + 1;
			int xs = 1 + (x1 - x0) / 4;
			unsigned r = 0, g = 0, b = 0, a = 0, c = 0;
			for (int yy = y0; yy < y1; yy += ys)
			{
				const unsigned *row = src + (long) yy * sw;
				for (int xx = x0; xx < x1; xx += xs)
				{
					unsigned p = row[xx], pa = p >> 24;
					r += (p >> 16 & 255) * pa; g += (p >> 8 & 255) * pa; b += (p & 255) * pa; a += pa; c++;
				}
			}
			o[x] = a ? (a / c) << 24 | (r / a) << 16 | (g / a) << 8 | (b / a) : 0;
		}
	}
}
static const unsigned *small_frame (int dw, int dh)
{
	const unsigned *src = frame_px ();
	if (g_small && g_sw == dw && g_sh == dh && g_sFrame == g_frame && g_sSrc == src) return g_small;
	delete[] g_small;
	g_small = new unsigned[(long) dw * dh];
	g_sw = dw; g_sh = dh; g_sFrame = g_frame; g_sSrc = src;
	shrink (src, g_im.w, g_im.h, g_small, dw, dh);
	return g_small;
}
// a pixel over the checkerboard (the transparent parts), at (x, y) of the view
static inline unsigned over_check (unsigned c, int x, int y)
{
	unsigned a = c >> 24;
	if (a == 255) return c & 0xFFFFFF;
	unsigned bg = (((x >> 3) + (y >> 3)) & 1) ? 0x00C8C8C8 : 0x00989898;
	unsigned r = (((c >> 16) & 255) * a + ((bg >> 16) & 255) * (255 - a)) / 255;
	unsigned g = (((c >> 8) & 255) * a + ((bg >> 8) & 255) * (255 - a)) / 255;
	unsigned b = ((c & 255) * a + (bg & 255) * (255 - a)) / 255;
	return r << 16 | g << 8 | b;
}
static void status_text (char *st, int cap)
{
	int p = 0;
	auto put = [&] (const char *fmt, auto... a) { if (p < cap) p += snprintf (st + p, (size_t) (cap - p), fmt, a...); };
	if (g_path[0]) put ("%s", base_name (g_path));
	if (g_im.n)
	{
		put ("   %d \xC3\x97 %d", g_im.w, g_im.h);
		if (g_im.format[0]) put ("   %s", g_im.format);
		if (g_im.n > 1) { put ("   "); put (TR ("%d frames"), g_im.n); }
		put ("   %d%%", (int) (zoom () * 100 + 0.5));
		if (g_fit) { put (" "); put ("%s", TR ("(fit)")); }
	}
	if (g_nfiles > 1 && g_cur >= 0) put ("   %d / %d", g_cur + 1, g_nfiles);
}
static long view_paint (long me, long w, long h)
{
	g_vw = (int) w; g_vh = (int) h;
	long pw = 0, ph = 0;
	uint32_t *px = uikit_canvas_pixels (me, &pw, &ph);
	int ah = area_h ();
	unsigned back = jet (TJ_CREUX);
	long font = (long) ui.font;
	if (!g_im.n)
	{
		ui_fill (0, 0, w, ah, back);
		const char *t = g_err[0] ? g_err : TR ("Open an image: File > Open... (Ctrl+O), or drop one here.");
		long tw = ui_text_width ((long) t, 14, font);
		ui_text ((long) t, tw < w - 32 ? (w - tw) / 2 : 16, ah / 2, jet (TJ_TEXTE2), 14, font, w - 32);
	}
	else if (px)
	{
		clamp_pan ();
		double z = zoom ();
		int dw = disp_w (), dh = disp_h ();
		const unsigned *src = z < 1 ? small_frame (dw, dh) : frame_px ();
		int sw = z < 1 ? dw : g_im.w, shh = z < 1 ? dh : g_im.h;
		long long step = z < 1 ? 65536 : (long long) (65536.0 / z);	// 16.16 source step per view pixel
		int rows = ah < (int) ph ? ah : (int) ph;
		for (int y = 0; y < rows; y++)
		{
			uint32_t *row = px + (long) y * pw;
			int iy = y - g_oy;
			int sy = iy < 0 ? -1 : (int) (((long long) iy * step) >> 16);
			if (sy < 0 || sy >= shh) { for (int x = 0; x < pw; x++) row[x] = back; continue; }
			const unsigned *srow = src + (long) sy * sw;
			for (int x = 0; x < pw; x++)
			{
				int ix = x - g_ox;
				int sx = ix < 0 ? -1 : (int) (((long long) ix * step) >> 16);
				if (sx < 0 || sx >= sw) { row[x] = back; continue; }
				row[x] = over_check (srow[sx], x, y);
			}
		}
	}
	// the status strip: name, size, format, zoom, place in the folder (the theme's face under a line)
	ui_fill (0, ah, w, h - ah, jet (TJ_FACE));
	ui_fill (0, ah, w, 1, jet (TJ_LIGNE));
	char st[600]; status_text (st, sizeof st);
	ui_text_fit ((long) st, 10, ah + (ST_H + 9) / 2 + 1, jet (TJ_TEXTE2), 13, font, w - 20);
	return 0;
}

// ---- the zoom ----------------------------------------------------------------------------------------------------------
static void refresh ()
{
	ui_view_dirty (g_view);
	bool has = g_im.n > 0;
	ui_w_set_value (g_bFit, g_fit && has ? 1 : 0);
	for (long b : { g_bOut, g_bIn, g_bFit, g_bActual }) ui_w_enable (b, has);
	ui_w_enable (g_bPrev, g_nfiles > 1); ui_w_enable (g_bNext, g_nfiles > 1);
	ui_w_cursor (g_view, larger () ? UC_MOVE : -1);
}
// zoom z (screen pixels per image pixel), the image point at (ax, ay) of the view staying there
static void set_zoom (double z, int ax, int ay)
{
	if (!g_im.n) return;
	double old = zoom ();
	if (z < 0.01) z = 0.01;
	if (z > 16) z = 16;
	clamp_pan ();
	double ix = (ax - g_ox) / old, iy = (ay - g_oy) / old;
	g_fit = false; g_zoom = z;
	g_ox = (int) (ax - ix * z); g_oy = (int) (ay - iy * z);
	clamp_pan ();
	refresh ();
}
static void zoom_by (double f, int ax = -1, int ay = -1)
{
	if (ax < 0) { ax = g_vw / 2; ay = area_h () / 2; }
	double z = zoom () * f;
	if (fabs (z - 1) < 0.04) z = 1;			// (a step through 100 % lands on it)
	set_zoom (z, ax, ay);
}
static void on_fit () { g_fit = true; clamp_pan (); refresh (); }
static void on_actual () { set_zoom (1, g_vw / 2, area_h () / 2); }

// ---- the folder's images (next / previous) ------------------------------------------------------------------------------
static int ci_cmp (const void *a, const void *b) { return strcasecmp ((const char *) a, (const char *) b); }
static void list_folder ()
{
	g_nfiles = 0; g_cur = -1;
	char dir[400]; dir_of (g_path, dir, sizeof dir);
	DIR *d = opendir (dir[0] ? dir : ".");
	if (!d) return;
	struct dirent *e;
	while (g_nfiles < MAXF && (e = readdir (d)) != 0)
		if (e->d_name[0] != '.' && img_is_image_name (e->d_name) && strlen (e->d_name) < sizeof g_files[0])
			scpy (g_files[g_nfiles++], e->d_name, sizeof g_files[0]);
	closedir (d);
	qsort (g_files, (size_t) g_nfiles, sizeof g_files[0], ci_cmp);
	const char *b = base_name (g_path);
	for (int i = 0; i < g_nfiles; i++) if (!strcasecmp (g_files[i], b)) g_cur = i;
}
static void open_image (const char *path, bool relist = true)
{
	scpy (g_path, path, sizeof g_path);
	g_frame = 0; g_frameT = ms_now ();
	g_fit = true; g_zoom = 1; g_err[0] = 0;
	delete[] g_small; g_small = 0;
	if (!load_image (path)) snprintf (g_err, sizeof g_err, TR ("Cannot read this image: %s"), path);
	if (relist) list_folder ();
	char t[160]; snprintf (t, sizeof t, "%s", base_name (path));
	ui_title ((long) (t[0] ? t : TR ("Image Viewer")));
	clamp_pan ();
	refresh ();
}
static void step (int d)
{
	if (g_nfiles == 0) return;
	int i = g_cur < 0 ? 0 : g_cur + d;
	if (i < 0) i = g_nfiles - 1;
	if (i >= g_nfiles) i = 0;
	char dir[400], p[520];
	dir_of (g_path, dir, sizeof dir);
	snprintf (p, sizeof p, "%s%s", dir, g_files[i]);
	open_image (p, false);
	g_cur = i;
	refresh ();
}
static void go_to (int i) { if (g_nfiles) { g_cur = i; step (0); } }

// ---- the commands ------------------------------------------------------------------------------------------------------
enum { C_OPEN = 1, C_OPENWITH, C_NEXT, C_PREV, C_FIRST, C_LAST, C_PAINT, C_FILES, C_COPY, C_FIT, C_ACTUAL, C_ZIN, C_ZOUT };
static void menus ()
{
	ui_menu ((long)
		"MFile\n" "I1\tOpen...\tCtrl+O\n" "I2\tOpen With...\n" "-\n" "I3\tNext Image\tRight\n" "I4\tPrevious Image\tLeft\n"
		"I5\tFirst Image\tHome\n" "I6\tLast Image\tEnd\n" "-\n" "I7\tEdit in Paint\n" "I8\tShow in Files\n"
		"MEdit\n" "I9\tCopy\tCtrl+C\n"
		"MView\n" "I10\tFit to Window\t0\n" "I11\tActual Size\t1\n" "I12\tZoom In\t+\n" "I13\tZoom Out\t-\n");
}
static bool spawn_app (const char *prog, const char *arg)
{
	char *argv[] = { (char *) prog, (char *) arg, 0 };
	pid_t p;
	return posix_spawn (&p, prog, 0, 0, argv, environ) == 0;
}
static void on_open ()
{
	static char p[400];
	char dir[400];
	if (g_path[0]) { dir_of (g_path, dir, sizeof dir); size_t n = strlen (dir); if (n > 1 && dir[n - 1] == '/' && dir[n - 2] != ':') dir[n - 1] = 0; }
	else scpy (dir, PICTURES, sizeof dir);
	ui_file_filter ((long) FILTER);
	long r = ui_file_open ((long) dir, (long) p, sizeof p);
	ui_file_filter (0);
	ui_w_draw (0);
	if (r == 1) open_image (p);
}
static void on_copy ()
{
	if (!g_im.n) return;
	long n = (long) g_im.w * g_im.h;
	unsigned *c = new unsigned[n];
	const unsigned *s = frame_px ();
	for (long i = 0; i < n; i++)			// (laid on white: the clipboard's images have no alpha)
	{
		unsigned v = s[i], a = v >> 24;
		if (a == 255) { c[i] = v & 0xFFFFFF; continue; }
		unsigned r = (((v >> 16) & 255) * a + 255 * (255 - a)) / 255, g = (((v >> 8) & 255) * a + 255 * (255 - a)) / 255, b = ((v & 255) * a + 255 * (255 - a)) / 255;
		c[i] = r << 16 | g << 8 | b;
	}
	if (!ui_clip_set_image ((long) c, g_im.w, g_im.h))
		ui_message ((long) TR ("Image Viewer"), (long) TR ("The picture could not be copied: the clipboard service (clipd) is not there."), UM_OK);
	delete[] c;
}
static void command (int c)
{
	switch (c)
	{
	case C_OPEN: on_open (); break;
	case C_OPENWITH: if (g_path[0]) ui_open_with ((long) g_path); break;
	case C_NEXT: step (1); break;
	case C_PREV: step (-1); break;
	case C_FIRST: go_to (0); break;
	case C_LAST: go_to (g_nfiles - 1); break;
	case C_PAINT: if (g_path[0]) spawn_app (PAINT_PROGRAM, g_path); break;
	case C_FILES: if (g_path[0]) { char d[400]; dir_of (g_path, d, sizeof d); spawn_app (FILES_PROGRAM, d); } break;
	case C_COPY: on_copy (); break;
	case C_FIT: on_fit (); break;
	case C_ACTUAL: on_actual (); break;
	case C_ZIN: zoom_by (1.25); break;
	case C_ZOUT: zoom_by (0.8); break;
	}
}
// a key the widgets left -> true if it was one of ours
static bool key (int hid, int ch, int mods)
{
	if (mods & (UI_MOD_CTRL | UI_MOD_ALT)) return false;
	switch (hid)
	{
	case KEY_RIGHT: case KEY_PGDN: step (1); return true;
	case KEY_LEFT: case KEY_PGUP: case KEY_BACKSPACE: step (-1); return true;
	case KEY_HOME: go_to (0); return true;
	case KEY_END: go_to (g_nfiles - 1); return true;
	}
	switch (ch)
	{
	case ' ': step (1); return true;
	case '+': case '=': zoom_by (1.25); return true;
	case '-': zoom_by (0.8); return true;
	case '0': on_fit (); return true;
	case '1': on_actual (); return true;
	}
	return false;
}

// ---- the mouse: drag to pan, the wheel zooms at the pointer, a double click: fit / actual size ------------------------
static bool g_drag; static int g_dx, g_dy, g_mx, g_my;
static long view_mouse (long, long kind, long x, long y, long info)
{
	if (kind == 3) { g_drag = false; return 0; }
	g_mx = (int) x; g_my = (int) y;
	if (kind == 1 && (info >> 8 & 1) && y < area_h () && g_im.n)
	{
		if ((info & 255) >= 2) { if (g_fit && fit_zoom () < 1) set_zoom (1, (int) x, (int) y); else on_fit (); return 0; }
		g_drag = true; g_dx = (int) x; g_dy = (int) y;
		return 0;
	}
	if (kind == 2) { g_drag = false; return 0; }
	if (kind == 0 && g_drag)
	{
		if (g_fit) { g_zoom = fit_zoom (); g_fit = false; }
		g_ox += (int) x - g_dx; g_oy += (int) y - g_dy; g_dx = (int) x; g_dy = (int) y;
		clamp_pan ();
		refresh ();
	}
	return 0;
}
static long view_wheel (long, long n)
{
	if (!g_im.n || n == 0) return 0;
	zoom_by (n > 0 ? 1.25 : 0.8, g_mx, g_my < area_h () ? g_my : area_h () / 2);
	return 0;
}

// ---- the window ----------------------------------------------------------------------------------------------------------
static int ctl_h () { return (int) ui_metric (UMT_CTL); }	// hauteur d'un controle : celle de UIKit (echelle, densite tactile)
static void lay_out (long w, long h)
{
	ui_w_move (g_tb, 0, 0, w, TB_H);
	ui_w_move (g_view, 0, TB_H, w, h - TB_H);
	ui_view_dirty (g_view);
}
static void paint_all ()
{
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	lay_out (ui.w, ui.h);
	ui_w_draw (0);
	ui_update_all ();
}
static void on_resize (long, long) { paint_all (); }
static void on_drop ()
{
	static char b[1024];
	long kind = 0, n = uikit_drop_get (b, sizeof b - 1, &kind);
	if (n <= 0 || kind != UDT_FILES) return;
	b[n] = 0;
	char *e = b; while (*e && *e != '\n' && *e != '\r') e++;
	*e = 0;
	if (b[0]) open_image (b);
}
static void control (long w, long code)
{
	if (code != UN_ACTIVATE && code != UN_CHANGE) return;
	if (w == g_bOpen) on_open ();
	else if (w == g_bPrev) step (-1);
	else if (w == g_bNext) step (1);
	else if (w == g_bOut) zoom_by (0.8);
	else if (w == g_bIn) zoom_by (1.25);
	else if (w == g_bFit) { if (g_fit) refresh (); else on_fit (); }
	else if (w == g_bActual) on_actual ();
	ui_w_focus (g_view);
}
// the animated GIF: the next frame when its time is up -> ms to wait for the one after
static int animate ()
{
	if (g_im.n < 2) return 500;
	unsigned now = ms_now ();
	int d = g_im.delay[g_frame]; if (d < 20) d = 100;		// (as browsers do with the too fast ones)
	if (now - g_frameT >= (unsigned) d)
	{
		g_frame = (g_frame + 1) % g_im.n;
		g_frameT = now;
		ui_view_dirty (g_view);
		d = g_im.delay[g_frame]; if (d < 20) d = 100;
		return d;
	}
	return d - (int) (now - g_frameT);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/imageview.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	long scr[4] = { 1024, 768, 0, 0 };
	ui_screen ((long) scr);
	int w = (int) scr[0] - 40 < 860 ? (int) scr[0] - 40 : 860, h = (int) (scr[1] - scr[2] - scr[3]) - 40; if (h > 640) h = 640;
	if (ui_window_ex (-1, -1, w, h, (long) TR ("Image Viewer"), WF_RESIZABLE) <= 0) return 1;
	ui_min_size (420, 300);
	ui_on_resize ((long) on_resize, 0);
	TB_H = ctl_h () + 16;

	g_tb = ui_toolbar (0, 0, ui.w, TB_H);
	g_bOpen = ui_toolbar_add (g_tb, (long) "\xE2\x87\xA7", 0, 0);			// (glyph-tb-open)
	g_bPrev = ui_toolbar_add (g_tb, (long) "\xE2\x86\x90", 0, UTB_SEP);			// (glyph-tb-left)
	g_bNext = ui_toolbar_add (g_tb, (long) "\xE2\x86\x92", 0, 0);			// (glyph-tb-right)
	g_bOut = ui_toolbar_add (g_tb, (long) "\xE2\x88\x92", 0, UTB_SEP);			// (glyph-tb-minus)
	g_bIn = ui_toolbar_add (g_tb, (long) "+", 0, 0);
	g_bFit = ui_toolbar_add (g_tb, 0, (long) TR ("Fit"), UTB_TOGGLE | UTB_SEP);
	g_bActual = ui_toolbar_add (g_tb, 0, (long) "100 %", 0);
	ui_w_tip (g_bOpen, (long) TR ("Open... (Ctrl+O)"));
	ui_w_tip (g_bPrev, (long) TR ("Previous image (Left)"));
	ui_w_tip (g_bNext, (long) TR ("Next image (Right)"));
	ui_w_tip (g_bOut, (long) TR ("Zoom out (-)"));
	ui_w_tip (g_bIn, (long) TR ("Zoom in (+)"));
	ui_w_tip (g_bFit, (long) TR ("Fit to the window (0)"));
	ui_w_tip (g_bActual, (long) TR ("Actual size (1)"));
	g_view = ui_view (0, TB_H, ui.w, ui.h - TB_H, UVW_FOCUS);
	ui_view_on (g_view, (long) view_paint, (long) view_mouse, 0, (long) view_wheel);
	menus ();

	// the file given ("a path with spaces" arrives in pieces when nothing quoted it)
	char arg[400] = "";
	for (int i = 1; i < argc; i++) { if (i > 1) strncat (arg, " ", sizeof arg - strlen (arg) - 1); strncat (arg, argv[i], sizeof arg - strlen (arg) - 1); }
	if (arg[0]) open_image (arg);
	else refresh ();
	paint_all ();
	ui_w_focus (g_view);

	while (!g_quit)
	{
		long t = ui_event (animate ());
		if (t == WEV_CLOSE) break;
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_THEME) paint_all ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0, wd = uikit_w_event (&code, &taken);
			if (t == WEV_DROP && code != UN_CHANGE) on_drop ();
			else if (code) control (wd, code);
			if (press && !taken && !key (hid, ch, mods)) { long id = ui_menu_key (0); if (id >= 0) command ((int) id); }
		}
		else ui_w_idle ();
		ui_view_flush ();
	}
	close_image ();
	return 0;
}
