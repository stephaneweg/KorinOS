/* Banc différentiel : synthé MIDI (ak_synth.c, ak_gmbank.c) et lecture des
 * fichiers .mid / .fms (ak_midi.c). */
#include <dirent.h>
#include <sys/mman.h>

#include "ak_fm.h"
#include "ak_synth.h"
#include "cmp.h"

static uint32_t rng;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}
static int rr(int a, int b)
{
	return a + (int)(rnd() % (uint32_t)(b - a + 1));
}

static void put16(uint8_t *p, uint32_t v)
{
	p[0] = v;
	p[1] = v >> 8;
}
static void put32(uint8_t *p, uint32_t v)
{
	put16(p, v);
	put16(p + 2, v >> 16);
}

static uint8_t *sfmem; /* SoundFont fabriquée (plus bas) */

/* ---- état d'un synthé, sans les adresses des blocs (différentes d'une passe
 * à l'autre) */
static void dump_sy(const char *nm, long h)
{
	static struct aksy c;
	char n2[96];
	memcpy(&c, (void *)h, sizeof c);
	struct akfm *fm = c.fm;
	uint8_t *rv = c.rvb, *ch = c.cho;
	c.sf = 0;
	c.fm = 0;
	c.rvb = 0;
	c.cho = 0;
	c.lock = 0;
	for (int i = 0; i < SY_NV; i++)
		if (c.voices[i].smp)
			c.voices[i].smp -= (uintptr_t)sfmem;
	OUT(nm, &c, sizeof c);
	if (fm) {
		snprintf(n2, sizeof n2, "%s.fm", nm);
		OUT(n2, (uint8_t *)fm + 8, AKFM_VOICES - 8 + fm->nv * FMV_SIZE);
	}
	if (rv) {
		uint8_t l[24 * 24];
		memcpy(l, rv, sizeof l);
		size_t nf = 0;
		for (int i = 0; i < 24; i++) {
			nf += *(uint32_t *)(l + 24 * i + 4);
			memset(l + 24 * i + 16, 0, 8);
		}
		snprintf(n2, sizeof n2, "%s.rv", nm);
		OUT(n2, l, sizeof l);
		OUTV(n2, crc32(rv + sizeof l, nf * 4));
	}
	if (ch) {
		snprintf(n2, sizeof n2, "%s.ch", nm);
		OUTV(n2, crc32(ch, 32 + 2048 * 4 + 4));
		OUT(n2, ch, 32);
	}
}

/* ---- tables : ak_concave et la banque (même suite d'octets que l'asm) */
static void t_tab(int ref)
{
	const void *p = ref ? refaddr(REF_ak_concave) : (const void *)&ak_gmtab;
	OUT("gmtab", p, sizeof ak_gmtab - 2);
	if (ref) {
		OUT("gm_prog", refaddr(REF_ak_gm_prog), 128);
		OUT("gm_drum", refaddr(REF_ak_gm_drum), 94);
		OUT("gm_ins", refaddr(REF_ak_gm_ins), GM_NINS * AKFI_SIZE);
	} else {
		OUT("gm_prog", ak_gmtab.prog, 128);
		OUT("gm_drum", ak_gmtab.drum, 94);
		OUT("gm_ins", ak_gmtab.ins, GM_NINS * AKFI_SIZE);
	}
}

/* ---- SoundFont fabriquée (graine) : échantillons, instruments, presets,
 * modulateurs ; valeurs bornées pour que la lecture reste dans la mémoire */
#define SFPAD (8u << 20)
/* sfmem : le fichier, suivi de zéros */
static size_t sflen;

static uint8_t ck_ph[8192], ck_pb[8192], ck_pm[8192], ck_pg[16384], ck_in[4096], ck_ib[8192], ck_im[8192],
	ck_ig[32768], ck_sh[4096];
static size_t n_ph, n_pb, n_pm, n_pg, n_in, n_ib, n_im, n_ig, n_sh;
static int16_t smp[200000];
static size_t nsmp;

static void gen(uint8_t *ck, size_t *n, int op, int amt)
{
	put16(ck + *n, op);
	put16(ck + *n + 2, (uint16_t)amt);
	*n += 4;
}
static void rangegen(uint8_t *ck, size_t *n, int op, int lo, int hi)
{
	put16(ck + *n, op);
	ck[*n + 2] = lo;
	ck[*n + 3] = hi;
	*n += 4;
}
static void mod(uint8_t *ck, size_t *n)
{
	static const uint16_t srcs[] = {0x0502, 0x0102, 0x0002, 0x0003, 0x0103, 0x0087, 0x008A, 0x0081, 0x008B,
					0x00DB, 0x00DD, 0x0A02, 0x0E02, 0x0602, 0x0203, 0x1002, 0x0007};
	static const uint16_t dsts[] = {48, 8, 11, 48, 8, 5, 48};
	uint8_t *m = ck + *n;
	put16(m, srcs[rnd() % (sizeof srcs / 2)]);
	put16(m + 2, dsts[rnd() % (sizeof dsts / 2)]);
	put16(m + 4, (uint16_t)rr(-960, 960));
	put16(m + 6, (rnd() & 7) ? 0 : (rnd() & 1) ? 0x0002 : 0x0100);
	put16(m + 8, 0);
	*n += 10;
}
static void mod_copy(uint8_t *ck, size_t *n, size_t from)
{
	memcpy(ck + *n, ck + from, 10);
	put16(ck + *n + 4, (uint16_t)rr(-500, 500));
	*n += 10;
}

