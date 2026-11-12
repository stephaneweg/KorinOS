/* AudioKit : FFT complexe à radix mixtes (2, 3, 5) et IMDCT par une FFT de
 * N/4 points (Vorbis : N = 64..8192 ; Opus/CELT : N = 2 x 120..960, facteurs
 * 2, 3, 5) ; sinus et cosinus en double précision pour les tables
 * (ak_mdct.inc). Mêmes opérations dans le même ordre que l'asm (NEON : une
 * voie après l'autre ; fmla = multiplication-addition fusionnée).
 *
 * FFT (décimation temporelle itérative) : l'entrée est rangée à la place de
 * ses chiffres inverses (pos), puis chaque étage de radix p combine des
 * sous-transformées de taille m en blocs de taille L = m p :
 *	y[b + j + k m] = somme_q (y[b + j + q m] W_L^(j q)) W_p^(q k)
 * IMDCT de N points (N / 2 coefficients X, sortie y[0..N-1], sans fenêtre) :
 *	y[n] = somme_k X[k] cos(2 pi / N (n + 1/2 + N/4)(k + 1/2))
 * z[k] = (X[2k] + i X[N/2-1-2k]) e^(-i t_k), t_k = 2 pi (k + 1/8) / N,
 * Z = FFT(z) e^(-i t_k), puis y d'après les quarts de Z. */
#include "ak_mdct.h"

static const double sc_c[3] = {0.63661977236758134308, /* 2 / pi */
			       1.5707963267341256,     /* pi/2 (tête) */
			       6.077100506506192e-11}; /* pi/2 (reste) */
/* sinus : s1..s6 (-1/3!, 1/5!, ...), puis 1/15! (s7) ; [0] et [7] inutilisés */
static const double sc_s[8] = {0.0,
			       -0.16666666666666666,
			       0.008333333333333333,
			       -0.0001984126984126984,
			       2.7557319223985893e-06,
			       -2.505210838544172e-08,
			       1.6059043836821613e-10,
			       -7.647163731819816e-13};
/* cosinus : c1..c7 (-1/2!, 1/4!, ...) */
static const double sc_k[8] = {0.0,
			       -0.5,
			       0.041666666666666664,
			       -0.001388888888888889,
			       2.48015873015873e-05,
			       -2.755731922398589e-07,
			       2.08767569878681e-09,
			       -1.1470745597729725e-11};

static const double ft_2pi = 6.283185307179586;

void ak_sincos(double x, double *s, double *c)
{
	double k = ak_drintn(x * sc_c[0]);
	s64 q = (s64)k; /* (fcvtzs : k est entier) */
	double r = ak_dmsub(k, sc_c[1], x);
	r = ak_dmsub(k, sc_c[2], r); /* r dans [-pi/4, pi/4] */
	double r2 = r * r;
	/* sin r = r (1 + r^2 (s1 + r^2 (s2 + ...))) */
	double a = sc_s[6];
	for (int i = 5; i >= 1; i--)
		a = ak_dmadd(a, r2, sc_s[i]);
	a = a * r2;
	double sn = ak_dmadd(a, r, r);
	/* cos r = 1 + r^2 (c1 + r^2 (c2 + ...)) */
	double b = sc_k[6];
	for (int i = 5; i >= 1; i--)
		b = ak_dmadd(b, r2, sc_k[i]);
	b = b * r2;
	double cs = b + 1.0;
	switch (q & 3) {
	case 1: /* sin(r + pi/2) = cos r */
		*s = cs;
		*c = -sn;
		break;
	case 2:
		*s = -sn;
		*c = -cs;
		break;
	case 3:
		*s = -cs;
		*c = sn;
		break;
	default:
		*s = sn;
		*c = cs;
	}
}

long ak_fft_init(struct akd *d, struct akfft *p, u64 n)
{
	u64 f[17];
	u64 nf = 0, x = n;
	p->n = n;
	/* facteurs (2, 3, 5), rangés à l'envers dans p->f (ordre de calcul) */
	while (!(x & 1)) {
		if (x == 1)
			break;
		if (nf >= 17) /* (l'asm déborderait de sa pile : n = 0) */
			return AKE_INVAL;
		f[nf++] = 2;
		x >>= 1;
	}
	if (x & 1) {
		while (x % 3 == 0 && nf < 17) {
			f[nf++] = 3;
			x /= 3;
		}
		while (x % 5 == 0 && nf < 17) {
			f[nf++] = 5;
			x /= 5;
		}
		if (x != 1)
			return AKE_INVAL;
	}
	if (nf > 16)
		return AKE_INVAL;
	p->nf = nf;
	for (u64 s = 0; s < nf; s++)
		p->f[s] = f[nf - 1 - s];
	/* rangs : pos(i) = somme d_s * n / (f_0 ... f_s) */
	u32 *pos = akd_alloc(d, n << 2);
	if (!pos)
		return AKE_NOMEM;
	p->pos = pos;
	for (u64 i = 0; i < n; i++) {
		u64 v = i, mult = n, r = 0;
		for (u64 s = 0; s < nf; s++) {
			u64 dg = v % f[s]; /* chiffre */
			v /= f[s];
			mult /= f[s];
			r += dg * mult;
		}
		pos[i] = (u32)r;
	}
	/* W_n^k */
	float *w = akd_alloc(d, n << 3);
	if (!w)
		return AKE_NOMEM;
	p->w = w;
	double st = ft_2pi / (double)(s64)n; /* 2 pi / n */
	for (u64 k = 0; k < n; k++) {
		double sn, cs;
		ak_sincos(-((double)(s64)k * st), &sn, &cs);
		w[2 * k] = (float)cs;
		w[2 * k + 1] = (float)sn; /* -sin */
	}
	return 0;
}

