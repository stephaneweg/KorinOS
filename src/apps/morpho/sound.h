//
// sound.h -- Morpho : le son, sur le synthétiseur General MIDI d'AudioKit (la SoundFont du système, sinon sa banque FM),
// dans un fil à lui qui rend et écrit le flux : la fenêtre n'attend jamais.
//
//   La musique : générée en continu, douce, jamais tout à fait la même. Une grille I vi IV V (ou i VI III VII, triste),
//   une nappe, une basse, une harpe en arpège, et une mélodie pentatonique : un motif de deux mesures, repris varié sur
//   l'accord suivant. Son humeur suit l'animal : le jour (célesta, 92 à la noire), la nuit et le sommeil (boîte à musique,
//   66), la tristesse ou la maladie (vibraphone, mineur), l'œuf (boîte à musique, clairsemée), la métamorphose (chœur
//   et harpe qui montent). Le passage d'une humeur à l'autre se fait en fondu.
//   Les bruitages : la voix de l'animal (un ocarina, plus aigu quand il est petit, avec des glissés), des percussions
//   douces (bois, maracas, triangle), le glockenspiel, la harpe, le chœur.
//
// Le programme : snd_open (), snd_mood (), snd_play (), snd_close (). Les notes passent par une file protégée : un appel
// ne fait que les déposer, avec leur délai.
//
#ifndef MORPHO_SOUND_H
#define MORPHO_SOUND_H
#include <pthread.h>
#include <audiokit_abi.h>
#include "m3d.h"

namespace mo {

enum { MU_NONE, MU_DAY, MU_NIGHT, MU_SAD, MU_EGG, MU_EVOLVE };
enum { SX_HAPPY, SX_SAD, SX_EAT, SX_BOUNCE, SX_CATCH, SX_POP, SX_EVOLVE, SX_HATCH, SX_YUCK, SX_SPLASH, SX_CLEAN, SX_PURR,
       SX_CRACK, SX_BLIP, SX_CHIRP, SX_SNORE, SX_SICK, SX_YUM, SX_WAKE, SX_SLEEP, SX_REFUSE, SX_GONE, SX_MISS };

struct Sound
{
	enum { RATE = 44100, BLOCK = 512, QN = 512 };
	enum { C_MEL, C_PAD, C_BASS, C_ARP, C_DRUM = 9, C_VOICE, C_GLOCK, C_HARP, C_CHOIR, C_HUM, C_LOW };
	struct Ev { long long at; int ch, cmd, a, b; };

	pthread_t tid; bool running = false;
	volatile bool quit = false;
	pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
	Ev q[QN]; int nq = 0;
	volatile long long clock = 0;			// les trames déjà rendues
	long syn = 0, out = -1;
	volatile int want = MU_DAY;			// l'humeur voulue
	volatile int musicOn = 1, sfxOn = 1;
	volatile int keyBase = 60;
	// (le fil seul)
	int mode = MU_NONE;
	float gain = 0;
	long left = 0;					// trames avant le pas suivant
	int step = 0, bar = 0;
	Rng rng = Rng (12345);				// (le fil de la musique)
	Rng srng = Rng (777);				// (les bruitages, le programme : chacun le sien)
	int motif[16], motifRest[16];			// deux mesures de croches : degrés (-1 : tenue), silences
	int melNote = -1, padNotes[4] = { -1, -1, -1, -1 }, bassNote = -1, arpNote = -1;
	int melIdx = 7;

	// ---- la file (le programme) ----
	void push (int delayMs, int ch, int cmd, int a, int b)
	{
		pthread_mutex_lock (&lock);
		if (nq < QN) q[nq++] = { clock + (long long) delayMs * RATE / 1000 + BLOCK, ch, cmd, a, b };
		pthread_mutex_unlock (&lock);
	}
	void note (int delayMs, int ch, int k, int vel, int durMs)
	{
		if (k < 0 || k > 127) return;
		push (delayMs, ch, 0x90, k, vel);
		push (delayMs + durMs, ch, 0x80, k, 0);
	}
	void bend (int delayMs, int ch, int v)		// v -8192..8191
	{
		int x = clampi (v + 8192, 0, 16383);
		push (delayMs, ch, 0xE0, x & 127, x >> 7);
	}

