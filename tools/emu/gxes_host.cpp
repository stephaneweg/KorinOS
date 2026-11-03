// gxes_host -- le rendu GL ES de gcemu contre celui d'Onyx, sur le PC (WSL), par le meme Mesa logiciel (llvmpipe) :
// la machine (x86-64, JIT d'Onyx) tourne, chaque draw va aux deux renderers (gxgl.cpp d'Onyx en OpenGL 3.3 ;
// gxes en OpenGL ES 3.0), et leurs images de l'EFB sont comparees a chaque XFB.
//     gxes_host <iso | dol> <trames> [pas]
//   une ligne pour chaque image du pas : trame, pixels differents (> 8, > 32 niveaux), ecart moyen, programmes
//   GC_SNAP=dossier : les deux images en PPM ; GC_PAD, GC_CARD : comme ref_gc ; GC_DUMPFS=1 : les shaders, sur stdout
#include "gc/gc.h"
#include "gxes.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
using namespace gc;

gc::GxGpu *gxgl_create (int scale, char *err, int cap);
int gxgl_read (gc::GxGpu *g, unsigned *px, int cap, int *w, int *h);
void gxgl_release (gc::GxGpu *g);

static EGLDisplay g_dpy; static EGLContext g_cGl, g_cEs; static EGLSurface g_sGl, g_sEs;
static void *proc (const char *n) { return (void *) eglGetProcAddress (n); }

static bool setup ()
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC get = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress ("eglGetPlatformDisplayEXT");
	g_dpy = get ? get (EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, 0) : eglGetDisplay (EGL_DEFAULT_DISPLAY);
	EGLint ma, mi;
	if (!eglInitialize (g_dpy, &ma, &mi)) { fprintf (stderr, "pas d'EGL\n"); return false; }
	EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT | EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_NONE };
	EGLConfig cfg; EGLint n = 0;
	if (!eglChooseConfig (g_dpy, ca, &cfg, 1, &n) || !n) { fprintf (stderr, "pas de configuration EGL\n"); return false; }
	EGLint pa[] = { EGL_WIDTH, 640, EGL_HEIGHT, 528, EGL_NONE };
	g_sGl = eglCreatePbufferSurface (g_dpy, cfg, pa); g_sEs = eglCreatePbufferSurface (g_dpy, cfg, pa);
	eglBindAPI (EGL_OPENGL_API);
	EGLint a33[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
	g_cGl = eglCreateContext (g_dpy, cfg, EGL_NO_CONTEXT, a33);
	eglBindAPI (EGL_OPENGL_ES_API);
	EGLint a30[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0, EGL_NONE };
	g_cEs = eglCreateContext (g_dpy, cfg, EGL_NO_CONTEXT, a30);
	if (!g_cGl || !g_cEs) { fprintf (stderr, "pas de contextes : %x %p %p\n", eglGetError (), g_cGl, g_cEs); return false; }
	return true;
}

