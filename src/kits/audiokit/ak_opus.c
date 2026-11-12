/* AudioKit : Opus (RFC 6716) dans Ogg (RFC 7845) : décodeur de plage (RFC 6716
 * 4.1), couche Opus (TOC, trames d'un paquet : codes 0 à 3, bourrage, VBR ;
 * paquets auto-délimités des flux multiples), modes SILK, hybride (SILK +
 * CELT des bandes 17 et plus), CELT, trames redondantes CELT des changements
 * de mode (fondus de 2,5 ms), en-têtes Ogg Opus (OpusHead, OpusTags, familles
 * de canaux 0 et 1, gain de sortie), API par paquets (d'après ak_opus.inc).
 *
 * Un flux Opus (struct od) a son état CELT (ak_celt.c) et SILK (ak_silk.c) ;
 * il décode ses trames dans ses propres plans (flottants, 48 kHz), que la
 * table des canaux (famille 1) recopie dans les plans du décodeur (AKD_PCM)
 * en appliquant le gain d'OpusHead, puis un écrêtage doux (comme libopus en
 * sortie 16 bits).
 * Hors de la norme stricte (pas de dissimulation des pertes, PLC) : une trame
 * vide (DTX, 0 ou 1 octet) rend du silence ; un changement de mode sans trame
 * redondante passe directement au nouveau mode. */
#include "ak_opus.h"
#include "ak_celt.h"
#include "ak_silk.h"

/* champs de l'état Ogg (OG_*, ak_ogg.inc) que la couche Opus pose */
#define OPS_OG_PREROLL 104
#define OPS_OG_GRANOFF 112
#define OPS_OG_STATE 136

static inline u64 *og_field(void *og, u64 off)
{
	return (u64 *)((u8 *)og + off);
}

/* ---------------------------------------------------------------------------
 * Décodeur de plage (RFC 6716 4.1)
 * ------------------------------------------------------------------------- */

void ec_init(struct ec_dec *ec, const u8 *buf, u64 n)
{
	ec->buf = buf;
	ec->storage = n;
	const u8 *end = buf + n;
	ec->end = end;
	ec->eptr = end;
	ec->endwin = 0;
	ec->nendbits = 0;
	ec->err = 0;
	ec->nbits = 9; /* EC_CODE_BITS + 1 - 3 x 8 */
	u32 rem = 0;
	if ((uintptr_t)buf < (uintptr_t)end)
		rem = *buf++;
	ec->ptr = buf;
	ec->rem = rem;
	ec->val = (u32)(127 - (rem >> 1)); /* val = rng - 1 - (rem >> 1) */
	ec->rng = 128;
	ec_norm(ec);
}

void ec_shrink(struct ec_dec *ec, u64 n)
{
	ec->storage -= n;
	ec->end -= n;
	ec->eptr = ec->end;
}

void ec_norm(struct ec_dec *ec)
{
	u32 r = (u32)ec->rng;
	if (r > 0x800000)
		return;
	u32 v = (u32)ec->val, rem = (u32)ec->rem;
	const u8 *p = ec->ptr, *end = ec->end;
	u64 nb = ec->nbits;
	do {
		nb += 8;
		r <<= 8;
		u32 t = rem << 8;
		rem = 0;
		if ((uintptr_t)p < (uintptr_t)end)
			rem = *p++;
		t |= rem;
		t >>= 1;
		t = ~t & 255;
		v = (t + (v << 8)) & 0x7FFFFFFF;
	} while (r <= 0x800000);
	ec->rng = r;
	ec->val = v;
	ec->rem = rem;
	ec->ptr = p;
	ec->nbits = nb;
}

u32 ec_decode(struct ec_dec *ec, u32 ft)
{
	u32 ext = ak_udivw((u32)ec->rng, ft);
	ec->ext = ext;
	u32 s = ak_udivw((u32)ec->val, ext) + 1;
	if (!(s < ft))
		s = ft;
	return ft - s;
}

u32 ec_decode_bin(struct ec_dec *ec, u32 bits)
{
	u32 ext = (u32)ec->rng >> (bits & 31);
	ec->ext = ext;
	u32 s = ak_udivw((u32)ec->val, ext) + 1;
	u32 m = 1u << (bits & 31);
	if (!(s < m))
		s = m;
	return m - s;
}

