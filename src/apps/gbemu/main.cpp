//
// gbemu -- l'emulateur Game Boy et Game Boy Color (docs/EMULATEURS.md) : le coeur C++ d'Onyx (gb/), sur
// EmuKit.
//     gbemu [rom.gb | rom.gbc]
// Un source pour le Pi et le PC. Tout le reste (fenetre, menus, trois vues, son, manette, cadence,
// <rom>.sav, pause) : EmuKit. Boutons par leur place, comme sur les manettes de Nintendo : a droite et
// en haut A, en bas et a gauche B. Comme gbemu.asm (garde a cote jusqu'a la validation sur le Pi).
//
#include <new>
#include <string.h>
#include <emukit.h>
#include "gb/gb.h"

static gb::Machine *g_m;
static struct emu_desc d;

static long load (void *, const unsigned char *rom, long n) { return g_m->load (rom, (int) n) ? 1 : 0; }

static void frame (void *, unsigned *px, long pitch, long pad, long)
{
	int b = 0;
	if (pad & (PAD_B | PAD_Y)) b |= gb::BTN_A;
	if (pad & (PAD_A | PAD_X)) b |= gb::BTN_B;
	if (pad & PAD_SELECT) b |= gb::BTN_SELECT;
	if (pad & PAD_START) b |= gb::BTN_START;
	if ((pad & PAD_UP) && !(pad & PAD_DOWN)) b |= gb::BTN_UP;		// (pas les deux)
	if ((pad & PAD_DOWN) && !(pad & PAD_UP)) b |= gb::BTN_DOWN;
	if ((pad & PAD_LEFT) && !(pad & PAD_RIGHT)) b |= gb::BTN_LEFT;
	if ((pad & PAD_RIGHT) && !(pad & PAD_LEFT)) b |= gb::BTN_RIGHT;
	g_m->setButtons (b);
	g_m->runFrame ();
	for (int y = 0; y < gb::H; y++) memcpy ((char *) px + y * pitch, g_m->fb + y * gb::W, gb::W * 4);
}

static long audio (void *, short *lr, long n) { return g_m->audioRead (lr, (int) n); }
static void reset (void *) { g_m->reset (); }
static long save (void *, const void **p)
{
	if (!g_m->sramDirty || !g_m->battery || !g_m->sram) return 0;
	g_m->sramDirty = false;
	*p = g_m->sram;
	return g_m->sramSize;
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
	g_m = new gb::Machine;
	g_m->setAudioRate (44100);
	d.name = "Game Boy"; d.ext = "*.gb;*.gbc";
	d.w = gb::W; d.h = gb::H;
	d.fps_milli = 59727; d.rate = 44100;
	d.load = load; d.frame = frame; d.audio = audio; d.reset = reset; d.save = save; d.restore = restore;
	d.error = lang_text ("Not a Game Boy ROM.");
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
