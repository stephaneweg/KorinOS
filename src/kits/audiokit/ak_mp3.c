/* AudioKit : MP3, MPEG-1 (ISO/IEC 11172-3), MPEG-2 et 2.5 (13818-3,
 * fréquences moitié) couche III : décodage (ak_mp3.inc).
 *
 * Trame : en-tête, CRC sauté, informations de côté, données principales avec
 * le réservoir de bits ; facteurs d'échelle (scfsi ; LSF), Huffman (tables
 * 0-31 et quadruplets, ak_mp3tab.c), requantification (|x|^4/3 et 2^(q/4)),
 * stéréo M/S et d'intensité (MPEG-1 et 2), blocs courts et mixtes,
 * anticrénelage, IMDCT 36/12 et fenêtres, recouvrement, inversion de
 * fréquence, banc de synthèse polyphase (matrice N et fenêtre D : NEON dans
 * l'asm, une voie après l'autre ici ; fmla = ak_fmadd). Sortie : flottants
 * plans (AKD_F32). Fichier : synchronisation, en-tête Xing/Info (nombre de
 * trames, table de recherche), ID3v1 à la fin ; recherche par la table Xing
 * ou au prorata. Mêmes opérations dans le même ordre que l'asm. */
#include "ak_mp3.h"

/* ---- état (MP_*) : disposition de l'asm */

/* informations d'un granule (GI_*, 128 octets) */
struct mp3gi {
	u32 p23;    /* 0 */
	u32 bigv;   /* 4 */
	u32 gain;   /* 8 */
	u32 sfc;    /* 12 */
	u32 wsf;    /* 16 */
	u32 bt;	    /* 20 */
	u32 mix;    /* 24 */
	u32 ts[3];  /* 28 */
	u32 sbg[3]; /* 40 */
	u32 r0;	    /* 52 */
	u32 r1;	    /* 56 */
	u32 pre;    /* 60 */
	u32 scale;  /* 64 */
	u32 c1t;    /* 68 */
	u8 pad[128 - 72];
};
AK_STATIC_OFF(struct mp3gi, ts, 28);
AK_STATIC_OFF(struct mp3gi, sbg, 40);
AK_STATIC_OFF(struct mp3gi, c1t, 68);
_Static_assert(sizeof(struct mp3gi) == 128, "mp3gi");

#define MP_SIZE 1280
#define MP_RESMAX 4096
#define MP_MAINMAX 8192
#define MP_POW43N 8208
#define MP_ARRAYS (MP_POW43N * 4 + MP_RESMAX + MP_MAINMAX + 2304 + 4608 + 4608 + 8192 + 2304 + 256 + 64)
#define MP_SYNC_MASK 0xFFFE0C00u /* synchro, version, couche, fréquence */

struct mp3 {
	s64 ver;	   /* 0 1 MPEG-1, 2 MPEG-2, 3 MPEG-2.5 */
	s64 lsf;	   /* 8 */
	s64 sri;	   /* 16 fréquence : indice 0..8 des tables */
	s64 rate;	   /* 24 */
	s64 mode;	   /* 32 0 stéréo, 1 stéréo jointe, 2 double, 3 mono */
	s64 modext;	   /* 40 */
	s64 nch;	   /* 48 */
	s64 flen;	   /* 56 octets de la trame */
	s64 crc;	   /* 64 */
	s64 br;		   /* 72 kbit/s */
	s64 ngr;	   /* 80 */
	s64 mdb;	   /* 88 main_data_begin */
	s64 reslen;	   /* 96 octets du réservoir */
	s64 vpos[2];	   /* 104 synthèse : position dans V (2 canaux) */
	u8 pad0[8];	   /* 120 */
	u8 scfsi[8];	   /* 128 2 x 4 octets */
	u8 pad1[120];	   /* 136 */
	struct mp3gi gr[4]; /* 256 granules : [gr][canal] */
	u8 sf[2][64];	   /* 768 facteurs : longs 0..21, courts 24 + 3 sfb + f */
	u8 ismax[64];	   /* 896 LSF : position d'intensité interdite par bande */
	u64 nz[2];	   /* 960 lignes non nulles (2 canaux) */
	const u16 *sfbl;   /* 976 débuts des bandes longues (23) */
	const u16 *sfbs;   /* 984 débuts des bandes courtes (14) */
	s64 tocok;	   /* 992 table Xing présente */
	s64 frames;	   /* 1000 trames (Xing) */
	float *pow43;	   /* 1008 tableaux */
	u8 *res;	   /* 1016 réservoir (MP_RESMAX) */
	u8 *main;	   /* 1024 données principales de la trame */
	s32 *is;	   /* 1032 valeurs de Huffman (576) */
	float *xr;	   /* 1040 spectre (2 x 576) */
	float *ovl;	   /* 1048 recouvrement (2 x 576) */
	float *v;	   /* 1056 synthèse (2 x 1024) */
	float *sbs;	   /* 1064 sorties hybrides (18 x 32) */
	float *tmp;	   /* 1072 64 flottants (ou octets : liste des facteurs LSF) */
	s64 spf;	   /* 1080 trames par trame MP3 (1152 / 576) */
	s64 side;	   /* 1088 octets d'informations de côté */
	s64 hdr;	   /* 1096 en-tête de référence (masque de synchro) */
	u8 toc[100];	   /* 1104 table Xing */
	u8 pad2[4];	   /* 1204 */
	s64 mixl;	   /* 1208 blocs mixtes : lignes longues (36, 72 à 8 kHz) */
	u8 pad3[MP_SIZE - 1216];
};
AK_STATIC_OFF(struct mp3, vpos, 104);
AK_STATIC_OFF(struct mp3, scfsi, 128);
AK_STATIC_OFF(struct mp3, gr, 256);
AK_STATIC_OFF(struct mp3, sf, 768);
AK_STATIC_OFF(struct mp3, ismax, 896);
AK_STATIC_OFF(struct mp3, nz, 960);
AK_STATIC_OFF(struct mp3, sfbl, 976);
AK_STATIC_OFF(struct mp3, tocok, 992);
AK_STATIC_OFF(struct mp3, pow43, 1008);
AK_STATIC_OFF(struct mp3, tmp, 1072);
AK_STATIC_OFF(struct mp3, spf, 1080);
AK_STATIC_OFF(struct mp3, toc, 1104);
AK_STATIC_OFF(struct mp3, mixl, 1208);
_Static_assert(sizeof(struct mp3) == MP_SIZE, "mp3");

