//
// kplug.h -- the plugin side of Koton's plugins (the protocol: kplug_proto.h). A plugin -- an
// instrument, an effect or a generator -- is a small program: its parameters, a few callbacks, and
//
//     static const KpParamDef P[] = { { "cutoff", "Cutoff", 20, 18000, 2000, "Hz" }, ... };
//     static void render (float *l, float *r, int n) { ... kp_param (0) ... }
//     static const KpDesc desc = { .name = "My synth", .kind = KP_INSTRUMENT, .params = P,
//                                  .nparams = 3, .noteOn = on, .noteOff = off, .render = render };
//     KPLUG_MAIN (desc)
//
// and this runtime does the rest: the shared region, the handshake, the render thread (high
// priority, a short sleep between two passes: it renders ahead, sample-accurately, the events the
// engine sent), the parameters (from the host), the state (JSON: {"v":1, "params": {id: value...}}
// + what saveState adds) and a generator's requests. On POSIX, as a program of libposix; no kit. The
// editor is the host's, made from the parameters (plughost.cpp): a plugin has no window.
//
// Threads: prepare / setParam / noteOn / noteOff / allOff / cc / render / process run on the RENDER
// thread (prepare and the first setParam before it starts) -- they must not allocate, lock or wait;
// kp_param (i) reads a parameter there. generate / saveState / loadState run on the main thread
// (they may allocate). A plugin never calls the kernel from its DSP.
//
// Build: tools/webkit/build-koton.sh: plugins/<id>/main.cpp -> SD0:/apps/koton.app/plugins/<id>/main
// + plugin.json. For the tests without a process, define KPLUG_DSP_ONLY and KPLUG_TEST_SYM=<a name>:
// no kernel; KPLUG_MAIN then exports a KpTestApi under that name.
//
#ifndef _kplug_h
#define _kplug_h

#include "kplug_proto.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "ui/json.hpp"

// ---- the description ----------------------------------------------------------------------------------
struct KpParamDef
{
	const char *id, *name;			// the id is saved in the states: never change it
	float min, max, def;
	const char *unit;			// "Hz", "ms", "dB", "%"... (0: none)
	float step;				// 0: continuous; 1: whole numbers
	const char *const *choices;		// an enumeration: its names, 0-terminated (min 0, max n - 1)
};

struct KpNote { double start, len; int note, vel; };	// a generator's note: beats from the block's start, MIDI

class KpNotes
{
public:
	KpNotes () : m_v (0), m_n (0), m_cap (0) {}
	~KpNotes () { free (m_v); }
	bool add (double start, double len, int note, int vel)
	{
		if (note < 0 || note > 127 || len <= 0) return false;
		if (m_n == m_cap)
		{
			int nc = m_cap ? m_cap * 2 : 256;
			KpNote *v = (KpNote *) realloc (m_v, sizeof (KpNote) * (size_t) nc);
			if (!v) return false;
			m_v = v; m_cap = nc;
		}
		KpNote &x = m_v[m_n++]; x.start = start; x.len = len; x.note = note; x.vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
		return true;
	}
	int size () const { return m_n; }
	const KpNote &operator[] (int i) const { return m_v[i]; }
	void clear () { m_n = 0; }
private:
	KpNotes (const KpNotes &);
	KpNotes &operator= (const KpNotes &);
	KpNote *m_v; int m_n, m_cap;
};

// A chord of the chord track under a generator's block (the host's kt::segments).
struct KpChord
{
	double start, len;			// beats from the block's start
	int root;				// its pitch class 0..11
	int quality;				// Koton's quality (0..34: Major, Minor, Diminished... 7#5)
	int iv[8], niv;				// its intervals from the root (the whole chord: 9ths, 11ths...)
	int basic;				// Koton's plugin contract (KotonChordQuality 0..13: Major, Minor,
	int biv[4], nbiv;			//   Diminished, Augmented, Sus2, Sus4, Power, Dom7, Maj7, Min7,
						//   MinMaj7, Dim7, HalfDim7, Aug7) and its intervals
	int bass;				// the pitch class at the bass (an inversion), = root otherwise
};

