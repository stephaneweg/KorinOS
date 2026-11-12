/* Banc différentiel : encodeur générique et WAV (ak_enc.c). */
#include "cmp.h"
#define AKF_WAV 1

long ak_enc_open_io(), ak_enc_write(), ak_enc_close(), ak_enc_size(), ak_wav_header();

struct sink {
	uint8_t *b;
	long pos, end, cap, writes, seeks, fail;
};
static long sk_write(long ctx, const void *p, long n)
{
	struct sink *s = (struct sink *)ctx;
	s->writes++;
	if (s->fail && s->writes == s->fail)
		return n - 1;
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
static long sk_close(long ctx)
{
	((struct sink *)ctx)->writes += 1000;
	return 0;
}

static void t_wavenc(int ref)
{
	char nm[64];
	uint8_t h[64];
	long r = AKCALL(ref, ak_wav_header, h, 44100, 2, 16, 12345);
	OUTV("hdr", r);
	OUT("hdr", h, 44);
	AKCALL(ref, ak_wav_header, h, 8000, 7, 24, 0xFFFFFFF0L);
	OUT("hdr2", h, 44);
	static const long cs[][5] = {{AKF_WAV, 44100, 2, 1, 0}, {AKF_WAV, 8000, 1, 0, 0}, {AKF_WAV, 192000, 8, 1, 3},
				     {AKF_WAV, 7999, 1, 1, 0},	{AKF_WAV, 192001, 1, 1, 0}, {AKF_WAV, 44100, 0, 1, 0},
				     {AKF_WAV, 44100, 9, 1, 0}, {99, 44100, 2, 1, 0},	   {AKF_WAV, 22050, 3, 1, 1},
				     {AKF_WAV, 22050, 3, 1, 2}};
	int16_t pcm[8 * 1000];
	for (int i = 0; i < 8000; i++)
		pcm[i] = (int16_t)(i * 7919);
	for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; i++) {
		struct sink s = {calloc(1, 1 << 20), 0, 0, 1 << 20, 0, 0, cs[i][4]};
		long e = AKCALL(ref, ak_enc_open_io, refcb(ref, 0, (void *)sk_write),
				cs[i][3] ? refcb(ref, 1, (void *)sk_seek) : 0, &s, cs[i][0], cs[i][1], cs[i][2], 0);
		snprintf(nm, sizeof nm, "enc%u.open", i);
		OUTV(nm, e < 0 ? e : 1);
		if (e >= 0) {
			if (i == 2)
				*(void **)((char *)e + 112) = refcb(ref, 2, (void *)sk_close);
			for (int k = 0; k < 5; k++) {
				r = AKCALL(ref, ak_enc_write, e, pcm, k * 37 % 1000);
				snprintf(nm, sizeof nm, "enc%u.w%d", i, k);
				OUTV(nm, r);
				OUTV(nm, AKCALL(ref, ak_enc_size, e));
			}
			r = AKCALL(ref, ak_enc_close, e);
			snprintf(nm, sizeof nm, "enc%u.close", i);
			OUTV(nm, r);
		}
		OUT(nm, s.b, s.end);
		OUTV(nm, s.writes);
		OUTV(nm, s.seeks);
		free(s.b);
	}
}

int main(void)
{
	RUN(t_wavenc);
	return cmp_end();
}