static const u32 mp3_rates[9] = {44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000};
static const u16 mp3_brtab[32] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0,
				  0, 8,	 16, 24, 32, 40, 48, 56, 64,  80,  96,	112, 128, 144, 160, 0};

static inline float tf(const u32 *t, u64 i)
{
	return ak_u2f(t[i]);
}

static long mp3_pkt(struct akd *d, const u8 *pkt, u64 n);
static void mp3_reset(struct akd *d);
static long mp3_next(struct akd *d);
static long mp3_seek(struct akd *d, u64 fr);

/* ---------------------------------------------------------------------------
 *  Ouverture */

/* décodeur -> état (0 : mémoire) */
static struct mp3 *mp3_new(struct akd *d)
{
	struct mp3 *m = akd_alloc(d, MP_SIZE);
	if (!m)
		return 0;
	d->codec = m;
	u8 *a = akd_alloc(d, MP_ARRAYS);
	if (!a)
		return 0;
	m->pow43 = (float *)a;
	a += MP_POW43N * 4;
	m->res = a;
	a += MP_RESMAX;
	m->main = a;
	a += MP_MAINMAX;
	m->is = (s32 *)a;
	a += 2304;
	m->xr = (float *)a;
	a += 4608;
	m->ovl = (float *)a;
	a += 4608;
	m->v = (float *)a;
	a += 8192;
	m->sbs = (float *)a;
	a += 2304;
	m->tmp = (float *)a;
	/* |i|^(4/3) = i * racine cubique (Newton en double) */
	float *p = m->pow43;
	p[0] = 0;
	double y = 1.0; /* racine cubique de i (gardée d'un i au suivant) */
	for (u64 i = 1; i < MP_POW43N; i++) {
		double x = (double)(s64)i;
		for (int k = 0; k < 4; k++) { /* y - (y^3 - i) / (3 y^2) */
			double y2 = y * y;
			double t = y2 * y;
			t = t - x;
			y2 = y2 * 3.0;
			t = t / y2;
			y = y - t;
		}
		p[i] = (float)(x * y);
	}
	if (akd_planes(d, 1152, 2) < 0)
		return 0;
	d->pcmfmt = AKD_F32;
	d->fmt = AKF_MP3;
	d->pktfn = mp3_pkt;
	d->resetfn = mp3_reset;
	return m;
}

/* oublie le réservoir, le recouvrement et la synthèse (pas MP_VPOS, comme l'asm) */
static void mp3_reset(struct akd *d)
{
	struct mp3 *m = d->codec;
	m->reslen = 0;
	memset(m->ovl, 0, 4608);
	memset(m->v, 0, 8192);
}

/* décodeur, données (rien), octets (API par paquets) -> 0 / AKE_NOMEM */
long mp3_init_pkt(struct akd *d, const u8 *extra, u64 n)
{
	(void)extra;
	(void)n;
	return mp3_new(d) ? 0 : AKE_NOMEM;
}

static long mp3_frame(struct akd *d, const u8 *p, u64 n, u64 *flen);

/* décodeur, paquet (trames MP3), octets -> trames */
static long mp3_pkt(struct akd *d, const u8 *pkt, u64 n)
{
	u64 fl;
	long r = mp3_frame(d, pkt, n, &fl);
	if (r >= 0) {
		struct mp3 *m = d->codec;
		d->rate = m->rate;
		d->ch = m->nch;
	}
	return r;
}

/* en-tête (gros-boutiste, lu tel quel), état -> octets de la trame (0 :
 * en-tête invalide, rien n'est posé) ; pose ver .. side */
static u64 mp3_hdr(u32 h, struct mp3 *m)
{
	if ((h >> 21) != 0x7FF)
		return 0;
	u32 ver = (h >> 19) & 3; /* 0 2.5, 2 MPEG-2, 3 MPEG-1 */
	if (ver == 1)
		return 0;
	if (((h >> 17) & 3) != 1) /* couche III = 01 */
		return 0;
	u32 bri = (h >> 12) & 15;
	if (!bri || bri == 15) /* (format libre : non) */
		return 0;
	u32 fr = (h >> 10) & 3;
	if (fr == 3)
		return 0;
	u32 v = 1, sri = 0;
	if (ver != 3) {
		v = 2;
		sri = 3;
		if (!ver) {
			v = 3;
			sri = 6;
		}
	}
	sri += fr;
	m->ver = v;
	u32 lsf = v != 1;
	m->lsf = lsf;
	m->sri = sri;
	u64 rate = mp3_rates[sri];
	m->rate = (s64)rate;
	u64 br = mp3_brtab[lsf * 16 + bri];
	m->br = (s64)br;
	u32 mode = (h >> 6) & 3;
	m->mode = mode;
	m->modext = (h >> 4) & 3;
	u32 nch = mode == 3 ? 1 : 2;
	m->nch = nch;
	m->crc = ((h >> 16) & 1) ^ 1;
	m->ngr = lsf ? 1 : 2;
	m->spf = lsf ? 576 : 1152;
	/* informations de côté : MPEG-1 17 / 32, LSF 9 / 17 */
	u32 side = nch == 1 ? 17 : 32;
	if (lsf)
		side = nch == 1 ? 9 : 17;
	m->side = side;
	/* octets = 144000 * débit / fréquence (MPEG-1), 72000 (LSF) + bourrage */
	u64 f = lsf ? 72000 : 144000;
	f = f * br / rate;
	f += (h >> 9) & 1;
	m->flen = (s64)f;
	return f;
}

/* avance jusqu'à une trame valide suivie d'une autre compatible (ou de la
 * fin) -> 0 (MP_* posés), < 0 : rien dans le 1 Mo */
static long mp3_sync(struct akd *d)
{
	struct mp3 *m = d->codec;
	u64 skipped = 0;
	u32 h;
	for (;;) {
		s64 rem = d->dataend - d->off;
		if (rem < 4)
			return -1;
		u64 avail;
		const u8 *p = akd_need(d, 8192, &avail);
		if (avail < (u64)rem)
			rem = (s64)avail;
		if (rem < 4)
			return -1;
		h = ak_rd32be(p);
		u64 fl = mp3_hdr(h, m);
		if (fl) {
			if (fl + 4 > (u64)rem)
				break; /* (fin des données : acceptée) */
			u32 h2 = ak_rd32be(p + fl);
			if ((h2 & MP_SYNC_MASK) == (h & MP_SYNC_MASK) && mp3_hdr(h2, m)) { /* l'autre doit être valide aussi */
				mp3_hdr(h, m); /* (en-tête de la première) */
				break;
			}
		}
		akd_skip(d, 1);
		skipped++;
		if (skipped >= 1048576)
			return -1;
	}
	m->hdr = h & MP_SYNC_MASK;
	return 0;
}

