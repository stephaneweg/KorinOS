// ref_gb : la reference du coeur Game Boy en assembleur, le coeur C++ d'Onyx sur le PC. Meme sortie que
// gb_harness.asm : numero, somme de l'image, somme du son, trames de son, ligne.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#define private public
#include "gb/gb.h"
#undef private

static unsigned fnv (const void *p, size_t n)
{
	const unsigned char *d = (const unsigned char *) p;
	unsigned h = 0x811C9DC5;
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
	gb::Machine *m = new gb::Machine;
	m->setAudioRate (44100);
	if (!m->load (rom, n)) { puts ("load failed"); return 1; }
	static short pcm[8192 * 2];
	for (int i = 0; i < frames; i++)
	{
		m->setButtons ((int) ((((unsigned) i >> 5) * 2654435761u) >> 24));
		m->runFrame ();
		if (argc > 3 && i == atoi (argv[3])) fwrite (m->fb, 1, sizeof m->fb, stderr);
		int k = m->audioRead (pcm, 8192);
		printf ("%d %08x %08x %d %d %08x %08x %08x %08x %08x\n", i, fnv (m->fb, sizeof m->fb), fnv (pcm, (size_t) k * 4), k, m->ly, fnv (m->oam, sizeof m->oam), fnv (m->obRGB, sizeof m->obRGB), fnv (m->bgRGB, sizeof m->bgRGB), fnv (m->vram, sizeof m->vram), fnv (m->wram, sizeof m->wram));
	}
	return 0;
}
