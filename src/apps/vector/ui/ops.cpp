//
// ops.cpp -- operations on paths: booleans (Skia's PathOps), a stroke as a shape, curve fitting (P. J. Schneider, "An
// Algorithm for Automatically Fitting Digitized Curves", Graphics Gems, 1990), simplification, reversal.
//
#include "editor.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathUtils.h"
#include "include/pathops/SkPathOps.h"
#include <cstring>

using namespace vd;
using namespace vr;

namespace ve {

// ---- Skia's paths back to ours ------------------------------------------------------------------------------------------
vg::Path from_skpath (const SkPath &sp)
{
	vg::Path p;
	Vec2 cur;
	SkPathIter it = sp.iter ();
	while (auto r = it.next ())
	{
		const SkPoint *q = r->fPoints.data ();
		auto V = [] (SkPoint a) { return Vec2 (a.fX, a.fY); };
		switch (r->fVerb)
		{
		case SkPathVerb::kMove: p.move_to (V (q[0])); cur = V (q[0]); break;
		case SkPathVerb::kLine: p.line_to (V (q[1])); cur = V (q[1]); break;
		case SkPathVerb::kQuad:
		{
			Vec2 c = V (q[1]), e = V (q[2]);
			p.curve_to (cur + (c - cur) * (2.0 / 3), e + (c - e) * (2.0 / 3), e);
			cur = e;
			break;
		}
		case SkPathVerb::kConic:
		{
			double w = r->fConicWeight, k = 4 * w / (3 * (1 + w));
			Vec2 c = V (q[1]), e = V (q[2]);
			p.curve_to (cur + (c - cur) * k, e + (c - e) * k, e);
			cur = e;
			break;
		}
		case SkPathVerb::kCubic: p.curve_to (V (q[1]), V (q[2]), V (q[3])); cur = V (q[3]); break;
		case SkPathVerb::kClose: p.close (); break;
		}
	}
	return p;
}

// ---- booleans -----------------------------------------------------------------------------------------------------------
// an object as a filled shape in page px, its fill rule kept
static bool object_path (XNode *n, SkPath &out)
{
	vg::Path p = shape_to_path (n);
	if (p.empty ()) return false;
	p.transform (node_to_page (n));
	Style st, base;
	compute_style (n, base, E.builder->sheet, st);
	out = to_skpath (p, st.fillRule == FR_EVENODD);
	return true;
}
static double area_of (const SkPath &p)
{
	// (the control points' polygon is close enough to tell a result from nothing)
	SkRect b = p.getBounds ();
	return (double) b.width () * b.height ();
}

static void replace_with (std::vector<XNode *> gone, const std::vector<vg::Path> &parts, XNode *styleFrom, const char *label)
{
	XNode *par = styleFrom->parent;
	int idx = styleFrom->index () + 1;
	Affine inv = node_to_page (styleFrom).inverse ();
	Affine parentInv = parent_to_page (styleFrom).inverse ();
	(void) inv;
	std::string style = styleFrom->get2 ("style", "");
	E.doc.begin (label);
	std::vector<XNode *> made;
	for (auto &q : parts)
	{
		vg::Path r = q;
		r.transform (parentInv);
		XNode *n = x_new (X_ELEMENT, "path");
		n->attrs.push_back ({ "id", E.doc.new_id ("path") });
		if (!style.empty ()) n->attrs.push_back ({ "style", style });
		n->attrs.push_back ({ "d", vg::write_d (r) });
		E.doc.insert (par, idx++, n);
		made.push_back (n);
	}
	for (XNode *g : gone) E.doc.remove (g);
	E.doc.commit ();
	E.sel = made;
	E.sync ();
}

// the selection in drawing order
static std::vector<XNode *> ordered ()
{
	std::vector<XNode *> v = E.sel;
	std::sort (v.begin (), v.end (), [] (XNode *a, XNode *b) {
		return a->parent == b->parent ? a->index () < b->index () : a->uid < b->uid;
	});
	return v;
}

void boolean_op (int cmd)
{
	std::vector<XNode *> v = ordered ();
	std::vector<XNode *> keep;
	std::vector<SkPath> paths;
	for (XNode *n : v)
	{
		SkPath p;
		if (object_path (n, p)) { paths.push_back (p); keep.push_back (n); }
	}
	if (paths.size () < 2) { E.say (TR ("Select at least two shapes.")); return; }
	if (cmd == C_DIV)
	{
		// the top object cuts everything under it
		const SkPath &knife = paths.back ();
		std::vector<vg::Path> parts;
		for (size_t i = 0; i + 1 < paths.size (); i++)
		{
			auto a = ::Op (paths[i], knife, kIntersect_SkPathOp); auto b = ::Op (paths[i], knife, kDifference_SkPathOp);
			if (!a || !b) { E.say (TR ("The operation failed on these shapes.")); return; }
			if (!a->isEmpty ()) parts.push_back (from_skpath (*a));
			if (!b->isEmpty ()) parts.push_back (from_skpath (*b));
		}
		replace_with (keep, parts, keep[0], TR ("Division"));
		return;
	}
	SkPathOp op = cmd == C_UNION ? kUnion_SkPathOp : cmd == C_DIFF ? kDifference_SkPathOp : cmd == C_INTER ? kIntersect_SkPathOp : kXOR_SkPathOp;
	// the difference takes the top object out of what lies below, as the others go in order
	SkPath r = paths[0];
	for (size_t i = 1; i < paths.size (); i++)
	{
		auto q = ::Op (r, paths[i], op);
		if (!q) { E.say (TR ("The operation failed on these shapes.")); return; }
		r = *q;
	}
	static const char *const L[] = { "Union", "Difference", "Intersection", "Exclusion" };
	const char *lb = L[cmd == C_UNION ? 0 : cmd == C_DIFF ? 1 : cmd == C_INTER ? 2 : 3];
	if (r.isEmpty () && cmd != C_INTER) { E.say (TR ("The result is empty.")); return; }
	(void) area_of;
	replace_with (keep, { from_skpath (r) }, keep[0], TR (lb));
}

void stroke_to_path ()
{
	if (E.sel.empty ()) return;
	E.doc.begin (TR ("Stroke to path"));
	std::vector<XNode *> made;
	for (XNode *n : E.sel)
	{
		Style st, base;
		compute_style (n, base, E.builder->sheet, st);
		vg::Path p = shape_to_path (n);
		if (p.empty () || st.stroke.kind == P_NONE) continue;
		SkPaint sp;
		E.builder->setup_stroke (st, sp);
		SkPath o = skpathutils::FillPathWithPaint (to_skpath (p, false), sp);
		auto s = Simplify (o);
		vg::Path r = from_skpath (s ? *s : o);
		std::string col = get_prop (n, "stroke");
		XNode *q = x_new (X_ELEMENT, "path");
		q->attrs.push_back ({ "id", E.doc.new_id ("path") });
		q->attrs.push_back ({ "style", "fill:" + col + ";stroke:none" });
		if (const char *t = n->get ("transform")) q->attrs.push_back ({ "transform", t });
		q->attrs.push_back ({ "d", vg::write_d (r) });
		E.doc.insert (n->parent, n->index () + 1, q);
		E.doc.set (n, "style", style_with (n->get ("style"), "stroke", "none"));
		if (st.fill.kind == P_NONE) E.doc.remove (n);
		made.push_back (q);
	}
	E.doc.commit ();
	E.sel = made;
	E.sync ();
}

void reverse_selection ()
{
	E.doc.begin (TR ("Reverse"));
	for (XNode *n : E.sel)
	{
		if (strcmp (el_name (n), "path")) continue;
		vg::Path p = shape_to_path (n);
		for (auto &s : p.sub)
		{
			std::reverse (s.n.begin (), s.n.end ());
			for (auto &q : s.n) std::swap (q.in, q.out);
		}
		E.doc.set (n, "d", vg::write_d (p));
		E.doc.set (n, "sodipodi:nodetypes", vg::write_nodetypes (p));
	}
	E.doc.commit ();
	E.sync ();
}

void combine_selection ()
{
	std::vector<XNode *> v = ordered ();
	if (v.size () < 2) return;
	vg::Path all;
	Affine inv = parent_to_page (v[0]).inverse ();
	for (XNode *n : v)
	{
		vg::Path p = shape_to_path (n);
		p.transform (inv * node_to_page (n));
		all.append (p);
	}
	replace_with (v, { [&] { vg::Path q = all; q.transform (parent_to_page (v[0])); return q; } () }, v[0], TR ("Combine"));
}

void break_selection ()
{
	std::vector<XNode *> v = ordered ();
	if (v.size () != 1) return;
	vg::Path p = shape_to_path (v[0]);
	if (p.sub.size () < 2) return;
	std::vector<vg::Path> parts;
	Affine m = node_to_page (v[0]);
	for (auto &s : p.sub)
	{
		vg::Path q;
		q.sub.push_back (s);
		q.transform (m);
		parts.push_back (q);
	}
	replace_with (v, parts, v[0], TR ("Break apart"));
}

// ---- curve fitting ------------------------------------------------------------------------------------------------------
namespace {
struct Fit
{
	const std::vector<Vec2> &d;
	double tol2;
	vg::Path &out;

