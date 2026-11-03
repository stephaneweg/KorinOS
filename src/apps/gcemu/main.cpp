//
// gcemu -- l'emulateur GameCube (docs/EMULATEURS.md) : le coeur d'Onyx (gc/), son recompilateur PowerPC vers A64 compris,
// sur EmuKit, le GX sur OpenGL ES 3.0 par Mesa (gxes.h).
//     gcemu [--full] [--interp] [--nogl] [--gldry] [--log fichier] disque.iso | disque.gcm | programme.dol
// Trois threads : la machine (EmuKit), le GX (la FIFO, l'enregistreur), la fenetre (GL : le contexte y vit, chaque
// tour d'EmuKit y joue les images finies). Sans GPU : le GX sur la machine, les images dessinees par soft.cpp.
//   Le disque reste sur la carte (emu_rom_by_path) : 1,4 Go, lus a la demande par le thread de la machine.
//   La carte memoire en A (le fichier de Dolphin et d'Onyx) et le cache des shaders vont dans SD0:/home/gamelib/gc :
//   rien n'est jamais ecrit a cote du disque, SD1 est intouchable. Un <disque>.sav n'y est que lu, la premiere fois.
//
#include <new>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <armgl.h>
#include "gc/gc.h"
#include "gxes.h"
#include "soft.h"
// (les boutons de la console sous d'autres noms : emukit.h fait des PAD_* des macros)
enum
{
	GB_LEFT = gc::Machine::PAD_LEFT, GB_RIGHT = gc::Machine::PAD_RIGHT, GB_DOWN = gc::Machine::PAD_DOWN, GB_UP = gc::Machine::PAD_UP,
	GB_Z = gc::Machine::PAD_Z, GB_R = gc::Machine::PAD_R, GB_L = gc::Machine::PAD_L, GB_A = gc::Machine::PAD_A, GB_B = gc::Machine::PAD_B,
	GB_X = gc::Machine::PAD_X, GB_Y = gc::Machine::PAD_Y, GB_START = gc::Machine::PAD_START
};
#include <emukit.h>

enum { SND_RATE = 44100, FIELDS_STALE = 30 };
static gc::Machine *g_m;
static gxes::Rec g_rec;
static gxes::Player g_pl;
static FILE *g_disc;
static unsigned char *g_card; static unsigned g_cardSize = gc::Machine::CARD_SIZE;
static bool g_wantJit = true, g_noGl, g_dry;
static gc::Jit *g_jit;
static const char *g_log;
// GL
static void *g_ctx, *g_off, *g_surf; static long g_win; static int g_vx, g_vy, g_vw, g_vh;
static volatile int g_glOn;				// le GX va vers le GPU
static volatile int g_fbNew;				// une image du VI a montrer (le jeu ecrit lui-meme son XFB)
static gc::u32 g_seenWr; static int g_age = 1000;
// <disque>.gxs : les configurations du TEV deja vues, compilees au debut de la partie suivante
enum { GXS_MAGIC = 0x31535847 };
static char g_gxs[544], g_sav[544]; static int g_gxsDone, g_gxsPre, g_gxsCount; static volatile int g_shaderOnly;
static volatile int g_gxStop;

static inline void relax () { for (int i = 0; i < 64; i++) __asm__ volatile ("yield"); }

