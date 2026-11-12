/* Banc différentiel : FLAC (ak_flac.c).
 * Fichiers : skel/sd0/res/audiotest/f16s.flac et build/audiokit_cmp/flac/ (*.flac)
 * (python -I src/kits/audiokit/test/cmp/mkflac.py). */
#include "cmp.h"

long ak_dec_open_mem(), ak_dec_open_io(), ak_dec_read(), ak_dec_info(), ak_dec_tell(), ak_dec_seek();
long ak_dec_tag(), ak_dec_close();
long ak_codec_open(), ak_codec_decode(), ak_codec_reset(), ak_codec_info(), ak_codec_close();

#define AKF_FLAC 2
#define PCMMAX (1 << 23)

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}

/* copie avec une marge à zéro (l'en-tête de trame peut lire un peu au-delà) */
static uint8_t *padded(const uint8_t *p, size_t n)
{
	uint8_t *b = calloc(1, n + 4096);
	memcpy(b, p, n);
	return b;
}

/* source par fonctions (mode 1) sur un tampon en mémoire */
struct src {
	const uint8_t *p;
	long n, pos, reads, seeks;
};
static long src_read(long ctx, void *buf, long n)
{
	struct src *s = (struct src *)ctx;
	s->reads++;
	long k = s->n - s->pos;
	if (k > n)
		k = n;
	if (k > 7000) /* lectures courtes */
		k = 7000;
	if (k < 0)
		k = 0;
	memcpy(buf, s->p + s->pos, k);
	s->pos += k;
	return k;
}
static long src_seek(long ctx, long pos)
{
	struct src *s = (struct src *)ctx;
	s->seeks++;
	s->pos = pos;
	return 0;
}

/* état FLAC (256 octets), sans le pointeur de la SEEKTABLE */
static void out_state(const char *name, long d)
{
	const uint8_t *f = *(uint8_t **)((uint8_t *)d + 136);
	if (!f)
		return;
	char nm[96];
	snprintf(nm, sizeof nm, "%s.st", name);
	OUT(nm, f, 48);
	OUT(nm, f + 56, 200);
}

static void decode_all(int ref, const char *name, const uint8_t *buf, size_t n, int io, int full)
{
	char nm[96];
	struct src s = {buf, (long)n, 0, 0, 0};
	long d;
	if (io)
		d = AKCALL(ref, ak_dec_open_io, refcb(ref, 0, (void *)src_read), refcb(ref, 1, (void *)src_seek), &s, n);
	else
		d = AKCALL(ref, ak_dec_open_mem, buf, n);
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
		long k = AKCALL(ref, ak_dec_tag, d, t, tag, t == 1 ? 4 : sizeof tag);
		snprintf(nm, sizeof nm, "%s.tag%d", name, t);
		OUTV(nm, k);
		OUT(nm, tag, sizeof tag);
	}
	out_state(name, d);
	int16_t *pcm = malloc(PCMMAX);
	size_t tot = 0;
	int chunk = 1;
	long ch = info[2];
	for (;;) {
		long r = AKCALL(ref, ak_dec_read, d, (uint8_t *)pcm + tot, chunk);
		if (r <= 0) {
			snprintf(nm, sizeof nm, "%s.end", name);
			OUTV(nm, r);
			break;
		}
		tot += r * 2 * ch;
		chunk = chunk * 3 + 1;
		if (chunk > 5000)
			chunk = 7;
		if (tot > PCMMAX - 5000 * 16 - 64)
			break;
		if (!full && tot > 200000)
			break;
	}
	snprintf(nm, sizeof nm, "%s.pcm", name);
	OUT(nm, pcm, tot);
	snprintf(nm, sizeof nm, "%s.tell", name);
	OUTV(nm, AKCALL(ref, ak_dec_tell, d));
	out_state(name, d);
	/* recherches */
	long ms[] = {0, 3, 50, -5, 100000, 17, 1234, 2500, 3999, 777, 3000, 1, 600, 2999};
	unsigned nms = full ? sizeof ms / sizeof ms[0] : 6;
	for (unsigned i = 0; i < nms; i++) {
		long r = AKCALL(ref, ak_dec_seek, d, ms[i]);
		snprintf(nm, sizeof nm, "%s.seek%ld", name, ms[i]);
		OUTV(nm, r);
		OUTV(nm, AKCALL(ref, ak_dec_tell, d));
		long want = i == 7 ? 9000 : 300;
		r = AKCALL(ref, ak_dec_read, d, pcm, want);
		OUTV(nm, r);
		if (r > 0)
			OUT(nm, pcm, r * 2 * ch);
		AKCALL(ref, ak_dec_info, d, info);
		OUT(nm, info, sizeof info);
		if (full)
			out_state(nm, d);
	}
	free(pcm);
	OUTV("close", AKCALL(ref, ak_dec_close, d));
	if (io) {
		snprintf(nm, sizeof nm, "%s.io", name);
		OUTV(nm, s.reads);
		OUTV(nm, s.seeks);
	}
}