void ec_update(struct ec_dec *ec, u32 fl, u32 fh, u32 ft)
{
	u32 ext = (u32)ec->ext;
	u32 s = ext * (ft - fh);
	ec->val = (u32)((u32)ec->val - s);
	u32 r = (u32)ec->rng - s;
	if (fl)
		r = ext * (fh - fl);
	ec->rng = r;
	ec_norm(ec);
}

u32 ec_bit_logp(struct ec_dec *ec, u32 logp)
{
	u32 r = (u32)ec->rng, d = (u32)ec->val;
	u32 s = r >> (logp & 31);
	u32 ret = d < s;
	if (ret) {
		ec->rng = s;
	} else {
		ec->val = d - s;
		ec->rng = r - s;
	}
	ec_norm(ec);
	return ret;
}

u32 ec_icdf(struct ec_dec *ec, const u8 *icdf, u32 ftb)
{
	u32 s = (u32)ec->rng, d = (u32)ec->val;
	u32 r = s >> (ftb & 31);
	u32 ret = (u32)-1, t;
	do {
		t = s;
		ret++;
		s = r * icdf[ret];
	} while (d < s);
	ec->val = d - s;
	ec->rng = t - s;
	ec_norm(ec);
	return ret;
}

u32 ec_icdf16(struct ec_dec *ec, const u16 *icdf, u32 ftb)
{
	u32 s = (u32)ec->rng, d = (u32)ec->val;
	u32 r = s >> (ftb & 31);
	u32 ret = (u32)-1, t;
	do {
		t = s;
		ret++;
		s = r * icdf[ret];
	} while (d < s);
	ec->val = d - s;
	ec->rng = t - s;
	ec_norm(ec);
	return ret;
}

u32 ec_bits(struct ec_dec *ec, u32 bits)
{
	u32 w = (u32)ec->endwin, nb = (u32)ec->nendbits;
	if (nb < bits) {
		const u8 *p = ec->eptr, *buf = ec->buf;
		do {
			u32 b = 0;
			if ((uintptr_t)p > (uintptr_t)buf)
				b = *--p;
			w |= b << (nb & 31);
			nb += 8;
		} while ((s32)nb <= 24);
		ec->eptr = p;
	}
	u32 m = (1u << (bits & 31)) - 1;
	u32 ret = w & m;
	w >>= bits & 31;
	nb -= bits;
	ec->endwin = w;
	ec->nendbits = nb;
	ec->nbits += bits;
	return ret;
}

u32 ec_uint(struct ec_dec *ec, u32 ft)
{
	u32 ft1 = ft - 1;
	u32 ftb = 32 - ak_clz32(ft1);
	if (ftb <= 8) {
		u32 s = ec_decode(ec, ft1 + 1);
		ec_update(ec, s, s + 1, ft1 + 1);
		return s;
	}
	u32 b = ftb - 8;
	u32 t = (ft1 >> (b & 31)) + 1;
	u32 s = ec_decode(ec, t);
	ec_update(ec, s, s + 1, t);
	u32 v = (s << (b & 31)) | ec_bits(ec, b);
	if (v <= ft1)
		return v;
	ec->err = 1; /* trop grand : erreur */
	return ft1;
}

s64 ec_tell(const struct ec_dec *ec)
{
	return (s64)(ec->nbits - 32 + ak_clz32((u32)ec->rng));
}

static const u32 ec_corr[8] = {35733, 38967, 42495, 46340, 50535, 55109, 60097, 65535};

s64 ec_tell_frac(const struct ec_dec *ec)
{
	u64 nb = ec->nbits << 3;
	u32 r = (u32)ec->rng;
	u32 l = 32 - ak_clz32(r);
	r >>= (l - 16) & 31;
	u32 b = (r >> 12) - 8;
	if (r > ec_corr[b])
		b++;
	u32 x = b + (l << 3);
	return (s64)(nb - x);
}

