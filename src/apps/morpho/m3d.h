//
// m3d.h -- Morpho : vecteurs, matrices (ligne par ligne, comme gk_batch.m), hasard reproductible, couleurs.
//
#ifndef MORPHO_M3D_H
#define MORPHO_M3D_H
#include <math.h>
#include <stdint.h>
#include <string.h>

namespace mo {

static const float PI = 3.14159265f;

struct V3
{
	float x, y, z;
	V3 () : x (0), y (0), z (0) {}
	V3 (float a, float b, float c) : x (a), y (b), z (c) {}
	V3 operator+ (const V3 &o) const { return V3 (x + o.x, y + o.y, z + o.z); }
	V3 operator- (const V3 &o) const { return V3 (x - o.x, y - o.y, z - o.z); }
	V3 operator* (float k) const { return V3 (x * k, y * k, z * k); }
	V3 operator- () const { return V3 (-x, -y, -z); }
	V3 &operator+= (const V3 &o) { x += o.x; y += o.y; z += o.z; return *this; }
	V3 &operator-= (const V3 &o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
	V3 &operator*= (float k) { x *= k; y *= k; z *= k; return *this; }
};
static inline float dot (const V3 &a, const V3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross (const V3 &a, const V3 &b) { return V3 (a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float len (const V3 &a) { return sqrtf (dot (a, a)); }
static inline V3 norm (const V3 &a) { float l = len (a); return l > 1e-8f ? a * (1.0f / l) : V3 (0, 1, 0); }
static inline V3 mul (const V3 &a, const V3 &b) { return V3 (a.x * b.x, a.y * b.y, a.z * b.z); }
static inline V3 lerp (const V3 &a, const V3 &b, float t) { return a + (b - a) * t; }
static inline float lerpf (float a, float b, float t) { return a + (b - a) * t; }
static inline float clampf (float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float sat (float v) { return clampf (v, 0, 1); }
static inline float smooth (float e0, float e1, float x) { float t = sat ((x - e0) / (e1 - e0)); return t * t * (3 - 2 * t); }
static inline int clampi (int v, int a, int b) { return v < a ? a : v > b ? b : v; }

// rotations autour des axes (angle en radians)
static inline V3 rotY (const V3 &p, float a) { float c = cosf (a), s = sinf (a); return V3 (c * p.x + s * p.z, p.y, -s * p.x + c * p.z); }
static inline V3 rotX (const V3 &p, float a) { float c = cosf (a), s = sinf (a); return V3 (p.x, c * p.y - s * p.z, s * p.y + c * p.z); }
static inline V3 rotZ (const V3 &p, float a) { float c = cosf (a), s = sinf (a); return V3 (c * p.x - s * p.y, s * p.x + c * p.y, p.z); }
// autour d'un axe unitaire quelconque (Rodrigues)
static inline V3 rotAxis (const V3 &p, const V3 &k, float a)
{
	float c = cosf (a), s = sinf (a);
	return p * c + cross (k, p) * s + k * (dot (k, p) * (1 - c));
}

// un repère : trois axes et une origine (pièces posées sur le corps)
struct Frame
{
	V3 o, x, y, z;		// origine ; x à droite, y en haut, z devant
	Frame () : o (), x (1, 0, 0), y (0, 1, 0), z (0, 0, 1) {}
	V3 at (const V3 &p) const { return o + x * p.x + y * p.y + z * p.z; }
	V3 dir (const V3 &d) const { return x * d.x + y * d.y + z * d.z; }
};
// un repère dont z suit d, y le plus près possible de up
static inline Frame frameLook (const V3 &o, const V3 &d, const V3 &up)
{
	Frame f; f.o = o; f.z = norm (d);
	V3 x = cross (up, f.z);
	if (len (x) < 1e-4f) x = cross (V3 (1, 0, 0), f.z);
	f.x = norm (x); f.y = cross (f.z, f.x);
	return f;
}

struct M4
{
	float m[16];
	static M4 id () { M4 r; memset (r.m, 0, sizeof r.m); r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1; return r; }
	M4 operator* (const M4 &b) const
	{
		M4 r;
		for (int i = 0; i < 4; i++)
			for (int j = 0; j < 4; j++)
			{
				float s = 0;
				for (int k = 0; k < 4; k++) s += m[i * 4 + k] * b.m[k * 4 + j];
				r.m[i * 4 + j] = s;
			}
		return r;
	}
	// point -> espace de découpe (x, y, z, w)
	void apply (const V3 &p, float out[4]) const
	{
		for (int i = 0; i < 4; i++) out[i] = m[i * 4] * p.x + m[i * 4 + 1] * p.y + m[i * 4 + 2] * p.z + m[i * 4 + 3];
	}
};
static inline M4 perspective (float fovy, float aspect, float n, float f)
{
	M4 r; memset (r.m, 0, sizeof r.m);
	float k = 1.0f / tanf (fovy * 0.5f);
	r.m[0] = k / aspect; r.m[5] = k;
	r.m[10] = (f + n) / (n - f); r.m[11] = 2 * f * n / (n - f);
	r.m[14] = -1;
	return r;
}
static inline M4 lookAt (const V3 &eye, const V3 &at, const V3 &up)
{
	V3 f = norm (at - eye), s = norm (cross (f, up)), u = cross (s, f);
	M4 r = M4::id ();
	r.m[0] = s.x; r.m[1] = s.y; r.m[2] = s.z; r.m[3] = -dot (s, eye);
	r.m[4] = u.x; r.m[5] = u.y; r.m[6] = u.z; r.m[7] = -dot (u, eye);
	r.m[8] = -f.x; r.m[9] = -f.y; r.m[10] = -f.z; r.m[11] = dot (f, eye);
	return r;
}

// ---- le hasard : reproductible, par graine ---------------------------------------------------------------------------
struct Rng
{
	uint32_t s;
	explicit Rng (uint32_t seed = 1) { s = seed ^ 0x9E3779B9u; s ^= s >> 16; s *= 0x7FEB352Du; s ^= s >> 15; s *= 0x846CA68Bu; s ^= s >> 16; if (!s) s = 1; }
	uint32_t next () { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
	float f () { return (float) (next () >> 8) * (1.0f / 16777216.0f); }		// [0, 1)
	float range (float a, float b) { return a + (b - a) * f (); }
	int irange (int a, int b) { return a + (int) (next () % (uint32_t) (b - a + 1)); }	// [a, b]
	bool chance (float p) { return f () < p; }
	// un tirage pondéré : w[n] -> l'indice
	int pick (const float *w, int n)
	{
		float t = 0;
		for (int i = 0; i < n; i++) t += w[i];
		float r = f () * t;
		for (int i = 0; i < n; i++) { if (r < w[i]) return i; r -= w[i]; }
		return n - 1;
	}
};
static inline uint32_t hash32 (uint32_t x)
{
	x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
	return x;
}
static inline float hashf (int a, int b, int c = 0)
{
	return (float) (hash32 ((uint32_t) a * 73856093u ^ (uint32_t) b * 19349663u ^ (uint32_t) c * 83492791u) >> 8) * (1.0f / 16777216.0f);
}
// un bruit lisse de valeur, 2D (périodique en x de période px si px > 0)
static inline float vnoise (float x, float y, int seed, int px = 0)
{
	int ix = (int) floorf (x), iy = (int) floorf (y);
	float fx = x - (float) ix, fy = y - (float) iy;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	int x0 = ix, x1 = ix + 1;
	if (px > 0) { x0 = ((x0 % px) + px) % px; x1 = ((x1 % px) + px) % px; }
	float a = hashf (x0, iy, seed), b = hashf (x1, iy, seed), c = hashf (x0, iy + 1, seed), d = hashf (x1, iy + 1, seed);
	return lerpf (lerpf (a, b, fx), lerpf (c, d, fx), fy);
}

// ---- les couleurs : flottants 0..1 ; HSV -------------------------------------------------------------------------------
struct Col
{
	float r, g, b;
	Col () : r (0), g (0), b (0) {}
	Col (float a, float b_, float c) : r (a), g (b_), b (c) {}
	Col operator+ (const Col &o) const { return Col (r + o.r, g + o.g, b + o.b); }
	Col operator* (float k) const { return Col (r * k, g * k, b * k); }
	Col operator* (const Col &o) const { return Col (r * o.r, g * o.g, b * o.b); }
};
static inline Col mixc (const Col &a, const Col &b, float t) { return Col (lerpf (a.r, b.r, t), lerpf (a.g, b.g, t), lerpf (a.b, b.b, t)); }
static inline Col hexc (unsigned v) { return Col ((float) ((v >> 16) & 255) / 255.0f, (float) ((v >> 8) & 255) / 255.0f, (float) (v & 255) / 255.0f); }
static inline unsigned c8 (float v) { return v <= 0 ? 0u : v >= 1 ? 255u : (unsigned) (v * 255.0f + 0.5f); }
static inline unsigned packc (const Col &c) { return c8 (c.r) << 16 | c8 (c.g) << 8 | c8 (c.b); }
static inline Col hsv (float h, float s, float v)
{
	h = h - floorf (h);
	float i = floorf (h * 6), f = h * 6 - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
	switch ((int) i % 6)
	{
	case 0: return Col (v, t, p);
	case 1: return Col (q, v, p);
	case 2: return Col (p, v, t);
	case 3: return Col (p, q, v);
	case 4: return Col (t, p, v);
	default: return Col (v, p, q);
	}
}
static inline float luma (const Col &c) { return c.r * 0.299f + c.g * 0.587f + c.b * 0.114f; }

}	// namespace mo
#endif