static void *hostCode (u32 n)
{
	void *p = mmap (0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}
static FILE *g_f;
static bool discRead (void *, u32 off, u32 len, u8 *dst) { return !fseeko (g_f, off, SEEK_SET) && fread (dst, 1, len, g_f) == len; }

static gxes::Rec g_rec; static gxes::Player g_pl; static GxGpu *g_ref;
static int g_field, g_every = 1, g_frame;
static const char *g_snap;
static unsigned *g_pa, *g_pb;
static gc::GxCopy g_last;
static u8 g_dirty[Machine::MAX_TEX];

static void ppm (const char *n, const unsigned *px, int w, int h)
{
	FILE *o = fopen (n, "wb");
	if (!o) return;
	fprintf (o, "P6\n%d %d\n255\n", w, h);
	for (int i = 0; i < w * h; i++) { u32 c = px[i]; u8 t[3] = { (u8) (c >> 16), (u8) (c >> 8), (u8) c }; fwrite (t, 1, 3, o); }
	fclose (o);
}

// chaque image finie : gxes joue ses ordres, puis on compare
static void compare (gc::Machine &m)
{
	g_frame++;
	eglMakeCurrent (g_dpy, g_sEs, g_sEs, g_cEs);
	static bool once;
	if (!once) { once = true; if (!g_pl.init (proc)) { fprintf (stderr, "gxes : %s\n", g_pl.st.err ? g_pl.st.err : "?"); exit (1); } }
	g_pl.budgetUs = 100000000;
	int w = 0, h = 0;
	gc::GxCopy c = g_last;
	if (const char *dbg = getenv ("GC_DBG"))
		if (g_frame == atoi (dbg))
		{
			gxes::Frame &F = g_rec.frame[g_rec.rd % gxes::NFRAMES];
			for (u32 i = 0; i < F.nb; i++)
			{
				gxes::Batch &b = F.b[i];
				if (b.kind != gxes::B_DRAW) { printf ("copie\n"); continue; }
				printf ("lot %u : prog %d n %u z %d%d%d cull %d cm %d am %d bl %d %d %d sc %d %d %d %d", i, b.prog, b.count, b.zon, b.zfunc, b.zwrite, b.cull, b.cmask, b.amask, b.blendOn, b.blendSrc, b.blendDst, b.scissor[0], b.scissor[1], b.scissor[2], b.scissor[3]);
				for (int k = 0; k < 8; k++) if (b.tex[k] >= 0) printf (" t%d=%d(%dx%d L%d m0 %x m1 %x)", k, b.tex[k], m.tex[b.tex[k] & 255].w, m.tex[b.tex[k] & 255].h, m.tex[b.tex[k] & 255].levels, b.mode0[k], b.mode1[k]);
				float *v = F.v + b.off * (4 + 4 * b.chunks);
				printf (" v0 %.3f %.3f %.3f %.3f | %.3f %.3f %.3f %.3f %.3f\n", v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8]);
			}
			static char fs[60000];
			for (u32 i = 0; i < F.nb && i < 4; i++) if (F.b[i].kind == gxes::B_DRAW) { gxes::fragmentSource (g_rec.prog[F.b[i].prog], fs, sizeof fs); printf ("%s", fs); }
		}
	g_pl.grab = g_pb;
	for (int k = 0; k < Machine::MAX_TEX; k++) if (g_dirty[k]) { m.tex[k].dirty = true; g_dirty[k] = 0; }
	g_pl.pump (g_rec, m, 0, 0, 640, 528, 0);
	if (g_frame % g_every == 0)
	{
		w = c.w > 640 ? 640 : c.w; h = c.h > 528 ? 528 : c.h;
		eglMakeCurrent (g_dpy, g_sGl, g_sGl, g_cGl);
		int rw = 0, rh = 0;
		gxgl_release (g_ref);
		gxgl_read (g_ref, g_pa, 640 * 576, &rw, &rh);
		long d8 = 0, d32 = 0, sum = 0;
		if (rw == w && rh == h)
			for (int i = 0; i < w * h; i++)
			{
				int md = 0;
				for (int s = 0; s < 24; s += 8)
				{
					int dd = (int) ((g_pa[i] >> s) & 255) - (int) ((g_pb[i] >> s) & 255);
					if (dd < 0) dd = -dd;
					if (dd > md) md = dd;
					sum += dd;
				}
				if (md > 8) d8++;
				if (md > 32) d32++;
			}
		printf ("image %d trame %d %dx%d (ref %dx%d) : >8 : %ld (%.2f %%), >32 : %ld, moyenne %.3f ; lots %u, programmes %d compiles %u refuses %u, copies %u\n",
			g_frame, g_field, w, h, rw, rh, d8, 100.0 * d8 / (w * h ? w * h : 1), d32, (double) sum / 3 / (w * h ? w * h : 1),
			g_pl.st.batches, g_rec.nProg, g_pl.st.compiled, g_pl.st.failed, g_pl.st.copies);
		fflush (stdout);
		if (g_snap)
		{
			char n[256];
			snprintf (n, sizeof n, "%s/%05d_ref.ppm", g_snap, g_frame); ppm (n, g_pa, rw, rh);
			snprintf (n, sizeof n, "%s/%05d_es.ppm", g_snap, g_frame); ppm (n, g_pb, w, h);
		}
	}
	eglMakeCurrent (g_dpy, g_sGl, g_sGl, g_cGl);
}

// les deux renderers a la fois
struct Tee : GxGpu
{
	void draw (Machine &m, const GxState &s, const GxVertex *v, int nv, const u32 *idx, int ni, int prim) override
	{
		// (gxgl efface T.dirty en chargeant : gxes garde le sien)
		for (int k = 0; k < Machine::MAX_TEX; k++) if (m.tex[k].dirty) g_dirty[k] = 1;
		g_ref->draw (m, s, v, nv, idx, ni, prim);
		g_rec.draw (m, s, v, nv, idx, ni, prim);
	}
	void copy (Machine &m, const GxCopy &c) override
	{
		g_ref->copy (m, c);
		g_rec.copy (m, c);
		if (c.toXfb) { g_last = c; compare (m); }
	}
};
static Tee g_tee;