s32 ec_laplace(struct ec_dec *ec, u32 fs, u32 decay)
{
	u32 fm = ec_decode_bin(ec, 15);
	u32 val = 0, fl = 0;
	if (fm >= fs) {
		val = 1;
		fl = fs;
		fs = (((32768 - 32) - fs) * (16384 - decay) >> 15) + 1;
		while (fs > 1 && fm >= fl + (fs << 1)) {
			fs <<= 1;
			fl += fs;
			fs = (((fs - 2) * decay) >> 15) + 1;
			val++;
		}
		if (fs <= 1) {
			u32 di = (fm - fl) >> 1;
			val += di;
			fl += di << 1;
		}
		if (fm < fl + fs)
			val = 0 - val;
		else
			fl += fs;
	}
	u32 fh = fl + fs;
	if (!(fh < 32768))
		fh = 32768;
	ec_update(ec, fl, fh, 32768);
	return (s32)val;
}

/* ---------------------------------------------------------------------------
 * État
 * ------------------------------------------------------------------------- */

struct ops *ops_state(struct akd *d)
{
	if (d->mode == 2)
		return d->codec;
	return (struct ops *)*og_field(d->codec, OPS_OG_STATE);
}

/* décodeur (mémoire), canaux (1, 2), IMDCT -> flux / < 0 (mémoire) */
static long od_new(struct akd *d, u64 cc, struct akmdct *md)
{
	struct od *od = akd_alloc(d, OD_SIZE);
	if (!od)
		return AKE_NOMEM;
	od->cc = cc;
	od->pcm[0] = akd_alloc(d, (OPS_MAXF + 64) * 4);
	if (!od->pcm[0])
		return AKE_NOMEM;
	od->pcm[1] = akd_alloc(d, (OPS_MAXF + 64) * 4);
	if (!od->pcm[1])
		return AKE_NOMEM;
	long r = celt_new(d, cc, md);
	if (r < 0)
		return r;
	od->ce = (struct celt *)r;
	r = silk_new(d, cc);
	if (r < 0)
		return r;
	od->sk = (struct silk *)r;
	return (long)od;
}

static void od_reset(struct od *od)
{
	od->prevmode = 0;
	od->prevred = 0;
	celt_reset(od->ce);
	silk_reset(od->sk);
}

/* décodeur, OpusHead (0 : AKD_CH déjà posé, famille 0), octets, ogg = 1 : dans
 * Ogg -> 0 / < 0 : état, flux, plans de sortie */
static long ops_setup(struct akd *d, const u8 *head, u64 n, u64 ogg)
{
	struct ops *st = akd_alloc(d, OPS_SIZE);
	if (!st)
		return AKE_NOMEM;
	u64 preskip = 0; /* avance */
	s32 gain = 0;	 /* gain (Q7.8 dB) */
	u64 ch;
	if (!head) {
		ch = (u64)d->ch;
		if (!ch)
			return AKE_CORRUPT;
		if (ch > 2)
			return AKE_FORMAT;
		goto fam0;
	}
	if (n < 19)
		return AKE_CORRUPT;
	if (ak_load64(head) != 0x646165487375704FULL) /* "OpusHead" */
		return AKE_CORRUPT;
	if (head[8] > 15) /* version : 0..15 (majeure 0) */
		return AKE_FORMAT;
	ch = head[9];
	if (!ch)
		return AKE_CORRUPT;
	preskip = ak_rd16le(head + 10);
	gain = (s16)ak_rd16le(head + 16);
	if (head[18]) /* famille (toute famille non nulle : comme la 1, comme l'asm) */
		goto fam1;
	if (ch > 2)
		return AKE_CORRUPT;
fam0:
	st->ch = ch;
	st->ns = 1;
	st->nc = ch - 1;
	st->map[0] = 0;
	st->map[1] = 1;
	goto streams;
fam1:
	if (ch > 8) /* (8 plans au plus) */
		return AKE_FORMAT;
	if (n < ch + 21)
		return AKE_CORRUPT;
	st->ch = ch;
	{
		u32 ns = head[19], nc = head[20];
		if (!ns || nc > ns)
			return AKE_CORRUPT;
		if (ns > 8)
			return AKE_FORMAT;
		st->ns = ns;
		st->nc = nc;
		u32 coded = ns + nc;
		for (u64 i = 0; i < ch; i++) {
			u32 m = head[21 + i];
			if (m != 255 && m >= coded)
				return AKE_CORRUPT;
			st->map[i] = (u8)m;
		}
	}
streams:
	/* IMDCT de CELT (N = 240 << k) */
	for (u64 k = 0; k < 4; k++) {
		long r = ak_imdct_init(d, &st->md[k], 240u << k);
		if (r < 0)
			return r;
	}
	/* les flux : couplés (stéréo) d'abord */
	for (u64 s = 0; s < st->ns; s++) {
		long r = od_new(d, (s < st->nc) + 1, st->md);
		if (r < 0)
			return r;
		st->od[s] = (struct od *)r;
	}
	/* gain de sortie : 10^(g / (20 x 256)) = 2^(g x log2(10) / 5120) */
	if (gain) {
		st->gain = ce_exp2((float)gain * ak_u2f(0x3A2A152D));
		st->hasgain = 1;
	}
	d->fmt = AKF_OPUS;
	d->rate = 48000;
	d->ch = (s64)st->ch;
	d->pcmfmt = AKD_F32;
	long r = akd_planes(d, OPS_MAXF, st->ch);
	if (r < 0)
		return r;
	if (ogg) { /* Ogg : avance, pré-roll, rognage */
		void *og = d->codec;
		*og_field(og, OPS_OG_STATE) = (u64)st;
		*og_field(og, OPS_OG_GRANOFF) = preskip;
		*og_field(og, OPS_OG_PREROLL) = 3840;
		d->skip = (s64)preskip;
	} else {
		d->codec = st;
	}
	return 0;
}

