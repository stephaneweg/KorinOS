/* gamesnd.c : les effets sonores des jeux sur les voix FM d'AudioKit (gamesnd.h), traduit ligne
 * à ligne de src/inc/gamesnd.inc. Le programme importe audiokit (sys.conf). */
#include "gamesnd.h"
#include "audiokit_import.h"

/* une note en attente (GS_QE : +0 quand, +8 hz, +16 ms, +24 onde, +28 volume) */
struct gs_q {
	u64 when;
	long hz, ms;
	u32 wave, vol;
};

long gs_fm;          /* synthé (0 : pas de son) */
static long gs_mute;
static u64 gs_next;  /* la voix suivante */
static u64 gs_nq;    /* notes en attente */
static u64 gs_end_t[GS_NV]; /* fin de chaque voix (ms), 0 : libre */
static struct gs_q gs_q[GS_Q];

const struct gs_ent gs_tab_win[] = {
	GS_ENT(0, 523, 110, AKW_SQUARE, 90),
	GS_ENT(120, 659, 110, AKW_SQUARE, 90),
	GS_ENT(240, 784, 110, AKW_SQUARE, 90),
	GS_ENT(360, 1047, 360, AKW_SQUARE, 90),
	GS_END,
};
const struct gs_ent gs_tab_lose[] = {
	GS_ENT(0, 392, 150, AKW_TRIANGLE, 120),
	GS_ENT(160, 330, 150, AKW_TRIANGLE, 120),
	GS_ENT(320, 262, 150, AKW_TRIANGLE, 120),
	GS_ENT(480, 196, 150, AKW_TRIANGLE, 120),
	GS_END,
};

void gs_open(void)
{
	long fm = ak_fm_new(GS_NV);
	if (fm <= 0)
		return;
	if (ak_fm_live(fm, 0) < 0) {
		ak_fm_free(fm);
		return;
	}
	gs_fm = fm;
}

void gs_close(void)
{
	long fm = gs_fm;
	if (!fm)
		return;
	gs_fm = 0;
	ak_fm_free(fm); /* (arrête le thread de sortie) */
}

void gs_note(long hz, long ms, long wave, long vol)
{
	if (!gs_fm || gs_mute)
		return;
	u64 v = gs_next;
	gs_next = (v + 1) & (GS_NV - 1);
	ak_fm_start(gs_fm, (long)v, hz * 1000, wave, vol);
	gs_end_t[v] = ((u64)sys_time_ms() + (u64)ms) | 1;
}

void gs_later(long delay, long hz, long ms, long wave, long vol)
{
	if (!gs_fm || gs_mute || gs_nq >= GS_Q)
		return;
	struct gs_q *q = &gs_q[gs_nq++];
	q->when = (u64)sys_time_ms() + (u64)delay;
	q->hz = hz;
	q->ms = ms;
	q->wave = (u32)wave;
	q->vol = (u32)vol;
}

void gs_seq(const struct gs_ent *t)
{
	for (; t->hz; t++)
		gs_later(t->delay, t->hz, t->ms, t->wave, t->vol);
}

void gs_tick(void)
{
	if (!gs_fm)
		return;
	u64 now = (u64)sys_time_ms();
	for (u64 i = 0; i < gs_nq;) {
		struct gs_q *q = &gs_q[i];
		if (now < q->when) {
			i++;
			continue;
		}
		gs_note(q->hz, q->ms, q->wave, q->vol);
		*q = gs_q[--gs_nq]; /* la dernière prend sa place */
	}
	for (u64 v = 0; v < GS_NV; v++) {
		if (!gs_end_t[v] || now < gs_end_t[v])
			continue;
		gs_end_t[v] = 0;
		ak_fm_stop(gs_fm, (long)v);
	}
}

long gs_toggle(void)
{
	gs_mute ^= 1;
	gs_nq = 0;
	if (gs_mute && gs_fm)
		ak_fm_stop(gs_fm, -1);
	return gs_mute ^ 1;
}
