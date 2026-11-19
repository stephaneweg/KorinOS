/*
 * music.c -- les pistes de Relics (src/apps/relics/music.h) en fichiers MIDI, pour les ecouter sans Korin :
 *
 *   gcc -O2 -o music tools/relics/music.c && ./music out/      ->  out/relics-0.mid .. relics-5.mid
 *   fluidsynth -ni -F out.wav /usr/share/sounds/sf2/FluidR3_GM.sf2 out/relics-1.mid
 *
 * Le sequenceur est celui du jeu : ses appels au synthetiseur sont notes avec leur instant.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static long ak_synth_new (long a, long b) { (void) a; (void) b; return 1; }
static long ak_synth_free (long a) { (void) a; return 0; }
static long ak_open (long a, long b, long c, long d) { (void) a; (void) b; (void) c; (void) d; return 1; }
static long ak_close (long a) { (void) a; return 0; }
static long ak_write_all (long a, long b, long c) { (void) a; (void) b; return c; }
static long ak_synth_render (long a, long b, long n);
static long ak_synth_reset (long a) { (void) a; return 0; }
static long ak_synth_midi (long s, long ch, long cmd, long a, long b);
#include "../../src/apps/relics/music.h"

static unsigned char ev[1 << 22];
static long nev, now, last;
static void vlq (unsigned long v)
{
	unsigned char t[5]; int n = 0;
	t[n++] = v & 127;
	while (v >>= 7) t[n++] = 128 | (v & 127);
	while (n) ev[nev++] = t[--n];
}
static long ak_synth_render (long a, long b, long n) { (void) a; (void) b; now += n; return 0; }
static long ak_synth_midi (long s, long ch, long cmd, long a, long b)
{
	(void) s;
	long tick = now * 960 / MU_RATE;		/* (960 tops par seconde : tempo 1 noire = 1 s) */
	vlq ((unsigned long) (tick - last)); last = tick;
	ev[nev++] = (unsigned char) (cmd | ch);
	ev[nev++] = (unsigned char) a;
	if (cmd != 0xC0) ev[nev++] = (unsigned char) b;
	return 0;
}

int main (int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	for (int n = 0; n < MU_NTRACK; n++)
	{
		nev = now = last = 0;
		mu_syn = 1;
		mu_setup (n);
		long steps = 32 + 2 * 256;
		for (long i = 0; i < steps; i++)
		{
			mu_tick ();
			long f = (long) MU_RATE * 60 / (mu_tracks[n].bpm * 4);
			ak_synth_render (0, 0, f);
		}
		mu_all_off ();
		ak_synth_render (0, 0, MU_RATE * 2);
		mu_midi (0, 0xB0, 7, 0);
		vlq (0); ev[nev++] = 0xFF; ev[nev++] = 0x2F; ev[nev++] = 0;
		char p[512]; snprintf (p, sizeof p, "%s/relics-%d.mid", dir, n);
		FILE *f = fopen (p, "wb");
		if (!f) return 1;
		/* format 0, une piste, 960 tops a la noire ; tempo : la noire = 1 s */
		unsigned char h[] = { 'M','T','h','d',0,0,0,6,0,0,0,1,0x03,0xC0, 'M','T','r','k',0,0,0,0, 0,0xFF,0x51,3,0x0F,0x42,0x40 };
		long len = nev + 7;
		h[18] = len >> 24; h[19] = len >> 16; h[20] = len >> 8; h[21] = len;
		fwrite (h, 1, sizeof h, f);
		fwrite (ev, 1, nev, f);
		fclose (f);
		printf ("%s\n", p);
	}
	return 0;
}
