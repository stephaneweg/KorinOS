/* AudioKit : Opus (ak_opus.c, d'après ak_opus.inc) : décodeur de plage (RFC
 * 6716 4.1), couche Opus (TOC, trames d'un paquet), en-têtes Ogg Opus
 * (OpusHead, OpusTags), API par paquets, écrêtage doux. SILK : ak_silk.h ;
 * CELT : ak_celt.h. Les dispositions des états sont celles de l'asm (EC_*,
 * OPS_*, OD_*). */
#ifndef AK_OPUS_H
#define AK_OPUS_H

#include "ak.h"
#include "ak_mdct.h"

/* ---- décodeur de plage (EC_*, EC_SIZE = 112). Les champs de 32 bits de
 * l'asm (rng, val...) sont rangés sur 64 bits (str x), lus sur 32 (ldr w). */
struct ec_dec {
	const u8 *buf;	/* 0 début des données */
	const u8 *ptr;	/* 8 prochain octet lu par l'avant */
	const u8 *end;	/* 16 fin des données (les octets de fin : bits bruts) */
	const u8 *eptr; /* 24 prochain octet lu par la fin (décroissant) */
	u64 storage;	/* 32 octets */
	u64 endwin;	/* 40 bits bruts en attente (32 bits) */
	u64 nendbits;	/* 48 */
	u64 nbits;	/* 56 bits lus en tout (nbits_total) */
	u64 rng;	/* 64 */
	u64 val;	/* 72 */
	u64 ext;	/* 80 */
	u64 rem;	/* 88 */
	u64 err;	/* 96 */
	u64 pad;	/* 104 */
};
#define EC_SIZE 112
_Static_assert(sizeof(struct ec_dec) == EC_SIZE, "ec_dec");
AK_STATIC_OFF(struct ec_dec, storage, 32);
AK_STATIC_OFF(struct ec_dec, nbits, 56);
AK_STATIC_OFF(struct ec_dec, rng, 64);
AK_STATIC_OFF(struct ec_dec, err, 96);

/* décodeur, données, octets */
void ec_init(struct ec_dec *ec, const u8 *buf, u64 n);
/* retire n octets de la fin (trame redondante ; aucun bit brut lu) */
void ec_shrink(struct ec_dec *ec, u64 n);
/* renormalise (rng > 2^23) */
void ec_norm(struct ec_dec *ec);
/* ft -> symbole cumulé (ec_decode) */
u32 ec_decode(struct ec_dec *ec, u32 ft);
/* bits -> symbole cumulé (ec_decode_bin) */
u32 ec_decode_bin(struct ec_dec *ec, u32 bits);
/* fl, fh, ft (ec_dec_update) */
void ec_update(struct ec_dec *ec, u32 fl, u32 fh, u32 ft);
/* logp -> 1 (probabilité 1/2^logp) / 0 */
u32 ec_bit_logp(struct ec_dec *ec, u32 logp);
/* table icdf (octets), ftb -> symbole */
u32 ec_icdf(struct ec_dec *ec, const u8 *icdf, u32 ftb);
/* table icdf (16 bits), ftb -> symbole */
u32 ec_icdf16(struct ec_dec *ec, const u16 *icdf, u32 ftb);
/* bits (0..25) -> bits bruts lus par la fin */
u32 ec_bits(struct ec_dec *ec, u32 bits);
/* ft (> 1) -> entier uniforme dans [0, ft) (err = 1 si trop grand) */
u32 ec_uint(struct ec_dec *ec, u32 ft);
/* -> bits lus (ec_tell) */
s64 ec_tell(const struct ec_dec *ec);
/* -> huitièmes de bits lus (ec_tell_frac) */
s64 ec_tell_frac(const struct ec_dec *ec);
/* fs (probabilité de 0, Q15), décroissance -> valeur de Laplace (rendue en
 * 32 bits, comme l'asm : w0) */
s32 ec_laplace(struct ec_dec *ec, u32 fs, u32 decay);

