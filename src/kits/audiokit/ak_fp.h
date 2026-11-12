/* AudioKit en C : les opérations flottantes de l'asm, au bit près sur les deux
 * architectures.
 *
 * Les codecs en asm (AArch64) utilisent fmadd/fmla (multiplication-addition
 * FUSIONNÉE : un seul arrondi), fmax/fmin (sémantique AArch64 : NaN propagé,
 * +0 > -0), fcvtns/fcvtzs (conversions saturées), frintm/frintn/frintp. En C :
 *  * le kit est compilé avec -ffp-contract=off : a * b + c fait deux arrondis,
 *    comme fmul puis fadd ; la forme fusionnée s'écrit AK_FMA* ;
 *  * sur AArch64, chaque fonction est l'instruction elle-même ;
 *  * sur x86-64 (pas d'AVX dans le noyau du PC : pas de FMA matérielle), FMA
 *    en logiciel, exacte (un seul arrondi au plus proche pair), conversions
 *    saturées comme AArch64.
 * Noms : d'après l'instruction AArch64, opérandes dans son ordre
 * (fmadd Sd, Sn, Sm, Sa : d = a + n * m -> ak_fmadd(n, m, a)).
 *
 * Même ordre des opérations que l'asm : une réduction NEON (faddp, sommes
 * partielles par voie) se recopie voie par voie ; fcmp + b.lt/b.le/b.hs est
 * VRAI sur NaN (non ordonné). NaN (flux abîmés) : le signe et la charge d'un
 * NaN ne sont pas reproduits (GCC permute les opérandes de a + b, x86-64 crée
 * 0xFFC00000 et AArch64 0x7FC00000) : sans effet sur le s16 ; les essais
 * mettent les NaN du F32 sous une forme canonique.
 * QEMU raspi4b sur hôte Windows (11.1) : sa FMA n'est exacte que tant que son
 * drapeau « inexact » interne est baissé (chaque msr fpsr du noyau le baisse) :
 * les sorties F32 et les octets encodés y changent d'un passage à l'autre, avec
 * l'asm comme avec le C ; le QEMU de Linux (WSL), le banc et le vrai Pi sont
 * exacts (audtest le détecte). */
#ifndef AK_FP_H
#define AK_FP_H

#include <stdint.h>

static inline uint32_t ak_f2u(float f)
{
	uint32_t u;
	__builtin_memcpy(&u, &f, 4);
	return u;
}
static inline float ak_u2f(uint32_t u)
{
	float f;
	__builtin_memcpy(&f, &u, 4);
	return f;
}
static inline uint64_t ak_d2u(double f)
{
	uint64_t u;
	__builtin_memcpy(&u, &f, 8);
	return u;
}
static inline double ak_u2d(uint64_t u)
{
	double f;
	__builtin_memcpy(&f, &u, 8);
	return f;
}

#if defined(__aarch64__)

