/* AudioKit : CELT (RFC 6716 4.3), décodeur (ak_celt.c, d'après ak_celt.inc) ;
 * tables (ak_opustab.c, d'après ak_opustab.inc et la fin de ak_celt.inc).
 *
 * L'état garde la disposition de l'asm (CE_*). La couche Opus (ak_opus.c) pose
 * CE_START, CE_END, CE_OUT, CE_ACCUM et appelle celt_new, celt_reset,
 * celt_decode, ce_exp2, ce_window. L'encodeur Opus (ak_opusenc.inc) appelle
 * ce_cache, ce_b2p, ce_p2b, ce_getp, ce_bexcos, ce_log2tan, ce_rot1 et lit
 * les tables ce_* (voir plus bas : les registres de l'asm de chaque fonction). */
#ifndef AK_CELT_H
#define AK_CELT_H

#include "ak.h"
#include "ak_mdct.h"

struct ec_dec;

#define CE_MEMN (2048 + 120) /* flottants d'historique par canal */
#define CE_NBE 21	     /* bandes */

/* CE_* (CE_SIZE = 2112) */
struct celt {
	u64 cc;		 /* 0 canaux de sortie (1, 2) */
	u64 c;		 /* 8 canaux de la trame */
	u64 start;	 /* 16 première bande (17 en hybride) */
	u64 end;	 /* 24 bande de fin (largeur de bande) */
	u64 n;		 /* 32 échantillons de la trame */
	u64 lm;		 /* 40 log2(N / 120) */
	u64 len;	 /* 48 octets de la trame */
	u64 r56;	 /* 56 (inutilisé) */
	u64 seed;	 /* 64 graine (rng final de la trame précédente ; 32 bits utiles) */
	s64 pfper;	 /* 72 post-filtre : période, gain, jeu de prises */
	s64 pfperold;	 /* 80 */
	float pfgain;	 /* 88 (flottants ; 4 octets de bourrage à zéro) */
	u32 pfgain_pad;
	float pfgainold; /* 96 */
	u32 pfgainold_pad;
	u64 pftap;	   /* 104 */
	u64 pftapold;	   /* 112 */
	float preemph[2];  /* 120 mémoire de la désaccentuation */
	float *out[2];	   /* 128 sortie de la trame (flottants -1..1) */
	float *mem[2];	   /* 144 historique : 2 x CE_MEMN flottants (un seul bloc) */
	float *x;	   /* 160 spectres normalisés (2 x 960) */
	float *norm;	   /* 168 repliement : norm, norm2 (2 x 1024) */
	float *freq;	   /* 176 spectres dénormalisés (2 x 960) */
	float *tmp;	   /* 184 IMDCT, permutations (3 x 960) */
	u32 *ut;	   /* 192 table U(n, k) de CWRS (2048 mots) */
	s32 *iy;	   /* 200 impulsions (256 mots) */
	struct akmdct *md; /* 208 IMDCT (4 plans : N = 240 << k) */
	u64 silence;	   /* 216 */
	u64 trans;	   /* 224 */
	u64 intra;	   /* 232 */
	u64 spread;	   /* 240 */
	u64 trim;	   /* 248 */
	u64 intens;	   /* 256 bande d'intensité */
	u64 dual;	   /* 264 double stéréo */
	u64 coded;	   /* 272 bandes codées */
	s64 balance;	   /* 280 */
	u64 antirsv;	   /* 288 réserve d'anti-effondrement (0, 8) */
	u64 pfpitch;	   /* 296 post-filtre de la trame */
	float pfg;	   /* 304 */
	u32 pfg_pad;
	u64 pftapn;	   /* 312 */
	u64 b;		   /* 320 blocs (M si transitoire, sinon 1) */
	u64 i;		   /* 328 bande courante */
	s64 tfc;	   /* 336 tf_change de la bande */
	s64 rem;	   /* 344 remaining_bits */
	s64 total;	   /* 352 bits de la trame (1/8, après dynalloc) */
	u64 accum;	   /* 360 1 : la sortie s'ajoute à CE_OUT (hybride : SILK déjà là) */
	u8 r368[512 - 368];
	float olde[48];	    /* 512 énergies (log2, 2 x 21) */
	float oldloge[48];  /* 704 */
	float oldloge2[48]; /* 896 */
	s32 tfres[24];	    /* 1088 tableaux de 21 entiers (32 bits) */
	s32 cap[24];	    /* 1184 */
	s32 offs[24];	    /* 1280 */
	s32 pulses[24];	    /* 1376 */
	s32 fineq[24];	    /* 1472 */
	s32 prio[24];	    /* 1568 */
	s32 thresh[24];	    /* 1664 */
	s32 trimoff[24];    /* 1760 */
	s32 bits1[24];	    /* 1856 */
	s32 bits2[24];	    /* 1952 */
	u8 cmask[64];	    /* 2048 masques d'effondrement (bande x C + canal) */
};
#define CE_SIZE 2112
_Static_assert(sizeof(struct celt) == CE_SIZE, "celt");
AK_STATIC_OFF(struct celt, start, 16);
AK_STATIC_OFF(struct celt, end, 24);
AK_STATIC_OFF(struct celt, seed, 64);
AK_STATIC_OFF(struct celt, pfgain, 88);
AK_STATIC_OFF(struct celt, pfgainold, 96);
AK_STATIC_OFF(struct celt, preemph, 120);
AK_STATIC_OFF(struct celt, out, 128);
AK_STATIC_OFF(struct celt, mem, 144);
AK_STATIC_OFF(struct celt, md, 208);
AK_STATIC_OFF(struct celt, silence, 216);
AK_STATIC_OFF(struct celt, pfpitch, 296);
AK_STATIC_OFF(struct celt, pfg, 304);
AK_STATIC_OFF(struct celt, b, 320);
AK_STATIC_OFF(struct celt, total, 352);
AK_STATIC_OFF(struct celt, accum, 360);
AK_STATIC_OFF(struct celt, olde, 512);
AK_STATIC_OFF(struct celt, oldloge, 704);
AK_STATIC_OFF(struct celt, oldloge2, 896);
AK_STATIC_OFF(struct celt, tfres, 1088);
AK_STATIC_OFF(struct celt, cap, 1184);
AK_STATIC_OFF(struct celt, offs, 1280);
AK_STATIC_OFF(struct celt, pulses, 1376);
AK_STATIC_OFF(struct celt, fineq, 1472);
AK_STATIC_OFF(struct celt, prio, 1568);
AK_STATIC_OFF(struct celt, thresh, 1664);
AK_STATIC_OFF(struct celt, trimoff, 1760);
AK_STATIC_OFF(struct celt, bits1, 1856);
AK_STATIC_OFF(struct celt, bits2, 1952);
AK_STATIC_OFF(struct celt, cmask, 2048);

