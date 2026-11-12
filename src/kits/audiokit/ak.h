/* AudioKit en C : déclarations communes aux fichiers du kit (audiokit.c et les
 * codecs ak_*.c). Réécriture de audiokit.asm et de ses .inc : mêmes résultats
 * au bit près (PCM décodé, octets encodés), mêmes codes d'erreur.
 *
 * Les codecs (tout sauf audiokit.c et ak_play.c) ne font aucun appel système :
 * la mémoire vient de ak_alloc / ak_free (audiokit.c dans le kit ; le banc
 * différentiel src/kits/audiokit/test/cmp les fournit sous Linux).
 *
 * Les blocs d'état gardent les dispositions de l'asm (mêmes décalages,
 * vérifiés par _Static_assert) : on relit l'asm à côté du C.
 *
 * Conventions du port : un fichier C par .inc de l'asm ; fonctions partagées
 * entre fichiers sous le nom de l'étiquette de l'asm (ak_<module>.h), le reste
 * static ; aucune variable modifiable dans les codecs, aucun pointeur dans des
 * données initialisées (le chargeur de kits ne reloge rien) ; -fwrapv ;
 * divisions par ak_udiv/ak_sdiv (diviseur nul -> 0), décalages par
 * AK_SHL/AK_SHR/AK_ASR (modulo 64), clz par ak_clz64 ; flottant : ak_fp.h.
 * Bizarreries de l'asm reproduites : « comme l'asm » ; écarts voulus (là où
 * l'asm planterait ou lirait hors de ses blocs) : en tête du fichier concerné.
 * Vérification : banc différentiel test/cmp (cmp.h), audtest sur Korin. */
#ifndef AK_H
#define AK_H

#pragma GCC visibility push(hidden)

#include <stddef.h>
#include <stdint.h>

#ifdef AK_HOST /* banc différentiel (Linux) : la libc fournit mem* */
#include <string.h>
#else
#include "../common/korin_kit.h"
#endif

#include "ak_fp.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#define AK_STATIC_OFF(type, field, off) _Static_assert(offsetof(type, field) == (off), #type "." #field)

/* ---- audiokit.inc (constantes publiques) */
#define AKF_WAV 1
#define AKF_FLAC 2
#define AKF_MP3 3
#define AKF_VORBIS 4
#define AKF_OPUS 5
#define AKF_MIDI 6
#define AKF_FMS 7

#define AKI_FORMAT 0
#define AKI_RATE 8
#define AKI_CHANNELS 16
#define AKI_FRAMES 24
#define AKI_DURATION 32
#define AKI_BITRATE 40
#define AKI_POSITION 48
#define AKI_BITS 56
#define AKI_SIZE 64

#define AKT_TITLE 0
#define AKT_ARTIST 1
#define AKT_ALBUM 2

#define AKE_NOMEM (-12)
#define AKE_INVAL (-22)
#define AKE_FORMAT (-95)
#define AKE_CORRUPT (-74)
#define AKE_IO (-5)

#define AKC_S16 0
#define AKC_F32 1

#define AKP_PLAYING 1
#define AKP_PAUSED 2
#define AKP_ENDED 3
#define AKP_ERROR 4

#define AK_FM_MAXV 64
#define AKW_SQUARE 0
#define AKW_SINE 1
#define AKW_TRIANGLE 2
#define AKW_SAW 3
#define AKW_NOISE 4
#define AKW_FM 5
#define AKFO_ATTACK 0
#define AKFO_DECAY 1
#define AKFO_SUSTAIN 2
#define AKFO_RELEASE 3
#define AKFO_LEVEL 4
#define AKFO_KSL 5
#define AKFO_MULT 6
#define AKFO_WAVE 7
#define AKFO_FLAGS 8
#define AKFO_SIZE 10
#define AKFI_OP0 0
#define AKFI_OP1 AKFO_SIZE
#define AKFI_FEEDBACK 20
#define AKFI_CONN 21
#define AKFI_SIZE 24
#define AKFF_SUSTAINED 1
#define AKFF_TREMOLO 2
#define AKFF_VIBRATO 4
#define AKFF_KSR 8

#define AKS_FM 1
#define AKS_NOFX 2
#define AKSP_BANK 0
#define AKSP_PROGRAM 4
#define AKSP_NAME 8
#define AKSP_SIZE 32

/* src/inc/armpi.inc */
#define SND_RATE_STD 44100

/* ---- mémoire (audiokit.c ; banc : cmp_host.c) */

/* octets -> bloc à zéro (pages de VM_MAP, taille rangée 16 octets avant), 0 si échec */
void *ak_alloc(u64 n);
/* bloc (0 : rien) */
void ak_free(void *p);

/* ---- le décodeur (ak_dec.c) : disposition de ak_dec.inc (AKD_*) */

