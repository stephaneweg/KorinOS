/*
 * kernels.cpp -- les calculs de Fractales : kernels.inc (NEON, deux points a la fois en double precision) et les
 * couleurs de mandelbrot.asm, en C++ portable. Memes operations dans le meme ordre, memes constantes, memes
 * approximations (log2 et sinus par polynome, pas ceux de la libm) : les memes bits que l'asm sur le Pi.
 *
 * Les points vont par quatre (deux paires de l'asm) dans des vecteurs de GCC (vector_size) : NEON sur le Pi, SSE2
 * sur le PC, sans intrinseque ni asm. Chaque voie fait exactement le calcul d'une voie de l'asm ; les x des points
 * s'obtiennent par la meme addition repetee (la paire avance de deux pas).
 *
 * Pas de contraction : a * b + c reste deux arrondis, sauf la ou l'asm fait fmla / fmls (un seul arrondi : fmad).
 * Sur le PC sans FMA (x86-64 de base), fmad fait deux arrondis : les derniers bits peuvent y differer (Newton,
 * Multibrot 4 et 5, Phoenix, attracteurs).
 */
#pragma GCC optimize ("O3", "fp-contract=off")
/* (les vecteurs de 32 octets ne passent qu'entre fonctions static inline de ce fichier : pas d'ABI en jeu sur le PC
 * sans AVX) */
#pragma GCC diagnostic ignored "-Wpsabi"
#include <string.h>
#include <limits.h>
#include "fractal.h"

typedef double v4d __attribute__ ((vector_size (32)));
typedef long long v4l __attribute__ ((vector_size (32)));
typedef double v2d __attribute__ ((vector_size (16)));
typedef long long v2l __attribute__ ((vector_size (16)));

/* .25, 1, 1/16, 3, seuil de fuite, epsilon de Newton (k_tab) */
#define K_Q		0.25
#define K_BULB		0.0625
#define K_ESC		1.0e6
#define K_EPS		1.0e-10
/* log2 (1 + t) = t (a1 + t (a2 + t (a3 + t (a4 + t a5)))) (k_log) */
#define LA1		1.4417403028805735
#define LA2		-0.7077701799616332
#define LA3		0.4123442155932546
#define LA4		-0.19031902828119998
#define LA5		0.04400468976900496
/* 1 / 2 pi, 2 pi, sin : s1, s3, s5, s7, s9 sur (-pi, pi), pi / 2 (k_sin) */
#define KS_INV2PI	0.15915494309189535
#define KS_2PI		6.283185307179586
#define KS_S1		0.9999845756675534
#define KS_S3		-0.166632562538804
#define KS_S5		0.008312374042022508
#define KS_S7		-0.0001931603634860011
#define KS_S9		2.173132085161652e-06
#define KS_PIO2		1.5707963267948966
/* 3 / 2^26, 2, 1/16 (k_bud) */
#define KB_SCALE	4.470348358154297e-08
#define KB_TWO		2.0

/* a * b + c en un seul arrondi (fmla / fmls de l'asm) */
static inline double fmad (double a, double b, double c)
{
#if defined(__aarch64__) || defined(__FMA__)
	return __builtin_fma (a, b, c);
#else
	return a * b + c;
#endif
}
template <class V> static inline V vfma (V a, V b, V c)
{
	V r;
	for (unsigned i = 0; i < sizeof (V) / sizeof (double); i++)
		r[i] = fmad (a[i], b[i], c[i]);
	return r;
}

template <class V> static inline V vabs (V a)
{
	typedef long long L __attribute__ ((vector_size (sizeof (V))));
	return (V) ((L) a & 0x7FFFFFFFFFFFFFFFLL);
}

