/* Banc différentiel : CELT (ak_celt.c, ak_opustab.c).
 * 1) tables (asm contre C) et outils appelables (ce_bexcos, ce_log2tan,
 *    ce_cache, ce_b2p, ce_p2b, ce_getp ; ce_exp2 et ce_rot1 ont des arguments
 *    flottants : appelés directement, ils ne touchent que des registres libres) ;
 * 2) celt_decode directement sur des trames d'octets au hasard : toutes les
 *    durées, start 0 / 17, end 13 / 17 / 19 / 21, C et CC 1 / 2, accumulation,
 *    réinitialisations ; sorties, état (pointeurs à zéro), décodeur de plage ;
 * 3) par l'API par paquets (ak_codec_open(AKF_OPUS) / ak_codec_decode en S16 et
 *    F32 / ak_codec_reset) : skel/sd0/res/audiotest/celt64.opus (démultiplexé
 *    ici), les flux de libopus de mkcelt.py (build/audiokit_cmp/celt/lib_*.pk :
 *    NB à FB, mono / stéréo, 2,5 à 60 ms, débits, transitoires, post-filtre,
 *    silence, inversion), ceux de l'encodeur Opus en asm (enc_*.opus, faits ici
 *    sur aarch64 puis relus des deux côtés) ; paquets abîmés.
 * Les NaN des sorties F32 sont mis sous une forme canonique (voir ak_fp.h). */
#include "cmp.h"

#include "ak.h"
#include "ak_celt.h"
#include "ak_opus.h"

long ak_enc_open_io(), ak_enc_write(), ak_enc_close();

#define DIR "build/audiokit_cmp/celt/"
#define MAXPK 1024

static uint32_t rng = 1;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}

/* NaN -> 0x7FC00000 ; rend le nombre de NaN */
static int canon(float *f, size_t n)
{
	int k = 0;
	for (size_t i = 0; i < n; i++) {
		uint32_t u;
		memcpy(&u, f + i, 4);
		if ((u & 0x7F800000u) == 0x7F800000u && (u & 0x7FFFFFu)) {
			u = 0x7FC00000u;
			memcpy(f + i, &u, 4);
			k++;
		}
	}
	return k;
}

static void outf(const char *nm, const float *p, size_t n)
{
	float *b = malloc(n * 4 + 4);
	memcpy(b, p, n * 4);
	char m[96];
	snprintf(m, sizeof m, "%s.nan", nm);
	int k = canon(b, n);
	if (k)
		OUTV(m, k);
	OUT(nm, b, n * 4);
	free(b);
}

/* ---------------------------------------------------------------------------
 * Tables et outils
 * ------------------------------------------------------------------------- */

#define TAB(ref, t) OUT(#t, (ref) ? refaddr(REF_##t) : (const void *)&(t), sizeof(t))

static void t_tables(int ref)
{
	TAB(ref, ce_window);
	TAB(ref, ce_emeans);
	TAB(ref, ce_eband);
	TAB(ref, ce_logn);
	TAB(ref, ce_cindex);
	TAB(ref, ce_balloc);
	TAB(ref, ce_cbits);
	TAB(ref, ce_ccaps);
	TAB(ref, ce_eprob);
	TAB(ref, ce_log2frac);
	TAB(ref, ce_halfpi);
	TAB(ref, ce_pred);
	TAB(ref, ce_combg);
	TAB(ref, ce_exp2t8);
	TAB(ref, ce_tfsel);
	TAB(ref, ce_spread_icdf);
	TAB(ref, ce_trim_icdf);
	TAB(ref, ce_tapset_icdf);
	TAB(ref, ce_small_icdf);
	TAB(ref, ce_spreadf);
	TAB(ref, ce_ordery);
	TAB(ref, ce_bitil);
	TAB(ref, ce_bitdi);
}

static float exp2_of(int ref, float x)
{
	if (ref)
		return ((float (*)(float))refaddr(REF_ce_exp2))(x);
	return ce_exp2(x);
}

static void rot1_of(int ref, float *x, uint32_t len, uint32_t stride, float c, float s)
{
	if (ref)
		((void (*)(float *, uint32_t, uint32_t, float, float))refaddr(REF_ce_rot1))(x, len, stride, c, s);
	else
		ce_rot1(x, len, stride, c, s);
}

