// audio.cpp -- TerraCube : la musique générative, l'ambiance et les effets (audio.h).
//
// Tout est synthétisé ici : des oscillateurs (dents de scie, sinus, modulation de fréquence pour les cloches), du bruit
// filtré (filtre d'état de Chamberlin) et un écho stéréo. Le thread du son calcule des blocs de 256 trames et les
// écrit dans un flux d'AudioKit (ak_write_all attend la place) ; la situation et les effets demandés arrivent par un
// verrou, une fois par bloc.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener.
#include "audio.h"

#include <math.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <vector>
#include <audiokit_abi.h>
#include "blocks.h"

namespace tc {
namespace audio {

namespace {

const float SR = 44100.0f;
const int BLOCK = 256;
const float TAU = 6.28318530718f;

// ---- petits outils ------------------------------------------------------------------------------------------------

struct Rng {
	uint32_t s = 0x9E3779B9u;
	uint32_t next ()
	{
		s ^= s << 13, s ^= s >> 17, s ^= s << 5;
		return s;
	}
	float uni () { return (next () >> 8) * (1.0f / 16777216.0f); }		// [0, 1)
	float bi () { return uni () * 2.0f - 1.0f; }				// [-1, 1)
	float range (float a, float b) { return a + (b - a) * uni (); }
	int pick (int n) { return (int) (next () % (uint32_t) n); }
};

inline float mtof (float n) { return 440.0f * powf (2.0f, (n - 69.0f) / 12.0f); }
inline float lpCoef (float fc) { return 1.0f - expf (-TAU * fc / SR); }

// filtre d'état (Chamberlin) : passe-bas, passe-bande, passe-haut
struct Svf {
	float low = 0, band = 0;
	float f = 0.1f, q = 1.0f;
	void set (float fc, float damp)
	{
		f = 2.0f * sinf (3.14159265f * std::min (fc, SR * 0.22f) / SR);
		q = damp;
	}
	void run (float in, float &lo, float &bp, float &hp)
	{
		low += f * band;
		hp = in - low - q * band;
		band += f * hp;
		lo = low, bp = band;
	}
};

// ---- les voix de la musique ---------------------------------------------------------------------------------------

// une nappe : deux dents de scie désaccordées et un sinus une octave dessous, adoucies par un passe-bas ; elle monte et
// descend lentement
struct Pad {
	float note = 60, freq = 261.6f;
	float ph[3] = { 0, 0.33f, 0.66f };
	float amp = 0, target = 0;
	float lp = 0, lp2 = 0;
	float pan = 0;
	void setNote (float n)
	{
		note = n;
		freq = mtof (n);
	}
	float tick (float bright, float &l, float &r)
	{
		amp += (target - amp) * (target > amp ? 0.00004f : 0.000025f);	// (≈ 0,6 s pour monter, plus pour descendre)
		if (amp < 1e-5f && target == 0) return 0;
		float s = 0;
		const float det[2] = { 0.9975f, 1.0026f };
		for (int i = 0; i < 2; i++) {
			ph[i] += freq * det[i] / SR;
			if (ph[i] >= 1) ph[i] -= 1;
			s += ph[i] * 2 - 1;
		}
		ph[2] += freq * 0.5f / SR;
		if (ph[2] >= 1) ph[2] -= 1;
		s = s * 0.35f + sinf (ph[2] * TAU) * 0.5f;
		float a = lpCoef (300 + 1500 * bright);
		lp += (s - lp) * a;
		lp2 += (lp - lp2) * a;
		float o = lp2 * amp;
		l += o * (0.5f - pan * 0.4f), r += o * (0.5f + pan * 0.4f);
		return o;
	}
};

// une note de la mélodie : modulation de fréquence (cloche, corde pincée, marimba), qui s'éteint seule
struct Bell {
	bool on = false;
	float fc, ratio, index, idxDecay, amp, ampDecay;
	float pc = 0, pm = 0;
	float pan = 0;
	float tick (float &l, float &r)
	{
		if (!on) return 0;
		pm += fc * ratio / SR;
		if (pm >= 1) pm -= 1;
		pc += fc / SR;
		if (pc >= 1) pc -= 1;
		float o = sinf (pc * TAU + index * sinf (pm * TAU)) * amp;
		index *= idxDecay;
		amp *= ampDecay;
		if (amp < 0.0005f) on = false;
		l += o * (0.5f - pan * 0.45f), r += o * (0.5f + pan * 0.45f);
		return o;
	}
};

// ---- les effets ---------------------------------------------------------------------------------------------------

struct Voice {
	bool on = false;
	int kind;
	float t = 0, dur;		// secondes
	float vol, panL, panR;
	// bruit filtré
	Svf f;
	int filt;			// 0 passe-bas, 1 passe-bande, 2 passe-haut
	float nf, nfEnd;		// fréquence du filtre (du début à la fin)
	float nDecay;			// constante de temps du bruit (s)
	float grain;			// 0 à 1 : craquements (le bruit haché)
	float gate = 1;
	// sons purs
	float tf[3], tfEnd[3], tDecay[3], tAmp[3];
	float tph[3];
	int tones;
	float brown = 0;		// (le tonnerre)
	float delay = 0;		// (s : commence plus tard)
};

// ---- l'état partagé -----------------------------------------------------------------------------------------------

struct Req {
	int sfx, mat;
	float vol, pan;
};

pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
Mood g_mood;
std::vector<Req> g_reqs;
bool g_musicOn = true, g_sfxOn = true;
float g_musicVol = 0.7f, g_sfxVol = 0.8f;
std::atomic<bool> g_run { false }, g_stop { false };
pthread_t g_thread;
long g_out = -1;

// ---- le moteur (le thread du son seulement) -----------------------------------------------------------------------

struct Engine {
	Rng rng;
	Mood m;			// la situation, lissée
	Mood want;
	bool musicOn = true, sfxOn = true;
	float musicVol = 0.7f, sfxVol = 0.8f;
	float musicFade = 0;	// (couper la musique en douceur)

