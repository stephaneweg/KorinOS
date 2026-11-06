//
// screenshot -- Onyx's screen capture tool (user/Apps/screenshot, laid out as Windows' Snipping Tool) on Korin OS's UIKit,
// without a bridge. The first bar: New, the mode (a rectangle, a window, the full screen) and the delay; once a capture is
// made, Copy and Save As. The second bar, under it, the drawing tools: a pen, a marker (their colour and size), an eraser
// that takes a stroke away, a crop, Undo / Redo. The capture below, fitted in the window, the state at the bottom.
//
// A capture: the window is minimised (ui_minimize), a see-through window over the whole screen hides the pointer
// (ui_pointer_lock), the screen is taken as it is shown (ui_screen_grab: Elegant's WS_OP_SNAPSHOT, the one of the
// screenshot command), then shown frozen and darkened in that window while a rectangle is dragged or a window clicked;
// the full screen is taken at once. A delay counts down in the window first. The picture is copied to the clipboard at
// once; it is saved only with Save As (PNG, JPEG or BMP: Paint's pngsave.hpp).
//
// Onyx's Print Screen key (the kernel told the app) is not there: Korin OS has no global key for it yet.
// Its settings: SD0:/etc/screenshot.ini (the mode, the delay, the pen's and the marker's colour and size, the last folder).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <initializer_list>
#include <uikit.h>
#include "../paint/pngsave.hpp"

static struct ui_vars ui;
static const unsigned *s_jet;
static inline unsigned jet (int tj) { return s_jet[tj / 4] & 0xFFFFFF; }
static inline const char *TR (const char *s) { return (const char *) ui_tr ((long) s); }
static inline unsigned mix (unsigned a, unsigned b, int t) { return (unsigned) ui_mix (a, b, t) & 0xFFFFFF; }

#define SETTINGS	"SD0:/etc/screenshot.ini"
#define PICTURES	"SD0:/home/pictures"
#define SHOTS_DIR	"SD0:/home/pictures/Screenshots"
#define ST_H		26			// the state, at the bottom of the view

enum { MODE_RECT, MODE_WINDOW, MODE_FULL };
static const char *const MODE_NAME[3] = { "Rectangle", "Window", "Full screen" };
static const int DELAYS[4] = { 0, 3, 5, 10 };
enum { TOOL_PEN, TOOL_MARKER, TOOL_ERASER, TOOL_CROP };
static const int PEN_SIZES[4] = { 2, 4, 7, 12 };
static const int MARK_SIZES[4] = { 10, 16, 24, 34 };
static const int MARK_ALPHA = 118;

