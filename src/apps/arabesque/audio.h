//
// audio.h -- le son d'Arabesque : un thread tient le sequenceur et nourrit le flux d'AudioKit, bloc apres bloc, sur le
// synthetiseur General MIDI du kit. En composition, il compose un peu en avance sur ce qu'il joue. En export, le meme
// chemin ecrit dans un encodeur au lieu du flux, aussi vite que possible.
//
// g_lk protege la sequence, le style, le trace, l'image : le thread les tient le temps de lire un bloc d'evenements, la
// fenetre le temps d'un changement. La SoundFont se charge dans le thread : la fenetre n'attend jamais.
//
#ifndef ARABESQUE_AUDIO_H
#define ARABESQUE_AUDIO_H

#include <pthread.h>
#include <audiokit_abi.h>
#include "compose.h"

enum { BLOCK = 256, RATE = 44100, MAX_ACTIVE = 256, LOOKAHEAD = PPQ / 2, TAIL_FRAMES = RATE * 2 };
enum { M_IDLE, M_PLAY, M_COMPOSE, M_EXPORT };
enum { EXPORT_RUN, EXPORT_DONE, EXPORT_FAIL, EXPORT_CANCEL };

static pthread_mutex_t g_lk = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_tid; static bool g_threadOn;
static volatile bool g_synthOk, g_quit;
static long g_synth;

static Song g_song;
static Style g_style;
static Ifs g_ifs;
static Image g_img;
static Composer g_comp;

static volatile int g_mode = M_IDLE;
static volatile double g_tick;			// la position, en pulsations
static int g_idx[NPART];			// le prochain evenement de chaque piste
struct Active { int tr, idx; };
static Active g_act[MAX_ACTIVE];
static int g_nact;

// l'export : le fichier est ouvert et ferme par le thread
static char g_expPath[300];
static int g_expFormat, g_expBits;
static volatile int g_expState = EXPORT_DONE, g_expPct;
static volatile bool g_expCancel;

static inline void syn (int ch, int cmd, int a, int b) { if (g_synthOk) ak_synth_midi (g_synth, ch, cmd, a, b); }

static void all_off ()
{
	for (int c = 0; c < 16; c++) { syn (c, 0xB0, 64, 0); syn (c, 0xB0, 123, 0); }
	g_nact = 0;
}
static int part_volume (int tr, int v) { return g_style.p[tr].mute ? 0 : v; }

// au depart ou apres un saut : les pistes a leur place, les instruments et volumes d'alors
static void seq_rewind (uint32_t tick)
{
	all_off ();
	for (int i = 0; i < NPART; i++)
	{
		Track &t = g_song.tr[i];
		g_idx[i] = t.find (tick);
		for (int k = 0; k < g_idx[i]; k++)
		{
			const Note &m = t.n[k];
			if (m.kind == NK_PROGRAM) syn (m.ch, 0xC0, m.key, 0);
			else if (m.kind == NK_CC) syn (m.ch, 0xB0, m.key, m.key == 7 ? part_volume (i, m.vel) : m.vel);
		}
	}
}
static void seq_dispatch (uint32_t now)
{
	for (int i = 0; i < g_nact; )
	{
		const Note &m = g_song.tr[g_act[i].tr].n[g_act[i].idx];
		if (m.tick + m.dur <= now) { syn (m.ch, 0x80, m.key, 0); g_act[i] = g_act[--g_nact]; }
		else i++;
	}
	for (int i = 0; i < NPART; i++)
	{
		Track &t = g_song.tr[i];
		while (g_idx[i] < t.count && t.n[g_idx[i]].tick <= now)
		{
			const Note &m = t.n[g_idx[i]];
			if (m.kind == NK_PROGRAM) syn (m.ch, 0xC0, m.key, 0);
			else if (m.kind == NK_CC) syn (m.ch, 0xB0, m.key, m.key == 7 ? part_volume (i, m.vel) : m.vel);
			else
			{
				syn (m.ch, 0x90, m.key, m.vel);
				if (g_nact < MAX_ACTIVE) { g_act[g_nact].tr = i; g_act[g_nact].idx = g_idx[i]; g_nact++; }
			}
			g_idx[i]++;
		}
	}
}
static double ticks_per_block () { return (double) BLOCK * g_song.tempo * PPQ / (60.0 * (double) RATE); }

