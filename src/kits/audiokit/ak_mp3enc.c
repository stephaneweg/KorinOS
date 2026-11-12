/* AudioKit : encodeur MP3 (ak_mp3enc.c, d'après ak_mp3enc.inc) : MPEG-1
 * couche III (ISO/IEC 11172-3), MPEG-2 et 2.5 (13818-3), débit constant.
 *
 * Banc d'analyse polyphase (fenêtre C[i] = D[i] / 32 de la norme, matrice
 * cos((2k + 1)(i - 16) pi / 64)), MDCT de 36 points par sous-bande (blocs
 * longs seulement), inversion de fréquence, papillons d'anticrénelage
 * inverses. Stéréo : gauche/droite ou M/S par trame. Pas de modèle
 * psychoacoustique : les facteurs d'échelle mettent le bruit en forme
 * d'après l'énergie des bandes ; global_gain commun aux canaux du granule,
 * cherché par dichotomie dans le budget de bits ; une cible de global_gain
 * et le réservoir de bits reportent les bits des granules faciles vers les
 * difficiles. Huffman : big_values en trois régions (toutes les coupes
 * essayées), quadruplets (table A ou B). Bourrage pour le débit exact ;
 * passe-bas selon le débit par canal. Fin : zéros pour sortir le retard du
 * banc et de la MDCT. Pas d'en-tête Xing.
 *
 * Mêmes octets que l'asm au bit près : mêmes opérations flottantes dans le
 * même ordre (fmla de l'asm -> ak_fmadd, voie par voie ; réduction faddp
 * recopiée). Écart voulu : après une erreur de sortie, l'asm planterait au
 * ak_enc_write ou ak_enc_close suivant (trame pleine non vidée : copie de 0
 * trame dans une boucle do-while, 2^64 tours) ; le C n'en copie aucune et
 * réencode la trame pleine (l'erreur gardée revient). */
#include "ak_enc.h"
#include "ak_mdct.h"
#include "ak_mp3.h"
#include "ak_mp3enc.h"

#define XRGF (ME_XRG / 4)
#define PCMF (ME_PCMCH / 4)

/* ---- tables (mêmes bits que l'asm) */
const u64 mp3e_kd[6] = {
	0x3FA921FB54442D18, /* pi / 64 */
	0x3FB657184AE74487, /* pi / 36 */
	0x3FA657184AE74487, /* pi / 72 */
	0x3FBC71C71C71C71C, /* 1 / 9 */
	0x4264BFDAD5362A27, /* 2^39,375 (pas de global_gain 0) */
	0x3FEC199BDD85529C, /* 2^-0,1875 */
};
const u32 mp3e_kf[9] = {
	0x3ECF9097, /* 0,4054 (arrondi de la norme) */
	0x3F3504F3, /* racine de 1/2 */
	0x43160000, /* 150 : cible de global_gain au départ */
	0x35800000, /* 2^-20 */
	0x34000000, /* 2^-23 (log2 approché) */
	0x42FE0000, /* 127 */
	0x322BCC77, /* 1e-8 : énergie par ligne négligeable */
	0x3F000000, /* alpha : 1/2 (mise en forme du bruit) */
	0xC47A0000, /* -1000 */
};
/* 2^(3/8 sf) : facteur d'échelle sf (2^(sf/2)) sur |xr|^3/4 */
const u32 mp3e_amp[16] = {
	0x3F800000, 0x3FA5FED7, 0x3FD744FD, 0x400B95C2, 0x403504F3, 0x406AC0C7, 0x409837F0, 0x40C5672A,
	0x41000000, 0x4125FED7, 0x415744FD, 0x418B95C2, 0x41B504F3, 0x41EAC0C7, 0x421837F0, 0x4245672A,
};
const u32 mp3e_sgn[4] = {0x3F800000, 0xBF800000, 0x3F800000, 0xBF800000};
const u32 mp3e_rates[9] = {44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000};
const u16 mp3e_br1[15] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
const u16 mp3e_br2[15] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
/* passe-bas : kbit/s par canal (au moins), fréquence de coupure (Hz) */
const u16 mp3e_lp[20] = {96, 24000, 80, 20000, 64, 18500, 56, 17000, 48, 15500,
			 40, 14000, 32, 12000, 24, 9500,  16, 7500,  0,	 5000};
/* MPEG-1 : scalefac_compress -> slen1 (bandes 0..10), slen2 (11..20) */
const u8 mp3e_sfc1[32] = {0, 0, 0, 1, 0, 2, 0, 3, 3, 0, 1, 1, 1, 2, 1, 3,
			  2, 1, 2, 2, 2, 3, 3, 1, 3, 2, 3, 3, 4, 2, 4, 3};
/* fins des partitions de facteurs d'échelle (0 : fin) */
const u8 mp3e_part1[3] = {11, 21, 0};
const u8 mp3e_part2[5] = {6, 11, 16, 21, 0};

static long mp3_enc_push(struct akn *e, const s16 *pcm, u64 frames);
static long mp3_enc_finish(struct akn *e);