/* fcvtzs / fcvtzu : vers zero, satures, NaN -> 0 (comme AArch64, sur toutes les cibles) */
static inline long cvt_s64 (double v)
{
	if (v != v)
		return 0;
	if (v >= 9223372036854775807.0)
		return LONG_MAX;
	if (v <= -9223372036854775808.0)
		return LONG_MIN;
	return (long) v;
}
static inline int32_t cvt_s32f (float v)
{
	if (v != v)
		return 0;
	if (v >= 2147483647.0f)
		return INT32_MAX;
	if (v <= -2147483648.0f)
		return INT32_MIN;
	return (int32_t) v;
}
static inline uint32_t cvt_u32f (float v)
{
	if (!(v > 0.0f))
		return 0;
	if (v >= 4294967295.0f)
		return 0xFFFFFFFFu;
	return (uint32_t) v;
}

/* d > 0 -> log2 (d), a 2e-5 pres (le polynome vaut 0 et 1 aux bouts : la valeur lissee ne saute pas) */
double log2d (double x)
{
	uint64_t b;
	memcpy (&b, &x, 8);
	long e = (long) ((b >> 52) & 0x7FF) - 1023;
	b = (b & 0x000FFFFFFFFFFFFFull) | (0x3FFull << 52);
	double t;
	memcpy (&t, &b, 8);
	t = t - 1.0;
	double p = t * LA5;
	p = p + LA4;
	p = p * t;
	p = p + LA3;
	p = p * t;
	p = p + LA2;
	p = p * t;
	p = p + LA1;
	p = p * t;
	return p + (double) e;
}

