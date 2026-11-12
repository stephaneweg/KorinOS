/* Banc différentiel : synthèse FM (ak_fm.c, ak_fmtab.c). */
#include <dirent.h>

#include "ak_fm.h"
#include "cmp.h"

static uint32_t rng;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}

/* instruments FM Song (26 octets) de src/apps/fmtracker/songs (.FMS) */
#define FMS_HEAD 107
#define FMS_INS 34
#define FMS_CH 8
static uint8_t fmsins[1024][26];
static int nfmsins;

static int cmpstr(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

static void load_fms(void)
{
	if (nfmsins)
		return;
	const char *dir = "src/apps/fmtracker/songs";
	DIR *d = opendir(dir);
	char *names[256];
	int n = 0;
	struct dirent *e;
	while (d && (e = readdir(d)) && n < 256)
		if (strstr(e->d_name, ".FMS"))
			names[n++] = strdup(e->d_name);
	if (d)
		closedir(d);
	qsort(names, n, sizeof names[0], cmpstr);
	for (int i = 0; i < n; i++) {
		char p[512];
		size_t sz;
		snprintf(p, sizeof p, "%s/%s", dir, names[i]);
		uint8_t *f = load(p, &sz);
		for (int k = 0; f && k < FMS_CH && nfmsins < 1024; k++) {
			size_t o = FMS_HEAD + FMS_INS * k + 8;
			if (o + 26 <= sz)
				memcpy(fmsins[nfmsins++], f + o, 26);
		}
		free(f);
		free(names[i]);
	}
	printf("(%d fichiers .FMS, %d instruments)\n", n, nfmsins);
}

static void dump(const char *nm, struct akfm *fm)
{
	OUT(nm, fm, AKFM_VOICES + fm->nv * FMV_SIZE);
}

/* ---- tables : les étiquettes de ak_fmtab.inc (incluse par ak_fm.inc) ne sont
 * pas dans la table du banc : on les retrouve après fm_fms_map (16 octets,
 * align 4), puis 2048 et 512 octets plus loin */
static void t_tab(int ref)
{
	const uint8_t *sin = (const uint8_t *)ak_sin1024, *ex = (const uint8_t *)ak_exp256, *ce = (const uint8_t *)ak_cents1200;
	if (ref) {
		uintptr_t a = (uintptr_t)refaddr(REF_fm_fms_map) + 16;
		sin = (const uint8_t *)((a + 3) & ~(uintptr_t)3);
		ex = sin + 2048;
		ce = ex + 512;
		OUT("mult2", refaddr(REF_fm_mult2), 16);
		OUT("fmsmap", refaddr(REF_fm_fms_map), 16);
	} else {
		static const uint8_t m2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};
		static const uint8_t fmap[16] = {0, 15, 2, 15, 4, 15, 6, 15, 8, 63, 10, 3, 16, 15, 20, 3};
		OUT("mult2", m2, 16);
		OUT("fmsmap", fmap, 16);
	}
	OUT("sin1024", sin, 2048);
	OUT("exp256", ex, 512);
	OUT("cents1200", ce, 4800);
}

/* ---- création, notes, cents */
static void t_misc(int ref)
{
	char nm[64];
	static const long nvs[] = {0, 1, 16, 64, 65, -1, 2, 1000};
	for (unsigned i = 0; i < sizeof nvs / sizeof nvs[0]; i++) {
		long fm = AKCALL(ref, ak_fm_new, nvs[i]);
		snprintf(nm, sizeof nm, "new%ld", nvs[i]);
		if (fm < 0) {
			OUTV(nm, fm);
			continue;
		}
		dump(nm, (struct akfm *)fm);
		OUTV(nm, AKCALL(ref, ak_fm_active, fm));
		OUTV(nm, AKCALL(ref, ak_fm_free, fm));
	}
	OUTV("free0", AKCALL(ref, ak_fm_free, 0));
	for (long k = -10; k < 140; k++) {
		snprintf(nm, sizeof nm, "note%ld", k);
		OUTV(nm, AKCALL(ref, ak_note_mhz, k));
	}
	OUTV("noteMIN", AKCALL(ref, ak_note_mhz, (long)0x8000000000000000));
	OUTV("noteMAX", AKCALL(ref, ak_note_mhz, (long)0x7FFFFFFFFFFFFFFF));
	for (long o = -3; o <= 11; o++)
		for (long n = -2; n <= 13; n++) {
			snprintf(nm, sizeof nm, "oct%ld.%ld", o, n);
			OUTV(nm, AKCALL(ref, ak_note_octave_mhz, n, o));
		}
	rng = 77;
	for (int i = 0; i < 4000; i++) {
		long c, b;
		switch (i & 3) {
		case 0:
			c = (long)(rnd() % 40000) - 20000;
			b = rnd() % 2000000;
			break;
		case 1:
			c = (long)(rnd() % 100000) - 50000;
			b = (long)rnd();
			b <<= rnd() % 40;
			break;
		case 2:
			c = (long)rnd() << 16;
			c = (c | rnd()) - (1L << 38);
			b = (long)rnd();
			b *= rnd();
			break;
		default:
			c = (long)(rnd() % 2400) - 1200;
			c -= (long)(rnd() % 3) * 1228800;
			b = -(long)rnd();
		}
		snprintf(nm, sizeof nm, "cents%d", i);
		OUTV(nm, AKCALL(ref, ak_cents_mul, c, b));
	}
	static const long cs[] = {-1228800, -1228801, -1228800 - 1199, 37200, 37201, 38400, 75600, 76799, 76800, 1L << 40};
	for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		snprintf(nm, sizeof nm, "centsx%u", i);
		OUTV(nm, AKCALL(ref, ak_cents_mul, cs[i], 440000));
		OUTV(nm, AKCALL(ref, ak_cents_mul, cs[i], 1));
	}
}

