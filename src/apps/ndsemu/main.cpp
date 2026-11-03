//
// ndsemu -- l'emulateur Nintendo DS (docs/EMULATEURS.md) : le coeur C++ d'Onyx (nds/), son recompilateur
// ARM / Thumb vers A64 compris, sur EmuKit.
//     ndsemu [rom.nds]
// Les deux ecrans l'un sur l'autre ; l'ecran tactile a la souris. La 3D sur un thread a elle.
// Une ROM dont la zone protegee est encore chiffree demande SD0:/home/roms/bios7.bin, celui de l'utilisateur.
//
#include <new>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/mman.h>
#include <emukit.h>
#include "nds/nds.h"

enum { SW = nds::W, SH = nds::H };
static nds::Machine *g_m;
static volatile int g_touchDown, g_touchX, g_touchY;
static volatile unsigned g_r3dReq;
static bool g_r3dOn;

// (le thread de la 3D et la machine s'attendent sans dormir : l'attente dure moins d'une ligne)
static inline void relax () { for (int i = 0; i < 64; i++) __asm__ volatile ("yield"); }

static void r3d_thread (long)
{
	unsigned seen = 0;
	for (;;)
	{
		if (g_r3dReq != seen) { seen = g_r3dReq; __atomic_thread_fence (__ATOMIC_SEQ_CST); g_m->render3dNow (); }
		else sched_yield ();
	}
}
static void r3d_kick (void *)
{
	while (g_m->r3d.linesDone < SH) relax ();		// (l'image d'avant d'abord : ses polygones resservent)
	g_m->r3d.linesDone = 0;
	__atomic_thread_fence (__ATOMIC_SEQ_CST);
	g_r3dReq = g_r3dReq + 1;
}
static void r3d_wait (void *, int line)
{
	while (g_m->r3d.linesDone <= line) relax ();
	__atomic_thread_fence (__ATOMIC_SEQ_CST);
}

static void *code_alloc (unsigned n)
{
	void *p = mmap (0, n, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}

static long load (void *, const unsigned char *rom, long n)
{
	g_m->setUser ("ARMPI", 2, 1, 1, 4);
	time_t t = time (0);
	struct tm *u = gmtime (&t);
	if (u && u->tm_year >= 100) g_m->setTime (u->tm_year + 1900, u->tm_mon + 1, u->tm_mday, u->tm_hour, u->tm_min, u->tm_sec);
	FILE *f = fopen ("SD0:/home/roms/bios7.bin", "rb");
	if (f)
	{
		static unsigned char b7[16384];
		size_t k = fread (b7, 1, sizeof b7, f);
		fclose (f);
		if (k) g_m->setBios7 (b7, (unsigned) k);
	}
	if (!g_m->load (rom, (unsigned) n)) return 0;
	g_m->setAudioRate (44100);
	if (g_m->jitEnable (code_alloc)) g_m->useJit = true;
	if (!g_r3dOn && emu_thread ((long) r3d_thread, 0, 0x100000, (long) "nds-3d") > 0)
	{
		g_r3dOn = true;
		g_m->render3dKick = r3d_kick;
		g_m->render3dWait = r3d_wait;
	}
	return 1;
}
static void frame (void *, unsigned *px, long pitch, long pad, long)
{
	int b = 0;
	if (pad & PAD_RIGHT) b |= nds::BTN_RIGHT;
	if (pad & PAD_LEFT) b |= nds::BTN_LEFT;
	if (pad & PAD_UP) b |= nds::BTN_UP;
	if (pad & PAD_DOWN) b |= nds::BTN_DOWN;
	if (pad & PAD_B) b |= nds::BTN_A;
	if (pad & PAD_A) b |= nds::BTN_B;
	if (pad & PAD_Y) b |= nds::BTN_X;
	if (pad & PAD_X) b |= nds::BTN_Y;
	if (pad & PAD_START) b |= nds::BTN_START;
	if (pad & PAD_SELECT) b |= nds::BTN_SELECT;
	if (pad & (PAD_L | PAD_L2)) b |= nds::BTN_L;
	if (pad & (PAD_R | PAD_R2)) b |= nds::BTN_R;
	if ((b & nds::BTN_LEFT) && (b & nds::BTN_RIGHT)) b &= ~(nds::BTN_LEFT | nds::BTN_RIGHT);
	if ((b & nds::BTN_UP) && (b & nds::BTN_DOWN)) b &= ~(nds::BTN_UP | nds::BTN_DOWN);
	g_m->setButtons (b);
	g_m->setTouch (g_touchDown != 0, g_touchX, g_touchY);
	g_m->runFrame ();
	for (int s = 0; s < 2; s++)
		for (int y = 0; y < SH; y++) memcpy ((char *) px + (s * SH + y) * pitch, g_m->screen[s] + y * SW, SW * 4);
}
static long audio (void *, short *lr, long n) { return g_m->audioRead (lr, (int) n); }
static long save (void *, const void **p)
{
	if (!g_m->saveDirty || !g_m->save || !g_m->saveSize) return 0;
	g_m->saveDirty = false;
	*p = g_m->save;
	return g_m->saveSize;
}
static void restore (void *, const void *d, long n)
{
	if (n > nds::Cart::SAVE_MAX) n = nds::Cart::SAVE_MAX;
	g_m->setSaveData ((const unsigned char *) d, (unsigned) n);
}
static void reset (void *) { g_m->reset (); }
// (kind : 0 deplacement, 1 appui, 2 relache ; l'ecran du bas : y >= SH)
static void mouse (void *, long, long x, long y, long buttons)
{
	y -= SH;
	if ((buttons & 1) && x >= 0 && y >= 0 && x < SW && y < SH) { g_touchX = (int) x; g_touchY = (int) y; g_touchDown = 1; }
	else if (!(buttons & 1)) g_touchDown = 0;
}

static emu_desc d = { "Nintendo DS", "*.nds", SW, 2 * SH, 0, 0, 59826, 44100, 0,
		      load, frame, audio, reset, save, restore, 0, 0, mouse, 0, 0,
		      "Not a Nintendo DS ROM, or its secure area is encrypted: it needs a decrypted dump or SD0:/home/roms/bios7.bin." };

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
	g_m = new nds::Machine;
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
