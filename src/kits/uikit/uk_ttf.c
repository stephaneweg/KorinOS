/* uk_ttf.c -- UIKit : rasteriseur TrueType (« font-rs »), port ligne à ligne
 * de ttf.asm (src/kits/uikit/PORT.md).
 *
 * Tout l'état est dans la structure police (F_*, TTF_FONT_SIZE octets) et dans
 * le tampon de travail fournis par l'appelant : mêmes décalages, mêmes
 * contenus que l'asm (F_SCALE : flottant 32 bits ; grille F_ACC ; cache TC_* ;
 * F_FALLBACK : rappel « x0 = police -> x0 = police de repli »).
 *
 * Flottant : chaque instruction de l'asm est traduite dans le même ordre, en
 * simple précision (float, jamais de double intermédiaire) ; le fichier est
 * compilé avec -ffp-contract=off : a * b + c fait deux arrondis (fmul puis
 * fadd), la forme fusionnée de fmadd / fmsub s'écrit fp_fma. fmin / fmax,
 * fcvt* (saturées, NaN -> 0), frint* : helpers qui reproduisent AArch64 au
 * bit près (aussi sur x86-64).
 *
 * Calcul entier : w = 32 bits zéro-étendus, x = 64 bits, comme l'asm. */
#define UK_FP
#include "uk.h"

/* ======================================================================== */
/* Opérations flottantes d'AArch64                                          */
/* ======================================================================== */

static inline u32 fp_bits(float f)
{
	u32 u;
	__builtin_memcpy(&u, &f, 4);
	return u;
}
static inline float fp_from(u32 u)
{
	float f;
	__builtin_memcpy(&f, &u, 4);
	return f;
}

/* flottant 32 bits dans un bloc (ldr s / str s), par ses bits : un type
 * float sous-aligné (aligned(1)) qui arrive en argument d'un appel fait
 * planter GCC sur AArch64 (aarch64_function_arg_alignment) */
static inline float LDF(long p, long o)
{
	return fp_from((u32)LDW(p, o));
}
static inline void STF(long p, long o, float v)
{
	STW(p, o, fp_bits(v));
}

#if defined(__x86_64__) && !defined(__FMA__)
/* FMA exacte sans instruction FMA : produit exact en double (24 + 24 bits),
 * somme arrondie « à l'impair » en double (TwoSum), puis un seul arrondi
 * correct vers float (53 >= 24 + 2 : pas de double arrondi). */
static inline u64 fp_dbits(double d)
{
	u64 u;
	__builtin_memcpy(&u, &d, 8);
	return u;
}
static inline double fp_dfrom(u64 u)
{
	double d;
	__builtin_memcpy(&d, &u, 8);
	return d;
}
static inline float fp_soft_fma(float n, float m, float a)
{
	double p = (double)n * (double)m; /* exact */
	double s = p + (double)a;
	if (__builtin_isfinite(s)) {
		double bb = s - p;
		double err = (p - (s - bb)) + ((double)a - bb);
		if (err != 0) {
			u64 b = fp_dbits(s);
			if (!(b & 1)) /* arrondi à l'impair */
				b += ((err > 0) == (s > 0)) ? 1 : (u64)-1;
			s = fp_dfrom(b);
		}
	}
	return (float)s;
}
#define FP_FMA(n, m, a) fp_soft_fma((n), (m), (a))
#else
#define FP_FMA(n, m, a) __builtin_fmaf((n), (m), (a))
#endif

/* fmadd d, n, m, a : d = a + n * m (un seul arrondi) */
static inline float fp_fmadd(float n, float m, float a)
{
	return FP_FMA(n, m, a);
}
/* fmsub d, n, m, a : d = a - n * m (un seul arrondi) */
static inline float fp_fmsub(float n, float m, float a)
{
	return FP_FMA(-n, m, a);
}

/* fmin / fmax d'AArch64 : NaN propagé ; fmin(-0, +0) = -0, fmax(-0, +0) = +0 */
static inline float fp_fmin(float a, float b)
{
	if (a != a || b != b)
		return a + b;
	if (a == b)
		return fp_from(fp_bits(a) | fp_bits(b));
	return a < b ? a : b;
}
static inline float fp_fmax(float a, float b)
{
	if (a != a || b != b)
		return a + b;
	if (a == b)
		return fp_from(fp_bits(a) & fp_bits(b));
	return a > b ? a : b;
}

/* fsqrt (en ligne : __builtin_sqrtf appellerait sqrtf pour errno) */
static inline float fp_fsqrt(float a)
{
	float d;
#if defined(__aarch64__)
	__asm__("fsqrt %s0, %s1" : "=w"(d) : "w"(a));
#elif defined(__AVX__)
	__asm__("vsqrtss %1, %1, %0" : "=x"(d) : "x"(a));
#else
	__asm__("sqrtss %1, %0" : "=x"(d) : "x"(a));
#endif
	return d;
}

/* frintm, frintp, frintn */
#define fp_frintm(a) __builtin_floorf(a)
#define fp_frintp(a) __builtin_ceilf(a)
#define fp_frintn(a) __builtin_roundevenf(a)

/* fcvtzs w / x : vers zéro, saturée comme AArch64, NaN -> 0 */
static inline s32 fp_cvtw(float f)
{
	if (f != f)
		return 0;
	if (f >= 2147483648.0f)
		return 0x7FFFFFFF;
	if (f < -2147483648.0f)
		return (s32)0x80000000u;
	return (s32)f;
}
static inline long fp_cvtx(float f)
{
	if (f != f)
		return 0;
	if (f >= 9223372036854775808.0f)
		return 0x7FFFFFFFFFFFFFFFL;
	if (f < -9223372036854775808.0f)
		return (long)0x8000000000000000UL;
	return (long)f;
}
/* fcvtms (vers -inf), fcvtps (vers +inf), fcvtns (au plus proche, pair) */
#define fcvtms_w(f) fp_cvtw(fp_frintm(f))
#define fcvtps_w(f) fp_cvtw(fp_frintp(f))
#define fcvtns_w(f) fp_cvtw(fp_frintn(f))
#define fcvtzs_w(f) fp_cvtw(f)
#define fcvtps_x(f) fp_cvtx(fp_frintp(f))
#define fcvtns_x(f) fp_cvtx(fp_frintn(f))

/* ======================================================================== */
/* Lecture big-endian (macros be16, bes16, be32)                            */
/* ======================================================================== */

/* be16 : ldrh + rev16 (zéro-étendu) */
static inline long be16(long p, long o)
{
	return (long)__builtin_bswap16((u16)LDH(p, o));
}
/* bes16 : be16 + sxth w : valeur 32 bits signée (le registre w) */
static inline s32 bes16(long p, long o)
{
	return (s32)(s16)__builtin_bswap16((u16)LDH(p, o));
}
/* be32 : ldr w + rev (zéro-étendu) */
static inline long be32(long p, long o)
{
	return (long)__builtin_bswap32((u32)LDW(p, o));
}

/* étiquette de table (constante « 'head' » de FASMARM : premier octet en bas) */
#define TAG(a, b, c, d) ((u32)(a) | (u32)(b) << 8 | (u32)(c) << 16 | (u32)(d) << 24)

/* ======================================================================== */
/* ttf_open, ttf_set_size                                                   */
/* ======================================================================== */

/* ttf_open : x0 = font, x1 = fichier TTF, x2 = taille, x3 = scratch,
 * x4 = taille du scratch -> 1 si la police est utilisable */