/* ---- instruments : ak_fm_from_fms, fm_op_set */
static void t_inst(int ref)
{
	char nm[64];
	uint8_t inst[AKFI_SIZE + 8];
	load_fms();
	for (int i = 0; i < nfmsins; i++) {
		memset(inst, 0xA5, sizeof inst);
		snprintf(nm, sizeof nm, "fms%d", i);
		OUTV(nm, AKCALL(ref, ak_fm_from_fms, inst, fmsins[i]));
		OUT(nm, inst, sizeof inst);
	}
	rng = 5;
	for (int i = 0; i < 2000; i++) {
		uint8_t b[26];
		for (int k = 0; k < 26; k++)
			b[k] = rnd() & ((i & 1) ? 0xFF : 0x01);
		memset(inst, i & 0xFF, sizeof inst);
		snprintf(nm, sizeof nm, "fmsr%d", i);
		OUTV(nm, AKCALL(ref, ak_fm_from_fms, inst, b));
		OUT(nm, inst, sizeof inst);
	}
	/* fm_op_set : tous les rangs, tous les octets */
	for (int i = 0; i < 4096; i++) {
		struct fop op;
		uint8_t par[AKFO_SIZE];
		memset(&op, 0x5A, sizeof op);
		for (int k = 0; k < AKFO_SIZE; k++)
			par[k] = rnd();
		if (i < 256) { /* rangs 0..15 sur attaque, déclin, relâchement, maintien */
			par[AKFO_ATTACK] = i & 15;
			par[AKFO_DECAY] = (i >> 4) & 15;
			par[AKFO_RELEASE] = i & 15;
			par[AKFO_SUSTAIN] = (i >> 4) | (i << 4);
		}
		snprintf(nm, sizeof nm, "opset%d", i);
		AKCALL(ref, fm_op_set, &op, par, i & 1);
		OUT(nm, &op, sizeof op);
	}
}

/* instrument au hasard (tous les champs, octets entiers) */
static void rnd_inst(uint8_t *inst)
{
	for (int k = 0; k < AKFI_SIZE; k++)
		inst[k] = rnd();
	switch (rnd() % 4) {
	case 0: /* attaque rapide, maintien : sonne fort */
		inst[AKFO_ATTACK] = 12 + rnd() % 4;
		inst[AKFI_OP1 + AKFO_ATTACK] = 10 + rnd() % 6;
		inst[AKFI_OP1 + AKFO_LEVEL] &= 0xC7;
		inst[AKFI_OP1 + AKFO_FLAGS] |= AKFF_SUSTAINED;
		break;
	case 1: /* relâchement rapide */
		inst[AKFO_RELEASE] = 8 + rnd() % 8;
		inst[AKFI_OP1 + AKFO_RELEASE] = 8 + rnd() % 8;
		inst[AKFI_OP1 + AKFO_LEVEL] &= 0xC3;
		break;
	default:
		break;
	}
}

static long freq(void)
{
	switch (rnd() % 8) {
	case 0:
		return (long)(rnd() % 30000000);
	case 1:
		return -(long)(rnd() % 1000);
	case 2:
		return (long)rnd() << 20;
	default:
		return 0; /* une note */
	}
}

static const int blks[] = {1, 7, 256, 1000, 63, 64, 65, 3, 129, 2, 4410};