	// ---- le fil ----
	void midi (int ch, int cmd, int a, int b) { if (syn) ak_synth_midi (syn, ch, cmd, a, b); }
	void prog (int ch, int p) { midi (ch, 0xC0, p, 0); }
	void cc (int ch, int c, int v) { midi (ch, 0xB0, c, clampi (v, 0, 127)); }
	void off (int &n, int ch) { if (n >= 0) midi (ch, 0x80, n, 0); n = -1; }
	struct Look { int bpm, mel, pad, arp, velMel, volMel, volPad, volBass, volArp; bool minor, useArp, sparse; };
	Look look () const
	{
		switch (mode)
		{
		case MU_NIGHT: return { 66, 10, 92, 46, 58, 82, 52, 50, 0, false, false, true };
		case MU_SAD: return { 72, 11, 49, 46, 64, 80, 54, 56, 0, true, false, false };
		case MU_EGG: return { 70, 10, 89, 46, 60, 80, 46, 0, 0, false, false, true };
		case MU_EVOLVE: return { 100, 8, 52, 46, 80, 84, 76, 60, 72, false, true, false };
		default: return { 92, 8, 89, 46, 74, 86, 48, 58, 44, false, true, false };
		}
	}
	void setup ()
	{
		Look L = look ();
		for (int c = 0; c < 4; c++) cc (c, 123, 0);
		melNote = bassNote = arpNote = -1;
		for (int &n : padNotes) n = -1;
		prog (C_MEL, L.mel); prog (C_PAD, L.pad); prog (C_BASS, 32); prog (C_ARP, L.arp);
		cc (C_MEL, 91, 70); cc (C_PAD, 91, 90); cc (C_ARP, 91, 70); cc (C_BASS, 91, 30);
		step = 0; bar = 0; left = 0;
		volumes ();
	}
	void volumes ()
	{
		Look L = look ();
		float g = gain * (musicOn ? 1 : 0);
		cc (C_MEL, 7, (int) ((float) L.volMel * g)); cc (C_PAD, 7, (int) ((float) L.volPad * g));
		cc (C_BASS, 7, (int) ((float) L.volBass * g)); cc (C_ARP, 7, (int) ((float) L.volArp * g));
	}
	// la gamme pentatonique (majeure ou mineure) : le degré d -> la touche
	int deg (int d, bool minor) const
	{
		static const int MAJ[5] = { 0, 2, 4, 7, 9 }, MIN[5] = { 0, 3, 5, 7, 10 };
		int o = d >= 0 ? d / 5 : (d - 4) / 5, i = d - o * 5;
		return keyBase + o * 12 + (minor ? MIN[i] : MAJ[i]);
	}
	// l'accord de la mesure : sa fondamentale (demi-tons), mineur ou non
	void chord (int b, bool minor, int &root, bool &mn) const
	{
		static const int MAJ[4] = { 0, 9, 5, 7 }, MIN[4] = { 0, 8, 3, 10 };
		static const bool MAJm[4] = { false, true, false, false }, MINm[4] = { true, false, false, false };
		int i = (b / 2) % 4;
		root = minor ? MIN[i] : MAJ[i];
		mn = minor ? MINm[i] : MAJm[i];
	}
	void newMotif (bool sparse)
	{
		static const char *const RH[8] = { "x-x-x---", "x--x--x-", "x-xx-x--", "x---x---", "xx-x-x--", "x-----x-", "x-x---x-", "x--xx---" };
		for (int m = 0; m < 2; m++)
		{
			const char *r = RH[rng.irange (0, sparse ? 3 : 7) + (sparse && rng.chance (0.5f) ? 4 : 0) - (sparse ? 0 : 0)];
			if (sparse) r = rng.chance (0.5f) ? RH[3] : RH[5];
			for (int s = 0; s < 8; s++)
			{
				int i = m * 8 + s;
				motifRest[i] = r[s] == '.';
				if (r[s] == 'x') { melIdx = clampi (melIdx + rng.irange (-2, 2), 4, 12); motif[i] = melIdx; }
				else motif[i] = -1;
			}
		}
	}
	void tick ()
	{
		Look L = look ();
		int s = step % 8;
		if (s == 0)
		{
			if (bar % 4 == 0) newMotif (L.sparse);
			else if (bar % 4 == 2)
				for (int i = 0; i < 16; i++) if (motif[i] >= 0 && rng.chance (0.3f)) motif[i] = clampi (motif[i] + rng.irange (-1, 1), 3, 13);
		}
		int root; bool mn;
		chord (bar, L.minor, root, mn);
		// la nappe : l'accord, tenu deux mesures
		if (s == 0 && bar % 2 == 0)
		{
			for (int &n : padNotes) off (n, C_PAD);
			int k = keyBase - 12 + root;
			int tones[3] = { k, k + (mn ? 3 : 4), k + 7 };
			for (int i = 0; i < 3; i++) { padNotes[i] = tones[i]; midi (C_PAD, 0x90, tones[i], 60); }
		}
		// la basse
		if ((s == 0 || (s == 4 && !L.sparse)) && L.volBass)
		{
			off (bassNote, C_BASS);
			bassNote = keyBase - 24 + root + (s == 4 ? 7 : 0);
			midi (C_BASS, 0x90, bassNote, s == 0 ? 72 : 56);
		}
		// la harpe en arpège
		if (L.useArp)
		{
			static const int PAT[8] = { 0, 1, 2, 3, 2, 1, 0, 1 };
			off (arpNote, C_ARP);
			int k = keyBase + root, t[4] = { k, k + (mn ? 3 : 4), k + 7, k + 12 };
			arpNote = t[PAT[s]];
			midi (C_ARP, 0x90, arpNote, 44 + (s == 0 ? 10 : 0));
		}
		// la mélodie : le motif, posé sur l'accord
		int i = (bar % 2) * 8 + s;
		if (motif[i] >= 0)
		{
			off (melNote, C_MEL);
			int d = motif[i];
			int k = deg (d, L.minor);
			// (au premier temps : la note de l'accord la plus proche)
			if (s == 0)
			{
				int best = k, bd = 99;
				for (int o = -1; o <= 2; o++)
				{
					int base = keyBase + root + 12 * o, ct[3] = { base, base + (mn ? 3 : 4), base + 7 };
					for (int c : ct) if (abs (c - k) < bd) { bd = abs (c - k); best = c; }
				}
				k = best;
			}
			melNote = k;
			midi (C_MEL, 0x90, k, L.velMel + rng.irange (-8, 8) + (s == 0 ? 6 : 0));
		}
		else if (motifRest[i]) off (melNote, C_MEL);
		if (++step % 8 == 0) bar++;
	}
	void loop ()
	{
		static short buf[BLOCK * 2];
		while (!quit)
		{
			// les notes déposées, quand leur heure vient
			pthread_mutex_lock (&lock);
			for (int i = 0; i < nq; )
				if (q[i].at <= clock) { if (sfxOn || q[i].cmd == 0x80) midi (q[i].ch, q[i].cmd, q[i].a, q[i].b); q[i] = q[--nq]; }
				else i++;
			pthread_mutex_unlock (&lock);
			// l'humeur : un fondu, puis l'autre
			int w = musicOn ? want : MU_NONE;
			float to = w == mode && mode != MU_NONE ? 1.0f : 0.0f;
			float g0 = gain;
			gain += clampf (to - gain, -(float) BLOCK / ((float) RATE * 1.2f), (float) BLOCK / ((float) RATE * 1.5f));
			if (gain <= 0.001f && w != mode)
			{
				for (int &n : padNotes) off (n, C_PAD);
				off (melNote, C_MEL); off (bassNote, C_BASS); off (arpNote, C_ARP);
				mode = w;
				if (mode != MU_NONE) setup ();
			}
			if (fabsf (gain - g0) > 1e-4f) volumes ();
			long done = 0;
			while (done < BLOCK)
			{
				if (mode != MU_NONE && left <= 0) { tick (); left = (long) RATE * 60 / (look ().bpm * 2); }
				long n = BLOCK - done;
				if (mode != MU_NONE && n > left) n = left;
				ak_synth_render (syn, (long) (buf + 2 * done), n);
				if (mode != MU_NONE) left -= n;
				done += n;
			}
			// le gain d'ensemble, et un limiteur doux (tanh) : la banque FM est faible, la SoundFont plus forte
			for (int i = 0; i < BLOCK * 2; i++) buf[i] = (short) (32000.0f * tanhf ((float) buf[i] * (4.0f / 32000.0f)));
			clock += BLOCK;
			ak_write_all (out, (long) buf, sizeof buf);
		}
	}
	static void *run (void *self)
	{
		Sound *S = (Sound *) self;
		long s = ak_synth_new (0, 0);
		if (s <= 0) return 0;
		long o = ak_open (RATE, 2, 16, 0);
		if (o < 0) { ak_synth_free (s); return 0; }
		S->syn = s; S->out = o;
		S->prog (C_VOICE, 79); S->prog (C_GLOCK, 9); S->prog (C_HARP, 46); S->prog (C_CHOIR, 52); S->prog (C_HUM, 89); S->prog (C_LOW, 57);
		S->cc (C_VOICE, 7, 100); S->cc (C_GLOCK, 7, 80); S->cc (C_HARP, 7, 90); S->cc (C_CHOIR, 7, 84); S->cc (C_HUM, 7, 90);
		S->cc (C_LOW, 7, 70); S->cc (C_DRUM, 7, 92);
		for (int c = C_VOICE; c <= C_LOW; c++) S->cc (c, 91, 60);
		S->loop ();
		ak_close (o);
		ak_synth_free (s);
		return 0;
	}
	void open () { running = pthread_create (&tid, 0, run, this) == 0; }
	void close () { quit = true; if (running) pthread_join (tid, 0); running = false; }