/* générateur additif au hasard (valeurs sûres), niveau instrument (1) ou preset (0) */
static void rnd_gen(uint8_t *ck, size_t *n, int inst)
{
	int op, v;
	switch (rnd() % 28) {
	case 0: op = 5, v = rr(-600, 600); break;
	case 1: op = 6, v = rr(-300, 300); break;
	case 2: op = 7, v = rr(-1200, 1200); break;
	case 3: op = 8, v = (rnd() & 3) ? rr(1500, 13500) : rr(-2000, 16000); break;
	case 4: op = 9, v = rr(-100, 960); break;
	case 5: op = 10, v = rr(-2400, 2400); break;
	case 6: op = 11, v = rr(-4800, 4800); break;
	case 7: op = 13, v = rr(-200, 200); break;
	case 8: op = 15, v = rr(-200, 1000); break;
	case 9: op = 16, v = rr(-200, 1000); break;
	case 10: op = 17, v = rr(-600, 600); break;
	case 11: op = 21 + (rnd() & 2), v = rr(-12000, 1500); break;
	case 12: op = 22 + (rnd() & 2), v = rr(-3000, 2500); break;
	case 13: op = 25 + rnd() % 4, v = rr(-12000, 2500); break;
	case 14: op = 29, v = rr(-100, 1100); break;
	case 15: op = 30, v = rr(-12000, 2000); break;
	case 16: op = 31 + (rnd() & 1), v = rr(-120, 120); break;
	case 17: op = 33 + rnd() % 4, v = rr(-12000, 2000); break;
	case 18: op = 37, v = rr(-100, 1500); break;
	case 19: op = 38, v = rr(-12000, 1500); break;
	case 20: op = 39 + (rnd() & 1), v = rr(-120, 120); break;
	case 21: op = 48, v = rr(-100, 960); break;
	case 22: op = 51, v = rr(-12, 12); break;
	case 23: op = 52, v = rr(-99, 99); break;
	case 24: op = inst ? 54 : 52, v = inst ? rr(0, 3) : rr(-50, 50); break;
	case 25: op = inst ? 56 : 51, v = inst ? rr(0, 150) : rr(-5, 5); break;
	case 26: op = 57, v = rr(0, 3); break;
	default:
		if (inst) {
			switch (rnd() % 6) {
			case 0: op = 46, v = (rnd() & 1) ? -1 : rr(36, 96); break;
			case 1: op = 47, v = (rnd() & 3) ? rr(1, 127) : (rnd() & 1) ? rr(128, 900) : -1; break;
			case 2: op = 58, v = (rnd() & 1) ? -1 : rr(50, 72); break;
			case 3: op = rr(0, 3), v = rr(0, 40) * (op ? -1 : 1); break;
			case 4: op = 2 + (rnd() & 1), v = rr(-40, 40); break;
			default: op = 60, v = rr(0, 100); /* (rien : générateur sans effet) */
			}
		} else
			op = 13, v = rr(-50, 50);
	}
	gen(ck, n, op, v);
}

static size_t chunk(uint8_t *b, const char *id, const void *d, size_t n)
{
	memcpy(b, id, 4);
	put32(b + 4, n);
	memcpy(b + 8, d, n);
	size_t o = 8 + n;
	if (n & 1)
		b[o++] = 0;
	return o;
}

/* fabrique la SoundFont (graine) dans sfmem -> octets */
static size_t mksf(uint32_t seed, int nomods)
{
	rng = seed;
	n_ph = n_pb = n_pm = n_pg = n_in = n_ib = n_im = n_ig = n_sh = 0;
	nsmp = 0;
	/* échantillons */
	int ns = rr(2, 5);
	for (int s = 0; s < ns; s++) {
		int len = rr(6000, 14000), ls = rr(100, 500), le = ls + rr(4000, len - 600 - ls);
		size_t st = nsmp;
		int kind = rnd() % 4, per = rr(40, 200);
		uint32_t ph = 0, inc = 0xFFFFFFFFu / per;
		for (int i = 0; i < len; i++) {
			int v;
			ph += inc;
			if (kind == 0)
				v = (int)(ph >> 17) - 16384; /* scie */
			else if (kind == 1)
				v = (ph >> 31) ? 12000 : -12000;
			else if (kind == 2)
				v = (int)(rnd() & 0x7FFF) - 16384;
			else {
				int t = (int)(ph >> 16) - 32768;
				v = t < 0 ? -t - 16384 : t - 16384; /* triangle */
			}
			smp[nsmp++] = (int16_t)(v * (len - i) / len);
		}
		for (int i = 0; i < 46; i++)
			smp[nsmp++] = 0;
		uint8_t *h = ck_sh + n_sh;
		memset(h, 0, 46);
		snprintf((char *)h, 20, "S%d", s);
		put32(h + 20, st);
		put32(h + 24, st + len);
		put32(h + 28, st + ls);
		put32(h + 32, st + le);
		static const uint32_t rates[] = {44100, 44100, 32000, 48000, 22050};
		put32(h + 36, rates[rnd() % 5]);
		h[40] = rr(50, 72);
		h[41] = (uint8_t)rr(-50, 50);
		put16(h + 44, 1);
		n_sh += 46;
	}
	memset(ck_sh + n_sh, 0, 46);
	memcpy(ck_sh + n_sh, "EOS", 3);
	n_sh += 46;
	/* instruments */
	int ni = rr(2, 6);
	for (int i = 0; i < ni; i++) {
		uint8_t *h = ck_in + n_in;
		memset(h, 0, 22);
		snprintf((char *)h, 20, "I%d", i);
		put16(h + 20, n_ib / 4);
		n_in += 22;
		int nz = rr(1, 4), glob = rnd() & 1;
		for (int z = 0; z < nz + glob; z++) {
			put16(ck_ib + n_ib, n_ig / 4);
			put16(ck_ib + n_ib + 2, n_im / 10);
			n_ib += 4;
			if (!nomods) {
				size_t m0 = n_im;
				int nm = rnd() % 4;
				for (int k = 0; k < nm; k++)
					mod(ck_im, &n_im);
				if (nm && (rnd() & 1))
					mod_copy(ck_im, &n_im, m0);
			}
			if (z < glob && (rnd() & 7) == 0)
				continue; /* zone globale vide */
			if (z >= glob && (rnd() & 1)) {
				int lo = rr(0, 90);
				rangegen(ck_ig, &n_ig, 43, lo, rr(lo, 127));
			}
			if (z >= glob && (rnd() & 3) == 0) {
				int lo = rr(0, 100);
				rangegen(ck_ig, &n_ig, 44, lo, rr(lo, 127));
			}
			int ng = rr(0, 10);
			for (int k = 0; k < ng; k++)
				rnd_gen(ck_ig, &n_ig, 1);
			if (z >= glob)
				gen(ck_ig, &n_ig, 53, (rnd() & 15) ? (int)(rnd() % ns) : ns + 3);
		}
	}
	memset(ck_in + n_in, 0, 22);
	memcpy(ck_in + n_in, "EOI", 3);
	put16(ck_in + n_in + 20, n_ib / 4);
	n_in += 22;
	put16(ck_ib + n_ib, n_ig / 4);
	put16(ck_ib + n_ib + 2, n_im / 10);
	n_ib += 4;
	memset(ck_im + n_im, 0, 10);
	n_im += 10;
	memset(ck_ig + n_ig, 0, 4);
	n_ig += 4;
	/* presets */
	static const int pbk[][2] = {{0, 0}, {0, 1}, {128, 0}, {0, 5}, {1, 0}, {0, 127}, {128, 16}, {0, 40}, {2, 1}};
	int np = rr(3, 9);
	for (int p = 0; p < np; p++) {
		uint8_t *h = ck_ph + n_ph;
		memset(h, 0, 38);
		snprintf((char *)h, 20, "Preset number %d....", p);
		put16(h + 20, pbk[p][1]);
		put16(h + 22, pbk[p][0]);
		put16(h + 24, n_pb / 4);
		n_ph += 38;
		int nz = rr(1, 3), glob = rnd() & 1;
		for (int z = 0; z < nz + glob; z++) {
			put16(ck_pb + n_pb, n_pg / 4);
			put16(ck_pb + n_pb + 2, n_pm / 10);
			n_pb += 4;
			if (!nomods) {
				size_t m0 = n_pm;
				int nm = rnd() % 3;
				for (int k = 0; k < nm; k++)
					mod(ck_pm, &n_pm);
				if (nm && (rnd() & 1))
					mod_copy(ck_pm, &n_pm, m0);
			}
			if (z >= glob && (rnd() & 1)) {
				int lo = rr(0, 80);
				rangegen(ck_pg, &n_pg, 43, lo, rr(lo, 127));
			}
			if (z >= glob && (rnd() & 7) == 0)
				rangegen(ck_pg, &n_pg, 44, 0, rr(30, 127));
			int ng = rr(0, 4);
			for (int k = 0; k < ng; k++)
				rnd_gen(ck_pg, &n_pg, 0);
			if (z >= glob)
				gen(ck_pg, &n_pg, 41, rnd() % ni);
		}
	}
	memset(ck_ph + n_ph, 0, 38);
	memcpy(ck_ph + n_ph, "EOP", 3);
	put16(ck_ph + n_ph + 24, n_pb / 4);
	n_ph += 38;
	put16(ck_pb + n_pb, n_pg / 4);
	put16(ck_pb + n_pb + 2, n_pm / 10);
	n_pb += 4;
	memset(ck_pm + n_pm, 0, 10);
	n_pm += 10;
	memset(ck_pg + n_pg, 0, 4);
	n_pg += 4;
	/* le fichier */
	if (sflen)
		memset(sfmem, 0, sflen + 64);
	static uint8_t tmp[1 << 19];
	uint8_t *b = sfmem;
	size_t o = 12, l0, n;
	memcpy(b, "RIFF", 4);
	memcpy(b + 8, "sfbk", 4);
	/* INFO */
	l0 = o;
	memcpy(b + o, "LIST", 4);
	memcpy(b + o + 8, "INFO", 4);
	o += 12;
	uint8_t ifil[4] = {2, 0, 4, 0};
	o += chunk(b + o, "ifil", ifil, 4);
	char nm[80];
	n = snprintf(nm, sizeof nm, "Banc %u : une SoundFont fabriquee pour le port C d'AudioKit, assez longue",
		     seed);
	o += chunk(b + o, "INAM", nm, (seed & 1) ? n + 1 : 9);
	put32(b + l0 + 4, o - l0 - 8);
	/* sdta */
	l0 = o;
	memcpy(b + o, "LIST", 4);
	memcpy(b + o + 8, "sdta", 4);
	o += 12;
	o += chunk(b + o, "smpl", smp, nsmp * 2);
	put32(b + l0 + 4, o - l0 - 8);
	/* un morceau inconnu entre les listes */
	memcpy(tmp, "junkjunk", 8);
	o += chunk(b + o, "JUNK", tmp, 7);
	/* pdta */
	l0 = o;
	memcpy(b + o, "LIST", 4);
	memcpy(b + o + 8, "pdta", 4);
	o += 12;
	o += chunk(b + o, "phdr", ck_ph, n_ph);
	o += chunk(b + o, "pbag", ck_pb, n_pb);
	if (nomods != 2)
		o += chunk(b + o, "pmod", ck_pm, n_pm);
	o += chunk(b + o, "pgen", ck_pg, n_pg);
	o += chunk(b + o, "inst", ck_in, n_in);
	o += chunk(b + o, "ibag", ck_ib, n_ib);
	if (nomods != 2)
		o += chunk(b + o, "imod", ck_im, n_im);
	o += chunk(b + o, "igen", ck_ig, n_ig);
	o += chunk(b + o, "shdr", ck_sh, n_sh);
	put32(b + l0 + 4, o - l0 - 8);
	put32(b + 4, o - 8);
	sflen = o;
	return o;
}