	// la musique
	int root = 50;		// la tonalité (MIDI)
	const int *scale = nullptr;
	int scaleN = 7;
	float bpm = 66;
	double nextEighth = 0;	// (en trames)
	double now = 0;
	int eighth = 0;		// croches depuis le début de la section
	int section = 0;	// 0 plein, 1 clairsemé, 2 silence
	int sectionLeft = 0;	// croches qui restent
	int chord = 0;		// le degré de l'accord en cours
	int melIdx = 14;	// la note de la mélodie (indice dans la gamme étendue)
	Pad pads[4];
	Bell bells[10];
	float bassFreq = 55, bassPh = 0, bassAmp = 0, bassTarget = 0;
	float brightness = 0.6f;
	// l'écho
	std::vector<float> dl, dr;
	int dpos = 0, dlenL, dlenR;
	float dlp = 0, drp = 0;
	// l'ambiance
	float rainHp[2] = { 0, 0 }, rainLp[2] = { 0, 0 }, windLp = 0, windLp2 = 0, windPh = 0;
	Svf dropF;
	double nextBird = 0, nextCricket = 0, nextDrip = 0;
	struct Chirp {
		float t, dur, f0, f1, amp, ph, pan;
		bool on = false;
	} chirps[8];
	float cricketEnv = 0, cricketTarget = 0, cricketPh = 0;
	int cricketPulses = 0;
	double cricketNext = 0;
	// les effets
	Voice voices[24];
	float uwLp[2] = { 0, 0 };	// (sous l'eau : tout étouffé)
	float uw = 0;

	Engine ()
	{
		dlenL = (int) (SR * 0.43f), dlenR = (int) (SR * 0.57f);
		dl.assign ((size_t) dlenL, 0), dr.assign ((size_t) dlenR, 0);
		dropF.set (2500, 0.3f);
	}

	// ---- les gammes ----
	const int *pickScale (int &n)
	{
		static const int IONIAN[7] = { 0, 2, 4, 5, 7, 9, 11 }, LYDIAN[7] = { 0, 2, 4, 6, 7, 9, 11 }, DORIAN[7] = { 0, 2, 3, 5, 7, 9, 10 },
				 AEOLIAN[7] = { 0, 2, 3, 5, 7, 8, 10 }, PHRYGIAN[7] = { 0, 1, 3, 5, 7, 8, 10 }, HIJAZ[7] = { 0, 1, 4, 5, 7, 8, 10 };
		n = 7;
		if (m.underwater) return LYDIAN;
		if (m.cave > 0.5f) return m.daylight > 0.5f ? AEOLIAN : PHRYGIAN;
		if (m.storm > 0.4f) return PHRYGIAN;
		if (m.biome == BI_DESERT && m.daylight > 0.3f) return HIJAZ;
		if (m.biome == BI_SNOW) return LYDIAN;
		if (m.daylight < 0.3f) return DORIAN;
		if (m.rain > 0.4f) return AEOLIAN;
		return IONIAN;
	}
	float scaleNote (int idx) const	// (un indice dans la gamme, sur plusieurs octaves)
	{
		int o = idx >= 0 ? idx / scaleN : -((-idx + scaleN - 1) / scaleN);
		int d = idx - o * scaleN;
		return (float) (root + 12 * o + scale[d]);
	}

