//
// path.cpp -- see path.h. The arc conversion follows the endpoint-to-center formulas of the SVG specification (appendix
// "Elliptical arc implementation notes").
//
#include "path.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace vg {

Vec2 Bezier::at (double t) const
{
	double u = 1 - t;
	return p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + p3 * (t * t * t);
}
Vec2 Bezier::deriv (double t) const
{
	double u = 1 - t;
	return (p1 - p0) * (3 * u * u) + (p2 - p1) * (6 * u * t) + (p3 - p2) * (3 * t * t);
}
void Bezier::split (double t, Bezier &a, Bezier &b) const
{
	Vec2 p01 = lerp (p0, p1, t), p12 = lerp (p1, p2, t), p23 = lerp (p2, p3, t);
	Vec2 q = lerp (p01, p12, t), r = lerp (p12, p23, t), m = lerp (q, r, t);
	Bezier x { p0, p01, q, m }, y { m, r, p23, p3 };
	a = x; b = y;
}
// the extrema: roots of the derivative, axis by axis
static void extrema (double a, double b, double c, double d, double *ts, int &n)
{
	double k0 = b - a, k1 = c - b, k2 = d - c;
	double A = k0 - 2 * k1 + k2, B = 2 * (k1 - k0), C = k0;
	if (std::fabs (A) < 1e-12)
	{
		if (std::fabs (B) > 1e-12) { double t = -C / B; if (t > 0 && t < 1) ts[n++] = t; }
		return;
	}
	double D = B * B - 4 * A * C;
	if (D < 0) return;
	D = std::sqrt (D);
	double t = (-B + D) / (2 * A); if (t > 0 && t < 1) ts[n++] = t;
	t = (-B - D) / (2 * A); if (t > 0 && t < 1) ts[n++] = t;
}
Box Bezier::bounds () const
{
	Box r;
	r.add (p0); r.add (p3);
	if (line ()) return r;
	double ts[4]; int n = 0;
	extrema (p0.x, p1.x, p2.x, p3.x, ts, n);
	extrema (p0.y, p1.y, p2.y, p3.y, ts, n);
	for (int i = 0; i < n; i++) r.add (at (ts[i]));
	return r;
}
static double blen (const Bezier &b, double tol, int depth)
{
	double chord = dist (b.p0, b.p3), poly = dist (b.p0, b.p1) + dist (b.p1, b.p2) + dist (b.p2, b.p3);
	if (poly - chord <= tol || depth > 16) return (poly + chord) / 2;
	Bezier l, r;
	b.split (0.5, l, r);
	return blen (l, tol / 2, depth + 1) + blen (r, tol / 2, depth + 1);
}
double Bezier::length (double tol) const { return blen (*this, tol, 0); }

Bezier seg_of (const SubPath &s, int i)
{
	const Node &a = s.n[i], &b = s.n[(i + 1) % s.n.size ()];
	return { a.p, a.out, b.in, b.p };
}

Box Path::bounds () const
{
	Box r;
	for (auto &s : sub)
	{
		if (s.n.size () == 1) r.add (s.n[0].p);
		for (int i = 0; i < s.segments (); i++) r.add (seg_of (s, i).bounds ());
	}
	return r;
}
void Path::transform (const Affine &m)
{
	for (auto &s : sub)
		for (auto &n : s.n) { n.p = m.apply (n.p); n.in = m.apply (n.in); n.out = m.apply (n.out); }
}
void Path::move_to (Vec2 p) { SubPath s; s.n.emplace_back (p); sub.push_back (std::move (s)); }
void Path::line_to (Vec2 p)
{
	if (sub.empty () || sub.back ().closed) { Vec2 q = sub.empty () ? p : sub.back ().n[0].p; move_to (q); }
	sub.back ().n.emplace_back (p);
}
void Path::curve_to (Vec2 a, Vec2 b, Vec2 p)
{
	if (sub.empty () || sub.back ().closed) { Vec2 q = sub.empty () ? a : sub.back ().n[0].p; move_to (q); }
	SubPath &s = sub.back ();
	s.n.back ().out = a;
	Node q (p);
	q.in = b;
	s.n.push_back (q);
}
void Path::close ()
{
	if (sub.empty ()) return;
	SubPath &s = sub.back ();
	if (s.closed) return;
	s.closed = true;
	// a last node on the first one is the same node
	if (s.n.size () > 1 && s.n.back ().p == s.n[0].p)
	{
		s.n[0].in = s.n.back ().in;
		s.n.pop_back ();
	}
}
void Path::append (const Path &o) { sub.insert (sub.end (), o.sub.begin (), o.sub.end ()); }