// What a generator is asked: the notes of [from, to) beats of its block, knowing the song.
struct KpContext
{
	double from, to, length;		// beats from the block's start (the whole block: 0, its length)
	double blockStart;			// the block's absolute position (beats)
	double bpm, pickup;
	int tonic;				// the key: its tonic's pitch class
	int mode;				// 0 major, 1 minor (the key's scale below says more)
	int scale[12], nscale;			// the key's scale: pitch classes from the tonic (7 for a mode)
	int meterNum, meterDen;
	bool ternary;				// a compound meter (6/8, 9/8, 12/8)
	const KpChord *chords; int nchords;	// by time
	unsigned seed;				// the module's own (stable across renders)
	const json::Value *state;		// the module's whole state (a generator's extras)
	// the chord at a beat of the block, 0: none (a hole in the chord track)
	const KpChord *chordAt (double beat) const
	{
		for (int i = 0; i < nchords; i++) if (beat >= chords[i].start - 1e-9 && beat < chords[i].start + chords[i].len - 1e-9) return &chords[i];
		return 0;
	}
};

struct KpDesc				// (designated initialisers -- .name = ..., in this order -- as GCC takes them in
				//  C++17; what is left out is 0)
{
	const char *name = 0;
	int kind = 0;				// KP_INSTRUMENT / KP_EFFECT / KP_GENERATOR
	const KpParamDef *params = 0; int nparams = 0;
	int editorW = 0, editorH = 0;		// the editor's size it prefers (0: from its parameters)
	void (*prepare) (int rate) = 0;
	void (*noteOn) (int note, int vel) = 0;
	void (*noteOff) (int note) = 0;
	void (*allOff) (bool hard) = 0;		// hard: cut every voice at once (a seek); else release them
	void (*cc) (int controller, int value) = 0;
	void (*render) (float *l, float *r, int n) = 0;		// an instrument: WRITE n frames
	void (*process) (float *l, float *r, int n) = 0;	// an effect: in place
	bool (*generate) (const KpContext &ctx, const float *params, KpNotes &out) = 0;	// a generator
	void (*setParam) (int index, float value) = 0;	// a parameter changed (the render thread, between blocks)
	void (*saveState) (json::Writer &w) = 0;	// its own keys, beyond "params" (optional)
	void (*loadState) (const json::Value &state) = 0;
};

