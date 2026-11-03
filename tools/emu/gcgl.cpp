// gcgl -- gcemu sans EmuKit, pour mesurer sur le Pi sans rien changer au systeme (docs/EMULATEURS.md) : la machine, le GX
// sur son thread, GL dans une fenetre, sans son ni cadence : la machine va aussi vite qu'elle peut.
// N'ecrit jamais a cote du disque : la carte reste en memoire, seules les images de --snap vont sur le disque.
//     gcgl disque.iso [--fields n] [--snap n dossier] [--pad "f0-f1:hex;..."] [--card f.sav] [--interp] [--dry]
//          [--shaders f.gxs] [--w l] [--h h] [--oneth]
// Une ligne par seconde : trames/s, images/s, ms par trame de la machine, attente de la FIFO, temps du GL.
// --snap : une image PPM de l'EFB toutes les n images. --shaders : chaque configuration du fichier par Mesa, temps et refus.
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <armgl.h>
#include "gc/gc.h"
#include "gxes.h"
extern "C" {
#include <uikit.h>
}
using namespace gc;

static struct ui_vars ui;
static Machine *g_m;
static gxes::Rec g_rec;
static gxes::Player g_pl;
static FILE *g_disc;
static void *g_ctx, *g_surf;
static volatile int g_stop, g_fields;
static int g_maxFields = 0, g_snapN = 0, g_w = 640, g_h = 480;
static const char *g_snapDir = "SD0:/gcgl";
static volatile unsigned long long g_emuUs, g_gxUs;

