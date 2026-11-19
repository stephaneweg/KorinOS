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
		amp += (target - amp) * (target > amp ? 0.00002f : 0.000015f);	// (≈ 1 s pour monter, plus pour descendre)
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
		float a = lpCoef (220 + 650 * bright);	// (un fond sourd : la mélodie passe devant)
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

// une note du piano électrique (la mélodie et ses accords) : une modulation de fréquence douce qui donne le « tine » de
// l'attaque puis s'adoucit, une tenue qui s'éteint lentement (plus longue dans le grave), un relâchement quand la
// touche est levée ; wait retarde son départ (le balancement des contretemps, un peu d'humain)
struct EPNote {
	bool on = false;
	float f = 440, vel = 0, t = 0, hold = 1, wait = 0;
	float bark = 1;		// la force du tine (0 à 1)
	float pc = 0, pm = 0, ph2 = 0;
	float pan = 0;
	float tick (float &l, float &r)
	{
		if (!on) return 0;
		if (wait > 0) {
			wait -= 1 / SR;
			return 0;
		}
		t += 1 / SR;
		float decayT = 1.0f + 2.2f * std::max (0.0f, 1.0f - f / 900.0f);
		float env = vel * std::min (1.0f, t / 0.005f) * expf (-t / decayT);
		if (t > hold) env *= expf (-(t - hold) / 0.16f);
		if (t > 0.05f && env < 0.00015f) {
			on = false;
			return 0;
		}
		pc += f / SR;
		if (pc >= 1) pc -= 1;
		pm += f / SR;
		if (pm >= 1) pm -= 1;
		ph2 += f * 3.0f / SR;
		if (ph2 >= 1) ph2 -= 1;
		float idx = 0.3f + 1.5f * bark * expf (-t / 0.09f);
		float o = sinf (pc * TAU + idx * sinf (pm * TAU)) + 0.10f * bark * sinf (ph2 * TAU) * expf (-t / 0.25f);
		o *= env;
		l += o * (0.5f - pan * 0.4f), r += o * (0.5f + pan * 0.4f);
		return o;
	}
};

// un coup de la batterie feutrée : grosse caisse (un sinus qui tombe), balai (bruit adouci), charleston (bruit aigu)
struct Drum {
	bool on = false;
	int kind = 0;		// 0 grosse caisse, 1 balai, 2 charleston
	float t = 0, wait = 0, vol = 0, ph = 0, lp = 0, lp2 = 0;
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
	int arg = 0;
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
	Pad pads[4];
	Bell bells[10];
	// la mélodie : des phrases de deux mesures (16 croches) sur une gamme pentatonique, en périodes de quatre phrases
	// A A' B A'' : un motif (un rythme, un contour), sa reprise variée, une autre idée, le motif qui se pose
	EPNote keys[20];
	Drum drums[8];
	int phrase = 0;		// la phrase dans la période (0 à 3)
	int motifR = 0, otherR = 1;	// les rythmes du motif et de la phrase B
	int motifC[8], otherC[8];	// leurs contours (pas pentatoniques)
	int phraseR = 0, phraseC[8];	// la phrase en cours
	int phraseNote = 0, melPent = 12;	// la note suivante de la phrase, la note jouée (indice pentatonique)
	bool phraseRest = false;	// (une phrase muette)
	int barChord[2] = { 0, 3 };	// les accords des deux mesures de la phrase
	bool beat = false;		// la batterie feutrée, dans cette section
	float drumGain = 0, epLp[2] = { 0, 0 }, tremPh = 0;
	float eighthSec = 0.4f;
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
	Bell noteBells[12];	// (les blocs musicaux : avec les effets)
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

	// la pentatonique de la gamme : majeure (1 2 3 5 6) si la tierce est majeure, sinon mineure (1 b3 4 5 b7)
	float pentNote (int idx) const
	{
		static const int MAJ[5] = { 0, 1, 2, 4, 5 }, MIN[5] = { 0, 2, 3, 4, 6 };
		const int *P = scale[2] == 3 ? MIN : MAJ;
		int o = idx >= 0 ? idx / 5 : -((-idx + 4) / 5);
		int d = idx - o * 5;
		return (float) (root + 12 * o + scale[P[d]]);
	}
	bool chordTone (float note, int chordDeg) const
	{
		int pc = ((int) note - root) % 12;
		if (pc < 0) pc += 12;
		for (int k = 0; k < 6; k += 2) {
			int d = (chordDeg + k) % scaleN;
			if (scale[d] == pc) return true;
		}
		return false;
	}
	// le centre de la mélodie (MIDI) : plus grave dans les grottes et la nuit
	float melCenter () const { return 71.0f - 7.0f * m.cave - 2.0f * (1 - m.daylight); }