void arc_to (Path &path, double rx, double ry, double rotDeg, bool large, bool sweep, Vec2 b)
{
	if (path.sub.empty ()) return;
	Vec2 a = path.sub.back ().n.back ().p;
	if (a == b) return;
	rx = std::fabs (rx); ry = std::fabs (ry);
	if (rx == 0 || ry == 0) { path.line_to (b); return; }
	double phi = rotDeg * M_PI / 180, cp = std::cos (phi), sp = std::sin (phi);
	double dx = (a.x - b.x) / 2, dy = (a.y - b.y) / 2;
	double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
	double lam = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
	if (lam > 1) { double k = std::sqrt (lam); rx *= k; ry *= k; }
	double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
	double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
	double co = den == 0 ? 0 : std::sqrt (std::max (0.0, num / den));
	if (large == sweep) co = -co;
	double cxp = co * rx * y1 / ry, cyp = -co * ry * x1 / rx;
	double cx = cp * cxp - sp * cyp + (a.x + b.x) / 2, cy = sp * cxp + cp * cyp + (a.y + b.y) / 2;
	double th1 = std::atan2 ((y1 - cyp) / ry, (x1 - cxp) / rx);
	double th2 = std::atan2 ((-y1 - cyp) / ry, (-x1 - cxp) / rx);
	double dth = th2 - th1;
	if (sweep && dth < 0) dth += 2 * M_PI;
	else if (!sweep && dth > 0) dth -= 2 * M_PI;
	int segs = (int) std::ceil (std::fabs (dth) / (M_PI / 2) - 1e-9);
	if (segs < 1) segs = 1;
	double step = dth / segs, t = 4.0 / 3 * std::tan (step / 4);
	auto P = [&] (double x, double y) {
		x *= rx; y *= ry;
		return Vec2 (cp * x - sp * y + cx, sp * x + cp * y + cy);
	};
	for (int i = 0; i < segs; i++)
	{
		double s1 = std::sin (th1), c1 = std::cos (th1), th = th1 + step, s2 = std::sin (th), c2 = std::cos (th);
		Vec2 end = i == segs - 1 ? b : P (c2, s2);
		path.curve_to (P (c1 - t * s1, s1 + t * c1), P (c2 + t * s2, s2 - t * c2), end);
		th1 = th;
	}
}

