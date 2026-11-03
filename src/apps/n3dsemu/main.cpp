//
// n3dsemu -- l'emulateur Nintendo 3DS (docs/EMULATEURS.md) : le coeur d'Onyx (n3ds/), un ARM11 interprete et recompile
// ecrit ici, le GPU sur Mesa (n3ds/n3ds_gl.cpp) ou en logiciel, sur EmuKit.
//     n3dsemu [--soft] [--full] jeu.3ds
// Les deux ecrans dans une image : celui du bas centre sous celui du haut, ou a cote (View > Side by Side). L'ecran
// tactile a la souris. Une ROM de plusieurs Go n'est pas lue en memoire : le coeur en lit des morceaux quand le jeu
// les demande (emu_rom_by_path). Une sauvegarde : <jeu>.sav, a cote du jeu.
//
#include <new>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <emukit.h>
#include "n3ds/n3ds.h"
#include "n3ds/n3ds_gl.h"

using namespace n3ds;

enum { TW = TOP_W, BW = BOTTOM_W, SH = SCREEN_H, ID_SIDE = 100, ID_ARROWS, ID_EVERY };

static Machine *g_m;
static int g_fd = -1;
static Source g_src;
static bool g_side, g_arrowsPad, g_every, g_noGpu;
static volatile int g_touchDown, g_touchX, g_touchY;
static u8 *g_sav; static u32 g_savCap;
extern "C" void n3ds_blit (unsigned *dst, long pitch, const unsigned *src, int w, int h);
static u32 g_top[TW * SH], g_bottom[BW * SH];

static bool fileRead (void *, u64 off, void *dst, u32 n)
{
	if (lseek (g_fd, (off_t) off, SEEK_SET) < 0) return false;
	u8 *d = (u8 *) dst;
	while (n)
	{
		const long k = read (g_fd, d, n);
		if (k <= 0) return false;
		d += k; n -= (u32) k;
	}
	return true;
}

static Machine *machineNew ()
{
	Machine *m = new (std::nothrow) Machine;
	if (!m || !m->init ()) return 0;
	m->setUser ("ARMPI", 2, 2);
	m->monoOnly = true;						// (un seul oeil : l'ecran du Pi n'en a qu'un)
	int ff = open ("SD0:/apps/n3dsemu.app/sysfont.bcfnt", O_RDONLY);
	if (ff >= 0)
	{
		const long n = lseek (ff, 0, SEEK_END);
		u8 *b = n > 0 && n < (8 << 20) ? (u8 *) malloc ((size_t) n) : 0;
		lseek (ff, 0, SEEK_SET);
		if (b && read (ff, b, (size_t) n) == n) m->setSharedFont (b, (u32) n);
		free (b);
		close (ff);
	}
	return m;
}

// (l'argument : le chemin, emu_rom_by_path)
static long load (void *, const unsigned char *path, long size)
{
	g_fd = open ((const char *) path, O_RDONLY);
	if (g_fd < 0) return 0;
	g_src.user = 0; g_src.size = (u64) size; g_src.read = fileRead;
	return g_m->loadFrom (g_src) ? 1 : 0;
}

static void layout ()
{
	emu_frame_size (g_side ? TW + BW : TW, g_side ? SH : 2 * SH);
}