/* fin des métadonnées (début des trames), 0 si illisible */
static size_t meta_end(const uint8_t *b, size_t n)
{
	if (n < 8 || memcmp(b, "fLaC", 4))
		return 0;
	size_t p = 4;
	for (;;) {
		if (p + 4 > n)
			return 0;
		size_t len = (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3];
		int last = b[p] & 0x80;
		p += 4 + len;
		if (last)
			return p <= n ? p : 0;
	}
}

/* API par paquets : données du codec (variante), puis chaque « trame » coupée
 * aux synchros 0xFFF8/0xFFF9 (les fausses synchros font des paquets abîmés :
 * mêmes erreurs attendues) */
static void packets(int ref, const char *name, const uint8_t *b, size_t n, int xv, long rate, long ch, int bad)
{
	char nm[96];
	size_t me = meta_end(b, n);
	if (!me)
		return;
	const uint8_t *extra = 0;
	size_t en = 0;
	switch (xv) {
	case 0: /* "fLaC" + blocs */
		extra = b;
		en = me;
		break;
	case 1: /* bloc STREAMINFO avec son en-tête */
		extra = b + 4;
		en = 38;
		break;
	case 2: /* STREAMINFO seul */
		extra = b + 8;
		en = 34;
		break;
	case 3: /* rien : paramètres donnés */
		break;
	case 4: /* trop court */
		extra = b + 8;
		en = 33;
		break;
	case 5: /* "fLaC" + STREAMINFO seul (sans en-tête de bloc) */
	{
		static uint8_t x[38];
		memcpy(x, "fLaC", 4);
		memcpy(x + 4, b + 8, 34);
		extra = x;
		en = 38;
		break;
	}
	}
	long d = AKCALL(ref, ak_codec_open, AKF_FLAC, rate, ch, extra, en);
	snprintf(nm, sizeof nm, "%s.pk%d.open", name, xv);
	OUTV(nm, d < 0 ? d : 1);
	if (d < 0)
		return;
	long info[8];
	AKCALL(ref, ak_codec_info, d, info);
	OUT(nm, info, sizeof info);
	out_state(nm, d);
	uint8_t *out = malloc(65536 * 8 * 4 + 64);
	uint8_t *pk = malloc(1 << 20);
	size_t p = me;
	int k = 0;
	long sum = 0;
	while (p < n && k < 400) {
		size_t q = p + 2;
		while (q + 1 < n && !(b[q] == 0xFF && (b[q + 1] & 0xFE) == 0xF8))
			q++;
		if (q + 1 >= n)
			q = n;
		size_t len = q - p;
		if (len > (1 << 20))
			len = 1 << 20;
		memcpy(pk, b + p, len);
		memset(pk + len, 0, 64);
		if (bad && len > 8)
			for (int j = 0; j < 2; j++)
				pk[rnd() % len] = rnd();
		if (bad && (k % 5) == 2)
			len = len / 2;
		int flags = k & 1;
		long max = (k % 7) == 3 ? 100 : 65536;
		memset(out, 0x5A, 64);
		long r = AKCALL(ref, ak_codec_decode, d, pk, len, out, max, flags);
		AKCALL(ref, ak_codec_info, d, info);
		snprintf(nm, sizeof nm, "%s.pk%d.%d", name, xv, k);
		OUTV(nm, r);
		if (r > 0) {
			sum += r;
			if (!bad || k < 40)
				OUT(nm, out, r * info[2] * (flags ? 4 : 2));
			else
				OUTV(nm, crc32(out, r * info[2] * (flags ? 4 : 2)));
		}
		OUT(nm, info, sizeof info);
		if (k == 9) {
			OUTV(nm, AKCALL(ref, ak_codec_reset, d));
			out_state(nm, d);
		}
		p = q;
		k++;
	}
	snprintf(nm, sizeof nm, "%s.pk%d.sum", name, xv);
	OUTV(nm, sum);
	out_state(nm, d);
	free(out);
	free(pk);
	OUTV("close", AKCALL(ref, ak_codec_close, d));
}