/* ---- décodeur (couche Opus) */

/* x0 = décodeur (mémoire), x1 = canaux, x2 = IMDCT (4 plans : N = 240 << k)
 * -> état (long : < 0 = AKE_NOMEM) */
long celt_new(struct akd *d, u64 cc, struct akmdct *md);
/* x0 = état : oublie l'historique (OPUS_RESET_STATE) */
void celt_reset(struct celt *ce);
/* x0 = état (start, end, out, accum posés), x1 = octets, x2 = N (120, 240,
 * 480, 960 ; sinon AKE_CORRUPT), x3 = canaux de la trame, x4 = décodeur de
 * plage (initialisé sur la trame) -> N / < 0 ; sortie dans out */
long celt_decode(struct celt *ce, u64 len, u64 n, u64 c, struct ec_dec *ec);
/* s0 = x -> s0 = 2^x (double en interne ; 0 sous 2^-1000, FLT_MAX au-dessus
 * de 2^1000) */
float ce_exp2(float x);

/* ---- outils partagés avec l'encodeur Opus (ak_opusenc.inc : bl ce_*).
 * Dans l'asm, ces fonctions ne suivent pas toutes l'AAPCS : elles ne touchent
 * que les registres dits ; l'appelant en asm garde ses valeurs ailleurs. En C,
 * des fonctions ordinaires. */

/* ce_cache : w0 = bande, w1 = LM (-1..3) -> x0 = cache des impulsions de la
 * bande (cache[0] = q max, cache[q] = bits - 1, en 1/8) ; ne touche que x0-x2 */
const u8 *ce_cache(u32 band, s32 lm);
/* ce_b2p (bits2pulses) : w0 = bande, w1 = LM, w2 = bits (1/8) -> w0 = q ;
 * ne touche que x0-x7 */
u32 ce_b2p(u32 band, s32 lm, s32 bits);
/* ce_p2b (pulses2bits) : w0 = bande, w1 = LM, w2 = q -> w0 = bits (1/8 ; 0
 * si q = 0) ; ne touche que x0-x7 */
u32 ce_p2b(u32 band, s32 lm, u32 q);
/* ce_getp (get_pulses) : w0 = q -> w0 = impulsions K ; ne touche que x0-x2 */
u32 ce_getp(u32 q);
/* ce_bexcos (bitexact_cos) : w0 = x (0..16384) -> w0 = cos (Q15, 32 bits) ;
 * ne touche que x0-x4 */
s32 ce_bexcos(s32 x);
/* ce_log2tan (bitexact_log2tan) : w0 = isin, w1 = icos -> w0 = log2(isin /
 * icos) en Q11 (32 bits ; l'asm le rend tel quel, l'appelant fait sxth) ; ne
 * touche que x0-x6 */
s32 ce_log2tan(s32 isin, s32 icos);
/* ce_rot1 (exp_rotation1) : x0 = X, w1 = longueur, w2 = pas, s0 = c, s1 = s :
 * deux passes de rotations de Givens ; ne touche que x0-x5, v0-v5 */
void ce_rot1(float *x, u32 len, u32 stride, float c, float s);

/* ---- tables (ak_opustab.c ; disposition de l'asm, sans adresse) */
extern const float ce_window[120];   /* fenêtre de recouvrement */
extern const float ce_emeans[25];    /* eMeans */
extern const u16 ce_eband[22];	     /* eband5ms */
extern const u16 ce_logn[21];	     /* logN400 */
extern const s16 ce_cindex[105];     /* [LM + 1][21] -> décalage dans ce_cbits */
extern const u8 ce_balloc[231];	     /* [11][21] */
extern const u8 ce_cbits[392];	     /* cache_bits50 */
extern const u8 ce_ccaps[168];	     /* [LM][C][21] */
extern const u8 ce_eprob[336];	     /* [LM][intra][42] */
extern const u8 ce_log2frac[24];
extern const double ce_halfpi;	     /* pi / 2 */
extern const float ce_pred[9];	     /* coef[LM], beta[LM], beta intra */
extern const float ce_combg[9];	     /* [jeu][3] */
extern const u16 ce_exp2t8[8];	     /* 2^(k/8), Q14 */
extern const s8 ce_tfsel[32];	     /* [LM][isTransient][tf_select][tf_changed] */
extern const u8 ce_spread_icdf[4];
extern const u8 ce_trim_icdf[11];
extern const u8 ce_tapset_icdf[3];
extern const u8 ce_small_icdf[3];
extern const u8 ce_spreadf[3];
extern const u8 ce_ordery[30];
extern const u8 ce_bitil[16];
extern const u8 ce_bitdi[16];

#endif
