//
// pview.h -- Paint's canvas. The layers are composited by the CPU into one buffer (premultiplied; only the rectangles that
// changed are made again, each layer with its opacity and blend mode; a hidden layer is left out, a layer under a clip mask
// multiplied by the mask's alpha), shown at the zoom (the nearest pixel from 100 %, bilinear below) over the checkerboard, on
// the theme's sunken tone.
//
// Over it: the pixel grid (the Grid toggle, from 300 %), the selection's marching ants, the brush's
// reach, the text box and its caret, the gradient's line and its ends.
//
// The tools -- Select (a rectangle, free-form, the magic wand; Shift adds, Alt takes away; dragged
// inside, the pixels lift and move), Pencil, Brush (its kinds: pbrush.h), Eraser, Fill (a colour, a
// pattern, or a gradient along the line dragged from the press), Text, Colour picker, Magnifier (a drag
// moves the picture: the finger's and the pen's way), Gradient (over the selection or the layer; or
// fading the layer), the Shapes. The left button uses the chosen colour (the only one a finger has),
// the right one the other. Ctrl + wheel zooms around the pointer, the wheel scrolls (Shift: sideways),
// the middle button pans.
//
// MIT licence (Onyx).
//
#ifndef _paint_pview_h
#define _paint_pview_h

#include "plook.h"
#include "raster.h"
#include "pbrush.h"
#include "pgrad.h"
#include "ptext.h"

