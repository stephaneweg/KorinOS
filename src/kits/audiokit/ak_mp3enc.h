/* AudioKit : encodeur MP3 (ak_mp3enc.c, d'après ak_mp3enc.inc). Point
 * d'entrée : mp3_enc_init (ak_enc.h). Disposition de l'état (ME_*) et
 * petites tables de l'asm (mêmes noms d'étiquettes ; le banc les compare). */
#ifndef AK_MP3ENC_H
#define AK_MP3ENC_H

#include "ak.h"

#define ME_SIZE 1024
#define ME_PCMCH 6528 /* par canal : 480 d'historique + 1152 (flottants) */
#define ME_XRG 2368   /* spectre d'un [gr][canal] (576 + 16 flottants) */
#define ME_A_CR 13056
#define ME_A_MT 15104
#define ME_A_WM 23296
#define ME_A_SBH 26176
#define ME_A_XR 35392
#define ME_A_X34 44864
#define ME_A_IX 49600
#define ME_A_STEP 54208
#define ME_A_MSB 55232
#define ME_A_PEND 63424
#define ME_A_ZERO 65472
#define ME_A_WK 67776
#define ME_ARRAYS 69824
#define ME_MSBN 8192
#define ME_PENDN 32
#define ME_INF (1 << 30)

/* travail du comptage (u32) */
#define ME_WK_E 1472  /* valeurs >= 15, cumulé par segment */
#define ME_WK_M 1568  /* maximum par segment */
#define ME_WK_B0 1664 /* région 0 [0, a) : coût, table */
#define ME_WK_B2 1808 /* région 2 [c, fin) : coût, table */

/* informations d'un granule (128 octets) */
struct mp3gi {
	u32 bits;   /* 0 part2_3_length */
	u32 bv;	    /* 4 big_values */
	u32 gg;	    /* 8 global_gain */
	u32 t[3];   /* 12 table_select */
	u32 r0, r1; /* 24 region0_count, region1_count */
	u32 c1t;    /* 32 count1table_select */
	u32 c1;	    /* 36 quadruplets */
	u32 p2;	    /* 40 bits des facteurs d'échelle (part2) */
	u32 sfc;    /* 44 scalefac_compress */
	u8 sf[24];  /* 48 facteurs d'échelle des bandes 0..20 */
	u8 sl[24];  /* 72 bits de chacun */
	u8 pad[32];
};
_Static_assert(sizeof(struct mp3gi) == 128, "mp3gi");
AK_STATIC_OFF(struct mp3gi, c1t, 32);
AK_STATIC_OFF(struct mp3gi, sfc, 44);
AK_STATIC_OFF(struct mp3gi, sl, 72);

/* trame en attente (64 octets) : en-tête et informations de côté, créneau */
struct mp3pend {
	u8 hs[40];  /* 0 en-tête (4), informations de côté */
	u64 cpos;   /* 40 début du créneau (flux principal) */
	u64 slot;   /* 48 octets du créneau */
	u64 n;	    /* 56 octets de hs */
};
_Static_assert(sizeof(struct mp3pend) == 64, "mp3pend");

/* ME_* : état (ME_SIZE), suivi des tableaux (ME_ARRAYS) */
struct mp3e {
	u64 nch;	     /* 0 */
	u64 ngr;	     /* 8 granules par trame (2 ; LSF : 1) */
	u64 spf;	     /* 16 échantillons par trame */
	u64 sri;	     /* 24 indice de la fréquence 0..8 */
	u64 lsf;	     /* 32 */
	u64 hdr;	     /* 40 en-tête (sans bourrage ni stéréo jointe) */
	u64 num;	     /* 48 144 (72) x débit */
	u64 rate;	     /* 56 */
	u64 acc;	     /* 64 reste cumulé (bourrage) */
	u64 rmax;	     /* 72 réservoir maximal (octets) */
	u64 side;	     /* 80 octets d'informations de côté */
	u64 fill;	     /* 88 échantillons de la trame en cours */
	u64 lim;	     /* 96 lignes gardées (passe-bas) */
	float t;	     /* 104 cible de global_gain */
	u32 tpad;
	u64 cpos;	     /* 112 flux principal : début du créneau de la trame */
	u64 ppos;	     /* 120 début des données de la trame */
	u64 base;	     /* 128 position du premier octet de msb */
	u64 npend;	     /* 136 trames en attente */
	u64 ms;		     /* 144 trame en M/S */
	float *pcm;	     /* 152 */
	float *cr;	     /* 160 fenêtre d'analyse (512) */
	float *mt;	     /* 168 matrice d'analyse (64 x 32) */
	float *wm;	     /* 176 MDCT (36 x 20) */
	float *sbh;	     /* 184 sous-bandes [canal][32][36] */
	float *xr;	     /* 192 [gr][canal] x ME_XRG */
	float *x34;	     /* 200 |xr|^3/4 [canal] x ME_XRG */
	u32 *ix;	     /* 208 valeurs quantifiées [canal] x 576 */
	float *step;	     /* 216 2^(-3/16 (g - 210)) */
	u8 *msb;	     /* 224 flux principal (ME_MSBN) */
	struct mp3pend *pend; /* 232 trames en attente (ME_PENDN) */
	s16 *zero;	     /* 240 576 trames stéréo nulles */
	const u16 *sfb;	     /* 248 débuts des bandes longues */
	u32 *wk;	     /* 256 travail du comptage */
	u64 used;	     /* 264 bits de la trame déjà pris */
	u64 avail;	     /* 272 bits disponibles */
	u64 meangr;	     /* 280 part d'un granule */
	u64 wacc;	     /* 288 écrivain de bits */
	u64 wn;		     /* 296 */
	u8 *wptr;	     /* 304 */
	u8 pad[512 - 312];
	struct mp3gi gi[4]; /* 512 [gr][canal] */
};
_Static_assert(sizeof(struct mp3e) == ME_SIZE, "mp3e");
AK_STATIC_OFF(struct mp3e, t, 104);
AK_STATIC_OFF(struct mp3e, pcm, 152);
AK_STATIC_OFF(struct mp3e, sfb, 248);
AK_STATIC_OFF(struct mp3e, wptr, 304);
AK_STATIC_OFF(struct mp3e, gi, 512);

/* tables (bits des flottants) */
extern const u64 mp3e_kd[6];
extern const u32 mp3e_kf[9];
extern const u32 mp3e_amp[16];
extern const u32 mp3e_sgn[4];
extern const u32 mp3e_rates[9];
extern const u16 mp3e_br1[15];
extern const u16 mp3e_br2[15];
extern const u16 mp3e_lp[20];
extern const u8 mp3e_sfc1[32];
extern const u8 mp3e_part1[3];
extern const u8 mp3e_part2[5];

#endif