/* d = a + n * m (un arrondi) */
static inline float ak_fmadd(float n, float m, float a)
{
	float d;
	__asm__("fmadd %s0, %s1, %s2, %s3" : "=w"(d) : "w"(n), "w"(m), "w"(a));
	return d;
}
/* d = a - n * m */
static inline float ak_fmsub(float n, float m, float a)
{
	float d;
	__asm__("fmsub %s0, %s1, %s2, %s3" : "=w"(d) : "w"(n), "w"(m), "w"(a));
	return d;
}
/* d = -a - n * m */
static inline float ak_fnmadd(float n, float m, float a)
{
	float d;
	__asm__("fnmadd %s0, %s1, %s2, %s3" : "=w"(d) : "w"(n), "w"(m), "w"(a));
	return d;
}
/* d = n * m - a */
static inline float ak_fnmsub(float n, float m, float a)
{
	float d;
	__asm__("fnmsub %s0, %s1, %s2, %s3" : "=w"(d) : "w"(n), "w"(m), "w"(a));
	return d;
}
static inline double ak_dmadd(double n, double m, double a)
{
	double d;
	__asm__("fmadd %d0, %d1, %d2, %d3" : "=w"(d) : "w"(n), "w"(m), "w"(a));
	return d;
}
static inline double ak_dmsub(double n, double m, double a)
{
	double d;
	__asm__("fmsub %d0, %d1, %d2, %d3" : "=w"(d) : "w"(n), "w"(m), "w"(a));
	return d;
}
static inline float ak_fmax(float a, float b)
{
	float d;
	__asm__("fmax %s0, %s1, %s2" : "=w"(d) : "w"(a), "w"(b));
	return d;
}
static inline float ak_fmin(float a, float b)
{
	float d;
	__asm__("fmin %s0, %s1, %s2" : "=w"(d) : "w"(a), "w"(b));
	return d;
}
static inline double ak_dmax(double a, double b)
{
	double d;
	__asm__("fmax %d0, %d1, %d2" : "=w"(d) : "w"(a), "w"(b));
	return d;
}
static inline double ak_dmin(double a, double b)
{
	double d;
	__asm__("fmin %d0, %d1, %d2" : "=w"(d) : "w"(a), "w"(b));
	return d;
}
static inline float ak_fsqrt(float a)
{
	float d;
	__asm__("fsqrt %s0, %s1" : "=w"(d) : "w"(a));
	return d;
}
static inline double ak_dsqrt(double a)
{
	double d;
	__asm__("fsqrt %d0, %d1" : "=w"(d) : "w"(a));
	return d;
}
static inline float ak_frintm(float a)
{
	float d;
	__asm__("frintm %s0, %s1" : "=w"(d) : "w"(a));
	return d;
}
static inline float ak_frintn(float a)
{
	float d;
	__asm__("frintn %s0, %s1" : "=w"(d) : "w"(a));
	return d;
}
static inline float ak_frintp(float a)
{
	float d;
	__asm__("frintp %s0, %s1" : "=w"(d) : "w"(a));
	return d;
}
static inline double ak_drintm(double a)
{
	double d;
	__asm__("frintm %d0, %d1" : "=w"(d) : "w"(a));
	return d;
}
static inline double ak_drintn(double a)
{
	double d;
	__asm__("frintn %d0, %d1" : "=w"(d) : "w"(a));
	return d;
}
static inline double ak_drintp(double a)
{
	double d;
	__asm__("frintp %d0, %d1" : "=w"(d) : "w"(a));
	return d;
}
/* conversions : w = 32 bits, x = 64 bits ; ns au plus proche pair, zs vers
 * zéro, zu vers zéro non signé ; saturées, NaN -> 0 */
static inline int32_t ak_fcvtns_w(float a)
{
	int32_t r;
	__asm__("fcvtns %w0, %s1" : "=r"(r) : "w"(a));
	return r;
}
static inline int64_t ak_fcvtns_x(float a)
{
	int64_t r;
	__asm__("fcvtns %x0, %s1" : "=r"(r) : "w"(a));
	return r;
}
static inline int32_t ak_fcvtzs_w(float a)
{
	int32_t r;
	__asm__("fcvtzs %w0, %s1" : "=r"(r) : "w"(a));
	return r;
}
static inline int64_t ak_fcvtzs_x(float a)
{
	int64_t r;
	__asm__("fcvtzs %x0, %s1" : "=r"(r) : "w"(a));
	return r;
}
static inline uint32_t ak_fcvtzu_w(float a)
{
	uint32_t r;
	__asm__("fcvtzu %w0, %s1" : "=r"(r) : "w"(a));
	return r;
}
static inline uint64_t ak_fcvtzu_x(float a)
{
	uint64_t r;
	__asm__("fcvtzu %x0, %s1" : "=r"(r) : "w"(a));
	return r;
}
static inline int32_t ak_dcvtns_w(double a)
{
	int32_t r;
	__asm__("fcvtns %w0, %d1" : "=r"(r) : "w"(a));
	return r;
}
static inline int64_t ak_dcvtns_x(double a)
{
	int64_t r;
	__asm__("fcvtns %x0, %d1" : "=r"(r) : "w"(a));
	return r;
}
static inline int32_t ak_dcvtzs_w(double a)
{
	int32_t r;
	__asm__("fcvtzs %w0, %d1" : "=r"(r) : "w"(a));
	return r;
}
static inline int64_t ak_dcvtzs_x(double a)
{
	int64_t r;
	__asm__("fcvtzs %x0, %d1" : "=r"(r) : "w"(a));
	return r;
}
static inline uint32_t ak_dcvtzu_w(double a)
{
	uint32_t r;
	__asm__("fcvtzu %w0, %d1" : "=r"(r) : "w"(a));
	return r;
}