#define AKD_F32 0
#define AKD_S32 1
#define AKD_IOBUF 262144
#define AKD_BYTES 1152

struct akd;
typedef long (*akd_next_fn)(struct akd *d);
typedef long (*akd_seek_fn)(struct akd *d, u64 frame);
typedef long (*akd_pkt_fn)(struct akd *d, const u8 *pkt, u64 n);
typedef void (*akd_reset_fn)(struct akd *d);
typedef void (*akd_modfree_fn)(struct akd *d);
typedef long (*ak_io_read_fn)(long ctx, void *buf, long n);
typedef long (*ak_io_seek_fn)(long ctx, long pos);
typedef long (*ak_io_close_fn)(long ctx);

struct akd {
	s64 fmt;	       /* 0 AKF_* */
	s64 rate;	       /* 8 */
	s64 ch;		       /* 16 */
	s64 total;	       /* 24 trames en tout (-1 : inconnu) */
	s64 pos;	       /* 32 trames rendues */
	s64 bitrate;	       /* 40 */
	s64 bits;	       /* 48 bits de la source (0 : compressé) */
	s64 mode;	       /* 56 0 mémoire, 1 fonctions, 2 paquets */
	u8 *base;	       /* 64 mémoire : début ; fonctions : tampon */
	s64 len;	       /* 72 taille du flux */
	s64 off;	       /* 80 position de lecture */
	s64 bufoff;	       /* 88 fonctions : position du début du tampon */
	s64 buflen;	       /* 96 fonctions : octets valides dans le tampon */
	s64 iopos;	       /* 104 fonctions : position du fichier */
	ak_io_read_fn ioread;  /* 112 */
	ak_io_seek_fn ioseek;  /* 120 */
	long ioctx;	       /* 128 */
	void *codec;	       /* 136 état du module */
	akd_next_fn next;      /* 144 (dec) -> trames (0 : fin, < 0 : erreur) */
	akd_seek_fn seekfn;    /* 152 (dec, trame) -> 0 ; 0 : impossible */
	void *allocs;	       /* 160 blocs de akd_alloc (liste) */
	s64 pcmfmt;	       /* 168 AKD_F32 / AKD_S32 */
	s64 pcmn;	       /* 176 trames dans les tampons plans */
	s64 pcmi;	       /* 184 trames déjà rendues */
	s64 skip;	       /* 192 trames à sauter */
	s64 data0;	       /* 200 début des données audio */
	s64 dataend;	       /* 208 fin des données audio */
	s64 eof;	       /* 216 */
	s64 limit;	       /* 224 1 : pas plus de total trames */
	akd_pkt_fn pktfn;      /* 232 */
	akd_reset_fn resetfn;  /* 240 */
	ak_io_close_fn ioclose; /* 248 */
	void *pcm[8];	       /* 256 tampons plans */
	u8 tags[3][256];       /* 320 titre, artiste, album (UTF-8) */
	akd_modfree_fn modfree; /* 1088 */
	u8 pad[AKD_BYTES - 1096];
};
AK_STATIC_OFF(struct akd, ioread, 112);
AK_STATIC_OFF(struct akd, pktfn, 232);
AK_STATIC_OFF(struct akd, pcm, 256);
AK_STATIC_OFF(struct akd, tags, 320);
AK_STATIC_OFF(struct akd, modfree, 1088);
_Static_assert(sizeof(struct akd) == AKD_BYTES, "akd");

void *akd_alloc(struct akd *d, u64 n);
void akd_free(struct akd *d);
long akd_planes(struct akd *d, u64 frames, u64 ch);
/* -> adresse de la position courante ; *avail = octets contigus (>= voulus, sauf en fin) */
u8 *akd_need(struct akd *d, u64 want, u64 *avail);
static inline void akd_skip(struct akd *d, u64 n)
{
	d->off += n;
}
static inline void akd_seek(struct akd *d, u64 pos)
{
	d->off = pos;
}
void akd_emit(struct akd *d, u64 first, u64 frames, void *out, long flags);
void akd_tag_put(struct akd *d, u64 tag, const u8 *txt, u64 n, u64 enc);
void akd_vcomment(struct akd *d, const u8 *p, u64 n);
long akd_key(const u8 *s, u64 n, const char *key);
void akd_id3v2(struct akd *d, u64 avail, const u8 *tag);
long akd_id3v1(struct akd *d, const u8 *tail);