static unsigned ms_now ()
{
	struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

// ---- the settings ------------------------------------------------------------------------------------------------------
static int g_mode = MODE_RECT, g_delay = 0;			// (g_delay: an index in DELAYS)
static unsigned g_penColour = 0xDC3C32, g_markColour = 0xFADC28;
static int g_penSize = 1, g_markSize = 1;			// (indexes in PEN_SIZES / MARK_SIZES)
static char g_lastDir[300] = SHOTS_DIR;

static void settings_load ()
{
	FILE *f = fopen (SETTINGS, "r");
	if (!f) return;
	char line[400];
	while (fgets (line, sizeof line, f))
	{
		char k[40], v[300];
		if (sscanf (line, " %39[a-z_] = %299[^\r\n]", k, v) != 2) continue;
		long n = strtol (v, 0, 0);
		if (!strcmp (k, "mode")) g_mode = n >= 0 && n <= 2 ? (int) n : MODE_RECT;
		else if (!strcmp (k, "delay")) { g_delay = 0; for (int i = 0; i < 4; i++) if (DELAYS[i] == n) g_delay = i; }
		else if (!strcmp (k, "pen_colour")) g_penColour = (unsigned) n & 0xFFFFFF;
		else if (!strcmp (k, "marker_colour")) g_markColour = (unsigned) n & 0xFFFFFF;
		else if (!strcmp (k, "pen_size")) g_penSize = (int) n & 3;
		else if (!strcmp (k, "marker_size")) g_markSize = (int) n & 3;
		else if (!strcmp (k, "folder")) scpy (g_lastDir, v, sizeof g_lastDir);
	}
	fclose (f);
}
static void settings_save ()
{
	FILE *f = fopen (SETTINGS, "w");
	if (!f) return;
	fprintf (f, "# Screenshot's settings (the app writes them)\nmode = %d\ndelay = %d\npen_colour = 0x%06X\npen_size = %d\n"
		 "marker_colour = 0x%06X\nmarker_size = %d\nfolder = %s\n",
		 g_mode, DELAYS[g_delay], g_penColour, g_penSize, g_markColour, g_markSize, g_lastDir);
	fclose (f);
}

// ---- a thick stroke, anti-aliased: points in pixels of the target, its coverage the union of its segments (an overlap
// is not darker: a marker's ink), laid in colour with alpha (0..255) -------------------------------------------------------
static void draw_stroke (uint32_t *px, int tw, int th, int stride, const float *xy, int n, float width, unsigned colour, int alpha)
{
	if (n <= 0 || tw <= 0 || th <= 0) return;
	float r = width / 2; if (r < 0.6f) r = 0.6f;
	float x0 = xy[0], x1 = xy[0], y0 = xy[1], y1 = xy[1];
	for (int k = 1; k < n; k++)
	{
		float x = xy[2 * k], y = xy[2 * k + 1];
		if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y;
	}
	int bx0 = (int) floorf (x0 - r - 1), by0 = (int) floorf (y0 - r - 1), bx1 = (int) ceilf (x1 + r + 1), by1 = (int) ceilf (y1 + r + 1);
	if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; if (bx1 > tw) bx1 = tw; if (by1 > th) by1 = th;
	if (bx1 <= bx0 || by1 <= by0) return;
	int mw = bx1 - bx0, mh = by1 - by0;
	unsigned char *m = (unsigned char *) calloc ((size_t) mw * mh, 1);
	if (!m) return;
	for (int k = 0; k < n; k++)
	{
		float ax = xy[2 * k], ay = xy[2 * k + 1];
		float bx = k + 1 < n ? xy[2 * k + 2] : ax, by = k + 1 < n ? xy[2 * k + 3] : ay;
		float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
		int sx0 = (int) floorf ((ax < bx ? ax : bx) - r - 1), sx1 = (int) ceilf ((ax > bx ? ax : bx) + r + 1);
		int sy0 = (int) floorf ((ay < by ? ay : by) - r - 1), sy1 = (int) ceilf ((ay > by ? ay : by) + r + 1);
		if (sx0 < bx0) sx0 = bx0; if (sy0 < by0) sy0 = by0; if (sx1 > bx1) sx1 = bx1; if (sy1 > by1) sy1 = by1;
		for (int py = sy0; py < sy1; py++)
		{
			unsigned char *mr = m + (long) (py - by0) * mw - bx0;
			float cy = py + 0.5f;
			for (int pxl = sx0; pxl < sx1; pxl++)
			{
				float cx = pxl + 0.5f, t = l2 > 0 ? ((cx - ax) * dx + (cy - ay) * dy) / l2 : 0;
				if (t < 0) t = 0; else if (t > 1) t = 1;
				float ex = ax + t * dx - cx, ey = ay + t * dy - cy, c = r + 0.5f - sqrtf (ex * ex + ey * ey);
				if (c <= 0) continue;
				int v = c >= 1 ? 255 : (int) (c * 255);
				if (v > mr[pxl]) mr[pxl] = (unsigned char) v;
			}
		}
	}
	unsigned cr = colour >> 16 & 255, cg = colour >> 8 & 255, cb = colour & 255;
	for (int y = 0; y < mh; y++)
	{
		uint32_t *row = px + (long) (y + by0) * stride + bx0;
		const unsigned char *mr = m + (long) y * mw;
		for (int x = 0; x < mw; x++)
		{
			if (!mr[x]) continue;
			unsigned a = mr[x] * (unsigned) alpha / 255, d = row[x];
			unsigned r2 = ((d >> 16 & 255) * (255 - a) + cr * a) / 255, g2 = ((d >> 8 & 255) * (255 - a) + cg * a) / 255, b2 = ((d & 255) * (255 - a) + cb * a) / 255;
			row[x] = r2 << 16 | g2 << 8 | b2;
		}
	}
	free (m);
}

// ---- the capture and what is drawn on it ----------------------------------------------------------------------------------
// The picture as grabbed (g_base), the part of it kept (the crop), the strokes (in the base's pixels), the operations done
// (undo / redo), and the result: g_doc, the crop with the strokes over it.
struct Stroke { int tool; unsigned colour; int size; int n, cap; float *xy; bool alive; };
struct Op { int kind; int stroke; int r0[4], r1[4]; };
enum { OP_ADD, OP_ERASE, OP_CROP };

static uint32_t *g_base; static int g_bw, g_bh;
static int g_crop[4];						// x, y, w, h in the base
static Stroke *g_st; static int g_nst, g_stcap;
static Op *g_ops; static int g_nops, g_opcap, g_opPos;		// done: [0, g_opPos); redo: [g_opPos, g_nops)
static uint32_t *g_doc; static int g_dw, g_dh;
static unsigned g_docGen;
static char g_what[120];					// "Window 'Terminal'" / "Rectangle" / "Full screen"
static char g_savedAs[300];
static bool g_dirty;
static unsigned g_copiedGen = ~0u;

static void strokes_free ()
{
	for (int i = 0; i < g_nst; i++) free (g_st[i].xy);
	g_nst = 0; g_nops = g_opPos = 0;
}
static void doc_render ()
{
	int w = g_crop[2], h = g_crop[3];
	if (w != g_dw || h != g_dh || !g_doc)
	{
		delete[] g_doc; g_doc = new uint32_t[(long) w * h];
		g_dw = w; g_dh = h;
	}
	for (int y = 0; y < h; y++) memcpy (g_doc + (long) y * w, g_base + (long) (g_crop[1] + y) * g_bw + g_crop[0], (size_t) w * 4);
	for (int i = 0; i < g_nst; i++)
	{
		Stroke &s = g_st[i];
		if (!s.alive || !s.n) continue;
		float *q = (float *) malloc (sizeof (float) * 2 * s.n);
		for (int k = 0; k < s.n; k++) { q[2 * k] = s.xy[2 * k] - g_crop[0]; q[2 * k + 1] = s.xy[2 * k + 1] - g_crop[1]; }
		draw_stroke (g_doc, w, h, w, q, s.n, (float) s.size, s.colour, s.tool == TOOL_MARKER ? MARK_ALPHA : 255);
		free (q);
	}
	g_docGen++;
}
static void op_push (const Op &o)
{
	g_nops = g_opPos;					// the redo branch: gone (its strokes stay dead)
	if (g_nops == g_opcap)
	{
		int nc = g_opcap ? g_opcap * 2 : 64; Op *n = new Op[nc];
		if (g_nops) memcpy (n, g_ops, sizeof (Op) * g_nops);
		delete[] g_ops; g_ops = n; g_opcap = nc;
	}
	g_ops[g_nops++] = o; g_opPos = g_nops;
}
static bool can_undo () { return g_base && g_opPos > 0; }
static bool can_redo () { return g_base && g_opPos < g_nops; }
static void op_apply (const Op &o, bool undo)
{
	if (o.kind == OP_ADD) g_st[o.stroke].alive = !undo;
	else if (o.kind == OP_ERASE) g_st[o.stroke].alive = undo;
	else memcpy (g_crop, undo ? o.r0 : o.r1, sizeof g_crop);
}
static Stroke *stroke_new (int tool)
{
	if (g_nst == g_stcap)
	{
		int nc = g_stcap ? g_stcap * 2 : 64; Stroke *n = new Stroke[nc];
		if (g_nst) memcpy (n, g_st, sizeof (Stroke) * g_nst);
		delete[] g_st; g_st = n; g_stcap = nc;
	}
	Stroke &s = g_st[g_nst++];
	s.tool = tool; s.colour = tool == TOOL_MARKER ? g_markColour : g_penColour;
	s.size = tool == TOOL_MARKER ? MARK_SIZES[g_markSize] : PEN_SIZES[g_penSize];
	s.n = 0; s.cap = 64; s.xy = (float *) malloc (sizeof (float) * 2 * s.cap); s.alive = false;
	return &s;
}
static void stroke_add (Stroke *s, float x, float y)
{
	if (s->n && fabsf (s->xy[2 * s->n - 2] - x) < 0.5f && fabsf (s->xy[2 * s->n - 1] - y) < 0.5f) return;	// (half a pixel)
	if (s->n == s->cap) { s->cap *= 2; s->xy = (float *) realloc (s->xy, sizeof (float) * 2 * s->cap); }
	s->xy[2 * s->n] = x; s->xy[2 * s->n + 1] = y; s->n++;
}
// a hand-drawn stroke smoothed (twice 1-2-1 over its points, the ends kept): the mouse's steps and jitter gone
static void stroke_smooth (Stroke *s)
{
	if (s->n < 4) return;
	float *t = (float *) malloc (sizeof (float) * 2 * s->n);
	for (int pass = 0; pass < 2; pass++)
	{
		memcpy (t, s->xy, sizeof (float) * 2 * s->n);
		for (int k = 1; k < s->n - 1; k++)
			for (int c = 0; c < 2; c++) s->xy[2 * k + c] = (t[2 * (k - 1) + c] + 2 * t[2 * k + c] + t[2 * (k + 1) + c]) / 4;
	}
	free (t);
}
// the topmost stroke under (x, y) (the base's pixels), within `slack` pixels of its edge: -1 none
static int stroke_at (float x, float y, float slack)
{
	for (int i = g_nst - 1; i >= 0; i--)
	{
		Stroke &s = g_st[i];
		if (!s.alive) continue;
		float r = s.size / 2.0f + slack, r2 = r * r;
		for (int k = 0; k < s.n; k++)
		{
			float ax = s.xy[2 * k], ay = s.xy[2 * k + 1];
			float bx = k + 1 < s.n ? s.xy[2 * k + 2] : ax, by = k + 1 < s.n ? s.xy[2 * k + 3] : ay;
			float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
			if (t < 0) t = 0; else if (t > 1) t = 1;
			float ex = ax + t * dx - x, ey = ay + t * dy - y;
			if (ex * ex + ey * ey <= r2) return i;
		}
	}
	return -1;
}

// ---- the window's parts ------------------------------------------------------------------------------------------------
static long g_main;						// the window (its handle)
static long g_tb, g_tools, g_view;
static long g_bNew, g_ddMode, g_ddDelay, g_bCopy, g_bSave;
static long g_bPen, g_bMark, g_bErase, g_bCrop, g_cColour, g_ddSize, g_bUndo, g_bRedo;
static int  TB_H = 48, CTL = 32;
static int  g_tool = TOOL_PEN, g_prevTool = TOOL_PEN, g_shownTool = -1;

// The app's state: idle (a capture shown, or none), the delay counting, the screen being taken, a part being chosen.
enum { ST_IDLE, ST_COUNT, ST_HIDE, ST_CHOOSE };
static int g_state = ST_IDLE;
static unsigned g_t0;						// ms
static int g_pending;						// the mode of the capture under way

static void refresh_ui ();
static void set_tool (int t);
static int ctl_h () { return (int) ui_metric (UMT_CTL); }	// hauteur d'un controle : celle de UIKit (echelle, densite tactile)
static int text_w (const char *s) { return (int) ui_text_width ((long) s, 14, (long) ui.font); }
static bool has_capture () { return g_base != 0; }

// ---- the view: the capture fitted in the window, the drawing on it; or the welcome, the countdown --------------------------
static struct
{
	uint32_t *scaled; int sw, sh; long cap; unsigned gen;	// the doc fitted (a box filter)
	int ox, oy;						// where it is
	Stroke *live; bool straight;				// the stroke being drawn
	bool dragging; int ax, ay, bx, by;			// a crop being dragged (view px)
	int eraseHot;						// the stroke the eraser points at
	int w, h;						// the view's size
	bool down;
} V = { 0, 0, 0, 0, ~0u, 0, 0, 0, false, false, 0, 0, 0, 0, -1, 100, 100, false };

static int area_h () { return V.h - (has_capture () ? ST_H : 0); }
static void fit_size (int *w, int *h)
{
	int aw = V.w - 40, ah = area_h () - 40;
	if (aw < 16) aw = 16; if (ah < 16) ah = 16;
	double s = 1;
	if (g_dw > aw) s = (double) aw / g_dw;
	if (g_dh * s > ah) s = (double) ah / g_dh;
	*w = (int) (g_dw * s); *h = (int) (g_dh * s);
	if (*w < 1) *w = 1; if (*h < 1) *h = 1;
}
static int zoom_pc () { int w, h; fit_size (&w, &h); return (int) (((long long) w * 100 + g_dw / 2) / g_dw); }
static void rescale ()
{
	int w, h; fit_size (&w, &h);
	if (w == V.sw && h == V.sh && V.gen == g_docGen) return;
	if ((long) w * h > V.cap) { delete[] V.scaled; V.cap = (long) w * h; V.scaled = new uint32_t[V.cap]; }
	V.sw = w; V.sh = h; V.gen = g_docGen;
	if (w == g_dw && h == g_dh) { memcpy (V.scaled, g_doc, (size_t) w * h * 4); return; }
	for (int y = 0; y < h; y++)					// the area of each pixel averaged
	{
		int y0 = (int) ((long long) y * g_dh / h), y1 = (int) ((long long) (y + 1) * g_dh / h); if (y1 <= y0) y1 = y0 + 1;
		for (int x = 0; x < w; x++)
		{
			int x0 = (int) ((long long) x * g_dw / w), x1 = (int) ((long long) (x + 1) * g_dw / w); if (x1 <= x0) x1 = x0 + 1;
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int yy = y0; yy < y1; yy++)
			{
				const uint32_t *row = g_doc + (long) yy * g_dw;
				for (int xx = x0; xx < x1; xx++) { unsigned c = row[xx]; r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++; }
			}
			V.scaled[(long) y * w + x] = ((r / n) << 16) | ((g / n) << 8) | (b / n);
		}
	}
}
// view px <-> the base's pixels
static float toX (int vx) { return g_crop[0] + (vx - V.ox + 0.5f) * g_dw / V.sw; }
static float toY (int vy) { return g_crop[1] + (vy - V.oy + 0.5f) * g_dh / V.sh; }
static float fromX (float x) { return V.ox + (x - g_crop[0]) * V.sw / g_dw; }
static float fromY (float y) { return V.oy + (y - g_crop[1]) * V.sh / g_dh; }