static void t_tools(int ref)
{
	static uint32_t v[200000];
	int n = 0;
	for (int x = 0; x <= 16384; x++)
		v[n++] = (uint32_t)AKCALL(ref, ce_bexcos, x);
	OUT("bexcos", v, n * 4);
	n = 0;
	rng = 11;
	for (int k = 0; k < 4000; k++) {
		int s = 1 + (int)(rnd() % 32767), c = 1 + (int)(rnd() % 32767);
		if (k < 64)
			s = 32767 >> (k & 15), c = 32767 >> (k >> 4);
		v[n++] = (uint32_t)AKCALL(ref, ce_log2tan, s, c);
	}
	OUT("log2tan", v, n * 4);
	/* cache (décalage dans ce_cbits), b2p, p2b de chaque bande et LM (LM = -1 :
	 * bandes 8 et plus) */
	const uint8_t *cb = ref ? refaddr(REF_ce_cbits) : ce_cbits;
	n = 0;
	for (int lm = -1; lm <= 3; lm++)
		for (int b = lm < 0 ? 8 : 0; b < 21; b++) {
			const uint8_t *c = (const uint8_t *)AKCALL(ref, ce_cache, b, lm);
			v[n++] = (uint32_t)(c - cb);
			for (int bits = -16; bits < 2200; bits += 1 + bits / 64)
				v[n++] = (uint32_t)AKCALL(ref, ce_b2p, b, lm, bits);
			for (int q = 0; q <= c[0]; q++)
				v[n++] = (uint32_t)AKCALL(ref, ce_p2b, b, lm, q);
		}
	OUT("cache", v, n * 4);
	n = 0;
	for (int q = 0; q < 64; q++)
		v[n++] = (uint32_t)AKCALL(ref, ce_getp, q);
	OUT("getp", v, n * 4);
	/* ce_exp2 : valeurs ordinaires, limites, infinis, NaN */
	float *f = (float *)v;
	n = 0;
	static const uint32_t sp[] = {0x00000000, 0x80000000, 0x7F800000, 0xFF800000, 0x7FC00000, 0x447A0000, 0x447A4000,
				      0xC47A0000, 0xC47A4000, 0x3F000000, 0xBF000000, 0x3EFFFFFF, 0x4B000001, 0x7F7FFFFF,
				      0x00000001, 0x42000000, 0x42FE0000, 0x43000000, 0xC3160000, 0xC3170000};
	for (unsigned k = 0; k < sizeof sp / sizeof sp[0]; k++)
		f[n++] = exp2_of(ref, ak_u2f(sp[k]));
	rng = 12;
	for (int k = 0; k < 6000; k++) {
		float x = (float)((int)(rnd() % 2000001) - 1000000) / 1000.0f;
		if (k & 1)
			x = x / 64.0f;
		f[n++] = exp2_of(ref, x);
	}
	outf("exp2", f, n);
	/* ce_rot1 */
	rng = 13;
	static float x[256];
	for (int k = 0; k < 300; k++) {
		uint32_t len = 2 + rnd() % 200, st = 1 + rnd() % 12;
		for (uint32_t i = 0; i < len; i++)
			x[i] = (float)((int)(rnd() % 20001) - 10000) / 3000.0f;
		float c = (float)(rnd() % 10001) / 10000.0f, s = (float)(rnd() % 10001) / 10000.0f;
		rot1_of(ref, x, len, st, c, s);
		char nm[32];
		snprintf(nm, sizeof nm, "rot1.%d", k);
		outf(nm, x, len);
	}
}

/* ---------------------------------------------------------------------------
 * celt_decode sur des trames au hasard
 * ------------------------------------------------------------------------- */

#define ECB 112
static void ec_snap(long *o, const uint8_t *ec, const uint8_t *buf)
{
	uint64_t w[14];
	memcpy(w, ec, ECB);
	for (int i = 0; i < 4; i++)
		w[i] -= (uint64_t)buf;
	memcpy(o, w, ECB);
}

/* état, pointeurs à zéro, flottants canoniques */
static void snap_state(const char *nm, const struct celt *ce)
{
	static struct celt s;
	memcpy(&s, ce, sizeof s);
	s.out[0] = s.out[1] = 0;
	s.mem[0] = s.mem[1] = 0;
	s.x = s.norm = s.freq = s.tmp = 0;
	s.ut = 0;
	s.iy = 0;
	s.md = 0;
	canon(&s.pfgain, 1);
	canon(&s.pfgainold, 1);
	canon(s.preemph, 2);
	canon(&s.pfg, 1);
	canon(s.olde, 3 * 48);
	OUT(nm, &s, sizeof s);
}

