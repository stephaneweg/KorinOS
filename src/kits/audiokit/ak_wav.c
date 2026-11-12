/* AudioKit : WAV (RIFF/WAVE, ak_wav.inc) : PCM 8 (non signé), 16, 24, 32
 * bits, flottant 32 bits, WAVE_FORMAT_EXTENSIBLE ; étiquettes LIST/INFO. */
#include "ak.h"

#define WAV_UNIT 1024 /* trames par unité rendue */

struct wv {
	u64 ba;	   /* 0 octets par trame */
	u64 bytes; /* 8 octets par échantillon */
	u64 flt;   /* 16 */
	u64 pad;
};

static long wav_next(struct akd *d);
static long wav_seek(struct akd *d, u64 frame);

/* octets, morceau 'LIST' (en-tête compris) : INAM, IART, IPRD */
static void wav_info(struct akd *d, u64 n, const u8 *p)
{
	uintptr_t end = (uintptr_t)p + n;
	if (ak_rd32le(p + 8) != 0x4F464E49) /* 'INFO' */
		return;
	const u8 *q = p + 12;
	for (;;) {
		if ((uintptr_t)q + 8 > end)
			return;
		u32 id = ak_rd32le(q);
		u32 sz = ak_rd32le(q + 4);
		const u8 *t = q + 8;
		if ((uintptr_t)t + sz > end)
			return;
		if (id == 0x4D414E49) /* INAM */
			akd_tag_put(d, AKT_TITLE, t, sz, 3);
		else if (id == 0x54524149) /* IART */
			akd_tag_put(d, AKT_ARTIST, t, sz, 3);
		else if (id == 0x44525049) /* IPRD */
			akd_tag_put(d, AKT_ALBUM, t, sz, 3);
		q += 8 + (u64)sz + (sz & 1);
	}
}

/* décodeur (lecture sur "RIFF") -> 0 / < 0 */
long wav_open(struct akd *d)
{
	u32 fmt = 0; /* 0 : pas encore de 'fmt ' */
	u32 ch = 0, rate = 0, ba = 0, bits = 0;
	akd_skip(d, 12);
	for (;;) {
		u64 av;
		u8 *p = akd_need(d, 8, &av);
		if (av < 8)
			return AKE_CORRUPT;
		u32 id = ak_rd32le(p);
		u32 sz = ak_rd32le(p + 4);
		if (id == 0x20746D66) { /* 'fmt ' */
			if (sz < 16)
				return AKE_CORRUPT;
			p = akd_need(d, (u64)sz + 8, &av);
			if (av < (u64)sz + 8)
				return AKE_CORRUPT;
			fmt = ak_rd16le(p + 8);
			ch = ak_rd16le(p + 10);
			rate = ak_rd32le(p + 12);
			ba = ak_rd16le(p + 20);
			bits = ak_rd16le(p + 22);
			if (fmt == 0xFFFE) { /* WAVE_FORMAT_EXTENSIBLE : le sous-format */
				if (sz < 40)
					return AKE_CORRUPT;
				fmt = ak_rd16le(p + 32);
			}
		} else if (id == 0x5453494C) { /* 'LIST' */
			if (sz >= 4) {
				u64 w = (u64)sz + 8;
				p = akd_need(d, w < 65536 ? w : 65536, &av);
				wav_info(d, av < w ? av : w, p);
			}
		} else if (id == 0x61746164) { /* 'data' */
			if (!fmt)
				return AKE_CORRUPT;
			s64 d0 = d->off + 8;
			d->data0 = d0;
			u64 e = (u64)d0 + sz;
			if (!sz) /* taille 0 ou trop grande (flux) : le reste */
				e = (u64)d->len;
			if (e > (u64)d->len)
				e = (u64)d->len;
			d->dataend = (s64)e;
			d->off = d0;
			/* format accepté ? */
			if (ch - 1 > 7 || !rate)
				return AKE_FORMAT;
			u64 flt = 0;
			if (fmt == 3) {
				if (bits != 32)
					return AKE_FORMAT;
				flt = 1;
			} else {
				if (fmt != 1)
					return AKE_FORMAT;
				if (bits != 8 && bits != 16 && bits != 24 && bits != 32)
					return AKE_FORMAT;
			}
			if (ba != (bits >> 3) * ch)
				return AKE_FORMAT;
			struct wv *w = akd_alloc(d, sizeof(struct wv));
			if (!w)
				return AKE_NOMEM;
			d->codec = w;
			w->ba = ba;
			w->bytes = bits >> 3;
			w->flt = flt;
			d->fmt = AKF_WAV;
			d->rate = rate;
			d->ch = ch;
			d->bits = bits;
			d->bitrate = (s64)((u64)rate * ba) << 3;
			d->total = (s64)((u64)(d->dataend - d->data0) / ba);
			d->pcmfmt = flt ? AKD_F32 : AKD_S32;
			d->next = wav_next;
			d->seekfn = wav_seek;
			return akd_planes(d, WAV_UNIT, ch);
		}
		akd_skip(d, (u64)sz + 8 + (sz & 1));
	}
}

/* -> trames dans les tampons plans (0 : fin) */
static long wav_next(struct akd *d)
{
	struct wv *w = d->codec;
	u64 ba = w->ba;
	s64 rem = d->dataend - d->off;
	if (rem <= 0)
		return 0;
	u64 n = (u64)rem / ba; /* trames restantes */
	if (n > WAV_UNIT)
		n = WAV_UNIT;
	if (!n)
		return 0;
	u64 av;
	const u8 *src = akd_need(d, n * ba, &av);
	av /= ba;
	if (n > av)
		n = av;
	if (!n)
		return 0;
	u64 bytes = w->bytes;
	for (u64 c = 0; c < (u64)d->ch; c++) {
		u32 *o = d->pcm[c];
		const u8 *s = src + c * bytes;
		for (u64 i = 0; i < n; i++, s += ba) {
			u32 v;
			if (bytes == 2)
				v = (u32)ak_rd16le(s) << 16;
			else if (bytes == 3)
				v = ((u32)s[0] << 8) | ((u32)s[1] << 16) | ((u32)s[2] << 24);
			else if (bytes == 4)
				v = ak_rd32le(s); /* (entier ou flottant : tel quel) */
			else
				v = ((u32)s[0] - 128) << 24;
			o[i] = v;
		}
	}
	akd_skip(d, n * ba);
	return (long)n;
}

/* trame -> 0 */
static long wav_seek(struct akd *d, u64 frame)
{
	struct wv *w = d->codec;
	d->off = (s64)(frame * w->ba + (u64)d->data0);
	d->pos = (s64)frame;
	return 0;
}