	void newContour (int *c, int notes)
	{
		static const int STEP[10] = { -2, -1, -1, -1, 1, 1, 1, 2, 0, 3 };
		int pos = 0;
		for (int i = 0; i < 8; i++) {
			if (i == 0) {
				c[i] = 0;
				continue;
			}
			int st = STEP[rng.pick (10)];
			if (pos + st > 4) st = -1 - rng.pick (2);	// (revenir vers le milieu)
			if (pos + st < -4) st = 1 + rng.pick (2);
			if (i == notes - 1 && rng.uni () < 0.5f) st = -pos > 0 ? 1 : pos > 0 ? -1 : 0;	// (finir près du départ)
			pos += st;
			c[i] = st;
		}
	}
	static int countNotes (int r);
	static const char *rhythm (int r)
	{
		// une lettre par croche : un chiffre, une note de tant de croches ; « . », rien
		static const char *const R[8] = { "2.1.2.3..1.4....", "3..2.1..4.......", "1.1.3...2.2.6...", "..2.1.3...1.4...",
						  "4...2.2.3..1....", "2.2.1.1.2...6...", "6.....1.3...2...", "..1.1.2.4...2.1." };
		return R[r & 7];
	}

	void playKey (float note, float vel, float holdSec, float wait, float pan, float bark)
	{
		EPNote *k = nullptr;
		for (auto &e : keys)
			if (!e.on) {
				k = &e;
				break;
			}
		if (!k) {	// (la plus ancienne)
			k = &keys[0];
			for (auto &e : keys)
				if (e.t > k->t) k = &e;
		}
		*k = EPNote ();
		k->on = true, k->f = mtof (note), k->vel = vel, k->hold = holdSec, k->wait = wait, k->pan = pan, k->bark = bark;
	}

	void hitDrum (int kind, float vol, float wait)
	{
		for (auto &d : drums)
			if (!d.on) {
				d = Drum ();
				d.on = true, d.kind = kind, d.vol = vol, d.wait = wait;
				return;
			}
	}