static const char *files[] = {
	"skel/sd0/res/audiotest/f16s.flac",
	"build/audiokit_cmp/flac/f24v.flac",
	"build/audiokit_cmp/flac/f16w.flac",
	"build/audiokit_cmp/flac/f8m.flac",
	"build/audiokit_cmp/flac/f12c3.flac",
	"build/audiokit_cmp/flac/f20c6.flac",
	"build/audiokit_cmp/flac/f16c8.flac",
	"build/audiokit_cmp/flac/n24.flac",
	"build/audiokit_cmp/flac/f8r14.flac",
	"build/audiokit_cmp/flac/f16b16.flac",
	"build/audiokit_cmp/flac/f16b8k.flac",
	"build/audiokit_cmp/flac/f16b32k.flac",
	"build/audiokit_cmp/flac/f16l.flac",
	"build/audiokit_cmp/flac/f16l_noseek.flac",
	"build/audiokit_cmp/flac/f16l_nototal.flac",
	"build/audiokit_cmp/flac/f16l_badseek.flac",
	"build/audiokit_cmp/flac/f16l_meta.flac",
};
#define NFILES (sizeof files / sizeof files[0])

static const char *base(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

/* fichiers entiers : mémoire, fonctions */
static void t_files(int ref)
{
	for (unsigned i = 0; i < NFILES; i++) {
		size_t n;
		uint8_t *f = load(files[i], &n);
		if (!f) {
			if (ref || !cmp_has_ref)
				fprintf(stderr, "absent : %s (mkflac.py)\n", files[i]);
			OUTV(files[i], -1);
			continue;
		}
		uint8_t *b = padded(f, n);
		free(f);
		char nm[96];
		decode_all(ref, base(files[i]), b, n, 0, 1);
		snprintf(nm, sizeof nm, "%s.io", base(files[i]));
		decode_all(ref, nm, b, n, 1, i == 0 || i == 12 || i == 13);
		free(b);
	}
}

/* API par paquets */
static void t_packets(int ref)
{
	for (unsigned i = 0; i < NFILES; i++) {
		size_t n;
		uint8_t *f = load(files[i], &n);
		if (!f)
			continue;
		uint8_t *b = padded(f, n);
		free(f);
		if (n > 120000) /* (les gros : le début) */
			n = 120000;
		for (int xv = 0; xv < 6; xv++) {
			if (i > 3 && xv != 0 && xv != 3)
				continue;
			rng = 50 + i;
			packets(ref, base(files[i]), b, n, xv, 44100 + xv, xv == 3 ? 2 : 1, 0);
		}
		free(b);
	}
}

/* STREAMINFO modifié : canaux, bits, fréquence, total, tailles de bloc */
static void t_streaminfo(int ref)
{
	size_t n;
	uint8_t *f = load("build/audiokit_cmp/flac/f16l.flac", &n);
	if (!f)
		f = load("skel/sd0/res/audiotest/f16s.flac", &n);
	if (!f)
		return;
	if (n > 150000)
		n = 150000;
	uint8_t *b = padded(f, n);
	free(f);
	/* STREAMINFO : données à l'octet 8 ; 64 bits gros-boutistes à 18 */
	for (int k = 0; k < 16; k++) {
		uint8_t *c = padded(b, n);
		uint64_t v = 0;
		for (int j = 0; j < 8; j++)
			v = v << 8 | c[18 + j];
		switch (k) {
		case 0: /* 1 canal : la trame en a 2 */
		case 1: /* 4 canaux : plans 2, 3 jamais écrits */
		case 2: /* 8 canaux */
			v = (v & ~(7ull << 41)) | ((uint64_t)(k == 0 ? 0 : k == 1 ? 3 : 7) << 41);
			break;
		case 3: /* 20 bits (trames à bits « comme STREAMINFO » faussées) */
			v = (v & ~(31ull << 36)) | (19ull << 36);
			break;
		case 4: /* 3 bits : refusé */
			v = (v & ~(31ull << 36)) | (2ull << 36);
			break;
		case 5: /* 25 bits : refusé */
			v = (v & ~(31ull << 36)) | (24ull << 36);
			break;
		case 6: /* fréquence 0 : refusée */
			v &= (1ull << 44) - 1;
			break;
		case 7: /* autre fréquence (trames à fréquence « comme STREAMINFO ») */
			v = (v & ((1ull << 44) - 1)) | (22050ull << 44);
			break;
		case 8: /* total énorme */
			v |= (1ull << 36) - 1;
			break;
		case 9: /* bloc maximal 1024 (trames de 4096 refusées) */
			c[10] = 4;
			c[11] = 0;
			break;
		case 10: /* bloc minimal différent du maximal (numéros de trame) */
			c[8] = 0;
			c[9] = 192;
			break;
		case 11: /* bloc maximal < 16 */
			c[10] = 0;
			c[11] = 15;
			break;
		case 12: /* taille de trame maximale petite */
			c[15] = 0;
			c[16] = 0;
			c[17] = 100;
			break;
		case 13: /* longueur du bloc STREAMINFO < 34 */
			c[7] = 33;
			break;
		case 14: /* STREAMINFO absent (type 1) */
			c[4] = (c[4] & 0x80) | 1;
			break;
		case 15: /* STREAMINFO « dernier » : trames prises pour des blocs */
			c[4] |= 0x80;
			break;
		}
		if (k < 9)
			for (int j = 0; j < 8; j++)
				c[18 + j] = v >> (56 - 8 * j);
		char nm[32];
		snprintf(nm, sizeof nm, "si%d", k);
		decode_all(ref, nm, c, n, k & 1, 1);
		rng = 7 + k;
		packets(ref, nm, c, n < 60000 ? n : 60000, 2, 44100, 2, 0);
		free(c);
	}
	free(b);
}

/* données abîmées : octets changés, coupures */
static void t_bad(int ref)
{
	static const int pick[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 16};
	for (unsigned i = 0; i < sizeof pick / sizeof pick[0]; i++) {
		size_t n;
		uint8_t *f = load(files[pick[i]], &n);
		if (!f)
			continue;
		if (n > 90000)
			n = 90000;
		for (int k = 0; k < 8; k++) {
			rng = 1000 + i * 37 + k;
			uint8_t *c = padded(f, n);
			size_t m = n;
			size_t me = meta_end(c, n);
			int nb = 1 + (k % 4) * 3;
			for (int j = 0; j < nb; j++) {
				size_t at;
				if (k == 5 && me) /* dans les métadonnées */
					at = rnd() % me;
				else
					at = me + rnd() % (n - me);
				c[at] = (k & 1) ? (uint8_t)rnd() : (uint8_t)(c[at] ^ (1u << (rnd() & 7)));
			}
			if (k >= 6)
				m = rnd() % n;
			char nm[48];
			snprintf(nm, sizeof nm, "bad%u.%d", i, k);
			decode_all(ref, nm, c, m, k & 1, k < 2);
			free(c);
		}
		/* coupures nettes */
		size_t me = meta_end(f, n);
		size_t cuts[] = {3, 4, 7, 8, 20, 41, 42, me - 1, me, me + 1, me + 5, me + 17, n - 1};
		for (unsigned j = 0; j < sizeof cuts / sizeof cuts[0]; j++) {
			if (cuts[j] > n)
				continue;
			uint8_t *c = padded(f, cuts[j]);
			char nm[48];
			snprintf(nm, sizeof nm, "cut%u.%u", i, j);
			decode_all(ref, nm, c, cuts[j], j & 1, 0);
			free(c);
		}
		/* paquets abîmés */
		uint8_t *c = padded(f, n);
		rng = 77 + i;
		char nm[48];
		snprintf(nm, sizeof nm, "badpk%u", i);
		packets(ref, nm, c, n, 0, 44100, 2, 1);
		free(c);
		free(f);
	}
}

/* octets fabriqués : en-têtes de trame (numéros UTF-8, codes de taille et
 * de fréquence, CRC-8), sous-trames de tous les types (dont réservés) */
static uint8_t crc8(const uint8_t *p, size_t n)
{
	uint8_t c = 0;
	for (size_t i = 0; i < n; i++) {
		c ^= p[i];
		for (int j = 0; j < 8; j++)
			c = (c & 0x80) ? (uint8_t)((c << 1) ^ 7) : (uint8_t)(c << 1);
	}
	return c;
}

static void t_synth(int ref)
{
	static const uint8_t si[38] = {0x80, 0, 0, 34, 0, 16, 0x10, 0, 0, 0, 0, 0, 0, 0,
				       0x0A, 0xC4, 0x40, 0xF0, 0, 0, 0x10, 0}; /* 44100, 1 canal, 16 bits */
	uint8_t *buf = calloc(1, 1 << 16);
	uint8_t *out = malloc(65536 * 8 * 4);
	char nm[48];
	for (int k = 0; k < 600; k++) {
		rng = 3000 + k;
		uint8_t *p = buf;
		memset(buf, 0, 1 << 16);
		*p++ = 0xFF;
		*p++ = 0xF8 | (rnd() & 1);
		int bsc = rnd() % 16, rc = rnd() % 16, cha = rnd() % 16, sc = rnd() % 8;
		if (k % 3) { /* surtout des en-têtes valides */
			if (!bsc)
				bsc = 1;
			if (rc == 15)
				rc = 9;
			if (cha > 10)
				cha = 1;
			if (sc == 3 || sc == 7)
				sc = 4;
		}
		*p++ = bsc << 4 | rc;
		*p++ = cha << 4 | sc << 1 | (k % 41 == 7);
		int ul = rnd() % 8; /* numéro UTF-8 : 1 à 7 octets */
		if (ul == 0)
			*p++ = rnd() & 0x7F;
		else {
			int nb = ul + 1;
			*p++ = (uint8_t)(0xFF << (8 - nb)) | (rnd() & (0x7F >> nb));
			for (int j = 1; j < nb; j++)
				*p++ = 0x80 | (rnd() & 0x3F) | (k % 23 == 5 ? 0x40 : 0);
		}
		if (k % 17 == 3)
			*p++ = 0xFF; /* 8 uns de tête */
		if (bsc == 6)
			*p++ = rnd();
		if (bsc == 7) {
			*p++ = rnd() & 0x1F;
			*p++ = rnd();
		}
		if (rc == 12)
			*p++ = rnd();
		if (rc == 13 || rc == 14) {
			*p++ = rnd();
			*p++ = rnd();
		}
		*p = crc8(buf, p - buf) ^ (k % 29 == 4);
		p++;
		/* sous-trames : octets au hasard, avec des types choisis */
		for (int c = 0; c < 8; c++) {
			int t = rnd() % 64;
			*p++ = (t << 1) | (rnd() % 5 == 0) | (k % 31 == 9 ? 0x80 : 0);
			int nb = 4 + rnd() % 200;
			for (int j = 0; j < nb; j++)
				*p++ = (k & 4) ? rnd() : (rnd() & 0x0F);
		}
		size_t n = p - buf;
		n += rnd() % 2000;
		long d = AKCALL(ref, ak_codec_open, AKF_FLAC, 44100, 2, k & 8 ? si : 0, k & 8 ? 38 : 0);
		snprintf(nm, sizeof nm, "syn%d", k);
		OUTV(nm, d < 0 ? d : 1);
		if (d < 0)
			continue;
		long r = AKCALL(ref, ak_codec_decode, d, buf, n, out, 70000, k & 1);
		OUTV(nm, r);
		long info[8];
		AKCALL(ref, ak_codec_info, d, info);
		OUT(nm, info, sizeof info);
		if (r > 0)
			OUTV(nm, crc32(out, r * info[2] * ((k & 1) ? 4 : 2)));
		out_state(nm, d);
		AKCALL(ref, ak_codec_close, d);
	}
	/* fichiers d'en-têtes */
	static const uint8_t h1[] = "fLaC";
	decode_all(ref, "hdr1", h1, 4, 0, 0);
	uint8_t f[64 + 4096] = {'f', 'L', 'a', 'C'};
	memcpy(f + 4, si, 38);
	decode_all(ref, "hdr2", f, 42, 0, 0); /* STREAMINFO seul, sans trame */
	decode_all(ref, "hdr3", f, 41, 1, 0);
	f[4] = 0x04; /* VORBIS_COMMENT coupé (moins de 4 octets disponibles) */
	f[5] = 0;
	f[6] = 0;
	f[7] = 40;
	decode_all(ref, "hdr4", f, 8, 0, 0);
	decode_all(ref, "hdr5", f, 10, 0, 0);
	free(buf);
	free(out);
}

int main(void)
{
	RUN(t_files);
	RUN(t_packets);
	RUN(t_streaminfo);
	RUN(t_bad);
	RUN(t_synth);
	return cmp_end();
}