namespace pd {

enum { T_SELECT, T_PENCIL, T_BRUSH, T_ERASER, T_FILL, T_PICKER, T_ZOOM, T_SHAPE, T_TEXT, T_GRADIENT, T_COUNT };
static const char *const TOOL_NAMES[T_COUNT] = { "Select", "Pencil", "Brush", "Eraser", "Fill", "Colour picker", "Magnifier", "Shapes", "Text", "Gradient" };
enum { SK_RECT, SK_FREE, SK_WAND };
enum { FM_COLOUR, FM_GRADIENT, FM_PATTERN };
static int g_tool = T_BRUSH, g_prevTool = T_BRUSH, g_selKind = SK_RECT;
static int g_activeCol = 1;					// the colour the left button draws with (the palette sets it)
static int g_shape = SH_RECT;
static int g_sizes[T_COUNT] = { 1, 1, 12, 16, 1, 1, 1, 2, 1, 1 };	// each tool's width
static int g_opac[T_COUNT] = { 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };	// ... its opacity, %
static int g_hard = 40, g_eraseHard = 100;		// the soft brush's / the eraser's hardness, %
static int g_brush = BR_BRUSH, g_pattern = BR_DOTS;
static bool g_outline = true, g_fillShape = false;
static unsigned g_col1 = 0xFF000000u, g_col2 = 0xFFFFFFFFu;
static bool g_grid;
static int g_fillMode = FM_COLOUR, g_tol = 32, g_selMode = SEL_REPLACE;
static bool g_contig = true, g_allLayers = false, g_gradErase = false;
static void (*g_changed) ();					// the app's: something to show again (status, layers)
static void (*g_toolChanged) ();				// ... the tool changed (the picker's end)

static const int ZOOMS[] = { 12, 25, 33, 50, 67, 100, 150, 200, 300, 400, 600, 800, 1200, 1600, 2400, 3200 };
enum { NZOOMS = sizeof ZOOMS / sizeof ZOOMS[0], MARGIN = 32, SBW = 14 };
#define DESK jet (J_CREUX)				// the desk around the picture

static inline int fdiv (int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static void (*g_statusChanged) ();				// ... the pointer moved: the status bar alone
static void notify () { if (g_statusChanged) g_statusChanged (); }
static void notify_all () { if (g_changed) g_changed (); }
// What a pixel becomes when it is cleared (moved away, deleted): on an opaque bottom layer colour 2
// (as classic Paint); elsewhere, transparent.
static inline unsigned cleared (unsigned old) { return D.cur == 0 && (old >> 24) == 255 ? g_col2 : 0; }
static bool bottom_opaque ()
{
	if (D.cur != 0) return false;
	for (int i = 0; i < D.w * D.h; i++) if ((D.lay[0].px[i] >> 24) != 255) return false;
	return true;
}

// ---- the overlay: put down on the current layer ----------------------------------------------------------
static void ov_clear ()
{
	Rect r = D.ov.r;
	for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) D.ov.px[(unsigned) y * D.w + x] = 0;
	D.ov.r = norect (); D.ov.eraser = false;
	compose (r);
}
static void ov_commit ()
{
	Rect r = D.ov.r;
	if (r.empty ()) return;
	rec_begin (); rec_touch (r);
	unsigned *L = D.lay[D.cur].px;
	for (int y = r.y0; y < r.y1; y++)
		for (int x = r.x0; x < r.x1; x++)
		{
			unsigned o = D.ov.px[(unsigned) y * D.w + x];
			if (!(o >> 24)) continue;
			unsigned &p = L[(unsigned) y * D.w + x];
			if (D.ov.eraser) { unsigned a = ((p >> 24) * (255 - (o >> 24)) + 127) / 255; p = a ? (p & 0xFFFFFF) | a << 24 : 0; }
			else p = over (p, o, 255);
		}
	rec_end ();
	ov_clear ();
}

// ---- the floating selection -----------------------------------------------------------------------------
static Rect float_rect () { return mkrect (D.fl.x, D.fl.y, D.fl.x + D.fl.w, D.fl.y + D.fl.h); }
// The selection's pixels of the current layer lifted (they float; the layer is clear there).
static bool float_lift ()
{
	if (D.fl.px) return true;
	Rect r = g_sel; r.clip (D.w, D.h);
	if (r.empty ()) return false;
	rec_begin (); rec_touch (r);
	D.fl.w = r.x1 - r.x0; D.fl.h = r.y1 - r.y0; D.fl.x = r.x0; D.fl.y = r.y0;
	D.fl.px = new unsigned[(unsigned) D.fl.w * D.fl.h];
	unsigned *L = D.lay[D.cur].px;
	bool keepCol = D.cur == 0 && bottom_opaque ();
	for (int y = 0; y < D.fl.h; y++)
		for (int x = 0; x < D.fl.w; x++)
		{
			unsigned &p = L[(unsigned) (r.y0 + y) * D.w + r.x0 + x];
			int m = sel_at (r.x0 + x, r.y0 + y);
			unsigned &f = D.fl.px[(unsigned) y * D.fl.w + x];
			if (m == 255) { f = p; p = keepCol ? g_col2 : 0; }
			else if (m == 0) f = 0;
			else
			{
				unsigned a = p >> 24;
				f = (p & 0xFFFFFF) | ((a * (unsigned) m + 127) / 255) << 24;
				p = keepCol ? over (p, g_col2, (unsigned) m) : (p & 0xFFFFFF) | ((a * (unsigned) (255 - m) + 127) / 255) << 24;
			}
		}
	sel_rect (r);
	compose (r);
	return true;
}
// Put the floating pixels down on the current layer (over what is there); keepSel: still selected.
static void float_commit (bool keepSel)
{
	if (!D.fl.px) return;
	Rect r = float_rect (), rc = r;
	rc.clip (D.w, D.h);
	if (!rc.empty ())
	{
		rec_touch (rc);
		unsigned *L = D.lay[D.cur].px;
		for (int y = rc.y0; y < rc.y1; y++)
			for (int x = rc.x0; x < rc.x1; x++)
			{
				unsigned f = D.fl.px[(unsigned) (y - D.fl.y) * D.fl.w + (x - D.fl.x)];
				if (f >> 24) { unsigned &p = L[(unsigned) y * D.w + x]; p = (f >> 24) == 255 ? f : over (p, f, 255); }
			}
	}
	delete[] D.fl.px; D.fl.px = 0;
	rec_end ();
	compose (r);
	if (keepSel) sel_rect (rc); else sel_clear ();
}
static void float_discard ()
{
	if (!D.fl.px) return;
	Rect r = float_rect ();
	delete[] D.fl.px; D.fl.px = 0;
	rec_end ();
	compose (r);
	sel_clear ();
}
// New floating pixels (a paste, a text) at (x, y): the operation's own (put down by float_commit).
static void float_new (unsigned *px, int w, int h, int x, int y)
{
	float_commit (false);
	D.fl.px = px; D.fl.w = w; D.fl.h = h; D.fl.x = x; D.fl.y = y;
	rec_begin ();
	sel_rect (float_rect ());
	g_sel = float_rect ();				// (its rectangle, even past the picture's edges)
	compose (g_sel);
}

// ---- a gradient being placed (the Fill tool's gradient mode, the Gradient tool) -------------------------------
struct GradJob
{
	bool on;
	unsigned char *mask;			// the zone (D.w x D.h: the fill's region), 0: the selection / the layer
	Rect box;				// the zone's bounds
	float ax, ay, bx, by;			// the line (picture coordinates)
	bool erase;				// the layer faded along it (the Gradient tool's Erase)
	int opacity;				// 0..255
};
static GradJob g_gj;
static void grad_preview ()
{
	ov_clear ();
	if (!g_gj.on) return;
	Gradient &g = g_grads[pclamp (g_grad, 0, g_ngrads - 1)];
	grad_dyn (g, g_col1, g_col2);
	Rect b = g_gj.box;
	for (int y = b.y0; y < b.y1; y++)
		for (int x = b.x0; x < b.x1; x++)
		{
			int m = g_gj.mask ? g_gj.mask[(unsigned) y * D.w + x] : 255;
			m = m * sel_at (x, y) / 255;
			if (!m) continue;
			float t = grad_t (x + 0.5f, y + 0.5f, g_gj.ax, g_gj.ay, g_gj.bx, g_gj.by);
			unsigned c;
			if (g_gj.erase) c = (unsigned) (255 - (int) (t * 255 + 0.5f)) << 24;	// (opaque at A: kept, clear at B: erased)
			else c = grad_at (g, t);
			unsigned a = (c >> 24) * (unsigned) m / 255 * (unsigned) g_gj.opacity / 255;
			if (g_gj.erase) a = 255 - a, a = a * (unsigned) m / 255;
			D.ov.px[(unsigned) y * D.w + x] = a ? (c & 0xFFFFFF) | a << 24 : 0;
		}
	D.ov.r = b; D.ov.eraser = g_gj.erase;
	compose (b);
}
static void grad_end (bool apply)
{
	if (!g_gj.on) return;
	if (apply) ov_commit (); else ov_clear ();
	delete[] g_gj.mask; g_gj.mask = 0; g_gj.on = false;
}

// ---- the text box -----------------------------------------------------------------------------------------
static void text_preview ()
{
	ov_clear ();
	if (!g_text.on) return;
	Rect r = norect ();
	text_layout (D.ov.px, &r, g_col1, g_col2, 0);
	// (the selection keeps the text inside it, as every drawing)
	for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++)
	{
		unsigned &o = D.ov.px[(unsigned) y * D.w + x]; int s = sel_at (x, y);
		if (s < 255) o = (o & 0xFFFFFF) | ((o >> 24) * (unsigned) s / 255) << 24;
	}
	D.ov.r = r; D.ov.eraser = false;
	compose (r);
}
static void text_end (bool apply)
{
	if (!g_text.on) return;
	if (apply && g_text.n) ov_commit (); else ov_clear ();
	g_text.on = false;
}

// Everything pending put down (before an undo, a change of layer, of tool...).
static void settle () { float_commit (false); grad_end (true); text_end (true); }

