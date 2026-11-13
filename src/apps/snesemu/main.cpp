//
// snesemu -- l'emulateur Super Nintendo (docs/EMULATEURS.md) : le coeur C++ d'Onyx (snes/), sur EmuKit.
//     snesemu [rom.sfc]
// Un source pour le Pi et le PC. Tout le reste : EmuKit. Boutons par leur place, comme sur la manette
// de Nintendo. Comme snesemu.asm (garde a cote jusqu'a la validation sur le Pi).
//
#include <new>
#include <string.h>
#include <emukit.h>
#include "snes/snes.h"

static snes::Machine *g_m;
static struct emu_desc d;

static long load (void *, const unsigned char *rom, long n)
{
	if (!g_m->load (rom, (int) n)) return 0;
	d.fps_milli = g_m->pal ? 50007 : 60099;
	return 1;
}

static const struct { long pad; int btn; } pad_map[] = {
	{ PAD_RIGHT, snes::BTN_RIGHT }, { PAD_LEFT, snes::BTN_LEFT }, { PAD_UP, snes::BTN_UP }, { PAD_DOWN, snes::BTN_DOWN },
	{ PAD_B, snes::BTN_A }, { PAD_A, snes::BTN_B }, { PAD_Y, snes::BTN_X }, { PAD_X, snes::BTN_Y },
	{ PAD_L, snes::BTN_L }, { PAD_R, snes::BTN_R }, { PAD_START, snes::BTN_START }, { PAD_SELECT, snes::BTN_SELECT } };

static void frame (void *, unsigned *px, long pitch, long pad, long)
{
	int b = 0;
	for (unsigned i = 0; i < sizeof pad_map / sizeof pad_map[0]; i++)
		if (pad & pad_map[i].pad) b |= pad_map[i].btn;
	if ((b & 0x0300) == 0x0300) b &= ~0x0300;		// (pas gauche et droite a la fois)
	if ((b & 0x0C00) == 0x0C00) b &= ~0x0C00;
	g_m->setButtons (b);
	g_m->runFrame ();
	for (int y = 0; y < snes::H; y++) memcpy ((char *) px + y * pitch, g_m->fb + y * snes::W, snes::W * 4);
}

static long audio (void *, short *lr, long n) { return g_m->audioRead (lr, (int) n); }
// (la sauvegarde survit a la remise a zero)
static void reset (void *) { g_m->reset (); g_m->sramDirty = false; }
static long save (void *, const void **p)
{
	if (!g_m->sramSize || !g_m->sramDirty) return 0;
	g_m->sramDirty = false;
	*p = g_m->sram;
	return g_m->sramSize;
}
static void restore (void *, const void *data, long n) { g_m->setSaveRam ((const unsigned char *) data, (int) n); }

extern "C" void *__armpi_appkit (unsigned rank);
static const char *lang_text (const char *s)
{
	const char *(*f) (const char *) = (const char *(*) (const char *)) __armpi_appkit (42);
	return f ? f (s) : s;
}

int main (int argc, char **argv)
{
	if (!emukit_kit_open ()) return 1;
	g_m = new snes::Machine;
	g_m->setAudioRate (44100);
	d.name = "Super Nintendo"; d.ext = "*.sfc;*.smc";
	d.w = snes::W; d.h = snes::H;
	d.fps_milli = 60099; d.rate = 44100;
	d.load = load; d.frame = frame; d.audio = audio; d.reset = reset; d.save = save; d.restore = restore;
	d.error = lang_text ("Not a Super Nintendo ROM, or it needs a chip not supported yet (SA-1, Super FX).");
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