/* ---- scénario MIDI au hasard sur un synthé */
static const int blks[] = {1, 64, 7, 256, 1000, 63, 65, 3, 129, 2, 441, 128, 4410};

struct scn {
	int sf;	     /* 1 : SoundFont (valeurs plus sages) */
	int notepct; /* part des notes */
	int keys[16][8];
};

static void events(int ref, long s, const char *tag, int step, struct scn *c)
{
	char nm[96];
	int ne = rnd() % 5;
	for (int e = 0; e < ne; e++) {
		long ch = rnd() % 16, r = 0;
		uint32_t a = rnd() % 100;
		snprintf(nm, sizeof nm, "%s.%d.%d", tag, step, e);
		if ((rnd() & 3) == 0)
			ch = 9;
		if (a < (uint32_t)c->notepct) {
			int k = c->sf ? rr(36, 96) : rr(0, 127);
			if ((rnd() & 7) == 0)
				k = rr(30, 85); /* percussions surtout */
			c->keys[ch][rnd() & 7] = k;
			long cmd = 0x90 | (rnd() & 15);
			long d1 = k | ((rnd() & 1) << 7);
			long d2 = rr(1, 127);
			r = AKCALL(ref, ak_synth_midi, s, ch, cmd, d1, d2);
		} else if (a < 55) {
			int k = c->keys[ch][rnd() & 7];
			long cmd = (rnd() & 1) ? 0x80 : 0x90;
			long d2 = (rnd() & 1) ? 0 : 64;
			r = AKCALL(ref, ak_synth_midi, s, ch, cmd, k, d2);
		} else if (a < 80) {
			static const int ccs[] = {1, 7, 10, 11, 64, 64, 0, 6, 38, 91, 93, 100, 101, 120, 121, 123, 2, 32,
						  126, 7, 10, 11, 1, 91, 93, 64};
			int cc = ccs[rnd() % (sizeof ccs / sizeof ccs[0])];
			int v = rr(0, 127);
			if (cc == 6 && c->sf)
				v = rr(0, 12);
			if ((cc == 100 || cc == 101) && (rnd() & 1))
				v = 0;
			if (cc == 64)
				v = (rnd() & 1) ? 127 : rr(0, 127);
			r = AKCALL(ref, ak_synth_midi, s, ch, 0xB0, cc | ((rnd() & 3) == 0 ? 0x80 : 0), v);
		} else if (a < 87)
		{
			long d1 = rr(0, 127);
			long d2 = rnd();
			r = AKCALL(ref, ak_synth_midi, s, ch, 0xC0, d1, d2);
		}
		else if (a < 95) {
			long lo = rnd() & 127, hi = rnd() & 127;
			if ((rnd() & 7) == 0)
				lo = 0, hi = (rnd() & 1) ? 0 : 127;
			r = AKCALL(ref, ak_synth_midi, s, ch, 0xE0, lo, hi);
		} else if (a < 97)
			r = AKCALL(ref, ak_synth_midi, s, (rnd() & 1) ? ch + 16 : -1, 0x90, 60, 100);
		else if (a < 98)
			r = AKCALL(ref, ak_synth_midi, s, ch, (rnd() & 1) ? 0xA0 : (rnd() & 1) ? 0xD0 : 0xF0, 60, 1);
		else
			r = AKCALL(ref, ak_synth_voices, s);
		OUTV(nm, r);
	}
}

