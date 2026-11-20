//
// mesh.h -- Morpho : les maillages (sommets indexés, éclairés plus tard par render.cpp) et leurs primitives :
// ellipsoïdes, tubes lissés (membres, cornes, queues), feuilles à deux faces (ailes, voiles), tout dans un repère
// courant (Xf) qui pose la pièce dans le monde.
//
#ifndef MORPHO_MESH_H
#define MORPHO_MESH_H
#include "m3d.h"
#include <vector>

namespace mo {

// une transformation affine : 3 × 3 (colonnes ax, ay, az) puis une translation
struct Xf
{
	V3 ax, ay, az, t;
	Xf () : ax (1, 0, 0), ay (0, 1, 0), az (0, 0, 1), t () {}
	V3 p (const V3 &v) const { return ax * v.x + ay * v.y + az * v.z + t; }
	V3 d (const V3 &v) const { return ax * v.x + ay * v.y + az * v.z; }
	V3 n (const V3 &v) const { return norm (d (v)); }	// (échelle presque uniforme : assez juste pour une normale)
	// une direction du monde -> la pièce (transposée, à l'échelle près)
	V3 inv (const V3 &v) const { return norm (V3 (dot (ax, v), dot (ay, v), dot (az, v))); }
	static Xf trans (const V3 &v) { Xf x; x.t = v; return x; }
	static Xf scale (float sx, float sy, float sz) { Xf x; x.ax = V3 (sx, 0, 0); x.ay = V3 (0, sy, 0); x.az = V3 (0, 0, sz); return x; }
	static Xf rotY (float a) { Xf x; x.ax = mo::rotY (V3 (1, 0, 0), a); x.ay = V3 (0, 1, 0); x.az = mo::rotY (V3 (0, 0, 1), a); return x; }
	static Xf rotX (float a) { Xf x; x.ax = V3 (1, 0, 0); x.ay = mo::rotX (V3 (0, 1, 0), a); x.az = mo::rotX (V3 (0, 0, 1), a); return x; }
	static Xf rotZ (float a) { Xf x; x.ax = mo::rotZ (V3 (1, 0, 0), a); x.ay = mo::rotZ (V3 (0, 1, 0), a); x.az = V3 (0, 0, 1); return x; }
	static Xf axis (const V3 &k, float a) { Xf x; x.ax = rotAxis (V3 (1, 0, 0), k, a); x.ay = rotAxis (V3 (0, 1, 0), k, a); x.az = rotAxis (V3 (0, 0, 1), k, a); return x; }
	static Xf frame (const Frame &f) { Xf x; x.ax = f.x; x.ay = f.y; x.az = f.z; x.t = f.o; return x; }
	// autour d'un pivot
	static Xf about (const V3 &pivot, const Xf &r) { return trans (pivot) * r * trans (-pivot); }
	Xf operator* (const Xf &b) const { Xf r; r.ax = d (b.ax); r.ay = d (b.ay); r.az = d (b.az); r.t = p (b.t); return r; }
};

struct Vtx
{
	V3 p, n;
	Col c;
	float u, v;
	float gloss, emit, alpha;
	unsigned char two;		// deux faces : la normale se tourne vers l'œil
};

enum { MF_CULL = 1, MF_ALPHA = 2, MF_ADD = 4, MF_NOFOG = 8, MF_NOZW = 16, MF_UNLIT = 32 };
struct Mesh
{
	std::vector<Vtx> v;
	std::vector<int> i;
	int tex = -1;
	unsigned flags = MF_CULL;
	void clear () { v.clear (); i.clear (); }
};

struct Builder
{
	Mesh *m = 0;
	Xf xf;
	float gloss = 0.3f, emit = 0, alpha = 1;
	bool two = false;
	Col tint = Col (1, 1, 1);

