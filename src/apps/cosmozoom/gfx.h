//
// gfx.h -- a frame for the GPU: the vertices and the batches in GpuKit's layout (GKV_*, GKB_*), built by the game each
// picture, then given to gk_draw (main.cpp), which transforms, clips and draws them, by the V3D or the processor.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef CZ_GFX_H
#define CZ_GFX_H

#include "math3d.h"
#include "mesh.h"

namespace cz {

struct Vtx { float x, y, z, w, s, t; unsigned char r, g, b, a, r2, g2, b2, a2; };
struct Batch { unsigned first, count; int texture; unsigned flags; float m[16]; };
static_assert (sizeof (Vtx) == 32 && sizeof (Batch) == 80, "GKV_SIZE, GKB_SIZE");

enum
{
	B_NOZWRITE = 1 << 3, B_CULL = 1 << 4, B_CULLFRONT = 1 << 5,
	B_ALPHA = 1 << 8, B_ADD = 2 << 8, B_LINEAR = 1 << 12,
	B_CLAMP = (1 << 13) | (1 << 15), B_NOMATRIX = 1 << 17,
	B_ZALWAYS = 7, B_ZLEQUAL = 3
};

static inline void setVtx (Vtx &v, const V3 &p, float s, float t, unsigned rgb, unsigned a = 255, unsigned add = 0)
{
	v.x = p.x; v.y = p.y; v.z = p.z; v.w = 1; v.s = s; v.t = t;
	v.r = (unsigned char) (rgb >> 16); v.g = (unsigned char) (rgb >> 8); v.b = (unsigned char) rgb; v.a = (unsigned char) a;
	v.r2 = (unsigned char) (add >> 16); v.g2 = (unsigned char) (add >> 8); v.b2 = (unsigned char) add; v.a2 = 0;
}

// A list of triangles in world (or model) space, kept from frame to frame (the world's pieces, the models).
struct VList
{
	Arr<Vtx> v;
	V3 centre; float radius = 0;		// a sphere around it (the culling)
	void tri (const V3 &a, const V3 &b, const V3 &c, unsigned ca, unsigned cb, unsigned cc, unsigned alpha = 255)
	{
		v.reserve (v.n + 3);
		Vtx *p = v.push (); if (!p) return; setVtx (*p, a, 0, 0, ca, alpha);
		p = v.push (); setVtx (*p, b, 0, 0, cb, alpha);
		p = v.push (); setVtx (*p, c, 0, 0, cc, alpha);
	}
	void tri (const V3 &a, const V3 &b, const V3 &c, unsigned col) { tri (a, b, c, col, col, col); }
	// a quad a b c d (counter-clockwise from its front)
	void quad (const V3 &a, const V3 &b, const V3 &c, const V3 &d, unsigned col) { tri (a, b, c, col); tri (a, c, d, col); }
	// the same, turned to face n (its winding made counter-clockwise seen from n's side)
	void face (const V3 &a, const V3 &b, const V3 &c, const V3 &d, unsigned col, const V3 &n)
	{
		V3 cn = cross (b - a, c - a) + cross (c - a, d - a);
		if (dot (cn, n) < 0) quad (d, c, b, a, col); else quad (a, b, c, d, col);
	}
	// the same, its a-b edge in colour ca and its c-d edge in cd (a smooth light from one end to the other)
	void face2 (const V3 &a, const V3 &b, const V3 &c, const V3 &d, unsigned ca, unsigned cd, const V3 &n)
	{
		V3 cn = cross (b - a, c - a) + cross (c - a, d - a);
		if (dot (cn, n) < 0) { tri (d, c, b, cd, cd, ca); tri (d, b, a, cd, ca, ca); }
		else { tri (a, b, c, ca, ca, cd); tri (a, c, d, ca, cd, cd); }
	}
	void quadUV (const V3 &a, const V3 &b, const V3 &c, const V3 &d, unsigned col, float s0, float t0, float s1, float t1, unsigned alpha = 255)
	{
		v.reserve (v.n + 6);
		Vtx *p;
		p = v.push (); setVtx (*p, a, s0, t1, col, alpha); p = v.push (); setVtx (*p, b, s1, t1, col, alpha); p = v.push (); setVtx (*p, c, s1, t0, col, alpha);
		p = v.push (); setVtx (*p, a, s0, t1, col, alpha); p = v.push (); setVtx (*p, c, s1, t0, col, alpha); p = v.push (); setVtx (*p, d, s0, t0, col, alpha);
	}
	void mesh (const Mesh &m, const M4 &M)
	{
		v.reserve (v.n + m.tris.n * 3);
		for (int i = 0; i < m.tris.n; i++)
		{
			const Tri &T = m.tris[i];
			for (int k = 0; k < 3; k++) { Vtx *p = v.push (); if (!p) return; setVtx (*p, M.point (T.p[k]), T.s[k], T.t[k], T.col[k], T.alpha); }
		}
	}
	void bound ()
	{
		if (!v.n) { centre = V3 (); radius = 0; return; }
		V3 lo (1e9f, 1e9f, 1e9f), hi (-1e9f, -1e9f, -1e9f);
		for (int i = 0; i < v.n; i++)
		{
			if (v[i].x < lo.x) lo.x = v[i].x; if (v[i].y < lo.y) lo.y = v[i].y; if (v[i].z < lo.z) lo.z = v[i].z;
			if (v[i].x > hi.x) hi.x = v[i].x; if (v[i].y > hi.y) hi.y = v[i].y; if (v[i].z > hi.z) hi.z = v[i].z;
		}
		centre = (lo + hi) * 0.5f; radius = len (hi - lo) * 0.5f;
	}
};

// One picture: the vertices and the batches handed to the GPU at once.
struct Frame
{
	Arr<Vtx> v;
	Arr<Batch> b;
	void clear () { v.clear (); b.clear (); }
	// A new batch: the vertices added from now on, through M, with its texture (the game's number, 0: none)
	Batch *begin (const M4 &M, int tex, unsigned flags)
	{
		Batch *B = b.push (); if (!B) return 0;
		B->first = (unsigned) v.n; B->count = 0; B->texture = tex > 0 ? tex : -1; B->flags = flags;
		memcpy (B->m, M.m, sizeof B->m);
		return B;
	}
	void end () { if (b.n) b.p[b.n - 1].count = (unsigned) v.n - b.p[b.n - 1].first; }
	void add (const VList &L)
	{
		v.reserve (v.n + L.v.n);
		if (v.cap < v.n + L.v.n) return;
		memcpy (v.p + v.n, L.v.p, sizeof (Vtx) * (size_t) L.v.n); v.n += L.v.n;
	}
	static int partOf (const Tri &T) { return (T.flags & TRI_GLOW) ? 2 : (T.flags & TRI_ALPHA) ? 1 : 0; }
	Vtx *room (int k) { v.reserve (v.n + k); if (v.cap < v.n + k) return 0; Vtx *p = v.p + v.n; v.n += k; return p; }
	void tri (const V3 &a, const V3 &b2, const V3 &c, unsigned col, unsigned alpha = 255, unsigned add = 0)
	{
		Vtx *p = room (3); if (!p) return;
		setVtx (p[0], a, 0, 0, col, alpha, add); setVtx (p[1], b2, 0, 0, col, alpha, add); setVtx (p[2], c, 0, 0, col, alpha, add);
	}
	void quad (const V3 &a, const V3 &b2, const V3 &c, const V3 &d, unsigned col, unsigned alpha = 255, float s0 = 0, float t0 = 0, float s1 = 1, float t1 = 1, unsigned add = 0)
	{
		Vtx *p = room (6); if (!p) return;
		setVtx (p[0], a, s0, t1, col, alpha, add); setVtx (p[1], b2, s1, t1, col, alpha, add); setVtx (p[2], c, s1, t0, col, alpha, add);
		setVtx (p[3], a, s0, t1, col, alpha, add); setVtx (p[4], c, s1, t0, col, alpha, add); setVtx (p[5], d, s0, t0, col, alpha, add);
	}
	// A mesh through M (its lit colours; add: added). part: 0 the solid triangles, 1 the see-through ones
	// (glass: an alpha batch), 2 the glowing ones (an additive batch)
	void mesh (const Mesh &m, const M4 &M, unsigned alphaMul = 255, unsigned add = 0, int part = 0)
	{
		int cnt = 0;
		for (int i = 0; i < m.tris.n; i++) if (partOf (m.tris[i]) == part) cnt++;
		Vtx *p = room (cnt * 3); if (!p) return;
		for (int i = 0; i < m.tris.n; i++)
		{
			const Tri &T = m.tris[i];
			if (partOf (T) != part) continue;
			unsigned a = (unsigned) T.alpha * alphaMul / 255;
			for (int k = 0; k < 3; k++) setVtx (*p++, M.point (T.p[k]), T.s[k], T.t[k], T.col[k], a, (T.flags & TRI_OUTLINE) ? 0 : add);
		}
	}
};

// ---- the textures: made by the game (0xAARRGGBB), numbered 1.. ------------------------------------------------
struct TexStore
{
	enum { MAX = 32 };
	unsigned *px[MAX]; int w[MAX], h[MAX]; int n = 0;
	TexStore () { for (int i = 0; i < MAX; i++) { px[i] = 0; w[i] = h[i] = 0; } }
	int add (unsigned *pixels, int W, int H) { if (n + 1 >= MAX) return 0; n++; px[n] = pixels; w[n] = W; h[n] = H; return n; }
};

} // namespace cz

#endif