#elif defined(__x86_64__)

/* ---- FMA exacte en logiciel : x * y + z, un arrondi au plus proche pair, à
 * la précision p (24 ou 53 bits), exposant minimal emin (-126 ou -1022) */
typedef unsigned __int128 ak_u128;

static inline int ak_clz128(ak_u128 v)
{
	uint64_t hi = (uint64_t)(v >> 64), lo = (uint64_t)v;
	return hi ? __builtin_clzll(hi) : 64 + __builtin_clzll(lo);
}

/* valeur exacte s * R * 2^er (R != 0) -> double arrondi à p bits, emin ;
 * rend les bits du résultat dans le format (float si p = 24) */
static uint64_t ak_soft_round(int s, ak_u128 R, int er, int p, int emin, int ebias, int ebits)
{
	int msb = 127 - ak_clz128(R);
	int E = msb + er; /* exposant (non biaisé) du bit de tête */
	int lsb;	  /* poids du dernier bit gardé */
	if (E >= emin)
		lsb = E - (p - 1);
	else
		lsb = emin - (p - 1); /* sous-normal */
	int sh = lsb - er;
	ak_u128 q;
	if (sh <= 0) {
		q = R << (-sh);
	} else if (sh >= 128) { /* bien sous le plus petit sous-normal */
		q = (sh == 128 && R > ((ak_u128)1 << 127)) ? 1 : 0;
	} else {
		ak_u128 rem = R & (((ak_u128)1 << sh) - 1);
		ak_u128 half = (ak_u128)1 << (sh - 1);
		q = R >> sh;
		if (rem > half || (rem == half && (q & 1)))
			q++;
	}
	/* q : mantisse (avec le bit de tête) ; normaliser après l'arrondi */
	uint64_t mant = (uint64_t)q;
	int e = lsb + (p - 1); /* exposant si mant a son bit de tête en p-1 */
	if (mant >> p) {       /* 2^p : l'arrondi a débordé */
		mant >>= 1;
		e++;
	}
	uint64_t sign = (uint64_t)s << (ebits + p - 1);
	uint64_t emax = ((uint64_t)1 << ebits) - 1;
	if (!(mant >> (p - 1))) /* sous-normal (ou 0) */
		return sign | mant;
	int64_t be = (int64_t)e + ebias;
	if (be >= (int64_t)emax)
		return sign | (emax << (p - 1)); /* infini */
	return sign | ((uint64_t)be << (p - 1)) | (mant & (((uint64_t)1 << (p - 1)) - 1));
}

/* x * y + z exact puis arrondi ; x, y, z finis, x * y != 0 ; (entrées en
 * double : les float y sont exacts) */
