/* AudioKit : CELT (RFC 6716 4.3), décodeur, d'après ak_celt.inc (au bit près).
 *
 * Une trame : silence, post-filtre (filtre en peigne), transitoire (blocs
 * courts), énergies grossières (prédiction, Laplace), tf_res, étalement,
 * dynalloc, trim, allocation des bits (4.3.3), énergies fines, bandes
 * (quantification vectorielle PVQ, CWRS ; division récursive des bandes,
 * theta, stéréo d'intensité, double stéréo, M/S ; repliement avec la graine),
 * anti-effondrement, énergies finales, dénormalisation, IMDCT (ak_mdct.c),
 * recouvrement de 120 (TDAC), post-filtre, désaccentuation. Flottant 32 bits ;
 * le signal interne est à l'échelle de 32768.
 *
 * Dans l'asm, x19 = état et x20 = décodeur de plage dans tout le fichier : ici
 * ce et ec, passés à chaque fonction. Les fonctions internes de l'asm qui
 * rendent plusieurs registres (ce_theta) rendent une struct. Les calculs sur
 * des registres w (32 bits) se font en u32 / s32, ceux sur x en u64 / s64 ;
 * les décalages par registre sont masqués comme AArch64 (& 31 en 32 bits). */
#include "ak_celt.h"
#include "ak_opus.h"

#define EB(i) ((u32)ce_eband[i])
#define SHL32(v, n) ((u32)(v) << ((n) & 31))
#define SHR32(v, n) ((u32)(v) >> ((n) & 31))
#define ASR32(v, n) ((s32)(v) >> ((n) & 31))
#define MINUS28 0xC1E00000u

/* ---------------------------------------------------------------------------
 * État
 * ------------------------------------------------------------------------- */

long celt_new(struct akd *d, u64 cc, struct akmdct *md)
{
	struct celt *ce = akd_alloc(d, CE_SIZE);
	if (!ce)
		return AKE_NOMEM;
	ce->cc = cc;
	ce->md = md;
	float *m = akd_alloc(d, CE_MEMN * 4 * 2 + 64);
	if (!m)
		return AKE_NOMEM;
	ce->mem[0] = m;
	ce->mem[1] = m + CE_MEMN;
	if (!(ce->x = akd_alloc(d, 2 * 960 * 4 + 64)))
		return AKE_NOMEM;
	if (!(ce->norm = akd_alloc(d, 2 * 1024 * 4 + 64)))
		return AKE_NOMEM;
	if (!(ce->freq = akd_alloc(d, 2 * 960 * 4 + 64)))
		return AKE_NOMEM;
	if (!(ce->tmp = akd_alloc(d, 3 * 960 * 4 + 64)))
		return AKE_NOMEM;
	if (!(ce->ut = akd_alloc(d, 2048 * 4 + 64)))
		return AKE_NOMEM;
	if (!(ce->iy = akd_alloc(d, 256 * 4 + 64)))
		return AKE_NOMEM;
	celt_reset(ce);
	return (long)ce;
}

void celt_reset(struct celt *ce)
{
	memset(ce->mem[0], 0, CE_MEMN * 4 * 2);
	ce->seed = 0;
	ce->pfper = 0;
	ce->pfperold = 0;
	ce->pfgain = 0;
	ce->pfgain_pad = 0;
	ce->pfgainold = 0;
	ce->pfgainold_pad = 0;
	ce->pftap = 0;
	ce->pftapold = 0;
	ce->preemph[0] = 0;
	ce->preemph[1] = 0;
	for (int i = 0; i < 2 * CE_NBE; i++) {
		ce->olde[i] = 0;
		ce->oldloge[i] = ak_u2f(MINUS28);
		ce->oldloge2[i] = ak_u2f(MINUS28);
	}
}

/* ---------------------------------------------------------------------------
 * Outils
 * ------------------------------------------------------------------------- */

float ce_exp2(float x)
{
	static const double c[9] = {0.6931471805599453,	  1.0,
				    1.0,		  0.5,
				    0.16666666666666666,  0.041666666666666664,
				    0.008333333333333333, 0.001388888888888889,
				    0.0001984126984126984};
	double d = (double)x;
	double k = ak_drintn(d);
	d = d - k; /* f dans [-1/2, 1/2] */
	s64 ki = ak_dcvtzs_x(k);
	double t = d * c[0]; /* f ln 2 */
	double e = c[8];     /* e^t (Horner : 1/7! ... 1/0!) */
	for (int i = 7; i >= 1; i--)
		e = ak_dmadd(e, t, c[i]);
	if (ki < -1000)
		return 0.0f;
	if (ki > 1000)
		return ak_u2f(0x7F7FFFFF);
	e = e * ak_u2d((u64)(ki + 1023) << 52);
	return (float)e;
}

s32 ce_bexcos(s32 x)
{
	s32 x2 = (x * x + 4096) >> 13;
	s32 t = (-626 * x2 + 16384) >> 15;
	t = (s16)(t + 8277);
	t = (t * x2 + 16384) >> 15;
	t = (s16)(t - 7651);
	t = (t * x2 + 16384) >> 15;
	return 32767 - x2 + t + 1;
}

s32 ce_log2tan(s32 isin, s32 icos)
{
	s32 lc = 32 - (s32)ak_clz32((u32)icos);
	s32 ls = 32 - (s32)ak_clz32((u32)isin);
	icos = (s32)SHL32(icos, 15 - lc);
	isin = (s32)SHL32(isin, 15 - ls);
	s32 r = (s32)SHL32(ls - lc, 11);
	s32 t = (isin * -2597 + 16384) >> 15;
	t = (s16)(t + 7932);
	t = (t * isin + 16384) >> 15;
	r += t;
	t = (icos * -2597 + 16384) >> 15;
	t = (s16)(t + 7932);
	t = (t * icos + 16384) >> 15;
	return r - t;
}

/* v -> floor(sqrt(v)) (isqrt32, par le double) */
static u32 isqrt(u32 v)
{
	return ak_dcvtzu_w(ak_dsqrt((double)v));
}

/* les cases -1 de ce_cindex (LM = -1, bandes 0 à 7) ne sont jamais lues : une
 * bande de largeur 1 n'est pas divisée jusqu'à LM = -1 */
const u8 *ce_cache(u32 band, s32 lm)
{
	u32 idx = (u32)(lm + 1) * CE_NBE + band;
	return ce_cbits + ce_cindex[idx];
}

u32 ce_b2p(u32 band, s32 lm, s32 bits)
{
	const u8 *c = ce_cache(band, lm);
	s32 b = bits - 1;
	u32 lo = 0, hi = c[0];
	for (int k = 0; k < 6; k++) {
		u32 mid = (lo + hi + 1) >> 1;
		if ((s32)c[mid] >= b)
			hi = mid;
		else
			lo = mid;
	}
	s32 l = lo ? (s32)c[lo] : -1;
	return b - l <= (s32)c[hi] - b ? lo : hi;
}

u32 ce_p2b(u32 band, s32 lm, u32 q)
{
	if (!q)
		return 0;
	return ce_cache(band, lm)[q] + 1u;
}

u32 ce_getp(u32 q)
{
	if (q < 8)
		return q;
	return SHL32((q & 7) + 8, (q >> 3) - 1);
}

/* ---------------------------------------------------------------------------
 * Énergies
 * ------------------------------------------------------------------------- */

/* énergies grossières (RFC 6716 4.3.2.1) : prédiction temporelle (sauf intra)
 * et fréquentielle, résidus de Laplace */
static void coarse(struct celt *ce, struct ec_dec *ec)
{
	u64 start = ce->start, end = ce->end, C = ce->c, lm = ce->lm, intra = ce->intra;
	const u8 *prob = ce_eprob + (intra + lm * 2) * 42;
	float coef, beta;
	if (intra) {
		coef = 0.0f;
		beta = ce_pred[8];
	} else {
		coef = ce_pred[lm];
		beta = ce_pred[4 + lm];
	}
	float prev[2] = {0.0f, 0.0f};
	s64 budget = (s64)(ec->storage << 3);
	for (u64 i = start; i < end; i++)
		for (u64 c = 0; c < C; c++) {
			s64 left = budget - ec_tell(ec);
			s32 qi;
			if (left >= 15) {
				u64 pi = (i < 20 ? i : 20) * 2;
				qi = ec_laplace(ec, (u32)prob[pi] << 7, (u32)prob[pi + 1] << 6);
			} else if (left >= 2) {
				u32 q = ec_icdf(ec, ce_small_icdf, 2);
				qi = (s32)((q >> 1) ^ (0u - (q & 1)));
			} else if (left >= 1) {
				qi = -(s32)ec_bit_logp(ec, 1);
			} else {
				qi = -1;
			}
			float q = (float)qi;
			u64 k = c * CE_NBE + i;
			float t = ak_fmax(ce->olde[k], ak_u2f(0xC1100000)) * coef;
			int p = c ? 1 : 0;
			t = t + prev[p];
			t = t + q;
			prev[p] = prev[p] + q; /* prev = prev + q - beta q */
			prev[p] = ak_fmsub(beta, q, prev[p]);
			ce->olde[k] = t;
		}
}

