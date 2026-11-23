// render.cpp -- Forge : la vue 3D par le processeur (render.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "render.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace forge {

static const double DEG = M_PI / 180;

static unsigned mix (unsigned a, unsigned b, int t)	// t sur 256 : 0 a, 256 b
{
	unsigned o = 0;
	for (int s = 0; s < 24; s += 8) {
		int x = (a >> s) & 255, y = (b >> s) & 255;
		o |= (unsigned) (x + ((y - x) * t >> 8)) << s;
	}
	return o;
}

static unsigned shade (unsigned c, double k)
{
	unsigned o = 0;
	for (int s = 0; s < 24; s += 8) {
		double v = ((c >> s) & 255) * k;
		if (k > 1) v += (255 - v) * (k - 1) * 0.5;
		o |= (unsigned) std::min (255.0, std::max (0.0, v)) << s;
	}
	return o;
}

void Camera::basis (double r[3], double u[3], double f[3]) const
{
	double cp = cos (pitch * DEG), sp = sin (pitch * DEG), cy = cos (yaw * DEG), sy = sin (yaw * DEG);
	double e[3] = { cp * cy, cp * sy, sp };		// du point visé vers l'œil
	f[0] = -e[0], f[1] = -e[1], f[2] = -e[2];
	r[0] = f[1], r[1] = -f[0], r[2] = 0;		// f × Z
	double n = sqrt (r[0] * r[0] + r[1] * r[1]);
	if (n < 1e-9) r[0] = 1, r[1] = 0, n = 1;
	r[0] /= n, r[1] /= n;
	u[0] = r[1] * f[2] - r[2] * f[1];		// r × f
	u[1] = r[2] * f[0] - r[0] * f[2];
	u[2] = r[0] * f[1] - r[1] * f[0];
}

void View::resize (int w, int h)
{
	W = std::max (w, 1);
	H = std::max (h, 1);
	zb.assign ((size_t) W * H, 1e30f);
	fid.assign ((size_t) W * H, 0);
}

// ---- la projection ------------------------------------------------------------------------------------------------
//
// Les points passent dans le repère de la caméra : x à droite, y en haut, z le long de la vue (la profondeur, en mm).
// Orthographique : depuis le point visé, l'écran à cam.scale pixels par mm. Perspective : depuis l'œil, à dist () du
// point visé, l'écran à focal () / z pixels par mm ; ce qui est en deçà du plan proche (z < nearZ) est découpé avant
// la division. Le tampon de profondeur garde z (en mm) dans les deux cas : en perspective, c'est 1 / z qui varie
// linéairement sur l'écran, et l'interpolation passe par lui.

struct Proj {
	double r[3], u[3], f[3], o[3];	// le repère ; o : le point visé (orthographique) ou l'œil
	double W2, H2, k, nearZ;	// k : l'échelle (orthographique) ou la focale
	bool persp;
	void cam (const double p[3], double c[3]) const
	{
		double q[3] = { p[0] - o[0], p[1] - o[1], p[2] - o[2] };
		c[0] = q[0] * r[0] + q[1] * r[1] + q[2] * r[2];
		c[1] = q[0] * u[0] + q[1] * u[1] + q[2] * u[2];
		c[2] = q[0] * f[0] + q[1] * f[1] + q[2] * f[2];
	}
	void cam (const float p[3], double c[3]) const
	{
		double d[3] = { p[0], p[1], p[2] };
		cam (d, c);
	}
	// un point du repère de la caméra à l'écran (en perspective : z >= nearZ, découpé avant)
	void pix (const double c[3], double &sx, double &sy) const
	{
		double m = persp ? k / std::max (c[2], 1e-9) : k;
		sx = W2 + c[0] * m;
		sy = H2 - c[1] * m;
	}
};

double View::focal () const { return H / 2.0 / tan (FOV * DEG / 2); }
double View::dist () const { return focal () / cam.scale; }

void View::eye (double e[3]) const
{
	double r[3], u[3], f[3];
	cam.basis (r, u, f);
	double d = dist ();
	e[0] = cam.tx - f[0] * d, e[1] = cam.ty - f[1] * d, e[2] = cam.tz - f[2] * d;
}

Proj View::proj () const
{
	Proj P;
	cam.basis (P.r, P.u, P.f);
	P.persp = persp;
	P.W2 = W / 2.0, P.H2 = H / 2.0;
	if (persp) {
		eye (P.o);
		P.k = focal ();
		P.nearZ = std::max (1e-3, dist () * 0.01);
	} else {
		P.o[0] = cam.tx, P.o[1] = cam.ty, P.o[2] = cam.tz;
		P.k = cam.scale;
		P.nearZ = -1e30;
	}
	return P;
}

