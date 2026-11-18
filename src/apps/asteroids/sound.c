// sound.c -- les effets sonores d'Asteroids : un petit synthétiseur logiciel (bruit filtré, ondes,
// enveloppes) qui tourne dans son propre thread et écrit dans un flux AudioKit à 22 050 Hz, mono.
#include <math.h>
#include <pthread.h>
#include <string.h>
#include <audiokit_abi.h>
#include "sound.h"

#define RATE	22050
#define BLOCK	256			// trames par bloc (11,6 ms)
#define MAXV	24			// voix simultanées
#define QMAX	32
#define TAU	6.2831853f

typedef struct
{
	int type;			// SFX_*, -1 : libre
	float t, dur;			// temps écoulé, durée (s)
	float ph, ph2;			// phases des oscillateurs (0..1)
	float lp1, lp2;			// états des filtres
	unsigned rng;			// bruit propre à la voix
} voice;

static long s_stream = -1;
static pthread_t s_thread;
static int s_thread_ok;
static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static int s_queue[QMAX], s_nq;
static volatile int s_quit, s_on = 1, s_ufo;
static volatile float s_thrust;
static voice s_v[MAXV];

// états des sons continus (réacteur, sirène)
static float s_thr_lvl, s_thr_lp1, s_thr_lp2, s_ufo_lvl, s_ufo_ph, s_ufo_t;
static int s_ufo_last = 1;
static unsigned s_rng = 0x2545F491u;

static const float s_dur[SFX_COUNT] = {
	0.24f, 1.7f, 1.1f, 0.65f, 2.6f, 0.2f, 0.2f, 0.25f, 1.4f, 0.75f, 0.7f, 1.0f
};

static inline float noise (unsigned *r)
{
	*r = *r * 1664525u + 1013904223u;
	return (float) (int) *r * (1.0f / 2147483648.0f);	// -1..1
}

static inline float wrap1 (float p) { return p >= 1.0f ? p - 1.0f : p; }

// bruit passé par deux filtres passe-bas, à niveau à peu près constant quelle que soit la coupure
static inline float rumble (voice *v, float cut)
{
	float n = noise (&v->rng);
	v->lp1 += cut * (n - v->lp1);
	v->lp2 += cut * (v->lp1 - v->lp2);
	return v->lp2 * (0.6f / sqrtf (cut));
}

// une explosion : bruit dont la coupure descend, plus un coup sourd
static float bang (voice *v, float decay, float cut0, float thump, float gain)
{
	float t = v->t;
	float cut = cut0 * expf (-t * 1.6f) + 0.015f;
	float env = (t < 0.004f ? t / 0.004f : 1.0f) * expf (-t * decay);
	float s = rumble (v, cut) * env * gain;
	if (thump > 0)
	{
		float f = thump * expf (-t * 3.0f) + 22.0f;
		v->ph = wrap1 (v->ph + f / RATE);
		s += sinf (TAU * v->ph) * expf (-t * 6.0f) * 0.55f * gain;
	}
	return s;
}

