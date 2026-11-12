/* Banc différentiel : Ogg (ak_ogg.c) avec Vorbis et Opus, en mémoire et par
 * fonctions ; flux abîmés, ordures devant, flux multiplexés. */
#include "cmp.h"

long ak_dec_open_mem(), ak_dec_open_io(), ak_dec_read(), ak_dec_info(), ak_dec_tell(), ak_dec_seek();
long ak_dec_tag(), ak_dec_close();

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
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

static void decode_all(int ref, const char *name, const uint8_t *buf, size_t n, int io)
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
	int16_t *pcm = malloc(1 << 22);
	size_t tot = 0;
	int chunk = 1;
	for (;;) {
		long r = AKCALL(ref, ak_dec_read, d, (uint8_t *)pcm + tot, chunk);
		if (r <= 0) {
			snprintf(nm, sizeof nm, "%s.end", name);
			OUTV(nm, r);
			break;
		}
		tot += r * 2 * info[2];
		chunk = chunk * 3 + 1;
		if (chunk > 5000)
			chunk = 7;
		if (tot > (1 << 22) - 200000)
			break;
	}
	snprintf(nm, sizeof nm, "%s.pcm", name);
	OUT(nm, pcm, tot);
	snprintf(nm, sizeof nm, "%s.tell", name);
	OUTV(nm, AKCALL(ref, ak_dec_tell, d));
	/* recherches */
	long ms[] = {0, 3, 50, -5, 100000, 17};
	for (unsigned i = 0; i < sizeof ms / sizeof ms[0]; i++) {
		long r = AKCALL(ref, ak_dec_seek, d, ms[i]);
		snprintf(nm, sizeof nm, "%s.seek%ld", name, ms[i]);
		OUTV(nm, r);
		OUTV(nm, AKCALL(ref, ak_dec_tell, d));
		r = AKCALL(ref, ak_dec_read, d, pcm, 300);
		OUTV(nm, r);
		if (r > 0)
			OUT(nm, pcm, r * 2 * info[2]);
		AKCALL(ref, ak_dec_info, d, info);
		OUT(nm, info, sizeof info);
	}
	free(pcm);
	OUTV("close", AKCALL(ref, ak_dec_close, d));
	if (io) {
		snprintf(nm, sizeof nm, "%s.io", name);
		OUTV(nm, s.reads);
		OUTV(nm, s.seeks);
	}
}

static const char *files[] = {"skel/sd0/res/audiotest/kenney_confirm.ogg", "skel/sd0/res/audiotest/kenney_select.ogg",
			      "skel/sd0/res/audiotest/silk16.opus", "skel/sd0/res/audiotest/celt64.opus"};

static void t_ogg(int ref)
{
	char nm[64];
	for (unsigned f = 0; f < sizeof files / sizeof files[0]; f++) {
		size_t n;
		uint8_t *b = load(files[f], &n);
		if (!b) {
			OUTV("absent", f);
			continue;
		}
		snprintf(nm, sizeof nm, "f%u", f);
		decode_all(ref, nm, b, n, 0);
		snprintf(nm, sizeof nm, "f%u.io", f);
		decode_all(ref, nm, b, n, 1);
		snprintf(nm, sizeof nm, "f%u.cut", f);
		decode_all(ref, nm, b, n * 2 / 3, 0);
		/* ordures devant (resynchronisation) */
		uint8_t *j = malloc(n + 5000);
		rng = f;
		for (int i = 0; i < 5000; i++)
			j[i] = rnd() % 3 ? rnd() : 'O';
		memcpy(j, "OggS", 4);
		j[4] = 1;
		memcpy(j + 5000, b, n);
		snprintf(nm, sizeof nm, "f%u.junk", f);
		decode_all(ref, nm, j, n + 5000, 0);
		/* deux flux entrelacés : les pages de l'autre (série changée) sautées */
		uint8_t *m = malloc(2 * n);
		size_t o = 0, p = 0;
		while (p + 27 < n) {
			size_t ns = b[p + 26], body = 0;
			for (size_t k = 0; k < ns; k++)
				body += b[p + 27 + k];
			size_t len = 27 + ns + body;
			if (p + len > n)
				break;
			memcpy(m + o, b + p, len);
			o += len;
			if (p) {
				memcpy(m + o, b + p, len);
				m[o + 14] ^= 0x5A; /* autre série (CRC faux : non vérifié) */
				o += len;
			}
			p += len;
		}
		snprintf(nm, sizeof nm, "f%u.mux", f);
		decode_all(ref, nm, m, o, f & 1);
		/* octets changés */
		for (int k = 0; k < 12; k++) {
			memcpy(m, b, n);
			rng = 100 * f + k;
			int cnt = 1 + k * 3;
			for (int c = 0; c < cnt; c++)
				m[(k < 4 ? 0 : 200) + rnd() % (n - 200)] = rnd();
			if (memcmp(m, "OggS", 4)) /* (sinon un autre format serait cherché) */
				memcpy(m, "OggS", 4);
			snprintf(nm, sizeof nm, "f%u.bad%d", f, k);
			decode_all(ref, nm, m, n, k & 1);
		}
		free(m);
		free(j);
		free(b);
	}
}

int main(void)
{
	RUN(t_ogg);
	return cmp_end();
}
