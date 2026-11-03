//
// core.cpp -- what every tool needs: where an object is, what lies under the pointer, how to change a transform or a
// style without touching anything else in the file.
//
#include "editor.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathUtils.h"
#include <cstdio>
#include <cstring>

using namespace vd;
using namespace vr;

namespace ve {

Editor E;

std::string fmt (double v)
{
	char b[40];
	snprintf (b, sizeof b, "%.6g", v);
	return b;
}

// ---- the editor -------------------------------------------------------------------------------------------------------
void Editor::sync ()
{
	if (doc.touched.empty () && !doc.structure && scene) return;
	scene = builder->update ();
	tiles->set_scene (scene, builder->lastAll ? nullptr : &builder->lastDamage);
	// (what the journal took away cannot stay selected)
	for (size_t i = 0; i < sel.size (); )
	{
		XNode *n = sel[i];
		while (n && n != doc.top) n = n->parent;
		if (!n) sel.erase (sel.begin () + i); else i++;
	}
	dirty = chromeDirty = panelDirty = true;
}
void Editor::select (XNode *n, bool add)
{
	if (!add) sel.clear ();
	if (n)
	{
		auto f = std::find (sel.begin (), sel.end (), n);
		if (f == sel.end ()) sel.push_back (n);
		else if (add) sel.erase (f);
	}
	dirty = chromeDirty = panelDirty = true;
}
void Editor::select_none () { sel.clear (); dirty = chromeDirty = panelDirty = true; }
bool Editor::selected (XNode *n) const { return std::find (sel.begin (), sel.end (), n) != sel.end (); }
void Editor::set_tool (int t)
{
	if (t == tool) return;
	tools[tool]->leave ();
	tool = t;
	tools[tool]->enter ();
	ui_cursor (tools[tool]->cursor ());
	dirty = chromeDirty = panelDirty = true;
}
void Editor::say (const char *m) { message = m; msgUntil = 300; chromeDirty = true; }

// ---- geometry of objects ---------------------------------------------------------------------------------------------
Affine node_transform (const XNode *n) { return parse_transform (n->get ("transform")); }

Affine parent_to_page (const XNode *n)
{
	std::vector<const XNode *> chain;
	for (const XNode *p = n->parent; p && p != E.doc.root && p != E.doc.top; p = p->parent) chain.push_back (p);
	Affine m = E.scene->docToPx;
	for (int i = (int) chain.size () - 1; i >= 0; i--) m = m * node_transform (chain[i]);
	return m;
}
Affine node_to_page (const XNode *n) { return parent_to_page (n) * node_transform (n); }

Box object_box (const XNode *n)
{
	auto f = E.scene->objs.find (n->uid);
	return f == E.scene->objs.end () ? Box () : f->second.visual;
}
Box selection_box ()
{
	Box b;
	for (XNode *n : E.sel) b.add (object_box (n));
	return b;
}

bool is_layer (const XNode *n)
{
	const char *g = n->get ("inkscape:groupmode");
	return g && !strcmp (g, "layer");
}
XNode *current_layer ()
{
	if (E.layer)
	{
		XNode *n = E.layer;
		while (n && n != E.doc.root) n = n->parent;
		if (n) return E.layer;
	}
	XNode *last = nullptr;
	for (XNode *k : E.doc.root->kids) if (k->element () && is_layer (k)) last = k;
	E.layer = last ? last : E.doc.root;
	return E.layer;
}

// ---- hit testing -------------------------------------------------------------------------------------------------------
static bool hit_shape (const DNode &n, SkPoint p, float tol)
{
	if (E.outline || !n.hasFill || n.path.isEmpty ())
	{
	}
	else if (n.path.contains (p.fX, p.fY)) return true;
	SkPaint s;
	s.setStyle (SkPaint::kStroke_Style);
	float w = n.hasStroke ? n.stroke.getStrokeWidth () : 0;
	s.setStrokeWidth (w + tol * 2);
	s.setStrokeCap (SkPaint::kRound_Cap);
	s.setStrokeJoin (SkPaint::kRound_Join);
	SkRect b = n.path.getBounds ();
	b.outset (w + tol, w + tol);
	if (!b.contains (p.fX, p.fY)) return false;
	if (!n.hasStroke && n.hasFill && !E.outline) return false;
	SkPath o = skpathutils::FillPathWithPaint (n.path, s);
	return o.contains (p.fX, p.fY);
}

// the topmost leaf under the point; -> its uid
static uint32_t hit_node (const DNode &n, SkPoint p, float tol)
{
	SkMatrix inv;
	if (!n.m.invert (&inv)) return 0;
	SkPoint q = inv.mapPoint (p);
	float k = (float) std::max (0.0001f, n.m.getMaxScale ());
	float t = tol / k;
	if (n.hasClipRect && !n.clipRect.contains (q.fX, q.fY)) return 0;
	if (n.hasClip && !n.clip.contains (q.fX, q.fY)) return 0;
	switch (n.kind)
	{
	case D_SHAPE:
		return hit_shape (n, q, t) ? n.uid : 0;
	case D_IMAGE:
		return n.dst.contains (q.fX, q.fY) ? n.uid : 0;
	case D_TEXT:
	{
		SkRect b = n.bounds;
		SkMatrix i2;
		if (n.m.invert (&i2)) b = i2.mapRect (b);
		return b.contains (q.fX, q.fY) ? n.uid : 0;
	}
	default:
		for (int i = (int) n.kids.size () - 1; i >= 0; i--)
			if (uint32_t u = hit_node (*n.kids[i], q, t)) return n.uid && !n.kids[i]->uid ? n.uid : u;
		return 0;
	}
}
static XNode *find_uid (XNode *n, uint32_t u)
{
	if (n->uid == u) return n;
	for (XNode *k : n->kids)
		if (k->element ())
			if (XNode *f = find_uid (k, u)) return f;
	return nullptr;
}
XNode *hit_leaf (Vec2 page, double tol)
{
	if (!E.scene->root) return nullptr;
	uint32_t u = hit_node (*E.scene->root, { (float) page.x, (float) page.y }, (float) tol);
	return u ? find_uid (E.doc.root, u) : nullptr;
}
static bool locked (const XNode *n)
{
	for (; n && n != E.doc.root; n = n->parent)
	{
		const char *l = n->get ("sodipodi:insensitive");
		if (l && !strcmp (l, "true")) return true;
	}
	return false;
}
// a leaf is reached through its object: the child of a layer or of the root
XNode *hit_object (Vec2 page, double tol, XNode *, bool enterGroups)
{
	XNode *l = hit_leaf (page, tol);
	if (!l || l == E.doc.root || locked (l)) return nullptr;
	if (enterGroups) return l;
	XNode *n = l;
	while (n->parent && n->parent != E.doc.root && !is_layer (n->parent)) n = n->parent;
	return is_layer (n) ? nullptr : n;
}

// ---- changing the tree -------------------------------------------------------------------------------------------------
void apply_transform (XNode *n, const Affine &pageM)
{
	Affine P = parent_to_page (n);
	if (!P.invertible ()) return;
	Affine d = P.inverse () * pageM * P;
	E.doc.set (n, "transform", write_transform (d * node_transform (n)).c_str ());
}
void transform_selection (const Affine &pageM)
{
	for (XNode *n : E.sel) apply_transform (n, pageM);
}

void set_style (XNode *n, const char *prop, const char *value)
{
	E.doc.set (n, "style", style_with (n->get ("style"), prop, value));
	// (a presentation attribute would lose to the style attribute anyway; it stays as the author wrote it)
}
void style_selection (const char *prop, const char *value, const char *label, int merge)
{
	if (E.sel.empty ()) return;
	E.doc.begin (label, merge);
	for (XNode *n : E.sel) set_style (n, prop, value);
	E.doc.commit ();
	E.sync ();
}

XNode *add_shape (const char *name, const std::vector<std::pair<const char *, std::string>> &attrs)
{
	XNode *n = x_new (X_ELEMENT, name);
	n->attrs.push_back ({ "id", E.doc.new_id (name) });
	for (auto &a : attrs) n->attrs.push_back ({ a.first, a.second });
	XNode *l = current_layer ();
	Affine m = parent_to_page (n) ;
	(void) m;
	E.doc.insert (l, -1, n);
	return n;
}

// ---- to a path --------------------------------------------------------------------------------------------------------
vg::Path shape_to_path (XNode *n)
{
	const char *name = el_name (n);
	// (the path alone: nothing here may need the editor, the tools without a window use it too)
	auto num = [&] (const char *a) { return parse_length (n->get (a), 100, 16); };
	vg::Path p;
	if (!strcmp (name, "path")) { p = vg::parse_d (n->get ("d")); vg::apply_nodetypes (p, n->get ("sodipodi:nodetypes")); }
	else if (!strcmp (name, "rect"))
	{
		bool ok1, ok2;
		double rx = parse_length (n->get ("rx"), 100, 16, &ok1), ry = parse_length (n->get ("ry"), 100, 16, &ok2);
		p = vg::path_rect (num ("x"), num ("y"), num ("width"), num ("height"), ok1 ? rx : -1, ok2 ? ry : -1);
	}
	else if (!strcmp (name, "circle")) p = vg::path_ellipse (num ("cx"), num ("cy"), num ("r"), num ("r"));
	else if (!strcmp (name, "ellipse")) p = vg::path_ellipse (num ("cx"), num ("cy"), num ("rx"), num ("ry"));
	else if (!strcmp (name, "line")) { p.move_to ({ num ("x1"), num ("y1") }); p.line_to ({ num ("x2"), num ("y2") }); }
	else if (!strcmp (name, "polyline") || !strcmp (name, "polygon"))
	{
		auto v = parse_numbers (n->get ("points"));
		for (size_t i = 0; i + 1 < v.size (); i += 2) { if (i == 0) p.move_to ({ v[0], v[1] }); else p.line_to ({ v[i], v[i + 1] }); }
		if (name[4] == 'g') p.close ();
	}
	return p;
}
std::string path_of (XNode *n) { return vg::write_d (shape_to_path (n)); }

XNode *convert_to_path (XNode *n)
{
	const char *name = el_name (n);
	if (!strcmp (name, "path")) return n;
	if (strcmp (name, "rect") && strcmp (name, "circle") && strcmp (name, "ellipse") && strcmp (name, "line") && strcmp (name, "polyline") && strcmp (name, "polygon")) return n;
	vg::Path p = shape_to_path (n);
	XNode *q = x_new (X_ELEMENT, n->name.compare (0, 4, "svg:") ? "path" : "svg:path");
	static const char *const DROP[] = { "x", "y", "width", "height", "rx", "ry", "cx", "cy", "r", "x1", "y1", "x2", "y2", "points" };
	for (auto &a : n->attrs)
	{
		bool drop = false;
		for (auto d : DROP) if (a.name == d) drop = true;
		if (!drop) q->attrs.push_back (a);
	}
	q->attrs.push_back ({ "d", vg::write_d (p) });
	q->attrs.push_back ({ "sodipodi:nodetypes", vg::write_nodetypes (p) });
	XNode *par = n->parent;
	int idx = n->index ();
	bool sel = E.selected (n);
	E.doc.remove (n);
	E.doc.insert (par, idx, q);
	if (sel) std::replace (E.sel.begin (), E.sel.end (), n, q);
	return q;
}

}