static inline unsigned long long us () { timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (unsigned long long) t.tv_sec * 1000000ull + (unsigned long long) t.tv_nsec / 1000; }
static void *code_alloc (unsigned n)
{
	void *p = mmap (0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}
static bool disc_read (void *, u32 off, u32 len, u8 *dst) { return !fseeko (g_disc, off, SEEK_SET) && fread (dst, 1, len, g_disc) == len; }

struct PadStep { int f0, f1; u32 b; };
static PadStep g_pad[256]; static int g_npad;
static void pad_parse (const char *s)
{
	while (s && *s && g_npad < 256)
	{
		PadStep &p = g_pad[g_npad];
		if (sscanf (s, "%d-%d:%x", &p.f0, &p.f1, &p.b) == 3) g_npad++;
		s = strchr (s, ';');
		if (s) s++;
	}
}
static void *machine_thread (void *)
{
	static s16 pcm[8192];
	int f = 0;
	while (!g_stop && (!g_maxFields || f < g_maxFields))
	{
		f++;
		u32 b = 0;
		for (int i = 0; i < g_npad; i++) if (f >= g_pad[i].f0 && f <= g_pad[i].f1) b |= g_pad[i].b;
		g_m->setPad (0, b & 0xFFFF, (b & 0x10000) ? -100 : (b & 0x20000) ? 100 : 0, (b & 0x80000) ? -100 : (b & 0x40000) ? 100 : 0, 0, 0, 0, 0);
		unsigned long long t0 = us ();
		g_m->runFrame ();
		g_emuUs = g_emuUs + (us () - t0);
		while (g_m->audioRead (pcm, 4096) > 0) {}
		g_fields = f;
	}
	g_stop = 1;
	return 0;
}
static void *gx_thread (void *)
{
	unsigned idle = 0;
	while (!g_stop)
	{
		unsigned long long t0 = us ();
		if (g_m->gxStep ()) { idle = 0; g_gxUs = g_gxUs + (us () - t0); continue; }
		if (++idle < 256) { for (int i = 0; i < 64; i++) __asm__ volatile ("yield"); }
		else sched_yield ();
	}
	return 0;
}
static void present (void *) { armgl_swap (g_surf); }

static void ppm (const char *n, const unsigned *px, int w, int h)
{
	FILE *o = fopen (n, "wb");
	if (!o) return;
	fprintf (o, "P6\n%d %d\n255\n", w, h);
	static u8 line[640 * 3];
	for (int y = 0; y < h; y++)
	{
		for (int x = 0; x < w; x++) { u32 c = px[y * 640 + x]; line[x * 3] = (u8) (c >> 16); line[x * 3 + 1] = (u8) (c >> 8); line[x * 3 + 2] = (u8) c; }
		fwrite (line, 1, (size_t) w * 3, o);
	}
	fclose (o);
}

static int shader_test (const char *path)
{
	FILE *c = fopen (path, "rb");
	unsigned h[2];
	if (!c || fread (h, 4, 2, c) != 2 || h[1] != sizeof (gxes::TevCfg)) { printf ("gcgl : %s : pas un cache\n", path); return 1; }
	static gxes::TevCfg t;
	int n = 0;
	while (fread (&t, sizeof t, 1, c) == 1 && g_rec.addCfg (t) >= 0) n++;
	fclose (c);
	int bad = 0, big = 0;
	long worst = 0, total = 0;
	for (int i = 0; i < n; i++)
	{
		unsigned long long a = us ();
		if (!g_pl.precompile (g_rec, i)) { bad++; printf ("%d : REFUSE, %u etages\n", i, g_rec.prog[i].cfg.nStages); }
		long d = (long) (us () - a);
		total += d;
		if (d > worst) worst = d;
		if (d > 16000) big++;
		if (i < 200) printf ("%d : %u etages, ind %u, %ld ms\n", i, g_rec.prog[i].cfg.nStages, g_rec.prog[i].cfg.nInd, d / 1000);
	}
	armgl_finish ();
	printf ("gcgl : %d shaders, %d refuses, %ld ms en tout, pire %ld ms, %d au-dela de 16 ms\n", n, bad, total / 1000, worst / 1000, big);
	return bad != 0;
}

int main (int argc, char **argv)
{
	const char *path = 0, *shaders = 0, *cardf = 0;
	bool jit = true, dry = false, oneThread = false;
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (!strcmp (a, "--fields") && i + 1 < argc) g_maxFields = atoi (argv[++i]);
		else if (!strcmp (a, "--snap") && i + 2 < argc) { g_snapN = atoi (argv[++i]); g_snapDir = argv[++i]; }
		else if (!strcmp (a, "--pad") && i + 1 < argc) pad_parse (argv[++i]);
		else if (!strcmp (a, "--card") && i + 1 < argc) cardf = argv[++i];
		else if (!strcmp (a, "--shaders") && i + 1 < argc) shaders = argv[++i];
		else if (!strcmp (a, "--w") && i + 1 < argc) g_w = atoi (argv[++i]);
		else if (!strcmp (a, "--h") && i + 1 < argc) g_h = atoi (argv[++i]);
		else if (!strcmp (a, "--interp")) jit = false;
		else if (!strcmp (a, "--dry")) dry = true;
		else if (!strcmp (a, "--oneth")) oneThread = true;
		else path = a;
	}
	setvbuf (stdout, 0, _IOLBF, 0);
	int mode = armgl_init (dry ? ARMGL_F_DRY : 0);
	printf ("gcgl : armgl_init %d\n", mode);
	if (mode == ARMGL_NONE) return 1;
	g_ctx = armgl_context (3, 0, 0);
	void *off = armgl_surface (16, 16);
	if (!g_ctx || !off || !armgl_make_current (g_ctx, off)) { printf ("gcgl : pas de contexte\n"); return 1; }
	unsigned long long t0 = us ();
	if (!g_rec.init () || !g_pl.init ((void *(*) (const char *)) armgl_proc)) { printf ("gcgl : GL refuse : %s\n", g_pl.st.err ? g_pl.st.err : "?"); return 1; }
	printf ("gcgl : lecteur pret en %llu ms\n", (us () - t0) / 1000);
	if (shaders) return shader_test (shaders);
	if (!path) return 2;
	if (!uikit_open (&ui) || ui_window (g_w, g_h, (long) "gcgl") <= 0) { printf ("gcgl : pas de fenetre\n"); return 1; }
	g_surf = armgl_window (ui.win, 0, 0, g_w, g_h);
	if (!g_surf || !armgl_make_current (g_ctx, g_surf)) { printf ("gcgl : pas de surface\n"); return 1; }
	g_m = new (std::nothrow) Machine;
	if (!g_m) { printf ("gcgl : pas de memoire\n"); return 1; }
	g_disc = fopen (path, "rb");
	if (!g_disc) { printf ("gcgl : pas de %s\n", path); return 1; }
	fseeko (g_disc, 0, SEEK_END);
	long sz = ftello (g_disc);
	static u8 *card = new u8[Machine::CARD_SIZE];
	memset (card, 0xFF, Machine::CARD_SIZE);
	if (cardf) { FILE *c = fopen (cardf, "rb"); if (c) { size_t k = fread (card, 1, Machine::CARD_SIZE, c); (void) k; fclose (c); } }
	g_m->cardInsert (0, card, Machine::CARD_SIZE);
	g_m->setAudioRate (44100);
	g_m->discRead = disc_read;
	if (!g_m->loadDiscImage ((u32) sz)) { printf ("gcgl : disque refuse\n"); return 1; }
	codeAlloc = code_alloc;
	if (jit && !g_m->jitEnable ()) printf ("gcgl : pas de recompilateur\n");
	g_m->gpu = &g_rec;
	printf ("gcgl : %s, %s, %ld octets\n", g_m->title, g_m->pal ? "PAL" : "NTSC", sz);
	pthread_t tm, tg;
	pthread_attr_t at;
	pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, 1 << 20);
	if (!oneThread) { g_m->gxAsync = true; pthread_create (&tg, &at, gx_thread, 0); }
	pthread_create (&tm, &at, machine_thread, 0);
	unsigned long long lastWait = 0, last = us(), lastEmu = 0, lastGx = 0, glUs = 0, attUs = 0;
	int lastF = 0; u32 lastFrames = 0, lastComp = 0; u64 lastCu = 0;
	static unsigned *snap = g_snapN ? new unsigned[640 * 576] : 0;
	g_pl.grab = 0;
	while (!g_stop)
	{
		ui_event (0);
		unsigned long long a = us ();
		u32 fr = g_pl.st.frames;
		if (snap && (fr + 1) % g_snapN == 0) g_pl.grab = snap;
		long n = g_pl.pump (g_rec, *g_m, present, 0, g_w, g_h, 0);
		if (g_pl.grab && g_pl.st.frames != fr)
		{
			g_pl.grab = 0;
			char nm[128];
			snprintf (nm, sizeof nm, "%s/%05u.ppm", g_snapDir, g_pl.st.frames);
			// (readEfb : rangee serree ; ppm attend un pas de 640)
			int w = g_pl.lastW, h = g_pl.lastH;
			for (int y = h - 1; y > 0; y--) memmove (snap + y * 640, snap + y * w, (size_t) w * 4);
			ppm (nm, snap, w, h);
		}
		if (n) glUs += us () - a;
		else if (g_rec.rd == g_rec.wr) sched_yield ();
		unsigned long long t = us ();
		if (t - last >= 1000000)
		{
			int f = g_fields;
			unsigned long long e = g_emuUs, g = g_gxUs;
			unsigned long long w = g_m->gxWaitTicks + g_rec.waitTicks, rate;
			__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (rate));
			u32 frames = g_pl.st.frames - lastFrames;
			printf ("%d trames, %.1f trames/s, %u images/s, machine %.1f ms/trame, GX %.1f ms/trame, GL %.1f ms/image, la machine attend le GX ou GL %.1f ms/trame, %d prog (+%u, %.0f ms), lots/image %u, sautees %u\n",
				f, (f - lastF) * 1e6 / (double) (t - last), frames, f != lastF ? (double) (e - lastEmu) / (f - lastF) / 1000 : 0, f != lastF ? (double) (g - lastGx) / (f - lastF) / 1000 : 0,
				frames ? (double) glUs / frames / 1000 : 0, f != lastF ? (double) (w - lastWait) * 1000.0 / (double) rate / (f - lastF) : 0, g_rec.nProg, g_pl.st.compiled - lastComp, (double) (g_pl.st.compileUs - lastCu) / 1000,
				frames ? g_pl.st.batches / (g_pl.st.frames ? g_pl.st.frames : 1) : 0, g_pl.st.skipped);
			lastWait = w; last = t; lastEmu = e; lastGx = g; lastF = f; lastFrames = g_pl.st.frames; lastComp = g_pl.st.compiled; lastCu = g_pl.st.compileUs; glUs = 0;
		}
	}
	g_rec.quit = 1;
	double fin, cache, attach;
	armgl_times (g_surf, &fin, &cache, &attach);
	printf ("gcgl : fin. %d trames, %u images, %u lots, %d programmes, %u compiles, %u repli, %u refuses, compilation %llu ms (max %llu), copies %u, textures %u ; par image : GPU %.2f ms, cache %.2f, rattachement %.2f\n",
		(int) g_fields, g_pl.st.frames, g_pl.st.batches, g_rec.nProg, g_pl.st.compiled, g_pl.st.fallback, g_pl.st.failed,
		(unsigned long long) g_pl.st.compileUs / 1000, (unsigned long long) g_pl.st.maxCompileUs / 1000, g_pl.st.copies, g_pl.st.uploads, fin, cache, attach);
	char st[256]; g_m->status (st, sizeof st);
	printf ("%s\n", st);
	return 0;
}