	static Vec2 B3 (const Vec2 *c, double t) { vg::Bezier b { c[0], c[1], c[2], c[3] }; return b.at (t); }
	void chord (int a, int b, std::vector<double> &u)
	{
		u.assign (b - a + 1, 0);
		for (int i = a + 1; i <= b; i++) u[i - a] = u[i - a - 1] + vg::dist (d[i], d[i - 1]);
		double L = u.back ();
		if (L > 0) for (auto &v : u) v /= L;
	}
	void generate (int a, int b, const std::vector<double> &u, Vec2 t1, Vec2 t2, Vec2 *c)
	{
		double C[2][2] = {}, X[2] = {};
		int n = b - a + 1;
		for (int i = 0; i < n; i++)
		{
			double t = u[i], s = 1 - t;
			double b0 = s * s * s, b1 = 3 * t * s * s, b2 = 3 * t * t * s, b3 = t * t * t;
			Vec2 A0 = t1 * b1, A1 = t2 * b2;
			C[0][0] += vg::dot (A0, A0); C[0][1] += vg::dot (A0, A1); C[1][1] += vg::dot (A1, A1);
			Vec2 tmp = d[a + i] - (d[a] * (b0 + b1) + d[b] * (b2 + b3));
			X[0] += vg::dot (A0, tmp); X[1] += vg::dot (A1, tmp);
		}
		C[1][0] = C[0][1];
		double det = C[0][0] * C[1][1] - C[1][0] * C[0][1];
		double seg = vg::dist (d[a], d[b]), al, ar;
		if (std::fabs (det) > 1e-12)
		{
			al = (X[0] * C[1][1] - X[1] * C[0][1]) / det;
			ar = (C[0][0] * X[1] - C[1][0] * X[0]) / det;
		}
		else al = ar = 0;
		double eps = 1e-6 * seg;
		if (al < eps || ar < eps) al = ar = seg / 3;
		c[0] = d[a]; c[3] = d[b];
		c[1] = d[a] + t1 * al;
		c[2] = d[b] + t2 * ar;
	}
	double max_err (int a, int b, const Vec2 *c, const std::vector<double> &u, int &split)
	{
		double m = 0;
		split = (a + b) / 2;
		for (int i = a + 1; i < b; i++)
		{
			Vec2 p = B3 (c, u[i - a]);
			double e = vg::dot (p - d[i], p - d[i]);
			if (e >= m) { m = e; split = i; }
		}
		return m;
	}
	void reparam (int a, int b, std::vector<double> &u, const Vec2 *c)
	{
		vg::Bezier q { c[0], c[1], c[2], c[3] };
		for (int i = a; i <= b; i++)
		{
			double t = u[i - a];
			Vec2 p = q.at (t) - d[i], d1 = q.deriv (t);
			double u0 = t, u1 = 1 - t;
			Vec2 d2 = (c[0] - c[1] * 2 + c[2]) * (6 * u1) + (c[1] - c[2] * 2 + c[3]) * (6 * u0);
			double num = vg::dot (p, d1), den = vg::dot (d1, d1) + vg::dot (p, d2);
			if (den != 0) { t -= num / den; if (t < 0) t = 0; if (t > 1) t = 1; }
			u[i - a] = t;
		}
	}
	void fit (int a, int b, Vec2 t1, Vec2 t2, int depth)
	{
		if (b - a == 1)
		{
			double k = vg::dist (d[a], d[b]) / 3;
			out.curve_to (d[a] + t1 * k, d[b] + t2 * k, d[b]);
			return;
		}
		std::vector<double> u;
		chord (a, b, u);
		Vec2 c[4];
		generate (a, b, u, t1, t2, c);
		int split;
		double e = max_err (a, b, c, u, split);
		if (e < tol2) { out.curve_to (c[1], c[2], c[3]); return; }
		if (e < tol2 * 16 && depth < 64)
			for (int i = 0; i < 4; i++)
			{
				reparam (a, b, u, c);
				generate (a, b, u, t1, t2, c);
				e = max_err (a, b, c, u, split);
				if (e < tol2) { out.curve_to (c[1], c[2], c[3]); return; }
			}
		if (split <= a) split = a + 1;
		if (split >= b) split = b - 1;
		Vec2 tc = vg::unit (d[split - 1] - d[split + 1]);
		if (tc == Vec2 ()) tc = vg::perp (vg::unit (d[split] - d[split - 1]));
		fit (a, split, t1, tc, depth + 1);
		out.sub.back ().n.back ().type = vg::N_SMOOTH;
		fit (split, b, -tc, t2, depth + 1);
	}
};
}

vg::Path fit_curve (const std::vector<Vec2> &in, double tol)
{
	std::vector<Vec2> pts;
	for (auto &p : in) if (pts.empty () || vg::dist (p, pts.back ()) > 1e-9) pts.push_back (p);
	vg::Path out;
	if (pts.size () < 2) return out;
	out.move_to (pts[0]);
	Fit f { pts, tol * tol, out };
	int n = (int) pts.size ();
	Vec2 t1 = vg::unit (pts[std::min (2, n - 1)] - pts[0]), t2 = vg::unit (pts[std::max (0, n - 3)] - pts[n - 1]);
	f.fit (0, n - 1, t1, t2, 0);
	return out;
}

void align_selection (int how)
{
	if (E.sel.size () < 2) { E.say (TR ("Select at least two objects.")); return; }
	Box all = selection_box ();
	E.doc.begin (TR ("Align"));
	std::vector<XNode *> v = E.sel;
	for (XNode *n : v)
	{
		Box b = object_box (n);
		double dx = 0, dy = 0;
		switch (how)
		{
		case 0: dx = all.x0 - b.x0; break;
		case 1: dx = all.center ().x - b.center ().x; break;
		case 2: dx = all.x1 - b.x1; break;
		case 3: dy = all.y0 - b.y0; break;
		case 4: dy = all.center ().y - b.center ().y; break;
		default: dy = all.y1 - b.y1;
		}
		apply_transform (n, Affine::translate (dx, dy));
	}
	E.doc.commit ();
	E.sync ();
}

void distribute_selection (bool h)
{
	if (E.sel.size () < 3) { E.say (TR ("Select at least three objects.")); return; }
	std::vector<XNode *> v = E.sel;
	std::sort (v.begin (), v.end (), [&] (XNode *a, XNode *b) { return h ? object_box (a).center ().x < object_box (b).center ().x : object_box (a).center ().y < object_box (b).center ().y; });
	double a = h ? object_box (v[0]).center ().x : object_box (v[0]).center ().y;
	double z = h ? object_box (v.back ()).center ().x : object_box (v.back ()).center ().y;
	double step = (z - a) / (v.size () - 1);
	std::vector<double> cur;
	for (XNode *n : v) cur.push_back (h ? object_box (n).center ().x : object_box (n).center ().y);
	E.doc.begin (TR ("Distribute"));
	for (size_t i = 1; i + 1 < v.size (); i++)
	{
		double d = a + step * i - cur[i];
		apply_transform (v[i], h ? Affine::translate (d, 0) : Affine::translate (0, d));
	}
	E.doc.commit ();
	E.sync ();
}

void simplify_selection ()
{
	E.doc.begin (TR ("Simplify"));
	for (XNode *n : E.sel)
	{
		if (strcmp (el_name (n), "path")) continue;
		vg::Path p = shape_to_path (n), r;
		Box b = p.bounds ();
		double tol = std::max (b.w (), b.h ()) / 400;
		for (auto &s : p.sub)
		{
			std::vector<Vec2> pts;
			pts.push_back (s.n[0].p);
			for (int i = 0; i < s.segments (); i++) vg::flatten (vg::seg_of (s, i), tol / 4, pts);
			vg::Path q = fit_curve (pts, tol);
			if (s.closed) q.close ();
			r.append (q);
		}
		E.doc.set (n, "d", vg::write_d (r));
		E.doc.set (n, "sodipodi:nodetypes", vg::write_nodetypes (r));
	}
	E.doc.commit ();
	E.sync ();
}

}
