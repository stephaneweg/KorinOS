// ref_snes : la reference du coeur SNES en assembleur, le coeur C++ d'Onyx sur le PC. Meme sortie que
// snes_harness.asm : numero, somme de l'image, somme du son, trames de son, horloge, puis l'etat.
//     g++ -O2 -Dprivate=public -I $ONYX/user/Emulators ... (tools/emu/check_snes.sh)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "snes/snes.h"

static unsigned fnv (const void *p, size_t n, unsigned h = 0x811C9DC5)
{
	const unsigned char *d = (const unsigned char *) p;
	for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
	return h;
}
int main (int argc, char **argv)
{
	FILE *f = fopen (argv[1], "rb");
	if (!f) return 1;
	static unsigned char rom[1 << 26];
	int n = (int) fread (rom, 1, sizeof rom, f);
	fclose (f);
	int frames = atoi (argv[2]);
	bool quiet = argc > 3 && !strcmp (argv[3], "time");		// (mesure : ni sommes ni sorties)
	static unsigned char mem[sizeof (snes::Machine)];
	snes::Machine *m = new (mem) snes::Machine;
	m->setAudioRate (44100);
	if (!m->load (rom, n)) { puts ("load failed"); return 1; }
	static short pcm[8192 * 2];
	for (int i = 0; i < frames; i++)
	{
		m->setButtons ((int) (((((unsigned) i >> 5) * 2654435761u) >> 16) & 0xFFF0));
		m->runFrame ();
		int k = m->audioRead (pcm, 8192);
		if (quiet) continue;
		printf ("%d %08x %08x %d %llu\n", i, fnv (m->fb, sizeof m->fb), fnv (pcm, (size_t) k * 4), k, m->now);
		unsigned st[] = { m->A, m->X, m->Y, m->S, m->PC, m->PB, m->P, m->D, m->DB, (unsigned) m->E, (unsigned) m->height,
			m->spcA, m->spcX, m->spcY, m->spcS, m->spcPC, m->spcP };
		unsigned h = fnv (st, sizeof st);
		if (!(i & 15))
		{
			h = fnv (m->wram, sizeof m->wram, h);
			h = fnv (m->vram, sizeof m->vram, h);
			h = fnv (m->aram, sizeof m->aram, h);
			h = fnv (m->cgram, sizeof m->cgram, h);
			h = fnv (m->oam, sizeof m->oam, h);
			h = fnv (m->sram, sizeof m->sram, h);
		}
		printf ("%04x%04x%04x%04x%02x%04x %08x\n", m->A, m->X, m->Y, m->S, m->PB, m->PC, h);
	}
	return 0;
}