// la profondeur au paramètre t (sur l'écran) d'un trait de z0 à z1 ; la marge d'un trait sur ce qui le cache
float View::zAt (float z0, float z1, double t) const
{
	if (!persp) return (float) (z0 + (z1 - z0) * t);
	return (float) (1 / ((1 - t) / z0 + t / z1));
}
float View::bias (float z) const
{
	return (float) ((persp ? 1.5 * z / focal () : 1.5 / cam.scale) + 0.02);	// (un pixel et demi à cette profondeur)
}

// un triangle à l'écran (x, y, profondeur par sommet), un attribut par sommet (l'éclairage) ; fn (x, y, z, a) pour
// chaque pixel couvert, z et a interpolés selon la projection
template <class Fn> static void raster (int W, int H, bool persp, const double v[3][3], const double a[3], Fn fn)
{
	double area = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[1][1] - v[0][1]) * (v[2][0] - v[0][0]);
	if (std::fabs (area) < 1e-9) return;
	int x0 = std::max (0, (int) floor (std::min ({ v[0][0], v[1][0], v[2][0] })));
	int x1 = std::min (W - 1, (int) ceil (std::max ({ v[0][0], v[1][0], v[2][0] })));
	int y0 = std::max (0, (int) floor (std::min ({ v[0][1], v[1][1], v[2][1] })));
	int y1 = std::min (H - 1, (int) ceil (std::max ({ v[0][1], v[1][1], v[2][1] })));
	double inv = 1 / area;
	double iz[3] = { 1, 1, 1 };
	if (persp)
		for (int k = 0; k < 3; k++) iz[k] = 1 / v[k][2];
	for (int y = y0; y <= y1; y++) {
		double py = y + 0.5;
		for (int x = x0; x <= x1; x++) {
			double px = x + 0.5;
			double w0 = ((v[1][0] - px) * (v[2][1] - py) - (v[1][1] - py) * (v[2][0] - px)) * inv;
			double w1 = ((v[2][0] - px) * (v[0][1] - py) - (v[2][1] - py) * (v[0][0] - px)) * inv;
			double w2 = 1 - w0 - w1;
			if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
			if (persp) {	// (1 / z, a / z : linéaires sur l'écran)
				double q = w0 * iz[0] + w1 * iz[1] + w2 * iz[2], z = 1 / q;
				fn (x, y, (float) z, (w0 * a[0] * iz[0] + w1 * a[1] * iz[1] + w2 * a[2] * iz[2]) * z);
			} else
				fn (x, y, (float) (w0 * v[0][2] + w1 * v[1][2] + w2 * v[2][2]), w0 * a[0] + w1 * a[1] + w2 * a[2]);
		}
	}
}

// un triangle dans le repère de la caméra : découpé contre le plan proche (perspective), projeté, rastérisé
template <class Fn> static void triClip (const Proj &P, int W, int H, const double c[3][3], const double a[3], Fn fn)
{
	double v[3][3], at[3];
	int nIn = 0;
	for (int k = 0; k < 3; k++) nIn += c[k][2] >= P.nearZ;
	if (nIn == 3) {
		for (int k = 0; k < 3; k++) P.pix (c[k], v[k][0], v[k][1]), v[k][2] = c[k][2];
		raster (W, H, P.persp, v, a, fn);
		return;
	}
	if (nIn == 0) return;
	// (Sutherland-Hodgman sur un seul plan : 3 ou 4 sommets ; l'attribut suit linéairement dans l'espace)
	double pc[4][3], pa[4];
	int n = 0;
	for (int i = 0; i < 3; i++) {
		const double *p = c[i], *q = c[(i + 1) % 3];
		bool pin = p[2] >= P.nearZ, qin = q[2] >= P.nearZ;
		if (pin) memcpy (pc[n], p, sizeof pc[n]), pa[n++] = a[i];
		if (pin != qin) {
			double t = (P.nearZ - p[2]) / (q[2] - p[2]);
			for (int k = 0; k < 3; k++) pc[n][k] = p[k] + (q[k] - p[k]) * t;
			pc[n][2] = P.nearZ;
			pa[n++] = a[i] + (a[(i + 1) % 3] - a[i]) * t;
		}
	}
	for (int t = 1; t + 1 < n; t++) {
		int ix[3] = { 0, t, t + 1 };
		for (int k = 0; k < 3; k++) P.pix (pc[ix[k]], v[k][0], v[k][1]), v[k][2] = pc[ix[k]][2], at[k] = pa[ix[k]];
		raster (W, H, P.persp, v, at, fn);
	}
}