long ttf_open(long font, long data, long len, long scratch, long scratch_len)
{
	long i, n, p, cm, x5, w3, w4, w6;

	for (i = 0; i < TTF_FONT_SIZE / 8; i++) /* structure à zéro */
		STX(font, i * 8, 0);
	STX(font, F_DATA, data);
	STX(font, F_LEN, len);
	STX(font, F_SCRATCH, scratch);
	STX(font, F_SCRATCH_LEN, scratch_len);
	if (ULT(len, 12))
		return 0;

	n = be16(data, 4); /* nombre de tables */
	p = data + 12;
	while ((u32)n) {
		u32 tag = (u32)LDW(p, 0); /* étiquette (octets bruts) */
		long t = data + be32(p, 8);
		if (tag == TAG('h', 'e', 'a', 'd'))
			STX(font, F_THEAD, t);
		if (tag == TAG('m', 'a', 'x', 'p'))
			STX(font, F_TMAXP, t);
		if (tag == TAG('h', 'h', 'e', 'a'))
			STX(font, F_THHEA, t);
		if (tag == TAG('c', 'm', 'a', 'p'))
			STX(font, F_CMAP, t);
		if (tag == TAG('l', 'o', 'c', 'a'))
			STX(font, F_LOCA, t);
		if (tag == TAG('g', 'l', 'y', 'f'))
			STX(font, F_GLYF, t);
		if (tag == TAG('h', 'm', 't', 'x'))
			STX(font, F_HMTX, t);
		if (tag == TAG('k', 'e', 'r', 'n'))
			STX(font, F_TKERN, t);
		p += 16;
		n = (u32)(n - 1);
	}
	if (!LDX(font, F_THEAD) || !LDX(font, F_TMAXP) || !LDX(font, F_THHEA) || !LDX(font, F_LOCA) ||
	    !LDX(font, F_GLYF) || !LDX(font, F_HMTX))
		return 0;

	p = LDX(font, F_THEAD); /* --- head */
	i = be16(p, 18);
	if (!i)
		return 0;
	STX(font, F_UPEM, i);
	/* comme l'asm : bes16 puis str x0 sans sxtw (32 bits signés, zéro-étendus) */
	STX(font, F_LOCFMT, (u32)bes16(p, 50));
	p = LDX(font, F_TMAXP); /* --- maxp */
	STX(font, F_NGLYPHS, be16(p, 4));
	{
		u32 w0 = (u32)be16(p, 6);  /* maxPoints */
		u32 w2 = (u32)be16(p, 10); /* maxCompositePoints */
		w0 = w0 >= w2 ? w0 : w2;
		w0 += 4;
		STX(font, F_MAXPTS, w0);
		w0 = w0 * 12;
		w0 += 15;
		w0 &= ~15u;
		STX(font, F_PTSBYTES, w0);
	}
	p = LDX(font, F_THHEA); /* --- hhea */
	STX(font, F_ASC, (long)bes16(p, 4));
	STX(font, F_DESC, (long)bes16(p, 6));
	STX(font, F_GAP, (long)bes16(p, 8));
	i = be16(p, 34);
	if (!i)
		return 0;
	STX(font, F_NHM, i);

	/* --- cmap : on préfère le format 12 (Unicode complet), sinon le 4 */
	cm = LDX(font, F_CMAP);
	STX(font, F_CMAP, 0);
	if (!cm)
		return 0;
	n = be16(cm, 2);
	p = cm + 4;
	while ((u32)n) {
		w3 = be16(p, 0); /* plateforme */
		w4 = be16(p, 2); /* encodage */
		x5 = cm + be32(p, 4); /* sous-table */
		w6 = be16(x5, 0);     /* format */
		if (w6 == 12) {
			if (w3 == 0 || (w3 == 3 && w4 == 10)) {
				STX(font, F_CMAP, x5);
				STX(font, F_CMAPFMT, 12);
				break;
			}
		} else if (w6 == 4) {
			if (!LDX(font, F_CMAP) && (w3 == 0 || (w3 == 3 && w4 == 1))) {
				STX(font, F_CMAP, x5);
				STX(font, F_CMAPFMT, 4);
			}
		}
		p += 8;
		n = (u32)(n - 1);
	}
	if (!LDX(font, F_CMAP))
		return 0;

	/* --- kern (format 0, horizontal, première sous-table) */
	p = LDX(font, F_TKERN);
	if (p && !be16(p, 0) && be16(p, 2)) { /* seule la version 0 (Microsoft) */
		long x2 = p + 4;
		long cov = be16(x2, 4); /* coverage */
		if (!((u32)cov >> 8) && (cov & 1)) { /* format 0, horizontal */
			STX(font, F_NKERN, be16(x2, 6));
			STX(font, F_KERN, x2 + 14);
		}
	}
	ttf_set_size(font, 16);
	return 1;
}

/* ttf_set_size : x0 = font, x1 = taille en pixels par em (-> x0 = font) */
long ttf_set_size(long font, long px)
{
	float s0, s1;
	long x2, x3, x4, x5;

	STX(font, F_PIXELS, px);
	s0 = (float)px;
	s1 = (float)LDX(font, F_UPEM);
	s0 = s0 / s1;
	STF(font, F_SCALE, s0);
	x2 = LDX(font, F_ASC);
	s1 = (float)x2;
	s1 = s1 * s0;
	x3 = fcvtps_x(s1); /* ascendante arrondie vers le haut */
	STX(font, F_ASC_PX, x3);
	x4 = -LDX(font, F_DESC);
	s1 = (float)x4;
	s1 = s1 * s0;
	x5 = fcvtps_x(s1);
	STX(font, F_DESC_PX, x5);
	x2 = x2 + x4;
	x2 = x2 + LDX(font, F_GAP);
	s1 = (float)x2;
	s1 = s1 * s0;
	x2 = fcvtns_x(s1);
	x3 = x3 + x5;
	x2 = UGE(x2, x3) ? x2 : x3; /* comme l'asm : comparaison non signée (hs) */
	STX(font, F_LINE_PX, x2);
	return font;
}

/* ======================================================================== */
/* Tables : cmap, loca, hmtx, kern                                          */
/* ======================================================================== */