static unsigned backdrop () { return jet (TJ_CREUX); }
static void text_c (int x, int y, int w, int h, const char *s, unsigned c, int px = 14, long font = 0)
{
	if (!font) font = (long) ui.font;
	long tw = ui_text_width ((long) s, px, font);
	ui_text ((long) s, x + (w - tw) / 2, y + (h + px * 7 / 10) / 2, c, px, font, 0);
}
// a ring (an arc from a0 to a1 degrees, clockwise from the top) of radius r, thickness t (pixels)
static void ring (float cx, float cy, float r, float t, float a0, float a1, unsigned c)
{
	int n = (int) ((a1 - a0) / 6) + 2; if (n > 64) n = 64;
	int32_t pts[4 * 66];
	for (int i = 0; i < n; i++)
	{
		float a = (a0 + (a1 - a0) * i / (n - 1)) * 3.14159265f / 180;
		pts[2 * i] = (int32_t) ((cx + (r + t / 2) * sinf (a)) * 16); pts[2 * i + 1] = (int32_t) ((cy - (r + t / 2) * cosf (a)) * 16);
		pts[2 * (2 * n - 1 - i)] = (int32_t) ((cx + (r - t / 2) * sinf (a)) * 16); pts[2 * (2 * n - 1 - i) + 1] = (int32_t) ((cy - (r - t / 2) * cosf (a)) * 16);
	}
	ui_vp_poly ((long) pts, 2 * n, c);
}
static void draw_welcome ()
{
	int y = area_h () / 2 - 24;
	text_c (0, y, V.w, 28, TR ("Click New to start a capture"), jet (TJ_TEXTE1), 16, (long) ui.font_b);
	text_c (0, y + 32, V.w, 22, TR ("The capture is copied at once, saved only with Save As."), jet (TJ_TEXTE2), 13);
}
static void draw_countdown ()
{
	int total = DELAYS[g_delay] * 1000, el = (int) (ms_now () - g_t0), left = (total - el + 999) / 1000;
	if (left < 1) left = 1;
	int cx = V.w / 2, cy = V.h / 2 - 16, r = 46;
	ring ((float) cx, (float) cy, (float) r, 6, 0, 360, mix (jet (TJ_FACE), jet (TJ_TEXTE1), 40));
	float ang = 360 - (float) el * 360 / (total ? total : 1); if (ang < 1) ang = 1;
	ring ((float) cx, (float) cy, (float) r, 6, 0, ang, jet (TJ_ACCENT));
	char t[8]; snprintf (t, sizeof t, "%d", left);
	text_c (cx - r, cy - r, 2 * r, 2 * r, t, jet (TJ_TEXTE1), 40, (long) ui.font_b);
	char m[160];
	snprintf (m, sizeof m, TR (g_mode == MODE_RECT ? "Choose a rectangle in %d s  -  Esc: cancel" : g_mode == MODE_WINDOW ? "Choose a window in %d s  -  Esc: cancel"
				  : "The screen is taken in %d s  -  Esc: cancel"), left);
	text_c (0, cy + r + 14, V.w, 20, m, jet (TJ_TEXTE2), 13);
}
static void status_line (char *l, int lc, char *r, int rc)
{
	const char *state = g_copiedGen == g_docGen ? TR ("copied to the clipboard, not saved") : g_savedAs[0] ? TR ("changed since saved") : TR ("not saved");
	const char *sa = g_savedAs[0] ? strrchr (g_savedAs, '/') : 0;
	if (g_savedAs[0] && !g_dirty) { char f[200]; snprintf (f, sizeof f, TR ("saved as %s"), sa ? sa + 1 : g_savedAs); snprintf (l, lc, "%s  -  %d \xC3\x97 %d  -  %s", g_what, g_dw, g_dh, f); }
	else snprintf (l, lc, "%s  -  %d \xC3\x97 %d  -  %s", g_what, g_dw, g_dh, state);
	const char *hint = g_tool == TOOL_PEN || g_tool == TOOL_MARKER ? TR ("Drag to draw, Shift: a straight line") :
			   g_tool == TOOL_ERASER ? TR ("Click a stroke to take it away") : TR ("Drag the part to keep");
	snprintf (r, rc, "%s     %d %%", hint, zoom_pc ());
}
static long view_paint (long me, long w, long h)
{
	V.w = (int) w; V.h = (int) h;
	long pw = 0, ph = 0;
	uint32_t *px = uikit_canvas_pixels (me, &pw, &ph);
	int ah = area_h ();
	if (g_state == ST_COUNT) { ui_fill (0, 0, w, h, jet (TJ_FACE)); draw_countdown (); return 0; }
	if (!has_capture ()) { ui_fill (0, 0, w, h, jet (TJ_FACE)); draw_welcome (); return 0; }
	ui_fill (0, 0, w, ah, backdrop ());
	rescale ();
	V.ox = ((int) w - V.sw) / 2; V.oy = (ah - V.sh) / 2;
	ui_rbox (V.ox + 2, V.oy + 4, V.sw, V.sh, 2, mix (backdrop (), 0, 60), mix (backdrop (), 0, 60));	// a soft shadow
	for (int y = 0; y < V.sh && V.oy + y < ph; y++)
		if (V.oy + y >= 0) memcpy (px + (long) (V.oy + y) * pw + V.ox, V.scaled + (long) y * V.sw, (size_t) V.sw * 4);
	ui_frame (V.ox - 1, V.oy - 1, V.sw + 2, V.sh + 2, mix (backdrop (), 0, 90));
	float sc = (float) V.sw / g_dw;
	if (V.live && V.live->n)						// the stroke being drawn, over the view
	{
		float *q = (float *) malloc (sizeof (float) * 2 * V.live->n);
		for (int k = 0; k < V.live->n; k++) { q[2 * k] = fromX (V.live->xy[2 * k]); q[2 * k + 1] = fromY (V.live->xy[2 * k + 1]); }
		float s = V.live->size * sc; if (s < 1) s = 1;
		draw_stroke (px, (int) pw, ah, (int) pw, q, V.live->n, s, V.live->colour, V.live->tool == TOOL_MARKER ? MARK_ALPHA : 255);
		free (q);
	}
	if (g_tool == TOOL_ERASER && V.eraseHot >= 0 && V.eraseHot < g_nst)	// the stroke the eraser would take
	{
		Stroke &st = g_st[V.eraseHot];
		float *q = (float *) malloc (sizeof (float) * 2 * st.n);
		for (int k = 0; k < st.n; k++) { q[2 * k] = fromX (st.xy[2 * k]); q[2 * k + 1] = fromY (st.xy[2 * k + 1]); }
		draw_stroke (px, (int) pw, ah, (int) pw, q, st.n, st.size * sc + 6, jet (TJ_ACCENT), 90);
		free (q);
	}
	if (g_tool == TOOL_CROP && V.dragging)				// the part kept: bright, the rest dimmed
	{
		int x0 = V.ax < V.bx ? V.ax : V.bx, x1 = V.ax < V.bx ? V.bx : V.ax, y0 = V.ay < V.by ? V.ay : V.by, y1 = V.ay < V.by ? V.by : V.ay;
		if (x0 < V.ox) x0 = V.ox; if (y0 < V.oy) y0 = V.oy; if (x1 > V.ox + V.sw) x1 = V.ox + V.sw; if (y1 > V.oy + V.sh) y1 = V.oy + V.sh;
		for (int y = V.oy; y < V.oy + V.sh && y < ph; y++)
		{
			uint32_t *row = px + (long) y * pw;
			for (int x = V.ox; x < V.ox + V.sw && x < pw; x++)
				if (y < y0 || y >= y1 || x < x0 || x >= x1) { unsigned c = row[x]; row[x] = (c >> 1) & 0x7F7F7F; }
		}
		ui_frame (x0, y0, x1 - x0, y1 - y0, 0xFFFFFF);
		for (int i = 1; i < 3; i++)					// thirds, as a camera's
		{
			ui_fill (x0 + (x1 - x0) * i / 3, y0, 1, y1 - y0, 0xC0C0C0);
			ui_fill (x0, y0 + (y1 - y0) * i / 3, x1 - x0, 1, 0xC0C0C0);
		}
		char t[32]; snprintf (t, sizeof t, "%d \xC3\x97 %d", (int) (toX (x1) - toX (x0) + 0.5f), (int) (toY (y1) - toY (y0) + 0.5f));
		int tw = text_w (t) + 14, ty = y1 + 6 + 20 <= ah ? y1 + 6 : y0 - 26;
		ui_rbox (x0, ty, tw, 20, 6, 0x202024, 0x202024);
		text_c (x0, ty, tw, 20, t, 0xFFFFFF, 13);
	}
	// the state: what was taken, its size, copied / saved; the tool's hint and the zoom
	ui_fill (0, ah, w, h - ah, jet (TJ_FACE));
	ui_fill (0, ah, w, 1, jet (TJ_LIGNE));
	char l[400], r[200]; status_line (l, sizeof l, r, sizeof r);
	int rw = (int) ui_text_width ((long) r, 13, (long) ui.font);
	ui_vp_disc (12 * 16, (ah + ST_H / 2) * 16, 4 * 16, g_savedAs[0] && !g_dirty ? jet (TJ_SUCCES) : g_copiedGen == g_docGen ? jet (TJ_SUCCES) : jet (TJ_ALERTE));
	ui_text_fit ((long) l, 22, ah + (ST_H + 9) / 2 + 1, jet (TJ_TEXTE1), 13, (long) ui.font, w - 40 - rw - 20);
	ui_text ((long) r, w - 10 - rw, ah + (ST_H + 9) / 2 + 1, jet (TJ_TEXTE2), 13, (long) ui.font, 0);
	return 0;
}
static void view_dirty () { ui_view_dirty (g_view); }
static long view_mouse (long, long kind, long mx, long my, long info)
{
	if (!has_capture () || g_state != ST_IDLE) return 0;
	if (kind == 3) { if (V.eraseHot >= 0) { V.eraseHot = -1; view_dirty (); } return 0; }
	bool in = mx >= V.ox && my >= V.oy && mx < V.ox + V.sw && my < V.oy + V.sh;
	bool down = kind == 1 && (info >> 8 & 1), up = kind == 2 && V.down;
	if (down) V.down = true;
	if (up) V.down = false;
	bool shift = (info >> 16 & UI_MOD_SHIFT) != 0;
	float x = toX ((int) mx), y = toY ((int) my);
	if (g_tool == TOOL_PEN || g_tool == TOOL_MARKER)
	{
		if (down && in)
		{
			V.live = stroke_new (g_tool);
			stroke_add (V.live, x, y);
			V.straight = shift;
			view_dirty ();
		}
		else if (V.live && V.down)
		{
			if (V.straight || shift) { V.live->n = 1; V.straight = true; }
			stroke_add (V.live, x, y);
			view_dirty ();
		}
		else if (V.live && up)
		{
			if (!V.straight) stroke_smooth (V.live);
			V.live->alive = true;
			Op o; memset (&o, 0, sizeof o); o.kind = OP_ADD; o.stroke = (int) (V.live - g_st);
			V.live = 0;
			op_push (o);
			g_dirty = true; doc_render (); refresh_ui (); view_dirty ();
		}
	}
	else if (g_tool == TOOL_ERASER)
	{
		float slack = 5.0f * g_dw / (V.sw ? V.sw : 1);
		int hh = in ? stroke_at (x, y, slack) : -1;
		if (hh != V.eraseHot) { V.eraseHot = hh; view_dirty (); }
		if ((down || V.down) && hh >= 0)
		{
			Op o; memset (&o, 0, sizeof o); o.kind = OP_ERASE; o.stroke = hh; op_push (o);
			g_st[hh].alive = false; V.eraseHot = -1;
			g_dirty = true; doc_render (); refresh_ui (); view_dirty ();
		}
	}
	else if (g_tool == TOOL_CROP)
	{
		if (down && in) { V.dragging = true; V.ax = V.bx = (int) mx; V.ay = V.by = (int) my; view_dirty (); }
		else if (V.dragging && V.down)
		{
			V.bx = mx < V.ox ? V.ox : mx > V.ox + V.sw ? V.ox + V.sw : (int) mx;
			V.by = my < V.oy ? V.oy : my > V.oy + V.sh ? V.oy + V.sh : (int) my;
			view_dirty ();
		}
		else if (V.dragging && up)
		{
			V.dragging = false;
			int x0 = (int) toX (V.ax < V.bx ? V.ax : V.bx), x1 = (int) toX (V.ax < V.bx ? V.bx : V.ax);
			int y0 = (int) toY (V.ay < V.by ? V.ay : V.by), y1 = (int) toY (V.ay < V.by ? V.by : V.ay);
			if (x0 < g_crop[0]) x0 = g_crop[0]; if (y0 < g_crop[1]) y0 = g_crop[1];
			if (x1 > g_crop[0] + g_crop[2]) x1 = g_crop[0] + g_crop[2]; if (y1 > g_crop[1] + g_crop[3]) y1 = g_crop[1] + g_crop[3];
			if (x1 - x0 >= 8 && y1 - y0 >= 8 && (x1 - x0 < g_crop[2] || y1 - y0 < g_crop[3]))
			{
				Op o; memset (&o, 0, sizeof o); o.kind = OP_CROP; o.stroke = -1;
				memcpy (o.r0, g_crop, sizeof g_crop);
				o.r1[0] = x0; o.r1[1] = y0; o.r1[2] = x1 - x0; o.r1[3] = y1 - y0;
				op_push (o); memcpy (g_crop, o.r1, sizeof g_crop);
				g_dirty = true; doc_render ();
				set_tool (g_prevTool);				// (cropped: back to the tool before)
			}
			view_dirty ();
		}
	}
	return 0;
}

