//
// n64emu -- l'emulateur Nintendo 64 (docs/EMULATEURS.md) : le coeur C++ d'Onyx (n64/), sur EmuKit.
//     n64emu [rom.z64]
// Les listes d'affichage sont rendues par GpuKit dans l'image ; sans GPU (QEMU), par le rendu logiciel
// du BASIC 3D d'Onyx (bas3d.h). Le R4300 passe par le recompilateur (n64/n64_jit.cpp) ; le menu CPU
// revient a l'interprete.
//
#include <new>
#include <stdio.h>
#include <string.h>
#include <emukit.h>
#include <gpukit_abi.h>
#include <sys/mman.h>
#include "n64/n64.h"
#include "bas3d.h"

enum { OUT_W = 640, OUT_H = 480 };
static n64::Machine *g_m;
static void *g_bufs[3];
static int g_gpuTex[n64::Machine::MAX_TEX];
static bool g_gpu;
static unsigned g_lastSerial, g_gfxAge = 1000;

static bool gpu_frame (unsigned *px, int w, int h, int stride)
{
	if (!g_gpu || g_m->gfxReady < 0) return false;
	const n64::GFrame &F = g_m->gfxFrame[g_m->gfxReady];
	for (int i = 0; i < n64::Machine::MAX_TEX; i++)
	{
		n64::GTexture &T = g_m->tex[i];
		if (!T.dirty) continue;
		if (T.w <= 0 || T.h <= 0 || !T.px || T.w * T.h > (int) n64::Machine::TEX_PIXELS) continue;
		long r = gk_texture (g_gpuTex[i], (long) T.px, T.w, T.h, T.w);
		if (r < 0 && g_gpuTex[i] >= 0) r = gk_texture (-1, (long) T.px, T.w, T.h, T.w);
		g_gpuTex[i] = (int) r;
		T.dirty = false;
	}
	static struct { unsigned first, count; int tex; unsigned flags; float m[16]; } bt[n64::GFrame::MAXB];
	int nb = 0;
	for (int i = 0; i < F.nb; i++)
	{
		const n64::GBatch &B = F.b[i];
		bt[nb].first = B.first; bt[nb].count = B.count; bt[nb].flags = B.flags;
		bt[nb].tex = B.tex >= 0 ? g_gpuTex[B.tex] : -1;
		if (B.tex >= 0 && bt[nb].tex < 0) continue;
		for (int k = 0; k < 16; k++) bt[nb].m[k] = B.m[k];
		nb++;
	}
	struct { unsigned *px; int w, h, stride; unsigned clear, flags; } fr = { px, w, h, stride, F.clear, 0 };
	return gk_render ((long) &fr, (long) F.v, F.nv, (long) bt, nb) == 0;
}

static bool sw_frame (unsigned *px, int w, int h, int stride)
{
	if (g_m->gfxReady < 0) return false;
	const n64::GFrame &F = g_m->gfxFrame[g_m->gfxReady];
	int W = F.width, H = F.height;
	if (W <= 0 || H <= 0 || W > n64::FB_MAX_W || H > n64::FB_MAX_H) return false;
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
		unsigned *d = px + (long) y * stride;
		for (int x = 0; x < w; x++) d[x] = r[x * W / w];
	}
	return true;
}

static long load (void *, const unsigned char *rom, long n)
{
	return g_m->load (rom, (int) n) ? 1 : 0;
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
	int stride = (int) (pitch / 4);
	if (g_gfxAge < 30 && (gpu_frame (px, OUT_W, OUT_H, stride) || sw_frame (px, OUT_W, OUT_H, stride)))
	{
		g_m->fbSnapshot (px, OUT_W, OUT_H, stride);
		emu_frame_size (OUT_W, OUT_H);
		return;
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
enum { ID_INTERP = 100 };
static void *code_alloc (unsigned long n)
{
	void *p = mmap (0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}
static bool g_jit;
static void cmd (void *, long id)
{
	if (id != ID_INTERP || !g_jit) return;
	g_m->useJit = !g_m->useJit;
	puts (g_m->useJit ? "n64emu : recompilateur" : "n64emu : interprete");
}

static emu_desc d = { "Nintendo 64", "*.z64;*.n64;*.v64", OUT_W, OUT_H, n64::FB_MAX_W, n64::FB_MAX_H, 60000, 44100, 0,
		      load, frame, audio, reset, save, restore, "MCPU\nI100\tRecompiler / Interpreter\n", cmd, 0, g_bufs, 1, "Not a Nintendo 64 ROM." };

int main (int argc, char **argv)
{
	emukit_kit_open ();
	gpukit_kit_open ();
	g_m = new n64::Machine;
	g_m->setAudioRate (44100);
	g_jit = g_m->jitEnable (code_alloc);
	puts (g_jit ? "n64emu : recompilateur actif" : "n64emu : interprete seul (pas de memoire executable)");
	g_gpu = gk_info (0) == 1;
	for (int i = 0; i < n64::Machine::MAX_TEX; i++) g_gpuTex[i] = -1;
	if (g_gpu) for (int i = 0; i < 3; i++) g_bufs[i] = (void *) gk_alloc (n64::FB_MAX_W * n64::FB_MAX_H * 4);
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
