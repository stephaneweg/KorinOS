/*
 * snd.c -- le son : le moteur ecrit d'avance dans un anneau qu'il croit lu par un DMA. Ici, cette lecture suit
 * l'horloge, et EmuKit passe ce qui est du a AudioKit, apres chaque image (armpi_audio). Tout sur le thread de la machine.
 */
#include "quakedef.h"
#include "armpi.h"

#define RING (1 << 17)		/* echantillons mono : 1,5 s */

extern volatile dma_t *shm;
extern volatile dma_t sn;
static short ring[RING];
static double t0;
static long rp;			/* trames lues */
static int on;

static long frames_due (void) { return (long) ((Sys_FloatTime () - t0) * ARMPI_RATE); }

qboolean SNDDMA_Init (void)
{
	shm = &sn;
	shm->splitbuffer = 0;
	shm->samplebits = 16;
	shm->speed = ARMPI_RATE;
	shm->channels = 2;
	shm->samples = RING;
	shm->samplepos = 0;
	shm->submission_chunk = 1;
	shm->buffer = (unsigned char *) ring;
	t0 = Sys_FloatTime ();
	rp = 0;
	on = 1;
	return true;
}
int SNDDMA_GetDMAPos (void) { return shm->samplepos = (frames_due () * 2) & (RING - 1); }
void SNDDMA_Shutdown (void) { on = 0; }
void SNDDMA_Submit (void) {}

long armpi_audio (void *ctx, short *lr, long frames)
{
	long due, n, i, p;
	if (!on) return 0;
	due = frames_due () - rp;
	if (due <= 0) return 0;
	if (due > ARMPI_RATE / 4) { rp += due - 2048; due = 2048; }	/* (une longue attente : le passe est perdu) */
	n = due < frames ? due : frames;
	p = rp * 2;
	for (i = 0; i < n * 2; i++) lr[i] = ring[(p + i) & (RING - 1)];
	rp += n;
	return n;
}