	// ---- une croche de la musique : les accords, la mélodie, la basse ----
	void stepMusic ()
	{
		if (sectionLeft <= 0) {
			// la section suivante : pleine, clairsemée ou un silence (plus de silences dans les grottes et la nuit)
			float quiet = 0.15f + 0.3f * m.cave + 0.15f * (1 - m.daylight);
			float r = rng.uni ();
			int prev = section;
			section = r < quiet * 0.6f && prev != 2 ? 2 : r < quiet + 0.25f ? 1 : 0;
			sectionLeft = section == 2 ? 8 * rng.range (4, 9) : 8 * (8 + 4 * rng.pick (3));
			eighth = 0;
		}
		sectionLeft--;
		bool barStart = eighth % 8 == 0, chordStart = eighth % 16 == 0;
		if (chordStart) {
			// la gamme suit la situation (aux changements d'accord), puis l'accord suivant
			int n;
			const int *s = pickScale (n);
			scale = s, scaleN = n;
			static const int NEXT[7][4] = { { 3, 4, 5, 1 }, { 4, 6, 2, 0 }, { 5, 3, 0, 1 }, { 0, 4, 1, 5 }, { 0, 5, 3, 1 }, { 3, 1, 4, 0 }, { 0, 2, 4, 3 } };
			chord = NEXT[chord % 7][rng.pick (4)];
			float lvl = section == 2 ? 0 : section == 1 ? 0.10f : 0.16f;
			lvl *= 1 - 0.4f * m.cave;
			// les notes de l'accord (1, 3, 5, parfois 7), chacune vers la plus proche de la voix d'avant
			int tones[4] = { chord, chord + 2, chord + 4, chord + (rng.uni () < 0.4f ? 6 : 7) };
			for (int v = 0; v < 4; v++) {
				float best = 0, bd = 1e9f;
				for (int o = -2; o <= 2; o++) {
					float n2 = scaleNote (tones[v]) + 12 * o;
					if (n2 < 50 || n2 > 72) continue;
					float d = fabsf (n2 - pads[v].note);
					if (d < bd) bd = d, best = n2;
				}
				if (best > 0) pads[v].setNote (best);
				pads[v].target = v == 3 && rng.uni () < 0.5f ? lvl * 0.6f : lvl;
				pads[v].pan = (v - 1.5f) * 0.4f;
			}
			bassFreq = mtof (scaleNote (chord) - 24 + (scaleNote (chord) - 24 < 33 ? 12 : 0));
			bassTarget = section == 2 ? 0 : (0.10f + 0.12f * m.storm + 0.08f * m.cave) * (section == 1 ? 0.6f : 1);
		}
		// la mélodie : quelques notes, plus quand le joueur s'active, presque rien dans les grottes
		float p = section == 0 ? 0.22f : section == 1 ? 0.08f : 0.0f;
		p *= 0.6f + 0.6f * m.activity;
		p *= 1 - 0.6f * m.cave;
		if (m.daylight < 0.3f) p *= 0.7f;
		if (barStart) p *= 1.6f;
		if (rng.uni () < p) {
			static const int STEP[8] = { -2, -1, -1, 1, 1, 2, 0, 3 };
			melIdx += STEP[rng.pick (8)];
			int lo = 2 * scaleN, hi = 4 * scaleN;
			if (melIdx < lo) melIdx = lo + 1;
			if (melIdx > hi) melIdx = hi - 1;
			// au temps fort, une note de l'accord
			if (barStart) {
				int c = chord + 2 * rng.pick (3);
				int base = melIdx - ((melIdx - c) % scaleN + scaleN) % scaleN;
				melIdx = base;
			}
			float n = scaleNote (melIdx);
			for (auto &b : bells)
				if (!b.on) {
					b.on = true;
					b.fc = mtof (n);
					b.pc = b.pm = 0;
					float v = rng.range (0.05f, 0.09f);
					if (m.cave > 0.5f) b.fc *= 0.5f, b.ratio = 4.0f, b.index = 1.2f, b.idxDecay = 0.9997f, b.ampDecay = 0.99984f;	// (marimba sourde)
					else if (m.biome == BI_SNOW || m.daylight < 0.3f) b.ratio = 3.5f, b.index = 2.2f, b.idxDecay = 0.99985f, b.ampDecay = 0.99993f;	// (cloche)
					else b.ratio = 1.0f, b.index = 1.6f, b.idxDecay = 0.9996f, b.ampDecay = 0.99978f;	// (corde pincée)
					b.amp = v;
					b.pan = rng.bi () * 0.6f;
					break;
				}
		}
		eighth++;
	}

