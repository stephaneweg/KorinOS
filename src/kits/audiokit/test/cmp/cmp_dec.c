/* Banc différentiel : décodeur générique et WAV (ak_dec.c, ak_wav.c). */
#include "cmp.h"

long ak_dec_open_mem(), ak_dec_open_io(), ak_dec_read(), ak_dec_info(), ak_dec_tell(), ak_dec_seek();
long ak_dec_tag(), ak_dec_close();

static uint32_t rng = 12345;
static uint32_t rnd(void)
{
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}

static void put32(uint8_t *p, uint32_t v)
{
	memcpy(p, &v, 4);
}
static void put16(uint8_t *p, uint16_t v)
{
	memcpy(p, &v, 2);
}

/* WAV fabriqué : format, canaux, fréquence, bits, trames ; LIST/INFO ; ext : WAVE_FORMAT_EXTENSIBLE */
static size_t mkwav(uint8_t *b, int fmt, int ch, int rate, int bits, int frames, int ext, int list, int datasz0)
{
	size_t o = 12;
	memcpy(b, "RIFF", 4);
	memcpy(b + 8, "WAVE", 4);
	if (list) {
		static const char info[] = "INFO"
					   "INAM\x06\0\0\0Titre "
					   "IART\x05\0\0\0Artis\0"
					   "IPRD\x03\0\0\0Alb\0";
		memcpy(b + o, "LIST", 4);
		put32(b + o + 4, sizeof info - 1);
		memcpy(b + o + 8, info, sizeof info - 1);
		o += 8 + sizeof info - 1;
		if (o & 1)
			b[o++] = 0;
	}
	memcpy(b + o, "fmt ", 4);
	int fsz = ext ? 40 : 16;
	put32(b + o + 4, fsz);
	memset(b + o + 8, 0, fsz);
	put16(b + o + 8, ext ? 0xFFFE : fmt);
	put16(b + o + 10, ch);
	put32(b + o + 12, rate);
	put32(b + o + 16, rate * ch * bits / 8);
	put16(b + o + 20, ch * bits / 8);
	put16(b + o + 22, bits);
	if (ext)
		put16(b + o + 32, fmt);
	o += 8 + fsz;
	memcpy(b + o, "junk", 4);
	put32(b + o + 4, 3);
	o += 8 + 4;
	memcpy(b + o, "data", 4);
	int n = frames * ch * bits / 8;
	put32(b + o + 4, datasz0 ? 0 : n);
	o += 8;
	for (int i = 0; i < n; i++)
		b[o + i] = rnd();
	if (fmt == 3)
		for (int i = 0; i < frames * ch; i++) {
			float f = ((int)(rnd() & 0xFFFF) - 32768) / 30000.0f;
			memcpy(b + o + 4 * i, &f, 4);
		}
	o += n;
	put32(b + 4, o - 8);
	return o;
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

static void t_wav(int ref)
{
	static uint8_t b[1 << 20];
	struct {
		int fmt, ch, rate, bits, frames, ext, list, d0;
	} cs[] = {
		{1, 1, 8000, 8, 3000, 0, 0, 0},	   {1, 2, 44100, 16, 2500, 0, 1, 0}, {1, 3, 48000, 24, 1500, 1, 0, 0},
		{1, 2, 22050, 32, 1100, 0, 1, 1},   {3, 2, 32000, 32, 2049, 1, 1, 0}, {3, 1, 32000, 16, 100, 0, 0, 0},
		{1, 9, 32000, 16, 100, 0, 0, 0},    {1, 2, 0, 16, 100, 0, 0, 0},      {2, 2, 8000, 16, 100, 0, 0, 0},
		{1, 8, 96000, 16, 1024, 0, 1, 0},
	};
	for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		rng = 777 + i;
		size_t n = mkwav(b, cs[i].fmt, cs[i].ch, cs[i].rate, cs[i].bits, cs[i].frames, cs[i].ext, cs[i].list,
				 cs[i].d0);
		char nm[32];
		snprintf(nm, sizeof nm, "wav%u", i);
		decode_all(ref, nm, b, n, 0);
		snprintf(nm, sizeof nm, "wavio%u", i);
		decode_all(ref, nm, b, n, 1);
		snprintf(nm, sizeof nm, "wavcut%u", i);
		decode_all(ref, nm, b, n - 37, 0);
	}
	/* ID3v2 devant un WAV ; données abîmées */
	rng = 99;
	size_t n = mkwav(b + 300, 1, 2, 44100, 16, 900, 0, 1, 0);
	static const uint8_t id3[] = {'I', 'D', '3', 3, 0, 0, 0, 0, 2, 34, 'T', 'I', 'T', '2', 0, 0, 0, 7, 0, 0,
				      0, 'T', 'i', 't', 'r', 'e', ' ', 'T', 'P', 'E', '1', 0, 0, 0, 5, 0, 0, 1,
				      0xFF, 0xFE, 'A', 0, 'B', 0};
	memset(b, 0, 300);
	memcpy(b, id3, sizeof id3);
	decode_all(ref, "id3wav", b, n + 300, 0);
	for (int k = 0; k < 40; k++) {
		rng = 1000 + k;
		n = mkwav(b, 1, 1 + k % 3, 8000 + k, k % 2 ? 16 : 8, 500, k & 4, k & 8, 0);
		for (int j = 0; j < 3; j++)
			b[12 + rnd() % 70] = rnd();
		char nm[32];
		snprintf(nm, sizeof nm, "bad%d", k);
		decode_all(ref, nm, b, n, k & 1);
	}
	decode_all(ref, "short", (const uint8_t *)"RIFF\0\0\0\0WAVEfm", 14, 0);
	decode_all(ref, "unknown", (const uint8_t *)"XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX", 30, 0);
	size_t fn;
	uint8_t *f = load("skel/sd0/res/audiotest/w8m.wav", &fn);
	if (f) {
		decode_all(ref, "w8m", f, fn, 0);
		decode_all(ref, "w8m.io", f, fn, 1);
		free(f);
	}
}

int main(void)
{
	RUN(t_wav);
	return cmp_end();
}
