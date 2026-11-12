/* Banc différentiel : encodeur Opus (ak_opusenc.c) par l'encodeur générique
 * (ak_enc_open_io / ak_enc_write / ak_enc_size / ak_enc_close).
 * 1) flux : fréquences (8 à 192 kHz, rééchantillonneur avec et sans table des
 *    phases), mono et stéréo, débits (0, bas, hauts, hors bornes), PCM varié
 *    (silence, sinus, bruit, transitoires, saturation, très faible, voies
 *    opposées), écritures par morceaux de tailles variées ; octets écrits
 *    (pages Ogg, CRC), nombre d'écritures et de positionnements, codes ;
 * 2) ouvertures refusées (canaux, fréquences), sortie qui échoue ;
 * 3) chaque flux est relu (pages, CRC vérifiés ici) et décodé par l'API par
 *    paquets (le décodeur Opus de la même passe : asm pour l'asm, C pour le
 *    C) ; écart au signal d'entrée en entiers (comparable entre architectures). */
#include "cmp.h"

#define AKF_OPUS 5

long ak_enc_open_io(), ak_enc_write(), ak_enc_close(), ak_enc_size();
long ak_codec_open(), ak_codec_decode(), ak_codec_close(), ak_codec_info();

static uint32_t rng = 1;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}

struct sink {
	uint8_t *b;
	long pos, end, cap, writes, seeks, fail;
};
static long sk_write(long ctx, const void *p, long n)
{
	struct sink *s = (struct sink *)ctx;
	s->writes++;
	if (s->fail && s->writes >= s->fail)
		return n ? n - 1 : 0;
	if (s->pos + n > s->cap)
		n = s->cap - s->pos;
	memcpy(s->b + s->pos, p, n);
	s->pos += n;
	if (s->pos > s->end)
		s->end = s->pos;
	return n;
}
static long sk_seek(long ctx, long pos)
{
	struct sink *s = (struct sink *)ctx;
	s->seeks++;
	s->pos = pos;
	return 0;
}

/* ---- signaux (entiers seulement : mêmes valeurs sur les deux architectures) */

/* sinus en virgule fixe : phase sur 32 bits -> Q15 (table par la récurrence
 * de Chebyshev en entiers 64 bits, sans flottant) */
