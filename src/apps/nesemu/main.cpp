//
// nesemu -- l'emulateur NES / Famicom (docs/EMULATEURS.md) : le coeur C++ d'Onyx (nes/), sur EmuKit.
//     nesemu [rom.nes]
// Un source pour le Pi et le PC. Tout le reste (fenetre, menus, trois vues, son, manette, cadence,
// <rom>.sav, pause) : EmuKit. Boutons par leur place, comme sur les manettes de Nintendo : a droite et
// en haut A, en bas et a gauche B. Comme nesemu.asm (garde a cote jusqu'a la validation sur le Pi).
//
#include <new>
#include <string.h>
#include <emukit.h>
#include "nes/nes.h"

static nes::Machine *g_m;
static struct emu_desc d;

// « (Europe) », « (E) », « (PAL) »... dans le nom du fichier : un jeu PAL (comme l'asm et Onyx)
static bool name_says_pal (const char *p)
{
	static const char *const tags[] = { "(Europe", "(E)", "(PAL", "(Australia", "(Germany", "(France", "(Spain",
					    "(Italy", "(Sweden", "(Netherlands", "(UK" };
	if (!p) return false;
	for (unsigned t = 0; t < sizeof tags / sizeof tags[0]; t++)
		if (strstr (p, tags[t])) return true;
	return false;
}

static long load (void *, const unsigned char *rom, long n)
{
	if (!g_m->load (rom, (int) n)) return 0;
	if (name_says_pal ((const char *) emu_rom_path ())) g_m->setPal (true);
	d.fps_milli = g_m->pal ? 50007 : 60099;
	return 1;
}

static void frame (void *, unsigned *px, long pitch, long pad, long)
{
	int b = 0;
	if (pad & (PAD_B | PAD_Y)) b |= nes::BTN_A;
	if (pad & (PAD_A | PAD_X)) b |= nes::BTN_B;
	if (pad & PAD_SELECT) b |= nes::BTN_SELECT;
	if (pad & PAD_START) b |= nes::BTN_START;
	if ((pad & PAD_UP) && !(pad & PAD_DOWN)) b |= nes::BTN_UP;		// (pas les deux)
	if ((pad & PAD_DOWN) && !(pad & PAD_UP)) b |= nes::BTN_DOWN;
	if ((pad & PAD_LEFT) && !(pad & PAD_RIGHT)) b |= nes::BTN_LEFT;
	if ((pad & PAD_RIGHT) && !(pad & PAD_LEFT)) b |= nes::BTN_RIGHT;
	g_m->setButtons (b);
	g_m->runFrame ();
	for (int y = 0; y < nes::H; y++) memcpy ((char *) px + y * pitch, g_m->fb + y * nes::W, nes::W * 4);
}

static long audio (void *, short *lr, long n) { return g_m->audioRead (lr, (int) n); }
static void reset (void *) { g_m->reset (); }
static long save (void *, const void **p)
{
	if (!g_m->battery || !g_m->sramDirty) { g_m->sramDirty = false; return 0; }
	g_m->sramDirty = false;
	*p = g_m->sram;
	return sizeof g_m->sram;
}
static void restore (void *, const void *data, long n) { g_m->setSaveRam ((const unsigned char *) data, (int) n); }

// (lang/fr.txt, dans le dictionnaire du systeme : lang_tr d'AppKit par son rang, comme ndsemu)
extern "C" void *__armpi_appkit (unsigned rank);
static const char *lang_text (const char *s)
{
	const char *(*f) (const char *) = (const char *(*) (const char *)) __armpi_appkit (42);
	return f ? f (s) : s;
}

int main (int argc, char **argv)
{
	if (!emukit_kit_open ()) return 1;
	static unsigned char mem[sizeof (nes::Machine)] __attribute__ ((aligned (64)));
	g_m = new (mem) nes::Machine;
	g_m->setAudioRate (44100);
	d.name = "NES"; d.ext = "*.nes";
	d.w = nes::W; d.h = nes::H;
	d.fps_milli = 60099; d.rate = 44100;
	d.load = load; d.frame = frame; d.audio = audio; d.reset = reset; d.save = save; d.restore = restore;
	d.error = lang_text ("This game uses a mapper not supported yet.\nSupported: 0, 1, 2, 3, 4, 7, 66.");
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
