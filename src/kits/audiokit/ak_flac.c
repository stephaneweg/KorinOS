/* AudioKit : FLAC (RFC 9639), décodage (ak_flac.inc).
 *
 * Métadonnées : STREAMINFO, SEEKTABLE, VORBIS_COMMENT (le reste est sauté).
 * Trame : en-tête (CRC-8 vérifié), sous-trames CONSTANT, VERBATIM, FIXED
 * (ordres 0-4), LPC (ordres 1-32), résidu de Rice (paramètres de 4 ou 5
 * bits, échappement), bits perdus ; stéréo gauche/côté, côté/droite,
 * milieu/côté. 4 à 24 bits par échantillon, 1 à 8 canaux.
 * Sortie : entiers 32 bits cadrés à gauche (AKD_S32).
 *
 * Le décodage d'une trame écrit dans les tampons plans même s'il échoue en
 * route (comme l'asm) : une trame suivante qui a moins de canaux que le flux
 * laisse les plans des autres canaux tels quels. */
#include "ak.h"

#define FLC_MAXBLOCK 65536

/* état FLAC (FLC_*, 256 octets) */
struct flc {
	u64 minbs;	   /* 0 */
	u64 maxbs;	   /* 8 */
	u64 maxframe;	   /* 16 */
	u64 rate;	   /* 24 */
	u64 ch;		   /* 32 */
	u64 bps;	   /* 40 */
	const u8 *seektab; /* 48 points de SEEKTABLE (18 o chacun, gros-boutistes) */
	u64 nseek;	   /* 56 */
	u64 h_bs;	   /* 64 en-tête de la dernière trame lue */
	u64 h_rate;	   /* 72 */
	u64 h_cha;	   /* 80 affectation des canaux (0-10) */
	u64 h_bps;	   /* 88 */
	u64 h_num;	   /* 96 premier échantillon de la trame */
	u64 h_len;	   /* 104 octets de l'en-tête (CRC-8 compris) */
	u64 h_nch;	   /* 112 */
	u64 pad;	   /* 120 */
	s32 coef[32];	   /* 128 coefficients */
};
AK_STATIC_OFF(struct flc, seektab, 48);
AK_STATIC_OFF(struct flc, h_bs, 64);
AK_STATIC_OFF(struct flc, h_num, 96);
AK_STATIC_OFF(struct flc, h_nch, 112);
AK_STATIC_OFF(struct flc, coef, 128);
_Static_assert(sizeof(struct flc) == 256, "flc");

/* prédicteurs fixes (ordres 1-4) */
static const s32 flac_fixed[4][4] = {{1, 0, 0, 0}, {2, -1, 0, 0}, {3, -3, 1, 0}, {4, -6, 4, -1}};
static const u32 flac_rates[12] = {0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000};
static const u8 flac_sizes[8] = {0, 8, 12, 0, 16, 20, 24, 32};

static long flac_next(struct akd *d);
static long flac_seek(struct akd *d, u64 target);
static long flac_pkt(struct akd *d, const u8 *pkt, u64 n);

/* STREAMINFO (34 o) */
static void flac_streaminfo(struct flc *f, const u8 *p)
{
	f->minbs = ((u32)p[0] << 8) | p[1];
	f->maxbs = ((u32)p[2] << 8) | p[3];
	f->maxframe = ((u32)p[7] << 16) | ((u32)p[8] << 8) | p[9]; /* taille maximale de trame */
	u64 v = ak_bswap64(ak_load64(p + 10)); /* fréquence 20, canaux 3, bits 5, total 36 */
	f->rate = v >> 44;
	f->ch = ((v >> 41) & 7) + 1;
	f->bps = ((v >> 36) & 31) + 1;
	f->h_num = v & 0xFFFFFFFFFull; /* (total : rendu par flac_setup) */
}

/* STREAMINFO lu : champs du décodeur, tampons -> 0 / < 0 */
static long flac_setup(struct akd *d)
{
	struct flc *f = d->codec;
	if (f->bps < 4 || f->bps > 24)
		return AKE_FORMAT;
	if (!f->rate)
		return AKE_FORMAT;
	d->fmt = AKF_FLAC;
	d->rate = (s64)f->rate;
	d->ch = (s64)f->ch;
	d->bits = (s64)f->bps;
	s64 tot = f->h_num ? (s64)f->h_num : -1; /* total d'échantillons (0 : inconnu) */
	d->total = tot;
	if (tot >= 0) {
		s64 x = d->dataend - d->data0; /* débit moyen */
		if (x > 0)
			d->bitrate = (s64)((((u64)x << 3) * f->rate) / (u64)tot);
	}
	d->pcmfmt = AKD_S32;
	d->next = flac_next;
	d->seekfn = flac_seek;
	d->pktfn = flac_pkt;
	u64 mb = f->maxbs;
	if (mb < 16)
		mb = FLC_MAXBLOCK;
	return akd_planes(d, mb, 8); /* (8 plans : une trame peut en avoir plus) */
}

