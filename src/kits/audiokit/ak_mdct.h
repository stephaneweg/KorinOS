/* AudioKit : FFT complexe à radix mixtes (2, 3, 5) et IMDCT (ak_mdct.c,
 * d'après ak_mdct.inc) ; sinus et cosinus en double pour les tables. */
#ifndef AK_MDCT_H
#define AK_MDCT_H

#include "ak.h"

/* FT_* : plan de FFT (FT_SIZE = 160) */
struct akfft {
	u64 n;	   /* 0 */
	u64 nf;	   /* 8 étages */
	u64 f[16]; /* 16 radix de chaque étage (ordre de calcul) */
	u32 *pos;  /* 144 rangs de l'entrée (n) */
	float *w;  /* 152 W_n^k = e^(-2 pi i k / n) : (cos, sin) x n */
};
#define FT_SIZE 160
_Static_assert(sizeof(struct akfft) == FT_SIZE, "akfft");

/* MD_* : plan d'IMDCT (MD_SIZE = 224) */
struct akmdct {
	u64 n;	   /* 0 N */
	u64 n4;	   /* 8 N / 4 */
	float *tw; /* 16 e^(-i t_k) : (cos, sin) x N/4 */
	float *z;  /* 24 tampons complexes de N/4 */
	float *z2; /* 32 */
	u64 pad[3];
	struct akfft fft; /* 64 FFT de N/4 */
};
#define MD_SIZE (64 + FT_SIZE)
_Static_assert(sizeof(struct akmdct) == MD_SIZE, "akmdct");

/* x (|x| < 2^20) -> *s = sin x, *c = cos x (double, comme l'asm) */
void ak_sincos(double x, double *s, double *c);
/* décodeur (mémoire : akd_alloc), plan, n -> 0 / < 0 */
long ak_fft_init(struct akd *d, struct akfft *p, u64 n);
/* plan, entrée (n complexes), sortie (n complexes, autre tampon) : y = FFT(x) */
void ak_fft(const struct akfft *p, const float *in, float *out);
/* décodeur, plan, N -> 0 / < 0 */
long ak_imdct_init(struct akd *d, struct akmdct *m, u64 n);
/* plan, X (N/2 flottants), y (N flottants) : IMDCT sans fenêtre */
void ak_imdct(const struct akmdct *m, const float *x, float *y);

#endif
