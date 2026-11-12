/* AudioKit : encodeur Opus (RFC 6716) en mode CELT seul, dans Ogg Opus (RFC
 * 7845) (ak_opusenc.c, d'après ak_opusenc.inc ; mêmes octets au bit près).
 *
 * Trames CELT de 20 ms (TOC : configuration CELT 20 ms, code 0), débit
 * constant (paquets de débit / 400 octets). Largeur de bande d'après le débit
 * par voie. Une trame : pré-accentuation, MDCT (fenêtre de recouvrement de
 * 120, FFT de 480 points d'ak_mdct.c), énergies des bandes, codeur de plage ;
 * silence, post-filtre, transitoire à 0 ; intra à la première trame ;
 * énergies grossières, tf_res à 0, étalement normal, dynalloc (analyse
 * simplifiée de libopus), trim, allocation (le calcul du décodeur), énergies
 * fines, bandes (division récursive, theta, PVQ, rotation d'étalement, CWRS),
 * énergies finales. Retard : 120 échantillons (pré-skip). Hors de 48 kHz :
 * rééchantillonneur polyphase à sinus cardinal fenêtré (Blackman).
 *
 * L'asm garde l'état dans x19 et le codeur de plage dans x20, et compte sur
 * le fait que ses fonctions ne touchent que certains registres : en C, les
 * valeurs sont dans des variables. L'état garde la disposition de l'asm
 * (OE_*, OEC_*). */
#include "ak_enc.h"
#include "ak_celt.h"
#include "ak_mdct.h"

#define OE_N 960 /* échantillons d'une trame (20 ms, LM = 3) */
#define OE_LM 3
#define OE_PCMN (OE_N + 120) /* tampon d'une voie : recouvrement + trame */
#define OE_PAGEPK 50	     /* paquets par page au plus (1 s) */
#define OE_MINRATE 16000
#define OE_MAXRATE 256000
#define OE_DEFRATE 96000
#define OE_SERIAL 0x41524D50
#define OE_RSZ 32      /* rééchantillonneur : demi-largeur (passages par zéro) */
#define OE_RSR 128     /* pas de la table par passage */
#define OE_RSHB 8192   /* historique d'une voie (flottants) */
#define OE_RSWTN 32768 /* table des poids : phases x prises au plus */

/* codeur de plage (OEC_* ; nbits et rng aux places du décodeur : EC_NBITS,
 * EC_RNG, que lisent ec_tell et ec_tell_frac) */
struct oec {
	u8 *buf;      /* 0 */
	u64 storage;  /* 8 */
	u64 offs;     /* 16 octets écrits par l'avant */
	u64 endoffs;  /* 24 octets écrits par la fin */
	u64 win;      /* 32 bits bruts en attente */
	u64 nend;     /* 40 */
	u64 r48;      /* 48 */
	u64 nbits;    /* 56 */
	u64 rng;      /* 64 (32 bits utiles) */
	u64 val;      /* 72 bas de l'intervalle */
	u64 ext;      /* 80 octets 255 en attente (retenue) */
	s64 rem;      /* 88 octet en attente (-1 : aucun) */
	u64 err;      /* 96 */
	u64 pad;      /* 104 */
};
#define OEC_SIZE 112
_Static_assert(sizeof(struct oec) == OEC_SIZE, "oec");
AK_STATIC_OFF(struct oec, nbits, 56);
AK_STATIC_OFF(struct oec, rng, 64);
AK_STATIC_OFF(struct oec, val, 72);
AK_STATIC_OFF(struct oec, err, 96);

/* état (OE_*) */
struct oe {
	u64 c;		   /* 0 voies (1, 2) */
	u64 rate;	   /* 8 fréquence d'entrée */
	u64 nbytes;	   /* 16 octets CELT d'une trame (paquet - TOC) */
	u64 end;	   /* 24 bande de fin */
	u64 toc;	   /* 32 */
	u64 itgt;	   /* 40 bande d'intensité visée */
	u64 preskip;	   /* 48 */
	u64 fill;	   /* 56 échantillons (48 kHz) de la trame en cours */
	s64 len48;	   /* 64 échantillons utiles à 48 kHz */
	u64 pkts;	   /* 72 paquets audio */
	u64 seq;	   /* 80 numéro de la prochaine page */
	u64 npkt;	   /* 88 page en cours : paquets, valeurs de laçage, octets */
	u64 nseg;	   /* 96 */
	u64 dlen;	   /* 104 */
	s64 lastcoded;	   /* 112 bandes codées de la trame précédente */
	u64 frames;	   /* 120 */
	s64 intens;	   /* 128 */
	s64 coded;	   /* 136 */
	s64 balance;	   /* 144 */
	u64 i;		   /* 152 bande courante */
	s64 rem;	   /* 160 remaining_bits */
	u64 trim;	   /* 168 */
	u64 intra;	   /* 176 */
	struct akn *enc;   /* 184 encodeur générique (akn_out) */
	u64 nin;	   /* 192 trames d'entrée reçues */
	s64 rsip;	   /* 200 rééchantillonneur : position de la sortie */
	u64 rsfrac;	   /* 208 reste sur 48000 */
	s64 rsbase;	   /* 216 indice absolu de H[0] */
	s64 rscnt;	   /* 224 échantillons dans H */
	s64 rsti;	   /* 232 demi-largeur */
	s64 rslim;	   /* 240 sorties maximales */
	float rsstep;	   /* 248 pas de la table */
	float rsgain;	   /* 252 gain (2 fc) */
	float pre[2];	   /* 256 pré-accentuation : mémoire */
	s64 tot8;	   /* 264 total (1/8 bits) après le dynalloc */
	struct oec ec;	   /* 272 codeur de plage */
	struct akmdct md;  /* 384 plan de la MDCT (N = 1920) */
	u64 dual;	   /* 608 */
	s64 sigbw;	   /* 616 dernière bande où il y a du signal */
	u64 rsg;	   /* 624 pgcd(fs, 48000) ; 0 : poids sans table */
	u64 pad632;
	float bande[48];   /* 640 amplitudes des bandes (2 x 21) */
	float loge[48];	   /* 832 log2 (moins eMeans) */
	float olde[48];	   /* 1024 énergies du décodeur */
	float err[48];	   /* 1216 erreur de quantification */
	float fol[48];	   /* 1408 dynalloc : suiveuse */
	s32 cap[24];	   /* 1600 tableaux de 21 entiers */
	s32 offs[24];	   /* 1696 */
	s32 pulses[24];	   /* 1792 */
	s32 fineq[24];	   /* 1888 */
	s32 prio[24];	   /* 1984 */
	s32 thresh[24];	   /* 2080 */
	s32 trimoff[24];   /* 2176 */
	s32 bits1[24];	   /* 2272 */
	s32 bits2[24];	   /* 2368 */
	s32 want[24];	   /* 2464 dynalloc : quanta voulus */
	float pcm[2][OE_PCMN]; /* 2560 tampons des voies */
	float freq[2][960];    /* 11200 spectres */
	float x[2][960];       /* 18880 spectres normalisés */
	float v[1920];	       /* 26560 MDCT : trame virtuelle */
	u32 ut[2048];	       /* 34240 table U(n, k) de CWRS */
	s32 iy[256];	       /* 42432 impulsions */
	float px[256];	       /* 43456 PVQ : |X| */
	float py[256];	       /* 44480 PVQ : 2 y */
	u8 pkt[1280];	       /* 45504 paquet (TOC + 1275) */
	u32 crc[256];	       /* 46784 table du CRC d'Ogg */
	u8 page[288];	       /* 47808 en-tête de page et laçage */
	u8 pdata[65040];       /* 48096 données de la page */
	float rsgk[OE_RSZ * OE_RSR + 4]; /* 113136 noyau du rééchantillonneur */
	float rsw[512];			 /* 129536 poids d'une sortie */
	float rsh[2][OE_RSHB];		 /* 131584 historique */
	float rswt[OE_RSWTN];		 /* 197120 poids de chaque phase */
};
#define OE_SIZE 328192
_Static_assert(sizeof(struct oe) == OE_SIZE, "oe");
AK_STATIC_OFF(struct oe, enc, 184);
AK_STATIC_OFF(struct oe, rsstep, 248);
AK_STATIC_OFF(struct oe, pre, 256);
AK_STATIC_OFF(struct oe, ec, 272);
AK_STATIC_OFF(struct oe, md, 384);
AK_STATIC_OFF(struct oe, dual, 608);
AK_STATIC_OFF(struct oe, rsg, 624);
AK_STATIC_OFF(struct oe, bande, 640);
AK_STATIC_OFF(struct oe, loge, 832);
AK_STATIC_OFF(struct oe, olde, 1024);
AK_STATIC_OFF(struct oe, err, 1216);
AK_STATIC_OFF(struct oe, fol, 1408);
AK_STATIC_OFF(struct oe, cap, 1600);
AK_STATIC_OFF(struct oe, want, 2464);
AK_STATIC_OFF(struct oe, pcm, 2560);
AK_STATIC_OFF(struct oe, freq, 11200);
AK_STATIC_OFF(struct oe, x, 18880);
AK_STATIC_OFF(struct oe, v, 26560);
AK_STATIC_OFF(struct oe, ut, 34240);
AK_STATIC_OFF(struct oe, iy, 42432);
AK_STATIC_OFF(struct oe, px, 43456);
AK_STATIC_OFF(struct oe, py, 44480);
AK_STATIC_OFF(struct oe, pkt, 45504);
AK_STATIC_OFF(struct oe, crc, 46784);
AK_STATIC_OFF(struct oe, page, 47808);
AK_STATIC_OFF(struct oe, pdata, 48096);
AK_STATIC_OFF(struct oe, rsgk, 113136);
AK_STATIC_OFF(struct oe, rsw, 129536);
AK_STATIC_OFF(struct oe, rsh, 131584);
AK_STATIC_OFF(struct oe, rswt, 197120);

static const u8 ope_head[19] = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static const u8 ope_tags[30] = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's', 14,  0,	  0,   0,   'K', 'o', 'r',
				'i', 'n', ' ', 'A', 'u', 'd', 'i', 'o', 'K', 'i', 't', 0,   0,	 0,   0};
static const u16 ope_ithr[21] = {1, 2, 3, 4, 5, 6, 7, 8, 16, 24, 36, 44, 50, 56, 62, 67, 72, 79, 88, 106, 134};

static void frame(struct oe *o);

/* ---------------------------------------------------------------------------
 * Ogg (RFC 3533, 7845)
 * ------------------------------------------------------------------------- */

