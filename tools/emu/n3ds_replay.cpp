// n3ds_replay.cpp -- rejoue sur le PC, par llvmpipe, les images que le coeur a deposees (run_n3ds, N3DS_GLDUMP) : le meme
// n3ds_gl.cpp et le meme GLSL que sur le Pi, et compare chaque image au rendu logiciel du coeur.
//     n3ds_replay fichier.gld... [--ppm dossier]
// Un fichier : l'enregistrement d'une image et ce que le rendu logiciel en a fait (tools/emu/run_n3ds.cpp, dumpDraw).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "n3ds/n3ds.h"
#include "n3ds/n3ds_gl.h"
#include "n3ds/pica_gen.h"
#include <math.h>

using namespace n3ds;

bool n3ds_gl_begin (void *(*proc) (const char *), Machine *m);
bool n3ds_gl_draw (const GpuFrame *f, u32 *pixels);
void n3ds_gl_say (FILE *o);
void n3ds_gl_reset ();

static void *proc (const char *name) { return (void *) eglGetProcAddress (name); }

static bool plat ()
{
	static const EGLint cfg[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
	static const EGLint ctx[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
	EGLint pb[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
	PFNEGLGETPLATFORMDISPLAYEXTPROC gd = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress ("eglGetPlatformDisplayEXT");
	EGLDisplay d = gd (0x31DD, EGL_DEFAULT_DISPLAY, NULL);
	EGLConfig c; EGLint n;
	if (!eglInitialize (d, NULL, NULL) || !eglChooseConfig (d, cfg, &c, 1, &n) || !n) return false;
	eglBindAPI (EGL_OPENGL_ES_API);
	EGLSurface s = eglCreatePbufferSurface (d, c, pb);
	EGLContext x = eglCreateContext (d, c, EGL_NO_CONTEXT, ctx);
	return eglMakeCurrent (d, s, s, x);
}

static u8 *slurp (const char *name, size_t &n)
{
	FILE *f = fopen (name, "rb");
	if (!f) return 0;
	fseek (f, 0, SEEK_END); n = (size_t) ftell (f); fseek (f, 0, SEEK_SET);
	u8 *b = (u8 *) malloc (n);
	if (fread (b, 1, n, f) != n) { free (b); b = 0; }
	fclose (f);
	return b;
}

static const char *base (const char *p) { const char *b = strrchr (p, '/'); return b ? b + 1 : p; }
static void ppm (const char *name, const u32 *px, int w, int h)
{
	FILE *o = fopen (name, "wb");
	if (!o) return;
	fprintf (o, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) { const u32 v = px[i]; const u8 c[3] = { (u8) (v >> 16), (u8) (v >> 8), (u8) v }; fwrite (c, 1, 3, o); }
	fclose (o);
}

// --proc : la texture procedurale en GLSL contre le calcul du rendu logiciel (procTexel de n3ds_pica.cpp, recopie
// ici), toutes les combinaisons de bornes et de fonctions, des tables au hasard.
static float pClamp (float c, u32 m)
{
	c = fabsf (c);
	switch (m)
	{
	case 0: return c > 1.0f ? 0.0f : c;
	case 1: return c > 1.0f ? 1.0f : c;
	case 2: return c - floorf (c);
	case 3: { const int i = (int) c; const float f = c - (float) i; return (i & 1) ? 1.0f - f : f; }
	default: return c > 0.5f ? 1.0f : 0.0f;
	}
}
static float pCombine (float u, float v, u32 f)
{
	switch (f)
	{
	case 0: return u; case 1: return u * u; case 2: return v; case 3: return v * v;
	case 4: return (u + v) * 0.5f; case 5: return (u * u + v * v) * 0.5f;
	case 6: { const float d = sqrtf (u * u + v * v); return d > 1.0f ? 1.0f : d; }
	case 7: return u < v ? u : v; case 8: return u > v ? u : v;
	default: { const float a = (u + v) * 0.5f, d = sqrtf (u * u + v * v), x = a > d ? a : d; return x > 1.0f ? 1.0f : x; }
	}
}
static float pMapped (const float *map, int t, float x)
{
	x *= 128.0f;
	int i = (int) x;
	if (i < 0) i = 0; else if (i > 127) i = 127;
	const float r = map[t * 2 * 128 + i] + map[(t * 2 + 1) * 128 + i] * (x - (float) i);
	return r < 0 ? 0 : r > 1 ? 1 : r;
}
static int procTest ()
{
	enum { W = 64, H = 64 };
	static picatev::Shader sh;
	static float map[4 * 128]; static u32 colors[256], pixels[W * H];
	srand (1);
	for (int i = 0; i < 128; i++) { map[i] = (float) (i + 1) / 128.0f; map[128 + i] = 0.0078f; map[256 + i] = 1.0f - (float) i / 128.0f; map[384 + i] = -0.0078f; }
	for (int i = 0; i < 256; i++) colors[i] = (u32) rand () ^ (u32) rand () << 16;
	u64 total = 0, bad1 = 0, bad8 = 0;
	for (int separate = 0; separate < 2; separate++)
	for (u32 cu = 0; cu < 5; cu++) for (u32 cv = 0; cv < 5; cv++) for (u32 fn = 0; fn < 10; fn++)
	{
		picatev::Config c;
		memset (&c, 0, sizeof c);
		for (int k = 0; k < 6; k++) c.stage[k].pass = true;
		picatev::Config::Stage &g = c.stage[0];
		g.pass = false; g.src[0] = g.srcA[0] = 6; g.scale = g.scaleA = 1;
		c.procOn = true; c.procSeparateA = separate; c.procClampU = (unsigned char) cu; c.procClampV = (unsigned char) cv; c.procFuncRgb = (unsigned char) fn; c.procFuncA = (unsigned char) ((fn + 3) % 10);
		if (!picatev::build (c, sh)) { printf ("generateur refuse : %s\n", sh.err); return 1; }
		const u32 width = 200, offset = 20;
		static u32 uni[64];
		for (int i = 0; i < sh.nUni; i++) uni[i] = sh.uni[i].kind == picatev::U_PROCW ? width : sh.uni[i].kind == picatev::U_PROCO ? offset : 0;
		const float ru = 1.5f, rv = 2.5f;			// (au-dela de 1 : les bornes travaillent)
		const float v[36] = { -1, 1, 0, 1, 0, 0, 1, 1, 0, 1, ru, 0, -1, -1, 0, 1, 0, rv, 1, 1, 0, 1, ru, 0, 1, -1, 0, 1, ru, rv, -1, -1, 0, 1, 0, rv };
		GpuProgramInfo pi = { sh.prog.words (), (u32) sh.prog.count (), (u32) sh.nVary, 0 };
		GpuProcInfo pr = { map, colors, 1 };
		GpuBatch b;
		memset (&b, 0, sizeof b);
		b.count = 6; b.stride = 6; b.flags = 7 | 8; b.nUni = (u32) sh.nUni; b.vprog = -1; b.proc = 0;
		for (int k = 0; k < 3; k++) { b.tex[k] = -1; b.texUni[k] = -1; }
		GpuFrame f;
		memset (&f, 0, sizeof f);
		f.w = W; f.h = H; f.v = v; f.nFloats = 36; f.b = &b; f.nb = 1; f.u = uni; f.nu = (u32) sh.nUni; f.programs = &pi; f.nPrograms = 1; f.procs = &pr; f.nProcs = 1; f.color = 1;
		n3ds_gl_reset ();					// (ne pas reprendre le programme du tour d'avant)
		if (!n3ds_gl_draw (&f, pixels)) { printf ("dessin refuse\n"); return 1; }
		for (int y = 0; y < H; y++)
			for (int x = 0; x < W; x++)
			{
				const float tu = ((float) x + 0.5f) / W * ru, tv = ((float) y + 0.5f) / H * rv;
				const float a = pClamp (tu, cu), bb = pClamp (tv, cv);
				const float m = pMapped (map, 0, pCombine (a, bb, fn));
				u32 i = offset + (u32) (m * (float) (width - 1) + 0.5f);
				if (i > 255) i = 255;
				u32 w = colors[i];
				int ch[4] = { (int) (w & 255), (int) (w >> 8 & 255), (int) (w >> 16 & 255), (int) (w >> 24) };
				if (separate) ch[3] = (int) (pMapped (map, 1, pCombine (a, bb, c.procFuncA)) * 255.0f + 0.5f);
				const u32 o = pixels[y * W + x];
				const int g2[4] = { (int) (o >> 16 & 255), (int) (o >> 8 & 255), (int) (o & 255), (int) (o >> 24) };
				int worst = 0;
				for (int k = 0; k < 4; k++) { int e = g2[k] - ch[k]; if (e < 0) e = -e; if (e > worst) worst = e; }
				total++;
				if (worst > 0) bad1++;
				if (worst > 8) bad8++;
			}
	}
	printf ("texture procedurale : %llu pixels, %llu differents, %llu a plus de 8\n", (unsigned long long) total, (unsigned long long) bad1, (unsigned long long) bad8);
	return 0;
}

int main (int argc, char **argv)
{
	if (!plat ()) { fprintf (stderr, "EGL refuse\n"); return 1; }
	if (!n3ds_gl_begin (proc, 0)) return 1;
	if (argc > 1 && !strcmp (argv[1], "--proc")) return procTest ();
	const char *dir = 0, *saveDir = 0, *against = 0;
	static GpuProgramInfo progs[256];
	static GpuTextureInfo texs[96];
	u64 total = 0, off8 = 0, off32 = 0;
	int files = 0;
	for (int a = 1; a < argc; a++)
	{
		if (!strcmp (argv[a], "--ppm")) { dir = argv[++a]; continue; }
		if (!strcmp (argv[a], "--save")) { saveDir = argv[++a]; continue; }	// (les pixels rendus, pour une autre passe)
		if (!strcmp (argv[a], "--against")) { against = argv[++a]; continue; }	// (... comparer a ceux-la, et non au rendu logiciel)
		size_t n;
		u8 *b = slurp (argv[a], n);
		if (!b) continue;
		const u8 *q = b;
		auto take = [&] (size_t k) { const u8 *r = q; q += k; return r; };
		const u32 *h = (const u32 *) take (9 * 4);
		GpuFrame f;
		memset (&f, 0, sizeof f);
		f.w = (int) h[1]; f.h = (int) h[2]; f.nPrograms = h[3]; f.nTextures = h[4]; f.nb = h[5]; f.nu = h[6]; f.nFloats = h[7]; f.color = h[8];
		for (u32 i = 0; i < f.nPrograms; i++)
		{
			const u32 *pv = (const u32 *) take (8);
			progs[i].nWords = pv[0]; progs[i].nVary = pv[1];
			progs[i].words = (const unsigned long long *) take ((size_t) pv[0] * 8);
		}
		for (u32 i = 0; i < f.nTextures; i++)
		{
			const u32 *wh = (const u32 *) take (12);
			texs[i].w = wh[0]; texs[i].h = wh[1]; texs[i].serial = wh[2] ? wh[2] : 1;
			texs[i].px = wh[0] ? (const u32 *) take ((size_t) wh[0] * wh[1] * 4) : 0;
		}
		f.programs = progs; f.textures = texs;
		static GpuProcInfo procs[8];
		{
			const u32 np = *(const u32 *) take (4);
			for (u32 i = 0; i < np; i++)
			{
				procs[i].serial = *(const u32 *) take (4);
				procs[i].map = (const float *) take (4 * 128 * 4); procs[i].colors = (const u32 *) take (4 * 256);
			}
			f.procs = procs; f.nProcs = np;
		}
		static GpuVProgramInfo vps[512];
		{
			const u32 nvp = *(const u32 *) take (4);
			for (u32 i = 0; i < nvp; i++)
			{
				const u32 *pv = (const u32 *) take (8);
				vps[i].nvs = pv[0]; vps[i].inputs = pv[1];
				vps[i].vs = (const unsigned long long *) take ((size_t) pv[0] * 8);
			}
			f.vprograms = vps; f.nVPrograms = nvp;
		}
		f.b = (const GpuBatch *) take (sizeof (GpuBatch) * f.nb);
		f.u = (const u32 *) take (4 * f.nu);
		f.v = (const float *) take (4 * f.nFloats);
		const size_t px = (size_t) f.w * (size_t) f.h;
		u32 *pixels = (u32 *) malloc (px * 4);
		memcpy (pixels, take (px * 4), px * 4);
		f.stale = true;
		const u32 *soft = (const u32 *) take (px * 4);
		u32 *other = 0;
		if (against)
		{
			char nm[512]; snprintf (nm, sizeof nm, "%s/%s.raw", against, base (argv[a]));
			size_t k; other = (u32 *) slurp (nm, k);
			if (!other || k != px * 4) { printf ("%s : pas de pendant\n", nm); free (b); free (pixels); free (other); continue; }
			soft = other;
		}
		const bool ok = n3ds_gl_draw (&f, pixels);
		u32 d8 = 0, d32 = 0, vb = 0;
		for (u32 i = 0; i < f.nb; i++) if (f.b[i].vprog >= 0) vb++;
		for (size_t i = 0; i < px; i++)
		{
			int worst = 0;
			for (int c = 0; c < 24; c += 8) { int e = (int) (pixels[i] >> c & 255) - (int) (soft[i] >> c & 255); if (e < 0) e = -e; if (e > worst) worst = e; }
			if (worst > 8) d8++;
			if (worst > 32) d32++;
		}
		if (saveDir) { char nm[512]; snprintf (nm, sizeof nm, "%s/%s.raw", saveDir, base (argv[a])); FILE *o = fopen (nm, "wb"); if (o) { fwrite (pixels, 4, px, o); fclose (o); } }
		printf ("%s : %dx%d, %u lots, %s, %u pixels a plus de 8, %u a plus de 32, sur %zu\n", argv[a], f.w, f.h, f.nb, ok ? "ok" : "REFUSE", d8, d32, px);
		total += px; off8 += d8; off32 += d32; files++;
		if (dir)
		{
			char nm[512];
			snprintf (nm, sizeof nm, "%s/%d_gl.ppm", dir, a); ppm (nm, pixels, f.w, f.h);
			snprintf (nm, sizeof nm, "%s/%d_soft.ppm", dir, a); ppm (nm, soft, f.w, f.h);
		}
		free (pixels); free (b); free (other);
	}
	printf ("%d images : %llu pixels, %llu a plus de 8 (%.3f %%), %llu a plus de 32 (%.3f %%)\n", files, (unsigned long long) total, (unsigned long long) off8, 100.0 * (double) off8 / (double) (total ? total : 1), (unsigned long long) off32, 100.0 * (double) off32 / (double) (total ? total : 1));
	n3ds_gl_say (stdout);
	return 0;
}