	explicit Builder (Mesh *mesh = 0) : m (mesh) {}
	int add (const V3 &p, const V3 &n, const Col &c, float u = 0, float v = 0)
	{
		Vtx x;
		x.p = xf.p (p); x.n = xf.n (n); x.c = c * tint; x.u = u; x.v = v;
		x.gloss = gloss; x.emit = emit; x.alpha = alpha; x.two = two;
		m->v.push_back (x);
		return (int) m->v.size () - 1;
	}
	void tri (int a, int b, int c) { m->i.push_back (a); m->i.push_back (b); m->i.push_back (c); }
	// une grille (rows + 1) × (cols + 1), rangée par rangée : faces tournées vers dθ × ds
	void grid (int base, int rows, int cols)
	{
		for (int r = 0; r < rows; r++)
			for (int c = 0; c < cols; c++)
			{
				int a = base + r * (cols + 1) + c, b = a + 1, d = a + cols + 1, e = d + 1;
				tri (a, b, e); tri (a, e, d);
			}
	}

	// un ellipsoïde dans le repère f, rayons r ; couleur de bas (cb) en haut (ct)
	void ellipsoid (const Frame &f, const V3 &r, const Col &ct, const Col &cb, int seg = 14, int rings = 9)
	{
		int base = (int) m->v.size ();
		for (int i = 0; i <= rings; i++)
		{
			float ph = -PI / 2 + PI * (float) i / (float) rings, cp = cosf (ph), sp = sinf (ph);
			Col c = mixc (cb, ct, (sp + 1) * 0.5f);
			for (int j = 0; j <= seg; j++)
			{
				float th = 2 * PI * (float) j / (float) seg;
				V3 l (cp * cosf (th), sp, -cp * sinf (th));
				V3 p = f.at (V3 (l.x * r.x, l.y * r.y, l.z * r.z));
				V3 n = f.dir (V3 (l.x / r.x, l.y / r.y, l.z / r.z));
				add (p, n, c, (float) j / (float) seg, (float) i / (float) rings);
			}
		}
		grid (base, rings, seg);
	}
	void sphere (const V3 &o, float r, const Col &c, int seg = 12, int rings = 8)
	{
		Frame f; f.o = o;
		ellipsoid (f, V3 (r, r, r), c, c, seg, rings);
	}