/* petit-boutiste, octet par octet */
static void putle(u8 *p, u64 v, u64 n)
{
	while (n) {
		*p++ = (u8)v;
		v >>= 8;
		n--;
	}
}

static u32 crc_upd(const struct oe *o, u32 crc, const u8 *p, u64 n)
{
	while (n) {
		u32 b = *p++;
		crc = o->crc[b ^ (crc >> 24)] ^ (crc << 8);
		n--;
	}
	return crc;
}

/* granule, drapeaux (2 début, 4 fin) : écrit la page en cours */
static void page_flush(struct oe *o, u64 granule, u64 flags)
{
	u8 *h = o->page;
	putle(h, 0x5367674F, 4); /* 'OggS' */
	h[4] = 0;
	h[5] = (u8)flags;
	putle(h + 6, granule, 8);
	putle(h + 14, OE_SERIAL, 4);
	putle(h + 18, o->seq, 4);
	putle(h + 22, 0, 4);
	h[26] = (u8)o->nseg;
	u32 crc = crc_upd(o, 0, h, o->nseg + 27); /* CRC de l'en-tête (CRC à 0) et des données */
	crc = crc_upd(o, crc, o->pdata, o->dlen);
	putle(h + 22, crc, 4);
	akn_out(o->enc, h, o->nseg + 27);
	akn_out(o->enc, o->pdata, o->dlen);
	o->seq++;
	o->nseg = 0;
	o->npkt = 0;
	o->dlen = 0;
}

/* octets, longueur : ajoute un paquet à la page (une page pleine part d'abord) */
static void page_add(struct oe *o, const u8 *p, u64 n)
{
	u64 segs = n / 255 + 1; /* valeurs de laçage */
	if (o->nseg + segs > 255 || o->npkt >= OE_PAGEPK)
		page_flush(o, o->pkts * OE_N, 0); /* granule sans le pré-skip, comme l'asm */
	u8 *lace = o->page + 27;
	u64 k = o->nseg, r = n;
	while (r >= 255) {
		lace[k++] = 255;
		r -= 255;
	}
	lace[k++] = (u8)r;
	o->nseg = k;
	u8 *d = o->pdata + o->dlen;
	o->dlen += n;
	memmove(d, p, n);
	o->npkt++;
}

/* ---------------------------------------------------------------------------
 * Codeur de plage (RFC 6716 5.1)
 * ------------------------------------------------------------------------- */

static void oec_init(struct oec *ec, u8 *buf, u64 n)
{
	ec->buf = buf;
	ec->storage = n;
	ec->offs = 0;
	ec->endoffs = 0;
	ec->win = 0;
	ec->nend = 0;
	ec->nbits = 33; /* EC_CODE_BITS + 1 */
	ec->rng = 0x80000000u;
	ec->val = 0;
	ec->ext = 0;
	ec->rem = -1;
	ec->err = 0;
}

static s64 oec_tell(const struct oec *ec)
{
	return (s64)(ec->nbits - 32 + ak_clz32((u32)ec->rng));
}

static const u32 ec_corr[8] = {35733, 38967, 42495, 46340, 50535, 55109, 60097, 65535};

static s64 oec_tell_frac(const struct oec *ec)
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

/* octet écrit par l'avant */
static void oec_wb(struct oec *ec, u32 b)
{
	if (ec->offs + ec->endoffs >= ec->storage) {
		ec->err = 1;
		return;
	}
	ec->buf[ec->offs] = (u8)b;
	ec->offs++;
}

/* octet écrit par la fin */
static void oec_wbe(struct oec *ec, u32 b)
{
	if (ec->offs + ec->endoffs >= ec->storage) {
		ec->err = 1;
		return;
	}
	ec->endoffs++;
	ec->buf[ec->storage - ec->endoffs] = (u8)b;
}

/* symbole (9 bits : retenue en bit 8) (ec_enc_carry_out) */
static void oec_carry(struct oec *ec, u32 c)
{
	if (c == 255) {
		ec->ext++;
		return;
	}
	u32 cy = c >> 8; /* retenue */
	if (ec->rem >= 0)
		oec_wb(ec, (u32)ec->rem + cy);
	if (ec->ext) {
		u64 n = ec->ext;
		do
			oec_wb(ec, cy + 255);
		while (--n);
		ec->ext = 0;
	}
	ec->rem = c & 255;
}

/* renormalise (rng > 2^23) */
static void oec_norm(struct oec *ec)
{
	while ((u32)ec->rng <= 0x800000u) {
		oec_carry(ec, (u32)(ec->val >> 23));
		ec->val = (ec->val << 8) & 0x7FFFFFFF;
		ec->rng = (u32)((u32)ec->rng << 8);
		ec->nbits += 8;
	}
}

static void oec_enc_r(struct oec *ec, u32 fl, u32 fh, u32 ft, u32 rng, u32 r)
{
	if (fl) {
		ec->val += (u32)(rng - r * (ft - fl));
		rng = r * (fh - fl);
	} else
		rng = rng - r * (ft - fh);
	ec->rng = rng;
	oec_norm(ec);
}

/* fl, fh, ft (ec_encode) */
static void oec_encode(struct oec *ec, u32 fl, u32 fh, u32 ft)
{
	u32 rng = (u32)ec->rng;
	oec_enc_r(ec, fl, fh, ft, rng, ak_udivw(rng, ft));
}

/* fl, fh, bits (ec_encode_bin) */
static void oec_encode_bin(struct oec *ec, u32 fl, u32 fh, u32 bits)
{
	u32 rng = (u32)ec->rng;
	oec_enc_r(ec, fl, fh, 1u << (bits & 31), rng, rng >> (bits & 31));
}

/* bit, logp (probabilité de 1 : 1/2^logp) */
static void oec_bit_logp(struct oec *ec, u32 bit, u32 logp)
{
	u32 rng = (u32)ec->rng;
	u32 s = rng >> (logp & 31);
	u32 r = rng - s;
	if (bit) {
		ec->val += r;
		r = s;
	}
	ec->rng = r;
	oec_norm(ec);
}

/* symbole, table icdf (octets), ftb */
static void oec_icdf(struct oec *ec, u32 s, const u8 *icdf, u32 ftb)
{
	u32 rng = (u32)ec->rng;
	u32 r = rng >> (ftb & 31);
	if (s) {
		u32 a = icdf[s - 1];
		ec->val += (u32)(rng - r * a);
		rng = r * (a - icdf[s]);
	} else
		rng = rng - r * icdf[0];
	ec->rng = rng;
	oec_norm(ec);
}

/* valeur, bits (<= 25) : bits bruts par la fin */
static void oec_bits(struct oec *ec, u32 v, u32 bits)
{
	u64 win = ec->win, nend = ec->nend;
	if (nend + bits > 32) {
		do {
			oec_wbe(ec, (u32)win & 255);
			win >>= 8;
			nend -= 8;
		} while (nend >= 8);
	}
	win |= AK_SHL(v, nend);
	nend += bits;
	ec->win = win;
	ec->nend = nend;
	ec->nbits += bits;
}

/* valeur, ft (> 1) : entier uniforme (ec_enc_uint) */
static void oec_uint(struct oec *ec, u32 v, u32 ft)
{
	ft--;
	u32 ftb = 32 - ak_clz32(ft);
	if (ftb <= 8) {
		oec_encode(ec, v, v + 1, ft + 1);
		return;
	}
	ftb -= 8; /* bits bruts */
	u32 ft1 = (ft >> ftb) + 1;
	u32 fl1 = v >> ftb;
	oec_encode(ec, fl1, fl1 + 1, ft1);
	oec_bits(ec, v & ((1u << ftb) - 1), ftb);
}

/* fin (ec_enc_done) ; les octets libres du milieu à zéro */
static void oec_done(struct oec *ec)
{
	u32 rng = (u32)ec->rng;
	u32 l = ak_clz32(rng); /* 32 - ilog(rng) */
	u32 msk = 0x7FFFFFFFu >> (l & 31);
	u64 val = ec->val;
	u64 end = (val + msk) & ~(u64)msk;
	if ((end | msk) >= val + rng) {
		l++;
		msk >>= 1;
		end = (val + msk) & ~(u64)msk;
	}
	s64 ll = (s32)l;
	while (ll > 0) {
		u32 s = (u32)(end >> 23);
		end = (end << 8) & 0x7FFFFFFF;
		ll -= 8;
		oec_carry(ec, s);
	}
	if (ec->rem >= 0 || ec->ext)
		oec_carry(ec, 0);
	u64 win = ec->win, nend = ec->nend; /* bits bruts restants */
	while (nend >= 8) {
		oec_wbe(ec, (u32)win & 255);
		win >>= 8;
		nend -= 8;
	}
	if (ec->err)
		return;
	u64 st = ec->storage, ee = ec->endoffs;
	for (u64 i = ec->offs; i < st - ee; i++) /* le milieu à zéro */
		ec->buf[i] = 0;
	if (!nend)
		return;
	if (ee >= st) {
		ec->err = 1;
		return;
	}
	u64 fr = (u64)-ll; /* bits libres du dernier octet */
	if (ec->offs + ee >= st && fr < nend) {
		win &= AK_SHL(1, fr) - 1;
		ec->err = 1;
	}
	ec->buf[st - ee - 1] |= (u8)win;
}

/* valeur, fs (probabilité de 0, Q15), décroissance -> valeur codée (bornée)
 * (Laplace, RFC 6716 4.3.2.1) */
static s32 oec_laplace(struct oec *ec, s32 val, u32 fs, u32 decay)
{
	u32 fl = 0;
	if (val) {
		s32 s = val >> 31;
		s32 a = (s32)(((u32)val + (u32)s) ^ (u32)s); /* |valeur| */
		fl = fs;
		u32 f = ((32768 - 32 - fs) * (16384 - decay)) >> 15; /* fs sans la probabilité minimale */
		s32 i = 1;
		for (;;) {
			if (!f) { /* probabilité minimale : borne */
				s32 ndi = (s32)(32768 - fl - (u32)s) >> 1;
				s32 di = a - i;
				if (!(di < ndi - 1))
					di = ndi - 1;
				fl += (u32)(2 * di + 1 + s);
				s32 r = (s32)(32768 - fl);
				fs = r < 1 ? (u32)r : 1;
				val = (s32)((u32)(i + di + s) ^ (u32)s);
				break;
			}
			if (i >= a) {
				f++;
				fl += f & ~(u32)s;
				fs = f;
				break;
			}
			f <<= 1;
			fl += f;
			fl += 2;
			f = (f * decay) >> 15;
			i++;
		}
	}
	oec_encode_bin(ec, fl, fl + fs, 15);
	return val;
}