/* ttf_glyph_index : x1 = point de code Unicode -> glyphe (0 = absent) */
static long ttf_glyph_index(long font, long cp)
{
	long x2 = LDX(font, F_CMAP);
	long n = LDX(font, F_NGLYPHS);
	u32 c = (u32)cp, w4, w7, w8, w9, w13;
	long x6, r;

	if (LDX(font, F_CMAPFMT) == 12)
		goto f12;
	/* --- format 4 : segments [start, end] triés par end */
	if (UGT(cp, 0xFFFF))
		return 0;
	w4 = (u32)be16(x2, 6); /* segCountX2 */
	if (!(w4 >> 1))
		return 0;
	x6 = x2 + 14; /* endCode[] */
	w7 = 0;
	w8 = (w4 >> 1) - 1;
	while (w7 < w8) { /* premier segment avec end >= cp */
		w9 = (w7 + w8) >> 1;
		if ((u32)be16(x6 + (long)w9 * 2, 0) >= c)
			w8 = w9;
		else
			w7 = w9 + 1;
	}
	{
		long x12, x14, x16;
		u32 w17, d;
		if ((u32)be16(x6 + (long)w7 * 2, 0) < c)
			return 0;
		x12 = x6 + w4 + 2 + (long)w7 * 2; /* startCode[i] */
		w13 = (u32)be16(x12, 0);
		if (c < w13)
			return 0;
		x14 = x12 + w4; /* idDelta[i] */
		d = (u32)be16(x14, 0);
		x16 = x14 + w4; /* idRangeOffset[i] */
		w17 = (u32)be16(x16, 0);
		if (!w17) {
			r = (c + d) & 0xFFFF;
		} else {
			x16 = x16 + w17 + (long)(u32)(c - w13) * 2;
			r = be16(x16, 0);
			if (!r)
				return 0;
			r = ((u32)r + d) & 0xFFFF;
		}
		goto check;
	}
f12: /* --- format 12 : groupes [start, end] -> startGlyph */
	w4 = (u32)be32(x2, 12); /* nGroups */
	if (!w4)
		return 0;
	x6 = x2 + 16;
	w7 = 0;
	w8 = w4 - 1;
	while (w7 < w8) {
		w9 = (w7 + w8) >> 1;
		if ((u32)be32(x6 + (long)w9 * 12, 4) >= c) /* endCharCode */
			w8 = w9;
		else
			w7 = w9 + 1;
	}
	{
		long x10 = x6 + (long)w7 * 12;
		u32 st = (u32)be32(x10, 0), en = (u32)be32(x10, 4);
		if (c < st || c > en)
			return 0;
		r = (u32)(c - st + (u32)be32(x10, 8));
	}
check:
	if (UGE(r, n))
		return 0;
	return r;
}

/* ttf__glyph_loc : x1 = glyphe -> r0 = données glyf, r1 = longueur (0 si vide) */
static ukr ttf_glyph_loc(long font, long g)
{
	ukr r = {0, 0, 0, 0};
	long loca;
	u32 w6, w7;

	if (UGE(g, LDX(font, F_NGLYPHS)))
		return r;
	loca = LDX(font, F_LOCA);
	if (!LDX(font, F_LOCFMT)) {
		long x5 = loca + g * 2;
		w6 = (u32)be16(x5, 0) << 1;
		w7 = (u32)be16(x5, 2) << 1;
	} else {
		long x5 = loca + g * 4;
		w6 = (u32)be32(x5, 0);
		w7 = (u32)be32(x5, 4);
	}
	if (w7 <= w6)
		return r;
	r.r1 = (u32)(w7 - w6);
	r.r0 = LDX(font, F_GLYF) + (long)w6;
	return r;
}

/* ttf__advance : x1 = glyphe -> avance (unités) */
static long ttf_advance(long font, long g)
{
	long n = LDX(font, F_NHM);
	if (!ULT(g, n))
		g = n - 1;
	return be16(LDX(font, F_HMTX) + g * 4, 0);
}

/* ttf_kern : x1 = glyphe gauche, x2 = glyphe droit -> valeur (unités) */
static long ttf_kern(long font, long left, long right)
{
	long x3 = LDX(font, F_KERN), x4, x10;
	u32 key, w6, w7, w8, w11;

	if (!x3)
		return 0;
	x4 = LDX(font, F_NKERN);
	if (!x4)
		return 0;
	key = (u32)right | (u32)left << 16; /* clé (gauche << 16) | droit */
	w6 = 0;
	w7 = (u32)x4 - 1;
	for (;;) {
		if (w6 > w7)
			return 0;
		w8 = (w6 + w7) >> 1;
		x10 = x3 + (long)w8 * 6;
		w11 = (u32)be32(x10, 0);
		if (w11 == key)
			return (long)bes16(x10, 4);
		if (w11 > key) {
			if (!w8)
				return 0;
			w7 = w8 - 1;
		} else {
			w6 = w8 + 1;
		}
	}
}

/* ======================================================================== */
/* Accumulation (font-rs)                                                   */
/* ======================================================================== */

/* ttf__line : segment (s0,s1) -> (s2,s3) en pixels de la grille */
static void ttf_line(long font, float s0, float s1, float s2, float s3)
{
	float s4, s5, s6, s7, s16, s17, s18, s19, s20, s21, s22, s23, s24, s25, s26, s27, s28, s29, s30, s31;
	long x10, x17, x6, x4;
	s32 w8, w9, w11, w12, w13, w14, w15, w16, w5, w6, w7;

	if (s1 == s3) /* b.eq */
		return;
	s4 = 1.0f; /* sens */
	if (!(s1 < s3)) { /* b.mi .ordered : non ordonné -> échange, comme l'asm */
		s5 = s0;
		s0 = s2;
		s2 = s5;
		s5 = s1;
		s1 = s3;
		s3 = s5;
		s4 = -1.0f;
	}
	x10 = LDX(font, F_ACC);
	w11 = (s32)LDX(font, F_ACCW);
	w12 = (s32)LDX(font, F_ACCH);
	s5 = s2 - s0;
	s6 = s3 - s1;
	s5 = s5 / s6; /* dx/dy */
	s7 = s0;      /* x courant */
	if (s1 >= 0.0f) { /* b.ge (faux si non ordonné) */
		w13 = fcvtms_w(s1);
	} else {
		s7 = fp_fmsub(s1, s5, s7); /* x à y = 0 */
		w13 = 0;
	}
	w14 = fcvtps_w(s3);
	w14 = w14 < w12 ? w14 : w12;
	s26 = 1.0f;
	s27 = 0.5f;
	w9 = (s32)((u32)w11 - 2); /* derniers index autorisés */
	w8 = (s32)((u32)w11 - 1);
	while (w13 < w14) {
		s16 = (float)w13;
		s17 = s16 + s26;
		s18 = fp_fmin(s17, s3);
		s19 = fp_fmax(s16, s1);
		s18 = s18 - s19;               /* dy dans cette ligne */
		s19 = fp_fmadd(s5, s18, s7);   /* x suivant */
		s20 = s18 * s4;                /* d = dy * sens */
		s21 = fp_fmin(s7, s19);        /* x0 */
		s22 = fp_fmax(s7, s19);        /* x1 */
		s23 = fp_frintm(s21);
		w15 = fcvtzs_w(s23);
		s24 = fp_frintp(s22);
		w16 = fcvtzs_w(s24);
		w15 = w15 > 0 ? w15 : 0; /* bornage mémoire */
		w15 = w15 < w9 ? w15 : w9;
		w16 = w16 < w8 ? w16 : w8;
		x17 = x10 + (long)(u32)((u32)w13 * (u32)w11) * 4; /* début de ligne (uxtw) */
		w6 = (s32)((u32)w15 + 1);
		if (!(w16 > w6)) {
			/* --- une seule cellule (et sa voisine) */
			s25 = s7 + s19;
			s25 = s25 * s27;
			s25 = s25 - s23; /* milieu - x0 entier */
			s28 = s20 * s25;
			s29 = s20 - s28;
			x6 = x17 + (long)(u32)w15 * 4;
			s30 = LDF(x6, 0);
			s31 = LDF(x6, 4);
			s30 = s30 + s29;
			s31 = s31 + s28;
			STF(x6, 0, s30);
			STF(x6, 4, s31);
			goto next;
		}
		/* --- plusieurs cellules */
		s25 = s22 - s21;
		s25 = s26 / s25; /* s = 1/(x1-x0) */
		s28 = s21 - s23; /* x0f */
		s29 = s26 - s28;
		s29 = s29 * s29;
		s29 = s29 * s25;
		s29 = s29 * s27; /* a0 = s/2 (1-x0f)^2 */
		s30 = s22 - s24;
		s30 = s30 + s26; /* x1f */
		s30 = s30 * s30;
		s30 = s30 * s25;
		s30 = s30 * s27; /* am = s/2 x1f^2 */
		x6 = x17 + (long)(u32)w15 * 4;
		s31 = LDF(x6, 0);
		s31 = fp_fmadd(s20, s29, s31);
		STF(x6, 0, s31);
		w7 = (s32)((u32)w15 + 2);
		if (w16 == w7) {
			s31 = s26 - s29;
			s31 = s31 - s30;
			s0 = LDF(x6, 4);
			s0 = fp_fmadd(s20, s31, s0);
			STF(x6, 4, s0);
		} else {
			float sm;
			s0 = 1.5f;
			s0 = s0 - s28;
			s0 = s0 * s25; /* a1 = s (1.5 - x0f) */
			s6 = s0 - s29;
			s2 = LDF(x6, 4);
			s2 = fp_fmadd(s20, s6, s2);
			STF(x6, 4, s2);
			sm = s20 * s25; /* d * s */
			w5 = (s32)((u32)w16 - 1);
			while (w7 < w5) {
				x4 = x17 + (long)(u32)w7 * 4;
				s2 = LDF(x4, 0);
				s2 = s2 + sm;
				STF(x4, 0, s2);
				w7 = (s32)((u32)w7 + 1);
			}
			w7 = (s32)((u32)w16 - (u32)w15);
			w7 = (s32)((u32)w7 - 3);
			s2 = (float)w7;
			s2 = fp_fmadd(s2, s25, s0); /* a2 */
			s31 = s26 - s2;
			s31 = s31 - s30;
			x4 = x17 + (long)(u32)w5 * 4;
			s2 = LDF(x4, 0);
			s2 = fp_fmadd(s20, s31, s2);
			STF(x4, 0, s2);
		}
		/* .last */
		x4 = x17 + (long)(u32)w16 * 4;
		s2 = LDF(x4, 0);
		s2 = fp_fmadd(s20, s30, s2);
		STF(x4, 0, s2);
	next:
		s7 = s19;
		w13 = (s32)((u32)w13 + 1);
	}
}