/* ---------------------------------------------------------------------------
 * Paquets (RFC 6716 3)
 * ------------------------------------------------------------------------- */

/* octet de TOC -> échantillons par trame (48 kHz), *mode (OPM_*), *bw (0 NB .. 4 FB) */
static u64 op_toc(u32 toc, u64 *mode, u64 *bw)
{
	u32 cfg = toc >> 3;
	if (cfg < 12) { /* SILK : 10, 20, 40, 60 ms */
		u32 a = cfg & 3;
		*mode = OPM_SILK;
		*bw = cfg >> 2;
		return a == 1 ? 960 : a == 2 ? 1920 : a == 3 ? 2880 : 480;
	}
	if (cfg < 16) { /* hybride : 10, 20 ms ; SWB, FB */
		*mode = OPM_HYBRID;
		*bw = ((cfg >> 1) & 1) + 3;
		return cfg & 1 ? 960 : 480;
	}
	*mode = OPM_CELT; /* CELT : 2,5, 5, 10, 20 ms */
	u32 b = (cfg - 16) >> 2;
	if (b)
		b++; /* (pas de MB en CELT) */
	*bw = b;
	return (u64)120 << (cfg & 3);
}

/* taille codée (1 ou 2 octets), fin -> taille ; *used = octets lus (0 : données manquantes) */
static u64 op_size(const u8 *p, const u8 *end, u64 *used)
{
	if ((uintptr_t)p >= (uintptr_t)end)
		goto bad;
	u32 b = p[0];
	if (b < 252) {
		*used = 1;
		return b;
	}
	if ((uintptr_t)(p + 1) >= (uintptr_t)end)
		goto bad;
	*used = 2;
	return b + ((u32)p[1] << 2);
bad:
	*used = 0;
	return (u64)-1;
}

/* tailles (16 bits, comme l'asm : une taille au-delà de 65535 est tronquée) et débuts */
struct op_frames {
	u16 size[48];
	const u8 *start[48];
};

/* paquet, octets, 1 : auto-délimité (RFC 6716 annexe B), trames -> trames
 * (< 0 : invalide) ; *used = octets du paquet (avec le bourrage) */