static void scenario(int ref, const char *tag, long sf, long flags, uint32_t seed, int secs, int notepct, int issf)
{
	char nm[96];
	struct scn c;
	memset(&c, 0, sizeof c);
	c.sf = issf;
	c.notepct = notepct;
	long s = AKCALL(ref, ak_synth_new, sf, flags);
	snprintf(nm, sizeof nm, "%s.new", tag);
	OUTV(nm, s < 0 ? s : 1);
	if (s < 0)
		return;
	dump_sy(nm, s);
	rng = seed;
	long total = (long)secs * 44100, done = 0;
	int bi = 0, step = 0;
	int16_t *buf = malloc((4410 + 8) * 4);
	float *fl = malloc((4410 + 8) * 4), *fr = malloc((4410 + 8) * 4);
	while (done < total) {
		events(ref, s, tag, step, &c);
		long bn = blks[bi++ % (int)(sizeof blks / sizeof blks[0])];
		if (bn > total - done)
			bn = total - done;
		snprintf(nm, sizeof nm, "%s.r%d", tag, step);
		if (step % 3 == 2) {
			memset(fl, 0x7F, (bn + 4) * 4);
			memset(fr, 0x7F, (bn + 4) * 4);
			AKCALL(ref, ak_synth_render_f32, s, fl, fr, bn);
			OUT(nm, fl, (bn + 4) * 4);
			OUT(nm, fr, (bn + 4) * 4);
		} else {
			memset(buf, 0xC3, (bn + 4) * 4);
			AKCALL(ref, ak_synth_render, s, buf, bn);
			OUT(nm, buf, (bn + 4) * 4);
		}
		done += bn;
		if ((step & 31) == 0) {
			dump_sy(nm, s);
			OUTV(nm, AKCALL(ref, ak_synth_voices, s));
		}
		if ((rnd() % 400) == 0) {
			snprintf(nm, sizeof nm, "%s.reset%d", tag, step);
			OUTV(nm, AKCALL(ref, ak_synth_reset, s));
			dump_sy(nm, s);
		}
		step++;
	}
	/* fin : tout relâcher (CC 123 puis pédale), rendre la queue */
	for (int ch = 0; ch < 16; ch++) {
		AKCALL(ref, ak_synth_midi, s, ch, 0xB0, 64, 0);
		AKCALL(ref, ak_synth_midi, s, ch, 0xB0, 123, 0);
	}
	for (int i = 0; i < 20; i++) {
		AKCALL(ref, ak_synth_render, s, buf, 4410);
		snprintf(nm, sizeof nm, "%s.end%d", tag, i);
		OUT(nm, buf, 4410 * 4);
		OUTV(nm, AKCALL(ref, ak_synth_voices, s));
	}
	snprintf(nm, sizeof nm, "%s.fin", tag);
	dump_sy(nm, s);
	OUTV(nm, AKCALL(ref, ak_synth_reset, s));
	OUTV(nm, AKCALL(ref, ak_synth_voices, s));
	OUTV(nm, AKCALL(ref, ak_synth_free, s));
	free(buf);
	free(fl);
	free(fr);
}

/* ---- banque FM */
static void t_fm(int ref)
{
	OUTV("free0", AKCALL(ref, ak_synth_free, 0));
	scenario(ref, "fm0", 0, 0, 11, 6, 40, 0);
	scenario(ref, "fmfm", 0, AKS_FM, 12, 5, 55, 0);
	scenario(ref, "fmnofx", 0, AKS_NOFX, 13, 4, 30, 0);
	scenario(ref, "fm3", 0, AKS_FM | AKS_NOFX, 14, 5, 60, 0);
	/* chaque programme, chaque percussion : une note courte */
	long s = AKCALL(ref, ak_synth_new, 0, 0);
	int16_t *buf = malloc(2048 * 4);
	char nm[64];
	uint32_t crc = 0;
	for (int p = 0; p < 128; p++) {
		AKCALL(ref, ak_synth_midi, s, p & 7, 0xC0, p, 0);
		AKCALL(ref, ak_synth_midi, s, p & 7, 0x90, 40 + (p % 50), 100);
		AKCALL(ref, ak_synth_render, s, buf, 1500);
		crc = crc32(buf, 1500 * 4) ^ (crc * 31);
		AKCALL(ref, ak_synth_midi, s, p & 7, 0x80, 40 + (p % 50), 0);
		AKCALL(ref, ak_synth_render, s, buf, 300);
		crc = crc32(buf, 300 * 4) ^ (crc * 31);
		if ((p & 15) == 15) {
			snprintf(nm, sizeof nm, "prog%d", p);
			OUTV(nm, crc);
			dump_sy(nm, s);
		}
	}
	for (int k = 30; k < 90; k++) {
		AKCALL(ref, ak_synth_midi, s, 9, 0x90, k, 30 + k);
		AKCALL(ref, ak_synth_render, s, buf, 700);
		crc = crc32(buf, 700 * 4) ^ (crc * 31);
		if ((k & 7) == 7) {
			snprintf(nm, sizeof nm, "drum%d", k);
			OUTV(nm, crc);
			dump_sy(nm, s);
		}
	}
	/* RPN de l'étendue de la molette, molette aux bouts */
	AKCALL(ref, ak_synth_midi, s, 3, 0x90, 60, 127);
	for (int i = 0; i < 40; i++) {
		AKCALL(ref, ak_synth_midi, s, 3, 0xB0, 101, (i & 4) ? 1 : 0);
		AKCALL(ref, ak_synth_midi, s, 3, 0xB0, 100, (i & 8) ? 5 : 0);
		AKCALL(ref, ak_synth_midi, s, 3, 0xB0, 6, i * 3);
		AKCALL(ref, ak_synth_midi, s, 3, 0xB0, 38, i * 7);
		AKCALL(ref, ak_synth_midi, s, 3, 0xE0, (i & 1) ? 127 : 0, (i & 2) ? 127 : 0);
		AKCALL(ref, ak_synth_render, s, buf, 200);
		snprintf(nm, sizeof nm, "rpn%d", i);
		OUT(nm, buf, 200 * 4);
		OUT(nm, (uint8_t *)s + SY_CHAN + 3 * SC_SIZE, SC_SIZE);
	}
	free(buf);
	AKCALL(ref, ak_synth_free, s);
}