/* ---- rendu : scénario au hasard (graine, voix, part de FM, secondes) */
static void scenario(int ref, const char *tag, uint32_t seed, long nv, int fmpct, int secs)
{
	char nm[96];
	load_fms();
	rng = seed;
	long fmh = AKCALL(ref, ak_fm_new, nv);
	if (fmh < 0) {
		OUTV(tag, fmh);
		return;
	}
	struct akfm *fm = (struct akfm *)fmh;
	long n = fm->nv;
	uint8_t inst[AKFI_SIZE];
	/* instruments sur une partie des voix */
	for (long v = 0; v < n; v++) {
		if ((long)(rnd() % 100) >= fmpct)
			continue;
		if (nfmsins && (rnd() & 1)) {
			memset(inst, 0, sizeof inst);
			AKCALL(ref, ak_fm_from_fms, inst, fmsins[rnd() % nfmsins]);
		} else
			rnd_inst(inst);
		snprintf(nm, sizeof nm, "%s.inst%ld", tag, v);
		OUTV(nm, AKCALL(ref, ak_fm_instrument, fm, v, inst));
	}
	snprintf(nm, sizeof nm, "%s.instbad", tag);
	OUTV(nm, AKCALL(ref, ak_fm_instrument, fm, n, inst));
	OUTV(nm, AKCALL(ref, ak_fm_instrument, fm, -1, inst));
	OUTV(nm, AKCALL(ref, ak_fm_instrument, fm, 0, 0));
	OUTV(nm, AKCALL(ref, ak_fm_start, fm, n, 440000, 0, 100));
	OUTV(nm, AKCALL(ref, ak_fm_pan, fm, n, 10));
	OUTV(nm, AKCALL(ref, ak_fm_pitch, fm, -2, 10));
	OUTV(nm, AKCALL(ref, ak_fm_gain, fm, n + 5, 10));
	OUTV(nm, AKCALL(ref, ak_fm_stop, fm, n + 3));
	dump(tag, fm);
	long total = (long)secs * 44100, done = 0;
	int bi = 0, step = 0;
	long energy = 0, nclip = 0, nfm = 0;
	int16_t *buf = malloc((4410 + 4) * 4);
	while (done < total) {
		/* quelques événements */
		int ne = rnd() % 4;
		for (int e = 0; e < ne; e++) {
			long v = rnd() % n;
			long r;
			uint32_t a = rnd() % 100;
			snprintf(nm, sizeof nm, "%s.%d.%d", tag, step, e);
			if (a < 30) {
				long f = freq();
				if (!f && (rnd() & 1))
					f = AKCALL(ref, ak_note_mhz, (long)(rnd() % 140) - 6);
				else if (!f) {
					long nt = rnd() % 12;
					f = AKCALL(ref, ak_note_octave_mhz, nt, (long)(rnd() % 10) - 1);
				}
				long w = rnd() % 9;
				if (w == 6 || w == 7)
					w = AKW_FM;
				else if (w == 8) /* au-delà de AKW_FM : carré */
					w = 6 + rnd() % 100;
				if ((rnd() & 15) == 0)
					w = -(long)(rnd() % 3) - 1;
				long vol = (long)(rnd() % 300) - 20;
				r = AKCALL(ref, ak_fm_start, fm, v, f, w, vol);
			} else if (a < 50)
				r = AKCALL(ref, ak_fm_stop, fm, (rnd() & 7) ? v : -1);
			else if (a < 60)
				r = AKCALL(ref, ak_fm_pan, fm, v, (rnd() & 7) ? (long)(rnd() % 256) : (long)rnd() - 300);
			else if (a < 72)
				r = AKCALL(ref, ak_fm_pitch, fm, v, freq() ? freq() : AKCALL(ref, ak_note_mhz, rnd() % 128));
			else if (a < 82)
				r = AKCALL(ref, ak_fm_gain, fm, v, (rnd() & 7) ? (long)(rnd() % 256) : (long)rnd());
			else if (a < 90) {
				if (nfmsins && (rnd() & 1)) {
					memset(inst, 0, sizeof inst);
					AKCALL(ref, ak_fm_from_fms, inst, fmsins[rnd() % nfmsins]);
				} else
					rnd_inst(inst);
				r = AKCALL(ref, ak_fm_instrument, fm, v, inst);
			} else if (a < 91)
				r = AKCALL(ref, ak_fm_silence, fm);
			else
				r = AKCALL(ref, ak_fm_active, fm);
			OUTV(nm, r);
		}
		/* glissé : plusieurs petits blocs, la hauteur change entre chacun */
		long bn = blks[bi++ % (int)(sizeof blks / sizeof blks[0])];
		if (bn > total - done)
			bn = total - done;
		memset(buf, 0xC3, (bn + 4) * 4);
		AKCALL(ref, ak_fm_render, fm, buf, bn);
		snprintf(nm, sizeof nm, "%s.r%d", tag, step);
		OUT(nm, buf, (bn + 4) * 4);
		for (long k = 0; k < bn * 2; k++) {
			energy += buf[k] < 0 ? -buf[k] : buf[k];
			nclip += buf[k] == 32767 || buf[k] == -32768;
		}
		done += bn;
		for (long v = 0; v < n; v++)
			nfm += fm->voices[v].wave == AKW_FM && (fm->voices[v].op[0].stage | fm->voices[v].op[1].stage);
		if ((step & 15) == 0) {
			dump(nm, fm);
			OUTV(nm, AKCALL(ref, ak_fm_active, fm));
		}
		step++;
	}
	/* niveau moyen, échantillons saturés, voix FM actives (somme sur les blocs) */
	snprintf(nm, sizeof nm, "%s.energy", tag);
	OUTV(nm, energy / (total * 2));
	OUTV(nm, nclip);
	OUTV(nm, nfm);
	/* fin : tout relâcher, rendre jusqu'au silence (10 s au plus) */
	AKCALL(ref, ak_fm_stop, fm, -1);
	for (int i = 0; i < 100 && AKCALL(ref, ak_fm_active, fm); i++) {
		AKCALL(ref, ak_fm_render, fm, buf, 4410);
		snprintf(nm, sizeof nm, "%s.end%d", tag, i);
		OUT(nm, buf, 4410 * 4);
	}
	snprintf(nm, sizeof nm, "%s.fin", tag);
	dump(nm, fm);
	OUTV(nm, AKCALL(ref, ak_fm_active, fm));
	free(buf);
	AKCALL(ref, ak_fm_free, fm);
}

