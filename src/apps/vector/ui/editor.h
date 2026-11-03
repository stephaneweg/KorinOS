//
// editor.h -- the editor's state, shared by the tools, the panels and the window (docs/UX-VECTOR.md). One document, one
// view. Everything that changes the drawing goes through vd::Document; everything here only points at it.
//
#ifndef _vector_editor_h
#define _vector_editor_h

#include "../render/tiles.h"
#include <uikit.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class SkCanvas;

#define TR(x) ((const char *) ui_tr ((long) (x)))

namespace ve {

using vd::XNode;
using vg::Affine;
using vg::Box;
using vg::Vec2;

enum Jet { J_FACE, J_SURFACE, J_CREUX, J_ELEVE, J_ELEVE_BAS, J_LIGNE, J_LIGNE_FORTE, J_TEXTE1, J_TEXTE2, J_TEXTE3,
	J_ACCENT, J_ACCENT_CLAIR, J_ACCENT_FONCE, J_ACCENT_VIF, J_DANGER, J_SUCCES, J_ALERTE };
unsigned jet (int j);

// the rail, in the order of docs/UX-VECTOR.md 3.4 (what exists so far)
enum ToolId { T_SELECT, T_NODE, T_RECT, T_ELLIPSE, T_STAR, T_SPIRAL, T_PEN, T_PENCIL, T_TEXT, T_GRADIENT, T_DROPPER, T_HAND, T_ZOOM,
	T_CALLI, T_ERASER, T_BUCKET, T_TWEAK, T_SPRAY, T_CONNECT, T_MEASURE, T_BUILDER, T_COUNT };

enum { MOD_SHIFT = 1, MOD_CTRL = 2, MOD_ALT = 4 };

struct Pointer
{
	Vec2 s;				// in the canvas, device px
	Vec2 p;				// on the page, px
	int mods = 0, clicks = 0, button = 1;
};

class Tool
{
public:
	virtual ~Tool () {}
	virtual void press (const Pointer &) {}
	virtual void move (const Pointer &) {}
	virtual void hover (const Pointer &) {}
	virtual void release (const Pointer &) {}
	virtual bool key (int hid, int ch, int mods) { (void) hid; (void) ch; (void) mods; return false; }
	virtual void overlay (SkCanvas *) {}
	virtual void enter () {}
	virtual void leave () {}
	virtual const char *hint () { return ""; }
	virtual int cursor () { return UC_CROSS; }
	// the bar's buttons for this tool (docs/UX-VECTOR.md 3.4)
	virtual int bar_count () { return 0; }
	virtual const char *bar_label (int) { return ""; }
	virtual void bar_click (int) {}
};

struct Editor
{
	vd::Document doc;
	vr::SceneBuilder *builder = nullptr;
	std::shared_ptr<const vr::Scene> scene;
	vr::TileCache *tiles = nullptr;
	vr::ViewState view;
	int cw = 0, ch = 0;			// the canvas, px
	std::vector<XNode *> sel;
	XNode *layer = nullptr;			// where new objects go
	int tool = T_SELECT, prevTool = -1;
	Tool *tools[T_COUNT] = {};
	std::string fill = "#4992a7", stroke = "none";	// the next object's
	double strokeWidth = 1;
	bool outline = false, dirty = true, chromeDirty = true, panelDirty = true;
	bool snap = true, grid = false, guides = true, rulers = false;
	double gridStep = 10;			// page px
	bool snapShown = false;			// the last snap found something
	Vec2 snapAt;
	const char *snapWhat = "";
	std::string message;
	int msgUntil = 0;

	Vec2 to_page (Vec2 s) const { return { (s.x - view.ox) / view.zoom, (s.y - view.oy) / view.zoom }; }
	Vec2 to_screen (Vec2 p) const { return { p.x * view.zoom + view.ox, p.y * view.zoom + view.oy }; }
	Affine page_to_doc () const { return scene->docToPx.inverse (); }