/* décodeur (lecture au début des trames) -> 0 / < 0 */
long mp3_open(struct akd *d)
{
	struct mp3 *m = mp3_new(d);
	if (!m)
		return AKE_NOMEM;
	/* ID3v1 à la fin */
	if ((u64)d->dataend >= 128) {
		s64 back = d->off;
		akd_seek(d, (u64)d->dataend - 128);
		u64 avail;
		const u8 *t = akd_need(d, 128, &avail);
		if (avail >= 128 && akd_id3v1(d, t))
			d->dataend -= 128;
		akd_seek(d, (u64)back);
	}
	/* première trame valide (suivie d'une autre du même genre) */
	if (mp3_sync(d) < 0)
		return AKE_FORMAT;
	d->data0 = d->off;
	d->rate = m->rate;
	d->ch = m->nch;
	d->bitrate = m->br * 1000;
	/* en-tête Xing / Info dans la première trame ? */
	u64 avail;
	const u8 *f = akd_need(d, (u64)m->flen, &avail);
	if (avail >= (u64)m->flen) {
		const u8 *x = f + m->side + 4 + (m->crc << 1); /* après les informations de côté */
		u32 w = ak_rd32le(x);
		s64 vbr;
		if (w == 0x676E6958) /* 'Xing' : débit variable */
			vbr = 1;
		else if (w == 0x6F666E49) /* 'Info' : débit constant (au prorata) */
			vbr = 0;
		else
			goto noxing;
		u32 fl = ak_rd32be(x + 4); /* drapeaux */
		x += 8;
		if (fl & 1) { /* trames */
			u32 nf = ak_rd32be(x);
			x += 4;
			m->frames = nf;
			d->total = (s64)((u64)nf * (u64)m->spf);
		}
		if (fl & 2) /* octets */
			x += 4;
		if (fl & 4) { /* table de recherche (100 o ; lue au-delà de la trame si elle est courte, comme l'asm) */
			memmove(m->toc, x, 100);
			m->tocok = vbr;
		}
		akd_skip(d, (u64)m->flen); /* la trame Xing n'a pas de son */
		d->data0 = d->off;
		if (m->frames) { /* débit moyen */
			u64 b = ((u64)(d->dataend - d->data0) << 3) * (u64)m->rate;
			d->bitrate = (s64)ak_udiv(b, (u64)d->total);
			goto total;
		}
	}
noxing:
	/* durée estimée au débit de la première trame */
	d->total = (s64)(ak_udiv((u64)(d->dataend - d->data0), (u64)m->flen) * (u64)m->spf);
total:
	d->next = mp3_next;
	d->seekfn = mp3_seek;
	return 0;
}

/* décodeur -> trames décodées (0 : fin) */
static long mp3_next(struct akd *d)
{
	struct mp3 *m = d->codec;
	u64 tries = 0;
	for (;;) {
		s64 rem = d->dataend - d->off;
		if (rem < 4)
			return 0;
		u64 avail;
		const u8 *p = akd_need(d, 4096, &avail);
		if (avail < (u64)rem)
			rem = (s64)avail;
		if (rem < 4)
			return 0;
		if ((ak_rd32be(p) & MP_SYNC_MASK) == (u32)m->hdr) { /* même genre de trame qu'au début ? */
			u64 fl;
			long r = mp3_frame(d, p, (u64)rem, &fl);
			if (r >= 0) {
				akd_skip(d, fl);
				return r;
			}
		}
		tries++;
		if (tries >= 64)
			return 0;
		akd_skip(d, 1);
		if (mp3_sync(d) < 0)
			return 0;
	}
}

/* décodeur, trame voulue -> 0 (à la trame MP3 près) */
static long mp3_seek(struct akd *d, u64 fr)
{
	struct mp3 *m = d->codec;
	s64 total = d->total;
	u64 first; /* première trame décodée (échantillon) */
	if (total < 1)
		goto start;
	u64 bytes = (u64)(d->dataend - d->data0); /* octets de son */
	u64 d0 = (u64)d->data0;
	u64 spf = (u64)m->spf;
	u64 pos;
	if (m->tocok) { /* table Xing (8 trames plus tôt) : pourcentage -> 1/256 des octets */
		u64 e = 8 * spf;
		u64 k = (s64)fr < (s64)e ? 0 : fr - e;
		k = ak_udiv(k * 100, (u64)total);
		if (k > 99)
			k = 99;
		pos = ((bytes * m->toc[k]) >> 8) + d0;
		/* position à la trame : celle du pourcentage */
		k = ak_udiv(k * (u64)total, 100);
		k = ak_udiv(k, spf);
		first = k * spf;
	} else {
		/* au prorata (débit constant), 8 trames plus tôt : le réservoir de bits
		 * se remplit avant la trame voulue (elles sont sautées) */
		u64 k = ak_udiv(fr, spf);
		k = (s64)k < 8 ? 0 : k - 8;
		first = k * spf;
		pos = ak_udiv(bytes * first, (u64)total) + d0;
	}
	akd_seek(d, pos);
	if (mp3_sync(d) < 0)
		goto start;
	if (!m->tocok) { /* débit constant : numéro de la trame trouvée, d'après sa position */
		u64 x = (u64)(d->off - d->data0);
		x *= ak_udiv((u64)d->total, spf); /* trames */
		u64 b = (u64)(d->dataend - d->data0);
		x += b >> 1; /* (arrondi) */
		x = ak_udiv(x, b);
		first = x * spf;
	}
	goto done;
start:
	akd_seek(d, (u64)d->data0);
	first = 0;
done:
	mp3_reset(d);
	s64 sk = (s64)(fr - first); /* le reste : sauté */
	if (sk < 0)
		sk = 0;
	d->skip = sk;
	d->pos = (s64)(first + (u64)sk);
	return 0;
}

/* ---------------------------------------------------------------------------
 *  Une trame */

static long mp3_side(struct mp3 *m, struct br *b);
static void mp3_granules(struct akd *d, const u8 *main, u64 n);

/* décodeur, trame, octets disponibles -> trames (< 0 : erreur), *flen =
 * octets de la trame (posé si >= 0) */