/* ttf__seg_line : x1 = point A, x2 = point B (paires de flottants) */
static void ttf_seg_line(long font, const float *a, const float *b)
{
	ttf_line(font, a[0], a[1], b[0], b[1]);
}

/* ttf__seg_quad : x1 = P0, x2 = P1 (contrôle), x3 = P2 : courbe aplatie */
static void ttf_seg_quad(long font, const float *p0, const float *p1, const float *p2)
{
	float c[8]; /* copies : P0, P1, P2, point précédent */
	float s0, s1, s2, s3, s4, s5, s6, s7;
	u32 w20, w21;

	c[0] = p0[0];
	c[1] = p0[1];
	c[6] = p0[0];
	c[7] = p0[1];
	c[2] = p1[0];
	c[3] = p1[1];
	c[4] = p2[0];
	c[5] = p2[1];
	s0 = c[0]; /* déviation |P0 - 2 P1 + P2|^2 */
	s1 = c[1];
	s2 = c[2];
	s3 = c[3];
	s4 = c[4];
	s5 = c[5];
	s6 = s0 + s4;
	s6 = s6 - s2;
	s6 = s6 - s2;
	s7 = s1 + s5;
	s7 = s7 - s3;
	s7 = s7 - s3;
	s6 = s6 * s6;
	s6 = fp_fmadd(s7, s7, s6);
	s7 = 3.0f;
	s6 = s6 * s7;
	s6 = fp_fsqrt(s6);
	s6 = fp_fsqrt(s6);
	w20 = (u32)fcvtzs_w(s6);
	w20 = w20 + 1; /* nombre de segments */
	w20 = w20 < 32 ? w20 : 32; /* lo : non signé */
	for (w21 = 1; w21 <= w20; w21++) {
		s0 = (float)(s32)w21;
		s1 = (float)(s32)w20;
		s0 = s0 / s1; /* t */
		s1 = 1.0f;
		s1 = s1 - s0;  /* 1 - t */
		s2 = s1 * s1;  /* (1-t)^2 */
		s3 = s0 * s1;
		s3 = s3 + s3;  /* 2t(1-t) */
		s4 = s0 * s0;  /* t^2 */
		s5 = s2 * c[0];
		s5 = fp_fmadd(s3, c[2], s5);
		s5 = fp_fmadd(s4, c[4], s5); /* x */
		s6 = s2 * c[1];
		s6 = fp_fmadd(s3, c[3], s6);
		s6 = fp_fmadd(s4, c[5], s6); /* y */
		s0 = c[6]; /* segment depuis le point précédent */
		s1 = c[7];
		c[6] = s5;
		c[7] = s6;
		ttf_line(font, s0, s1, s5, s6);
	}
}

/* ttf__contour : x1 = premier point, x2 = nombre de points (PT_SIZE octets :
 * x flottant, y flottant, drapeaux ; bit 0 = sur la courbe) */
static void ttf_contour(long font, long pts, long n)
{
	float cur[2], ctrl[2] = {0, 0}, start[2], mid[2];
	long last, p, left, pending;

	if (n < 2)
		return;
	last = pts + (n - 1) * PT_SIZE;
	if (LDB(pts, 8) & 1) { /* début = premier point (sur la courbe) */
		start[0] = LDF(pts, 0);
		start[1] = LDF(pts, 4);
		p = pts + PT_SIZE;
		left = n - 1;
	} else if (LDB(last, 8) & 1) { /* début = dernier point */
		start[0] = LDF(last, 0);
		start[1] = LDF(last, 4);
		p = pts;
		left = n - 1;
	} else { /* début = milieu implicite */
		float s0 = LDF(pts, 0), s1 = LDF(pts, 4);
		s0 = s0 + LDF(last, 0);
		s1 = s1 + LDF(last, 4);
		s0 = s0 * 0.5f;
		s1 = s1 * 0.5f;
		start[0] = s0;
		start[1] = s1;
		p = pts;
		left = n;
	}
	cur[0] = start[0];
	cur[1] = start[1];
	pending = 0; /* contrôle en attente */
	while (left) {
		if (LDB(p, 8) & 1) { /* point sur la courbe */
			float pt[2] = {LDF(p, 0), LDF(p, 4)};
			if (pending)
				ttf_seg_quad(font, cur, ctrl, pt);
			else
				ttf_seg_line(font, cur, pt);
			cur[0] = LDF(p, 0);
			cur[1] = LDF(p, 4);
			pending = 0;
		} else { /* point hors courbe */
			if (pending) { /* deux contrôles : milieu implicite */
				float s0 = ctrl[0], s1 = ctrl[1];
				s0 = s0 + LDF(p, 0);
				s1 = s1 + LDF(p, 4);
				s0 = s0 * 0.5f;
				s1 = s1 * 0.5f;
				mid[0] = s0;
				mid[1] = s1;
				ttf_seg_quad(font, cur, ctrl, mid);
				cur[0] = mid[0];
				cur[1] = mid[1];
			}
			ctrl[0] = LDF(p, 0);
			ctrl[1] = LDF(p, 4);
			pending = 1;
		}
		p += PT_SIZE;
		left--;
	}
	if (pending)
		ttf_seg_quad(font, cur, ctrl, start);
	else
		ttf_seg_line(font, cur, start);
}