// un trait de l'espace : découpé contre le plan proche puis contre la vue (à quelques pixels près), dessiné ; gardé
// dans segs pour le choix d'une arête si body >= 0 ; col 0xFFFFFFFF : gardé, pas dessiné
void View::seg (unsigned *pix, int pitch, const Proj &P, const double a[3], const double b[3], unsigned col, int thick, bool depth, int body, int edge)
{
	double ca[3], cb[3];
	P.cam (a, ca);
	P.cam (b, cb);
	if (P.persp) {
		bool ain = ca[2] >= P.nearZ, bin = cb[2] >= P.nearZ;
		if (!ain && !bin) return;
		if (ain != bin) {
			double *o = ain ? cb : ca;	// (le bout derrière : ramené sur le plan proche)
			double t = (P.nearZ - ca[2]) / (cb[2] - ca[2]);
			for (int k = 0; k < 3; k++) o[k] = ca[k] + (cb[k] - ca[k]) * t;
			o[2] = P.nearZ;
		}
	}
	double x0, y0, x1, y1;
	P.pix (ca, x0, y0);
	P.pix (cb, x1, y1);
	// (Liang-Barsky contre la vue élargie ; la profondeur suit : 1 / z en perspective, z sinon, linéaires sur l'écran)
	double m = thick + 4, lo = 0, hi = 1, dx = x1 - x0, dy = y1 - y0;
	double pp[4] = { -dx, dx, -dy, dy }, qq[4] = { x0 + m, W + m - x0, y0 + m, H + m - y0 };
	for (int i = 0; i < 4; i++) {
		if (pp[i] == 0) {
			if (qq[i] < 0) return;
			continue;
		}
		double t = qq[i] / pp[i];
		if (pp[i] < 0) lo = std::max (lo, t);
		else hi = std::min (hi, t);
		if (lo > hi) return;
	}
	float z0 = zAt ((float) ca[2], (float) cb[2], lo), z1 = zAt ((float) ca[2], (float) cb[2], hi);
	double X0 = x0 + dx * lo, Y0 = y0 + dy * lo, X1 = x0 + dx * hi, Y1 = y0 + dy * hi;
	if (body >= 0) segs.push_back ({ (float) X0, (float) Y0, z0, (float) X1, (float) Y1, z1, body, edge });
	if (col != 0xFFFFFFFF) line (pix, pitch, X0, Y0, z0, X1, Y1, z1, col, thick, depth);
}

// un trait de (x0, y0, z0) à (x1, y1, z1), épaisseur en pixels ; depth : caché par ce qui est devant
void View::line (unsigned *pix, int pitch, double x0, double y0, double z0, double x1, double y1, double z1, unsigned col, int thick, bool depth)
{
	double dx = x1 - x0, dy = y1 - y0;
	int n = (int) std::max (std::fabs (dx), std::fabs (dy)) + 1;
	if (n > 8000) return;			// (un trait hors de toute mesure)
	int a = -(thick - 1) / 2, b = thick / 2;
	for (int k = 0; k <= n; k++) {
		double t = (double) k / n;
		int x = (int) floor (x0 + dx * t + 0.5), y = (int) floor (y0 + dy * t + 0.5);
		float z = depth ? zAt ((float) z0, (float) z1, t) : 0, bz = depth ? bias (z) : 0;
		for (int oy = a; oy <= b; oy++)
			for (int ox = a; ox <= b; ox++) {
				int X = x + ox, Y = y + oy;
				if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
				size_t i = (size_t) Y * W + X;
				if (depth && z > zb[i] + bz) continue;
				pix[(size_t) Y * pitch + X] = col;
			}
	}
}

// un quadrilatère convexe sans profondeur (le cube de vue)
static void quad (unsigned *pix, int pitch, int W, int H, const double q[4][2], unsigned col)
{
	int y0 = std::max (0, (int) floor (std::min ({ q[0][1], q[1][1], q[2][1], q[3][1] })));
	int y1 = std::min (H - 1, (int) ceil (std::max ({ q[0][1], q[1][1], q[2][1], q[3][1] })));
	for (int y = y0; y <= y1; y++) {
		double py = y + 0.5, lo = 1e9, hi = -1e9;
		for (int i = 0; i < 4; i++) {
			const double *a = q[i], *b = q[(i + 1) % 4];
			if ((a[1] <= py && b[1] > py) || (b[1] <= py && a[1] > py)) {
				double x = a[0] + (py - a[1]) * (b[0] - a[0]) / (b[1] - a[1]);
				lo = std::min (lo, x), hi = std::max (hi, x);
			}
		}
		for (int x = std::max (0, (int) ceil (lo - 0.5)); x <= std::min (W - 1, (int) floor (hi - 0.5)); x++)
			pix[(size_t) y * pitch + x] = col;
	}
}