static void *code_alloc (unsigned n)
{
	void *p = mmap (0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}
static bool disc_read (void *, gc::u32 off, gc::u32 len, gc::u8 *dst)
{
	return !fseeko (g_disc, off, SEEK_SET) && fread (dst, 1, len, g_disc) == len;
}
static bool ends (const char *s, const char *e)
{
	size_t a = strlen (s), b = strlen (e);
	if (a < b) return false;
	for (size_t i = 0; i < b; i++) { char c = s[a - b + i]; if (c >= 'A' && c <= 'Z') c = (char) (c + 32); if (c != e[i]) return false; }
	return true;
}

// le thread du GX : la FIFO a mesure que la machine l'ecrit
static void gx_thread (long)
{
	unsigned idle = 0;
	while (!g_gxStop)
	{
		if (__atomic_load_n (&g_m->gxAsync, __ATOMIC_ACQUIRE) && g_m->gxStep ()) { idle = 0; continue; }
		if (++idle < 256) relax ();
		else if (idle < 4096) sched_yield ();
		else { struct timespec t = { 0, 1000000 }; nanosleep (&t, 0); }
	}
}

static long load (void *, const unsigned char *, long n)
{
	const char *path = (const char *) emu_rom_path ();
	if (g_disc) { fclose (g_disc); g_disc = 0; }
	FILE *f = fopen (path, "rb");
	if (!f) return 0;
	if (!g_card) { g_card = new unsigned char[g_cardSize]; memset (g_card, 0xFF, g_cardSize); }
	g_m->cardInsert (0, g_card, g_cardSize);
	g_m->setAudioRate (SND_RATE);
	bool ok;
	if (ends (path, ".dol"))
	{
		unsigned char *d = new (std::nothrow) unsigned char[n + 1];
		ok = d && fread (d, 1, (size_t) n, f) == (size_t) n && g_m->loadDol (d, (unsigned) n);
		delete [] d;
		fclose (f);
	}
	else
	{
		g_disc = f;
		g_m->discRead = disc_read;
		ok = g_m->loadDiscImage ((unsigned) n);
	}
	if (!ok) return 0;
	// nos fichiers, sur SD0 : par le nom du disque
	const char *base = path;
	for (const char *q = path; *q; q++) if (*q == '/' || *q == ':') base = q + 1;
	mkdir ("SD0:/home/gamelib", 0777);
	mkdir ("SD0:/home/gamelib/gc", 0777);
	snprintf (g_gxs, sizeof g_gxs, "SD0:/home/gamelib/gc/%s", base);
	char *dot = strrchr (g_gxs, '.');
	if (dot) *dot = 0;
	strcpy (g_sav, g_gxs);
	strcat (g_gxs, ".gxs"); strcat (g_sav, ".sav");
	// la carte : la notre, sinon celle d'a cote du disque, seulement lue
	FILE *cf = fopen (g_sav, "rb");
	if (!cf)
	{
		static char side[544];
		strncpy (side, path, sizeof side - 5);
		char *e = strrchr (side, '.');
		strcpy (e ? e : side + strlen (side), ".sav");
		cf = fopen (side, "rb");
	}
	if (cf) { size_t got = fread (g_card, 1, g_cardSize, cf); fclose (cf); if (got != g_cardSize) memset (g_card, 0xFF, g_cardSize); }
	if (g_glOn && !g_m->gpu)
	{
		FILE *c = fopen (g_gxs, "rb");
		unsigned h[2];
		if (c && fread (h, 4, 2, c) == 2 && h[0] == GXS_MAGIC && h[1] == sizeof (gxes::TevCfg))
		{
			static gxes::TevCfg t;
			while (fread (&t, sizeof t, 1, c) == 1 && g_rec.addCfg (t) >= 0) g_gxsCount++;
		}
		if (c) fclose (c);
		g_gxsDone = g_gxsCount;
	}
	gc::codeAlloc = code_alloc;
	if (g_m->jitEnable ()) g_jit = g_m->jit;
	if (!g_wantJit) g_m->jit = 0;
	return 1;
}

static void frame (void *, unsigned *px, long pitch, long pad, long)
{
	// le GX : sur son thread pour le GPU, ici pour le repli
	bool gl = g_glOn != 0;
	if ((g_m->gpu != 0) != gl)
	{
		g_m->gxSync ();
		g_m->gpu = gl ? (gc::GxGpu *) &g_rec : 0; g_m->gxsDirty = true;
		g_m->gxDoneW = g_m->piFifoWptr & 0x03FFFFFF;
		__atomic_store_n (&g_m->gxAsync, gl, __ATOMIC_RELEASE);
	}
	gc::u32 b = 0;
	if (pad & PAD_A) b |= GB_A;
	if (pad & PAD_X) b |= GB_B;
	if (pad & PAD_B) b |= GB_X;
	if (pad & PAD_Y) b |= GB_Y;
	if (pad & (PAD_L | PAD_R)) b |= GB_Z;
	if (pad & PAD_START) b |= GB_START;
	if (pad & PAD_UP) b |= GB_UP;
	if (pad & PAD_DOWN) b |= GB_DOWN;
	if (pad & PAD_LEFT) b |= GB_LEFT;
	if (pad & PAD_RIGHT) b |= GB_RIGHT;
	int l = 0, r = 0;
	if (pad & PAD_L2) { b |= GB_L; l = 255; }
	if (pad & PAD_R2) { b |= GB_R; r = 255; }
	short ax[4] = { 0, 0, 0, 0 };
	emu_pad_axes (0, (long) ax);
	int x = ax[0] / 10, y = -ax[1] / 10, cx = ax[2] / 10, cy = -ax[3] / 10;
	if (!x && !y)					// (le clavier et la croix : le stick aussi, comme Onyx)
	{
		if (pad & PAD_LEFT) x = -100; else if (pad & PAD_RIGHT) x = 100;
		if (pad & PAD_UP) y = 100; else if (pad & PAD_DOWN) y = -100;
	}
	g_m->setPad (0, b, x, y, cx, cy, l, r);
	g_m->runFrame ();
	if (gl)
	{
		gc::u32 w = g_rec.wr;
		if (w != g_seenWr) { g_seenWr = w; g_age = 0; } else if (g_age < 1000) g_age++;
		if (g_age >= FIELDS_STALE) g_fbNew = 1;
		emu_frame_same ();
		return;
	}
	if (g_m->gfxReady >= 0 && g_m->xfbIsCopy ())
	{
		const gc::GFrame &F = g_m->gfxFrame[g_m->gfxReady];
		softFrame (*g_m, F, (gc::u32 *) px, 640, 480, pitch);
		emu_frame_size (640, 480);
		return;
	}
	int w = g_m->fbW, h = g_m->fbH;
	if (w > gc::Machine::FB_MAX_W) w = gc::Machine::FB_MAX_W;
	if (h > gc::Machine::FB_MAX_H) h = gc::Machine::FB_MAX_H;
	for (int yy = 0; yy < h; yy++) memcpy ((char *) px + yy * pitch, g_m->fb + yy * w, (size_t) w * 4);
	emu_frame_size (w, h);
}

static long audio (void *, short *lr, long n) { return g_m->audioRead (lr, (int) n); }
static void card_write ()
{
	if (!g_m->card[0].dirty || !g_sav[0]) return;
	g_m->card[0].dirty = false;
	FILE *f = fopen (g_sav, "wb");
	if (!f) return;
	fwrite (g_card, 1, g_cardSize, f);
	fclose (f);
}
// (pas EMD_SAVE : EmuKit ecrit a cote de la ROM)
static void save_thread (long)
{
	for (;;)
	{
		struct timespec t = { 3, 0 };
		nanosleep (&t, 0);
		card_write ();
	}
}
static void reset (void *)
{
	g_m->reset ();
	g_m->jit = g_wantJit ? g_jit : 0;
	g_m->jitFlush = true;
	g_rec.flushPlan ();
}
static void cmd (void *, long id)
{
	if (id == 100) { g_wantJit = !g_wantJit; g_m->jit = g_wantJit ? g_jit : 0; g_m->jitFlush = true; }
}

// ---- la fenetre : GL --------------------------------------------------------------------------------------------
static void present (void *) { armgl_swap (g_surf); }

// (le thread de la fenetre, a chaque tour d'EmuKit) -> images montrees
static long gl_hook (void *, long win, long x, long y, long w, long h)
{
	if (win != g_win || x != g_vx || y != g_vy || w != g_vw || h != g_vh)
	{
		armgl_make_current (g_ctx, g_off);
		if (g_surf) { armgl_window_free (g_surf); g_surf = 0; }
		g_win = win; g_vx = (int) x; g_vy = (int) y; g_vw = (int) w; g_vh = (int) h;
		g_surf = armgl_window (win, (int) x, (int) y, (int) w, (int) h);
		if (g_surf) armgl_make_current (g_ctx, g_surf);
		else fprintf (stderr, "gcemu : pas de surface GPU %ldx%ld\n", w, h);
	}
	if (!g_surf) return 0;
	// le cache : quelques programmes par tour, avant qu'un jeu ne les demande
	for (int k = 0; k < 2 && g_gxsPre < g_gxsCount; k++) g_pl.precompile (g_rec, g_gxsPre++);
	// les configurations neuves : au bout du fichier, des qu'elles sont compilees
	while (g_gxsDone < g_rec.nProg)
	{
		gxes::Prog &p = g_rec.prog[g_gxsDone];
		if (!p.program) break;
		if (p.program != ~0u)
		{
			FILE *c = fopen (g_gxs, g_gxsDone ? "ab" : "wb");
			if (c)
			{
				unsigned h[2] = { GXS_MAGIC, sizeof (gxes::TevCfg) };
				if (!g_gxsDone) fwrite (h, 4, 2, c);
				fwrite (&p.cfg, sizeof p.cfg, 1, c);
				fclose (c);
			}
		}
		g_gxsDone++;
	}
	long n = g_pl.pump (g_rec, *g_m, present, 0, (int) w, (int) h, 0);
	if (!n && g_fbNew)
	{
		g_fbNew = 0;
		g_pl.showFb (g_m->fb, g_m->fbW, g_m->fbH, g_m->fbW, present, 0, (int) w, (int) h, 0);
		n = 1;
	}
	static long lastLog;
	if (g_log && g_pl.st.frames / 300 != lastLog)
	{
		lastLog = g_pl.st.frames / 300;
		FILE *f = fopen (g_log, "a");
		if (f)
		{
			const gxes::Player::Stats &s = g_pl.st;
			fprintf (f, "images %u sautees %u lots %u programmes %d compiles %u repli %u refuses %u compile %llu ms (max %llu) copies %u textures %u, rec %llu sommets, %u laisses\n",
				 s.frames, s.skipped, s.batches, g_rec.nProg, s.compiled, s.fallback, s.failed, (unsigned long long) s.compileUs / 1000, (unsigned long long) s.maxCompileUs / 1000,
				 s.copies, s.uploads, (unsigned long long) g_rec.drawVerts, g_rec.skipped + g_rec.dropped);
			fclose (f);
		}
	}
	return n;
}

// le GPU, avant la fenetre : un contexte sur une petite surface, le lecteur et ses shaders communs -> false : le repli
static bool gl_start ()
{
	if (g_noGl) return false;
	int mode = armgl_init (g_dry ? ARMGL_F_DRY : 0);
	if (mode == ARMGL_NONE) return false;
	g_ctx = armgl_context (3, 0, 0);
	g_off = armgl_surface (16, 16);
	if (!g_ctx || !g_off || !armgl_make_current (g_ctx, g_off)) return false;
	if (!g_rec.init () || !g_pl.init ((void *(*) (const char *)) armgl_proc))
	{
		fprintf (stderr, "gcemu : GL refuse : %s\n", g_pl.st.err ? g_pl.st.err : "?");
		return false;
	}
	return true;
}

static emu_desc d = { "GameCube", "*.iso;*.gcm;*.dol", 640, 480, gc::Machine::FB_MAX_W, gc::Machine::FB_MAX_H, 59940, SND_RATE, 0,
		      load, frame, audio, reset, 0, 0, "MCPU\nI100\tRecompiler / Interpreter\n", cmd, 0, 0, 0,
		      "Not a GameCube disc image (.iso, .gcm) or program (.dol), or its start failed." };

// (essai, gcemu --shaders fichier.gxs) chaque configuration du fichier passe par Mesa : temps, refus
static int shader_test (const char *path)
{
	FILE *c = fopen (path, "rb");
	unsigned h[2];
	if (!c || fread (h, 4, 2, c) != 2 || h[0] != GXS_MAGIC || h[1] != sizeof (gxes::TevCfg)) { printf ("gcemu : %s : pas un cache\n", path); return 1; }
	static gxes::TevCfg t;
	int n = 0;
	while (fread (&t, sizeof t, 1, c) == 1 && g_rec.addCfg (t) >= 0) n++;
	fclose (c);
	int bad = 0;
	struct timespec a, b;
	clock_gettime (CLOCK_MONOTONIC, &a);
	long worst = 0;
	for (int i = 0; i < n; i++)
	{
		struct timespec x, y;
		clock_gettime (CLOCK_MONOTONIC, &x);
		if (!g_pl.precompile (g_rec, i)) bad++;
		clock_gettime (CLOCK_MONOTONIC, &y);
		long us = (y.tv_sec - x.tv_sec) * 1000000 + (y.tv_nsec - x.tv_nsec) / 1000;
		if (us > worst) worst = us;
		printf ("%d : %u etages, %ld ms\n", i, g_rec.prog[i].cfg.nStages, us / 1000);
	}
	armgl_finish ();
	clock_gettime (CLOCK_MONOTONIC, &b);
	printf ("gcemu : %d shaders, %d refuses, %ld ms en tout, pire %ld ms\n", n, bad, (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000, worst / 1000);
	return bad != 0;
}

// (lang/fr.txt, dans le dictionnaire du systeme : ni UIKit ici, lang_tr d'AppKit par son rang)
extern "C" void *__armpi_appkit (unsigned rank);
static const char *lang_text (const char *s)
{
	const char *(*f) (const char *) = (const char *(*) (const char *)) __armpi_appkit (42);
	return f ? f (s) : s;
}

int main (int argc, char **argv)
{
	emukit_kit_open ();
	d.error = lang_text (d.error);
	const char *shaders = 0;
	int n = 1;
	for (int i = 1; i < argc; i++)			// (les nos, le reste pour EmuKit)
	{
		if (!strcmp (argv[i], "--interp")) g_wantJit = false;
		else if (!strcmp (argv[i], "--nogl")) g_noGl = true;
		else if (!strcmp (argv[i], "--gldry")) g_dry = true;
		else if (!strcmp (argv[i], "--log") && i + 1 < argc) g_log = argv[++i];
		else if (!strcmp (argv[i], "--shaders") && i + 1 < argc) shaders = argv[++i];
		else argv[n++] = argv[i];
	}
	argc = n;
	g_m = new (std::nothrow) gc::Machine;
	if (!g_m) return 1;
	emu_rom_by_path ();
	bool gl = gl_start ();
	if (shaders) return gl ? shader_test (shaders) : (printf ("gcemu : pas de GL\n"), 1);
	if (gl)
	{
		emu_gl_hook ((long) gl_hook, 0);
		g_glOn = 1;
		emu_thread ((long) gx_thread, 0, 0x100000, (long) "gc-gx");
	}
	emu_thread ((long) save_thread, 0, 0x10000, (long) "gc-save");
	long r = emu_run ((long) &d, emu_args (argc, argv));
	card_write ();
	g_rec.quit = 1;
	g_gxStop = 1;
	return (int) r;
}