static const u32 ft_c3[2] = {0xBF000000, 0x3F5DB3D7};			/* -1/2, sin(pi/3) */
static const u32 ft_c5[4] = {0x3E9E3779, 0x3F737871, 0xBF4F1BBD, 0x3F167918}; /* cos, sin de 2pi/5, 4pi/5 */

struct cpx {
	float re, im;
};

/* (a + ib)(c + id) = (ac - bd) + i(ad + bc), comme .cmul de l'asm */
static inline struct cpx cmul(struct cpx v, const float *w)
{
	struct cpx r;
	r.re = v.re * w[0] + -(v.im * w[1]);
	r.im = v.im * w[0] + v.re * w[1];
	return r;
}

static inline struct cpx ld(const float *p)
{
	struct cpx r = {p[0], p[1]};
	return r;
}
static inline void st(float *p, struct cpx v)
{
	p[0] = v.re;
	p[1] = v.im;
}

void ak_fft(const struct akfft *p, const float *in, float *out)
{
	u64 n = p->n;
	const u32 *pos = p->pos;
	u64 i = 0;
	do { /* rangement : y[pos(i)] = x[i] */
		u64 v;
		__builtin_memcpy(&v, in + 2 * i, 8);
		__builtin_memcpy(out + 2 * (u64)pos[i], &v, 8);
		i++;
	} while (i < n);
	const float *W = p->w;
	u64 L = 1;
	float c3a = ak_u2f(ft_c3[0]), c3b = ak_u2f(ft_c3[1]);
	float c1 = ak_u2f(ft_c5[0]), s1 = ak_u2f(ft_c5[1]), c2 = ak_u2f(ft_c5[2]), s2 = ak_u2f(ft_c5[3]);
	for (u64 s = 0; s < p->nf; s++) {
		u64 rdx = p->f[s];
		u64 m = L;
		L *= rdx;
		u64 step = n / L; /* pas de W : n / L */
		for (u64 b = 0; b < n; b += L) {
			for (u64 j = 0; j < m; j++) {
				float *y = out + 2 * (b + j);
				u64 wi = j * step;
				if (rdx == 2) {
					struct cpx v0 = ld(y), v1 = ld(y + 2 * m);
					if (wi)
						v1 = cmul(v1, W + 2 * wi);
					struct cpx a = {v0.re + v1.re, v0.im + v1.im};
					struct cpx d = {v0.re - v1.re, v0.im - v1.im};
					st(y, a);
					st(y + 2 * m, d);
				} else if (rdx == 3) {
					struct cpx v0 = ld(y), v1 = ld(y + 2 * m), v2 = ld(y + 4 * m);
					if (wi) {
						v1 = cmul(v1, W + 2 * wi);
						v2 = cmul(v2, W + 4 * wi);
					}
					/* y0 = v0 + v1 + v2 ; y1,2 = v0 - (v1 + v2)/2 -/+ i sin60 (v1 - v2) */
					struct cpx a = {v1.re + v2.re, v1.im + v2.im};
					struct cpx t = {v1.re - v2.re, v1.im - v2.im};
					struct cpx y0 = {v0.re + a.re, v0.im + a.im};
					v0.re = ak_fmadd(a.re, c3a, v0.re);
					v0.im = ak_fmadd(a.im, c3a, v0.im);
					t.re = t.re * c3b;
					t.im = t.im * c3b;
					struct cpx mi = {t.im, ak_fneg(t.re)}; /* -i t */
					st(y, y0);
					struct cpx o1 = {v0.re + mi.re, v0.im + mi.im};
					struct cpx o2 = {v0.re - mi.re, v0.im - mi.im};
					st(y + 2 * m, o1);
					st(y + 4 * m, o2);
				} else { /* radix 5 */
					struct cpx v0 = ld(y), v1 = ld(y + 2 * m), v2 = ld(y + 4 * m), v3 = ld(y + 6 * m),
						   v4 = ld(y + 8 * m);
					if (wi) {
						v1 = cmul(v1, W + 2 * wi);
						v2 = cmul(v2, W + 4 * wi);
						v3 = cmul(v3, W + 6 * wi);
						v4 = cmul(v4, W + 8 * wi);
					}
					/* a = v1 + v4, b = v1 - v4, c = v2 + v3, d = v2 - v3 */
					struct cpx a = {v1.re + v4.re, v1.im + v4.im};
					struct cpx bb = {v1.re - v4.re, v1.im - v4.im};
					struct cpx c = {v2.re + v3.re, v2.im + v3.im};
					struct cpx d = {v2.re - v3.re, v2.im - v3.im};
					struct cpx y0 = {v0.re + a.re, v0.im + a.im};
					y0.re = y0.re + c.re;
					y0.im = y0.im + c.im;
					struct cpx r1, t1, r2, t2;
					r1.re = ak_fmadd(c.re, c2, a.re * c1);
					r1.im = ak_fmadd(c.im, c2, a.im * c1);
					r1.re = r1.re + v0.re;
					r1.im = r1.im + v0.im;
					t1.re = ak_fmadd(d.re, s2, bb.re * s1); /* t1 = s1 b + s2 d */
					t1.im = ak_fmadd(d.im, s2, bb.im * s1);
					r2.re = ak_fmadd(c.re, c1, a.re * c2);
					r2.im = ak_fmadd(c.im, c1, a.im * c2);
					r2.re = r2.re + v0.re;
					r2.im = r2.im + v0.im;
					t2.re = ak_fmsub(d.re, s1, bb.re * s2); /* t2 = s2 b - s1 d */
					t2.im = ak_fmsub(d.im, s1, bb.im * s2);
					struct cpx m1 = {t1.im, ak_fneg(t1.re)}; /* -i t1 */
					struct cpx m2 = {t2.im, ak_fneg(t2.re)};
					st(y, y0);
					struct cpx o;
					o.re = r1.re + m1.re;
					o.im = r1.im + m1.im;
					st(y + 2 * m, o);
					o.re = r2.re + m2.re;
					o.im = r2.im + m2.im;
					st(y + 4 * m, o);
					o.re = r2.re - m2.re;
					o.im = r2.im - m2.im;
					st(y + 6 * m, o);
					o.re = r1.re - m1.re;
					o.im = r1.im - m1.im;
					st(y + 8 * m, o);
				}
			}
		}
	}
}

