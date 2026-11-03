/* mediakit : les fonctions de math de libopus et de la bibliotheque av (voir mk_libc.c :
 * pas de libm de newlib, qui ecrit errno dans une variable par thread). */
#include <stdint.h>

/* 2^k (k entier, sans debordement : borne par l'appelant) */
static double pow2i(int k)
{
	union { uint64_t u; double d; } v;
	v.u = (uint64_t)(k + 1023) << 52;
	return v.d;
}

/* exp : x = k ln 2 + r, |r| <= ln 2 / 2 ; exp (r) par Taylor (degre 13 : < 1 ulp ici) */
double exp(double x)
{
	if (x != x) return x;
	if (x > 709.0) return 1.0 / 0.0;
	if (x < -745.0) return 0.0;
	const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10;
	double kf = x * 1.4426950408889634 + (x < 0 ? -0.5 : 0.5);
	int k = (int)kf;
	double r = (x - k * ln2_hi) - k * ln2_lo;
	double t = 1.0, s = 1.0;
	for (int i = 1; i <= 13; i++) { t *= r / i; s += t; }
	if (k < -1021) return s * pow2i(k + 600) * pow2i(-600);
	if (k > 1023) return s * pow2i(k - 600) * pow2i(600);
	return s * pow2i(k);
}

float expf(float x) { return (float)exp(x); }

/* sin et cos de |r| <= pi / 4 (Taylor) */
static double ksin(double r)
{
	double r2 = r * r, t = r, s = r;
	for (int i = 1; i <= 8; i++) { t *= -r2 / ((2 * i) * (2 * i + 1)); s += t; }
	return s;
}

static double kcos(double r)
{
	double r2 = r * r, t = 1.0, s = 1.0;
	for (int i = 1; i <= 8; i++) { t *= -r2 / ((2 * i - 1) * (2 * i)); s += t; }
	return s;
}

/* x = q pi / 2 + r (Cody-Waite, assez juste pour |x| < 1e6) */
static double reduce(double x, int *q)
{
	const double p1 = 1.57079632673412561417e+00, p2 = 6.07710050650619224932e-11,
		p3 = 2.02226624879595063154e-21;
	double kf = x * 0.63661977236758134308;
	kf = kf < 0 ? kf - 0.5 : kf + 0.5;
	long k = (long)kf;
	*q = (int)(k & 3);
	return ((x - k * p1) - k * p2) - k * p3;
}

double cos(double x)
{
	int q;
	double r = reduce(x, &q);
	switch (q) {
	case 0: return kcos(r);
	case 1: return -ksin(r);
	case 2: return -kcos(r);
	}
	return ksin(r);
}

double sin(double x)
{
	int q;
	double r = reduce(x, &q);
	switch (q) {
	case 0: return ksin(r);
	case 1: return kcos(r);
	case 2: return -ksin(r);
	}
	return -kcos(r);
}

float cosf(float x) { return (float)cos(x); }
float sinf(float x) { return (float)sin(x); }