/* ---- SoundFont : lecture, presets, rendu */
static void sf_info(int ref, const char *tag, long sf)
{
	char nm[96];
	snprintf(nm, sizeof nm, "%s.open", tag);
	OUTV(nm, sf < 0 ? sf : 1);
	if (sf < 0)
		return;
	{ /* les pointeurs : décalages dans le tampon (son adresse change d'un programme à l'autre) */
		uint64_t q[SF_BYTES / 8];
		memcpy(q, (void *)sf, SF_BYTES);
		static const int pi[] = {2, 4, 6, 7, 8, 9, 10, 11, 21, 22};
		for (unsigned k = 0; k < sizeof pi / sizeof pi[0]; k++)
			if (q[pi[k]])
				q[pi[k]] -= (uint64_t)(uintptr_t)sfmem;
		OUT(nm, q, SF_BYTES);
	}
	OUT(nm, (char *)AKCALL(ref, ak_sf_name, sf), 64);
	long np = AKCALL(ref, ak_sf_presets, sf);
	OUTV(nm, np);
	for (long i = -1; i < 12; i++) {
		uint8_t p[AKSP_SIZE + 8];
		memset(p, 0xEE, sizeof p);
		snprintf(nm, sizeof nm, "%s.preset%ld", tag, i);
		long r = AKCALL(ref, ak_sf_preset, sf, i, p);
		OUTV(nm, r);
		OUT(nm, p, sizeof p);
	}
}

static void t_sf(int ref)
{
	char tag[64];
	for (uint32_t sd = 1; sd <= 14; sd++) {
		size_t n = mksf(1000 + sd, sd % 5 == 0 ? 1 : sd % 7 == 0 ? 2 : 0);
		snprintf(tag, sizeof tag, "sf%u", sd);
		long sf = AKCALL(ref, ak_sf_open_mem, sfmem, n);
		sf_info(ref, tag, sf);
		if (sf < 0)
			continue;
		long fl = sd % 3 == 0 ? AKS_NOFX : 0;
		int secs = sd <= 4 ? 4 : 2;
		scenario(ref, tag, sf, fl, 77 + sd, secs, 45, 1);
		if (sd == 2) {
			snprintf(tag, sizeof tag, "sf%u.fm", sd);
			scenario(ref, tag, sf, AKS_FM, 5, 1, 45, 0);
		}
		/* chaque preset : des notes tenues, la pédale, le relâchement */
		long s = AKCALL(ref, ak_synth_new, sf, (sd & 1) ? 0 : AKS_NOFX);
		int16_t *buf = malloc(4096 * 4);
		uint32_t crc = 0;
		char nm[96];
		for (int p = 0; p < 10; p++) {
			int ch = p == 9 ? 9 : p & 7;
			static const int pr[] = {0, 1, 5, 127, 40, 0, 1, 16, 3, 0};
			AKCALL(ref, ak_synth_midi, s, ch, 0xB0, 0, p == 4 ? 1 : p == 8 ? 2 : 0);
			AKCALL(ref, ak_synth_midi, s, ch, 0xC0, pr[p], 0);
			AKCALL(ref, ak_synth_midi, s, ch, 0xB0, 64, 127);
			for (int k = 0; k < 6; k++)
				AKCALL(ref, ak_synth_midi, s, ch, 0x90, 40 + 9 * k + p, 20 + 20 * k);
			AKCALL(ref, ak_synth_render, s, buf, 3000);
			crc = crc32(buf, 3000 * 4) ^ (crc * 31);
			for (int k = 0; k < 6; k++)
				AKCALL(ref, ak_synth_midi, s, ch, 0x80, 40 + 9 * k + p, 0);
			AKCALL(ref, ak_synth_render, s, buf, 1000);
			crc = crc32(buf, 1000 * 4) ^ (crc * 31);
			AKCALL(ref, ak_synth_midi, s, ch, 0xB0, 64, 0);
			AKCALL(ref, ak_synth_render, s, buf, 4000);
			crc = crc32(buf, 4000 * 4) ^ (crc * 31);
			snprintf(nm, sizeof nm, "%s.p%d", tag, p);
			OUTV(nm, crc);
			OUTV(nm, AKCALL(ref, ak_synth_voices, s));
			dump_sy(nm, s);
		}
		/* CC 120 au milieu de notes */
		for (int k = 0; k < 70; k++)
			AKCALL(ref, ak_synth_midi, s, k & 3, 0x90, 36 + k % 60, 100);
		AKCALL(ref, ak_synth_render, s, buf, 500);
		snprintf(nm, sizeof nm, "%s.full", tag);
		OUT(nm, buf, 500 * 4);
		OUTV(nm, AKCALL(ref, ak_synth_voices, s));
		AKCALL(ref, ak_synth_midi, s, 1, 0xB0, 120, 0);
		AKCALL(ref, ak_synth_render, s, buf, 500);
		OUT(nm, buf, 500 * 4);
		OUTV(nm, AKCALL(ref, ak_synth_voices, s));
		dump_sy(nm, s);
		free(buf);
		AKCALL(ref, ak_synth_free, s);
		ak_free((void *)sf); /* (la SoundFont : un bloc d'ak_alloc, des deux côtés) */
	}
}

/* ---- SoundFonts abîmées */
static void play_short(int ref, const char *tag, long sf)
{
	char nm[96];
	long s = AKCALL(ref, ak_synth_new, sf, 0);
	if (s < 0)
		return;
	int16_t buf[256 * 2];
	uint32_t crc = 0;
	for (int i = 0; i < 6; i++) {
		AKCALL(ref, ak_synth_midi, s, i == 5 ? 9 : i, 0xC0, i, 0);
		AKCALL(ref, ak_synth_midi, s, i == 5 ? 9 : i, 0x90, 40 + 7 * i, 100);
	}
	for (int i = 0; i < 6; i++) {
		AKCALL(ref, ak_synth_render, s, buf, 256);
		crc = crc32(buf, sizeof buf) ^ (crc * 31);
	}
	snprintf(nm, sizeof nm, "%s.play", tag);
	OUTV(nm, crc);
	OUTV(nm, AKCALL(ref, ak_synth_voices, s));
	dump_sy(nm, s);
	AKCALL(ref, ak_synth_free, s);
}