/* lecture sur "fLaC" -> 0 / < 0 */
long flac_open(struct akd *d)
{
	struct flc *f = akd_alloc(d, sizeof *f);
	if (!f)
		return AKE_NOMEM;
	d->codec = f;
	akd_skip(d, 4);
	int si = 0; /* STREAMINFO lu ? */
	for (;;) {
		u64 av;
		u8 *p = akd_need(d, 4, &av);
		if (av < 4)
			return AKE_CORRUPT;
		u32 w = ak_rd32be(p);
		u32 type = (w >> 24) & 0x7F;
		u32 last = w >> 31;
		u64 len = w & 0xFFFFFF;
		if (type == 0) {
			if (len < 34)
				return AKE_CORRUPT;
			p = akd_need(d, 38, &av);
			if (av < 38)
				return AKE_CORRUPT;
			flac_streaminfo(f, p + 4);
			si = 1;
		} else if (type == 3) { /* SEEKTABLE : copie */
			p = akd_need(d, len + 4, &av);
			if (av >= len + 4) {
				u8 *t = akd_alloc(d, len);
				if (t) {
					f->seektab = t;
					memmove(t, p + 4, len);
					f->nseek = len / 18;
				}
			}
		} else if (type == 4) { /* VORBIS_COMMENT : étiquettes */
			u64 want = len + 4;
			if (want >= 65536)
				want = 65536;
			p = akd_need(d, want, &av);
			u64 m = av - 4; /* (moins de 4 octets : m énorme, donc len, comme l'asm) */
			if (m >= len)
				m = len;
			akd_vcomment(d, p + 4, m);
		}
		akd_skip(d, len + 4);
		if (last)
			break;
	}
	if (!si)
		return AKE_CORRUPT;
	d->data0 = d->off;
	return flac_setup(d);
}

/* données du codec (API par paquets) : "fLaC" + blocs, un bloc STREAMINFO
 * avec son en-tête, ou STREAMINFO seul -> 0 / < 0 */
long flac_init_pkt(struct akd *d, const u8 *extra, u64 n)
{
	struct flc *f = akd_alloc(d, sizeof *f);
	if (!f)
		return AKE_NOMEM;
	d->codec = f;
	f->rate = (u64)d->rate; /* par défaut : les paramètres donnés */
	f->ch = (u64)d->ch;
	f->bps = 16;
	if (n >= 34) {
		if (ak_rd32le(extra) == 0x43614C66) { /* 'fLaC' */
			extra += 4;
			n -= 4;
		}
		if (n >= 38 && !(extra[0] & 0x7F) && extra[3] == 34) { /* en-tête de bloc STREAMINFO */
			extra += 4;
			n -= 4;
		}
		if (n >= 34)
			flac_streaminfo(f, extra);
	}
	f->h_num = 0;
	return flac_setup(d);
}