/* ---------------------------------------------------------------------------
 * Interface (ak_enc.c)
 * ------------------------------------------------------------------------- */

static void rs_init(struct oe *o);
static void rs_push(struct oe *o, const s16 *pcm, u64 frames);
static void rs_flush(struct oe *o);

/* échantillon (48 kHz, échelle 32768) des voies : pré-accentuation, tampon de
 * la trame ; la trame pleine est encodée */
static void feed(struct oe *o, float s0, float s1)
{
	u64 f = o->fill;
	u64 k = f + 120;
	const float coef = ak_u2f(0x3F599A00); /* 0,85000610 */
	o->pcm[0][k] = s0 - o->pre[0];
	o->pre[0] = s0 * coef;
	if (o->c == 2) {
		o->pcm[1][k] = s1 - o->pre[1];
		o->pre[1] = s1 * coef;
	}
	f++;
	o->fill = f;
	if (f >= OE_N) {
		o->fill = 0;
		frame(o);
	}
}

/* encodeur, PCM s16 entrelacé, trames -> 0 */
static long opus_enc_push(struct akn *e, const s16 *pcm, u64 frames)
{
	struct oe *o = e->codec;
	o->nin += frames;
	if (o->rate != 48000) {
		rs_push(o, pcm, frames);
		return 0;
	}
	u64 C = o->c;
	while (frames) {
		float s0 = (float)pcm[0], s1 = s0;
		if (C == 2)
			s1 = (float)pcm[1];
		pcm += C;
		o->len48++;
		feed(o, s0, s1);
		frames--;
	}
	return 0;
}

/* dernière trame (zéros jusqu'à couvrir le retard), dernière page (granule
 * final, fin du flux) -> 0 */
static long opus_enc_finish(struct akn *e)
{
	struct oe *o = e->codec;
	if (o->rate != 48000)
		rs_flush(o);
	while (o->pkts * OE_N < (u64)o->len48 + o->preskip)
		feed(o, 0.0f, 0.0f);
	page_flush(o, (u64)o->len48 + o->preskip, 4); /* fin du flux */
	return 0;
}

/* encodeur (rate, ch, quality : débit en bits/s) -> 0 / < 0 */
long opus_enc_init(struct akn *e)
{
	if ((u64)e->ch - 1 > 1)
		return AKE_FORMAT;
	struct oe *o = akn_alloc(e, OE_SIZE);
	if (!o)
		return AKE_NOMEM;
	e->codec = o;
	o->enc = e;
	o->c = (u64)e->ch;
	o->rate = (u64)e->rate;
	/* débit : paquets de débit / 400 octets */
	u64 q = (u64)e->quality;
	if (!q)
		q = OE_DEFRATE;
	if (!(q > OE_MINRATE))
		q = OE_MINRATE;
	if (!(q < OE_MAXRATE))
		q = OE_MAXRATE;
	o->nbytes = q / 400 - 1;
	/* largeur de bande d'après le débit par voie : bande de fin, configuration */
	u64 per = q / o->c;
	u64 end = 21, cfg = 31;
	if (per < 40000) {
		end = 19;
		cfg = 27;
		if (per < 24000) {
			end = 17;
			cfg = 23;
			if (per < 14000) {
				end = 13;
				cfg = 19;
			}
		}
	}
	o->end = end;
	cfg <<= 3;
	if (o->c == 2)
		cfg |= 4;
	o->toc = cfg;
	/* bande d'intensité : seuils de libopus (kbit/s) pris au double du débit */
	u64 kb = q / 500, it = 0;
	while (it < 21 && kb >= ope_ithr[it])
		it++;
	o->itgt = it;
	o->preskip = 120;
	/* plan de la MDCT (N = 1920) : ak_imdct_init prend ses blocs par
	 * akd_alloc(décodeur) : on lui passe l'encodeur décalé pour que les blocs
	 * du décodeur tombent sur ceux de l'encodeur (même liste), comme l'asm */
	struct akd *d = (struct akd *)(void *)((u8 *)e - (offsetof(struct akd, allocs) - offsetof(struct akn, allocs)));
	long r = ak_imdct_init(d, &o->md, 1920);
	if (r < 0)
		return r;
	/* table du CRC d'Ogg (polynôme 0x04C11DB7) */
	for (u32 i = 0; i < 256; i++) {
		u32 c = i << 24;
		for (int k = 0; k < 8; k++)
			c = (c & 0x80000000u) ? (c << 1) ^ 0x04C11DB7u : c << 1;
		o->crc[i] = c;
	}
	if (o->rate != 48000)
		rs_init(o);
	/* OpusHead, OpusTags : une page chacun */
	u8 *p = o->pkt;
	memmove(p, ope_head, 19);
	p[9] = (u8)o->c;
	putle(p + 10, o->preskip, 2);
	putle(p + 12, o->rate, 4);
	page_add(o, p, 19);
	page_flush(o, 0, 2); /* début du flux */
	page_add(o, ope_tags, 30);
	page_flush(o, 0, 0);
	e->push = opus_enc_push;
	e->finish = opus_enc_finish;
	return 0;
}

/* ---------------------------------------------------------------------------
 * MDCT, énergies
 * ------------------------------------------------------------------------- */

/* u[n] = -v[1439-n] - v[1440+n] sous 480, v[n-480] - v[1439-n] au-delà */
static float mdct_u(const float *v, u64 n)
{
	if (n < 480)
		return ak_fneg(v[1439 - n] + v[1440 + n]);
	return v[n - 480] - v[1439 - n];
}

/* entrée (OE_PCMN : recouvrement + trame), sortie (960) : MDCT de la fenêtre
 * de CELT, comme si la trame de 1920 points était [420 zéros, entrée
 * fenêtrée, 420 zéros] ; pliage en u (960), puis la DCT-IV par une FFT de 480
 * points ; échelle 1/480 */
static void mdct(struct oe *o, const float *in, float *out)
{
	float *v = o->v;
	memset(v, 0, 1920 * 4);
	float *d = v + 420;
	u64 k = 0;
	for (; k < 120; k++)
		d[k] = in[k] * ce_window[k];
	for (; k < 960; k++)
		d[k] = in[k];
	for (; k < OE_PCMN; k++)
		d[k] = in[k] * ce_window[1079 - k];
	const struct akmdct *md = &o->md;
	const float *tw = md->tw;
	float *z = md->z;
	for (k = 0; k < 480; k++) {
		float re = mdct_u(v, k << 1);
		float im = mdct_u(v, 959 - (k << 1));
		float c = tw[2 * k], s = tw[2 * k + 1];
		z[2 * k] = ak_fmadd(im, s, re * c);	 /* re c + im s */
		z[2 * k + 1] = ak_fmsub(re, s, im * c); /* im c - re s */
	}
	ak_fft(&md->fft, md->z, md->z2);
	const float *z2 = md->z2;
	const float sc = ak_u2f(0x3B088889); /* 1/480 */
	for (k = 0; k < 480; k++) {
		float a = z2[2 * k], b = z2[2 * k + 1];
		float c = tw[2 * k], s = tw[2 * k + 1];
		float r4 = ak_fmadd(b, s, a * c);
		float r5 = ak_fmsub(a, s, b * c);
		r4 = r4 * sc;
		r5 = r5 * sc;
		out[k << 1] = r4;
		out[959 - (k << 1)] = ak_fneg(r5);
	}
}