static long op_parse(const u8 *pkt, u64 n, u64 selfdelim, struct op_frames *f, u64 *used)
{
	const u8 *end = pkt + n;
	u64 m, pad = 0, k;
	*used = 0;
	if (!n)
		return AKE_CORRUPT;
	u32 toc = pkt[0];
	const u8 *p = pkt + 1;
	u64 mode, bw;
	u64 fs = op_toc(toc, &mode, &bw);
	u32 code = toc & 3;
	if (code == 0) {
		m = 1;
		if (selfdelim) {
			u64 sz = op_size(p, end, &k);
			if (!k)
				return AKE_CORRUPT;
			p += k;
			f->size[0] = (u16)sz;
		} else {
			f->size[0] = (u16)(end - p);
		}
	} else if (code == 1) {
		m = 2;
		if (selfdelim) {
			u64 sz = op_size(p, end, &k);
			if (!k)
				return AKE_CORRUPT;
			p += k;
			f->size[0] = (u16)sz;
			f->size[1] = (u16)sz;
		} else {
			u64 r = (u64)(end - p);
			if (r & 1)
				return AKE_CORRUPT;
			f->size[0] = (u16)(r >> 1);
			f->size[1] = (u16)(r >> 1);
		}
	} else if (code == 2) {
		m = 2;
		u64 sz = op_size(p, end, &k);
		if (!k)
			return AKE_CORRUPT;
		p += k;
		f->size[0] = (u16)sz;
		if (selfdelim) {
			sz = op_size(p, end, &k);
			if (!k)
				return AKE_CORRUPT;
			p += k;
			f->size[1] = (u16)sz;
		} else {
			s64 r = (s64)(end - p) - (s64)f->size[0];
			if (r < 0)
				return AKE_CORRUPT;
			f->size[1] = (u16)r;
		}
	} else {
		/* code 3 : nombre de trames, bourrage, VBR */
		if ((uintptr_t)p >= (uintptr_t)end)
			return AKE_CORRUPT;
		u32 b = *p++;
		m = b & 63;
		if (!m || m * fs > OPS_MAXF)
			return AKE_CORRUPT;
		if (b & 0x40) {
			for (;;) { /* bourrage : 255 -> 254 et suite */
				if ((uintptr_t)p >= (uintptr_t)end)
					return AKE_CORRUPT;
				u32 v = *p++;
				u64 t = v == 255 ? 254 : v;
				pad += t;
				end -= t;
				if ((uintptr_t)end < (uintptr_t)p)
					return AKE_CORRUPT;
				if (p[-1] != 255)
					break;
			}
		}
		if (b & 0x80) { /* VBR : M - 1 tailles (et la dernière si auto-délimité) */
			u64 i;
			for (i = 0; i < m - 1; i++) {
				u64 sz = op_size(p, end, &k);
				if (!k)
					return AKE_CORRUPT;
				p += k;
				f->size[i] = (u16)sz;
			}
			if (selfdelim) {
				u64 sz = op_size(p, end, &k);
				if (!k)
					return AKE_CORRUPT;
				p += k;
				f->size[i] = (u16)sz;
			} else { /* la dernière : le reste */
				u64 sum = 0;
				for (u64 j = 0; j < i; j++)
					sum += f->size[j];
				s64 r = (s64)((u64)(end - p) - sum);
				if (r < 0)
					return AKE_CORRUPT;
				f->size[i] = (u16)r;
			}
		} else {
			u64 sz;
			if (selfdelim) {
				sz = op_size(p, end, &k);
				if (!k)
					return AKE_CORRUPT;
				p += k;
			} else {
				u64 r = (u64)(end - p);
				sz = r / m;
				if (r - sz * m)
					return AKE_CORRUPT;
			}
			for (u64 i = 0; i < m; i++)
				f->size[i] = (u16)sz;
		}
	}
	/* débuts des trames ; tailles <= 1275, dans le paquet */
	for (u64 i = 0; i < m; i++) {
		u32 s = f->size[i];
		if (s > 1275)
			return AKE_CORRUPT;
		f->start[i] = p;
		p += s;
		if ((uintptr_t)p > (uintptr_t)end)
			return AKE_CORRUPT;
	}
	*used = (u64)(p - pkt) + pad;
	return (long)m;
}

static const u8 od_silence[2] = {0xFF, 0xFF};
static const u8 od_endband[8] = {13, 17, 17, 19, 21, 0, 0, 0};

/* trame CELT dans la sortie de la trame : octets, N, 1 : ajout, décodeur de plage */
static long od_celt(struct od *od, float *o0, float *o1, u64 len, u64 n, u64 accum, struct ec_dec *ec)
{
	struct celt *ce = od->ce;
	ce->out[0] = o0;
	ce->out[1] = o1;
	ce->accum = accum;
	return celt_decode(ce, len, n, od->sc, ec);
}

