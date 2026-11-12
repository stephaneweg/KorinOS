/* AudioKit en C : synthèse FM (réécriture de ak_fm.inc ; voir ak_fm.h).
 *
 * Des voix, chacune une onde simple (carré, sinus, triangle, scie, bruit) ou un
 * instrument FM à deux opérateurs (à la manière de l'OPL2 : AKFI_*), en
 * arithmétique entière : d'après fmsynth.h d'Onyx, mêmes calculs au bit près
 * que l'asm (calculs 32 bits : u32/s32, débordements compris). */
#include "ak_fm.h"

/* le kit est construit avec -mno-outline-atomics (tools/kits/build_c.sh), pas
 * le banc : sans cela, GCC y appellerait __aarch64_cas4_acq (libgcc) */
#if defined(__aarch64__)
#pragma GCC target("no-outline-atomics")
#endif

/* ---- verrou : ldaxr / stxr de l'asm, attente par yield (pas d'appel système) */

static inline void cpu_relax(void)
{
#if defined(__aarch64__)
	__asm__ __volatile__("yield");
#else
	__builtin_ia32_pause();
#endif
}

void fm_lock(void *blk)
{
	u32 *l = (u32 *)((u8 *)blk + AKFM_LOCK);
	for (;;) {
		u32 z = 0;
		if (__atomic_compare_exchange_n(l, &z, 1, 1, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return;
		while (__atomic_load_n(l, __ATOMIC_RELAXED))
			cpu_relax();
	}
}

void fm_unlock(void *blk)
{
	__atomic_store_n((u32 *)((u8 *)blk + AKFM_LOCK), 0, __ATOMIC_RELEASE);
}

/* ---- création */

/* voix (1..64 ; 0 : 16) -> synthé, < 0 : erreur */
long ak_fm_new(long nv)
{
	u64 n = (u64)nv;
	if (!n)
		n = 16;
	if (n > AK_FM_MAXV)
		return AKE_INVAL;
	struct akfm *fm = ak_alloc(FMV_SIZE * n + AKFM_VOICES);
	if (!fm)
		return AKE_NOMEM;
	fm->nv = n;
	for (u64 i = 0; i < n; i++) {
		struct fmv *v = &fm->voices[i];
		v->noise = 0xACE1 + (u32)i; /* (bruit : une graine par voix) */
		v->gl = 256;
		v->gr = 256;
	}
	return (long)fm;
}

/* -> 0 (arrête ak_fm_live s'il tourne) */
long ak_fm_free(struct akfm *fm)
{
	if (!fm)
		return 0;
	if (fm->live)
		ak_fm_live((long)fm, -1);
	ak_free(fm);
	return 0;
}

struct fmv *fm_voice(struct akfm *fm, u64 v)
{
	if (v >= fm->nv)
		return 0;
	return &fm->voices[v];
}

/* ---- instruments */

static const u8 fm_mult2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
/* ak_fm_from_fms : pour chaque champ AKFO_* : le champ P_* de FM Song, son masque */
static const u8 fm_fms_map[16] = {0, 15, 2, 15, 4, 15, 6, 15, 8, 63, 10, 3, 16, 15, 20, 3};

/* rang (0..15) -> facteur 16.16 d'attaque par trame : 0..100 % en 2826 ms au
 * rang 1, moitié moins par rang (15 : instantané) */
static u32 fm_attack_k(u32 r)
{
	if (!r)
		return 0;
	if (r >= 15)
		return 0xFFFFFFFFu;
	u64 k = ((u64)543949 << (r - 1)) / 124637;
	if (!k)
		k = 1;
	return (u32)k;
}

/* rang -> unités 16.16 par trame : 96 dB en 39280 ms au rang 1, moitié moins
 * par rang (0 : jamais) */
static u32 fm_linear_inc(u32 r)
{
	if (!r)
		return 0;
	u64 x = ((u64)FM_ATT_MAX << 16) << (r - 1);
	return (u32)(x * 1000 / ((u64)39280 * FM_RATE));
}

void fm_op_set(struct fop *op, const u8 *par, u64 fresh)
{
	if (fresh) {
		op->stage = EG_OFF;
		op->att = FM_ATT_MAX << 16;
	}
	op->mult2 = fm_mult2[par[AKFO_MULT] & 15];
	op->levatt = (u32)(par[AKFO_LEVEL] & 63) << 5; /* 0,75 dB par pas */
	u32 s = par[AKFO_SUSTAIN] & 15;
	op->susatt = s == 15 ? FM_ATT_MAX : s << 7; /* 3 dB par pas */
	op->attk = fm_attack_k(par[AKFO_ATTACK] & 15);
	op->decinc = fm_linear_inc(par[AKFO_DECAY] & 15);
	op->relinc = fm_linear_inc(par[AKFO_RELEASE] & 15);
	op->wave = par[AKFO_WAVE] & 3;
	u32 f = par[AKFO_FLAGS];
	op->sus = f & AKFF_SUSTAINED;
	op->trem = f & AKFF_TREMOLO;
	op->vib = f & AKFF_VIBRATO;
}

/* instrument : AKFI_SIZE octets -> 0 / < 0 */
long ak_fm_instrument(struct akfm *fm, long vi, const u8 *inst)
{
	struct fmv *v = fm_voice(fm, (u64)vi);
	if (!v || !inst)
		return AKE_INVAL;
	fm_lock(fm);
	u64 fresh = v->wave != AKW_FM;
	fm_op_set(&v->op[0], inst + AKFI_OP0, fresh);
	fm_op_set(&v->op[1], inst + AKFI_OP1, fresh);
	v->feedback = inst[AKFI_FEEDBACK] & 7;
	v->conn = inst[AKFI_CONN] & 1;
	v->hasfm = 1;
	fm_unlock(fm);
	return 0;
}

/* inst (AKFI_SIZE, rempli) <- les 26 octets d'un instrument FM Song (.FMS :
 * paires opérateur 1, opérateur 2) -> 0 (inst[AKFO_FLAGS + 1] et les octets
 * 22, 23 de inst ne sont pas touchés, comme l'asm) */
long ak_fm_from_fms(u8 *inst, const u8 *fms)
{
	for (int o = 0; o < 2; o++) {
		u8 *d = inst + AKFO_SIZE * o;
		for (int f = 0; f < 8; f++)
			d[f] = fms[fm_fms_map[2 * f] + o] & fm_fms_map[2 * f + 1];
		u8 fl = 0;
		if (fms[o + 24]) /* P_EGT */
			fl |= AKFF_SUSTAINED;
		if (fms[o + 14]) /* P_AM */
			fl |= AKFF_TREMOLO;
		if (fms[o + 12]) /* P_VIB */
			fl |= AKFF_VIBRATO;
		if (fms[o + 18]) /* P_KSR */
			fl |= AKFF_KSR;
		d[AKFO_FLAGS] = fl;
	}
	inst[AKFI_FEEDBACK] = fms[22] & 7; /* P_FB */
	inst[AKFI_CONN] = fms[23] & 1;	   /* P_CON */
	return 0;
}

/* ---- jeu */

/* milli-Hz (bornés à 20 kHz, non signés comme l'asm) -> pas de phase */
static u32 fm_inc(long mhz)
{
	u64 f = (u64)mhz;
	if (f > 20000000)
		f = 20000000;
	return (u32)((f << 32) / ((u64)1000 * FM_RATE));
}

/* fréquence (milli-Hz), AKW_*, volume 0..255 -> 0 / < 0 : la note sonne jusqu'à
 * ak_fm_stop */
long ak_fm_start(struct akfm *fm, long vi, long mhz, long wave, long vol)
{
	struct fmv *v = fm_voice(fm, (u64)vi);
	if (!v)
		return AKE_INVAL;
	if (vol < 0)
		vol = 0;
	if (vol > 255)
		vol = 255;
	fm_lock(fm);
	v->inc = fm_inc(mhz);
	if (wave == AKW_FM && v->hasfm) {
		const u32 max = FM_ATT_MAX << 16;
		if (v->wave != AKW_FM) { /* touche enfoncée : les enveloppes repartent */
			v->op[0].att = max;
			v->op[1].att = max;
		}
		v->wave = AKW_FM;
		v->gain = 257 * (u32)vol;
		v->target = 257 * (u32)vol;
		v->op[0].stage = EG_ATTACK;
		v->op[1].stage = EG_ATTACK;
		v->op[0].phase = 0;
		v->op[1].phase = 0;
		if (v->op[0].att > max)
			v->op[0].att = max;
		if (v->op[1].att > max)
			v->op[1].att = max;
		v->fb1 = 0;
		v->fb2 = 0;
	} else {
		if (v->wave == AKW_FM) {
			v->op[0].stage = EG_OFF;
			v->op[1].stage = EG_OFF;
			v->gain = 0;
		}
		if ((u64)wave > AKW_NOISE) /* (AKW_FM sans instrument, négatif : carré) */
			wave = AKW_SQUARE;
		v->wave = (u32)wave;
		v->target = 257 * (u32)vol;
		if (!v->on && !v->gain)
			v->phase = 0;
	}
	v->on = 1;
	fm_unlock(fm);
	return 0;
}

void fm_key_off(struct fmv *v)
{
	v->on = 0;
	if (v->wave != AKW_FM) {
		v->target = 0;
		return;
	}
	if (v->op[0].stage)
		v->op[0].stage = EG_RELEASE;
	if (v->op[1].stage)
		v->op[1].stage = EG_RELEASE;
}

/* voix (-1, tout négatif : toutes ; numéro non vérifié, comme l'asm) -> 0 : les
 * relâchements s'entendent */
long ak_fm_stop(struct akfm *fm, long vi)
{
	fm_lock(fm);
	u64 n = fm->nv;
	for (u64 i = 0; i < n; i++)
		if (vi < 0 || i == (u64)vi)
			fm_key_off(&fm->voices[i]);
	fm_unlock(fm);
	return 0;
}

/* -> 0 : tout se tait d'un coup */
long ak_fm_silence(struct akfm *fm)
{
	fm_lock(fm);
	for (u64 i = 0; i < fm->nv; i++) {
		struct fmv *v = &fm->voices[i];
		v->on = 0;
		v->gain = 0;
		v->target = 0;
		v->op[0].stage = EG_OFF;
		v->op[1].stage = EG_OFF;
	}
	fm_unlock(fm);
	return 0;
}

long fm_voice_active(const struct fmv *v)
{
	if (v->wave == AKW_FM)
		return (v->op[0].stage | v->op[1].stage) != 0;
	return (v->on | v->gain) != 0;
}

/* -> 1 tant qu'une voix sonne (sans verrou, comme l'asm) */
long ak_fm_active(struct akfm *fm)
{
	for (u64 i = 0; i < fm->nv; i++)
		if (fm_voice_active(&fm->voices[i]))
			return 1;
	return 0;
}

/* panoramique 0 (gauche) .. 128 (centre) .. 255 -> 0 (sans verrou ; au-delà de
 * 255 : division non signée de 255 - pan, comme l'asm) */
long ak_fm_pan(struct akfm *fm, long vi, long pan)
{
	struct fmv *v = fm_voice(fm, (u64)vi);
	if (!v)
		return AKE_INVAL;
	if ((u64)pan < 128) {
		v->gl = 256;
		v->gr = (u32)((u64)pan << 1);
	} else {
		v->gl = (u32)(((u64)(255 - pan) << 8) / 127); /* droite pleine : gauche 0 */
		v->gr = 256;
	}
	return 0;
}

/* fréquence (milli-Hz) -> 0 : la note glisse (pas de phase changé, enveloppes
 * intactes ; sans verrou) */
long ak_fm_pitch(struct akfm *fm, long vi, long mhz)
{
	struct fmv *v = fm_voice(fm, (u64)vi);
	if (!v)
		return AKE_INVAL;
	v->inc = fm_inc(mhz);
	return 0;
}

/* volume 0..255 (non borné, comme l'asm) -> 0 : volume d'une note qui sonne */
long ak_fm_gain(struct akfm *fm, long vi, long vol)
{
	struct fmv *v = fm_voice(fm, (u64)vi);
	if (!v)
		return AKE_INVAL;
	u32 g = 257 * (u32)vol;
	v->target = g;
	if (v->wave == AKW_FM)
		v->gain = g;
	return 0;
}

/* ---- rendu */

/* opérateur -> atténuation courante (unités), une trame de plus */
static u32 fm_env(struct fop *op)
{
	u32 st = op->stage, a = op->att;
	switch (st) {
	case EG_ATTACK: {
		u32 k = op->attk;
		if (k == 0xFFFFFFFFu)
			a = 0;
		else if (k)
			a -= (u32)(((u64)a * k) >> 16) + 1;
		if ((s32)a <= 0x10000) {
			a = 0;
			st = EG_DECAY;
			op->stage = st;
		}
		break;
	}
	case EG_DECAY:
		a += op->decinc;
		if (!(op->susatt > a >> 16)) {
			a = op->susatt << 16;
			st = EG_SUSTAIN;
			op->stage = st;
		}
		break;
	case EG_SUSTAIN:
		if (op->sus)
			break;
		a += op->relinc;
		break;
	case EG_RELEASE:
		a += op->relinc;
		break;
	default:
		return FM_ATT_MAX;
	}
	u32 u = a >> 16;
	if (u >= FM_ATT_MAX) {
		op->att = FM_ATT_MAX << 16;
		if (st >= EG_SUSTAIN)
			op->stage = EG_OFF;
		return FM_ATT_MAX;
	}
	op->att = a;
	return u;
}

/* opérateur, phase, atténuation -> sortie */
static s32 fm_wave(const struct fop *op, u32 ph, u32 att)
{
	u32 i = ph >> 22; /* 1024 pas */
	s32 s = ak_sin1024[i];
	u32 w = op->wave;
	if (w == 1) {
		if (i >= 512) /* demi-sinus */
			s = 0;
	} else if (w) {
		if (s < 0)
			s = -s;
		if (w != 2 && (i & 256)) /* impulsions */
			s = 0;
	}
	if (att >= FM_ATT_MAX)
		return 0;
	u32 e = (u32)ak_exp256[att & 255] >> (att >> 8);
	return (s32)((u32)s * e) >> 16;
}

/* pas d'un opérateur, vibrato compris */
static u32 fm_op_step(const struct fop *op, u32 inc2, s64 vib)
{
	u32 f = op->mult2 * inc2;
	if (op->vib)
		f += (u32)((s64)((u64)(f >> 8) * (u64)vib) >> 8);
	return f;
}

/* voix FM, trémolo, vibrato -> sortie */
static s32 fm_sample(struct fmv *v, u32 trem, s64 vib)
{
	u32 inc2 = v->inc >> 1;
	/* modulateur (avec retour) */
	struct fop *op = &v->op[0];
	u32 f = fm_op_step(op, inc2, vib);
	u32 att = fm_env(op) + op->levatt;
	if (op->trem)
		att += trem;
	u32 ph = op->phase;
	if (v->feedback) {
		s32 s = (s32)((u32)v->fb1 + (u32)v->fb2);
		s >>= (12 - v->feedback) & 31;
		ph += (u32)s << 22;
	}
	s32 mo = fm_wave(op, ph, att);
	op->phase += f;
	v->fb2 = v->fb1;
	v->fb1 = mo;
	/* porteuse */
	op = &v->op[1];
	f = fm_op_step(op, inc2, vib);
	att = fm_env(op) + op->levatt;
	if (op->trem)
		att += trem;
	ph = op->phase;
	if (!v->conn)
		ph += (u32)mo << 19; /* FM : +-4 périodes à plein niveau */
	s32 out = fm_wave(op, ph, att);
	op->phase += f;
	if (v->conn)
		out = (s32)((u32)out + (u32)mo) >> 1;
	return out;
}

/* voix simple -> sortie ; enveloppe, phase, bruit */
static s32 fm_plain(struct fmv *v)
{
	u32 g = v->gain, t = v->target;
	if (g != t) {
		if (g > t)
			g = g > t + 300 ? g - 300 : t;
		else {
			g += 300;
			if (g > t)
				g = t;
		}
		v->gain = g;
	}
	u32 p = v->phase; /* avant le pas */
	u32 np = p + v->inc;
	v->phase = np;
	u32 w = v->wave;
	if (w == AKW_NOISE) {
		if (np < p) { /* une nouvelle valeur par période */
			u32 n = v->noise;
			n = ((0 - (n & 1)) & 0xB400) ^ (n >> 1);
			v->noise = n;
			v->noisev = (s32)(n & 0xFFFF) - 0x8000;
		}
		return v->noisev;
	}
	if (w == AKW_SINE)
		return ak_sin1024[np >> 22];
	if (w == AKW_TRIANGLE) {
		s32 x = (s32)(np >> 15);
		if (x & 0x10000)
			return 98303 - x;
		return x - 0x8000;
	}
	if (w == AKW_SAW)
		return (s32)(np >> 16) - 0x8000;
	return (np & 0x80000000u) ? -24000 : 24000; /* carré (un peu moins fort) */
}

/* triangle des LFO : phase 16 bits -> 0..32767..0 */
static u32 lfo_tri(u32 ph)
{
	u32 x = ph >> 16;
	return (x & 0x8000) ? 65535 - x : x;
}

void fm_render_piece(struct akfm *fm, s16 *out, u64 frames)
{
	for (; frames; frames--) {
		/* LFO : trémolo 1 dB (42 unités) à 3,7 Hz, vibrato +-7 cents à 6,1 Hz */
		fm->lfot += 360353;
		u32 trem = (lfo_tri(fm->lfot) * 42) >> 15;
		fm->lfov += 594096;
		s32 x = ((s32)lfo_tri(fm->lfov) >> 6) - 256;
		s64 vib = ak_sdivw(x * 7, 17);
		/* les voix */
		s32 l = 0, r = 0;
		for (u64 i = 0; i < fm->nv; i++) {
			struct fmv *v = &fm->voices[i];
			s32 s;
			if (v->wave == AKW_FM) {
				if (!(v->op[0].stage | v->op[1].stage))
					continue;
				s = fm_sample(v, trem, vib);
			} else {
				if (!(v->on | v->gain))
					continue;
				s = fm_plain(v);
			}
			s = (s32)((u32)s * (v->gain >> 1)) >> 15;
			l += (s32)((u32)s * v->gl) >> 8;
			r += (s32)((u32)s * v->gr) >> 8;
		}
		l >>= 2; /* 4 voix pleines avant saturation */
		r >>= 2;
		if (l > 32767)
			l = 32767;
		if (l < -32768)
			l = -32768;
		if (r > 32767)
			r = 32767;
		if (r < -32768)
			r = -32768;
		out[0] = (s16)l;
		out[1] = (s16)r;
		out += 2;
	}
}

/* sortie s16 stéréo, trames -> 0 : les trames suivantes ÉCRITES dans la sortie
 * (hors ligne ; à ne pas mélanger avec ak_fm_live) */
long ak_fm_render(struct akfm *fm, s16 *out, u64 frames)
{
	while (frames) {
		u64 n = frames < FM_PIECE ? frames : FM_PIECE;
		fm_lock(fm);
		fm_render_piece(fm, out, n);
		fm_unlock(fm);
		out += n * 2;
		frames -= n;
	}
	return 0;
}

/* cents (signés), base -> base * 2^(cents / 1200), arrondi */
long ak_cents_mul(long cents, long base)
{
	u64 x = (u64)1200 * 1024 + (u64)cents; /* (positif : division entière vers le bas) */
	u64 q = x / 1200;
	u64 rem = x - q * 1200; /* 0..1199 (cents < -1228800 : division d'un nombre énorme, comme l'asm) */
	u64 oct = q - 1024;
	u64 m = (u64)ak_cents1200[rem] * (u64)base; /* 2.30 */
	s64 sh = 30 - (s64)oct; /* décalage final */
	if (sh < 1)
		return (long)AK_SHL(m, (u64)-sh);
	if (sh > 63)
		return 0;
	m += (u64)1 << (sh - 1);
	return (long)(m >> sh);
}

/* touche MIDI -> sa fréquence en milli-Hz (69 : la4 = 440000) */
long ak_note_mhz(long key)
{
	if (key < 0)
		key = 0;
	if (key > 127)
		key = 127;
	return ak_cents_mul((key - 69) * 100, 440000);
}

/* note 0..11 (C..B), octave -> milli-Hz (C4 = 60, comme ak_note_key) */
long ak_note_octave_mhz(long note, long octave)
{
	return ak_note_mhz(note + (octave + 1) * 12);
}