namespace {
struct Reader
{
	const char *s;
	void ws () { while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f') s++; }
	void comma () { ws (); if (*s == ',') { s++; ws (); } }
	bool num (double &v)
	{
		ws ();
		const char *p = s;
		if (*p == '+' || *p == '-') p++;
		bool dig = false;
		while (*p >= '0' && *p <= '9') { p++; dig = true; }
		if (*p == '.') { p++; while (*p >= '0' && *p <= '9') { p++; dig = true; } }
		if (!dig) return false;
		if (*p == 'e' || *p == 'E')
		{
			const char *q = p + 1;
			if (*q == '+' || *q == '-') q++;
			if (*q >= '0' && *q <= '9') { while (*q >= '0' && *q <= '9') q++; p = q; }
		}
		v = strtod (s, 0);
		s = p;
		return true;
	}
	bool flag (bool &f)
	{
		comma ();
		if (*s == '0' || *s == '1') { f = *s == '1'; s++; return true; }
		return false;
	}
	bool two (double &x, double &y)
	{
		if (!num (x)) return false;
		comma ();
		if (!num (y)) return false;
		comma ();
		return true;
	}
};
}

Path parse_d (const char *d)
{
	Path p;
	if (!d) return p;
	Reader r { d };
	Vec2 cur, start, lastC;
	char cmd = 0, prev = 0;
	for (;;)
	{
		r.ws ();
		char c = *r.s;
		if (!c) break;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) { cmd = c; r.s++; }
		else if (!cmd || cmd == 'z' || cmd == 'Z') break;
		else if (cmd == 'm') cmd = 'l';
		else if (cmd == 'M') cmd = 'L';
		bool rel = cmd >= 'a';
		Vec2 base = rel ? cur : Vec2 ();
		double a, b, c1, c2, e, f;
		switch (cmd | 32)
		{
		case 'm':
			if (!r.two (a, b)) return p;
			cur = base + Vec2 (a, b); start = cur; p.move_to (cur);
			break;
		case 'z':
			p.close (); cur = start;
			break;
		case 'l':
			if (!r.two (a, b)) return p;
			cur = base + Vec2 (a, b); p.line_to (cur);
			break;
		case 'h':
			if (!r.num (a)) return p;
			r.comma (); cur.x = rel ? cur.x + a : a; p.line_to (cur);
			break;
		case 'v':
			if (!r.num (a)) return p;
			r.comma (); cur.y = rel ? cur.y + a : a; p.line_to (cur);
			break;
		case 'c':
			if (!r.two (a, b) || !r.two (c1, c2) || !r.two (e, f)) return p;
			lastC = base + Vec2 (c1, c2); cur = base + Vec2 (e, f);
			p.curve_to (base + Vec2 (a, b), lastC, cur);
			break;
		case 's':
		{
			if (!r.two (c1, c2) || !r.two (e, f)) return p;
			char pc = prev | 32;
			Vec2 k = pc == 'c' || pc == 's' ? cur * 2 - lastC : cur;
			lastC = base + Vec2 (c1, c2); Vec2 end = base + Vec2 (e, f);
			p.curve_to (k, lastC, end); cur = end;
			break;
		}
		case 'q':
		case 't':
		{
			Vec2 q;
			if ((cmd | 32) == 'q') { if (!r.two (a, b)) return p; q = base + Vec2 (a, b); }
			else { char pc = prev | 32; q = pc == 'q' || pc == 't' ? cur * 2 - lastC : cur; }
			if (!r.two (e, f)) return p;
			Vec2 end = base + Vec2 (e, f);
			p.curve_to (cur + (q - cur) * (2.0 / 3), end + (q - end) * (2.0 / 3), end);
			lastC = q; cur = end;
			break;
		}
		case 'a':
		{
			bool la, sw; double rot;
			if (!r.num (a)) return p;
			r.comma ();
			if (!r.num (b)) return p;
			r.comma ();
			if (!r.num (rot) || !r.flag (la) || !r.flag (sw)) return p;
			r.comma ();
			if (!r.two (e, f)) return p;
			Vec2 end = base + Vec2 (e, f);
			if (p.sub.empty ()) p.move_to (cur);
			arc_to (p, a, b, rot, la, sw, end);
			cur = end;
			break;
		}
		default:
			return p;
		}
		prev = cmd;
	}
	return p;
}

static void put (std::string &o, double v, int digits)
{
	char b[48];
	snprintf (b, sizeof b, "%.*g", digits + 2, v);
	if (strchr (b, 'e'))
	{
		snprintf (b, sizeof b, "%.*f", digits, v);
		char *e = b + strlen (b) - 1;
		while (*e == '0') *e-- = 0;
		if (*e == '.') *e = 0;
	}
	o += b;
}
static void put2 (std::string &o, Vec2 p, int digits) { put (o, p.x, digits); o += ','; put (o, p.y, digits); }

std::string write_d (const Path &p, int digits)
{
	std::string o;
	for (auto &s : p.sub)
	{
		if (s.n.empty ()) continue;
		if (!o.empty ()) o += ' ';
		o += "M "; put2 (o, s.n[0].p, digits);
		char mode = 0;
		for (int i = 0; i < s.segments (); i++)
		{
			Bezier b = seg_of (s, i);
			if (s.closed && i == s.segments () - 1 && b.line ()) break;	// (Z draws it)
			if (b.line ())
			{
				if (mode != 'L') { o += " L"; mode = 'L'; }
				o += ' '; put2 (o, b.p3, digits);
			}
			else
			{
				if (mode != 'C') { o += " C"; mode = 'C'; }
				o += ' '; put2 (o, b.p1, digits); o += ' '; put2 (o, b.p2, digits); o += ' '; put2 (o, b.p3, digits);
			}
		}
		if (s.closed) o += " Z";
	}
	return o;
}