/* en-tête de trame -> 0 (h_* posés), < 0 si ce n'en est pas un */
static long flac_hdr(struct flc *f, const u8 *p, u64 n)
{
	if (n < 6)
		return AKE_CORRUPT;
	if (p[0] != 0xFF)
		return AKE_CORRUPT;
	u32 b = p[1];
	if ((b & 0xFE) != 0xF8)
		return AKE_CORRUPT;
	u32 var = b & 1; /* stratégie : 1 taille variable */
	b = p[2];
	u32 bsc = b >> 4; /* taille de bloc */
	u32 rc = b & 15;  /* fréquence */
	b = p[3];
	u32 cha = b >> 4;	/* canaux */
	u32 sc = (b >> 1) & 7; /* bits */
	if ((b & 1) || !bsc || rc == 15 || cha > 10 || sc == 3)
		return AKE_CORRUPT;
	/* numéro (UTF-8 jusqu'à 36 bits) ; peut lire au-delà de n (comme l'asm :
	 * le CRC est vérifié ensuite) */
	const u8 *q = p + 4;
	u32 c = *q++;
	u64 num = c;
	if (c & 0x80) {
		u32 k = 0; /* nombre de 1 de tête */
		while ((c << k) & 0x80) {
			k++;
			if (k > 7)
				return AKE_CORRUPT;
		}
		if (k == 1)
			return AKE_CORRUPT;
		k--; /* octets de suite */
		num &= 0xFFu >> (k + 2);
		if (k == 6)
			num = 0; /* 11111110 : 36 bits dans la suite */
		while (k) {
			c = *q++;
			if ((c & 0xC0) != 0x80)
				return AKE_CORRUPT;
			num = (u64)(c & 0x3F) | (num << 6);
			k--;
		}
	}
	u64 bs;
	if (bsc == 1)
		bs = 192;
	else if (bsc <= 5)
		bs = 576ull << (bsc - 2);
	else if (bsc == 6)
		bs = (u64)*q++ + 1;
	else if (bsc == 7) {
		bs = (((u64)q[0] << 8) | q[1]) + 1;
		q += 2;
	} else
		bs = 256ull << (bsc - 8);
	u64 rate;
	if (!rc)
		rate = f->rate;
	else if (rc < 12)
		rate = flac_rates[rc];
	else if (rc == 12)
		rate = (u64)*q++ * 1000;
	else {
		u64 v = ((u64)q[0] << 8) | q[1];
		q += 2;
		rate = rc == 13 ? v : v * 10;
	}
	u64 bps = sc ? flac_sizes[sc] : f->bps;
	/* CRC-8 (x^8 + x^2 + x + 1) de l'en-tête */
	u64 hl = (u64)(q - p); /* octets avant le CRC */
	if (hl + 1 > n)
		return AKE_CORRUPT;
	u32 crc = 0;
	for (const u8 *s = p; (uintptr_t)s < (uintptr_t)q; s++) {
		crc ^= *s;
		for (int i = 0; i < 8; i++) {
			u32 t = crc & 0x80;
			crc = (crc << 1) & 0xFF;
			if (t)
				crc ^= 7;
		}
	}
	if (*q != crc)
		return AKE_CORRUPT;
	f->h_len = hl + 1;
	f->h_bs = bs;
	f->h_rate = rate;
	f->h_cha = cha;
	f->h_bps = bps;
	f->h_nch = cha < 8 ? cha + 1 : 2;
	if (!var) { /* taille fixe : numéro de trame */
		u64 m = f->maxbs;
		if (f->minbs != m)
			m = bs;
		if (m < 16)
			m = bs;
		num *= m;
	}
	f->h_num = num;
	/* (champs posés même si les bits sont refusés, comme l'asm) */
	if (bps < 4 || bps > 24)
		return AKE_CORRUPT;
	return 0;
}

/* résidu : sortie, bloc, ordre du prédicteur -> 0 / < 0 ; résidus en
 * out[order..bs[ */
static long flac_residual(struct rb *r, u32 *out, u64 bs, u64 order)
{
	u64 m = rb_get(r, 2);
	if (m > 1)
		return AKE_CORRUPT;
	u64 pb = m + 4;		       /* bits du paramètre (4 ou 5) */
	u64 esc = (15ull << m) + m;    /* échappement : 15 ou 31 */
	u64 po = rb_get(r, 4);	       /* ordre de partition */
	u64 ps = bs >> po;	       /* échantillons par partition */
	if ((ps << po) != bs)
		return AKE_CORRUPT;
	if (ps < order)
		return AKE_CORRUPT;
	u32 *o = out + order;
	u64 np = 1ull << po; /* partitions */
	u64 cnt = ps - order; /* la première : moins l'ordre */
	do {
		u64 k = rb_get(r, pb); /* paramètre de Rice */
		if (k == esc) {
			u64 eb = rb_get(r, 5); /* échappement : eb bits bruts */
			while (cnt) {
				*o++ = (u32)rb_sget(r, eb);
				cnt--;
			}
		} else if (cnt) {
			do {
				u64 q = ak_clz64(r->cache); /* quotient (unaire) */
				if (q >= r->n) {
					q = rb_unary(r);
					if ((uintptr_t)r->p > (uintptr_t)r->end + 8)
						return AKE_CORRUPT;
				} else {
					r->cache = AK_SHL(AK_SHL(r->cache, q), 1); /* (en deux fois, comme l'asm) */
					r->n -= q + 1;
				}
				if (k) {
					if (r->n < k)
						rb_fill(r);
					u64 lo = r->cache >> (64 - k);
					r->cache <<= k;
					r->n -= k;
					q = AK_SHL(q, k) | lo;
				}
				*o++ = (u32)((0 - (q & 1)) ^ (q >> 1)); /* zigzag */
			} while (--cnt);
		}
		cnt = ps;
	} while (--np);
	return 0;
}