struct dstate {
	struct akd *d;
	struct akmdct *md;
	struct celt *ce;
	uint8_t *ec;
	float *out[2];
};

static int dnew(int ref, struct dstate *s, long cc)
{
	s->d = ak_alloc(AKD_BYTES);
	s->md = ak_alloc(4 * MD_SIZE);
	for (int k = 0; k < 4; k++)
		if (AKCALL(ref, ak_imdct_init, s->d, s->md + k, 240 << k) < 0)
			return -1;
	long r = AKCALL(ref, celt_new, s->d, cc, s->md);
	if (r < 0)
		return -1;
	s->ce = (struct celt *)r;
	s->ec = ak_alloc(ECB);
	s->out[0] = ak_alloc(960 * 4 + 64);
	s->out[1] = ak_alloc(960 * 4 + 64);
	return 0;
}

static void dfree(int ref, struct dstate *s)
{
	AKCALL(ref, akd_free, s->d); /* (rend aussi le décodeur) */
	ak_free(s->md);
	ak_free(s->ec);
	ak_free(s->out[0]);
	ak_free(s->out[1]);
}

static void t_frames(int ref)
{
	static uint8_t buf[1400];
	static const int ends[] = {13, 17, 19, 21};
	char nm[96];
	rng = 777;
	for (int cfg = 0; cfg < 64; cfg++) {
		struct dstate s;
		long cc = 1 + (cfg & 1);
		if (dnew(ref, &s, cc) < 0) {
			OUTV("celt_new", -1);
			continue;
		}
		struct celt *ce = s.ce;
		for (int f = 0; f < 24; f++) {
			long C = (cfg >> 1) & 1 ? 1 + (long)(rnd() & 1) : cc;
			long n = 120L << (rnd() % 4);
			if (rnd() % 50 == 0)
				n = (long)(rnd() % 1000);
			long end = ends[rnd() % 4];
			long start = end > 17 && rnd() % 4 == 0 ? 17 : 0;
			long len;
			switch (rnd() % 6) {
			case 0: len = (long)(rnd() % 8); break;
			case 1: len = (long)(rnd() % 40); break;
			case 2: len = 40 + (long)(rnd() % 300); break;
			case 3: len = 200 + (long)(rnd() % 1076); break;
			default: len = 10 + (long)(rnd() % 160); break;
			}
			int mode = (int)(rnd() % 5);
			for (long i = 0; i < len; i++) {
				uint32_t r = rnd();
				buf[i] = mode == 0 ? 0 : mode == 1 ? 0xFF : mode == 2 ? (uint8_t)(r & r >> 8) : (uint8_t)r;
			}
			if (len && mode < 2)
				buf[0] = (uint8_t)rnd();
			ce->start = (u64)start;
			ce->end = (u64)end;
			ce->out[0] = s.out[0];
			ce->out[1] = s.out[1];
			ce->accum = rnd() % 4 == 0;
			for (int i = 0; i < 960; i++)
				s.out[0][i] = s.out[1][i] = (float)(i % 17) * 0.01f;
			if (rnd() % 16 == 0)
				AKCALL(ref, celt_reset, ce);
			AKCALL(ref, ec_init, s.ec, buf, len);
			long r = AKCALL(ref, celt_decode, ce, len, n, C, s.ec);
			snprintf(nm, sizeof nm, "f%d.%d", cfg, f);
			OUTV(nm, r);
			long e[14];
			ec_snap(e, s.ec, buf);
			snprintf(nm, sizeof nm, "f%d.%d.ec", cfg, f);
			OUT(nm, e, ECB);
			snprintf(nm, sizeof nm, "f%d.%d.st", cfg, f);
			snap_state(nm, ce);
			if (r > 0)
				for (long c = 0; c < cc; c++) {
					snprintf(nm, sizeof nm, "f%d.%d.o%ld", cfg, f, c);
					outf(nm, s.out[c], (size_t)r);
					snprintf(nm, sizeof nm, "f%d.%d.m%ld", cfg, f, c);
					outf(nm, ce->mem[c], CE_MEMN);
				}
		}
		dfree(ref, &s);
	}
}

