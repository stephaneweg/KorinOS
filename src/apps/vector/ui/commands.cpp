//
// commands.cpp -- every command once: its label, its shortcut, what it does. The menus (the core that fits the 2 048 bytes
// of ui_menu, docs/UX-VECTOR.md 6), the shortcuts and Ctrl+K all read this table.
//
#include "editor.h"
#include "../text/fonts.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/docs/SkPDFDocument.h"
#include "include/core/SkDocument.h"
#include "include/encode/SkPngEncoder.h"
#include <cstdio>
#include <cstring>

using namespace vd;
using namespace vr;

namespace vapp { bool read_file (const char *path, std::string &out); bool write_file (const char *path, const std::string &s); }

namespace ve {

struct CmdInfo { int id; const char *menu, *label, *key; bool core; };
// menu: its title opens a new menu; "-" a separator in the row before
static const CmdInfo CMDS[] = {
	{ C_NEW, "File", "New", "Ctrl+N", true }, { C_OPEN, "", "Open...", "Ctrl+O", true }, { C_NONE, "", "-", "", true },
	{ C_SAVE, "", "Save", "Ctrl+S", true }, { C_SAVEAS, "", "Save As...", "Ctrl+Shift+S", true }, { C_NONE, "", "-", "", true },
	{ C_IMPORT, "", "Import...", "Ctrl+I", true }, { C_EXPORT, "", "Export PNG...", "Ctrl+E", true }, { C_EXPORTPDF, "", "Export PDF...", "", true }, { C_EXPORTOTHER, "", "Export Other...", "", false },
	{ C_UNDO, "Edit", "Undo", "Ctrl+Z", true }, { C_REDO, "", "Redo", "Ctrl+Y", true }, { C_NONE, "", "-", "", true },
	{ C_CUT, "", "Cut", "Ctrl+X", true }, { C_COPY, "", "Copy", "Ctrl+C", true }, { C_PASTE, "", "Paste", "Ctrl+V", true }, { C_PASTEPLACE, "", "Paste in Place", "Ctrl+Shift+V", true },
	{ C_DUP, "", "Duplicate", "Ctrl+D", true }, { C_DELETE, "", "Delete", "Backspace", true }, { C_NONE, "", "-", "", true },
	{ C_SELALL, "", "Select All", "Ctrl+A", true }, { C_SELNONE, "", "Deselect", "Ctrl+Shift+A", true },
	{ C_ZOOMIN, "View", "Zoom In", "=", true }, { C_ZOOMOUT, "", "Zoom Out", "-", true }, { C_ZOOM100, "", "Zoom 100%", "1", true }, { C_ZOOMSEL, "", "Fit Selection", "3", true }, { C_ZOOMDRAW, "", "Fit Drawing", "4", true },
	{ C_ZOOMPAGE, "", "Fit Page", "5", true }, { C_NONE, "", "-", "", true }, { C_OUTLINE, "", "Outlines", "Alt+C", true }, { C_PANELS, "", "Panels", "Tab", true },
	{ C_GROUP, "Object", "Group", "Ctrl+G", true }, { C_UNGROUP, "", "Ungroup", "Ctrl+Shift+G", true }, { C_NONE, "", "-", "", true },
	{ C_RAISE, "", "Raise", "Ctrl+Shift+Up", true }, { C_LOWER, "", "Lower", "Ctrl+Shift+Down", true }, { C_TOP, "", "To Front", "Alt+Shift+Up", true }, { C_BOTTOM, "", "To Back", "Alt+Shift+Down", true }, { C_NONE, "", "-", "", true },
	{ C_FLIPH, "", "Flip Horizontal", "Shift+H", true }, { C_FLIPV, "", "Flip Vertical", "Shift+V", true }, { C_ROTCW, "", "Rotate Right", "Shift+R", false }, { C_ROTCCW, "", "Rotate Left", "Shift+L", false }, { C_NONE, "", "-", "", true },
	{ C_NEWLAYER, "", "New Layer", "Ctrl+Shift+N", true }, { C_LAYERS, "", "Layers...", "Ctrl+L", true },
	{ C_TOPATH, "Path", "Object to Path", "Ctrl+Shift+C", true }, { C_STROKEPATH, "", "Stroke to Path", "Ctrl+Alt+C", true }, { C_NONE, "", "-", "", true },
	{ C_UNION, "", "Union", "Ctrl+Alt+U", true }, { C_DIFF, "", "Difference", "Ctrl+Alt+D", true }, { C_INTER, "", "Intersection", "Ctrl+Alt+I", true }, { C_XOR, "", "Exclusion", "Ctrl+Alt+E", true }, { C_DIV, "", "Division", "Ctrl+Alt+B", true }, { C_NONE, "", "-", "", true },
	{ C_COMBINE, "", "Combine", "Ctrl+Alt+K", false }, { C_BREAK, "", "Break Apart", "Ctrl+Shift+K", false }, { C_SIMPLIFY, "", "Simplify", "Ctrl+Alt+L", true }, { C_REVERSE, "", "Reverse", "Ctrl+Alt+R", false }, { C_TRACE, "", "Trace Image...", "", true },
	{ C_FILTERS, "Text", "Filters...", "", true }, { C_LPE, "", "Path Effects...", "Ctrl+Alt+P", true }, { C_FILLPANEL, "", "Fill and Stroke...", "Ctrl+Shift+F", true }, { C_TEXTPANEL, "", "Text and Font...", "Ctrl+T", true }, { C_EXTENSIONS, "", "Extensions...", "", true }, { C_XML, "", "XML Editor...", "Ctrl+Shift+X", true }, { C_DOCPANEL, "", "Document...", "Ctrl+Shift+E", true },
	{ C_CLONE, "", "Clone", "Alt+D", false }, { C_UNLINK, "", "Unlink Clone", "Alt+Shift+D", false }, { C_CLIPSET, "", "Clip by Top Object", "", false }, { C_MASKSET, "", "Mask by Top Object", "", false }, { C_CLIPOFF, "", "Release Clip and Mask", "", false },
	{ C_GRID, "", "Grid", "Alt+G", false }, { C_GUIDES, "", "Guides", "Alt+U", false }, { C_SNAP, "", "Snapping", "Alt+A", false }, { C_RULERS, "", "Rulers", "Alt+R", false }, { C_GUIDEV, "", "Add Vertical Guide", "", false }, { C_GUIDEH, "", "Add Horizontal Guide", "", false },
	{ C_SEARCH, "Help", "Search Commands...", "Ctrl+K", true }, { C_ABOUT, "", "About Vector", "", true },
};

void build_menu ()
{
	static char b[2040];
	int n = 0;
	for (auto &c : CMDS)
	{
		if (!c.core) continue;
		if (c.menu[0]) n += snprintf (b + n, sizeof b - n, "M%s\n", TR (c.menu));
		if (c.label[0] == '-') n += snprintf (b + n, sizeof b - n, "-\n");
		else n += snprintf (b + n, sizeof b - n, "I%d\t%s\t%s\n", c.id, TR (c.label), c.key);
	}
	ui_menu ((long) b);
}

// ---- files --------------------------------------------------------------------------------------------------------------
static void after_load ()
{
	E.sel.clear ();
	E.layer = nullptr;
	E.builder->invalidate ();
	E.scene = E.builder->update ();
	E.tiles->set_scene (E.scene, nullptr);
	fit_page ();
	E.dirty = E.chromeDirty = E.panelDirty = true;
}
bool load_file (const char *path)
{
	std::string s, err;
	if (!vapp::read_file (path, s)) { E.say (TR ("Cannot read the file.")); return false; }
	if (!E.doc.load (s.data (), s.size (), &err))
	{
		char m[300];
		snprintf (m, sizeof m, "%s\n%s", TR ("This file is not a drawing that Vector can read."), err.c_str ());
		ui_message ((long) "Vector", (long) m, UM_OK);
		return false;
	}
	E.doc.path = path;
	after_load ();
	return true;
}
bool save_file (const char *path)
{
	std::string tmp = std::string (path) + ".tmp";
	if (!vapp::write_file (tmp.c_str (), E.doc.text ())) { ui_message ((long) "Vector", (long) TR ("The file could not be written."), UM_OK); return false; }
	remove (path);
	rename (tmp.c_str (), path);
	E.doc.path = path;
	E.doc.mark_saved ();
	E.say (TR ("Saved."));
	return true;
}
static bool ask_save ()
{
	if (!E.doc.modified ()) return true;
	long r = ui_message ((long) "Vector", (long) TR ("Save the changes to this drawing?"), UM_YESNOCANCEL);
	if (r == 0) return false;		// Cancel
	if (r == 1)
	{
		if (E.doc.path.empty ()) { command (C_SAVEAS); return !E.doc.modified (); }
		return save_file (E.doc.path.c_str ());
	}
	return true;
}
static void ensure_dot_svg (char *f, size_t n)
{
	size_t l = strlen (f);
	if (l < 4 || strcasecmp (f + l - 4, ".svg")) strncat (f, ".svg", n - l - 1);
}
static void export_png ()
{
	char f[256];
	ui_file_filter ((long) "*.png");
	if (!ui_file_save ((long) "SD0:/home/documents", (long) "drawing.png", (long) f, sizeof f)) return;
	int w = (int) std::ceil (E.scene->width), h = (int) std::ceil (E.scene->height);
	std::vector<uint32_t> px ((size_t) w * h);
	render_page (*E.scene, 1, px.data (), w, h, 0xFFFFFF, true);
	SkPixmap pm (SkImageInfo::MakeN32Premul (w, h), px.data (), (size_t) w * 4);
	SkFILEWStream st (f);
	E.say (st.isValid () && SkPngEncoder::Encode (&st, pm, SkPngEncoder::Options ()) ? TR ("Exported.") : TR ("The file could not be written."));
}
bool export_pdf_to (const char *f)
{
	SkFILEWStream st (f);
	if (!st.isValid ()) return false;
	SkPDF::Metadata md;
	md.fCreator = "Vector";
	auto doc = SkPDF::MakeDocument (&st, md);
	if (!doc) return false;
	float k = 72.f / 96;
	SkCanvas *c = doc->beginPage ((float) E.scene->width * k, (float) E.scene->height * k);
	c->scale (k, k);
	draw_scene (c, *E.scene);
	doc->endPage ();
	doc->close ();
	return true;
}
static void export_pdf ()
{
	char f[256];
	ui_file_filter ((long) "*.pdf");
	if (!ui_file_save ((long) "SD0:/home/documents", (long) "drawing.pdf", (long) f, sizeof f)) return;
	E.say (export_pdf_to (f) ? TR ("Exported.") : TR ("The file could not be written."));
}
static void import_image ()
{
	char f[256];
	ui_file_filter ((long) "*.png;*.jpg;*.jpeg;*.webp;*.svg");
	if (!ui_file_open ((long) "SD0:/home/documents", (long) f, sizeof f)) return;
	size_t l = strlen (f);
	if (l > 4 && !strcasecmp (f + l - 4, ".svg"))
	{
		std::string s;
		Document d;
		if (!vapp::read_file (f, s) || !d.load (s.data (), s.size (), nullptr)) { E.say (TR ("Cannot read the file.")); return; }
		E.doc.begin (TR ("Import"));
		XNode *g = x_new (X_ELEMENT, "g");
		g->attrs.push_back ({ "id", E.doc.new_id ("g") });
		for (XNode *k : d.root->kids) if (k->element ()) { XNode *c = k->clone (); c->parent = g; g->kids.push_back (c); }
		E.doc.insert (current_layer (), -1, g);
		E.doc.commit ();
		E.sync ();
		E.select (g);
		return;
	}
	E.doc.begin (TR ("Import"));
	XNode *n = add_shape ("image", { { "xlink:href", f }, { "x", "0" }, { "y", "0" } });
	E.doc.commit ();
	if (!E.doc.root->get ("xmlns:xlink")) { E.doc.begin ("ns"); E.doc.set (E.doc.root, "xmlns:xlink", "http://www.w3.org/1999/xlink"); E.doc.commit (); }
	E.sync ();
	E.select (n);
}

// ---- the clipboard ------------------------------------------------------------------------------------------------------
static const char SVG_MIME[] = "image/svg+xml";
static std::string s_clip;

static std::vector<XNode *> ordered_sel ()
{
	std::vector<XNode *> v = E.sel;
	std::sort (v.begin (), v.end (), [] (XNode *a, XNode *b) { return a->parent == b->parent ? a->index () < b->index () : a->uid < b->uid; });
	return v;
}
static void do_copy ()
{
	if (E.sel.empty ()) return;
	std::string s = "<svg xmlns=\"http://www.w3.org/2000/svg\" xmlns:xlink=\"http://www.w3.org/1999/xlink\" xmlns:sodipodi=\"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd\" xmlns:inkscape=\"http://www.inkscape.org/namespaces/inkscape\">";
	for (XNode *n : ordered_sel ())
	{
		Affine m = parent_to_page (n) * node_transform (n);
		XNode *c = n->clone ();
		for (auto &a : c->attrs) if (a.name == "transform") a.value = "";
		bool has = false;
		for (auto &a : c->attrs) if (a.name == "transform") { a.value = write_transform (m); has = true; }
		if (!has) c->attrs.push_back ({ "transform", write_transform (m) });
		s += x_write_node (c);
		delete c;
	}
	s += "</svg>";
	s_clip = s;
	ui_clip_put ((long) SVG_MIME, (long) s.data (), (long) s.size (), 0);
}
static void do_paste (bool inPlace)
{
	long n = ui_clip_take ((long) "image/svg+xml\0", 0, 0);
	std::string s;
	if (n > 0) { s.resize (n); ui_clip_take ((long) "image/svg+xml\0", (long) &s[0], n); }
	else s = s_clip;
	Document d;
	if (s.empty () || !d.load (s.data (), s.size (), nullptr)) { E.say (TR ("Nothing to paste.")); return; }
	E.doc.begin (TR ("Paste"));
	XNode *l = current_layer ();
	Affine inv = (l == E.doc.root ? E.scene->docToPx : node_to_page (l)).inverse ();
	std::vector<XNode *> made;
	for (XNode *k : d.root->kids)
	{
		if (!k->element ()) continue;
		XNode *c = k->clone ();
		for (auto &a : c->attrs)
		{
			if (a.name == "transform") a.value = write_transform (inv * parse_transform (a.value.c_str ()));
			if (a.name == "id") a.value = E.doc.new_id ("o");
		}
		E.doc.insert (l, -1, c);
		made.push_back (c);
	}
	E.doc.commit ();
	E.sel = made;
	E.sync ();
	if (!inPlace)
	{
		Box b = selection_box ();
		Vec2 c = E.to_page ({ E.cw / 2.0, E.ch / 2.0 });
		E.doc.begin (TR ("Paste"));
		transform_selection (Affine::translate (c.x - b.center ().x, c.y - b.center ().y));
		E.doc.commit ();
		E.sync ();
	}
}

// ---- view ---------------------------------------------------------------------------------------------------------------
void zoom_at (double k, Vec2 s)
{
	double z = E.view.zoom * k;
	if (z < 0.01) z = 0.01;
	if (z > 256) z = 256;
	k = z / E.view.zoom;
	E.view.ox = s.x - (s.x - E.view.ox) * k;
	E.view.oy = s.y - (s.y - E.view.oy) * k;
	E.view.zoom = z;
	E.dirty = E.chromeDirty = true;
}
static void fit_box (const Box &b)
{
	if (b.empty () || b.w () <= 0 || b.h () <= 0 || E.cw <= 16 || E.ch <= 16) return;
	double z = std::min ((E.cw - 16) / b.w (), (E.ch - 16) / b.h ());
	E.view.zoom = std::min (256.0, z);
	E.view.ox = std::round (E.cw / 2.0 - b.center ().x * E.view.zoom);
	E.view.oy = std::round (E.ch / 2.0 - b.center ().y * E.view.zoom);
	E.dirty = E.chromeDirty = true;
}
void fit_page () { fit_box (Box (0, 0, E.scene->width, E.scene->height)); }

// ---- object order -------------------------------------------------------------------------------------------------------
static void reorder (int cmd)
{
	E.doc.begin (TR ("Order"));
	std::vector<XNode *> v = ordered_sel ();
	if (cmd == C_RAISE || cmd == C_TOP) std::reverse (v.begin (), v.end ());
	for (XNode *n : v)
	{
		XNode *p = n->parent;
		int i = n->index (), cnt = (int) p->kids.size ();
		if (cmd == C_TOP) E.doc.move (n, p, cnt - 1);
		else if (cmd == C_BOTTOM) E.doc.move (n, p, 0);
		else
		{
			// the next element up or down: spaces in between do not count
			int j = i;
			while (true)
			{
				j += cmd == C_RAISE ? 1 : -1;
				if (j < 0 || j >= cnt) { j = i; break; }
				if (p->kids[j]->element ()) break;
			}
			E.doc.move (n, p, j);
		}
	}
	E.doc.commit ();
	E.sync ();
}
static void group ()
{
	if (E.sel.size () < 1) return;
	std::vector<XNode *> v = ordered_sel ();
	E.doc.begin (TR ("Group"));
	XNode *g = x_new (X_ELEMENT, "g");
	g->attrs.push_back ({ "id", E.doc.new_id ("g") });
	XNode *p = v.back ()->parent;
	E.doc.insert (p, v.back ()->index () + 1, g);
	for (XNode *n : v) E.doc.move (n, g, -1);
	E.doc.commit ();
	E.select (g);
	E.sync ();
}
static void ungroup ()
{
	std::vector<XNode *> out;
	E.doc.begin (TR ("Ungroup"));
	for (XNode *n : E.sel)
	{
		if (strcmp (el_name (n), "g") || is_layer (n)) { out.push_back (n); continue; }
		Affine t = node_transform (n);
		XNode *p = n->parent;
		int i = n->index ();
		std::vector<XNode *> kids = n->kids;
		for (XNode *k : kids)
		{
			if (!k->element ()) continue;
			if (!t.identity ()) E.doc.set (k, "transform", write_transform (t * node_transform (k)).c_str ());
			E.doc.move (k, p, i++);
			out.push_back (k);
		}
		E.doc.remove (n);
	}
	E.doc.commit ();
	E.sel = out;
	E.sync ();
}
static void flip (bool h)
{
	if (E.sel.empty ()) return;
	Vec2 c = selection_box ().center ();
	E.doc.begin (TR ("Flip"));
	transform_selection (h ? Affine::scale_at (-1, 1, c) : Affine::scale_at (1, -1, c));
	E.doc.commit ();
	E.sync ();
}
static void rotate90 (double sgn)
{
	if (E.sel.empty ()) return;
	Vec2 c = selection_box ().center ();
	E.doc.begin (TR ("Rotate"));
	transform_selection (Affine::rotate_at (sgn * M_PI / 2, c));
	E.doc.commit ();
	E.sync ();
}
static void new_layer ()
{
	char name[64] = "";
	snprintf (name, sizeof name, "%s", TR ("Layer"));
	if (!ui_ask ((long) TR ("New layer"), (long) TR ("Name:"), (long) name, sizeof name)) return;
	E.doc.begin (TR ("New layer"));
	XNode *g = x_new (X_ELEMENT, "g");
	g->attrs.push_back ({ "id", E.doc.new_id ("layer") });
	g->attrs.push_back ({ "inkscape:groupmode", "layer" });
	g->attrs.push_back ({ "inkscape:label", name });
	E.doc.insert (E.doc.root, -1, g);
	if (!E.doc.root->get ("xmlns:inkscape")) E.doc.set (E.doc.root, "xmlns:inkscape", "http://www.inkscape.org/namespaces/inkscape");
	E.doc.commit ();
	E.layer = g;
	E.sync ();
}
static void delete_sel ()
{
	if (E.sel.empty ()) return;
	E.doc.begin (TR ("Delete"));
	for (XNode *n : E.sel) E.doc.remove (n);
	E.doc.commit ();
	E.sel.clear ();
	E.sync ();
}

// ---- clones, clips, text as a path -----------------------------------------------------------------------------------------
static void make_clones ()
{
	if (E.sel.empty ()) return;
	E.doc.begin (TR ("Clone"));
	if (!E.doc.root->get ("xmlns:xlink")) E.doc.set (E.doc.root, "xmlns:xlink", "http://www.w3.org/1999/xlink");
	std::vector<XNode *> made;
	for (XNode *n : ordered_sel ())
	{
		if (!n->get ("id")) E.doc.set (n, "id", E.doc.new_id ("o"));
		XNode *u = x_new (X_ELEMENT, "use");
		u->attrs.push_back ({ "id", E.doc.new_id ("use") });
		u->attrs.push_back ({ "xlink:href", std::string ("#") + n->get ("id") });
		E.doc.insert (n->parent, n->index () + 1, u);
		made.push_back (u);
	}
	E.doc.commit ();
	E.sel = made;
	E.sync ();
}
static void unlink_clones ()
{
	std::vector<XNode *> out;
	E.doc.begin (TR ("Unlink clone"));
	for (XNode *n : E.sel)
	{
		const char *h = n->get ("xlink:href");
		if (!h) h = n->get ("href");
		XNode *t = !strcmp (el_name (n), "use") && h && h[0] == '#' ? E.doc.by_id (h + 1) : nullptr;
		if (!t) { out.push_back (n); continue; }
		XNode *c = t->clone ();
		for (auto &a : c->attrs) if (a.name == "id") a.value = E.doc.new_id ("o");
		Affine m = node_transform (n) * Affine::translate (parse_length (n->get ("x"), 100, 16), parse_length (n->get ("y"), 100, 16)) * node_transform (t);
		bool has = false;
		for (auto &a : c->attrs) if (a.name == "transform") { a.value = write_transform (m); has = true; }
		if (!has) c->attrs.push_back ({ "transform", write_transform (m) });
		E.doc.insert (n->parent, n->index (), c);
		E.doc.remove (n);
		out.push_back (c);
	}
	E.doc.commit ();
	E.sel = out;
	E.sync ();
}
static XNode *defs_node ()
{
	for (XNode *k : E.doc.root->kids) if (k->is ("defs") || k->is ("svg:defs")) return k;
	XNode *d = x_new (X_ELEMENT, "defs");
	d->attrs.push_back ({ "id", E.doc.new_id ("defs") });
	E.doc.insert (E.doc.root, 0, d);
	return d;
}
// the top object of the selection clips or masks the others
static void clip_by_top (bool mask)
{
	std::vector<XNode *> v = ordered_sel ();
	if (v.size () < 2) { E.say (TR ("Select at least two objects.")); return; }
	XNode *top = v.back ();
	v.pop_back ();
	E.doc.begin (mask ? TR ("Mask") : TR ("Clip"));
	XNode *d = defs_node ();
	XNode *holder = x_new (X_ELEMENT, mask ? "mask" : "clipPath");
	std::string id = E.doc.new_id (mask ? "mask" : "clipPath");
	holder->attrs.push_back ({ "id", id });
	if (mask) holder->attrs.push_back ({ "maskUnits", "userSpaceOnUse" });
	E.doc.insert (d, -1, holder);
	Affine m = node_to_page (top);
	XNode *first = v[0];
	Affine inv = node_to_page (first).inverse ();
	E.doc.set (top, "transform", write_transform (inv * m).c_str ());
	E.doc.move (top, holder, -1);
	std::string u = "url(#" + id + ")";
	for (XNode *n : v) set_style (n, mask ? "mask" : "clip-path", u.c_str ());
	E.doc.commit ();
	E.sel = v;
	E.sync ();
}
static void release_clip ()
{
	E.doc.begin (TR ("Release"));
	for (XNode *n : E.sel)
	{
		for (const char *p : { "clip-path", "mask" })
		{
			std::string id = url_id (get_prop (n, p).c_str ());
			if (id.empty ()) continue;
			XNode *h = E.doc.by_id (id);
			set_style (n, p, nullptr);
			if (!h) continue;
			// what clipped comes back, above the object
			std::vector<XNode *> kids = h->kids;
			for (XNode *k : kids)
				if (k->element ())
				{
					E.doc.set (k, "transform", write_transform (node_transform (n) * node_transform (k)).c_str ());
					E.doc.move (k, n->parent, n->index () + 1);
				}
		}
	}
	E.doc.commit ();
	E.sync ();
}
static void text_to_path (XNode *n)
{
	auto f = E.scene->objs.find (n->uid);
	if (f == E.scene->objs.end ()) return;
	vg::Path p = from_skpath (text_outline (f->second.node->pieces));
	XNode *q = x_new (X_ELEMENT, "path");
	for (auto &a : n->attrs)
		if (a.name == "id" || a.name == "style" || a.name == "transform" || a.name == "class") q->attrs.push_back (a);
	q->attrs.push_back ({ "d", vg::write_d (p) });
	XNode *par = n->parent;
	int idx = n->index ();
	E.doc.remove (n);
	E.doc.insert (par, idx, q);
	std::replace (E.sel.begin (), E.sel.end (), n, q);
}

void command (int id)
{
	switch (id)
	{
	case C_NEW:
		if (ask_save ()) { E.doc.create_blank (210, 297); E.doc.path.clear (); after_load (); }
		break;
	case C_OPEN:
	{
		if (!ask_save ()) break;
		char f[256];
		ui_file_filter ((long) "*.svg");
		if (ui_file_open ((long) "SD0:/home/documents", (long) f, sizeof f)) load_file (f);
		break;
	}
	case C_SAVE:
		if (E.doc.path.empty ()) command (C_SAVEAS);
		else save_file (E.doc.path.c_str ());
		break;
	case C_SAVEAS:
	{
		char f[256];
		ui_file_filter ((long) "*.svg");
		if (ui_file_save ((long) "SD0:/home/documents", (long) "drawing.svg", (long) f, sizeof f - 8)) { ensure_dot_svg (f, sizeof f); save_file (f); }
		break;
	}
	case C_IMPORT: import_image (); break;
	case C_EXPORT: export_png (); break;
	case C_EXPORTPDF: export_pdf (); break;
	case C_EXPORTOTHER: export_other (); break;
	case C_EXTENSIONS: extensions_menu (); break;
	case C_DOCPANEL: panel_show (5); break;
	case C_XML: panel_show (4); break;
	case C_FILTERS: panel_show (6); break;
	case C_LPE: panel_show (7); break;
	case C_TRACE:
	{
		char n[16] = "1";
		if (ui_ask ((long) TR ("Trace Image"), (long) TR ("Levels of grey (1 to 8):"), (long) n, sizeof n))
			trace_selection (std::max (1, std::min (8, atoi (n))));
		break;
	}
	case C_CLONE: make_clones (); break;
	case C_UNLINK: unlink_clones (); break;
	case C_CLIPSET: clip_by_top (false); break;
	case C_MASKSET: clip_by_top (true); break;
	case C_CLIPOFF: release_clip (); break;
	case C_GRID: E.grid = !E.grid; E.dirty = true; break;
	case C_GUIDES: E.guides = !E.guides; E.dirty = true; break;
	case C_SNAP: E.snap = !E.snap; E.say (E.snap ? TR ("Snapping on") : TR ("Snapping off")); break;
	case C_RULERS: E.rulers = !E.rulers; chrome_layout (); break;
	case C_GUIDEV: add_guide (true, E.to_page ({ E.cw / 2.0, 0 }).x); break;
	case C_GUIDEH: add_guide (false, E.to_page ({ 0, E.ch / 2.0 }).y); break;
	case C_UNDO: E.tools[E.tool]->leave (); if (E.doc.undo ()) E.sync (); E.tools[E.tool]->enter (); break;
	case C_REDO: E.tools[E.tool]->leave (); if (E.doc.redo ()) E.sync (); E.tools[E.tool]->enter (); break;
	case C_CUT: do_copy (); delete_sel (); break;
	case C_COPY: do_copy (); break;
	case C_PASTE: do_paste (false); break;
	case C_PASTEPLACE: do_paste (true); break;
	case C_DUP:
	{
		if (E.sel.empty ()) break;
		E.doc.begin (TR ("Duplicate"));
		std::vector<XNode *> made;
		for (XNode *n : ordered_sel ())
		{
			XNode *c = n->clone ();
			for (auto &a : c->attrs) if (a.name == "id") a.value = E.doc.new_id ("o");
			E.doc.insert (n->parent, n->index () + 1, c);
			made.push_back (c);
		}
		E.doc.commit ();
		E.sel = made;
		E.sync ();
		break;
	}
	case C_DELETE: delete_sel (); break;
	case C_SELALL:
		E.sel.clear ();
		for (XNode *k : current_layer ()->kids) if (k->element () && !object_box (k).empty ()) E.sel.push_back (k);
		E.dirty = E.chromeDirty = E.panelDirty = true;
		break;
	case C_SELNONE: E.select_none (); break;
	case C_ZOOMIN: zoom_at (1.25, { E.cw / 2.0, E.ch / 2.0 }); break;
	case C_ZOOMOUT: zoom_at (0.8, { E.cw / 2.0, E.ch / 2.0 }); break;
	case C_ZOOM100: zoom_at (1 / E.view.zoom, { E.cw / 2.0, E.ch / 2.0 }); break;
	case C_ZOOM200: zoom_at (2 / E.view.zoom, { E.cw / 2.0, E.ch / 2.0 }); break;
	case C_ZOOMSEL: fit_box (selection_box ()); break;
	case C_ZOOMDRAW: { Box b; if (E.scene->root) { SkRect r = E.scene->root->bounds; b = Box (r.left (), r.top (), r.right (), r.bottom ()); } fit_box (b); break; }
	case C_ZOOMPAGE: fit_page (); break;
	case C_OUTLINE: E.outline = !E.outline; E.say (E.outline ? TR ("Outlines") : TR ("Normal view")); break;
	case C_PANELS: panel_toggle (); break;
	case C_GROUP: group (); break;
	case C_UNGROUP: ungroup (); break;
	case C_RAISE: case C_LOWER: case C_TOP: case C_BOTTOM: reorder (id); break;
	case C_FLIPH: flip (true); break;
	case C_FLIPV: flip (false); break;
	case C_ROTCW: rotate90 (1); break;
	case C_ROTCCW: rotate90 (-1); break;
	case C_NEWLAYER: new_layer (); break;
	case C_LAYERS: panel_show (1); break;
	case C_TOPATH:
		E.doc.begin (TR ("Object to path"));
		{ std::vector<XNode *> v = E.sel; for (XNode *n : v) { if (!strcmp (el_name (n), "text")) text_to_path (n); else convert_to_path (n); } }
		E.doc.commit ();
		E.sync ();
		break;
	case C_STROKEPATH: stroke_to_path (); break;
	case C_UNION: case C_DIFF: case C_INTER: case C_XOR: case C_DIV: boolean_op (id); break;
	case C_SIMPLIFY: simplify_selection (); break;
	case C_REVERSE: reverse_selection (); break;
	case C_COMBINE: combine_selection (); break;
	case C_BREAK: break_selection (); break;
	case C_FILLPANEL: panel_show (0); break;
	case C_TEXTPANEL: panel_show (2); break;
	case C_SEARCH: search_commands (); break;
	case C_ABOUT: ui_message ((long) TR ("About Vector"), (long) TR ("Vector: a vector drawing editor for Korin OS. SVG is its native format. Skia, HarfBuzz, FreeType, libxml2."), UM_OK); break;
	case C_QUIT: break;
	}
}

// ---- Ctrl+K -------------------------------------------------------------------------------------------------------------
static long s_list, s_field;
static std::vector<int> s_rows;
static bool has_text (const char *h, const char *n)
{
	size_t l = strlen (n);
	for (; *h; h++) if (!strncasecmp (h, n, l)) return true;
	return false;
}
static void fill_rows ()
{
	char q[64] = "";
	const char *t = (const char *) ui_w_get_text (s_field);
	if (t) snprintf (q, sizeof q, "%s", t);
	s_rows.clear ();
	ui_list_clear (s_list);
	for (int i = 0; i < (int) (sizeof CMDS / sizeof CMDS[0]); i++)
	{
		const CmdInfo &c = CMDS[i];
		if (c.label[0] == '-') continue;
		const char *l = TR (c.label);
		if (q[0] && !has_text (l, q) && !has_text (c.label, q)) continue;
		char row[120];
		snprintf (row, sizeof row, "%s\t%s", l, c.key);
		ui_list_add (s_list, (long) row, c.id);
	}
	ui_w_set_value (s_list, 0);
	ui_w_draw (s_list);
}
static long search_cb (long dlg, long w, long code)
{
	if (w == s_field && code == UN_CHANGE) fill_rows ();
	else if ((w == s_list || w == s_field) && code == UN_ACTIVATE)
	{
		long data = 0;
		uikit_list_get (s_list, ui_w_get_value (s_list), &data);
		ui_dialog_close (dlg, data);
	}
	return 0;
}
void search_commands ()
{
	long d = ui_dialog_begin (480, 340, (long) TR ("Search Commands"));
	s_field = ui_textbox (12, 36, 456, 28, 0);
	ui_textbox_hint (s_field, (long) TR ("Type a few letters"));
	s_list = ui_list (12, 72, 456, 256, 0);
	ui_list_column (s_list, (long) "", 340, 0);
	ui_list_column (s_list, (long) "", 100, 0);
	ui_dialog_cancel (d, 0);
	fill_rows ();
	ui_w_focus (s_field);
	long r = ui_dialog_run (d, (long) search_cb, 0);
	ui_dialog_end (d);
	if (r > 0) command ((int) r);
}

// ---- keys ---------------------------------------------------------------------------------------------------------------
// the letter of a key: the layout is AZERTY by default (hidmanager), keyb= in options.txt says otherwise
int key_letter (int hid, int mods)
{
	(void) mods;
	static int azerty = -1;
	if (azerty < 0)
	{
		std::string s;
		azerty = 1;
		if (vapp::read_file ("SD0:/etc/options.txt", s))
		{
			size_t k = s.find ("keyb");
			if (k != std::string::npos)
			{
				size_t e = s.find ('=', k);
				if (e != std::string::npos)
				{
					e++;
					while (e < s.size () && s[e] == ' ') e++;
					azerty = (s[e] == 'F' || s[e] == 'f');
				}
			}
		}
	}
	if (hid < 4 || hid > 0x1D) { if (azerty && hid == 0x33) return 'm'; return 0; }
	char c = (char) ('a' + hid - 4);
	if (azerty)
	{
		if (c == 'a') c = 'q'; else if (c == 'q') c = 'a';
		else if (c == 'z') c = 'w'; else if (c == 'w') c = 'z';
		else if (c == 'm') c = 0;
	}
	return c;
}

bool handle_key (int hid, int ch, int mods)
{
	if (E.tools[E.tool]->key (hid, ch, mods)) return true;
	int L = key_letter (hid, mods);
	if (mods & MOD_CTRL)
	{
		bool sh = mods & MOD_SHIFT, al = mods & MOD_ALT;
		int c = C_NONE;
		if (hid == KEY_UP && sh) c = C_RAISE;
		else if (hid == KEY_DOWN && sh) c = C_LOWER;
		else if (al)
			switch (L)
			{
			case 'u': c = C_UNION; break; case 'd': c = C_DIFF; break; case 'i': c = C_INTER; break; case 'e': c = C_XOR; break; case 'b': c = C_DIV; break;
			case 'c': c = C_STROKEPATH; break; case 'k': c = C_COMBINE; break; case 'l': c = C_SIMPLIFY; break; case 'r': c = C_REVERSE; break;
			}
		else
			switch (L)
			{
			case 'n': c = sh ? C_NEWLAYER : C_NEW; break; case 'o': c = C_OPEN; break; case 's': c = sh ? C_SAVEAS : C_SAVE; break;
			case 'i': c = C_IMPORT; break; case 'e': c = sh ? C_DOCPANEL : C_EXPORT; break; case 'z': c = sh ? C_REDO : C_UNDO; break; case 'y': c = C_REDO; break;
			case 'c': c = sh ? C_TOPATH : C_COPY; break; case 'v': c = sh ? C_PASTEPLACE : C_PASTE; break; case 'd': c = C_DUP; break;
			case 'a': c = sh ? C_SELNONE : C_SELALL; break; case 'w': c = C_NONE; break; case 'g': c = sh ? C_UNGROUP : C_GROUP; break; case 'l': c = C_LAYERS; break; case 'k': c = sh ? C_BREAK : C_SEARCH; break; case 'x': c = sh ? C_XML : C_CUT; break;
			case 't': c = C_TEXTPANEL; break; case 'f': c = sh ? C_FILLPANEL : C_NONE; break; case 'p': c = C_NONE; break;
			}
		if (c) { command (c); return true; }
		return false;
	}
	if (mods & MOD_ALT)
	{
		if (hid == KEY_UP && (mods & MOD_SHIFT)) { command (C_TOP); return true; }
		if (hid == KEY_DOWN && (mods & MOD_SHIFT)) { command (C_BOTTOM); return true; }
		if (L == 'c') { command (C_OUTLINE); return true; }
		if (L == 'g') { command (C_GRID); return true; }
		if (L == 'u') { command (C_GUIDES); return true; }
		if (L == 'a') { command (C_SNAP); return true; }
		if (L == 'r') { command (C_RULERS); return true; }
		if (L == 'd') { command (mods & MOD_SHIFT ? C_UNLINK : C_CLONE); return true; }
		return false;
	}
	switch (hid)
	{
	case KEY_BACKSPACE: case KEY_DELETE: command (C_DELETE); return true;
	case KEY_TAB: command (C_PANELS); return true;
	case 0x1E: command (C_ZOOM100); return true;
	case 0x1F: command (C_ZOOM200); return true;
	case 0x20: command (C_ZOOMSEL); return true;
	case 0x21: command (C_ZOOMDRAW); return true;
	case 0x22: command (C_ZOOMPAGE); return true;
	case KEY_ESC: if (E.tool != T_SELECT) E.set_tool (T_SELECT); else E.select_none (); return true;
	case KEY_LEFT: case KEY_RIGHT: case KEY_UP: case KEY_DOWN:
		E.view.ox += hid == KEY_LEFT ? 48 : hid == KEY_RIGHT ? -48 : 0;
		E.view.oy += hid == KEY_UP ? 48 : hid == KEY_DOWN ? -48 : 0;
		E.dirty = true;
		return true;
	}
	if (ch == '=') { command (C_ZOOMIN); return true; }
	if (ch == '-') { command (C_ZOOMOUT); return true; }
	if (mods & MOD_SHIFT)
	{
		int c = ch | 32;
		if (c == 'h') { command (C_FLIPH); return true; }
		if (c == 'v') { command (C_FLIPV); return true; }
		if (c == 'r') { command (C_ROTCW); return true; }
		if (c == 'l') { command (C_ROTCCW); return true; }
		return false;
	}
	static const struct { char c; int t; } K[] = { {'v',T_SELECT},{'a',T_NODE},{'r',T_RECT},{'e',T_ELLIPSE},{'y',T_STAR},{'s',T_SPIRAL},{'p',T_PEN},{'n',T_PENCIL},{'t',T_TEXT},{'g',T_GRADIENT},{'i',T_DROPPER},{'h',T_HAND},{'z',T_ZOOM},{'b',T_CALLI},{'w',T_ERASER},{'f',T_BUCKET},{'u',T_TWEAK},{'j',T_SPRAY},{'l',T_CONNECT},{'q',T_MEASURE},{'k',T_BUILDER} };
	for (auto &k : K) if ((ch | 32) == k.c) { E.set_tool (k.t); return true; }
	if ((ch | 32) == 'c') { panel_show (0); return true; }
	if ((ch | 32) == 'x') { std::swap (E.fill, E.stroke); if (!E.sel.empty ()) { E.doc.begin (TR ("Swap")); for (XNode *n : E.sel) { std::string f = get_prop (n, "fill"), s = get_prop (n, "stroke"); set_style (n, "fill", s.empty () ? "none" : s.c_str ()); set_style (n, "stroke", f.empty () ? "none" : f.c_str ()); } E.doc.commit (); E.sync (); } E.chromeDirty = true; return true; }
	return false;
}

}