	// ---- les bruitages (le programme) : t = transposition de la voix, en demi-tons ----
	// (les percussions General MIDI : 37 baguette sur le cercle, 39 claquement de mains, 45 tom grave, 49 cymbale, 54
	// tambourin, 55 cymbale splash, 63/64 congas, 69 cabasa, 70 maracas, 76/77 blocs de bois, 80/81 triangle)
	void voice (int at, int k, int vel, int dur, int bendTo = 0, int bendAt = -1)
	{
		bend (at, C_VOICE, 0);
		note (at, C_VOICE, k, vel, dur);
		if (bendTo) { bend (bendAt < 0 ? at + dur / 2 : bendAt, C_VOICE, bendTo); bend (at + dur + 10, C_VOICE, 0); }
	}
	void sparkle (int at, int n, int base, int vel)
	{
		static const int UP[6] = { 0, 4, 7, 12, 16, 19 };
		for (int i = 0; i < n; i++) note (at + i * 55, C_GLOCK, base + UP[i % 6] + 12 * (i / 6), vel - i * 3, 220);
	}
	void play (int id, int t)
	{
		if (!sfxOn) return;
		int v = 72 + t;					// (la voix : autour du do aigu, plus haut pour un petit)
		switch (id)
		{
		case SX_HAPPY:
			// une appoggiature, trois notes qui montent, un glissé vers le haut ; une pluie de glockenspiel, le tambourin
			voice (0, v + 12, 55, 35);
			voice (40, v + 4, 88, 90); voice (130, v + 7, 92, 90); voice (220, v + 12, 96, 200, 2400, 300);
			note (0, C_DRUM, 54, 46, 120); note (240, C_DRUM, 54, 38, 120);
			sparkle (260, 4, 84, 58);
			break;
		case SX_CHIRP:
		{
			int n = srng.irange (1, 3), k = v + srng.irange (0, 7);
			for (int i = 0; i < n; i++) voice (i * 110, k + (i & 1 ? 5 : 0) + srng.irange (-2, 2), 70 + srng.irange (0, 20), 80 + srng.irange (0, 60),
							     srng.chance (0.5f) ? 1500 : -1200);
			break;
		}
		case SX_SAD: voice (0, v + 2, 80, 200); voice (200, v - 3, 76, 380, -2400, 330); note (200, C_HUM, 48 + t / 2, 40, 600); break;
		case SX_REFUSE: voice (0, v - 2, 82, 90); voice (110, v - 5, 82, 160, -1600); note (0, C_DRUM, 77, 50, 60); break;
		case SX_SICK:		// « atchoum » : une inspiration qui monte, l'éternuement, un petit gémissement
			note (0, C_HUM, 50 + t / 2, 50, 420); bend (0, C_HUM, -2000); bend (380, C_HUM, 2500); bend (520, C_HUM, 0);
			note (440, C_DRUM, 39, 74, 80); note (450, C_DRUM, 70, 70, 80); voice (470, v + 7, 70, 90, -3000);
			voice (900, v - 5, 54, 420, -2600, 1100);
			break;
		case SX_EAT:		// une bouchée : le croquant (bois, maracas), un « miam » grave
		{
			int r = srng.irange (-2, 2);
			note (0, C_DRUM, 76, 74, 50); note (35, C_DRUM, 70, 58, 60); note (80, C_DRUM, 77, 50, 50);
			voice (60, v - 12 + r, 64, 110, -900);
			note (160, C_DRUM, 69, 40, 60);
			break;
		}
		case SX_YUM:		// « mmm » satisfait, avec vibrato, puis un ding
			voice (0, v - 5, 70, 520);
			for (int i = 0; i < 6; i++) bend (60 + i * 70, C_VOICE, i & 1 ? -500 : 500);
			bend (500, C_VOICE, 0);
			voice (520, v + 4, 78, 160, 1800);
			note (600, C_GLOCK, 96, 50, 300);
			break;
		case SX_BOUNCE: { int r = srng.irange (0, 2); note (0, C_DRUM, r ? 77 : 76, 50 + srng.irange (0, 10), 80); note (0, C_DRUM, r == 2 ? 47 : 45, 30, 120); break; }
		case SX_MISS:		// raté : « boing » sur la tête, un petit « oh » qui tombe
			note (0, C_DRUM, 45, 70, 120); note (40, C_DRUM, 77, 60, 80);
			voice (90, v + 4, 74, 260, -3000, 200);
			break;
		case SX_CATCH:		// « hop ! » et deux notes claires
			voice (0, v + 7, 90, 70, 2000); note (0, C_DRUM, 39, 50, 60);
			note (60, C_GLOCK, 84, 72, 140); note (140, C_GLOCK, 91, 74, 260);
			break;
		case SX_POP:		// « plop » : deux congas, la voix qui tombe
			note (0, C_DRUM, 64, 84, 90); note (80, C_DRUM, 63, 60, 90);
			voice (40, v - 7, 50, 120, -3500);
			break;
		case SX_EVOLVE:		// roulement, harpe qui monte, chœur qui s'enfle, scintillements, cymbale, la fanfare
			for (int i = 0; i < 16; i++) note (i * 70, C_DRUM, 45, 30 + i * 3, 70);
			for (int i = 0; i < 15; i++) note (300 + i * 45, C_HARP, deg (5 + i, false) - keyBase + 48, 72, 700);
			note (0, C_CHOIR, 60, 60, 3900); note (0, C_CHOIR, 64, 56, 3900); note (400, C_CHOIR, 67, 58, 3500); note (1100, C_CHOIR, 72, 58, 2800);
			for (int i = 0; i < 4; i++) sparkle (900 + i * 420, 3, 84 + i * 2, 50);
			note (2400, C_DRUM, 55, 62, 900); note (2400, C_DRUM, 81, 64, 900);
			for (int i = 0; i < 4; i++) { static const int F[4] = { 72, 76, 79, 84 }; note (2450 + i * 110, C_GLOCK, F[i], 80, 300); }
			note (2900, C_GLOCK, 88, 82, 900);
			voice (3400, v + 7, 90, 120); voice (3520, v + 12, 96, 280, 2400);
			break;
		case SX_CRACK:		// une fêlure, parfois un petit cri dans l'œuf
			note (0, C_DRUM, 37, 82 + srng.irange (0, 12), 50); note (30, C_DRUM, 76, 44, 40);
			if (srng.chance (0.5f)) voice (180, v + 12, 34, 70, 1600);
			break;
		case SX_HATCH:		// l'éclat : triangle, cymbale, harpe, fanfare, chœur ; puis le premier cri
			note (0, C_DRUM, 37, 100, 60); note (0, C_DRUM, 55, 66, 900); note (20, C_DRUM, 81, 76, 1200);
			for (int i = 0; i < 10; i++) note (30 + i * 28, C_HARP, deg (8 + i, false) - keyBase + 48, 68, 500);
			for (int i = 0; i < 5; i++) { static const int F[5] = { 72, 76, 79, 84, 88 }; note (120 + i * 95, C_GLOCK, F[i], 78, 320); }
			note (100, C_CHOIR, 64, 50, 1600); note (100, C_CHOIR, 67, 46, 1600); note (100, C_CHOIR, 72, 46, 1600);
			voice (900, v + 12, 80, 90); voice (1000, v + 16, 86, 90); voice (1100, v + 19, 92, 240, 2600);
			break;
		case SX_YUCK:		// la grimace : le trombone qui s'effondre, « beurk »
			note (0, C_LOW, 43, 82, 460); bend (0, C_LOW, 0); bend (140, C_LOW, -3800); bend (560, C_LOW, 0);
			voice (60, v - 10, 70, 160, -3000); note (300, C_DRUM, 70, 40, 60);
			break;
		case SX_SPLASH:		// le bain : un ruissellement de maracas et de cabasa, la harpe qui descend, des bulles
			for (int i = 0; i < 8; i++) note (i * 42, C_DRUM, i & 1 ? 70 : 69, 72 - i * 5, 60);
			for (int i = 0; i < 7; i++) note (60 + i * 38, C_HARP, 98 - i * 4, 52, 300);
			for (int i = 0; i < 9; i++) note (350 + srng.irange (0, 1300), C_GLOCK, 96 + srng.irange (0, 12), 26 + srng.irange (0, 16), 70);
			voice (500, v + 5, 60, 160, 1500);
			break;
		case SX_CLEAN:		// tout propre : une gamme scintillante et le triangle
			sparkle (0, 5, 84, 62); note (240, C_DRUM, 81, 52, 900); note (0, C_DRUM, 69, 40, 80);
			break;
		case SX_PURR:		// le ronron : une note grave qui ondule
			note (0, C_HUM, 40 + t / 2, 48, 620); bend (0, C_HUM, 0);
			for (int i = 0; i < 6; i++) bend (90 * i, C_HUM, i & 1 ? -350 : 350);
			bend (640, C_HUM, 0);
			break;
		case SX_SNORE:		// inspirer (monte), expirer (descend, un petit sifflement)
			note (0, C_HUM, 33 + t / 3, 34, 1400); bend (0, C_HUM, -900); bend (650, C_HUM, 300); bend (1100, C_HUM, -1200); bend (1450, C_HUM, 0);
			if (srng.chance (0.4f)) voice (900, v + 19, 16, 260, -900);
			break;
		case SX_SLEEP:		// il s'endort : un bâillement qui descend, une berceuse de trois notes
			voice (0, v, 70, 700, -3200, 250);
			note (750, C_GLOCK, 79, 40, 400); note (1050, C_GLOCK, 76, 36, 400); note (1350, C_GLOCK, 72, 34, 700);
			break;
		case SX_WAKE:		// il se réveille : un bâillement qui monte, deux notes vives
			voice (0, v - 5, 66, 600, 2800, 300);
			voice (650, v + 7, 86, 90); voice (760, v + 12, 90, 160, 1500);
			break;
		case SX_GONE:		// le départ : le chœur, la harpe qui monte lentement, le triangle
			note (0, C_CHOIR, 60, 56, 3500); note (0, C_CHOIR, 67, 50, 3500); note (600, C_CHOIR, 76, 46, 2900);
			for (int i = 0; i < 12; i++) note (i * 160, C_HARP, deg (5 + i, false) - keyBase + 48, 54, 900);
			note (2000, C_DRUM, 81, 46, 1400);
			break;
		case SX_BLIP: note (0, C_GLOCK, 96, 34, 60); break;
		}
	}
};

}	// namespace mo
#endif