/* ---- un flux Opus (OD_*, OD_SIZE = 2240) */
#define OPM_SILK 1
#define OPM_HYBRID 2
#define OPM_CELT 3
#define OPS_MAXF 5760 /* 120 ms à 48 kHz */

struct celt;
struct silk;

struct od {
	u64 cc;		  /* 0 canaux du flux (1, 2) */
	u64 mode;	  /* 8 mode de la trame : OPM_* */
	u64 prevmode;	  /* 16 mode de la trame précédente (0 : aucune) */
	u64 endband;	  /* 24 (inutilisé, comme l'asm) */
	u64 fs;		  /* 32 échantillons par trame (48 kHz) */
	u64 sc;		  /* 40 canaux de la trame */
	u64 prevred;	  /* 48 la trame précédente finissait par une redondance CELT */
	struct celt *ce;  /* 56 état CELT */
	struct silk *sk;  /* 64 état SILK */
	float *pcm[2];	  /* 72 plans de sortie (OPS_MAXF + 64) */
	u64 bw;		  /* 88 largeur de bande : 0 NB, 1 MB, 2 WB, 3 SWB, 4 FB */
	struct ec_dec ec; /* 96 décodeur de plage de la trame */
	struct ec_dec ec2; /* 208 celui de la trame redondante */
	float tr[2][240]; /* 320 trame redondante (2 x 240 flottants) */
};
#define OD_SIZE 2240
_Static_assert(sizeof(struct od) == OD_SIZE, "od");
AK_STATIC_OFF(struct od, ce, 56);
AK_STATIC_OFF(struct od, pcm, 72);
AK_STATIC_OFF(struct od, ec, 96);
AK_STATIC_OFF(struct od, ec2, 208);
AK_STATIC_OFF(struct od, tr, 320);

/* ---- état Ogg Opus / API par paquets (OPS_*) */
struct ops {
	u64 ch;		  /* 0 canaux de sortie */
	u64 ns;		  /* 8 flux */
	u64 nc;		  /* 16 flux couplés (stéréo), les premiers */
	float gain;	  /* 24 gain linéaire */
	u32 pad0;
	u64 hasgain;	  /* 32 */
	u8 map[8];	  /* 40 table des canaux */
	struct od *od[8]; /* 48 flux */
	float clip[8];	  /* 112 écrêtage doux : mémoire de chaque canal */
	u8 pad1[16];
	struct akmdct md[4]; /* 160 IMDCT de CELT : N = 240, 480, 960, 1920 */
};
#define OPS_SIZE (160 + 4 * MD_SIZE)
_Static_assert(sizeof(struct ops) == OPS_SIZE, "ops");
AK_STATIC_OFF(struct ops, hasgain, 32);
AK_STATIC_OFF(struct ops, map, 40);
AK_STATIC_OFF(struct ops, od, 48);
AK_STATIC_OFF(struct ops, clip, 112);
AK_STATIC_OFF(struct ops, md, 160);

/* décodeur -> état Opus (paquets : AKD_CODEC ; Ogg : OG_STATE) */
struct ops *ops_state(struct akd *d);
/* décodeur, paquet OpusHead, octets -> 0 / < 0 (dans Ogg : avance, pré-roll) */
long ops_head(struct akd *d, const u8 *head, u64 n);
/* décodeur, paquet OpusTags, octets (étiquettes) */
void ops_tags(struct akd *d, const u8 *p, u64 n);
/* décodeur, paquet, octets -> trames (48 kHz, dans AKD_PCM) / < 0 */
long ops_packet(struct akd *d, const u8 *pkt, u64 n);
/* décodeur : oublie l'historique (après une recherche) */
void ops_reset(struct akd *d);
/* ops_init_pkt : ak.h */

/* plan (flottants), trames, mémoire du canal : écrêtage doux (opus_pcm_soft_clip) */
void ops_softclip(float *x, u32 n, float *mem);

#endif
