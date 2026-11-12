/* Banc différentiel : encodeur MP3 (ak_mp3enc.c) par l'encodeur générique
 * (ak_enc_open_io, ak_enc_write, ak_enc_size, ak_enc_close).
 *
 * Toutes les fréquences acceptées (8 à 48 kHz) et refusées, mono, stéréo,
 * plus de 2 canaux ; débits par défaut, bas, hauts, invalides ; PCM fabriqué
 * en entiers (silence, sinus, bruit, transitoires, saturation, signal très
 * faible, glissando, impulsions) écrit par morceaux de tailles variées
 * (0, 1, 575, 576, 1152, 5000...) ; sorties qui échouent (écriture courte) ;
 * état interne après quelques trames (tableaux, granules) ; puis chaque flux
 * est relu par le décodeur (asm pour l'asm, C pour le C) : PCM, et un
 * rapport signal / erreur au retard du codec. */
#include "cmp.h"

long ak_enc_open_io(), ak_enc_write(), ak_enc_close(), ak_enc_size();
long ak_dec_open_mem(), ak_dec_read(), ak_dec_info(), ak_dec_close();

#define AKF_MP3 3
#define CAP (8 << 20)

struct sink {
	uint8_t *b;
	long pos, end, cap, writes, seeks, fail;
};
static long sk_write(long ctx, const void *p, long n)
{
	struct sink *s = (struct sink *)ctx;
	s->writes++;
	if (s->fail && s->writes == s->fail)
		return n > 0 ? n - 1 : 0;
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

/* ---- PCM en entiers (mêmes échantillons sur aarch64 et x86-64) */
static uint32_t rng;
static int32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return (int32_t)(rng >> 8);
}
/* sinus parabolique : phase (32 bits) -> [-32768, 32768] */
static int32_t psin(uint32_t ph)
{
	int64_t t = (int32_t)ph >> 16; /* -32768..32767 : -pi..pi */
	int64_t a = t < 0 ? -t : t;
	return (int32_t)((4 * t * (32768 - a)) >> 15);
}
static int16_t clip(int64_t v)
{
	return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

#define NSIG 9
static int16_t *gen(int sig, long ch, long n, long rate)
{
	int16_t *p = calloc((size_t)(n * ch + 16), 2);
	rng = (uint32_t)(sig * 7919 + n + rate);
	uint32_t ph[2] = {0, 0x40000000u}, inc[2];
	inc[0] = (uint32_t)(((uint64_t)440 << 32) / (uint64_t)rate);
	inc[1] = (uint32_t)(((uint64_t)1234 << 32) / (uint64_t)rate);
	for (long i = 0; i < n; i++)
		for (long c = 0; c < ch; c++) {
			int64_t v = 0;
			switch (sig) {
			case 0: /* silence */
				break;
			case 1: /* sinus (deux fréquences) */
				v = psin(ph[c]) * 3 / 4;
				ph[c] += inc[c];
				break;
			case 2: /* bruit blanc */
				v = (rnd() & 0xFFFF) - 32768;
				break;
			case 3: /* transitoires : bouffées de bruit, silence entre */
				if ((i % 4000) < 300)
					v = ((rnd() & 0xFFFF) - 32768) * (300 - i % 4000) / 300;
				break;
			case 4: /* saturation : carré plein */
				v = ((i / (37 + 11 * c)) & 1) ? 32767 : -32768;
				break;
			case 5: /* très faible */
				v = (rnd() % 5) - 2;
				break;
			case 6: /* glissando + bruit */
				v = psin(ph[c]) / 2 + ((rnd() & 0x1FFF) - 4096);
				ph[c] += inc[c] / 8 + (uint32_t)i * 900u;
				break;
			case 7: /* continu + impulsions */
				v = 5000 * (c ? -1 : 1) + ((i % 1500) == 0 ? 25000 : 0);
				break;
			case 8: /* gauche = droite (M/S), sinus fort + faible bruit */
				if (c == 0) {
					v = psin(ph[0]) * 9 / 10 + (rnd() % 64);
					ph[0] += inc[0] * 3;
				} else
					v = p[i * ch];
				break;
			}
			p[i * ch + c] = clip(v);
		}
	return p;
}

static const long chunks[] = {0, 1, 575, 576, 1152, 5000, 3, 1151, 1153, 700, 2304, 0, 64};
#define NCHUNK (long)(sizeof chunks / sizeof chunks[0])

/* rapport signal / erreur (entiers) du PCM décodé au retard lag */
static long snr(const int16_t *in, const int16_t *out, long n, long ch, long lag, long got)
{
	int64_t es = 0, ee = 0;
	for (long i = 0; i < n && i + lag < got; i++)
		for (long c = 0; c < ch; c++) {
			int64_t a = in[i * ch + c], b = out[(i + lag) * ch + c];
			es += a * a;
			ee += (a - b) * (a - b);
		}
	return (long)(es / (ee / 100 + 1)); /* 100 x signal / erreur */
}

static int16_t *dpcm;

/* un flux : ouverture, écritures, fermeture, octets ; relecture */
static void stream(int ref, const char *name, long rate, long ch, long br, int sig, long n, int ck, int seek,
		   long fail)
{
	char nm[96];
	struct sink s = {calloc(1, CAP), 0, 0, CAP, 0, 0, fail};
	long e = AKCALL(ref, ak_enc_open_io, refcb(ref, 0, (void *)sk_write), seek ? refcb(ref, 1, (void *)sk_seek) : 0,
			&s, AKF_MP3, rate, ch, br);
	snprintf(nm, sizeof nm, "%s.open", name);
	OUTV(nm, e < 0 ? e : 1);
	if (e < 0) {
		OUTV(nm, s.writes);
		free(s.b);
		return;
	}
	long ech = ch > 2 ? 2 : ch;
	int16_t *pcm = gen(sig, ech, n, rate);
	long off = 0, k = ck, failed = 0;
	while (off < n) {
		long m = chunks[k++ % NCHUNK];
		if (m > n - off)
			m = n - off;
		long r = AKCALL(ref, ak_enc_write, e, pcm + off * ech, m);
		off += m;
		snprintf(nm, sizeof nm, "%s.w", name);
		OUTV(nm, r);
		OUTV(nm, AKCALL(ref, ak_enc_size, e));
		if (r < 0) {
			failed = 1;
			break;
		}
	}
	snprintf(nm, sizeof nm, "%s.close", name);
	if (!failed)
		OUTV(nm, AKCALL(ref, ak_enc_close, e));
	else if (!ref)
		/* l'asm planterait ici (voir ak_mp3enc.c) : le C seulement, sans sortie
		 * (aucune écriture : l'erreur est gardée) */
		ak_enc_close(e);
	OUT(nm, s.b, s.end);
	OUTV(nm, s.writes);
	OUTV(nm, s.seeks);
	if (!failed && s.end) {
		long d = AKCALL(ref, ak_dec_open_mem, s.b, s.end);
		snprintf(nm, sizeof nm, "%s.dec", name);
		OUTV(nm, d < 0 ? d : 1);
		if (d >= 0) {
			long info[8];
			AKCALL(ref, ak_dec_info, d, info);
			OUT(nm, info, sizeof info);
			long got = 0, r = 0;
			long maxf = (16 << 20) / (2 * ech);
			while (got < maxf && (r = AKCALL(ref, ak_dec_read, d, dpcm + got * ech, 4096 < maxf - got ? 4096 : maxf - got)) > 0)
				got += r;
			OUTV(nm, r);
			OUTV(nm, got);
			OUT(nm, dpcm, got * ech * 2);
			/* retard du codec : 1057 échantillons (banc, MDCT, décodeur) */
			OUTV(nm, snr(pcm, dpcm, n, ech, 1057, got));
			AKCALL(ref, ak_dec_close, d);
		}
	}
	free(pcm);
	free(s.b);
}

static const long rates[9] = {44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000};

/* toutes les fréquences, mono et stéréo, débits bas, par défaut, hauts */
static void t_matrix(int ref)
{
	static const long br1[] = {32000, 0, 320000, 64000, 192000, 40000};
	static const long br2[] = {8000, 0, 160000, 24000, 144000, 56000};
	char nm[64];
	int i = 0;
	for (int r = 0; r < 9; r++)
		for (long ch = 1; ch <= 2; ch++)
			for (int b = 0; b < 3; b++, i++) {
				long br = (r < 3 ? br1 : br2)[b + 3 * (i & 1)];
				long n = rates[r] / 3 + (i * 997) % 9000;
				snprintf(nm, sizeof nm, "m%d.%ld.%ld.%ld", r, ch, br, (long)(i % NSIG));
				stream(ref, nm, rates[r], ch, br, i % NSIG, n, i % NCHUNK, i & 1, 0);
			}
}

/* tous les signaux à 44,1 kHz stéréo et 22,05 kHz mono */
static void t_signals(int ref)
{
	char nm[64];
	for (int s = 0; s < NSIG; s++) {
		snprintf(nm, sizeof nm, "s%d.a", s);
		stream(ref, nm, 44100, 2, 128000, s, 30000, s, 1, 0);
		snprintf(nm, sizeof nm, "s%d.b", s);
		stream(ref, nm, 22050, 1, 32000, s, 15000, s + 3, 0, 0);
		snprintf(nm, sizeof nm, "s%d.c", s);
		stream(ref, nm, 48000, 2, 32000, s, 20000, s + 5, 0, 0); /* pression : réservoir vide */
	}
}

/* longs flux : réservoir, cible */
static void t_long(int ref)
{
	stream(ref, "l0", 44100, 2, 128000, 3, 44100 * 3, 2, 1, 0);
	stream(ref, "l1", 44100, 2, 320000, 2, 44100 * 2, 4, 0, 0);
	stream(ref, "l2", 32000, 1, 32000, 6, 32000 * 3, 6, 0, 0);
	stream(ref, "l3", 8000, 2, 8000, 2, 8000 * 4, 1, 0, 0);
	stream(ref, "l4", 24000, 2, 160000, 4, 24000 * 2, 7, 1, 0);
	/* créneaux de 3 à 15 octets : global_gain 255 ne tient pas, le granule
	 * déborde (main_data_begin « négatif » ensuite, comme l'asm) */
	stream(ref, "l5", 24000, 2, 8000, 2, 24000, 3, 0, 0);
	stream(ref, "l6", 22050, 2, 8000, 3, 22050, 5, 0, 0);
	stream(ref, "l7", 16000, 2, 8000, 4, 16000, 8, 0, 0);
	stream(ref, "l8", 12000, 2, 8000, 6, 12000, 9, 0, 0);
	stream(ref, "l9", 24000, 1, 8000, 2, 24000, 10, 0, 0);
}

/* longueurs limites (fin du flux, bourrage) */
static void t_lengths(int ref)
{
	static const long ns[] = {0, 1, 575, 576, 577, 1151, 1152, 1153, 2304, 3000};
	char nm[64];
	for (unsigned i = 0; i < sizeof ns / sizeof ns[0]; i++) {
		snprintf(nm, sizeof nm, "n%ld.a", ns[i]);
		stream(ref, nm, 44100, 2, 0, 1, ns[i], i, 0, 0);
		snprintf(nm, sizeof nm, "n%ld.b", ns[i]);
		stream(ref, nm, 16000, 1, 0, 2, ns[i], i + 1, 0, 0);
	}
}

/* paramètres refusés */
static void t_refused(int ref)
{
	static const long cs[][3] = {
		{44101, 2, 0},	   {96000, 2, 0},     {192000, 1, 0},	 {7999, 1, 0},	   {44100, 3, 0},
		{44100, 8, 0},	   {44100, 0, 0},     {44100, 9, 0},	 {44100, 2, 32500}, {44100, 2, 33000},
		{44100, 2, 8000},  {22050, 1, 320000}, {22050, 1, 1},	 {44100, 2, -128000}, {48000, 1, 128000000},
		{11025, 2, 176000}, {32000, 2, 1000},  {8000, 1, 192000}, {24000, 2, 999},	   {12000, 1, 0},
		{44100, 1, 320000}, {8000, 2, 160000},
	};
	char nm[64];
	for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		snprintf(nm, sizeof nm, "r%u", i);
		stream(ref, nm, cs[i][0], cs[i][1], cs[i][2], 1, 2000, i, 0, 0);
	}
}

