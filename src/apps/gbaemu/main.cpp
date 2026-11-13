//
// gbaemu -- l'emulateur Game Boy Advance (docs/EMULATEURS.md) : le coeur C++ d'Onyx (gba/), sur EmuKit.
//     gbaemu [rom.gba]
// Pas de BIOS de Nintendo : gba/gba_bios.cpp en est un de remplacement. Un source pour le Pi et le PC.
// Tout le reste : EmuKit. Boutons par leur place, comme sur les manettes de Nintendo. Comme gbaemu.asm
// (garde a cote jusqu'a la validation sur le Pi).
//
#include <new>
#include <string.h>
#include <emukit.h>
#include "gba/gba.h"

static gba::Machine *g_m;
static struct emu_desc d;

static long load (void *, const unsigned char *rom, long n) { return g_m->load (rom, (int) n) ? 1 : 0; }

static const struct { long pad; int btn; } pad_map[] = {
	{ PAD_RIGHT, gba::BTN_RIGHT }, { PAD_LEFT, gba::BTN_LEFT }, { PAD_UP, gba::BTN_UP }, { PAD_DOWN, gba::BTN_DOWN },
	{ PAD_B | PAD_Y, gba::BTN_A }, { PAD_A | PAD_X, gba::BTN_B }, { PAD_START, gba::BTN_START },
	{ PAD_SELECT, gba::BTN_SELECT }, { PAD_L | PAD_L2, gba::BTN_L }, { PAD_R | PAD_R2, gba::BTN_R } };

static void frame (void *, unsigned *px, long pitch, long pad, long)
{
	int b = 0;
	for (unsigned i = 0; i < sizeof pad_map / sizeof pad_map[0]; i++)
		if (pad & pad_map[i].pad) b |= pad_map[i].btn;
	if ((b & 0x30) == 0x30) b &= ~0x30;			// (pas gauche et droite a la fois)
	if ((b & 0xC0) == 0xC0) b &= ~0xC0;
	g_m->setButtons (b);
	g_m->runFrame ();
	for (int y = 0; y < gba::H; y++) memcpy ((char *) px + y * pitch, g_m->fb + y * gba::W, gba::W * 4);
}

static long audio (void *, short *lr, long n) { return g_m->audioRead (lr, (int) n); }
static void reset (void *) { g_m->reset (); }
static long save (void *, const void **p)
{
	if (g_m->saveType == gba::SAVE_NONE || !g_m->saveSize || !g_m->saveDirty) return 0;
	g_m->saveDirty = false;
	*p = g_m->save;
	return g_m->saveSize;
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
	g_m = new gba::Machine;
	g_m->setAudioRate (44100);
	d.name = "Game Boy Advance"; d.ext = "*.gba";
	d.w = gba::W; d.h = gba::H;
	d.fps_milli = 59727; d.rate = 44100;
	d.load = load; d.frame = frame; d.audio = audio; d.reset = reset; d.save = save; d.restore = restore;
	d.error = lang_text ("Not a Game Boy Advance ROM.");
	return (int) emu_run ((long) &d, emu_args (argc, argv));
}