long ak_imdct_init(struct akd *d, struct akmdct *m, u64 n)
{
	m->n = n;
	u64 n4 = n >> 2;
	m->n4 = n4;
	long r = ak_fft_init(d, &m->fft, n4);
	if (r < 0)
		return r;
	float *tw = akd_alloc(d, (n4 << 3) * 3); /* 3 x N/4 complexes */
	if (!tw)
		return AKE_NOMEM;
	m->tw = tw;
	m->z = tw + 2 * n4;
	m->z2 = tw + 4 * n4;
	double st = ft_2pi / (double)(s64)m->n; /* 2 pi / N */
	for (u64 k = 0; k < n4; k++) {
		double sn, cs;
		ak_sincos(((double)(s64)k + 0.125) * st, &sn, &cs);
		tw[2 * k] = (float)cs;
		tw[2 * k + 1] = (float)sn; /* (cos t, sin t) */
	}
	return 0;
}

void ak_imdct(const struct akmdct *m, const float *x, float *y)
{
	u64 n4 = m->n4;
	const float *tw = m->tw;
	float *z = m->z;
	u64 n2 = m->n >> 1;
	u64 k = 0;
	do { /* z[k] = (X[2k] + i X[N/2-1-2k]) e^(-i t) */
		float re = x[2 * k], im = x[n2 - 1 - 2 * k];
		float c = tw[2 * k], s = tw[2 * k + 1];
		z[2 * k] = ak_fmadd(im, s, re * c); /* re c + im s + i (im c - re s) */
		z[2 * k + 1] = ak_fmsub(re, s, im * c);
		k++;
	} while (k < n4);
	ak_fft(&m->fft, z, m->z2);
	float *Z = m->z2;
	k = 0;
	do { /* Z *= e^(-i t) */
		float a = Z[2 * k], b = Z[2 * k + 1];
		float c = tw[2 * k], s = tw[2 * k + 1];
		Z[2 * k] = ak_fmadd(b, s, a * c);
		Z[2 * k + 1] = ak_fmsub(a, s, b * c);
		k++;
	} while (k < n4);
	/* sortie : quatre quarts (n8 = N/8, j < n8) */
	u64 n8 = n4 >> 1;
	for (u64 j = 0; j < n8; j++) {
		float *o = y + 2 * j;
		const float *za = Z + 2 * (n8 + j), *zb = Z + 2 * (n8 - 1 - j), *zc = Z + 2 * j,
			    *zd = Z + 2 * (n4 - 1 - j);
		o[0] = za[0]; /* quart 0 : re[n8+m], -im[n8-1-m] */
		o[1] = ak_fneg(zb[1]);
		o += n4;
		o[0] = zc[1]; /* quart 1 : im[m], -re[n4-1-m] */
		o[1] = ak_fneg(zd[0]);
		o += n4;
		o[0] = za[1]; /* quart 2 : im[n8+m], -re[n8-1-m] */
		o[1] = ak_fneg(zb[0]);
		o += n4;
		o[0] = ak_fneg(zc[0]); /* quart 3 : -re[m], im[n4-1-m] */
		o[1] = zd[1];
	}
}