	// ---- l'ambiance : oiseaux, grillons, gouttes ----
	void stepAmbience ()
	{
		bool surface = m.cave < 0.3f && !m.underwater;
		if (now >= nextBird) {
			nextBird = now + SR * rng.range (2.5f, 10.0f);
			float p = surface ? m.daylight * (1 - m.rain) * (m.biome == BI_DESERT ? 0.3f : 1.0f) : 0;
			if (rng.uni () < p) {	// un oiseau : deux à cinq glissés aigus
				int n = 2 + rng.pick (4);
				float base = rng.range (2600, 4200), pan = rng.bi () * 0.8f;
				float t0 = 0;
				for (int i = 0; i < n; i++)
					for (auto &c : chirps)
						if (!c.on) {
							c.on = true;
							c.t = -t0;
							c.dur = rng.range (0.05f, 0.12f);
							c.f0 = base * rng.range (0.85f, 1.15f), c.f1 = c.f0 * rng.range (0.7f, 1.4f);
							c.amp = 0.025f, c.ph = 0, c.pan = pan;
							t0 += c.dur + rng.range (0.02f, 0.09f);
							break;
						}
			}
		}
		if (now >= nextDrip) {
			nextDrip = now + SR * rng.range (1.5f, 6.0f);
			if (m.cave > 0.4f && rng.uni () < m.cave) spawnTone (rng.range (1200, 2600), 0.45f, 0.08f, 0.6f, rng.bi () * 0.7f, true);
		}
	}

	void spawnTone (float f, float f1Ratio, float decay, float vol, float pan, bool toEcho)
	{
		(void) toEcho;
		Voice *v = freeVoice ();
		if (!v) return;
		*v = Voice ();
		v->on = true, v->kind = 99, v->t = 0, v->dur = decay * 6;
		v->vol = vol * 0.12f;
		setPan (*v, pan);
		v->nDecay = 0;
		v->tones = 1;
		v->tf[0] = f, v->tfEnd[0] = f * f1Ratio, v->tDecay[0] = decay, v->tAmp[0] = 1, v->tph[0] = 0;
	}

	Voice *freeVoice ()
	{
		for (auto &v : voices)
			if (!v.on) return &v;
		Voice *old = &voices[0];	// (la plus ancienne)
		for (auto &v : voices)
			if (v.t / v.dur > old->t / old->dur) old = &v;
		return old;
	}

	static void setPan (Voice &v, float pan)
	{
		pan = std::max (-1.0f, std::min (1.0f, pan));
		v.panL = sqrtf (0.5f * (1 - pan)), v.panR = sqrtf (0.5f * (1 + pan));
	}