// ---- le cube de vue -----------------------------------------------------------------------------------------------

static const int CUBE = 112, CUBE_M = 12, CUBE_R = 31;
static const double CN[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

void View::cubeRect (int &x, int &y, int &w, int &h) const
{
	x = W - CUBE_M - CUBE, y = CUBE_M, w = CUBE, h = CUBE;
}

// les quatre coins d'une face du cube à l'écran, sa profondeur, et si elle est vue
static bool cubeFace (const Camera &c, int W, int f, double q[4][2], double &depth)
{
	double r[3], u[3], fw[3];
	c.basis (r, u, fw);
	const double *n = CN[f];
	int ax = f / 2, a1 = (ax + 1) % 3, a2 = (ax + 2) % 3;
	double cx = W - CUBE_M - CUBE / 2.0, cy = CUBE_M + CUBE / 2.0;
	static const int S[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
	for (int i = 0; i < 4; i++) {
		double p[3] = { 0, 0, 0 };
		p[ax] = n[ax], p[a1] = S[i][0], p[a2] = S[i][1];
		q[i][0] = cx + (p[0] * r[0] + p[1] * r[1] + p[2] * r[2]) * CUBE_R;
		q[i][1] = cy - (p[0] * u[0] + p[1] * u[1] + p[2] * u[2]) * CUBE_R;
	}
	depth = n[0] * fw[0] + n[1] * fw[1] + n[2] * fw[2];
	return depth < -0.05;
}

int View::cubeFaceAt (int x, int y) const
{
	int best = -1;
	double bd = 1e9;
	for (int f = 0; f < 6; f++) {
		double q[4][2], d;
		if (!cubeFace (cam, W, f, q, d)) continue;
		bool in = true, pos = false, neg = false;
		for (int i = 0; i < 4; i++) {
			const double *a = q[i], *b = q[(i + 1) % 4];
			double c = (b[0] - a[0]) * (y + 0.5 - a[1]) - (b[1] - a[1]) * (x + 0.5 - a[0]);
			pos = pos || c > 0, neg = neg || c < 0;
		}
		in = !(pos && neg);
		if (in && d < bd) bd = d, best = f;
	}
	return best;
}

void View::cubeLabels (int cx[6], int cy[6], bool visible[6]) const
{
	for (int f = 0; f < 6; f++) {
		double q[4][2], d;
		visible[f] = cubeFace (cam, W, f, q, d) && d < -0.35;
		cx[f] = (int) ((q[0][0] + q[1][0] + q[2][0] + q[3][0]) / 4);
		cy[f] = (int) ((q[0][1] + q[1][1] + q[2][1] + q[3][1]) / 4);
	}
}

// ---- le rendu -----------------------------------------------------------------------------------------------------

void View::render (unsigned *pix, int pitch, const Doc &d, const Palette &p, const std::vector<Pick> &sel, const Pick &hot)
{
	auto chosen = [&] (int b, int f, int e) {
		for (auto &k : sel) if (k.body == b && k.face == f && k.edge == e) return true;
		return false;
	};
	auto t0 = std::chrono::steady_clock::now ();
	std::fill (zb.begin (), zb.end (), 1e30f);
	std::fill (fid.begin (), fid.end (), 0);
	for (int y = 0; y < H; y++) std::fill (pix + (size_t) y * pitch, pix + (size_t) y * pitch + W, p.bg);
	rcam = cam, rpersp = persp, rdone = true;
	const Proj P = proj ();
	const double *r = P.r, *u = P.u, *f = P.f;
	auto f2d = [] (const float *a, double o[3]) { o[0] = a[0], o[1] = a[1], o[2] = a[2]; };
	// la grille du plan XY, sous la pièce (sans profondeur : la pièce la recouvre ; en perspective, découpée au plan
	// proche, et bornée comme en orthographique : pas de trait jusqu'à l'horizon)
	if (grid) {
		float lo[2] = { -100, -100 }, hi[2] = { 200, 200 };
		for (auto &b : d.bodies)
			if (b.visible && !b.mesh.pts.empty ()) {
				lo[0] = std::min (lo[0], (float) floor (b.mesh.lo[0] / 50 - 2) * 50);
				lo[1] = std::min (lo[1], (float) floor (b.mesh.lo[1] / 50 - 2) * 50);
				hi[0] = std::max (hi[0], (float) ceil (b.mesh.hi[0] / 50 + 2) * 50);
				hi[1] = std::max (hi[1], (float) ceil (b.mesh.hi[1] / 50 + 2) * 50);
			}
		for (int axis = 0; axis < 2; axis++)
			for (float v = lo[axis]; v <= hi[axis]; v += 10) {
				double a[3], b[3];
				a[axis] = b[axis] = v;
				a[1 - axis] = lo[1 - axis], b[1 - axis] = hi[1 - axis];
				a[2] = b[2] = 0;
				bool major = std::fmod (std::fabs (v), 50.0f) < 0.01f;
				unsigned c = v == 0 ? p.axis[1 - axis] : major ? p.grid50 : p.grid;
				seg (pix, pitch, P, a, b, c, 1, false);
			}
	}
	// les faces
	double L[3] = { -f[0] * 0.8 + u[0] * 0.45 - r[0] * 0.25, -f[1] * 0.8 + u[1] * 0.45 - r[1] * 0.25, -f[2] * 0.8 + u[2] * 0.45 - r[2] * 0.25 };
	double ln = sqrt (L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
	for (double &x : L) x /= ln;
	for (size_t bi = 0; bi < d.bodies.size (); bi++) {
		const Body &b = d.bodies[bi];
		if (!b.visible || b.shape.IsNull ()) continue;
		const Mesh &m = b.mesh;
		unsigned base = bodyColor (b.color, p.dark);
		bool bodySel = chosen ((int) bi, -1, -1);
		bool bodyHot = hot.body == (int) bi && hot.face < 0 && hot.edge < 0;
		std::vector<double> cp (m.pts.size ()), li (m.pts.size () / 3);
		for (size_t i = 0; i < m.pts.size () / 3; i++) {
			const float *q = &m.pts[i * 3];
			P.cam (q, &cp[i * 3]);
			const float *n = &m.nrm[i * 3];
			double dl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
			// (vue de dos ? en perspective, d'après le trait de l'œil au sommet)
			double vd[3] = { f[0], f[1], f[2] };
			if (persp) vd[0] = q[0] - P.o[0], vd[1] = q[1] - P.o[1], vd[2] = q[2] - P.o[2];
			double front = -(n[0] * vd[0] + n[1] * vd[1] + n[2] * vd[2]);
			li[i] = front < 0 ? 0.42 : 0.5 + 0.62 * std::max (0.0, dl);
		}
		for (int fi = 0; fi < m.nfaces; fi++) {
			unsigned col = base;
			if (chosen ((int) bi, fi, -1)) col = mix (base, p.accent, 107);
			else if (hot.body == (int) bi && hot.face == fi) col = mix (base, p.hover, 97);
			else if (bodySel) col = mix (base, p.accent, 60);
			else if (bodyHot) col = mix (base, p.hover, 50);
			int id = (int) (bi << 20) | (fi + 1);
			auto put = [&] (int x, int y, float z, double l) {
				size_t i = (size_t) y * W + x;
				if (z >= zb[i]) return;
				zb[i] = z;
				fid[i] = id;
				pix[(size_t) y * pitch + x] = shade (col, l);
			};
			for (int t = m.faceFirst[fi]; t < m.faceFirst[fi + 1]; t++) {
				double c[3][3], l3[3];
				for (int k = 0; k < 3; k++) {
					int ix = m.tris[t * 3 + k];
					memcpy (c[k], &cp[ix * 3], sizeof c[k]);
					l3[k] = li[ix];
				}
				triClip (P, W, H, c, l3, put);
			}
		}
	}
	// les arêtes
	segs.clear ();
	for (size_t bi = 0; bi < d.bodies.size (); bi++) {
		const Body &b = d.bodies[bi];
		if (!b.visible) continue;
		const Mesh &m = b.mesh;
		unsigned base = mix (bodyColor (b.color, p.dark), p.edge, 184);
		for (int ei = 0; ei < m.nedges; ei++) {
			unsigned col = base;
			int th = 1;
			if (chosen ((int) bi, -1, ei)) col = p.accent, th = 3;
			else if (hot.body == (int) bi && hot.edge == ei) col = p.hover, th = 3;
			else if (!edges) col = 0xFFFFFFFF;
			for (int k = m.edgeFirst[ei]; k + 1 < m.edgeFirst[ei + 1]; k++) {
				double a[3], c[3];
				f2d (&m.edgePts[k * 3], a);
				f2d (&m.edgePts[k * 3 + 3], c);
				seg (pix, pitch, P, a, c, col, th, true, (int) bi, ei);
			}
		}
	}
	// le fantôme : ses faces mêlées à la scène (joindre : cachées par ce qui est devant ; enlever : vues à travers)
	if (ghost && !ghost->pts.empty ()) {
		const Mesh &m = *ghost;
		unsigned gc = ghostCut ? p.danger : p.accent;
		std::vector<double> cp (m.pts.size ());
		for (size_t i = 0; i < m.pts.size () / 3; i++) P.cam (&m.pts[i * 3], &cp[i * 3]);
		const double F = focal ();
		std::vector<unsigned char> hit ((size_t) W * H, 0);	// (chaque pixel une fois : pas de double teinte)
		auto put = [&] (int x, int y, float z, double) {
			size_t i = (size_t) y * W + x;
			if (hit[i]) return;
			if (!ghostCut && z > zb[i] + 0.05f + (persp ? (float) (0.5 * z / F) : 0.0f)) return;	// (½ pixel de marge en perspective)
			hit[i] = 1;
			unsigned &px0 = pix[(size_t) y * pitch + x];
			px0 = mix (px0, gc, ghostCut ? 90 : 110);
		};
		const double none[3] = { 0, 0, 0 };
		for (size_t t = 0; t < m.tris.size () / 3; t++) {
			double c[3][3];
			for (int k = 0; k < 3; k++) memcpy (c[k], &cp[m.tris[t * 3 + k] * 3], sizeof c[k]);
			triClip (P, W, H, c, none, put);
		}
		for (int ei = 0; ei < m.nedges; ei++)
			for (int k = m.edgeFirst[ei]; k + 1 < m.edgeFirst[ei + 1]; k++) {
				double a[3], c[3];
				f2d (&m.edgePts[k * 3], a);
				f2d (&m.edgePts[k * 3 + 3], c);
				seg (pix, pitch, P, a, c, gc, 1, !ghostCut);
			}
	}
	// l'esquisse : par-dessus tout, sans profondeur
	if (dimBodies)
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
				if (fid[(size_t) y * W + x]) {
					unsigned &px0 = pix[(size_t) y * pitch + x];
					px0 = mix (px0, p.bg, 150);
				}
	for (const Stroke &s : strokes)
		for (size_t k = 0; k + 5 < s.pts.size (); k += 3) {
			if (s.dashed && (k / 3) % 2) continue;
			seg (pix, pitch, P, &s.pts[k], &s.pts[k + 3], s.col, s.thick, false);
		}
	for (const Dot &d : dots) {
		double c[3], x, y;
		P.cam (d.p, c);
		if (c[2] < P.nearZ) continue;	// (derrière l'œil)
		P.pix (c, x, y);
		if (x < -50 || y < -50 || x > W + 50 || y > H + 50) continue;
		for (int oy = -d.size; oy <= d.size; oy++)
			for (int ox = -d.size; ox <= d.size; ox++) {
				int X = (int) x + ox, Y = (int) y + oy;
				if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
				if (d.round) {	// (le disque, son bord adouci d'un demi-pixel)
					double r = sqrt ((double) (ox * ox + oy * oy));
					if (r > d.size + 0.5) continue;
					unsigned col = d.hole > 0 && r < d.size - d.hole ? d.holeCol : d.col;
					unsigned &px0 = pix[(size_t) Y * pitch + X];
					px0 = r > d.size - 0.5 ? mix (px0, col, 128) : col;
				} else
					pix[(size_t) Y * pitch + X] = d.col;
			}
	}
	// le trièdre, en bas à gauche
	int ox = 44, oy = H - 44;
	for (int a = 0; a < 3; a++)
		line (pix, pitch, ox, oy, 0, ox + r[a] * 28, oy - u[a] * 28, 0, p.axis[a], 2, false);
	// le cube de vue : ses faces vues, de la plus lointaine à la plus proche
	int order[6] = { 0, 1, 2, 3, 4, 5 };
	double dep[6];
	double q[6][4][2];
	bool vis[6];
	for (int i = 0; i < 6; i++) vis[i] = cubeFace (cam, W, i, q[i], dep[i]);
	std::sort (order, order + 6, [&] (int a, int b) { return dep[a] > dep[b]; });
	for (int k = 0; k < 6; k++) {
		int i = order[k];
		if (!vis[i]) continue;
		quad (pix, pitch, W, H, q[i], i == hotCube ? p.cubeHot : p.cubeFace);
		for (int e = 0; e < 4; e++) {
			const double *a = q[i][e], *b = q[i][(e + 1) % 4];
			line (pix, pitch, a[0], a[1], 0, b[0], b[1], 0, p.cubeLine, 1, false);
		}
	}
	lastMs = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now () - t0).count ();
}

// ---- le choix sous le pointeur ------------------------------------------------------------------------------------

Pick View::pick (const Doc &, int x, int y) const
{
	Pick out;
	if (x < 0 || y < 0 || x >= W || y >= H) return out;
	// une arête vue à moins de 5 pixels l'emporte sur la face
	double best = 5.0 * 5.0;
	for (const Seg &s : segs) {
		double dx = s.x1 - s.x0, dy = s.y1 - s.y0, l2 = dx * dx + dy * dy;
		double t = l2 > 0 ? ((x + 0.5 - s.x0) * dx + (y + 0.5 - s.y0) * dy) / l2 : 0;
		t = std::max (0.0, std::min (1.0, t));
		double px = s.x0 + dx * t, py = s.y0 + dy * t, d2 = (px - x - 0.5) * (px - x - 0.5) + (py - y - 0.5) * (py - y - 0.5);
		if (d2 >= best) continue;
		int ix = (int) px, iy = (int) py;
		if (ix < 0 || iy < 0 || ix >= W || iy >= H) continue;
		float z = rpersp == persp ? zAt (s.z0, s.z1, t) : (float) (s.z0 + (s.z1 - s.z0) * t);
		if (z > zb[(size_t) iy * W + ix] + bias (z)) continue;	// (cachée)
		best = d2;
		out.body = s.body, out.edge = s.edge, out.face = -1;
	}
	if (!out.none ()) return out;
	int id = fid[(size_t) y * W + x];
	if (id) out.body = id >> 20, out.face = (id & 0xFFFFF) - 1;
	return out;
}

// ---- les points sous le pointeur ---------------------------------------------------------------------------------

void View::ray (int x, int y, double o[3], double dir[3]) const
{
	double r[3], u[3], f[3];
	cam.basis (r, u, f);
	double t[3] = { cam.tx, cam.ty, cam.tz };
	if (persp) {	// (de l'œil, par le centre du pixel)
		double F = focal (), a = (x + 0.5 - W / 2.0) / F, b = -(y + 0.5 - H / 2.0) / F;
		eye (o);
		for (int i = 0; i < 3; i++) dir[i] = f[i] + a * r[i] + b * u[i];
		double n = sqrt (dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
		for (int i = 0; i < 3; i++) dir[i] /= n;
		return;
	}
	double a = (x + 0.5 - W / 2.0) / cam.scale, b = -(y + 0.5 - H / 2.0) / cam.scale;
	for (int i = 0; i < 3; i++) o[i] = t[i] + a * r[i] + b * u[i] - f[i] * 10000, dir[i] = f[i];
}

bool View::onPlane (int x, int y, const double p[3], const double n[3], double out[3]) const
{
	double o[3], d[3];
	ray (x, y, o, d);
	double dn = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
	if (std::fabs (dn) < 1e-4) return false;
	double t = ((p[0] - o[0]) * n[0] + (p[1] - o[1]) * n[1] + (p[2] - o[2]) * n[2]) / dn;
	if (persp && (t <= 0 || t > 1e3 * std::max (100.0, dist ()))) return false;	// (le plan derrière l'œil, ou à l'horizon)
	for (int i = 0; i < 3; i++) out[i] = o[i] + d[i] * t;
	return true;
}

void View::toScreen (const double p[3], double &x, double &y) const
{
	const Proj P = proj ();
	double c[3];
	P.cam (p, c);
	if (persp && c[2] < P.nearZ) {	// (derrière l'œil : loin hors de la vue)
		x = y = -1e7;
		return;
	}
	P.pix (c, x, y);
}

// ---- la caméra ----------------------------------------------------------------------------------------------------

void View::orbit (double dyaw, double dpitch)
{
	cam.yaw = std::fmod (cam.yaw + dyaw, 360.0);
	cam.pitch = std::max (-89.5, std::min (89.5, cam.pitch + dpitch));
}

// la profondeur (le long de la vue) de ce qui est sous (x, y) au dernier rendu, si ce rendu vaut encore
static bool depthUnder (const std::vector<float> &zb, int W, int H, int x, int y, double &z)
{
	if (x < 0 || y < 0 || x >= W || y >= H) return false;
	float v = zb[(size_t) y * W + x];
	if (v >= 1e29f) return false;
	z = v;
	return true;
}

void View::panFrom (int x, int y)
{
	double z;
	panZ = persp && sameAsRendered () && depthUnder (zb, W, H, x, y, z) && z > 0 ? z : 0;
}

void View::pan (int dx, int dy)
{
	double r[3], u[3], f[3];
	cam.basis (r, u, f);
	// (mm par pixel : à la profondeur du point pris en perspective, le point visé suit, le point pris reste sous le
	// pointeur ; en orthographique, partout le même)
	double mpp = persp ? (panZ > 0 ? panZ : dist ()) / focal () : 1 / cam.scale;
	double kx = dx * mpp, ky = dy * mpp;
	cam.tx += -kx * r[0] + ky * u[0];
	cam.ty += -kx * r[1] + ky * u[1];
	cam.tz += -kx * r[2] + ky * u[2];
}

void View::zoomAt (int x, int y, double factor)
{
	double r[3], u[3], f[3];
	cam.basis (r, u, f);
	double s0 = cam.scale, s1 = std::max (0.05, std::min (400.0, s0 * factor));
	if (persp) {
		// le point sous le pointeur (la pièce, sinon le plan du point visé face à la vue) reste en place : la caméra
		// entière (œil et point visé) se rapproche de lui dans le rapport s0 / s1
		double F = focal (), a = (x + 0.5 - W / 2.0) / F, b = -(y + 0.5 - H / 2.0) / F, z = dist (), e[3];
		if (!(sameAsRendered () && depthUnder (zb, W, H, x, y, z) && z > 0)) z = dist ();
		eye (e);
		double P[3], t[3] = { cam.tx, cam.ty, cam.tz }, k = s0 / s1;
		for (int i = 0; i < 3; i++) P[i] = e[i] + (f[i] + a * r[i] + b * u[i]) * z;
		cam.tx = P[0] + (t[0] - P[0]) * k;
		cam.ty = P[1] + (t[1] - P[1]) * k;
		cam.tz = P[2] + (t[2] - P[2]) * k;
		cam.scale = s1;
		return;
	}
	double a = (x - W / 2.0) / s0, b = -(y - H / 2.0) / s0, k = 1 - s0 / s1;
	cam.tx += (a * r[0] + b * u[0]) * k;
	cam.ty += (a * r[1] + b * u[1]) * k;
	cam.tz += (a * r[2] + b * u[2]) * k;
	cam.scale = s1;
}

void View::standard (int face)
{
	static const double YP[7][2] = { { 0, 0 }, { 180, 0 }, { 90, 0 }, { -90, 0 }, { -90, 89.5 }, { -90, -89.5 }, { -50, 28 } };
	if (face < 0 || face > 6) return;
	cam.yaw = YP[face][0];
	cam.pitch = YP[face][1];
}

void View::lookAlong (const double n[3])
{
	double l = sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	double z = n[2] / l;
	if (z > 0.995) cam.yaw = -90, cam.pitch = 89.5;
	else if (z < -0.995) cam.yaw = -90, cam.pitch = -89.5;
	else cam.yaw = atan2 (n[1], n[0]) * 180 / M_PI, cam.pitch = asin (z) * 180 / M_PI;
}

void View::fit (const Doc &d)
{
	float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
	for (auto &b : d.bodies)
		if (b.visible && !b.mesh.pts.empty ())
			for (int i = 0; i < 3; i++) lo[i] = std::min (lo[i], b.mesh.lo[i]), hi[i] = std::max (hi[i], b.mesh.hi[i]);
	if (lo[0] > hi[0]) return;
	cam.tx = (lo[0] + hi[0]) / 2, cam.ty = (lo[1] + hi[1]) / 2, cam.tz = (lo[2] + hi[2]) / 2;
	double dx = hi[0] - lo[0], dy = hi[1] - lo[1], dz = hi[2] - lo[2];
	double diag = std::max (1.0, sqrt (dx * dx + dy * dy + dz * dz));
	if (persp) {
		// la sphère autour de la boîte vue sous l'angle qui fait 78 % du petit côté (comme en orthographique) :
		// à la distance R / sin (cet angle), toute la boîte est dans l'image
		double F = focal (), half = atan (std::min (W, H) * 0.39 / F), D = diag / 2 / sin (half);
		cam.scale = std::max (0.05, std::min (400.0, F / D));
		return;
	}
	cam.scale = std::min (W, H) * 0.78 / diag;
}

} // namespace forge