// ---- the composite ---------------------------------------------------------------------------------------------
static unsigned *g_comp; static int g_cw, g_ch;		// the picture as shown, premultiplied
// The rectangles that changed made again.
static void comp_sync ()
{
	if (!g_comp || g_cw != D.w || g_ch != D.h)
	{
		delete[] g_comp;
		g_comp = new unsigned[(unsigned) D.w * D.h]; g_cw = D.w; g_ch = D.h;
		D.dirtyAll = true;
	}
	Rect r = D.dirtyAll ? mkrect (0, 0, D.w, D.h) : D.dirty;
	D.dirtyAll = false; D.dirty = norect ();
	r.clip (D.w, D.h);
	if (r.empty ()) return;
	const Layer &l0 = D.lay[0];
	bool plain = D.n == 1 && l0.visible && l0.opacity == 255 && l0.blend == B_NORMAL && !D.fl.px && D.ov.r.empty ();
	for (int y = r.y0; y < r.y1; y++)
	{
		unsigned *o = g_comp + (unsigned) y * D.w;
		if (plain) { const unsigned *p = l0.px + (unsigned) y * D.w; for (int x = r.x0; x < r.x1; x++) o[x] = premul (p[x]); }
		else for (int x = r.x0; x < r.x1; x++) o[x] = comp_pre (x, y, true);
	}
}
static inline unsigned lerp4 (unsigned a, unsigned b, unsigned c, unsigned d, unsigned fx, unsigned fy)
{
	if (a == b && a == c && a == d) return a;
	unsigned rb0 = ((a & 0x00FF00FF) * (256 - fx) + (b & 0x00FF00FF) * fx) >> 8 & 0x00FF00FF;
	unsigned ag0 = (((a >> 8) & 0x00FF00FF) * (256 - fx) + ((b >> 8) & 0x00FF00FF) * fx) >> 8 & 0x00FF00FF;
	unsigned rb1 = ((c & 0x00FF00FF) * (256 - fx) + (d & 0x00FF00FF) * fx) >> 8 & 0x00FF00FF;
	unsigned ag1 = (((c >> 8) & 0x00FF00FF) * (256 - fx) + ((d >> 8) & 0x00FF00FF) * fx) >> 8 & 0x00FF00FF;
	unsigned rb = (rb0 * (256 - fy) + rb1 * fy + 0x00800080) >> 8 & 0x00FF00FF;
	unsigned ag = (ag0 * (256 - fy) + ag1 * fy + 0x00800080) & 0xFF00FF00;
	return rb | ag;
}

// ---- the canvas ----------------------------------------------------------------------------------------
class CanvasView
{
public:
	long view; int width, height;				// its ui_view
	int zoom, sx, sy;					// %, scrolled (px)
	int ptrX, ptrY;						// the pointer's pixel (-1: outside)
	CanvasView () : view (0), width (100), height (100), zoom (100), sx (0), sy (0), ptrX (-1), ptrY (-1),
		m_btn (0), m_drag (0), m_ax (0), m_ay (0), m_lx (0), m_ly (0), m_barHot (0), m_hoverX (-1), m_hoverY (-1),
		m_vx (0), m_vy (0), m_panned (false), m_lasso (0), m_nl (0), m_capl (0), m_dragSel (norect ()) {}
	void invalidate () { if (view) ui_view_dirty (view); }
	void setFocus () { ui_w_focus (view); }

	int viewW () const { return width - SBW; }
	int viewH () const { return height - SBW; }
	int zw () const { return (int) ((long long) D.w * zoom / 100); }
	int zh () const { return (int) ((long long) D.h * zoom / 100); }
	int docW () const { return zw () + 2 * MARGIN; }
	int docH () const { return zh () + 2 * MARGIN; }
	int ox () const { return docW () <= viewW () ? (viewW () - zw ()) / 2 : MARGIN - sx; }
	int oy () const { return docH () <= viewH () ? (viewH () - zh ()) / 2 : MARGIN - sy; }
	int toX (int ix) const { return ox () + (int) ((long long) ix * zoom / 100); }
	int toY (int iy) const { return oy () + (int) ((long long) iy * zoom / 100); }
	int imX (int vx) const { return fdiv ((vx - ox ()) * 100, zoom); }
	int imY (int vy) const { return fdiv ((vy - oy ()) * 100, zoom); }
	float fimX (int vx) const { return (vx + 0.5f - ox ()) * 100.0f / zoom; }
	float fimY (int vy) const { return (vy + 0.5f - oy ()) * 100.0f / zoom; }
	void clampScroll () { sx = pclamp (sx, 0, pmax (0, docW () - viewW ())); sy = pclamp (sy, 0, pmax (0, docH () - viewH ())); }

	// The zoom changed, the pixel under (vx, vy) of the view kept there.
	void setZoom (int z, int vx = -1, int vy = -1)
	{
		z = pclamp (z, ZOOMS[0], ZOOMS[NZOOMS - 1]);
		if (vx < 0) { vx = viewW () / 2; vy = viewH () / 2; }
		float ix = fimX (vx), iy = fimY (vy);
		zoom = z;
		sx = 0; sy = 0;
		if (docW () > viewW ()) sx = MARGIN + (int) (ix * zoom / 100) - vx;
		if (docH () > viewH ()) sy = MARGIN + (int) (iy * zoom / 100) - vy;
		clampScroll ();
		invalidate (); notify ();
	}
	void zoomStep (int dir, int vx = -1, int vy = -1)
	{
		int k = 0; while (k < NZOOMS - 1 && ZOOMS[k] < zoom) k++;
		if (dir > 0) { while (k < NZOOMS - 1 && ZOOMS[k] <= zoom) k++; }
		else { while (k > 0 && ZOOMS[k] >= zoom) k--; }
		setZoom (ZOOMS[k], vx, vy);
	}
	void zoomFit ()
	{
		int zx = (viewW () - 2 * MARGIN) * 100 / pmax (1, D.w), zy = (viewH () - 2 * MARGIN) * 100 / pmax (1, D.h);
		int z = pmin (zx, zy), best = ZOOMS[0];
		for (int i = 0; i < NZOOMS; i++) if (ZOOMS[i] <= z) best = ZOOMS[i];
		setZoom (pmin (best, 100));
	}

	// ---- drawing ----
	void paint ()
	{
		begin_paint (view);
		int vw = viewW (), vh = viewH ();
		fill (0, 0, width, height, DESK);
		int X0 = ox (), Y0 = oy ();
		int ix0 = pmax (0, X0), iy0 = pmax (0, Y0), ix1 = pmin (vw, X0 + zw ()), iy1 = pmin (vh, Y0 + zh ());
		shadow (X0, Y0, zw (), zh ());
		if (ix1 > ix0 && iy1 > iy0)
		{
			composite (ix0, iy0, ix1, iy1);
			if (g_grid && zoom >= 300) drawGrid (ix0, iy0, ix1, iy1);
		}
		drawSelection ();
		drawToolMarks ();
		drawBars ();
	}