	// ---- un effet demandé ----
	void trigger (const Req &q)
	{
		struct MatSnd {
			float nf, decay, grain;
			int filt;
			float tone, toneDecay;
		};
		static const MatSnd MAT[9] = {
			{ 2200, 0.16f, 0.6f, 1, 0, 0 },		// pierre : clair, craquant
			{ 700, 0.14f, 0.2f, 1, 160, 0.12f },	// bois : sourd, un coup
			{ 900, 0.12f, 0.35f, 0, 0, 0 },		// terre
			{ 3200, 0.18f, 0.85f, 2, 0, 0 },	// sable, gravier : granuleux
			{ 4800, 0.22f, 0.3f, 2, 3600, 0.35f },	// verre : brisé, tintant
			{ 3600, 0.12f, 0.55f, 1, 0, 0 },	// feuilles : bruissement
			{ 500, 0.10f, 0.1f, 0, 0, 0 },		// laine : feutré
			{ 1800, 0.12f, 0.6f, 0, 0, 0 },		// neige
			{ 3000, 0.20f, 0.3f, 1, 1800, 0.4f },	// métal : sonne
		};
		const MatSnd &ms = MAT[std::max (0, std::min (8, q.mat))];
		Voice *v = freeVoice ();
		if (!v) return;
		*v = Voice ();
		v->on = true, v->kind = q.sfx, v->t = 0;
		v->vol = q.vol;
		setPan (*v, q.pan);
		v->nf = v->nfEnd = ms.nf * rng.range (0.85f, 1.15f);
		v->filt = ms.filt, v->grain = ms.grain, v->nDecay = ms.decay;
		v->tones = 0;
		auto tone = [&] (float f, float fe, float dec, float a) {
			int i = v->tones++;
			v->tf[i] = f, v->tfEnd[i] = fe, v->tDecay[i] = dec, v->tAmp[i] = a, v->tph[i] = 0;
		};
		switch (q.sfx) {
		case S_BREAK:
			v->vol *= 0.5f;
			if (ms.tone > 0) tone (ms.tone * rng.range (0.9f, 1.1f), ms.tone * 0.8f, ms.toneDecay, 0.5f);
			if (q.mat == M_GLASS) tone (rng.range (2800, 5200), rng.range (2800, 5200), 0.3f, 0.35f), tone (rng.range (3000, 6000), rng.range (3000, 6000), 0.25f, 0.3f);
			break;
		case S_HIT:
			v->vol *= 0.28f, v->nDecay *= 0.35f, v->nf *= 1.2f;
			if (ms.tone > 0) tone (ms.tone, ms.tone, ms.toneDecay * 0.3f, 0.4f);
			break;
		case S_PLACE:
			v->vol *= 0.42f, v->nDecay = 0.05f, v->nf *= 0.6f, v->grain = 0;
			tone (rng.range (110, 150), 80, 0.07f, 0.9f);
			break;
		case S_STEP:
			v->vol *= 0.16f, v->nDecay = 0.05f, v->nf *= rng.range (0.7f, 1.0f);
			break;
		case S_JUMP:
			v->vol *= 0.08f, v->nDecay = 0.04f;
			break;
		case S_LAND:
			v->vol *= 0.4f, v->nDecay = 0.08f, v->nf = 600, v->filt = 0, v->grain = 0.2f;
			tone (75, 50, 0.12f, 0.9f);
			break;
		case S_SPLASH:
			v->vol *= 0.4f, v->filt = 0, v->nf = 4000, v->nfEnd = 400, v->nDecay = 0.45f, v->grain = 0.3f;
			for (int i = 0; i < 3; i++) tone (rng.range (300, 600), rng.range (900, 1500), rng.range (0.05f, 0.12f), 0.3f);
			break;
		case S_CRAFT:
			v->vol *= 0.3f, v->nDecay = 0;
			tone (1318.5f, 1318.5f, 0.35f, 0.6f), tone (1975.5f, 1975.5f, 0.45f, 0.5f), tone (2637, 2637, 0.3f, 0.25f);
			break;
		case S_PICKUP:
			v->vol *= 0.22f, v->nDecay = 0;
			tone (rng.range (650, 750), 1400, 0.07f, 1);
			break;
		case S_TOOLBREAK:
			v->vol *= 0.45f, v->nf = 3000, v->filt = 2, v->nDecay = 0.07f, v->grain = 0.5f;
			tone (900, 300, 0.18f, 0.6f);
			break;
		case S_CLICK:
			v->vol *= 0.12f, v->nDecay = 0.006f, v->nf = 2500, v->filt = 1, v->grain = 0;
			break;
		case S_THUNDER:
			v->vol *= 0.9f, v->nf = 260, v->nfEnd = 160, v->filt = 0, v->nDecay = 1.6f, v->grain = 0;
			v->delay = 0;
			break;
		}
		v->dur = std::max (v->nDecay * 6.0f, 0.02f);
		for (int i = 0; i < v->tones; i++) v->dur = std::max (v->dur, v->tDecay[i] * 6.0f);
		v->f.set (v->nf, v->filt == 1 ? 0.6f : 0.9f);
	}