/* ---------------------------------------------------------------------------
 * Paquets : API par paquets
 * ------------------------------------------------------------------------- */

struct pk {
	const uint8_t *p;
	int n;
};

/* Ogg (un flux) -> paquets */
static int ogg_demux(const uint8_t *f, size_t n, struct pk *pk, uint8_t *store)
{
	size_t o = 0, so = 0, curo = 0;
	int np = 0, cur = 0;
	while (o + 27 <= n && !memcmp(f + o, "OggS", 4)) {
		int ns = f[o + 26];
		const uint8_t *lace = f + o + 27;
		size_t b = o + 27 + ns;
		for (int i = 0; i < ns; i++) {
			if (!cur) {
				curo = so;
				cur = 1;
			}
			memcpy(store + so, f + b, lace[i]);
			so += lace[i];
			b += lace[i];
			if (lace[i] < 255 && np < MAXPK) {
				pk[np].p = store + curo;
				pk[np].n = (int)(so - curo);
				np++;
				cur = 0;
			}
		}
		o = b;
	}
	return np;
}

static uint8_t head_buf[32];
static int mkhead(int ch, int preskip)
{
	uint8_t *h = head_buf;
	memset(h, 0, sizeof head_buf);
	memcpy(h, "OpusHead", 8);
	h[8] = 1;
	h[9] = (uint8_t)ch;
	h[10] = (uint8_t)preskip;
	h[11] = (uint8_t)(preskip >> 8);
	h[12] = 0x80;
	h[13] = 0xBB;
	return 19;
}

/* une suite de paquets ; mx : trames au plus par appel (0 : 5760) */
static void dec_seq(int ref, const char *name, const uint8_t *head, int hn, const struct pk *pk, int np, int flags,
		    int reset_at)
{
	static uint8_t pcm[5760 * 2 * 4 + 64];
	char nm[96];
	long d = AKCALL(ref, ak_codec_open, AKF_OPUS, 48000, 0, head, hn);
	snprintf(nm, sizeof nm, "%s.open", name);
	OUTV(nm, d < 0 ? d : 1);
	if (d < 0)
		return;
	long info[8];
	AKCALL(ref, ak_codec_info, d, info);
	snprintf(nm, sizeof nm, "%s.info", name);
	OUT(nm, info, sizeof info);
	int oc = (int)info[2];
	for (int i = 0; i < np; i++) {
		if (i == reset_at)
			AKCALL(ref, ak_codec_reset, d);
		memset(pcm, 0x55, sizeof pcm);
		int mx = i % 11 == 5 ? 100 : 5760;
		long r = AKCALL(ref, ak_codec_decode, d, pk[i].p, pk[i].n, pcm, mx, flags);
		snprintf(nm, sizeof nm, "%s.%d", name, i);
		OUTV(nm, r);
		if (r > 0) {
			if (flags)
				outf(nm, (float *)pcm, (size_t)r * oc);
			else
				OUT(nm, pcm, (size_t)r * oc * 2);
		}
	}
	AKCALL(ref, ak_codec_close, d);
}

/* une suite : par l'en-tête du flux (F32), puis l'autre nombre de canaux (S16),
 * puis abîmée de quelques façons */
static uint8_t work[1 << 20];

