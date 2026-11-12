/* AudioKit : encodeur générique (ak_enc.c, d'après ak_enc.inc) : WAV ; MP3
 * (ak_mp3enc.c), Opus (ak_opusenc.c).
 *
 * Interface d'un module (enc = l'encodeur) :
 *	<fmt>_enc_init(enc) -> 0 / < 0 (pose push, finish, son état codec)
 *	push(enc, pcm s16 entrelacé, trames) -> 0 / < 0
 *	finish(enc) -> 0 / < 0
 * Sortie : akn_out(enc, octets, longueur) ; akn_rewrite(enc, position,
 * octets, longueur) (la position courante est gardée). */
#ifndef AK_ENC_H
#define AK_ENC_H

#include "ak.h"

struct akn;
typedef long (*akn_write_fn)(long ctx, const void *buf, long n);
typedef long (*akn_seek_fn)(long ctx, long pos);
typedef long (*akn_close_fn)(long ctx);
typedef long (*akn_push_fn)(struct akn *e, const s16 *pcm, u64 frames);
typedef long (*akn_finish_fn)(struct akn *e);

/* AKN_* : 128 octets */
struct akn {
	s64 fmt;		 /* 0 AKF_* */
	s64 rate;		 /* 8 */
	s64 ch;			 /* 16 */
	s64 quality;		 /* 24 débit (bits/s) ; 0 : par défaut */
	akn_write_fn writefn;	 /* 32 (contexte, octets, longueur) -> longueur écrite */
	akn_seek_fn seekfn;	 /* 40 (contexte, position) -> 0 (0 : impossible) */
	long ctx;		 /* 48 */
	s64 pos;		 /* 56 octets écrits */
	void *codec;		 /* 64 état du module */
	akn_push_fn push;	 /* 72 */
	akn_finish_fn finish;	 /* 80 */
	s64 frames;		 /* 88 trames reçues */
	void *allocs;		 /* 96 blocs (liste, comme akd) */
	s64 err;		 /* 104 */
	akn_close_fn closefn;	 /* 112 (contexte) à la fermeture */
	u64 pad;
};
#define AKN_BYTES 128
_Static_assert(sizeof(struct akn) == AKN_BYTES, "akn");
AK_STATIC_OFF(struct akn, closefn, 112);

void *akn_alloc(struct akn *e, u64 n);
long akn_out(struct akn *e, const void *p, u64 n);
long akn_rewrite(struct akn *e, long pos, const void *p, u64 n);

long ak_enc_open_io(akn_write_fn wr, akn_seek_fn sk, long ctx, long fmt, u64 rate, u64 ch, long quality);
long ak_enc_write(struct akn *e, const s16 *pcm, u64 frames);
long ak_enc_close(struct akn *e);
long ak_enc_size(struct akn *e);
long ak_wav_header(void *buf, u64 rate, u64 ch, u64 bits, u64 bytes);

/* modules (ak_mp3enc.c, ak_opusenc.c) */
long mp3_enc_init(struct akn *e);
long opus_enc_init(struct akn *e);

#endif