static long mp3_frame(struct akd *d, const u8 *p, u64 n, u64 *flen)
{
	struct mp3 *m = d->codec;
	if (n < 4)
		return AKE_CORRUPT;
	u64 fl = mp3_hdr(ak_rd32be(p), m);
	if (!fl)
		return AKE_CORRUPT;
	if (fl > n)
		return AKE_CORRUPT;
	u64 sri = (u64)m->sri; /* tables de bandes de cette fréquence */
	m->sfbl = mp3_sfb_long + sri * 24;
	m->sfbs = mp3_sfb_short + sri * 16;
	m->mixl = sri == 8 ? 72 : 36;
	/* informations de côté */
	const u8 *side = p + 4 + (m->crc << 1);
	const u8 *md = side + m->side; /* début des données principales */
	struct br b;
	br_init(&b, side, (u64)m->side);
	if (mp3_side(m, &b) < 0)
		return AKE_CORRUPT;
	/* données principales : réservoir + celles de cette trame */
	s64 own = (s64)((uintptr_t)p + fl - (uintptr_t)md); /* octets de cette trame */
	if (own < 0)
		return AKE_CORRUPT;
	u64 mdb = (u64)m->mdb, res = (u64)m->reslen;
	int lack = 0; /* réservoir insuffisant */
	if (mdb > res) {
		lack = 1;
		mdb = 0;
	}
	memmove(m->main, m->res + res - mdb, mdb);
	memmove(m->main + mdb, md, (u64)own);
	/* le réservoir garde la fin (au plus MP_RESMAX octets) */
	if (res + (u64)own > MP_RESMAX) {
		u64 drop = res + (u64)own - MP_RESMAX;
		memmove(m->res, m->res + drop, res - drop);
		res -= drop;
	}
	memmove(m->res + res, md, (u64)own);
	m->reslen = (s64)(res + (u64)own);
	if (!lack) {
		mp3_granules(d, m->main, mdb + (u64)own);
	} else {
		/* pas assez de réservoir (début de flux ou après une recherche) : du
		 * silence (les deux plans, même en mono, comme l'asm) */
		memset(d->pcm[0], 0, (u64)m->spf << 2);
		memset(d->pcm[1], 0, (u64)m->spf << 2);
	}
	*flen = fl;
	return m->spf;
}

/* état, lecteur sur les informations de côté -> 0 / < 0 */
static long mp3_side(struct mp3 *m, struct br *b)
{
	u64 lsf = (u64)m->lsf, nch = (u64)m->nch;
	m->mdb = (s64)br_bits(b, lsf ? 8 : 9);
	/* bits privés */
	u64 k = nch == 1 ? 5 : 3;
	if (lsf)
		k = nch == 1 ? 1 : 2;
	br_bits(b, k);
	if (!lsf) /* scfsi (MPEG-1) */
		for (u64 c = 0; c < nch; c++)
			for (u64 i = 0; i < 4; i++)
				m->scfsi[c * 4 + i] = (u8)br_bits(b, 1);
	for (u64 g = 0; g < (u64)m->ngr; g++) {
		for (u64 c = 0; c < nch; c++) {
			struct mp3gi *gi = &m->gr[c + (g << 1)];
			gi->p23 = (u32)br_bits(b, 12);
			u32 v = (u32)br_bits(b, 9);
			if (v > 288)
				return AKE_CORRUPT;
			gi->bigv = v;
			gi->gain = (u32)br_bits(b, 8);
			gi->sfc = (u32)br_bits(b, lsf ? 9 : 4);
			v = (u32)br_bits(b, 1);
			gi->wsf = v;
			if (v) {
				v = (u32)br_bits(b, 2);
				if (!v) /* (type 0 interdit ici) */
					return AKE_CORRUPT;
				gi->bt = v;
				gi->mix = (u32)br_bits(b, 1);
				gi->ts[0] = (u32)br_bits(b, 5);
				gi->ts[1] = (u32)br_bits(b, 5);
				gi->ts[2] = 0;
				gi->sbg[0] = (u32)br_bits(b, 3);
				gi->sbg[1] = (u32)br_bits(b, 3);
				gi->sbg[2] = (u32)br_bits(b, 3);
				/* régions implicites : 7 (8 pour les blocs courts non mixtes), le reste */
				gi->r0 = (gi->bt == 2 && !gi->mix) ? 8 : 7;
				gi->r1 = 20;
			} else {
				gi->bt = 0;
				gi->mix = 0;
				gi->sbg[0] = 0;
				gi->sbg[1] = 0;
				gi->sbg[2] = 0;
				gi->ts[0] = (u32)br_bits(b, 5);
				gi->ts[1] = (u32)br_bits(b, 5);
				gi->ts[2] = (u32)br_bits(b, 5);
				gi->r0 = (u32)br_bits(b, 4);
				gi->r1 = (u32)br_bits(b, 3);
			}
			gi->pre = 0;
			if (!lsf)
				gi->pre = (u32)br_bits(b, 1);
			gi->scale = (u32)br_bits(b, 1);
			gi->c1t = (u32)br_bits(b, 1);
		}
	}
	return 0;
}

/* bits (0 : 0) -> valeur (.get de l'asm) */
static inline u64 sf_get(struct rb *r, u64 k)
{
	return k ? rb_get(r, k) : 0;
}