	// un échantillon d'un effet
	float voiceTick (Voice &v)
	{
		float dt = 1.0f / SR;
		if (v.delay > 0) {
			v.delay -= dt;
			return 0;
		}
		v.t += dt;
		if (v.t >= v.dur) {
			v.on = false;
			return 0;
		}
		float s = 0;
		if (v.nDecay > 0) {
			float n;
			if (v.kind == S_THUNDER) {	// (bruit brun, des grondements)
				v.brown = v.brown * 0.995f + rng.bi () * 0.06f;
				n = v.brown * 3.0f * (0.6f + 0.4f * sinf (v.t * 7.0f + sinf (v.t * 2.3f) * 3));
				float env = (v.t < 0.06f ? v.t / 0.06f : expf (-(v.t - 0.06f) / v.nDecay));
				n *= env;
			} else {
				n = rng.bi () * expf (-v.t / v.nDecay);
				if (v.grain > 0) {	// des craquements : le bruit coupé par moments
					if (rng.uni () < 0.004f) v.gate = rng.uni () < v.grain ? rng.range (0.0f, 0.3f) : 1.0f;
					n *= v.gate;
				}
			}
			if (v.nfEnd != v.nf) {
				float k = std::min (1.0f, v.t / std::max (0.001f, v.nDecay * 2));
				v.f.set (v.nf + (v.nfEnd - v.nf) * k, v.filt == 1 ? 0.6f : 0.9f);
			}
			float lo, bp, hp;
			v.f.run (n, lo, bp, hp);
			s += v.filt == 0 ? lo : v.filt == 1 ? bp * 1.5f : hp;
		}
		for (int i = 0; i < v.tones; i++) {
			float k = std::min (1.0f, v.t / std::max (0.001f, v.tDecay[i] * 2));
			float f = v.tf[i] + (v.tfEnd[i] - v.tf[i]) * k;
			v.tph[i] += f / SR;
			if (v.tph[i] >= 1) v.tph[i] -= 1;
			s += sinf (v.tph[i] * TAU) * v.tAmp[i] * expf (-v.t / v.tDecay[i]);
		}
		return s * v.vol;
	}

