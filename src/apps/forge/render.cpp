// render.cpp -- Forge : la vue 3D par le processeur (render.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "render.h"

#include <algorithm>
#include <chrono>
#include <cmath>

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

void View::project (const float *p, double &sx, double &sy, double &sz) const
{
	double r[3], u[3], f[3];
	cam.basis (r, u, f);
	double q[3] = { p[0] - cam.tx, p[1] - cam.ty, p[2] - cam.tz };
	sx = W / 2.0 + (q[0] * r[0] + q[1] * r[1] + q[2] * r[2]) * cam.scale;
	sy = H / 2.0 - (q[0] * u[0] + q[1] * u[1] + q[2] * u[2]) * cam.scale;
	sz = q[0] * f[0] + q[1] * f[1] + q[2] * f[2];
}

// un triangle (x, y, profondeur par sommet), éclairage par sommet, couleur ; id -> tampon des faces
void View::tri (unsigned *pix, int pitch, const double v[3][3], const double li[3], unsigned col, int id)
{
	double area = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[1][1] - v[0][1]) * (v[2][0] - v[0][0]);
	if (std::fabs (area) < 1e-9) return;
	int x0 = std::max (0, (int) floor (std::min ({ v[0][0], v[1][0], v[2][0] })));
	int x1 = std::min (W - 1, (int) ceil (std::max ({ v[0][0], v[1][0], v[2][0] })));
	int y0 = std::max (0, (int) floor (std::min ({ v[0][1], v[1][1], v[2][1] })));
	int y1 = std::min (H - 1, (int) ceil (std::max ({ v[0][1], v[1][1], v[2][1] })));
	double inv = 1 / area;
	for (int y = y0; y <= y1; y++) {
		double py = y + 0.5;
		for (int x = x0; x <= x1; x++) {
			double px = x + 0.5;
			double w0 = ((v[1][0] - px) * (v[2][1] - py) - (v[1][1] - py) * (v[2][0] - px)) * inv;
			double w1 = ((v[2][0] - px) * (v[0][1] - py) - (v[2][1] - py) * (v[0][0] - px)) * inv;
			double w2 = 1 - w0 - w1;
			if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
			float z = (float) (w0 * v[0][2] + w1 * v[1][2] + w2 * v[2][2]);
			size_t i = (size_t) y * W + x;
			if (z >= zb[i]) continue;
			zb[i] = z;
			fid[i] = id;
			pix[(size_t) y * pitch + x] = shade (col, w0 * li[0] + w1 * li[1] + w2 * li[2]);
		}
	}
}