// ---- small DSP helpers (float, no allocation) --------------------------------------------------------------
static const float KP_PI = 3.14159265358979f;
static inline float kp_mtof (float note) { return 440.0f * powf (2.0f, (note - 69.0f) / 12.0f); }
static inline float kp_db (float db) { return powf (10.0f, db / 20.0f); }
static inline float kp_clampf (float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

// sin (2 pi x), x in turns, from a table (linear interpolation)
static const int KP_SIN_N = 2048;
static float kp_sinTab[KP_SIN_N + 1];
static inline void kp_dsp_init ()
{
	for (int i = 0; i <= KP_SIN_N; i++) kp_sinTab[i] = sinf (2.0f * KP_PI * (float) i / KP_SIN_N);
}
static inline float kp_sin (float x)
{
	x -= floorf (x);
	float f = x * KP_SIN_N; int i = (int) f; if (i >= KP_SIN_N) i = KP_SIN_N - 1;
	float t = f - (float) i;
	return kp_sinTab[i] + (kp_sinTab[i + 1] - kp_sinTab[i]) * t;
}

// a fast noise (xorshift32) in [-1, 1)
struct KpNoise { unsigned s; KpNoise () : s (0x9E3779B9u) {} float next () { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (int) s * (1.0f / 2147483648.0f); } };

// ADSR: a linear attack, exponential decay and release (times in seconds, sustain 0..1)
struct KpAdsr
{
	enum { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };
	int stage; float v, aStep, dMul, s, rMul;
	KpAdsr () : stage (IDLE), v (0), aStep (1), dMul (0.999f), s (1), rMul (0.999f) {}
	void set (float a, float d, float sus, float r, int rate)
	{
		aStep = 1.0f / (a * rate + 1.0f);
		dMul = expf (-6.9f / (d * rate + 1.0f));		// -60 dB in the time given
		rMul = expf (-6.9f / (r * rate + 1.0f));
		s = kp_clampf (sus, 0, 1);
	}
	void on () { stage = ATTACK; }
	void off () { if (stage != IDLE) stage = RELEASE; }
	void kill () { stage = IDLE; v = 0; }
	bool active () const { return stage != IDLE; }
	float next ()
	{
		switch (stage)
		{
		case ATTACK: v += aStep; if (v >= 1) { v = 1; stage = DECAY; } break;
		case DECAY: v = s + (v - s) * dMul; if (v - s < 1e-4f) { v = s; stage = SUSTAIN; } break;
		case SUSTAIN: v = s; if (s <= 0) stage = IDLE; break;
		case RELEASE: v *= rMul; if (v < 1e-4f) { v = 0; stage = IDLE; } break;
		default: v = 0; break;
		}
		return v;
	}
};

// a state-variable filter (the trapezoidal "TPT" one: stable when modulated)
struct KpSvf
{
	float g, k, a1, a2, a3, ic1, ic2;
	KpSvf () : g (0), k (1), a1 (0), a2 (0), a3 (0), ic1 (0), ic2 (0) {}
	void set (float hz, float q, int rate)
	{
		hz = kp_clampf (hz, 10, rate * 0.45f);
		g = tanf (KP_PI * hz / rate); k = 1.0f / (q < 0.5f ? 0.5f : q);
		a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
	}
	void reset () { ic1 = ic2 = 0; }
	// mode 0 low-pass, 1 band-pass, 2 high-pass
	float tick (float v0, int mode)
	{
		float v3 = v0 - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
		return mode == 0 ? v2 : mode == 1 ? v1 : v0 - k * v1 - v2;
	}
};

// a biquad (the RBJ cookbook's)
struct KpBiquad
{
	float b0, b1, b2, a1, a2, z1, z2;
	KpBiquad () : b0 (1), b1 (0), b2 (0), a1 (0), a2 (0), z1 (0), z2 (0) {}
	enum { LOWPASS, HIGHPASS, PEAK, LOWSHELF, HIGHSHELF };
	void set (int type, float hz, float q, float gainDb, int rate)
	{
		hz = kp_clampf (hz, 10, rate * 0.45f);
		float A = powf (10.0f, gainDb / 40.0f), w = 2 * KP_PI * hz / rate, c = cosf (w), s = sinf (w), al = s / (2 * (q < 0.1f ? 0.1f : q));
		float B0, B1, B2, A0, A1, A2;
		switch (type)
		{
		case LOWPASS: B0 = (1 - c) / 2; B1 = 1 - c; B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
		case HIGHPASS: B0 = (1 + c) / 2; B1 = -(1 + c); B2 = B0; A0 = 1 + al; A1 = -2 * c; A2 = 1 - al; break;
		case PEAK: B0 = 1 + al * A; B1 = -2 * c; B2 = 1 - al * A; A0 = 1 + al / A; A1 = -2 * c; A2 = 1 - al / A; break;
		case LOWSHELF:
		{
			float sa = 2 * sqrtf (A) * al;
			B0 = A * ((A + 1) - (A - 1) * c + sa); B1 = 2 * A * ((A - 1) - (A + 1) * c); B2 = A * ((A + 1) - (A - 1) * c - sa);
			A0 = (A + 1) + (A - 1) * c + sa; A1 = -2 * ((A - 1) + (A + 1) * c); A2 = (A + 1) + (A - 1) * c - sa;
		} break;
		default:
		{
			float sa = 2 * sqrtf (A) * al;
			B0 = A * ((A + 1) + (A - 1) * c + sa); B1 = -2 * A * ((A - 1) + (A + 1) * c); B2 = A * ((A + 1) + (A - 1) * c - sa);
			A0 = (A + 1) - (A - 1) * c + sa; A1 = 2 * ((A - 1) - (A + 1) * c); A2 = (A + 1) - (A - 1) * c - sa;
		} break;
		}
		b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
	}
	void reset () { z1 = z2 = 0; }
	float tick (float x) { float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// Koton's basic chord qualities (KotonChordQuality) and their intervals
static const int KP_BASIC_IV[14][4] = {
	{ 0, 4, 7, -1 }, { 0, 3, 7, -1 }, { 0, 3, 6, -1 }, { 0, 4, 8, -1 }, { 0, 2, 7, -1 }, { 0, 5, 7, -1 }, { 0, 7, -1, -1 },
	{ 0, 4, 7, 10 }, { 0, 4, 7, 11 }, { 0, 3, 7, 10 }, { 0, 3, 7, 11 }, { 0, 3, 6, 9 }, { 0, 3, 6, 10 }, { 0, 4, 8, 10 } };

// ---- the runtime's core (no kernel call: the PC tests run it too) ---------------------------------------
struct KpRuntime
{
	KpShm *shm;
	const KpDesc *desc;
	int rate;
	float cur[KP_MAX_PARAMS];		// the render thread's values (kp_param)
	float want[KP_MAX_PARAMS];		// the main thread's (the host, the editor): applied between blocks
	volatile unsigned long long dirty;	// want[i] changed
	unsigned long long done;		// the render thread's position
	bool started, threaded;
	volatile int quit;
};
static KpRuntime g_kp;

static inline float kp_param (int i) { return i >= 0 && i < KP_MAX_PARAMS ? g_kp.cur[i] : 0; }
static inline int kp_rate () { return g_kp.rate; }
static inline int kp_nparams () { return g_kp.desc ? (g_kp.desc->nparams < KP_MAX_PARAMS ? g_kp.desc->nparams : KP_MAX_PARAMS) : 0; }
static inline int kp_param_index (const char *id)
{
	for (int i = 0; i < kp_nparams (); i++) if (!strcmp (g_kp.desc->params[i].id, id)) return i;
	return -1;
}

static inline float kp__clampParam (int i, float v)
{
	const KpParamDef &p = g_kp.desc->params[i];
	if (!(v == v)) v = p.def;
	if (v < p.min) v = p.min;
	if (v > p.max) v = p.max;
	if (p.step > 0) v = p.min + p.step * floorf ((v - p.min) / p.step + 0.5f);
	return v;
}

// a parameter from the main thread (the host, a state): for the render thread's next block
static inline void kp__set (int i, float v)
{
	if (i < 0 || i >= kp_nparams ()) return;
	v = kp__clampParam (i, v);
	g_kp.want[i] = v;
	if (g_kp.shm) g_kp.shm->param[i] = v;
	if (g_kp.threaded) __atomic_or_fetch (&g_kp.dirty, 1ull << i, __ATOMIC_RELEASE);
	else { g_kp.cur[i] = v; if (g_kp.started && g_kp.desc->setParam) g_kp.desc->setParam (i, v); }
}

// the render thread: the parameters changed since the last block
static inline void kp__apply ()
{
	unsigned long long m = __atomic_exchange_n (&g_kp.dirty, 0ull, __ATOMIC_ACQUIRE);
	for (int i = 0; m && i < KP_MAX_PARAMS; i++, m >>= 1)
		if (m & 1) { g_kp.cur[i] = g_kp.want[i]; if (g_kp.desc->setParam) g_kp.desc->setParam (i, g_kp.cur[i]); }
}

static inline void kp__event (const KpEvent &e)
{
	const KpDesc *d = g_kp.desc;
	switch (e.type)
	{
	case KPE_NOTE_ON: if (d->noteOn) { if (e.b) d->noteOn (e.a, e.b); else if (d->noteOff) d->noteOff (e.a); } break;
	case KPE_NOTE_OFF: if (d->noteOff) d->noteOff (e.a); break;
	case KPE_ALL_OFF: if (d->allOff) d->allOff (false); break;
	case KPE_RESET: if (d->allOff) d->allOff (true); break;
	case KPE_CC: if (d->cc) d->cc (e.a, e.b); break;
	case KPE_PARAM:
		if (e.a < kp_nparams ()) { float v = kp__clampParam (e.a, e.v); g_kp.cur[e.a] = v; if (d->setParam) d->setParam (e.a, v); }
		break;
	default: break;
	}
}

static inline void kp__clean (float *l, float *r, int n)
{
	for (int k = 0; k < n; k++)
	{
		if (!(l[k] == l[k]) || l[k] > 64 || l[k] < -64) l[k] = 0;	// (a NaN, a blow-up: never into the mix)
		if (!(r[k] == r[k]) || r[k] > 64 || r[k] < -64) r[k] = 0;
	}
}

// One render pass: the frames [done, want) -> the output ring. Frames rendered.
static inline int kp__serve ()
{
	KpShm *s = g_kp.shm;
	const KpDesc *d = g_kp.desc;
	if (!s || d->kind == KP_GENERATOR) return 0;
	unsigned long long want = kp_ld64 (&s->want), rp = kp_ld64 (&s->readPos), pos = g_kp.done;
	if (pos < rp)					// too late for those: from where the engine reads
	{
		if (g_kp.started && pos) s->lateFrames = s->lateFrames + (unsigned) (rp - pos);
		pos = rp;
		kp_st64 (&s->done, pos);
	}
	g_kp.started = true;
	float bl[KP_BLOCK], br[KP_BLOCK];
	int frames = 0;
	while (pos < want)
	{
		kp__apply ();
		unsigned long long end = want - pos > KP_BLOCK ? pos + KP_BLOCK : want;
		if (d->kind == KP_INSTRUMENT)
		{
			const KpEvent *e;
			while ((e = kp_ev_peek (s)) && e->at <= pos) { kp__event (*e); kp_ev_pop (s); }
			if ((e = kp_ev_peek (s)) && e->at < end) end = e->at;	// (sample-accurate: cut there)
			int n = (int) (end - pos);
			if (d->render) d->render (bl, br, n); else for (int k = 0; k < n; k++) bl[k] = br[k] = 0;
			kp__clean (bl, br, n);
			kp_au_write (s->outL, s->outR, pos, bl, br, n);
		}
		else
		{
			int n = (int) (end - pos);
			kp_au_read (s->inL, s->inR, pos, bl, br, n);
			if (d->process) d->process (bl, br, n);
			kp__clean (bl, br, n);
			kp_au_write (s->outL, s->outR, pos, bl, br, n);
		}
		frames += (int) (end - pos);
		pos = end;
		kp_st64 (&s->done, pos);
	}
	g_kp.done = pos;
	return frames;
}

// ---- the state and the parameter list (JSON) ----
static inline void kp__writeParams (json::Writer &w)
{
	const KpDesc *d = g_kp.desc;
	w.beginObj ();
	w.key ("name"); w.str (d->name ? d->name : "");
	w.key ("kind"); w.str (d->kind == KP_INSTRUMENT ? "instrument" : d->kind == KP_EFFECT ? "effect" : "generator");
	w.key ("version"); w.num (KP_PROTO_VERSION);
	if (d->editorW > 0 && d->editorH > 0) { w.key ("editor"); w.beginObj (true); w.key ("w"); w.num (d->editorW); w.key ("h"); w.num (d->editorH); w.endObj (); }
	w.key ("params"); w.beginArr ();
	for (int i = 0; i < kp_nparams (); i++)
	{
		const KpParamDef &p = d->params[i];
		w.beginObj (true);
		w.key ("id"); w.str (p.id); w.key ("name"); w.str (p.name);
		w.key ("min"); w.num ((double) p.min); w.key ("max"); w.num ((double) p.max); w.key ("default"); w.num ((double) p.def);
		w.key ("unit"); w.str (p.unit ? p.unit : "");
		if (p.step > 0) { w.key ("step"); w.num ((double) p.step); }
		if (p.choices) { w.key ("choices"); w.beginArr (true); for (int c = 0; p.choices[c]; c++) w.str (p.choices[c]); w.endArr (); }
		w.endObj ();
	}
	w.endArr ();
	w.endObj ();
}

static inline void kp__writeState (json::Writer &w)
{
	w.beginObj ();
	w.key ("v"); w.num (1);
	w.key ("params"); w.beginObj ();
	for (int i = 0; i < kp_nparams (); i++) { w.key (g_kp.desc->params[i].id); w.num ((double) g_kp.want[i]); }
	w.endObj ();
	if (g_kp.desc->saveState) g_kp.desc->saveState (w);
	w.endObj ();
}

// the parameters of a state into vals[] (missing: what vals holds already). "params": {id: v}, or the
// ids at the top (Koton's flat states).
static inline void kp__readParams (const json::Value &st, float *vals)
{
	const json::Value &p = st["params"];
	for (int i = 0; i < kp_nparams (); i++)
	{
		const char *id = g_kp.desc->params[i].id;
		const json::Value *v = p.isObj () ? p.find (id) : 0;
		if (!v || !v->isNum ()) v = st.find (id);
		if (v && (v->isNum () || v->isBool ())) vals[i] = kp__clampParam (i, (float) v->asDouble (vals[i]));
	}
}

static inline bool kp__loadState (const char *text, unsigned len)
{
	json::Doc doc;
	if (!doc.parse (text, len, json::TOLERANT) || !doc.root ().isObj ()) return false;
	float v[KP_MAX_PARAMS];
	for (int i = 0; i < kp_nparams (); i++) v[i] = g_kp.want[i];
	kp__readParams (doc.root (), v);
	for (int i = 0; i < kp_nparams (); i++) if (v[i] != g_kp.want[i]) kp__set (i, v[i]);
	if (g_kp.desc->loadState) g_kp.desc->loadState (doc.root ());
	return true;
}

// a generator's request: the context (JSON) -> the notes' reply ({"notes": [[start, len, note, vel]...]})
static inline bool kp__generate (const char *text, unsigned len, json::Writer &out)
{
	const KpDesc *d = g_kp.desc;
	if (!d->generate) return false;
	json::Doc doc;
	if (!doc.parse (text, len, json::TOLERANT) || !doc.root ().isObj ()) return false;
	const json::Value &r = doc.root ();
	KpContext c;
	c.from = r["from"].asDouble (0); c.length = r["length"].asDouble (4); c.to = r["to"].asDouble (c.length);
	c.blockStart = r["blockStart"].asDouble (0); c.bpm = r["bpm"].asDouble (120); c.pickup = r["pickup"].asDouble (0);
	const json::Value &k = r["key"];
	c.tonic = ((k["tonic"].asInt (0) % 12) + 12) % 12; c.mode = k["mode"].asInt (0);
	c.nscale = 0;
	for (const json::Value *e = k["scale"].first (); e && c.nscale < 12; e = e->next) c.scale[c.nscale++] = ((e->asInt (0) % 12) + 12) % 12;
	if (!c.nscale) { static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, min[7] = { 0, 2, 3, 5, 7, 8, 10 }; for (int i = 0; i < 7; i++) c.scale[i] = c.mode ? min[i] : maj[i]; c.nscale = 7; }
	c.meterNum = r["meter"][0].asInt (4); c.meterDen = r["meter"][1].asInt (4);
	if (c.meterNum < 1) c.meterNum = 4;
	if (c.meterDen < 1) c.meterDen = 4;
	c.ternary = c.meterDen == 8 && c.meterNum % 3 == 0;
	c.seed = (unsigned) r["seed"].asU64 (1);
	c.state = &r["state"];
	int nch = (int) r["chords"].size ();
	KpChord *ch = nch ? (KpChord *) calloc ((size_t) nch, sizeof (KpChord)) : 0;
	c.nchords = 0;
	for (const json::Value *e = r["chords"].first (); e && ch; e = e->next)
	{
		KpChord &x = ch[c.nchords++];
		x.start = (*e)["start"].asDouble (0); x.len = (*e)["len"].asDouble (0);
		x.root = (((*e)["root"].asInt (0) % 12) + 12) % 12; x.quality = (*e)["quality"].asInt (0);
		x.niv = 0; for (const json::Value *v = (*e)["iv"].first (); v && x.niv < 8; v = v->next) x.iv[x.niv++] = v->asInt (0);
		if (!x.niv) { x.iv[0] = 0; x.iv[1] = 4; x.iv[2] = 7; x.niv = 3; }
		x.basic = (*e)["basic"].asInt (0); if (x.basic < 0 || x.basic > 13) x.basic = 0;
		x.nbiv = 0; for (int q = 0; q < 4 && KP_BASIC_IV[x.basic][q] >= 0; q++) x.biv[x.nbiv++] = KP_BASIC_IV[x.basic][q];
		x.bass = (((*e)["bass"].asInt (x.root) % 12) + 12) % 12;
	}
	c.chords = ch;
	// the module's parameters: the defaults, then its state's
	float vals[KP_MAX_PARAMS];
	for (int i = 0; i < kp_nparams (); i++) vals[i] = d->params[i].def;
	if (c.state->isObj ()) kp__readParams (*c.state, vals);
	KpNotes notes;
	bool ok = d->generate (c, vals, notes);
	free (ch);
	if (!ok) return false;
	out.beginObj (); out.key ("notes"); out.beginArr ();
	for (int i = 0; i < notes.size (); i++)
	{
		const KpNote &n = notes[i];
		if (n.start < c.from - 1e-9 || n.start >= c.to - 1e-9) continue;
		out.beginArr (true); out.num (n.start, 5); out.num (n.len, 5); out.num (n.note); out.num (n.vel); out.endArr ();
	}
	out.endArr (); out.endObj ();
	return true;
}

// the request in the channel (the host posted it: kp_req_post): handled, answered
static inline void kp__request ()
{
	KpShm *s = g_kp.shm;
	unsigned seq = kp_ld32 (&s->reqSeq), type = s->reqType, len = s->reqLen;
	if (len > KP_DATA_BYTES) len = KP_DATA_BYTES;
	unsigned status = KP_OK;
	json::Writer w (false);
	bool reply = false;
	switch (type)
	{
	case KP_STATE_GET: kp__writeState (w); reply = true; break;
	case KP_STATE_SET: if (!kp__loadState (s->data, len)) status = KP_ERR_BAD; break;
	case KP_PARAMS: kp__writeParams (w); reply = true; break;
	case KP_GENERATE:
		if (!g_kp.desc->generate) status = KP_ERR_UNSUPPORTED;
		else if (!kp__generate (s->data, len, w)) status = KP_ERR_BAD;
		else reply = true;
		break;
	default: status = KP_ERR_UNSUPPORTED; break;
	}
	unsigned out = 0;
	if (reply && status == KP_OK)
	{
		if (!w.ok () || w.size () > KP_DATA_BYTES) status = KP_ERR_TOOBIG;
		else { memcpy (s->data, w.data (), w.size ()); out = (unsigned) w.size (); }
	}
	s->repStatus = status; s->repLen = out;
	kp_st32 (&s->repSeq, seq);
}

// Taking the region: the defaults, the initial state (data[], initLen), prepare, every parameter set,
// the parameter list into data[]. The render thread may start after it.
static inline bool kp__attach (KpShm *s, const KpDesc *d)
{
	if (!s || s->magic != KP_MAGIC || s->version < 1 || s->size < sizeof (KpShm)) return false;
	g_kp.shm = s; g_kp.desc = d; g_kp.rate = s->rate ? (int) s->rate : 44100;
	g_kp.dirty = 0; g_kp.started = false; g_kp.threaded = false; g_kp.quit = 0;
	kp_dsp_init ();
	for (int i = 0; i < KP_MAX_PARAMS; i++) g_kp.cur[i] = g_kp.want[i] = 0;
	for (int i = 0; i < kp_nparams (); i++) { g_kp.cur[i] = g_kp.want[i] = kp__clampParam (i, d->params[i].def); s->param[i] = g_kp.cur[i]; }
	if (s->initLen && s->initLen <= KP_DATA_BYTES) kp__loadState (s->data, s->initLen);
	if (d->prepare) d->prepare (g_kp.rate);
	g_kp.started = true;
	for (int i = 0; i < kp_nparams (); i++) { g_kp.cur[i] = g_kp.want[i]; if (d->setParam) d->setParam (i, g_kp.cur[i]); }
	g_kp.started = false;
	json::Writer w (false);
	kp__writeParams (w);
	if (w.ok () && w.size () <= KP_DATA_BYTES) { memcpy (s->data, w.data (), w.size ()); s->paramsLen = (unsigned) w.size (); }
	s->nparams = (unsigned) kp_nparams ();
	s->pluginVersion = KP_PROTO_VERSION;
	kp_st32 (&s->evRd, kp_ld32 (&s->evWr));		// (what an earlier process of this region left)
	g_kp.done = kp_ld64 (&s->done);
	return true;
}

#ifdef KPLUG_DSP_ONLY
// ---- the tests: the runtime without the kernel -------------------------------------------------------------
struct KpTestApi
{
	const KpDesc *desc;
	bool (*attach) (KpShm *s);		// as a process starting on that region
	int (*serve) ();			// one render pass
	void (*request) ();			// the request posted in the region, answered
	void (*setParam) (int i, float v);	// from the host
	float (*param) (int i);
};
#define KPLUG_MAIN(d) \
	static bool kp__t_attach (KpShm *s) { return kp__attach (s, &(d)); } \
	extern "C" const KpTestApi *KPLUG_TEST_SYM () \
	{ static const KpTestApi a = { &(d), kp__t_attach, kp__serve, kp__request, kp__set, kp_param }; return &a; }

#else
// ---- the process: POSIX ------------------------------------------------------------------------------------
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int kp__sock = -1;			// the host's channel (SOCK_SEQPACKET)
static pthread_t kp__render;
static bool kp__haveRender, kp__quitAll;

static inline unsigned long kp__ns ()
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (unsigned long) t.tv_sec * 1000000000ul + (unsigned long) t.tv_nsec;
}

static inline bool kp__send (int type, const void *data, int len)
{
	KpMsg m;
	m.type = type;
	if (len > (int) sizeof m.data) len = (int) sizeof m.data;
	if (len > 0) memcpy (m.data, data, (size_t) len);
	return send (kp__sock, &m, sizeof m.type + (size_t) (len > 0 ? len : 0), MSG_DONTWAIT | MSG_NOSIGNAL) > 0;
}

// ---- the render thread ----
// A futex of the kernel wakes the threads of one process only: the render thread cannot sleep on a
// word that the engine, in the host, moves. It sleeps a little between two passes (Onyx's kernel looks
// at its words every 10 ms too; the engine's events are lead () frames ahead).
static void *kp__renderMain (void *)
{
	KpShm *s = g_kp.shm;
	unsigned long sec = kp__ns ();
	while (!g_kp.quit)
	{
		unsigned long t0 = kp__ns ();
		int n = kp__serve ();
		unsigned long now = kp__ns ();
		if (n)
		{
			unsigned us = (unsigned) ((now - t0) / 1000);
			s->dspUs = us;
			if (us > s->dspMaxUs) s->dspMaxUs = us;
		}
		s->dspBeat = s->dspBeat + 1;
		if (now - sec >= 1000000000ul) { sec = now; s->dspMaxUs = 0; }
		usleep (kp_ld32 (&s->active) ? KP_RENDER_US : KP_IDLE_US);
	}
	return 0;
}

static inline bool kp__startRender ()
{
	pthread_attr_t at; struct sched_param sp;
	pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, 64 * 1024);
	pthread_attr_setinheritsched (&at, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy (&at, SCHED_FIFO);		// (libposix: ARMPI's high priority)
	sp.sched_priority = 1;
	pthread_attr_setschedparam (&at, &sp);
	kp__haveRender = pthread_create (&kp__render, &at, kp__renderMain, 0) == 0;
	pthread_attr_destroy (&at);
	return kp__haveRender;
}

// Run the plugin: "--kplug <region fd> <channel fd>" -> the exit code.
static inline int kplug_main (int argc, char **argv, const KpDesc *d)
{
	if (argc < 4 || strcmp (argv[1], "--kplug")) { fprintf (stderr, "kplug: a Koton plugin, started by Koton\n"); return 2; }
	int shmFd = atoi (argv[2]);
	kp__sock = atoi (argv[3]);
	void *m = mmap (0, KP_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, shmFd, 0);
	if (m == MAP_FAILED) return 3;
	KpShm *s = (KpShm *) m;
	if (s->magic != KP_MAGIC || (int) s->kind != d->kind) { s->ready = KP_FAILED; return 4; }
	if (!kp__attach (s, d)) { s->ready = KP_FAILED; return 4; }
	if (d->kind != KP_GENERATOR)
	{
		g_kp.threaded = true;
		if (!kp__startRender ()) { g_kp.threaded = false; s->ready = KP_FAILED; return 5; }
	}
	unsigned lastReq = kp_ld32 (&s->reqSeq);
	s->repSeq = lastReq;
	kp_st32 (&s->ready, KP_READY);
	KpHello hi = { KP_PROTO_VERSION, (unsigned) d->kind, (unsigned) kp_nparams (), 0 };
	kp__send (KP_HELLO, &hi, sizeof hi);
	while (!kp__quitAll)
	{
		s->beat = s->beat + 1;
		struct pollfd pf = { kp__sock, POLLIN, 0 };
		poll (&pf, 1, 16);
		KpMsg msg;
		int n;
		while ((n = (int) recv (kp__sock, &msg, sizeof msg, MSG_DONTWAIT)) > 0)
		{
			n -= (int) sizeof msg.type;
			switch (msg.type)
			{
			case KP_SET_PARAM: if (n >= (int) sizeof (KpParamMsg)) { KpParamMsg p; memcpy (&p, msg.data, sizeof p); kp__set (p.index, p.value); } break;
			case KP_BYE: kp__quitAll = true; break;
			default: break;		// (KP_STATE_GET...: doorbells -- the request is seen below)
			}
		}
		if (n == 0 || (n < 0 && errno != EAGAIN)) break;	// the host gone (killed, crashed): end
		if (kp_ld32 (&s->reqSeq) != lastReq) { lastReq = kp_ld32 (&s->reqSeq); kp__request (); }
	}
	kp_st32 (&s->ready, KP_ENDING);
	g_kp.quit = 1;
	if (kp__haveRender) pthread_join (kp__render, 0);
	kp__send (KP_BYE, 0, 0);
	return 0;
}

#define KPLUG_MAIN(d) int main (int argc, char **argv) { return kplug_main (argc, argv, &(d)); }
#endif

#endif
