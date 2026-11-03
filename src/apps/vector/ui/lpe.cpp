//
// lpe.cpp -- path effects: the path keeps what was drawn in inkscape:original-d, a stack of effects in
// inkscape:path-effect (ids of inkscape:path-effect elements in defs), and "d" always holds the result, so that any SVG
// reader shows the right drawing. Parameters are attributes of the effect element; they are ours, not exchanged with other
// programs. The geometry is written from the usual constructions: fillets by trimming tangents, offsets by a stroke.
//
#include "lpe.h"
#include "editor.h"
#include "include/core/SkPathUtils.h"
#include "include/pathops/SkPathOps.h"
#include <cstdio>
#include <cstring>

using namespace vd;
using namespace vr;

namespace ve {

static const LpeParam P_CORNERS[] = { { "radius", "Radius", 8, 0, 500, 1 } };
static const LpeParam P_OFFSET[] = { { "offset", "Distance", 5, -200, 200, 1 } };
static const LpeParam P_MIRROR[] = { { "axis", "Axis", 0, -2000, 2000, 1 }, { "vertical", "Top to bottom", 0, 0, 1, 1 } };
static const LpeParam P_ROTATE[] = { { "copies", "Copies", 6, 2, 64, 1 }, { "angle", "Total angle", 360, 1, 360, 15 } };
static const LpeParam P_ROUGHEN[] = { { "amount", "Amount", 3, 0, 100, 1 }, { "segment", "Segment length", 12, 2, 200, 1 }, { "seed", "Seed", 1, 0, 9999, 1 } };
static const LpeParam P_SIMPLIFY[] = { { "tolerance", "Tolerance", 2, 0.1, 50, 0.5 } };
static const LpeParam P_TAPER[] = { { "width", "Width", 10, 0, 200, 1 }, { "start", "Start, %", 0, 0, 100, 5 }, { "end", "End, %", 0, 0, 100, 5 } };
static const LpeParam P_WAVE[] = { { "amplitude", "Height", 10, -200, 200, 1 }, { "waves", "Waves", 3, 0.5, 50, 0.5 } };
static const LpeParam P_DASHLINE[] = { { "length", "Length", 12, 1, 500, 1 }, { "gap", "Gap", 6, 1, 500, 1 } };

const LpeType LPE_TYPES[] = {
	{ "corners", "Corners", P_CORNERS, 1 }, { "offset", "Offset", P_OFFSET, 1 }, { "mirror_symmetry", "Mirror", P_MIRROR, 2 },
	{ "copy_rotate", "Rotate copies", P_ROTATE, 2 }, { "roughen", "Roughen", P_ROUGHEN, 3 }, { "simplify", "Simplify", P_SIMPLIFY, 1 },
	{ "taper_stroke", "Taper stroke", P_TAPER, 3 }, { "bend_wave", "Wave", P_WAVE, 2 }, { "dash_stroke", "Dashes", P_DASHLINE, 2 },
};
const int LPE_COUNT = sizeof LPE_TYPES / sizeof LPE_TYPES[0];

const LpeType *lpe_type (const char *id)
{
	for (auto &t : LPE_TYPES) if (id && !strcmp (t.id, id)) return &t;
	return nullptr;
}
double lpe_param (const XNode *e, const LpeParam &p)
{
	const char *v = e->get (p.name);
	return v ? atof (v) : p.def;
}

static void polyline (const vg::SubPath &s, double tol, std::vector<Vec2> &out)
{
	out.push_back (s.n[0].p);
	for (int i = 0; i < s.segments (); i++) vg::flatten (vg::seg_of (s, i), tol, out);
}

// ---- the effects ----------------------------------------------------------------------------------------------------------
static vg::Path corners (const vg::Path &in, double r)
{
	vg::Path o;
	const double K = 0.5522847498;
	for (auto &s : in.sub)
	{
		int cnt = (int) s.n.size ();
		vg::SubPath q;
		q.closed = s.closed;
		for (int i = 0; i < cnt; i++)
		{
			const vg::Node &n = s.n[i];
			bool end = !s.closed && (i == 0 || i == cnt - 1);
			const vg::Node &pv = s.n[(i + cnt - 1) % cnt], &nx = s.n[(i + 1) % cnt];
			bool straight = n.in == n.p && n.out == n.p && pv.out == pv.p && nx.in == nx.p;
			if (end || !straight || r <= 0 || cnt < 3) { q.n.push_back (n); continue; }
			Vec2 a = vg::unit (pv.p - n.p), b = vg::unit (nx.p - n.p);
			double ang = std::acos (std::max (-1.0, std::min (1.0, vg::dot (a, b)))), t = r / std::tan (ang / 2);
			double room = std::min (vg::dist (pv.p, n.p), vg::dist (nx.p, n.p)) / 2;
			if (!(t > 0) || std::fabs (ang - M_PI) < 1e-6) { q.n.push_back (n); continue; }
			if (t > room) t = room;
			vg::Node x (n.p + a * t), y (n.p + b * t);
			x.out = x.p + (n.p - x.p) * K;
			y.in = y.p + (n.p - y.p) * K;
			x.type = y.type = vg::N_SMOOTH;
			q.n.push_back (x);
			q.n.push_back (y);
		}
		o.sub.push_back (q);
	}
	return o;
}

static vg::Path offset_path (const vg::Path &in, double d)
{
	if (d == 0) return in;
	SkPath src = to_skpath (in, false);
	SkPaint sp;
	sp.setStyle (SkPaint::kStroke_Style);
	sp.setStrokeWidth ((float) (std::fabs (d) * 2));
	sp.setStrokeJoin (SkPaint::kRound_Join);
	sp.setStrokeCap (SkPaint::kRound_Cap);
	SkPath ring = skpathutils::FillPathWithPaint (src, sp);
	auto r = ::Op (src, ring, d > 0 ? kUnion_SkPathOp : kDifference_SkPathOp);
	return r ? from_skpath (*r) : in;
}

static vg::Path mirror (const vg::Path &in, double axis, bool vert)
{
	Box b = in.bounds ();
	Vec2 c = b.center ();
	Affine m = vert ? Affine::scale_at (1, -1, { c.x, b.y1 + axis }) : Affine::scale_at (-1, 1, { b.x1 + axis, c.y });
	vg::Path o = in, q = in;
	q.transform (m);
	o.append (q);
	return o;
}

static vg::Path rotate_copies (const vg::Path &in, int n, double total)
{
	Box b = in.bounds ();
	Vec2 c (b.x1 + b.w () / 2, b.center ().y);
	vg::Path o;
	double step = total / (total >= 360 ? n : std::max (1, n - 1)) * M_PI / 180;
	for (int i = 0; i < n; i++)
	{
		vg::Path q = in;
		q.transform (Affine::rotate_at (step * i, c));
		o.append (q);
	}
	return o;
}

static vg::Path roughen (const vg::Path &in, double amount, double seg, unsigned seed)
{
	unsigned st = seed * 2654435761u + 1;
	auto rnd = [&] { st = st * 1103515245 + 12345; return ((st >> 8) & 0xFFFF) / 32768.0 - 1; };
	vg::Path o;
	for (auto &s : in.sub)
	{
		std::vector<Vec2> pts;
		polyline (s, 0.1, pts);
		vg::SubPath q;
		q.closed = s.closed;
		double run = 0;
		for (size_t i = 0; i < pts.size (); i++)
		{
			if (i) run += vg::dist (pts[i], pts[i - 1]);
			if (i == 0 || i + 1 == pts.size () || run >= seg)
			{
				run = 0;
				q.n.emplace_back (pts[i] + Vec2 (rnd (), rnd ()) * amount);
			}
		}
		o.sub.push_back (q);
	}
	return o;
}

static vg::Path simplify (const vg::Path &in, double tol)
{
	vg::Path r;
	for (auto &s : in.sub)
	{
		std::vector<Vec2> pts;
		polyline (s, tol / 4, pts);
		vg::Path q = fit_curve (pts, tol);
		if (s.closed) q.close ();
		r.append (q);
	}
	return r;
}

// a shape around the line: width grows from the start's share to full at a third, and down to the end's share
static vg::Path taper (const vg::Path &in, double w, double a, double z)
{
	vg::Path o;
	for (auto &s : in.sub)
	{
		std::vector<Vec2> pts;
		polyline (s, 0.1, pts);
		if (pts.size () < 2) continue;
		std::vector<double> L (pts.size (), 0);
		for (size_t i = 1; i < pts.size (); i++) L[i] = L[i - 1] + vg::dist (pts[i], pts[i - 1]);
		double total = L.back ();
		if (total <= 0) continue;
		std::vector<Vec2> left, right;
		for (size_t i = 0; i < pts.size (); i++)
		{
			double t = L[i] / total, k = t < 1.0 / 3 ? a + (1 - a) * (t * 3) : t > 2.0 / 3 ? z + (1 - z) * ((1 - t) * 3) : 1;
			Vec2 d = vg::unit (pts[std::min (i + 1, pts.size () - 1)] - pts[i ? i - 1 : 0]), nrm = vg::perp (d) * (w * k / 2);
			left.push_back (pts[i] + nrm);
			right.push_back (pts[i] - nrm);
		}
		std::reverse (right.begin (), right.end ());
		left.insert (left.end (), right.begin (), right.end ());
		vg::Path q = fit_curve (left, 0.25);
		q.close ();
		o.append (q);
	}
	return o;
}

static vg::Path wave (const vg::Path &in, double h, double waves)
{
	vg::Path o;
	for (auto &s : in.sub)
	{
		std::vector<Vec2> pts;
		polyline (s, 0.1, pts);
		if (pts.size () < 2) continue;
		double total = 0;
		for (size_t i = 1; i < pts.size (); i++) total += vg::dist (pts[i], pts[i - 1]);
		std::vector<Vec2> q;
		double run = 0;
		for (size_t i = 0; i < pts.size (); i++)
		{
			if (i) run += vg::dist (pts[i], pts[i - 1]);
			Vec2 d = vg::unit (pts[std::min (i + 1, pts.size () - 1)] - pts[i ? i - 1 : 0]);
			q.push_back (pts[i] + vg::perp (d) * (h * std::sin (2 * M_PI * waves * run / std::max (total, 1e-9))));
		}
		vg::Path f = fit_curve (q, 0.15);
		if (s.closed) f.close ();
		o.append (f);
	}
	return o;
}

static vg::Path dashes (const vg::Path &in, double len, double gap)
{
	vg::Path o;
	for (auto &s : in.sub)
	{
		std::vector<Vec2> pts;
		polyline (s, 0.1, pts);
		double pos = 0;
		bool on = true;
		std::vector<Vec2> cur { pts[0] };
		for (size_t i = 1; i < pts.size (); i++)
		{
			Vec2 a = pts[i - 1], b = pts[i];
			double seg = vg::dist (a, b), done = 0;
			while (seg - done > 1e-9)
			{
				double left = (on ? len : gap) - pos, take = std::min (left, seg - done);
				done += take; pos += take;
				Vec2 p = lerp (a, b, done / seg);
				if (on) cur.push_back (p);
				if (pos >= (on ? len : gap) - 1e-9)
				{
					if (on && cur.size () > 1) { vg::Path f = fit_curve (cur, 0.1); o.append (f); }
					cur.clear ();
					on = !on; pos = 0;
					if (on) cur.push_back (p);
				}
			}
		}
		if (on && cur.size () > 1) o.append (fit_curve (cur, 0.1));
	}
	return o;
}

vg::Path lpe_apply (const XNode *e, const vg::Path &in)
{
	const char *t = e->get ("effect");
	const LpeType *ty = lpe_type (t);
	if (!ty) return in;
	auto P = [&] (int i) { return lpe_param (e, ty->params[i]); };
	if (!strcmp (t, "corners")) return corners (in, P (0));
	if (!strcmp (t, "offset")) return offset_path (in, P (0));
	if (!strcmp (t, "mirror_symmetry")) return mirror (in, P (0), P (1) != 0);
	if (!strcmp (t, "copy_rotate")) return rotate_copies (in, (int) P (0), P (1));
	if (!strcmp (t, "roughen")) return roughen (in, P (0), P (1), (unsigned) P (2));
	if (!strcmp (t, "simplify")) return simplify (in, P (0));
	if (!strcmp (t, "taper_stroke")) return taper (in, P (0), P (1) / 100, P (2) / 100);
	if (!strcmp (t, "bend_wave")) return wave (in, P (0), P (1));
	if (!strcmp (t, "dash_stroke")) return dashes (in, P (0), P (1));
	return in;
}

// ---- the stack on a path --------------------------------------------------------------------------------------------------
std::vector<XNode *> lpe_stack (const XNode *path)
{
	std::vector<XNode *> o;
	const char *s = path->get ("inkscape:path-effect");
	if (!s) return o;
	while (*s)
	{
		while (*s == ';' || *s == ' ') s++;
		if (*s != '#') break;
		s++;
		std::string id;
		while (*s && *s != ';') id += *s++;
		if (XNode *e = E.doc.by_id (id)) o.push_back (e);
	}
	return o;
}
void lpe_refresh (XNode *path)
{
	const char *orig = path->get ("inkscape:original-d");
	if (!orig) return;
	vg::Path p = vg::parse_d (orig);
	for (XNode *e : lpe_stack (path)) p = lpe_apply (e, p);
	E.doc.set (path, "d", vg::write_d (p));
	E.doc.set (path, "sodipodi:nodetypes", nullptr);
}
static XNode *defs ()
{
	for (XNode *k : E.doc.root->kids) if (k->is ("defs") || k->is ("svg:defs")) return k;
	XNode *d = x_new (X_ELEMENT, "defs");
	d->attrs.push_back ({ "id", E.doc.new_id ("defs") });
	E.doc.insert (E.doc.root, 0, d);
	return d;
}
void lpe_add (XNode *&path, const LpeType *t)
{
	E.doc.begin (TR ("Path effect"));
	path = convert_to_path (path);
	if (!E.doc.root->get ("xmlns:inkscape")) E.doc.set (E.doc.root, "xmlns:inkscape", "http://www.inkscape.org/namespaces/inkscape");
	XNode *e = x_new (X_ELEMENT, "inkscape:path-effect");
	std::string id = E.doc.new_id ("path-effect");
	e->attrs.push_back ({ "id", id });
	e->attrs.push_back ({ "effect", t->id });
	E.doc.insert (defs (), -1, e);
	if (!path->get ("inkscape:original-d")) E.doc.set (path, "inkscape:original-d", path->get2 ("d", ""));
	std::string list = path->get2 ("inkscape:path-effect", "");
	if (!list.empty ()) list += ';';
	list += "#" + id;
	E.doc.set (path, "inkscape:path-effect", list);
	lpe_refresh (path);
	E.doc.commit ();
	E.sync ();
}
void lpe_remove (XNode *path, int index)
{
	auto st = lpe_stack (path);
	if (index < 0 || index >= (int) st.size ()) return;
	E.doc.begin (TR ("Remove effect"));
	std::string list;
	for (int i = 0; i < (int) st.size (); i++)
		if (i != index) { if (!list.empty ()) list += ';'; list += std::string ("#") + st[i]->get ("id"); }
	E.doc.remove (st[index]);
	if (list.empty ())
	{
		E.doc.set (path, "d", path->get2 ("inkscape:original-d", ""));
		E.doc.set (path, "inkscape:path-effect", nullptr);
		E.doc.set (path, "inkscape:original-d", nullptr);
	}
	else
	{
		E.doc.set (path, "inkscape:path-effect", list);
		lpe_refresh (path);
	}
	E.doc.commit ();
	E.sync ();
}
void lpe_set (XNode *path, XNode *e, const LpeParam &p, double v)
{
	if (v < p.min) v = p.min;
	if (v > p.max) v = p.max;
	E.doc.begin (TR ("Path effect"), 13);
	E.doc.set (e, p.name, fmt (v));
	lpe_refresh (path);
	E.doc.commit ();
	E.sync ();
}

// ---- filters, our presets -------------------------------------------------------------------------------------------------
struct Preset { const char *label, *body; };
static const Preset PRESETS[] = {
	{ "Soft blur", "<feGaussianBlur stdDeviation='3'/>" },
	{ "Drop shadow", "<feGaussianBlur in='SourceAlpha' stdDeviation='4' result='b'/><feOffset dx='5' dy='5' result='o'/><feFlood flood-color='#000' flood-opacity='.45'/><feComposite in2='o' operator='in' result='s'/><feMerge><feMergeNode in='s'/><feMergeNode in='SourceGraphic'/></feMerge>" },
	{ "Glow", "<feGaussianBlur stdDeviation='5' result='b'/><feMerge><feMergeNode in='b'/><feMergeNode in='b'/><feMergeNode in='SourceGraphic'/></feMerge>" },
	{ "Black and white", "<feColorMatrix type='saturate' values='0'/>" },
	{ "Warm", "<feColorMatrix type='matrix' values='1.1 0 0 0 .03  0 1 0 0 0  0 0 .85 0 0  0 0 0 1 0'/>" },
	{ "Negative", "<feComponentTransfer><feFuncR type='linear' slope='-1' intercept='1'/><feFuncG type='linear' slope='-1' intercept='1'/><feFuncB type='linear' slope='-1' intercept='1'/></feComponentTransfer>" },
	{ "Rough edges", "<feTurbulence type='fractalNoise' baseFrequency='.05' numOctaves='2' result='n'/><feDisplacementMap in='SourceGraphic' in2='n' scale='12'/>" },
	{ "Paper grain", "<feTurbulence type='fractalNoise' baseFrequency='.8' numOctaves='3' result='n'/><feColorMatrix in='n' type='saturate' values='0' result='g'/><feBlend in='SourceGraphic' in2='g' mode='multiply'/>" },
	{ "Raised", "<feGaussianBlur in='SourceAlpha' stdDeviation='2' result='b'/><feSpecularLighting in='b' surfaceScale='4' specularConstant='.8' specularExponent='18' lighting-color='#fff' result='l'><feDistantLight azimuth='225' elevation='45'/></feSpecularLighting><feComposite in='l' in2='SourceAlpha' operator='in' result='k'/><feComposite in='SourceGraphic' in2='k' operator='arithmetic' k1='0' k2='1' k3='1' k4='0'/>" },
	{ "Outline glow", "<feMorphology in='SourceAlpha' operator='dilate' radius='3' result='m'/><feGaussianBlur in='m' stdDeviation='2' result='b'/><feFlood flood-color='#fff'/><feComposite in2='b' operator='in' result='w'/><feMerge><feMergeNode in='w'/><feMergeNode in='SourceGraphic'/></feMerge>" },
};
int filter_presets () { return sizeof PRESETS / sizeof PRESETS[0]; }
const char *filter_preset_label (int i) { return PRESETS[i].label; }
void filter_apply (int i)
{
	if (E.sel.empty ()) { E.say (TR ("Select an object first.")); return; }
	std::string s = "<svg xmlns='http://www.w3.org/2000/svg'><filter>";
	s += PRESETS[i].body;
	s += "</filter></svg>";
	Document d;
	if (!d.load (s.data (), s.size (), nullptr)) return;
	XNode *f = nullptr;
	for (XNode *k : d.root->kids) if (k->element ()) f = k;
	XNode *c = f->clone ();
	std::string id = E.doc.new_id ("filter");
	c->attrs.push_back ({ "id", id });
	c->attrs.push_back ({ "x", "-20%" }); c->attrs.push_back ({ "y", "-20%" });
	c->attrs.push_back ({ "width", "140%" }); c->attrs.push_back ({ "height", "140%" });
	E.doc.begin (TR ("Filter"));
	E.doc.insert (defs (), -1, c);
	for (XNode *n : E.sel) set_style (n, "filter", ("url(#" + id + ")").c_str ());
	E.doc.commit ();
	E.sync ();
}
void filter_remove ()
{
	style_selection ("filter", nullptr, TR ("Remove filter"));
}

}