/* trame redondante (5 ms ; len = fin de la trame principale, rb = octets) -> od->tr */
static long od_red(struct od *od, const u8 *frame, u64 len, u64 rb)
{
	struct celt *ce = od->ce;
	ec_init(&od->ec2, frame + len, rb);
	ce->out[0] = od->tr[0];
	ce->out[1] = od->tr[1];
	ce->accum = 0;
	return celt_decode(ce, rb, 240, od->sc, &od->ec2);
}

/* début dans la trame, 120 ; dir = 0 : pcm = fondu(pcm, red[120 ..]), 1 : pcm =
 * fondu(red[120 ..], pcm) (smooth_fade : w^2 vers le second) */
static void od_fade(struct od *od, float *o0, float *o1, u64 at, u64 n, u64 dir)
{
	const float *w = ce_window;
	for (u64 c = 0; c < od->cc; c++) {
		float *p = (c == 0 ? o0 : o1) + at;
		const float *red = &od->tr[c][120];
		u64 i = 0;
		do {
			float wv = w[i] * w[i];
			float om = 1.0f - wv;
			float pc = p[i], r = red[i];
			float o;
			if (!dir) {
				r = r * wv; /* w red + (1 - w) pcm */
				o = ak_fmadd(om, pc, r);
			} else {
				pc = pc * wv; /* w pcm + (1 - w) red */
				o = ak_fmadd(om, r, pc);
			}
			p[i] = o;
			i++;
		} while (i < n);
	}
}

/* flux, trame, octets, position dans od->pcm (trames) -> trames (od->fs) / < 0
 * (opus_decode_frame : SILK, hybride, CELT ; trames redondantes CELT des
 * changements de mode) */
static long od_frame(struct od *od, const u8 *frame, u64 len, u64 pos)
{
	u64 fs = od->fs;
	struct celt *ce = od->ce;
	float *o0 = od->pcm[0] + pos, *o1 = od->pcm[1] + pos;
	long r;
	if (len <= 1) { /* trame vide (DTX) : silence de la durée de la trame */
		for (u64 i = 0; i < fs; i++) {
			o0[i] = 0;
			o1[i] = 0;
		}
		return (long)fs;
	}
	ec_init(&od->ec, frame, len);
	ce->end = od_endband[od->bw]; /* bande de fin de CELT */
	if (od->mode == OPM_CELT) {
		/* CELT seul : état CELT oublié après un autre mode (sans redondance) */
		if (od->prevmode && od->prevmode != OPM_CELT && !od->prevred)
			celt_reset(ce);
		ce->start = 0;
		r = od_celt(od, o0, o1, len, fs, 0, &od->ec);
		if (r < 0)
			return r;
		od->prevmode = OPM_CELT;
		od->prevred = 0;
		return (long)fs;
	}
	/* SILK (10 à 60 ms), après CELT : remise à zéro */
	if (od->prevmode == OPM_CELT)
		silk_reset(od->sk);
	u64 ms = fs / 48; /* durée du paquet (ms), 10 au moins */
	if (ms < 10)
		ms = 10;
	u64 khz = 16; /* fréquence interne */
	if (od->mode == OPM_SILK)
		khz = od->bw < 1 ? 8 : od->bw == 1 ? 12 : 16;
	u64 done = 0;
	do {
		r = silk_decode(od->sk, &od->ec, (u32)od->sc, (u32)khz, (u32)ms, done == 0, o0 + done, o1 + done);
		if (r < 0)
			return r;
		if (!r)
			return AKE_CORRUPT;
		done += (u64)r;
	} while (done < fs);
	/* trame redondante CELT ? */
	u64 red = 0, c2s = 0, rb = 0;
	s64 t = ec_tell(&od->ec) + 17;
	if (od->mode == OPM_HYBRID)
		t += 20;
	if (t <= (s64)(len << 3)) {
		red = 1;
		if (od->mode == OPM_HYBRID)
			red = ec_bit_logp(&od->ec, 12);
		if (red) {
			c2s = ec_bit_logp(&od->ec, 1);
			if (od->mode == OPM_HYBRID)
				rb = (u64)ec_uint(&od->ec, 256) + 2;
			else
				rb = len - (((u64)ec_tell(&od->ec) + 7) >> 3);
			len -= rb;
			/* comparaison non signée, comme l'asm (len « négatif » : rien n'est annulé) */
			if ((u64)ec_tell(&od->ec) > (len << 3)) {
				len = 0;
				rb = 0;
				red = 0;
			}
			ec_shrink(&od->ec, rb);
		}
	}
	/* redondance CELT -> SILK : décodée avant la trame */
	if (red && c2s) {
		ce->start = 0;
		r = od_red(od, frame, len, rb);
		if (r < 0)
			return r;
	}
	if (od->mode == OPM_HYBRID) {
		/* hybride : CELT à partir de la bande 17, ajouté à SILK */
		ce->start = 17;
		if (od->prevmode && od->prevmode != OPM_HYBRID && !od->prevred)
			celt_reset(ce);
		r = od_celt(od, o0, o1, len, fs < 960 ? fs : 960, 1, &od->ec);
		if (r < 0)
			return r;
	} else if (od->prevmode == OPM_HYBRID && !(red && c2s && od->prevred)) {
		/* SILK seul après hybride : CELT finit son recouvrement (trame de
		 * silence de 2,5 ms) */
		ce->start = 0;
		ec_init(&od->ec2, od_silence, 2);
		r = od_celt(od, o0, o1, 2, 120, 1, &od->ec2);
		if (r < 0)
			return r;
	}
	if (red && !c2s) {
		/* redondance SILK -> CELT : décodée après, fondu sur la fin */
		celt_reset(ce);
		ce->start = 0;
		r = od_red(od, frame, len, rb);
		if (r < 0)
			return r;
		od_fade(od, o0, o1, fs - 120, 120, 0); /* pcm[F - 120 ..] = fondu(pcm, red[120 ..]) */
	} else if (red && c2s && (od->prevmode != OPM_SILK || od->prevred)) {
		/* redondance CELT -> SILK : son début remplace celui de la trame */
		for (u64 i = 0; i < 120; i++) { /* pcm[0 .. 120) = red[0 .. 120) */
			o0[i] = od->tr[0][i];
			o1[i] = od->tr[1][i];
		}
		od_fade(od, o0, o1, 120, 120, 1); /* pcm[120 .. 240) = fondu(red, pcm) */
	}
	od->prevmode = od->mode;
	od->prevred = red && !c2s;
	return (long)fs;
}

