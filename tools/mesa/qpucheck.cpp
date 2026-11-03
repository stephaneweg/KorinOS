// qpucheck -- execute sur le PC, par qpusim d'Onyx, le code QPU que Mesa a produit dans ARMPI, et le compare
// a un calcul fait ici, sans Mesa. Construit par tools/mesa/qpucheck.sh.
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#include "qpusim.h"

#define GAIN 1.0f
#define TEX0 0x1000
static uint32_t F (float f) { uint32_t u; memcpy (&u, &f, 4); return u; }
#include "qpu_shaders.h"

static int fails, tests;

static void check (const char *what, float got, float want, float tol)
{
	tests++;
	if (fabsf (got - want) > tol) {
		fails++;
		if (fails < 20)
			printf ("  %s : %f, attendu %f\n", what, got, want);
	}
}

static float clamp01 (float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float smooth (float a, float b, float x)
{
	float t = clamp01 ((x - a) / (b - a));
	return t * t * (3 - 2 * t);
}

// le triangle : couleur interpolee, W et C d'un vrai ldvary
static void test_fs1 ()
{
	using namespace qpusim;
	std::vector<Pixel> px;
	for (int i = 0; i < 16; i++) {
		Pixel p {};
		float r = i / 15.0f, g = 1 - r, b = (i % 4) / 3.0f;
		p.w = 1.0f;
		p.vary = { r, g, b };
		px.push_back (p);
	}
	Run run;
	if (!runFragment (fs1_code, sizeof fs1_code / 8, px, run)) {
		printf ("fs1 : %s\n", run.error.c_str ());
		fails++;
		return;
	}
	for (int i = 0; i < 16; i++) {
		float r = i / 15.0f, g = 1 - r, b = (i % 4) / 3.0f;
		uint32_t c = px[i].rgba;
		check ("fs1 r", (c & 255) / 255.0f, r, 1.5f / 255);
		check ("fs1 g", (c >> 8 & 255) / 255.0f, g, 1.5f / 255);
		check ("fs1 b", (c >> 16 & 255) / 255.0f, b, 1.5f / 255);
		check ("fs1 a", (c >> 24) / 255.0f, 1, 0.5f / 255);
	}
}

// le programme a la maniere de Skia : texture 2 x 2 au plus proche, disque, teinte
static void test_fs2 ()
{
	using namespace qpusim;
	Texture t;
	t.w = t.h = 2;
	t.px = { 0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFFFFFFFF };
	t.wrapS = t.wrapT = 1;
	t.linear = false;
	std::vector<Pixel> px;
	const int N = 16 * 16;
	std::vector<float> us, vs;
	for (int i = 0; i < N; i++) {
		float u = (i % 16 + 0.5f) / 16, v = (i / 16 + 0.5f) / 16;
		Pixel p {};
		p.w = 1.0f;
		p.vary = { u, v };
		px.push_back (p);
		us.push_back (u);
		vs.push_back (v);
	}
	Run run;
	run.uniforms.assign (fs2_unif, fs2_unif + sizeof fs2_unif / 4);
	run.textures[TEX0] = t;
	if (!runFragment (fs2_code, sizeof fs2_code / 8, px, run)) {
		printf ("fs2 : %s\n", run.error.c_str ());
		fails++;
		return;
	}
	static const float tint[4] = { 1, 0.5f, 0.25f, 1 };
	static const float tex[4][4] = { { 1, 0, 0, 1 }, { 0, 1, 0, 1 }, { 0, 0, 1, 1 }, { 1, 1, 1, 1 } };
	for (int i = 0; i < N; i++) {
		float u = us[i], v = vs[i];
		float d = sqrtf ((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f));
		float cov = 1 - smooth (0.3f, 0.4f, d);
		const float *c = tex[(v >= 0.5f ? 2 : 0) + (u >= 0.5f ? 1 : 0)];
		for (int k = 0; k < 4; k++) {
			float want = c[k] * tint[k] * cov;
			check ("fs2", (px[i].rgba >> (k * 8) & 255) / 255.0f, want, 2.5f / 255);
		}
	}
}

// le sommet : position, coordonnees d'ecran, couleur multipliee par gain
static void test_vs1 ()
{
	using namespace qpusim;
	std::vector<Pixel> vx;
	for (int i = 0; i < 16; i++) {
		Pixel p {};
		float x = -1 + i / 8.0f, y = 1 - i / 8.0f;
		p.vpmIn = { F (x), F (y), F (0.25f), F (0.5f), F (0.75f) };
		p.vpmOut.assign (32, 0);
		vx.push_back (p);
	}
	Run run;
	run.uniforms.assign (vs1_unif, vs1_unif + sizeof vs1_unif / 4);
	if (!runVertex (vs1_code, sizeof vs1_code / 8, vx, run)) {
		printf ("vs1 : %s\n", run.error.c_str ());
		fails++;
		return;
	}
	// en rendu : xs, ys, zs, 1/w, puis les varyings (l'ordre lu dans le shader)
	for (int i = 0; i < 16; i++) {
		float f[7];
		memcpy (f, vx[i].vpmOut.data (), sizeof f);
		float x = -1 + i / 8.0f, y = 1 - i / 8.0f;
		check ("vs1 xs", (float) (int) vx[i].vpmOut[0], floorf (x * 12800), 1);
		check ("vs1 ys", (float) (int) vx[i].vpmOut[1], floorf (y * -12800), 1);
		check ("vs1 zs", f[2], 0.5f, 1e-5f);
		check ("vs1 1/w", f[3], 1, 1e-5f);
		check ("vs1 r", f[4], 0.25f * GAIN, 1e-5f);
		check ("vs1 g", f[5], 0.5f * GAIN, 1e-5f);
		check ("vs1 b", f[6], 0.75f * GAIN, 1e-5f);
	}
}

int main ()
{
	test_fs1 ();
	test_vs1 ();
	test_fs2 ();
	printf ("qpucheck : %d valeurs comparees, %d ecarts : %s\n", tests, fails, fails ? "FAIL" : "PASS");
	return fails != 0;
}