/* ttf__simple : x1 = données glyf, x2 = nombre de contours, x3 =
 * transformation (6 flottants : xx xy yx yy dx dy, unités -> grille) */
static void ttf_simple(long font, long data, long ncont, const float *m)
{
	long pts, ends, flags, npts, i, ci, first, last, cnt;
	u32 w2, w4, w6;

	if (!ncont)
		return;
	pts = LDX(font, F_SCRATCH); /* zone des points */
	ends = data + 10;           /* endPtsOfContours[] */
	npts = (u32)((u32)be16(ends + (ncont - 1) * 2, 0) + 1); /* nombre de points */
	if (UGT(npts, LDX(font, F_MAXPTS)))
		return;
	flags = ends + ncont * 2;
	flags = flags + 2 + be16(flags, 0); /* après les instructions : drapeaux */

	/* --- drapeaux (avec répétitions) */
	i = 0;
	while (ULT(i, npts)) {
		w2 = (u32)LDB(flags, 0);
		flags++;
		STB(pts + i * PT_SIZE, 8, w2);
		i++;
		if (!(w2 & 8))
			continue;
		w4 = (u32)LDB(flags, 0); /* nombre de répétitions */
		flags++;
		while (w4) {
			if (!ULT(i, npts))
				goto fl_done;
			STB(pts + i * PT_SIZE, 8, w2);
			i++;
			w4--;
		}
	}
fl_done:
	/* --- coordonnées x (bits 1 = court, 4 = signe/identique) */
	w6 = 0;
	for (i = 0; ULT(i, npts); i++) {
		long q = pts + i * PT_SIZE;
		w2 = (u32)LDB(q, 8);
		if (w2 & 2) {
			w4 = (u32)LDB(flags, 0);
			flags++;
			if (!(w2 & 16))
				w4 = 0 - w4;
			w6 += w4;
		} else if (!(w2 & 16)) { /* (bit 4 sans bit 1 : identique au précédent) */
			w4 = (u32)LDB(flags, 0);
			w4 = (u32)LDB(flags, 1) | w4 << 8;
			flags += 2;
			w4 = (u32)(s32)(s16)w4;
			w6 += w4;
		}
		STW(q, 0, w6);
	}
	/* --- coordonnées y (bits 2 = court, 5 = signe/identique) */
	w6 = 0;
	for (i = 0; ULT(i, npts); i++) {
		long q = pts + i * PT_SIZE;
		w2 = (u32)LDB(q, 8);
		if (w2 & 4) {
			w4 = (u32)LDB(flags, 0);
			flags++;
			if (!(w2 & 32))
				w4 = 0 - w4;
			w6 += w4;
		} else if (!(w2 & 32)) {
			w4 = (u32)LDB(flags, 0);
			w4 = (u32)LDB(flags, 1) | w4 << 8;
			flags += 2;
			w4 = (u32)(s32)(s16)w4;
			w6 += w4;
		}
		STW(q, 4, w6);
	}
	/* --- transformation vers la grille */
	for (i = 0; ULT(i, npts); i++) {
		long q = pts + i * PT_SIZE;
		float s0 = (float)(s32)LDW(q, 0);
		float s1 = (float)(s32)LDW(q, 4);
		float s2, s3;
		s2 = m[0] * s0;
		s2 = fp_fmadd(m[1], s1, s2);
		s2 = s2 + m[4];
		s3 = m[2] * s0;
		s3 = fp_fmadd(m[3], s1, s3);
		s3 = s3 + m[5];
		STF(q, 0, s2);
		STF(q, 4, s3);
	}
	/* --- contours */
	first = 0; /* premier point du contour */
	for (ci = 0; ULT(ci, ncont); ci++) {
		last = be16(ends + ci * 2, 0); /* dernier point du contour */
		if (UGE(last, npts) || ULT(last, first))
			return;
		cnt = last - first + 1;
		i = pts + first * PT_SIZE;
		first += cnt;
		ttf_contour(font, i, cnt);
	}
}

/* ttf__outline : x1 = glyphe, x2 = transformation, x3 = profondeur
 * (glyphes composites imbriqués) */
static void ttf_outline(long font, long glyph, const float *m, long depth)
{
	ukr loc;
	long data, p;
	s32 nc;

	if (UGT(depth, 6))
		return;
	loc = ttf_glyph_loc(font, glyph);
	if (ULT(loc.r1, 10))
		return;
	data = loc.r0;
	nc = bes16(data, 0); /* nombre de contours */
	if (nc >= 0) {
		ttf_simple(font, data, (long)nc, m);
		return;
	}
	p = data + 10; /* curseur dans les composants */
	for (;;) {
		u32 fl = (u32)be16(p, 0);  /* drapeaux */
		long cg = be16(p, 2);      /* glyphe du composant */
		s32 a0, a1;
		float s0, s1, s2, s3, s4, s5, s6, r[6];
		p += 4;
		if (fl & 1) {
			a0 = bes16(p, 0);
			a1 = bes16(p, 2);
			p += 4;
		} else {
			a0 = (s32)LDSB(p, 0);
			a1 = (s32)LDSB(p, 1);
			p += 2;
		}
		if (!(fl & 2)) { /* ARGS_ARE_XY_VALUES ; sinon points appariés */
			a0 = 0;
			a1 = 0;
		}
		s4 = (float)a0; /* dx */
		s5 = (float)a1; /* dy */
		s0 = 1.0f;      /* xx */
		s1 = 0.0f;      /* xy */
		s2 = 0.0f;      /* yx */
		s3 = 1.0f;      /* yy */
		s6 = (float)16384;
		if (fl & 8) { /* WE_HAVE_A_SCALE */
			s0 = (float)bes16(p, 0);
			s0 = s0 / s6;
			s3 = s0;
			p += 2;
		} else if (fl & 64) { /* WE_HAVE_AN_X_AND_Y_SCALE */
			s0 = (float)bes16(p, 0);
			s0 = s0 / s6;
			s3 = (float)bes16(p, 2);
			s3 = s3 / s6;
			p += 4;
		} else if (fl & 128) { /* WE_HAVE_A_TWO_BY_TWO */
			s0 = (float)bes16(p, 0);
			s0 = s0 / s6; /* xscale */
			s2 = (float)bes16(p, 2);
			s2 = s2 / s6; /* scale01 -> yx */
			s1 = (float)bes16(p, 4);
			s1 = s1 / s6; /* scale10 -> xy */
			s3 = (float)bes16(p, 6);
			s3 = s3 / s6; /* yscale */
			p += 8;
		}
		/* R = parent o composant */
		r[0] = m[0] * s0;
		r[0] = fp_fmadd(m[1], s2, r[0]); /* R.xx */
		r[1] = m[0] * s1;
		r[1] = fp_fmadd(m[1], s3, r[1]); /* R.xy */
		r[2] = m[2] * s0;
		r[2] = fp_fmadd(m[3], s2, r[2]); /* R.yx */
		r[3] = m[2] * s1;
		r[3] = fp_fmadd(m[3], s3, r[3]); /* R.yy */
		r[4] = m[0] * s4;
		r[4] = fp_fmadd(m[1], s5, r[4]);
		r[4] = r[4] + m[4]; /* R.dx */
		r[5] = m[2] * s4;
		r[5] = fp_fmadd(m[3], s5, r[5]);
		r[5] = r[5] + m[5]; /* R.dy */
		ttf_outline(font, cg, r, depth + 1);
		if (!(fl & 32)) /* MORE_COMPONENTS */
			return;
	}
}