static void frame (void *, unsigned *px, long pitch, long pad, long flags)
{
	static bool glTried;
	if (!glTried) { glTried = true; if (!g_noGpu) n3ds_gl_setup (g_m); layout (); }
	u32 b = 0;
	if (pad & PAD_B) b |= BTN_A;
	if (pad & PAD_A) b |= BTN_B;
	if (pad & PAD_Y) b |= BTN_X;
	if (pad & PAD_X) b |= BTN_Y;
	if (pad & PAD_START) b |= BTN_START;
	if (pad & PAD_SELECT) b |= BTN_SELECT;
	if (pad & (PAD_L | PAD_L2)) b |= BTN_L;
	if (pad & (PAD_R | PAD_R2)) b |= BTN_R;
	int dx = ((pad & PAD_RIGHT) ? 1 : 0) - ((pad & PAD_LEFT) ? 1 : 0), dy = ((pad & PAD_UP) ? 1 : 0) - ((pad & PAD_DOWN) ? 1 : 0), cx = 0, cy = 0;
	if (g_arrowsPad)
	{
		if (dx > 0) b |= BTN_RIGHT; else if (dx < 0) b |= BTN_LEFT;
		if (dy > 0) b |= BTN_UP; else if (dy < 0) b |= BTN_DOWN;
	}
	else { cx = dx * 156; cy = dy * 156; }
	for (int i = 0; i < 2; i++)						// (le stick gauche d'une manette : le Circle Pad)
	{
		short ax[4] = { 0, 0, 0, 0 };
		emu_pad_axes (i, (long) ax);
		if (ax[0] > 150 || ax[0] < -150 || ax[1] > 150 || ax[1] < -150) { cx = ax[0] * 156 / 1000; cy = -ax[1] * 156 / 1000; }
	}
	if (cx > 40) b |= BTN_CPAD_RIGHT; else if (cx < -40) b |= BTN_CPAD_LEFT;
	if (cy > 40) b |= BTN_CPAD_UP; else if (cy < -40) b |= BTN_CPAD_DOWN;
	g_m->setInput (b, cx, cy, g_touchDown != 0, g_touchX, g_touchY);
	const bool skip = (flags & EMF_SKIP) && !g_every;
	g_m->skipDraw = skip;
	g_m->runFrame ();
	if (skip) return;
	g_m->gpuSync ();
	g_m->screenImage (SCREEN_TOP, g_top);
	g_m->screenImage (SCREEN_BOTTOM, g_bottom);
	const long W = g_side ? TW + BW : TW;
	const int bx = g_side ? TW : (TW - BW) / 2, by = g_side ? 0 : SH;
	for (int y = 0; y < (g_side ? SH : 2 * SH); y++) memset ((char *) px + y * pitch, 0, (size_t) W * 4);
	n3ds_blit (px, pitch, g_top, TW, SH);
	n3ds_blit ((unsigned *) ((char *) px + by * pitch) + bx, pitch, g_bottom, BW, SH);
}

static long audio (void *, short *lr, long n) { return g_m->audioRead (lr, (int) n); }

static long save (void *, const void **p)
{
	if (!g_m->storageDirty) return 0;
	const u32 need = g_m->storageExport (0, 0);
	if (!need) { g_m->storageDirty = false; return 0; }
	if (need > g_savCap) { free (g_sav); g_sav = (u8 *) malloc (need); g_savCap = g_sav ? need : 0; }
	if (!g_sav || g_m->storageExport (g_sav, need) != need) return 0;
	g_m->storageDirty = false;
	*p = g_sav;
	return need;
}
static void restore (void *, const void *d, long n) { g_m->storageImport ((const u8 *) d, (u32) n); }

static void reset (void *)
{
	n3ds_gl_quit (g_m);
	delete g_m;
	g_m = machineNew ();
	if (g_m && g_m->loadFrom (g_src))
	{
		long sz = 0;
		(void) sz;
		if (!g_noGpu) n3ds_gl_setup (g_m);
	}
}

static void cmd (void *, long id)
{
	if (id == ID_SIDE) { g_side = !g_side; layout (); }
	else if (id == ID_ARROWS) g_arrowsPad = !g_arrowsPad;
	else if (id == ID_EVERY) g_every = !g_every;
}

// (kind : 0 deplacement, 1 appui, 2 relache ; x, y dans l'image)
static void mouse (void *, long, long x, long y, long buttons)
{
	if (g_side) x -= TW;
	else { x -= (TW - BW) / 2; y -= SH; }
	if ((buttons & 1) && x >= 0 && y >= 0 && x < BW && y < SH) { g_touchX = (int) x; g_touchY = (int) y; g_touchDown = 1; }
	else if (!(buttons & 1)) g_touchDown = 0;
}

static const char MENU[] = "M3DS\nI100\tSide by Side\nI102\tDraw Every Frame\nI101\tArrows Are the + Control Pad\n";

static emu_desc d = { "Nintendo 3DS", "*.3ds;*.cci;*.cxi;*.3dsx", TW, 2 * SH, TW + BW, 2 * SH, 59831, Machine::AUDIO_RATE, 0,
		      load, frame, audio, reset, save, restore, MENU, cmd, mouse, 0, 1,
		      "This is not a Nintendo 3DS game this emulator can run: it needs a decrypted .3ds, .cci or .cxi dump, or a .3dsx program." };

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
	int n = 1;
	for (int i = 1; i < argc; i++)
		if (!strcmp (argv[i], "--soft")) g_noGpu = true;
		else if (!strcmp (argv[i], "--dry")) n3ds_gl_dry = 1;		// (essai sous QEMU : Mesa et son compilateur, sans GPU)
		else if (!strcmp (argv[i], "--nosave")) { d.save = 0; d.restore = 0; }	// (essai : <jeu>.sav ni lu ni ecrit)
		else argv[n++] = argv[i];
	g_m = machineNew ();
	if (!g_m) return 1;
	emu_rom_by_path ();
	return (int) emu_run ((long) &d, emu_args (n, argv));
}