static long find_ck(const char *id)
{
	for (size_t o = 12; o + 8 < sflen; o++)
		if (!memcmp(sfmem + o, id, 4))
			return (long)o;
	return -1;
}

static void bad_one(int ref, const char *tag, size_t n, int play)
{
	long sf = AKCALL(ref, ak_sf_open_mem, sfmem, n);
	sf_info(ref, tag, sf);
	if (sf < 0)
		return;
	long np = (long)((struct aksf *)sf)->nphdr;
	if (play && np >= 0 && np < 100000)
		play_short(ref, tag, sf);
	ak_free((void *)sf);
}

static void t_sfbad(int ref)
{
	char tag[64];
	static uint8_t orig[1 << 20];
	size_t n = mksf(4242, 0);
	memcpy(orig, sfmem, n);
	/* en-têtes */
	for (size_t k = 0; k < 14; k++) {
		snprintf(tag, sizeof tag, "short%zu", k);
		bad_one(ref, tag, k, 1);
	}
	memcpy(sfmem, "RIFX", 4);
	bad_one(ref, "riff", n, 1);
	memcpy(sfmem, orig, 12);
	memcpy(sfmem + 8, "sfbK", 4);
	bad_one(ref, "sfbk", n, 1);
	memcpy(sfmem, orig, 12);
	/* morceaux absents (nom changé) */
	static const char *ids[] = {"smpl", "phdr", "pbag", "pmod", "pgen", "inst", "ibag", "imod", "igen", "shdr",
				    "INAM", "sdta", "pdta", "INFO"};
	for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) {
		long o = find_ck(ids[i]);
		if (o < 0)
			continue;
		sfmem[o + 1] ^= 0x20;
		snprintf(tag, sizeof tag, "no%s", ids[i]);
		bad_one(ref, tag, n, i == 3 || i == 7 ? 0 : 1); /* (sans pmod / imod : l'asm lirait à 0) */
		memcpy(sfmem, orig, n);
	}
	/* tailles : phdr < 38 (nphdr = -1), shdr vide, tailles impaires, LIST trop longue */
	long o = find_ck("phdr");
	put32(sfmem + o + 4, 20);
	bad_one(ref, "phdr20", n, 0);
	put32(sfmem + o + 4, 38);
	bad_one(ref, "phdr38", n, 1);
	put32(sfmem + o + 4, 38 * 3 + 5);
	bad_one(ref, "phdr3", n, 1);
	memcpy(sfmem, orig, n);
	o = find_ck("shdr");
	put32(sfmem + o + 4, 0);
	bad_one(ref, "shdr0", n, 1);
	put32(sfmem + o + 4, 45);
	bad_one(ref, "shdr45", n, 1);
	memcpy(sfmem, orig, n);
	o = find_ck("smpl");
	put32(sfmem + o + 4, *(uint32_t *)(sfmem + o + 4) - 3);
	bad_one(ref, "smplodd", n, 1);
	memcpy(sfmem, orig, n);
	o = find_ck("pdta") - 8;
	put32(sfmem + o + 4, (uint32_t)(n - o) + 5000);
	bad_one(ref, "listlong", n, 1);
	put32(sfmem + o + 4, 2);
	bad_one(ref, "list2", n, 1);
	memcpy(sfmem, orig, n);
	o = find_ck("INAM");
	put32(sfmem + o + 4, 200);
	bad_one(ref, "inam200", n, 1);
	memcpy(sfmem, orig, n);
	/* troncatures */
	for (size_t k = 12; k < n; k += 1 + n / 41) {
		snprintf(tag, sizeof tag, "trunc%zu", k);
		bad_one(ref, tag, k, 1);
	}
	for (size_t k = n - 120; k < n; k += 9) { /* dans shdr : la lecture réussit */
		snprintf(tag, sizeof tag, "trunc%zu", k);
		bad_one(ref, tag, k, 1);
	}
	/* index des zones au hasard (dans leurs tables) ; programmes, banques */
	rng = 99;
	long oph = find_ck("phdr") + 8, opb = find_ck("pbag") + 8, oin = find_ck("inst") + 8, oib = find_ck("ibag") + 8;
	uint32_t nph = *(uint32_t *)(sfmem + oph - 4) / 38, npb = *(uint32_t *)(sfmem + opb - 4) / 4;
	uint32_t nin = *(uint32_t *)(sfmem + oin - 4) / 22, nib = *(uint32_t *)(sfmem + oib - 4) / 4;
	uint32_t npg = (uint32_t)(n_pg / 4), npm = (uint32_t)(n_pm / 10), nig = (uint32_t)(n_ig / 4),
		 nim = (uint32_t)(n_im / 10);
	for (int it = 0; it < 40; it++) {
		int nmut = rr(1, 4);
		for (int k = 0; k < nmut; k++) {
			/* (un appel de rnd() par instruction : l'ordre des arguments
			 * n'est pas le même sur les deux architectures) */
			uint32_t w = rnd() % 7, a = rnd(), b = rnd(), c = rnd();
			switch (w) {
			case 0:
				put16(sfmem + oph + 38 * (a % nph) + 24, b % npb);
				break;
			case 1:
				put16(sfmem + oph + 38 * (a % nph) + 20 + 2 * (c & 1), b % 130);
				break;
			case 2:
				put16(sfmem + opb + 4 * (a % npb), b % (npg + 1));
				break;
			case 3:
				put16(sfmem + opb + 4 * (a % npb) + 2, (c & 7) ? b % (npm + 1) : 65535);
				break;
			case 4:
				put16(sfmem + oin + 22 * (a % nin) + 20, b % nib);
				break;
			case 5:
				put16(sfmem + oib + 4 * (a % nib), b % (nig + 1));
				break;
			default:
				put16(sfmem + oib + 4 * (a % nib) + 2, (c & 7) ? b % (nim + 1) : 60000);
			}
		}
		snprintf(tag, sizeof tag, "mut%d", it);
		bad_one(ref, tag, n, 1);
		memcpy(sfmem, orig, n);
	}
}

/* ---- fichiers : .mid fabriqués, .FMS */
static size_t varlen(uint8_t *b, uint32_t v)
{
	uint8_t t[5];
	int n = 0;
	do {
		t[n++] = v & 127;
		v >>= 7;
	} while (v);
	for (int i = 0; i < n; i++)
		b[i] = t[n - 1 - i] | (i < n - 1 ? 0x80 : 0);
	return n;
}