/* ---- fonctions publiques du décodeur (audiokit.abi) */
long ak_dec_open_mem(const void *buf, u64 n);
long ak_dec_open_io(ak_io_read_fn rd, ak_io_seek_fn sk, long ctx, u64 size);
long ak_dec_read(struct akd *d, void *buf, u64 frames);
long ak_dec_info(struct akd *d, void *info);
long ak_dec_tell(struct akd *d);
long ak_dec_seek(struct akd *d, long ms);
long ak_dec_tag(struct akd *d, u64 tag, char *buf, u64 size);
long ak_dec_close(struct akd *d);
long ak_codec_open(long fmt, long rate, long ch, const void *extra, u64 n);
long ak_codec_decode(struct akd *d, const void *pkt, u64 n, void *out, u64 max, long flags);
long ak_codec_reset(struct akd *d);
long ak_codec_info(struct akd *d, void *info);
long ak_codec_close(struct akd *d);

/* ---- modules des formats (noms des étiquettes de l'asm) */
long wav_open(struct akd *d);
long flac_open(struct akd *d);
long flac_init_pkt(struct akd *d, const u8 *extra, u64 n);
long mp3_open(struct akd *d);
long mp3_init_pkt(struct akd *d, const u8 *extra, u64 n);
long ogg_open(struct akd *d);
long vb_init_pkt(struct akd *d, const u8 *extra, u64 n);
long ops_init_pkt(struct akd *d, const u8 *extra, u64 n);
long midi_open(struct akd *d);
long fms_open(struct akd *d);

/* ---- lecteur de bits, poids fort d'abord (ak_bits.inc) ------------------
 * Les décalages par registre d'AArch64 se font modulo 64 : AK_SHL/AK_SHR le
 * reproduisent (rb_get(0) rend tout le cache, comme l'asm). */
#define AK_SHL(x, n) ((u64)(x) << ((n) & 63))
#define AK_SHR(x, n) ((u64)(x) >> ((n) & 63))
#define AK_ASR(x, n) ((s64)(x) >> ((n) & 63))

static inline u64 ak_bswap64(u64 v)
{
	return __builtin_bswap64(v);
}
static inline u64 ak_load64(const u8 *p)
{
	u64 v;
	__builtin_memcpy(&v, p, 8);
	return v;
}
/* clz d'AArch64 : 64 pour 0 */
static inline u64 ak_clz64(u64 v)
{
	return v ? (u64)__builtin_clzll(v) : 64;
}
static inline u32 ak_clz32(u32 v)
{
	return v ? (u32)__builtin_clz(v) : 32;
}

/* BR_* : 48 octets */
struct br {
	const u8 *ptr; /* 0 prochain octet à charger */
	const u8 *end; /* 8 */
	u64 cache;     /* 16 bits à lire cadrés à gauche */
	u64 n;	       /* 24 bits valides */
	const u8 *start; /* 32 */
	u64 pad;
};

static inline void br_init(struct br *b, const u8 *p, u64 n)
{
	b->ptr = p;
	b->start = p;
	b->end = p + n;
	b->cache = 0;
	b->n = 0;
}

/* remplit le cache (au moins 57 bits) */
static inline void br_fill(struct br *b)
{
	const u8 *p = b->ptr;
	u64 c = b->cache, n = b->n;
	if ((uintptr_t)p + 8 <= (uintptr_t)b->end) {
		u64 v = ak_bswap64(ak_load64(p));
		u64 k = (64 - n) >> 3; /* octets entiers qui tiennent */
		p += k;
		v = AK_SHR(v, n);
		n += k << 3;
		u64 m = AK_SHL(~(u64)0, 64 - n);
		if (n == 64)
			m = ~(u64)0;
		c |= v & m;
	} else {
		while (n <= 56) {
			u64 v = 0;
			if (p < b->end)
				v = *p;
			p++;
			c |= AK_SHL(v, 56 - n);
			n += 8;
		}
	}
	b->ptr = p;
	b->cache = c;
	b->n = n;
}

/* bits (0..32) -> valeur */
static inline u64 br_bits(struct br *b, u64 k)
{
	if (b->n < k)
		br_fill(b);
	if (!k)
		return 0;
	u64 v = AK_SHR(b->cache, 64 - k);
	b->cache = AK_SHL(b->cache, k);
	b->n -= k;
	return v;
}

/* bits (1..32) -> valeur signée */
static inline s64 br_sbits(struct br *b, u64 k)
{
	u64 v = br_bits(b, k);
	return AK_ASR(AK_SHL(v, 64 - k), 64 - k);
}

/* -> nombre de 0 avant le prochain 1 (le 1 est lu) */
static inline u64 br_unary(struct br *b)
{
	u64 z = 0;
	u64 c = b->cache, n = b->n;
	while (!c) {
		z += n;
		n = 0;
		if ((uintptr_t)b->ptr > (uintptr_t)b->end + 8)
			return z; /* au-delà de la fin : arrêt (cache et n non rangés, comme l'asm) */
		b->cache = 0;
		b->n = 0;
		br_fill(b);
		c = b->cache;
		n = b->n;
	}
	u64 l = ak_clz64(c);
	z += l;
	c = AK_SHL(c, l);
	c = AK_SHL(c, 1);
	n -= l + 1;
	b->cache = c;
	b->n = n;
	return z;
}