/* facteurs d'échelle du canal ch, granule gr */
static void mp3_scalefactors(struct mp3 *m, struct mp3gi *gi, struct rb *r, u64 ch, u64 gr)
{
	u8 *sf = m->sf[ch];
	if (!m->lsf) {
		u64 s1 = mp3_slen[gi->sfc], s2 = mp3_slen[16 + gi->sfc];
		if (gi->bt == 2) {
			u64 k = 0;
			if (gi->mix) { /* mixte : longs 0..7 (slen1), courts 3..5 (slen1), 6..11 (slen2) */
				for (k = 0; k < 8; k++)
					sf[k] = (u8)sf_get(r, s1);
				k = 3;
			}
			for (; k < 12; k++) {
				u64 o = k * 3 + 24;
				sf[o] = (u8)sf_get(r, k < 6 ? s1 : s2);
				sf[o + 1] = (u8)sf_get(r, k < 6 ? s1 : s2);
				sf[o + 2] = (u8)sf_get(r, k < 6 ? s1 : s2);
			}
			sf[60] = 0; /* bande 12 : 0 */
			sf[61] = 0;
			sf[62] = 0;
			return;
		}
		/* groupes 0-5, 6-10, 11-15, 16-20 ; scfsi au second granule */
		for (u64 k = 0; k < 21; k++) {
			u64 g = k < 6 ? 0 : k < 11 ? 1 : k < 16 ? 2 : 3;
			if (gr && m->scfsi[ch * 4 + g])
				continue;
			sf[k] = (u8)sf_get(r, g < 2 ? s1 : s2);
		}
		sf[21] = 0;
		return;
	}
	/* MPEG-2 : facteurs d'après scalefac_compress (13818-3) */
	u32 sfc = gi->sfc;
	u8 slen[4];
	u64 ti;
	u32 pre = 0;
	u32 isr = (u32)m->modext & (m->mode == 1) & 1 & (ch == 1); /* intensité, canal droit */
	if (!isr) {
		if (sfc < 400) {
			u32 a = sfc >> 4;
			slen[0] = (u8)(a / 5);
			slen[1] = (u8)(a % 5);
			slen[2] = (u8)((sfc >> 2) & 3);
			slen[3] = (u8)(sfc & 3);
			ti = 0;
		} else if (sfc < 500) {
			u32 x = sfc - 400, a = x >> 2;
			slen[0] = (u8)(a / 5);
			slen[1] = (u8)(a % 5);
			slen[2] = (u8)(x & 3);
			slen[3] = 0;
			ti = 1;
		} else {
			u32 x = sfc - 500;
			slen[0] = (u8)(x / 3);
			slen[1] = (u8)(x % 3);
			slen[2] = 0;
			slen[3] = 0;
			ti = 2;
			pre = 1;
		}
	} else {
		u32 x = sfc >> 1;
		if (x < 180) {
			slen[0] = (u8)(x / 36);
			u32 y = x % 36;
			slen[1] = (u8)(y / 6);
			slen[2] = (u8)(y % 6);
			slen[3] = 0;
			ti = 3;
		} else if (x < 244) {
			x -= 180;
			slen[0] = (u8)((x >> 4) & 3);
			slen[1] = (u8)((x >> 2) & 3);
			slen[2] = (u8)(x & 3);
			slen[3] = 0;
			ti = 4;
		} else {
			x -= 244;
			slen[0] = (u8)(x / 3);
			slen[1] = (u8)(x % 3);
			slen[2] = 0;
			slen[3] = 0;
			ti = 5;
		}
	}
	gi->pre = pre;
	/* nombre de bandes : nsf[table][genre de bloc][4] */
	u64 bk = 0; /* longs */
	if (gi->bt == 2)
		bk = gi->mix ? 2 : 1;
	const u8 *nsf = mp3_nsf + ti * 12 + (bk << 2);
	/* lire les facteurs dans une liste (MP_TMP, octets), avec le maximum
	 * interdit de chacun (MP_TMP + 128) */
	u8 *lst = (u8 *)m->tmp, *mx = lst + 128;
	u64 n = 0;
	for (u64 g = 0; g < 4; g++) {
		u32 cnt = nsf[g], bits = slen[g];
		while (cnt) {
			lst[n] = (u8)sf_get(r, bits);
			u32 f = (1u << (bits & 31)) - 1; /* position interdite = 2^slen - 1 (aucune si slen = 0) */
			if (!bits)
				f = 255;
			mx[n] = (u8)f;
			n++;
			cnt--;
		}
	}
	/* liste -> bandes longues ou courtes (et l'intensité au même rang) */
	u8 *ism = m->ismax;
	if (!bk) {
		for (u64 k = 0; k < 21; k++) {
			sf[k] = lst[k];
			ism[k] = mx[k];
		}
		sf[21] = 0;
		return;
	}
	u64 i = 0, k = 0;
	if (bk == 2) { /* mixte : 6 longues puis courtes depuis 3 */
		for (k = 0; k < 6; k++, i++) {
			sf[k] = lst[i];
			ism[k] = mx[i];
		}
		k = 3;
	}
	for (; k < 12; k++) {
		for (u64 o = k * 3; o < k * 3 + 3; o++, i++) {
			sf[o + 24] = lst[i];
			ism[o + 24] = mx[i];
		}
	}
	sf[60] = 0;
	sf[61] = 0;
	sf[62] = 0;
}

/* position du lecteur en registres (bits depuis le début des données) */
static inline u64 rb_bitpos(const struct rb *r, const u8 *start)
{
	return (((u64)((uintptr_t)r->p - (uintptr_t)start)) << 3) - r->n;
}

/* Huffman : granule, lecteur, début des données, fin (bits), canal ->
 * MP_IS[576] (entiers signés), MP_NZ[canal] = lignes jusqu'à la dernière non nulle */
