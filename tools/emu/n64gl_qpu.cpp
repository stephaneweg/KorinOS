// n64gl_qpu -- le code QPU d'un shader de n64gl.c, produit par Mesa dans ARMPI (n64gltest --qpu), execute sur le PC
// par qpusim d'Onyx, contre un calcul fait ici : deux cycles du combineur, deux textures, trois points, 1er cycle du
// blender. Le lot : qpu_test de n64gl_replay.c. Construit par n64gl_qpu.sh, l'en-tete par n64gl_qpu.py.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "qpusim.h"

#define TEX0 0x1000
#define TEX1 0x2000
static uint32_t F (float f) { uint32_t u; memcpy (&u, &f, 4); return u; }
#include "n64gl_qpu.h"

struct V { float c[4]; };

static float sat (float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }

static V of (uint32_t c)
{
	V v = { { (c >> 24) / 255.0f, (c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f, (c & 255) / 255.0f } };
	return v;
}

// 0xAARRGGBB de l'emulateur -> ce que le TMU rend apres la permutation R, B de la texture
static uint32_t tmu (uint32_t p)
{
	return (p & 0xFF00FF00) | (p >> 16 & 255) | (p & 255) << 16;
}

static V texel (const uint32_t *px, int x, int y)
{
	uint32_t p = px[(y & 3) * 4 + (x & 3)];
	V v = { { (p >> 16 & 255) / 255.0f, (p >> 8 & 255) / 255.0f, (p & 255) / 255.0f, (p >> 24) / 255.0f } };
	return v;
}

static V three (const uint32_t *px, float s, float t)
{
	int ix = floorf (s), iy = floorf (t);
	float fx = s - ix, fy = t - iy;
	int up = fx + fy >= 1;
	V c = texel (px, ix + up, iy + up), x = texel (px, up ? ix : ix + 1, up ? iy + 1 : iy), y = texel (px, up ? ix + 1 : ix, up ? iy : iy + 1), o;
	float gx = up ? 1 - fx : fx, gy = up ? 1 - fy : fy;
	for (int k = 0; k < 4; k++)
		o.c[k] = c.c[k] + gx * (x.c[k] - c.c[k]) + gy * (y.c[k] - c.c[k]);
	return o;
}

int main ()
{
	using namespace qpusim;
	static uint32_t p0[16], p1[16];
	const V prim = of (0x20406080), env = of (0x10203040), fog = of (0xC0A08060);
	const float sh[4] = { 200 / 255.0f, 100 / 255.0f, 50 / 255.0f, 128 / 255.0f };
	Texture a, b;
	a.w = a.h = b.w = b.h = 4;
	a.wrapS = a.wrapT = b.wrapS = b.wrapT = 0;
	a.linear = b.linear = false;
	for (int i = 0; i < 16; i++) {
		p0[i] = 0xFF000000 | (i * 16) << 16 | (255 - i * 16) << 8 | 128;
		p1[i] = 0x80000000 | (i * 8) << 16 | 64 << 8 | (255 - i * 8);
		a.px.push_back (tmu (p0[i]));
		b.px.push_back (tmu (p1[i]));
	}
	std::vector<Pixel> px;
	const int N = 16 * 16;
	for (int i = 0; i < N; i++) {
		float s = (i % 16 + 0.5f) / 4, t = (i / 16 + 0.5f) / 4;
		Pixel p {};
		p.w = 1.0f;
		p.vary = { sh[0], sh[1], sh[2], sh[3], s, t, s, t };
		px.push_back (p);
	}
	Run run;
	run.uniforms.assign (fs_unif, fs_unif + sizeof fs_unif / 4);
	run.textures[TEX0] = a;
	run.textures[TEX1] = b;
	if (!runFragment (fs_code, sizeof fs_code / 8, px, run)) {
		printf ("n64gl_qpu : %s\n", run.error.c_str ());
		return 1;
	}
	int tests = 0, fails = 0;
	for (int i = 0; i < N; i++) {
		float s = (i % 16 + 0.5f) / 4, t = (i / 16 + 0.5f) / 4;
		V t0 = three (p0, s, t), t1 = three (p1, s, t);
		float want[4];
		float sa = sh[3];
		for (int k = 0; k < 3; k++) {
			float c = sat ((t0.c[k] - prim.c[k]) * sh[k] + env.c[k]);	// (T0 - PRIM) * SHADE + ENV
			c = sat (c * env.c[3] + t1.c[k]);				// COMBINED * ENV_A + TEXEL0, soit T1 au 2e cycle
			want[k] = fog.c[k] * sa + c * (1 - sa);				// le brouillard par l'alpha de l'ombrage
		}
		want[3] = t1.c[3];							// TEXEL0 du 2e cycle, soit T1
		for (int k = 0; k < 4; k++) {
			float got = (px[i].rgba >> (k * 8) & 255) / 255.0f;
			tests++;
			if (fabsf (got - want[k]) > 3.0f / 255) {
				if (fails++ < 12)
					printf ("  pixel %d voie %d : %f, attendu %f\n", i, k, got, want[k]);
			}
		}
	}
	printf ("n64gl_qpu : %d valeurs comparees, %d ecarts : %s\n", tests, fails, fails ? "FAIL" : "PASS");
	return fails != 0;
}
