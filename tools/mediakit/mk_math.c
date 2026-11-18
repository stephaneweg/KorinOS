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

/* ---- pour FFmpeg (tools/mediakit/ffmpeg.sh) : surtout le calcul de ses tables (AAC : x^(4/3), cbrt...),
 * a quelques ulp ; sqrt est l'instruction du processeur (__builtin_sqrt, -fno-math-errno) ---------- */

typedef union { double d; uint64_t u; } mk_dbits;

double fabs(double x) { mk_dbits v = { x }; v.u &= ~(1ull << 63); return v.d; }

/* partie entiere vers zero, sans debordement (|x| >= 2^52 : deja entier) */
static double mk_trunc(double x)
{
	if (!(fabs(x) < 4503599627370496.0)) return x;
	return (double)(int64_t)x;
}
double ceil(double x) { double t = mk_trunc(x); return t < x ? t + 1.0 : t; }
double round(double x) { return x < 0 ? -mk_trunc(-x + 0.5) : mk_trunc(x + 0.5); }

double frexp(double x, int *e)
{
	mk_dbits v = { x };
	int ex = (int)((v.u >> 52) & 0x7FF);
	*e = 0;
	if (x == 0 || ex == 0x7FF) return x;
	if (ex == 0) { v.d = x * 18014398509481984.0; ex = (int)((v.u >> 52) & 0x7FF) - 54; }	/* (sous-normal : x 2^54) */
	*e = ex - 1022;
	v.u = (v.u & ~(0x7FFull << 52)) | (1022ull << 52);
	return v.d;
}

double scalbn(double x, int n)
{
	while (n > 1000) { x *= pow2i(1000); n -= 1000; }
	while (n < -1000) { x *= pow2i(-1000); n += 1000; }
	return x * pow2i(n);
}

/* log : x = m 2^e, m dans [sqrt(1/2), sqrt(2)) ; log m = 2 atanh ((m - 1) / (m + 1)) en serie */
double log(double x)
{
	if (x != x || x < 0) return 0.0 / 0.0;
	if (x == 0) return -1.0 / 0.0;
	if (x > 1.7976931348623157e308) return x;
	int e;
	double m = frexp(x, &e) * 2.0;		/* [1, 2) */
	e--;
	if (m > 1.4142135623730951) { m *= 0.5; e++; }
	double s = (m - 1.0) / (m + 1.0), s2 = s * s, t = s, sum = 0;
	for (int k = 1; k <= 41; k += 2) { sum += t / k; t *= s2; }
	const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10;
	return e * ln2_hi + (2.0 * sum + e * ln2_lo);
}
float log2f(float x) { return (float)(log(x) * 1.4426950408889634); }

double exp2(double x)
{
	if (x != x) return x;
	if (x > 1024.0) return 1.0 / 0.0;
	if (x < -1075.0) return 0.0;
	double kf = x < 0 ? x - 0.5 : x + 0.5;
	int k = (int)kf;
	return scalbn(exp((x - k) * 0.69314718055994530942), k);
}

/* pow : exp (y log x) ; les puissances entieres par multiplications (exactes pour les petites) */
double pow(double x, double y)
{
	if (y == 0) return 1.0;
	if (x == 1.0) return 1.0;
	if (x != x || y != y) return 0.0 / 0.0;
	double yi = mk_trunc(y);
	if (yi == y && fabs(y) <= 64) {
		int n = (int)fabs(y);
		double r = 1.0, b = x;
		while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
		return y < 0 ? 1.0 / r : r;
	}
	if (x == 0) return y > 0 ? 0.0 : 1.0 / 0.0;
	if (x < 0) {
		if (yi != y) return 0.0 / 0.0;
		double r = exp(y * log(-x));
		return ((int64_t)yi & 1) ? -r : r;
	}
	return exp(y * log(x));
}
float powf(float x, float y) { return (float)pow(x, y); }

/* cbrt : une estimation par exp/log, puis deux pas de Newton */
double cbrt(double x)
{
	if (x == 0 || x != x || fabs(x) > 1.7976931348623157e308) return x;
	double a = fabs(x), y = exp(log(a) / 3.0);
	y -= (y * y * y - a) / (3.0 * y * y);
	y -= (y * y * y - a) / (3.0 * y * y);
	return x < 0 ? -y : y;
}
float cbrtf(float x) { return (float)cbrt(x); }

double hypot(double x, double y)
{
	x = fabs(x); y = fabs(y);
	if (x < y) { double t = x; x = y; y = t; }
	if (x == 0) return 0;
	double r = y / x;
	return x * __builtin_sqrt(1.0 + r * r);
}

double sinh(double x)
{
	if (fabs(x) < 0.5) {
		double x2 = x * x, t = x, s = x;
		for (int i = 1; i <= 8; i++) { t *= x2 / ((2 * i) * (2 * i + 1)); s += t; }
		return s;
	}
	double e = exp(x);
	return 0.5 * (e - 1.0 / e);
}
double cosh(double x) { double e = exp(fabs(x)); return 0.5 * (e + 1.0 / e); }
double tanh(double x)
{
	if (fabs(x) > 22.0) return x > 0 ? 1.0 : -1.0;
	if (fabs(x) < 0.5) return sinh(x) / cosh(x);
	double e = exp(2.0 * x);
	return (e - 1.0) / (e + 1.0);
}
double tan(double x) { return sin(x) / cos(x); }

/* atan : |x| > 1 -> pi/2 - atan (1/x) ; |x| > tan (pi/8) -> pi/4 + atan ((x-1)/(x+1)) ; puis la serie */
double atan(double x)
{
	const double pi2 = 1.57079632679489661923, pi4 = 0.78539816339744830962;
	if (x != x) return x;
	if (x < 0) return -atan(-x);
	if (x > 1.0) return pi2 - atan(1.0 / x);
	if (x > 0.41421356237309503) return pi4 + atan((x - 1.0) / (x + 1.0));
	double x2 = x * x, t = x, s = 0;
	for (int k = 1; k <= 51; k += 2) { s += t / k; t *= -x2; }
	return s;
}
double atan2(double y, double x)
{
	const double pi = 3.14159265358979323846;
	if (x != x || y != y) return 0.0 / 0.0;
	if (x == 0) return y > 0 ? pi / 2 : y < 0 ? -pi / 2 : 0.0;
	double a = atan(y / x);
	if (x > 0) return a;
	return y >= 0 ? a + pi : a - pi;
}
double asin(double x) { return atan2(x, __builtin_sqrt((1.0 - x) * (1.0 + x))); }
double acos(double x) { return atan2(__builtin_sqrt((1.0 - x) * (1.0 + x)), x); }
float atanf(float x) { return (float)atan(x); }
float atan2f(float y, float x) { return (float)atan2(y, x); }