/* (la meme chose sur quatre voies : vlog2 de k_lyap) */
static inline v4d vlog2 (v4d x)
{
	typedef unsigned long long v4u __attribute__ ((vector_size (32)));
	v4u b = (v4u) x;
	v4l e = (v4l) (b >> 52) - 1023;
	v4d ed = __builtin_convertvector (e, v4d);
	v4d t = (v4d) ((b & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull);
	t = t - 1.0;
	v4d p = (v4d) {} + LA5;
	p = p * t;
	p = p + LA4;
	p = p * t;
	p = p + LA3;
	p = p * t;
	p = p + LA2;
	p = p * t;
	p = p + LA1;
	p = p * t;
	return p + ed;
}

/* n + 1 - log_d (log2 |z|) : la valeur lissee du temps de fuite */
static inline float fin_smooth (double zr, double zi, unsigned long n)
{
	double s = zr * zr;
	double t = zi * zi;
	s = s + t;
	s = log2d (s);				/* log2 |z|^2 */
	s = s * 0.5;
	s = log2d (s);
	s = s * g_ild;
	double v = (double) n + 1.0;
	v = v - s;
	v = v > 0.0 ? v : 0.0;			/* (fmaxnm : NaN -> 0) */
	return (float) v;
}

/* racine * 1000 + iterations */
static inline float fin_newton (double zr, double zi, unsigned long n)
{
	unsigned long r = 0;
	if (!(zr > 0.0))
	{
		r = 1;
		if (!(zi > 0.0))
			r = 2;
	}
	return (float) (r * 1000 + n);
}

/*
 * esc_row : le patron des fractales a temps de fuite. Une voie finie garde son z ; on regarde toutes les 8
 * iterations si toutes le sont. Le compte d'une voie : les iterations faites avant sa fuite.
 */
template <int T>
static void esc_row (float *out, long n, long maxit, double x0, double y, double d)
{
	const double d2 = d + d;
	double xa = x0, xb = x0 + d;		/* la paire de l'asm */
	const v4d vy = (v4d) {} + y;
	const v4d gcr = (v4d) {} + g_cr, gci = (v4d) {} + g_ci;
	for (; n > 0; n -= 4, out += 4)
	{
		v4d px;
		px[0] = xa;
		px[1] = xb;
		xa = xa + d2;
		xb = xb + d2;
		px[2] = xa;
		px[3] = xb;
		xa = xa + d2;
		xb = xb + d2;
		v4d zr, zi, cr = px, ci = vy, pr = {}, pi = {};
		v4l skip = {};
		if constexpr (T == T_MANDEL)
		{
			/* deux points (une paire) dans la cardioide ou le grand disque : pas de calcul */
			v4d a = px - K_Q;
			v4d q = a * a;
			v4d y2 = vy * vy;
			q = q + y2;
			v4d u = q + a;
			u = u * q;
			v4d w = y2 * K_Q;
			v4l in = w > u;
			v4d b = px + 1.0;
			b = b * b;
			b = b + y2;
			in |= K_BULB > b;
			long long s0 = in[0] & in[1], s1 = in[2] & in[3];
			skip = (v4l) { s0, s0, s1, s1 };
			if (s0 && s1)
			{
				out[0] = out[1] = out[2] = out[3] = -1.0f;
				continue;
			}
		}
		if constexpr (T == T_JULIA)
		{
			zr = px;
			zi = vy;
			cr = gcr;
			ci = gci;
		}
		else if constexpr (T == T_PHOENIX)
		{
			zr = vy;			/* (retournee : la forme connue, debout) */
			zi = px;
			cr = gcr;
			ci = gci;
		}
		else if constexpr (T == T_NEWTON)
		{
			zr = px;
			zi = vy;
		}
		else
			zr = zi = (v4d) {};
		v4l done = skip, cnt = {};
		for (long k = maxit;;)
		{
			v4d nr, ni;
			if constexpr (T == T_NEWTON)
			{
				/* z^3 - 1 : f, z^2 ; fini quand |f|^2 < epsilon. Puis z - f / (3 z^2) */
				v4d r2 = zr * zr, i2 = zi * zi;
				v4d sr = r2 - i2;
				v4d si = zr * zi;
				si = si + si;
				v4d fr = sr * zr;
				fr = vfma (-si, zi, fr);
				fr = fr - 1.0;
				v4d fi = sr * zi;
				fi = vfma (si, zr, fi);
				v4d m = fr * fr;
				m = vfma (fi, fi, m);
				done |= K_EPS > m;
				cnt += 1;
				cnt += done;
				v4d dn = sr * sr;
				dn = vfma (si, si, dn);
				dn = dn * 3.0;
				nr = fr * sr;
				nr = vfma (fi, si, nr);
				nr = nr / dn;
				ni = fi * sr;
				ni = vfma (-fr, si, ni);
				ni = ni / dn;
				nr = zr - nr;
				ni = zi - ni;
			}
			else
			{
				v4d r2 = zr * zr, i2 = zi * zi;
				v4d m = r2 + i2;
				done |= m > K_ESC;
				cnt += 1;
				cnt += done;
				if constexpr (T == T_MANDEL || T == T_JULIA)
				{
					nr = r2 - i2;
					nr = nr + cr;
					ni = zr * zi;
					ni = ni + ni;
					ni = ni + ci;
				}
				else if constexpr (T == T_SHIP)
				{
					nr = r2 - i2;
					nr = nr + cr;
					ni = zr * zi;
					ni = vabs (ni);
					ni = ni + ni;
					ni = ni + ci;
				}
				else if constexpr (T == T_TRICORN)
				{
					nr = r2 - i2;
					nr = nr + cr;
					ni = zr * zi;
					ni = ni + ni;
					ni = ci - ni;
				}
				else if constexpr (T == T_M3)
				{
					v4d t = i2 * 3.0;
					t = r2 - t;
					nr = zr * t;
					nr = nr + cr;
					t = r2 * 3.0;
					t = t - i2;
					ni = zi * t;
					ni = ni + ci;
				}
				else if constexpr (T == T_M4)
				{
					v4d sr = r2 - i2;
					v4d si = zr * zi;
					si = si + si;
					nr = sr * sr;
					nr = vfma (-si, si, nr);
					nr = nr + cr;
					ni = sr * si;
					ni = ni + ni;
					ni = ni + ci;
				}
				else if constexpr (T == T_M5)
				{
					v4d sr = r2 - i2;		/* z^2 */
					v4d si = zr * zi;
					si = si + si;
					v4d qr = sr * sr;		/* z^4 */
					qr = vfma (-si, si, qr);
					v4d qi = sr * si;
					qi = qi + qi;
					nr = qr * zr;			/* z^5 */
					nr = vfma (-qi, zi, nr);
					nr = nr + cr;
					ni = qr * zi;
					ni = vfma (qi, zr, ni);
					ni = ni + ci;
				}
				else if constexpr (T == T_PHOENIX)
				{
					nr = r2 - i2;
					nr = nr + cr;
					nr = vfma (ci, pr, nr);
					ni = zr * zi;
					ni = ni + ni;
					ni = vfma (ci, pi, ni);
					pr = done ? pr : zr;
					pi = done ? pi : zi;
				}
			}
			zr = done ? zr : nr;
			zi = done ? zi : ni;
			if (--k == 0)
				break;
			if (!(k & 7) && (done[0] & done[1] & done[2] & done[3]))
				break;
		}
		for (int l = 0; l < 4; l++)
		{
			if (skip[l] || !done[l])
				out[l] = -1.0f;		/* dedans */
			else if constexpr (T == T_NEWTON)
				out[l] = fin_newton (zr[l], zi[l], (unsigned long) cnt[l]);
			else
				out[l] = fin_smooth (zr[l], zi[l], (unsigned long) cnt[l]);
		}
	}
}

/* --- Lyapunov : l'exposant de x -> r x (1 - x), r pris dans (a, b) selon la suite g_seq ; a : le x du point, b : son y */
static void lyap_row (float *out, long n, long maxit, double x0, double y, double d)
{
	const double d2 = d + d;
	double xa = x0, xb = x0 + d;
	const v4d b = (v4d) {} + y;
	const long seqn = g_seqn;
	for (; n > 0; n -= 4, out += 4)
	{
		v4d a;
		a[0] = xa;
		a[1] = xb;
		xa = xa + d2;
		xb = xb + d2;
		a[2] = xa;
		a[3] = xb;
		xa = xa + d2;
		xb = xb + d2;
		v4d x = (v4d) {} + 0.5;
		unsigned long k = (unsigned long) maxit >> 2;	/* mise en route : un quart des iterations */
		long i = 0;
		do
		{
			unsigned char s = g_seq[i];
			if (++i >= seqn)
				i = 0;
			v4d r = s ? b : a;
			v4d t = 1.0 - x;
			x = x * t;
			x = x * r;
		}
		while (--k);
		v4d sum = {};
		k = (unsigned long) maxit;
		do
		{
			unsigned char s = g_seq[i];
			if (++i >= seqn)
				i = 0;
			v4d r = s ? b : a;
			v4d t = 1.0 - x;
			x = x * t;
			x = x * r;			/* x suivant */
			t = x * 2.0;
			t = 1.0 - t;
			t = t * r;
			t = vabs (t);			/* |r (1 - 2 x)| */
			sum = sum + vlog2 (t);
		}
		while (--k);
		sum = sum / (double) (unsigned long) maxit;
		for (int l = 0; l < 4; l++)
			out[l] = (float) sum[l];
	}
}

void row_kernel (long type, float *out, long n, long maxit, double x0, double y, double d)
{
	switch (type)
	{
	case T_MANDEL: esc_row<T_MANDEL> (out, n, maxit, x0, y, d); break;
	case T_JULIA: esc_row<T_JULIA> (out, n, maxit, x0, y, d); break;
	case T_SHIP: esc_row<T_SHIP> (out, n, maxit, x0, y, d); break;
	case T_TRICORN: esc_row<T_TRICORN> (out, n, maxit, x0, y, d); break;
	case T_M3: esc_row<T_M3> (out, n, maxit, x0, y, d); break;
	case T_M4: esc_row<T_M4> (out, n, maxit, x0, y, d); break;
	case T_M5: esc_row<T_M5> (out, n, maxit, x0, y, d); break;
	case T_NEWTON: esc_row<T_NEWTON> (out, n, maxit, x0, y, d); break;
	case T_PHOENIX: esc_row<T_PHOENIX> (out, n, maxit, x0, y, d); break;
	case T_LYAP: lyap_row (out, n, maxit, x0, y, d); break;
	}
}

/* --- attracteurs : Clifford, De Jong ------------------------------------------------------------------------------ */

/* sinus sur deux voies : x - k 2 pi (k entier le plus proche) dans (-pi, pi), puis un polynome impair */
static inline v2d vsin (v2d x)
{
	v2d k = x * KS_INV2PI;
	k = (k + 6755399441055744.0) - 6755399441055744.0;	/* (frintn : au plus proche, pair) */
	v2d r = vfma (-k, (v2d) {} + KS_2PI, x);
	v2d r2 = r * r;
	v2d p = (v2d) {} + KS_S9;
	p = p * r2;
	p = p + KS_S7;
	p = p * r2;
	p = p + KS_S5;
	p = p * r2;
	p = p + KS_S3;
	p = p * r2;
	p = p + KS_S1;
	return p * r;
}

/* sin (a y) + c cos (a x), sin (b x) + d cos (b y) ; De Jong : sin (a y) - cos (b x), sin (c x) - cos (d y) */
template <int T>
static inline void attr_step (v2d &x, v2d &y, v2d a, v2d b, v2d c, v2d d)
{
	if constexpr (T == T_CLIFFORD)
	{
		v2d nx = vsin (a * y);
		v2d t = a * x;
		t = t + KS_PIO2;
		nx = vfma (vsin (t), c, nx);
		v2d ny = vsin (b * x);
		t = b * y;
		t = t + KS_PIO2;
		ny = vfma (vsin (t), d, ny);
		x = nx;
		y = ny;
	}
	else
	{
		v2d nx = vsin (a * y);
		v2d t = b * x;
		t = t + KS_PIO2;
		nx = nx - vsin (t);
		v2d ny = vsin (c * x);
		t = d * y;
		t = t + KS_PIO2;
		ny = ny - vsin (t);
		x = nx;
		y = ny;
	}
}

/* un lot : deux orbites, chaque point compte dans g_mu (sans verrou, comme l'asm) */
template <int T>
static void attr_batch (long me)
{
	struct orbit_state *o = &g_orb[me];
	const v2d a = (v2d) {} + g_pa[0], b = (v2d) {} + g_pa[1], c = (v2d) {} + g_pa[2], d = (v2d) {} + g_pa[3];
	const double x0 = g_x0, y0 = g_y0, inv = 1.0 / g_dx;
	const unsigned long vw = (unsigned long) g_vw, vh = (unsigned long) g_vh;
	uint32_t *mu = (uint32_t *) g_mu;
	v2d x = { o->x[0], o->x[1] }, y = { o->y[0], o->y[1] };
	for (long n = ATTR_N; n; n--)
	{
		attr_step<T> (x, y, a, b, c, d);
		for (int l = 0; l < 2; l++)
		{
			unsigned long px = (unsigned long) cvt_s64 ((x[l] - x0) * inv);
			unsigned long py = (unsigned long) cvt_s64 ((y[l] - y0) * inv);
			if (px < vw && py < vh)
				mu[py * vw + px]++;
		}
	}
	o->x[0] = x[0];
	o->x[1] = x[1];
	o->y[0] = y[0];
	o->y[1] = y[1];
}

void attr_bounds (long type, double *bd)
{
	const v2d a = (v2d) {} + g_pa[0], b = (v2d) {} + g_pa[1], c = (v2d) {} + g_pa[2], d = (v2d) {} + g_pa[3];
	v2d x = (v2d) {} + 0.07, y = (v2d) {} + 0.11;	/* (k_orb) */
	for (int i = 0; i < 256; i++)
		if (type == T_CLIFFORD)
			attr_step<T_CLIFFORD> (x, y, a, b, c, d);
		else
			attr_step<T_DEJONG> (x, y, a, b, c, d);
	double x1 = x[0], x2 = x[0], y1 = y[0], y2 = y[0];
	for (int i = 0; i < 40000; i++)
	{
		if (type == T_CLIFFORD)
			attr_step<T_CLIFFORD> (x, y, a, b, c, d);
		else
			attr_step<T_DEJONG> (x, y, a, b, c, d);
		x1 = x[0] < x1 ? x[0] : x1;
		x2 = x[0] > x2 ? x[0] : x2;
		y1 = y[0] < y1 ? y[0] : y1;
		y2 = y[0] > y2 ? y[0] : y2;
	}
	bd[0] = x1;
	bd[1] = x2;
	bd[2] = y1;
	bd[3] = y2;
}

/* --- Buddhabrot : les orbites qui s'echappent, comptees dans g_mu ----------------------------------------------------
 * Un lot de points c au hasard. Le plan est tourne : Re vers le bas, Im en travers ; les deux moities (conjuguees)
 * comptent. */
static void buddha_batch (long me)
{
	struct orbit_state *o = &g_orb[me];
	uint64_t s = o->rng;
	const unsigned long maxit = (unsigned long) g_maxit;
	double *orb = o->orb;
	for (long k = BUD_N; k; k--)
	{
		uint64_t t = s ^ (s << 13);
		t ^= t >> 7;
		s = t ^ (t << 17);
		double cr = (double) (s & 0x3FFFFFF), ci = (double) ((s >> 26) & 0x3FFFFFF);
		cr = cr * KB_SCALE;			/* c : (-2 .. 1, -1.5 .. 1.5) */
		ci = ci * KB_SCALE;
		cr = cr - KB_TWO;
		ci = ci - 1.5;
		double a = cr - 0.25;			/* cardioide, grand disque : jamais de fuite */
		double q = a * a;
		double y2 = ci * ci;
		q = q + y2;
		double u = q + a;
		u = u * q;
		if (y2 * 0.25 > u)
			continue;
		double bb = cr + 1.0;
		bb = bb * bb;
		bb = bb + y2;
		if (bb < K_BULB)
			continue;
		double zr = 0.0, zi = 0.0;
		unsigned long n = 0;
		double *p = orb;
		for (;;)
		{
			double r2 = zr * zr, i2 = zi * zi;
			double m = r2 + i2;
			if (m > 4.0)
				break;
			zi = zr * zi;
			zi = zi + zi;
			zi = zi + ci;
			zr = r2 - i2;
			zr = zr + cr;
			p[0] = zr;
			p[1] = zi;
			p += 2;
			if (++n >= maxit)
				break;
		}
		if (n >= maxit || n < 8)		/* (dedans : rien ; les fuites immediates : un fond uni) */
			continue;
		const double x0 = g_x0, y0 = g_y0, inv = 1.0 / g_dx;	/* x0 : Im, y0 : Re */
		const unsigned long vw = (unsigned long) g_vw, vh = (unsigned long) g_vh;
		uint32_t *mu = (uint32_t *) g_mu;
		for (p = orb; n; n--, p += 2)
		{
			unsigned long row = (unsigned long) cvt_s64 ((p[0] - y0) * inv);
			if (row >= vh)
				continue;
			unsigned long col = (unsigned long) cvt_s64 ((p[1] - x0) * inv);
			if (col < vw)
				mu[row * vw + col]++;
			col = (unsigned long) cvt_s64 ((-p[1] - x0) * inv);
			if (col < vw)
				mu[row * vw + col]++;
		}
	}
	o->rng = s;
}

void hist_kernel (long type, long me)
{
	if (type == T_CLIFFORD)
		attr_batch<T_CLIFFORD> (me);
	else if (type == T_DEJONG)
		attr_batch<T_DEJONG> (me);
	else
		buddha_batch (me);
}

/* --- les couleurs ----------------------------------------------------------------------------------------------- */

/* g_lut : 4096 couleurs, boucle fermee sur les huit arrets */
void set_lut (const uint32_t *stops)
{
	for (unsigned i = 0; i < LUT_N; i++)
	{
		uint32_t c0 = stops[i >> 9], c1 = stops[((i >> 9) + 1) & 7];
		uint32_t f = i & 511, g = 512 - f;
		uint32_t r = (((c0 >> 16) & 255) * g + ((c1 >> 16) & 255) * f) >> 9;
		uint32_t v = (((c0 >> 8) & 255) * g + ((c1 >> 8) & 255) * f) >> 9;
		uint32_t b = ((c0 & 255) * g + (c1 & 255) * f) >> 9;
		g_lut[i] = (r << 16) | (v << 8) | b;
	}
}

void color_line (const float *mu, long n, uint32_t *px)
{
	const uint32_t off = (uint32_t) g_offset;
	const float scale = g_scale;
	if (g_mode == C_NEWTON)
	{
		/* racine * 1000 + iterations : un tiers de la palette par racine, plus sombre quand il a fallu longtemps */
		for (long i = 0; i < n; i++)
		{
			int32_t w = cvt_s32f (mu[i]);
			if (w < 0)
			{
				px[i] = 0;
				continue;
			}
			uint32_t root = (uint32_t) w / 1000, it = (uint32_t) w - root * 1000;
			uint32_t c = g_lut[(root * (LUT_N / 3) + off + (it << 3)) & (LUT_N - 1)];
			int32_t light = 240 - (int32_t) (it << 4);
			if (light < 48)
				light = 48;
			uint32_t l = (uint32_t) light;
			px[i] = ((((c >> 16) & 255) * l >> 8) << 16) | ((((c >> 8) & 255) * l >> 8) << 8) | ((c & 255) * l >> 8);
		}
	}
	else if (g_mode == C_LYAP)
	{
		/* l'exposant : negatif (stable) la palette, positif (chaos) du noir vers le bleu */
		for (long i = 0; i < n; i++)
		{
			float s = mu[i];
			if (s != s)
				px[i] = 0;
			else if (s < 0.0f)
				px[i] = g_lut[(cvt_u32f (-s * scale) + off) & (LUT_N - 1)];
			else if (s > 8.0f)		/* (hors de 0..4 : x part a l'infini) */
				px[i] = 0;
			else
			{
				if (s > 1.0f)
					s = 1.0f;
				uint32_t w = cvt_u32f (s * 160.0f);
				px[i] = w | (w >> 2) << 8;
			}
		}
	}
	else
		for (long i = 0; i < n; i++)
		{
			float s = mu[i];
			px[i] = s < 0.0f ? 0 : g_lut[(cvt_u32f (s * scale) + off) & (LUT_N - 1)];
		}
}

/* x > 0 -> 256 * log2 approche */
static inline unsigned long ilog2 (unsigned long x)
{
	int z = __builtin_clzl (x);
	return (((x << z) >> 55) & 255) + ((unsigned long) (63 - z) << 8);
}

/* les comptes de g_mu : le plus grand fait le blanc de l'echelle (logarithme) */
void color_hist (uint32_t *px, long pitch)
{
	const uint32_t *mu = (const uint32_t *) g_mu;
	const unsigned long vw = (unsigned long) g_vw, vh = (unsigned long) g_vh, total = vw * vh;
	unsigned long mx = 0;
	for (unsigned long i = 0; i < total; i++)
		if (mu[i] > mx)
			mx = mu[i];
	unsigned long lm = ilog2 (mx + 1);
	if (lm < 256)
		lm = 256;
	const unsigned long off = g_offset;
	const int buddha = g_type == T_BUDDHA;
	for (unsigned long y = 0; y < vh; y++, mu += vw, px = (uint32_t *) ((char *) px + pitch))
		for (unsigned long x = 0; x < vw; x++)
		{
			uint32_t c = mu[x];
			if (!c)
			{
				px[x] = 0;
				continue;
			}
			unsigned long v = ilog2 ((unsigned long) c + 1) * (LUT_N - 1) / lm;
			if (buddha)			/* (Buddhabrot : le contraste) */
			{
				v = v * v;
				v = v * v;
				v >>= 36;
			}
			v >>= 1;			/* (la premiere moitie de la palette) */
			uint32_t k = g_lut[(v + off) & (LUT_N - 1)];
			if (v < 512)			/* le fond sort du noir */
			{
				uint32_t w = (uint32_t) v;
				k = ((((k >> 16) & 255) * w >> 9) << 16) | ((((k >> 8) & 255) * w >> 9) << 8) | ((k & 255) * w >> 9);
			}
			px[x] = k;
		}
}