static void streams(int ref, const char *name, int ch, const struct pk *pk, int np, const uint8_t *head, int hn)
{
	char nm[96];
	int h1 = head ? hn : mkhead(ch, 0);
	static uint8_t hcopy[64];
	memcpy(hcopy, head ? head : head_buf, (size_t)h1);
	snprintf(nm, sizeof nm, "%s.a", name);
	dec_seq(ref, nm, hcopy, h1, pk, np, AKC_F32, np / 2);
	int h2 = mkhead(3 - ch, 0);
	snprintf(nm, sizeof nm, "%s.b", name);
	dec_seq(ref, nm, head_buf, h2, pk, np, AKC_S16, -1);
	/* abîmés : coupés, octets changés (pas le TOC), fin à zéro, bits
	 * inversés au début, TOC changé (durée, stéréo) */
	static struct pk b[MAXPK];
	for (int v = 0; v < 5; v++) {
		rng = 5000 + (uint32_t)v * 31 + (uint32_t)np;
		size_t wo = 0;
		for (int i = 0; i < np; i++) {
			int n = pk[i].n;
			memcpy(work + wo, pk[i].p, (size_t)n);
			if (v == 0 && n > 1)
				n = 1 + (int)(rnd() % (uint32_t)n);
			else if (v == 1)
				for (int k = 0; k < 3; k++)
					if (n > 1)
						work[wo + 1 + rnd() % (uint32_t)(n - 1)] = (uint8_t)rnd();
			if (v == 2)
				for (int k = n / 2; k < n; k++)
					work[wo + k] = 0;
			if (v == 3 && n > 2)
				work[wo + 1] ^= 0xFF;
			if (v == 4 && n > 0 && rnd() % 3 == 0) {
				uint32_t cfg = 16 + rnd() % 16; /* (un seul rnd() par instruction) */
				uint32_t st = rnd() & 4;
				work[wo] = (uint8_t)((work[wo] & 0x83) | cfg << 3 | st);
			}
			b[i].p = work + wo;
			b[i].n = n;
			wo += (size_t)n + 8;
		}
		snprintf(nm, sizeof nm, "%s.bad%d", name, v);
		h1 = mkhead(ch, 0);
		dec_seq(ref, nm, head_buf, h1, b, np, v & 1 ? AKC_F32 : AKC_S16, -1);
	}
}

static uint8_t *celt_file;
static size_t celt_n;
static struct pk celt_pk[MAXPK];
static int celt_np;
static uint8_t celt_store[1 << 17];

static void t_file(int ref)
{
	if (!celt_file) {
		celt_file = load("skel/sd0/res/audiotest/celt64.opus", &celt_n);
		if (!celt_file) {
			fprintf(stderr, "celt64.opus absent\n");
			exit(2);
		}
		celt_np = ogg_demux(celt_file, celt_n, celt_pk, celt_store);
	}
	OUTV("celt64.np", celt_np);
	const struct pk *h = &celt_pk[0];
	int ch = h->n > 9 ? h->p[9] : 2;
	streams(ref, "celt64", ch, celt_pk + 2, celt_np - 2, h->p, h->n);
}

/* build/audiokit_cmp/celt/lib_*.pk (mkcelt.py) */
static const char *const libs[] = {
    "fb20s",  "fb20m",	 "fb10s",   "fb5s",    "fb2m",	 "fb2s",    "swb20s", "wb20m", "mb20m",	 "nb20m",  "nb10s",
    "lo20s",  "lo10m",	 "hi20s",   "hi20m",   "tr20s",	 "tr20m",   "tr10s",  "tr5m",  "pf20m",	 "pf20s",  "pf10m",
    "pf5s",   "fm20s",	 "fm10s",   "cbr20s",  "cvbr20m", "cx0s",   "cx3m",   "sil20s", "sil10m", "anti20s", "anti20n",
    "anti10s", "ld20s", "ld5m",    "m40s",    "m60m",	 "m40m",    "sweep20s", "sweep2s",
};

static int load_pk(const char *path, struct pk *pk, int *ch, uint8_t **file)
{
	size_t n;
	uint8_t *f = load(path, &n);
	*file = f;
	if (!f || n < 12 || memcmp(f, "CELTPK1", 7))
		return -1;
	*ch = f[7];
	uint32_t np;
	memcpy(&np, f + 8, 4);
	size_t o = 12;
	int k = 0;
	for (uint32_t i = 0; i < np && k < MAXPK && o + 2 <= n; i++) {
		uint16_t l;
		memcpy(&l, f + o, 2);
		o += 2;
		if (o + l > n)
			break;
		pk[k].p = f + o;
		pk[k].n = l;
		k++;
		o += l;
	}
	return k;
}

static int lib_i;
static void t_lib(int ref)
{
	char path[160];
	static struct pk pk[MAXPK];
	snprintf(path, sizeof path, DIR "lib_%s.pk", libs[lib_i]);
	int ch;
	uint8_t *f;
	int np = load_pk(path, pk, &ch, &f);
	OUTV(libs[lib_i], np);
	if (np > 0) {
		uint8_t tocs[MAXPK];
		for (int i = 0; i < np; i++)
			tocs[i] = pk[i].n ? pk[i].p[0] : 0;
		OUT("tocs", tocs, (size_t)np);
		streams(ref, libs[lib_i], ch, pk, np, 0, 0);
	}
	free(f);
}