static float voice_sample (voice *v)
{
	float t = v->t, s = 0;
	switch (v->type)
	{
	case SFX_FIRE:			// « piou » : onde carrée et triangle qui plongent
	{
		float f = 260.0f + 1650.0f * expf (-t * 20.0f);
		v->ph = wrap1 (v->ph + f / RATE);
		float sq = v->ph < 0.5f ? 1.0f : -1.0f;
		float tri = 4.0f * fabsf (v->ph - 0.5f) - 1.0f;
		s = (0.35f * sq + 0.65f * tri) * expf (-t * 15.0f) * 0.30f;
		break;
	}
	case SFX_BANG_L: s = bang (v, 2.4f, 0.22f, 58.0f, 1.25f); break;
	case SFX_BANG_M: s = bang (v, 3.8f, 0.32f, 80.0f, 1.0f); break;
	case SFX_BANG_S: s = bang (v, 6.5f, 0.48f, 0.0f, 0.8f); break;
	case SFX_UFO_BANG:
		s = bang (v, 3.0f, 0.4f, 70.0f, 1.1f);
		v->ph2 = wrap1 (v->ph2 + (900.0f * expf (-t * 2.5f) + 80.0f) / RATE);
		s += sinf (TAU * v->ph2) * expf (-t * 3.0f) * 0.15f;
		break;
	case SFX_DIE:			// longue explosion et plainte qui descend
		s = bang (v, 1.35f, 0.30f, 50.0f, 1.4f);
		v->ph2 = wrap1 (v->ph2 + (520.0f * expf (-t * 1.3f) + 40.0f) / RATE);
		s += (2.0f * fabsf (2.0f * v->ph2 - 1.0f) - 1.0f) * expf (-t * 1.4f) * 0.22f;
		break;
	case SFX_BEAT1:
	case SFX_BEAT2:			// le battement sourd des bornes d'arcade
	{
		float f0 = v->type == SFX_BEAT1 ? 64.0f : 54.0f;
		float f = f0 * (1.0f + 0.7f * expf (-t * 28.0f));
		v->ph = wrap1 (v->ph + f / RATE);
		float env = (t < 0.003f ? t / 0.003f : 1.0f) * expf (-t * 13.0f);
		s = (sinf (TAU * v->ph) + 0.45f * sinf (2.0f * TAU * v->ph) + 0.2f * sinf (3.0f * TAU * v->ph)) * env * 0.42f;
		break;
	}
	case SFX_UFO_FIRE:		// tir ennemi : montée nasillarde
	{
		float f = 620.0f + 2600.0f * t;
		v->ph = wrap1 (v->ph + f / RATE);
		float x = sinf (TAU * v->ph) * 3.0f;
		s = x / (1.0f + fabsf (x)) * expf (-t * 11.0f) * 0.22f;
		break;
	}
	case SFX_LIFE:			// carillon : quatre notes montantes
	{
		static const float notes[4] = { 988.0f, 1319.0f, 1568.0f, 1976.0f };
		int i = (int) (t / 0.1f);
		if (i > 3) i = 3;
		float tn = t - i * 0.1f;
		v->ph = wrap1 (v->ph + notes[i] / RATE);
		s = (sinf (TAU * v->ph) + 0.3f * sinf (2.0f * TAU * v->ph)) * expf (-tn * 9.0f) * 0.22f;
		break;
	}
	case SFX_HYPER:			// balayage montant avec vibrato
	{
		float f = 140.0f * powf (18.0f, t / 0.7f) * (1.0f + 0.04f * sinf (TAU * 28.0f * t));
		v->ph = wrap1 (v->ph + f / RATE);
		float env = sinf (3.14159f * fminf (t / 0.7f, 1.0f));
		s = sinf (TAU * v->ph) * env * 0.22f + rumble (v, 0.3f) * env * 0.05f;
		break;
	}
	case SFX_START:			// accord arpégé en triangle
	{
		static const float notes[4] = { 392.0f, 523.0f, 659.0f, 784.0f };
		for (int i = 0; i < 4; i++)
		{
			float tn = t - i * 0.11f;
			if (tn < 0) break;
			float p = fmodf (notes[i] * t, 1.0f);
			s += (4.0f * fabsf (p - 0.5f) - 1.0f) * expf (-tn * 3.5f) * 0.11f;
		}
		break;
	}
	}
	v->t += 1.0f / RATE;
	if (v->t >= v->dur) v->type = -1;
	return s;
}

static void voice_start (int type)
{
	int k = -1;
	float oldest = -1;
	for (int i = 0; i < MAXV; i++)
	{
		if (s_v[i].type < 0) { k = i; break; }
		if (s_v[i].t > oldest) { oldest = s_v[i].t; k = i; }
	}
	voice *v = &s_v[k];
	memset (v, 0, sizeof *v);
	v->type = type;
	v->dur = s_dur[type];
	s_rng = s_rng * 747796405u + 2891336453u;
	v->rng = s_rng | 1;
}

