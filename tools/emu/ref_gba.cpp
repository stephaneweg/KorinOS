// ref_gba : la reference du coeur GBA en assembleur, le coeur C++ d'Onyx sur le PC. Meme sortie que
// gba_harness.asm : numero, somme de l'image, somme du son, trames de son, cycles, puis l'etat.
//     g++ -O2 -Dprivate=public -I $ONYX/user/Emulators ... (tools/emu/check_gba.sh)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "gba/gba.h"

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
	static unsigned char mem[sizeof (gba::Machine)];
	gba::Machine *m = new (mem) gba::Machine;
	m->setAudioRate (44100);
	if (!m->load (rom, n)) { puts ("load failed"); return 1; }
	static short pcm[8192 * 2];
	for (int i = 0; i < frames; i++)
	{
		m->setButtons ((int) (((((unsigned) i >> 5) * 2654435761u) >> 22) & 0x3FF));
		m->runFrame ();
		int k = m->audioRead (pcm, 8192);
		if (quiet) continue;
		printf ("%d %08x %08x %d %llu\n", i, fnv (m->fb, sizeof m->fb), fnv (pcm, (size_t) k * 4), k, m->now);
		unsigned h = fnv (m->r, sizeof m->r);
		h = fnv (&m->cpsr, 4, h);
		if (!(i & 15))
		{
			h = fnv (m->ewram, sizeof m->ewram, h);
			h = fnv (m->iwram, sizeof m->iwram, h);
			h = fnv (m->vram, sizeof m->vram, h);
			h = fnv (m->pal, sizeof m->pal, h);
			h = fnv (m->oam, sizeof m->oam, h);
			h = fnv (m->saveBuf, sizeof m->saveBuf, h);
		}
		printf ("%08x%08x %08x\n", m->r[15], m->cpsr, h);
	}
	return 0;
}