// ---- the actions ---------------------------------------------------------------------------------------------------------
static void copy_doc (bool quiet)
{
	if (!g_doc) return;
	if (!ui_clip_set_image ((long) g_doc, g_dw, g_dh))
	{
		if (!quiet) { ui_message ((long) TR ("Screenshot"), (long) TR ("The picture could not be copied: the clipboard service (clipd) is not there."), UM_OK); ui_w_draw (0); }
		return;
	}
	g_copiedGen = g_docGen;
}
static bool ends_with (const char *s, const char *e)
{
	int a = (int) strlen (s), b = (int) strlen (e);
	return b <= a && !strcasecmp (s + a - b, e);
}
static void save_as ()
{
	if (!g_doc) return;
	mkdir (PICTURES, 0755); mkdir (SHOTS_DIR, 0755);
	time_t now = time (0); struct tm tm; localtime_r (&now, &tm);
	char name[96]; snprintf (name, sizeof name, "Screenshot %04d-%02d-%02d %02d-%02d-%02d.png", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	static char path[300];
	struct stat st;
	const char *dir = stat (g_lastDir, &st) == 0 ? g_lastDir : SHOTS_DIR;
	ui_file_filter ((long) "*.png;*.jpg;*.jpeg;*.bmp");
	long r = ui_file_save ((long) dir, (long) name, (long) path, sizeof path - 8);
	ui_file_filter (0);
	ui_w_draw (0);
	if (r != 1) return;
	if (!ends_with (path, ".png") && !ends_with (path, ".jpg") && !ends_with (path, ".jpeg") && !ends_with (path, ".bmp")) strcat (path, ".png");
	long n = (long) g_dw * g_dh;
	unsigned *px = new unsigned[n];
	for (long i = 0; i < n; i++) px[i] = g_doc[i] | 0xFF000000u;
	unsigned len = 0; unsigned char *b;
	if (ends_with (path, ".jpg") || ends_with (path, ".jpeg")) b = pngsave::jpeg_encode (px, g_dw, g_dh, 92, &len);
	else if (ends_with (path, ".bmp")) b = pngsave::bmp_encode (px, g_dw, g_dh, &len);
	else b = pngsave::png_encode (px, g_dw, g_dh, false, &len);
	delete[] px;
	bool ok = false;
	int f = b ? open (path, O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
	if (f >= 0) { ok = write (f, b, len) == (long) len; close (f); }
	delete[] b;
	if (!ok)
	{
		char m[400]; snprintf (m, sizeof m, TR ("The picture could not be saved in %s."), path);
		ui_message ((long) TR ("Screenshot"), (long) m, UM_OK); ui_w_draw (0);
		return;
	}
	scpy (g_savedAs, path, sizeof g_savedAs);
	g_dirty = false;
	char *sl = strrchr (path, '/'); if (sl) { *sl = 0; scpy (g_lastDir, path, sizeof g_lastDir); }
	settings_save ();
	refresh_ui ();
}
static void undo () { if (!can_undo ()) return; op_apply (g_ops[--g_opPos], true); g_dirty = true; doc_render (); refresh_ui (); view_dirty (); }
static void redo () { if (!can_redo ()) return; op_apply (g_ops[g_opPos++], false); g_dirty = true; doc_render (); refresh_ui (); view_dirty (); }
static void set_tool (int t)
{
	if (t == TOOL_CROP && g_tool != TOOL_CROP) g_prevTool = g_tool;
	g_tool = t; V.eraseHot = -1; V.dragging = false;
	refresh_ui (); view_dirty ();
}
static void clear_drawing ()
{
	if (!has_capture ()) return;
	bool any = false;
	for (int i = 0; i < g_nst; i++) if (g_st[i].alive) any = true;
	if (!any) return;
	for (int i = 0; i < g_nst; i++) if (g_st[i].alive) { Op o; memset (&o, 0, sizeof o); o.kind = OP_ERASE; o.stroke = i; op_push (o); g_st[i].alive = false; }
	g_dirty = true; doc_render (); refresh_ui (); view_dirty ();
}

// ---- the layout ----------------------------------------------------------------------------------------------------------
static void lay_out (long w, long h)
{
	bool has = has_capture ();
	int y = (TB_H - CTL) / 2, x = 8;
	ui_w_move (g_tb, 0, 0, w, TB_H);
	auto place = [&] (long wd, int wid, int gap) { ui_w_move (wd, x, y, wid, CTL); x += wid + gap; };
	place (g_bNew, text_w (TR ("New")) + 36, 14);
	place (g_ddMode, 40 + (text_w (TR ("Full screen")) > text_w (TR ("Rectangle")) ? text_w (TR ("Full screen")) : text_w (TR ("Rectangle"))), 6);
	place (g_ddDelay, 40 + text_w (TR ("10 seconds")), 14);
	place (g_bCopy, text_w (TR ("Copy")) + 28, 4);
	place (g_bSave, text_w (TR ("Save As...")) + 28, 0);
	ui_w_show (g_bCopy, has); ui_w_show (g_bSave, has);
	ui_w_show (g_tools, has);
	int top = TB_H;
	if (has)
	{
		ui_w_move (g_tools, 0, TB_H, w, TB_H);
		x = 8; y = TB_H + (TB_H - CTL) / 2;
		place (g_bPen, text_w (TR ("Pen")) + 28, 4);
		place (g_bMark, text_w (TR ("Marker")) + 28, 4);
		place (g_bErase, text_w (TR ("Eraser")) + 28, 4);
		place (g_bCrop, text_w (TR ("Crop")) + 28, 14);
		place (g_cColour, CTL + 18, 6);
		place (g_ddSize, 40 + text_w (TR ("Very thick")), 14);
		place (g_bUndo, text_w (TR ("Undo")) + 28, 4);
		place (g_bRedo, text_w (TR ("Redo")) + 28, 0);
		top += TB_H;
	}
	ui_w_move (g_view, 0, top, w, h - top);
	view_dirty ();
}
static void paint_all ()
{
	ui_window_use (g_main);
	ui_fill (0, 0, ui.w, ui.h, jet (TJ_FACE));
	lay_out (ui.w, ui.h);
	refresh_ui ();
	ui_w_draw (0);
	ui_update_all ();
}
static void on_resize (long, long) { paint_all (); }
// the tools' settings shown: the pen's or the marker's colour and size
static void show_tool_settings ()
{
	bool m = g_tool == TOOL_MARKER;
	ui_w_set_value (g_cColour, m ? g_markColour : g_penColour);
	ui_w_set_value (g_ddSize, m ? g_markSize : g_penSize);
}
static void refresh_ui ()
{
	bool has = has_capture (), idle = g_state == ST_IDLE;
	ui_w_enable (g_bNew, idle);
	for (long b : { g_bCopy, g_bSave, g_bPen, g_bMark, g_bErase, g_bCrop, g_cColour, g_ddSize }) ui_w_enable (b, has && idle);
	ui_w_enable (g_bUndo, can_undo () && idle); ui_w_enable (g_bRedo, can_redo () && idle);
	ui_w_enable (g_cColour, has && idle && (g_tool == TOOL_PEN || g_tool == TOOL_MARKER));
	ui_w_enable (g_ddSize, has && idle && (g_tool == TOOL_PEN || g_tool == TOOL_MARKER));
	if (g_tool != g_shownTool)					// the tool chosen: its button in the accent
	{
		long tb[4] = { g_bPen, g_bMark, g_bErase, g_bCrop };
		for (int i = 0; i < 4; i++) { ui_w_style (tb[i], i == g_tool ? UB_DEFAULT : UB_GHOST); ui_w_draw (tb[i]); }
		g_shownTool = g_tool;
		show_tool_settings ();
	}
	ui_menu_state (12, can_undo () ? 0 : UMS_DISABLED);
	ui_menu_state (13, can_redo () ? 0 : UMS_DISABLED);
	view_dirty ();
}

// ---- the capture: hiding, grabbing, choosing ------------------------------------------------------------------------------
// The overlay: a frameless window over the whole screen, first see-through (it hides the pointer while the screen is taken),
// then the screen frozen and darkened, a rectangle dragged or a window clicked on it.
struct OvWin { int x, y, w, h; char title[33]; };
static struct
{
	long win; int W, H;
	uint32_t *frozen, *dim;
	int mode, mx, my; bool down, dragging; int ax, ay;
	OvWin wins[64]; int nwin, hot;
	int rx, ry, rw, rh; char title[40];
	int barX, barY, barW, barH;
	int dirty[4];						// the area to show again (x0, y0, x1, y1)
} O;

static void ov_windows ()
{
	O.nwin = 0;
	unsigned char e[WE_PER_MSG * WE_SIZE];
	int mine = getpid ();
	long total = 0;
	OvWin all[64]; int n = 0;
	for (long first = 0; n < 64; first += WE_PER_MSG)
	{
		total = ui_win_details (first, (long) e);
		if (total <= first) break;
		for (int i = 0; i < WE_PER_MSG && first + i < total && n < 64; i++)
		{
			const unsigned char *p = e + i * WE_SIZE;
			int pid = *(const int *) (p + WE_PID); unsigned flags = *(const unsigned *) (p + WE_FLAGS), state = *(const unsigned *) (p + WE_STATE);
			int x = *(const short *) (p + WE_X), y = *(const short *) (p + WE_Y), w = *(const unsigned short *) (p + WE_W), h = *(const unsigned short *) (p + WE_H);
			if (pid == mine || (flags & WF_BACK) || (state & (WES_MINI | WES_REDUITE)) || w <= 0 || h <= 0) continue;
			OvWin &o = all[n++]; o.x = x; o.y = y; o.w = w; o.h = h;
			memcpy (o.title, p + WE_TITLE, 32); o.title[32] = 0;
		}
	}
	for (int i = n - 1; i >= 0; i--) O.wins[O.nwin++] = all[i];	// (the front one first)
}
static int ov_window_at (int x, int y)
{
	for (int i = 0; i < O.nwin; i++) { auto &w = O.wins[i]; if (x >= w.x && y >= w.y && x < w.x + w.w && y < w.y + w.h) return i; }
	return -1;
}
static int ov_bar_item (int x, int y)
{
	if (y < O.barY || y >= O.barY + O.barH || x < O.barX || x >= O.barX + O.barW) return -1;
	int bw = (O.barW - 16 - 44) / 3, i = (x - O.barX - 8) / bw;
	if (x >= O.barX + O.barW - 44) return 3;			// close
	if (i >= 0 && i < 3) return i;
	return -2;
}
static void ov_mark (int x, int y, int w, int h)
{
	if (w <= 0 || h <= 0) return;
	if (x < O.dirty[0]) O.dirty[0] = x; if (y < O.dirty[1]) O.dirty[1] = y;
	if (x + w > O.dirty[2]) O.dirty[2] = x + w; if (y + h > O.dirty[3]) O.dirty[3] = y + h;
}
static void ov_copy (const uint32_t *src, int x, int y, int w, int h)	// a part of the frozen or the dimmed screen, put back
{
	if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
	if (x + w > O.W) w = O.W - x; if (y + h > O.H) h = O.H - y;
	if (w <= 0 || h <= 0) return;
	for (int j = y; j < y + h; j++) memcpy (ui.surf + (long) j * O.W + x, src + (long) j * O.W + x, (size_t) w * 4);
	ov_mark (x, y, w, h);
}
static void ov_pill (int cx, int y, const char *s, unsigned bg)
{
	int w = text_w (s) + 32;
	ui_rbox (cx - w / 2, y, w, 32, 16, bg, bg);
	text_c (cx - w / 2, y, w, 32, s, 0xFFFFFF, 14);
	ov_mark (cx - w / 2, y, w, 32);
}
static int g_drawn[8][4]; static int g_ndrawn;			// what the last drawing put over the dimmed screen
static void ov_note (int x, int y, int w, int h) { if (g_ndrawn < 8) { int *r = g_drawn[g_ndrawn++]; r[0] = x; r[1] = y; r[2] = w; r[3] = h; } }
static void ov_draw (bool all)
{
	ui_window_use (O.win);
	O.dirty[0] = O.dirty[1] = 1 << 30; O.dirty[2] = O.dirty[3] = -(1 << 30);
	if (all) ov_copy (O.dim, 0, 0, O.W, O.H);
	else for (int i = 0; i < g_ndrawn; i++) ov_copy (O.dim, g_drawn[i][0], g_drawn[i][1], g_drawn[i][2], g_drawn[i][3]);
	g_ndrawn = 0;
	unsigned acc = jet (TJ_ACCENT);
	bool overBar = ov_bar_item (O.mx, O.my) != -1;
	if (O.mode == MODE_RECT)
	{
		if (O.dragging)
		{
			int x0 = O.ax < O.mx ? O.ax : O.mx, y0 = O.ay < O.my ? O.ay : O.my, w = abs (O.mx - O.ax) + 1, h = abs (O.my - O.ay) + 1;
			ov_copy (O.frozen, x0, y0, w, h);
			ui_frame (x0 - 1, y0 - 1, w + 2, h + 2, 0xFFFFFF);
			ui_frame (x0 - 2, y0 - 2, w + 4, h + 4, acc);
			ov_note (x0 - 2, y0 - 2, w + 4, h + 4);
			char t[32]; snprintf (t, sizeof t, "%d \xC3\x97 %d", w, h);
			int tw = text_w (t) + 16, ty = y0 + h + 8 + 22 <= O.H ? y0 + h + 8 : y0 - 30; if (ty < 0) ty = y0 + 6;
			ui_rbox (x0, ty, tw, 22, 6, 0x202024, 0x202024);
			text_c (x0, ty, tw, 22, t, 0xFFFFFF, 13);
			ov_note (x0, ty, tw, 22); ov_mark (x0, ty, tw, 22);
		}
		if (!overBar)						// a magnifier at the pointer
		{
			const int Z = 6, N = 21, M = N * Z;
			int mx0 = O.mx + 24, my0 = O.my + 24;
			if (mx0 + M + 4 > O.W) mx0 = O.mx - 24 - M; if (my0 + M + 28 > O.H) my0 = O.my - 24 - M - 22;
			for (int j = 0; j < N; j++)
				for (int i = 0; i < N; i++)
				{
					int sx = O.mx - N / 2 + i, sy = O.my - N / 2 + j;
					unsigned c = sx >= 0 && sy >= 0 && sx < O.W && sy < O.H ? O.frozen[(long) sy * O.W + sx] : 0;
					ui_fill (mx0 + i * Z, my0 + j * Z, Z, Z, c);
				}
			ui_frame (mx0 + (N / 2) * Z - 1, my0 + (N / 2) * Z - 1, Z + 2, Z + 2, 0xFFFFFF);
			ui_frame (mx0 - 1, my0 - 1, M + 2, M + 2, 0xFFFFFF);
			char t[32]; snprintf (t, sizeof t, "%d, %d", O.mx, O.my);
			ui_fill (mx0 - 1, my0 + M + 1, M + 2, 20, 0x202024);
			text_c (mx0, my0 + M + 1, M, 20, t, 0xFFFFFF, 13);
			ov_note (mx0 - 1, my0 - 1, M + 2, M + 22); ov_mark (mx0 - 1, my0 - 1, M + 2, M + 22);
		}
	}
	else if (O.mode == MODE_WINDOW && O.hot >= 0)
	{
		auto &w = O.wins[O.hot];
		ov_copy (O.frozen, w.x, w.y, w.w, w.h);
		for (int i = 0; i < 3; i++) ui_frame (w.x - 2 - i, w.y - 2 - i, w.w + 4 + 2 * i, w.h + 4 + 2 * i, acc);
		ov_note (w.x - 5, w.y - 5, w.w + 10, w.h + 10); ov_mark (w.x - 5, w.y - 5, w.w + 10, w.h + 10);
		char t[96]; snprintf (t, sizeof t, "%s  -  %d \xC3\x97 %d", w.title[0] ? w.title : TR ("Window"), w.w, w.h);
		int cy = w.y + w.h / 2 - 16; if (cy < 70) cy = 70;
		int tw = text_w (t) + 32;
		ov_pill (w.x + w.w / 2, cy, t, acc);
		ov_note (w.x + w.w / 2 - tw / 2, cy, tw, 32);
	}
	// the bar: the modes, and close
	{
		unsigned face = jet (TJ_SURFACE), ink = jet (TJ_TEXTE1);
		int hot = ov_bar_item (O.mx, O.my);
		ui_rbox (O.barX, O.barY, O.barW, O.barH, 12, face, face);
		int bw = (O.barW - 16 - 44) / 3;
		for (int i = 0; i < 3; i++)
		{
			int bx = O.barX + 8 + i * bw, by = O.barY + 6;
			if (i == O.mode) ui_rbox (bx, by, bw - 4, O.barH - 12, 8, acc, acc);
			else if (i == hot) { unsigned c = mix (face, acc, 50); ui_rbox (bx, by, bw - 4, O.barH - 12, 8, c, c); }
			text_c (bx, by, bw - 4, O.barH - 12, TR (MODE_NAME[i]), i == O.mode ? jet (TJ_ACCENT_INK) : ink, 13);
		}
		int cx = O.barX + O.barW - 40;
		ui_fill (cx - 4, O.barY + 10, 1, O.barH - 20, jet (TJ_LIGNE));
		if (hot == 3) { unsigned c = mix (face, 0xE04030, 70); ui_rbox (cx, O.barY + 6, 34, O.barH - 12, 8, c, c); }
		text_c (cx, O.barY + 6, 34, O.barH - 12, "\xC3\x97", ink, 18);
		ov_mark (O.barX, O.barY, O.barW, O.barH);
	}
	// the hint
	const char *h = O.mode == MODE_RECT ? TR ("Drag a rectangle   -   Enter: the whole screen   -   Esc: cancel")
		      : TR ("Click a window   -   Tab: the next one   -   Esc: cancel");
	ov_pill (O.W / 2, O.H - 110, h, 0x18181C);
	if (O.dirty[2] > O.dirty[0]) ui_update (O.dirty[0], O.dirty[1], O.dirty[2] - O.dirty[0], O.dirty[3] - O.dirty[1]);
	ui_window_use (g_main);
}

static void after_capture ();
static void overlay_close ()
{
	if (O.win) { ui_pointer_lock (0); ui_window_close (O.win); O.win = 0; }
	ui_window_use (g_main);
	delete[] O.frozen; delete[] O.dim; O.frozen = O.dim = 0;
}
// the part chosen (O.rx..) becomes the capture
static void take (int mode)
{
	if (O.rx < 0) { O.rw += O.rx; O.rx = 0; } if (O.ry < 0) { O.rh += O.ry; O.ry = 0; }
	if (O.rx + O.rw > O.W) O.rw = O.W - O.rx; if (O.ry + O.rh > O.H) O.rh = O.H - O.ry;
	if (O.rw > 0 && O.rh > 0)
	{
		strokes_free ();
		delete[] g_base;
		g_base = new uint32_t[(long) O.rw * O.rh]; g_bw = O.rw; g_bh = O.rh;
		for (int y = 0; y < O.rh; y++) memcpy (g_base + (long) y * O.rw, O.frozen + (long) (O.ry + y) * O.W + O.rx, (size_t) O.rw * 4);
		g_crop[0] = g_crop[1] = 0; g_crop[2] = g_bw; g_crop[3] = g_bh;
		if (mode == MODE_WINDOW) snprintf (g_what, sizeof g_what, TR ("Window \xE2\x80\x98%s\xE2\x80\x99"), O.title[0] ? O.title : TR ("untitled"));
		else scpy (g_what, TR (MODE_NAME[mode]), sizeof g_what);
		g_savedAs[0] = 0; g_dirty = true;
		doc_render ();
	}
	overlay_close ();
	after_capture ();
}
static void cancel_choose () { overlay_close (); after_capture (); }

// the window back, as large as the capture wants (within the screen)
static void after_capture ()
{
	g_state = ST_IDLE;
	settings_save ();
	ui_w_set_value (g_ddMode, g_mode);			// (the overlay's bar may have changed it)
	ui_raise (g_main);
	if (has_capture ())
	{
		long scr[4] = { 1024, 768, 0, 0 }; ui_screen ((long) scr);
		int cw = g_dw + 48, ch = 2 * TB_H + g_dh + 48 + ST_H;
		if (cw < 760) cw = 760; if (ch < 400) ch = 400;
		int wx = 20, wy = (int) scr[3] + 20;			// (where the window is: it grows to the right and down)
		unsigned char e[WE_PER_MSG * WE_SIZE];
		for (long first = 0, total = 1; first < total; first += WE_PER_MSG)
		{
			total = ui_win_details (first, (long) e);
			for (int i = 0; i < WE_PER_MSG && first + i < total; i++)
				if (*(const unsigned *) (e + i * WE_SIZE + WE_ID) == (unsigned) g_main)
				{ wx = *(const short *) (e + i * WE_SIZE + WE_X); wy = *(const short *) (e + i * WE_SIZE + WE_Y); }
		}
		int mw = (int) scr[0] - wx - 16, mh = (int) (scr[1] - scr[2]) - wy - 16;
		if (cw > mw) cw = mw; if (ch > mh) ch = mh;
		if (cw > ui.w || ch > ui.h) { ui_resize (cw > ui.w ? cw : ui.w, ch > ui.h ? ch : ui.h); }
		copy_doc (true);
	}
	paint_all ();
}
// the screen taken now (the window minimised, the pointer hidden)
static void grab_now ()
{
	O.frozen = new uint32_t[(long) O.W * O.H];
	bool ok = ui_screen_grab ((long) O.frozen, O.W, O.H) != 0;
	ui_window_use (O.win); ui_pointer_lock (0); ui_window_use (g_main);
	if (!ok)
	{
		overlay_close ();
		after_capture ();
		ui_message ((long) TR ("Screenshot"), (long) TR ("The screen could not be taken: Elegant does not answer."), UM_OK); ui_w_draw (0);
		return;
	}
	for (long i = 0, n = (long) O.W * O.H; i < n; i++) O.frozen[i] &= 0xFFFFFF;
	if (g_pending == MODE_FULL) { O.rx = O.ry = 0; O.rw = O.W; O.rh = O.H; take (MODE_FULL); return; }
	// (the see-through window gives way to an opaque one: Elegant would blend the whole screen at each change)
	ui_window_close (O.win);
	O.win = ui_window_ex (0, 0, O.W, O.H, (long) TR ("Screenshot"), WF_FRAMELESS | WF_TOP | WF_GRAB | WF_NOLIST);
	if (O.win <= 0) { O.win = 0; overlay_close (); after_capture (); return; }
	ui_window_use (g_main);
	O.dim = new uint32_t[(long) O.W * O.H];
	for (long i = 0, n = (long) O.W * O.H; i < n; i++) { unsigned c = O.frozen[i]; O.dim[i] = ((c >> 1) & 0x7F7F7F) - ((c >> 3) & 0x1F1F1F); }
	O.mode = g_pending; O.down = O.dragging = false;
	O.barH = 48; O.barW = 3 * 120 + 16 + 44; O.barX = (O.W - O.barW) / 2; O.barY = 12;
	ov_windows ();
	O.hot = ov_window_at (O.mx, O.my);
	g_ndrawn = 0;
	g_state = ST_CHOOSE;
	ui_window_use (O.win); ui_cursor (O.mode == MODE_RECT ? UC_CROSS : UC_ARROW); ui_window_use (g_main);
	ov_draw (true);
}
static void ov_key (int hid)
{
	if (hid == KEY_ESC) { cancel_choose (); return; }
	if (hid == KEY_ENTER)
	{
		if (O.mode == MODE_WINDOW && O.hot >= 0) { auto &w = O.wins[O.hot]; O.rx = w.x; O.ry = w.y; O.rw = w.w; O.rh = w.h; scpy (O.title, w.title, sizeof O.title); take (MODE_WINDOW); }
		else { O.rx = O.ry = 0; O.rw = O.W; O.rh = O.H; take (MODE_FULL); }
		return;
	}
	if (hid == KEY_TAB && O.mode == MODE_WINDOW && O.nwin) { O.hot = (O.hot + 1) % O.nwin; ov_draw (false); }
}
static void ov_mouse (int kind, int x, int y, int buttons)
{
	O.mx = x; O.my = y;
	if (kind == 1 && (buttons & 2)) { cancel_choose (); return; }	// a right click: cancel
	if (kind == 1 && (buttons & 1))
	{
		int b = ov_bar_item (x, y);
		if (b >= 0 && b < 3)
		{
			if (b == MODE_FULL) { O.rx = O.ry = 0; O.rw = O.W; O.rh = O.H; take (MODE_FULL); return; }
			O.mode = b; g_mode = b; O.hot = ov_window_at (x, y);
			ui_window_use (O.win); ui_cursor (O.mode == MODE_RECT ? UC_CROSS : UC_ARROW); ui_window_use (g_main);
			ov_draw (true);
			return;
		}
		if (b == 3) { cancel_choose (); return; }
		if (b == -1)
		{
			if (O.mode == MODE_RECT) { O.dragging = true; O.ax = x; O.ay = y; }
			else if (O.hot >= 0)
			{
				auto &w = O.wins[O.hot];
				O.rx = w.x; O.ry = w.y; O.rw = w.w; O.rh = w.h; scpy (O.title, w.title, sizeof O.title);
				take (MODE_WINDOW);
				return;
			}
		}
	}
	if (kind == 2 && O.dragging)
	{
		O.dragging = false;
		int x0 = O.ax < x ? O.ax : x, y0 = O.ay < y ? O.ay : y, w = abs (x - O.ax) + 1, h = abs (y - O.ay) + 1;
		if (w >= 4 && h >= 4) { O.rx = x0; O.ry = y0; O.rw = w; O.rh = h; take (MODE_RECT); return; }
	}
	if (O.mode == MODE_WINDOW) O.hot = ov_window_at (x, y);
	ov_draw (false);
}

// New: count down (in the window), then hide, then grab
static void hide_and_take ()
{
	long scr[4] = { 0, 0, 0, 0 }; ui_screen ((long) scr);
	O.W = (int) scr[0]; O.H = (int) scr[1];
	if (O.W <= 0 || O.H <= 0) { g_state = ST_IDLE; refresh_ui (); return; }
	O.mx = O.W / 2; O.my = O.H / 2;				// (the pointer is known at its first move)
	ui_minimize ();
	O.win = ui_window_ex (0, 0, O.W, O.H, (long) TR ("Screenshot"), WF_FRAMELESS | WF_TOP | WF_GRAB | WF_NOLIST | WF_ALPHA);
	if (O.win <= 0) { O.win = 0; ui_window_use (g_main); ui_raise (g_main); g_state = ST_IDLE; refresh_ui (); return; }
	for (long i = 0, n = (long) ui.w * ui.h; i < n; i++) ui.surf[i] = 0xFF000000u;	// (see-through: alpha 255)
	ui_update_all ();
	ui_pointer_lock (1);					// (no pointer in the picture)
	ui_window_use (g_main);
	g_state = ST_HIDE; g_t0 = ms_now ();
}
static void start_capture (int mode)
{
	if (g_state != ST_IDLE) return;
	V.live = 0; V.dragging = false;
	g_pending = mode;
	if (DELAYS[g_delay] > 0) { g_state = ST_COUNT; g_t0 = ms_now (); refresh_ui (); view_dirty (); return; }
	hide_and_take ();
}
static void cancel_countdown () { if (g_state == ST_COUNT) { g_state = ST_IDLE; refresh_ui (); view_dirty (); } }
// -> ms to wait in the loop
static int tick ()
{
	if (g_state == ST_COUNT)
	{
		int el = (int) (ms_now () - g_t0);
		if (el >= DELAYS[g_delay] * 1000) { hide_and_take (); return 20; }
		view_dirty ();
		return 40;
	}
	if (g_state == ST_HIDE)
	{
		if (ms_now () - g_t0 >= 350) grab_now ();		// (Elegant has drawn the screen without us)
		return 20;
	}
	return 500;
}

// ---- the menus, the keys, the controls -----------------------------------------------------------------------------------
enum { C_NEW = 1, C_RECT, C_WINDOW, C_FULL, C_SAVE, C_COPY, C_PEN = 8, C_MARK, C_ERASE, C_CROP, C_UNDO, C_REDO, C_CLEAR };
static void menus ()
{
	ui_menu ((long)
		"MFile\n" "I1\tNew Capture\tCtrl+N\n" "I2\tNew Rectangle\n" "I3\tNew Window\n" "I4\tNew Full Screen\n" "-\n"
		"I5\tSave As...\tCtrl+S\n" "I6\tCopy\tCtrl+C\n"
		"MEdit\n" "I12\tUndo\tCtrl+Z\n" "I13\tRedo\tCtrl+Y\n" "-\n" "I14\tClear the Drawing\n"
		"MTools\n" "I8\tPen\tP\n" "I9\tMarker\tM\n" "I10\tEraser\tE\n" "I11\tCrop\tR\n");
}
static void set_mode (int m) { g_mode = m; ui_w_set_value (g_ddMode, m); settings_save (); }
static void command (int c)
{
	if (g_state != ST_IDLE) return;
	switch (c)
	{
	case C_NEW: start_capture (g_mode); break;
	case C_RECT: set_mode (MODE_RECT); start_capture (g_mode); break;
	case C_WINDOW: set_mode (MODE_WINDOW); start_capture (g_mode); break;
	case C_FULL: set_mode (MODE_FULL); start_capture (g_mode); break;
	case C_SAVE: if (has_capture ()) save_as (); break;
	case C_COPY: if (has_capture ()) { copy_doc (false); refresh_ui (); } break;
	case C_UNDO: undo (); break;
	case C_REDO: redo (); break;
	case C_CLEAR: clear_drawing (); break;
	case C_PEN: if (has_capture ()) set_tool (TOOL_PEN); break;
	case C_MARK: if (has_capture ()) set_tool (TOOL_MARKER); break;
	case C_ERASE: if (has_capture ()) set_tool (TOOL_ERASER); break;
	case C_CROP: if (has_capture ()) set_tool (g_tool == TOOL_CROP ? g_prevTool : TOOL_CROP); break;
	}
}
static bool key (int hid, int ch, int mods)
{
	if (hid == KEY_ESC && g_state == ST_COUNT) { cancel_countdown (); return true; }
	if (hid == KEY_ESC && V.dragging) { V.dragging = false; view_dirty (); return true; }
	if (mods & (UI_MOD_CTRL | UI_MOD_ALT)) return false;
	if (!has_capture () || g_state != ST_IDLE) return false;
	switch (ch)
	{
	case 'p': case 'P': set_tool (TOOL_PEN); return true;
	case 'm': case 'M': set_tool (TOOL_MARKER); return true;
	case 'e': case 'E': set_tool (TOOL_ERASER); return true;
	case 'r': case 'R': set_tool (TOOL_CROP); return true;
	}
	return false;
}
static void control (long w, long code)
{
	if (code != UN_ACTIVATE && code != UN_CHANGE) return;
	if (w == g_bNew) start_capture (g_mode);
	else if (w == g_ddMode) { g_mode = (int) ui_w_get_value (g_ddMode); if (g_mode < 0 || g_mode > 2) g_mode = MODE_RECT; settings_save (); }
	else if (w == g_ddDelay) { g_delay = (int) ui_w_get_value (g_ddDelay) & 3; settings_save (); }
	else if (w == g_bCopy) { copy_doc (false); refresh_ui (); }
	else if (w == g_bSave) save_as ();
	else if (w == g_bPen) set_tool (TOOL_PEN);
	else if (w == g_bMark) set_tool (TOOL_MARKER);
	else if (w == g_bErase) set_tool (TOOL_ERASER);
	else if (w == g_bCrop) set_tool (g_tool == TOOL_CROP ? g_prevTool : TOOL_CROP);
	else if (w == g_bUndo) undo ();
	else if (w == g_bRedo) redo ();
	else if (w == g_cColour && code == UN_CHANGE)
	{
		unsigned c = (unsigned) ui_w_get_value (g_cColour) & 0xFFFFFF;
		if (g_tool == TOOL_MARKER) g_markColour = c; else g_penColour = c;
		settings_save ();
	}
	else if (w == g_ddSize && code == UN_CHANGE)
	{
		int s = (int) ui_w_get_value (g_ddSize) & 3;
		if (g_tool == TOOL_MARKER) g_markSize = s; else g_penSize = s;
		settings_save ();
	}
	if (w != g_cColour && w != g_ddSize && w != g_ddMode && w != g_ddDelay) ui_w_focus (g_view);
}

int main (int argc, char **argv)
{
	if (!uikit_open (&ui)) return 1;
	s_jet = (const unsigned *) ui_jetons ();
	{ char lang[80]; snprintf (lang, sizeof lang, "SD0:/apps/screenshot.app/lang-%s.txt", (const char *) ui_lang ()); ui_tr_load ((long) lang); }
	settings_load ();
	g_main = ui_window_ex (-1, -1, 760, 280, (long) TR ("Screenshot"), WF_RESIZABLE);
	if (g_main <= 0) return 1;
	ui_min_size (720, 240);
	ui_on_resize ((long) on_resize, 0);
	CTL = ctl_h (); TB_H = CTL + 16;

	g_tb = ui_toolbar (0, 0, ui.w, TB_H);
	auto in_bar = [] (long w, long bar) { ui_w_set_parent (w, bar); return w; };
	g_bNew = in_bar (ui_pushbutton (0, 0, 80, CTL, (long) TR ("New"), UB_DEFAULT), g_tb);
	char items[160]; snprintf (items, sizeof items, "%s|%s|%s", TR ("Rectangle"), TR ("Window"), TR ("Full screen"));
	g_ddMode = in_bar (ui_dropdown (0, 0, 140, CTL, (long) items), g_tb);
	snprintf (items, sizeof items, "%s|%s|%s|%s", TR ("No delay"), TR ("3 seconds"), TR ("5 seconds"), TR ("10 seconds"));
	g_ddDelay = in_bar (ui_dropdown (0, 0, 130, CTL, (long) items), g_tb);
	g_bCopy = in_bar (ui_pushbutton (0, 0, 80, CTL, (long) TR ("Copy"), UB_GHOST), g_tb);
	g_bSave = in_bar (ui_pushbutton (0, 0, 100, CTL, (long) TR ("Save As..."), UB_GHOST), g_tb);
	g_tools = ui_toolbar (0, TB_H, ui.w, TB_H);
	g_bPen = in_bar (ui_pushbutton (0, 0, 70, CTL, (long) TR ("Pen"), UB_GHOST), g_tools);
	g_bMark = in_bar (ui_pushbutton (0, 0, 70, CTL, (long) TR ("Marker"), UB_GHOST), g_tools);
	g_bErase = in_bar (ui_pushbutton (0, 0, 70, CTL, (long) TR ("Eraser"), UB_GHOST), g_tools);
	g_bCrop = in_bar (ui_pushbutton (0, 0, 70, CTL, (long) TR ("Crop"), UB_GHOST), g_tools);
	g_cColour = in_bar (ui_colorpicker (0, 0, CTL + 18, CTL, g_penColour), g_tools);
	snprintf (items, sizeof items, "%s|%s|%s|%s", TR ("Thin"), TR ("Medium"), TR ("Thick"), TR ("Very thick"));
	g_ddSize = in_bar (ui_dropdown (0, 0, 120, CTL, (long) items), g_tools);
	g_bUndo = in_bar (ui_pushbutton (0, 0, 70, CTL, (long) TR ("Undo"), UB_GHOST), g_tools);
	g_bRedo = in_bar (ui_pushbutton (0, 0, 70, CTL, (long) TR ("Redo"), UB_GHOST), g_tools);
	ui_w_tip (g_bNew, (long) TR ("New capture (Ctrl+N)"));
	ui_w_tip (g_ddMode, (long) TR ("What to take"));
	ui_w_tip (g_ddDelay, (long) TR ("Wait before taking the screen"));
	ui_w_tip (g_bCopy, (long) TR ("Copy (Ctrl+C)"));
	ui_w_tip (g_bSave, (long) TR ("Save As... (Ctrl+S): PNG, JPEG or BMP"));
	ui_w_tip (g_bPen, (long) TR ("Pen (P)"));
	ui_w_tip (g_bMark, (long) TR ("Marker (M)"));
	ui_w_tip (g_bErase, (long) TR ("Eraser (E): a stroke at a time"));
	ui_w_tip (g_bCrop, (long) TR ("Crop (R)"));
	ui_w_tip (g_bUndo, (long) TR ("Undo (Ctrl+Z)"));
	ui_w_tip (g_bRedo, (long) TR ("Redo (Ctrl+Y)"));
	ui_w_set_value (g_ddMode, g_mode);
	ui_w_set_value (g_ddDelay, g_delay);
	g_view = ui_view (0, TB_H, ui.w, ui.h - TB_H, UVW_FOCUS);
	ui_view_on (g_view, (long) view_paint, (long) view_mouse, 0, 0);
	menus ();
	paint_all ();
	ui_w_focus (g_view);

	// "screenshot --now": a capture at once, with the last mode and delay; "--rect", "--window", "--full": that mode
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "--rect")) set_mode (MODE_RECT);
		else if (!strcmp (argv[i], "--window")) set_mode (MODE_WINDOW);
		else if (!strcmp (argv[i], "--full")) set_mode (MODE_FULL);
		else if (strcmp (argv[i], "--now")) continue;
		start_capture (g_mode);
		break;
	}

	for (;;)
	{
		long t = ui_event (tick ());
		if (t == WEV_CLOSE && ui_ev_field (&ui, EV_WIN) == g_main) break;
		if (g_state == ST_CHOOSE && t > 0 && O.win && ui_ev_field (&ui, EV_WIN) == O.win)
		{
			if (t == WEV_MOUSE) ov_mouse (ui_ev_field (&ui, EV_KIND), ui_ev_field (&ui, EV_X), ui_ev_field (&ui, EV_Y), ui_ev_field (&ui, EV_CODE));
			else if (t == WEV_KEY && ui_ev_field (&ui, EV_KIND)) ov_key (ui_ev_field (&ui, EV_CODE));
			continue;
		}
		if (O.win && ui_ev_field (&ui, EV_WIN) == O.win) continue;	// (the see-through window, while the screen is taken)
		if (t == WEV_MENU) command (ui_ev_field (&ui, EV_CODE));
		else if (t == WEV_THEME) paint_all ();
		else if (t > 0)
		{
			bool press = t == WEV_KEY && ui_ev_field (&ui, EV_KIND) != 0;
			int hid = ui_ev_field (&ui, EV_CODE), ch = ui_ev_field (&ui, EV_CHAR), mods = ui_ev_field (&ui, EV_MODS);
			long code = 0, taken = 0, wd = uikit_w_event (&code, &taken);
			if (code) control (wd, code);
			if (press && !taken && !key (hid, ch, mods)) { long id = ui_menu_key (0); if (id >= 0) command ((int) id); }
		}
		else ui_w_idle ();
		ui_view_flush ();
	}
	overlay_close ();
	return 0;
}