/* ---------------------------------------------------------------------------
 * Encodeur Opus en asm (aarch64) : fichiers relus des deux côtés
 * ------------------------------------------------------------------------- */

struct wbuf {
	uint8_t *p;
	long n;
};
static long w_write(long ctx, const void *b, long n)
{
	struct wbuf *w = (struct wbuf *)ctx;
	if (w->n + n > (8 << 20))
		return -5;
	memcpy(w->p + w->n, b, (size_t)n);
	w->n += n;
	return n;
}

static double xsin(double x)
{
	x -= 6.283185307179586 * (double)(long)(x / 6.283185307179586);
	if (x > 3.141592653589793)
		x -= 6.283185307179586;
	if (x < -3.141592653589793)
		x += 6.283185307179586;
	double x2 = x * x;
	return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72 * (1 - x2 / 110)))));
}

static const struct {
	int rate, ch, br;
} encs[] = {
    {48000, 2, 96000}, {48000, 1, 32000}, {48000, 2, 16000}, {48000, 2, 256000}, {44100, 2, 128000},
    {44100, 1, 20000}, {16000, 1, 24000}, {8000, 1, 16000},	 {32000, 2, 48000},  {22050, 2, 64000},
};
#define NENC (int)(sizeof encs / sizeof encs[0])

static void mkenc(void)
{
	struct wbuf w = {malloc(8 << 20), 0};
	int16_t *s = malloc(48000 * 2 * 2 * 2);
	for (int k = 0; k < NENC; k++) {
		int rate = encs[k].rate, ch = encs[k].ch;
		int frames = rate * 6 / 5;
		rng = 900 + (uint32_t)k;
		for (int i = 0; i < frames; i++) /* sinus, balayage, bruit, clics, silences */
			for (int c = 0; c < ch; c++) {
				double t = (double)i / rate;
				double v = 0.3 * xsin(6.2831853 * (220 + 110 * c) * t) +
					   0.2 * xsin(6.2831853 * (200 + 3000 * t) * t) +
					   (int)(rnd() % 2001 - 1000) / 8000.0;
				if (i % (rate / 5) < 30)
					v += 0.6;
				if ((i / 4000) % 3 == 2)
					v *= c ? 0.0 : 0.05;
				s[i * ch + c] = (int16_t)(v * 30000);
			}
		w.n = 0;
		long e = AKCALL(1, ak_enc_open_io, refcb(1, 2, (void *)w_write), 0, &w, AKF_OPUS, rate, ch, encs[k].br);
		if (e < 0)
			continue;
		for (int i = 0; i < frames;) {
			int m = 700 + i % 911;
			if (m > frames - i)
				m = frames - i;
			AKCALL(1, ak_enc_write, e, s + i * ch, m);
			i += m;
		}
		AKCALL(1, ak_enc_close, e);
		char path[128];
		snprintf(path, sizeof path, DIR "enc_%d.opus", k);
		FILE *f = fopen(path, "wb");
		if (f) {
			fwrite(w.p, 1, (size_t)w.n, f);
			fclose(f);
		}
	}
	free(s);
	free(w.p);
}

static void t_enc(int ref)
{
	static struct pk pk[MAXPK];
	static uint8_t store[1 << 20];
	for (int k = 0; k < NENC; k++) {
		char path[128], nm[32];
		snprintf(path, sizeof path, DIR "enc_%d.opus", k);
		size_t n;
		uint8_t *f = load(path, &n);
		snprintf(nm, sizeof nm, "enc%d", k);
		OUTV(nm, f ? (long)n : -1);
		if (!f)
			continue;
		int np = ogg_demux(f, n, pk, store);
		OUTV("np", np);
		if (np > 2) {
			int ch = pk[0].n > 9 ? pk[0].p[9] : 1;
			streams(ref, nm, ch, pk + 2, np - 2, pk[0].p, pk[0].n);
		}
		free(f);
	}
}

int main(void)
{
	if (cmp_has_ref)
		mkenc();
	RUN(t_tables);
	RUN(t_tools);
	RUN(t_frames);
	RUN(t_file);
	for (lib_i = 0; lib_i < (int)(sizeof libs / sizeof libs[0]); lib_i++)
		RUN(t_lib);
	RUN(t_enc);
	return cmp_end();
}