static void export_run (short *buf)
{
	long enc = ak_enc_open ((long) g_expPath, g_expFormat, RATE, 2, g_expBits);
	if (enc <= 0) { g_expState = EXPORT_FAIL; return; }
	pthread_mutex_lock (&g_lk);
	uint32_t end = g_song.length ();
	seq_rewind (0);
	pthread_mutex_unlock (&g_lk);
	double tick = 0, step = ticks_per_block ();
	long tail = TAIL_FRAMES; bool ok = true;
	while (tail > 0 && !g_expCancel && !g_quit)
	{
		pthread_mutex_lock (&g_lk);
		seq_dispatch ((uint32_t) tick);
		pthread_mutex_unlock (&g_lk);
		ak_synth_render (g_synth, (long) buf, BLOCK);
		if (ak_enc_write (enc, (long) buf, BLOCK) < 0) { ok = false; break; }
		tick += step;
		if (tick >= end) tail -= BLOCK;
		g_tick = tick;
		g_expPct = end ? (int) (tick * 100 / end) : 100;
	}
	ak_enc_close (enc);
	pthread_mutex_lock (&g_lk); all_off (); g_tick = 0; pthread_mutex_unlock (&g_lk);
	g_expState = !ok ? EXPORT_FAIL : g_expCancel ? EXPORT_CANCEL : EXPORT_DONE;
}

static void end_recipe ();			// (plus bas : le prereglage de la composition finie)
static void *audio_main (void *)
{
	long s = ak_synth_new (0, 0);
	if (s <= 0) return 0;
	long out = ak_open (RATE, 2, 16, 0);
	g_synth = s; g_synthOk = true;
	static short buf[BLOCK * 2];
	while (!g_quit)
	{
		if (g_mode == M_EXPORT)
		{
			export_run (buf);
			g_mode = M_IDLE;
			continue;
		}
		pthread_mutex_lock (&g_lk);
		if (g_mode != M_IDLE)
		{
			double tick = g_tick + ticks_per_block ();
			if (g_mode == M_COMPOSE) g_comp.advance ((uint32_t) tick + LOOKAHEAD);
			seq_dispatch ((uint32_t) tick);
			g_tick = tick;
			if (g_mode == M_PLAY && (uint32_t) tick > g_song.length ()) { all_off (); g_mode = M_IDLE; g_tick = 0; }
			// (la forme est finie et ses dernieres notes se sont tues : la composition s'arrete d'elle-meme)
			if (g_mode == M_COMPOSE && g_comp.ended () && (uint32_t) tick > (g_song.length () > g_comp.end () ? g_song.length () : g_comp.end ()))
			{ end_recipe (); all_off (); g_mode = M_IDLE; g_tick = 0; }
		}
		pthread_mutex_unlock (&g_lk);
		ak_synth_render (s, (long) buf, BLOCK);
		if (out > 0) ((long (*) (long, long, long)) ak_write_all) (out, (long) buf, sizeof buf);
		else usleep (5000);
	}
	if (out > 0) ak_close (out);
	return 0;
}
static void audio_start () { g_threadOn = pthread_create (&g_tid, 0, audio_main, 0) == 0; }
static void audio_stop () { g_quit = true; if (g_threadOn) pthread_join (g_tid, 0); }

// ---- ce que demande la fenetre ------------------------------------------------------------------------------------------
// le prereglage du morceau : celui de la derniere composition, ou celui qui l'a recompose ; 0 : un morceau MIDI
static Recipe *g_recipe;
static void end_recipe ()
{
	if (g_mode != M_COMPOSE || !g_comp.rec) return;
	delete g_recipe;
	g_recipe = g_comp.rec;
	g_recipe->length = g_comp.done;
	g_comp.rec = 0;
}
static void cmd_stop ()
{
	pthread_mutex_lock (&g_lk);
	if (g_mode != M_EXPORT) { end_recipe (); g_mode = M_IDLE; all_off (); }
	pthread_mutex_unlock (&g_lk);
}
static void cmd_play ()
{
	pthread_mutex_lock (&g_lk);
	seq_rewind ((uint32_t) g_tick);
	g_mode = M_PLAY;
	pthread_mutex_unlock (&g_lk);
}
static void cmd_compose (uint32_t seed)
{
	pthread_mutex_lock (&g_lk);
	end_recipe ();
	g_comp.rec = new Recipe ();
	g_comp.start (seed);
	delete g_recipe; g_recipe = 0;
	g_tick = 0;
	seq_rewind (0);
	g_mode = M_COMPOSE;
	pthread_mutex_unlock (&g_lk);
}
static void cmd_seek (uint32_t tick)
{
	pthread_mutex_lock (&g_lk);
	g_tick = tick;
	if (g_mode == M_PLAY) seq_rewind (tick);
	pthread_mutex_unlock (&g_lk);
}
// AKF_WAV, AKF_MP3 ; bits : le debit (0 : celui du kit)
static void cmd_export (const char *path, int format, int bits)
{
	cmd_stop ();
	strncpy (g_expPath, path, sizeof g_expPath - 1);
	g_expFormat = format; g_expBits = bits; g_expPct = 0; g_expCancel = false;
	g_expState = EXPORT_RUN;
	g_mode = M_EXPORT;
}

#endif
