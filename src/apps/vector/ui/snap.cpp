//
// snap.cpp -- grid, guides, and what a gesture snaps to (docs/UX-VECTOR.md 5.4). The candidates are gathered once when a
// gesture starts: the boxes of the objects near the view, the guides, the grid. Everything in page px; the tolerance is on
// the screen, whatever the zoom. Guides are sodipodi:guide in the named view, grids inkscape:grid: the file stays readable
// elsewhere. Priority: objects' corners, then guides, then the grid, then centres.
//
#include "editor.h"
#include <cstdio>
#include <cstring>

using namespace vd;
using namespace vr;

namespace ve {

SkColor sk (unsigned rgb, float a = 1);

static std::vector<double> s_xs, s_ys, s_xc, s_yc;	// edges, centres
static std::vector<double> s_gx, s_gy;			// guides
static const double TOL = 8;

XNode *namedview (bool create)
{
	for (XNode *k : E.doc.root->kids) if (k->is ("sodipodi:namedview")) return k;
	if (!create) return nullptr;
	if (!E.doc.root->get ("xmlns:sodipodi")) E.doc.set (E.doc.root, "xmlns:sodipodi", "http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd");
	if (!E.doc.root->get ("xmlns:inkscape")) E.doc.set (E.doc.root, "xmlns:inkscape", "http://www.inkscape.org/namespaces/inkscape");
	XNode *n = x_new (X_ELEMENT, "sodipodi:namedview");
	n->attrs.push_back ({ "id", E.doc.new_id ("namedview") });
	E.doc.insert (E.doc.root, 0, n);
	return n;
}

std::vector<Guide> guides_of ()
{
	std::vector<Guide> o;
	XNode *nv = namedview (false);
	if (!nv) return o;
	for (XNode *k : nv->kids)
	{
		if (!k->is ("sodipodi:guide")) continue;
		auto p = parse_numbers (k->get ("position")), d = parse_numbers (k->get ("orientation"));
		if (p.size () < 2 || d.size () < 2) continue;
		Vec2 q = E.scene->docToPx.apply ({ p[0], p[1] });
		// (a normal along x is a vertical guide; diagonal guides are kept in the file, not used here)
		if (d[1] == 0) o.push_back ({ k, true, q.x });
		else if (d[0] == 0) o.push_back ({ k, false, q.y });
	}
	return o;
}
void add_guide (bool vertical, double at)
{
	E.doc.begin (TR ("Guide"));
	XNode *nv = namedview (true);
	Vec2 p = E.scene->docToPx.inverse ().apply (vertical ? Vec2 (at, 0) : Vec2 (0, at));
	XNode *g = x_new (X_ELEMENT, "sodipodi:guide");
	g->attrs.push_back ({ "id", E.doc.new_id ("guide") });
	g->attrs.push_back ({ "position", fmt (p.x) + "," + fmt (p.y) });
	g->attrs.push_back ({ "orientation", vertical ? "1,0" : "0,1" });
	E.doc.insert (nv, -1, g);
	E.doc.commit ();
	E.sync ();
	E.dirty = true;
}

void snap_begin (bool ignoreSelection)
{
	s_xs.clear (); s_ys.clear (); s_xc.clear (); s_yc.clear (); s_gx.clear (); s_gy.clear ();
	if (!E.snap) return;
	XNode *l = current_layer ();
	for (XNode *k : l->kids)
	{
		if (!k->element () || (ignoreSelection && E.selected (k))) continue;
		Box b = object_box (k);
		if (b.empty ()) continue;
		s_xs.push_back (b.x0); s_xs.push_back (b.x1); s_ys.push_back (b.y0); s_ys.push_back (b.y1);
		s_xc.push_back (b.center ().x); s_yc.push_back (b.center ().y);
	}
	// the page's edges and middle
	s_xs.push_back (0); s_xs.push_back (E.scene->width); s_ys.push_back (0); s_ys.push_back (E.scene->height);
	s_xc.push_back (E.scene->width / 2); s_yc.push_back (E.scene->height / 2);
	if (E.guides) for (auto &g : guides_of ()) (g.vertical ? s_gx : s_gy).push_back (g.at);
}
void snap_end () { if (E.snapShown) { E.snapShown = false; E.dirty = true; } }

// the nearest candidate to v within t; -> its distance
static double nearest (const std::vector<double> &c, double v, double t, double &best)
{
	double d = t;
	for (double q : c) { double e = std::fabs (q - v); if (e < d) { d = e; best = q; } }
	return d;
}
static bool one_axis (double v, double t, const std::vector<double> &edges, const std::vector<double> &guides, const std::vector<double> &centres, double &out, const char **what)
{
	double b;
	if (nearest (edges, v, t, b) < t) { out = b; *what = TR ("edge"); return true; }
	if (nearest (guides, v, t, b) < t) { out = b; *what = TR ("guide"); return true; }
	if (E.grid) { double g = std::round (v / E.gridStep) * E.gridStep; if (std::fabs (g - v) < t) { out = g; *what = TR ("grid"); return true; } }
	if (nearest (centres, v, t, b) < t) { out = b; *what = TR ("centre"); return true; }
	return false;
}

Vec2 snap_point (Vec2 p, int mods)
{
	E.snapShown = false;
	if (!E.snap || (mods & MOD_SHIFT)) return p;
	double t = TOL / E.view.zoom, x, y;
	const char *wx = "", *wy = "";
	bool hx = one_axis (p.x, t, s_xs, s_gx, s_xc, x, &wx), hy = one_axis (p.y, t, s_ys, s_gy, s_yc, y, &wy);
	if (hx) p.x = x;
	if (hy) p.y = y;
	if (hx || hy) { E.snapShown = true; E.snapAt = p; E.snapWhat = hx ? wx : wy; E.dirty = true; }
	return p;
}

Vec2 snap_box (const Box &b, int mods)
{
	E.snapShown = false;
	if (!E.snap || (mods & MOD_SHIFT)) return {};
	double t = TOL / E.view.zoom, v;
	const char *w = "";
	Vec2 d;
	bool hit = false;
	double bestx = t, besty = t;
	for (double x : { b.x0, b.x1, b.center ().x })
		if (one_axis (x, bestx, s_xs, s_gx, s_xc, v, &w)) { d.x = v - x; bestx = std::fabs (d.x); E.snapAt.x = v; E.snapWhat = w; hit = true; }
	for (double y : { b.y0, b.y1, b.center ().y })
		if (one_axis (y, besty, s_ys, s_gy, s_yc, v, &w)) { d.y = v - y; besty = std::fabs (d.y); E.snapAt.y = v; E.snapWhat = w; hit = true; }
	if (hit)
	{
		if (bestx >= t) E.snapAt.x = b.center ().x;
		if (besty >= t) E.snapAt.y = b.center ().y;
		E.snapShown = true;
		E.dirty = true;
	}
	return d;
}

void paint_grid (SkCanvas *c)
{
	if (E.grid)
	{
		double step = E.gridStep * E.view.zoom;
		int major = 5;
		while (step < 8) { step *= 5; }
		Vec2 o = E.to_screen ({ 0, 0 });
		SkPaint th, ma;
		th.setColor (sk (jet (J_LIGNE), 0.8f));
		ma.setColor (sk (jet (J_LIGNE_FORTE), 0.8f));
		int i0 = (int) std::floor (-o.x / step), i1 = (int) std::ceil ((E.cw - o.x) / step);
		for (int i = i0; i <= i1; i++) { float x = (float) std::floor (o.x + i * step) + .5f; c->drawLine (x, 0, x, (float) E.ch, i % major ? th : ma); }
		i0 = (int) std::floor (-o.y / step); i1 = (int) std::ceil ((E.ch - o.y) / step);
		for (int i = i0; i <= i1; i++) { float y = (float) std::floor (o.y + i * step) + .5f; c->drawLine (0, y, (float) E.cw, y, i % major ? th : ma); }
	}
	if (E.guides)
	{
		SkPaint g;
		g.setColor (sk (jet (J_ALERTE)));
		for (auto &q : guides_of ())
		{
			if (q.vertical) { float x = (float) std::floor (E.to_screen ({ q.at, 0 }).x) + .5f; c->drawLine (x, 0, x, (float) E.ch, g); }
			else { float y = (float) std::floor (E.to_screen ({ 0, q.at }).y) + .5f; c->drawLine (0, y, (float) E.cw, y, g); }
		}
	}
}

}