	// ---- une croche de la musique : les accords, la mélodie, la basse, la batterie ----
	void stepMusic ()
	{
		if (sectionLeft <= 0) {
			// la section suivante : pleine, clairsemée ou un silence (plus de silences dans les grottes et la nuit)
			float quiet = 0.12f + 0.3f * m.cave + 0.12f * (1 - m.daylight);
			float r = rng.uni ();
			int prev = section;
			section = r < quiet * 0.5f && prev != 2 ? 2 : r < quiet + 0.25f ? 1 : 0;
			sectionLeft = section == 2 ? 8 * (2 + rng.pick (3)) : 16 * (4 + 2 * rng.pick (3));
			eighth = 0;
			beat = section == 0 && rng.uni () < 0.6f;
		}
		sectionLeft--;
		int e16 = eighth % 16;
		bool barStart = eighth % 8 == 0, phraseStart = e16 == 0;
		float swing = (eighth & 1) ? eighthSec * 0.17f : 0.0f;	// (les contretemps un peu en retard)
		if (phraseStart) {
			// la gamme suit la situation (aux phrases), puis les accords des deux mesures
			int n;
			const int *s = pickScale (n);
			scale = s, scaleN = n;
			static const int NEXT[7][4] = { { 3, 5, 1, 3 }, { 4, 4, 6, 2 }, { 5, 3, 1, 5 }, { 4, 0, 1, 1 }, { 0, 5, 0, 3 }, { 1, 3, 4, 1 }, { 2, 4, 0, 3 } };
			barChord[0] = NEXT[chord % 7][rng.pick (4)];
			barChord[1] = NEXT[barChord[0]][rng.pick (4)];
			chord = barChord[1];
			// la phrase : A, A', B, A'' (puis une nouvelle période, parfois un nouveau motif)
			if (phrase == 0 && rng.uni () < 0.6f) {
				motifR = rng.pick (8);
				newContour (motifC, countNotes (motifR));
				otherR = (motifR + 1 + rng.pick (7)) & 7;
				newContour (otherC, countNotes (otherR));
			}
			phraseR = phrase == 2 ? otherR : motifR;
			memcpy (phraseC, phrase == 2 ? otherC : motifC, sizeof phraseC);
			int nn = countNotes (phraseR);
			if (phrase == 1 && nn > 2) phraseC[nn - 1] = -phraseC[nn - 1] + (rng.uni () < 0.5f ? 1 : -1);	// (une autre fin)
			phraseNote = 0;
			// la note de départ : une note de l'accord, près du centre
			float c = melCenter ();
			int best = melPent;
			float bd = 1e9f;
			for (int i = 0; i < 30; i++) {
				float nt = pentNote (i);
				if (!chordTone (nt, barChord[0])) continue;
				float d = fabsf (nt - c) + fabsf (nt - pentNote (melPent)) * 0.3f;
				if (d < bd) bd = d, best = i;
			}
			melPent = best;
			// clairsemé : une phrase sur deux se tait ; la phrase B parfois aussi (un temps pour respirer)
			phraseRest = section == 2 || (section == 1 && (phrase & 1)) || (phrase == 2 && rng.uni () < 0.25f);
			if (m.cave > 0.6f && rng.uni () < 0.4f) phraseRest = true;
			phrase = (phrase + 1) & 3;
		}
		int ch = barChord[e16 < 8 ? 0 : 1];
		if (barStart) {
			// les nappes, discrètes : la fondamentale, la quinte, l'octave de l'accord
			float lvl = section == 2 ? 0 : section == 1 ? 0.016f : 0.022f;
			lvl *= 1 - 0.3f * m.cave;
			int tones[4] = { ch, ch + 4, ch + 7, ch + 2 };
			for (int v = 0; v < 4; v++) {
				float best = 0, bd = 1e9f;
				for (int o = -2; o <= 2; o++) {
					float n2 = scaleNote (tones[v]) + 12 * o;
					if (n2 < 45 || n2 > 64) continue;
					float d = fabsf (n2 - pads[v].note);
					if (d < bd) bd = d, best = n2;
				}
				if (best > 0) pads[v].setNote (best);
				pads[v].target = v == 3 ? lvl * 0.5f : lvl;
				pads[v].pan = (v - 1.5f) * 0.4f;
			}
			bassFreq = mtof (scaleNote (ch) - 24 + (scaleNote (ch) - 24 < 33 ? 12 : 0));
			bassTarget = section == 2 ? 0 : (0.032f + 0.05f * m.storm + 0.03f * m.cave) * (section == 1 ? 0.6f : 1);
			// les accords du piano : sans fondamentale (tierce, quinte, septième, neuvième), égrenés, doux
			bool comp = section == 0 || (section == 1 && (eighth & 8) == 0);
			if (comp && m.cave < 0.8f) {
				int deg[4] = { ch + 2, ch + 4, ch + 6, ch + 8 };
				float prev = 54;
				for (int v = 0; v < 4; v++) {
					float n2 = scaleNote (deg[v]);
					while (n2 < prev) n2 += 12;
					while (n2 > prev + 9) n2 -= 12;
					if (n2 < 52) n2 += 12;
					prev = n2;
					playKey (n2, rng.range (0.030f, 0.040f), eighthSec * 5.5f, 0.012f * v + rng.uni () * 0.006f, (v - 1.5f) * 0.25f, 0.35f);
				}
			}
		}
		// une relance de l'accord, sur le contretemps du troisième temps (parfois)
		if (eighth % 8 == 5 && section == 0 && rng.uni () < 0.35f) {
			int deg[3] = { ch + 2, ch + 4, ch + 6 };
			for (int v = 0; v < 3; v++) {
				float n2 = scaleNote (deg[v]);
				while (n2 < 55) n2 += 12;
				while (n2 > 67) n2 -= 12;
				playKey (n2, 0.022f, eighthSec * 1.2f, swing + 0.01f * v, (v - 1) * 0.3f, 0.25f);
			}
		}
		// la mélodie : la note de la phrase à cette croche
		const char *rh = rhythm (phraseR);
		char cell = rh[e16];
		if (cell >= '1' && cell <= '9') {
			int dur = cell - '0';
			int i = phraseNote++;
			if (i > 0 && i < 8) melPent += phraseC[i];
			// rester dans le registre (une octave et demie autour du centre)
			float c = melCenter ();
			while (pentNote (melPent) > c + 9) melPent -= 2;
			while (pentNote (melPent) < c - 9) melPent += 2;
			float nt = pentNote (melPent);
			// au temps fort de chaque mesure, une note de l'accord ; la dernière phrase finit sur la tonique
			if ((e16 % 8) == 0 && !chordTone (nt, ch)) {
				if (chordTone (pentNote (melPent + 1), ch)) melPent++;
				else if (chordTone (pentNote (melPent - 1), ch)) melPent--;
				nt = pentNote (melPent);
			}
			if (phrase == 0 && phraseNote == countNotes (phraseR)) {	// (la phrase A'' vient de commencer sa dernière note)
				for (int d = 0; d < 3; d++) {
					if (((int) pentNote (melPent + d) - root) % 12 == 0) { melPent += d; break; }
					if (((int) pentNote (melPent - d) - root) % 12 == 0) { melPent -= d; break; }
				}
				nt = pentNote (melPent);
			}
			if (!phraseRest) {
				float vel = rng.range (0.13f, 0.17f) * (0.85f + 0.3f * m.activity) * ((e16 % 4) == 0 ? 1.1f : 0.92f);
				if (section == 1) vel *= 0.8f;
				float bark = 0.55f + 0.35f * m.daylight - 0.4f * m.cave;
				playKey (nt, vel, eighthSec * dur * 0.92f, swing + rng.uni () * 0.008f, rng.bi () * 0.25f, std::max (0.1f, bark));
			}
		}
		// des étincelles de cloche, la nuit et dans la neige
		if (section == 0 && (m.daylight < 0.3f || m.biome == BI_SNOW) && rng.uni () < 0.03f) {
			float n = scaleNote (ch + 2 * rng.pick (3)) + 24;
			while (n > 92) n -= 12;
			for (auto &b : bells)
				if (!b.on) {
					b.on = true, b.fc = mtof (n), b.pc = b.pm = 0;
					b.ratio = 3.5f, b.index = 1.6f, b.idxDecay = 0.99985f, b.ampDecay = 0.99993f;
					b.amp = rng.range (0.015f, 0.03f), b.pan = rng.bi () * 0.7f;
					break;
				}
		}
		// la batterie feutrée (de jour, au grand air, dans certaines sections pleines)
		bool drumsOk = beat && section == 0 && m.cave < 0.3f && m.storm < 0.3f && !m.underwater && m.daylight > 0.25f && m.rain < 0.6f;
		if (drumsOk) {
			int e8 = eighth % 8;
			if (e8 == 0 || (e8 == 5 && rng.uni () < 0.8f) || (e8 == 3 && rng.uni () < 0.2f)) hitDrum (0, e8 == 0 ? 0.42f : 0.30f, swing);
			if (e8 == 2 || e8 == 6) hitDrum (1, rng.range (0.10f, 0.13f), 0.006f);
			if (rng.uni () < 0.85f) hitDrum (2, (eighth & 1) ? 0.025f : 0.04f, swing + rng.uni () * 0.004f);
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
		if (m.lava > 0.05f && rng.uni () < 0.012f * m.lava) {	// la lave : des bulles graves qui éclatent
			float f = rng.range (90, 220);
			spawnTone (f, rng.range (1.6f, 2.6f), rng.range (0.03f, 0.07f), 0.9f * m.lava, rng.bi () * 0.6f, false);
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
		case S_CLICK_ON:	// (un levier, un bouton : un clic sec, un peu plus aigu quand il s'allume)
			v->vol *= 0.25f, v->nDecay = 0.008f, v->nf = 3200, v->filt = 1, v->grain = 0;
			tone (q.arg ? 1400 : 1000, q.arg ? 1500 : 900, 0.012f, 0.7f);
			break;
		case S_PISTON_OUT: case S_PISTON_IN:	// un souffle et un choc de bois
			v->vol *= 0.4f, v->filt = 1, v->nf = q.sfx == S_PISTON_OUT ? 900 : 1300, v->nfEnd = q.sfx == S_PISTON_OUT ? 1400 : 700, v->nDecay = 0.07f, v->grain = 0.15f;
			tone (q.sfx == S_PISTON_OUT ? 190 : 160, 120, 0.06f, 0.8f);
			break;
		case S_FIZZ:	// la lave qui fige : un sifflement
			v->vol *= 0.35f, v->filt = 2, v->nf = 3500, v->nfEnd = 6000, v->nDecay = 0.35f, v->grain = 0.5f;
			break;
		case S_FUSE:
			v->vol *= 0.3f, v->filt = 2, v->nf = 4500, v->nDecay = 0.5f, v->grain = 0.8f;
			break;
		case S_EXPLODE:
			v->vol *= 1.0f, v->nf = 900, v->nfEnd = 120, v->filt = 0, v->nDecay = 0.7f, v->grain = 0.2f;
			tone (70, 35, 0.4f, 1.0f);
			break;
		}
		v->dur = std::max (v->nDecay * 6.0f, 0.02f);
		for (int i = 0; i < v->tones; i++) v->dur = std::max (v->dur, v->tDecay[i] * 6.0f);
		v->f.set (v->nf, v->filt == 1 ? 0.6f : 0.9f);
	}

	// une note du bloc musical : une voix de piano électrique (ou de la batterie), sur le bus des effets
	void note (const Req &q)
	{
		float n = 54.0f + q.arg;	// (fa dièse 3, comme les blocs du genre)
		switch (q.mat) {
		case IN_DRUM: {
			Voice *v = freeVoice ();
			*v = Voice ();
			v->on = true, v->kind = S_NOTE, v->vol = q.vol * 0.7f, v->nDecay = 0.02f, v->nf = 400, v->filt = 0, v->tones = 1;
			v->tf[0] = mtof (n - 12), v->tfEnd[0] = mtof (n - 24), v->tDecay[0] = 0.12f, v->tAmp[0] = 1, v->tph[0] = 0;
			setPan (*v, q.pan);
			v->dur = 0.8f;
			v->f.set (v->nf, 0.9f);
			return;
		}
		case IN_SNARE: case IN_CLICK: {
			Voice *v = freeVoice ();
			*v = Voice ();
			v->on = true, v->kind = S_NOTE, v->vol = q.vol * (q.mat == IN_SNARE ? 0.35f : 0.25f), v->filt = q.mat == IN_SNARE ? 1 : 2;
			v->nf = mtof (n + 36) * (q.mat == IN_SNARE ? 0.5f : 1.0f), v->nDecay = q.mat == IN_SNARE ? 0.09f : 0.02f, v->tones = 0;
			setPan (*v, q.pan);
			v->dur = v->nDecay * 6;
			v->f.set (v->nf, v->filt == 1 ? 0.6f : 0.9f);
			return;
		}
		}
		for (auto &b : noteBells)
			if (!b.on) {
				b.on = true, b.pc = b.pm = 0, b.pan = q.pan;
				b.fc = mtof (q.mat == IN_BASS ? n - 24 : q.mat == IN_BELL || q.mat == IN_CHIME ? n + 24 : q.mat == IN_GUITAR ? n - 12 : n);
				switch (q.mat) {
				case IN_BELL: b.ratio = 3.5f, b.index = 2.0f, b.idxDecay = 0.99985f, b.ampDecay = 0.99993f; break;
				case IN_CHIME: b.ratio = 5.0f, b.index = 1.2f, b.idxDecay = 0.9999f, b.ampDecay = 0.99995f; break;
				case IN_BASS: b.ratio = 1.0f, b.index = 2.5f, b.idxDecay = 0.9995f, b.ampDecay = 0.99985f; break;
				case IN_GUITAR: b.ratio = 2.0f, b.index = 1.8f, b.idxDecay = 0.9996f, b.ampDecay = 0.9998f; break;
				default: b.ratio = 1.0f, b.index = 1.4f, b.idxDecay = 0.9996f, b.ampDecay = 0.99982f; break;	// (une harpe, pincée)
				}
				b.amp = 0.35f * q.vol;
				return;
			}
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
		m.lava += (want.lava - m.lava) * 0.05f;
		m.underwater = want.underwater, m.biome = want.biome, m.snowing = want.snowing;
		if (m.seed != want.seed) {
			m.seed = want.seed;
			root = 45 + (int) (m.seed % 10);	// (la tonalité du monde : de la à fa dièse)
		}
		if (!scale) scale = pickScale (scaleN);
		brightness = 0.35f + 0.5f * m.daylight - 0.25f * m.cave - 0.2f * m.storm;
		uw += ((m.underwater ? 1.0f : 0.0f) - uw) * 0.05f;
		float musicTarget = musicOn ? musicVol : 0;
		bpm = 74 - 8 * m.cave - 4 * (1 - m.daylight) + 3 * m.activity;
		double spe = SR * 60.0 / bpm / 2.0;	// trames par croche
		eighthSec = (float) (spe / SR);
		drumGain += ((beat && section == 0 && m.cave < 0.3f && m.storm < 0.3f && !m.underwater && m.daylight > 0.25f ? 1.0f : 0.0f) - drumGain) * 0.01f;
		float epA = lpCoef (3200 - 1800 * m.cave - 600 * (1 - m.daylight));
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
			// le piano électrique, adouci par un passe-bas, avec un trémolo lent qui va d'un côté à l'autre
			float kl = 0, kr = 0;
			for (auto &kk : keys) kk.tick (kl, kr);
			epLp[0] += (kl - epLp[0]) * epA, epLp[1] += (kr - epLp[1]) * epA;
			tremPh += 2.6f / SR;
			if (tremPh >= 1) tremPh -= 1;
			float tr = sinf (tremPh * TAU) * 0.10f;
			ml += epLp[0] * (1 + tr), mr += epLp[1] * (1 - tr);
			el += epLp[0] * 0.35f, er += epLp[1] * 0.35f;
			// la batterie
			for (auto &d : drums) {
				if (!d.on) continue;
				if (d.wait > 0) {
					d.wait -= 1 / SR;
					continue;
				}
				d.t += 1 / SR;
				float o = 0;
				if (d.kind == 0) {
					d.ph += (46.0f + 70.0f * expf (-d.t / 0.025f)) / SR;
					if (d.ph >= 1) d.ph -= 1;
					o = sinf (d.ph * TAU) * expf (-d.t / 0.22f) * std::min (1.0f, d.t / 0.002f);
					if (d.t > 1.2f) d.on = false;
				} else {
					float nz = rng.bi ();
					float lc = d.kind == 1 ? 0.12f : 0.6f;
					d.lp += (nz - d.lp) * lc;
					float hpn = nz - d.lp;	// (passe-haut)
					d.lp2 += (hpn - d.lp2) * (d.kind == 1 ? 0.35f : 0.5f);
					float dec = d.kind == 1 ? 0.10f : 0.022f;
					o = d.lp2 * expf (-d.t / dec) * std::min (1.0f, d.t / (d.kind == 1 ? 0.004f : 0.001f));
					if (d.t > dec * 8) d.on = false;
				}
				o *= d.vol * drumGain;
				ml += o, mr += o * (d.kind == 2 ? 0.8f : 1.0f);
			}
			bassAmp += (bassTarget - bassAmp) * 0.00003f;
			bassPh += bassFreq / SR;
			if (bassPh >= 1) bassPh -= 1;
			float bass = tanhf (sinf (bassPh * TAU) * 1.4f) * bassAmp;
			ml += bass, mr += bass;
			el += ml * 0.08f, er += mr * 0.08f;
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
			for (auto &b : noteBells) b.tick (sl, sr);
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

int Engine::countNotes (int r)
{
	int n = 0;
	for (const char *p = rhythm (r); *p; p++) n += *p >= '1' && *p <= '9';
	return n;
}

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
			if (e->sfxOn) {
				if (q.sfx == S_NOTE) e->note (q);
				else e->trigger (q);
			}
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
	if (g_reqs.size () < 64) g_reqs.push_back ({ (int) s, material, volume, pan, material });
	pthread_mutex_unlock (&g_mu);
}

void playNote (int pitch, int instrument, float volume, float pan)
{
	if (!g_run) return;
	pthread_mutex_lock (&g_mu);
	if (g_reqs.size () < 64) g_reqs.push_back ({ (int) S_NOTE, instrument, volume, pan, pitch });
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
	case GOLD_BLOCK: case IRON_ORE: case GOLD_ORE: case COPPER_BLOCK: case COPPER_ORE: return M_METAL;
	case NOTE_BLOCK: case DAYLIGHT_SENSOR: case WOOD_BUTTON: case WOOD_PLATE: return M_WOOD;
	case SIGNAL_LAMP: return M_GLASS;
	case GLUE_BLOCK: case TNT: case WIRE: return M_SOFT;
	case WOOL_WHITE: case WOOL_RED: case WOOL_ORANGE: case WOOL_YELLOW: case WOOL_GREEN: case WOOL_BLUE: case WOOL_PURPLE: case WOOL_BLACK:
		return M_SOFT;
	default: return M_STONE;
	}
}

} // namespace audio
} // namespace tc