	// ---- un bloc de trames ----
	void render (int16_t *out, int frames)
	{
		// la situation, lissée (une fois par bloc)
		float k = 0.02f;
		m.daylight += (want.daylight - m.daylight) * k;
		m.rain += (want.rain - m.rain) * k;
		m.storm += (want.storm - m.storm) * k;
		m.cave += (want.cave - m.cave) * k;
		m.altitude += (want.altitude - m.altitude) * k;
		m.activity += (want.activity - m.activity) * 0.05f;
		m.underwater = want.underwater, m.biome = want.biome, m.snowing = want.snowing;
		if (m.seed != want.seed) {
			m.seed = want.seed;
			root = 45 + (int) (m.seed % 10);	// (la tonalité du monde : de la à fa dièse)
		}
		if (!scale) scale = pickScale (scaleN);
		brightness = 0.35f + 0.5f * m.daylight - 0.25f * m.cave - 0.2f * m.storm;
		uw += ((m.underwater ? 1.0f : 0.0f) - uw) * 0.05f;
		float musicTarget = musicOn ? musicVol : 0;
		bpm = 64 - 8 * m.cave + 4 * m.activity;
		double spe = SR * 60.0 / bpm / 2.0;	// trames par croche
		float uwA = lpCoef (500 + 9000 * (1 - uw));
		float rainA = lpCoef (1400), rainB = lpCoef (6000), windA = lpCoef (350 + 300 * m.storm);
		for (int i = 0; i < frames; i++) {
			if (now >= nextEighth) {
				nextEighth = now + spe;
				stepMusic ();
			}
			if (((int) now & 255) == 0) stepAmbience ();
			musicFade += (musicTarget - musicFade) * 0.00005f;
			// la musique
			float ml = 0, mr = 0, el = 0, er = 0;
			for (auto &p : pads) p.tick (brightness, ml, mr);
			for (auto &b : bells) {
				float bl = 0, br = 0;
				b.tick (bl, br);
				ml += bl, mr += br;
				el += bl * 0.8f, er += br * 0.8f;	// (les cloches vont à l'écho)
			}
			bassAmp += (bassTarget - bassAmp) * 0.00003f;
			bassPh += bassFreq / SR;
			if (bassPh >= 1) bassPh -= 1;
			float bass = tanhf (sinf (bassPh * TAU) * 1.4f) * bassAmp;
			ml += bass, mr += bass;
			el += ml * 0.15f, er += mr * 0.15f;
			// l'écho stéréo (croisé), assourdi à chaque passage
			float dL = dl[(size_t) dpos % dlenL], dR = dr[(size_t) dpos % dlenR];
			dlp += (dL - dlp) * 0.35f, drp += (dR - drp) * 0.35f;
			dl[(size_t) dpos % dlenL] = el + drp * 0.42f;
			dr[(size_t) dpos % dlenR] = er + dlp * 0.42f;
			dpos++;
			ml += dlp * 0.55f, mr += drp * 0.55f;
			float L = ml * musicFade, R = mr * musicFade;
			// l'ambiance (avec les effets)
			float al = 0, ar = 0;
			if (m.rain > 0.01f && !m.snowing) {	// la pluie : un souffle adouci (entre 1,4 et 6 kHz, un bruit par canal) et des gouttes
				float lvl = 0.06f * m.rain * (1 - 0.7f * m.cave);
				for (int c = 0; c < 2; c++) {
					float n = rng.bi ();
					rainHp[c] += (n - rainHp[c]) * rainA;
					rainLp[c] += ((n - rainHp[c]) - rainLp[c]) * rainB;
				}
				if (rng.uni () < 0.0008f * m.rain) dropF.band += rng.range (0.3f, 0.9f);
				float lo, bp, hp;
				dropF.run (0, lo, bp, hp);
				al += rainLp[0] * lvl + bp * 0.05f, ar += rainLp[1] * lvl + bp * 0.03f;
			}
			float windAmt = (0.02f + 0.08f * m.altitude + 0.12f * m.storm + (m.snowing ? 0.06f : 0)) * (1 - m.cave);
			if (windAmt > 0.001f) {
				windPh += 0.07f / SR;
				if (windPh > 1) windPh -= 1;
				float n = rng.bi ();
				windLp += (n - windLp) * windA, windLp2 += (windLp - windLp2) * windA;
				float w = windLp2 * 4.0f * windAmt * (0.6f + 0.4f * sinf (windPh * TAU) * sinf (windPh * TAU * 3.1f));
				al += w, ar += w * 0.9f;
			}
			for (auto &c : chirps)
				if (c.on) {
					c.t += 1 / SR;
					if (c.t < 0) continue;
					if (c.t >= c.dur) {
						c.on = false;
						continue;
					}
					float x = c.t / c.dur;
					c.ph += (c.f0 + (c.f1 - c.f0) * x) / SR;
					float o = sinf (c.ph * TAU) * c.amp * sinf (x * 3.14159f);
					al += o * (0.5f - c.pan * 0.4f), ar += o * (0.5f + c.pan * 0.4f);
				}
			// les grillons, la nuit, en surface
			float crk = (1 - m.daylight) * (1 - m.rain) * (m.cave < 0.3f && !m.underwater ? 1 : 0) * (m.biome == BI_SNOW ? 0.2f : 1);
			if (crk > 0.05f) {
				if (now >= cricketNext) {
					if (cricketPulses <= 0) cricketPulses = 3 + rng.pick (2), cricketNext = now + SR * rng.range (0.5f, 1.4f);
					else cricketPulses--, cricketTarget = 1, cricketNext = now + SR * 0.035f;
				}
				// (l'enveloppe monte en 2 ms puis redescend : pas de clic) ; un son pur vers 4,6 kHz, un peu trillé
				cricketEnv += (cricketTarget - cricketEnv) * 0.012f;
				if (cricketEnv > 0.95f * cricketTarget) cricketTarget *= 0.9985f;
				cricketPh += 4600.0f / SR;
				if (cricketPh >= 1) cricketPh -= 1;
				float o = sinf (cricketPh * TAU) * (0.7f + 0.3f * sinf ((float) now * TAU * 90.0f / SR)) * cricketEnv * 0.02f * crk;
				al += o, ar += o * 0.7f;
			}
			// les effets
			float sl = 0, sr = 0;
			for (auto &v : voices)
				if (v.on) {
					float s = voiceTick (v);
					sl += s * v.panL, sr += s * v.panR;
				}
			float fx = sfxOn ? sfxVol : 0;
			L += (al + sl) * fx, R += (ar + sr) * fx;
			// sous l'eau : tout étouffé
			uwLp[0] += (L - uwLp[0]) * uwA, uwLp[1] += (R - uwLp[1]) * uwA;
			L = uwLp[0], R = uwLp[1];
			out[i * 2] = (int16_t) (tanhf (L * 1.3f) * 30000.0f);
			out[i * 2 + 1] = (int16_t) (tanhf (R * 1.3f) * 30000.0f);
			now += 1;
		}
	}
};

void *loop (void *)
{
	Engine *e = new Engine ();
	e->rng.s ^= (uint32_t) (size_t) e;
	static int16_t buf[BLOCK * 2];
	std::vector<Req> reqs;
	while (!g_stop) {
		pthread_mutex_lock (&g_mu);
		e->want = g_mood;
		e->musicOn = g_musicOn, e->sfxOn = g_sfxOn, e->musicVol = g_musicVol, e->sfxVol = g_sfxVol;
		reqs.swap (g_reqs);
		pthread_mutex_unlock (&g_mu);
		for (auto &q : reqs)
			if (e->sfxOn) e->trigger (q);
		reqs.clear ();
		e->render (buf, BLOCK);
		if (ak_write_all (g_out, (long) buf, sizeof buf) < 0) break;
	}
	delete e;
	return nullptr;
}

} // namespace

bool start ()
{
	if (g_run) return true;
	if (!audiokit_kit_open ()) return false;
	g_out = ak_open (44100, 2, 16, 0);
	if (g_out < 0) return false;
	ak_latency (g_out, 8192);	// (≈ 46 ms d'avance : les effets suivent le jeu de près)
	g_stop = false;
	if (pthread_create (&g_thread, nullptr, loop, nullptr) != 0) {
		ak_close (g_out);
		g_out = -1;
		return false;
	}
	g_run = true;
	return true;
}

void stop ()
{
	if (!g_run) return;
	g_stop = true;
	pthread_join (g_thread, nullptr);
	ak_close (g_out);
	g_out = -1;
	g_run = false;
}

bool running () { return g_run; }

void setMood (const Mood &m)
{
	pthread_mutex_lock (&g_mu);
	g_mood = m;
	pthread_mutex_unlock (&g_mu);
}

void setMusic (bool on, float volume)
{
	pthread_mutex_lock (&g_mu);
	g_musicOn = on, g_musicVol = std::max (0.0f, std::min (1.0f, volume));
	pthread_mutex_unlock (&g_mu);
}

void setSfx (bool on, float volume)
{
	pthread_mutex_lock (&g_mu);
	g_sfxOn = on, g_sfxVol = std::max (0.0f, std::min (1.0f, volume));
	pthread_mutex_unlock (&g_mu);
}

void play (Sfx s, int material, float volume, float pan)
{
	if (!g_run) return;
	pthread_mutex_lock (&g_mu);
	if (g_reqs.size () < 64) g_reqs.push_back ({ (int) s, material, volume, pan });
	pthread_mutex_unlock (&g_mu);
}

int materialOf (int b)
{
	switch (b) {
	case PLANKS: case LOG: case PINE_LOG: case BOOKSHELF: case WORKBENCH: return M_WOOD;
	case GRASS: case DIRT: case CLAY: return M_EARTH;
	case SAND: case GRAVEL: case SANDSTONE: return M_SAND;
	case GLASS: case ICE: case LAMP: case CRYSTAL_ORE: return M_GLASS;
	case LEAVES: case PINE_LEAVES: case TALL_GRASS: case FLOWER_RED: case FLOWER_YELLOW: case CACTUS: return M_LEAVES;
	case SNOW: return M_SNOW;
	case GOLD_BLOCK: case IRON_ORE: case GOLD_ORE: return M_METAL;
	case WOOL_WHITE: case WOOL_RED: case WOOL_ORANGE: case WOOL_YELLOW: case WOOL_GREEN: case WOOL_BLUE: case WOOL_PURPLE: case WOOL_BLACK:
		return M_SOFT;
	default: return M_STONE;
	}
}

} // namespace audio
} // namespace tc