	// un tube lisse par les points p (Catmull-Rom, sub pas par segment), rayons r, couleurs c ; flat : section aplatie
	// dans la direction de ref (0 : ronde) ; caps : bouts arrondis (1 au début, 2 à la fin)
	void tube (const V3 *p, const float *r, const Col *c, int n, int seg, int sub, float flat = 0, V3 ref = V3 (0, 1, 0), int caps = 3)
	{
		if (n < 2) return;
		std::vector<V3> P; std::vector<float> R; std::vector<Col> C;
		for (int k = 0; k < n - 1; k++)
		{
			V3 p0 = p[k > 0 ? k - 1 : 0], p1 = p[k], p2 = p[k + 1], p3 = p[k + 2 < n ? k + 2 : n - 1];
			for (int s = 0; s < sub; s++)
			{
				float t = (float) s / (float) sub, t2 = t * t, t3 = t2 * t;
				V3 q = (p1 * 2 + (p2 - p0) * t + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * t2 + (p1 * 3 - p0 - p2 * 3 + p3) * t3) * 0.5f;
				P.push_back (q);
				float rr0 = r[k > 0 ? k - 1 : 0], rr1 = r[k], rr2 = r[k + 1], rr3 = r[k + 2 < n ? k + 2 : n - 1];
				float rq = 0.5f * (2 * rr1 + (rr2 - rr0) * t + (2 * rr0 - 5 * rr1 + 4 * rr2 - rr3) * t2 + (3 * rr1 - rr0 - 3 * rr2 + rr3) * t3);
				R.push_back (fmaxf (rq, 0.0005f));
				C.push_back (mixc (c[k], c[k + 1], t));
			}
		}
		P.push_back (p[n - 1]); R.push_back (fmaxf (r[n - 1], 0.0005f)); C.push_back (c[n - 1]);
		int N = (int) P.size ();
		// les repères, transportés le long du tube
		std::vector<V3> T (N), A (N), B (N);
		for (int k = 0; k < N; k++) T[k] = norm (P[k < N - 1 ? k + 1 : k] - P[k > 0 ? k - 1 : k]);
		V3 a = cross (ref, T[0]);
		if (len (a) < 1e-4f) a = cross (V3 (1, 0, 0), T[0]);
		A[0] = norm (a); B[0] = cross (T[0], A[0]);
		for (int k = 1; k < N; k++)
		{
			V3 aa = A[k - 1] - T[k] * dot (A[k - 1], T[k]);
			A[k] = norm (aa); B[k] = cross (T[k], A[k]);
		}
		// (B : la direction aplatie ; anneaux des bouts en plus)
		struct Ring { V3 c, t, a, b; float r, sc, off; Col col; float nt; };
		std::vector<Ring> rings;
		const int CAP = 3;
		if (caps & 1)
			for (int q = CAP; q >= 1; q--)
			{
				float ph = (PI / 2) * (float) q / (float) CAP;
				Ring g = { P[0], T[0], A[0], B[0], R[0], cosf (ph), -sinf (ph), C[0], -sinf (ph) };
				rings.push_back (g);
			}
		for (int k = 0; k < N; k++)
		{
			float dr = (R[k < N - 1 ? k + 1 : k] - R[k > 0 ? k - 1 : k]) / fmaxf (len (P[k < N - 1 ? k + 1 : k] - P[k > 0 ? k - 1 : k]), 1e-4f);
			Ring g = { P[k], T[k], A[k], B[k], R[k], 1, 0, C[k], -clampf (dr, -3, 3) };
			rings.push_back (g);
		}
		if (caps & 2)
			for (int q = 1; q <= CAP; q++)
			{
				float ph = (PI / 2) * (float) q / (float) CAP;
				Ring g = { P[N - 1], T[N - 1], A[N - 1], B[N - 1], R[N - 1], cosf (ph), sinf (ph), C[N - 1], sinf (ph) };
				rings.push_back (g);
			}
		int base = (int) m->v.size ();
		float fb = 1 - flat;
		for (size_t k = 0; k < rings.size (); k++)
		{
			const Ring &g = rings[k];
			bool cap = g.off != 0;
			for (int j = 0; j <= seg; j++)
			{
				float th = 2 * PI * (float) j / (float) seg, ct = cosf (th), st = sinf (th);
				V3 rad = g.a * st + g.b * (ct * fb);
				V3 pos = g.c + rad * (g.r * g.sc) + g.t * (g.off * g.r);
				V3 en = norm (g.a * st + g.b * (fb > 0.05f ? ct / fb : ct * 20));
				V3 nn = cap ? norm (en * g.sc + g.t * g.nt) : norm (en + g.t * g.nt);
				add (pos, nn, g.col, (float) k / (float) (rings.size () - 1), (float) j / (float) seg);
			}
		}
		// (sens : dθ × ds vers l'extérieur ; ici a × t = -b : le tour est pris à l'envers)
		int rows = (int) rings.size () - 1;
		for (int rr = 0; rr < rows; rr++)
			for (int cc = 0; cc < seg; cc++)
			{
				int i0 = base + rr * (seg + 1) + cc, i1 = i0 + 1, i2 = i0 + seg + 1, i3 = i2 + 1;
				tri (i0, i3, i1); tri (i0, i2, i3);
			}
	}

	// une feuille entre deux bords a[] et b[] (n points chacun), à deux faces
	void sheet (const V3 *a, const V3 *b, const Col *ca, const Col *cb, int n)
	{
		bool was = two; two = true;
		int base = (int) m->v.size ();
		for (int k = 0; k < n; k++)
		{
			V3 along = (k < n - 1 ? a[k + 1] - a[k] : a[k] - a[k - 1]);
			V3 across = b[k] - a[k];
			V3 nn = norm (cross (along, across));
			add (a[k], nn, ca[k], 0, (float) k / (float) (n - 1));
			add (b[k], nn, cb[k], 1, (float) k / (float) (n - 1));
		}
		for (int k = 0; k < n - 1; k++)
		{
			int i0 = base + k * 2;
			tri (i0, i0 + 1, i0 + 3); tri (i0, i0 + 3, i0 + 2);
		}
		two = was;
	}
};

}	// namespace mo
#endif