// un trait de (x0, y0, z0) à (x1, y1, z1), épaisseur en pixels ; depth : caché par ce qui est devant
void View::line (unsigned *pix, int pitch, double x0, double y0, double z0, double x1, double y1, double z1, unsigned col, int thick, bool depth)
{
	double dx = x1 - x0, dy = y1 - y0;
	int n = (int) std::max (std::fabs (dx), std::fabs (dy)) + 1;
	if (n > 8000) return;			// (un trait hors de toute mesure : derrière la caméra)
	float bias = (float) (1.5 / cam.scale + 0.02);
	int a = -(thick - 1) / 2, b = thick / 2;
	for (int k = 0; k <= n; k++) {
		double t = (double) k / n;
		int x = (int) floor (x0 + dx * t + 0.5), y = (int) floor (y0 + dy * t + 0.5);
		float z = (float) (z0 + (z1 - z0) * t);
		for (int oy = a; oy <= b; oy++)
			for (int ox = a; ox <= b; ox++) {
				int X = x + ox, Y = y + oy;
				if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
				size_t i = (size_t) Y * W + X;
				if (depth && z > zb[i] + bias) continue;
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
	double r[3], u[3], f[3];
	cam.basis (r, u, f);
	// la grille du plan XY, sous la pièce (sans profondeur : la pièce la recouvre)
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
				float a[3], b[3];
				a[axis] = b[axis] = v;
				a[1 - axis] = lo[1 - axis], b[1 - axis] = hi[1 - axis];
				a[2] = b[2] = 0;
				double ax, ay, az, bx, by, bz;
				project (a, ax, ay, az);
				project (b, bx, by, bz);
				bool major = std::fmod (std::fabs (v), 50.0f) < 0.01f;
				unsigned c = v == 0 ? p.axis[1 - axis] : major ? p.grid50 : p.grid;
				line (pix, pitch, ax, ay, az, bx, by, bz, c, 1, false);
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
		std::vector<double> sp (m.pts.size ()), li (m.pts.size () / 3);
		for (size_t i = 0; i < m.pts.size () / 3; i++) {
			project (&m.pts[i * 3], sp[i * 3], sp[i * 3 + 1], sp[i * 3 + 2]);
			const float *n = &m.nrm[i * 3];
			double dl = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
			double front = -(n[0] * f[0] + n[1] * f[1] + n[2] * f[2]);
			li[i] = front < 0 ? 0.42 : 0.5 + 0.62 * std::max (0.0, dl);
		}
		for (int fi = 0; fi < m.nfaces; fi++) {
			unsigned col = base;
			if (chosen ((int) bi, fi, -1)) col = mix (base, p.accent, 107);
			else if (hot.body == (int) bi && hot.face == fi) col = mix (base, p.hover, 97);
			else if (bodySel) col = mix (base, p.accent, 60);
			else if (bodyHot) col = mix (base, p.hover, 50);
			int id = (int) (bi << 20) | (fi + 1);
			for (int t = m.faceFirst[fi]; t < m.faceFirst[fi + 1]; t++) {
				double v[3][3], l3[3];
				for (int k = 0; k < 3; k++) {
					int ix = m.tris[t * 3 + k];
					v[k][0] = sp[ix * 3], v[k][1] = sp[ix * 3 + 1], v[k][2] = sp[ix * 3 + 2];
					l3[k] = li[ix];
				}
				tri (pix, pitch, v, l3, col, id);
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
				double ax, ay, az, bx, by, bz;
				project (&m.edgePts[k * 3], ax, ay, az);
				project (&m.edgePts[k * 3 + 3], bx, by, bz);
				segs.push_back ({ (float) ax, (float) ay, (float) az, (float) bx, (float) by, (float) bz, (int) bi, ei });
				if (col != 0xFFFFFFFF) line (pix, pitch, ax, ay, az, bx, by, bz, col, th, true);
			}
		}
	}
	// le fantôme : ses faces mêlées à la scène (joindre : cachées par ce qui est devant ; enlever : vues à travers)
	if (ghost && !ghost->pts.empty ()) {
		const Mesh &m = *ghost;
		unsigned gc = ghostCut ? p.danger : p.accent;
		std::vector<double> sp (m.pts.size ());
		for (size_t i = 0; i < m.pts.size () / 3; i++) project (&m.pts[i * 3], sp[i * 3], sp[i * 3 + 1], sp[i * 3 + 2]);
		std::vector<unsigned char> hit ((size_t) W * H, 0);	// (chaque pixel une fois : pas de double teinte)
		for (size_t t = 0; t < m.tris.size () / 3; t++) {
			double v[3][3];
			for (int k = 0; k < 3; k++) {
				int ix = m.tris[t * 3 + k];
				v[k][0] = sp[ix * 3], v[k][1] = sp[ix * 3 + 1], v[k][2] = sp[ix * 3 + 2];
			}
			double area = (v[1][0] - v[0][0]) * (v[2][1] - v[0][1]) - (v[1][1] - v[0][1]) * (v[2][0] - v[0][0]);
			if (std::fabs (area) < 1e-9) continue;
			int x0 = std::max (0, (int) floor (std::min ({ v[0][0], v[1][0], v[2][0] })));
			int x1 = std::min (W - 1, (int) ceil (std::max ({ v[0][0], v[1][0], v[2][0] })));
			int y0 = std::max (0, (int) floor (std::min ({ v[0][1], v[1][1], v[2][1] })));
			int y1 = std::min (H - 1, (int) ceil (std::max ({ v[0][1], v[1][1], v[2][1] })));
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++) {
					double px = x + 0.5, py = y + 0.5;
					double w0 = ((v[1][0] - px) * (v[2][1] - py) - (v[1][1] - py) * (v[2][0] - px)) / area;
					double w1 = ((v[2][0] - px) * (v[0][1] - py) - (v[2][1] - py) * (v[0][0] - px)) / area;
					double w2 = 1 - w0 - w1;
					if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) continue;
					size_t i = (size_t) y * W + x;
					if (hit[i]) continue;
					float z = (float) (w0 * v[0][2] + w1 * v[1][2] + w2 * v[2][2]);
					if (!ghostCut && z > zb[i] + 0.05f) continue;
					hit[i] = 1;
					unsigned &px0 = pix[(size_t) y * pitch + x];
					px0 = mix (px0, gc, ghostCut ? 90 : 110);
				}
		}
		for (int ei = 0; ei < m.nedges; ei++)
			for (int k = m.edgeFirst[ei]; k + 1 < m.edgeFirst[ei + 1]; k++) {
				double ax, ay, az, bx, by, bz;
				project (&m.edgePts[k * 3], ax, ay, az);
				project (&m.edgePts[k * 3 + 3], bx, by, bz);
				line (pix, pitch, ax, ay, az, bx, by, bz, gc, 1, !ghostCut);
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
			float a[3] = { (float) s.pts[k], (float) s.pts[k + 1], (float) s.pts[k + 2] }, b[3] = { (float) s.pts[k + 3], (float) s.pts[k + 4], (float) s.pts[k + 5] };
			double ax, ay, az, bx, by, bz;
			project (a, ax, ay, az);
			project (b, bx, by, bz);
			line (pix, pitch, ax, ay, az, bx, by, bz, s.col, s.thick, false);
		}
	for (const Dot &d : dots) {
		float a[3] = { (float) d.p[0], (float) d.p[1], (float) d.p[2] };
		double x, y, z;
		project (a, x, y, z);
		for (int oy = -d.size; oy <= d.size; oy++)
			for (int ox = -d.size; ox <= d.size; ox++) {
				int X = (int) x + ox, Y = (int) y + oy;
				if (X >= 0 && Y >= 0 && X < W && Y < H) pix[(size_t) Y * pitch + X] = d.col;
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
	float bias = (float) (1.5 / cam.scale + 0.02);
	for (const Seg &s : segs) {
		double dx = s.x1 - s.x0, dy = s.y1 - s.y0, l2 = dx * dx + dy * dy;
		double t = l2 > 0 ? ((x + 0.5 - s.x0) * dx + (y + 0.5 - s.y0) * dy) / l2 : 0;
		t = std::max (0.0, std::min (1.0, t));
		double px = s.x0 + dx * t, py = s.y0 + dy * t, d2 = (px - x - 0.5) * (px - x - 0.5) + (py - y - 0.5) * (py - y - 0.5);
		if (d2 >= best) continue;
		int ix = (int) px, iy = (int) py;
		if (ix < 0 || iy < 0 || ix >= W || iy >= H) continue;
		float z = (float) (s.z0 + (s.z1 - s.z0) * t);
		if (z > zb[(size_t) iy * W + ix] + bias) continue;	// (cachée)
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
	double a = (x + 0.5 - W / 2.0) / cam.scale, b = -(y + 0.5 - H / 2.0) / cam.scale;
	double t[3] = { cam.tx, cam.ty, cam.tz };
	for (int i = 0; i < 3; i++) o[i] = t[i] + a * r[i] + b * u[i] - f[i] * 10000, dir[i] = f[i];
}

bool View::onPlane (int x, int y, const double p[3], const double n[3], double out[3]) const
{
	double o[3], d[3];
	ray (x, y, o, d);
	double dn = d[0] * n[0] + d[1] * n[1] + d[2] * n[2];
	if (std::fabs (dn) < 1e-4) return false;
	double t = ((p[0] - o[0]) * n[0] + (p[1] - o[1]) * n[1] + (p[2] - o[2]) * n[2]) / dn;
	for (int i = 0; i < 3; i++) out[i] = o[i] + d[i] * t;
	return true;
}

void View::toScreen (const double p[3], double &x, double &y) const
{
	float q[3] = { (float) p[0], (float) p[1], (float) p[2] };
	double z;
	project (q, x, y, z);
}

// ---- la caméra ----------------------------------------------------------------------------------------------------

void View::orbit (double dyaw, double dpitch)
{
	cam.yaw = std::fmod (cam.yaw + dyaw, 360.0);
	cam.pitch = std::max (-89.5, std::min (89.5, cam.pitch + dpitch));
}

void View::pan (int dx, int dy)
{
	double r[3], u[3], f[3];
	cam.basis (r, u, f);
	double kx = dx / cam.scale, ky = dy / cam.scale;
	cam.tx += -kx * r[0] + ky * u[0];
	cam.ty += -kx * r[1] + ky * u[1];
	cam.tz += -kx * r[2] + ky * u[2];
}

void View::zoomAt (int x, int y, double factor)
{
	double r[3], u[3], f[3];
	cam.basis (r, u, f);
	double s0 = cam.scale, s1 = std::max (0.05, std::min (400.0, s0 * factor));
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
	cam.scale = std::min (W, H) * 0.78 / diag;
}

} // namespace forge