/* .mid au hasard : format, pistes, division, secondes ; bad : abîmé */
static size_t mkmid(uint8_t *b, uint32_t seed, int fmt, int ntrk, int div, int secs, int bad)
{
	rng = seed;
	size_t o = 0;
	memcpy(b, "MThd", 4);
	int hl = (bad == 1) ? 8 : 6;
	b[4] = 0, b[5] = 0, b[6] = 0, b[7] = hl;
	b[8] = 0, b[9] = fmt;
	b[10] = ntrk >> 8, b[11] = ntrk;
	b[12] = div >> 8, b[13] = div;
	o = 8 + hl;
	if (hl > 6)
		b[14] = 0x55, b[15] = 0xAA;
	int tps = (div & 0x8000) ? (-(int8_t)(div >> 8)) * (div & 255) / 2 * 2 : div * 2; /* pulsations / s (à peu près) */
	if (tps <= 0)
		tps = 96;
	for (int t = 0; t < ntrk - (bad == 2); t++) {
		size_t h = o;
		memcpy(b + o, "MTrk", 4);
		o += 8;
		uint32_t tick = 0, end = (uint32_t)(secs * tps);
		int run = -1;
		if (bad == 3 && t == 0)
			b[o++] = 0, b[o++] = 60, b[o++] = 100; /* données sans statut */
		while (tick < end) {
			uint32_t d = (rnd() & 3) ? rnd() % (tps / 4 + 1) : 0;
			if ((rnd() & 63) == 0)
				d = (uint32_t)tps; /* délai long */
			tick += d;
			o += varlen(b + o, d);
			uint32_t a = rnd() % 100;
			int ch = (fmt == 0 || ntrk == 1) ? (int)(rnd() % 16) : (t * 3 + (int)(rnd() % 3)) & 15;
			if ((rnd() & 7) == 0)
				ch = 9;
			int st;
			if (a < 50) {
				st = ((rnd() & 3) ? 0x90 : 0x80) | ch;
				if (st == run && (rnd() & 1)) {
				} else
					b[o++] = st;
				run = st;
				b[o++] = rr(24, 100);
				b[o++] = (rnd() & 3) ? rr(1, 127) : 0;
			} else if (a < 68) {
				static const int ccs[] = {7, 10, 11, 1, 64, 91, 93, 0, 100, 101, 6, 38, 121, 123, 120};
				st = 0xB0 | ch;
				if (st != run || (rnd() & 1))
					b[o++] = st;
				run = st;
				b[o++] = ccs[rnd() % (sizeof ccs / sizeof ccs[0])];
				b[o++] = rnd() & 127;
			} else if (a < 74) {
				st = 0xC0 | ch;
				b[o++] = st;
				run = st;
				b[o++] = rnd() & 127;
			} else if (a < 80) {
				st = 0xE0 | ch;
				b[o++] = st;
				run = st;
				b[o++] = rnd() & 127;
				b[o++] = rnd() & 127;
			} else if (a < 83) {
				st = ((rnd() & 1) ? 0xA0 : 0xD0) | ch;
				b[o++] = st;
				run = st;
				b[o++] = rnd() & 127;
				if ((st & 0xF0) == 0xA0)
					b[o++] = rnd() & 127;
			} else if (a < 87) { /* sysex */
				b[o++] = (rnd() & 3) ? 0xF0 : 0xF7;
				int n = rr(0, 20);
				o += varlen(b + o, n);
				for (int i = 0; i < n; i++)
					b[o++] = rnd() & 127;
			} else if (a < 93) { /* méta : texte, signature, tempo */
				b[o++] = 0xFF;
				int ty = (rnd() & 1) ? 0x51 : (rnd() & 1) ? 0x01 : 0x58;
				b[o++] = ty;
				if (ty == 0x51) {
					int bl = (rnd() & 7) ? 3 : rr(0, 4);
					b[o++] = bl;
					uint32_t tp = (rnd() & 15) ? (uint32_t)rr(250000, 1000000) : 0;
					for (int i = 0; i < bl; i++)
						b[o++] = i < 3 ? tp >> (16 - 8 * i) : 7;
				} else {
					int n = rr(0, 30);
					o += varlen(b + o, n);
					for (int i = 0; i < n; i++)
						b[o++] = 'a' + rnd() % 26;
				}
			} else if (a < 95 && bad == 4) { /* octet de données perdu (après un méta) */
				b[o++] = 0xFF, b[o++] = 0x01, b[o++] = 0;
				o += varlen(b + o, 0);
				b[o++] = 0x40;
			} else { /* note */
				st = 0x90 | ch;
				b[o++] = st;
				run = st;
				b[o++] = rr(30, 90);
				b[o++] = rr(60, 127);
			}
		}
		if (!(bad == 5 && t == 0)) { /* fin de piste */
			o += varlen(b + o, rnd() % 50);
			b[o++] = 0xFF, b[o++] = 0x2F, b[o++] = 0;
		}
		uint32_t len = (uint32_t)(o - h - 8);
		if (bad == 6 && t == ntrk - 1)
			len += 100000; /* plus long que le fichier */
		b[h + 4] = len >> 24, b[h + 5] = len >> 16, b[h + 6] = len >> 8, b[h + 7] = len;
	}
	if (bad == 7)
		memcpy(b + 8 + hl, "XTrk", 4); /* nom de piste ignoré */
	return o;
}

