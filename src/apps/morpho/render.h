//
// render.h -- Morpho : la caméra, la lumière du moment (heure du jour, lampe éteinte), l'éclairage des maillages
// (par sommet : soleil enveloppant, ciel et sol, reflet, contre-jour, brouillard) en sommets de GpuKit, le ciel en 2D,
// la prairie et son décor.
//
#ifndef MORPHO_RENDER_H
#define MORPHO_RENDER_H
#include "mesh.h"

namespace mo {

struct GkV { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };
struct GkB { unsigned first, count; int tex; unsigned flags; float m[16]; };

struct Camera
{
	V3 eye, at;
	float fov = 0.8f;
	int w = 1, h = 1;
	M4 vp;
	void setup (int W, int H)
	{
		w = W; h = H;
		vp = perspective (fov, (float) W / (float) (H > 0 ? H : 1), 0.1f, 60) * lookAt (eye, at, V3 (0, 1, 0));
	}
	// monde -> pixels (false : derrière)
	bool project (const V3 &p, float &sx, float &sy, float *depth = 0) const
	{
		float o[4]; vp.apply (p, o);
		if (o[3] <= 0.05f) return false;
		sx = (o[0] / o[3] * 0.5f + 0.5f) * (float) w; sy = (0.5f - o[1] / o[3] * 0.5f) * (float) h;
		if (depth) *depth = o[3];
		return true;
	}
	// pixels -> rayon (direction du monde)
	V3 ray (float sx, float sy) const
	{
		V3 f = norm (at - eye), s = norm (cross (f, V3 (0, 1, 0))), u = cross (s, f);
		float k = tanf (fov * 0.5f), a = (float) w / (float) (h > 0 ? h : 1);
		float x = (sx / (float) w * 2 - 1) * k * a, y = (1 - sy / (float) h * 2) * k;
		return norm (f + s * x + u * y);
	}
};

struct Light
{
	V3 sun = V3 (0.4f, 0.8f, 0.45f);
	Col sunC, skyC, groundC, rimC, fog;
	Col skyTop, skyHor, skyLow;		// le ciel en 2D
	Col sunDisc;
	float amb = 0.6f, fogNear = 7, fogFar = 22;
	float night = 0;			// 0 jour, 1 nuit (étoiles, lune)
	float sunUp = 1;			// le disque du soleil est-il au ciel
	float hour = 12;
};
// l'heure (0..24) et la lampe (true : éteinte) -> la lumière
Light lightAt (float hour, bool lampOff, float overcast);

enum { TEX_SKIN, TEX_SHADOW, TEX_GRASS, TEX_N };

struct DrawList
{
	std::vector<GkV> v;
	std::vector<GkB> b;
	void clear () { v.clear (); b.clear (); }
	// un maillage éclairé ; flags du lot GpuKit déjà faits par l'appelant (gkFlags)
	void add (const Mesh &m, const Light &L, const Camera &C);
};

// la prairie, les fleurs, les arbres au loin (une fois)
void buildWorld (Mesh &ground, Mesh &decor, Mesh &grassBlades, uint32_t seed);
void makeShadowTex (uint32_t *px, int n);
void makeGrassTex (uint32_t *px, int n);
// le ciel dans les pixels (0x00RRGGBB), avec le soleil, la lune, les étoiles, les nuages
void drawSky (uint32_t *px, int w, int h, int stride, const Camera &C, const Light &L, float t, float clouds);

}	// namespace mo
#endif
