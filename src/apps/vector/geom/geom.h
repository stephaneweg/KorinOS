//
// geom.h -- the plain geometry under everything: points, boxes, affine transforms. No Skia, no UIKit: this folder also
// compiles and runs on the PC (tools/vector/geomtest.cpp). Document coordinates are doubles; Skia only ever sees floats.
//
#ifndef _vector_geom_h
#define _vector_geom_h

#include <cmath>
#include <limits>

namespace vg {

struct Vec2
{
	double x = 0, y = 0;
	Vec2 () {}
	Vec2 (double x_, double y_) : x (x_), y (y_) {}
	Vec2 operator+ (Vec2 o) const { return { x + o.x, y + o.y }; }
	Vec2 operator- (Vec2 o) const { return { x - o.x, y - o.y }; }
	Vec2 operator* (double k) const { return { x * k, y * k }; }
	Vec2 operator/ (double k) const { return { x / k, y / k }; }
	Vec2 operator- () const { return { -x, -y }; }
	Vec2 &operator+= (Vec2 o) { x += o.x; y += o.y; return *this; }
	Vec2 &operator-= (Vec2 o) { x -= o.x; y -= o.y; return *this; }
	bool operator== (Vec2 o) const { return x == o.x && y == o.y; }
	bool operator!= (Vec2 o) const { return !(*this == o); }
};
inline double dot (Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline double cross (Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
inline double length (Vec2 a) { return std::hypot (a.x, a.y); }
inline double dist (Vec2 a, Vec2 b) { return length (a - b); }
inline Vec2 lerp (Vec2 a, Vec2 b, double t) { return a + (b - a) * t; }
inline Vec2 unit (Vec2 a) { double l = length (a); return l > 0 ? a / l : Vec2 (); }
inline Vec2 perp (Vec2 a) { return { -a.y, a.x }; }

struct Box
{
	double x0 = std::numeric_limits<double>::infinity (), y0 = x0, x1 = -x0, y1 = x1;
	Box () {}
	Box (double a, double b, double c, double d) : x0 (a), y0 (b), x1 (c), y1 (d) {}
	bool empty () const { return !(x1 >= x0 && y1 >= y0); }
	double w () const { return x1 - x0; }
	double h () const { return y1 - y0; }
	Vec2 center () const { return { (x0 + x1) / 2, (y0 + y1) / 2 }; }
	void add (Vec2 p) { if (p.x < x0) x0 = p.x; if (p.y < y0) y0 = p.y; if (p.x > x1) x1 = p.x; if (p.y > y1) y1 = p.y; }
	void add (const Box &b) { if (b.empty ()) return; add (Vec2 (b.x0, b.y0)); add (Vec2 (b.x1, b.y1)); }
	bool contains (Vec2 p) const { return p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1; }
	bool overlaps (const Box &b) const { return !(b.x0 > x1 || b.x1 < x0 || b.y0 > y1 || b.y1 < y0); }
	bool inside (const Box &b) const { return x0 >= b.x0 && x1 <= b.x1 && y0 >= b.y0 && y1 <= b.y1; }
	Box grown (double k) const { return { x0 - k, y0 - k, x1 + k, y1 + k }; }
};

// | a c e |
// | b d f |   as in SVG: x' = a x + c y + e
struct Affine
{
	double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
	Affine () {}
	Affine (double a_, double b_, double c_, double d_, double e_, double f_) : a (a_), b (b_), c (c_), d (d_), e (e_), f (f_) {}
	static Affine translate (double x, double y) { return { 1, 0, 0, 1, x, y }; }
	static Affine scale (double x, double y) { return { x, 0, 0, y, 0, 0 }; }
	static Affine rotate (double rad) { double s = std::sin (rad), k = std::cos (rad); return { k, s, -s, k, 0, 0 }; }
	static Affine rotate_at (double rad, Vec2 c) { return translate (c.x, c.y) * rotate (rad) * translate (-c.x, -c.y); }
	static Affine scale_at (double x, double y, Vec2 c) { return translate (c.x, c.y) * scale (x, y) * translate (-c.x, -c.y); }
	// this after o: (A * B) p = A (B p)
	Affine operator* (const Affine &o) const
	{
		return { a * o.a + c * o.b, b * o.a + d * o.b, a * o.c + c * o.d, b * o.c + d * o.d,
			a * o.e + c * o.f + e, b * o.e + d * o.f + f };
	}
	Vec2 apply (Vec2 p) const { return { a * p.x + c * p.y + e, b * p.x + d * p.y + f }; }
	Vec2 linear (Vec2 p) const { return { a * p.x + c * p.y, b * p.x + d * p.y }; }
	double det () const { return a * d - b * c; }
	bool invertible () const { return std::fabs (det ()) > 1e-300; }
	Affine inverse () const
	{
		double k = det ();
		if (k == 0) return {};
		k = 1 / k;
		return { d * k, -b * k, -c * k, a * k, (c * f - d * e) * k, (b * e - a * f) * k };
	}
	double mean_scale () const { return std::sqrt (std::fabs (det ())); }
	bool identity () const { return a == 1 && b == 0 && c == 0 && d == 1 && e == 0 && f == 0; }
	Box apply (const Box &q) const
	{
		Box r;
		if (q.empty ()) return r;
		r.add (apply (Vec2 (q.x0, q.y0))); r.add (apply (Vec2 (q.x1, q.y0)));
		r.add (apply (Vec2 (q.x0, q.y1))); r.add (apply (Vec2 (q.x1, q.y1)));
		return r;
	}
};

}

#endif
