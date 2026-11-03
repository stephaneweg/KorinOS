//
// path.h -- a path as an editor needs it: subpaths of nodes, each with its two handles, in doubles. A straight segment
// is one whose handles sit on their nodes. Arcs and quadratics become cubics on reading: the attribute keeps the
// author's text until the path is really edited (doc/document.h never rewrites what did not change).
//
#ifndef _vector_path_h
#define _vector_path_h

#include "geom.h"
#include <cstdint>
#include <string>
#include <vector>

namespace vg {

enum NodeType : uint8_t { N_CUSP, N_SMOOTH, N_SYMMETRIC, N_AUTO };

struct Node
{
	Vec2 p, in, out;		// the point, the handle arriving, the handle leaving
	NodeType type = N_CUSP;
	Node () {}
	explicit Node (Vec2 q) : p (q), in (q), out (q) {}
};

struct SubPath
{
	std::vector<Node> n;
	bool closed = false;
	int segments () const { int c = (int) n.size (); return closed ? c : c - 1; }
};

struct Bezier
{
	Vec2 p0, p1, p2, p3;
	bool line () const { return p1 == p0 && p2 == p3; }
	Vec2 at (double t) const;
	Vec2 deriv (double t) const;
	void split (double t, Bezier &a, Bezier &b) const;
	Box bounds () const;
	double length (double tol = 0.01) const;
};

struct Path
{
	std::vector<SubPath> sub;
	bool empty () const { return sub.empty (); }
	int nodes () const { int c = 0; for (auto &s : sub) c += (int) s.n.size (); return c; }
	Box bounds () const;
	void transform (const Affine &m);
	void move_to (Vec2 p);
	void line_to (Vec2 p);
	void curve_to (Vec2 a, Vec2 b, Vec2 p);
	void close ();
	void append (const Path &o);
};

// the segment of s that leaves node i
Bezier seg_of (const SubPath &s, int i);
// the "d" attribute: lenient (it stops at the first error and keeps what it has, as SVG asks)
Path parse_d (const char *d);
std::string write_d (const Path &p, int digits = 6);
// sodipodi:nodetypes, read and written alongside "d"
void apply_nodetypes (Path &p, const char *t);
std::string write_nodetypes (const Path &p);
// an SVG arc from the current point to b, as cubics appended to the path
void arc_to (Path &p, double rx, double ry, double rotDeg, bool large, bool sweep, Vec2 b);
// the standard shapes
Path path_rect (double x, double y, double w, double h, double rx, double ry);
Path path_ellipse (double cx, double cy, double rx, double ry);
// flattening: the polyline of a cubic to a tolerance, without its first point
void flatten (const Bezier &b, double tol, std::vector<Vec2> &out);

}

#endif