/* flux, paquet, octets, 1 : auto-délimité -> trames rendues dans od->pcm (< 0 :
 * erreur) ; *used = octets consommés */
static long od_packet(struct od *od, const u8 *pkt, u64 n, u64 selfdelim, u64 *used)
{
	struct op_frames f;
	long m = op_parse(pkt, n, selfdelim, &f, used);
	if (m < 0)
		return m;
	u64 mode, bw;
	od->fs = op_toc(pkt[0], &mode, &bw);
	od->mode = mode;
	od->bw = bw;
	od->sc = ((pkt[0] >> 2) & 1) + 1;
	u64 total = 0;
	for (long i = 0; i < m; i++) {
		long r = od_frame(od, f.start[i], f.size[i], total);
		if (r < 0)
			return r;
		total += (u64)r;
	}
	return (long)total;
}

/* ---------------------------------------------------------------------------
 * Ogg Opus et API par paquets
 * ------------------------------------------------------------------------- */

long ops_head(struct akd *d, const u8 *head, u64 n)
{
	return ops_setup(d, head, n, 1);
}

void ops_tags(struct akd *d, const u8 *p, u64 n)
{
	if (n < 8 || ak_load64(p) != 0x736761547375704FULL) /* "OpusTags" */
		return;
	akd_vcomment(d, p + 8, n - 8);
}

long ops_packet(struct akd *d, const u8 *pkt, u64 n)
{
	struct ops *st = ops_state(d);
	const u8 *p = pkt, *end = pkt + n;
	if (!n)
		return AKE_CORRUPT;
	/* les flux : tous auto-délimités sauf le dernier */
	s64 frames = -1;
	for (u64 s = 0; s < st->ns; s++) {
		u64 left = (u64)(end - p);
		if (!left)
			return AKE_CORRUPT;
		u64 used;
		long r = od_packet(st->od[s], p, left, s < st->ns - 1, &used);
		if (r < 0)
			return r;
		p += used;
		if (frames != -1 && r != frames)
			return AKE_CORRUPT;
		frames = r;
	}
	/* table des canaux, gain */
	for (u64 c = 0; c < st->ch; c++) {
		u32 m = st->map[c];
		float *dst = d->pcm[c];
		if (m == 255) {
			for (s64 i = 0; i < frames; i++)
				dst[i] = 0;
			st->clip[c] = 0;
			continue;
		}
		u64 sn, sub;
		if (m < (st->nc << 1)) { /* flux couplé, canal (bit 0) */
			sn = m >> 1;
			sub = m & 1;
		} else {
			sn = m - st->nc;
			sub = 0;
		}
		const float *src = st->od[sn]->pcm[sub];
		float g = st->gain;
		for (s64 i = 0; i < frames; i++) {
			float v = src[i];
			if (st->hasgain)
				v = v * g;
			dst[i] = v;
		}
		ops_softclip(dst, (u32)frames, &st->clip[c]);
	}
	return frames;
}