/* ttf__advance converti : avance en 1/64 de pixel (ttf__adv64) ;
 * même suite pour le crénage (k : valeur en unités) */
static long ttf_units64(long font, long k)
{
	float s0 = (float)k;
	s0 = s0 * LDF(font, F_SCALE);
	s0 = s0 * (float)64;
	return fcvtns_x(s0);
}

/* ttf_raster : x1 = glyphe -> 1 (ou 0 si scratch trop petit). Remplit la
 * grille et F_GX0, F_GY0, F_GW, F_ACCH, F_ACCW, F_GADV. */
static long ttf_raster(long font, long glyph)
{
	ukr loc;
	float s0, s1, m[6];
	s32 w21, w22, w5, w6, w23, w24, w7;
	long x8, x9, x10, i;

	STX(font, F_GADV, ttf_units64(font, ttf_advance(font, glyph))); /* avance en 1/64 */
	STX(font, F_GW, 0);
	STX(font, F_ACCH, 0);
	STX(font, F_GX0, 0);
	STX(font, F_GY0, 0);
	loc = ttf_glyph_loc(font, glyph);
	if (ULT(loc.r1, 10))
		return 1; /* glyphe vide (espace) */
	s0 = LDF(font, F_SCALE);
	s1 = (float)bes16(loc.r0, 2); /* xMin */
	s1 = s1 * s0;
	w21 = fcvtms_w(s1); /* bx0 = floor(xMin * s) */
	s1 = (float)bes16(loc.r0, 6); /* xMax */
	s1 = s1 * s0;
	w5 = fcvtps_w(s1); /* bx1 = ceil(xMax * s) */
	s1 = (float)bes16(loc.r0, 8); /* yMax */
	s1 = -(s1 * s0);              /* fnmul */
	w22 = fcvtms_w(s1);           /* by0 = floor(-yMax * s) */
	s1 = (float)bes16(loc.r0, 4); /* yMin */
	s1 = -(s1 * s0);
	w6 = fcvtps_w(s1); /* by1 = ceil(-yMin * s) */
	w23 = (s32)((u32)w5 - (u32)w21); /* largeur */
	w24 = (s32)((u32)w6 - (u32)w22); /* hauteur */
	if (w23 <= 0 || w24 <= 0)
		return 1;
	w7 = (s32)((u32)w23 + 3); /* largeur de ligne de la grille */
	x8 = (long)(u32)((u32)(w24 + 1) * (u32)w7);
	x8 = x8 << 2;
	x8 = (x8 + 15) & ~15L; /* octets de grille */
	x9 = LDX(font, F_PTSBYTES);
	if (UGT(x9 + x8, LDX(font, F_SCRATCH_LEN)))
		return 0;
	x10 = LDX(font, F_SCRATCH) + x9;
	STX(font, F_ACC, x10);
	STX(font, F_ACCW, (long)w7);
	STX(font, F_ACCH, (long)w24);
	STX(font, F_GX0, (long)w21);
	STX(font, F_GY0, (long)w22);
	STX(font, F_GW, (long)(s32)((u32)w23 + 2));
	for (i = 0; i < x8; i += 16) { /* grille à zéro */
		STX(x10, i, 0);
		STX(x10, i + 8, 0);
	}
	/* transformation initiale : unités -> grille (y vers le bas) */
	s0 = LDF(font, F_SCALE);
	m[0] = s0;
	m[1] = 0.0f;
	m[2] = 0.0f;
	m[3] = -s0;
	m[4] = -(float)w21;
	m[5] = -(float)w22;
	ttf_outline(font, glyph, m, 0);
	return 1;
}

/* ttf_render_glyph : x1 = glyphe, x2 = métriques (GM_SIZE octets), x3 = bitmap
 * 8 bits (0 = métriques seules), x4 = pas -> 1 si succès. (Pas appelée par
 * UIKit : gardée comme l'asm.) */
static __attribute__((unused)) long ttf_render_glyph(long font, long glyph, long gm, long bmp, long pitch)
{
	long acc, accw, h, gw, x;
	float s0, s1;

	if (!ttf_raster(font, glyph))
		return 0;
	STW(gm, GM_W, LDX(font, F_GW));
	STW(gm, GM_H, LDX(font, F_ACCH));
	STW(gm, GM_BX, LDX(font, F_GX0));
	STW(gm, GM_BY, LDX(font, F_GY0));
	STW(gm, GM_ADV64, LDX(font, F_GADV));
	if (!bmp)
		return 1;
	acc = LDX(font, F_ACC);
	accw = LDX(font, F_ACCW);
	h = LDX(font, F_ACCH);
	gw = LDX(font, F_GW);
	for (; h; h--) {
		s0 = 0.0f; /* somme préfixe de la ligne */
		for (x = 0; ULT(x, gw); x++) {
			s0 = s0 + LDF(acc, x * 4);
			s1 = __builtin_fabsf(s0);
			s1 = fp_fmin(s1, 1.0f);
			s1 = s1 * (float)255;
			STB(bmp, x, fcvtns_w(s1));
		}
		acc += accw * 4;
		bmp += pitch;
	}
	return 1;
}

/* ======================================================================== */
/* Texte                                                                    */
/* ======================================================================== */

/* ttf_utf8_next : x0 = chaîne UTF-8 -> r0 = point de code (0 = fin), r1 = suite */
ukr ttf_utf8_next(long s)
{
	ukr r = {0, 0, 0, 0};
	u32 b = (u32)LDB(s, 0), cp, n;
	long p;

	if (!b)
		return r;
	p = s + 1;
	r.r1 = p;
	if (!(b & 0x80)) {
		r.r0 = b;
		return r;
	}
	if ((b & 0xE0) == 0xC0) {
		cp = b & 0x1F;
		n = 1;
	} else if ((b & 0xF0) == 0xE0) {
		cp = b & 0x0F;
		n = 2;
	} else if ((b & 0xF8) == 0xF0) {
		cp = b & 0x07;
		n = 3;
	} else {
		r.r0 = 0xFFFD;
		return r;
	}
	do {
		b = (u32)LDB(p, 0);
		if ((b & 0xC0) != 0x80) { /* suite invalide : on ne la consomme pas */
			r.r0 = 0xFFFD;
			r.r1 = p;
			return r;
		}
		p++;
		cp = (b & 0x3F) | cp << 6;
	} while (--n);
	r.r0 = cp;
	r.r1 = p;
	return r;
}

/* ttf__pick : x1 = point de code -> r0 = police qui a le glyphe (celle de
 * F_FALLBACK, à la même taille, s'il manque), r1 = glyphe */
static ukr ttf_pick(long font, long cp)
{
	ukr r = {0, 0, 0, 0};
	long g = ttf_glyph_index(font, cp), fb, g2;

	if (!g && LDX(font, F_FALLBACK)) {
		fb = UK_CB1(LDX(font, F_FALLBACK), font);
		if (fb) {
			if (LDX(font, F_PIXELS) != LDX(fb, F_PIXELS))
				ttf_set_size(fb, LDX(font, F_PIXELS));
			g2 = ttf_glyph_index(fb, cp);
			if (g2) {
				r.r0 = fb;
				r.r1 = g2;
				return r;
			}
		}
	}
	r.r0 = font; /* absent des deux : celui de la police */
	r.r1 = g;
	return r;
}