/* x (> 0) -> log2 x (double en interne) */
static float olog2(float xf)
{
	u64 b = ak_d2u((double)xf);
	s64 e = (s64)((b >> 52) & 0x7FF) - 1023; /* exposant */
	double m = ak_u2d((b & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull); /* m dans [1, 2) */
	if (!(m <= 1.4142135623730951)) {
		m = m * 0.5;
		e++;
	}
	double s = (m - 1.0) / (m + 1.0); /* s = (m - 1) / (m + 1) */
	double s2 = s * s;
	double p = ak_dmadd(0.09090909090909091, s2, 0.1111111111111111); /* 1 + s^2/3 + ... + s^10/11 */
	p = ak_dmadd(p, s2, 0.14285714285714285);
	p = ak_dmadd(p, s2, 0.2);
	p = ak_dmadd(p, s2, 0.3333333333333333);
	p = ak_dmadd(p, s2, 1.0);
	double r = s * p;
	r = r * 2.8853900817779268; /* 2 / ln 2 */
	return (float)((double)e + r);
}

/* y >= 0, x > 0 -> atan2(y, x) (0..pi/2) */
static float oatan2(float yf, float xf)
{
	double y = (double)yf, x = (double)xf;
	int inv = 0;
	if (!(y <= x)) { /* y > x : pi/2 - atan(x / y) */
		double t = y;
		y = x;
		x = t;
		inv = 1;
	}
	double t = y / x; /* t dans [0, 1] */
	double off = 0.0;
	if (!(t <= 0.2679491924311227)) { /* atan t = pi/6 + atan((t - 1/r3) / (1 + t/r3)) */
		double a = t - 0.5773502691896258;
		double b = t * 0.5773502691896258;
		b = b + 1.0;
		t = a / b;
		off = 0.5235987755982988;
	}
	double t2 = t * t;
	double p = ak_dmadd(-0.09090909090909091, t2, 0.1111111111111111);
	p = ak_dmadd(p, t2, -0.14285714285714285);
	p = ak_dmadd(p, t2, 0.2);
	p = ak_dmadd(p, t2, -0.3333333333333333);
	p = ak_dmadd(p, t2, 1.0);
	double r = t * p;
	r = r + off;
	if (inv)
		r = 1.5707963267948966 - r;
	return (float)r;
}

/* énergies des bandes : amplitudes (bande), log2 moins eMeans (loge),
 * spectres normalisés (x) */
static void bandse(struct oe *o)
{
	u64 end = o->end, C = o->c;
	const float eps = ak_u2f(0x129E74D2); /* 1e-27 */
	for (u64 c = 0; c < C; c++) {
		const float *f = o->freq[c];
		float *x = o->x[c];
		for (u64 i = 0; i < end; i++) {
			u32 lo = (u32)ce_eband[i] << OE_LM, hi = (u32)ce_eband[i + 1] << OE_LM;
			float s = eps;
			u64 j = lo;
			do
				s = ak_fmadd(f[j], f[j], s);
			while (++j < hi);
			float a = ak_fsqrt(s);
			float g = 1.0f / (a + eps);
			j = lo;
			do
				x[j] = f[j] * g;
			while (++j < hi);
			o->bande[c * CE_NBE + i] = a;
			o->loge[c * CE_NBE + i] = olog2(a) - ce_emeans[i];
		}
	}
}

/* ---------------------------------------------------------------------------
 * Énergies grossières : prédiction (intra ou d'une trame à l'autre), résidus
 * de Laplace (bornés comme libopus près de la fin du budget)
 * ------------------------------------------------------------------------- */

static void coarse(struct oe *o)
{
	u64 end = o->end, C = o->c, intra = o->intra;
	struct oec *ec = &o->ec;
	const u8 *prob = ce_eprob + (intra + OE_LM * 2) * 42; /* modèle : [LM][intra][42] */
	float coef, beta;
	if (intra) {
		coef = 0.0f;
		beta = ce_pred[8];
	} else {
		coef = ce_pred[OE_LM];
		beta = ce_pred[4 + OE_LM];
	}
	float prev[2] = {0.0f, 0.0f};
	float maxd = (float)o->nbytes; /* max_decay = min(16, octets / 8) */
	maxd = maxd * 0.125f;
	maxd = ak_fmin(maxd, 16.0f);
	s64 budget = (s64)(o->nbytes << 3);
	for (u64 i = 0; i < end; i++)
		for (u64 c = 0; c < C; c++) {
			u64 k = c * CE_NBE + i;
			float x = o->loge[k], old = o->olde[k];
			float p3 = ak_fmax(old, -9.0f) * coef;
			float p4 = prev[c];
			float f = x - p3;
			f = f - p4;
			float pf = p3 + p4;
			s32 qi = ak_fcvtzs_w(ak_frintm(f + 0.5f));
			float db = ak_fmax(old, -28.0f) - maxd; /* decay_bound */
			if (qi < 0 && !(x >= db)) {
				qi += ak_fcvtzs_w(db - x);
				if (qi > 0)
					qi = 0;
			}
			s64 left = budget - oec_tell(ec);
			s64 bl = left - (s64)((end - i) * C * 3); /* bits_left */
			if (i && bl < 30) {
				if (bl < 24 && !(qi < 1))
					qi = 1;
				if (bl < 16 && !(qi > -1))
					qi = -1;
			}
			if (left >= 15) {
				u64 pi = i < 20 ? i : 20;
				pi <<= 1;
				qi = oec_laplace(ec, qi, (u32)prob[pi] << 7, (u32)prob[pi + 1] << 6);
			} else if (left >= 2) {
				if (!(qi < 1))
					qi = 1;
				if (!(qi > -1))
					qi = -1;
				oec_icdf(ec, (u32)(qi << 1) ^ (u32)(qi >> 31), ce_small_icdf, 2);
			} else if (left >= 1) {
				/* qi n'est pas borné à -1 (le bit vaut -qi != 0), comme l'asm */
				if (!(qi < 0))
					qi = 0;
				oec_bit_logp(ec, (u32)-qi, 1);
			} else
				qi = -1;
			float q = (float)qi;
			o->err[k] = f - q;    /* erreur */
			o->olde[k] = pf + q; /* énergie du décodeur */
			prev[c] = ak_fmsub(beta, q, prev[c] + q);
		}
}

/* ---------------------------------------------------------------------------
 * Dynalloc, trim
 * ------------------------------------------------------------------------- */

/* valeurs (n = 3 ou 5) -> médiane */
static float median(const float *v, u64 n)
{
	float t[8];
	for (u64 i = 0; i < n; i++)
		t[i] = v[i];
	for (u64 i = 1; i < n; i++) {
		float a = t[i];
		u64 j = i;
		while (j) {
			float b = t[j - 1];
			if (!(b > a)) /* b.le (vrai sur NaN) : arrêt */
				break;
			t[j] = b;
			j--;
		}
		t[j] = a;
	}
	return t[n >> 1];
}

/* dynalloc (analyse simplifiée de libopus) : enveloppe suiveuse des énergies,
 * plancher de bruit ; ce qui la dépasse -> quanta voulus (want), au plus 2/3
 * de la trame */
static void dynalloc(struct oe *o)
{
	memset(o->want, 0, 96);
	if (o->nbytes <= 50)
		return;
	u64 end = o->end, C = o->c;
	for (u64 c = 0; c < C; c++) {
		const float *e = o->loge + c * CE_NBE;
		float *f = o->fol + c * CE_NBE;
		f[0] = e[0];
		u64 last = 0;
		for (u64 i = 1; i < end; i++) {
			if (e[i] > e[i - 1] + 0.5f)
				last = i;
			f[i] = ak_fmin(f[i - 1] + 1.5f, e[i]);
		}
		for (s64 i = (s64)last - 1; i >= 0; i--) {
			float t = ak_fmin(f[i + 1] + 2.0f, e[i]);
			f[i] = ak_fmin(f[i], t);
		}
		for (u64 i = 2; i < end - 2; i++) { /* médiane de 5 - 1 */
			float m = median(e + i - 2, 5) - 1.0f;
			f[i] = ak_fmax(f[i], m);
		}
		float m = median(e, 3) - 1.0f; /* bords : médiane de 3 - 1 */
		f[0] = ak_fmax(f[0], m);
		f[1] = ak_fmax(f[1], m);
		m = median(e + end - 3, 3) - 1.0f;
		f[end - 2] = ak_fmax(f[end - 2], m);
		f[end - 1] = ak_fmax(f[end - 1], m);
		/* plancher : logN / 16 + 0,5 - 7 - eMeans + 0,0062 (i + 5)^2 */
		for (u64 i = 0; i < end; i++) {
			float v = (float)ce_logn[i] * ak_u2f(0x3D800000);
			v = v + -6.5f;
			v = v - ce_emeans[i];
			v = ak_fmadd((float)((i + 5) * (i + 5)), ak_u2f(0x3BCB295F), v);
			f[i] = ak_fmax(f[i], v);
		}
	}
	/* dépassement (stéréo : diaphonie de 24 dB, moyenne des voies) */
	float *f0 = o->fol, *f1 = o->fol + CE_NBE;
	const float *e0 = o->loge;
	for (u64 i = 0; i < end; i++) {
		float s0 = e0[i], s2 = f0[i];
		if (C == 2) {
			float s1 = e0[i + CE_NBE], s3 = f1[i];
			s3 = ak_fmax(s3, s2 - 4.0f);
			s2 = ak_fmax(s2, s3 - 4.0f);
			s0 = ak_fmax(s0 - s2, 0.0f);
			s1 = ak_fmax(s1 - s3, 0.0f);
			s0 = s0 + s1;
			s0 = s0 * 0.5f;
		} else
			s0 = ak_fmax(s0 - s2, 0.0f);
		s0 = s0 * 0.5f; /* débit constant : moitié */
		if (i < 8)
			s0 = s0 + s0;
		if (i >= 12)
			s0 = s0 * 0.5f;
		s0 = ak_fmin(s0, 4.0f);
		f0[i] = s0;
	}
	/* quanta voulus, au plus 2/3 de la trame */
	s64 tot = 0;
	s64 lim = (s64)(((o->nbytes << 1) / 3) << 6);
	for (u64 i = 0; i < end; i++) {
		float s0 = f0[i];
		u32 w = ((u32)(ce_eband[i + 1] - ce_eband[i]) * (u32)C) << OE_LM; /* width */
		s32 b;
		u32 w7;
		if (w < 6) {
			b = ak_fcvtzs_w(s0);
			w7 = ((u32)b * w) << 3;
		} else if (w > 48) {
			b = ak_fcvtzs_w(s0 * 8.0f);
			w7 = (u32)b * w;
		} else {
			float t = s0 * (float)w;
			t = t / 6.0f;
			b = ak_fcvtzs_w(t);
			w7 = (u32)b * 48;
		}
		s64 t8 = tot + (s64)w7;
		if (t8 > lim) { /* trop : les quanta qui tiennent */
			u32 a = w << 3, m = w > 48 ? w : 48;
			if (!(a < m))
				a = m;
			o->want[i] = (s32)ak_udiv((u64)(lim - tot), a);
			return;
		}
		o->want[i] = b;
		tot = t8;
	}
}

/* -> trim (0..10) : d'après le débit, la corrélation des voies et la pente du
 * spectre (alloc_trim_analysis de libopus, simplifié) */
static u64 trim(struct oe *o)
{
	u64 rate = o->nbytes * 400;
	float t = 5.0f;
	if (rate < 80000) {
		t = 4.0f;
		if (rate >= 64000) {
			float a = (float)((rate - 64000) >> 10);
			a = a / 16.0f;
			t = t + a;
		}
	}
	if (o->c == 2) { /* corrélation des 8 premières bandes */
		float s = 0.0f;
		for (u64 j = 0; j < 64; j++)
			s = ak_fmadd(o->x[0][j], o->x[1][j], s);
		s = s * 0.125f;
		s = ak_fabs(s);
		s = ak_fmin(s, 1.0f);
		float a = ak_fmsub(s, s, ak_u2f(0x3F8020C5)); /* log2(1,001 - s^2) */
		a = olog2(a);
		a = a * 0.75f;
		a = ak_fmax(a, -4.0f);
		t = t + a;
	}
	u64 end = o->end, C = o->c;
	float s = 0.0f;
	for (u64 c = 0; c < C; c++)
		for (u64 i = 0; i < end - 1; i++)
			s = ak_fmadd(o->loge[c * CE_NBE + i], (float)(s64)((i << 1) + 2 - end), s);
	s = s / (float)(s64)((end - 1) * C);
	s = s + 1.0f;
	s = s / 6.0f;
	s = ak_fmin(s, 2.0f);
	s = ak_fmax(s, -2.0f);
	t = t - s;
	t = t + 0.5f;
	s32 r = ak_fcvtzs_w(ak_frintm(t));
	if (!(r > 0))
		r = 0;
	if (!(r < 10))
		r = 10;
	return (u32)r;
}

/* ---------------------------------------------------------------------------
 * Allocation des bits : le calcul du décodeur (ce_alloc, RFC 6716 4.3.3), les
 * choix codés (sauts de bandes, intensité, double stéréo à 0)
 * ------------------------------------------------------------------------- */

/* vecteur -> bits demandés (bissection) */
static s64 vsum(const struct oe *o, u64 start, u64 end, u64 C, u64 v)
{
	s64 psum = 0;
	int done = 0;
	const u8 *ba = ce_balloc + v * CE_NBE;
	for (u64 j = end; j > start;) {
		j--;
		s32 b = (s32)((((u32)(ce_eband[j + 1] - ce_eband[j]) * (u32)C * ba[j]) << OE_LM)) >> 2;
		if (b > 0) {
			b += o->trimoff[j];
			if (!(b > 0))
				b = 0;
		}
		b += o->offs[j];
		if (b >= o->thresh[j] || done) {
			done = 1;
			s32 cp = o->cap[j];
			psum += b < cp ? b : cp;
		} else if (b >= (s32)(C << 3))
			psum += (s64)(C << 3);
	}
	return psum;
}

/* bits disponibles (1/8) -> pulses, fineq, prio, coded, intens, dual, balance */
static void alloc(struct oe *o, s32 avail)
{
	struct oec *ec = &o->ec;
	const u64 start = 0;
	u64 end = o->end, C = o->c;
	const u32 LM = OE_LM;
	s64 total = avail > 0 ? avail : 0;
	s64 skip_start = (s64)start;
	const u16 *eb = ce_eband;
	s64 skip_rsv = total >= 8 ? 8 : 0;
	total -= skip_rsv;
	s64 irsv = 0, drsv = 0; /* intensity_rsv, dual_stereo_rsv */
	if (C == 2) {
		irsv = ce_log2frac[end - start];
		if (irsv > total)
			irsv = 0;
		else {
			total -= irsv;
			if (total >= 8) {
				drsv = 8;
				total -= drsv;
			}
		}
	}
	u32 tr = (u32)o->trim;
	for (u64 j = start; j < end; j++) {
		u32 N = (u32)(eb[j + 1] - eb[j]);
		s32 t = (s32)(((((N + (N << 1)) << LM) << 3)) >> 4);
		s32 c8 = (s32)(C << 3);
		o->thresh[j] = t > c8 ? t : c8;
		u32 v = (u32)C * N;
		v *= tr - 5 - LM;
		v *= (u32)end - (u32)j - 1;
		v <<= LM + 3;
		s32 to = (s32)v >> 6;
		if ((N << LM) == 1)
			to -= (s32)(C << 3);
		o->trimoff[j] = to;
	}
	s64 lo = 1, hi = 10;
	while (lo <= hi) {
		s64 mid = (lo + hi) >> 1;
		if (vsum(o, start, end, C, (u64)mid) <= total)
			lo = mid + 1;
		else
			hi = mid - 1;
	}
	hi = lo;
	lo = lo - 1;
	for (u64 j = start; j < end; j++) {
		u32 N = (u32)(eb[j + 1] - eb[j]);
		s32 b1 = (s32)((ce_balloc[lo * CE_NBE + j] * N * (u32)C) << LM) >> 2;
		s32 b2;
		if ((u64)hi >= 11)
			b2 = o->cap[j];
		else
			b2 = (s32)((ce_balloc[hi * CE_NBE + j] * N * (u32)C) << LM) >> 2;
		s32 to = o->trimoff[j];
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
		s32 of = o->offs[j];
		if (lo)
			b1 += of;
		b2 += of;
		if (of > 0)
			skip_start = (s64)j;
		b2 -= b1;
		if (!(b2 > 0))
			b2 = 0;
		o->bits1[j] = b1;
		o->bits2[j] = b2;
	}
	s64 floor8 = (s64)(C << 3); /* alloc_floor */
	s64 l2 = 0, h2 = 64;
	for (int it = 0; it < 6; it++) {
		s64 mid = (l2 + h2) >> 1;
		s64 psum = 0;
		int done = 0;
		for (u64 j = end; j > start;) {
			j--;
			s32 t = o->bits1[j] + ((s32)((u32)o->bits2[j] * (u32)mid) >> 6);
			if (t >= o->thresh[j] || done) {
				done = 1;
				s32 cp = o->cap[j];
				psum += t < cp ? t : cp;
			} else if (t >= (s32)floor8)
				psum += floor8;
		}
		if (psum > total)
			h2 = mid;
		else
			l2 = mid;
	}
	s64 psum = 0;
	{
		int done = 0;
		for (u64 j = end; j > start;) {
			j--;
			s32 t = o->bits1[j] + ((s32)((u32)o->bits2[j] * (u32)l2) >> 6);
			if (t >= o->thresh[j] || done)
				done = 1;
			else
				t = t >= (s32)floor8 ? (s32)floor8 : 0;
			s32 cp = o->cap[j];
			if (!(t < cp))
				t = cp;
			o->pulses[j] = t;
			psum += t;
		}
	}
	/* bandes sautées, depuis la fin (choix : profondeur de libopus) */
	s64 coded = (s64)end;
	for (;;) {
		s64 j = coded - 1;
		if (!(j > skip_start)) {
			total += skip_rsv; /* (rend le bit réservé) */
			break;
		}
		s32 left = (s32)(total - psum);
		s32 w2 = (s32)eb[coded] - (s32)eb[start];
		s32 pc = ak_sdivw(left, w2); /* percoeff */
		left = left - pc * w2;
		s32 w5 = (s32)eb[j];
		s32 rm = left - (w5 - (s32)eb[start]);
		if (!(rm > 0))
			rm = 0;
		s32 w7 = (s32)eb[coded] - w5; /* largeur */
		s32 bb = o->pulses[j] + pc * w7 + rm; /* band_bits */
		s32 th = o->thresh[j], f8 = (s32)floor8 + 8;
		if (!(th > f8))
			th = f8;
		if (bb >= th) {
			s32 depth = 1; /* garder : codedBands <= start + 2, ou band_bits >
					* profondeur x largeur, j <= sigbw */
			if (coded > 17)
				depth = j < o->lastcoded ? 7 : 9;
			depth = (s32)((u32)(depth * w7) << (OE_LM + 3)) >> 4;
			u32 keep = 1;
			if (coded > (s64)start + 2) {
				keep = bb > depth;
				if (!(j <= o->sigbw))
					keep = 0;
			}
			oec_bit_logp(ec, keep, 1);
			if (keep)
				break;
			psum += 8;
			bb -= 8;
		}
		psum -= (s64)(u32)o->pulses[j] + irsv;
		if (irsv)
			irsv = ce_log2frac[j - (s64)start];
		psum += irsv;
		s32 pv = 0;
		if (bb >= (s32)floor8) {
			psum += floor8;
			pv = (s32)floor8;
		}
		o->pulses[j] = pv;
		coded--;
	}
	o->coded = coded;
	o->lastcoded = coded;
	/* intensité (d'après le débit, au plus codedBands), double stéréo : 0 */
	s64 intens = 0;
	if (irsv) {
		s64 v = (s64)o->itgt;
		if (!(v < coded))
			v = coded;
		if (!(v > (s64)start))
			v = (s64)start;
		oec_uint(ec, (u32)v - (u32)start, (u32)(coded + 1 - (s64)start));
		intens = v;
	}
	o->intens = intens;
	if (!(intens > (s64)start)) {
		total += drsv;
		drsv = 0;
	}
	if (drsv)
		oec_bit_logp(ec, 0, 1);
	o->dual = 0;
	/* le reste, réparti */
	{
		s32 left = (s32)(total - psum);
		s32 w2 = (s32)eb[coded] - (s32)eb[start];
		s32 pc = ak_sdivw(left, w2);
		left = left - pc * w2;
		for (s64 j = (s64)start; j < coded; j++)
			o->pulses[j] += pc * (s32)(eb[j + 1] - eb[j]);
		for (s64 j = (s64)start; j < coded; j++) {
			s32 w = (s32)(eb[j + 1] - eb[j]);
			s32 t = left < w ? left : w;
			o->pulses[j] += t;
			left -= t;
		}
	}
	/* énergie fine et impulsions de chaque bande */
	s32 bal = 0;
	u32 st = (u32)C - 1; /* stéréo */
	s64 ib = o->intens;
	u64 dual = o->dual;
	s64 j = (s64)start;
	for (; j < coded; j++) {
		s32 N = (s32)((u32)(eb[j + 1] - eb[j]) << LM);
		s32 bit = o->pulses[j] + bal;
		s32 cp = o->cap[j];
		s32 exc, eb3, pr;
		if (N > 1) {
			exc = bit - cp; /* excess */
			if (!(exc > 0))
				exc = 0;
			bit -= exc;
			s32 den = (s32)C * N;
			if (C == 2 && N > 2 && !dual && j < ib)
				den++;
			s32 nc = ((s32)ce_logn[j] + (s32)(LM << 3)) * den; /* NClogN */
			s32 off = (nc >> 1) - 21 * den;
			if (N == 2)
				off += den << 1;
			s32 w3 = bit + off;
			if (w3 < den << 4)
				off += nc >> 2;
			else if (w3 < (den + (den << 1)) << 3)
				off += nc >> 3;
			w3 = bit + off + (den << 2);
			if (!(w3 > 0))
				w3 = 0;
			eb3 = (s32)(ak_udivw((u32)w3, (u32)den) >> 3); /* ebits */
			if ((s32)C * eb3 > bit >> 3)
				eb3 = (bit >> (st & 31)) >> 3;
			if (!(eb3 < 8))
				eb3 = 8;
			pr = (eb3 * den) << 3 >= bit + off; /* priorité */
			bit -= ((s32)C * eb3) << 3;
		} else { /* N = 1 : un bit de signe */
			s32 c8 = (s32)(C << 3);
			exc = bit - c8;
			if (!(exc > 0))
				exc = 0;
			bit -= exc;
			eb3 = 0;
			pr = 1;
		}
		if (exc > 0) { /* bits fins en plus */
			s32 e1 = exc >> ((st + 3) & 31);
			s32 e4 = 8 - eb3;
			if (!(e1 < e4))
				e1 = e4;
			eb3 += e1;
			s32 extra = (e1 * (s32)C) << 3;
			pr = extra >= exc - bal;
			exc -= extra;
		}
		bal = exc;
		o->pulses[j] = bit;
		o->fineq[j] = eb3;
		o->prio[j] = pr;
	}
	o->balance = bal;
	for (; j < (s64)end; j++) { /* bandes sautées : tout en énergie fine */
		s32 w0 = o->pulses[j];
		o->pulses[j] = 0;
		w0 = (w0 >> (st & 31)) >> 3;
		o->fineq[j] = w0;
		o->prio[j] = w0 < 1;
	}
}

/* ---------------------------------------------------------------------------
 * Énergies fines et finales (RFC 6716 4.3.2.2)
 * ------------------------------------------------------------------------- */

static void fine(struct oe *o)
{
	u64 end = o->end, C = o->c;
	for (u64 i = 0; i < end; i++) {
		s32 fq = o->fineq[i];
		if (fq <= 0)
			continue;
		for (u64 c = 0; c < C; c++) {
			u64 k = c * CE_NBE + i;
			float s = o->err[k] + 0.5f; /* q2 = floor((erreur + 1/2) 2^fq) */
			s = s * ak_u2f((u32)(fq + 127) << 23);
			s32 q2 = ak_fcvtzs_w(ak_frintm(s));
			s32 mx = (s32)((1u << (fq & 31)) - 1);
			if (!(q2 < mx))
				q2 = mx;
			if (!(q2 > 0))
				q2 = 0;
			oec_bits(&o->ec, (u32)q2, (u32)fq);
			float off = (float)(u32)q2 + 0.5f; /* (q2 + 1/2) 2^-fq - 1/2 */
			off = off * ak_u2f((u32)(127 - fq) << 23);
			off = off - 0.5f;
			o->olde[k] = o->olde[k] + off;
			o->err[k] = o->err[k] - off;
		}
	}
}

/* énergies finales : les bits restants, deux passes de priorité */
static void final(struct oe *o)
{
	u64 end = o->end;
	s64 C = (s64)o->c;
	s64 left = (s64)(o->nbytes << 3) - oec_tell(&o->ec); /* bits_left */
	for (s64 p = 0; p < 2; p++)
		for (u64 i = 0; i < end; i++) {
			if (left < C)
				break;
			s32 fq = o->fineq[i];
			if (fq >= 8)
				continue;
			if ((s64)(u32)o->prio[i] != p)
				continue;
			for (s64 c = 0; c < C; c++) {
				u64 k = (u64)c * CE_NBE + i;
				u32 q2 = o->err[k] >= 0.0f;
				oec_bits(&o->ec, q2, 1);
				float off = (float)q2 - 0.5f; /* (q2 - 1/2) 2^-(fq + 1) */
				off = off * ak_u2f((u32)(126 - fq) << 23);
				o->olde[k] = o->olde[k] + off;
				o->err[k] = o->err[k] - off;
				left--;
			}
		}
}

/* ---------------------------------------------------------------------------
 * Bandes (quant_all_bands, côté encodeur ; blocs longs, tf_change = 0)
 * ------------------------------------------------------------------------- */

static void qp(struct oe *o, float *X, s32 N, s32 b, s32 LM);

/* X, Y (0) : bande d'un coefficient, un bit de signe par voie */
static void qn1(struct oe *o, float *X, float *Y)
{
	for (;;) {
		if (o->rem >= 8) {
			o->rem -= 8;
			oec_bits(&o->ec, X[0] < 0.0f, 1);
		}
		if (!Y)
			return;
		X = Y;
		Y = 0;
	}
}

/* X, N, b, LM : une bande mono (quant_band) */
static void qb(struct oe *o, float *X, s32 N, s32 b, s32 LM)
{
	if (N != 1)
		qp(o, X, N, b, LM);
	else
		qn1(o, X, 0);
}

/* X, Y, N : X = intensité (gauche et droite pondérées par les amplitudes de
 * la bande courante) */
static void istereo(struct oe *o, float *X, const float *Y, s32 N)
{
	float l = o->bande[o->i], r = o->bande[o->i + CE_NBE];
	const float eps = ak_u2f(0x26901D7D); /* 1e-15 */
	float n = l * l;
	n = ak_fmadd(r, r, n);
	n = n + eps;
	n = ak_fsqrt(n);
	n = n + eps;
	l = l / n;
	r = r / n;
	u32 j = 0;
	do {
		X[j] = ak_fmadd(Y[j], r, X[j] * l);
		j++;
	} while (j < (u32)N);
}

/* X, Y, N, b, LM, stéréo -> itheta ; *delta, *qalloc, *b (compute_theta,
 * côté encodeur : l'angle mesuré, quantifié et codé ; en stéréo, X et Y
 * deviennent milieu et côté, ou X l'intensité) */
static s32 theta(struct oe *o, float *X, float *Y, s32 N, s32 *pb, s32 LM, int stereo, s32 *delta, s32 *qalloc)
{
	struct oec *ec = &o->ec;
	s32 b = *pb;
	u64 i = o->i;
	s32 pcap = (s32)ce_logn[i] + (s32)((u32)LM << 3); /* pulse_cap */
	s32 off = (stereo && N == 2) ? 16 : 4;
	off = (pcap >> 1) - off;
	s32 n2 = (N << 1) - 1; /* qn (compute_qn) */
	if (stereo && N == 2)
		n2--;
	s32 q = ak_sdivw(n2 * off + b, n2); /* qb */
	s32 m = b - pcap - 32;
	if (!(q < m))
		q = m;
	if (!(q < 64))
		q = 64;
	u32 qn = 1;
	if (q >= 4) {
		u32 t = ce_exp2t8[q & 7];
		t >>= (u32)(14 - (q >> 3)) & 31;
		t = (t + 1) >> 1;
		qn = t << 1;
	}
	if (stereo && i >= (u64)o->intens)
		qn = 1;
	/* angle mesuré : 16384 x 0,63662 atan2(|côté|, |milieu|) */
	const float eps = ak_u2f(0x26901D7D); /* 1e-15 */
	float em = eps, es = eps;
	u32 j = 0;
	do {
		float x = X[j], y = Y[j];
		if (stereo) {
			float a = x + y, d = x - y;
			em = ak_fmadd(a, a, em);
			es = ak_fmadd(d, d, es);
		} else {
			em = ak_fmadd(x, x, em);
			es = ak_fmadd(y, y, es);
		}
		j++;
	} while (j < (u32)N);
	float at = oatan2(ak_fsqrt(es), ak_fsqrt(em));
	at = at * ak_u2f(0x4622F987); /* 16384 x 0,63662 */
	at = at + 0.5f;
	s32 it = ak_fcvtzs_w(ak_frintm(at)); /* itheta */
	s64 tell = oec_tell_frac(ec);
	if (qn != 1) {
		u32 ut = ((u32)it * qn + 8192) >> 14; /* quantifié */
		if (!stereo) { /* densité triangulaire */
			u32 x0 = qn >> 1, w5 = x0 + 1, ft = w5 * w5, fl, fs;
			if (ut > x0) {
				fs = qn + 1 - ut;
				fl = ft - ((fs * (fs + 1)) >> 1);
			} else {
				fs = ut + 1;
				fl = (ut * fs) >> 1;
			}
			oec_encode(ec, fl, fl + fs, ft);
		} else if (N <= 2)
			oec_uint(ec, ut, qn + 1);
		else { /* stéréo : densité en marche (p0 = 3 jusqu'à qn/2) */
			u32 x0 = qn >> 1;
			u32 w5 = (x0 + 1) * 3;
			u32 ft = w5 + x0, fl, fh;
			if (ut > x0) {
				fl = ut - 1 - x0 + w5;
				fh = ut - x0 + w5;
			} else {
				fl = ut * 3;
				fh = fl + 3;
			}
			oec_encode(ec, fl, fh, ft);
		}
		ut = ak_udivw(ut << 14, qn); /* itheta = itheta 16384 / qn */
		it = (s32)ut;
		if (stereo) {
			if (!it)
				istereo(o, X, Y, N);
			else { /* milieu et côté (stereo_split) */
				const float h = ak_u2f(0x3F3504F3);
				j = 0;
				do {
					float a = X[j] * h, c = Y[j] * h;
					X[j] = a + c;
					Y[j] = c - a;
					j++;
				} while (j < (u32)N);
			}
		}
	} else if (stereo) { /* qn = 1, stéréo : intensité (inversion ?) */
		u32 inv = it > 8192;
		if (inv) {
			j = 0;
			do {
				Y[j] = ak_fneg(Y[j]);
				j++;
			} while (j < (u32)N);
		}
		istereo(o, X, Y, N);
		if (b > 16 && o->rem > 16)
			oec_bit_logp(ec, inv, 2);
		it = 0;
	}
	/* mono et qn = 1 : itheta reste l'angle mesuré, comme l'asm */
	s32 qa = (s32)((u32)oec_tell_frac(ec) - (u32)tell); /* qalloc */
	b -= qa;
	s32 dl;
	if (!it)
		dl = -16384;
	else if (it == 16384)
		dl = 16384;
	else {
		s32 imid = ce_bexcos(it);
		s32 iside = ce_bexcos(16384 - it);
		s32 lt = (s16)ce_log2tan(iside, imid);
		s32 n7 = (s16)(u32)((u32)(N - 1) << 7);
		dl = (s32)((u32)lt * (u32)n7 + 16384) >> 15; /* delta */
	}
	*delta = dl;
	*qalloc = qa;
	*pb = b;
	return it;
}

/* X, N, K : rotation d'étalement, recherche PVQ, indice CWRS codé (alg_quant) */
static void algq(struct oe *o, float *X, u32 N, u32 K)
{
	/* rotation d'étalement (normal : facteur 10), sens direct */
	if (K << 1 < N) {
		u32 w5 = (u32)ce_spreadf[1] * K + N;
		float g = (float)N / (float)w5;
		g = g * g;
		float th = g * 0.5f; /* theta */
		double sn, cs;
		ak_sincos((double)th * ce_halfpi, &sn, &cs);
		float c = (float)cs;
		ak_sincos((double)(1.0f - th) * ce_halfpi, &sn, &cs);
		float s = (float)cs;
		u32 s2 = 0; /* stride2 */
		if (N >= 8) {
			s2 = 1;
			while (s2 * s2 + s2 < N)
				s2++;
		}
		ce_rot1(X, N, 1, c, ak_fneg(s));
		if (s2)
			ce_rot1(X, N, s2, s, ak_fneg(c));
	}
	/* recherche PVQ (op_pvq_search) : |X|, y = 0, impulsions à 0 */
	float *px = o->px, *py = o->py;
	s32 *iy = o->iy;
	float sum = 0.0f;
	u32 j = 0;
	do {
		float a = ak_fabs(X[j]);
		px[j] = a;
		sum = sum + a;
		py[j] = 0.0f;
		iy[j] = 0;
		j++;
	} while (j < N);
	float xy = 0.0f, yy = 0.0f;
	s32 left = (s32)K;
	if (K > N >> 1) {
		/* projection : floor((K + 0,8) / somme |X|) */
		if (!(sum > ak_u2f(0x26901D7D)) || !(sum < 64.0f)) {
			/* X = (1, 0, ...) ; la somme reste celle de |X|, comme l'asm
			 * (bande nulle : rcp infini, iy[0] saturé à INT32_MAX) */
			px[0] = 1.0f;
			for (j = 1; j < N; j++)
				px[j] = 0.0f;
		}
		float rcp = (float)K + ak_u2f(0x3F4CCCCD);
		rcp = rcp / sum;
		j = 0;
		do {
			float a = px[j];
			s32 k = ak_fcvtzs_w(a * rcp);
			iy[j] = k;
			float y = (float)k;
			yy = ak_fmadd(y, y, yy);
			xy = ak_fmadd(a, y, xy);
			py[j] = y + y;
			left -= k;
			j++;
		} while (j < N);
	}
	if (left > (s32)N + 3) { /* trop d'impulsions : toutes sur la première */
		float t = (float)left;
		yy = ak_fmadd(t, t, yy);
		yy = ak_fmadd(t, py[0], yy);
		iy[0] += left;
		left = 0;
	}
	while (left > 0) {
		yy = yy + 1.0f;
		float bn = xy + px[0];
		bn = bn * bn; /* best_num */
		float bd = yy + py[0]; /* best_den */
		u32 best = 0;
		for (j = 1; j < N; j++) {
			float rxy = xy + px[j];
			rxy = rxy * rxy;
			float ryy = yy + py[j];
			if (bd * rxy > ryy * bn) {
				bd = ryy;
				bn = rxy;
				best = j;
			}
		}
		xy = xy + px[best];
		yy = yy + py[best];
		py[best] = py[best] + 2.0f;
		iy[best]++;
		left--;
	}
	j = 0;
	do {
		if (!(X[j] >= 0.0f))
			iy[j] = -iy[j];
		j++;
	} while (j < N);
	/* table U(n, k), n <= N, k <= K + 1 (comme le décodeur) */
	u32 *ut = o->ut;
	u64 rs = K + 2; /* pas des lignes */
	ut[0] = 1;
	for (u64 k = 1; k < rs; k++)
		ut[k] = 0;
	for (u64 n = 1; n <= N; n++) {
		u32 *row = ut + n * rs, *pr = row - rs;
		row[0] = 0;
		for (u64 k = 1; k < rs; k++)
			row[k] = pr[k] + row[k - 1] + pr[k - 1];
	}
	/* indice CWRS, depuis les deux dernières dimensions */
	s32 a = iy[N - 1];
	u32 kr = a < 0 ? (u32)-a : (u32)a;
	u32 idx = a < 0;
	s32 b4 = iy[N - 2];
	u32 k = (b4 < 0 ? (u32)-b4 : (u32)b4) + kr;
	if (kr)
		idx += (kr << 1) - 1;
	else
		idx = 0;
	if (b4 < 0)
		idx += (k << 1) + 1;
	kr = k;
	for (s32 jj = (s32)N - 3; jj >= 0; jj--) {
		u32 n = N - (u32)jj;
		s32 w = iy[jj];
		k = (w < 0 ? (u32)-w : (u32)w) + kr;
		const u32 *row = ut + (u64)(u32)(n * (u32)rs);
		if (!w)
			idx += row[k]; /* zéro : + U(n, k) */
		else {
			idx += row[kr]; /* + U(n, kr) */
			if (!(w > 0))
				idx += row[k + 1]; /* négatif : + U(n, k + 1) */
		}
		kr = k;
	}
	const u32 *row = ut + (u64)(u32)(N * (u32)rs); /* V(N, K) = U(N, K) + U(N, K + 1) */
	oec_uint(&o->ec, idx, row[K] + row[K + 1]);
}

/* X, N, b, LM (-1..3) : division récursive en deux moitiés (theta), sinon PVQ
 * (quant_partition) */
static void qp(struct oe *o, float *X, s32 N, s32 b, s32 LM)
{
	const u8 *cache = ce_cache((u32)o->i, LM);
	s32 lim = (s32)cache[cache[0]] + 12;
	if (LM != -1 && b > lim && N > 2) {
		N = (s32)((u32)N >> 1); /* division */
		LM--;
		s32 delta, qa;
		s32 it = theta(o, X, X + (u32)N, N, &b, LM, 0, &delta, &qa);
		s32 mb = (b - delta) / 2; /* mbits = max(0, min(b, (b - delta) / 2)) */
		if (!(mb < b))
			mb = b;
		if (!(mb > 0))
			mb = 0;
		s32 sb = b - mb; /* sbits */
		o->rem -= qa;
		s64 reb = o->rem; /* rebalance */
		if (mb >= sb) {
			qp(o, X, N, mb, LM);
			s64 r = (s64)mb - (reb - o->rem);
			if (r > 24 && it)
				sb += (s32)(r - 24);
			qp(o, X + (u32)N, N, sb, LM);
		} else {
			qp(o, X + (u32)N, N, sb, LM);
			s64 r = (s64)sb - (reb - o->rem);
			if (r > 24 && it != 16384)
				mb += (s32)(r - 24);
			qp(o, X, N, mb, LM);
		}
		return;
	}
	/* impulsions (bits2pulses, sans dépasser le budget) */
	u32 q = ce_b2p((u32)o->i, LM, b);
	u64 cost = ce_p2b((u32)o->i, LM, q);
	o->rem -= (s64)cost;
	while (o->rem < 0 && q) {
		o->rem += (s64)cost;
		q--;
		cost = ce_p2b((u32)o->i, LM, q);
		o->rem -= (s64)cost;
	}
	if (q)
		algq(o, X, (u32)N, ce_getp(q));
}

/* X, Y, N, b : bande stéréo (quant_band_stereo : M/S ou intensité ; pas de
 * double stéréo) */
static void qbs(struct oe *o, float *X, float *Y, s32 N, s32 b)
{
	if (N == 1) {
		qn1(o, X, Y);
		return;
	}
	s32 delta, qa;
	s32 it = theta(o, X, Y, N, &b, OE_LM, 1, &delta, &qa);
	if (N == 2) { /* N = 2 : un seul bit pour le côté */
		s32 sb = 0;
		if (it && it != 16384)
			sb = 8;
		o->rem -= qa + sb;
		s32 mb = b - sb;
		float *x1 = X, *x2 = Y;
		if (it > 8192) {
			x1 = Y;
			x2 = X;
		}
		if (sb) { /* signe de x2[0] y2[1] - x2[1] y2[0] */
			float s = x1[0] * x2[1];
			s = ak_fmsub(x1[1], x2[0], s);
			oec_bits(&o->ec, s < 0.0f, 1);
		}
		qb(o, x1, 2, mb, OE_LM);
		return;
	}
	s32 mb = (b - delta) / 2; /* mbits = max(0, min(b, (b - delta) / 2)) */
	if (!(mb < b))
		mb = b;
	if (!(mb > 0))
		mb = 0;
	s32 sb = b - mb; /* sbits */
	o->rem -= qa;
	s64 reb = o->rem; /* rebalance */
	if (mb >= sb) {
		qb(o, X, N, mb, OE_LM);
		s64 r = (s64)mb - (reb - o->rem);
		if (r > 24 && it)
			sb += (s32)(r - 24);
		qb(o, Y, N, sb, OE_LM);
	} else {
		qb(o, Y, N, sb, OE_LM);
		s64 r = (s64)sb - (reb - o->rem);
		if (r > 24 && it != 16384)
			mb += (s32)(r - 24);
		qb(o, X, N, mb, OE_LM);
	}
}

static void bands(struct oe *o)
{
	u64 end = o->end;
	u64 coded = (u64)o->coded;
	s64 bal = o->balance;
	s64 total = (s64)(o->nbytes << 6); /* total (1/8 bits) */
	for (u64 i = 0; i < end; i++) {
		o->i = i;
		s64 tell = oec_tell_frac(&o->ec);
		if (i)
			bal -= tell; /* balance -= tell */
		s64 rem = total - tell - 1; /* remaining = total - tell - 1 */
		o->rem = rem;
		s64 b = 0;
		if (i < coded) {
			s64 d = (s64)(coded - i);
			if (!(d < 3))
				d = 3;
			s64 cb = ak_sdiv(bal, d); /* curr_balance */
			cb += o->pulses[i];
			if (!(cb < rem + 1))
				cb = rem + 1;
			if (!(cb < 16383))
				cb = 16383;
			b = cb > 0 ? cb : 0;
		}
		s32 N = (s32)((u32)(ce_eband[i + 1] - ce_eband[i]) << OE_LM);
		float *X = o->x[0] + ((u64)ce_eband[i] << OE_LM);
		if (o->c == 2)
			qbs(o, X, X + 960, N, (s32)b);
		else
			qb(o, X, N, (s32)b, OE_LM);
		bal += o->pulses[i]; /* balance += pulses[i] + tell */
		bal += tell;
	}
}

/* ---------------------------------------------------------------------------
 * Trame
 * ------------------------------------------------------------------------- */

/* la trame (pcm) -> un paquet dans la page */
static void frame(struct oe *o)
{
	u64 end = o->end, C = o->c;
	struct oec *ec = &o->ec;
	/* MDCT de chaque voie ; le recouvrement passe au début du tampon */
	for (u64 c = 0; c < C; c++) {
		mdct(o, o->pcm[c], o->freq[c]);
		memmove(o->pcm[c], o->pcm[c] + 960, 120 * 4);
	}
	bandse(o);
	/* largeur de bande du signal : dernière bande dont l'amplitude moyenne par
	 * coefficient dépasse 1/4 ; les bandes au-delà sont sautées (allocation) */
	o->sigbw = 0;
	for (u64 i = 0; i < end; i++) {
		float w = (float)((u32)(ce_eband[i + 1] - ce_eband[i]) << OE_LM);
		w = w * ak_u2f(0x3D800000); /* N / 16 */
		for (u64 c = 0; c < C; c++) {
			float a = o->bande[c * CE_NBE + i];
			a = a * a;
			if (a > w)
				o->sigbw = (s64)i;
		}
	}
	oec_init(ec, o->pkt + 1, o->nbytes);
	s64 tb = (s64)(o->nbytes << 3); /* total_bits */
	/* silence (0), post-filtre (0), transitoire (0), intra (première trame) */
	oec_bit_logp(ec, 0, 15);
	if (!(oec_tell(ec) + 16 > tb))
		oec_bit_logp(ec, 0, 1);
	if (!(oec_tell(ec) + 3 > tb))
		oec_bit_logp(ec, 0, 3);
	u64 intra = o->frames == 0;
	if (oec_tell(ec) + 3 > tb)
		intra = 0;
	else
		oec_bit_logp(ec, (u32)intra, 3);
	o->intra = intra;
	coarse(o);
	/* tf_res : 0 partout (pas de tf_select : il ne change rien sans transitoire) */
	s64 bud = tb;
	bud -= (u64)(oec_tell(ec) + 5) <= (u64)bud; /* tf_select_rsv */
	u32 lp = 4;
	for (u64 i = 0; i < end; i++) {
		if (!(oec_tell(ec) + lp > bud))
			oec_bit_logp(ec, 0, lp);
		lp = 5;
	}
	/* étalement normal */
	if (!(oec_tell(ec) + 4 > tb))
		oec_icdf(ec, 2, ce_spread_icdf, 5);
	/* plafonds : (caps + 64) C N / 4 */
	const u8 *cc = ce_ccaps + (OE_LM * 2 + C - 1) * CE_NBE;
	for (u64 i = 0; i < CE_NBE; i++) {
		u32 n = (u32)(ce_eband[i + 1] - ce_eband[i]) << OE_LM;
		o->cap[i] = (s32)((((u32)cc[i] + 64) * (u32)C * n) >> 2);
	}
	/* dynalloc : analyse, puis les quanta voulus (tant qu'ils tiennent) */
	dynalloc(o);
	s64 total = tb << 3; /* total (1/8 bits) */
	s64 tell = oec_tell_frac(ec);
	s64 dlogp = 6;
	for (u64 i = 0; i < end; i++) {
		s32 width = (s32)(((u32)(ce_eband[i + 1] - ce_eband[i]) * (u32)C) << OE_LM);
		s32 w2 = width > 48 ? width : 48;
		s32 w8 = (s32)((u32)width << 3);
		u32 quanta = (u32)(w8 < w2 ? w8 : w2);
		s64 lpg = dlogp, boost = 0, j = 0;
		s64 cap = (s64)(u32)o->cap[i], want = (s64)(u32)o->want[i];
		while (tell + (lpg << 3) < total && boost < cap) {
			u32 flag = j < want;
			oec_bit_logp(ec, flag, (u32)lpg);
			tell = oec_tell_frac(ec);
			if (!flag)
				break;
			boost += quanta;
			total -= quanta;
			lpg = 1;
			j++;
		}
		o->offs[i] = (s32)boost;
		if (j) {
			dlogp--;
			if (dlogp < 2)
				dlogp = 2;
		}
	}
	o->tot8 = total;
	/* trim */
	o->trim = trim(o);
	if (!(oec_tell_frac(ec) + 48 > total))
		oec_icdf(ec, (u32)o->trim, ce_trim_icdf, 7);
	else
		o->trim = 5;
	/* allocation : bits restants (1/8), sans réserve d'anti-effondrement */
	alloc(o, (s32)((s64)(o->nbytes << 6) - oec_tell_frac(ec) - 1));
	fine(o);
	bands(o);
	final(o);
	oec_done(ec);
	/* paquet : TOC + trame */
	o->pkt[0] = (u8)o->toc;
	page_add(o, o->pkt, o->nbytes + 1);
	o->pkts++;
	o->frames++;
}

/* ---------------------------------------------------------------------------
 * Rééchantillonneur (fréquence d'entrée -> 48 kHz) : sinus cardinal fenêtré
 * (Blackman), noyau en table (OE_RSZ passages par zéro de chaque côté, OE_RSR
 * pas par passage, interpolation linéaire) ; coupure à 0,455 x la plus petite
 * des deux fréquences. La sortie m tombe à la position m fs / 48000 de
 * l'entrée (entier rsip + reste rsfrac / 48000) ; noyau symétrique : pas de
 * retard.
 * ------------------------------------------------------------------------- */

/* poids (2 Ti flottants), reste (sur 48000) : poids des prises
 * t = (m - Ti + 1) - reste / 48000 */
static void rs_weights(const struct oe *o, float *w, u64 rem)
{
	float r = (float)rem * ak_u2f(0x37AEC33E); /* 1 / 48000 */
	float step = o->rsstep, gain = o->rsgain;
	const float *g = o->rsgk;
	s64 ti = o->rsti;
	u64 n = (u64)ti << 1;
	for (u64 m = 0; m < n; m++) {
		float t = (float)((s64)m - ti + 1) - r;
		t = ak_fabs(t);
		t = t * step; /* position dans la table */
		u32 k = ak_fcvtzu_w(t);
		float v = 0.0f;
		if (k < OE_RSZ * OE_RSR) {
			float fr = t - (float)k;
			float a = g[k], b = g[k + 1];
			v = ak_fmadd(b - a, fr, a);
			v = v * gain;
		}
		w[m] = v;
	}
}

static void rs_init(struct oe *o)
{
	double f = 48000.0 / (double)o->rate; /* 2 fc = 0,91 min(1, 48000 / fs) */
	if (!(f <= 1.0))
		f = 1.0;
	f = f * 0.91;
	o->rsgain = (float)f;
	o->rsstep = (float)(128.0 * f);
	s64 ti = ak_dcvtzs_x(ak_drintp(32.0 / f)) + 1; /* demi-largeur : ceil(Z / 2 fc) + 1 */
	o->rsti = ti;
	o->rsbase = -ti; /* historique : Ti zéros avant le début */
	o->rscnt = ti;
	o->rsip = 0;
	o->rsfrac = 0;
	o->rslim = (s64)(~(u64)0 >> 1);
	/* noyau : g[j] = sinc(j / R) blackman(j / (Z R)), j < Z R */
	float *g = o->rsgk;
	g[0] = 1.0f;
	for (u64 j = 1; j < OE_RSZ * OE_RSR; j++) {
		double sn, cs, x = (double)j * 0.02454369260617026; /* pi u */
		ak_sincos(x, &sn, &cs);
		double sc = sn / x; /* sinc */
		x = (double)j * 0.0007669903939428206; /* pi x */
		ak_sincos(x, &sn, &cs);
		double c1 = cs; /* cos(pi x) */
		ak_sincos(x + x, &sn, &cs); /* cos(2 pi x) */
		double w = ak_dmadd(c1, 0.5, 0.42); /* 0,42 + 0,5 cos + 0,08 cos 2 */
		w = ak_dmadd(cs, 0.08, w);
		sc = sc * w;
		g[j] = (float)sc;
	}
	/* phases : 48000 / pgcd ; leurs poids en table si elle tient */
	u64 a = o->rate, b = 48000;
	do {
		u64 r = ak_udiv(a, b);
		r = a - r * b;
		a = b;
		b = r;
	} while (b);
	u64 gcd = a, ph = ak_udiv(48000, gcd);
	if (((u64)o->rsti << 1) * ph > OE_RSWTN)
		return;
	o->rsg = gcd;
	u64 step = (u64)o->rsti << 1; /* flottants d'une phase */
	float *w = o->rswt;
	for (u64 rem = 0; ph; ph--) {
		rs_weights(o, w, rem);
		w += step;
		rem += o->rsg;
	}
}

/* les sorties calculables (vers feed), puis l'historique inutile part */
static void rs_run(struct oe *o)
{
	s64 ip, ti, base, cnt;
	for (;;) {
		ip = o->rsip;
		ti = o->rsti;
		base = o->rsbase;
		cnt = o->rscnt;
		if (ip + ti - base >= cnt) /* ip + Ti dans l'historique ? */
			break;
		if (o->len48 >= o->rslim)
			break;
		/* poids de la phase : en table, sinon calculés */
		const float *w;
		if (o->rsg)
			w = o->rswt + ak_udiv(o->rsfrac, o->rsg) * ((u64)ti << 1);
		else {
			rs_weights(o, o->rsw, o->rsfrac);
			w = o->rsw;
		}
		u64 n = (u64)ti << 1;
		const float *h = o->rsh[0] + (ip - ti + 1 - base); /* H[ip - Ti + 1 - base + m] */
		float s0 = 0.0f;
		u64 m = 0;
		do
			s0 = ak_fmadd(h[m], w[m], s0);
		while (++m < n);
		float s1 = s0;
		if (o->c == 2) {
			s1 = 0.0f;
			h += OE_RSHB;
			m = 0;
			do
				s1 = ak_fmadd(h[m], w[m], s1);
			while (++m < n);
		}
		o->len48++;
		feed(o, s0, s1);
		u64 fr = o->rsfrac + o->rate; /* position suivante */
		s64 p = o->rsip;
		while (fr >= 48000) {
			fr -= 48000;
			p++;
		}
		o->rsfrac = fr;
		o->rsip = p;
	}
	/* garde l'historique depuis ip - Ti + 1 */
	s64 d = ip - ti + 1 - base; /* décalage */
	if (d <= 0)
		return;
	if (!(d < cnt))
		d = cnt;
	s64 r = cnt - d; /* restants */
	memmove(o->rsh[0], o->rsh[0] + d, (u64)r << 2);
	if (o->c == 2)
		memmove(o->rsh[1], o->rsh[1] + d, (u64)r << 2);
	o->rsbase = base + d;
	o->rscnt = r;
}

/* PCM s16 entrelacé, trames : dans l'historique, puis les sorties */
static void rs_push(struct oe *o, const s16 *pcm, u64 frames)
{
	u64 C = o->c;
	while (frames) {
		u64 cnt = (u64)o->rscnt;
		u64 n = OE_RSHB - cnt;
		if (!(n < frames))
			n = frames;
		float *h0 = o->rsh[0] + cnt, *h1 = h0 + OE_RSHB;
		for (u64 j = 0; j < n; j++) {
			h0[j] = (float)pcm[0];
			if (C == 2)
				h1[j] = (float)pcm[1];
			pcm += C;
		}
		o->rscnt = (s64)(cnt + n);
		frames -= n;
		rs_run(o);
	}
}

/* fin : sorties jusqu'à ceil(entrée x 48000 / fs), l'entrée prolongée de zéros */
static void rs_flush(struct oe *o)
{
	o->rslim = (s64)ak_udiv(o->nin * 48000 + o->rate - 1, o->rate);
	u64 cnt = (u64)o->rscnt;
	u64 z = (u64)o->rsti + 2; /* zéros */
	float *h0 = o->rsh[0] + cnt, *h1 = h0 + OE_RSHB;
	for (u64 j = 0; j < z; j++) {
		h0[j] = 0.0f;
		h1[j] = 0.0f;
	}
	o->rscnt = (s64)(cnt + z);
	rs_run(o);
}