long ops_init_pkt(struct akd *d, const u8 *extra, u64 n)
{
	d->codec = 0;
	long r = ops_setup(d, extra, n, 0);
	if (r < 0)
		return r;
	d->pktfn = ops_packet;
	d->resetfn = ops_reset;
	return 0;
}

void ops_reset(struct akd *d)
{
	struct ops *st = ops_state(d);
	for (int i = 0; i < 8; i++)
		st->clip[i] = 0;
	for (u64 s = 0; s < st->ns; s++)
		od_reset(st->od[s]);
}

/* écrêtage doux (comme opus_pcm_soft_clip de libopus : hors de la norme, il
 * évite d'écrêter net la sortie entière) : entre deux passages par zéro autour
 * d'un pic au-delà de 1, x + a x^2 ramène le pic à 1. Les comparaisons gardent
 * le sens de l'asm avec un NaN (b.lt vrai, b.gt / b.ge faux). */
void ops_softclip(float *x, u32 n, float *mem)
{
	if (!n)
		return;
	for (u32 i = 0; i < n; i++)
		x[i] = ak_fmax(ak_fmin(x[i], 2.0f), -2.0f);
	float a = *mem;
	u32 i = 0; /* suite de la trame précédente */
	while (i < n) {
		float v = x[i];
		float t = v * a;
		if (t >= 0.0f)
			break;
		t = t * v;
		x[i] = v + t;
		i++;
	}
	u32 curr = 0;
	float x0 = x[0];
	for (;;) {
		u32 k = curr; /* premier échantillon hors de [-1, 1] */
		float v = 0;
		for (; k < n; k++) {
			v = x[k];
			if (v > 1.0f || !(v >= -1.0f))
				break;
		}
		if (k >= n) {
			a = 0;
			break;
		}
		u32 peak = k, start = k, end = k;
		float maxval = ak_fabs(v);
		while (start) {
			float t = x[start - 1] * v;
			if (!(t >= 0.0f))
				break;
			start--;
		}
		while (end < n) {
			float y = x[end];
			float t = y * v;
			if (!(t >= 0.0f))
				break;
			y = ak_fabs(y);
			if (y > maxval) {
				maxval = y;
				peak = end;
			}
			end++;
		}
		/* écart voulu : un NaN (pris pour un pic, b.lt) arrête aussitôt la
		 * recherche de la fin et l'asm reboucle sans fin sur lui ; ici on
		 * passe l'échantillon (sans NaN, end > peak toujours) */
		if (end == k)
			end = k + 1;
		u32 special = 0; /* écrêtage avant le premier zéro */
		if (!start)
			special = x[0] * v >= 0.0f;
		a = (maxval - 1.0f) / (maxval * maxval); /* a = (maxval - 1) / maxval^2 */
		a = ak_fmadd(a, ak_u2f(0x3480D959), a); /* 2,4e-7 */
		if (v > 0.0f)
			a = ak_fneg(a);
		for (u32 j = start; j < end; j++) {
			float y = x[j];
			x[j] = ak_fmadd(a, y * y, y);
		}
		if (special && peak >= 2) { /* rampe du premier échantillon au pic */
			float off = x0 - x[0];
			float delta = off / (float)peak;
			for (u32 j = curr; j < peak; j++) {
				off = off - delta;
				x[j] = ak_fmax(ak_fmin(x[j] + off, 1.0f), -1.0f);
			}
		}
		curr = end;
		if (curr >= n)
			break;
	}
	*mem = a;
}