/* ttf__adv64 : x1 = glyphe -> avance en 1/64 de pixel */
static long ttf_adv64(long font, long glyph)
{
	return ttf_units64(font, ttf_advance(font, glyph));
}

/* ttf_text_width : x0 = font, x1 = chaîne UTF-8 -> largeur en pixels */
long ttf_text_width(long font, long s)
{
	return ttf_text_pos(font, s, -1, -1, 0).r0;
}

/* ttf_text_pos : x0 = font, x1 = chaîne UTF-8, x2 = octets au plus (-1 :
 * jusqu'au NUL), x3 = x cherché (-1 : aucun), x4 = 0 : milieu du glyphe (clic),
 * 1 : sa fin (ce qui tient dans x3) -> r0 = largeur parcourue en pixels,
 * r1 = octets parcourus. Même cumul que ttf_draw_text. */
ukr ttf_text_pos(long font, long s, long max, long x, long mode)
{
	ukr r = {0, 0, 0, 0};
	long orig = font, f = font, cur = s, end, target, pen = 0, prev = 0;
	long next, cp, adv, x2, x3;
	long tab;
	ukr pk, u;

	end = s + max; /* fin (borne) */
	if (max == -1)
		end = -1;
	target = LSL(x, 6); /* x cherché en 1/64 (négatif : aucun) */
	if (x < 0)
		target = -1;
	for (;;) {
		/* (après un glyphe de repli : la police du texte, sans crénage) */
		if (orig != f)
			prev = 0;
		f = orig;
		if (UGE(cur, end))
			break;
		u = ttf_utf8_next(cur);
		cp = u.r0;
		if (!cp)
			break;
		if (UGT(u.r1, end)) /* (glyphe coupé par la borne : arrêt) */
			break;
		next = u.r1;
		if (cp == 13) { /* retour chariot : invisible, sans largeur */
			cur = next;
			continue;
		}
		tab = cp == 9; /* tabulation : 4 espaces */
		if (tab)
			cp = 32;
		pk = ttf_pick(f, cp);
		if (pk.r0 != orig)
			prev = 0;
		f = pk.r0;
		x2 = pen; /* stylo avant le glyphe (crénage compris) */
		if (prev)
			x2 = pen + ttf_units64(f, ttf_kern(f, prev, pk.r1));
		prev = pk.r1;
		adv = ttf_adv64(f, prev);
		if (tab) { /* jusqu'au taquet suivant */
			long x1 = LSL(adv, 2);
			x3 = x2 < 0 ? 0 : x2;
			adv = x1 - UREM(x3, x1);
		}
		if (target >= 0) {
			x3 = ASR(adv, 1); /* milieu (ou fin) du glyphe au-delà du x */
			if (mode)
				x3 = adv;
			x3 = x2 + x3;
			if (x3 > target)
				break;
		}
		pen = x2 + adv;
		cur = next;
	}
	r.r0 = ASR(pen + 32, 6);
	r.r1 = cur - s;
	return r;
}

/* (25/11/2026) un bord de glyphe (couverture a, 1..255 sur 256) sur un fond en partie transparent (0xTTRRGGBB, TT : la
 * transparence, couleur non prémultipliée, comme ic_blit) : la couleur du texte à cette opacité devant le fond. Le
 * mélange de toujours (couleurs seules, pixel rendu opaque) frangeait de sombre le texte tracé sur une fenêtre
 * transparente (WF_ALPHA : le noir du fond vide entrait dans le bord des lettres) */
static u32 ttf_mix_alpha(u32 color, u32 bg, u32 a)
{
	u32 ab = 255 - (bg >> 24), as = a > 255 ? 255 : a; /* opacités du fond, du glyphe (0..255) */
	u32 rest = (ab * (255 - as) + 127) / 255, ao = as + rest, o, ch;
	if (!ao)
		return bg;
	o = (255 - ao) << 24;
	for (ch = 0; ch < 24; ch += 8) {
		u32 cs = (color >> ch) & 0xFF, cd = (bg >> ch) & 0xFF;
		u32 v = (cs * as + cd * rest + ao / 2) / ao;
		o |= (v > 255 ? 255 : v) << ch;
	}
	return o;
}

/* ttf__blend : mélange de la grille courante : x1 = surface, x2 = pas,
 * x3 = x, x4 = y (coin haut-gauche), w5 = couleur, w6 / w7 = clip */
static void ttf_blend(long font, long surf, long pitch, long x, long y, u32 color, u32 cw, u32 ch)
{
	long acc = LDX(font, F_ACC), accw = LDX(font, F_ACCW), h = LDX(font, F_ACCH), gw = LDX(font, F_GW);
	long clw = (long)(s32)cw, clh = (long)(s32)ch; /* sxtw */
	long line, col, sx, dst;
	u32 rb = color & 0x00FF00FF, g = color & 0x0000FF00, a, bg, ia, w0, w19;
	float s0, s1;

	for (; h; h--, acc += accw * 4, y++) {
		if (y < 0)
			continue;
		if (y >= clh)
			return;
		line = y * pitch + surf;
		s0 = 0.0f;
		for (col = 0; ULT(col, gw);) {
			s0 = s0 + LDF(acc, col * 4);
			sx = x + col; /* x écran */
			col++;
			if (sx < 0 || sx >= clw)
				continue;
			s1 = __builtin_fabsf(s0);
			s1 = fp_fmin(s1, 1.0f);
			s1 = s1 * (float)256;
			a = (u32)fcvtns_w(s1); /* alpha 0..256 */
			if (!a)
				continue;
			dst = line + sx * 4;
			if (a >= 256) { /* opaque : couleur pure */
				STW(dst, 0, rb | g);
				continue;
			}
			bg = (u32)LDW(dst, 0); /* fond */
			if (bg >> 24) { /* fond en partie transparent : la vraie composition */
				STW(dst, 0, ttf_mix_alpha(rb | g, bg, a));
				continue;
			}
			ia = 256 - a;
			w0 = (bg & 0x00FF00FF) * ia;
			w0 = rb * a + w0;
			w0 = w0 >> 8;
			w0 &= 0x00FF00FF;
			w19 = (bg & 0x0000FF00) * ia;
			w19 = g * a + w19;
			w19 = w19 >> 8;
			w19 &= 0x0000FF00;
			STW(dst, 0, w0 | w19);
		}
	}
}

/* ttf__blend8 : mélange d'un bitmap du cache : x1 = entrée, x2 = surface,
 * x3 = pas, x4 = x du stylo, x5 = y de base, w6 = couleur, x7 = clip
 * (largeur | hauteur << 32) */
