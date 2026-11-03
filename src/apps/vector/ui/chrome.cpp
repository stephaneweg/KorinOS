//
// chrome.cpp -- everything around the canvas (docs/UX-VECTOR.md 2 and 4): the bar, the rail, the bottom line, the panels.
// One function places everything from the content's size: layout (). All colours are UIKit's tokens.
//
#include "editor.h"
#include "../text/fonts.h"
#include "lpe.h"
#include "include/core/SkSurface.h"
#include <cstdio>
#include <cstring>

using namespace vd;
using namespace vr;

namespace vapp { bool read_file (const char *path, std::string &out); }

namespace ve {

extern struct ui_vars ui;
extern const unsigned *s_jet;
unsigned jet (int j) { return s_jet[j] & 0xFFFFFF; }
void canvas_place (int x, int y, int w, int h);		// window.cpp

enum { BAR_H = 36, RAIL_W = 40, BOTTOM_H = 32, PANEL_S = 248, CELL = 40, TABS = 32, TAB_H = 60 };

// ---- icons: our own, 24 x 24, one colour (docs/UX-VECTOR.md 3.3) --------------------------------------------------------
enum Icon { I_SELECT, I_NODE, I_RECT, I_ELLIPSE, I_STAR, I_SPIRAL, I_PEN, I_PENCIL, I_TEXT, I_GRADIENT, I_DROPPER, I_HAND, I_ZOOM,
	I_CALLI, I_ERASER, I_BUCKET, I_TWEAK, I_SPRAY, I_CONNECT, I_MEASURE, I_BUILDER, I_UNDO, I_REDO, I_DRAWER, I_LAYERS, I_FILLS, I_TRANSFORM, I_EYE, I_LOCK, I_PLUS, I_MINUS, I_XML, I_DOC, I_MORE, I_ALIGN, I_UNLOCK, I_UP, I_DOWN, I_COUNT };
static const char *const ICON[I_COUNT] = {
	"<path d='M5 3 L5 19 L9.5 15 L12.5 21 L15 20 L12 14 L18 14 Z' fill='#000' stroke='none'/>",
	"<path d='M3 19 C8 19 8 5 12 5 C16 5 16 19 21 19'/><rect x='9' y='2' width='6' height='6' fill='#000' stroke='none'/><circle cx='3.5' cy='19' r='2' fill='#000' stroke='none'/><circle cx='20.5' cy='19' r='2' fill='#000' stroke='none'/>",
	"<path d='M4 6 L14 6 Q20 6 20 12 L20 18 L4 18 Z'/>",
	"<ellipse cx='12' cy='12' rx='9' ry='7'/><circle cx='12' cy='12' r='1.2' fill='#000' stroke='none'/>",
	"<path d='M12 3 L14.6 9.2 L21 9.8 L16.2 14 L17.6 20.5 L12 17 L6.4 20.5 L7.8 14 L3 9.8 L9.4 9.2 Z'/>",
	"<path d='M12 12 C12 10 15 10 15 12.5 C15 16 9 16 9 12 C9 6.5 18 6.5 18 12.5 C18 20 5 20 5 12 C5 7 8 4 12 4'/>",
	"<path d='M4 20 L6 13 L15 4 L20 9 L11 18 Z'/><path d='M4 20 L10 14'/><circle cx='11' cy='13' r='1.3' fill='#000' stroke='none'/>",
	"<path d='M4 20 L5 15 L16 4 L20 8 L9 19 Z'/><path d='M14 6 L18 10'/>",
	"<path d='M5 7 L5 4 L19 4 L19 7 M12 4 L12 20 M9 20 L15 20'/>",
	"<rect x='3' y='5' width='18' height='14' rx='1'/><path d='M8 5 L8 19 M12.5 5 L12.5 19 M16.5 5 L16.5 19'/>",
	"<path d='M14 5 L19 10 M5 19 L5 16 L13 8 L16 11 L8 19 Z'/><path d='M15 3.5 L20.5 9' stroke-width='3'/>",
	"<path d='M7 13 L7 6.5 C7 5 9 5 9 6.5 L9 11 M9 11 L9 4.5 C9 3 11 3 11 4.5 L11 11 M11 11 L11 5 C11 3.5 13 3.5 13 5 L13 11 M13 11 L13 6.5 C13 5 15 5 15 6.5 L15 15 C15 19 13 21 10 21 C7 21 6 19 4 15 L3.5 14 C3 12.5 4.5 12 5.5 13 L7 15'/>",
	"<circle cx='10' cy='10' r='6.5'/><path d='M15 15 L21 21 M7 10 L13 10 M10 7 L10 13'/>",
	"<path d='M3 20 C7 19 9 11 13 8 L20 3 L15 11 C12 15 10 20 3 20 Z' fill='#000' stroke='none'/>",
	"<path d='M9 18 L3.5 12.5 L11 5 L20 13 L14.5 18 Z M7 8.5 L16.5 17 M4 21 L20 21'/>",
	"<path d='M4 11 L12 3 L20 11 L12 19 Z M4 11 L20 11'/><path d='M20 15 C21.5 17.5 22 18.5 22 19.5 C22 21.5 18.5 21.5 18.5 19.5 C18.5 18.5 19 17.5 20 15 Z' fill='#000' stroke='none'/>",
	"<path d='M3 15 C6 11 8 19 11 15 C14 11 16 19 19 15'/><path d='M9 3 L14 8 L9 13 M14 8 L4 8'/>",
	"<path d='M10 21 L10 11 L16 11 L16 21 Z M12 11 L12 8 L14 8 L14 11 M12 8 L9 8'/><circle cx='5' cy='5' r='1.3' fill='#000' stroke='none'/><circle cx='8' cy='3' r='1.3' fill='#000' stroke='none'/><circle cx='4' cy='9' r='1.3' fill='#000' stroke='none'/>",
	"<rect x='3' y='3' width='7' height='7' rx='1'/><rect x='14' y='14' width='7' height='7' rx='1'/><path d='M10 6.5 L17 6.5 L17 14'/>",
	"<path d='M3 18 L18 3 L21 6 L6 21 Z M7 14 L9 16 M10 11 L13 14 M13 8 L15 10 M16 5 L19 8'/>",
	"<circle cx='9' cy='12' r='6'/><circle cx='15' cy='12' r='6'/><path d='M12 6.8 C14 9 14 15 12 17.2 C10 15 10 9 12 6.8 Z' fill='#000' stroke='none'/>",
	"<path d='M9 7 L4 12 L9 17 M4 12 L15 12 C20 12 20 20 15 20'/>",
	"<path d='M15 7 L20 12 L15 17 M20 12 L9 12 C4 12 4 20 9 20'/>",
	"<rect x='3' y='5' width='18' height='14' rx='1.5'/><path d='M15 5 L15 19'/>",
	"<path d='M12 4 L21 9 L12 14 L3 9 Z M3 14 L12 19 L21 14'/>",
	"<rect x='4' y='4' width='11' height='11' fill='#000' stroke='none'/><rect x='10' y='10' width='10' height='10'/>",
	"<rect x='5' y='5' width='14' height='14' stroke-dasharray='3 2.5'/><path d='M9 12 L15 12 M12 9 L12 15'/>",
	"<path d='M2 12 C6 5 18 5 22 12 C18 19 6 19 2 12 Z'/><circle cx='12' cy='12' r='2.5' fill='#000' stroke='none'/>",
	"<rect x='6' y='11' width='12' height='9' rx='1' fill='#000' stroke='none'/><path d='M8 11 L8 8 C8 3 16 3 16 8 L16 11'/>",
	"<path d='M6 12 L18 12 M12 6 L12 18'/>",
	"<path d='M6 12 L18 12'/>",
	"<path d='M8 7 L3 12 L8 17 M16 7 L21 12 L16 17 M14 5 L10 19'/>",
	"<path d='M6 3 L14 3 L19 8 L19 21 L6 21 Z M14 3 L14 8 L19 8'/>",
	"<circle cx='5' cy='12' r='1.6' fill='#000' stroke='none'/><circle cx='12' cy='12' r='1.6' fill='#000' stroke='none'/><circle cx='19' cy='12' r='1.6' fill='#000' stroke='none'/>",
	"<path d='M4 3 L4 21'/><rect x='7' y='6' width='13' height='4' fill='#000' stroke='none'/><rect x='7' y='14' width='8' height='4' fill='#000' stroke='none'/>",
	"<rect x='6' y='11' width='12' height='9' rx='1'/><path d='M8 11 L8 8 C8 3 15 3 16 7'/>",
	"<path d='M12 19 L12 5 M6 11 L12 5 L18 11'/>",
	"<path d='M12 5 L12 19 M6 13 L12 19 L18 13'/>",
};
static uint8_t s_mask[I_COUNT][24 * 24];

static void make_icons ()
{
	std::vector<uint32_t> px (24 * 24 * 16);
	for (int i = 0; i < I_COUNT; i++)
	{
		std::string s = "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24' fill='none' stroke='#000' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>";
		s += ICON[i];
		s += "</svg>";
		Document d;
		d.load (s.data (), s.size (), nullptr);
		SceneBuilder b (&d);
		auto sc = b.update ();
		render_page (*sc, 1, px.data (), 24, 24, 0, true);
		for (int k = 0; k < 24 * 24; k++) s_mask[i][k] = (uint8_t) (px[k] >> 24);
	}
}
void paint_icon (int icon, int x, int y, unsigned color, uint32_t *px, int pw, int ph)
{
	unsigned r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
	for (int j = 0; j < 24; j++)
	{
		int yy = y + j;
		if (yy < 0 || yy >= ph) continue;
		for (int i = 0; i < 24; i++)
		{
			int xx = x + i;
			unsigned a = s_mask[icon][j * 24 + i];
			if (!a || xx < 0 || xx >= pw) continue;
			uint32_t &d = px[(long) yy * pw + xx];
			unsigned dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
			d = 0xFF000000 | (dr + (r - dr) * a / 255) << 16 | (dg + (g - dg) * a / 255) << 8 | (db + (b - db) * a / 255);
		}
	}
}
static long font () { return (long) ui.font; }
// a menu at a point: UIKit wants its labels one after the other, an empty one at the end
static long popup_at (int x, int y, const std::string &lines, int w)
{
	std::string t = lines;
	for (auto &c : t) if (c == '\n') c = 0;
	t.push_back (0);
	t.push_back (0);
	return ui_popup_local (x, y, (long) t.data (), w);
}
static void icon_in (long view, int icon, int x, int y, unsigned color)
{
	long w, h;
	uint32_t *px = uikit_canvas_pixels (view, &w, &h);
	if (px) paint_icon (icon, x, y, color, px, (int) w, (int) h);
}

// ---- state -------------------------------------------------------------------------------------------------------------
static long vBar, vRail, vBottom, vCanvas, vPanel;
static long fX, fY, fW, fH, fHex, fStroke, fMoveX, fMoveY, fRot, fScale, fFont, fAttrs, fDocW, fDocH, fGrid;
static long vRulH, vRulV;
static int s_rulers;			// their thickness, 0 when hidden
static int W, H;			// the content
static bool s_panel;			// the drawer is open
static int s_tab, s_page;		// tab of the panel; page of Fill and Stroke: 0 fill, 1 stroke, 2 stroke style
static int s_railHover = -1;

// the rail: a cell shows the last tool used of its family (docs/UX-VECTOR.md 3.1)
struct Family { int tools[5]; int n; int cur; };
static Family s_fam[] = { { { T_SELECT }, 1, 0 }, { { T_NODE }, 1, 0 }, { { T_RECT, T_ELLIPSE, T_STAR, T_SPIRAL }, 4, 0 }, { { T_PEN, T_PENCIL, T_CALLI }, 3, 0 },
	{ { T_TEXT }, 1, 0 }, { { T_GRADIENT }, 1, 0 }, { { T_BUCKET, T_DROPPER }, 2, 0 }, { { T_ERASER, T_BUILDER, T_TWEAK, T_SPRAY }, 4, 0 },
	{ { T_CONNECT, T_MEASURE }, 2, 0 }, { { T_HAND }, 1, 0 }, { { T_ZOOM }, 1, 0 } };
enum { N_FAM = sizeof s_fam / sizeof s_fam[0] };

static const char *tool_name (int t)
{
	static const char *const N[] = { "Selector", "Nodes", "Rectangle", "Ellipse", "Star", "Spiral", "Pen", "Pencil", "Text", "Gradient", "Dropper", "Hand", "Zoom",
		"Calligraphy", "Eraser", "Bucket", "Tweak", "Spray", "Connector", "Measure", "Shape Builder" };
	return TR (N[t]);
}

// ---- rail ---------------------------------------------------------------------------------------------------------------
static void rail_paint (long v, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_fill (w - 1, 0, 1, h, jet (J_LIGNE));
	for (int i = 0; i < N_FAM; i++)
	{
		Family &f = s_fam[i];
		int t = f.tools[f.cur], y = i * CELL;
		bool active = false;
		for (int k = 0; k < f.n; k++) if (f.tools[k] == E.tool) { active = true; f.cur = k; t = E.tool; }
		if (active) ui_rbox (3, y + 3, 34, 34, 6, jet (J_ACCENT), jet (J_ACCENT));
		else if (i == s_railHover) ui_rbox (3, y + 3, 34, 34, 6, jet (J_CREUX), jet (J_CREUX));
		icon_in (v, t, 8, y + 8, active ? (unsigned) ui_ink (jet (J_ACCENT)) : i == s_railHover ? jet (J_TEXTE1) : jet (J_TEXTE2));
		if (f.n > 1) for (int k = 0; k < 5; k++) ui_fill (34 - k, y + 30 + k, k + 1, 1, jet (J_TEXTE3));
	}
}
static long rail_mouse (long, long kind, long, long y, long info)
{
	int i = (int) (y / CELL);
	if (i >= N_FAM) i = -1;
	if (kind == 0 || kind == 3)
	{
		int nh = kind == 3 ? -1 : i;
		if (nh != s_railHover) { s_railHover = nh; ui_view_dirty (vRail); }
		if (nh >= 0 && kind == 0) ui_tip_at (44, 36 + nh * CELL + 10, (long) tool_name (s_fam[nh].tools[s_fam[nh].cur]));
		else ui_tip_at (0, 0, 0);
		return 0;
	}
	if (kind == 1 && i >= 0)
	{
		ui_tip_at (0, 0, 0);
		Family &f = s_fam[i];
		bool active = f.tools[f.cur] == E.tool;
		if (f.n > 1 && (active || (info & 0xFF00) >> 8 == 2))
		{
			std::string list;
			for (int k = 0; k < f.n; k++) { if (k) list += '\n'; list += tool_name (f.tools[k]); }
			long c = popup_at (RAIL_W, BAR_H + i * CELL, list, 160);
			s_railHover = -1;
			ui_view_dirty (vRail);
			if (c >= 0) { f.cur = (int) c; E.set_tool (f.tools[c]); }
		}
		else E.set_tool (f.tools[f.cur]);
	}
	return 0;
}

// ---- the bar ------------------------------------------------------------------------------------------------------------
static int s_barX[16], s_barW[16], s_barN;
static bool s_rulersSet;
static int bar_right () { return W - 107 - 110; }
static void bar_paint (long v, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_fill (0, h - 1, w, 1, jet (J_LIGNE));
	int x = bar_right ();
	icon_in (v, I_UNDO, x + 4, 6, E.doc.can_undo () ? jet (J_TEXTE2) : jet (J_TEXTE3));
	icon_in (v, I_REDO, x + 40, 6, E.doc.can_redo () ? jet (J_TEXTE2) : jet (J_TEXTE3));
	icon_in (v, I_DRAWER, x + 76, 6, s_panel ? jet (J_ACCENT) : jet (J_TEXTE2));
	if (E.tool == T_SELECT)
	{
		static const char *const L[] = { "X", "Y", "L", "H" };
		for (int i = 0; i < 4; i++) ui_text ((long) L[i], 8 + i * 96, 23, jet (J_TEXTE2), 12, font (), 0);
	}
	else
	{
		Tool *t = E.tools[E.tool];
		int n = t->bar_count (), bx = 6;
		s_barN = 0;
		for (int i = 0; i < n && s_barN < 16; i++)
		{
			const char *l = t->bar_label (i);
			int tw = (int) ui_text_width ((long) l, 12, font ()) + 16;
			if (bx + tw > x - 8) break;
			ui_rbox (bx, 5, tw, 26, 5, jet (J_CREUX), jet (J_CREUX));
			ui_text ((long) l, bx + 8, 23, jet (J_TEXTE1), 12, font (), 0);
			s_barX[s_barN] = bx; s_barW[s_barN++] = tw;
			bx += tw + 4;
		}
		if (bx + 80 < x) ui_text_fit ((long) t->hint (), bx + 6, 23, jet (J_TEXTE2), 12, font (), x - bx - 14);
	}
}
static long bar_mouse (long, long kind, long x, long, long)
{
	if (kind != 1) return 0;
	if (E.tool != T_SELECT)
		for (int i = 0; i < s_barN; i++)
			if (x >= s_barX[i] && x < s_barX[i] + s_barW[i]) { E.tools[E.tool]->bar_click (i); E.chromeDirty = true; return 0; }
	int r = bar_right ();
	if (x >= r && x < r + 32) command (C_UNDO);
	else if (x >= r + 36 && x < r + 68) command (C_REDO);
	else if (x >= r + 72 && x < r + 104) panel_toggle ();
	return 0;
}

static void put_field (long f, double v, bool any)
{
	char b[32] = "";
	if (any) snprintf (b, sizeof b, "%.2f", v);
	ui_w_set_text (f, (long) b);
	ui_w_enable (f, any);
}
static void sync_fields ()
{
	bool sel = E.tool == T_SELECT;
	for (long f : { fX, fY, fW, fH }) ui_w_show (f, sel);
	if (!sel) return;
	Box b = selection_box ();
	bool any = !E.sel.empty () && !b.empty ();
	for (long f : { fX, fY, fW, fH }) if (ui_w_get_focus () == f) return;
	put_field (fX, b.x0, any); put_field (fY, b.y0, any); put_field (fW, b.w (), any); put_field (fH, b.h (), any);
}
static void apply_fields ()
{
	Box b = selection_box ();
	if (b.empty ()) return;
	double x = atof ((const char *) ui_w_get_text (fX)), y = atof ((const char *) ui_w_get_text (fY));
	double w = atof ((const char *) ui_w_get_text (fW)), h = atof ((const char *) ui_w_get_text (fH));
	if (w <= 0 || h <= 0 || b.w () <= 0 || b.h () <= 0) return;
	Affine m = Affine::translate (x, y) * Affine::scale (w / b.w (), h / b.h ()) * Affine::translate (-b.x0, -b.y0);
	E.doc.begin (TR ("Transform"));
	transform_selection (m);
	E.doc.commit ();
	E.sync ();
}

// ---- bottom -------------------------------------------------------------------------------------------------------------
static const unsigned PALETTE[] = { 0x000000, 0x444444, 0x888888, 0xCCCCCC, 0xFFFFFF, 0xC62828, 0xEF6C00, 0xF9A825, 0x2E7D32, 0x00897B, 0x0277BD, 0x283593, 0x6A1B9A, 0xAD1457, 0x6D4C41, 0x4992A7, 0xFFCDD2, 0xBBDEFB };
enum { NPAL = sizeof PALETTE / sizeof PALETTE[0], SW = 28 };

static unsigned paint_color (const std::string &s, bool *none)
{
	uint32_t c = 0;
	*none = s == "none" || s.empty ();
	if (!*none && !parse_color (s.c_str (), c)) c = 0x808080;
	return c;
}
static void current_paints (std::string &f, std::string &s)
{
	f = E.fill; s = E.stroke;
	if (E.sel.size () >= 1 && E.sel[0]->element ())
	{
		Style st, base;
		XNode *of = E.sel[0];
		// (a clone shows what it repeats)
		if (!strcmp (el_name (of), "use"))
		{
			const char *h = of->get ("xlink:href");
			if (!h) h = of->get ("href");
			if (XNode *t = h && h[0] == '#' ? E.doc.by_id (h + 1) : nullptr) of = t;
		}
		compute_style (of, base, E.builder->sheet, st);
		auto name = [] (const PaintSpec &p) -> std::string {
			if (p.kind == P_NONE) return "none";
			if (p.kind == P_URL) return "#808080";
			char b[12]; snprintf (b, sizeof b, "#%06x", p.rgb); return b;
		};
		f = name (st.fill); s = name (st.stroke);
	}
}
static void bottom_paint (long, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_fill (0, 0, w, 1, jet (J_LIGNE));
	std::string f, s;
	current_paints (f, s);
	bool nf, ns;
	unsigned cf = paint_color (f, &nf), cs = paint_color (s, &ns);
	ui_fill (4, 6, 26, 20, nf ? jet (J_SURFACE) : cf);
	ui_frame (4, 6, 26, 20, jet (J_LIGNE_FORTE));
	if (nf) ui_vp_line (6 * 16, 24 * 16, 28 * 16, 8 * 16, 2 * 16, jet (J_DANGER));
	ui_fill (34, 6, 26, 20, jet (J_LIGNE_FORTE));
	ui_fill (37, 9, 20, 14, ns ? jet (J_SURFACE) : cs);
	if (ns) ui_vp_line (38 * 16, 22 * 16, 56 * 16, 10 * 16, 2 * 16, jet (J_DANGER));
	int x = 72;
	int pal = std::min ((int) NPAL, (W - 72 - 210) / SW);
	for (int i = 0; i < pal; i++)
	{
		ui_fill (x + i * SW + 1, 3, SW - 2, h - 6, PALETTE[i]);
		ui_frame (x + i * SW + 1, 3, SW - 2, h - 6, jet (J_LIGNE));
	}
	int zx = W - 100;
	const char *ln = TR ("Root");
	XNode *l = current_layer ();
	if (l != E.doc.root) ln = l->get2 ("inkscape:label", l->get2 ("id", ""));
	ui_text_fit ((long) ln, W - 200, 21, jet (J_TEXTE2), 12, font (), 80);
	int tx = W - 112;
	for (int k = 0; k < 3; k++) ui_fill (tx + k, 16 + k, 5 - k * 2, 1, jet (J_TEXTE3));
	char z[16];
	snprintf (z, sizeof z, "%.0f%%", E.view.zoom * 100);
	ui_text ((long) "\xE2\x88\x92", zx, 21, jet (J_TEXTE2), 14, font (), 0);
	ui_text ((long) z, zx + 22, 21, jet (J_TEXTE1), 12, font (), 0);
	ui_text ((long) "+", zx + 70, 21, jet (J_TEXTE2), 14, font (), 0);
	ui_fill (W - 10, 12, 8, 8, E.doc.modified () ? jet (J_ALERTE) : jet (J_FACE));
}
static long bottom_mouse (long, long kind, long x, long, long info)
{
	if (kind != 1) return 0;
	bool shift = (info >> 16) & 0x22;
	int pal = std::min ((int) NPAL, (W - 72 - 210) / SW);
	if (x < 64) { s_page = x < 32 ? 0 : 1; panel_show (0); return 0; }
	if (x >= 72 && x < 72 + pal * SW)
	{
		char c[12];
		snprintf (c, sizeof c, "#%06x", PALETTE[(x - 72) / SW]);
		if (E.sel.empty ()) { (shift ? E.stroke : E.fill) = c; E.chromeDirty = true; }
		else style_selection (shift ? "stroke" : "fill", c, TR ("Colour"));
		return 0;
	}
	int zx = W - 100;
	if (x >= W - 200 && x < zx - 8)
	{
		std::vector<XNode *> ls;
		std::string list;
		for (XNode *k : E.doc.root->kids) if (k->element () && is_layer (k)) { ls.push_back (k); list += k->get2 ("inkscape:label", k->get2 ("id", "")); list += '\n'; }
		list += TR ("New Layer");
		long c = popup_at (W - 200, H - BOTTOM_H - 28 * ((long) ls.size () + 1) - 12, list, 160);
		if (c >= 0 && c < (long) ls.size ()) { E.layer = ls[c]; E.chromeDirty = true; }
		else if (c == (long) ls.size ()) command (C_NEWLAYER);
		return 0;
	}
	if (x >= zx - 4 && x < zx + 18) command (C_ZOOMOUT);
	else if (x >= zx + 62 && x < zx + 88) command (C_ZOOMIN);
	else if (x >= zx + 18 && x < zx + 62)
	{
		long c = popup_at (zx, H - BOTTOM_H - 170, std::string ("50 %\n100 %\n200 %\n") + TR ("Selection") + "\n" + TR ("Drawing") + "\n" + TR ("Page"), 120);
		if (c == 0) zoom_at (0.5 / E.view.zoom, { E.cw / 2.0, E.ch / 2.0 });
		else if (c == 1) command (C_ZOOM100);
		else if (c == 2) command (C_ZOOM200);
		else if (c == 3) command (C_ZOOMSEL);
		else if (c == 4) command (C_ZOOMDRAW);
		else if (c == 5) command (C_ZOOMPAGE);
	}
	return 0;
}

// ---- Fill and Stroke ----------------------------------------------------------------------------------------------------
static double s_h, s_s = 0.5, s_v = 0.7, s_a = 1;	// the colour being edited
static int s_drag;				// 1 zone, 2 hue, 3 alpha, 4 width, 5 opacity

static void hsv_rgb (double h, double s, double v, int &r, int &g, int &b)
{
	double c = v * s, x = c * (1 - std::fabs (std::fmod (h * 6, 2) - 1)), m = v - c, R = 0, G = 0, B = 0;
	int i = (int) (h * 6) % 6;
	switch (i) { case 0: R = c; G = x; break; case 1: R = x; G = c; break; case 2: G = c; B = x; break; case 3: G = x; B = c; break; case 4: R = x; B = c; break; default: R = c; B = x; }
	r = (int) ((R + m) * 255 + .5); g = (int) ((G + m) * 255 + .5); b = (int) ((B + m) * 255 + .5);
}
static void rgb_hsv (unsigned c, double &h, double &s, double &v)
{
	double r = ((c >> 16) & 255) / 255.0, g = ((c >> 8) & 255) / 255.0, b = (c & 255) / 255.0;
	double mx = std::max (r, std::max (g, b)), mn = std::min (r, std::min (g, b)), d = mx - mn;
	v = mx; s = mx > 0 ? d / mx : 0;
	if (d <= 0) return;
	if (mx == r) h = std::fmod ((g - b) / d + 6, 6) / 6;
	else if (mx == g) h = ((b - r) / d + 2) / 6;
	else h = ((r - g) / d + 4) / 6;
}
enum { ZX = 8, ZW = 232, ZY = 100, ZH = 112, HY = 220, BARH = 20, AY = 246, FIELDY = 272 };

static void load_color ()
{
	std::string f, s;
	current_paints (f, s);
	const std::string &c = s_page == 0 ? f : s;
	bool none;
	unsigned rgb = paint_color (c, &none);
	if (!none) { double h = s_h; rgb_hsv (rgb, h, s_s, s_v); s_h = h; }
	char b[12];
	snprintf (b, sizeof b, "#%06x", rgb);
	if (ui_w_get_focus () != fHex) ui_w_set_text (fHex, (long) (none ? "" : b));
}
static void apply_color (bool drag)
{
	int r, g, b;
	hsv_rgb (s_h, s_s, s_v, r, g, b);
	char c[12];
	snprintf (c, sizeof c, "#%02x%02x%02x", r, g, b);
	if (E.sel.empty ()) { (s_page == 0 ? E.fill : E.stroke) = c; E.chromeDirty = true; }
	else style_selection (s_page == 0 ? "fill" : "stroke", c, TR ("Colour"), drag ? 11 : 0);
	ui_w_set_text (fHex, (long) c);
}

static void fill_panel (long v, int w)
{
	int pw = w - 16;
	static const char *const P[] = { "Fill", "Stroke", "Style" };
	for (int i = 0; i < 3; i++)
	{
		int x = 8 + i * pw / 3, sw = pw / 3 - 2;
		ui_rbox (x, TAB_H + 2, sw, 26, 5, i == s_page ? jet (J_ACCENT) : jet (J_CREUX), i == s_page ? jet (J_ACCENT) : jet (J_CREUX));
		ui_text ((long) TR (P[i]), x + 8, TAB_H + 20, i == s_page ? (unsigned) ui_ink (jet (J_ACCENT)) : jet (J_TEXTE1), 12, font (), 0);
	}
	if (s_page == 2)
	{
		char b[32];
		double sw = E.strokeWidth;
		double op = 1;
		if (!E.sel.empty ())
		{
			Style st, base;
			compute_style (E.sel[0], base, E.builder->sheet, st);
			sw = st.strokeWidth; op = st.opacity;
		}
		ui_text ((long) TR ("Width"), 8, 120, jet (J_TEXTE2), 12, font (), 0);
		(void) b;
		ui_text ((long) TR ("Opacity"), 8, 160, jet (J_TEXTE2), 12, font (), 0);
		ui_fill (8, 168, pw, 8, jet (J_CREUX));
		ui_fill (8, 168, (int) (pw * op), 8, jet (J_ACCENT));
		// the three rows of cases: caps, joins, blend mode
		static const char *const CAPS[] = { "Butt", "Round", "Square" }, *const JOINS[] = { "Miter", "Round", "Bevel" };
		int cap = 0, join = 0;
		const char *blend = "normal";
		if (!E.sel.empty ())
		{
			Style st, base;
			compute_style (E.sel[0], base, E.builder->sheet, st);
			cap = st.lineCap; join = st.lineJoin;
			std::string m = get_prop (E.sel[0], "mix-blend-mode");
			static std::string keep;
			keep = m.empty () ? "normal" : m;
			blend = keep.c_str ();
		}
		for (int r = 0; r < 2; r++)
			for (int i = 0; i < 3; i++)
			{
				int x = 8 + i * pw / 3, y = 188 + r * 32, on = r ? join : cap;
				ui_rbox (x, y, pw / 3 - 3, 28, 5, on == i ? jet (J_ACCENT) : jet (J_CREUX), on == i ? jet (J_ACCENT) : jet (J_CREUX));
				ui_text ((long) TR (r ? JOINS[i] : CAPS[i]), x + 8, y + 19, on == i ? (unsigned) ui_ink (jet (J_ACCENT)) : jet (J_TEXTE1), 12, font (), 0);
			}
		ui_text ((long) TR ("Blend"), 8, 272, jet (J_TEXTE2), 12, font (), 0);
		ui_rbox (64, 254, pw - 56, 28, 5, jet (J_CREUX), jet (J_CREUX));
		ui_text ((long) blend, 72, 273, jet (J_TEXTE1), 12, font (), 0);
		if (ui_w_get_focus () != fStroke) { char t[24]; snprintf (t, sizeof t, "%.3g", sw); ui_w_set_text (fStroke, (long) t); }
		return;
	}
	long pwd, phg;
	uint32_t *px = uikit_canvas_pixels (v, &pwd, &phg);
	if (!px) return;
	int zw = pw;
	for (int y = 0; y < ZH; y++)
		for (int x = 0; x < zw; x++)
		{
			int r, g, b;
			hsv_rgb (s_h, x / (double) (zw - 1), 1 - y / (double) (ZH - 1), r, g, b);
			px[(long) (ZY + y) * pwd + ZX + x] = 0xFF000000u | r << 16 | g << 8 | b;
		}
	for (int x = 0; x < zw; x++)
	{
		int r, g, b;
		hsv_rgb (x / (double) zw, 1, 1, r, g, b);
		for (int y = 0; y < BARH; y++) px[(long) (HY + y) * pwd + ZX + x] = 0xFF000000u | r << 16 | g << 8 | b;
	}
	ui_frame (ZX, ZY, zw, ZH, jet (J_LIGNE));
	int cx = ZX + (int) (s_s * (zw - 1)), cy = ZY + (int) ((1 - s_v) * (ZH - 1));
	ui_vp_disc (cx * 16, cy * 16, 7 * 16, 0x000000);
	ui_vp_disc (cx * 16, cy * 16, 6 * 16, 0xFFFFFF);
	{ int r, g, b; hsv_rgb (s_h, s_s, s_v, r, g, b); ui_vp_disc (cx * 16, cy * 16, 4 * 16, r << 16 | g << 8 | b); }
	int hx = ZX + (int) (s_h * (zw - 1));
	ui_fill (hx - 2, HY - 1, 5, BARH + 2, 0xFFFFFF);
	ui_frame (hx - 3, HY - 2, 7, BARH + 4, 0x000000);
	ui_text ((long) "#", 8, FIELDY + 19, jet (J_TEXTE2), 13, font (), 0);
	int r, g, b;
	hsv_rgb (s_h, s_s, s_v, r, g, b);
	ui_rbox (8 + 120, FIELDY + 1, pw - 120, 26, 4, r << 16 | g << 8 | b, r << 16 | g << 8 | b);
	static const char *const NONE = "None";
	ui_rbox (8, FIELDY + 36, 96, 26, 5, jet (J_CREUX), jet (J_CREUX));
	ui_text ((long) TR (NONE), 16, FIELDY + 54, jet (J_TEXTE1), 12, font (), 0);
}

// ---- Layers -------------------------------------------------------------------------------------------------------------
enum { ROW = 28 };
struct Row { XNode *n; int depth; };
static std::vector<Row> s_rows;
static void rows_add (XNode *p, int depth)
{
	for (int i = (int) p->kids.size () - 1; i >= 0; i--)
	{
		XNode *k = p->kids[i];
		if (!k->element ()) continue;
		const char *n = el_name (k);
		if (!strcmp (n, "defs") || !strcmp (n, "sodipodi:namedview") || !strcmp (n, "metadata") || !strcmp (n, "style") || !strcmp (n, "title") || !strcmp (n, "desc")) continue;
		s_rows.push_back ({ k, depth });
		if (is_layer (k)) rows_add (k, depth + 1);
	}
}
static const char *row_name (XNode *n)
{
	if (const char *l = n->get ("inkscape:label")) return l;
	if (const char *l = n->get ("id")) return l;
	return el_name (n);
}
static void layers_panel (long v, int w, int h)
{
	s_rows.clear ();
	rows_add (E.doc.root, 0);
	int y = TAB_H + 4;
	long pw, ph;
	uikit_canvas_pixels (v, &pw, &ph);
	for (auto &r : s_rows)
	{
		if (y + ROW > h - 36) break;
		bool sel = E.selected (r.n) || r.n == E.layer;
		if (sel) ui_fill (4, y, w - 8, ROW - 1, jet (J_ACCENT_CLAIR));
		ui_text_fit ((long) row_name (r.n), 8 + r.depth * 12, y + 19, is_layer (r.n) ? jet (J_TEXTE1) : jet (J_TEXTE2), 12, is_layer (r.n) ? (long) ui.font_b : font (), w - 80 - r.depth * 12);
		std::string disp = get_prop (r.n, "display");
		icon_in (v, I_EYE, w - 62, y + 2, disp == "none" ? jet (J_TEXTE3) : jet (J_TEXTE2));
		const char *lk = r.n->get ("sodipodi:insensitive");
		bool isLocked = lk && !strcmp (lk, "true");
		icon_in (v, isLocked ? I_LOCK : I_UNLOCK, w - 32, y + 2, isLocked ? jet (J_TEXTE1) : jet (J_TEXTE3));
		y += ROW;
	}
	if (s_rows.empty ()) ui_text ((long) TR ("Nothing here yet."), 8, TAB_H + 28, jet (J_TEXTE3), 12, font (), 0);
	ui_fill (0, h - 36, w, 1, jet (J_LIGNE));
	icon_in (v, I_PLUS, 8, h - 30, jet (J_TEXTE2));
	icon_in (v, I_MINUS, 44, h - 30, jet (J_TEXTE2));
	icon_in (v, I_UP, 80, h - 30, jet (J_TEXTE2));
	icon_in (v, I_DOWN, 116, h - 30, jet (J_TEXTE2));
}
static void layers_click (int x, int y, int h, int w)
{
	if (y >= h - 36)
	{
		if (x < 36) command (C_NEWLAYER);
		else if (x < 72) command (C_DELETE);
		else if (x < 108) command (C_RAISE);
		else if (x < 144) command (C_LOWER);
		return;
	}
	int i = (y - TAB_H - 4) / ROW;
	if (i < 0 || i >= (int) s_rows.size ()) return;
	XNode *n = s_rows[i].n;
	if (x >= w - 66 && x < w - 36)
	{
		std::string d = get_prop (n, "display");
		E.doc.begin (TR ("Visibility"));
		set_style (n, "display", d == "none" ? nullptr : "none");
		E.doc.commit ();
		E.sync ();
		return;
	}
	if (x >= w - 36)
	{
		const char *l = n->get ("sodipodi:insensitive");
		E.doc.begin (TR ("Lock"));
		E.doc.set (n, "sodipodi:insensitive", l && !strcmp (l, "true") ? nullptr : "true");
		if (!E.doc.root->get ("xmlns:sodipodi")) E.doc.set (E.doc.root, "xmlns:sodipodi", "http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd");
		E.doc.commit ();
		E.sync ();
		return;
	}
	if (is_layer (n)) { E.layer = n; E.select_none (); }
	else E.select (n);
}

// ---- Text panel ---------------------------------------------------------------------------------------------------------
static std::vector<std::string> s_fonts;
static void text_panel (long, int w, int h)
{
	if (s_fonts.empty ()) s_fonts = font_families ();
	int y = TAB_H + 8;
	ui_text ((long) TR ("Size"), 8, y + 18, jet (J_TEXTE2), 12, font (), 0);
	Style st, base;
	bool isText = E.sel.size () == 1;
	if (isText) compute_style (E.sel[0], base, E.builder->sheet, st);
	ui_rbox (w - 88, y, 36, 28, 5, st.fontWeight >= 600 ? jet (J_ACCENT) : jet (J_CREUX), st.fontWeight >= 600 ? jet (J_ACCENT) : jet (J_CREUX));
	ui_text ((long) "B", w - 75, y + 19, st.fontWeight >= 600 ? (unsigned) ui_ink (jet (J_ACCENT)) : jet (J_TEXTE1), 14, (long) ui.font_b, 0);
	ui_rbox (w - 46, y, 36, 28, 5, st.fontItalic ? jet (J_ACCENT) : jet (J_CREUX), st.fontItalic ? jet (J_ACCENT) : jet (J_CREUX));
	ui_text ((long) "I", w - 31, y + 19, st.fontItalic ? (unsigned) ui_ink (jet (J_ACCENT)) : jet (J_TEXTE1), 14, ui_font_get (UFN_ITALIC), 0);
	if (isText && ui_w_get_focus () != fFont) { char t[24]; snprintf (t, sizeof t, "%.4g", st.fontSize); ui_w_set_text (fFont, (long) t); }
	y += 36;
	ui_text ((long) TR ("Font"), 8, y + 12, jet (J_TEXTE2), 12, font (), 0);
	y += 20;
	std::string cur;
	if (!E.sel.empty ()) cur = get_prop (E.sel[0], "font-family");
	for (auto &f : s_fonts)
	{
		if (y + 26 > h) break;
		bool on = cur.find (f) != std::string::npos;
		if (on) ui_fill (4, y, w - 8, 25, jet (J_ACCENT_CLAIR));
		ui_text_fit ((long) f.c_str (), 8, y + 18, jet (J_TEXTE1), 13, font (), w - 16);
		y += 26;
	}
}
static void text_click (int x, int y, int w)
{
	if (y < TAB_H + 40)
	{
		if (E.sel.empty ()) return;
		Style st, base;
		compute_style (E.sel[0], base, E.builder->sheet, st);
		if (x >= w - 88 && x < w - 52) style_selection ("font-weight", st.fontWeight >= 600 ? "normal" : "bold", TR ("Bold"));
		else if (x >= w - 46) style_selection ("font-style", st.fontItalic ? "normal" : "italic", TR ("Italic"));
		return;
	}
	int i = (y - TAB_H - 64) / 26;
	if (i < 0 || i >= (int) s_fonts.size ()) return;
	std::string v = "'" + s_fonts[i] + "'";
	style_selection ("font-family", v.c_str (), TR ("Font"));
}

// ---- Transform and Align ------------------------------------------------------------------------------------------------
enum { TGRID_Y = TAB_H + 28, TCELL = 38 };
// a little picture of each alignment: a bar and two boxes
static void align_glyph (int i, int x, int y, unsigned c)
{
	bool vert = i < 3;
	int k = (i % 3) * 8;			// where the bar stands
	if (vert)
	{
		ui_fill (x + 4 + k, y + 3, 2, 18, c);
		ui_fill (x + 4 + (i % 3) * 4, y + 6, 10, 4, c);
		ui_fill (x + 4 + (i % 3) * 6, y + 14, 6, 4, c);
	}
	else
	{
		ui_fill (x + 3, y + 4 + k, 18, 2, c);
		ui_fill (x + 6, y + 4 + (i % 3) * 4, 4, 10, c);
		ui_fill (x + 14, y + 4 + (i % 3) * 6, 4, 6, c);
	}
}
static void align_panel (int)
{
	bool ok = E.sel.size () >= 2;
	ui_text ((long) (ok ? TR ("Relative to the selection.") : TR ("Select at least two objects.")), 8, TAB_H + 20, jet (J_TEXTE3), 12, font (), 0);
	for (int i = 0; i < 8; i++)
	{
		int x = 8 + (i % 6) * TCELL, y = TGRID_Y + (i / 6) * 36;
		ui_rbox (x, y, 36, 32, 5, jet (J_CREUX), jet (J_CREUX));
		if (i < 6) align_glyph (i, x + 4, y + 3, jet (J_TEXTE1));
		else
		{
			bool h = i == 6;
			for (int k = 0; k < 3; k++)
				if (h) ui_fill (x + 7 + k * 7, y + 8, 3, 14, jet (J_TEXTE1));
				else ui_fill (x + 9, y + 6 + k * 7, 14, 3, jet (J_TEXTE1));
		}
	}
}
static void transform_panel (int)
{
	int y = TAB_H + 4;
	if (E.sel.empty ()) ui_text ((long) TR ("Select an object to transform it."), 8, y + 14, jet (J_TEXTE3), 12, font (), 0);
	y += 26;
	ui_text ((long) TR ("Move X"), 8, y + 12, jet (J_TEXTE2), 12, font (), 0);
	ui_text ((long) TR ("Move Y"), 124, y + 12, jet (J_TEXTE2), 12, font (), 0);
	ui_text ((long) TR ("Scale, %"), 8, y + 74, jet (J_TEXTE2), 12, font (), 0);
	ui_text ((long) TR ("Rotation, degrees"), 124, y + 74, jet (J_TEXTE2), 12, font (), 0);
}
static void transform_click (int x, int y)
{
	int c = (x - 8) / TCELL, r = (y - TGRID_Y) / 36;
	if (x < 8 || y < TGRID_Y || r > 1 || c > 5) return;
	int i = r * 6 + c;
	if (i < 6) align_selection (i);
	else if (i < 8) distribute_selection (i == 6);
}
static void transform_apply (long w)
{
	if (E.sel.empty ()) return;
	double v = atof ((const char *) ui_w_get_text (w));
	Vec2 c = selection_box ().center ();
	Affine m;
	if (w == fMoveX) m = Affine::translate (v, 0);
	else if (w == fMoveY) m = Affine::translate (0, v);
	else if (w == fRot) m = Affine::rotate_at (v * M_PI / 180, c);
	else if (v > 0) m = Affine::scale_at (v / 100, v / 100, c);
	E.doc.begin (TR ("Transform"));
	transform_selection (m);
	E.doc.commit ();
	E.sync ();
}

// ---- XML editor -----------------------------------------------------------------------------------------------------------
enum { XROW = 22 };
static std::vector<Row> s_xrows;
static XNode *s_xnode;
static int s_xtop;
static void xml_rows (XNode *n, int depth)
{
	if (!n->element ()) return;
	s_xrows.push_back ({ n, depth });
	for (XNode *k : n->kids) xml_rows (k, depth + 1);
}
static void xml_fill ()
{
	if (!s_xnode) { ui_w_set_text (fAttrs, (long) ""); return; }
	std::string t;
	for (auto &a : s_xnode->attrs) { t += a.name; t += " = "; t += a.value; t += '\n'; }
	ui_w_set_text (fAttrs, (long) t.c_str ());
}
static void xml_panel (int w, int h)
{
	s_xrows.clear ();
	xml_rows (E.doc.root, 0);
	if (E.sel.size () == 1 && s_xnode != E.sel[0]) { s_xnode = E.sel[0]; xml_fill (); }
	int listH = (h - TAB_H - 44) / 2, rows = listH / XROW, y = TAB_H + 2;
	s_xtop = std::max (0, std::min (s_xtop, (int) s_xrows.size () - rows));
	for (int i = s_xtop; i < (int) s_xrows.size () && i < s_xtop + rows; i++, y += XROW)
	{
		Row &r = s_xrows[i];
		if (r.n == s_xnode) ui_fill (2, y, w - 4, XROW, jet (J_ACCENT_CLAIR));
		char b[80];
		const char *id = r.n->get ("id");
		snprintf (b, sizeof b, "%s%s%s", r.n->name.c_str (), id ? " #" : "", id ? id : "");
		ui_text_fit ((long) b, 6 + r.depth * 10, y + 16, jet (J_TEXTE1), 12, font (), w - 12 - r.depth * 10);
	}
	ui_rbox (w - 84, h - 34, 76, 28, 5, jet (J_CREUX), jet (J_CREUX));
	ui_text ((long) TR ("Apply"), w - 72, h - 15, jet (J_TEXTE1), 12, font (), 0);
	ui_text ((long) "▲", 10, h - 15, jet (J_TEXTE2), 12, font (), 0);
	ui_text ((long) "▼", 36, h - 15, jet (J_TEXTE2), 12, font (), 0);
}
static void xml_apply ()
{
	if (!s_xnode) return;
	std::string t = (const char *) ui_w_get_text (fAttrs);
	std::vector<std::pair<std::string, std::string>> now;
	size_t a = 0;
	while (a < t.size ())
	{
		size_t e = t.find ('\n', a);
		if (e == std::string::npos) e = t.size ();
		std::string l = t.substr (a, e - a);
		a = e + 1;
		size_t q = l.find ('=');
		if (q == std::string::npos) continue;
		std::string k = l.substr (0, q), v = l.substr (q + 1);
		while (!k.empty () && k.back () == ' ') k.pop_back ();
		while (!k.empty () && k[0] == ' ') k.erase (0, 1);
		if (!v.empty () && v[0] == ' ') v.erase (0, 1);
		if (!k.empty ()) now.push_back ({ k, v });
	}
	E.doc.begin (TR ("Edit attributes"));
	std::vector<std::string> old;
	for (auto &x : s_xnode->attrs) old.push_back (x.name);
	for (auto &o : old)
	{
		bool kept = false;
		for (auto &n : now) if (n.first == o) kept = true;
		if (!kept) E.doc.set (s_xnode, o.c_str (), nullptr);
	}
	for (auto &n : now) E.doc.set (s_xnode, n.first.c_str (), n.second);
	E.doc.commit ();
	E.sync ();
}
static void xml_click (int y, int h)
{
	if (y >= h - 36)
	{
		long pw, ph;
		uikit_canvas_pixels (vPanel, &pw, &ph);
		long x = ui_ev_field (&ui, EV_X) - (W - pw);
		if (x >= pw - 84) xml_apply ();
		else if (x < 30) s_xtop = std::max (0, s_xtop - 6);
		else if (x < 56) s_xtop += 6;
		ui_view_dirty (vPanel);
		return;
	}
	int i = s_xtop + (y - TAB_H - 2) / XROW;
	if (i < 0 || i >= (int) s_xrows.size ()) return;
	s_xnode = s_xrows[i].n;
	xml_fill ();
	if (s_xnode != E.doc.root && !object_box (s_xnode).empty ()) E.select (s_xnode);
	ui_view_dirty (vPanel);
}

// ---- Document -----------------------------------------------------------------------------------------------------------
static void check_row (int y, const char *label, bool on)
{
	ui_frame (8, y + 3, 16, 16, jet (J_LIGNE_FORTE));
	if (on) ui_fill (11, y + 6, 10, 10, jet (J_ACCENT));
	ui_text ((long) label, 32, y + 16, jet (J_TEXTE1), 13, font (), 0);
}
static void doc_panel (int)
{
	ui_text ((long) TR ("Page width, px"), 8, TAB_H + 20, jet (J_TEXTE2), 12, font (), 0);
	ui_text ((long) TR ("Height, px"), 124, TAB_H + 20, jet (J_TEXTE2), 12, font (), 0);
	ui_text ((long) TR ("Grid step, px"), 8, TAB_H + 84, jet (J_TEXTE2), 12, font (), 0);
	check_row (TAB_H + 130, TR ("Grid"), E.grid);
	check_row (TAB_H + 160, TR ("Guides"), E.guides);
	check_row (TAB_H + 190, TR ("Snapping"), E.snap);
	check_row (TAB_H + 220, TR ("Rulers"), E.rulers);
	char t[32];
	if (ui_w_get_focus () != fDocW) { snprintf (t, sizeof t, "%.4g", E.scene->width); ui_w_set_text (fDocW, (long) t); }
	if (ui_w_get_focus () != fDocH) { snprintf (t, sizeof t, "%.4g", E.scene->height); ui_w_set_text (fDocH, (long) t); }
	if (ui_w_get_focus () != fGrid) { snprintf (t, sizeof t, "%.4g", E.gridStep); ui_w_set_text (fGrid, (long) t); }
}
static void doc_click (int y)
{
	int i = (y - TAB_H - 130) / 30;
	if (y < TAB_H + 130) return;
	command (i == 0 ? C_GRID : i == 1 ? C_GUIDES : i == 2 ? C_SNAP : C_RULERS);
	ui_view_dirty (vPanel);
}
static void doc_size ()
{
	double w = atof ((const char *) ui_w_get_text (fDocW)), h = atof ((const char *) ui_w_get_text (fDocH));
	if (w <= 0 || h <= 0) return;
	double oldW = E.scene->width, oldH = E.scene->height;
	E.doc.begin (TR ("Page size"));
	const char *vb = E.doc.root->get ("viewBox");
	double v[4];
	if (vb && parse_viewbox (vb, v))
	{
		char b[100];
		snprintf (b, sizeof b, "%g %g %g %g", v[0], v[1], v[2] * w / oldW, v[3] * h / oldH);
		E.doc.set (E.doc.root, "viewBox", b);
	}
	E.doc.set (E.doc.root, "width", fmt (w));
	E.doc.set (E.doc.root, "height", fmt (h));
	E.doc.commit ();
	E.sync ();
	E.builder->invalidate ();
}

// ---- Filters ------------------------------------------------------------------------------------------------------------
enum { FROW = 28 };
static void filters_panel (int w)
{
	ui_text ((long) TR ("Applied at once to the selection."), 8, TAB_H + 18, jet (J_TEXTE3), 12, font (), 0);
	int y = TAB_H + 26;
	for (int i = 0; i < filter_presets (); i++, y += FROW)
	{
		ui_rbox (8, y, w - 16, FROW - 3, 5, jet (J_CREUX), jet (J_CREUX));
		ui_text ((long) TR (filter_preset_label (i)), 16, y + 17, jet (J_TEXTE1), 12, font (), 0);
	}
	ui_text ((long) TR ("Remove the filter"), 16, y + 18, jet (J_DANGER), 12, font (), 0);
}
static void filters_click (int y)
{
	int i = (y - TAB_H - 26) / FROW;
	if (y < TAB_H + 26) return;
	if (i < filter_presets ()) filter_apply (i);
	else filter_remove ();
}

// ---- Path effects -------------------------------------------------------------------------------------------------------
static XNode *lpe_target ()
{
	if (E.sel.size () != 1) return nullptr;
	const char *n = el_name (E.sel[0]);
	return strcmp (n, "g") && strcmp (n, "text") && strcmp (n, "image") && strcmp (n, "use") ? E.sel[0] : nullptr;
}
static void lpe_panel (int w)
{
	XNode *t = lpe_target ();
	if (!t) { ui_text ((long) TR ("Select one shape or path."), 8, TAB_H + 24, jet (J_TEXTE3), 12, font (), 0); return; }
	int y = TAB_H + 6;
	ui_rbox (8, y, w - 16, 26, 5, jet (J_ACCENT), jet (J_ACCENT));
	ui_text ((long) TR ("Add an effect..."), 16, y + 18, (unsigned) ui_ink (jet (J_ACCENT)), 12, font (), 0);
	y += 34;
	auto st = lpe_stack (t);
	for (XNode *e : st)
	{
		const LpeType *ty = lpe_type (e->get ("effect"));
		ui_text ((long) (ty ? TR (ty->label) : e->get2 ("effect", "?")), 8, y + 16, jet (J_TEXTE1), 13, (long) ui.font_b, 0);
		ui_text ((long) "\xE2\x9C\x95", w - 26, y + 16, jet (J_DANGER), 13, font (), 0);
		y += 24;
		if (!ty) continue;
		for (int i = 0; i < ty->nparams; i++, y += 26)
		{
			const LpeParam &p = ty->params[i];
			char b[32];
			snprintf (b, sizeof b, "%.4g", lpe_param (e, p));
			ui_text ((long) TR (p.label), 12, y + 17, jet (J_TEXTE2), 12, font (), 0);
			ui_rbox (w - 104, y + 2, 26, 22, 4, jet (J_CREUX), jet (J_CREUX));
			ui_text ((long) "\xE2\x88\x92", w - 95, y + 18, jet (J_TEXTE1), 13, font (), 0);
			ui_text ((long) b, w - 72, y + 17, jet (J_TEXTE1), 12, font (), 0);
			ui_rbox (w - 34, y + 2, 26, 22, 4, jet (J_CREUX), jet (J_CREUX));
			ui_text ((long) "+", w - 25, y + 18, jet (J_TEXTE1), 13, font (), 0);
		}
		y += 8;
	}
}
static void lpe_click (int x, int y, int w)
{
	XNode *t = lpe_target ();
	if (!t) return;
	int ry = TAB_H + 6;
	if (y >= ry && y < ry + 26)
	{
		std::string l;
		for (int i = 0; i < LPE_COUNT; i++) { if (i) l += '\n'; l += TR (LPE_TYPES[i].label); }
		long c = popup_at (W - w + 8, BAR_H + ry + 26, l, 200);
		if (c >= 0) { lpe_add (t, &LPE_TYPES[c]); E.sel[0] = t; ui_view_dirty (vPanel); }
		return;
	}
	ry += 34;
	auto st = lpe_stack (t);
	for (int k = 0; k < (int) st.size (); k++)
	{
		const LpeType *ty = lpe_type (st[k]->get ("effect"));
		if (y >= ry && y < ry + 24) { if (x >= w - 34) lpe_remove (t, k); return; }
		ry += 24;
		if (!ty) continue;
		for (int i = 0; i < ty->nparams; i++, ry += 26)
			if (y >= ry && y < ry + 26)
			{
				const LpeParam &p = ty->params[i];
				double v = lpe_param (st[k], p), d = 0;
				if (x >= w - 104 && x < w - 78) d = -p.step;
				else if (x >= w - 34) d = p.step;
				if (d != 0) lpe_set (t, st[k], p, v + d);
				return;
			}
		ry += 8;
	}
}

// ---- rulers -------------------------------------------------------------------------------------------------------------
static void ruler_paint (long v, long w, long h)
{
	bool horiz = v == vRulH;
	ui_fill (0, 0, w, h, jet (J_FACE));
	if (horiz) ui_fill (0, h - 1, w, 1, jet (J_LIGNE)); else ui_fill (w - 1, 0, 1, h, jet (J_LIGNE));
	double step = 1, z = E.view.zoom;
	static const double S[] = { 1, 2, 5 };
	double mag = 1;
	while (step * mag * z < 40) { int i = 0; for (; i < 3; i++) if (S[i] * mag * z >= 40) break; if (i < 3) { step = S[i]; break; } mag *= 10; }
	double unit = step * mag;
	long len = horiz ? w : h;
	double o = horiz ? E.view.ox + s_rulers : E.view.oy;	// (the horizontal one starts over the vertical one)
	int first = (int) std::floor (-o / (unit * z));
	for (int i = first; ; i++)
	{
		double p = o + i * unit * z;
		if (p > len) break;
		int q = (int) p;
		if (horiz) ui_fill (q, 10, 1, 8, jet (J_TEXTE3)); else ui_fill (10, q, 8, 1, jet (J_TEXTE3));
		for (int k = 1; k < 5; k++)
		{
			int m = (int) (p + k * unit * z / 5);
			if (m < 0 || m >= len) continue;
			if (horiz) ui_fill (m, 14, 1, 4, jet (J_TEXTE3)); else ui_fill (14, m, 4, 1, jet (J_TEXTE3));
		}
		char b[16];
		double lab = i * unit;
		if (!horiz && std::fabs (lab) >= 1000) snprintf (b, sizeof b, "%gk", lab / 1000);
		else snprintf (b, sizeof b, "%g", lab);
		if (horiz) ui_text ((long) b, q + 3, 10, jet (J_TEXTE3), 11, font (), 0);
		else if (q + 12 < len) ui_text ((long) b, 1, q + 12, jet (J_TEXTE3), 11, font (), 0);
	}
}
void ruler_pointer (Vec2 s)
{
	if (!s_rulers) return;
	static int lx = -1, ly = -1;
	int x = (int) s.x + s_rulers, y = (int) s.y;
	(void) x; (void) y; (void) lx; (void) ly;
}
// pulled out of a ruler: a guide where it is let go
static long ruler_mouse (long v, long kind, long x, long y, long)
{
	if (kind != 2) return 0;
	bool horiz = v == vRulH;
	if (horiz) { if (y < 18) return 0; add_guide (false, E.to_page ({ 0, (double) y - 18 }).y); }
	else { if (x < 18) return 0; add_guide (true, E.to_page ({ (double) x - 18, 0 }).x); }
	return 0;
}

// ---- the panel ----------------------------------------------------------------------------------------------------------
static int tab_cell () { return s_tab == 8 ? 4 : s_tab >= 4 ? 5 : s_tab; }
static void panel_paint (long v, long w, long h)
{
	ui_fill (0, 0, w, h, jet (J_FACE));
	ui_fill (0, 0, 1, h, jet (J_LIGNE));
	static const int T[] = { I_FILLS, I_LAYERS, I_TEXT, I_TRANSFORM, I_ALIGN, I_MORE };
	for (int i = 0; i < 6; i++)
	{
		bool on = i == tab_cell ();
		if (on) ui_rbox (4 + i * 40, 2, 36, 28, 5, jet (J_ACCENT), jet (J_ACCENT));
		icon_in (v, T[i], 10 + i * 40, 4, on ? (unsigned) ui_ink (jet (J_ACCENT)) : jet (J_TEXTE2));
	}
	static const char *const NAMES[] = { "Fill and Stroke", "Layers and Objects", "Text and Font", "Transform", "XML Editor", "Document", "Filters", "Path Effects", "Align and Distribute" };
	ui_text ((long) TR (NAMES[s_tab]), 8, TABS + 19, jet (J_TEXTE1), 13, (long) ui.font_b, 0);
	ui_fill (0, TABS - 1, w, 1, jet (J_LIGNE));
	switch (s_tab)
	{
	case 0: fill_panel (v, (int) w); break;
	case 1: layers_panel (v, (int) w, (int) h); break;
	case 2: text_panel (v, (int) w, (int) h); break;
	case 3: transform_panel ((int) w); break;
	case 8: align_panel ((int) w); break;
	case 4: xml_panel ((int) w, (int) h); break;
	case 6: filters_panel ((int) w); break;
	case 7: lpe_panel ((int) w); break;
	default: doc_panel ((int) w); break;
	}
}
static long panel_mouse (long, long kind, long x, long y, long)
{
	long pw, ph;
	uikit_canvas_pixels (vPanel, &pw, &ph);
	int w = (int) pw, h = (int) ph;
	if (kind == 2) { if (s_drag) s_drag = 0; return 0; }
	if (kind == 1 && y < TABS)
	{
		int t = (int) ((x - 4) / 40);
		if (t == 4) { s_tab = 8; ui_view_dirty (vPanel); return 0; }
		if (t == 5)
		{
			long c = popup_at (W - pw + 84, BAR_H + TABS, std::string (TR ("Filters")) + "\n" + TR ("Path Effects") + "\n" + TR ("XML Editor") + "\n" + TR ("Document"), 160);
			if (c >= 0) { s_tab = c == 0 ? 6 : c == 1 ? 7 : c == 2 ? 4 : 5; ui_view_dirty (vPanel); }
			return 0;
		}
		if (t >= 0 && t < 4)
		{
			if (t == s_tab) { panel_toggle (); return 0; }
			s_tab = t; ui_view_dirty (vPanel);
		}
		return 0;
	}
	if (s_tab == 1) { if (kind == 1) layers_click ((int) x, (int) y, h, w); return 0; }
	if (s_tab == 2) { if (kind == 1) text_click ((int) x, (int) y, w); return 0; }
	if (s_tab == 8) { if (kind == 1) transform_click ((int) x, (int) y); return 0; }
	if (s_tab == 3) return 0;
	if (s_tab == 4) { if (kind == 1) xml_click ((int) y, h); return 0; }
	if (s_tab == 5) { if (kind == 1) doc_click ((int) y); return 0; }
	if (s_tab == 6) { if (kind == 1) filters_click ((int) y); return 0; }
	if (s_tab == 7) { if (kind == 1) lpe_click ((int) x, (int) y, w); return 0; }
	if (s_tab != 0) return 0;
	int pwd = w - 16;
	if (kind == 1)
	{
		if (y < TAB_H + 32) { s_page = std::min (2, (int) ((x - 8) * 3 / pwd)); load_color (); ui_view_dirty (vPanel); return 0; }
		if (s_page == 2)
		{
			if (y >= 166 && y < 180) s_drag = 5;
			else if (y >= 188 && y < 252 && !E.sel.empty ())
			{
				static const char *const C[] = { "butt", "round", "square" }, *const J[] = { "miter", "round", "bevel" };
				int i = std::min (2, (int) ((x - 8) * 3 / pwd)), r = (int) ((y - 188) / 32);
				style_selection (r ? "stroke-linejoin" : "stroke-linecap", r ? J[i] : C[i], TR ("Stroke style"));
				ui_view_dirty (vPanel);
				return 0;
			}
			else if (y >= 254 && y < 282 && !E.sel.empty ())
			{
				static const char *const M = "normal\nmultiply\nscreen\noverlay\ndarken\nlighten\ncolor-dodge\ncolor-burn\nhard-light\nsoft-light\ndifference\nexclusion\nhue\nsaturation\ncolor\nluminosity";
				long c = popup_at (W - pw - 8 + 64, BAR_H + 282 - 150, M, 170);
				if (c >= 0)
				{
					std::string s = M;
					size_t a = 0;
					for (long i = 0; i < c; i++) a = s.find ('\n', a) + 1;
					size_t e = s.find ('\n', a);
					style_selection ("mix-blend-mode", s.substr (a, e == std::string::npos ? e : e - a).c_str (), TR ("Blend"));
				}
				return 0;
			}
		}
		else if (y >= ZY && y < ZY + ZH) s_drag = 1;
		else if (y >= HY && y < HY + BARH) s_drag = 2;
		else if (y >= FIELDY + 36 && y < FIELDY + 60)
		{
			if (E.sel.empty ()) (s_page == 0 ? E.fill : E.stroke) = "none";
			else style_selection (s_page == 0 ? "fill" : "stroke", "none", TR ("Colour"));
			return 0;
		}
	}
	if ((kind == 0 || kind == 1) && s_drag)
	{
		double fx = std::min (1.0, std::max (0.0, (x - 8) / (double) (pwd - 1)));
		if (s_drag == 1) { s_s = fx; s_v = 1 - std::min (1.0, std::max (0.0, (y - ZY) / (double) (ZH - 1))); apply_color (true); }
		else if (s_drag == 2) { s_h = std::min (0.999, fx); apply_color (true); }
		else if (s_drag == 5) { char b[16]; snprintf (b, sizeof b, "%.3g", fx); style_selection ("opacity", b, TR ("Opacity"), 12); }
		ui_view_dirty (vPanel);
	}
	return 0;
}

bool panel_visible () { return s_panel; }
static void show_selection ()
{
	Box b = selection_box ();
	if (b.empty ()) return;
	Vec2 a = E.to_screen ({ b.x0, b.y0 }), z = E.to_screen ({ b.x1, b.y1 });
	double dx = 0, dy = 0;
	if (z.x - a.x + 32 <= E.cw) { if (a.x < 16) dx = 16 - a.x; else if (z.x > E.cw - 16) dx = E.cw - 16 - z.x; }
	if (z.y - a.y + 32 <= E.ch) { if (a.y < 16) dy = 16 - a.y; else if (z.y > E.ch - 16) dy = E.ch - 16 - z.y; }
	E.view.ox += std::round (dx); E.view.oy += std::round (dy);
	E.dirty = true;
}
void panel_toggle () { s_panel = !s_panel; chrome_layout (); show_selection (); E.dirty = true; }
void panel_show (int which)
{
	s_tab = which;
	if (!s_panel) { s_panel = true; chrome_layout (); show_selection (); }
	load_color ();
	ui_view_dirty (vPanel);
}

// ---- layout -------------------------------------------------------------------------------------------------------------
void chrome_layout ()
{
	W = (int) ui.w; H = (int) ui.h;
	int panelW = 0;
	bool wide = W >= 1000;
	if (wide) { s_panel = true; panelW = W >= 1700 ? 340 : W >= 1200 ? 300 : 264; }
	else if (s_panel) panelW = W < 640 ? W - RAIL_W : PANEL_S;
	s_rulers = (E.rulers || wide) && H >= 520 ? 18 : 0;
	if (wide && !s_rulersSet) { E.rulers = true; s_rulersSet = true; }
	s_rulers = E.rulers ? 18 : 0;
	int cw = W - RAIL_W - panelW - s_rulers, ch = H - BAR_H - BOTTOM_H - s_rulers;
	ui_w_move (vBar, 0, 0, W, BAR_H);
	ui_w_move (vRail, 0, BAR_H, RAIL_W, ch + s_rulers);
	ui_w_show (vRulH, s_rulers > 0); ui_w_show (vRulV, s_rulers > 0);
	if (s_rulers) { ui_w_move (vRulH, RAIL_W, BAR_H, cw + s_rulers, 18); ui_w_move (vRulV, RAIL_W, BAR_H + 18, 18, ch); }
	ui_w_move (vBottom, 0, H - BOTTOM_H, W, BOTTOM_H);
	ui_w_move (vCanvas, RAIL_W + s_rulers, BAR_H + s_rulers, cw, ch);
	ui_w_show (vPanel, panelW > 0);
	if (panelW) ui_w_move (vPanel, W - panelW, BAR_H, panelW, ch + s_rulers);
	for (int i = 0; i < 4; i++) ui_w_move (i == 0 ? fX : i == 1 ? fY : i == 2 ? fW : fH, 22 + i * 96, 4, 72, 28);
	{
		// (a view's children stand in window coordinates)
		int px = W - panelW, py = BAR_H, fy = py + TAB_H + 48;
		ui_w_move (fHex, px + 22, py + FIELDY, 96, 28);
		ui_w_move (fStroke, px + 8, py + 126, 96, 28);
		ui_w_move (fMoveX, px + 8, fy, 112, 28); ui_w_move (fMoveY, px + 124, fy, 112, 28);
		ui_w_move (fScale, px + 8, fy + 62, 112, 28); ui_w_move (fRot, px + 124, fy + 62, 112, 28);
	}
	{
		int px = W - panelW, py = BAR_H;
		ui_w_move (fFont, px + 48, py + TAB_H + 8, 80, 28);
		ui_w_move (fAttrs, px + 4, py + TAB_H + 2 + ((H - BAR_H - BOTTOM_H - TAB_H - 44) / 2 / XROW) * XROW + 4, panelW - 8, (H - BAR_H - BOTTOM_H - TAB_H - 44) / 2);
		ui_w_move (fDocW, px + 8, py + TAB_H + 26, 96, 28); ui_w_move (fDocH, px + 124, py + TAB_H + 26, 96, 28);
		ui_w_move (fGrid, px + 8, py + TAB_H + 90, 96, 28);
	}
	canvas_place (RAIL_W + s_rulers, BAR_H + s_rulers, cw, ch);
	chrome_paint_all ();
}
void chrome_paint_all ()
{
	for (long v : { vBar, vRail, vBottom, vPanel, vRulH, vRulV }) ui_view_dirty (v);
}
// (children first: changing them while the parent paints comes too late)
static void panel_fields ()
{
	ui_w_show (fHex, s_panel && s_tab == 0 && s_page != 2);
	ui_w_show (fStroke, s_panel && s_tab == 0 && s_page == 2);
	for (long f : { fMoveX, fMoveY, fRot, fScale }) ui_w_show (f, s_panel && s_tab == 3);
	ui_w_show (fFont, s_panel && s_tab == 2);
	ui_w_show (fAttrs, s_panel && s_tab == 4);
	for (long f : { fDocW, fDocH, fGrid }) ui_w_show (f, s_panel && s_tab == 5);
}
void chrome_refresh ()
{
	if (E.chromeDirty)
	{
		E.chromeDirty = false;
		sync_fields ();
		ui_view_dirty (vBar); ui_view_dirty (vRail); ui_view_dirty (vBottom);
		if (s_rulers) { ui_view_dirty (vRulH); ui_view_dirty (vRulV); }
	}
	if (E.panelDirty)
	{
		E.panelDirty = false;
		if (!s_drag) load_color ();
		ui_view_dirty (vPanel);
	}
	panel_fields ();
}

// the canvas's view, under the surface: it takes the pointer
long chrome_canvas_view () { return vCanvas; }
bool chrome_field (long w, long code)
{
	if (code != UN_ACTIVATE && code != UN_CHANGE) return false;
	if (w == fX || w == fY || w == fW || w == fH) { if (code == UN_ACTIVATE) { apply_fields (); ui_w_focus (vCanvas); } return true; }
	if (w == fFont)
	{
		if (code == UN_ACTIVATE)
		{
			double v = atof ((const char *) ui_w_get_text (fFont));
			if (v > 0) { char t[32]; snprintf (t, sizeof t, "%gpx", v); style_selection ("font-size", t, TR ("Font size")); }
			ui_w_focus (vCanvas);
		}
		return true;
	}
	if (w == fDocW || w == fDocH) { if (code == UN_ACTIVATE) { doc_size (); ui_w_focus (vCanvas); } return true; }
	if (w == fGrid)
	{
		if (code == UN_ACTIVATE) { double v = atof ((const char *) ui_w_get_text (fGrid)); if (v > 0) E.gridStep = v; E.dirty = true; ui_w_focus (vCanvas); }
		return true;
	}
	if (w == fMoveX || w == fMoveY || w == fRot || w == fScale)
	{
		if (code == UN_ACTIVATE) { transform_apply (w); ui_w_set_text (w, (long) ""); }
		return true;
	}
	if (w == fHex)
	{
		if (code == UN_ACTIVATE)
		{
			std::string t = (const char *) ui_w_get_text (fHex);
			if (t.empty () || t[0] != '#') t = "#" + t;
			uint32_t c;
			if (parse_color (t.c_str (), c))
			{
				double h = s_h; rgb_hsv (c, h, s_s, s_v); s_h = h;
				if (E.sel.empty ()) (s_page == 0 ? E.fill : E.stroke) = t;
				else style_selection (s_page == 0 ? "fill" : "stroke", t.c_str (), TR ("Colour"));
				ui_view_dirty (vPanel);
			}
			ui_w_focus (vCanvas);
		}
		return true;
	}
	if (w == fStroke)
	{
		if (code == UN_ACTIVATE)
		{
			double v = atof ((const char *) ui_w_get_text (fStroke));
			E.strokeWidth = v;
			style_selection ("stroke-width", fmt (v).c_str (), TR ("Stroke width"));
			ui_w_focus (vCanvas);
		}
		return true;
	}
	return false;
}

void canvas_handlers (long *paint, long *mouse, long *wheel);

void chrome_build ()
{
	make_icons ();
	vBar = ui_view (0, 0, 10, 10, 0); ui_view_on (vBar, (long) bar_paint, (long) bar_mouse, 0, 0);
	vRail = ui_view (0, 0, 10, 10, 0); ui_view_on (vRail, (long) rail_paint, (long) rail_mouse, 0, 0);
	vBottom = ui_view (0, 0, 10, 10, 0); ui_view_on (vBottom, (long) bottom_paint, (long) bottom_mouse, 0, 0);
	long pa, mo, wh;
	canvas_handlers (&pa, &mo, &wh);
	vCanvas = ui_view (0, 0, 10, 10, UVW_FOCUS); ui_view_on (vCanvas, pa, mo, 0, wh);
	vPanel = ui_view (0, 0, 10, 10, 0); ui_view_on (vPanel, (long) panel_paint, (long) panel_mouse, 0, 0);
	fX = ui_textbox (0, 0, 72, 28, UTF_RIGHT); ui_w_set_parent (fX, vBar);
	fY = ui_textbox (0, 0, 72, 28, UTF_RIGHT); ui_w_set_parent (fY, vBar);
	fW = ui_textbox (0, 0, 72, 28, UTF_RIGHT); ui_w_set_parent (fW, vBar);
	fH = ui_textbox (0, 0, 72, 28, UTF_RIGHT); ui_w_set_parent (fH, vBar);
	fHex = ui_textbox (0, 0, 96, 28, 0); ui_w_set_parent (fHex, vPanel);
	fStroke = ui_textbox (0, 0, 96, 28, 0); ui_w_set_parent (fStroke, vPanel);
	ui_w_show (fHex, 0); ui_w_show (fStroke, 0);
	for (long *f : { &fMoveX, &fMoveY, &fRot, &fScale, &fFont, &fDocW, &fDocH, &fGrid }) { *f = ui_textbox (0, 0, 96, 28, UTF_RIGHT); ui_w_set_parent (*f, vPanel); ui_w_show (*f, 0); }
	fAttrs = ui_textarea (0, 0, 200, 100, UTF_MONO); ui_w_set_parent (fAttrs, vPanel); ui_w_show (fAttrs, 0);
	vRulH = ui_view (0, 0, 10, 18, 0); ui_view_on (vRulH, (long) ruler_paint, (long) ruler_mouse, 0, 0);
	vRulV = ui_view (0, 0, 18, 10, 0); ui_view_on (vRulV, (long) ruler_paint, (long) ruler_mouse, 0, 0);
}

}