void apply_nodetypes (Path &p, const char *t)
{
	if (!t) return;
	for (auto &s : p.sub)
	{
		for (auto &n : s.n)
		{
			switch (*t)
			{
			case 0: return;
			case 's': n.type = N_SMOOTH; break;
			case 'z': n.type = N_SYMMETRIC; break;
			case 'a': n.type = N_AUTO; break;
			default: n.type = N_CUSP; break;
			}
			t++;
		}
		if (s.closed && *t) t++;		// (the closing node is written twice)
	}
}
std::string write_nodetypes (const Path &p)
{
	static const char L[] = "csza";
	std::string o;
	for (auto &s : p.sub)
	{
		for (auto &n : s.n) o += L[n.type];
		if (s.closed && !s.n.empty ()) o += L[s.n[0].type];
	}
	return o;
}

static const double KAPPA = 0.5522847498307936;

Path path_rect (double x, double y, double w, double h, double rx, double ry)
{
	Path p;
	if (rx <= 0 && ry <= 0)
	{
		p.move_to ({ x, y }); p.line_to ({ x + w, y }); p.line_to ({ x + w, y + h }); p.line_to ({ x, y + h }); p.close ();
		return p;
	}
	if (rx <= 0) rx = ry;
	if (ry <= 0) ry = rx;
	rx = std::min (rx, w / 2); ry = std::min (ry, h / 2);
	double kx = rx * (1 - KAPPA), ky = ry * (1 - KAPPA);
	p.move_to ({ x + rx, y });
	p.line_to ({ x + w - rx, y });
	p.curve_to ({ x + w - kx, y }, { x + w, y + ky }, { x + w, y + ry });
	p.line_to ({ x + w, y + h - ry });
	p.curve_to ({ x + w, y + h - ky }, { x + w - kx, y + h }, { x + w - rx, y + h });
	p.line_to ({ x + rx, y + h });
	p.curve_to ({ x + kx, y + h }, { x, y + h - ky }, { x, y + h - ry });
	p.line_to ({ x, y + ry });
	p.curve_to ({ x, y + ky }, { x + kx, y }, { x + rx, y });
	p.close ();
	return p;
}
Path path_ellipse (double cx, double cy, double rx, double ry)
{
	Path p;
	double kx = rx * KAPPA, ky = ry * KAPPA;
	p.move_to ({ cx + rx, cy });
	p.curve_to ({ cx + rx, cy + ky }, { cx + kx, cy + ry }, { cx, cy + ry });
	p.curve_to ({ cx - kx, cy + ry }, { cx - rx, cy + ky }, { cx - rx, cy });
	p.curve_to ({ cx - rx, cy - ky }, { cx - kx, cy - ry }, { cx, cy - ry });
	p.curve_to ({ cx + kx, cy - ry }, { cx + rx, cy - ky }, { cx + rx, cy });
	p.close ();
	return p;
}

static void flat (const Bezier &b, double tol, std::vector<Vec2> &out, int depth)
{
	Vec2 d = b.p3 - b.p0;
	double l = length (d);
	double e1, e2;
	if (l > 1e-12) { e1 = std::fabs (cross (b.p1 - b.p0, d)) / l; e2 = std::fabs (cross (b.p2 - b.p0, d)) / l; }
	else { e1 = dist (b.p1, b.p0); e2 = dist (b.p2, b.p0); }
	if (depth >= 16 || std::max (e1, e2) <= tol) { out.push_back (b.p3); return; }
	Bezier l1, r1;
	b.split (0.5, l1, r1);
	flat (l1, tol, out, depth + 1);
	flat (r1, tol, out, depth + 1);
}
void flatten (const Bezier &b, double tol, std::vector<Vec2> &out) { flat (b, tol, out, 0); }

}
