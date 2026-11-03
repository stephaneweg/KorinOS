// geomtest.cpp -- src/apps/vector/geom on the PC, no Skia, no UIKit:
//   g++ -std=gnu++20 -O1 -g -fsanitize=address,undefined tools/vector/geomtest.cpp src/apps/vector/geom/path.cpp -o /tmp/geomtest && /tmp/geomtest
#include "../../src/apps/vector/geom/path.h"
#include <cstdio>
#include <cstdlib>

using namespace vg;

static int fails, checks;
static void ok (bool c, const char *what) { checks++; if (!c) { fails++; printf ("FAIL %s\n", what); } }
static bool near (double a, double b, double e = 1e-6) { return std::fabs (a - b) <= e; }

static bool same (const Path &a, const Path &b, double e)
{
	if (a.sub.size () != b.sub.size ()) return false;
	for (size_t i = 0; i < a.sub.size (); i++)
	{
		auto &s = a.sub[i], &t = b.sub[i];
		if (s.closed != t.closed || s.n.size () != t.n.size ()) return false;
		for (size_t j = 0; j < s.n.size (); j++)
			if (dist (s.n[j].p, t.n[j].p) > e || dist (s.n[j].in, t.n[j].in) > e || dist (s.n[j].out, t.n[j].out) > e) return false;
	}
	return true;
}

int main ()
{
	ok (parse_d ("").empty (), "empty");
	{
		Path p = parse_d ("M10 20 L30 40 H50 V60 Z");
		ok (p.sub.size () == 1 && p.sub[0].closed && p.sub[0].n.size () == 4, "lines");
		ok (p.sub[0].n[2].p == Vec2 (50, 40), "H");
		ok (p.sub[0].n[3].p == Vec2 (50, 60), "V");
	}
	{
		Path p = parse_d ("m1,1 2,2 3,3");
		ok (p.sub[0].n.size () == 3 && p.sub[0].n[2].p == Vec2 (6, 6), "implicit relative lineto");
	}
	{
		Path p = parse_d ("M1.5.5-2-3");
		ok (p.sub[0].n.size () == 2 && near (p.sub[0].n[0].p.y, .5) && near (p.sub[0].n[1].p.x, -2), "compact numbers");
	}
	{
		Path p = parse_d ("M0 0 L10 10 oops 5");
		ok (p.sub[0].n.size () == 2, "an error keeps what came before");
	}
	{
		// a half circle: the end lands exactly, the top is at the radius
		Path p = parse_d ("M0 0 A10 10 0 0 1 20 0");
		Box b = p.bounds ();
		ok (near (b.y0, -10, 0.01) && near (b.x0, 0, 0.01) && near (b.x1, 20, 0.01), "arc bounds");
		ok (p.sub[0].n.back ().p == Vec2 (20, 0), "arc end");
		Path q = parse_d ("M0 0 A5 5 0 0 1 20 0");
		ok (near (q.bounds ().y0, -10, 0.01), "arc radius grows to fit");
		Path r = parse_d ("M0 0 A10 10 0 1 0 20 0");
		ok (near (r.bounds ().y1, 10, 0.01), "sweep and large flags");
	}
	{
		Path p = parse_d ("M0 0 Q10 20 20 0 T40 0");
		ok (p.sub[0].n.size () == 3 && near (p.bounds ().y1, 10, 0.01) && near (p.bounds ().y0, -10, 0.01), "quadratics and T");
		Path s = parse_d ("M0 0 C0 10 10 10 10 0 S20 -10 20 0");
		ok (s.sub[0].n[1].out == Vec2 (10, -10), "S reflects");
	}
	{
		Path p = parse_d ("M0 0 C1 2 3 4 5 6 L7 8 Z M10 10 L20 20");
		Path q = parse_d (write_d (p, 8).c_str ());
		ok (same (p, q, 1e-6), "write then read");
		Path c = path_ellipse (5, 5, 3, 2);
		Path d = parse_d (write_d (c, 10).c_str ());
		ok (same (c, d, 1e-6), "closed curve keeps its closing segment");
	}
	{
		Path p = path_ellipse (0, 0, 10, 5);
		Box b = p.bounds ();
		ok (near (b.x1, 10, 1e-9) && near (b.y1, 5, 1e-9), "ellipse bounds");
		p.transform (Affine::translate (3, 4) * Affine::scale (2, 2));
		ok (near (p.bounds ().x1, 23, 1e-9), "transform");
		Path r = path_rect (0, 0, 10, 10, 2, 2);
		ok (r.sub[0].n.size () == 8 && near (r.bounds ().x1, 10), "round rect");
	}
	{
		Path p = parse_d ("M0 0 C0 10 10 10 10 0 L5 5 Z");
		apply_nodetypes (p, "szcz");
		ok (p.sub[0].n[0].type == N_SMOOTH && p.sub[0].n[1].type == N_SYMMETRIC, "nodetypes read");
		ok (write_nodetypes (p) == "szcs", "nodetypes written, closing node twice");
	}
	{
		Affine m = Affine::rotate_at (0.7, { 3, 4 }) * Affine::scale (2, 3) * Affine::translate (5, 6);
		Vec2 q = m.inverse ().apply (m.apply ({ 7, 8 }));
		ok (near (q.x, 7, 1e-9) && near (q.y, 8, 1e-9), "inverse");
	}
	{
		Bezier b { { 0, 0 }, { 0, 10 }, { 10, 10 }, { 10, 0 } }, l, r;
		b.split (0.3, l, r);
		ok (dist (l.at (1), b.at (0.3)) < 1e-9 && dist (r.at (0.5), b.at (0.65)) < 1e-9, "split");
		std::vector<Vec2> pts;
		flatten (b, 0.01, pts);
		ok (pts.size () > 8 && pts.back () == b.p3, "flatten");
		ok (near (b.length (0.001), 20, 0.5), "length");
	}
	printf ("geomtest: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