static uint64_t ak_soft_fma_core(double x, double y, double z, int p, int emin, int ebias, int ebits)
{
	uint64_t bx = ak_d2u(x), by = ak_d2u(y), bz = ak_d2u(z);
	int ex = (int)((bx >> 52) & 0x7FF), ey = (int)((by >> 52) & 0x7FF), ez = (int)((bz >> 52) & 0x7FF);
	int sp = (int)((bx ^ by) >> 63), sz = (int)(bz >> 63);
	uint64_t mx = bx & 0xFFFFFFFFFFFFFULL, my = by & 0xFFFFFFFFFFFFFULL, mz = bz & 0xFFFFFFFFFFFFFULL;
	if (ex)
		mx |= 1ULL << 52;
	else
		ex = 1;
	if (ey)
		my |= 1ULL << 52;
	else
		ey = 1;
	if (ez)
		mz |= 1ULL << 52;
	else
		ez = 1;
	ak_u128 P = (ak_u128)mx * my; /* != 0 */
	int pe = ex + ey - 2150;      /* poids du bit 0 de P */
	if (!mz)
		return ak_soft_round(sp, P, pe, p, emin, ebias, ebits);
	int s1 = 125 - (127 - ak_clz128(P));
	P <<= s1;
	pe -= s1;
	ak_u128 Z = mz;
	int ze = ez - 1075;
	int s2 = 125 - (127 - ak_clz128(Z));
	Z <<= s2;
	ze -= s2;
	/* A : le plus grand exposant */
	ak_u128 A = P, B = Z;
	int ea = pe, eb = ze, sa = sp, sb = sz;
	if (ze > pe) {
		A = Z;
		B = P;
		ea = ze;
		eb = pe;
		sa = sz;
		sb = sp;
	}
	int d = ea - eb;
	if (d >= 127)
		B = B ? 1 : 0;
	else if (d > 0) {
		ak_u128 lost = B & (((ak_u128)1 << d) - 1);
		B = (B >> d) | (lost ? 1 : 0);
	}
	ak_u128 R;
	int s;
	if (sa == sb) {
		R = A + B;
		s = sa;
	} else if (A >= B) {
		R = A - B;
		s = sa;
	} else {
		R = B - A;
		s = sb;
	}
	if (!R)
		return 0; /* somme exacte nulle : +0 au plus proche */
	return ak_soft_round(s, R, ea, p, emin, ebias, ebits);
}