/* glissés et vibrato longs sur une voix FM et une onde simple, au centre */
static void t_glide(int ref)
{
	uint8_t inst[AKFI_SIZE];
	int16_t *buf = malloc(64 * 4);
	char nm[64];
	load_fms();
	for (int w = 0; w <= AKW_FM; w++) {
		rng = 900 + w;
		long fm = AKCALL(ref, ak_fm_new, 2);
		memset(inst, 0, sizeof inst);
		if (nfmsins)
			AKCALL(ref, ak_fm_from_fms, inst, fmsins[(w * 7) % nfmsins]);
		inst[AKFO_FLAGS] |= AKFF_VIBRATO | AKFF_TREMOLO;
		inst[AKFI_OP1 + AKFO_FLAGS] |= AKFF_VIBRATO | AKFF_TREMOLO | AKFF_SUSTAINED;
		inst[AKFI_FEEDBACK] = w + 1;
		AKCALL(ref, ak_fm_instrument, fm, 0, inst);
		AKCALL(ref, ak_fm_start, fm, 0, 220000, w, 255);
		AKCALL(ref, ak_fm_start, fm, 1, 330000, (w + 2) % 5, 200);
		AKCALL(ref, ak_fm_pan, fm, 1, 200);
		uint32_t crc = 0;
		for (int i = 0; i < 2000; i++) {
			AKCALL(ref, ak_fm_pitch, fm, 0, 220000 + i * 150);
			AKCALL(ref, ak_fm_pitch, fm, 1, 330000 - i * 100);
			AKCALL(ref, ak_fm_render, fm, buf, 1 + i % 64);
			crc = crc32(buf, (1 + i % 64) * 4) ^ (crc * 31);
		}
		snprintf(nm, sizeof nm, "glide%d", w);
		OUTV(nm, crc);
		dump(nm, (struct akfm *)fm);
		AKCALL(ref, ak_fm_free, fm);
	}
	free(buf);
}

static void t_render(int ref)
{
	scenario(ref, "plain16", 1, 16, 0, 4);
	scenario(ref, "mix16", 2, 0, 50, 6);
	scenario(ref, "fm64", 3, 64, 100, 5);
	scenario(ref, "fm1", 4, 1, 100, 6);
	scenario(ref, "mix64", 5, 64, 40, 5);
	scenario(ref, "fm8", 6, 8, 90, 8);
}

int main(void)
{
	RUN(t_tab);
	RUN(t_misc);
	RUN(t_inst);
	RUN(t_glide);
	RUN(t_render);
	return cmp_end();
}
