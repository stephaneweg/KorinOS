/* AudioKit : encodeur générique (ak_enc.inc). Un encodeur (poignée opaque)
 * reçoit du PCM s16 entrelacé (ak_enc_write) et écrit les octets produits
 * par une fonction de sortie (le fichier du kit par AppKit). À la fermeture,
 * le module finit le flux (WAV : tailles de l'en-tête, par un
 * repositionnement au début). */
#include "ak_enc.h"

/* octets -> bloc à zéro (rendu à la fermeture) */
void *akn_alloc(struct akn *e, u64 n)
{
	u8 *b = ak_alloc(n + 16);
	if (!b)
		return 0;
	*(void **)b = e->allocs;
	e->allocs = b;
	return b + 16;
}

/* octets, longueur -> 0 / < 0 (la première erreur reste dans err) */
long akn_out(struct akn *e, const void *p, u64 n)
{
	if (!n)
		return 0;
	if (e->err < 0)
		return e->err;
	long r = e->writefn(e->ctx, p, (long)n);
	if ((u64)r != n) {
		e->err = AKE_IO;
		return AKE_IO;
	}
	e->pos += (s64)n;
	return 0;
}

/* réécrit un passage déjà écrit (en-tête), puis revient à la fin -> 0 / < 0
 * (le résultat de l'écriture n'est pas regardé, comme l'asm) */
long akn_rewrite(struct akn *e, long pos, const void *p, u64 n)
{
	if (!e->seekfn)
		return AKE_INVAL;
	e->seekfn(e->ctx, pos);
	e->writefn(e->ctx, p, (long)n);
	e->seekfn(e->ctx, e->pos);
	return 0;
}

static void akn_free(struct akn *e)
{
	void *b = e->allocs;
	while (b) {
		void *n = *(void **)b;
		ak_free(b);
		b = n;
	}
	ak_free(e);
}

/* ---- WAV (PCM 16 bits) */

static long wav_enc_push(struct akn *e, const s16 *pcm, u64 frames)
{
	return akn_out(e, pcm, frames * ((u64)e->ch << 1));
}

static long wav_enc_finish(struct akn *e)
{
	u8 h[48];
	ak_wav_header(h, (u64)e->rate, (u64)e->ch, 16, (u64)e->pos - 44);
	return akn_rewrite(e, 0, h, 44);
}

static long wav_enc_init(struct akn *e)
{
	u8 h[48];
	e->push = wav_enc_push;
	e->finish = wav_enc_finish;
	ak_wav_header(h, (u64)e->rate, (u64)e->ch, 16, 0); /* tailles à 0 : flux */
	return akn_out(e, h, 44);
}

/* écriture, positionnement (0 : aucun), contexte, AKF_*, fréquence, canaux,
 * débit (0 : par défaut) -> encodeur (< 0 : erreur) */
long ak_enc_open_io(akn_write_fn wr, akn_seek_fn sk, long ctx, long fmt, u64 rate, u64 ch, long quality)
{
	if (ch - 1 > 7 || rate < 8000 || rate > 192000) /* 1 à 8 canaux, 8 à 192 kHz */
		return AKE_INVAL;
	struct akn *e = ak_alloc(AKN_BYTES);
	if (!e)
		return AKE_NOMEM;
	e->writefn = wr;
	e->seekfn = sk;
	e->ctx = ctx;
	e->fmt = fmt;
	e->rate = (s64)rate;
	e->ch = (s64)ch;
	e->quality = quality;
	long r;
	if (fmt == AKF_WAV)
		r = wav_enc_init(e);
	else if (fmt == AKF_MP3)
		r = mp3_enc_init(e);
	else if (fmt == AKF_OPUS)
		r = opus_enc_init(e);
	else
		r = AKE_FORMAT;
	if (r < 0) {
		akn_free(e);
		return r;
	}
	return (long)e;
}

/* PCM s16 entrelacé, trames -> 0 / < 0 */
long ak_enc_write(struct akn *e, const s16 *pcm, u64 frames)
{
	e->frames += (s64)frames;
	long r = e->push(e, pcm, frames);
	if (r < 0)
		return r;
	return e->err;
}

/* finit le flux, ferme la sortie, rend tout -> 0 / < 0 (l'erreur gardée ;
 * celle de la fin du module n'est pas rendue, comme l'asm) */
long ak_enc_close(struct akn *e)
{
	if (e->finish)
		e->finish(e);
	long r = e->err;
	if (e->closefn)
		e->closefn(e->ctx);
	akn_free(e);
	return r;
}

long ak_enc_size(struct akn *e)
{
	return e->pos;
}

/* tampon (44 o), fréquence, canaux, bits, octets de données -> 44 */
long ak_wav_header(void *buf, u64 rate, u64 ch, u64 bits, u64 bytes)
{
	u8 *b = buf;
	u32 v;
#define W32(o, x) (v = (u32)(x), __builtin_memcpy(b + (o), &v, 4))
#define W16(o, x) (b[(o)] = (u8)(x), b[(o) + 1] = (u8)((u32)(x) >> 8))
	W32(0, 0x46464952); /* 'RIFF' */
	W32(4, (u32)bytes + 36);
	W32(8, 0x45564157);  /* 'WAVE' */
	W32(12, 0x20746D66); /* 'fmt ' */
	W32(16, 16);
	W16(20, 1); /* PCM */
	W16(22, ch);
	W32(24, rate);
	u32 ba = ((u32)bits >> 3) * (u32)ch; /* octets par trame */
	W32(28, (u32)rate * ba);
	W16(32, ba);
	W16(34, bits);
	W32(36, 0x61746164); /* 'data' */
	W32(40, bytes);
#undef W32
#undef W16
	return 44;
}
