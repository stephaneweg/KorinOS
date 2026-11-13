//
// n64emu (C++) -- l'emulateur Nintendo 64 en C++ (docs/EMULATEURS.md, docs/etude/PC-PILOTES.md « Emulateurs d'Onyx,
// Pi et PC ») : le coeur d'Onyx (n64/), son recompilateur a deux generateurs (n64/n64_jit.cpp : AArch64 et x86-64),
// sur EmuKit.
//     n64emu [rom.z64]
// Les listes d'affichage sont rendues par le rendu logiciel du BASIC 3D d'Onyx (bas3d.h) ; le rendu par le GPU (Mesa,
// gl/n64gl.c) reste a l'assembleur (n64emu.asm, tools/emu/build-n64.sh), qui est encore l'emulateur du Pi. Construit
// pour le PC par tools/pc/apps.sh (-DN64_CPP : gl/ n'y entre pas). Le menu CPU revient a l'interprete.
// Sur le PC (x86-64), avec le GPU Intel : les memes sommets et lots (le format de GpuKit) passent par gk_draw, que
// libgpukit_gl.a (GlKit, Mesa iris) rend a la taille de la fenetre ; sans GPU, le rendu logiciel ci-dessous.
//
#include <new>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <emukit.h>
#include "n64/n64.h"
#include "bas3d.h"
#if defined(__x86_64__)
#include <gpukit_abi.h>
#include "gpukit_gl.h"
static bool g_gk;					// (le PC : GpuKit rend par GlKit)
#endif

enum { OUT_W = 640, OUT_H = 480, ID_INTERP = 100 };
static n64::Machine *g_m;
static unsigned g_lastSerial, g_gfxAge = 1000, g_drawn = ~0u;
static bool g_jit;
static struct emu_desc d;

static bool sw_frame (unsigned *px, int w, int h, int stride)
{
	if (g_m->gfxReady < 0) return false;
	const n64::GFrame &F = g_m->gfxFrame[g_m->gfxReady];
	int W = F.width, H = F.height;
	if (W <= 0 || H <= 0 || W > n64::FB_MAX_W || H > n64::FB_MAX_H) return false;
#if defined(__x86_64__)
	if (g_gk)
	{
		// GpuKit par GlKit : la cible a la taille de la sortie, les textures de la machine telles quelles
		struct GkFrame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
		struct GkTex { const unsigned *px; int w, h, id, pad; };
		static_assert (sizeof (GkFrame) == GKF_SIZE && sizeof (GkTex) == GKT_SIZE && sizeof (n64::GVertex) == GKV_SIZE
			       && sizeof (n64::GBatch) == GKB_SIZE, "gpukit.inc");
		static GkTex gt[n64::Machine::MAX_TEX];
		for (int i = 0; i < n64::Machine::MAX_TEX; i++)
		{
			int tw = g_m->tex[i].w, th = g_m->tex[i].h;
			if (tw <= 0 || th <= 0 || tw * th > (int) n64::Machine::TEX_PIXELS) tw = th = 1;
			gt[i].px = g_m->tex[i].px; gt[i].w = tw; gt[i].h = th; gt[i].id = -1;
		}
		GkFrame f = { px, w, h, stride, F.clear, 0, 0 };
		long r = gk_draw ((long) &f, (long) F.v, F.nv, (long) F.b, F.nb, (long) gt);
		if (r == 1) return true;
		g_gk = false;					// (refuse, ou le GPU perdu : le rendu logiciel desormais)
		puts ("n64emu : GPU perdu, rendu logiciel");
		if (r == 0) return true;
	}
#endif
	static unsigned *pic; static float *zb;
	if (!pic) { pic = new unsigned[n64::FB_MAX_W * n64::FB_MAX_H]; zb = new float[n64::FB_MAX_W * n64::FB_MAX_H]; }
	static bas::G3Batch bt[n64::GFrame::MAXB];
	static bas::G3Texture tx[n64::Machine::MAX_TEX];
	for (int i = 0; i < F.nb; i++)
	{
		const n64::GBatch &B = F.b[i];
		bt[i].first = B.first; bt[i].count = B.count; bt[i].flags = B.flags;
		bt[i].texture = B.tex >= 0 ? B.tex + 1 : 0;
		for (int k = 0; k < 16; k++) bt[i].m[k] = B.m[k];
	}
	for (int i = 0; i < n64::Machine::MAX_TEX; i++)
	{
		int tw = g_m->tex[i].w, th = g_m->tex[i].h;
		if (tw <= 0 || th <= 0 || tw * th > (int) n64::Machine::TEX_PIXELS) tw = th = 1;
		tx[i].px = g_m->tex[i].px; tx[i].w = tw; tx[i].h = th;
	}
	bas::swRender (pic, W, H, W, zb, (const bas::G3Vertex *) F.v, F.nv, bt, F.nb, F.clear, false,
		       [] (int t) -> const bas::G3Texture * { return &tx[t - 1]; });
	for (int y = 0; y < h; y++)
	{
		const unsigned *r = pic + (y * H / h) * W;
		unsigned *o = px + (long) y * stride;
		for (int x = 0; x < w; x++) o[x] = r[x * W / w];
	}
	return true;
}

// (comme l'asm : la cadence suit la region de la ROM, 50 Hz en PAL)
static long load (void *, const unsigned char *rom, long n)
{
	if (!g_m->load (rom, (int) n)) return 0;
	d.fps_milli = g_m->pal ? 50000 : 60000;
	return 1;
}