/* tf_res (RFC 6716 4.3.4.5) */
static void tf_decode(struct celt *ce, struct ec_dec *ec)
{
	u64 start = ce->start, end = ce->end, trans = ce->trans, lm = ce->lm;
	s64 budget = (s64)(ec->storage << 3);
	s64 tell = ec_tell(ec);
	s64 logp = trans ? 2 : 4;
	s64 rsv = 0;
	if (lm && !(tell + logp + 1 > budget))
		rsv = 1;
	budget -= rsv;
	u64 changed = 0, curr = 0;
	for (u64 i = start; i < end; i++) {
		if (!(tell + logp > budget)) {
			curr ^= ec_bit_logp(ec, (u32)logp);
			tell = ec_tell(ec);
			changed |= curr;
		}
		ce->tfres[i] = (s32)curr;
		logp = trans ? 4 : 5;
	}
	const s8 *sel = ce_tfsel + lm * 8 + trans * 4;
	u64 tsel = 0;
	if (rsv && sel[changed] != sel[changed + 2])
		tsel = ec_bit_logp(ec, 1);
	sel += tsel * 2;
	for (u64 i = start; i < end; i++)
		ce->tfres[i] = sel[(u32)ce->tfres[i]];
}

/* énergies fines (RFC 6716 4.3.2.2) */
static void fine(struct celt *ce, struct ec_dec *ec)
{
	u64 C = ce->c;
	for (u64 i = ce->start; i < ce->end; i++) {
		s32 fq = ce->fineq[i];
		if (fq <= 0)
			continue;
		for (u64 c = 0; c < C; c++) {
			u32 q2 = ec_bits(ec, (u32)fq);
			float off = (float)q2 + 0.5f; /* (q2 + 1/2) 2^-fq - 1/2 */
			off = off * ak_u2f(SHL32(127 - fq, 23));
			off = off - 0.5f;
			u64 k = c * CE_NBE + i;
			ce->olde[k] = ce->olde[k] + off;
		}
	}
}

/* énergies finales : bits restants, deux passes de priorité (4.3.2.2) */
static void final_energy(struct celt *ce, struct ec_dec *ec)
{
	u64 C = ce->c;
	s64 left = (s64)(ce->len << 3) - ec_tell(ec);
	for (u64 p = 0; p < 2; p++)
		for (u64 i = ce->start; i < ce->end; i++) {
			if (left < (s64)C)
				break;
			s32 fq = ce->fineq[i];
			if (fq >= 8 || (u64)(u32)ce->prio[i] != p)
				continue;
			for (u64 c = 0; c < C; c++) {
				u32 q2 = ec_bits(ec, 1);
				float off = (float)q2 - 0.5f; /* (q2 - 1/2) 2^-(fq + 1) */
				off = off * ak_u2f(SHL32(126 - fq, 23));
				u64 k = c * CE_NBE + i;
				ce->olde[k] = ce->olde[k] + off;
				left--;
			}
		}
}

/* ---------------------------------------------------------------------------
 * Allocation des bits (RFC 6716 4.3.3 : clt_compute_allocation,
 * interp_bits2pulses)
 * ------------------------------------------------------------------------- */

/* vecteur -> bits demandés (bissection ; .vsum de l'asm) */
static s64 vsum(const struct celt *ce, u64 v)
{
	u64 start = ce->start, end = ce->end;
	u32 C = (u32)ce->c, lm = (u32)ce->lm;
	const u8 *ba = ce_balloc + v * CE_NBE;
	s64 psum = 0;
	int done = 0;
	for (u64 j = end; j > start;) {
		j--;
		s32 b = ASR32(SHL32((EB(j + 1) - EB(j)) * C * ba[j], lm), 2);
		if (b > 0) {
			b += ce->trimoff[j];
			if (!(b > 0))
				b = 0;
		}
		b += ce->offs[j];
		if (b >= ce->thresh[j] || done) {
			done = 1;
			psum += b < ce->cap[j] ? b : ce->cap[j];
		} else if (b >= (s32)(C << 3)) {
			psum += C << 3;
		}
	}
	return psum;
}

/* bits disponibles (1/8 ; w1 de l'asm) -> pulses (bits de chaque bande),
 * fineq, prio, coded, intens, dual, balance */