static void mp3_huffman(struct mp3 *m, const struct mp3gi *gi, struct rb *r, const u8 *start, u64 end, u64 ch)
{
	s32 *is = m->is;
	/* régions : débuts (lignes) */
	u32 bigend = gi->bigv << 1; /* fin des grandes valeurs */
	if (bigend > 576)
		bigend = 576;
	const u16 *sl = m->sfbl;
	u32 r1, r2;
	if (gi->wsf) {
		/* blocs spéciaux : région 1 à la ligne 36 (courts et mixtes ; 72 à
		 * 8 kHz), à la bande longue 8 sinon ; pas de 2 */
		if (gi->bt == 2)
			r1 = m->sri == 8 ? 72 : 36;
		else
			r1 = sl[8];
		r2 = 576;
	} else {
		u32 a = gi->r0 + 1;
		if (a > 22)
			a = 22;
		r1 = sl[a];
		a = a + gi->r1 + 1;
		if (a > 22)
			a = 22;
		r2 = sl[a];
	}
	u32 line = 0;
	while (line < bigend) {
		/* table de la région */
		u32 rend = bigend, t;
		if (line < r1) {
			if (r1 < rend)
				rend = r1;
			t = gi->ts[0];
		} else if (line < r2) {
			if (r2 < rend)
				rend = r2;
			t = gi->ts[1];
		} else {
			t = gi->ts[2];
		}
		const u16 *dir = mp3_huffdir + ((u64)t << 2);
		u64 bits0 = dir[1], lb = dir[2];
		if (!bits0) { /* table 0 : des zéros */
			while (line < rend) {
				is[line] = 0;
				is[line + 1] = 0;
				line += 2;
			}
			continue;
		}
		const u16 *tab = mp3_huff + dir[0];
		while (line < rend) {
			if (r->n < 48)
				rb_fill(r);
			const u16 *tb = tab;
			u64 nb = bits0;
			u32 e;
			for (;;) {
				e = tb[AK_SHR(r->cache, 64 - nb)];
				if (!(e & 0x8000))
					break;
				r->cache = AK_SHL(r->cache, nb); /* nœud : sous-table */
				r->n -= nb;
				tb = tab + (e & 0x7FF);
				nb = (e >> 11) & 15;
			}
			u64 len = (e >> 8) & 15;
			r->cache = AK_SHL(r->cache, len);
			r->n -= len;
			u64 x = (e >> 4) & 15, y = e & 15;
			if (x) {
				if (x == 15 && lb) {
					x += AK_SHR(r->cache, 64 - lb);
					r->cache = AK_SHL(r->cache, lb);
					r->n -= lb;
				}
				if (r->cache >> 63) /* signe */
					x = 0 - x;
				r->cache <<= 1;
				r->n -= 1;
			}
			if (y) {
				if (y == 15 && lb) {
					y += AK_SHR(r->cache, 64 - lb);
					r->cache = AK_SHL(r->cache, lb);
					r->n -= lb;
				}
				if (r->cache >> 63)
					y = 0 - y;
				r->cache <<= 1;
				r->n -= 1;
			}
			is[line] = (s32)x;
			is[line + 1] = (s32)y;
			line += 2;
		}
	}
	/* quadruplets jusqu'à la fin des bits du granule */
	while (line <= 572) {
		if (rb_bitpos(r, start) >= end)
			break;
		if (r->n < 32)
			rb_fill(r);
		u32 q;
		if (!gi->c1t) { /* table A */
			u32 e = mp3_quadA[AK_SHR(r->cache, 64 - MP3_QUADA_BITS)];
			u64 l = (e >> 8) & 15;
			r->cache = AK_SHL(r->cache, l);
			r->n -= l;
			q = e & 15;
		} else { /* table B : 4 bits inversés */
			q = (u32)(r->cache >> 60);
			r->cache <<= 4;
			r->n -= 4;
			q = ~q & 15;
		}
		s32 v[4]; /* v, w, x, y (bits 3..0) */
		for (int k = 3; k >= 0; k--) {
			s32 s = (s32)((q >> k) & 1);
			if (s) {
				if (r->cache >> 63)
					s = -s;
				r->cache <<= 1;
				r->n -= 1;
			}
			v[3 - k] = s;
		}
		if (rb_bitpos(r, start) > end) /* dépasse la fin : quadruplet ignoré */
			break;
		is[line] = v[0];
		is[line + 1] = v[1];
		is[line + 2] = v[2];
		is[line + 3] = v[3];
		line += 4;
	}
	/* le reste à zéro */
	memset(is + line, 0, (u64)(576 - line) << 2);
	/* dernière ligne non nulle */
	u64 nz = line;
	while (nz && !is[nz - 1])
		nz--;
	m->nz[ch] = nz;
}

/* lignes [*line, hi[ avec q ; *line avance (.band de l'asm) */
static void rq_band(const struct mp3 *m, float *xr, u32 *line, u32 hi, s32 q)
{
	s32 e = q >> 2; /* 2^(q/4) = 2^(q>>2) * 2^((q&3)/4) */
	u32 bits = mp3_pow2q[q & 3] + ((u32)e << 23);
	if (e < -120)
		bits = 0;
	float s1 = ak_u2f(bits);
	const s32 *is = m->is;
	const float *p43 = m->pow43;
	for (u32 i = *line; i < hi; i++) {
		s32 v = is[i];
		u32 a = v < 0 ? 0 - (u32)v : (u32)v;
		if (a > MP_POW43N - 1)
			a = MP_POW43N - 1;
		float s = p43[a] * s1;
		if (v < 0)
			s = ak_fneg(s);
		xr[i] = s;
		*line = i + 1;
	}
}

/* granule, canal : MP_IS -> MP_XR[canal] (ordre de la norme) ;
 * xr = signe * |is|^4/3 * 2^(q / 4) */
static void mp3_requant(struct mp3 *m, const struct mp3gi *gi, u64 ch)
{
	float *xr = m->xr + ch * 576;
	const u8 *sf = m->sf[ch];
	s32 base = (s32)gi->gain - 210; /* q de base */
	u32 mult = (gi->scale + 1) << 1; /* multiplicateur : 2 ou 4 (quarts) */
	const u8 *pre = gi->pre ? mp3_pretab : 0;
	u32 line = 0;
	u64 b;
	if (gi->bt == 2) {
		b = 0;
		if (gi->mix) { /* mixte : bandes longues jusqu'à la ligne 36 (MPEG-1 : 8 bandes ; LSF : 6) */
			const u16 *sl = m->sfbl;
			u32 mixl = (u32)m->mixl;
			for (b = 0; sl[b] < mixl; b++) {
				u32 v = sf[b];
				if (pre)
					v += pre[b];
				s32 q = base - (s32)(v * mult);
				u32 hi = sl[b + 1];
				if (hi > mixl)
					hi = mixl;
				rq_band(m, xr, &line, hi, q);
			}
			b = 3; /* puis les courtes depuis la bande 3 */
		}
		for (; b < 13; b++) {
			const u16 *ss = m->sfbs;
			u32 f0 = ss[b], w = ss[b + 1] - f0; /* largeur */
			line = f0 * 3; /* début (3 fenêtres) */
			for (u64 win = 0; win < 3; win++) {
				s32 q = base - (s32)(gi->sbg[win] << 3);
				u32 s = 0;
				if (b < 12)
					s = sf[b * 3 + win + 24];
				q = q - (s32)(s * mult);
				rq_band(m, xr, &line, line + w, q);
			}
		}
		return;
	}
	const u16 *sl = m->sfbl;
	for (b = 0; b < 22; b++) {
		u32 v = 0;
		if (b < 21) {
			v = sf[b];
			if (pre)
				v += pre[b];
		}
		s32 q = base - (s32)(v * mult);
		rq_band(m, xr, &line, sl[b + 1], q);
	}
}

/* M/S sur [lo, hi[ */
static void ms_lines(float *L, float *R, u64 lo, u64 hi)
{
	float c = ak_u2f(0x3F3504F3); /* 1 / racine de 2 */
	for (u64 i = lo; i < hi; i++) {
		float l = L[i], r = R[i];
		float a = l + r, s = l - r;
		L[i] = a * c;
		R[i] = s * c;
	}
}