struct PadStep { int f0, f1; u32 b; };
static PadStep g_pad[256]; static int g_npad;
static void padParse (const char *s)
{
	while (s && *s && g_npad < 256)
	{
		PadStep &p = g_pad[g_npad];
		if (sscanf (s, "%d-%d:%x", &p.f0, &p.f1, &p.b) == 3) g_npad++;
		s = strchr (s, ';');
		if (s) s++;
	}
}
static void padAt (Machine &m, int f)
{
	u32 b = 0;
	for (int i = 0; i < g_npad; i++) if (f >= g_pad[i].f0 && f <= g_pad[i].f1) b |= g_pad[i].b;
	int sx = (b & 0x10000) ? -100 : (b & 0x20000) ? 100 : 0, sy = (b & 0x80000) ? -100 : (b & 0x40000) ? 100 : 0;
	m.setPad (0, b & 0xFFFF, sx, sy, 0, 0, 0, 0);
}

int main (int argc, char **argv)
{
	if (argc < 3) return 2;
	g_every = argc > 3 ? atoi (argv[3]) : 1;
	g_snap = getenv ("GC_SNAP");
	if (!setup ()) return 1;
	static Machine *mp = new Machine;
	Machine &m = *mp;
	padParse (getenv ("GC_PAD"));
	g_pa = new unsigned[640 * 576]; g_pb = new unsigned[640 * 576];
	g_f = fopen (argv[1], "rb");
	if (!g_f) return 1;
	fseeko (g_f, 0, SEEK_END);
	long sz = ftello (g_f);
	fseeko (g_f, 0, SEEK_SET);
	static u8 card[Machine::CARD_SIZE];
	memset (card, 0xFF, sizeof card);
	if (const char *c = getenv ("GC_CARD")) { FILE *cf = fopen (c, "rb"); if (cf) { size_t k = fread (card, 1, sizeof card, cf); (void) k; fclose (cf); } }
	m.cardInsert (0, card, sizeof card);
	size_t n = strlen (argv[1]);
	bool ok;
	if (n > 4 && !strcmp (argv[1] + n - 4, ".dol"))
	{
		u8 *d = new u8[sz];
		size_t k = fread (d, 1, sz, g_f); (void) k;
		ok = m.loadDol (d, (u32) sz);
	}
	else { m.discRead = discRead; ok = m.loadDiscImage ((u32) sz); }
	if (!ok) return 1;
	codeAlloc = hostCode;
	const char *j = getenv ("GC_JIT");
	if ((!j || *j != '0') && !m.jitEnable ()) fprintf (stderr, "pas de JIT\n");
	eglMakeCurrent (g_dpy, g_sGl, g_sGl, g_cGl);
	char err[512] = "";
	g_ref = gxgl_create (1, err, sizeof err);
	if (!g_ref) { fprintf (stderr, "gxgl : %s\n", err); return 1; }
	if (!g_rec.init ()) return 1;
	m.gpu = &g_tee;
	int fields = atoi (argv[2]);
	for (g_field = 1; g_field <= fields; g_field++)
	{
		padAt (m, g_field);
		m.runFrame ();
		s16 pcm[4096 * 2];
		while (m.audioRead (pcm, 4096) > 0) {}
		if (m.halted) { printf ("arret : %s\n", m.haltMsg); break; }
	}
	if (const char *o = getenv ("GC_GXS"))		// (le cache que gcemu ecrirait)
	{
		FILE *f = fopen (o, "wb");
		unsigned h[2] = { 0x31535847, sizeof (gxes::TevCfg) };
		fwrite (h, 4, 2, f);
		for (int i = 0; i < g_rec.nProg; i++) fwrite (&g_rec.prog[i].cfg, sizeof (gxes::TevCfg), 1, f);
		fclose (f);
	}
	if (getenv ("GC_DUMPFS"))
	{
		static char b[60000];
		for (int i = 0; i < g_rec.nProg; i++) { gxes::fragmentSource (g_rec.prog[i], b, sizeof b); printf ("---- %d\n%s", i, b); }
	}
	char st[256]; m.status (st, sizeof st);
	fprintf (stderr, "%s\n", st);
	return 0;
}