static int16_t sintab[1024];
static void mksin(void)
{
	/* cos(2 pi / 1024) en Q30 */
	const int64_t c = 1073721611LL; /* round(cos(2pi/1024) * 2^30) */
	int64_t a = 0, b = 6588356LL;	/* sin(0), sin(2pi/1024) en Q30 */
	for (int i = 0; i < 1024; i++) {
		sintab[i] = (int16_t)(a >> 15);
		int64_t n = ((2 * c * b) >> 30) - a;
		a = b;
		b = n;
	}
}
static int32_t isin(uint32_t ph, int32_t amp)
{
	return (int32_t)(((int64_t)sintab[ph >> 22] * amp) >> 15);
}
static int16_t sat(int32_t v)
{
	return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

enum { S_SILENCE, S_SINE, S_NOISE, S_TRANS, S_SAT, S_WEAK, S_CHIRP, S_MIX, S_OPPOSE, S_SAME, S_BURST, S_COMB, S_RANDB, S_NSIG };

/* signal k, fréquence, voies, trames -> PCM entrelacé (malloc) */
static int16_t *mkpcm(int k, long rate, int ch, long n)
{
	int16_t *p = malloc((size_t)(n * ch + 16) * 2);
	uint32_t ph = 0, ph2 = 0, dph = (uint32_t)(440.0 * 4294967296.0 / rate);
	uint32_t dph2 = (uint32_t)(1250.0 * 4294967296.0 / rate);
	int32_t ra[26] = {0};
	uint32_t pb = 0, dpb = (uint32_t)(100.0 * 4294967296.0 / rate);
	rng = 77 + (uint32_t)k * 13 + (uint32_t)rate;
	for (long i = 0; i < n; i++) {
		for (int c = 0; c < ch; c++) {
			int32_t v = 0;
			switch (k) {
			case S_SILENCE:
				v = 0;
				break;
			case S_SINE:
				v = isin(c ? ph2 : ph, 20000);
				break;
			case S_NOISE:
				v = (int32_t)(rnd() & 0xFFFF) - 32768;
				v /= c ? 3 : 1;
				break;
			case S_TRANS: /* clics toutes les 0,1 s, silence entre */
				v = (i % (rate / 10)) < 40 ? ((i & 1) ? 30000 : -30000) : 0;
				break;
			case S_SAT: /* carré saturé */
				v = (i / (rate / 300 + 1)) & 1 ? 32767 : -32768;
				break;
			case S_WEAK:
				v = (int32_t)(rnd() % 3) - 1;
				break;
			case S_CHIRP:
				v = isin(ph, 12000) + isin(ph2 * 3, 6000) + (int32_t)(rnd() % 801) - 400;
				break;
			case S_MIX: /* sinus + bruit + rafales */
				v = isin(ph, 9000) + ((i / 2400) % 3 == 1 ? (int32_t)(rnd() % 30001) - 15000 : 0);
				break;
			case S_OPPOSE:
				v = isin(ph, 15000) + (int32_t)(rnd() % 2001) - 1000;
				if (c)
					v = -v;
				break;
			case S_SAME:
				v = isin(ph, 15000) + (int32_t)(rnd() % 4001) - 2000;
				break;
			case S_BURST: /* silence puis bruit à pleine échelle, en alternance */
				v = (i / 2900) & 1 ? (int32_t)(rnd() & 0xFFFF) - 32768 : 0;
				break;
			case S_RANDB: /* raies au milieu des 13 premières bandes, amplitudes au
				       * hasard (échelle log) à chaque trame : énergies coûteuses */
				if (i % 960 == 0 && c == 0)
					for (int q = 0; q < 26; q++)
						ra[q] = rnd() % 3 ? 1 << (rnd() % 12) : 0;
				{
					static const uint8_t m[] = {1, 3, 5, 7, 9, 11, 13, 15, 18, 22, 26, 30, 36};
					for (int q = 0; q < 13; q++)
						v += isin(pb * m[q], ra[q + 13 * c]);
				}
				break;
			case S_COMB: /* raies fortes isolées (une bande sur trois), par à-coups */
				if ((i / 1500 + 1) % 3) {
					static const uint8_t m[] = {3, 9, 15, 26, 44, 74, 138};
					for (int q = 0; q < 7; q++)
						v += isin(pb * m[q], 4000);
				}
				if (c)
					v = (v * 3) / 4 + (int32_t)(rnd() % 201) - 100;
				break;
			}
			p[i * ch + c] = sat(v);
		}
		ph += dph;
		ph2 += dph2;
		pb += dpb;
		if (k == S_CHIRP)
			dph += 3000;
	}
	return p;
}

/* ---- relecture : pages Ogg (CRC), paquets */

static uint32_t ogg_crc(const uint8_t *p, size_t n, uint32_t crc)
{
	for (size_t i = 0; i < n; i++) {
		crc ^= (uint32_t)p[i] << 24;
		for (int k = 0; k < 8; k++)
			crc = crc & 0x80000000u ? (crc << 1) ^ 0x04C11DB7u : crc << 1;
	}
	return crc;
}

#define MAXPK 4096
struct pk {
	const uint8_t *p;
	int n;
};

/* Ogg -> paquets ; *bad = pages au CRC faux, *pages */
static int ogg_demux(const uint8_t *f, size_t n, struct pk *pk, uint8_t *store, int *bad, int *pages)
{
	size_t o = 0, so = 0, curo = 0;
	int np = 0, cur = 0;
	*bad = 0;
	*pages = 0;
	while (o + 27 <= n && !memcmp(f + o, "OggS", 4)) {
		int ns = f[o + 26];
		const uint8_t *lace = f + o + 27;
		size_t b = o + 27 + ns, len = 0;
		for (int i = 0; i < ns; i++)
			len += lace[i];
		if (b + len > n)
			break;
		uint8_t h[27 + 255];
		memcpy(h, f + o, 27 + (size_t)ns);
		uint32_t want = (uint32_t)h[22] | (uint32_t)h[23] << 8 | (uint32_t)h[24] << 16 | (uint32_t)h[25] << 24;
		memset(h + 22, 0, 4);
		uint32_t c = ogg_crc(h, 27 + (size_t)ns, 0);
		c = ogg_crc(f + b, len, c);
		if (c != want)
			(*bad)++;
		(*pages)++;
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

static struct pk pks[MAXPK];
static uint8_t store[1 << 22];
static int16_t dec[48000 * 30 * 2];

/* décode le flux (paquets 2.. : audio) ; compare au signal d'entrée */
static void check(int ref, const char *name, const uint8_t *f, size_t n, const int16_t *in, long rate, int ch,
		  long frames)
{
	char nm[96];
	int bad, pages;
	int np = ogg_demux(f, n, pks, store, &bad, &pages);
	snprintf(nm, sizeof nm, "%s.pages", name);
	OUTV(nm, pages);
	snprintf(nm, sizeof nm, "%s.badcrc", name);
	OUTV(nm, bad);
	snprintf(nm, sizeof nm, "%s.npk", name);
	OUTV(nm, np);
	if (np < 2 || pks[0].n != 19)
		return;
	long d = AKCALL(ref, ak_codec_open, AKF_OPUS, 48000, 0, pks[0].p, 19);
	snprintf(nm, sizeof nm, "%s.dopen", name);
	OUTV(nm, d < 0 ? d : 1);
	if (d < 0)
		return;
	long got = 0, maxf = (long)(sizeof dec / 2 / 2);
	static int16_t pcm[5760 * 2];
	int errs = 0;
	for (int i = 2; i < np; i++) {
		long r = AKCALL(ref, ak_codec_decode, d, pks[i].p, pks[i].n, pcm, 5760, 0);
		if (r < 0) {
			errs++;
			continue;
		}
		for (long k = 0; k < r && got < maxf; k++, got++) {
			dec[got * 2] = pcm[k * ch];
			dec[got * 2 + 1] = pcm[k * ch + ch - 1];
		}
	}
	AKCALL(ref, ak_codec_close, d);
	snprintf(nm, sizeof nm, "%s.derr", name);
	OUTV(nm, errs);
	snprintf(nm, sizeof nm, "%s.dlen", name);
	OUTV(nm, got);
	snprintf(nm, sizeof nm, "%s.dcrc", name);
	OUTV(nm, (long)crc32(dec, (size_t)got * 4));
	/* écart à l'entrée (48 kHz seulement : même cadence ; retard de 120) :
	 * millièmes d'énergie d'erreur */
	if (rate == 48000 && frames > 2000) {
		int64_t es = 0, ee = 0;
		for (long k = 960; k + 120 < got && k < frames - 960; k++)
			for (int c = 0; c < ch; c++) {
				int64_t x = in[k * ch + c], y = dec[(k + 120) * 2 + (c ? 1 : 0)];
				es += x * x;
				ee += (x - y) * (x - y);
			}
		snprintf(nm, sizeof nm, "%s.nmr", name);
		OUTV(nm, es ? (long)(ee * 1000 / es) : -1);
	}
}

/* ---- essais */

struct cfg {
	long rate;
	int ch;
	long br;
	int sig;
	long frames;
	int chunk; /* 0 : au hasard */
};

static const struct cfg cfgs[] = {
	{48000, 1, 0, S_SINE, 48000, 0},     {48000, 2, 0, S_MIX, 48000, 960},	 {48000, 1, 16000, S_NOISE, 24000, 0},
	{48000, 2, 16000, S_CHIRP, 24000, 0}, {48000, 1, 24000, S_CHIRP, 24000, 7}, {48000, 2, 32000, S_SINE, 24000, 0},
	{48000, 2, 48000, S_OPPOSE, 24000, 0}, {48000, 2, 64000, S_SAME, 24000, 0}, {48000, 1, 64000, S_TRANS, 30000, 0},
	{48000, 2, 128000, S_NOISE, 24000, 0}, {48000, 2, 256000, S_CHIRP, 20000, 0}, {48000, 1, 256000, S_SAT, 20000, 0},
	{48000, 2, 300000, S_MIX, 12000, 0},  {48000, 1, 8000, S_SINE, 12000, 0},   {48000, 2, -1, S_SINE, 9000, 0},
	{48000, 1, 0, S_SILENCE, 20000, 0},   {48000, 2, 0, S_WEAK, 20000, 0},	   {48000, 2, 96000, S_TRANS, 30000, 0},
	{48000, 2, 96000, S_SAT, 20000, 0},   {48000, 1, 20000, S_MIX, 20000, 0},   {48000, 2, 40000, S_MIX, 20000, 0},
	{48000, 2, 80000, S_OPPOSE, 20000, 0}, {48000, 1, 70000, S_NOISE, 10000, 0}, {48000, 1, 0, S_SINE, 0, 0},
	{48000, 1, 0, S_SINE, 1, 0},	      {48000, 2, 0, S_CHIRP, 959, 0},	   {48000, 2, 0, S_CHIRP, 960, 0},
	{48000, 1, 0, S_NOISE, 961, 1},	      {48000, 2, 20480, S_SAME, 20000, 0},  {48000, 2, 28000, S_OPPOSE, 20000, 0},
	{8000, 1, 0, S_SINE, 8000, 0},	      {8000, 2, 24000, S_NOISE, 4000, 0},   {11025, 1, 32000, S_CHIRP, 6000, 0},
	{11025, 2, 0, S_MIX, 5000, 333},      {12000, 1, 0, S_TRANS, 6000, 0},	   {16000, 2, 48000, S_SINE, 8000, 0},
	{16000, 1, 16000, S_WEAK, 8000, 0},   {22050, 2, 0, S_CHIRP, 11025, 0},	   {22050, 1, 64000, S_SAT, 8000, 0},
	{24000, 2, 96000, S_OPPOSE, 12000, 0}, {32000, 1, 0, S_NOISE, 16000, 0},    {32000, 2, 128000, S_MIX, 16000, 4096},
	{44100, 2, 0, S_CHIRP, 22050, 0},     {44100, 1, 128000, S_SINE, 22050, 0}, {44100, 2, 256000, S_TRANS, 22050, 0},
	{88200, 2, 0, S_MIX, 22050, 0},	      {96000, 1, 0, S_SINE, 24000, 0},	   {96000, 2, 160000, S_NOISE, 20000, 0},
	{176400, 1, 0, S_CHIRP, 30000, 0},    {192000, 2, 0, S_SAT, 30000, 0},	   {37800, 2, 64000, S_CHIRP, 12000, 0},
	{8001, 1, 0, S_SINE, 6000, 0},	      {47999, 2, 0, S_MIX, 12000, 0},	   {48001, 1, 0, S_NOISE, 12000, 0},
	{192000, 1, 16000, S_SILENCE, 5000, 0}, {12345, 2, 0, S_TRANS, 9000, 0},
	{48000, 2, 16000, S_BURST, 30000, 0}, {48000, 1, 16000, S_BURST, 30000, 0}, {48000, 2, 24000, S_BURST, 30000, 0},
	{48000, 2, 32000, S_COMB, 30000, 0},  {48000, 1, 24000, S_COMB, 30000, 0},  {48000, 1, 40000, S_COMB, 30000, 0},
	{44100, 2, 16000, S_BURST, 30000, 0}, {48000, 2, 64000, S_COMB, 30000, 0},  {48000, 2, 21000, S_COMB, 30000, 0},
	{48000, 2, 16000, S_COMB, 30000, 0},  {48000, 1, 16000, S_COMB, 30000, 0},  {48000, 2, 128000, S_COMB, 20000, 0},
	{48000, 2, 16000, S_RANDB, 30000, 0}, {48000, 1, 16000, S_RANDB, 30000, 0}, {48000, 2, 21000, S_RANDB, 30000, 0},
	{192000, 1, 0, S_NOISE, 3000, 1},     {96000, 2, 0, S_SINE, 3000, 3},	   {176400, 1, 0, S_MIX, 4000, 5},
	{48000, 1, 21000, S_COMB, 30000, 0},  {48000, 1, 21000, S_RANDB, 30000, 0}, {48000, 2, 48000, S_RANDB, 20000, 0},
};

/* état forcé après l'ouverture (même disposition OE_* des deux côtés) :
 * octets par trame, bande de fin, bande d'intensité visée (< 0 : inchangé) */
static long force_nb = -1, force_end = -1, force_itgt = -1;

static void encode(int ref, int idx, const struct cfg *k, long fail, int seek)
{
	char nm[64];
	snprintf(nm, sizeof nm, "e%d%s", idx, fail ? "f" : "");
	struct sink s = {calloc(1, 1 << 22), 0, 0, 1 << 22, 0, 0, fail};
	int16_t *pcm = mkpcm(k->sig, k->rate, k->ch, k->frames);
	long e = AKCALL(ref, ak_enc_open_io, refcb(ref, 0, (void *)sk_write), seek ? refcb(ref, 1, (void *)sk_seek) : 0,
			&s, AKF_OPUS, k->rate, k->ch, k->br);
	char m2[80];
	snprintf(m2, sizeof m2, "%s.open", nm);
	OUTV(m2, e < 0 ? e : 1);
	if (e >= 0) {
		uint64_t *st = *(uint64_t **)((char *)e + 64); /* AKN_CODEC */
		if (force_nb >= 0)
			st[2] = (uint64_t)force_nb; /* OE_NBYTES */
		if (force_end >= 0)
			st[3] = (uint64_t)force_end; /* OE_END */
		if (force_itgt >= 0)
			st[5] = (uint64_t)force_itgt; /* OE_ITGT */
		rng = 1234 + (uint32_t)idx;
		long p = 0;
		int w = 0;
		while (p < k->frames || w == 0) {
			long n = k->chunk ? k->chunk : (long)(rnd() % 3000);
			if (w % 9 == 4)
				n = 0;
			if (n > k->frames - p)
				n = k->frames - p;
			long r = AKCALL(ref, ak_enc_write, e, pcm + p * k->ch, n);
			p += n;
			if (w < 40 || r != 0) {
				snprintf(m2, sizeof m2, "%s.w%d", nm, w);
				OUTV(m2, r);
				OUTV(m2, AKCALL(ref, ak_enc_size, e));
			}
			w++;
			if (p >= k->frames)
				break;
		}
		snprintf(m2, sizeof m2, "%s.size", nm);
		OUTV(m2, AKCALL(ref, ak_enc_size, e));
		long r = AKCALL(ref, ak_enc_close, e);
		snprintf(m2, sizeof m2, "%s.close", nm);
		OUTV(m2, r);
	}
	OUT(nm, s.b, (size_t)s.end);
	snprintf(m2, sizeof m2, "%s.writes", nm);
	OUTV(m2, s.writes);
	snprintf(m2, sizeof m2, "%s.seeks", nm);
	OUTV(m2, s.seeks);
	if (e >= 0 && !fail)
		check(ref, nm, s.b, (size_t)s.end, pcm, k->rate, k->ch, k->frames);
	free(pcm);
	free(s.b);
}

static void t_streams(int ref)
{
	for (unsigned i = 0; i < sizeof cfgs / sizeof cfgs[0]; i++)
		encode(ref, (int)i, &cfgs[i], 0, (int)(i & 1));
}

/* ouvertures refusées, sorties qui échouent */
static void t_errors(int ref)
{
	static const struct cfg bad[] = {
		{48000, 3, 0, S_SINE, 100, 0}, {48000, 8, 0, S_SINE, 100, 0}, {48000, 0, 0, S_SINE, 100, 0},
		{7999, 1, 0, S_SINE, 100, 0},  {192001, 2, 0, S_SINE, 100, 0},
	};
	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++)
		encode(ref, 100 + (int)i, &bad[i], 0, 0);
	static const struct cfg ok[] = {{48000, 2, 64000, S_MIX, 30000, 0}, {44100, 1, 0, S_CHIRP, 20000, 0}};
	for (int f = 1; f <= 5; f++)
		for (int j = 0; j < 2; j++)
			encode(ref, 200 + f * 2 + j, &ok[j], f == 5 ? 3 : f, j);
}

/* budgets forcés : énergies grossières bornées en fin de budget, dynalloc qui
 * déborde, réserves d'intensité qui ne tiennent pas, codeur de plage plein */
static void t_forced(int ref)
{
	static const long nbs[] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 12, 14, 16, 18, 20, 23, 26, 30, 34, 38, 51, 53, 56, 60, 66, 75};
	static const int sigs[] = {S_RANDB, S_NOISE, S_COMB, S_BURST, S_CHIRP};
	int idx = 300;
	for (unsigned i = 0; i < sizeof nbs / sizeof nbs[0]; i++)
		for (int ch = 1; ch <= 2; ch++) {
			struct cfg k = {48000, ch, 64000, sigs[(i + (unsigned)ch) % 5], 9600, 0};
			force_nb = nbs[i];
			force_end = (long[]){21, 13, 17, 19, 21}[(i + (unsigned)ch * 2) % 5];
			force_itgt = (long[]){-1, 0, 3, 21, 12, 30}[(i + (unsigned)ch) % 6];
			encode(ref, idx++, &k, 0, 0);
		}
	force_nb = force_end = force_itgt = -1;
}

int main(void)
{
	mksin();
	RUN(t_streams);
	RUN(t_errors);
	RUN(t_forced);
	return cmp_end();
}