static void frame (void *, unsigned *px, long pitch, long pad, long flags)
{
	int b = 0, x = 0, y = 0;
	if (pad & PAD_A) b |= n64::BTN_A;
	if (pad & PAD_X) b |= n64::BTN_B;
	if (pad & (PAD_L2 | PAD_R2)) b |= n64::BTN_Z;
	if (pad & PAD_L) b |= n64::BTN_L;
	if (pad & PAD_R) b |= n64::BTN_R;
	if (pad & PAD_START) b |= n64::BTN_START;
	if (pad & PAD_UP) b |= n64::BTN_DUP;
	if (pad & PAD_DOWN) b |= n64::BTN_DDOWN;
	if (pad & PAD_LEFT) b |= n64::BTN_DLEFT;
	if (pad & PAD_RIGHT) b |= n64::BTN_DRIGHT;
	if (pad & PAD_Y) b |= n64::BTN_CUP;
	if (pad & PAD_B) b |= n64::BTN_CRIGHT;
	short ax[4];
	for (int p = 3; p >= 0; p--)					// (le stick d'une manette branchee)
	{
		emu_pad_axes (p, (long) ax);
		if (ax[0] || ax[1]) { x = ax[0] * 80 / 1000; y = -ax[1] * 80 / 1000; }
		if (ax[2] > 500) b |= n64::BTN_CRIGHT;
		if (ax[2] < -500) b |= n64::BTN_CLEFT;
		if (ax[3] > 500) b |= n64::BTN_CDOWN;
		if (ax[3] < -500) b |= n64::BTN_CUP;
	}
	if (!x && !y)							// (au clavier : les fleches font aussi le stick)
	{
		if (pad & PAD_RIGHT) x = 80;
		if (pad & PAD_LEFT) x = -80;
		if (pad & PAD_UP) y = 80;
		if (pad & PAD_DOWN) y = -80;
	}
	g_m->setPad (0, (unsigned) b, x, y);
	g_m->runFrame ();
	if (flags & EMF_SKIP) return;
	if (g_m->gfxSerial != g_lastSerial) { g_lastSerial = g_m->gfxSerial; g_gfxAge = 0; }
	else if (g_gfxAge < 1000) g_gfxAge++;
	if (g_gfxAge < 30)
	{
		if (g_drawn == g_m->gfxSerial) { emu_frame_same (); return; }	// (une image du jeu pour plusieurs VI : rien de neuf)
		if (sw_frame (px, OUT_W, OUT_H, (int) (pitch / 4)))
		{
			g_drawn = g_m->gfxSerial;
			g_m->fbSnapshot (px, OUT_W, OUT_H, (int) (pitch / 4));
			emu_frame_size (OUT_W, OUT_H);
			return;
		}
	}
	int w = g_m->fbW, h = g_m->fbH;
	if (w <= 0 || h <= 0) return;
	for (int l = 0; l < h; l++) memcpy ((char *) px + l * pitch, g_m->fb + l * w, (size_t) w * 4);
	emu_frame_size (w, h);
}
static long audio (void *, short *lr, long n) { return g_m->audioRead (lr, (int) n); }
static long save (void *, const void **p)
{
	if (g_m->sramDirty) { g_m->sramDirty = false; *p = g_m->sram; return 0x8000; }
	if (g_m->eepromDirty) { g_m->eepromDirty = false; *p = g_m->eeprom; return g_m->eepromSize; }
	return 0;
}
static void restore (void *, const void *data, long n)
{
	if (n == 512 || n == 2048)
	{
		memcpy (g_m->eeprom, data, (size_t) n);
		g_m->eepromSize = (int) n; g_m->saveType = n == 512 ? 2 : 3;
	}
	else memcpy (g_m->sram, data, (size_t) n < sizeof g_m->sram ? (size_t) n : sizeof g_m->sram);
}
static void reset (void *) { g_m->reset (); }
static void *code_alloc (unsigned long n)
{
	void *p = mmap (0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}
static void cmd (void *, long id)
{
	if (id != ID_INTERP || !g_jit) return;
	g_m->useJit = !g_m->useJit;
	puts (g_m->useJit ? "n64emu : recompilateur" : "n64emu : interprete");
}

extern "C" void *__armpi_appkit (unsigned rank);
static const char *lang_text (const char *s)
{
	const char *(*f) (const char *) = (const char *(*) (const char *)) __armpi_appkit (42);
	return f ? f (s) : s;
}

int main (int argc, char **argv)
{
	if (!emukit_kit_open ()) return 1;
#if defined(__x86_64__)
	g_gk = gpukit_kit_open () && gk_gl_attach () == 1;
	printf ("n64emu : rendu par %s\n", g_gk ? gk_gl_describe () : "le processeur (rendu logiciel)");
#endif
	g_m = new n64::Machine;
	g_m->setAudioRate (44100);
	g_jit = g_m->jitEnable (code_alloc);
	puts (g_jit ? "n64emu : recompilateur actif" : "n64emu : interprete seul (pas de memoire executable)");
	d.name = "Nintendo 64"; d.ext = "*.z64;*.n64;*.v64";
	d.w = OUT_W; d.h = OUT_H; d.maxw = n64::FB_MAX_W; d.maxh = n64::FB_MAX_H;
	d.fps_milli = 60000; d.rate = 44100;
	d.load = load; d.frame = frame; d.audio = audio; d.reset = reset; d.save = save; d.restore = restore;
	d.menu = "MCPU\nI100\tRecompiler / Interpreter\n"; d.cmd = cmd; d.skip = 1;
	d.error = lang_text ("Not a Nintendo 64 ROM.");
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