/* prédiction : s[i] += (somme c[j] s[i-1-j]) >> décalage */
static void flac_lpc(const struct flc *f, u32 *s, u64 bs, u64 order, u64 sh)
{
	for (u64 i = order; i < bs; i++) {
		u64 acc = 0;
		for (u64 j = 0; j < order; j++)
			acc += (u64)((s64)(s32)s[i - 1 - j] * (s64)f->coef[j]);
		s[i] += (u32)AK_ASR(acc, sh);
	}
}

/* échantillons de chauffe (order de bits bits) */
static void flac_warm(struct rb *r, u32 *out, u64 order, u64 bits)
{
	for (u64 i = 0; i < order; i++)
		out[i] = (u32)rb_sget(r, bits);
}

/* sous-trame : sortie, bits, taille du bloc -> 0 / < 0 */
static long flac_sub(struct flc *f, struct rb *r, u32 *out, u64 bits, u64 bs)
{
	u64 h = rb_get(r, 8);
	if (h & 0x80) /* bit de bourrage */
		return AKE_CORRUPT;
	u64 type = (h >> 1) & 63;
	u64 waste = 0;
	if (h & 1) { /* bits perdus */
		waste = rb_unary(r) + 1;
		bits -= waste;
		if ((s64)bits < 1)
			return AKE_CORRUPT;
	}
	if (type == 0) { /* CONSTANT */
		u32 v = (u32)rb_sget(r, bits);
		for (u64 i = 0; i < bs; i++)
			out[i] = v;
	} else if (type == 1) { /* VERBATIM */
		u64 i = 0;
		do {
			out[i] = (u32)rb_sget(r, bits);
			i++;
		} while (i < bs);
	} else if (type < 8) {
		return AKE_CORRUPT;
	} else if (type <= 12) { /* FIXED : ordre 0..4 */
		u64 order = type - 8;
		if (order > bs)
			return AKE_CORRUPT;
		flac_warm(r, out, order, bits);
		if (flac_residual(r, out, bs, order) < 0)
			return AKE_CORRUPT;
		if (order) {
			for (int j = 0; j < 4; j++) /* coefficients de l'ordre (les 4) */
				f->coef[j] = flac_fixed[order - 1][j];
			flac_lpc(f, out, bs, order, 0);
		}
	} else if (type < 32) {
		return AKE_CORRUPT;
	} else { /* LPC */
		u64 order = type - 31;
		if (order > bs)
			return AKE_CORRUPT;
		flac_warm(r, out, order, bits);
		u64 prec = rb_get(r, 4);
		if (prec == 15)
			return AKE_CORRUPT;
		prec++;
		s64 sh = rb_sget(r, 5); /* décalage */
		if (sh < 0)
			return AKE_CORRUPT;
		for (u64 j = 0; j < order; j++)
			f->coef[j] = (s32)rb_sget(r, prec);
		if (flac_residual(r, out, bs, order) < 0)
			return AKE_CORRUPT;
		flac_lpc(f, out, bs, order, (u64)sh);
	}
	if (waste)
		for (u64 i = 0; i < bs; i++)
			out[i] <<= (waste & 31);
	return 0;
}

/* trame, octets -> trames (< 0 : erreur) ; *used = octets de la trame */
static long flac_frame(struct akd *d, const u8 *p, u64 n, u64 *used)
{
	struct flc *f = d->codec;
	long e = flac_hdr(f, p, n);
	if (e < 0)
		return e;
	u64 bs = f->h_bs;
	u64 mb = f->maxbs;
	if (mb < 16)
		mb = FLC_MAXBLOCK;
	if (bs > mb)
		return AKE_CORRUPT;
	if (f->h_nch > 8)
		return AKE_CORRUPT;
	struct rb r; /* lecteur après l'en-tête */
	rb_init(&r, p + f->h_len, n - f->h_len);
	for (u64 c = 0; c < f->h_nch; c++) {
		u64 bits = f->h_bps;
		u64 cha = f->h_cha; /* le canal de côté a un bit de plus */
		if (cha >= 8 && (cha == 9) == (c == 0)) /* 9 : côté/droite -> le canal 0 ; 8, 10 -> le canal 1 */
			bits++;
		e = flac_sub(f, &r, d->pcm[c], bits, bs);
		if (e < 0)
			return e;
	}
	/* décorrélation stéréo */
	u32 *a = d->pcm[0], *b = d->pcm[1];
	u64 cha = f->h_cha;
	if (cha == 8) { /* droite = gauche - côté */
		for (u64 i = 0; i < bs; i++)
			b[i] = a[i] - b[i];
	} else if (cha == 9) { /* gauche = côté + droite */
		for (u64 i = 0; i < bs; i++)
			a[i] = a[i] + b[i];
	} else if (cha == 10) { /* milieu/côté */
		for (u64 i = 0; i < bs; i++) {
			u32 m = a[i], s = b[i];
			m = (s & 1) | (m << 1);
			a[i] = (u32)((s32)(m + s) >> 1);
			b[i] = (u32)((s32)(m - s) >> 1);
		}
	}
	/* cadré à gauche sur 32 bits */
	u32 sh = (u32)(32 - f->h_bps) & 31;
	for (u64 c = 0; c < f->h_nch; c++) {
		u32 *pl = d->pcm[c];
		for (u64 i = 0; i < bs; i++)
			pl[i] <<= sh;
	}
	/* plus de canaux dans la trame que dans le flux : on rend ceux du flux */
	rb_align(&r);
	rb_get(&r, 16); /* CRC-16 (non vérifié) */
	u64 k = (u64)(rb_byteptr(&r) - p); /* octets de la trame */
	if (k > n)
		return AKE_CORRUPT;
	if (d->mode == 2) { /* (paquets : le flux peut changer) */
		d->ch = (s64)f->h_nch;
		d->rate = (s64)f->h_rate;
	}
	*used = k;
	return (long)bs;
}