/* lignes [lo, hi[, position pos, position interdite forbid (LSF) (.apply de l'asm) */
static void is_apply(const struct mp3 *m, const struct mp3gi *g1, float *L, float *R, u64 modext, u32 lo, u32 hi,
		     u32 pos, u32 forbid)
{
	float a, b; /* gauche, droite */
	if (!m->lsf) {
		if (pos >= 7)
			goto ms; /* 7 : pas d'intensité (M/S éventuel) */
		a = tf(mp3_is_l, pos);
		b = tf(mp3_is_r, pos);
	} else {
		if (pos == forbid)
			goto ms;
		const u32 *t = mp3_is_lsf + ((g1->sfc & 1) << 5); /* échelle : bit 0 de scalefac_compress */
		a = 1.0f;
		b = 1.0f;
		if (pos) {
			float s4 = tf(t, (pos + 1) >> 1);
			if (pos & 1)
				a = s4; /* impaire : gauche réduite */
			else
				b = tf(t, pos >> 1);
		}
	}
	for (u32 i = lo; i < hi; i++) {
		float s0 = L[i];
		float r = s0 * b;
		L[i] = s0 * a;
		R[i] = r;
	}
	return;
ms:
	if (modext & 2) /* M/S dans cette bande */
		ms_lines(L, R, lo, hi);
}

/* stéréo d'intensité : granule du canal 0 -> première ligne en intensité
 * (bandes longues ; pour les blocs courts, M/S s'applique sous la limite de
 * chaque fenêtre : approchée par la plus basse) */
static u64 mp3_intensity(struct mp3 *m, const struct mp3gi *g0, float *L, float *R, u64 modext)
{
	const struct mp3gi *g1 = g0 + 1; /* granule du canal droit */
	const u8 *sfr = m->sf[1];	 /* facteurs du canal droit */
	u32 nz = (u32)m->nz[1];		 /* dernière ligne non nulle à droite */
	u64 lim = 576;
	u32 pos, forbid;
	if (g1->bt != 2) {
		/* longues : la première bande au-dessus de la dernière ligne non nulle */
		const u16 *sl = m->sfbl;
		u64 b = 0;
		for (;;) {
			if (b >= 22)
				return lim;
			if (sl[b + 1] >= nz)
				break;
			b++;
		}
		if (nz) /* bande suivant celle de la dernière non nulle */
			b++;
		lim = sl[b];
		if (b >= 22)
			return lim;
		u64 first = b; /* première bande en intensité */
		for (; b < 22; b++) {
			u32 lo = sl[b], hi = sl[b + 1];
			u64 k = b;
			if (k >= 21) { /* bande 21 : la position de la 20 si elle est en intensité, sinon celle par défaut */
				k = 20;
				if (first > 20) {
					pos = m->lsf ? 0 : 3;
					forbid = 255;
					goto lap;
				}
			}
			pos = sfr[k];
			forbid = m->ismax[k];
		lap:
			is_apply(m, g1, L, R, modext, lo, hi, pos, forbid);
		}
		return lim;
	}
	/* courtes : par fenêtre, au-dessus de la dernière ligne non nulle de la
	 * fenêtre (droite, d'après le spectre requantifié) */
	const u16 *ss = m->sfbs;
	for (u64 w = 0; w < 3; w++) {
		u64 b = 12;
		for (;;) {
			u32 f0 = ss[b], wd = ss[b + 1] - f0;
			u32 lo = f0 * 3 + wd * (u32)w, hi = lo + wd;
			for (u32 i = lo; i < hi; i++)
				if (ak_f2u(R[i])) /* (-0 compte : bits non nuls, comme l'asm) */
					goto found;
			if (!b) {
				b = (u64)-1;
				goto found;
			}
			b--;
		}
	found:;
		u64 first = b + 1; /* première bande en intensité */
		b = first;
		u64 t = (u64)ss[b] * 3;
		if (lim > t)
			lim = t;
		for (; b < 13; b++) {
			u32 f0 = ss[b], wd = ss[b + 1] - f0;
			u32 lo = f0 * 3 + wd * (u32)w, hi = lo + wd;
			u64 k = b;
			if (k >= 12) { /* bande 12 : comme la 21 des longues */
				k = 11;
				if (first > 11) {
					pos = m->lsf ? 0 : 3;
					forbid = 255;
					goto sap;
				}
			}
			k = k * 3 + w + 24;
			pos = sfr[k];
			forbid = m->ismax[k];
		sap:
			is_apply(m, g1, L, R, modext, lo, hi, pos, forbid);
		}
	}
	return lim;
}

/* granule : stéréo jointe (M/S, intensité) sur MP_XR */
static void mp3_stereo(struct mp3 *m, u64 gr)
{
	const struct mp3gi *g0 = &m->gr[gr << 1];
	float *L = m->xr, *R = m->xr + 576;
	u64 modext = (u64)m->modext;
	u64 lim = 576; /* limite de M/S (intensité au-dessus) */
	if (modext & 1)
		lim = mp3_intensity(m, g0, L, R, modext);
	if (modext & 2)
		ms_lines(L, R, 0, lim); /* (NEON par 4 dans l'asm : mêmes opérations par voie) */
}

/* granule, canal : MP_XR -> MP_SBS (18 x 32) : blocs courts réordonnés,
 * anticrénelage, IMDCT, fenêtre, recouvrement, inversion */