static inline void br_skip(struct br *b, u64 k)
{
	while (k) {
		u64 t = k < 32 ? k : 32;
		k -= t;
		br_bits(b, t);
	}
}

static inline void br_align(struct br *b)
{
	u64 r = b->n & 7;
	b->cache = AK_SHL(b->cache, r);
	b->n -= r;
}

static inline u64 br_tell(const struct br *b)
{
	return (u64)(b->ptr - b->start) * 8 - b->n;
}

/* lecteur « en registres » de l'asm (rb_*) : x9 = cache, x10 = n, x11 = p, x12 = fin */
struct rb {
	u64 cache;
	u64 n;
	const u8 *p;
	const u8 *end;
};

static inline void rb_init(struct rb *r, const u8 *p, u64 n)
{
	r->p = p;
	r->end = p + n;
	r->cache = 0;
	r->n = 0;
}

static inline void rb_fill(struct rb *r)
{
	if ((uintptr_t)r->p + 8 <= (uintptr_t)r->end) {
		u64 v = ak_bswap64(ak_load64(r->p));
		u64 k = (64 - r->n) >> 3;
		r->p += k;
		v = AK_SHR(v, r->n);
		r->n += k << 3;
		u64 s = 64 - r->n;
		v = AK_SHR(v, s);
		v = AK_SHL(v, s);
		r->cache |= v;
	} else {
		while (r->n <= 56) {
			u64 v = 0;
			if (r->p < r->end)
				v = *r->p;
			r->p++;
			r->cache |= AK_SHL(v, 56 - r->n);
			r->n += 8;
		}
	}
}

/* bits (1..32 ; 0 rend tout le cache, comme l'asm) -> valeur */
static inline u64 rb_get(struct rb *r, u64 k)
{
	if (r->n < k)
		rb_fill(r);
	u64 v = AK_SHR(r->cache, 64 - k);
	r->cache = AK_SHL(r->cache, k);
	r->n -= k;
	return v;
}

/* bits (0..32) -> valeur signée */
static inline s64 rb_sget(struct rb *r, u64 k)
{
	if (!k)
		return 0;
	if (r->n < k)
		rb_fill(r);
	s64 v = AK_ASR(r->cache, 64 - k);
	r->cache = AK_SHL(r->cache, k);
	r->n -= k;
	return v;
}

static inline u64 rb_unary(struct rb *r)
{
	u64 z = 0;
	for (;;) {
		u64 l = ak_clz64(r->cache);
		if (l < r->n) {
			z += l;
			r->cache = AK_SHL(r->cache, l);
			r->cache = AK_SHL(r->cache, 1);
			r->n -= l + 1;
			return z;
		}
		z += r->n;
		r->cache = 0;
		r->n = 0;
		if ((uintptr_t)r->p > (uintptr_t)r->end + 8)
			return z;
		rb_fill(r);
	}
}

static inline void rb_align(struct rb *r)
{
	u64 k = r->n & 7;
	r->cache = AK_SHL(r->cache, k);
	r->n -= k;
}

static inline const u8 *rb_byteptr(const struct rb *r)
{
	return r->p - (r->n >> 3);
}

/* ---- petites aides */
static inline u16 ak_rd16le(const u8 *p)
{
	return (u16)(p[0] | (p[1] << 8));
}
static inline u32 ak_rd32le(const u8 *p)
{
	u32 v;
	__builtin_memcpy(&v, p, 4);
	return v;
}
static inline u64 ak_rd64le(const u8 *p)
{
	return ak_load64(p);
}
static inline u32 ak_rd32be(const u8 *p)
{
	return __builtin_bswap32(ak_rd32le(p));
}
/* divisions d'AArch64 : diviseur nul -> 0 */
static inline u64 ak_udiv(u64 a, u64 b)
{
	return b ? a / b : 0;
}
static inline s64 ak_sdiv(s64 a, s64 b)
{
	if (!b)
		return 0;
	if (b == -1)
		return (s64)(0 - (u64)a);
	return a / b;
}
static inline u32 ak_udivw(u32 a, u32 b)
{
	return b ? a / b : 0;
}
static inline s32 ak_sdivw(s32 a, s32 b)
{
	if (!b)
		return 0;
	if (b == -1)
		return (s32)(0 - (u32)a);
	return a / b;
}
/* smulh / umulh */
static inline s64 ak_smulh(s64 a, s64 b)
{
	return (s64)(((__int128)a * b) >> 64);
}
static inline u64 ak_umulh(u64 a, u64 b)
{
	return (u64)(((unsigned __int128)a * b) >> 64);
}

#endif