static void *audio_main (void *arg)
{
	static short buf[BLOCK];
	voice thr = { 0 };
	thr.rng = 0x9E3779B9u;
	(void) arg;
	while (!s_quit)
	{
		int q[QMAX], n;
		pthread_mutex_lock (&s_mu);
		n = s_nq;
		memcpy (q, s_queue, n * sizeof (int));
		s_nq = 0;
		pthread_mutex_unlock (&s_mu);
		for (int i = 0; i < n; i++) voice_start (q[i]);

		float thr_target = s_on ? s_thrust : 0;
		int ufo = s_on ? s_ufo : 0;
		for (int i = 0; i < BLOCK; i++)
		{
			float s = 0;
			for (int k = 0; k < MAXV; k++)
				if (s_v[k].type >= 0) s += voice_sample (&s_v[k]);

			// le réacteur : souffle grave qui suit la poussée
			s_thr_lvl += (thr_target - s_thr_lvl) * 0.0025f;
			if (s_thr_lvl > 0.002f)
			{
				thr.lp1 = s_thr_lp1; thr.lp2 = s_thr_lp2;
				float r = rumble (&thr, 0.05f + 0.05f * s_thr_lvl);
				s_thr_lp1 = thr.lp1; s_thr_lp2 = thr.lp2;
				s += r * s_thr_lvl * 0.55f;
			}

			// la sirène de l'ovni : onde arrondie modulée
			s_ufo_lvl += ((ufo ? 1.0f : 0.0f) - s_ufo_lvl) * 0.002f;
			s_ufo_t += 1.0f / RATE;
			if (s_ufo_t > 1000.0f) s_ufo_t -= 1000.0f;
			if (s_ufo_lvl > 0.002f)
			{
				if (ufo) s_ufo_last = ufo;		// en s'éteignant, la sirène garde sa voix
				int small = s_ufo_last == 2;
				float f = small ? 980.0f + 200.0f * sinf (TAU * 9.0f * s_ufo_t)
					: 540.0f + 110.0f * sinf (TAU * 4.5f * s_ufo_t);
				s_ufo_ph = wrap1 (s_ufo_ph + f / RATE);
				float x = sinf (TAU * s_ufo_ph) * 2.0f;
				s += x / (1.0f + fabsf (x)) * s_ufo_lvl * 0.07f;
			}

			if (!s_on) s = 0;
			// compression douce, puis 16 bits
			s *= 0.9f;
			s = s / (1.0f + fabsf (s) * 0.6f) * 1.25f;
			if (s > 1.0f) s = 1.0f;
			if (s < -1.0f) s = -1.0f;
			buf[i] = (short) (s * 30000.0f);
		}
		ak_write_all (s_stream, (long) buf, sizeof buf);	// attend la place : cadence le thread
	}
	return 0;
}

int snd_init (void)
{
	for (int i = 0; i < MAXV; i++) s_v[i].type = -1;
	if (!audiokit_kit_open ()) return 0;
	s_stream = ak_open (RATE, 1, 16, 0);
	if (s_stream < 0) return 0;
	ak_latency (s_stream, 6144);		// peu d'avance : le son suit l'image
	if (pthread_create (&s_thread, 0, audio_main, 0) != 0)
	{
		ak_close (s_stream);
		s_stream = -1;
		return 0;
	}
	s_thread_ok = 1;
	return 1;
}

void snd_quit (void)
{
	if (s_thread_ok)
	{
		s_quit = 1;
		pthread_join (s_thread, 0);
		s_thread_ok = 0;
	}
	if (s_stream >= 0) ak_close (s_stream);
	s_stream = -1;
}

void snd_play (int sfx)
{
	if (!s_thread_ok || !s_on || sfx < 0 || sfx >= SFX_COUNT) return;
	pthread_mutex_lock (&s_mu);
	if (s_nq < QMAX) s_queue[s_nq++] = sfx;
	pthread_mutex_unlock (&s_mu);
}

void snd_thrust (float level) { s_thrust = level < 0 ? 0 : level > 1 ? 1 : level; }
void snd_ufo (int kind) { s_ufo = kind; }
void snd_enable (int on) { s_on = on; }