	void sync ();				// after any change of the document: the scene, the tiles, the panels
	void select (XNode *n, bool add = false);
	void select_none ();
	bool selected (XNode *n) const;
	void set_tool (int t);
	void say (const char *m);
};
extern Editor E;

// ---- core.cpp
Affine node_transform (const XNode *n);		// its own transform attribute
Affine node_to_page (const XNode *n);		// its coordinates -> page px (its own transform included)
Affine parent_to_page (const XNode *n);
Box object_box (const XNode *n);		// visual, page px
Box selection_box ();
XNode *hit_object (Vec2 page, double tol, XNode *below = nullptr, bool enterGroups = false);
XNode *hit_leaf (Vec2 page, double tol);
bool is_layer (const XNode *n);
XNode *current_layer ();
void transform_selection (const Affine &pageM);	// inside an open transaction
void apply_transform (XNode *n, const Affine &pageM);
XNode *namedview (bool create);
void set_style (XNode *n, const char *prop, const char *value);
void style_selection (const char *prop, const char *value, const char *label, int merge = 0);
XNode *add_shape (const char *name, const std::vector<std::pair<const char *, std::string>> &attrs);
std::string path_of (XNode *n);			// the object's outline as "d", in its own coordinates
vg::Path shape_to_path (XNode *n);
XNode *convert_to_path (XNode *n);
std::string fmt (double v);

// ---- snap.cpp: page px; only what the pointer can reach counts
void snap_begin (bool ignoreSelection);		// gathers the candidates, once per gesture
Vec2 snap_point (Vec2 p, int mods);		// the point, moved to what it found (Shift suspends)
Vec2 snap_box (const Box &moved, int mods);	// the shift that makes a box's edges and centre land on something
void snap_end ();
struct Guide { XNode *node; bool vertical; double at; };	// page px
std::vector<Guide> guides_of ();
void add_guide (bool vertical, double at);
void paint_grid (SkCanvas *c);

// ---- commands.cpp
enum Cmd { C_NONE, C_NEW, C_OPEN, C_SAVE, C_SAVEAS, C_IMPORT, C_EXPORT, C_DOCUMENT, C_CLOSE,
	C_UNDO, C_REDO, C_CUT, C_COPY, C_PASTE, C_PASTEPLACE, C_PASTESTYLE, C_DUP, C_DELETE, C_SELALL, C_SELNONE, C_FIND, C_PREFS,
	C_ZOOMIN, C_ZOOMOUT, C_ZOOM100, C_ZOOM200, C_ZOOMSEL, C_ZOOMDRAW, C_ZOOMPAGE, C_OUTLINE, C_RULERS, C_GRID, C_GUIDES, C_SNAP, C_PANELS,
	C_GROUP, C_UNGROUP, C_RAISE, C_LOWER, C_TOP, C_BOTTOM, C_FLIPH, C_FLIPV, C_ROTCW, C_ROTCCW, C_TRANSFORM, C_ALIGN, C_NEWLAYER, C_LAYERS,
	C_TOPATH, C_STROKEPATH, C_UNION, C_DIFF, C_INTER, C_XOR, C_DIV, C_SIMPLIFY, C_REVERSE, C_COMBINE, C_BREAK,
	C_TEXTPANEL, C_TEXTPATH, C_FILLPANEL, C_FILTERS, C_LPE, C_SEARCH, C_KEYS, C_ABOUT, C_QUIT,
	C_EXPORTOTHER, C_EXTENSIONS, C_TRACE, C_CLONE, C_UNLINK, C_CLIPSET, C_MASKSET, C_CLIPOFF, C_GUIDEV, C_GUIDEH, C_XML, C_DOCPANEL, C_EXPORTPDF, C_COUNT_ };
void command (int id);
void build_menu ();
bool handle_key (int hid, int ch, int mods);	// the global shortcuts
void search_commands ();
bool load_file (const char *path);
bool save_file (const char *path);
void fit_page ();
void zoom_at (double k, Vec2 s);

// ---- ops.cpp
void boolean_op (int cmd);
void stroke_to_path ();
void simplify_selection ();
vg::Path from_skpath (const SkPath &p);
bool export_format (vd::Document &d, vr::SceneBuilder &b, const vr::Scene &sc, const char *path, const char *fmt);	// io.cpp
void export_other ();
void extensions_menu ();
bool ext_run_on (vd::Document &d, const std::string &cmd);
void reverse_selection ();
void combine_selection ();
void break_selection ();
vg::Path fit_curve (const std::vector<Vec2> &pts, double tol);

// ---- overlay.cpp
void paint_overlay (SkCanvas *c);
void paint_selection (SkCanvas *c);
void paint_handle (SkCanvas *c, Vec2 s, int shape, bool hot);	// 0 square, 1 diamond, 2 disc
void paint_badge (SkCanvas *c, Vec2 s, const char *text);
void paint_node (SkCanvas *c, Vec2 s, int kind, bool chosen);
enum { H_NONE = -1, H_NW, H_N, H_NE, H_E, H_SE, H_S, H_SW, H_W, H_ROT, H_COUNT };
Vec2 handle_pos (int h, const Box &screenBox);
int handle_at (Vec2 s);

// ---- tools.cpp
void make_tools ();
void make_tools3 ();
void update_connectors ();			// tools3.cpp: inside a transaction
void trace_selection (int levels);

// ---- chrome.cpp
void chrome_build ();
void chrome_layout ();
void chrome_paint_all ();
void chrome_refresh ();
void paint_icon (int icon, int x, int y, unsigned color, uint32_t *px, int pw, int ph);
bool panel_visible ();
void panel_toggle ();
void panel_show (int which);
void align_selection (int how);			// 0 left 1 centre 2 right 3 top 4 middle 5 bottom
void distribute_selection (bool horizontal);
int key_letter (int hid, int mods);		// the letter of a key under the active layout

}

#endif