static void alloc(struct celt *ce, struct ec_dec *ec, s32 bits)
{
	u64 start = ce->start, end = ce->end;
	u32 C = (u32)ce->c, lm = (u32)ce->lm;
	s64 total = bits > 0 ? bits : 0;
	u64 skip_start = start;
	/* réserves : fin des bandes sautées, intensité, double stéréo */
	s64 skip_rsv = total >= 8 ? 8 : 0;
	total -= skip_rsv;
	s64 irsv = 0, drsv = 0;
	if (C == 2) {
		irsv = ce_log2frac[end - start];
		if (irsv > total) {
			irsv = 0;
		} else {
			total -= irsv;
			if (total >= 8) {
				drsv = 8;
				total -= drsv;
			}
		}
	}
	/* seuils et pente du trim */
	s32 trim = (s32)ce->trim;
	for (u64 j = start; j < end; j++) {
		u32 N = EB(j + 1) - EB(j);
		s32 t = (s32)(SHL32(SHL32(N * 3, lm), 3) >> 4);
		s32 c8 = (s32)(C << 3);
		ce->thresh[j] = t > c8 ? t : c8;
		s32 v = (s32)(C * N) * (trim - 5 - (s32)lm) * (s32)(end - j - 1);
		v = ASR32(SHL32(v, lm + 3), 6);
		if (SHL32(N, lm) == 1)
			v -= (s32)(C << 3);
		ce->trimoff[j] = v;
	}
	/* bissection sur les vecteurs d'allocation */
	s64 lo = 1, hi = 10;
	while (lo <= hi) {
		s64 mid = (s64)((u64)(lo + hi) >> 1);
		if (vsum(ce, (u64)mid) <= total)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	hi = lo;
	lo = lo - 1;
	for (u64 j = start; j < end; j++) {
		u32 N = EB(j + 1) - EB(j);
		s32 b1 = ASR32(SHL32(ce_balloc[lo * CE_NBE + j] * N * C, lm), 2);
		s32 b2;
		if ((u64)hi >= 11)
			b2 = ce->cap[j];
		else
			b2 = ASR32(SHL32(ce_balloc[hi * CE_NBE + j] * N * C, lm), 2);
		s32 to = ce->trimoff[j];
		if (b1 > 0) {
			b1 += to;
			if (!(b1 > 0))
				b1 = 0;
		}
		if (b2 > 0) {
			b2 += to;
			if (!(b2 > 0))
				b2 = 0;
		}
		s32 o = ce->offs[j];
		if (lo)
			b1 += o;
		b2 += o;
		if (o > 0)
			skip_start = j;
		b2 -= b1;
		if (!(b2 > 0))
			b2 = 0;
		ce->bits1[j] = b1;
		ce->bits2[j] = b2;
	}
	/* interp_bits2pulses : bissection (6 pas, 1/64) */
	s32 afloor = (s32)(C << 3);
	s64 ilo = 0, ihi = 64;
	for (int k = 0; k < 6; k++) {
		s64 mid = (s64)((u64)(ilo + ihi) >> 1);
		s64 psum = 0;
		int done = 0;
		for (u64 j = end; j > start;) {
			j--;
			s32 tmp = ce->bits1[j] + ((s32)((u32)ce->bits2[j] * (u32)mid) >> 6);
			if (tmp >= ce->thresh[j] || done) {
				done = 1;
				psum += tmp < ce->cap[j] ? tmp : ce->cap[j];
			} else if (tmp >= afloor) {
				psum += afloor;
			}
		}
		if (psum > total)
			ihi = mid;
		else
			ilo = mid;
	}
	/* bits de chaque bande (lo) ; plafond aussi pour le plancher, comme l'asm */
	s64 psum = 0;
	int done = 0;
	for (u64 j = end; j > start;) {
		j--;
		s32 tmp = ce->bits1[j] + ((s32)((u32)ce->bits2[j] * (u32)ilo) >> 6);
		if (tmp >= ce->thresh[j] || done)
			done = 1;
		else
			tmp = tmp >= afloor ? afloor : 0;
		if (!(tmp < ce->cap[j]))
			tmp = ce->cap[j];
		ce->pulses[j] = tmp;
		psum += tmp;
	}
	/* bandes sautées, depuis la fin */
	u64 coded = end;
	for (;;) {
		s64 j = (s64)coded - 1;
		if (j <= (s64)skip_start) {
			total += skip_rsv; /* (rend le bit réservé) */
			break;
		}
		s32 left = (s32)(total - psum);
		s32 den = (s32)(EB(coded) - EB(start));
		s32 pc = ak_sdivw(left, den);
		left -= pc * den;
		s32 rem = left - (s32)(EB(j) - EB(start));
		if (!(rem > 0))
			rem = 0;
		s32 width = (s32)(EB(coded) - EB(j));
		s32 bb = ce->pulses[j] + pc * width + rem;
		s32 thr = ce->thresh[j] > afloor + 8 ? ce->thresh[j] : afloor + 8;
		if (bb >= thr) {
			if (ec_bit_logp(ec, 1))
				break;
			psum += 8;
			bb -= 8;
		}
		psum -= (s64)(u32)ce->pulses[j] + irsv;
		if (irsv)
			irsv = ce_log2frac[j - (s64)start];
		psum += irsv;
		if (bb >= afloor) {
			psum += afloor;
			ce->pulses[j] = afloor;
		} else {
			ce->pulses[j] = 0;
		}
		coded--;
	}
	ce->coded = coded;
	/* intensité, double stéréo */
	u64 intens = 0;
	if (irsv)
		intens = ec_uint(ec, (u32)(coded + 1 - start)) + start;
	ce->intens = intens;
	if (!(intens > start)) {
		total += drsv;
		drsv = 0;
	}
	u64 dual = 0;
	if (drsv)
		dual = ec_bit_logp(ec, 1);
	ce->dual = dual;
	/* le reste, réparti */
	s32 left = (s32)(total - psum);
	s32 den = (s32)(EB(coded) - EB(start));
	s32 pc = ak_sdivw(left, den);
	left -= pc * den;
	for (u64 j = start; j < coded; j++)
		ce->pulses[j] += pc * (s32)(EB(j + 1) - EB(j));
	for (u64 j = start; j < coded; j++) {
		s32 w = (s32)(EB(j + 1) - EB(j));
		s32 t = left < w ? left : w;
		ce->pulses[j] += t;
		left -= t;
	}
	/* énergie fine et impulsions de chaque bande */
	s32 balance = 0;
	u32 stereo = C - 1;
	u64 j = start;
	for (; j < coded; j++) {
		s32 N = (s32)SHL32(EB(j + 1) - EB(j), lm);
		s32 bit = ce->pulses[j] + balance;
		s32 cap = ce->cap[j];
		s32 excess, ebits, prio;
		if (N > 1) {
			excess = bit - cap;
			if (!(excess > 0))
				excess = 0;
			bit -= excess;
			s32 den2 = (s32)(C * (u32)N);
			if (C == 2 && N > 2 && !dual && (s64)j < (s64)intens)
				den2++;
			s32 nclogn = ((s32)ce_logn[j] + (s32)(lm << 3)) * den2;
			s32 off = -(21 * den2) + (nclogn >> 1);
			if (N == 2)
				off += den2 << 1;
			if (bit + off < den2 << 4)
				off += nclogn >> 2;
			else if (bit + off < (den2 * 3) << 3)
				off += nclogn >> 3;
			s32 t = bit + off + (den2 << 2);
			if (!(t > 0))
				t = 0;
			ebits = (s32)(ak_udivw((u32)t, (u32)den2) >> 3);
			if ((s32)(C * (u32)ebits) > bit >> 3)
				ebits = ASR32(bit, stereo) >> 3;
			if (!(ebits < 8))
				ebits = 8;
			prio = (s32)(((u32)ebits * (u32)den2) << 3) >= bit + off;
			bit -= (s32)((C * (u32)ebits) << 3);
		} else { /* N = 1 : un bit de signe */
			excess = bit - (s32)(C << 3);
			if (!(excess > 0))
				excess = 0;
			bit -= excess;
			ebits = 0;
			prio = 1;
		}
		if (excess > 0) { /* bits fins en plus */
			s32 ef = ASR32(excess, stereo + 3);
			if (!(ef < 8 - ebits))
				ef = 8 - ebits;
			ebits += ef;
			s32 xb = (s32)(((u32)ef * C) << 3);
			prio = xb >= excess - balance;
			excess -= xb;
		}
		balance = excess;
		ce->pulses[j] = bit;
		ce->fineq[j] = ebits;
		ce->prio[j] = prio;
	}
	ce->balance = balance;
	/* bandes sautées : tout en énergie fine */
	for (; j < end; j++) {
		s32 v = ce->pulses[j];
		ce->pulses[j] = 0;
		v = ASR32(v, stereo) >> 3;
		ce->fineq[j] = v;
		ce->prio[j] = v < 1;
	}
}

/* ---------------------------------------------------------------------------
 * Bandes (RFC 6716 4.3.4 : quant_all_bands)
 * ------------------------------------------------------------------------- */

/* x = itheta (0..16384), puis les valeurs que ce_theta rend dans w1-w7 */
struct theta {
	u32 itheta;
	s32 imid, iside, delta, qalloc, b;
	u32 fill, inv;
};

static u32 qp(struct celt *ce, struct ec_dec *ec, float *X, u32 N, s32 b, u32 B, float *lowband, s32 lm,
	      float gain, u32 fill);

/* X = gain X / |X| (renormalise_vector) */
static void renorm(float *X, u32 N, float gain)
{
	float e = 0.0f;
	u32 j = 0;
	do {
		e = ak_fmadd(X[j], X[j], e);
		j++;
	} while (j < N);
	e = e + ak_u2f(0x26901D7D); /* 1e-15 */
	e = ak_fsqrt(e);
	float g = 1.0f / e;
	g = g * gain;
	j = 0;
	do {
		X[j] = X[j] * g;
		j++;
	} while (j < N);
}

/* transformée de Haar (haar1) */
static void haar(float *X, u32 N0, u32 stride)
{
	N0 >>= 1;
	float c = ak_u2f(0x3F3504F3); /* 0,70710678 */
	for (u64 i = 0; i < stride; i++)
		for (u64 j = 0; j < N0; j++) {
			u64 a = 2 * j * stride + i, b = a + stride;
			float x0 = X[a] * c, x1 = X[b] * c;
			X[a] = x0 + x1;
			X[b] = x0 - x1;
		}
}

/* ordre du temps (deinterleave_hadamard) */
static void deint(struct celt *ce, float *X, u32 N0, u32 stride, u32 had)
{
	float *t = ce->tmp;
	for (u64 i = 0; i < stride; i++) {
		u64 d = had ? ce_ordery[(u32)(stride - 2) + i] : i;
		d *= N0;
		for (u64 j = 0; j < N0; j++)
			t[d + j] = X[j * stride + i];
	}
	for (u64 k = 0; k < (u64)N0 * stride; k++)
		X[k] = t[k];
}

/* ordre des fréquences (interleave_hadamard) */
static void inter(struct celt *ce, float *X, u32 N0, u32 stride, u32 had)
{
	float *t = ce->tmp;
	for (u64 i = 0; i < stride; i++) {
		u64 s = had ? ce_ordery[(u32)(stride - 2) + i] : i;
		s *= N0;
		for (u64 j = 0; j < N0; j++)
			t[j * stride + i] = X[s + j];
	}
	for (u64 k = 0; k < (u64)N0 * stride; k++)
		X[k] = t[k];
}

/* gauche, droite (stereo_merge) : X = milieu, Y = côté */
static void merge(float *X, float *Y, float mid, u32 N)
{
	float xp = 0.0f, side = 0.0f; /* <Y, X>, <Y, Y> */
	u32 j = 0;
	do {
		xp = ak_fmadd(Y[j], X[j], xp);
		side = ak_fmadd(Y[j], Y[j], side);
		j++;
	} while (j < N);
	xp = xp * mid;
	float m2 = mid * mid;
	m2 = m2 + side;
	float x2 = xp + xp;
	float el = m2 - x2, er = m2 + x2;
	float lim = ak_u2f(0x3A1D4952); /* 6e-4 */
	if (!(er >= lim) || !(el >= lim)) {
		j = 0;
		do {
			Y[j] = X[j];
			j++;
		} while (j < N);
		return;
	}
	float lg = 1.0f / ak_fsqrt(el), rg = 1.0f / ak_fsqrt(er);
	j = 0;
	do {
		float l = X[j] * mid, r = Y[j];
		X[j] = (l - r) * lg;
		Y[j] = (l + r) * rg;
		j++;
	} while (j < N);
}

void ce_rot1(float *x, u32 len, u32 stride, float c, float s)
{
	float ms = ak_fneg(s);
	float *p = x;
	for (s32 n = (s32)(len - stride); n > 0; n--, p++) {
		float x1 = p[0], x2 = p[stride];
		p[stride] = ak_fmadd(s, x1, c * x2);
		p[0] = ak_fmadd(ms, x2, c * x1);
	}
	s32 i = (s32)(len - 2 * stride) - 1;
	if (i < 0)
		return;
	p = x + (u32)i;
	do {
		float x1 = p[0], x2 = p[stride];
		p[stride] = ak_fmadd(s, x1, c * x2);
		p[0] = ak_fmadd(ms, x2, c * x1);
		p--;
	} while (--i >= 0);
}

/* rotation inverse d'étalement (exp_rotation, dir = -1) */
static void exprot(float *X, u32 len, u32 stride, u32 K, u32 spread)
{
	if (!spread || len <= K << 1)
		return;
	u32 f = ce_spreadf[spread - 1];
	float gain = (float)len / (float)(f * K + len);
	gain = gain * gain;
	float theta = gain * 0.5f;
	double sn, cs;
	ak_sincos((double)theta * ce_halfpi, &sn, &cs);
	float c = (float)cs; /* cos(pi/2 theta) */
	ak_sincos((double)(1.0f - theta) * ce_halfpi, &sn, &cs);
	float s = (float)cs; /* cos(pi/2 (1 - theta)) */
	/* stride2 : environ sqrt(longueur / stride) */
	u32 s2 = 0;
	if (len >= stride << 3) {
		s2 = 1;
		while ((s2 * s2 + s2) * stride + (stride >> 2) < len)
			s2++;
	}
	len = ak_udivw(len, stride);
	for (u64 i = 0; i < stride; i++) {
		float *x = X + i * len;
		if (s2)
			ce_rot1(x, len, s2, s, c);
		ce_rot1(x, len, 1, c, s);
	}
}

/* bande d'un coefficient : un bit de signe par canal -> 1 */
static u32 qn1(struct celt *ce, struct ec_dec *ec, float *X, float *Y, float *out)
{
	float *p = X;
	for (;;) {
		u32 s = 0;
		if (ce->rem >= 8) {
			ce->rem -= 8;
			s = ec_bits(ec, 1);
		}
		*p = s == 0 ? 1.0f : -1.0f;
		if (p == Y)
			break;
		p = Y;
		if (!Y)
			break;
	}
	if (out)
		out[0] = X[0];
	return 1;
}

/* indice CWRS, impulsions, normalisation, rotation (alg_unquant) -> masque */
static u32 algq(struct celt *ce, struct ec_dec *ec, float *X, u32 N, u32 K, u32 spread, u32 B, float gain)
{
	/* table U(n, k), n <= N, k <= K + 1 : U(0, 0) = 1, U(0, k) = 0,
	 * U(n, 0) = 0, U(n, k) = U(n-1, k) + U(n, k-1) + U(n-1, k-1) */
	u32 *U = ce->ut;
	u64 st = (u32)(K + 2); /* pas des lignes */
	U[0] = 1;
	for (u64 k = 1; k < st; k++)
		U[k] = 0;
	for (u64 n = 1; n <= N; n++) {
		u32 *row = U + n * st, *pr = row - st;
		row[0] = 0;
		for (u64 k = 1; k < st; k++)
			row[k] = pr[k] + row[k - 1] + pr[k - 1];
	}
	/* indice : uniforme dans V(N, K) = U(N, K) + U(N, K + 1) */
	u32 *rn = U + (u64)N * st;
	u32 i = ec_uint(ec, rn[K] + rn[(u64)K + 1]);
	/* CWRS : impulsions de chaque dimension (n > 2, puis 2 et 1) */
	s32 *iy = ce->iy;
	u32 k = K, n = N, ryy = 0;
	u64 o = 0;
	while (n > 2) {
		u32 *row = U + (u64)n * st;
		u32 p = row[k], q = row[(u64)k + 1];
		s32 v;
		if (i >= p && i < q) { /* zéro dans cette dimension */
			i -= p;
			v = 0;
		} else {
			u32 neg = i >= q;
			if (neg)
				i -= q;
			u32 k0 = k;
			do {
				k--;
				p = row[k];
			} while (p > i);
			i -= p;
			v = (s32)(k0 - k);
			if (neg)
				v = -v;
		}
		iy[o++] = v;
		ryy += (u32)(v * v);
		n--;
	}
	u32 p = k + k + 1; /* n = 2 : p = 2k + 1 */
	u32 neg = i >= p;
	if (neg)
		i -= p;
	u32 k0 = k;
	k = (i + 1) >> 1;
	if (k)
		i -= (k << 1) - 1;
	s32 v = (s32)(k0 - k);
	if (neg)
		v = -v;
	iy[o++] = v;
	ryy += (u32)(v * v);
	v = (s32)k; /* n = 1 : signe de i */
	if (i)
		v = -v;
	iy[o] = v;
	ryy += (u32)(v * v);
	/* X = gain / sqrt(Ryy) iy */
	float g = ak_fsqrt((float)ryy);
	g = 1.0f / g;
	g = g * gain;
	u64 j = 0;
	do {
		X[j] = (float)iy[j] * g;
		j++;
	} while (j < N);
	exprot(X, N, B, K, spread);
	/* masque : blocs non nuls */
	if (B <= 1)
		return 1;
	u32 N0 = ak_udivw(N, B), cm = 0;
	u64 idx = 0;
	for (u64 blk = 0; blk < B; blk++) {
		s32 t = 0;
		u64 jj = 0;
		do {
			t |= iy[idx++];
			jj++;
		} while (jj < N0);
		if (t)
			cm |= SHL32(1, blk);
	}
	return cm;
}

/* compute_theta : N, b, B, B0, LM, stéréo, fill -> t */
static void theta(struct celt *ce, struct ec_dec *ec, u32 N, s32 b, u32 B, u32 B0, s32 lm, u32 stereo, u32 fill,
		  struct theta *t)
{
	u64 i = ce->i;
	s32 pcap = (s32)ce_logn[i] + (s32)SHL32(lm, 3); /* pulse_cap */
	s32 off = stereo && N == 2 ? 16 : 4;
	off = -off + (pcap >> 1);
	/* qn (compute_qn) */
	s32 n2 = (s32)(N << 1) - 1;
	if (stereo && N == 2)
		n2--;
	s32 qb = ak_sdivw(n2 * off + b, n2);
	s32 m = b - pcap - 32;
	if (!(qb < m))
		qb = m;
	if (!(qb < 64))
		qb = 64;
	u32 qn = 1;
	if (qb >= 4) {
		u32 v = SHR32(ce_exp2t8[qb & 7], 14 - (qb >> 3));
		qn = ((v + 1) >> 1) << 1;
	}
	if (stereo && i >= ce->intens)
		qn = 1;
	s64 tell = ec_tell_frac(ec);
	u32 itheta = 0, inv = 0;
	if (qn != 1) {
		if (stereo && N > 2) { /* stéréo : densité en marche (p0 = 3 jusqu'à qn/2) */
			u32 x0 = qn >> 1, x1 = x0 + 1, p0x = x1 * 3, ft = p0x + x0;
			u32 fs = ec_decode(ec, ft);
			if (fs < p0x)
				itheta = fs / 3;
			else
				itheta = fs - p0x + x1;
			u32 fl, fh;
			if (itheta <= x0) {
				fl = itheta * 3;
				fh = fl + 3;
			} else {
				fl = itheta - 1 - x0 + p0x;
				fh = itheta - x0 + p0x;
			}
			ec_update(ec, fl, fh, ft);
		} else if (B0 > 1 || stereo) {
			itheta = ec_uint(ec, qn + 1);
		} else { /* densité triangulaire */
			u32 x0 = qn >> 1, x1 = x0 + 1, ft = x1 * x1;
			u32 fm = ec_decode(ec, ft);
			u32 fl, fs;
			if (fm < (x0 * x1) >> 1) {
				itheta = (isqrt((fm << 3) + 1) - 1) >> 1;
				fs = itheta + 1;
				fl = (itheta * fs) >> 1;
			} else {
				itheta = (((qn + 1) << 1) - isqrt(((ft - fm - 1) << 3) + 1)) >> 1;
				fs = qn + 1 - itheta;
				fl = ft - ((fs * (fs + 1)) >> 1);
			}
			ec_update(ec, fl, fl + fs, ft);
		}
		itheta = ak_udivw(itheta << 14, qn);
	} else if (stereo && b > 16 && ce->rem > 16) { /* qn = 1, stéréo : inversion */
		inv = ec_bit_logp(ec, 2);
		if (ce->cc == 1) /* sortie mono : pas d'inversion de phase (RFC 8251 4.7, comme libopus) */
			inv = 0;
	}
	s32 qalloc = (s32)((u32)ec_tell_frac(ec) - (u32)tell);
	b -= qalloc;
	s32 imid, iside, delta;
	if (!itheta) {
		imid = 32767;
		iside = 0;
		fill &= SHL32(1, B) - 1;
		delta = -16384;
	} else if (itheta == 16384) {
		imid = 0;
		iside = 32767;
		fill &= SHL32(SHL32(1, B) - 1, B);
		delta = 16384;
	} else {
		imid = ce_bexcos((s32)itheta);
		iside = ce_bexcos(16384 - (s32)itheta);
		s32 l = (s16)ce_log2tan(iside, imid);
		s32 nn = (s16)SHL32(N - 1, 7);
		delta = (l * nn + 16384) >> 15;
	}
	t->itheta = itheta;
	t->imid = imid;
	t->iside = iside;
	t->delta = delta;
	t->qalloc = qalloc;
	t->b = b;
	t->fill = fill;
	t->inv = inv;
}

static u32 bitil(u32 k)
{
	/* (fill <= 255 : l'asm lirait ce_bitdi au-delà de 16) */
	return k < 16 ? ce_bitil[k] : k < 32 ? ce_bitdi[k - 16] : 0;
}

/* quant_partition : division récursive en deux moitiés, sinon PVQ ou
 * repliement -> masque */
static u32 qp(struct celt *ce, struct ec_dec *ec, float *X, u32 N, s32 b, u32 B, float *lowband, s32 lm,
	      float gain, u32 fill)
{
	u32 band = (u32)ce->i;
	const u8 *cache = ce_cache(band, lm);
	s32 thr = (s32)cache[cache[0]] + 12;
	if (lm != -1 && b > thr && (s32)N > 2) { /* division */
		u32 B0 = B;
		N >>= 1;
		lm--;
		if (B == 1)
			fill = (fill & 1) | (fill << 1);
		B = (B + 1) >> 1;
		struct theta t;
		theta(ce, ec, N, b, B, B0, lm, 0, fill, &t);
		b = t.b;
		fill = t.fill;
		u32 itheta = t.itheta;
		float sc = ak_u2f(0x38000000); /* gain x mid, gain x side */
		float gmid = (float)t.imid * sc;
		gmid = gain * gmid;
		float gside = (float)t.iside * sc;
		gside = gain * gside;
		s32 delta = t.delta;
		/* pentes de masquage (blocs courts) */
		if (B0 > 1 && (itheta & 0x3FFF)) {
			if ((s32)itheta > 8192) {
				delta -= ASR32(delta, 4 - lm);
			} else {
				delta += ASR32(N << 3, 5 - lm);
				if (!(delta < 0))
					delta = 0;
			}
		}
		s32 mbits = ak_sdivw(b - delta, 2); /* max(0, min(b, (b - delta) / 2)) */
		if (!(mbits < b))
			mbits = b;
		if (!(mbits > 0))
			mbits = 0;
		s32 sbits = b - mbits;
		ce->rem -= t.qalloc;
		s64 rebalance = ce->rem;
		float *lb2 = lowband ? lowband + N : 0; /* bande basse de la seconde moitié */
		if (mbits >= sbits) {
			u32 cm = qp(ce, ec, X, N, mbits, B, lowband, lm, gmid, fill);
			s64 r = (s64)mbits - (rebalance - ce->rem);
			if (r > 24 && itheta)
				sbits += (s32)(r - 24);
			u32 c2 = qp(ce, ec, X + N, N, sbits, B, lb2, lm, gside, SHR32(fill, B));
			return SHL32(c2, B0 >> 1) | cm;
		}
		u32 cm = SHL32(qp(ce, ec, X + N, N, sbits, B, lb2, lm, gside, SHR32(fill, B)), B0 >> 1);
		s64 r = (s64)sbits - (rebalance - ce->rem);
		if (r > 24 && itheta != 16384)
			mbits += (s32)(r - 24);
		return qp(ce, ec, X, N, mbits, B, lowband, lm, gmid, fill) | cm;
	}
	/* impulsions (bits2pulses, sans dépasser le budget) */
	u32 q = ce_b2p(band, lm, b);
	u32 curr = ce_p2b(band, lm, q);
	ce->rem -= curr;
	while (ce->rem < 0 && q) {
		ce->rem += curr;
		q--;
		curr = ce_p2b(band, lm, q);
		ce->rem -= curr;
	}
	if (q)
		return algq(ce, ec, X, N, ce_getp(q), (u32)ce->spread, B, gain);
	u32 mask = SHL32(1, B) - 1;
	fill &= mask;
	if (!fill) {
		memset(X, 0, (u64)N << 2);
		return 0;
	}
	u32 seed = (u32)ce->seed;
	u64 j = 0;
	if (!lowband) { /* bruit */
		do {
			seed = seed * 1664525u + 1013904223u;
			X[j] = (float)((s32)seed >> 20);
			j++;
		} while (j < N);
		fill = mask;
	} else {
		float a = ak_u2f(0x3B800000), na = ak_fneg(a); /* 1/256 */
		do {
			seed = seed * 1664525u + 1013904223u;
			X[j] = lowband[j] + (seed & 0x8000 ? a : na);
			j++;
		} while (j < N);
	}
	ce->seed = (ce->seed & 0xFFFFFFFF00000000ull) | seed; /* (str w : 32 bits bas) */
	renorm(X, N, gain);
	return fill;
}

/* quant_band (mono) -> masque d'effondrement */
static u32 qb(struct celt *ce, struct ec_dec *ec, float *X, u32 N0, s32 b, u32 B, float *lowband, s32 lm,
	      float *out, float gain, float *scratch, u32 fill)
{
	if (N0 == 1)
		return qn1(ce, ec, X, 0, out);
	u32 longb = B == 1;
	u32 NB = ak_udivw(N0, B);
	s64 tfc = ce->tfc;
	u64 recombine = tfc > 0 ? (u64)tfc : 0;
	u64 tdiv = 0;
	/* copie de la bande basse si elle doit être transformée */
	if (scratch && lowband && (recombine || B > 1 || (!(NB & 1) && tfc < 0))) {
		memmove(scratch, lowband, (u64)N0 << 2);
		lowband = scratch;
	}
	/* recombinaison (meilleure résolution en fréquence) */
	for (u64 k = 0; k < recombine; k++) {
		if (lowband)
			haar(lowband, SHR32(N0, k), SHL32(1, k));
		fill = bitil(fill & 15) | bitil(fill >> 4) << 2;
	}
	B = SHR32(B, recombine);
	NB = SHL32(NB, recombine);
	/* division en temps */
	while (!(NB & 1) && tfc < 0) {
		if (lowband)
			haar(lowband, NB, B);
		fill |= SHL32(fill, B);
		B <<= 1;
		NB >>= 1;
		tdiv++;
		tfc++;
	}
	u32 NB0 = NB, B0 = B;
	if (B0 > 1 && lowband)
		deint(ce, lowband, SHR32(NB, recombine), SHL32(B0, recombine), longb);
	u32 cm = qp(ce, ec, X, N0, b, B, lowband, lm, gain, fill);
	if (B0 > 1)
		inter(ce, X, SHR32(NB0, recombine), SHL32(B0, recombine), longb);
	NB = NB0;
	B = B0;
	for (; tdiv; tdiv--) {
		B >>= 1;
		NB <<= 1;
		cm |= SHR32(cm, B);
		haar(X, NB, B);
	}
	for (u64 k = 0; k < recombine; k++) {
		cm = ce_bitdi[cm & 15];
		haar(X, SHR32(N0, k), SHL32(1, k));
	}
	B = SHL32(B, recombine);
	if (out) { /* sortie = sqrt(N0) X */
		float n = ak_fsqrt((float)N0);
		u64 j = 0;
		do {
			out[j] = X[j] * n;
			j++;
		} while (j < N0);
	}
	return cm & (SHL32(1, B) - 1);
}

/* quant_band_stereo -> masque */
static u32 qbs(struct celt *ce, struct ec_dec *ec, float *X, float *Y, u32 N, s32 b, u32 B, float *lowband, s32 lm,
	       float *out, float *scratch, u32 fill)
{
	if (N == 1)
		return qn1(ce, ec, X, Y, out);
	u32 ofill = fill;
	struct theta t;
	theta(ce, ec, N, b, B, B, lm, 1, fill, &t);
	float sc = ak_u2f(0x38000000); /* 1/32768 */
	float mid = (float)t.imid * sc;
	float side = (float)t.iside * sc;
	u32 cm;
	if (N == 2) { /* N = 2 : un seul bit pour le côté */
		s32 sb = 0;
		if (t.itheta && t.itheta != 16384)
			sb = 8;
		ce->rem -= t.qalloc + sb;
		int c = (s32)t.itheta > 8192;
		float *x2 = c ? Y : X, *y2 = c ? X : Y;
		u32 sign = 0;
		if (sb)
			sign = ec_bits(ec, 1);
		cm = qb(ce, ec, x2, 2, t.b - sb, B, lowband, lm, out, 1.0f, scratch, ofill);
		float s = sign == 0 ? 1.0f : -1.0f;
		float a0 = x2[0], a1 = x2[1]; /* y2 = (-sign x2[1], sign x2[0]) */
		float v = a1 * s;
		y2[0] = ak_fneg(v);
		y2[1] = a0 * s;
		float l0 = X[0] * mid, l1 = X[1] * mid;
		float r0 = Y[0] * side, r1 = Y[1] * side;
		X[0] = l0 - r0;
		Y[0] = l0 + r0;
		X[1] = l1 - r1;
		Y[1] = l1 + r1;
	} else {
		s32 bb = t.b; /* mbits = max(0, min(b, (b - delta) / 2)) */
		s32 mbits = ak_sdivw(bb - t.delta, 2);
		if (!(mbits < bb))
			mbits = bb;
		if (!(mbits > 0))
			mbits = 0;
		s32 sbits = bb - mbits;
		ce->rem -= t.qalloc;
		s64 rebalance = ce->rem;
		if (mbits >= sbits) {
			cm = qb(ce, ec, X, N, mbits, B, lowband, lm, out, 1.0f, scratch, t.fill);
			s64 r = (s64)mbits - (rebalance - ce->rem);
			if (r > 24 && t.itheta)
				sbits += (s32)(r - 24);
			cm |= qb(ce, ec, Y, N, sbits, B, 0, lm, 0, side, 0, SHR32(t.fill, B));
		} else {
			cm = qb(ce, ec, Y, N, sbits, B, 0, lm, 0, side, 0, SHR32(t.fill, B));
			s64 r = (s64)sbits - (rebalance - ce->rem);
			if (r > 24 && t.itheta != 16384)
				mbits += (s32)(r - 24);
			cm |= qb(ce, ec, X, N, mbits, B, lowband, lm, out, 1.0f, scratch, t.fill);
		}
		merge(X, Y, mid, N);
	}
	if (t.inv) {
		u64 j = 0;
		do {
			Y[j] = ak_fneg(Y[j]);
			j++;
		} while (j < N);
	}
	return cm;
}

/* spectres normalisés de toutes les bandes -> x (canal 0 ; canal 1 en x + N),
 * cmask ; seed mise à jour */
static void bands(struct celt *ce, struct ec_dec *ec)
{
	u64 start = ce->start, end = ce->end, C = ce->c;
	u64 M = 1ull << (ce->lm & 63);
	u64 norm_off = EB(start) * M;
	u64 lowband_off = 0;
	int update_lowband = 1;
	s64 balance = ce->balance;
	u64 dual = ce->dual;
	float *norm = ce->norm;
	for (u64 i = start; i < end; i++) {
		ce->i = i;
		int last = i == end - 1;
		u64 bs = EB(i) * M;
		u64 N = EB(i + 1) * M - bs;
		float *X = ce->x + bs;
		float *Y = 0;
		if (ce->c == 2)
			Y = ce->x + ce->n + bs;
		s64 tell = ec_tell_frac(ec);
		if (i != start)
			balance -= tell;
		s64 remaining = (s64)(ce->len << 6) - (s64)ce->antirsv - tell - 1; /* total - tell - 1 */
		ce->rem = remaining;
		s64 b = 0;
		if (i < ce->coded) {
			s64 d = (s64)(ce->coded - i);
			if (!(d < 3))
				d = 3;
			s64 cb = ak_sdiv(balance, d); /* curr_balance */
			cb += ce->pulses[i];
			if (!(cb < remaining + 1))
				cb = remaining + 1;
			if (!(cb < 16383))
				cb = 16383;
			b = cb > 0 ? cb : 0;
		}
		/* position du repliement */
		u64 ebs = EB(start) * M;
		if ((s64)(bs - N) >= (s64)ebs || i == start + 1)
			if (update_lowband || !lowband_off)
				lowband_off = i;
		if (i == start + 1) { /* repliement spécial (hybride) */
			s64 n1 = (s64)((EB(start + 1) - EB(start)) * M);
			s64 n2 = (s64)((EB(start + 2) - EB(start + 1)) * M);
			if (n2 - n1 > 0) {
				memmove(norm + n1, norm + (2 * n1 - n2), (u64)(n2 - n1) << 2);
				if (dual)
					memmove(norm + 1024 + n1, norm + 1024 + (2 * n1 - n2), (u64)(n2 - n1) << 2);
			}
		}
		s64 tfc = ce->tfres[i];
		ce->tfc = tfc;
		/* zone de travail (la dernière bande de X), aucune pour la dernière */
		float *scratch = last ? 0 : ce->x + EB(CE_NBE - 1) * M;
		/* masques des bandes repliées */
		u64 B = ce->b;
		u64 xcm = AK_SHL(1, B) - 1, ycm = xcm;
		s64 eff = -1; /* effective_lowband */
		if (lowband_off && (ce->spread != 3 || B > 1 || tfc < 0)) {
			s64 e = (s64)(EB(lowband_off) * M) - (s64)norm_off - (s64)N;
			eff = e > 0 ? e : 0;
			s64 lo = eff + (s64)norm_off;
			u64 fs = lowband_off; /* fold_start */
			do
				fs--;
			while ((s64)(EB(fs) * M) > lo);
			u64 fe = lowband_off - 1; /* fold_end */
			s64 hi = lo + (s64)N;
			for (;;) {
				fe++;
				if ((s64)fe >= (s64)i)
					break;
				if (!((s64)(EB(fe) * M) < hi))
					break;
			}
			xcm = 0;
			ycm = 0;
			do {
				xcm |= ce->cmask[fs * C];
				ycm |= ce->cmask[fs * C + C - 1];
				fs++;
			} while ((s64)fs < (s64)fe);
		}
		/* double stéréo arrêtée à la bande d'intensité */
		if (dual && i == ce->intens) {
			dual = 0;
			for (u64 j = 0; j < bs - norm_off; j++) {
				float v = norm[j] + norm[1024 + j];
				norm[j] = v * 0.5f;
			}
		}
		/* bande basse (repliement) et sortie de la bande pour la suite */
		float *lowband = eff != -1 ? norm + eff : 0;
		float *lbout = last ? 0 : norm + (bs - norm_off);
		if (dual) { /* double stéréo : deux bandes mono */
			xcm = qb(ce, ec, X, (u32)N, (s32)((u64)b >> 1), (u32)ce->b, lowband, (s32)ce->lm, lbout, 1.0f,
				 scratch, (u32)xcm);
			ycm = qb(ce, ec, Y, (u32)N, (s32)((u64)b >> 1), (u32)ce->b, lowband ? lowband + 1024 : 0,
				 (s32)ce->lm, lbout ? lbout + 1024 : 0, 1.0f, scratch, (u32)ycm);
		} else if (Y) {
			xcm = ycm = qbs(ce, ec, X, Y, (u32)N, (s32)b, (u32)ce->b, lowband, (s32)ce->lm, lbout, scratch,
					(u32)(xcm | ycm));
		} else {
			xcm = ycm = qb(ce, ec, X, (u32)N, (s32)b, (u32)ce->b, lowband, (s32)ce->lm, lbout, 1.0f, scratch,
				       (u32)(xcm | ycm));
		}
		ce->cmask[i * C] = (u8)xcm;
		ce->cmask[i * C + C - 1] = (u8)ycm;
		balance += ce->pulses[i]; /* balance += pulses[i] + tell */
		balance += tell;
		update_lowband = b > (s64)(N << 3); /* update_lowband = b > N << 3 */
	}
}

/* ---------------------------------------------------------------------------
 * Anti-effondrement, synthèse, post-filtre, désaccentuation
 * ------------------------------------------------------------------------- */

/* anti-effondrement (RFC 6716 4.3.5) : bruit dans les blocs courts vides (la
 * graine n'est pas rangée, comme l'asm) */
static void anti(struct celt *ce)
{
	u64 C = ce->c;
	u32 lm = (u32)ce->lm;
	u32 seed = (u32)ce->seed;
	for (u64 i = ce->start; i < ce->end; i++) {
		u32 N0 = EB(i + 1) - EB(i);
		u32 off = SHL32(EB(i), lm);
		u32 depth = SHR32(ak_udivw((u32)ce->pulses[i] + 1, N0), lm); /* (1 + pulses) / N0 >> LM */
		float th = (float)depth * ak_u2f(0xBE000000);		     /* 0,5 2^(-depth / 8) */
		th = ce_exp2(th);
		th = th * 0.5f;
		float sq = ak_fsqrt((float)SHL32(N0, lm)); /* 1 / sqrt(N0 << LM) */
		sq = 1.0f / sq;
		for (u64 c = 0; c < C; c++) {
			u64 k = c * CE_NBE + i;
			float p1 = ce->oldloge[k], p2 = ce->oldloge2[k];
			if (C == 1) {
				p1 = ak_fmax(p1, ce->oldloge[i + CE_NBE]);
				p2 = ak_fmax(p2, ce->oldloge2[i + CE_NBE]);
			}
			p1 = ak_fmin(p1, p2);
			float ed = ce->olde[k] - p1; /* Ediff */
			ed = ak_fmax(ed, 0.0f);
			float r = ce_exp2(ak_fneg(ed));
			r = r + r; /* r = 2 2^(-Ediff) */
			if (lm == 3)
				r = r * ak_u2f(0x3FB504F3); /* sqrt(2) */
			r = ak_fmin(th, r);
			r = r * sq;
			float nr = ak_fneg(r);
			float *X = ce->x + ce->n * c + off;
			int ren = 0;
			u32 mask = ce->cmask[i * C + c];
			for (u64 kk = 0; kk < (1ull << lm); kk++) {
				if (SHR32(mask, kk) & 1)
					continue;
				u64 j = 0;
				do {
					seed = seed * 1664525u + 1013904223u;
					X[(j << lm) + kk] = seed & 0x8000 ? r : nr;
					j++;
				} while (j < N0);
				ren = 1;
			}
			if (ren)
				renorm(X, SHL32(N0, lm), 1.0f);
		}
	}
}

/* X du canal, sortie (N), énergies du canal : dénormalise (gain 2^(oldE +
 * eMeans) de chaque bande ; zéro hors des bandes) */
static void denorm(struct celt *ce, const float *X, float *out, const float *E)
{
	memset(out, 0, ce->n << 2);
	if (ce->silence)
		return;
	u32 lm = (u32)ce->lm;
	for (u64 i = ce->start; i < ce->end; i++) {
		float g = E[i] + ce_emeans[i];
		g = ak_fmin(g, 32.0f); /* min(32, lg) */
		g = ce_exp2(g);
		u64 j = SHL32(EB(i), lm), e = SHL32(EB(i + 1), lm);
		do {
			out[j] = X[j] * g;
			j++;
		} while (j < e);
	}
}

/* canal : IMDCT des blocs de freq vers l'historique du canal */
static void imdcts(struct celt *ce, u64 ch)
{
	u64 N = ce->n;
	float *os = ce->mem[ch] + (2048 - N); /* out_syn */
	u64 B = ce->b;
	u64 NB = ak_udiv(N, B);
	u64 k = 0, t = 120; /* plan : NB = 120 << k */
	while (t < NB) {
		k++;
		t <<= 1;
	}
	const struct akmdct *md = ce->md + k;
	float *in = ce->tmp, *y = ce->tmp + 960; /* entrée (NB), sortie (2 NB) */
	for (u64 b = 0; b < B; b++) {
		const float *f = ce->freq + b; /* coefficients du bloc : freq[b + j B] */
		u64 j = 0;
		do {
			in[j] = *f;
			f += B;
			j++;
		} while (j < NB);
		ak_imdct(md, in, y);
		float *o = os + b * NB; /* out[60 + j] = y[NB/2 + j], j < NB */
		memmove(o + 60, y + (NB >> 1), NB << 2);
		for (u64 i = 0, i2 = 119; i < 60; i++, i2--) { /* TDAC : out[i], out[119 - i] */
			float x1 = o[i2], x2 = o[i];		     /* nouveau, précédent */
			float wi = ce_window[i], wj = ce_window[i2];
			o[i] = ak_fmsub(wi, x1, wj * x2);
			o[i2] = ak_fmadd(wj, x1, wi * x2);
		}
	}
}

/* synthèse (celt_synthesis) : spectres -> IMDCT de chaque bloc -> historique */
static void synth(struct celt *ce)
{
	u64 N = ce->n;
	float *freq = ce->freq;
	if (ce->cc == ce->c) {
		for (u64 c = 0; c < ce->cc; c++) {
			denorm(ce, ce->x + N * c, freq, ce->olde + c * CE_NBE);
			imdcts(ce, c);
		}
	} else if (ce->cc == 2) { /* flux mono, sortie stéréo : le même spectre */
		denorm(ce, ce->x, freq, ce->olde);
		imdcts(ce, 0);
		imdcts(ce, 1);
	} else { /* flux stéréo, sortie mono : moyenne des deux canaux */
		denorm(ce, ce->x, freq, ce->olde);
		denorm(ce, ce->x + N, freq + N, ce->olde + CE_NBE);
		u64 j = 0;
		do {
			float a = freq[j] * 0.5f, b = freq[j + N] * 0.5f;
			freq[j] = a + b;
			j++;
		} while (j < N);
		imdcts(ce, 0);
	}
}

/* signal (en place), T0, T1, N, g0, g1, prises 0, prises 1 : filtre en peigne,
 * passage de (T0, g0) à (T1, g1) sur les 120 premiers échantillons (fenêtre
 * au carré) */
static void comb(float *x, s64 T0, s64 T1, u64 N, float g0, float g1, u64 tap0, u64 tap1)
{
	if (g0 == 0 && g1 == 0)
		return;
	if (!(T0 > 15))
		T0 = 15;
	if (!(T1 > 15))
		T1 = 15;
	const float *a = ce_combg + tap0 * 3, *c = ce_combg + tap1 * 3;
	float g00 = a[0] * g0, g01 = a[1] * g0, g02 = a[2] * g0;
	float g10 = c[0] * g1, g11 = c[1] * g1, g12 = c[2] * g1;
	u64 ov = 120; /* recouvrement (0 si rien ne change) */
	if (g0 == g1 && T0 == T1 && tap0 == tap1)
		ov = 0;
	u64 i = 0;
	for (; i < ov; i++) {
		float f = ce_window[i] * ce_window[i]; /* f = w^2 */
		float mf = 1.0f - f;
		const float *p = x + ((s64)i - T0);
		float s = mf * g00;
		s = s * p[0]; /* (1 - f) g00 x[i - T0] */
		s = ak_fmadd(mf * g01, p[1] + p[-1], s);
		s = ak_fmadd(mf * g02, p[2] + p[-2], s);
		const float *q = x + ((s64)i - T1);
		s = ak_fmadd(f * g10, q[0], s);
		s = ak_fmadd(f * g11, q[1] + q[-1], s);
		s = ak_fmadd(f * g12, q[2] + q[-2], s);
		x[i] = x[i] + s;
	}
	if (g1 == 0) /* g1 = 0 : rien de plus */
		return;
	for (; i < N; i++) { /* filtre constant (T1, g1) */
		const float *q = x + ((s64)i - T1);
		float s = g10 * q[0];
		s = ak_fmadd(g11, q[1] + q[-1], s);
		s = ak_fmadd(g12, q[2] + q[-2], s);
		x[i] = x[i] + s;
	}
}

/* post-filtre de chaque canal ; puis ses paramètres passent à la trame
 * suivante */
static void postfilter(struct celt *ce)
{
	u64 N = ce->n, lm = ce->lm;
	for (u64 c = 0; c < ce->cc; c++) {
		if (!(ce->pfper > 15))
			ce->pfper = 15;
		if (!(ce->pfperold > 15))
			ce->pfperold = 15;
		float *os = ce->mem[c] + (2048 - N);
		comb(os, ce->pfperold, ce->pfper, 120, ce->pfgainold, ce->pfgain, ce->pftapold, ce->pftap);
		if (lm)
			comb(os + 120, ce->pfper, (s64)ce->pfpitch, N - 120, ce->pfgain, ce->pfg, ce->pftap, ce->pftapn);
	}
	ce->pfperold = ce->pfper; /* old = courant ; courant = trame */
	ce->pfgainold = ce->pfgain;
	ce->pftapold = ce->pftap;
	ce->pfper = (s64)ce->pfpitch;
	ce->pfgain = ce->pfg;
	ce->pftap = ce->pftapn;
	if (lm) {
		ce->pfperold = ce->pfper;
		ce->pfgainold = ce->pfgain;
		ce->pftapold = ce->pftap;
	}
}

/* énergies passées (anti-effondrement), bandes hors de [start, end) */
static void history(struct celt *ce, struct ec_dec *ec)
{
	if (ce->c == 1) /* mono : copie dans le canal 1 */
		for (int i = 0; i < CE_NBE; i++)
			ce->olde[i + CE_NBE] = ce->olde[i];
	if (!ce->trans) {
		for (int k = 0; k < 2 * CE_NBE; k++) {
			ce->oldloge2[k] = ce->oldloge[k];
			ce->oldloge[k] = ce->olde[k];
		}
	} else {
		for (int k = 0; k < 2 * CE_NBE; k++)
			ce->oldloge[k] = ak_fmin(ce->oldloge[k], ce->olde[k]);
	}
	for (u64 i = 0; i < CE_NBE; i++)
		if (i < ce->start || i >= ce->end) {
			ce->olde[i] = 0;
			ce->oldloge[i] = ak_u2f(MINUS28);
			ce->oldloge2[i] = ak_u2f(MINUS28);
			ce->olde[i + CE_NBE] = 0;
			ce->oldloge[i + CE_NBE] = ak_u2f(MINUS28);
			ce->oldloge2[i + CE_NBE] = ak_u2f(MINUS28);
		}
	ce->seed = (u32)ec->rng; /* graine de la trame suivante */
}

/* désaccentuation (1 / (1 - 0,85 z^-1)) -> out (flottants -1..1) */
static void deemph(struct celt *ce)
{
	u64 N = ce->n;
	float coef = ak_u2f(0x3F599A00); /* 0,85000610 */
	float tiny = ak_u2f(0x0DA24260); /* 1e-30 */
	float sc = ak_u2f(0x38000000);	 /* 1 / 32768 */
	for (u64 c = 0; c < ce->cc; c++) {
		const float *os = ce->mem[c] + (2048 - N);
		float *out = ce->out[c];
		float m = ce->preemph[c];
		u64 j = 0;
		do {
			float t = os[j] + tiny;
			t = t + m;
			m = t * coef;
			float v = t * sc;
			if (ce->accum)
				v = out[j] + v;
			out[j] = v;
			j++;
		} while (j < N);
		ce->preemph[c] = m;
	}
}

/* ---------------------------------------------------------------------------
 * Trame
 * ------------------------------------------------------------------------- */

long celt_decode(struct celt *ce, u64 len, u64 n, u64 c, struct ec_dec *ec)
{
	ce->len = len;
	ce->n = n;
	ce->c = c;
	u64 lm = 0, t = 120;
	while (t != n) {
		lm++;
		t <<= 1;
		if (lm > 3)
			return AKE_CORRUPT;
	}
	ce->lm = lm;
	u64 start = ce->start, end = ce->end, C = ce->c;
	(void)end;
	if (C == 1) /* mono : oldE = max(gauche, droite) */
		for (int i = 0; i < CE_NBE; i++)
			ce->olde[i] = ak_fmax(ce->olde[i], ce->olde[i + CE_NBE]);
	s64 total = (s64)(len << 3); /* total_bits */
	s64 tell = ec_tell(ec);
	/* silence */
	u64 silence = 1;
	if (tell < total) {
		silence = 0;
		if (tell == 1)
			silence = ec_bit_logp(ec, 15);
	}
	ce->silence = silence;
	if (silence) { /* tous les bits sont comme lus */
		ec->nbits += (u64)(total - ec_tell(ec));
		tell = total;
	}
	/* post-filtre */
	ce->pfpitch = 0;
	ce->pfg = 0;
	ce->pfg_pad = 0;
	ce->pftapn = 0;
	if (!start && !(tell + 16 > total)) {
		if (ec_bit_logp(ec, 1)) {
			u32 oct = ec_uint(ec, 6);
			u32 v = ec_bits(ec, oct + 4);
			ce->pfpitch = (u32)(v + SHL32(16, oct) - 1);
			u32 qg = ec_bits(ec, 3);
			if (!(ec_tell(ec) + 2 > total))
				ce->pftapn = ec_icdf(ec, ce_tapset_icdf, 2);
			float g = (float)(s32)(qg + 1); /* gain = 0,09375 (qg + 1) */
			ce->pfg = g * ak_u2f(0x3DC00000);
		}
		tell = ec_tell(ec);
	}
	/* transitoire */
	u64 trans = 0;
	if (lm && !(tell + 3 > total)) {
		trans = ec_bit_logp(ec, 3);
		tell = ec_tell(ec);
	}
	ce->trans = trans;
	ce->b = trans ? 1ull << lm : 1;
	/* énergies : intra, grossières */
	u64 intra = 0;
	if (!(tell + 3 > total))
		intra = ec_bit_logp(ec, 3);
	ce->intra = intra;
	coarse(ce, ec);
	tf_decode(ce, ec);
	/* étalement */
	u64 spread = 2;
	if (!(ec_tell(ec) + 4 > total))
		spread = ec_icdf(ec, ce_spread_icdf, 5);
	ce->spread = spread;
	/* plafonds : (caps + 64) C N / 4 */
	const u8 *caps = ce_ccaps + (lm * 2 + C - 1) * CE_NBE;
	for (int i = 0; i < CE_NBE; i++) {
		u32 N = SHL32(EB(i + 1) - EB(i), lm);
		ce->cap[i] = (s32)(((caps[i] + 64u) * (u32)C * N) >> 2);
	}
	/* dynalloc */
	total <<= 3; /* total_bits (1/8) */
	tell = ec_tell_frac(ec);
	s64 logp = 6; /* dynalloc_logp */
	for (u64 i = start; i < ce->end; i++) {
		u32 width = SHL32((EB(i + 1) - EB(i)) * (u32)C, lm);
		s32 w8 = (s32)(width << 3), m = (s32)width > 48 ? (s32)width : 48;
		u32 quanta = (u32)(w8 < m ? w8 : m);
		s64 lp = logp, boost = 0, cap = (u32)ce->cap[i];
		while (tell + (lp << 3) < total && boost < cap) {
			u32 f = ec_bit_logp(ec, (u32)lp);
			tell = ec_tell_frac(ec);
			if (!f)
				break;
			boost += quanta;
			total -= quanta;
			lp = 1;
		}
		ce->offs[i] = (s32)boost;
		if (boost) {
			logp--;
			if (logp < 2)
				logp = 2;
		}
	}
	ce->total = total;
	/* trim */
	u64 trim = 5;
	if (!(tell + 48 > total))
		trim = ec_icdf(ec, ce_trim_icdf, 7);
	ce->trim = trim;
	/* bits restants, réserve d'anti-effondrement */
	s64 bits = (s64)(len << 6) - ec_tell_frac(ec) - 1;
	u64 rsv = 0;
	if (ce->trans && (s64)lm >= 2 && bits >= (s64)((lm + 2) << 3))
		rsv = 8;
	ce->antirsv = rsv;
	bits -= (s64)rsv;
	alloc(ce, ec, (s32)bits);
	fine(ce, ec);
	/* historique : décalage de N */
	for (u64 ch = 0; ch < ce->cc; ch++)
		memmove(ce->mem[ch], ce->mem[ch] + ce->n, (CE_MEMN - ce->n) << 2);
	bands(ce, ec);
	/* anti-effondrement, énergies finales */
	u64 anticol = 0;
	if (ce->antirsv)
		anticol = ec_bits(ec, 1);
	final_energy(ce, ec);
	if (anticol)
		anti(ce);
	if (ce->silence)
		for (int i = 0; i < 2 * CE_NBE; i++)
			ce->olde[i] = ak_u2f(MINUS28);
	synth(ce);
	postfilter(ce);
	history(ce, ec);
	deemph(ce);
	return (long)ce->n;
}