/* paquet (une trame), octets -> trames */
static long flac_pkt(struct akd *d, const u8 *pkt, u64 n)
{
	u64 k;
	return flac_frame(d, pkt, n, &k);
}

/* -> trames décodées (0 : fin) */
static long flac_next(struct akd *d)
{
	struct flc *f = d->codec;
	u64 skipped = 0; /* octets sautés (resynchronisation) */
	for (;;) {
		s64 rest = d->dataend - d->off;
		if (rest <= 0)
			return 0;
		u64 want = f->maxframe + 64;
		if (want == 64)
			want = AKD_IOBUF;
		u64 av;
		const u8 *p = akd_need(d, want, &av);
		u64 n = av < (u64)rest ? av : (u64)rest;
		if (n < 2)
			return 0;
		u64 k;
		long r = flac_frame(d, p, n, &k);
		if (r > 0) {
			akd_skip(d, k);
			return r;
		}
		akd_skip(d, 1); /* trame illisible : chercher la suivante */
		skipped++;
		if (skipped >= 1048576)
			return AKE_CORRUPT;
	}
}

/* position : première trame valide à partir de là -> sa position (< 0 :
 * aucune dans les 64 Ko), *num = son échantillon */
static s64 flac_find(struct akd *d, u64 pos, u64 *num)
{
	struct flc *f = d->codec;
	akd_seek(d, pos);
	u64 av;
	const u8 *p = akd_need(d, 65536, &av);
	for (u64 i = 0; i + 16 < av; i++) {
		if (p[i] != 0xFF)
			continue;
		if (flac_hdr(f, p + i, av - i) < 0)
			continue;
		if (f->h_rate != f->rate || f->h_bps != f->bps) /* même fréquence, mêmes bits */
			continue;
		*num = f->h_num;
		return d->off + (s64)i;
	}
	return -1;
}

/* trame voulue -> 0 (pose off, pos, skip) */
static long flac_seek(struct akd *d, u64 target)
{
	struct flc *f = d->codec;
	u64 best = (u64)d->data0; /* meilleure trame : position */
	u64 bnum = 0;		  /*   et premier échantillon */
	if (f->seektab) { /* SEEKTABLE : le dernier point <= cible */
		const u8 *e = f->seektab;
		for (u64 k = f->nseek; k; k--, e += 18) {
			u64 sn = ak_bswap64(ak_load64(e));
			if (sn == ~0ull || sn > target || sn < bnum) /* point réservé */
				continue;
			best = (u64)d->data0 + ak_bswap64(ak_load64(e + 8));
			bnum = sn;
		}
	}
	/* bissection sur les positions : la trame qui suit le milieu */
	u64 lo = best, hi = (u64)d->dataend;
	int turns = 40; /* (tours au plus) */
	while (hi - lo >= 16384 && --turns) {
		u64 mid = (lo + hi) >> 1;
		u64 num;
		s64 pos = flac_find(d, mid, &num);
		if (pos < 0 || num > target || num < bnum) {
			hi = mid;
			continue;
		}
		lo = (u64)pos;
		best = (u64)pos;
		bnum = num;
	}
	d->off = (s64)best;
	d->skip = (s64)(target - bnum);
	d->pos = (s64)target;
	return 0;
}
