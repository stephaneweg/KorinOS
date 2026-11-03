//
// sidebar.h -- the panel at the right, four tabs: Slide (its layout, the theme, the fonts, the background, the master's
// objects, the slide's size, hidden), Shape (the fill, its transparency, the line, the corners, the shadow, the arrow
// heads, the place and size in cm, the rotation, the order, the alignment; a picture's, a table's, a chart's own), Text
// (the font, the size, bold..., the colour, the alignment, the list, the level, the spacing, the anchor, the autofit)
// and Animate (the transition to the slide and how it goes on; the slide's effects in their order, the one chosen's
// settings). The rows are built from the state at each change, as in Onyx; here each row is UIKit's controls on a
// ui_view (a button that opens a list, a colour picker, a field and its two arrows, a check box, a row of buttons),
// made again only when the rows change; section titles, the themes and the effects are drawn in the view.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_sidebar_h
#define _slides_sidebar_h

#include "panes.h"

namespace sl {

enum { SB_SLIDE, SB_SHAPE, SB_TEXT, SB_ANIM };
static int g_sbTab = SB_SLIDE;
static int g_animSel = -1;			// the effect chosen in the list

// The buttons' glyphs (DejaVu; Onyx's vector icons are drawn on its own widgets)
enum { SL_FORWARD = 200, SL_BACKWARD, SL_FRONT, SL_BACK, SL_AL_LEFT, SL_AL_CENTER, SL_AL_RIGHT, SL_AL_TOP, SL_AL_MIDDLE, SL_AL_BOTTOM,
       SL_ANCHOR_T, SL_ANCHOR_M, SL_ANCHOR_B, SL_DIST_H, SL_DIST_V, SL_REMOVE, SL_UP, SL_DOWN };
static const char *icon_glyph (int k)
{
	switch (k)
	{
	case SL_FRONT: return "\xE2\x87\x88"; case SL_FORWARD: return "\xE2\x86\x91"; case SL_BACKWARD: return "\xE2\x86\x93"; case SL_BACK: return "\xE2\x87\x8A";
	case SL_AL_LEFT: return "\xE2\x87\xA4"; case SL_AL_CENTER: return "\xE2\x86\x94"; case SL_AL_RIGHT: return "\xE2\x87\xA5";
	case SL_AL_TOP: case SL_ANCHOR_T: return "\xE2\x86\xA5"; case SL_AL_MIDDLE: case SL_ANCHOR_M: return "\xE2\x86\x95"; case SL_AL_BOTTOM: case SL_ANCHOR_B: return "\xE2\x86\xA7";
	case SL_DIST_H: return "\xE2\x8B\xAF"; case SL_DIST_V: return "\xE2\x8B\xAE"; case SL_REMOVE: return "\xE2\x9C\x95"; case SL_UP: return "\xE2\x86\x91"; case SL_DOWN: return "\xE2\x86\x93";
	case 300: return "B"; case 301: return "I"; case 302: return "U"; case 303: return "S"; case 304: return "x\xC2\xB2"; case 305: return "x\xE2\x82\x82";
	case 306: return "\xE2\x87\xA4"; case 307: return "\xE2\x86\x94"; case 308: return "\xE2\x87\xA5"; case 309: return "\xE2\x89\xA1";
	}
	return "?";
}

// ---- the rows ----------------------------------------------------------------------------------------------------------
enum { R_SECTION, R_PICK, R_COLOR, R_SPIN, R_CHECK, R_BUTTONS, R_THEMES, R_EFFECTS, R_BUTTON, R_INFO };
struct SRow
{
	int type, id;
	char label[64], value[64];
	unsigned color; bool autoColor;
	double num, step, lo, hi; int dec; const char *unit;
	bool on;
	int nbtn; int icon[8]; bool btnOn[8];
	bool half;				// shares the line with the row before (its right half)
	int y, h, x, w;				// placed by layout ()
};
// The ids of the rows (sidebar_act's)
enum {
	I_LAYOUT = 1, I_THEME, I_FONT_MAJOR, I_FONT_MINOR, I_BG_TYPE, I_BG_C1, I_BG_C2, I_BG_ANGLE, I_MASTER_OBJ, I_SIZE, I_HIDDEN, I_RESET, I_FOOTER, I_NUMBER,
	I_FILL_TYPE, I_FILL_C1, I_FILL_C2, I_FILL_ANGLE, I_FILL_ALPHA, I_LINE_TYPE, I_LINE_C, I_LINE_W, I_HEAD0, I_HEAD1, I_RADIUS, I_SHADOW, I_SHAPE,
	I_X, I_Y, I_W, I_H, I_ROT, I_ORDER, I_ALIGN, I_TBL_HEAD, I_TBL_BAND, I_TBL_ROWS, I_CHART_TYPE, I_CHART_DATA, I_CHART_LEGEND, I_CHART_LABELS, I_CHART_TITLE,
	I_FONT, I_SIZE_T, I_STYLE, I_COLOR, I_ALIGN_T, I_BULLET, I_LEVEL, I_SPACING, I_BEFORE, I_AFTER, I_ANCHOR, I_FIT, I_WRAP, I_FLIP,
	I_TR_TYPE, I_TR_DIR, I_TR_DUR, I_TR_ADV, I_TR_AFTER, I_TR_ALL, I_FX_LIST, I_FX_ADD, I_FX_TOOLS, I_FX_EFFECT, I_FX_START, I_FX_DELAY, I_FX_DUR, I_FX_DIR, I_FX_PARA, I_FX_PLAY,
	I_CROP, I_MASTER_VIEW, I_PH_TITLE, I_PH_TEXT, I_PH_PIC, I_LAYOUT_NAME
};
static const char *const FILL_NAMES[] = { "None", "Solid", "Gradient" };
static const char *const LINE_NAMES[] = { "None", "Solid", "Dashes", "Dots" };
static const char *const HEAD_NAMES[] = { "None", "Arrow", "Open arrow", "Dot" };
static const char *const BULLET_NAMES[] = { "None", "Bullets", "Numbers" };
static const char *const ANCHOR_NAMES[] = { "Top", "Middle", "Bottom" };
static const char *const FIT_NAMES[] = { "None", "Shrink the text", "Grow the box" };
static const char *const DIR_NAMES[] = { "-", "From the left", "From the right", "From the top", "From the bottom" };
static const char *const START_NAMES[] = { "On click", "With previous", "After previous" };
static const char *const FX_NAMES[FX_COUNT] = { "Appear", "Fade", "Fly in", "Wipe", "Zoom", "Float", "Grow", "Pulse", "Spin", "Colour" };
static const char *const CLASS_NAMES[] = { "Entrance", "Emphasis", "Exit" };
static const char *const CHART_NAMES[CH_COUNT] = { "Columns", "Bars", "Lines", "Pie", "Area" };
static const char *const SHAPE_NAMES[SH_COUNT] = { "Rectangle", "Rounded rectangle", "Ellipse", "Triangle", "Right triangle", "Diamond", "Pentagon",
	"Hexagon", "Octagon", "Star (5)", "Star (4)", "Heart", "Arrow right", "Arrow left", "Arrow up", "Arrow down", "Arrow left-right", "Chevron",
	"Pentagon arrow", "Callout", "Cloud", "Plus", "Parallelogram", "Trapezoid", "Can", "Donut", "Document", "Terminator" };
static const char *const SIZE_NAMES[] = { "16:9 widescreen", "16:10", "4:3 standard", "A4 landscape" };
static const int SIZE_W[] = { 28000, 28000, 28000, 29700 }, SIZE_H[] = { 15750, 17500, 21000, 21000 };
static const char *const ADV_NAMES[] = { "On click", "After a time" };

static Vec<SRow> rows;
static void sb_layout ();

static SRow &add (int type, int id, const char *label)
{
	SRow r; memset (&r, 0, sizeof r);
	r.type = type; r.id = id; scpy (r.label, TR (label), sizeof r.label); r.step = 1; r.lo = -1e9; r.hi = 1e9; r.unit = "";
	rows.push (r);
	return rows[rows.n - 1];
}
static void section (const char *t) { add (R_SECTION, 0, t); }
static void pick (int id, const char *label, const char *value) { SRow &r = add (R_PICK, id, label); scpy (r.value, TR (value), sizeof r.value); }
static void color (int id, const char *label, unsigned c, bool half = false) { SRow &r = add (R_COLOR, id, label); r.color = c; r.half = half; }
static void spin (int id, const char *label, double v, double step, double lo, double hi, int dec, const char *unit, bool half = false)
{ SRow &r = add (R_SPIN, id, label); r.num = v; r.step = step; r.lo = lo; r.hi = hi; r.dec = dec; r.unit = unit; r.half = half; }
static void check (int id, const char *label, bool on, bool half = false) { SRow &r = add (R_CHECK, id, label); r.on = on; r.half = half; }
static void buttons (int id, const char *label, const int *icons, int n, const bool *on = 0)
{ SRow &r = add (R_BUTTONS, id, label); r.nbtn = n > 8 ? 8 : n; for (int i = 0; i < n && i < 8; i++) { r.icon[i] = icons[i]; r.btnOn[i] = on && on[i]; } }
static void button (int id, const char *label) { add (R_BUTTON, id, label); }
static void info (const char *t) { add (R_INFO, 0, t); }

static void build_shape ();
static void build_text ();
static void build_anim (Slide &s);
static void build_slide (Slide &s);
static void build ()
{
	rows.clear ();
	Slide *s = cur_slide ();
	if (!s) return;
	switch (g_sbTab)
	{
	case SB_SLIDE: build_slide (*s); break;
	case SB_SHAPE: build_shape (); break;
	case SB_TEXT: build_text (); break;
	case SB_ANIM: build_anim (*s); break;
	}
	sb_layout ();
}
static void build_master (Slide &s);
static void build_slide (Slide &s)
{
	if (g_master) { build_master (s); return; }
	section ("Layout");
	pick (I_LAYOUT, "", LAYOUT_NAMES[iclamp (s.layout, 0, LY_COUNT - 1)]);
	button (I_RESET, "Reset to the layout");
	button (I_MASTER_VIEW, "Master and layouts...");
	section ("Theme");
	add (R_THEMES, I_THEME, "");
	pick (I_FONT_MAJOR, "Headings", g_deck.theme.major);
	pick (I_FONT_MINOR, "Body", g_deck.theme.minor);
	section ("Background");
	Fill bg = s.bg.type == FILL_INHERIT ? g_deck.masterBg : s.bg;
	pick (I_BG_TYPE, "", s.bg.type == FILL_INHERIT ? "The theme's" : bg.type == FILL_GRADIENT ? "Gradient" : "Solid colour");
	if (s.bg.type != FILL_INHERIT)
	{
		color (I_BG_C1, bg.type == FILL_GRADIENT ? "From" : "Colour", bg.c1);
		if (bg.type == FILL_GRADIENT) { color (I_BG_C2, "To", bg.c2, true); spin (I_BG_ANGLE, "Angle", bg.angle, 15, 0, 359, 0, "\xC2\xB0"); }
	}
	check (I_MASTER_OBJ, "The master's objects", s.masterObjects);
	section ("On every slide");
	check (I_NUMBER, "Slide number", g_deck.number);
	check (I_FOOTER, "Footer", g_deck.footer && g_deck.footerText[0]);
	section ("This slide");
	check (I_HIDDEN, "Hidden in the show", s.hidden);
	section ("Slide size");
	int k = 0; for (int i = 0; i < 4; i++) if (g_deck.sw == SIZE_W[i] && g_deck.sh == SIZE_H[i]) k = i;
	pick (I_SIZE, "", SIZE_NAMES[k]);
}
// The master view's Slide tab: the master (its background, the theme) or a layout (its name, its placeholders)
static void build_master (Slide &s)
{
	(void) s;
	if (g_cur == 0)
	{
		section ("The master");
		info ("Its objects: on every slide.");
		info ("The samples' formats are");
		info ("the styles all text follows.");
	}
	else
	{
		section ("Layout");
		pick (I_LAYOUT_NAME, "Name", g_deck.layout[g_cur - 1].name);
		info ("A text box drawn here, or");
		info ("a picture: a placeholder.");
		button (I_PH_TITLE, "Add a title placeholder");
		button (I_PH_TEXT, "Add a text placeholder");
		button (I_PH_PIC, "Add a picture placeholder");
	}
	button (I_MASTER_VIEW, "Close the master view");
	section ("Theme");
	add (R_THEMES, I_THEME, "");
	pick (I_FONT_MAJOR, "Headings", g_deck.theme.major);
	pick (I_FONT_MINOR, "Body", g_deck.theme.minor);
	if (g_cur == 0)
	{
		section ("Background");
		const Fill &bg = g_deck.masterBg;
		pick (I_BG_TYPE, "", bg.type == FILL_GRADIENT ? "Gradient" : "Solid colour");
		color (I_BG_C1, bg.type == FILL_GRADIENT ? "From" : "Colour", bg.c1);
		if (bg.type == FILL_GRADIENT) { color (I_BG_C2, "To", bg.c2, true); spin (I_BG_ANGLE, "Angle", bg.angle, 15, 0, 359, 0, "\xC2\xB0"); }
	}
	section ("On every slide");
	check (I_NUMBER, "Slide number", g_deck.number);
	check (I_FOOTER, "Footer", g_deck.footer && g_deck.footerText[0]);
}
static void build_shape ()
{
	Object *o = g_sel.n ? obj_of (g_sel[0]) : 0;
	if (!o) { info ("Select an object on the slide"); info ("(a click; Shift: several)."); return; }
	if (o->kind != OB_LINE && o->kind != OB_TABLE && o->kind != OB_CHART)
	{
		section ("Fill");
		int ft = o->fill.type == FILL_GRADIENT ? 2 : o->fill.type == FILL_SOLID ? 1 : 0;
		pick (I_FILL_TYPE, "", FILL_NAMES[ft]);
		if (ft) color (I_FILL_C1, ft == 2 ? "From" : "Colour", o->fill.c1);
		if (ft == 2) { color (I_FILL_C2, "To", o->fill.c2, true); spin (I_FILL_ANGLE, "Angle", o->fill.angle, 15, 0, 359, 0, "\xC2\xB0"); }
		if (ft) spin (I_FILL_ALPHA, "Transparency", (255 - o->fill.alpha) * 100 / 255, 5, 0, 100, 0, " %");
	}
	if (o->kind != OB_TABLE && o->kind != OB_CHART)
	{
		section ("Line");
		pick (I_LINE_TYPE, "", LINE_NAMES[(int) o->line.type]);
		if (o->line.type != LN_NONE) { color (I_LINE_C, "Colour", o->line.color); spin (I_LINE_W, "Width", o->line.width / 35.278, 0.25, 0.25, 20, 2, " pt", true); }
		if (o->kind == OB_LINE) { pick (I_HEAD0, "Start", HEAD_NAMES[(int) o->line.head0]); pick (I_HEAD1, "End", HEAD_NAMES[(int) o->line.head1]); }
	}
	if (o->kind == OB_SHAPE || o->kind == OB_PICTURE)
	{
		section (o->kind == OB_PICTURE ? "Picture" : "Shape");
		pick (I_SHAPE, "", SHAPE_NAMES[(int) o->shape]);
		if (o->shape == SH_ROUND) spin (I_RADIUS, "Corners", o->radius / 10, 5, 0, 50, 0, " %");
		if (o->kind == OB_PICTURE) spin (I_CROP, "Crop (sides)", o->crop[0] / 10, 2, 0, 45, 0, " %");
	}
	if (o->kind != OB_LINE) check (I_SHADOW, "Shadow", o->shadow);
	if (o->kind == OB_TABLE && o->tbl)
	{
		section ("Table");
		check (I_TBL_HEAD, "Heading row", o->tbl->header);
		check (I_TBL_BAND, "Banded rows", o->tbl->banded);
		
		pick (I_TBL_ROWS, "Rows, columns", "Insert or delete...");
	}
	if (o->kind == OB_CHART && o->chart)
	{
		section ("Chart");
		pick (I_CHART_TYPE, "Type", CHART_NAMES[o->chart->type]);
		check (I_CHART_LEGEND, "Legend", o->chart->legend);
		check (I_CHART_LABELS, "Values on the bars", o->chart->labels, true);
		button (I_CHART_DATA, "Edit the data...");
	}
	section ("Position and size (cm)");
	spin (I_X, "X", o->x / 1000.0, 0.1, -50, 100, 2, ""); spin (I_Y, "Y", o->y / 1000.0, 0.1, -50, 100, 2, "", true);
	spin (I_W, "W", o->w / 1000.0, 0.1, 0, 100, 2, ""); spin (I_H, "H", o->h / 1000.0, 0.1, 0, 100, 2, "", true);
	if (o->kind != OB_LINE) spin (I_ROT, "Rotation", o->rot, 15, 0, 359, 0, "\xC2\xB0");
	if (o->kind != OB_LINE && o->kind != OB_TABLE) { bool fl[2] = { o->flipH, o->flipV }; static const int fi[2] = { SL_DIST_H, SL_DIST_V }; (void) fl; (void) fi; check (I_FLIP, "Flipped", o->flipH, false); }
	section ("Arrange");
	static const int ord[4] = { SL_FRONT, SL_FORWARD, SL_BACKWARD, SL_BACK };
	buttons (I_ORDER, "", ord, 4);
	static const int al[8] = { SL_AL_LEFT, SL_AL_CENTER, SL_AL_RIGHT, SL_AL_TOP, SL_AL_MIDDLE, SL_AL_BOTTOM, SL_DIST_H, SL_DIST_V };
	buttons (I_ALIGN, "", al, g_sel.n >= 3 ? 8 : 6);
}
static void build_text ()
{
	Object *o = g_edit ? obj_of (g_edit) : g_sel.n ? obj_of (g_sel[0]) : 0;
	if (!o || o->kind == OB_PICTURE || o->kind == OB_LINE || o->kind == OB_CHART) { info ("Select a text, a shape or a table,"); info ("or click into its text."); return; }
	CharFmt f = shown_format ();
	ParaFmt pf = shown_para ();
	section ("Character");
	pick (I_FONT, "", g_deck.font_name (f.font));
	spin (I_SIZE_T, "Size", f.size / 10.0, 2, 4, 400, 1, " pt");
	color (I_COLOR, "Colour", f.color, true);
	int st[6] = { 300, 301, 302, 303, 304, 305 }; bool on[6] = { (f.flags & CF_BOLD) != 0, (f.flags & CF_ITALIC) != 0, (f.flags & CF_UNDER) != 0, (f.flags & CF_STRIKE) != 0, (f.flags & CF_SUPER) != 0, (f.flags & CF_SUB) != 0 };
	buttons (I_STYLE, "", st, 6, on);
	section ("Paragraph");
	int als[4] = { 306, 307, 308, 309 }; bool alo[4] = { pf.align == AL_LEFT, pf.align == AL_CENTER, pf.align == AL_RIGHT, pf.align == AL_JUSTIFY };
	buttons (I_ALIGN_T, "", als, 4, alo);
	pick (I_BULLET, "List", BULLET_NAMES[iclamp (pf.bullet, 0, 2)]);
	spin (I_LEVEL, "Level", pf.level + 1, 1, 1, 5, 0, "", true);
	spin (I_SPACING, "Line spacing", pf.spacing / 100.0, 0.1, 0.5, 3, 2, "");
	spin (I_BEFORE, "Before", pf.before / 10.0, 2, 0, 200, 0, " pt"); spin (I_AFTER, "After", pf.after / 10.0, 2, 0, 200, 0, " pt", true);
	if (!o->tbl)
	{
		section ("Text box");
		int an[3] = { SL_ANCHOR_T, SL_ANCHOR_M, SL_ANCHOR_B }; bool ano[3] = { o->tb.anchor == AN_TOP, o->tb.anchor == AN_MIDDLE, o->tb.anchor == AN_BOTTOM };
		buttons (I_ANCHOR, "", an, 3, ano);
		pick (I_FIT, "Autofit", FIT_NAMES[(int) o->tb.fit]);
		check (I_WRAP, "Wrap the text", o->tb.wrap);
	}
}
static void build_anim (Slide &s)
{
	section ("Transition to this slide");
	pick (I_TR_TYPE, "", TR_NAMES[(int) s.tr.type]);
	if (s.tr.type == TR_PUSH || s.tr.type == TR_WIPE || s.tr.type == TR_COVER || s.tr.type == TR_UNCOVER) pick (I_TR_DIR, "", DIR_NAMES[iclamp (s.tr.dir, 1, 4)]);
	if (s.tr.type != TR_NONE) spin (I_TR_DUR, "Duration", s.tr.dur / 1000.0, 0.1, 0.1, 5, 1, " s");
	pick (I_TR_ADV, "Next slide", ADV_NAMES[s.tr.after >= 0]);
	if (s.tr.after >= 0) spin (I_TR_AFTER, "After", s.tr.after / 1000.0, 1, 0, 600, 0, " s", true);
	button (I_TR_ALL, "Apply to every slide");
	section ("Effects on this slide");
	add (R_EFFECTS, I_FX_LIST, "");
	pick (I_FX_ADD, "", g_sel.n ? "Add an effect..." : "(select an object to add one)");
	static const int tools[3] = { SL_REMOVE, SL_UP, SL_DOWN };
	buttons (I_FX_TOOLS, "", tools, 3);
	if (g_animSel >= 0 && g_animSel < s.anim.n)
	{
		Anim &a = s.anim[g_animSel];
		Object *o = s.by_id (a.obj);
		char t[64]; snprintf (t, sizeof t, "%s  \xC2\xB7  %s", TR (FX_NAMES[(int) a.fx]), o ? (o->name[0] ? TR (o->name) : TR ("Object")) : "?");
		section (t);
		pick (I_FX_EFFECT, "Effect", FX_NAMES[(int) a.fx]);
		pick (I_FX_START, "Start", START_NAMES[(int) a.start]);
		if (a.fx == FX_FLY || a.fx == FX_WIPE) pick (I_FX_DIR, "From", DIR_NAMES[iclamp (a.dir, 1, 4)]);
		spin (I_FX_DELAY, "Delay", a.delay / 1000.0, 0.1, 0, 30, 1, " s");
		spin (I_FX_DUR, "Duration", a.dur / 1000.0, 0.1, 0.1, 10, 1, " s", true);
		if (o && !o->tb.empty () && o->tb.p.n > 1) check (I_FX_PARA, "By paragraph", a.byPara);
	}
	button (I_FX_PLAY, "\xC2\xBB  Play the slide from here");
}
// The colours of the picker: one of the theme's colours stays the theme's (a new theme recolours it).
static unsigned palette_code (long c)
{
	static const int order[10] = { TC_LT1, TC_DK1, TC_LT2, TC_DK2, TC_ACC1, TC_ACC2, TC_ACC3, TC_ACC4, TC_ACC5, TC_ACC6 };
	for (int i = 0; i < 10; i++) if (((unsigned) c & 0xFFFFFF) == g_deck.theme.col[order[i]]) return THEME | order[i];
	return (unsigned) c & 0xFFFFFF;
}


// ---- the rows' places -------------------------------------------------------------------------------------------
static long sbv, sb_seg, sb_bar;			// the view, its tabs (above it), its scroll bar
static int sb_x, sb_y, sb_w, sb_h, sb_top, sb_total;
enum { SB_TABS = 8, SB_SCROLL = 12 };
static int sb_tabs_h () { return ctl_h () + SB_TABS; }
static int row_h () { return ctl_h () - 2; }
static void sb_layout ()
{
	int y = 4, W = sb_w - 20 - SB_SCROLL / 2;
	for (int i = 0; i < rows.n; i++)
	{
		SRow &r = rows[i];
		int h = r.type == R_SECTION ? 24 : r.type == R_THEMES ? 3 * 60 + 6 : r.type == R_EFFECTS ? imax (1, cur_slide ()->anim.n) * 36 + 10 : r.type == R_INFO ? 20 : row_h ();
		if (r.half && i > 0)
		{
			// the row before keeps the left half
			SRow &p = rows[i - 1];
			p.w = W / 2 - 4; r.x = 10 + W / 2 + 4; r.w = W / 2 - 4; r.y = p.y; r.h = p.h;
			continue;
		}
		r.x = 10; r.w = W; r.y = y; r.h = h;
		y += h + (r.type == R_SECTION ? 0 : 4);
	}
	sb_total = y + 10;
}
// the room a row's label takes, its control's x, its width
static void row_cx (const SRow &r, int *cx, int *cw)
{
	*cx = r.x; *cw = r.w;
	if (r.label[0] && r.type != R_SECTION && r.type != R_CHECK && r.type != R_BUTTON && r.type != R_INFO)
	{
		int lw = r.half || r.w < 150 ? text_w (r.label) + 8 : 92;
		*cx += lw; *cw -= lw;
	}
}

// ---- the controls of the rows ------------------------------------------------------------------------------------
struct SCtl { long w[8]; int n; };
static Vec<SCtl> s_ctl;
static unsigned s_rowsHash, s_shapeHash;
static void sb_paint (long v, long w, long h);
static void ctl_destroy ()
{
	for (int i = 0; i < s_ctl.n; i++) for (int k = 0; k < s_ctl[i].n; k++) ui_w_destroy (s_ctl[i].w[k]);
	s_ctl.clear ();
}
static long kid (long w) { ui_w_set_parent (w, sbv); return w; }
// a row's controls, made
static void ctl_build ()
{
	ctl_destroy ();
	for (int i = 0; i < rows.n; i++)
	{
		SRow &r = rows[i];
		SCtl c; c.n = 0;
		switch (r.type)
		{
		case R_PICK: case R_BUTTON: c.w[c.n++] = kid (ui_pushbutton (0, 0, 10, 10, (long) "", 0)); break;
		case R_COLOR: c.w[c.n++] = kid (ui_colorpicker (0, 0, 40, 10, 0)); ui_w_set_range (c.w[0], 0, 0xFFFFFF, 0, 1); break;
		case R_CHECK: c.w[c.n++] = kid (ui_checkbox (0, 0, 10, 10, (long) r.label, r.on)); break;
		case R_SPIN:
			c.w[c.n++] = kid (ui_textbox (0, 0, 10, 10, 0));
			c.w[c.n++] = kid (ui_pushbutton (0, 0, 10, 10, (long) "\xE2\x88\x92", UB_GHOST));
			c.w[c.n++] = kid (ui_pushbutton (0, 0, 10, 10, (long) "+", UB_GHOST));
			break;
		case R_BUTTONS:
			for (int b = 0; b < r.nbtn; b++) c.w[c.n++] = kid (ui_pushbutton (0, 0, 10, 10, (long) icon_glyph (r.icon[b]), UB_GHOST));
			break;
		}
		s_ctl.push (c);
	}
}
// their places (the view scrolled)
static void ctl_place ()
{
	for (int i = 0; i < rows.n && i < s_ctl.n; i++)
	{
		SRow &r = rows[i]; SCtl &c = s_ctl[i];
		int cx, cw; row_cx (r, &cx, &cw);
		int x = sb_x + cx, y = sb_y + r.y - sb_top, h = r.h;
		switch (r.type)
		{
		case R_PICK: case R_CHECK: case R_BUTTON: ui_w_move (c.w[0], x, y, cw, h); break;
		case R_COLOR: ui_w_move (c.w[0], x, y, imin (cw, 54), h); break;
		case R_SPIN:
			// (a narrow row: the field alone)
			ui_w_show (c.w[1], cw >= 120); ui_w_show (c.w[2], cw >= 120);
			ui_w_move (c.w[0], x, y, cw >= 120 ? cw - 64 : cw, h);
			ui_w_move (c.w[1], x + cw - 62, y, 31, h); ui_w_move (c.w[2], x + cw - 31, y, 31, h);
			break;
		case R_BUTTONS:
			for (int b = 0; b < c.n; b++) ui_w_move (c.w[b], x + b * 30, y, 28, h);
			break;
		}
	}
}
static void ctl_values ()
{
	long focus = ui_w_get_focus ();
	for (int i = 0; i < rows.n && i < s_ctl.n; i++)
	{
		SRow &r = rows[i]; SCtl &c = s_ctl[i];
		char t[96];
		switch (r.type)
		{
		case R_PICK: snprintf (t, sizeof t, "%s \xE2\x96\xBE", r.value); ui_w_set_text (c.w[0], (long) t); break;
		case R_BUTTON: ui_w_set_text (c.w[0], (long) r.label); break;
		case R_COLOR: ui_w_set_value (c.w[0], g_deck.rgb (r.color)); break;
		case R_CHECK: ui_w_set_value (c.w[0], r.on); break;
		case R_SPIN:
			if (focus != c.w[0]) { snprintf (t, sizeof t, "%.*f%s", r.dec, r.num, r.unit); ui_w_set_text (c.w[0], (long) t); }
			break;
		case R_BUTTONS:
			for (int b = 0; b < c.n; b++) ui_w_style (c.w[b], r.btnOn[b] ? UB_DEFAULT : UB_GHOST);
			break;
		}
	}
}
static void sb_scroll_range ()
{
	sb_h = imax (0, sb_h);
	int maxTop = imax (0, sb_total - sb_h); if (sb_top > maxTop) sb_top = maxTop;
	ui_w_set_range (sb_bar, 0, sb_total, sb_h, 40);
	ui_w_set_value (sb_bar, sb_top);
	ui_w_show (sb_bar, sb_total > sb_h);
}
// The state -> the rows -> the controls (the main loop; nothing if nothing changed).
static void sb_refresh ()
{
	build ();
	Hash h; Hash sh;
	h.addi (g_sbTab); h.addi (rows.n); h.addi (cur_slide () ? cur_slide ()->anim.n : 0); h.addi (g_animSel);
	Slide *cs = cur_slide ();
	for (int i = 0; cs && i < cs->anim.n; i++) h.add (&cs->anim[i], sizeof (Anim));
	h.add (g_deck.theme.name, (int) strlen (g_deck.theme.name));
	for (int i = 0; i < rows.n; i++)
	{
		SRow &r = rows[i];
		h.addi (r.type); h.addi (r.id); h.add (r.label, sizeof r.label); h.add (r.value, sizeof r.value); h.addi ((int) r.color); h.add (&r.num, sizeof r.num); h.addi (r.on);
		h.addi (r.nbtn); for (int b = 0; b < r.nbtn; b++) h.addi (r.btnOn[b]);
		h.addi (r.y); h.addi (r.x); h.addi (r.w);
		sh.addi (r.type); sh.addi (r.id); sh.addi (r.nbtn); sh.add (r.label, sizeof r.label);
	}
	if (h.h == s_rowsHash) return;
	s_rowsHash = h.h;
	if (sh.h != s_shapeHash || s_ctl.n != rows.n) { s_shapeHash = sh.h; ctl_build (); }
	sb_scroll_range ();
	ctl_place ();
	ctl_values ();
	ui_view_dirty (sbv);
}
static void sb_set_top (int t)
{
	sb_top = imax (0, imin (sb_total - sb_h, t));
	sb_scroll_range ();
	ctl_place ();
	ui_view_dirty (sbv);
}

// ---- drawing ------------------------------------------------------------------------------------------------------
static void sb_paint (long, long w, long h)
{
	unsigned P = jet (J_FACE), ink = jet (J_TEXTE1), dim = jet (J_TEXTE3), acc = jet (J_ACCENT);
	ui_fill (0, 0, w, h, P);
	for (int i = 0; i < rows.n; i++)
	{
		SRow &r = rows[i];
		int y = r.y - sb_top;
		if (y + r.h < 0 || y > h) continue;
		if (r.label[0] && r.type != R_SECTION && r.type != R_CHECK && r.type != R_BUTTON && r.type != R_INFO) text_box (r.label, r.x, y, r.h, dim, 0, r.w);
		switch (r.type)
		{
		case R_SECTION:
			ui_fill (1, y + 2, w - 2, r.h - 4, jet (J_CREUX));
			text_box (r.label, 12, y, r.h, ink, font_bold (), (int) w - 16);
			break;
		case R_INFO: text_box (r.label, r.x, y, r.h, dim); break;
		case R_THEMES:
		{
			int tw = (r.w - 10) / 2, th = 50;
			for (int k = 0; k < NTHEMES; k++)
			{
				int tx = r.x + (k % 2) * (tw + 10), ty = y + (k / 2) * 60;
				const ThemeDef &T = THEMES[k];
				bool cur = !strcmp (g_deck.theme.name, T.name);
				if (cur) ui_rbox (tx - 3, ty - 3, tw + 6, th + 6, 5, acc, acc);
				ui_fill (tx, ty, tw, th - 14, 0xFFFFFF); ui_frame (tx, ty, tw, th - 14, 0xC0B4AC);
				ui_fill (tx, ty, 4, th - 14, T.acc1);
				ui_fill (tx + 10, ty + 7, tw / 2, 5, T.dk2); ui_fill (tx + 10, ty + 15, 18, 3, T.acc2);
				ui_fill (tx + 10, ty + 23, tw * 6 / 10, 2, 0xD2CEC8);
				text_box (TR (T.name), tx + 2, ty + th - 14, 14, cur ? jet (J_SURFACE) : ink);
			}
			break;
		}
		case R_EFFECTS:
		{
			Slide *s = cur_slide ();
			ui_rbox (r.x, y, r.w, r.h - 4, 5, jet (J_SURFACE), jet (J_SURFACE));
			ui_frame (r.x, y, r.w, r.h - 4, jet (J_LIGNE));
			if (!s->anim.n) { text_box (TR ("No effect yet."), r.x + 10, y + 4, 26, dim); break; }
			int n = 0;
			for (int k = 0; k < s->anim.n; k++)
			{
				Anim &a = s->anim[k];
				int ry = y + 4 + k * 36;
				if (k == g_animSel) ui_rbox (r.x + 3, ry, r.w - 6, 34, 4, jet (J_ACCENT_CLAIR), jet (J_ACCENT_CLAIR));
				if (a.start == ST_CLICK || k == 0) n++;
				char num[8]; snprintf (num, sizeof num, "%d", n);
				if (a.start == ST_CLICK || k == 0) text_box (num, r.x + 8, ry, 18, dim, font_bold ());
				unsigned dc = a.cls == AC_ENTRANCE ? 0x3C9650 : a.cls == AC_EMPHASIS ? 0xD2A028 : 0xBE463C;
				ui_vp_disc (16 * (r.x + 30), 16 * (ry + 9), 16 * 5, dc);
				Object *o = s->by_id (a.obj);
				char t[80]; snprintf (t, sizeof t, "%s  \xC2\xB7  %s", TR (FX_NAMES[(int) a.fx]), o ? (o->name[0] ? TR (o->name) : TR ("Object")) : "?");
				text_box (t, r.x + 42, ry, 18, ink, k == g_animSel ? font_bold () : 0, r.w - 48);
				text_box (TR (START_NAMES[(int) a.start]), r.x + 42, ry + 16, 16, dim);
			}
			break;
		}
		}
	}
}

// ---- the mouse, the controls' news --------------------------------------------------------------------------------
static void sb_pick_list (SRow &r, int x, int y, int w);	// (main.cpp)
static void sb_act (const SRow &r, int sub, double v);
static long sb_mouse (long, long kind, long mx, long my, long info)
{
	if (kind != 1 || !(info >> 8 & 1)) return 0;
	for (int i = 0; i < rows.n; i++)
	{
		SRow r = rows[i];
		int y = r.y - sb_top;
		if (my < y || my >= y + r.h || mx < r.x || mx >= r.x + r.w) continue;
		if (r.type == R_THEMES) { int tw = (r.w - 10) / 2; int k = (((int) my - y) / 60) * 2 + (((int) mx - r.x) / (tw + 10)); if (k >= 0 && k < NTHEMES) sb_act (r, k, 0); }
		if (r.type == R_EFFECTS) { int k = ((int) my - y - 4) / 36; Slide *s = cur_slide (); if (k >= 0 && k < s->anim.n) { g_animSel = k; g_ui = true; } }
		break;
	}
	return 0;
}
static long sb_wheel (long, long wheel) { sb_set_top (sb_top - (int) wheel * 40); return 0; }
// A control's news: true if it was one of the rows'.
static bool sb_event (long w, long code)
{
	if (w == sb_seg) { if (code == UN_CHANGE) { g_sbTab = (int) ui_w_get_value (sb_seg); sb_top = 0; g_ui = true; } return true; }
	if (w == sb_bar) { if (code == UN_CHANGE) sb_set_top ((int) ui_w_get_value (sb_bar)); return true; }
	for (int i = 0; i < rows.n && i < s_ctl.n; i++)
	{
		SCtl &c = s_ctl[i];
		int k = -1;
		for (int j = 0; j < c.n; j++) if (c.w[j] == w) k = j;
		if (k < 0) continue;
		SRow r = rows[i];
		int cx, cw; row_cx (r, &cx, &cw);
		switch (r.type)
		{
		case R_PICK: if (code == UN_ACTIVATE) sb_pick_list (r, sb_x + cx, sb_y + r.y - sb_top + r.h, cw); break;
		case R_BUTTON: if (code == UN_ACTIVATE) sb_act (r, 0, 0); break;
		case R_BUTTONS: if (code == UN_ACTIVATE) sb_act (r, k, 0); break;
		case R_CHECK: if (code == UN_CHANGE) sb_act (r, 0, ui_w_get_value (w) ? 1 : 0); break;
		case R_COLOR: if (code == UN_CHANGE) { SRow t = r; t.color = palette_code (ui_w_get_value (w)); sb_act (t, 0, 0); } break;
		case R_SPIN:
		{
			double v = r.num;
			if (k == 0) { if (code != UN_ACTIVATE) break; char b[32]; scpy (b, (const char *) ui_w_get_text (w), sizeof b); for (char *p = b; *p; p++) if (*p == ',') *p = '.'; v = strtod (b, 0); }
			else if (code != UN_ACTIVATE) break;
			else v += k == 1 ? -r.step : r.step;
			if (v < r.lo) v = r.lo;
			if (v > r.hi) v = r.hi;
			sb_act (r, 0, v);
			break;
		}
		}
		return true;
	}
	return false;
}
static void sb_create (int x, int y, int w, int h)
{
	sb_seg = ui_segmented (x + 6, y + 3, w - 12, ctl_h (), (long) TR ("Slide|Shape|Text|Fx"));
	int ty = sb_tabs_h ();
	sb_x = x; sb_y = y + ty; sb_w = w; sb_h = h - ty;
	sbv = ui_view (sb_x, sb_y, sb_w, sb_h, 0);
	ui_view_on (sbv, (long) sb_paint, (long) sb_mouse, 0, (long) sb_wheel);
	sb_bar = ui_scrollbar (x + w - SB_SCROLL, sb_y, SB_SCROLL, sb_h, 0);
	ui_w_set_parent (sb_bar, sbv);
}
static void sb_move (int x, int y, int w, int h)
{
	int ty = sb_tabs_h ();
	ui_w_move (sb_seg, x + 6, y + 3, w - 12, ctl_h ());
	sb_x = x; sb_y = y + ty; sb_w = w; sb_h = h - ty;
	ui_w_move (sbv, sb_x, sb_y, sb_w, sb_h);
	ui_w_move (sb_bar, x + w - SB_SCROLL, sb_y, SB_SCROLL, sb_h);
	s_rowsHash = 0;
}

} // namespace sl

#endif