/* ---- écrivain de bits (poids fort d'abord) */
struct bw {
	u64 acc, n;
	u8 *p;
};
/* n bits de v (v n'est pas masqué : comme l'asm) */
static inline void bw_put(struct bw *w, u64 v, u64 n)
{
	w->acc = AK_SHL(w->acc, n) | v;
	w->n += n;
	while (w->n >= 8) {
		w->n -= 8;
		*w->p++ = (u8)AK_SHR(w->acc, w->n);
	}
}

static inline u16 rd16(const u8 *p)
{
	return (u16)(p[0] | p[1] << 8);
}

/* ---------------------------------------------------------------------------
 *  Ouverture */

/* fenêtre, matrices, pas de quantification */
static void tables(struct mp3e *st)
{
	/* C inversée : Cr[m] = D[511 - m] / 32 / 32768 */
	float k20 = ak_u2f(mp3e_kf[3]);
	for (int m = 0; m < 512; m++)
		st->cr[m] = ak_u2f(mp3_D[511 - m]) * k20;
	/* analyse : Mt[i][k] = cos((2k + 1)(47 - i) pi / 64) */
	double d8 = ak_u2d(mp3e_kd[0]), s, c;
	float *p = st->mt;
	for (s64 i = 0; i < 64; i++)
		for (s64 k = 0; k < 32; k++) {
			ak_sincos((double)((2 * k + 1) * (47 - i)) * d8, &s, &c);
			*p++ = (float)c;
		}
	/* MDCT : W[n][k] = sin(pi/36 (n + 1/2)) cos(pi/72 (2n + 19)(2k + 1)) / 9 */
	double d9 = ak_u2d(mp3e_kd[1]), d10 = ak_u2d(mp3e_kd[2]), d11 = ak_u2d(mp3e_kd[3]);
	p = st->wm;
	for (s64 n = 0; n < 36; n++) {
		ak_sincos(((double)n + 0.5) * d9, &s, &c);
		double w = s * d11;
		for (s64 k = 0; k < 18; k++) {
			ak_sincos((double)((2 * n + 19) * (2 * k + 1)) * d10, &s, &c);
			p[k] = (float)(c * w);
		}
		p += 20;
	}
	/* pas : 2^(-3/16 (g - 210)) */
	double g = ak_u2d(mp3e_kd[4]), r = ak_u2d(mp3e_kd[5]);
	for (int i = 0; i < 256; i++) {
		st->step[i] = (float)g;
		g = g * r;
	}
}

