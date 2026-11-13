/*
 * gamesound.h -- petits effets sonores des jeux en C, sur les voix FM d'AudioKit (docs/SON.md) : la
 * traduction de gamesound.inc (src/inc/gamesnd.inc) pour les apps sur libposix. gs_note joue une note d'une
 * duree donnee, gs_later la joue plus tard (une melodie), gs_seq joue une table. Sans son (pas de kit, pas
 * de service, sortie prise), tout reste silencieux et le jeu tourne.
 *
 *	#include "../../inc/gamesound.h"  (avec <audiokit_abi.h> : -laudiokit, ou tous les kits)
 *	gs_open () au debut, gs_tick () a chaque image, gs_close () a la fin.
 *
 * (src/sys/common/gamesnd.c est le meme code pour les programmes du systeme, sur korinrt.)
 */
#ifndef KORIN_GAMESOUND_H
#define KORIN_GAMESOUND_H

#include <armpi/syscall.h>
#include <audiokit_abi.h>

#define GS_NV		8			/* voix */
#define GS_Q		24			/* notes en attente */

/* entree d'une table de gs_seq : attente (ms), hz, duree (ms), onde AKW_*, volume ; hz = 0 : la fin */
struct gs_ent
{
	unsigned short delay, hz, ms;
	unsigned char wave, vol;
};

static long gs_fm;				/* synthe (0 : pas de son) */
static long gs_mute;
static unsigned long gs_next;			/* la voix suivante */
static unsigned long gs_nq;			/* notes en attente */
static unsigned long gs_end_t[GS_NV];		/* fin de chaque voix (ms), 0 : libre */
static struct { unsigned long when; long hz, ms; unsigned wave, vol; } gs_q[GS_Q];

static inline unsigned long gs_time_ms (void) { return (unsigned long) armpi_syscall (0x19, 0, 0, 0, 0, 0, 0); }

/* ouvre le synthe ; gs_fm = 0 s'il n'y a pas de son */
static inline void gs_open (void)
{
	if (!audiokit_kit_open ())
		return;
	long fm = ak_fm_new (GS_NV);
	if (fm <= 0)
		return;
	if (ak_fm_live (fm, 0) < 0)
	{
		ak_fm_free (fm);
		return;
	}
	gs_fm = fm;
}

static inline void gs_close (void)
{
	long fm = gs_fm;
	if (!fm)
		return;
	gs_fm = 0;
	ak_fm_free (fm);			/* (arrete le thread de sortie) */
}

/* hz, ms, onde AKW_*, volume 0..255 */
static inline void gs_note (long hz, long ms, long wave, long vol)
{
	if (!gs_fm || gs_mute)
		return;
	unsigned long v = gs_next;
	gs_next = (v + 1) & (GS_NV - 1);
	ak_fm_start (gs_fm, (long) v, hz * 1000, wave, vol);
	gs_end_t[v] = (gs_time_ms () + (unsigned long) ms) | 1;
}

/* attente (ms), hz, ms, onde, volume */
static inline void gs_later (long delay, long hz, long ms, long wave, long vol)
{
	if (!gs_fm || gs_mute || gs_nq >= GS_Q)
		return;
	unsigned long i = gs_nq++;
	gs_q[i].when = gs_time_ms () + (unsigned long) delay;
	gs_q[i].hz = hz;
	gs_q[i].ms = ms;
	gs_q[i].wave = (unsigned) wave;
	gs_q[i].vol = (unsigned) vol;
}

/* toutes les notes d'une table */
static inline void gs_seq (const struct gs_ent *t)
{
	for (; t->hz; t++)
		gs_later (t->delay, t->hz, t->ms, t->wave, t->vol);
}

/* a chaque image : les notes dues partent, les notes finies s'arretent */
static inline void gs_tick (void)
{
	if (!gs_fm)
		return;
	unsigned long now = gs_time_ms ();
	for (unsigned long i = 0; i < gs_nq;)
	{
		if (now < gs_q[i].when)
		{
			i++;
			continue;
		}
		gs_note (gs_q[i].hz, gs_q[i].ms, gs_q[i].wave, gs_q[i].vol);
		gs_q[i] = gs_q[--gs_nq];	/* la derniere prend sa place */
	}
	for (unsigned long v = 0; v < GS_NV; v++)
	{
		if (!gs_end_t[v] || now < gs_end_t[v])
			continue;
		gs_end_t[v] = 0;
		ak_fm_stop (gs_fm, (long) v);
	}
}

/* Son on/off -> 1 si le son reste actif */
static inline long gs_toggle (void)
{
	gs_mute ^= 1;
	gs_nq = 0;
	if (gs_mute && gs_fm)
		ak_fm_stop (gs_fm, -1);
	return gs_mute ^ 1;
}

/* le gain et la perte d'une partie (les quatre notes d'Onyx) */
static const struct gs_ent gs_tab_win[] = {
	{ 0, 523, 110, AKW_SQUARE, 90 }, { 120, 659, 110, AKW_SQUARE, 90 }, { 240, 784, 110, AKW_SQUARE, 90 },
	{ 360, 1047, 360, AKW_SQUARE, 90 }, { 0, 0, 0, 0, 0 } };
static const struct gs_ent gs_tab_lose[] = {
	{ 0, 392, 150, AKW_TRIANGLE, 120 }, { 160, 330, 150, AKW_TRIANGLE, 120 }, { 320, 262, 150, AKW_TRIANGLE, 120 },
	{ 480, 196, 150, AKW_TRIANGLE, 120 }, { 0, 0, 0, 0, 0 } };

#endif