/* sorties qui échouent (écriture courte) : au début, au milieu, à la fin */
static void t_fail(int ref)
{
	static const long fs[] = {1, 2, 3, 10, 41, 60, 61, 62, 63, 64};
	char nm[64];
	for (unsigned i = 0; i < sizeof fs / sizeof fs[0]; i++) {
		snprintf(nm, sizeof nm, "f%ld.a", fs[i]);
		stream(ref, nm, 44100, 2, 128000, 2, 30000, i, 0, fs[i]);
		snprintf(nm, sizeof nm, "f%ld.b", fs[i]);
		stream(ref, nm, 24000, 1, 64000, 1, 20000, i + 2, 0, fs[i]);
	}
}

/* état interne (sans les pointeurs) après quelques trames */
static void t_state(int ref)
{
	static const long cs[][4] = {{44100, 2, 128000, 6}, {22050, 1, 48000, 3}, {8000, 2, 16000, 8}, {48000, 1, 320000, 2}};
	char nm[64];
	for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		struct sink s = {calloc(1, CAP), 0, 0, CAP, 0, 0, 0};
		long e = AKCALL(ref, ak_enc_open_io, refcb(ref, 0, (void *)sk_write), 0, &s, AKF_MP3, cs[i][0], cs[i][1],
				cs[i][2]);
		snprintf(nm, sizeof nm, "st%u", i);
		OUTV(nm, e < 0 ? e : 1);
		if (e < 0)
			continue;
		int16_t *pcm = gen((int)cs[i][3], cs[i][1], 6000, cs[i][0]);
		for (int k = 0; k < 4; k++) {
			OUTV(nm, AKCALL(ref, ak_enc_write, e, pcm + k * 1500 * cs[i][1], 1500));
			uint8_t *st = *(uint8_t **)((char *)e + 64);
			OUT(nm, st, 152);			 /* paramètres, positions */
			OUT(nm, st + 264, 312 - 264 - 8);	 /* used, avail, meangr, écrivain */
			OUT(nm, st + 512, 512);			 /* granules */
			OUT(nm, st + 1024, 69824);		 /* tableaux */
		}
		OUTV(nm, AKCALL(ref, ak_enc_close, e));
		OUT(nm, s.b, s.end);
		free(pcm);
		free(s.b);
	}
}

int main(void)
{
	dpcm = malloc(16 << 20);
	RUN(t_refused);
	RUN(t_state);
	RUN(t_lengths);
	RUN(t_signals);
	RUN(t_matrix);
	RUN(t_long);
	RUN(t_fail);
	return cmp_end();
}