static void ttf_blend8(long font, long e, long surf, long pitch, long x, long y, u32 color, long clip)
{
	long bmp = LDX(font, F_CACHE) + TC_HEADER + LDW(e, TC_OFF);
	long w = LDH(e, TC_W), col, sx, line, dst;
	u32 h = (u32)LDH(e, TC_H);
	/* comme l'asm : largeur zéro-étendue (w11), hauteur par lsr : comparées en
	 * 64 bits signés (ttf__blend, lui, les signe-étend) */
	long clw = (long)(u32)clip, clh = LSR(clip, 32);
	u32 rb = color & 0x00FF00FF, g = color & 0x0000FF00, full = rb | g, a, bg, ia, w0, w19;

	x = x + LDSH(e, TC_BX); /* coin haut-gauche */
	y = y + LDSH(e, TC_BY);
	for (; h; h--, bmp += w, y++) {
		if (y < 0)
			continue;
		if (y >= clh)
			return;
		line = y * pitch + surf;
		for (col = 0; ULT(col, w);) {
			a = (u32)LDB(bmp, col);
			sx = x + col;
			col++;
			if (!a || sx < 0 || sx >= clw)
				continue;
			dst = line + sx * 4;
			if (a >= 255) {
				STW(dst, 0, full);
				continue;
			}
			bg = (u32)LDW(dst, 0);
			if (bg >> 24) { /* fond en partie transparent : la vraie composition */
				STW(dst, 0, ttf_mix_alpha(full, bg, a));
				continue;
			}
			a = a + (a >> 7); /* 0..255 -> 0..256 */
			ia = 256 - a;
			w19 = (bg & 0x00FF00FF) * ia;
			w19 = rb * a + w19;
			w19 = w19 >> 8;
			w19 &= 0x00FF00FF;
			w0 = (bg & 0x0000FF00) * ia;
			w0 = g * a + w0;
			w0 = w0 >> 8;
			w0 &= 0x0000FF00;
			STW(dst, 0, w0 | w19);
		}
	}
}

/* ======================================================================== */
/* Cache de glyphes                                                         */
/* ======================================================================== */

/* ttf__cache_flush : vide le cache */
static void ttf_cache_flush(long font)
{
	long c = LDX(font, F_CACHE), i;

	STX(font, F_CACHE_USED, 0);
	for (i = 0; i < TC_HEADER; i += 16) {
		STX(c, i, 0);
		STX(c, i + 8, 0);
	}
}

/* ttf_set_cache : x0 = font, x1 = tampon (aligné 16), x2 = taille
 * (>= TTF_CACHE_MIN ; 0 = pas de cache) -> x0 = font */
long ttf_set_cache(long font, long buf, long size)
{
	if (ULT(size, TTF_CACHE_MIN)) {
		STX(font, F_CACHE, 0);
		return font;
	}
	STX(font, F_CACHE, buf);
	STX(font, F_CACHE_LEN, size);
	ttf_cache_flush(font);
	return font;
}

/* ttf__cache_get : x1 = glyphe -> r0 = entrée du cache (remplie), ou 0 avec
 * r1 = 1 si la grille de ttf_raster est valide mais non mise en cache,
 * r1 = 0 en cas d'échec */
static ukr ttf_cache_get(long font, long glyph)
{
	ukr r = {0, 0, 0, 0};
	long px = LDX(font, F_PIXELS) & 0x7FFF;
	u32 key = ((u32)glyph | (u32)px << 16) | 0x80000000u; /* clé */
	u32 slot = ((u32)glyph * 31 + ((u32)px << 3) + (u32)px) & (TC_SLOTS - 1);
	long e = LDX(font, F_CACHE) + (long)slot * 16; /* entrée */
	long gw, gh, size, room, used, dst, acc, accw, x;
	float s0, s1;

	if ((u32)LDW(e, TC_KEY) == key) {
		r.r0 = e;
		return r;
	}
	if (!ttf_raster(font, glyph)) /* absent : rastériser */
		return r;
	gw = LDX(font, F_GW);
	gh = LDX(font, F_ACCH);
	size = (gw * gh + 15) & ~15L; /* octets du bitmap */
	room = LDX(font, F_CACHE_LEN) - TC_HEADER; /* taille de la zone de données */
	if (UGT(size, room)) {
		r.r1 = 1;
		return r;
	}
	used = LDX(font, F_CACHE_USED);
	if (UGT(used + size, room)) { /* zone pleine : on vide tout */
		ttf_cache_flush(font);
		used = 0;
	}
	STX(font, F_CACHE_USED, used + size);
	STW(e, TC_KEY, key);
	STH(e, TC_W, gw);
	STH(e, TC_H, gh);
	STH(e, TC_BX, LDX(font, F_GX0));
	STH(e, TC_BY, LDX(font, F_GY0));
	STW(e, TC_OFF, used);
	dst = LDX(font, F_CACHE) + TC_HEADER + used; /* grille -> octets de couverture */
	acc = LDX(font, F_ACC);
	accw = LDX(font, F_ACCW);
	for (; gh; gh--) {
		s0 = 0.0f;
		for (x = 0; ULT(x, gw); x++) {
			s0 = s0 + LDF(acc, x * 4);
			s1 = __builtin_fabsf(s0);
			s1 = fp_fmin(s1, 1.0f);
			s1 = s1 * (float)255;
			STB(dst, 0, fcvtns_w(s1));
			dst++;
		}
		acc += accw * 4;
	}
	r.r0 = e;
	return r;
}

/* ttf_draw_text : x0 = font, x1 = chaîne UTF-8, x2 = surface 32 bpp, x3 = pas
 * en octets, x4 = x du stylo, x5 = y de la ligne de base, w6 = couleur
 * 0x00RRGGBB, x7 = clip (largeur | hauteur << 32). Mélange alpha avec le
 * fond. -> x du stylo après le texte */
long ttf_draw_text(long font, long s, long surf, long pitch, long x, long y, long color, long clip)
{
	long orig = font, f = font, pen = LSL(x, 6), start, prev = 0, cp, adv;
	u32 col = (u32)color;
	long cw = (u32)clip;         /* largeur de clip (w26 : zéro-étendue) */
	long chh = LSR(clip, 32);    /* hauteur de clip */
	long tab;
	ukr u, pk, ce;

	start = pen; /* stylo en 1/64 de pixel */
	for (;;) {
		/* (après un glyphe de repli : la police du texte, sans crénage) */
		if (orig != f)
			prev = 0;
		f = orig;
		u = ttf_utf8_next(s);
		cp = u.r0;
		if (!cp)
			break;
		s = u.r1;
		if (cp == 13) /* retour chariot : invisible */
			continue;
		tab = cp == 9; /* tabulation : 4 espaces */
		if (tab)
			cp = 32;
		pk = ttf_pick(f, cp);
		if (pk.r0 != orig)
			prev = 0;
		f = pk.r0;
		if (prev)
			pen = pen + ttf_units64(f, ttf_kern(f, prev, pk.r1));
		prev = pk.r1;
		if (LDX(f, F_CACHE)) {
			ce = ttf_cache_get(f, prev);
			if (ce.r0) {
				if (LDH(ce.r0, TC_W)) /* bitmap du cache */
					ttf_blend8(f, ce.r0, surf, pitch, ASR(pen + 32, 6), y, col, cw | LSL(chh, 32));
				goto advance;
			}
			if (!ce.r1) /* échec de rastérisation */
				goto advance;
			/* trop gros pour le cache : grille valide */
		} else if (!ttf_raster(f, prev)) {
			goto advance;
		}
		if (LDX(f, F_GW))
			ttf_blend(f, surf, pitch, ASR(pen + 32, 6) + LDX(f, F_GX0), y + LDX(f, F_GY0), col, (u32)cw,
				  (u32)chh);
	advance:
		adv = ttf_adv64(f, prev);
		if (tab) { /* jusqu'au taquet suivant */
			long x1 = LSL(adv, 2);
			/* comme l'asm : subs + csel lt : vrai si pen < start (signé) */
			long x2 = pen < start ? 0 : pen - start;
			adv = x1 - UREM(x2, x1);
		}
		pen = pen + adv;
	}
	return ASR(pen + 32, 6);
}