static inline double ak_dmadd(double n, double m, double a)
{
	if (!__builtin_isfinite(n) || !__builtin_isfinite(m) || !__builtin_isfinite(a)) {
		if (!__builtin_isfinite(a) && __builtin_isfinite(n) && __builtin_isfinite(m))
			return a + 0.0 * n;
		return n * m + a;
	}
	if (n == 0 || m == 0)
		return n * m + a; /* produit exact */
	return ak_u2d(ak_soft_fma_core(n, m, a, 53, -1022, 1023, 11));
}
static inline double ak_dmsub(double n, double m, double a)
{
	return ak_dmadd(-n, m, a);
}
static inline float ak_fmadd(float n, float m, float a)
{
	if (!__builtin_isfinite(n) || !__builtin_isfinite(m) || !__builtin_isfinite(a)) {
		if (!__builtin_isfinite(a) && __builtin_isfinite(n) && __builtin_isfinite(m))
			return a;
		return n * m + a;
	}
	if (n == 0 || m == 0)
		return n * m + a;
	return ak_u2f((uint32_t)ak_soft_fma_core(n, m, a, 24, -126, 127, 8));
}
static inline float ak_fmsub(float n, float m, float a)
{
	return ak_fmadd(-n, m, a);
}
static inline float ak_fnmadd(float n, float m, float a)
{
	return ak_fmadd(-n, m, -a);
}
static inline float ak_fnmsub(float n, float m, float a)
{
	return ak_fmadd(n, m, -a);
}
/* fmax/fmin d'AArch64 : NaN propagé, +0 > -0 */
static inline float ak_fmax(float a, float b)
{
	if (a != a)
		return a + b;
	if (b != b)
		return b + a;
	if (a == b)
		return ak_u2f(ak_f2u(a) & ak_f2u(b)); /* +0 si l'un est +0 */
	return a > b ? a : b;
}
static inline float ak_fmin(float a, float b)
{
	if (a != a)
		return a + b;
	if (b != b)
		return b + a;
	if (a == b)
		return ak_u2f(ak_f2u(a) | ak_f2u(b)); /* -0 si l'un est -0 */
	return a < b ? a : b;
}
static inline double ak_dmax(double a, double b)
{
	if (a != a)
		return a + b;
	if (b != b)
		return b + a;
	if (a == b)
		return ak_u2d(ak_d2u(a) & ak_d2u(b));
	return a > b ? a : b;
}
static inline double ak_dmin(double a, double b)
{
	if (a != a)
		return a + b;
	if (b != b)
		return b + a;
	if (a == b)
		return ak_u2d(ak_d2u(a) | ak_d2u(b));
	return a < b ? a : b;
}
static inline float ak_fsqrt(float a)
{
	return __builtin_sqrtf(a);
}
static inline double ak_dsqrt(double a)
{
	return __builtin_sqrt(a);
}
/* arrondis entiers : |a| >= 2^23 (float) / 2^52 (double) est déjà entier */
static inline float ak_frintn(float a)
{
	if (!(__builtin_fabsf(a) < 8388608.0f))
		return a;
	float t = __builtin_fabsf(a) + 8388608.0f - 8388608.0f; /* au plus proche pair */
	return __builtin_copysignf(t, a);
}
static inline float ak_frintm(float a)
{
	if (!(__builtin_fabsf(a) < 8388608.0f))
		return a;
	float t = ak_frintn(a);
	if (t > a)
		t -= 1.0f;
	return t;
}
static inline float ak_frintp(float a)
{
	if (!(__builtin_fabsf(a) < 8388608.0f))
		return a;
	float t = ak_frintn(a);
	if (t < a)
		t += 1.0f;
	return t == 0 ? __builtin_copysignf(0.0f, a) : t;
}
static inline double ak_drintn(double a)
{
	if (!(__builtin_fabs(a) < 4503599627370496.0))
		return a;
	double t = __builtin_fabs(a) + 4503599627370496.0 - 4503599627370496.0;
	return __builtin_copysign(t, a);
}
static inline double ak_drintm(double a)
{
	if (!(__builtin_fabs(a) < 4503599627370496.0))
		return a;
	double t = ak_drintn(a);
	if (t > a)
		t -= 1.0;
	return t == 0 ? __builtin_copysign(0.0, a) : t;
}
static inline double ak_drintp(double a)
{
	if (!(__builtin_fabs(a) < 4503599627370496.0))
		return a;
	double t = ak_drintn(a);
	if (t < a)
		t += 1.0;
	return t == 0 ? __builtin_copysign(0.0, a) : t;
}
/* conversions saturées comme AArch64 (NaN -> 0) */
static inline int64_t ak_dcvtzs_x(double a)
{
	if (a != a)
		return 0;
	if (a >= 9223372036854775808.0)
		return INT64_MAX;
	if (a <= -9223372036854775808.0)
		return INT64_MIN;
	return (int64_t)a;
}
static inline int32_t ak_dcvtzs_w(double a)
{
	if (a != a)
		return 0;
	if (a >= 2147483648.0)
		return INT32_MAX;
	if (a <= -2147483648.0)
		return INT32_MIN;
	return (int32_t)a;
}
static inline uint32_t ak_dcvtzu_w(double a)
{
	if (a != a || a <= 0)
		return 0;
	if (a >= 4294967296.0)
		return UINT32_MAX;
	return (uint32_t)a;
}
static inline int32_t ak_fcvtzs_w(float a)
{
	return ak_dcvtzs_w(a);
}
static inline int64_t ak_fcvtzs_x(float a)
{
	return ak_dcvtzs_x(a);
}
static inline uint32_t ak_fcvtzu_w(float a)
{
	return ak_dcvtzu_w(a);
}
static inline uint64_t ak_fcvtzu_x(float a)
{
	if (a != a || a <= 0)
		return 0;
	if (a >= 18446744073709551616.0f)
		return UINT64_MAX;
	return (uint64_t)a;
}
static inline int32_t ak_dcvtns_w(double a)
{
	return ak_dcvtzs_w(ak_drintn(a));
}
static inline int64_t ak_dcvtns_x(double a)
{
	return ak_dcvtzs_x(ak_drintn(a));
}
static inline int32_t ak_fcvtns_w(float a)
{
	return ak_dcvtzs_w(ak_frintn(a));
}
static inline int64_t ak_fcvtns_x(float a)
{
	return ak_dcvtzs_x(ak_frintn(a));
}

#else
#error "architecture non prise en charge"
#endif

/* fabs, fneg : exacts partout (bit de signe) */
static inline float ak_fabs(float a)
{
	return ak_u2f(ak_f2u(a) & 0x7FFFFFFFu);
}
static inline float ak_fneg(float a)
{
	return ak_u2f(ak_f2u(a) ^ 0x80000000u);
}

#endif