	// ---- the mouse ----
	void wheel (int w, unsigned mods)
	{
		int mx = m_hoverX >= 0 ? m_hoverX : viewW () / 2, my = m_hoverX >= 0 ? m_hoverY : viewH () / 2;
		if (mods & UI_MOD_CTRL) zoomStep (w > 0 ? 1 : -1, mx, my);
		else if (mods & UI_MOD_SHIFT) { sx -= w * 48; clampScroll (); invalidate (); }
		else { sy -= w * 48; clampScroll (); invalidate (); }
		notify ();
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, unsigned mods)
	{
		if (mx < 0 && !m_btn && !m_drag)
		{
			if (ptrX != -1 || m_hoverX != -1) { ptrX = ptrY = -1; m_hoverX = -1; invalidate (); notify (); }
			if (m_barHot) { m_barHot = 0; invalidate (); }
			return false;
		}
		int btn = bl ? 1 : br ? 2 : 0;
		int hot = mx >= viewW () && my < viewH () ? 1 : my >= viewH () && mx < viewW () ? 2 : 0;
		if (!m_btn && !m_drag && hot != m_barHot) { m_barHot = hot; invalidate (); }
		if (m_drag == 1 || m_drag == 2)
		{
			if (m_drag == 1) sy = (int) thumb_pos (my, viewH (), docH (), viewH (), thumb_of (docH (), viewH (), sy, viewH ()).h);
			else sx = (int) thumb_pos (mx, viewW (), docW (), viewW (), thumb_of (docW (), viewW (), sx, viewW ()).h);
			clampScroll (); invalidate ();
			if (!btn) m_drag = 0;
			return true;
		}
		if (btn && !m_btn && hot) { m_drag = hot; return onMouse (mx, my, bl, br, bm, mods); }
		if (m_drag == 3)						// (a pan: the middle button)
		{
			sx -= mx - m_lx; sy -= my - m_ly; m_lx = mx; m_ly = my; clampScroll (); invalidate ();
			if (!bm) m_drag = 0;
			return true;
		}
		if (bm && !m_btn) { m_drag = 3; m_lx = mx; m_ly = my; return true; }
		int ix = imX (mx), iy = imY (my);
		bool inside = ix >= 0 && iy >= 0 && ix < D.w && iy < D.h;
		int px2 = inside ? ix : -1, py2 = inside ? iy : -1;
		if (px2 != ptrX || py2 != ptrY) { ptrX = px2; ptrY = py2; notify (); }
		if (mx != m_hoverX || my != m_hoverY) { m_hoverX = mx; m_hoverY = my; if (g_tool == T_ERASER || g_tool == T_BRUSH || g_tool == T_PENCIL) invalidate (); }
		m_fx = fimX (mx); m_fy = fimY (my);
		if (btn && !m_btn) { setFocus (); m_btn = btn; m_vx = mx; m_vy = my; m_panned = false; press (ix, iy, btn, mods); }
		else if (m_btn && btn)
		{
			if (g_tool == T_ZOOM)					// (a drag with the magnifier: the picture moved)
			{
				if (m_panned || mx - m_vx > 6 || m_vx - mx > 6 || my - m_vy > 6 || m_vy - my > 6)
				{
					sx -= mx - m_vx; sy -= my - m_vy; m_vx = mx; m_vy = my; m_panned = true;
					clampScroll (); invalidate ();
				}
			}
			else drag (ix, iy, mods);
		}
		else if (m_btn && !btn) { drag (ix, iy, mods); release (ix, iy, mods); m_btn = 0; }
		return true;
	}

	// ---- the keys ----
	bool onKey (int k, unsigned ch, unsigned mods)
	{
		if (g_text.on && !(mods & UI_MOD_CTRL))			// (typing a text)
		{
			if (ch >= 32 && ch != 127) { char u[4]; int n = u8_put (ch, u); text_insert (u, n); }
			else if (k == KEY_ENTER) text_insert ("\n", 1);
			else if (k == KEY_BACKSPACE) text_back ();
			else if (k == KEY_DELETE) text_del ();
			else if (k == KEY_LEFT) g_text.caret = u8_prev (g_text.s, g_text.caret);
			else if (k == KEY_RIGHT) g_text.caret = u8_next (g_text.s, g_text.caret, g_text.n);
			else if (k == KEY_HOME) { while (g_text.caret > 0 && g_text.s[g_text.caret - 1] != '\n') g_text.caret--; }
			else if (k == KEY_END) { while (g_text.caret < g_text.n && g_text.s[g_text.caret] != '\n') g_text.caret++; }
			else if (k == KEY_ESC) { text_end (false); invalidate (); notify (); return true; }
			else return false;
			text_preview (); invalidate (); notify ();
			return true;
		}
		if (g_gj.on && (k == KEY_ENTER || k == KEY_ESC)) { grad_end (k == KEY_ENTER); invalidate (); notify (); return true; }
		int step = mods & UI_MOD_SHIFT ? 10 : 1;
		if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN)
		{
			if (has_sel () || D.fl.px)				// (the selection nudged)
			{
				if (!D.fl.px && !float_lift ()) return true;
				Rect old = float_rect ();
				D.fl.x += k == KEY_LEFT ? -step : k == KEY_RIGHT ? step : 0;
				D.fl.y += k == KEY_UP ? -step : k == KEY_DOWN ? step : 0;
				old.add (float_rect ()); compose (old); g_sel = float_rect ();
			}
			else { sx += (k == KEY_LEFT ? -40 : k == KEY_RIGHT ? 40 : 0); sy += (k == KEY_UP ? -40 : k == KEY_DOWN ? 40 : 0); clampScroll (); }
			invalidate (); notify ();
			return true;
		}
		if (k == KEY_DELETE || k == KEY_BACKSPACE) { deleteSelection (); return true; }
		if (k == KEY_ESC) { settle (); sel_clear (); invalidate (); notify (); return true; }
		if (k == KEY_PGUP || k == KEY_PGDN) { sy += (k == KEY_PGUP ? -1 : 1) * (viewH () - 40); clampScroll (); invalidate (); return true; }
		return false;
	}

	// The selection's pixels cleared (or the floating ones dropped).
	void deleteSelection ()
	{
		if (D.fl.px) { float_discard (); invalidate (); notify (); return; }
		Rect r = g_sel; r.clip (D.w, D.h);
		if (r.empty ()) return;
		rec_begin (); rec_touch (r);
		bool keepCol = bottom_opaque ();
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++)
		{
			int m = sel_at (x, y); if (!m) continue;
			unsigned &q = D.lay[D.cur].px[(unsigned) y * D.w + x];
			if (keepCol) q = over (q, g_col2, (unsigned) m);
			else { unsigned a = ((q >> 24) * (unsigned) (255 - m) + 127) / 255; q = a ? (q & 0xFFFFFF) | a << 24 : 0; }
		}
		rec_end (); compose (r);
		invalidate (); notify ();
	}

