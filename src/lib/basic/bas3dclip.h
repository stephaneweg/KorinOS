//
// bas3dclip.h -- a frame made well-formed for the GPU: GpuKit does not clip (docs/V3D.md: a vertex out of the guard band
// refuses the whole frame; extreme coordinates would block the V3D's binner), Onyx's kernel did (kern/v3d_clip.h,
// ClipFrame in sys/v3d.cpp). Each triangle is transformed by its batch's matrix and clipped, in clip space, against
// the near plane (z >= -w, w > 0) and a guard band 4 x the screen; the batches become NOMATRIX.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _bas3dclip_h
#define _bas3dclip_h

#include "bas3d.h"

namespace bas {

enum { G3_CLIP_MAX_VERTS = 49152 };		// (GK_MAX_VERTS)
#define G3_GUARD	4.0f
#define G3_EPS		1e-5f

static inline float g3ClipDist (const G3Vertex &v, int plane)
{
	switch (plane)
	{
	case 0:  return v.z + v.w;				// near
	case 1:  return v.w - G3_EPS;				// in front of the eye
	case 2:  return G3_GUARD * v.w - v.x;
	case 3:  return G3_GUARD * v.w + v.x;
	case 4:  return G3_GUARD * v.w - v.y;
	default: return G3_GUARD * v.w + v.y;
	}
}
static inline unsigned char g3Lerp8 (unsigned char a, unsigned char b, float t) { float v = (float) a + ((float) b - (float) a) * t; return (unsigned char) (v <= 0 ? 0 : v >= 255 ? 255 : (int) (v + 0.5f)); }
static inline void g3Lerp (G3Vertex &o, const G3Vertex &a, const G3Vertex &b, float t)
{
	o.x = a.x + (b.x - a.x) * t; o.y = a.y + (b.y - a.y) * t; o.z = a.z + (b.z - a.z) * t; o.w = a.w + (b.w - a.w) * t;
	o.s = a.s + (b.s - a.s) * t; o.t = a.t + (b.t - a.t) * t;
	o.r = g3Lerp8 (a.r, b.r, t); o.g = g3Lerp8 (a.g, b.g, t); o.b = g3Lerp8 (a.b, b.b, t); o.a = g3Lerp8 (a.a, b.a, t);
	o.r2 = g3Lerp8 (a.r2, b.r2, t); o.g2 = g3Lerp8 (a.g2, b.g2, t); o.b2 = g3Lerp8 (a.b2, b.b2, t); o.a2 = g3Lerp8 (a.a2, b.a2, t);
}

// One triangle (clip space) -> its clipped fan into out (room for 21 vertices): how many written.
static inline unsigned g3ClipTriangle (const G3Vertex *in, G3Vertex *out)
{
	bool inside = true;
	for (int p = 0; p < 6 && inside; p++)
		for (int k = 0; k < 3; k++) if (!(g3ClipDist (in[k], p) >= 0)) { inside = false; break; }	// (NaN: not inside)
	if (inside) { out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; return 3; }
	G3Vertex A[10], B[10]; unsigned nA = 3;
	A[0] = in[0]; A[1] = in[1]; A[2] = in[2];
	for (int p = 0; p < 6 && nA >= 3; p++)
	{
		unsigned nB = 0;
		for (unsigned k = 0; k < nA; k++)
		{
			const G3Vertex &a = A[k], &b = A[(k + 1) % nA];
			float da = g3ClipDist (a, p), db = g3ClipDist (b, p);
			bool ina = da >= 0, inb = db >= 0;		// (NaN: out)
			if (ina && nB < 10) B[nB++] = a;
			if (ina != inb && nB < 10 && da == da && db == db)
			{
				G3Vertex &o = B[nB++];
				g3Lerp (o, a, b, da / (da - db));
				switch (p)				// exactly on the plane (float rounding)
				{
				case 0:  o.z = -o.w; break;
				case 1:  if (o.w < G3_EPS) o.w = G3_EPS; break;
				case 2:  o.x = G3_GUARD * o.w; break;
				case 3:  o.x = -G3_GUARD * o.w; break;
				case 4:  o.y = G3_GUARD * o.w; break;
				default: o.y = -G3_GUARD * o.w; break;
				}
			}
		}
		for (unsigned k = 0; k < nB; k++) A[k] = B[k];
		nA = nB;
	}
	if (nA < 3) return 0;
	unsigned n = 0;
	for (unsigned k = 1; k + 1 < nA && n + 3 <= 21; k++) { out[n++] = A[0]; out[n++] = A[k]; out[n++] = A[k + 1]; }
	return n;
}

// The frame's batches with their triangles transformed and clipped (NOMATRIX) -> false: out of memory.
// The result stays in *ov, *ob (grown as needed), its sizes in *on (the vertices). More than the GPU takes: dropped.
struct G3GpuFrame { G3Vertex *v = 0; G3Batch *b = 0; int capV = 0, capB = 0; };
static inline bool g3ClipFrame (G3GpuFrame &c, const G3Vertex *pv, int nv, const G3Batch *pb, int nb, int *on)
{
	int cap = nv * 2 + 64;
	if (cap > G3_CLIP_MAX_VERTS) cap = G3_CLIP_MAX_VERTS;
	if (c.capV < cap) { delete [] c.v; c.v = new G3Vertex[cap]; c.capV = cap; }
	if (c.capB < nb + 1) { delete [] c.b; c.b = new G3Batch[nb + 1]; c.capB = nb + 1; }
	if (!c.v || !c.b) return false;
	static const float Ident[16] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 };
	int n = 0;
	for (int i = 0; i < nb; i++)
	{
		const G3Batch &b = pb[i];
		G3Batch &o = c.b[i];
		o = b; o.flags |= G3_NOMATRIX; o.first = (unsigned) n;
		for (int k = 0; k < 16; k++) o.m[k] = Ident[k];
		const float *M = (b.flags & G3_NOMATRIX) ? Ident : b.m;
		for (unsigned t = 0; t + 3 <= b.count && n + 21 <= cap; t += 3)
		{
			G3Vertex T[3], Out[21];
			for (int k = 0; k < 3; k++)
			{
				const G3Vertex &p = pv[b.first + t + k];
				G3Vertex &q = T[k];
				q = p;
				q.x = M[0] * p.x + M[1] * p.y + M[2] * p.z + M[3] * p.w;
				q.y = M[4] * p.x + M[5] * p.y + M[6] * p.z + M[7] * p.w;
				q.z = M[8] * p.x + M[9] * p.y + M[10] * p.z + M[11] * p.w;
				q.w = M[12] * p.x + M[13] * p.y + M[14] * p.z + M[15] * p.w;
			}
			unsigned k = g3ClipTriangle (T, Out);
			for (unsigned j = 0; j < k; j++) c.v[n++] = Out[j];
		}
		o.count = (unsigned) n - o.first;
	}
	*on = n;
	return true;
}

} // namespace bas

#endif