static void decode_all(int ref, const char *name, const uint8_t *buf, size_t n, long cap)
{
	char nm[96];
	long d = AKCALL(ref, ak_dec_open_mem, buf, n);
	snprintf(nm, sizeof nm, "%s.open", name);
	OUTV(nm, d < 0 ? d : 1);
	if (d < 0)
		return;
	long info[8];
	AKCALL(ref, ak_dec_info, d, info);
	snprintf(nm, sizeof nm, "%s.info", name);
	OUT(nm, info, sizeof info);
	char tag[64];
	for (int t = 0; t < 4; t++) {
		memset(tag, 0x55, sizeof tag);
		long k = AKCALL(ref, ak_dec_tag, d, t, tag, sizeof tag);
		snprintf(nm, sizeof nm, "%s.tag%d", name, t);
		OUTV(nm, k);
		OUT(nm, tag, sizeof tag);
	}
	int16_t *pcm = malloc(8192 * 4 + 64);
	long tot = 0, chunk = 1;
	int i = 0;
	for (;;) {
		if (cap && tot >= cap)
			break;
		long r = AKCALL(ref, ak_dec_read, d, pcm, chunk);
		snprintf(nm, sizeof nm, "%s.rd%d", name, i++);
		if (r <= 0) {
			OUTV(nm, r);
			break;
		}
		OUTV(nm, crc32(pcm, r * 4));
		tot += r;
		chunk = chunk * 3 + 1;
		if (chunk > 8192)
			chunk = 7;
	}
	snprintf(nm, sizeof nm, "%s.tot", name);
	OUTV(nm, tot);
	/* recherche : début, milieu, avant la fin, au-delà, retour au début */
	long dur = info[AKI_DURATION / 8];
	long ms[] = {dur / 3, dur / 2, dur - 700, dur + 5000, 0, 1234, dur / 5};
	for (unsigned k = 0; k < sizeof ms / sizeof ms[0]; k++) {
		snprintf(nm, sizeof nm, "%s.seek%u", name, k);
		OUTV(nm, AKCALL(ref, ak_dec_seek, d, ms[k]));
		OUTV(nm, AKCALL(ref, ak_dec_tell, d));
		for (int j = 0; j < 4; j++) {
			long r = AKCALL(ref, ak_dec_read, d, pcm, 5000);
			OUTV(nm, r);
			if (r <= 0)
				break;
			OUTV(nm, crc32(pcm, r * 4));
		}
		AKCALL(ref, ak_dec_info, d, info);
		OUT(nm, info, sizeof info);
	}
	free(pcm);
	snprintf(nm, sizeof nm, "%s.close", name);
	OUTV(nm, AKCALL(ref, ak_dec_close, d));
}

static uint8_t midbuf[1 << 20];

static void t_mid(int ref)
{
	struct {
		int fmt, ntrk, div, secs, bad;
	} f[] = {
		{0, 1, 96, 5, 0},  {1, 4, 480, 4, 0},	{1, 3, 0xE728, 3, 0}, {2, 2, 192, 3, 0}, {1, 6, 120, 3, 0},
		{0, 1, 960, 3, 1}, {1, 3, 96, 2, 2},	{1, 2, 96, 2, 3},     {1, 2, 96, 2, 4},	 {1, 2, 96, 2, 5},
		{1, 2, 96, 2, 6},  {1, 2, 96, 2, 7},	{1, 16, 64, 2, 0},    {0, 1, 0xE250, 2, 0}, {1, 1, 0, 1, 0},
		{1, 0, 96, 1, 0},  {0, 1, 0x8000, 1, 0}, {0, 1, 0xFF03, 2, 0},
	};
	char nm[64];
	for (unsigned i = 0; i < sizeof f / sizeof f[0]; i++) {
		size_t n = mkmid(midbuf, 500 + i, f[i].fmt, f[i].ntrk, f[i].div, f[i].secs, f[i].bad);
		snprintf(nm, sizeof nm, "mid%u", i);
		decode_all(ref, nm, midbuf, n, 0);
	}
	/* troncatures d'un fichier */
	size_t n = mkmid(midbuf, 77, 1, 3, 96, 2, 0);
	static uint8_t cut[1 << 16];
	for (size_t k = 0; k < n; k += 1 + n / 23) {
		memcpy(cut, midbuf, k);
		memset(cut + k, 0, 64);
		snprintf(nm, sizeof nm, "cut%zu", k);
		decode_all(ref, nm, cut, k, 3 * 44100);
	}
	/* en-tête : longueur fausse, 64 Mo */
	n = mkmid(midbuf, 78, 0, 1, 96, 1, 0);
	midbuf[7] = 200;
	decode_all(ref, "hdrlen", midbuf, n, 0);
	midbuf[7] = 6;
	uint8_t *big = calloc(1, (1 << 26) + 64);
	memcpy(big, midbuf, n);
	decode_all(ref, "big", big, 1 << 26, 0);
	free(big);
}

static int cmpstr(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void t_fms(int ref)
{
	const char *dir = "src/apps/fmtracker/songs";
	DIR *dd = opendir(dir);
	char *names[256];
	int n = 0;
	struct dirent *e;
	while (dd && (e = readdir(dd)) && n < 256)
		if (strstr(e->d_name, ".FMS"))
			names[n++] = strdup(e->d_name);
	if (dd)
		closedir(dd);
	qsort(names, n, sizeof names[0], cmpstr);
	OUTV("nfms", n);
	for (int i = 0; i < n; i++) {
		char p[512];
		size_t sz;
		snprintf(p, sizeof p, "%s/%s", dir, names[i]);
		uint8_t *f = load(p, &sz);
		if (!f)
			continue;
		/* trois en entier, les autres : 4 s puis les recherches */
		int full = !strcmp(names[i], "SAMBA.FMS") || !strcmp(names[i], "THUNDER.FMS") ||
			   !strcmp(names[i], "SERPENT.FMS");
		decode_all(ref, names[i], f, sz, full ? 0 : 4 * 44100);
		if (i % 6 == 0) { /* abîmés : coupé, motifs */
			char nm[600];
			for (size_t k = 100; k < sz; k += sz / 5 + 1) {
				snprintf(nm, sizeof nm, "%s.cut%zu", names[i], k);
				decode_all(ref, nm, f, k, 44100);
			}
			uint8_t *g = malloc(sz + 64);
			memcpy(g, f, sz + 64);
			static const int16_t nps[] = {0, -1, 65, 64, 40};
			for (unsigned k = 0; k < sizeof nps / sizeof nps[0]; k++) {
				put16(g + 105, (uint16_t)nps[k]);
				snprintf(nm, sizeof nm, "%s.np%d", names[i], nps[k]);
				decode_all(ref, nm, g, sz, 44100);
			}
			memcpy(g, f, sz);
			put16(g + 107 + 272, 300); /* rangs du premier motif */
			snprintf(nm, sizeof nm, "%s.rows", names[i]);
			decode_all(ref, nm, g, sz, 44100);
			memcpy(g, f, sz);
			put16(g + 107 + 272 + 2, (uint16_t)-5); /* vitesse */
			rng = 31 + i;
			for (int k = 0; k < 20; k++)
				g[107 + 272 + 12 + k * 3] = rnd();
			snprintf(nm, sizeof nm, "%s.speed", names[i]);
			decode_all(ref, nm, g, sz, 2 * 44100);
			free(g);
		}
		free(f);
		free(names[i]);
	}
}

int main(void)
{
	sfmem = mmap(0, SFPAD, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	RUN(t_tab);
	RUN(t_fm);
	RUN(t_sf);
	RUN(t_sfbad);
	RUN(t_mid);
	RUN(t_fms);
	return cmp_end();
}
