// ref_nes : la reference du coeur NES en assembleur, le coeur C++ d'Onyx sur le PC. Meme sortie que
// nes_harness.asm : numero, somme de l'image, somme du son, trames de son, cycles.
//     g++ -O2 -I $ONYX/user/Emulators ... (tools/emu/check.sh)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "nes/nes.h"

static unsigned fnv (const void *p, size_t n)
{
	const unsigned char *d = (const unsigned char *) p;
	unsigned h = 0x811C9DC5;
	for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
	return h;
}
static void *operator_rom;
int main (int argc, char **argv)
{
	FILE *f = fopen (argv[1], "rb");
	if (!f) return 1;
	static unsigned char rom[1 << 26];
	int n = (int) fread (rom, 1, sizeof rom, f);
	fclose (f);
	int frames = atoi (argv[2]);
	static unsigned char mem[sizeof (nes::Machine)];
	nes::Machine *m = new (mem) nes::Machine;
	m->setAudioRate (44100);
	if (!m->load (rom, n)) { puts ("load failed"); return 1; }
	if (argc > 3) m->setPal (true);
	static short pcm[8192 * 2];
	for (int i = 0; i < frames; i++)
	{
		m->setButtons ((int) ((((unsigned) i >> 5) * 2654435761u) >> 24));
		m->runFrame ();
		int k = m->audioRead (pcm, 8192);
		if (k > 0) ;
		printf ("%d %08x %08x %d %llu\n", i, fnv (m->fb, sizeof m->fb), fnv (pcm, (size_t) k * 4), k, m->cycles);
	}
	(void) operator_rom;
	return 0;
}