private:
	int m_btn, m_drag;					// the button held (1 left, 2 right); 1 / 2 a bar's thumb, 3 a pan
	int m_ax, m_ay, m_lx, m_ly;				// the press's pixel, the last one
	int m_barHot, m_hoverX, m_hoverY;
	float m_fx, m_fy;					// the pointer in the picture (fractions)
	int m_mode;						// what the press started (per tool)
	int m_fx0, m_fy0;					// a floater's place at the press
	int m_vx, m_vy; bool m_panned;				// a magnifier's drag: the last point, moved
	int *m_lasso, m_nl, m_capl;				// the free-form selection's points (1/16 px)
	Rect m_dragSel;						// a rectangle being dragged (Select)
	Stroke m_stroke;
	int m_cursorMx, m_cursorMy;

	unsigned drawColour (int btn) const { return (btn == 2) != (g_activeCol == 2) ? g_col2 : g_col1; }
	int selModeFor (unsigned mods) const { return mods & UI_MOD_SHIFT ? SEL_ADD : mods & UI_MOD_ALT ? SEL_SUB : g_selMode; }

	// ---- the composite ----
	void composite (int ix0, int iy0, int ix1, int iy1)
	{
		comp_sync ();
		int X0 = ox (), Y0 = oy (), n = ix1 - ix0;
		static int *cols; static int ccap;
		if (ccap < 2 * n) { delete[] cols; ccap = 2 * n + 64; cols = new int[ccap]; }
		bool near = zoom >= 100;
		for (int i = 0; i < n; i++)
		{
			long long u = ((long long) (ix0 + i - X0) * 65536 + 32768) * 100 / zoom;
			if (near) cols[i] = pclamp ((int) (u >> 16), 0, D.w - 1);
			else cols[i] = (int) (u - 32768 > 0 ? u - 32768 : 0);	// (bilinear: 16.16 from the first pixel's centre)
		}
		for (int y = iy0; y < iy1; y++)
		{
			long long v = ((long long) (y - Y0) * 65536 + 32768) * 100 / zoom;
			uint32_t *d = g_px + (long) y * g_pw;
			int cy = ((y - Y0) >> 3) & 1;
			const unsigned *s0 = g_comp + (unsigned) pclamp ((int) (v >> 16), 0, D.h - 1) * D.w, *s1 = s0;
			unsigned fy = 0;
			if (!near)
			{
				long long vv = v - 32768 > 0 ? v - 32768 : 0;
				int r0 = pclamp ((int) (vv >> 16), 0, D.h - 1), r1 = pmin (r0 + 1, D.h - 1);
				s0 = g_comp + (unsigned) r0 * D.w; s1 = g_comp + (unsigned) r1 * D.w; fy = (unsigned) (vv >> 8) & 255;
			}
			for (int x = ix0; x < ix1; x++)
			{
				unsigned c;
				if (near) c = s0[cols[x - ix0]];
				else
				{
					int u = cols[x - ix0], x0 = pclamp (u >> 16, 0, D.w - 1), x1 = pmin (x0 + 1, D.w - 1);
					c = lerp4 (s0[x0], s0[x1], s1[x0], s1[x1], ((unsigned) u >> 8) & 255, fy);
				}
				unsigned a = c >> 24;
				if (a == 255) { d[x] = c & 0xFFFFFF; continue; }
				unsigned ck = ((((x - X0) >> 3) & 1) ^ cy) ? 0xD6D6D6 : 0xFFFFFF, ia = 255 - a;
				unsigned r = ((c >> 16) & 255) + (((ck >> 16) & 255) * ia + 127) / 255, gg = ((c >> 8) & 255) + (((ck >> 8) & 255) * ia + 127) / 255, b = (c & 255) + ((ck & 255) * ia + 127) / 255;
				d[x] = pmin (255u, r) << 16 | pmin (255u, gg) << 8 | pmin (255u, b);
			}
		}
	}
	void shadow (int x, int y, int w, int h)
	{
		for (int i = 1; i <= 6; i++)
		{
			unsigned c = mix (DESK, 0x000000, 40 - i * 6);
			fillClip (x - i + 2, y + h + i - 1, w + 2 * i - 2, 1, c);
			fillClip (x + w + i - 1, y - i + 4, 1, h + 2 * i - 4, c);
		}
	}

	// ---- the marks over the picture ----
	void drawSelection ()
	{
		if (D.fl.px) { Rect s = float_rect (); dashed (toX (s.x0) - 1, toY (s.y0) - 1, toX (s.x1), toY (s.y1)); }
		else if (m_dragSel.x1 > m_dragSel.x0) { Rect s = m_dragSel; dashed (toX (s.x0) - 1, toY (s.y0) - 1, toX (s.x1), toY (s.y1)); }
		else if (has_sel () && !g_mask) { Rect s = g_sel; dashed (toX (s.x0) - 1, toY (s.y0) - 1, toX (s.x1), toY (s.y1)); }
		else if (has_sel ()) ants ();
		if (m_nl > 1)						// (the lasso being drawn)
			for (int i = 0; i + 1 < m_nl; i++) segment (toX (0) + m_lasso[2 * i] * zoom / 1600, toY (0) + m_lasso[2 * i + 1] * zoom / 1600,
								     toX (0) + m_lasso[2 * i + 2] * zoom / 1600, toY (0) + m_lasso[2 * i + 3] * zoom / 1600);
	}
	// The mask's edges: the view pixels whose picture pixel is selected and a neighbour's not.
	void ants ()
	{
		int vw = viewW (), vh = viewH ();
		int x0 = pmax (0, toX (g_sel.x0) - 1), y0 = pmax (0, toY (g_sel.y0) - 1), x1 = pmin (vw, toX (g_sel.x1) + 1), y1 = pmin (vh, toY (g_sel.y1) + 1);
		auto in = [&] (int vx, int vy) { int a = imX (vx), b = imY (vy); return a >= 0 && b >= 0 && a < D.w && b < D.h && sel_at (a, b) >= 128; };
		for (int y = y0; y < y1; y++)
			for (int x = x0; x < x1; x++)
			{
				bool s = in (x, y);
				if (s == in (x + 1, y) && s == in (x, y + 1) && s == in (x - 1, y) && s == in (x, y - 1)) continue;
				if (!s) continue;
				g_px[y * g_pw + x] = ((x + y) >> 2) & 1 ? 0xFFFFFF : 0x000000;
			}
	}
	void drawToolMarks ()
	{
		// the brush's reach under the pointer
		if (!m_btn && m_hoverX >= 0 && m_hoverX < viewW () && m_hoverY < viewH () && (g_tool == T_BRUSH || g_tool == T_ERASER || (g_tool == T_PENCIL && g_sizes[T_PENCIL] > 2)))
		{
			int r = g_sizes[g_tool] * zoom / 200;
			if (r >= 2) { ring (m_hoverX, m_hoverY, r, 0x000000); ring (m_hoverX, m_hoverY, r + 1, 0xFFFFFF); }
		}
		// the text box and its caret
		if (g_text.on)
		{
			int c[3]; text_layout (0, 0, 0, 0, c);
			int bx0 = toX (g_text.x) - 3, by0 = toY (g_text.y) - 3, bx1 = toX (g_text.x + g_text.w) + 3, by1 = toY (g_text.y + g_text.h) + 3;
			dashed (bx0, by0, bx1, by1);
			if (c[0] >= 0) { int cx = toX (c[0]); fillClip (cx, toY (c[1]), 2, pmax (2, toY (c[2]) - toY (c[1])), 0x000000); fillClip (cx + 2, toY (c[1]), 1, pmax (2, toY (c[2]) - toY (c[1])), 0xFFFFFF); }
		}
		// the gradient's line, its ends
		if (g_gj.on)
		{
			int ax = ox () + (int) (g_gj.ax * zoom / 100), ay = oy () + (int) (g_gj.ay * zoom / 100);
			int bx = ox () + (int) (g_gj.bx * zoom / 100), by = oy () + (int) (g_gj.by * zoom / 100);
			ui_vp_line (V (ax), V (ay), V (bx), V (by), 48, 0x000000);
			ui_vp_line (V (ax), V (ay), V (bx), V (by), 22, 0xFFFFFF);
			for (int e = 0; e < 2; e++)
			{
				int hx = e ? bx : ax, hy = e ? by : ay;
				disc (hx, hy, 9, 0x000000); disc (hx, hy, 8, 0xFFFFFF);		// (a bigger end: the finger's)
			}
			const char *tip = TR ("Drag the ends to adjust   -   Enter: apply   -   Esc: cancel");
			int tiw = tw (tip) + 24, th = TLH + 8, tx = (viewW () - tiw) / 2, ty = viewH () - th - 10;
			rbox (tx, ty, tiw, th, th / 2, 0x26262C);
			text_c (tx, ty, tiw, th, tip, 0xFFFFFF);
		}
	}
	void ring (int cx, int cy, int r, unsigned c)
	{
		Path p; p.circle (V (cx) + 8, V (cy) + 8, V (r)); p.hole (V (cx) + 8, V (cy) + 8, V (r) - 16); p.fill (c);
	}
	void segment (int x0, int y0, int x1, int y1) { ui_vp_line (V (x0), V (y0), V (x1), V (y1), 24, 0x000000); ui_vp_line (V (x0), V (y0), V (x1), V (y1), 10, 0xFFFFFF); }

	// ---- the tools ----
	bool nearHandle (float x, float y, float hx, float hy) const { float d = 16.0f * 100 / zoom; return (x - hx) * (x - hx) + (y - hy) * (y - hy) <= d * d; }
	// The colours the region tools look at: the layer, or every layer (the composite).
	unsigned *sampleBuffer (bool &own)
	{
		own = g_allLayers;
		if (!g_allLayers) return D.lay[D.cur].px;
		unsigned *b = new unsigned[(unsigned) D.w * D.h];
		for (int y = 0; y < D.h; y++) for (int x = 0; x < D.w; x++) b[(unsigned) y * D.w + x] = comp_px (x, y, false);
		return b;
	}

	void press (int ix, int iy, int btn, unsigned mods)
	{
		m_ax = m_lx = ix; m_ay = m_ly = iy; m_mode = 0;
		if (g_tool != T_SELECT) float_commit (false);
		if (g_tool != T_TEXT) text_end (true);
		// a gradient waiting: its ends dragged, or put down
		if (g_gj.on)
		{
			if (nearHandle (m_fx, m_fy, g_gj.ax, g_gj.ay)) { m_mode = 11; return; }
			if (nearHandle (m_fx, m_fy, g_gj.bx, g_gj.by)) { m_mode = 12; return; }
			grad_end (true);
		}
		switch (g_tool)
		{
		case T_SELECT:
		{
			Rect f = D.fl.px ? float_rect () : g_sel;
			bool in = !f.empty () && ix >= f.x0 && ix < f.x1 && iy >= f.y0 && iy < f.y1 && (D.fl.px || sel_at (ix, iy));
			if (in && !(mods & (UI_MOD_SHIFT | UI_MOD_ALT)) && (D.fl.px || float_lift ())) { m_mode = 1; m_fx0 = D.fl.x; m_fy0 = D.fl.y; break; }
			float_commit (true);
			if (g_selKind == SK_WAND)
			{
				bool own; unsigned *b = sampleBuffer (own); Rect box;
				unsigned char *m = region (b, ix, iy, g_tol, g_contig, &box);
				if (own) delete[] b;
				if (m) sel_apply (m, selModeFor (mods)); else if (selModeFor (mods) == SEL_REPLACE) sel_clear ();
				m_mode = 3;
			}
			else if (g_selKind == SK_FREE) { m_mode = 4; m_nl = 0; lassoAdd (); }
			else { m_mode = 2; m_dragSel = norect (); }
			break;
		}
		case T_PENCIL: case T_BRUSH: case T_ERASER:
		{
			int kind = g_tool == T_PENCIL ? BR_PENCIL : g_tool == T_ERASER ? BR_SOFT : g_brush;
			bool erase = g_tool == T_ERASER && !bottom_opaque ();
			unsigned c = g_tool == T_ERASER ? g_col2 : drawColour (btn);
			int hard = g_tool == T_ERASER ? g_eraseHard : g_hard;
			float x = g_tool == T_PENCIL ? ix + 0.5f : m_fx, y = g_tool == T_PENCIL ? iy + 0.5f : m_fy;
			m_stroke.begin (kind, g_sizes[g_tool], g_opac[g_tool], hard, c, erase, x, y);
			break;
		}
		case T_FILL:
		{
			if (ix < 0 || iy < 0 || ix >= D.w || iy >= D.h) break;
			bool own; unsigned *b = sampleBuffer (own); Rect box;
			unsigned char *m = region (b, ix, iy, g_tol, g_contig, &box);
			if (own) delete[] b;
			if (!m) break;
			if (g_fillMode == FM_GRADIENT)
			{
				g_gj.on = true; g_gj.mask = m; g_gj.box = box; g_gj.erase = false; g_gj.opacity = g_opac[T_FILL] * 255 / 100;
				g_gj.ax = g_gj.bx = m_fx; g_gj.ay = g_gj.by = m_fy;
				m_mode = 10;
				break;
			}
			fillRegion (m, box, drawColour (btn));
			delete[] m;
			break;
		}
		case T_GRADIENT:
		{
			Rect box = has_sel () ? g_sel : mkrect (0, 0, D.w, D.h); box.clip (D.w, D.h);
			if (box.empty ()) break;
			g_gj.on = true; g_gj.mask = 0; g_gj.box = box; g_gj.erase = g_gradErase; g_gj.opacity = g_opac[T_GRADIENT] * 255 / 100;
			g_gj.ax = g_gj.bx = m_fx; g_gj.ay = g_gj.by = m_fy;
			m_mode = 10;
			break;
		}
		case T_TEXT:
		{
			if (g_text.on && ix >= g_text.x - 4 && iy >= g_text.y - 4 && ix < g_text.x + g_text.w + 4 && iy < g_text.y + g_text.h + 4)
			{ m_mode = 20; m_fx0 = g_text.x; m_fy0 = g_text.y; break; }
			text_end (true);
			g_text.on = true; g_text.x = ix; g_text.y = iy - g_tsize / 2; g_text.n = 0; g_text.caret = 0; g_text.s[0] = 0;
			text_preview ();
			break;
		}
		case T_PICKER:
			if (ix >= 0 && iy >= 0 && ix < D.w && iy < D.h)
			{
				unsigned c = comp_px (ix, iy, true);
				if (c >> 24) { if (btn == 2) g_col2 = c | 0xFF000000u; else g_col1 = c | 0xFF000000u; }
			}
			break;
		case T_ZOOM: break;
		case T_SHAPE: shapePreview (ix, iy, btn, mods); break;
		}
		invalidate (); notify_all ();
	}
	void drag (int ix, int iy, unsigned mods)
	{
		if (m_mode == 11 || m_mode == 12 || m_mode == 10)	// (a gradient's end)
		{
			float x = m_fx, y = m_fy;
			if (mods & UI_MOD_SHIFT)				// (15-degree steps)
			{
				float bx = m_mode == 11 ? g_gj.bx : g_gj.ax, by = m_mode == 11 ? g_gj.by : g_gj.ay;
				float dx = x - bx, dy = y - by, L = sqrtf (dx * dx + dy * dy), a = atan2f (dy, dx);
				a = floorf (a / 0.2617994f + 0.5f) * 0.2617994f;
				x = bx + L * cosf (a); y = by + L * sinf (a);
			}
			if (m_mode == 11) { g_gj.ax = x; g_gj.ay = y; } else { g_gj.bx = x; g_gj.by = y; }
			grad_preview ();
			invalidate (); notify ();
			return;
		}
		if (ix == m_lx && iy == m_ly && g_tool != T_BRUSH && g_tool != T_ERASER) return;
		switch (g_tool)
		{
		case T_SELECT:
			if (m_mode == 1)
			{
				Rect old = float_rect ();
				D.fl.x = m_fx0 + ix - m_ax; D.fl.y = m_fy0 + iy - m_ay;
				old.add (float_rect ()); compose (old);
				g_sel = float_rect ();
			}
			else if (m_mode == 2)
			{
				int x0 = pclamp (pmin (m_ax, ix), 0, D.w), y0 = pclamp (pmin (m_ay, iy), 0, D.h);
				int x1 = pclamp (pmax (m_ax, ix) + 1, 0, D.w), y1 = pclamp (pmax (m_ay, iy) + 1, 0, D.h);
				m_dragSel = mkrect (x0, y0, x1, y1);
			}
			else if (m_mode == 4) lassoAdd ();
			break;
		case T_PENCIL: case T_BRUSH: case T_ERASER:
			if (g_tool == T_PENCIL) m_stroke.to (ix + 0.5f, iy + 0.5f); else m_stroke.to (m_fx, m_fy);
			break;
		case T_TEXT:
			if (m_mode == 20) { g_text.x = m_fx0 + ix - m_ax; g_text.y = m_fy0 + iy - m_ay; text_preview (); }
			break;
		case T_SHAPE: shapePreview (ix, iy, m_btn, mods); break;
		default: break;
		}
		m_lx = ix; m_ly = iy;
		invalidate (); notify ();
	}
	void release (int ix, int iy, unsigned mods)
	{
		if (m_mode == 10)					// (a gradient placed: a click alone, across the zone)
		{
			float dx = g_gj.bx - g_gj.ax, dy = g_gj.by - g_gj.ay;
			if (dx * dx + dy * dy < 4)
			{ g_gj.ax = (float) g_gj.box.x0; g_gj.bx = (float) g_gj.box.x1; g_gj.ay = g_gj.by = (g_gj.box.y0 + g_gj.box.y1) / 2.0f; }
			grad_preview ();
			invalidate (); notify_all ();
			return;
		}
		if (m_mode == 11 || m_mode == 12) return;
		switch (g_tool)
		{
		case T_SELECT:
			if (m_mode == 2)
			{
				Rect r = m_dragSel; m_dragSel = norect ();
				if (r.x1 - r.x0 <= 1 && r.y1 - r.y0 <= 1 && ix == m_ax && iy == m_ay) { if (selModeFor (mods) == SEL_REPLACE) sel_clear (); }	// (a click: nothing)
				else sel_apply_rect (r, selModeFor (mods));
			}
			else if (m_mode == 4)
			{
				lassoAdd ();
				if (m_nl >= 3) sel_apply (poly_mask (m_lasso, m_nl), selModeFor (mods));
				else if (selModeFor (mods) == SEL_REPLACE) sel_clear ();
				m_nl = 0;
			}
			break;
		case T_PENCIL: case T_BRUSH: case T_ERASER: m_stroke.end (); break;
		case T_SHAPE: ov_commit (); break;
		case T_PICKER: g_tool = g_prevTool == T_PICKER ? T_BRUSH : g_prevTool; if (g_toolChanged) g_toolChanged (); break;
		case T_ZOOM: if (!m_panned) zoomStep (m_btn == 2 ? -1 : 1, toX (ix), toY (iy)); break;
		default: break;
		}
		invalidate (); notify_all ();
	}
	void lassoAdd ()
	{
		int x = (int) (m_fx * 16), y = (int) (m_fy * 16);
		if (m_nl > 0 && m_lasso[2 * m_nl - 2] == x && m_lasso[2 * m_nl - 1] == y) return;
		if (m_nl == m_capl) { int c = m_capl ? m_capl * 2 : 256; int *n = new int[c * 2]; for (int i = 0; i < m_nl * 2; i++) n[i] = m_lasso[i]; delete[] m_lasso; m_lasso = n; m_capl = c; }
		m_lasso[2 * m_nl] = x; m_lasso[2 * m_nl + 1] = y; m_nl++;
	}
	// The region filled with a colour (or the pattern in it), inside the selection.
	void fillRegion (const unsigned char *m, Rect box, unsigned c)
	{
		rec_begin (); rec_touch (box);
		unsigned op = (unsigned) (g_opac[T_FILL] * 255 / 100);
		for (int y = box.y0; y < box.y1; y++)
			for (int x = box.x0; x < box.x1; x++)
			{
				int a = m[(unsigned) y * D.w + x] * sel_at (x, y) / 255;
				if (!a) continue;
				if (g_fillMode == FM_PATTERN && !pat_on (g_pattern, x, y)) continue;
				unsigned &p = D.lay[D.cur].px[(unsigned) y * D.w + x];
				p = a == 255 && op == 255 ? c | 0xFF000000u : over (p, c | 0xFF000000u, (unsigned) a * op / 255);
			}
		rec_end ();
		compose (box);
	}

	// The shape between the press and (ix, iy), into the overlay (put down on release).
	void shapePreview (int ix, int iy, int btn, unsigned mods)
	{
		ov_clear ();
		int x1 = ix, y1 = iy;
		if (mods & UI_MOD_SHIFT)
		{
			int dx = x1 - m_ax, dy = y1 - m_ay, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
			if (g_shape == SH_LINE)
			{
				if (ax > 2 * ay) y1 = m_ay; else if (ay > 2 * ax) x1 = m_ax;
				else { int m = pmax (ax, ay); x1 = m_ax + (dx < 0 ? -m : m); y1 = m_ay + (dy < 0 ? -m : m); }
			}
			else { int m = pmax (ax, ay); x1 = m_ax + (dx < 0 ? -m : m); y1 = m_ay + (dy < 0 ? -m : m); }
		}
		Target t = target (D.ov.px, D.w, D.h);
		unsigned oc = btn == 2 ? g_col2 : g_col1, fc = btn == 2 ? g_col1 : g_col2;
		bool outline = g_outline || g_shape == SH_LINE || !g_fillShape;
		shape (t, g_shape, m_ax, m_ay, x1, y1, g_sizes[T_SHAPE], outline, oc, g_fillShape && g_shape != SH_LINE, fc);
		Rect r = t.dirty;
		unsigned op = (unsigned) (g_opac[T_SHAPE] * 255 / 100);
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++)
		{
			unsigned &o = D.ov.px[(unsigned) y * D.w + x];
			if (o >> 24) { unsigned a = (o >> 24) * (unsigned) sel_at (x, y) / 255 * op / 255; o = a ? (o & 0xFFFFFF) | a << 24 : 0; }
		}
		D.ov.r = r; D.ov.eraser = false;
		compose (r);
	}

	void fillClip (int x, int y, int w, int h, unsigned c)
	{
		int x1 = pmin (x + w, viewW ()), y1 = pmin (y + h, viewH ());
		x = pmax (x, 0); y = pmax (y, 0);
		if (x1 > x && y1 > y) fill (x, y, x1 - x, y1 - y, c);
	}
	void dashed (int x0, int y0, int x1, int y1)
	{
		auto px = [&] (int x, int y, int k) { if (x >= 0 && y >= 0 && x < viewW () && y < viewH ()) g_px[y * g_pw + x] = (k / 4) & 1 ? 0xFFFFFF : 0x000000; };
		for (int x = x0; x <= x1; x++) { px (x, y0, x - x0); px (x, y1, x - x0); }
		for (int y = y0; y <= y1; y++) { px (x0, y, y - y0); px (x1, y, y - y0); }
	}
	void gridPx (int x, int y)
	{
		uint32_t &p = g_px[y * g_pw + x];
		p = bright (p) > 110 ? mix (p, 0x000000, 70) : mix (p, 0xFFFFFF, 80);
	}
	void drawGrid (int ix0, int iy0, int ix1, int iy1)
	{
		for (int i = pmax (0, imX (ix0)); i <= D.w; i++)
		{
			int x = toX (i); if (x >= ix1) break;
			if (x < ix0) continue;
			for (int y = iy0; y < iy1; y++) gridPx (x, y);
		}
		for (int j = pmax (0, imY (iy0)); j <= D.h; j++)
		{
			int y = toY (j); if (y >= iy1) break;
			if (y < iy0) continue;
			for (int x = ix0; x < ix1; x++) gridPx (x, y);
		}
	}
	// Thin bars on the desk: a track, a thumb.
	void drawBars ()
	{
		unsigned ink = ink_on (DESK);
		Thumb t = thumb_of (docH (), viewH (), sy, viewH () - 4);
		if (t.show)
		{
			fill (viewW () + 2, 2, SBW - 4, viewH () - 4, mix (DESK, ink, 18));
			rbox (viewW () + 3, 2 + t.y, SBW - 6, t.h, 3, mix (DESK, ink, m_barHot == 1 || m_drag == 1 ? 150 : 90));
		}
		Thumb u = thumb_of (docW (), viewW (), sx, viewW () - 4);
		if (u.show)
		{
			fill (2, viewH () + 2, viewW () - 4, SBW - 4, mix (DESK, ink, 18));
			rbox (2 + u.y, viewH () + 3, u.h, SBW - 6, 3, mix (DESK, ink, m_barHot == 2 || m_drag == 2 ? 150 : 90));
		}
	}
};

} // namespace pd

#endif
