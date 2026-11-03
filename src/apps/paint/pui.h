//
// pui.h -- Paint's controls, each a ui_view drawn here (Onyx's ribbon on UIKit): the ribbon (Clipboard, Image -- Select and
// its kinds, Crop, Resize, Rotate --, Tools, Brushes, the Shapes' gallery, Colours), the options bar under it (what the
// current tool takes: sizes, opacity, hardness, the selection's mode and tolerance, the fill's colour / gradient / pattern,
// the text's font...), the layers' panel (the current layer's blend mode and opacity; each layer's eye, thumbnail, name,
// mode; New, Duplicate, Delete, Up, Down, Merge, Properties) and the status bar (the pointer, the selection, the picture's
// size, the layer; the layers' panel, the grid, fit, the zoom's slider).
//
// A window under 1090 px (PocketTerm's 800 x 600): the compact ribbon, on one height, without its groups' names; the Shapes
// in a menu; the layers' panel hidden until asked. The top right of the ribbon stays free for the frame's buttons; a press on
// nothing there moves the window.
//
// MIT licence (Onyx).
//
#ifndef _paint_pui_h
#define _paint_pui_h

#include "picons.h"

namespace pd {

// The tones of the parts (the theme's tokens).
static unsigned rib_bg () { return C_BG; }
static unsigned opt_bg () { return jet (J_ELEVE); }
static unsigned pan_bg () { return jet (J_CREUX); }
static void hot_box (int x, int y, int w, int h, unsigned bg, bool on, bool hot, bool down = false)
{
	if (on) panel (x, y, w, h, 5, mix (bg, C_ACCENT, 56), mix (bg, C_ACCENT, 166));
	else if (hot || down) panel (x, y, w, h, 5, mix (bg, ink_on (bg), down ? 46 : 22), C_LINE);
}
static void ring2 (int cx, int cy, int r, unsigned c, int w = 16) { Path p; p.circle (V (cx), V (cy), V (r)); p.hole (V (cx), V (cy), V (r) - w); p.fill (c); }
static void fmt_int (char *b, int v, const char *suffix)
{
	int n = 0; char t[12]; int j = 0; if (v < 0) { b[n++] = '-'; v = -v; }
	do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[n++] = t[--j];
	for (const char *s = suffix; s && *s; s++) b[n++] = *s;
	b[n] = 0;
}
// A slider: a track, its part up to the value in the accent, a knob.
static void slider_draw (int x, int y, int w, int h, int v, int lo, int hi, unsigned bg)
{
	int cy = y + h / 2, k = x + (int) ((long long) (v - lo) * (w - 1) / pmax (1, hi - lo));
	rbox (x, cy - 2, w, 4, 2, C_LINE);
	if (k > x) rbox (x, cy - 2, k - x + 2, 4, 2, C_ACCENT);
	disc (k, cy, 8, mix (bg, ink_on (bg), 90)); disc (k, cy, 7, 0xFFFFFF);
}
static int slider_val (int mx, int x, int w, int lo, int hi) { return pclamp (lo + (int) ((long long) (mx - x) * (hi - lo) / pmax (1, w - 1)), lo, hi); }

// A brush's stroke, drawn small (the ribbon's button, the gallery).
static void brush_sample (int x, int y, int w, int h, int kind, unsigned col)
{
	unsigned char *cov = new unsigned char[(unsigned) w * h];
	for (int i = 0; i < w * h; i++) cov[i] = 0;
	float r = kind == BR_PENCIL ? 0.7f : kind == BR_AIR ? 5.5f : kind == BR_CALLIG ? 6 : kind >= BR_FIRST_PATTERN ? 6 : 3.4f;
	unsigned seed = 7;
	for (int i = 0; i <= 120; i++)
	{
		float t = i / 120.0f, cx = 4 + (w - 8) * t, cy = h / 2.0f + sinf (t * 6.2832f) * h * 0.26f;
		for (int yy = (int) (cy - r - 1); yy <= (int) (cy + r + 1); yy++)
			for (int xx = (int) (cx - r - 1); xx <= (int) (cx + r + 1); xx++)
			{
				if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
				float c;
				if (kind == BR_AIR) { seed = seed * 1103515245u + 12345u; c = ((seed >> 8) & 255) < 22 && (xx - cx) * (xx - cx) + (yy - cy) * (yy - cy) < r * r ? 0.9f : 0; }
				else c = Stroke::shape (kind == BR_PENCIL ? BR_BRUSH : kind, xx + 0.5f - cx, yy + 0.5f - cy, r, 0.3f);
				int ci = (int) (c * 255);
				if (kind == BR_CRAYON) { unsigned g = hash2 (xx, yy) & 255; ci = g < 90 ? 0 : ci; }
				if (kind >= BR_FIRST_PATTERN && !pat_on (kind, xx, yy)) ci = 0;
				unsigned char &v = cov[yy * w + xx];
				if (kind == BR_AIR) { if (ci) v = (unsigned char) pmin (255, v + 120); }
				else if (ci > v) v = (unsigned char) ci;
			}
	}
	int op = kind == BR_MARKER ? 140 : 255;
	for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) if (cov[j * w + i]) blend_view (x + i, y + j, col, cov[j * w + i] * op / 255);
	delete[] cov;
}
// A gradient's colours in a bar, over a checkerboard.
static void grad_draw (int x, int y, int w, int h, int gi)
{
	if (gi < 0 || gi >= g_ngrads) return;
	Gradient &g = g_grads[gi];
	grad_dyn (g, g_col1, g_col2);
	for (int i = 0; i < w; i++)
	{
		unsigned c = grad_at (g, w > 1 ? i / (float) (w - 1) : 0), a = c >> 24;
		for (int j = 0; j < h; j++)
		{
			unsigned ck = (((i >> 2) ^ (j >> 2)) & 1) ? 0xD6D6D6 : 0xFFFFFF;
			put_px (x + i, y + j, a == 255 ? c & 0xFFFFFF : mix (ck, c & 0xFFFFFF, (int) a + 1));
		}
	}
	frame (x, y, w, h, C_LINE);
}
// The gradient's shapes' little pictures.
static void gshape_draw (int x, int y, int s, int kind)
{
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			float u = i / (float) (s - 1), v = j / (float) (s - 1), t;
			switch (kind)
			{
			case GS_BILINEAR: t = fabsf (u - 0.5f) * 2; break;
			case GS_RADIAL: t = sqrtf ((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2; break;
			case GS_SQUARE: t = fmaxf (fabsf (u - 0.5f), fabsf (v - 0.5f)) * 2; break;
			case GS_CONICAL: t = atan2f (v - 0.5f, u - 0.5f) / 6.2832f; t -= floorf (t); break;
			default: t = u;
			}
			put_px (x + i, y + j, mix (0x3C3860, 0xFAFAFA, (int) (fminf (1, t) * 256)));
		}
	frame (x, y, s, s, C_LINE);
}

// A part of the window: its view, its place.
struct Part
{
	long view; int left, top, width, height;
	Part () : view (0), left (0), top (0), width (10), height (10) {}
	void invalidate () { if (view) ui_view_dirty (view); }
	void place (int x, int y, int w, int h) { left = x; top = y; width = w; height = h; if (view) { ui_w_move (view, x, y, w, h); ui_view_dirty (view); } }
};

// ---- the ribbon ----------------------------------------------------------------------------------------------
enum { RIBBON_H = 96, RIBBON_CH = 72, RIBBON_WIDE = 1090 };
enum { C_PASTE, C_CUT, C_COPY, C_UNDO, C_REDO, C_SELECT, C_CROP, C_RESIZE, C_ROTATE, C_TOOL, C_ADJUST, C_BRUSHES, C_SHAPE, C_SHAPES,
       C_COL1, C_COL2, C_PALETTE, C_CUSTOM, C_EDITCOL };
static void (*g_ribbonCmd) (int cmd, int arg, int btn, int x, int y);	// (x, y: where a popup opens, in the window)

static const unsigned PALETTE[20] = {
	0x000000, 0x7F7F7F, 0x880015, 0xED1C24, 0xFF7F27, 0xFFF200, 0x22B14C, 0x00A2E8, 0x3F48CC, 0xA349A4,
	0xFFFFFF, 0xC3C3C3, 0xB97A57, 0xFFAEC9, 0xFFC90E, 0xEFE4B0, 0xB5E61D, 0x99D9EA, 0x7092BE, 0xC8BFE7 };
static unsigned g_custom[10];					// (0xFF000000 | colour; 0: empty)

class Ribbon : public Part
{
public:
	Ribbon () : m_n (0), m_ng (0), m_hot (-1), m_down (-1), m_btn (0) {}
	void paint ()
	{
		unsigned bg = rib_bg ();
		layoutCells ();
		fill (0, 0, width, height, bg);
		fill (0, height - 1, width, 1, C_LINE);
		unsigned ink = C_TEXT, dim = C_DIM;
		for (int g = 0; g < m_ng; g++)
		{
			if (!compact ()) text_c (m_gx[g], height - 22, m_gw[g], 18, TR (m_gname[g]), dim);
			if (g < m_ng - 1) fill (m_gx[g] + m_gw[g] + (compact () ? 3 : 5), 8, 1, height - (compact () ? 16 : 22), C_LINE);
		}
		if (!compact ())						// the shapes' gallery: a box
		{
			fill (m_shX, 7, 5 * 24 + 4, 3 * 22 + 2, C_FIELD); frame (m_shX, 7, 5 * 24 + 4, 3 * 22 + 2, C_LINE);
		}
		for (int i = 0; i < m_n; i++) drawCell (i, ink, bg);
	}
	void onMouse (int kind, int mx, int my, int bl, int br)
	{
		layoutCells ();
		int hot = kind == 3 ? -1 : cellAt (mx, my);
		if (hot != m_hot) { m_hot = hot; ui_w_tip (view, hot >= 0 ? (long) TR (m_cell[hot].tip) : 0); invalidate (); }
		int btn = bl ? 1 : br ? 2 : 0;
		if (kind == 1 && btn && hot < 0) { ui_window_drag (); return; }	// (the ribbon's empty places: the window's top)
		if (kind == 1 && btn && m_down < 0 && hot >= 0) { m_down = hot; m_btn = btn; invalidate (); }
		else if (kind == 2 && m_down >= 0)
		{
			int d = m_down; m_down = -1; invalidate ();
			if (d == hot && g_ribbonCmd)
			{
				const Cell &c = m_cell[d];
				g_ribbonCmd (c.cmd, c.arg, m_btn, left + c.x, top + c.y + c.h + 2);
			}
		}
	}
private:
	struct Cell { int x, y, w, h, cmd, arg; const char *tip; const char *label; int icon; };
	enum { MAXC = 96 };
	Cell m_cell[MAXC]; int m_n;
	int m_gx[8], m_gw[8], m_ng; const char *m_gname[8];
	int m_hot, m_down, m_btn, m_shX;

	bool compact () const { return width < RIBBON_WIDE; }
	void add (int x, int y, int w, int h, int cmd, int arg, const char *tip, int ic = -1, const char *label = 0)
	{
		if (m_n >= MAXC) return;
		Cell &c = m_cell[m_n++];
		c.x = x; c.y = y; c.w = w; c.h = h; c.cmd = cmd; c.arg = arg; c.tip = tip; c.icon = ic; c.label = label;
	}
	void group (int x, int w, const char *name) { m_gx[m_ng] = x; m_gw[m_ng] = w; m_gname[m_ng] = name; m_ng++; }
	void tools (int x, int top, int step)
	{
		static const int TI[8] = { T_PENCIL, T_FILL, T_TEXT, T_ERASER, T_PICKER, T_ZOOM, T_GRADIENT, -1 };
		static const int TIC[8] = { I_PENCIL, I_FILL, I_TEXT, I_ERASER, I_PICKER, I_ZOOM, I_GRADIENT, I_FX };
		static const char *const TT[8] = { "Pencil (P)", "Fill: a colour, a gradient, a pattern (F)", "Text (T)", "Eraser (E)",
			"Colour picker (K)", "Magnifier: left in, right out; a drag moves the picture (Z)", "Gradient (G)", "Adjust: brightness, hue, blur..." };
		for (int i = 0; i < 8; i++) add (x + (i % 4) * step, top + 2 + (i / 4) * 30, step - 2, 28, TI[i] < 0 ? C_ADJUST : C_TOOL, TI[i], TT[i], TIC[i]);
	}
	static constexpr const char *TIP_PASTE = "Paste (Ctrl+V); right click: as a new layer, a file as a layer";
	static constexpr const char *TIP_SELECT = "Select: a rectangle, free-form, the magic wand (S, L, W)";
	static constexpr const char *TIP_ROTATE = "Rotate or flip (the selection, or the picture)";
	static constexpr const char *TIP_RESIZE = "Resize the picture or its canvas (Ctrl+W)";
	static constexpr const char *TIP_BRUSHES = "Brushes (B): pencil, brush, soft, calligraphy, airbrush, marker, crayon, patterns";
	static constexpr const char *TIP_COL1 = "Colour 1: what the left button or the finger draws with, when it is chosen";
	static constexpr const char *TIP_COL2 = "Colour 2: the other one; the shapes' inside, the text's background";
	static constexpr const char *TIP_PAL = "Left: the chosen colour / right: the other one";
	void layoutCompact ()
	{
		int x = 6, top = 6, sel = g_selKind == SK_FREE ? I_LASSO : g_selKind == SK_WAND ? I_WAND : I_SELECT;
		add (x, top, 40, 60, C_PASTE, 0, TIP_PASTE, I_PASTE, "Paste");
		add (x + 44, top, 26, 28, C_CUT, 0, "Cut (Ctrl+X)", I_CUT);
		add (x + 44, top + 32, 26, 28, C_COPY, 0, "Copy (Ctrl+C)", I_COPY);
		add (x + 72, top, 26, 28, C_UNDO, 0, "Undo (Ctrl+Z)", I_UNDO);
		add (x + 72, top + 32, 26, 28, C_REDO, 0, "Redo (Ctrl+Y)", I_REDO);
		group (x, 98, "Clipboard"); x += 104;
		add (x, top, 62, 60, C_SELECT, 0, TIP_SELECT, sel, "Select");
		add (x + 64, top, 26, 28, C_CROP, 0, "Crop to the selection", I_CROP);
		add (x + 64, top + 32, 26, 28, C_RESIZE, 0, TIP_RESIZE, I_RESIZE);
		add (x + 92, top, 26, 28, C_ROTATE, 0, TIP_ROTATE, I_ROTATE);
		group (x, 118, "Image"); x += 124;
		tools (x, top, 30);
		group (x, 118, "Tools"); x += 124;
		add (x, top, 60, 60, C_BRUSHES, 0, TIP_BRUSHES, -1, "Brushes");
		group (x, 60, "Brushes"); x += 66;
		add (x, top, 50, 60, C_SHAPES, 0, "Shapes", -2, "Shapes");
		group (x, 50, "Shapes"); x += 56;
		add (x, top, 30, 60, C_COL1, 0, TIP_COL1, -1, "1");
		add (x + 31, top, 30, 60, C_COL2, 0, TIP_COL2, -1, "2");
		int px0 = x + 64, st = width >= 900 ? 21 : 15;
		for (int i = 0; i < 20; i++) add (px0 + (i % 10) * st, 7 + (i / 10) * 20, st, 19, C_PALETTE, i, TIP_PAL);
		for (int i = 0; i < 10; i++) add (px0 + i * st, 7 + 2 * 20, st, 19, C_CUSTOM, i, "A colour of yours (Edit adds them)");
		add (px0 + 10 * st + 2, 34, 30, 32, C_EDITCOL, 0, "Edit colours: any colour (it joins yours)", I_EDITCOL);
		group (x, width - x, "Colours");
	}
	void layoutCells ()
	{
		m_n = 0; m_ng = 0;
		if (compact ()) { layoutCompact (); return; }
		int x = 10, top = 6;
		// Clipboard
		add (x, top, 46, 64, C_PASTE, 0, TIP_PASTE, I_PASTE, "Paste");
		add (x + 50, top, 28, 26, C_CUT, 0, "Cut (Ctrl+X)", I_CUT);
		add (x + 50, top + 30, 28, 26, C_COPY, 0, "Copy (Ctrl+C)", I_COPY);
		add (x + 80, top, 28, 26, C_UNDO, 0, "Undo (Ctrl+Z)", I_UNDO);
		add (x + 80, top + 30, 28, 26, C_REDO, 0, "Redo (Ctrl+Y)", I_REDO);
		group (x, 108, "Clipboard"); x += 120;
		// Image
		add (x, top, 48, 64, C_SELECT, 0, TIP_SELECT, g_selKind == SK_FREE ? I_LASSO : g_selKind == SK_WAND ? I_WAND : I_SELECT, "Select");
		add (x + 52, top - 2, 84, 22, C_CROP, 0, "Crop to the selection", I_CROP, "Crop");
		add (x + 52, top + 21, 84, 22, C_RESIZE, 0, TIP_RESIZE, I_RESIZE, "Resize");
		add (x + 52, top + 44, 84, 22, C_ROTATE, 0, TIP_ROTATE, I_ROTATE, "Rotate");
		group (x, 136, "Image"); x += 146;
		// Tools: 2 rows of 4
		tools (x, top, 31);
		group (x, 122, "Tools"); x += 134;
		// Brushes
		add (x, top, 62, 64, C_BRUSHES, 0, TIP_BRUSHES, -1, "Brushes");
		group (x, 62, "Brushes"); x += 74;
		// Shapes
		m_shX = x;
		for (int k = 0; k < SH_COUNT; k++) add (x + 2 + (k % 5) * 24, 8 + (k / 5) * 22, 24, 22, C_SHAPE, k, SHAPE_NAMES[k]);
		group (x, 124, "Shapes"); x += 136;
		// Colours
		add (x, top, 40, 64, C_COL1, 0, TIP_COL1, -1, "1");
		add (x + 42, top, 40, 64, C_COL2, 0, TIP_COL2, -1, "2");
		int px0 = x + 90;
		for (int i = 0; i < 20; i++) add (px0 + (i % 10) * 21, 7 + (i / 10) * 21, 19, 19, C_PALETTE, i, TIP_PAL);
		for (int i = 0; i < 10; i++) add (px0 + i * 21, 7 + 2 * 21, 19, 19, C_CUSTOM, i, "A colour of yours (Edit adds them)");
		add (px0 + 214, 34, 46, 36, C_EDITCOL, 0, "Edit colours: any colour (it joins yours)", I_EDITCOL);
		group (x, 90 + 214 + 46, "Colours");
	}
	int cellAt (int mx, int my) const
	{
		for (int i = 0; i < m_n; i++) { const Cell &c = m_cell[i]; if (mx >= c.x && my >= c.y && mx < c.x + c.w && my < c.y + c.h) return i; }
		return -1;
	}
	bool isOn (const Cell &c) const
	{
		switch (c.cmd)
		{
		case C_SELECT: return g_tool == T_SELECT;
		case C_TOOL: return g_tool == c.arg;
		case C_BRUSHES: return g_tool == T_BRUSH;
		case C_SHAPE: return g_tool == T_SHAPE && g_shape == c.arg;
		case C_SHAPES: return g_tool == T_SHAPE;
		case C_COL1: return g_activeCol == 1;
		case C_COL2: return g_activeCol == 2;
		}
		return false;
	}
	void drawCell (int i, unsigned ink, unsigned bg)
	{
		const Cell &c = m_cell[i];
		bool hot = i == m_hot, down = i == m_down, on = isOn (c);
		const char *label = c.label ? TR (c.label) : 0;
		if (c.cmd == C_PALETTE || c.cmd == C_CUSTOM)
		{
			int cx = c.x + c.w / 2, cy = c.y + c.h / 2, r = pmin (c.w, c.h) / 2;
			bool empty = c.cmd == C_CUSTOM && !(g_custom[c.arg] >> 24);
			unsigned col = c.cmd == C_PALETTE ? PALETTE[c.arg] : g_custom[c.arg] & 0xFFFFFF;
			if (hot) disc (cx, cy, r + 1, C_ACCENT);
			if (empty) { disc (cx, cy, r, C_LINE); disc (cx, cy, r - 1, mix (bg, ink, 20)); }
			else { disc (cx, cy, r, mix (col, ink, 80)); disc (cx, cy, r - 1, col); }
			return;
		}
		if (c.cmd == C_SHAPE)
		{
			if (on) fill (c.x, c.y, c.w, c.h, mix (C_FIELD, C_ACCENT, 70));
			else if (hot) fill (c.x, c.y, c.w, c.h, mix (C_FIELD, C_ACCENT, 30));
			shape_icon (c.arg, c.x + 4, c.y + 3, 16, 16, C_TEXT);
			return;
		}
		hot_box (c.x, c.y, c.w, c.h, bg, on, hot, down);
		if (c.cmd == C_COL1 || c.cmd == C_COL2)
		{
			unsigned col = (c.cmd == C_COL1 ? g_col1 : g_col2) & 0xFFFFFF;
			int cx = c.x + c.w / 2, cy = c.y + 21, r = pmin (c.w / 2 - 2, 15);
			disc (cx, cy, r, mix (bg, ink, 70)); disc (cx, cy, r - 1, mix (col, 0x000000, 70)); disc (cx, cy, r - 2, col);
			text_c (c.x, c.y + 40, c.w, 18, label, ink);
			return;
		}
		if (c.cmd == C_BRUSHES)
		{
			brush_sample (c.x + 6, c.y + 4, c.w - 12, 28, g_brush, C_ACCENT);
			text_c (c.x, c.y + 32, c.w, 18, label, ink);
			chevron (c.x + c.w / 2, c.y + c.h - 6, ink);
			return;
		}
		if (c.cmd == C_SHAPES)
		{
			shape_icon (g_shape, c.x + (c.w - 22) / 2, c.y + 8, 22, 22, ink);
			text_c (c.x, c.y + 32, c.w, 18, label, ink);
			chevron (c.x + c.w / 2, c.y + c.h - 6, ink);
			return;
		}
		if (label && c.h >= 50)						// (a big button: its icon, its name below)
		{
			icon (c.icon, c.x + (c.w - 20) / 2, c.y + 8, ink);
			text_c (c.x, c.y + 32, c.w, 18, label, ink);
			if (c.cmd == C_SELECT) chevron (c.x + c.w / 2, c.y + c.h - 6, ink);
			return;
		}
		if (label)							// (a wide small one: the icon, the name after it)
		{
			icon (c.icon, c.x + 3, c.y + (c.h - 20) / 2, ink);
			text_l (c.x + 27, c.y, c.h, label, ink);
			if (c.cmd == C_ROTATE) chevron (c.x + c.w - 8, c.y + c.h / 2, ink);
			return;
		}
		icon (c.icon, c.x + (c.w - 20) / 2, c.y + (c.h - 20) / 2, ink);
	}
};

// ---- the options bar ------------------------------------------------------------------------------------------
enum { OPT_H = 38 };
// The options (O_*: a toggle or a choice the app does; the sliders set their value themselves).
enum { O_SIZE, O_OPACITY, O_HARD, O_TOL,					// sliders
       O_BRUSHKIND, O_SELRECT, O_SELFREE, O_SELWAND, O_SELNEW, O_SELADD, O_SELSUB, O_CONTIG, O_ALLLAYERS,
       O_SELALL, O_INVERT, O_DESELECT, O_FILLCOL, O_FILLGRAD, O_FILLPAT, O_GRADLIST, O_GSHAPE, O_GREPEAT, O_GREVERSE,
       O_PATTERN, O_GERASE, O_FONT, O_TSIZE, O_BOLD, O_ITALIC, O_UNDER, O_ALIGN, O_SMOOTH, O_TBACK, O_OUTLINE, O_FILLSH };
static void (*g_optCmd) (int opt, int arg, int x, int y);

class OptionsBar : public Part
{
public:
	OptionsBar () : m_n (0), m_hot (-1), m_drag (-1), m_sx (0), m_over (false), m_toolWas (-1) {}
	void paint ()
	{
		unsigned bg = opt_bg ();
		fill (0, 0, width, height, bg);
		fill (0, height - 1, width, 1, C_LINE);
		layoutItems ();
		for (int i = 0; i < m_n; i++) drawItem (i, bg);
		if (m_over)							// more than the bar shows: it slides
		{
			fill (width - 26, 0, 26, height - 1, bg);
			hot_box (width - 24, 6, 20, height - 12, bg, false, m_hot == 1000);
			int cx = width - 14, cy = height / 2;
			int t[6] = { V (cx - 3), V (cy - 5), V (cx + 3), V (cy), V (cx - 3), V (cy + 5) };
			ui_vp_poly ((long) t, 3, C_TEXT);
		}
	}
	void wheel (int w) { if (m_over) { m_sx = pclamp (m_sx - w * 60, 0, pmax (0, m_x - width + 40)); invalidate (); } }
	void onMouse (int kind, int mx, int my, int bl)
	{
		layoutItems ();
		if (m_drag >= 0)
		{
			It &s = m_it[m_drag];
			setSlider (s.arg, slider_val (mx, s.sx, s.sw, s.lo, s.hi));
			if (kind == 2) m_drag = -1;
			invalidate ();
			return;
		}
		int hot = -1;
		if (kind != 3)
		{
			if (m_over && mx >= width - 26) hot = 1000;
			else for (int i = 0; i < m_n; i++) if (m_it[i].kind != K_LABEL && m_it[i].kind != K_SEP && mx >= m_it[i].x && mx < m_it[i].x + m_it[i].w && my >= 2 && my < height - 2) hot = i;
		}
		if (hot != m_hot) { m_hot = hot; invalidate (); }
		if (kind == 1 && bl && hot == 1000) { m_sx = m_sx + 160 >= m_x - width + 40 + 160 ? 0 : pmin (m_sx + 160, m_x - width + 40); invalidate (); return; }
		if (kind == 1 && bl && hot >= 0)
		{
			It &it = m_it[hot];
			if (it.kind == K_SLIDER) { m_drag = hot; setSlider (it.arg, slider_val (mx, it.sx, it.sw, it.lo, it.hi)); invalidate (); return; }
			int opt = it.arg, val = it.val, ax = left + it.x, ay = top + height;
			if (g_optCmd) g_optCmd (opt, val, ax, ay);
			invalidate ();
		}
	}
private:
	enum { K_LABEL, K_TOGGLE, K_SLIDER, K_DROP, K_SEP, K_GRAD, K_GSHAPE, K_ICON, K_GLYPH };
	struct It { int kind, x, w, arg, val; bool on; const char *text; int sx, sw, lo, hi, v; const char *suffix; int icon; };
	enum { MAXI = 48 };
	It m_it[MAXI]; int m_n, m_hot, m_drag, m_x, m_sx; bool m_over; int m_toolWas;

	static int *sliderVar (int opt)
	{
		switch (opt)
		{
		case O_SIZE: return &g_sizes[g_tool];
		case O_OPACITY: return &g_opac[g_tool];
		case O_HARD: return g_tool == T_ERASER ? &g_eraseHard : &g_hard;
		case O_TOL: return &g_tol;
		}
		return 0;
	}
	void setSlider (int opt, int v) { int *p = sliderVar (opt); if (p && *p != v) { *p = v; if (g_optCmd) g_optCmd (opt, v, -1, -1); } }

	It &push (int kind, int w) { It &i = m_it[m_n < MAXI - 1 ? m_n++ : m_n]; i.kind = kind; i.x = m_x; i.w = w; i.arg = -1; i.val = 0; i.on = false; i.text = 0; i.suffix = 0; i.icon = -1; m_x += w; return i; }
	void label (const char *s) { s = TR (s); push (K_LABEL, tw (s) + 8).text = s; }
	void ico (int ic) { push (K_ICON, 30).icon = ic; }
	void sep () { m_x += 6; push (K_SEP, 1); m_x += 9; }
	void toggle (int opt, const char *s, bool on, int val = 0) { s = TR (s); It &i = push (K_TOGGLE, tw (s) + 14); i.arg = opt; i.on = on; i.text = s; i.val = val; m_x += 4; }
	void glyph (int opt, const char *s, bool on, int style, int val = 0) { It &i = push (K_GLYPH, 26); i.arg = opt; i.on = on; i.text = s; i.val = val; i.sw = style; m_x += 4; }
	void drop (int opt, const char *s, int w) { It &i = push (K_DROP, w); i.arg = opt; i.text = s; m_x += 8; }
	void slider (int opt, const char *s, int w, int lo, int hi, const char *suffix)
	{
		s = TR (s);
		int *p = sliderVar (opt);
		char mx[16]; fmt_int (mx, hi, suffix);
		It &i = push (K_SLIDER, tw (s) + 10 + w + 10 + tw (mx) + 4);
		i.arg = opt; i.text = s; i.lo = lo; i.hi = hi; i.v = p ? *p : 0; i.suffix = suffix;
		i.sx = i.x + tw (s) + 10; i.sw = w;
		m_x += 6;
	}
	void layoutItems ()
	{
		if (g_tool != m_toolWas) { m_toolWas = g_tool; m_sx = 0; }
		m_n = 0; m_x = 10;
		switch (g_tool)
		{
		case T_SELECT:
			ico (g_selKind == SK_FREE ? I_LASSO : g_selKind == SK_WAND ? I_WAND : I_SELECT);
			toggle (O_SELRECT, "Rectangle", g_selKind == SK_RECT); toggle (O_SELFREE, "Free-form", g_selKind == SK_FREE); toggle (O_SELWAND, "Magic wand", g_selKind == SK_WAND);
			sep (); toggle (O_SELNEW, "New", g_selMode == SEL_REPLACE); toggle (O_SELADD, "Add (Shift)", g_selMode == SEL_ADD); toggle (O_SELSUB, "Subtract (Alt)", g_selMode == SEL_SUB);
			if (g_selKind == SK_WAND) { sep (); slider (O_TOL, "Tolerance", 90, 0, 255, ""); toggle (O_CONTIG, "Contiguous", g_contig); toggle (O_ALLLAYERS, "All layers", g_allLayers); }
			else { sep (); toggle (O_SELALL, "Select All", false); toggle (O_INVERT, "Invert", false); toggle (O_DESELECT, "Deselect", false); }
			break;
		case T_PENCIL:
			ico (I_PENCIL); slider (O_SIZE, "Size", 110, 1, 64, " px"); sep (); slider (O_OPACITY, "Opacity", 100, 1, 100, " %");
			break;
		case T_BRUSH:
			ico (I_BRUSH); drop (O_BRUSHKIND, TR (BRUSH_NAMES[g_brush]), 140);
			sep (); slider (O_SIZE, "Size", 110, 1, 200, " px"); sep (); slider (O_OPACITY, "Opacity", 100, 1, 100, " %");
			if (g_brush == BR_SOFT) { sep (); slider (O_HARD, "Hardness", 90, 0, 100, " %"); }
			break;
		case T_ERASER:
			ico (I_ERASER); slider (O_SIZE, "Size", 110, 1, 200, " px"); sep (); slider (O_OPACITY, "Opacity", 100, 1, 100, " %");
			sep (); slider (O_HARD, "Hardness", 90, 0, 100, " %");
			break;
		case T_FILL:
			ico (I_FILL);
			toggle (O_FILLCOL, "Colour", g_fillMode == FM_COLOUR); toggle (O_FILLGRAD, "Gradient", g_fillMode == FM_GRADIENT); toggle (O_FILLPAT, "Pattern", g_fillMode == FM_PATTERN);
			sep ();
			if (g_fillMode == FM_GRADIENT) gradOptions ();
			else if (g_fillMode == FM_PATTERN) { drop (O_PATTERN, TR (BRUSH_NAMES[g_pattern]), 110); sep (); }
			slider (O_TOL, "Tolerance", 56, 0, 255, ""); toggle (O_CONTIG, "Contiguous", g_contig); toggle (O_ALLLAYERS, "All layers", g_allLayers);
			if (g_fillMode != FM_GRADIENT) { sep (); slider (O_OPACITY, "Opacity", 70, 1, 100, " %"); }
			break;
		case T_GRADIENT:
			ico (I_GRADIENT); gradOptions (); slider (O_OPACITY, "Opacity", 90, 1, 100, " %");
			sep (); toggle (O_GERASE, "Erase (fade the layer)", g_gradErase);
			break;
		case T_TEXT:
		{
			ico (I_TEXT);
			drop (O_FONT, g_tfamNames[pclamp (g_tfam, 0, g_tnfam - 1)], 150);
			static char sz[12]; fmt_int (sz, g_tsize, " px"); drop (O_TSIZE, sz, 74);
			glyph (O_BOLD, "B", g_tbold, 2); glyph (O_ITALIC, "I", g_titalic, 1); glyph (O_UNDER, "U", g_tunder, 0);
			sep (); glyph (O_ALIGN, "L", g_talign == 0, 0, 0); glyph (O_ALIGN, "C", g_talign == 1, 0, 1); glyph (O_ALIGN, "R", g_talign == 2, 0, 2);
			sep (); toggle (O_SMOOTH, "Smooth edges", g_tsmooth); toggle (O_TBACK, "Background (colour 2)", g_tback);
			sep (); label (g_text.on ? "Enter: a new line.  Esc: cancel.  Click outside: put it down." : "Click on the picture, then type.");
			break;
		}
		case T_SHAPE:
			ico (I_OUTLINE); toggle (O_OUTLINE, "Outline (colour 1)", g_outline); toggle (O_FILLSH, "Fill (colour 2)", g_fillShape);
			sep (); slider (O_SIZE, "Width", 100, 1, 32, " px"); sep (); slider (O_OPACITY, "Opacity", 90, 1, 100, " %");
			sep (); label ("Shift: a square, a circle, a line at 45 degrees.");
			break;
		case T_PICKER: ico (I_PICKER); label ("Click: the chosen colour, right click: the other one -- from what is seen."); break;
		case T_ZOOM: ico (I_ZOOM); label ("Click: zoom in, right click: zoom out; a drag moves the picture.  Ctrl + wheel anywhere."); break;
		}
		m_over = m_x > width - 4;
		if (!m_over) m_sx = 0;
		m_sx = pclamp (m_sx, 0, pmax (0, m_x - width + 40));
		for (int i = 0; i < m_n; i++) { m_it[i].x -= m_sx; if (m_it[i].kind == K_SLIDER) m_it[i].sx -= m_sx; }
	}
	void gradOptions ()
	{
		It &g = push (K_GRAD, 100); g.arg = O_GRADLIST; m_x += 8;
		for (int k = 0; k < GS_COUNT; k++) { It &i = push (K_GSHAPE, 24); i.arg = O_GSHAPE; i.val = k; i.on = g_gshape == k; m_x += 2; }
		m_x += 6;
		drop (O_GREPEAT, TR (GREPEAT_NAMES[g_grepeat]), 98);
		toggle (O_GREVERSE, "Reverse", g_greverse);
		sep ();
	}
	void drawItem (int i, unsigned bg)
	{
		It &it = m_it[i];
		unsigned ink = C_TEXT, dim = C_DIM;
		int y = 5, h = height - 10;
		bool hot = i == m_hot;
		switch (it.kind)
		{
		case K_LABEL: text_l (it.x, 0, height, it.text, dim); break;
		case K_ICON: icon (it.icon, it.x, (height - 20) / 2, ink); break;
		case K_SEP: fill (it.x, 9, 1, height - 18, C_LINE); break;
		case K_TOGGLE: case K_GLYPH:
		{
			unsigned f = it.on ? mix (bg, C_ACCENT, 64) : hot ? mix (bg, ink, 30) : mix (bg, C_FIELD, 190);
			panel (it.x, y, it.w, h, 4, f, it.on ? mix (bg, C_ACCENT, 170) : C_LINE);
			text_c (it.x, y, it.w, h, it.text, ink, it.kind == K_GLYPH ? it.sw : 0);
			if (it.kind == K_GLYPH && it.arg == O_UNDER) fill (it.x + 9, y + h - 6, it.w - 18, 1, ink);
			break;
		}
		case K_DROP: case K_GRAD:
		{
			panel (it.x, y, it.w, h, 4, C_FIELD, hot ? C_ACCENT : C_LINE);
			if (it.kind == K_GRAD) grad_draw (it.x + 5, y + 5, it.w - 28, h - 10, g_grad);
			else text_l (it.x + 8, y, h, it.text, C_TEXT, 0, it.w - 26);
			chevron (it.x + it.w - 12, y + h / 2, C_TEXT);
			break;
		}
		case K_GSHAPE:
		{
			unsigned f = it.on ? mix (bg, C_ACCENT, 64) : hot ? mix (bg, ink, 30) : mix (bg, C_FIELD, 190);
			panel (it.x, y, it.w, h, 4, f, it.on ? mix (bg, C_ACCENT, 170) : C_LINE);
			gshape_draw (it.x + 3, y + (h - 18) / 2, 18, it.val);
			break;
		}
		case K_SLIDER:
		{
			int *p = sliderVar (it.arg); it.v = p ? *p : 0;
			text_l (it.x, 0, height, it.text, dim);
			slider_draw (it.sx, 0, it.sw, height, it.v, it.lo, it.hi, bg);
			char b[16]; fmt_int (b, it.v, it.suffix); text_l (it.sx + it.sw + 12, 0, height, b, ink);
			break;
		}
		}
	}
};

// ---- the layers' panel ---------------------------------------------------------------------------------------
enum { PANEL_W = 264, ROW_H = 54, THUMB_W = 54, THUMB_H = 36, PHEAD = 102 };
enum { L_ADD, L_DUP, L_DEL, L_UP, L_DOWN, L_MERGE, L_PROPS, L_SELECT, L_TOGGLE, L_RENAME, L_MENU, L_OPACITY, L_BLEND };
static void (*g_layerCmd) (int cmd, int arg, int x, int y);

class LayersPanel : public Part
{
public:
	LayersPanel () : m_hot (-1), m_hotBtn (-1), m_top (0), m_drag (false), m_barDrag (false), m_hotBlend (false) {}
	int listTop () const { return PHEAD; }
	int listH () const { return height - PHEAD - 44; }
	void sync () { invalidate (); }
	void paint ()
	{
		unsigned bg = pan_bg (), ink = C_TEXT, dim = C_DIM;
		fill (0, 0, width, height, bg);
		fill (0, 0, 1, height, C_LINE);
		text_l (14, 6, 26, TR ("Layers"), ink, 2);
		const Layer &cl = D.lay[D.cur];
		// the blend mode, the opacity
		text_l (14, 38, 24, TR ("Blend"), dim);
		panel (70, 38, width - 84, 24, 4, C_FIELD, m_hotBlend ? C_ACCENT : C_LINE);
		const char *bname = TR (BLEND_NAMES[pclamp (cl.blend, 0, NBLENDS - 1)]);
		char bn[80]; snprintf (bn, sizeof bn, cl.clip && (cl.blend == B_MASK || cl.blend == B_CUTOUT) ? TR ("%s (layer below)") : "%s", bname);
		text_l (78, 38, 24, bn, C_TEXT, 0, width - 84 - 30);
		chevron (width - 26, 50, C_TEXT);
		text_l (14, 68, 24, TR ("Opacity"), dim);
		int sw = width - 84 - 62;
		slider_draw (74, 68, sw, 24, (cl.opacity * 100 + 127) / 255, 0, 100, bg);
		char op[8]; fmt_int (op, (cl.opacity * 100 + 127) / 255, " %");
		panel (width - 62, 68, 48, 24, 4, C_FIELD, C_LINE);
		text_c (width - 62, 68, 48, 24, op, C_TEXT);
		// the list: the top layer first
		int y0 = listTop (), lh = listH ();
		panel (8, y0, width - 16, lh, 6, C_FIELD, C_LINE);
		int rows = pmax (1, (lh - 8) / ROW_H);
		m_top = pclamp (m_top, 0, pmax (0, D.n - rows));
		unsigned sel = C_ACCENT, seltx = ink_on (sel);
		for (int r = 0; r < rows; r++)
		{
			int li = D.n - 1 - (m_top + r);
			if (li < 0) break;
			int y = y0 + 4 + r * ROW_H;
			const Layer &l = D.lay[li];
			bool cur = li == D.cur;
			if (cur) rbox (11, y, width - 22, ROW_H - 2, 5, sel);
			else if (r == m_hot) rbox (11, y, width - 22, ROW_H - 2, 5, mix (C_FIELD, C_ACCENT, 30));
			unsigned tc = cur ? seltx : C_TEXT, dc = cur ? mix (sel, seltx, 190) : C_DIM;
			icon (l.visible ? I_EYE : I_EYEOFF, 16, y + (ROW_H - 2 - 20) / 2, tc);
			if (is_clip_mask (li)) { fill (44, y + 2, 2, ROW_H - 6, dc); fill (44, y + ROW_H - 6, 8, 2, dc); }
			thumb (li, is_clip_mask (li) ? 54 : 44, y + 8);
			int tx = (is_clip_mask (li) ? 54 : 44) + THUMB_W + 10;
			text_l (tx, y + 6, 18, l.name, tc, cur ? 2 : 0, width - tx - 40);
			char md[64]; scpy (md, TR (BLEND_NAMES[pclamp (l.blend, 0, NBLENDS - 1)]), sizeof md);
			if (l.opacity < 255 && l.blend != B_MASK) { int n = slen (md); char o2[12]; fmt_int (o2, (l.opacity * 100 + 127) / 255, " %"); scpy (md + n, " - ", (int) sizeof md - n); n = slen (md); scpy (md + n, o2, (int) sizeof md - n); }
			text_l (tx, y + 26, 18, md, dc, 0, width - tx - 40);
			if (l.blend == B_MASK || l.blend == B_CUTOUT) { ring2 (width - 30, y + ROW_H / 2, 7, dc, 20); disc (width - 30, y + ROW_H / 2, 3, dc); }
		}
		if (D.n > rows)
		{
			Thumb t = thumb_of (D.n, rows, m_top, lh - 8);
			rbox (width - 22, y0 + 4 + t.y, 8, t.h, 3, mix (C_FIELD, C_TEXT, 90));
		}
		// the buttons
		static const int BI[7] = { I_ADD, I_DUP, I_TRASH, I_UP, I_DOWN, I_MERGE, I_PROPS };
		int by = height - 38, bw = (width - 20) / 7;
		for (int i = 0; i < 7; i++)
		{
			int x = 10 + i * bw;
			bool off = (i == 2 && D.n < 2) || (i == 3 && D.cur >= D.n - 1) || (i == 4 && D.cur == 0) || (i == 5 && D.cur == 0) || (i < 2 && D.n >= MAXLAYERS);
			if (i == m_hotBtn && !off) hot_box (x, by, bw - 2, 30, bg, false, true);
			icon (BI[i], x + (bw - 22) / 2, by + 5, ink, off);
		}
	}
	void wheel (int w)
	{
		int rows = pmax (1, (listH () - 8) / ROW_H);
		m_top = pclamp (m_top - w, 0, pmax (0, D.n - rows));
		invalidate ();
	}
	void onMouse (int kind, int mx, int my, int bl, int br, int clicks)
	{
		bool in = kind != 3 && mx >= 0 && my >= 0 && mx < width && my < height;
		int y0 = listTop (), rows = pmax (1, (listH () - 8) / ROW_H), sw = width - 84 - 62;
		if (m_drag)
		{
			if (g_layerCmd) g_layerCmd (L_OPACITY, slider_val (mx, 74, sw, 0, 100), 0, 0);
			if (kind == 2) m_drag = false;
			return;
		}
		if (m_barDrag)
		{
			Thumb t = thumb_of (D.n, rows, m_top, listH () - 8);
			m_top = pclamp ((int) thumb_pos (my - y0 - 4, listH () - 8, D.n, rows, t.h), 0, pmax (0, D.n - rows));
			if (kind == 2) m_barDrag = false;
			invalidate ();
			return;
		}
		int bw = (width - 20) / 7, by = height - 38;
		int hb = in && my >= by && my < by + 30 && mx >= 10 && mx < 10 + 7 * bw ? (mx - 10) / bw : -1;
		int hr = in && my >= y0 + 4 && my < y0 + 4 + rows * ROW_H ? (my - y0 - 4) / ROW_H : -1;
		if (hr >= 0 && D.n - 1 - (m_top + hr) < 0) hr = -1;
		bool hbl = in && my >= 38 && my < 62 && mx >= 70 && mx < width - 14;
		if (hb != m_hotBtn || hr != m_hot || hbl != m_hotBlend)
		{
			m_hotBtn = hb; m_hot = hr; m_hotBlend = hbl;
			const char *tip = hb >= 0 ? BTN_TIPS[hb] : hr >= 0 ? "Click: the layer drawn on; the eye: shown / hidden; double click: its properties; right click: more" : hbl ? "How the layer mixes with what is under it" : 0;
			ui_w_tip (view, tip ? (long) TR (tip) : 0);
			invalidate ();
		}
		if (kind == 1 && (bl || br) && in)
		{
			int ax = left, ay = top;
			if (D.n > rows && hr >= 0 && mx >= width - 26 && bl) { m_barDrag = true; return; }	// (the list's bar, never the layer under it)
			if (hbl && bl) { if (g_layerCmd) g_layerCmd (L_BLEND, D.cur, ax + 70, ay + 64); return; }
			if (bl && my >= 68 && my < 92 && mx >= 66 && mx < 74 + sw + 8) { m_drag = true; if (g_layerCmd) g_layerCmd (L_OPACITY, slider_val (mx, 74, sw, 0, 100), 0, 0); return; }
			if (hb >= 0 && bl) { if (g_layerCmd) g_layerCmd (L_ADD + hb, 0, 0, 0); return; }
			if (hr >= 0)
			{
				int li = D.n - 1 - (m_top + hr);
				if (br) { if (g_layerCmd) { g_layerCmd (L_SELECT, li, 0, 0); g_layerCmd (L_MENU, li, ax + mx, ay + my); } return; }
				if (mx < 40) { if (g_layerCmd) g_layerCmd (L_TOGGLE, li, 0, 0); return; }
				if (g_layerCmd) g_layerCmd (clicks >= 2 ? L_RENAME : L_SELECT, li, 0, 0);
			}
		}
	}
private:
	int m_hot, m_hotBtn, m_top; bool m_drag, m_barDrag, m_hotBlend;
	static const char *const BTN_TIPS[7];
	// A layer's thumbnail: its pixels (the nearest ones) over a checkerboard, the picture's proportions.
	void thumb (int li, int x, int y)
	{
		const Layer &l = D.lay[li];
		int tw2 = THUMB_W, th = THUMB_H;
		if (D.w * THUMB_H > D.h * THUMB_W) th = pmax (4, D.h * THUMB_W / pmax (1, D.w)); else tw2 = pmax (4, D.w * THUMB_H / pmax (1, D.h));
		int ox2 = x + (THUMB_W - tw2) / 2, oy2 = y + (THUMB_H - th) / 2;
		for (int j = 0; j < th; j++)
			for (int i = 0; i < tw2; i++)
			{
				unsigned c = l.px[(unsigned) (j * D.h / th) * D.w + i * D.w / tw2], a = c >> 24;
				unsigned ck = ((i >> 2) ^ (j >> 2)) & 1 ? 0xD6D6D6 : 0xFFFFFF;
				put_px (ox2 + i, oy2 + j, a == 255 ? c & 0xFFFFFF : a ? mix (ck, c & 0xFFFFFF, (int) a + 1) : ck);
			}
		frame (ox2 - 1, oy2 - 1, tw2 + 2, th + 2, C_LINE);
	}
};
const char *const LayersPanel::BTN_TIPS[7] = { "New layer (Ctrl+L)", "Duplicate the layer", "Delete the layer", "Move the layer up", "Move the layer down",
	"Merge the layer into the one below", "The layer's properties: name, blend mode, opacity" };

// ---- the status bar --------------------------------------------------------------------------------------------
enum { STATUS_H = 30 };
static bool g_showLayers = true;				// the layers' panel shown
class StatusBar : public Part
{
public:
	CanvasView *view2;
	void (*zoomCmd) (int what);			// -1 out, +1 in, 0 the list (x of the popup in popX), 2 grid, 3 fit, 4 the layers' panel, 100+: a zoom (%)
	int popX;
	StatusBar (CanvasView *v) : view2 (v), zoomCmd (0), popX (0), m_hot (0), m_drag (false) {}
	void paint ()
	{
		unsigned bg = opt_bg (), ink = C_TEXT, dim = C_DIM;
		fill (0, 0, width, height, bg);
		fill (0, 0, width, 1, C_LINE);
		char b[64];
		int x = 10, wide = width >= 900;
		icon (I_POINTER, x, 5, ink); x += 24;
		if (view2->ptrX >= 0) { fmt2 (b, view2->ptrX, view2->ptrY, ", "); text_l (x, 0, height, b, ink); }
		x += 90;
		Rect s = D.fl.px ? float_rect () : g_sel;
		if (wide || !s.empty ())
		{
			icon (I_SELSIZE, x, 5, ink); x += 24;
			if (!s.empty ()) { fmt2 (b, s.x1 - s.x0, s.y1 - s.y0, " x "); text_l (x, 0, height, b, ink); }
			x += 100;
		}
		icon (I_IMGSIZE, x, 5, ink); x += 24;
		fmt2 (b, D.w, D.h, " x "); text_l (x, 0, height, b, ink);
		x += 104;
		const Layer &l = D.lay[D.cur];
		char lt[120]; snprintf (lt, sizeof lt, l.visible ? "%s (%s)" : TR ("%s (%s, hidden)"), l.name, TR (BLEND_NAMES[pclamp (l.blend, 0, NBLENDS - 1)]));
		if (x < zx () - 30) text_l (x, 0, height, lt, dim, 0, zx () - 16 - x);
		// the right: the layers, grid, fit, - slider +, the zoom
		int gx = zx ();
		hot_box (gx - 28, 3, 24, height - 6, bg, g_showLayers, m_hot == 7); icon (I_DUP, gx - 26, 5, ink);
		hot_box (gx, 3, 24, height - 6, bg, g_grid, m_hot == 4); icon (I_GRID, gx + 2, 5, ink);
		hot_box (gx + 28, 3, 24, height - 6, bg, false, m_hot == 5); icon (I_FIT, gx + 30, 5, ink);
		int mx = gx + 62;
		hot_box (mx, 3, 18, height - 6, bg, false, m_hot == 1); fill (mx + 5, height / 2, 8, 1, ink);
		slider_draw (mx + 26, 0, 110, height, zpos (view2->zoom), 0, 1000, bg);
		hot_box (mx + 144, 3, 18, height - 6, bg, false, m_hot == 2); fill (mx + 149, height / 2, 8, 1, ink); fill (mx + 153, height / 2 - 4, 1, 9, ink);
		char z[12]; fmt_int (z, view2->zoom, " %");
		hot_box (mx + 168, 3, 64, height - 6, bg, false, m_hot == 3);
		text_c (mx + 168, 0, 64, height, z, ink);
	}
	void onMouse (int kind, int mx, int my, int bl)
	{
		int gx = zx (), m0 = gx + 62;
		if (m_drag)
		{
			if (zoomCmd) zoomCmd (100 + zval (pclamp ((mx - m0 - 26) * 1000 / 109, 0, 1000)));
			if (kind == 2) m_drag = false;
			return;
		}
		bool in = kind != 3 && my >= 0 && my < height;
		int hot = !in ? 0 : mx >= gx - 28 && mx < gx - 4 ? 7 : mx >= gx && mx < gx + 24 ? 4 : mx >= gx + 28 && mx < gx + 52 ? 5 : mx >= m0 && mx < m0 + 18 ? 1 : mx >= m0 + 144 && mx < m0 + 162 ? 2 : mx >= m0 + 168 && mx < m0 + 232 ? 3 : mx >= m0 + 20 && mx < m0 + 142 ? 6 : 0;
		if (hot != m_hot)
		{
			m_hot = hot;
			const char *tip = hot == 7 ? "The layers' panel" : hot == 4 ? "The pixel grid (Ctrl+G; from 300 %)" : hot == 5 ? "The whole picture in the window" : hot == 6 ? "Zoom (Ctrl + wheel on the picture)" : 0;
			ui_w_tip (view, tip ? (long) TR (tip) : 0);
			invalidate ();
		}
		if (kind == 1 && bl && hot)
		{
			popX = left + m0 + 168;
			if (hot == 6) { m_drag = true; if (zoomCmd) zoomCmd (100 + zval (pclamp ((mx - m0 - 26) * 1000 / 109, 0, 1000))); }
			else if (zoomCmd) zoomCmd (hot == 1 ? -1 : hot == 2 ? 1 : hot == 3 ? 0 : hot == 4 ? 2 : hot == 7 ? 4 : 3);
		}
	}
private:
	int m_hot; bool m_drag;
	int zx () const { return width - 300; }
	// the zoom's slider: logarithmic, 12 % .. 3200 % on 0..1000
	static int zpos (int z) { return pclamp ((int) (logf (z / 12.0f) / logf (3200 / 12.0f) * 1000 + 0.5f), 0, 1000); }
	static int zval (int p) { return pclamp ((int) (12.0f * expf (p / 1000.0f * logf (3200 / 12.0f)) + 0.5f), 12, 3200); }
	static void fmt2 (char *b, int a, int c, const char *sep)
	{
		char t1[16], t2[16]; fmt_int (t1, a, 0); fmt_int (t2, c, " px");
		scpy (b, t1, 48); int n = slen (b); scpy (b + n, sep, 48 - n); n = slen (b); scpy (b + n, t2, 48 - n);
	}
};

} // namespace pd

#endif
