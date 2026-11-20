//
// render.h -- Morpho : la caméra, la lumière du moment (heure du jour, lampe éteinte), l'éclairage des maillages
// (par sommet : lumière principale enveloppante, ciel et sol, reflet, contre-jour) en sommets de GpuKit, le fond du
// studio en 2D (dégradé sombre, halo, anneau du socle).
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
	V3 sun = V3 (0.4f, 0.8f, 0.45f);	// la lumière principale (vers elle)
	Col sunC, skyC, groundC, rimC, fog;
	float amb = 0.6f, rimK = 0.3f, fogNear = 1e4f, fogFar = 2e4f;
	float night = 0;			// 0 jour, 1 nuit
	float hour = 12;
	// le studio : le fond (dégradé) et le halo derrière l'animal
	Col bgTop, bgBot, glow;
	float glowK = 1;
};
// la lumière du studio : l'heure teinte le fond et la lumière ; lampOff : la nuit, lumière éteinte. La lumière
// principale est placée par rapport à la caméra (keyFrom)
Light studioLight (float hour, bool lampOff);
void keyFrom (Light &L, const Camera &C);

enum { TEX_SKIN, TEX_SHADOW, TEX_N };

struct DrawList
{
	std::vector<GkV> v;
	std::vector<GkB> b;
	void clear () { v.clear (); b.clear (); }
	// un maillage éclairé ; flags du lot GpuKit déjà faits par l'appelant (gkFlags)
	void add (const Mesh &m, const Light &L, const Camera &C);
};

void makeShadowTex (uint32_t *px, int n);
// le fond du studio dans les pixels (0x00RRGGBB) : le dégradé, le halo derrière l'animal (at, en monde), l'anneau du
// socle au sol (centre, rayon)
void drawStudio (uint32_t *px, int w, int h, int stride, const Camera &C, const Light &L, const V3 &at, const V3 &ringC, float ringR, float t);

}	// namespace mo
#endif