/* encodeur -> 0 / < 0 */
long mp3_enc_init(struct akn *e)
{
	u64 rate = (u64)e->rate, sri, bi;
	for (sri = 0; sri < 9; sri++)
		if (mp3e_rates[sri] == rate)
			break;
	if (sri == 9)
		return AKE_FORMAT;
	if ((u64)e->ch - 1 > 1)
		return AKE_FORMAT;
	/* débit (exact, dans la table de la version) */
	u64 q = (u64)e->quality;
	if (!q)
		q = 128000;
	u64 kb = q / 1000;
	if (q - kb * 1000)
		return AKE_FORMAT;
	const u16 *br = sri < 3 ? mp3e_br1 : mp3e_br2;
	for (bi = 1; bi < 15; bi++)
		if (br[bi] == kb)
			break;
	if (bi == 15)
		return AKE_FORMAT;
	struct mp3e *st = akn_alloc(e, ME_SIZE + ME_ARRAYS);
	if (!st)
		return AKE_NOMEM;
	e->codec = st;
	u8 *a = (u8 *)st + ME_SIZE;
	st->pcm = (float *)a;
	st->cr = (float *)(a + ME_A_CR);
	st->mt = (float *)(a + ME_A_MT);
	st->wm = (float *)(a + ME_A_WM);
	st->sbh = (float *)(a + ME_A_SBH);
	st->xr = (float *)(a + ME_A_XR);
	st->x34 = (float *)(a + ME_A_X34);
	st->ix = (u32 *)(a + ME_A_IX);
	st->step = (float *)(a + ME_A_STEP);
	st->msb = a + ME_A_MSB;
	st->pend = (struct mp3pend *)(a + ME_A_PEND);
	st->zero = (s16 *)(a + ME_A_ZERO);
	st->wk = (u32 *)(a + ME_A_WK);
	/* paramètres */
	u64 nch = (u64)e->ch;
	st->nch = nch;
	st->rate = rate;
	st->sri = sri;
	u64 lsf = sri >= 3; /* MPEG-2, 2.5 */
	st->lsf = lsf;
	st->ngr = 2 - lsf;
	st->spf = st->ngr * 576;
	st->sfb = mp3_sfb_long + sri * 24;
	st->num = (144 >> lsf) * q;
	/* informations de côté : 17 / 32 octets ; LSF : 9 / 17 */
	st->side = !lsf ? (nch == 1 ? 17 : 32) : (nch == 1 ? 9 : 17);
	/* en-tête : synchro, version, couche III, sans CRC, débit, fréquence,
	 * original ; mono */
	u64 ver = sri >= 6 ? 0 : 3;
	if (sri >= 3 && sri < 6)
		ver = 2;
	u64 h = 0xFFE30004 | ver << 19 | bi << 12 | (sri % 3) << 10;
	if (nch == 1)
		h |= 0xC0;
	st->hdr = h;
	/* réservoir : 511 (LSF 255) octets, au plus 16 créneaux minimaux */
	s64 m = (s64)(st->num / st->rate - st->side - 4);
	if (m <= 0)
		return AKE_FORMAT;
	u64 rm = (u64)m << 4, cap = lsf ? 255 : 511;
	st->rmax = rm < cap ? rm : cap;
	/* passe-bas selon le débit par canal */
	u64 pc = kb / nch, f;
	const u16 *lp = mp3e_lp;
	do {
		f = lp[0];
		lp += 2;
	} while (pc < f);
	u64 lim = (u64)lp[-1] * 1152 / st->rate;
	st->lim = lim < 576 ? lim : 576;
	st->t = ak_u2f(mp3e_kf[2]); /* cible de départ */
	tables(st);
	e->push = mp3_enc_push;
	e->finish = mp3_enc_finish;
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Analyse */

/* canal, granule : 576 échantillons -> xr[gr][canal] */
static void analyze(struct mp3e *st, u64 ch, u64 gr)
{
	const float *s = st->pcm + ch * PCMF + gr * 576; /* (480 avant) */
	float *sb = st->sbh + ch * 32 * 36;
	float *xr = st->xr + (ch + gr * 2) * XRGF;
	float y[64];
	for (u64 t = 0; t < 18; t++) {
		/* Y'[i] = somme_j Cr[64 j + i] s[p + 64 j + i] (= Y[63 - i]) */
		const float *p = s + t * 32;
		for (int i = 0; i < 64; i++) {
			float acc = 0.0f;
			for (int j = 0; j < 8; j++)
				acc = ak_fmadd(st->cr[64 * j + i], p[64 * j + i], acc);
			y[i] = acc;
		}
		/* S[k] = somme_i Mt[i][k] Y'[i] ; inversion de fréquence : sous-bandes
		 * impaires, pas impairs ; sous-bande k, échantillon 18 + t */
		for (int k = 0; k < 32; k++) {
			float acc = 0.0f;
			for (int i = 0; i < 64; i++)
				acc = ak_fmadd(st->mt[i * 32 + k], y[i], acc);
			if (t & 1)
				acc = acc * ak_u2f(mp3e_sgn[k & 3]);
			sb[k * 36 + 18 + t] = acc;
		}
	}
	/* MDCT de chaque sous-bande (18 précédents + 18 nouveaux) */
	for (int b = 0; b < 32; b++) {
		float *x = sb + b * 36, acc[18];
		for (int k = 0; k < 18; k++)
			acc[k] = 0.0f;
		for (int n = 0; n < 36; n++)
			for (int k = 0; k < 18; k++)
				acc[k] = ak_fmadd(st->wm[n * 20 + k], x[n], acc[k]);
		for (int k = 0; k < 18; k++)
			xr[b * 18 + k] = acc[k];
		/* les 18 nouveaux deviennent les précédents */
		memmove(x, x + 18, 18 * 4);
	}
	/* anticrénelage inverse : bu' = bu cs + bd ca, bd' = bd cs - bu ca */
	for (int b = 1; b < 32; b++) {
		float *x = xr + b * 18;
		for (int i = 0; i < 8; i++) {
			float cs = ak_u2f(mp3_cs[i]), ca = ak_u2f(mp3_ca[i]);
			float bu = x[-1 - i], bd = x[i];
			float u = ak_fmadd(bd, ca, bu * cs);
			float d = ak_fmsub(bu, ca, bd * cs);
			x[-1 - i] = u;
			x[i] = d;
		}
	}
}

/* M/S si la somme des |M| + |S| est moindre que |G| + |D|
 * (M = (G + D) / racine 2, S = (G - D) / racine 2) */
static void stereo(struct mp3e *st)
{
	float v4[4] = {0, 0, 0, 0}, v5[4] = {0, 0, 0, 0};
	for (u64 g = 0; g < st->ngr; g++) {
		const float *l = st->xr + g * 2 * XRGF, *r = l + XRGF;
		for (int i = 0; i < 576; i += 4)
			for (int k = 0; k < 4; k++) {
				float a = l[i + k], b = r[i + k];
				v4[k] = v4[k] + ak_fabs(a);
				v4[k] = v4[k] + ak_fabs(b);
				v5[k] = v5[k] + ak_fabs(a + b);
				v5[k] = v5[k] + ak_fabs(a - b);
			}
	}
	/* faddp, faddp */
	float s4 = (v4[0] + v4[1]) + (v4[2] + v4[3]);
	float s5 = (v5[0] + v5[1]) + (v5[2] + v5[3]);
	float k = ak_u2f(mp3e_kf[1]);
	s5 = s5 * k;
	if (s5 >= s4)
		return;
	st->ms = 1;
	for (u64 g = 0; g < st->ngr; g++) {
		float *l = st->xr + g * 2 * XRGF, *r = l + XRGF;
		for (int i = 0; i < 576; i++) {
			float a = l[i], b = r[i];
			l[i] = (a + b) * k;
			r[i] = (a - b) * k;
		}
	}
}

/* ---------------------------------------------------------------------------
 *  Quantification */

/* facteurs d'échelle (mise en forme du bruit, sans modèle psychoacoustique) :
 * une bande dont l'énergie par ligne est r fois moindre que celle de la plus
 * forte (r <= 2^13) est amplifiée de 2^(sf / 2), sf = alpha log2 r (bornes
 * 15 / 7) ; appliqués à |xr|^3/4 ; part2 et scalefac_compress (MPEG-1 : 2
 * groupes, table ; LSF : partitions 6 5 5 5) */
static void scf(struct mp3e *st, u64 gr, u64 ch)
{
	struct mp3gi *gi = &st->gi[ch + gr * 2];
	const float *xr = st->xr + (ch + gr * 2) * XRGF;
	float *x34 = st->x34 + ch * XRGF;
	const u16 *sfb = st->sfb;
	float k23 = ak_u2f(mp3e_kf[4]), k127 = ak_u2f(mp3e_kf[5]), fl = ak_u2f(mp3e_kf[6]);
	float alpha = ak_u2f(mp3e_kf[7]), mute = ak_u2f(mp3e_kf[8]), mx = mute;
	union {
		float f[24];
		u8 b[96];
	} sp;
	/* log2 de l'énergie par ligne (approché : bits du flottant) */
	for (u64 b = 0; b < 21; b++) {
		u64 i = sfb[b], end = sfb[b + 1], n = end - i;
		float s0 = 0.0f;
		do {
			float v = xr[i];
			s0 = ak_fmadd(v, v, s0);
			i++;
		} while (i < end);
		s0 = s0 / (float)(s64)n;
		float s2 = mute;
		if (s0 >= fl) {
			s2 = (float)(s32)ak_f2u(s0);
			s2 = s2 * k23;
			s2 = s2 - k127;
		}
		sp.f[b] = s2;
		mx = ak_fmax(mx, s2);
	}
	/* facteurs */
	float s4 = mx - 13.0f;
	for (u64 b = 0; b < 21; b++) {
		float s2 = sp.f[b];
		u32 w = 0;
		if (!(s2 == mute)) {
			s2 = ak_fmax(s2, s4);
			s2 = mx - s2;
			s2 = s2 * alpha;
			w = (u32)ak_fcvtns_w(s2);
			u32 lim = b < 11 ? 15 : 7;
			if (!(w < lim))
				w = lim;
		}
		gi->sf[b] = (u8)w;
		if (w) {
			float amp = ak_u2f(mp3e_amp[w]); /* 2^(3/8 sf) sur |xr|^3/4 */
			u64 i = sfb[b], end = sfb[b + 1];
			do {
				x34[i] = x34[i] * amp;
				i++;
			} while (i < end);
		}
	}
	/* bits nécessaires par partition (sp : octets) */
	const u8 *part = st->lsf ? mp3e_part2 : mp3e_part1;
	u64 p = 0, b = 0;
	for (; part[p]; p++) {
		u32 m = 0;
		do {
			u32 v = gi->sf[b];
			if (v > m)
				m = v;
			b++;
		} while (b < part[p]);
		sp.b[p] = (u8)(32 - ak_clz32(m));
	}
	if (!st->lsf) {
		/* MPEG-1 : (slen1, slen2) de la table, le moins de bits */
		u32 n1 = sp.b[0], n2 = sp.b[1], best = 999;
		u64 bi = 0;
		for (u64 c = 0; c < 16; c++) {
			u32 s1 = mp3e_sfc1[2 * c], s2 = mp3e_sfc1[2 * c + 1];
			if (s1 < n1 || s2 < n2)
				continue;
			u32 cost = s1 * 11 + s2 * 10;
			if (cost >= best)
				continue;
			best = cost;
			bi = c;
		}
		gi->sfc = (u32)bi;
		gi->p2 = best;
		sp.b[0] = mp3e_sfc1[2 * bi];
		sp.b[1] = mp3e_sfc1[2 * bi + 1];
		part = mp3e_part1;
	} else {
		/* scalefac_compress = ((s0 5 + s1) << 4) + (s2 << 2) + s3 */
		u32 s0 = sp.b[0], s1 = sp.b[1], s2 = sp.b[2], s3 = sp.b[3];
		gi->sfc = ((s0 * 5 + s1) << 4) | s2 << 2 | s3;
		gi->p2 = (s1 + s2 + s3) * 5 + s0 * 6;
		part = mp3e_part2;
	}
	/* bits de chaque bande */
	for (p = 0, b = 0; part[p]; p++) {
		u8 n = sp.b[p];
		do
			gi->sl[b++] = n;
		while (b < part[p]);
	}
}

/* |xr|^3/4 (576), sortie, pas -> valeur maximale ; ix = partie entière(x34 pas + 0,4054) */
static u32 quant(const float *x34, u32 *ix, float step)
{
	float c = ak_u2f(mp3e_kf[0]);
	u32 m = 0;
	for (int i = 0; i < 576; i++) {
		float v = x34[i] * step;
		v = v + c;
		u32 q = ak_fcvtzu_w(v);
		ix[i] = q;
		if (q > m)
			m = q;
	}
	return m;
}

/* premier segment, fin (segments) -> bits des codes et linbits (sans les
 * signes), *tab = table */
static u64 best(const u32 *wk, u64 s, u64 e, u64 *tab)
{
	*tab = 0;
	if (s >= e)
		return 0;
	const u32 *M = wk + ME_WK_M / 4, *E = wk + ME_WK_E / 4;
	u64 mx = 0; /* maximum de la région */
	for (u64 i = s; i < e; i++)
		if (M[i] > mx)
			mx = M[i];
	if (!mx)
		return 0;
	u32 n15 = E[e] - E[s]; /* valeurs >= 15 */
	const u32 *ps = wk + s * 16, *pe = wk + e * 16;
	u32 bc = 0x7FFFFFFF;
	u64 bt = 0;
	for (int k = 0; k < 13; k++) {
		if (mx >= mp3e_hlist[4 * k + 2])
			continue;
		u32 c = pe[k] - ps[k];
		if (c >= bc)
			continue;
		bc = c;
		bt = mp3e_hlist[4 * k + 3];
	}
	/* familles 16 et 24 : linbits selon le maximum */
	u64 nb = 0;
	if (mx >= 15)
		nb = 64 - ak_clz64(mx - 15);
	const u8 *ls = mp3e_lbsel + nb * 4;
	u32 c = pe[13] - ps[13] + (u32)ls[1] * n15;
	if (c < bc) {
		bc = c;
		bt = ls[0];
	}
	c = pe[14] - ps[14] + (u32)ls[3] * n15;
	if (c < bc) {
		bc = c;
		bt = ls[2];
	}
	*tab = bt;
	return bc;
}

/* valeurs absolues (576), débuts des bandes, informations du granule,
 * travail -> bits (codes, linbits, signes ; big_values, régions, tables,
 * quadruplets remplis) */
static u64 count(const u32 *ix, const u16 *sfb, struct mp3gi *gi, u32 *wk)
{
	/* dernière valeur non nulle -> fin des paires */
	u64 n = 576;
	while (n && !ix[n - 1])
		n--;
	n = (n + 1) & ~(u64)1;
	/* quadruplets (valeurs <= 1) depuis la fin : tables A et B */
	u64 x5 = n, ba = 0, bb = 0;
	while (x5 >= 4) {
		const u32 *v = ix + x5 - 4;
		if ((v[0] | v[1] | v[2] | v[3]) > 1)
			break;
		u32 idx = v[0] << 3 | v[1] << 2 | v[2] << 1 | v[3];
		u32 sum = v[0] + v[1] + v[2] + v[3];
		ba += mp3e_quad[idx * 2 + 1];
		ba += sum;
		bb += sum;
		bb += 4;
		x5 -= 4;
	}
	gi->c1 = (u32)((n - x5) >> 2);
	gi->c1t = ba > bb;
	u64 bits = ba <= bb ? ba : bb; /* bits des quadruplets */
	gi->bv = (u32)(x5 >> 1);
	u64 nbv = x5; /* lignes des big_values */
	/* segments : bandes coupées à la fin des big_values */
	u64 nseg = 0;
	if (nbv) {
		nseg = 1;
		while (nseg < 22 && sfb[nseg] < nbv)
			nseg++;
	}
	u32 *E = wk + ME_WK_E / 4, *M = wk + ME_WK_M / 4;
	for (int i = 0; i < 16; i++)
		wk[i] = 0;
	E[0] = 0;
	for (u64 j = 0; j < nseg; j++) {
		u64 a = sfb[j], b = sfb[j + 1];
		if (!(b < nbv))
			b = nbv;
		/* maximum, valeurs >= 15, non nulles (signes) */
		u64 mx = 0, n15 = 0, nz = 0;
		for (u64 i = a; i < b; i++) {
			u64 v = ix[i];
			if (v > mx)
				mx = v;
			n15 += v >= 15;
			nz += v != 0;
		}
		bits += nz;
		E[j + 1] = E[j] + (u32)n15;
		M[j] = (u32)mx;
		/* coût de chaque table de codes possible, cumulé (P[j + 1] = P[j] + coût) */
		u32 *P = wk + j * 16;
		for (int k = 0; k < 15; k++) {
			const u8 *hl = mp3e_hlist + 4 * k;
			u32 dim = hl[2];
			if (k < 13 && mx >= dim) /* tables 16 et 24 : toujours */
				continue;
			const u8 *len = mp3e_hlen + rd16(hl);
			u64 c = 0;
			for (u64 i = a; i < b; i += 2) {
				u32 x = ix[i], y = ix[i + 1];
				if (!(x < 15))
					x = 15;
				if (!(y < 15))
					y = 15;
				c += len[x * dim + y];
			}
			P[16 + k] = P[k] + (u32)c;
		}
	}
	if (!nseg) {
		gi->t[0] = gi->t[1] = gi->t[2] = 0;
		gi->r0 = gi->r1 = 0;
		gi->bits = (u32)bits;
		return bits;
	}
	/* région 0 = [0, a) (a <= 16), région 2 = [c, fin) */
	u32 *B0 = wk + ME_WK_B0 / 4, *B2 = wk + ME_WK_B2 / 4;
	u64 t;
	for (u64 a = 1; a <= nseg && a <= 16; a++) {
		B0[2 * a] = (u32)best(wk, 0, a, &t);
		B0[2 * a + 1] = (u32)t;
	}
	for (u64 c = 0; c <= nseg; c++) {
		B2[2 * c] = (u32)best(wk, c, nseg, &t);
		B2[2 * c + 1] = (u32)t;
	}
	/* toutes les coupes (region0_count, region1_count) */
	u64 bc = 0x7FFFFFFF;
	for (u64 r0 = 0;;) {
		u64 x9 = r0 + 1, a = x9 < nseg ? x9 : nseg;
		for (u64 r1 = 0;;) {
			u64 x11 = x9 + r1 + 1;
			if (x11 > 22)
				break;
			u64 c = x11 < nseg ? x11 : nseg;
			u64 t1, cost = best(wk, a, c, &t1);
			cost += B0[2 * a];
			cost += B2[2 * c];
			if (cost < bc) {
				bc = cost;
				gi->r0 = (u32)r0;
				gi->r1 = (u32)r1;
				gi->t[0] = B0[2 * a + 1];
				gi->t[1] = (u32)t1;
				gi->t[2] = B2[2 * c + 1];
			}
			if (x11 >= nseg)
				break;
			if (++r1 >= 8)
				break;
		}
		if (x9 >= nseg)
			break;
		if (++r0 >= 16)
			break;
	}
	bits = bc + bits;
	gi->bits = (u32)bits;
	return bits;
}

/* granule, global_gain -> bits des canaux (ME_INF : valeur > 8206 ou plus de
 * 4095 bits) ; ix, gi remplis */
static u64 bitsat(struct mp3e *st, u64 gr, u64 g)
{
	u64 sum = 0;
	for (u64 ch = 0; ch < st->nch; ch++) {
		u32 *ix = st->ix + ch * 576;
		u32 m = quant(st->x34 + ch * XRGF, ix, st->step[g]);
		if (m > 8206)
			return ME_INF;
		struct mp3gi *gi = &st->gi[ch + gr * 2];
		u64 b = count(ix, st->sfb, gi, st->wk);
		b += gi->p2; /* + facteurs d'échelle */
		gi->bits = (u32)b;
		if (b > 4095)
			return ME_INF;
		sum += b;
	}
	return sum;
}

/* ---------------------------------------------------------------------------
 *  Huffman : écriture */

/* valeurs absolues, xr (signes), informations du granule, débuts des bandes */
static void hwrite(struct mp3e *st, const u32 *ix, const float *xr, const struct mp3gi *gi, const u16 *sfb)
{
	struct bw w = {st->wacc, st->wn, st->wptr};
	for (int b = 0; b < 21; b++) /* facteurs d'échelle */
		bw_put(&w, gi->sf[b], gi->sl[b]);
	u64 nbv = (u64)gi->bv << 1;
	u64 e0 = sfb[gi->r0 + 1], e1 = sfb[(u64)gi->r0 + gi->r1 + 2];
	if (!(e0 < nbv))
		e0 = nbv; /* fin de la région 0 */
	if (!(e1 < nbv))
		e1 = nbv; /* fin de la région 1 */
	u64 i = 0;
	for (; i < nbv; i += 2) {
		u32 tb = i < e0 ? gi->t[0] : i < e1 ? gi->t[1] : gi->t[2];
		if (!tb)
			continue;
		const u8 *hd = mp3e_hdir + tb * 4;
		u32 dim = hd[2], lb = hd[3];
		u32 x = ix[i], y = ix[i + 1];
		u32 idx = (x < 15 ? x : 15) * dim + (y < 15 ? y : 15) + rd16(hd);
		bw_put(&w, mp3e_hcode[idx], mp3e_hlen[idx]);
		if (x) {
			if (lb && x >= 15)
				bw_put(&w, x - 15, lb);
			bw_put(&w, ak_f2u(xr[i]) >> 31, 1);
		}
		if (y) {
			if (lb && y >= 15)
				bw_put(&w, y - 15, lb);
			bw_put(&w, ak_f2u(xr[i + 1]) >> 31, 1);
		}
	}
	/* quadruplets */
	for (u32 n = gi->c1; n; n--, i += 4) {
		const u32 *v = ix + i;
		u32 idx = v[0] << 3 | v[1] << 2 | v[2] << 1 | v[3];
		if (!gi->c1t)
			bw_put(&w, mp3e_quad[idx * 2], mp3e_quad[idx * 2 + 1]);
		else
			bw_put(&w, (u32)(15 - idx), 4); /* table B : 4 bits inversés */
		for (int k = 0; k < 4; k++)
			if (v[k])
				bw_put(&w, ak_f2u(xr[i + k]) >> 31, 1);
	}
	st->wacc = w.acc;
	st->wn = w.n;
	st->wptr = w.p;
}

/* granule : global_gain, bits ; écrit le granule */
static void granule(struct mp3e *st, u64 gr)
{
	/* |xr|^(3/4) = racine(|x| racine |x|) */
	for (u64 ch = 0; ch < st->nch; ch++) {
		const float *xr = st->xr + (ch + gr * 2) * XRGF;
		float *x34 = st->x34 + ch * XRGF;
		for (int i = 0; i < 576; i++) {
			float a = ak_fabs(xr[i]);
			float r = ak_fsqrt(a);
			r = r * a;
			x34[i] = ak_fsqrt(r);
		}
		scf(st, gr, ch);
	}
	/* budget : disponible - parts des granules suivants (<= 4095 par canal) */
	u64 avail = st->avail, used = st->used, mg = st->meangr, rest = st->ngr - gr - 1;
	s64 bud = (s64)(avail - used - mg * rest);
	s64 cap = (s64)(st->nch * 4095);
	if (!(bud < cap))
		bud = cap;
	/* plus petit global_gain qui tient (dichotomie) */
	u64 lo = 0, hi = 255;
	while (lo < hi) {
		u64 mid = (lo + hi) >> 1;
		if ((s64)bitsat(st, gr, mid) > bud)
			lo = mid + 1;
		else
			hi = mid;
	}
	/* pas plus fin que la cible */
	s64 g = ak_fcvtns_x(st->t);
	if (!(g > (s64)lo))
		g = (s64)lo;
	if (!(g < 255))
		g = 255;
	/* mais assez de bits pour ne pas déborder du réservoir */
	s64 fl = (s64)(avail - used - (st->rmax << 3) - mg * rest);
	while ((u64)g > lo) {
		if ((s64)bitsat(st, gr, (u64)g) >= fl)
			break;
		g--;
	}
	/* la cible suit les granules difficiles */
	float s1 = (float)g, t = st->t;
	if (s1 > t)
		st->t = ak_fmadd(s1 - t, 0.25f, t);
	/* quantification retenue (au besoin plus grossière : budget) */
	u64 b;
	for (;;) {
		b = bitsat(st, gr, (u64)g);
		if ((s64)b <= bud)
			break;
		if ((u64)g >= 255)
			break;
		g++;
	}
	st->used = used + b;
	/* Huffman de chaque canal */
	for (u64 ch = 0; ch < st->nch; ch++) {
		st->gi[ch + gr * 2].gg = (u32)g;
		hwrite(st, st->ix + ch * 576, st->xr + (ch + gr * 2) * XRGF, &st->gi[ch + gr * 2], st->sfb);
	}
}

/* ---------------------------------------------------------------------------
 *  Trame */

/* informations de côté -> sortie */
static void side(struct mp3e *st, u8 *out, u64 mdb)
{
	struct bw w = {0, 0, out};
	u64 lsf = st->lsf, nch = st->nch;
	if (!lsf) {
		bw_put(&w, mdb, 9);
		bw_put(&w, 0, nch == 1 ? 5 : 3); /* private_bits */
		bw_put(&w, 0, nch << 2);	  /* scfsi */
	} else {
		bw_put(&w, mdb, 8);
		bw_put(&w, 0, nch);
	}
	for (u64 gr = 0; gr < st->ngr; gr++)
		for (u64 ch = 0; ch < nch; ch++) {
			const struct mp3gi *g = &st->gi[ch + gr * 2];
			bw_put(&w, g->bits, 12);
			bw_put(&w, g->bv, 9);
			bw_put(&w, g->gg, 8);
			bw_put(&w, g->sfc, lsf ? 9 : 4);
			bw_put(&w, 0, 1); /* window_switching_flag */
			bw_put(&w, g->t[0], 5);
			bw_put(&w, g->t[1], 5);
			bw_put(&w, g->t[2], 5);
			bw_put(&w, g->r0, 4);
			bw_put(&w, g->r1, 3);
			if (!lsf)
				bw_put(&w, 0, 1); /* preflag */
			bw_put(&w, 0, 1);	  /* scalefac_scale */
			bw_put(&w, g->c1t, 1);
		}
}

/* all = 1 : tout -> écrit les trames dont le créneau est complet (avant
 * ppos), compacte le flux principal -> 0 / < 0 */
static long emit(struct akn *e, u64 all)
{
	struct mp3e *st = e->codec;
	u64 i = 0;
	long r;
	for (; i < st->npend; i++) {
		struct mp3pend *p = &st->pend[i];
		if (!all && p->cpos + p->slot > st->ppos)
			break;
		r = akn_out(e, p, p->n);
		if (r < 0)
			return r;
		r = akn_out(e, st->msb + (p->cpos - st->base), p->slot);
		if (r < 0)
			return r;
	}
	if (i) {
		u64 left = st->npend - i;
		st->npend = left;
		memmove(st->pend, st->pend + i, left << 6);
	}
	if (all)
		return 0;
	/* nouvelle base : premier créneau en attente (sinon ppos) */
	u64 nb = st->npend ? st->pend[0].cpos : st->ppos;
	u64 d = nb - st->base;
	if (!d)
		return 0;
	st->base = nb;
	u64 keep = st->ppos - nb;
	memmove(st->msb, st->msb + d, keep);
	memset(st->msb + keep, 0, ME_MSBN - keep);
	return 0;
}

/* encode la trame de pcm -> 0 / < 0 */
static long frame(struct akn *e)
{
	struct mp3e *st = e->codec;
	/* analyse : sous-bandes et MDCT de chaque granule et canal */
	for (u64 gr = 0; gr < st->ngr; gr++)
		for (u64 ch = 0; ch < st->nch; ch++)
			analyze(st, ch, gr);
	/* stéréo */
	st->ms = 0;
	if (st->nch == 2)
		stereo(st);
	/* passe-bas */
	for (int k = 0; k < 4; k++)
		memset(st->xr + k * XRGF + st->lim, 0, (576 - st->lim) << 2);
	/* taille de la trame (bourrage), créneau des données principales */
	u64 q = st->num / st->rate, rm = st->num - q * st->rate;
	u64 acc = st->acc + rm, pad = 0;
	if (acc >= st->rate) {
		acc -= st->rate;
		pad = 1; /* bourrage */
	}
	st->acc = acc;
	u64 slot = q + pad - st->side - 4; /* créneau (octets) */
	u64 mdb = st->cpos - st->ppos;	   /* main_data_begin */
	st->avail = (mdb + slot) << 3;
	st->used = 0;
	st->meangr = (slot << 3) / st->ngr;
	/* écrivain : au début des données */
	st->wptr = st->msb + (st->ppos - st->base);
	st->wacc = 0;
	st->wn = 0;
	for (u64 gr = 0; gr < st->ngr; gr++)
		granule(st, gr);
	/* dernier octet */
	u8 *wp = st->wptr;
	if (st->wn)
		*wp++ = (u8)AK_SHL(st->wacc, 8 - st->wn);
	u64 end = (u64)(wp - st->msb) + st->base; /* fin des données (position) */
	/* cible : le réservoir se remplit -> pas plus fin */
	float s0 = (float)(s64)(st->avail - st->used);
	float s1 = (float)(s64)(st->rmax << 3);
	s0 = s0 / s1;
	if (s0 > 0.5f) {
		s0 = s0 - 0.5f;
		s0 = s0 + s0;
		float t = st->t - s0;
		st->t = ak_fmax(t, 0.0f); /* (silence : pas sous 0) */
	}
	/* trame en attente : en-tête, informations de côté */
	struct mp3pend *pe = &st->pend[st->npend];
	st->npend++;
	u32 h = (u32)st->hdr | (u32)pad << 9;
	if (st->ms)
		h |= 0x60; /* stéréo jointe, M/S */
	h = __builtin_bswap32(h);
	__builtin_memcpy(pe->hs, &h, 4);
	side(st, pe->hs + 4, mdb);
	pe->cpos = st->cpos;
	pe->slot = slot;
	pe->n = st->side + 4;
	/* trame suivante : créneau, début des données (réservoir <= rmax) */
	u64 cp = st->cpos + slot;
	st->cpos = cp;
	s64 lo = (s64)(cp - st->rmax);
	st->ppos = (s64)end > lo ? end : (u64)lo;
	return emit(e, 0);
}

/* ---------------------------------------------------------------------------
 *  Entrée */

/* PCM s16 entrelacé, trames -> 0 / < 0 */
static long mp3_enc_push(struct akn *e, const s16 *pcm, u64 frames)
{
	struct mp3e *st = e->codec;
	while (frames) {
		u64 room = st->spf - st->fill;
		u64 take = frames < room ? frames : room; /* trames prises */
		float *d = st->pcm + 480 + st->fill;
		/* (take = 0 seulement après une erreur : l'asm copierait 2^64 trames) */
		if (st->nch == 1) {
			for (u64 k = take; k; k--)
				*d++ = (float)*pcm++;
		} else {
			float *d2 = d + PCMF;
			for (u64 k = take; k; k--) {
				*d++ = (float)*pcm++;
				*d2++ = (float)*pcm++;
			}
		}
		st->fill += take;
		frames -= take;
		if (st->fill < st->spf)
			continue;
		long r = frame(e);
		if (r < 0)
			return r;
		st->fill = 0;
		/* historique : les 480 derniers échantillons de chaque canal */
		for (u64 ch = 0; ch < st->nch; ch++) {
			float *c = st->pcm + ch * PCMF;
			memmove(c, c + st->spf, 480 * 4);
		}
	}
	return 0;
}

/* complète la trame par des zéros (et le retard du banc et de la MDCT),
 * vide le réservoir -> 0 / < 0 */
static long mp3_enc_finish(struct akn *e)
{
	struct mp3e *st = e->codec;
	u64 n = (st->spf - st->fill) % st->spf + 1152;
	while (n) {
		u64 k = n < 576 ? n : 576;
		long r = mp3_enc_push(e, st->zero, k);
		if (r < 0)
			return r;
		n -= k;
	}
	return emit(e, 1);
}