static void mp3_hybrid(struct mp3 *m, const struct mp3gi *gi, u64 ch)
{
	float *xr = m->xr + ch * 576, *ovl = m->ovl + ch * 576, *sbs = m->sbs, *z = m->tmp;
	u32 bt = gi->bt, mix = gi->mix;
	u64 na; /* anticrénelage : sous-bandes 1..na */
	if (bt == 2) {
		/* blocs courts : (bande, fenêtre, f) -> (sous-bande, fenêtre, k)
		 * par une copie du spectre court (MP_IS : tampon libre de 576 x 4) */
		u32 s0 = mix ? (u32)m->mixl : 0; /* début de la partie courte */
		float *cp = (float *)m->is;
		memmove(cp, xr + s0, (u64)(576 - s0) << 2);
		const u16 *ss = m->sfbs;
		for (u64 b = mix ? 3 : 0; b < 13; b++) {
			u32 f0 = ss[b], wd = ss[b + 1] - f0;
			for (u32 w = 0; w < 3; w++) {
				for (u32 f = 0; f < wd; f++) {
					/* source : 3 f0 + w largeur + f (depuis le début de la partie courte) */
					u32 src = f0 * 3 + w * wd + f - s0;
					float v = cp[src];
					/* destination : 18 sb + 6 w + k, avec f0 + f = 6 sb + k */
					u32 t = f0 + f, sb = t / 6, k = t % 6;
					xr[sb * 18 + w * 6 + k] = v;
				}
			}
		}
		if (!mix)
			goto imdct; /* court pur : pas d'anticrénelage */
		na = (u64)m->mixl / 18 - 1; /* mixte : entre les sous-bandes longues */
	} else {
		na = 31;
	}
	for (u64 sb = 1; sb <= na; sb++) { /* anticrénelage entre sb-1 et sb */
		u64 o = sb * 18;
		for (u64 i = 0; i < 8; i++) {
			float a = xr[o - 1 - i], b = xr[o + i];
			float cs = tf(mp3_cs, i), ca = tf(mp3_ca, i);
			float s4 = a * cs;
			s4 = ak_fmsub(b, ca, s4); /* a cs - b ca */
			float s5 = b * cs;
			s5 = ak_fmadd(a, ca, s5); /* b cs + a ca */
			xr[o - 1 - i] = s4;
			xr[o + i] = s5;
		}
	}
imdct:
	for (u64 sb = 0; sb < 32; sb++) {
		const float *x = xr + sb * 18; /* entrée (18) */
		float *ov = ovl + sb * 18;     /* recouvrement (18) */
		u32 t = bt;		       /* genre de bloc de cette sous-bande */
		if (bt == 2 && mix && sb * 18 < (u64)m->mixl)
			t = 0; /* mixte : longues en fenêtre normale */
		if (t != 2) {
			/* longue : z[i] = somme X[k] c36[k][i] (NEON, 9 accumulateurs) */
			float acc[36];
			for (int i = 0; i < 36; i++)
				acc[i] = 0.0f;
			const u32 *c = mp3_imdct36;
			for (u64 k = 0; k < 18; k++, c += 36) {
				float s = x[k];
				for (int i = 0; i < 36; i++)
					acc[i] = ak_fmadd(ak_u2f(c[i]), s, acc[i]);
			}
			const u32 *wn = mp3_win + t * 36; /* fenêtre du type de bloc */
			for (int i = 0; i < 36; i++)
				z[i] = acc[i] * ak_u2f(wn[i]);
		} else {
			/* courte : 3 IMDCT de 12, fenêtrées, placées à 6 + 6 w */
			for (int i = 0; i < 36; i++)
				z[i] = 0.0f;
			for (u64 w = 0; w < 3; w++) {
				float acc[12];
				for (int i = 0; i < 12; i++)
					acc[i] = 0.0f;
				const u32 *c = mp3_imdct12;
				const float *xw = x + w * 6; /* X de la fenêtre */
				for (u64 k = 0; k < 6; k++, c += 12) {
					float s = xw[k];
					for (int i = 0; i < 12; i++)
						acc[i] = ak_fmadd(ak_u2f(c[i]), s, acc[i]);
				}
				for (int i = 0; i < 12; i++)
					acc[i] = acc[i] * tf(mp3_win, 2 * 36 + i);
				float *zz = z + 6 + 6 * w;
				for (int i = 0; i < 12; i++)
					zz[i] = zz[i] + acc[i];
			}
		}
		/* sortie = z[0..17] + recouvrement ; recouvrement = z[18..35] */
		for (u64 i = 0; i < 18; i++) {
			float s = z[i] + ov[i];
			ov[i] = z[i + 18];
			if ((sb & 1) && (i & 1)) /* sous-bande impaire, échantillon impair : signe inversé */
				s = ak_fneg(s);
			sbs[(i << 5) + sb] = s; /* SBS[t][sb] */
		}
	}
}

/* canal, granule : synthèse polyphase de MP_SBS (18 tranches de 32
 * sous-bandes) -> AKD_PCM[canal] + 576 granule ;
 * V[0..63] = N . S ; sortie[j] = somme_i V[...] D[32 i + j] */
static void mp3_synth(struct akd *d, struct mp3 *m, u64 ch, u64 gr)
{
	float *out = (float *)d->pcm[ch] + gr * 576;
	float *V = m->v + (ch << 10); /* V du canal (1024 flottants) */
	s64 *vp = &m->vpos[ch];
	const float *s = m->sbs; /* tranche */
	for (u64 t = 0; t < 18; t++, s += 32, out += 32) {
		u64 pos = ((u64)*vp - 64) & 1023; /* position : V recule de 64 */
		*vp = (s64)pos;
		/* V = N . S (16 accumulateurs : 64 valeurs) */
		float acc[64];
		for (int j = 0; j < 64; j++)
			acc[j] = 0.0f;
		const u32 *nn = mp3_N;
		for (u64 k = 0; k < 32; k++, nn += 64) {
			float x = s[k];
			if (x == 0.0f) /* (fcmp : -0 aussi) */
				continue;
			for (int j = 0; j < 64; j++)
				acc[j] = ak_fmadd(ak_u2f(nn[j]), x, acc[j]);
		}
		memcpy(V + pos, acc, sizeof acc);
		/* fenêtre : 16 tranches de 32 V, à 128 m (i pair) ou 128 m + 96 (impair) */
		float o[32];
		for (int j = 0; j < 32; j++)
			o[j] = 0.0f;
		const u32 *dd = mp3_D;
		for (u64 i = 0; i < 16; i++, dd += 32) {
			u64 off = (i >> 1) << 7;
			if (i & 1)
				off += 96;
			off = (off + pos) & 1023;
			const float *v = V + off;
			for (int j = 0; j < 32; j++)
				o[j] = ak_fmadd(v[j], ak_u2f(dd[j]), o[j]);
		}
		memcpy(out, o, sizeof o);
	}
}

/* décodeur, données principales, octets : facteurs, Huffman,
 * requantification, stéréo, synthèse de chaque granule */
static void mp3_granules(struct akd *d, const u8 *main, u64 n)
{
	struct mp3 *m = d->codec;
	u64 pos = 0; /* position (bits) dans les données */
	for (u64 gr = 0; gr < (u64)m->ngr; gr++) {
		for (u64 ch = 0; ch < (u64)m->nch; ch++) {
			struct mp3gi *gi = &m->gr[ch + (gr << 1)];
			/* lecteur en registres à la position pos */
			const u8 *q = main + (pos >> 3);
			s64 left = (s64)((uintptr_t)main + n - (uintptr_t)q);
			if (left < 0)
				left = 0;
			struct rb r;
			rb_init(&r, q, (u64)left);
			if (pos & 7)
				rb_get(&r, pos & 7);
			u64 end = pos + gi->p23; /* fin des bits du granule */
			mp3_scalefactors(m, gi, &r, ch, gr);
			mp3_huffman(m, gi, &r, main, end, ch);
			mp3_requant(m, gi, ch);
			pos = end;
		}
		if (m->mode == 1) /* stéréo jointe */
			mp3_stereo(m, gr);
		for (u64 ch = 0; ch < (u64)m->nch; ch++) {
			mp3_hybrid(m, &m->gr[ch + (gr << 1)], ch);
			mp3_synth(d, m, ch, gr);
		}
	}
}
