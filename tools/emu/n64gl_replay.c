/*
 * n64gl_replay -- rejoue sur le PC, par llvmpipe, les images que le coeur asm a deposees (n64_harness, mode g) :
 * le meme n64gl.c que sur le Pi, le meme GLSL, sans GPU. Sort une image .ppm par image rejouee.
 *     n64gl_replay dump dossier [--bilin] [--keys fichier]
 * Sous ARMPI (n64gltest, build-n64.sh) le meme fichier tourne sur armgl, a blanc sous QEMU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "n64gl.h"

/* la plate-forme : contexte, cible de w x h courante, attente de fin, lecture ligne du haut d'abord */
void *plat_init (int w, int h, void **proc);
void plat_finish (void);
void plat_read (void *rgba, int w, int h);

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

#ifdef __linux__
#define GL_GLES_PROTOTYPES 0
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#define EGL_PLATFORM_SURFACELESS 0x31DD

void *plat_init (int w, int h, void **proc)
{
	static const EGLint cfg[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
		EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
	static const EGLint ctx[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
	EGLint pb[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_NONE };
	PFNEGLGETPLATFORMDISPLAYEXTPROC gd = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress ("eglGetPlatformDisplayEXT");
	EGLDisplay d = gd (EGL_PLATFORM_SURFACELESS, EGL_DEFAULT_DISPLAY, NULL);
	EGLConfig c;
	EGLint n;
	if (!eglInitialize (d, NULL, NULL) || !eglChooseConfig (d, cfg, &c, 1, &n) || !n) {
		printf ("EGL refuse\n");
		return NULL;
	}
	eglBindAPI (EGL_OPENGL_ES_API);
	EGLSurface s = eglCreatePbufferSurface (d, c, pb);
	EGLContext x = eglCreateContext (d, c, EGL_NO_CONTEXT, ctx);
	if (!eglMakeCurrent (d, s, s, x)) {
		printf ("eglMakeCurrent refuse\n");
		return NULL;
	}
	*proc = (void *) eglGetProcAddress;
	return s;
}

void plat_finish (void)
{
	((PFNGLFINISHPROC) eglGetProcAddress ("glFinish")) ();
}

void plat_read (void *rgba, int w, int h)
{
	static unsigned char *t;
	if (!t)
		t = malloc (w * h * 4);
	((PFNGLREADPIXELSPROC) eglGetProcAddress ("glReadPixels")) (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, t);
	for (int y = 0; y < h; y++)
		memcpy ((char *) rgba + y * w * 4, t + (h - 1 - y) * w * 4, w * 4);
}
#else
#include "armgl.h"

static void *surface;

void *plat_init (int w, int h, void **proc)
{
	static void *px[1];
	int mode = armgl_init (ARMGL_F_DRY_BUF);
	void *ctx = armgl_context (3, 0, NULL);
	surface = armgl_buffers (px, 1, w * h * 4, w, h, w * 4);
	if (!mode || !ctx || !surface || !armgl_make_current (ctx, surface))
		return NULL;
	armgl_buffer_bind (surface, 0);
	*proc = armgl_proc;
	return surface;
}

void plat_finish (void)
{
	armgl_finish ();
	printf ("n64gltest : jobs %ld\n", armgl_jobs ());
}

void plat_read (void *rgba, int w, int h)
{
}
#endif

#ifndef __linux__
/* n64gltest --qpu : un seul lot connu, son code QPU sur la sortie, pour tools/mesa/qpucheck.* */
static int qpu_test (void)
{
	static uint32_t px0[16], px1[16];
	static struct n64gl_tex tex[N64GL_MAX_TEX];
	static const float xy[6][2] = { { -1, 1 }, { -1, -1 }, { 1, 1 }, { 1, 1 }, { -1, -1 }, { 1, -1 } };
	static struct n64gl_vert v[6];
	struct n64gl_batch b;
	struct n64gl_frame f;
	void *proc = NULL;
	setenv ("V3D_DEBUG", "fs,qpu,shaderdb", 1);
	if (!plat_init (64, 64, &proc) || !n64gl_start ((void *(*) (const char *)) proc, 64, 64))
		return 1;
	for (int i = 0; i < 16; i++) {
		px0[i] = 0xFF000000 | (i * 16) << 16 | (255 - i * 16) << 8 | 128;
		px1[i] = 0x80000000 | (i * 8) << 16 | 64 << 8 | (255 - i * 8);
	}
	tex[0].px = px0;
	tex[1].px = px1;
	tex[0].w = tex[0].h = tex[1].w = tex[1].h = 4;
	tex[0].dirty = tex[1].dirty = 1;
	for (int i = 0; i < 6; i++) {
		v[i].x = xy[i][0];
		v[i].y = xy[i][1];
		v[i].w = 1;
		v[i].s = xy[i][0] < 0 ? 0 : 4;
		v[i].t = xy[i][1] > 0 ? 0 : 4;
		v[i].r = 200;
		v[i].g = 100;
		v[i].b = 50;
		v[i].a = 128;
	}
	memset (&b, 0, sizeof b);
	/* (T0 - PRIM) * SHADE + ENV, puis COMBINED * ENV_A + TEXEL0, soit T1 au 2e cycle ; brouillard par l'alpha de
	 * l'ombrage. T1 lu entier : qpusim ignore le masque de mots du TMU */
	b.combH = 1 << 20 | 4 << 15 | 7 << 12 | 7 << 9 | 0 << 5 | 12;
	b.combL = 3u << 28 | 8 << 24 | 7 << 21 | 7 << 18 | 5 << 15 | 7 << 12 | 1 << 9 | 1 << 6 | 7 << 3 | 1;
	b.omH = 1 << 20;
	b.omL = 3u << 30 | 2 << 26 | 0 << 22 | 0 << 18 | 0 << 28 | 0 << 24 | 1 << 20 | 0 << 16;
	b.prim = 0x20406080;
	b.env = 0x10203040;
	b.fog = 0xC0A08060;
	b.blend = 0;
	b.count = 6;
	b.tex0 = 0;
	b.tex1 = 1;
	b.sc1 = 320 | 240u << 16;
	b.flags = 7 | 8 | 1 << 12;
	memset (&f, 0, sizeof f);
	f.w = f.h = 64;
	f.gw = 320;
	f.gh = 240;
	f.v = v;
	f.nv = 6;
	f.nb = 1;
	f.b = &b;
	f.tex = tex;
	n64gl_render (&f);
	plat_finish ();
	printf ("n64gltest --qpu : PASS\n");
	return 0;
}
#endif

int main (int argc, char **argv)
{
	static struct n64gl_tex tex[N64GL_MAX_TEX];
	enum { W = 640, H = 480 };
	void *proc = NULL;
	int bilin = 0;
	const char *keys = NULL;
#ifndef __linux__
	if (argc > 1 && !strcmp (argv[1], "--qpu"))
		return qpu_test ();
#endif
	for (int i = 3; i < argc; i++) {
		if (!strcmp (argv[i], "--bilin")) bilin = 1;
		else if (!strcmp (argv[i], "--keys")) keys = argv[++i];
	}
	FILE *f = fopen (argv[1], "rb");
	if (!f || !plat_init (W, H, &proc) || !n64gl_start ((void *(*) (const char *)) proc, W, H)) {
		printf ("n64gl_replay : FAIL au depart\n");
		return 1;
	}
	if (bilin)
		n64gl_filter (0);
	if (keys)
		printf ("n64gl_replay : %d programmes d'avance\n", n64gl_keys_load (keys));
	static unsigned char rgba[W * H * 4];
	static uint32_t hdr[8];
	int frames = 0;
	double total = 0;
	while (fread (hdr, 32, 1, f) == 1) {
		struct n64gl_frame fr;
		memset (&fr, 0, sizeof fr);
		fr.w = W;
		fr.h = H;
		fr.stride = W;
		fr.clear = hdr[1];
		fr.gw = hdr[2];
		fr.gh = hdr[3];
		fr.nv = hdr[4];
		fr.nb = hdr[5];
		struct n64gl_vert *v = malloc (fr.nv * sizeof *v + 32);
		struct n64gl_batch *b = malloc (fr.nb * sizeof *b + 32);
		if (fread (v, sizeof *v, fr.nv, f) != (size_t) fr.nv || fread (b, sizeof *b, fr.nb, f) != (size_t) fr.nb
		    || fread (tex, sizeof tex, 1, f) != 1) {
			printf ("n64gl_replay : dump tronque\n");
			return 1;
		}
		for (int i = 0; i < N64GL_MAX_TEX; i++) {
			size_t n = (size_t) tex[i].w * tex[i].h;
			tex[i].px = NULL;
			tex[i].dirty = n != 0;
			if (n) {
				uint32_t *p = malloc (n * 4);
				if (fread (p, 4, n, f) != n)
					return 1;
				tex[i].px = p;
			}
		}
		fr.v = v;
		fr.b = b;
		fr.tex = tex;
		double t0 = now ();
		n64gl_render (&fr);
		int p, c, fb;
		n64gl_stats (&p, &c, &fb);
		plat_finish ();
		total += now () - t0;
		plat_read (rgba, W, H);
		char name[512];
		snprintf (name, sizeof name, "%s/f%05u.ppm", argv[2], hdr[6]);
#ifdef __linux__
		FILE *o = fopen (name, "wb");
		fprintf (o, "P6\n%d %d\n255\n", W, H);
		for (int i = 0; i < W * H; i++)
			fwrite (rgba + i * 4, 3, 1, o);
		fclose (o);
#endif
		printf ("image %u : %d sommets, %d lots, %d sans programme, %d programmes\n", hdr[6], fr.nv, fr.nb, fb, p);
		for (int i = 0; i < N64GL_MAX_TEX; i++)
			free ((void *) tex[i].px);
		free (v);
		free (b);
		frames++;
	}
	int p, c, fb;
	n64gl_stats (&p, &c, &fb);
	printf ("n64gl_replay : %d images, %d programmes, %.1f ms par image : PASS\n", frames, p, total / (frames ? frames : 1));
	return 0;
}
