/* AudioKit : Vorbis I, décodage (spécification Xiph ; ak_vorbis.inc, table
 * ak_vbtab.inc). Mêmes opérations dans le même ordre que l'asm, mêmes codes
 * d'erreur, mêmes dispositions des blocs d'état.
 *
 * En-têtes : identification, commentaires (étiquettes), configuration (livres
 * de codes : longueurs ordonnées ou non, creuses, codes construits dans
 * l'ordre de la spécification, recherche rapide sur 10 bits puis dichotomie ;
 * quantification vectorielle 1 et 2 précalculée ; plancher 1 ; résidus 0, 1,
 * 2 ; correspondances avec couplage ; modes). Paquet : plancher 1 (courbe,
 * table de dB inverse), résidu (8 passes, classifications), couplage inverse,
 * IMDCT (ak_mdct.c), fenêtres de Vorbis (pentes courtes / longues),
 * recouvrement du centre de la fenêtre précédente au centre de la courante.
 * Le plancher 0 (abandonné par les encodeurs depuis 2000) n'est pas pris en
 * charge. Bits lus poids faible d'abord : en-têtes par un lecteur en mémoire
 * (lrm_*), paquets par le lecteur « en registres » de l'asm (lr_*).
 *
 * Écarts voulus (l'asm lirait ou écrirait hors de ses blocs) :
 *  * numéro de livre de codes >= nombre de livres (plancher : livre maître ou
 *    de sous-classe ; résidu : livre des classes ou d'une passe) : l'asm lit
 *    un livre hors du tableau (plantage) ; ici le plancher rend AKE_CORRUPT,
 *    le résidu s'arrête (comme une fin de paquet) ;
 *  * couplage vers un canal >= canaux + 2 (possible avec 5 canaux) : l'asm
 *    écrirait hors des spectres ; ici la configuration est refusée ;
 *  * résidu de type 2 sur plus de 2 canaux : l'asm entrelace canaux x n/2
 *    flottants dans VB_TMP (bs1 flottants seulement) ; ici VB_TMP est assez
 *    grand (même taille qu'avant jusqu'à 2 canaux).
 * Non reproduit : signe et charge des NaN (flux abîmés seulement : valeurs de
 * quantification infinies) ; voir ak_fp.h (NaN). */
#include "ak_vorbis.h"
#include "ak_mdct.h"

/* bloc de l'Ogg (ak_ogg.inc) : les champs que Vorbis touche */
#define OG_PREROLL 104
#define OG_STATE 136

/* ---- blocs d'état (dispositions de l'asm) */

/* LR_* : lecteur en mémoire (en-têtes) */
struct lrm {
	const u8 *ptr; /* 0 */
	const u8 *end; /* 8 */
	u64 cache;     /* 16 */
	u64 n;	       /* 24 */
	u64 eop;       /* 32 */
	u64 pad;
};
_Static_assert(sizeof(struct lrm) == 48, "lrm");

/* VCB_* : livre de codes */
struct vcb {
	u64 dim;	/* 0 */
	u64 entries;	/* 8 */
	u64 fastbits;	/* 16 */
	u32 *fast;	/* 24 2^bits mots : entrée << 8 | longueur, -1 : code plus long */
	u32 *sorted;	/* 32 codes cadrés à gauche (32 bits), triés */
	u32 *sortval;	/* 40 entrée de chacun */
	u8 *sortlen;	/* 48 longueur de chacun */
	u64 nsorted;	/* 56 */
	float *vq;	/* 64 vecteurs précalculés (entrées x dim), 0 : aucun */
	u8 *lens;	/* 72 */
	u8 pad[128 - 80];
};
#define VCB_SIZE 128
_Static_assert(sizeof(struct vcb) == VCB_SIZE, "vcb");
AK_STATIC_OFF(struct vcb, vq, 64);

/* VFL_* : plancher 1 */
struct vfl {
	u64 parts;	   /* 0 */
	u64 mult;	   /* 8 */
	u64 rangebits;	   /* 16 */
	u64 values;	   /* 24 */
	u64 range;	   /* 32 */
	u8 pad0[24];
	u8 pclass[32];	   /* 64 */
	u8 cdim[16];	   /* 96 */
	u8 csub[16];	   /* 112 */
	u8 cmaster[16];	   /* 128 */
	u16 subbook[16][8]; /* 144 (numéro - 1 ; 0xFFFF : aucun livre) */
	u16 x[65];	   /* 400 */
	u8 pad1[2];
	u8 sort[65];	   /* 532 ordre croissant de X */
	u8 pad2[3];
	u8 low[65];	   /* 600 */
	u8 pad3[3];
	u8 high[65];	   /* 668 */
	u8 pad4[768 - 733];
};
#define VFL_SIZE 768
_Static_assert(sizeof(struct vfl) == VFL_SIZE, "vfl");
AK_STATIC_OFF(struct vfl, pclass, 64);
AK_STATIC_OFF(struct vfl, subbook, 144);
AK_STATIC_OFF(struct vfl, x, 400);
AK_STATIC_OFF(struct vfl, sort, 532);
AK_STATIC_OFF(struct vfl, low, 600);
AK_STATIC_OFF(struct vfl, high, 668);

/* VRS_* : résidu */
struct vrs {
	u64 type;	 /* 0 */
	u64 begin;	 /* 8 */
	u64 end;	 /* 16 */
	u64 psize;	 /* 24 */
	u64 nclass;	 /* 32 */
	u64 classbook;	 /* 40 */
	u8 pad[16];
	u16 books[64][8]; /* 64 (0xFFFF : aucun) */
};
#define VRS_SIZE 1088
_Static_assert(sizeof(struct vrs) == VRS_SIZE, "vrs");
AK_STATIC_OFF(struct vrs, books, 64);

/* VBM_* : correspondance */
struct vbm {
	u64 submaps;   /* 0 */
	u64 steps;     /* 8 */
	u8 mag[256];   /* 16 */
	u8 ang[256];   /* 272 */
	u8 mux[256];   /* 528 */
	u8 floor[16];  /* 784 */
	u8 res[16];    /* 800 */
	u8 pad[832 - 816];
};
#define VBM_SIZE 832
_Static_assert(sizeof(struct vbm) == VBM_SIZE, "vbm");
AK_STATIC_OFF(struct vbm, mux, 528);
AK_STATIC_OFF(struct vbm, res, 800);

/* VB_* : état du décodeur */
struct vb {
	u64 ch;		   /* 0 */
	u64 rate;	   /* 8 */
	u64 bs0;	   /* 16 */
	u64 bs1;	   /* 24 */
	u64 ncb;	   /* 32 */
	struct vcb *cb;	   /* 40 */
	u64 nfloor;	   /* 48 */
	struct vfl *floor; /* 56 */
	u64 nres;	   /* 64 */
	struct vrs *res;   /* 72 */
	u64 nmap;	   /* 80 */
	struct vbm *map;   /* 88 */
	u64 nmode;	   /* 96 */
	u64 modebits;	   /* 104 */
	u64 hdr;	   /* 112 en-têtes lus (bits 0, 1, 2) */
	u64 prevn;	   /* 120 taille du bloc précédent (0 : aucun) */
	float *prev;	   /* 128 moitiés droites précédentes (canaux x bs1/2) */
	float *resid;	   /* 136 spectres (canaux x bs1/2) */
	float *tmp;	   /* 144 sortie d'IMDCT (bs1) */
	u8 *classif;	   /* 152 classifications (8 x 8192 octets) */
	float *slope0;	   /* 160 pentes de fenêtre (bs0/2, bs1/2) */
	float *slope1;	   /* 168 */
	s32 *y;		   /* 176 valeurs du plancher (canaux x 65 x 32 bits) */
	u8 *used;	   /* 184 plancher utilisé (canaux, octets) */
	u8 *donot;	   /* 192 résidu à ne pas décoder (canaux, octets) */
	u64 flags;	   /* 200 bloc : bit 0 long, bit 1 précédent long, bit 2 suivant long */
	u64 n;		   /* 208 taille du bloc courant */
	u8 pad[256 - 216];
	struct {
		u64 blockflag;
		u64 mapping;
	} modes[64];	   /* 256 */
	struct akmdct md0; /* 1280 IMDCT de bs0 */
	struct akmdct md1; /* 1504 IMDCT de bs1 */
};
#define VB_SIZE (1280 + 2 * MD_SIZE)
_Static_assert(sizeof(struct vb) == VB_SIZE, "vb");
AK_STATIC_OFF(struct vb, hdr, 112);
AK_STATIC_OFF(struct vb, donot, 192);
AK_STATIC_OFF(struct vb, n, 208);
AK_STATIC_OFF(struct vb, modes, 256);
AK_STATIC_OFF(struct vb, md0, 1280);
AK_STATIC_OFF(struct vb, md1, 1280 + MD_SIZE);

static const u32 vb_invdb[256];
static const u32 vfl_ranges[4] = {256, 128, 86, 64};

/* ---- état */

/* décodeur -> 0 / < 0 : état Vorbis dans OG_STATE (ou d->codec) */
long vb_new(struct akd *d)
{
	void *s = akd_alloc(d, VB_SIZE);
	if (!s)
		return AKE_NOMEM;
	if (d->codec)
		*(void **)((u8 *)d->codec + OG_STATE) = s;
	else
		d->codec = s; /* (API par paquets : pas d'Ogg) */
	return 0;
}

void *vb_state(struct akd *d)
{
	if (d->mode == 2)
		return d->codec;
	return *(void **)((u8 *)d->codec + OG_STATE);
}

void vb_reset(struct akd *d)
{
	struct vb *s = vb_state(d);
	s->prevn = 0;
}

/* ---- lecteurs de bits, poids faible d'abord */

static void lrm_init(struct lrm *r, const u8 *p, u64 n)
{
	r->ptr = p;
	r->end = p + n;
	r->cache = 0;
	r->n = 0;
	r->eop = 0;
}

/* bits (0..32) -> valeur ; au-delà de la fin : 0 et eop */
static u64 lrm_get(struct lrm *r, u64 k)
{
	u64 c = r->cache, n = r->n;
	const u8 *p = r->ptr;
	while (n < k) {
		if ((uintptr_t)p >= (uintptr_t)r->end) {
			r->eop = 1;
			n += 64; /* (des zéros) */
			break;
		}
		c |= AK_SHL(*p++, n);
		n += 8;
	}
	if (!k)
		return 0;
	u64 v = c & ~AK_SHL(~(u64)0, k);
	r->cache = AK_SHR(c, k);
	r->n = n - k;
	r->ptr = p;
	return v;
}

/* lecteur « en registres » de l'asm : x9 cache, x10 bits, x11 octet suivant, x12 fin */
struct lr {
	u64 cache;
	u64 n;
	uintptr_t p;
	uintptr_t end;
};

/* remplit le cache (au moins 57 bits) */
static void lr_fill(struct lr *r)
{
	if (r->n > 56)
		return;
	if (r->p + 8 <= r->end) {
		r->cache |= AK_SHL(ak_load64((const u8 *)r->p), r->n);
		u64 k = (64 - r->n) >> 3;
		r->p += k;
		r->n += k << 3;
		return;
	}
	while (r->n <= 56) {
		u64 v = 0;
		if (r->p < r->end)
			v = *(const u8 *)r->p;
		r->cache |= AK_SHL(v, r->n);
		r->p++;
		r->n += 8;
	}
}

/* bits (1..32) -> valeur */
static u64 lr_get(struct lr *r, u64 k)
{
	if (r->n < k)
		lr_fill(r);
	u64 v = r->cache & ~AK_SHL(~(u64)0, k);
	r->cache = AK_SHR(r->cache, k);
	r->n -= k;
	return v;
}

/* fin du paquet dépassée ? (macro lr_eop) */
static int lr_eop(const struct lr *r)
{
	return (s64)(((u64)(r->p - r->end) << 3) - r->n) > 0;
}

static u32 rbit32(u32 v)
{
	v = ((v >> 1) & 0x55555555u) | ((v & 0x55555555u) << 1);
	v = ((v >> 2) & 0x33333333u) | ((v & 0x33333333u) << 2);
	v = ((v >> 4) & 0x0F0F0F0Fu) | ((v & 0x0F0F0F0Fu) << 4);
	return __builtin_bswap32(v);
}

/* livre -> entrée (-1 : code invalide) */
static s64 vcb_decode(const struct vcb *b, struct lr *r)
{
	if (r->n < 32)
		lr_fill(r);
	u32 fb = (u32)b->fastbits;
	u64 idx = r->cache & ~AK_SHL(~(u64)0, fb);
	u32 w = b->fast[idx];
	if (w != 0xFFFFFFFFu) {
		u32 l = w & 0xFF;
		r->cache = AK_SHR(r->cache, l);
		r->n -= l;
		return w >> 8;
	}
	u32 key = rbit32((u32)r->cache); /* 32 bits, le premier en tête */
	u64 ns = b->nsorted;
	if (!ns)
		return -1;
	u64 lo = 0, hi = ns;
	while (hi - lo > 1) {
		u64 mid = (lo + hi) >> 1;
		if (b->sorted[mid] <= key)
			lo = mid;
		else
			hi = mid;
	}
	u32 l = b->sortlen[lo];
	r->cache = AK_SHR(r->cache, l);
	r->n -= l;
	return b->sortval[lo];
}

/* ---- en-têtes */

static long vb_setup(struct akd *d, struct vb *s, struct lrm *r);

/* décodeur, paquet, octets -> 0 / < 0 */
long vb_header(struct akd *d, const u8 *pkt, u64 n)
{
	struct vb *s = vb_state(d);
	struct lrm r;
	if (n < 7)
		return AKE_CORRUPT;
	u32 type = pkt[0];
	if (ak_rd32le(pkt + 1) != 0x62726F76 || ak_rd16le(pkt + 5) != 0x7369) /* "vorbis" */
		return AKE_CORRUPT;
	lrm_init(&r, pkt + 7, n - 7);
	if (type == 1) {
		if (lrm_get(&r, 32)) /* version */
			return AKE_CORRUPT;
		u64 ch = lrm_get(&r, 8);
		if (!ch)
			return AKE_CORRUPT;
		if (ch > 8) /* (8 canaux au plus : AudioKit) */
			return AKE_FORMAT;
		s->ch = ch;
		d->ch = (s64)ch;
		u64 rate = lrm_get(&r, 32);
		if (!rate)
			return AKE_CORRUPT;
		s->rate = rate;
		d->rate = (s64)rate;
		lrm_get(&r, 32);		     /* débit maximal */
		d->bitrate = (s64)lrm_get(&r, 32); /* nominal */
		lrm_get(&r, 32);		     /* minimal */
		u64 b0 = lrm_get(&r, 4);
		u64 b1 = lrm_get(&r, 4);
		if (b0 < 6 || b1 > 13 || b0 > b1) /* 64 <= bs0 <= bs1 <= 8192 */
			return AKE_CORRUPT;
		s->bs0 = (u64)1 << b0;
		s->bs1 = (u64)1 << b1;
		d->fmt = AKF_VORBIS;
		d->pcmfmt = AKD_F32;
		if (d->mode != 2) /* (Ogg : avance de décodage) */
			*(u64 *)((u8 *)d->codec + OG_PREROLL) = s->bs1;
		s->hdr |= 1;
		return 0;
	}
	if (type == 3) {
		akd_vcomment(d, pkt + 7, n - 7);
		s->hdr |= 2;
		return 0;
	}
	if (type == 5) {
		if (!(s->hdr & 1))
			return AKE_CORRUPT;
		long e = vb_setup(d, s, &r);
		if (e < 0)
			return e;
		s->hdr |= 4;
		return 0;
	}
	return AKE_CORRUPT;
}

/* ilog (bits nécessaires ; ilog(0) = 0) */
static u64 vb_ilog(u64 v)
{
	return 64 - ak_clz64(v);
}

static long vcb_setup(struct akd *d, struct lrm *r, struct vcb *b);
static long vfl_setup(struct lrm *r, struct vfl *f);
static long vrs_setup(struct lrm *r, struct vrs *rs);
static long vbm_setup(struct lrm *r, struct vbm *m, const struct vb *s);
static long vb_buffers(struct akd *d, struct vb *s);

/* décodeur, état, lecteur sur la configuration -> 0 / < 0 */
static long vb_setup(struct akd *d, struct vb *s, struct lrm *r)
{
	long e;
	/* livres de codes */
	u64 n = lrm_get(r, 8) + 1;
	s->ncb = n;
	s->cb = akd_alloc(d, VCB_SIZE * n);
	if (!s->cb)
		return AKE_NOMEM;
	for (u64 i = 0; i < n; i++)
		if ((e = vcb_setup(d, r, &s->cb[i])) < 0)
			return e;
	/* transformées temporelles (inutilisées) */
	n = lrm_get(r, 6) + 1;
	while (n) {
		if (lrm_get(r, 16))
			return AKE_CORRUPT;
		n--;
	}
	/* planchers */
	n = lrm_get(r, 6) + 1;
	s->nfloor = n;
	s->floor = akd_alloc(d, VFL_SIZE * n);
	if (!s->floor)
		return AKE_NOMEM;
	for (u64 i = 0; i < n; i++)
		if ((e = vfl_setup(r, &s->floor[i])) < 0)
			return e;
	/* résidus */
	n = lrm_get(r, 6) + 1;
	s->nres = n;
	s->res = akd_alloc(d, VRS_SIZE * n);
	if (!s->res)
		return AKE_NOMEM;
	for (u64 i = 0; i < n; i++)
		if ((e = vrs_setup(r, &s->res[i])) < 0)
			return e;
	/* correspondances */
	n = lrm_get(r, 6) + 1;
	s->nmap = n;
	s->map = akd_alloc(d, VBM_SIZE * n);
	if (!s->map)
		return AKE_NOMEM;
	for (u64 i = 0; i < n; i++)
		if ((e = vbm_setup(r, &s->map[i], s)) < 0)
			return e;
	/* modes */
	n = lrm_get(r, 6) + 1;
	s->nmode = n;
	s->modebits = vb_ilog(n - 1);
	for (u64 i = 0; i < n; i++) {
		s->modes[i].blockflag = lrm_get(r, 1);
		if (lrm_get(r, 16))
			return AKE_CORRUPT;
		if (lrm_get(r, 16))
			return AKE_CORRUPT;
		u64 m = lrm_get(r, 8);
		if (m >= s->nmap)
			return AKE_CORRUPT;
		s->modes[i].mapping = m;
	}
	if (!lrm_get(r, 1)) /* bit de trame */
		return AKE_CORRUPT;
	if (r->eop)
		return AKE_CORRUPT;
	/* tampons, IMDCT, pentes de fenêtre */
	return vb_buffers(d, s);
}

/* entier de 32 bits -> flottant (float32_unpack de la spécification) */
static float vb_float32(u64 v)
{
	u64 m = v & 0x1FFFFF; /* mantisse */
	u64 e = (v >> 21) & 0x3FF;
	double f = (double)m;
	if (v & 0x80000000u)
		f = -f;
	s64 be = (s64)e - 788 + 1023; /* 2^(e - 788) en double */
	if (be < 1)
		return 0.0f;
	f = f * ak_u2d((u64)be << 52);
	return (float)f;
}

/* entrées, dimensions -> plus grand r tel que r^dim <= entrées */
static u64 vb_lookup1(u64 e, u64 dim)
{
	u64 lo = 0, hi = e + 1; /* bas (r^dim <= e), haut (exclu) */
	while (hi - lo > 1) {
		u64 r = (lo + hi) >> 1;
		u64 p = 1; /* r^dim, plafonné */
		for (u64 k = dim; k; k--) {
			p *= r;
			if (p > e)
				break;
		}
		if (p <= e)
			lo = r;
		else
			hi = r;
	}
	return lo;
}

/* tableau de 64 bits, nombre : tri croissant (Shell, écarts de Knuth) */
static void vb_sort64(u64 *a, u64 n)
{
	u64 g = 1; /* 1, 4, 13, 40... */
	for (;;) {
		u64 t = g * 3 + 1;
		if (t >= n)
			break;
		g = t;
	}
	for (;;) {
		if (!g)
			return;
		for (u64 i = g; i < n; i++) {
			u64 v = a[i];
			u64 j = i;
			while (j >= g && a[j - g] > v) {
				a[j] = a[j - g];
				j -= g;
			}
			a[j] = v;
		}
		if (g == 1)
			return;
		g = (g - 1) / 3;
	}
}

/* décodeur, livre -> 0 / < 0 : codes de la spécification (le plus à gauche
 * disponible), liste triée des codes cadrés à gauche, table rapide */
static long vcb_codes(struct akd *d, struct vcb *b)
{
	u32 mk[33]; /* marqueurs */
	memset(mk, 0, sizeof mk);
	u64 ent = b->entries;
	const u8 *lens = b->lens;
	/* entrées utilisées et plus grande longueur */
	u64 used = 0, maxl = 0;
	for (u64 i = 0; i < ent; i++) {
		if (!lens[i])
			continue;
		used++;
		if (lens[i] > maxl)
			maxl = lens[i];
	}
	b->nsorted = used;
	/* codes (64 bits : code cadré à gauche << 32 | entrée << 8 | longueur) */
	u64 *codes = akd_alloc(d, (used << 3) + 16);
	if (!codes)
		return AKE_NOMEM;
	u64 k = 0;
	for (u64 e = 0; e < ent; e++) {
		u32 len = lens[e];
		if (!len)
			continue;
		u32 code = mk[len];
		if (len < 32 && (code >> len)) /* (trop de codes de cette longueur) */
			return AKE_CORRUPT;
		u32 own = code;
		/* mise à jour des marqueurs */
		for (u64 j = len; j; j--) {
			u32 w = mk[j];
			if (w & 1) {
				if (j == 1)
					mk[1] = w + 1;
				else
					mk[j] = mk[j - 1] << 1;
				break;
			}
			mk[j] = w + 1;
		}
		for (u64 j = (u64)len + 1; j < 33; j++) {
			u32 w = mk[j];
			if ((w >> 1) != code)
				break;
			code = w;
			mk[j] = mk[j - 1] << 1;
		}
		u32 la = own << (32 - len); /* (len = 32 : décalage 0) */
		codes[k++] = ((u64)la << 32) | (e << 8) | len;
	}
	vb_sort64(codes, used);
	/* tableaux triés : code, entrée, longueur */
	u32 *sorted = akd_alloc(d, (used << 3) + used + 16);
	if (!sorted)
		return AKE_NOMEM;
	b->sorted = sorted;
	b->sortval = sorted + used;
	b->sortlen = (u8 *)(sorted + 2 * used);
	for (u64 i = 0; i < used; i++) {
		u64 c = codes[i];
		b->sorted[i] = (u32)(c >> 32);
		b->sortval[i] = (u32)((c >> 8) & 0xFFFFFF);
		b->sortlen[i] = (u8)c;
	}
	/* table rapide : 10 bits au plus, au moins 1 */
	u64 fb = maxl < 10 ? maxl : 10;
	if (fb < 1)
		fb = 1;
	b->fastbits = fb;
	u32 *fast = akd_alloc(d, (u64)4 << fb);
	if (!fast)
		return AKE_NOMEM;
	b->fast = fast;
	for (u64 i = (u64)1 << fb; i;) {
		i--;
		fast[i] = 0xFFFFFFFFu;
	}
	for (u64 i = 0; i < used; i++) {
		u64 c = codes[i];
		u64 len = c & 0xFF;
		if (len > fb)
			continue;
		u32 rev = rbit32((u32)(c >> 32)); /* premier bit du flux en bit 0 */
		u64 cnt = (u64)1 << (fb - len);	  /* suffixes */
		for (u64 j = 0; j < cnt; j++)
			fast[(j << len) | rev] = (u32)c;
	}
	return 0;
}

/* décodeur, lecteur, livre à remplir -> 0 / < 0 */
static long vcb_setup(struct akd *d, struct lrm *r, struct vcb *b)
{
	if (lrm_get(r, 24) != 0x564342)
		return AKE_CORRUPT;
	b->dim = lrm_get(r, 16);
	u64 ent = lrm_get(r, 24);
	b->entries = ent;
	if (!ent)
		return AKE_CORRUPT;
	u8 *lens = akd_alloc(d, ent + 16);
	if (!lens)
		return AKE_NOMEM;
	b->lens = lens;
	if (!lrm_get(r, 1)) { /* non ordonné */
		u64 sparse = lrm_get(r, 1);
		for (u64 i = 0; i < ent; i++) {
			if (sparse && !lrm_get(r, 1)) {
				lens[i] = 0;
				continue;
			}
			lens[i] = (u8)(lrm_get(r, 5) + 1);
		}
	} else {
		u64 len = lrm_get(r, 5) + 1; /* longueur courante */
		u64 i = 0;		     /* entrée courante */
		while (i < ent) {
			u64 e = i + lrm_get(r, vb_ilog(ent - i));
			if (e > ent)
				return AKE_CORRUPT;
			while (i < e)
				lens[i++] = (u8)len;
			len++;
			if (len > 32) /* (même après le dernier groupe, comme l'asm) */
				return AKE_CORRUPT;
		}
	}
	/* quantification vectorielle */
	u32 lt = (u32)lrm_get(r, 4);
	if (lt) {
		if (lt > 2)
			return AKE_CORRUPT;
		float mn = vb_float32(lrm_get(r, 32)); /* minimum */
		float dl = vb_float32(lrm_get(r, 32)); /* delta */
		u64 vbits = lrm_get(r, 4) + 1;	       /* bits des valeurs */
		u32 seq = (u32)lrm_get(r, 1);	       /* suite (sequence_p) */
		u64 dim = b->dim;
		u64 nv = lt == 1 ? vb_lookup1(ent, dim) : ent * dim; /* valeurs de la table */
		if (nv > 16777216)
			return AKE_CORRUPT;
		u16 *mul = akd_alloc(d, (nv << 1) + 16); /* multiplicateurs */
		if (!mul)
			return AKE_NOMEM;
		for (u64 i = 0; i < nv; i++)
			mul[i] = (u16)lrm_get(r, vbits);
		/* vecteurs de chaque entrée : entrées x dim flottants */
		u64 tot = dim * ent;
		if (tot > 4194304)
			return AKE_CORRUPT;
		float *o = akd_alloc(d, (tot << 2) + 16);
		if (!o)
			return AKE_NOMEM;
		b->vq = o;
		for (u64 e = 0; e < ent; e++) {
			float last = 0.0f;
			u64 div = 1;
			for (u64 i = 0; i < dim; i++) {
				u64 off;
				if (lt == 1) {
					u64 q = ak_udiv(e, div);
					off = q - ak_udiv(q, nv) * nv; /* (e / div) % valeurs */
					div *= nv;
				} else {
					off = e * dim + i;
				}
				float v = (float)(u32)mul[off];
				v = ak_fmadd(v, dl, mn);
				v = v + last;
				if (seq)
					last = v;
				*o++ = v;
			}
		}
	}
	/* codes (ordre de la spécification), puis tables de recherche */
	return vcb_codes(d, b);
}

/* lecteur, plancher -> 0 / < 0 */
static long vfl_setup(struct lrm *r, struct vfl *f)
{
	if (lrm_get(r, 16) != 1) /* plancher 0 : non */
		return AKE_FORMAT;
	u64 parts = lrm_get(r, 5);
	f->parts = parts;
	s64 maxc = -1; /* classe max */
	for (u64 p = 0; p < parts; p++) {
		u64 c = lrm_get(r, 4);
		f->pclass[p] = (u8)c;
		if ((s64)c > maxc)
			maxc = (s64)c;
	}
	for (s64 c = 0; c <= maxc; c++) { /* classes */
		f->cdim[c] = (u8)(lrm_get(r, 3) + 1);
		u64 sb = lrm_get(r, 2);
		f->csub[c] = (u8)sb;
		if (sb)
			f->cmaster[c] = (u8)lrm_get(r, 8);
		u64 ns = (u64)1 << sb; /* sous-classes */
		for (u64 k = 0; k < ns; k++)
			f->subbook[c][k] = (u16)(lrm_get(r, 8) - 1); /* (-1 : aucun livre) */
	}
	u64 mult = lrm_get(r, 2) + 1;
	f->mult = mult;
	f->range = vfl_ranges[mult - 1];
	u64 rb = lrm_get(r, 4);
	f->rangebits = rb;
	f->x[0] = 0;
	f->x[1] = (u16)((u64)1 << rb);
	u64 nv = 2; /* valeurs */
	for (u64 p = 0; p < f->parts; p++) {
		u64 cd = f->cdim[f->pclass[p]];
		while (cd) {
			if (nv >= 65)
				return AKE_CORRUPT;
			f->x[nv++] = (u16)lrm_get(r, rb);
			cd--;
		}
	}
	f->values = nv;
	/* ordre croissant de X (insertion) */
	for (u64 i = 0; i < nv; i++)
		f->sort[i] = (u8)i;
	for (u64 i = 1; i < nv; i++) {
		u8 id = f->sort[i];
		u32 xv = f->x[id];
		u64 j = i;
		while (j) {
			u8 o = f->sort[j - 1];
			if (f->x[o] <= xv)
				break;
			f->sort[j] = o;
			j--;
		}
		f->sort[j] = id;
	}
	/* voisins : low[i] = plus grand X[j] < X[i], high[i] = plus petit X[j] > X[i], j < i */
	for (u64 i = 2; i < nv; i++) {
		s32 xv = f->x[i];
		u64 lo = 0, hi = 0;
		s32 lv = -1, hv = 0x10000;
		for (u64 j = 0; j < i; j++) {
			s32 w = f->x[j];
			if (w < xv) {
				if (w > lv) {
					lv = w;
					lo = j;
				}
			} else if (w != xv && w < hv) {
				hv = w;
				hi = j;
			}
		}
		f->low[i] = (u8)lo;
		f->high[i] = (u8)hi;
	}
	return 0;
}

/* lecteur, résidu -> 0 / < 0 */
static long vrs_setup(struct lrm *r, struct vrs *rs)
{
	u64 t = lrm_get(r, 16);
	if (t > 2)
		return AKE_CORRUPT;
	rs->type = t;
	rs->begin = lrm_get(r, 24);
	rs->end = lrm_get(r, 24);
	rs->psize = lrm_get(r, 24) + 1;
	u64 nc = lrm_get(r, 6) + 1;
	rs->nclass = nc;
	rs->classbook = lrm_get(r, 8);
	/* cascade : 8 bits par classe */
	u8 casc[64];
	for (u64 c = 0; c < nc; c++) {
		u64 v = lrm_get(r, 3);
		if (lrm_get(r, 1))
			v |= lrm_get(r, 5) << 3;
		casc[c] = (u8)v;
	}
	for (u64 c = 0; c < nc; c++)
		for (u64 p = 0; p < 8; p++) {
			u64 bk = (u64)-1;
			if ((casc[c] >> p) & 1)
				bk = lrm_get(r, 8);
			rs->books[c][p] = (u16)bk;
		}
	return 0;
}

/* lecteur, correspondance, état (canaux, planchers, résidus) -> 0 / < 0 */
static long vbm_setup(struct lrm *r, struct vbm *m, const struct vb *s)
{
	if (lrm_get(r, 16))
		return AKE_CORRUPT;
	u64 sm = 1;
	if (lrm_get(r, 1))
		sm = lrm_get(r, 4) + 1;
	m->submaps = sm;
	u64 cb = vb_ilog(s->ch - 1); /* bits d'un numéro de canal */
	m->steps = 0;
	if (lrm_get(r, 1)) {
		u64 st = lrm_get(r, 8) + 1;
		m->steps = st;
		for (u64 i = 0; i < st; i++) {
			m->mag[i] = (u8)lrm_get(r, cb);
			m->ang[i] = (u8)lrm_get(r, cb);
		}
		/* écart voulu : canal hors des spectres (l'asm y écrirait) */
		for (u64 i = 0; i < st; i++)
			if (m->mag[i] >= s->ch + 2 || m->ang[i] >= s->ch + 2)
				return AKE_CORRUPT;
	}
	if (lrm_get(r, 2))
		return AKE_CORRUPT;
	sm = m->submaps;
	if (sm > 1) /* (un seul sous-plan : mux à zéro, déjà) */
		for (u64 c = 0; c < s->ch; c++) {
			u64 v = lrm_get(r, 4);
			if (v >= sm)
				return AKE_CORRUPT;
			m->mux[c] = (u8)v;
		}
	for (u64 k = 0; k < sm; k++) {
		lrm_get(r, 8); /* (temps : inutilisé) */
		u64 fl = lrm_get(r, 8);
		if (fl >= s->nfloor)
			return AKE_CORRUPT;
		m->floor[k] = (u8)fl;
		u64 re = lrm_get(r, 8);
		if (re >= s->nres)
			return AKE_CORRUPT;
		m->res[k] = (u8)re;
	}
	return 0;
}

/* décodeur, n -> pente (n flottants) : sin(pi/2 sin^2((j + 1/2) / n * pi/2)), 0 : mémoire */
static float *vb_slope(struct akd *d, u64 n)
{
	float *p = akd_alloc(d, n << 2);
	if (!p)
		return 0;
	double dn = (double)(s64)n;
	double hp = ak_u2d(0x3FF921FB54442D18ull); /* pi/2 */
	for (u64 j = 0; j < n; j++) {
		double x = ((double)(s64)j + 0.5) / dn * hp;
		double sn, cs;
		ak_sincos(x, &sn, &cs);
		x = sn * sn * hp;
		ak_sincos(x, &sn, &cs);
		p[j] = (float)sn;
	}
	return p;
}

/* décodeur, état : tampons, IMDCT, pentes -> 0 / < 0 */
static long vb_buffers(struct akd *d, struct vb *s)
{
	u64 bs1 = s->bs1, ch = s->ch;
	/* précédent, spectres : canaux x bs1/2 ; IMDCT : bs1 ; Y : canaux x 65 x 4 */
	s->prev = akd_alloc(d, ((bs1 * ch) << 1) + 64);
	if (!s->prev)
		return AKE_NOMEM;
	s->resid = akd_alloc(d, ((bs1 * ch) << 1) + (bs1 << 2) + 64);
	if (!s->resid)
		return AKE_NOMEM;
	u64 tsz = bs1 << 2;
	if (((bs1 * ch) << 1) > tsz) /* écart voulu : résidu de type 2 sur plus de 2 canaux */
		tsz = (bs1 * ch) << 1;
	s->tmp = akd_alloc(d, tsz + 64);
	if (!s->tmp)
		return AKE_NOMEM;
	s->classif = akd_alloc(d, 8 * 8192);
	if (!s->classif)
		return AKE_NOMEM;
	u8 *y = akd_alloc(d, 8 * 65 * 4 + 64);
	if (!y)
		return AKE_NOMEM;
	s->y = (s32 *)y;
	s->used = y + 8 * 65 * 4;
	s->donot = s->used + 16;
	long e = ak_imdct_init(d, &s->md0, s->bs0);
	if (e < 0)
		return e;
	e = ak_imdct_init(d, &s->md1, s->bs1);
	if (e < 0)
		return e;
	s->slope0 = vb_slope(d, s->bs0 >> 1);
	if (!s->slope0)
		return AKE_NOMEM;
	s->slope1 = vb_slope(d, s->bs1 >> 1);
	if (!s->slope1)
		return AKE_NOMEM;
	/* sortie : bs1 trames par canal */
	return akd_planes(d, s->bs1, s->ch);
}

/* ---- paquet audio */

/* plancher 1 : état, plancher, Y (65 x 32 bits), lecteur -> 1 utilisé, 0
 * inutilisé, < 0 erreur */
static long vfl_decode(const struct vb *s, const struct vfl *f, s32 *y, struct lr *r)
{
	if (!lr_get(r, 1))
		return 0;
	u64 rb = 64 - ak_clz64(f->range - 1); /* ilog(range - 1) */
	y[0] = (s32)lr_get(r, rb);
	y[1] = (s32)lr_get(r, rb);
	u64 k = 2; /* rang */
	for (u64 p = 0; p < f->parts; p++) {
		u8 cls = f->pclass[p];
		u32 cb = f->csub[cls]; /* bits de sous-classe */
		u64 cval = 0;
		if (cb) {
			u64 bk = f->cmaster[cls];
			if (bk >= s->ncb) /* écart voulu (l'asm lirait hors des livres) */
				return AKE_CORRUPT;
			s64 e = vcb_decode(&s->cb[bk], r);
			if (e < 0)
				return AKE_CORRUPT;
			cval = (u64)e;
		}
		for (u32 j = f->cdim[cls]; j; j--) {
			u64 sub = cval & (AK_SHL(1, cb) - 1);
			cval = AK_SHR(cval, cb);
			s32 bk = (s16)f->subbook[cls][sub];
			u32 v = 0;
			if (bk >= 0) {
				if ((u64)bk >= s->ncb) /* écart voulu */
					return AKE_CORRUPT;
				s64 e = vcb_decode(&s->cb[bk], r);
				if (e < 0)
					return AKE_CORRUPT;
				v = (u32)e;
			}
			y[k++] = (s32)v;
		}
	}
	if (lr_eop(r)) /* paquet trop court : inutilisé */
		return 0;
	return 1;
}

/* render_line (x0, y0) -> (x1, y1) sur [x0, x1[ (borné à n/2) : spectre[x] *= dB_inverse[y] */
static void vfl_line(float *spec, u32 n2, s32 x0, s32 y0, s32 x1, s32 y1)
{
	s32 dy = (s32)((u32)y1 - (u32)y0);
	s32 adx = (s32)((u32)x1 - (u32)x0);
	if (adx <= 0)
		return;
	s32 base = ak_sdivw(dy, adx);
	u32 ady = dy < 0 ? 0u - (u32)dy : (u32)dy;
	u32 ab = base < 0 ? 0u - (u32)base : (u32)base;
	ady = ady - ab * (u32)adx;
	s32 sy = (s32)((u32)base + (dy < 0 ? 0xFFFFFFFFu : 1u)); /* base -/+ 1 */
	s32 x = x0, y = y0, err = 0;
	for (;;) {
		if (x >= x1)
			return;
		if (x >= (s32)n2)
			return;
		s32 t = y > 255 ? 255 : y; /* (borne de la table) */
		if (t < 0)
			t = 0;
		spec[(u32)x] = spec[(u32)x] * ak_u2f(vb_invdb[t]);
		x = (s32)((u32)x + 1);
		err = (s32)((u32)err + ady);
		if (err < adx) {
			y = (s32)((u32)y + (u32)base);
		} else {
			err = (s32)((u32)err - (u32)adx);
			y = (s32)((u32)y + (u32)sy);
		}
	}
}

/* plancher, Y, spectre (multiplié par la courbe), n/2 */
static void vfl_render(const struct vfl *f, const s32 *Y, float *spec, u64 n2)
{
	s32 fy[65]; /* Y final */
	u8 st[68];  /* step2 */
	memset(st, 0, sizeof st);
	u64 nv = f->values;
	const u16 *X = f->x;
	fy[0] = Y[0];
	fy[1] = Y[1];
	st[0] = 1;
	st[1] = 1;
	u32 range = (u32)f->range;
	for (u64 i = 2; i < nv; i++) {
		u8 lo = f->low[i], hi = f->high[i];
		/* prédiction : render_point(X[lo], Y[lo], X[hi], Y[hi], X[i]) */
		s32 x0 = X[lo], x1 = X[hi];
		s32 y0 = fy[lo], y1 = fy[hi];
		s32 xx = X[i];
		s32 dy = (s32)((u32)y1 - (u32)y0);
		s32 adx = x1 - x0;
		u32 ady = dy < 0 ? 0u - (u32)dy : (u32)dy;
		s32 off = ak_sdivw((s32)(ady * (u32)(xx - x0)), adx);
		u32 pred = dy < 0 ? (u32)y0 - (u32)off : (u32)y0 + (u32)off;
		u32 val = (u32)Y[i];
		u32 hr = range - pred; /* highroom */
		u32 lr = pred;	       /* lowroom */
		u32 room = (hr < lr ? hr : lr) << 1;
		u32 v;
		if (val) {
			st[lo] = 1;
			st[hi] = 1;
			st[i] = 1;
			if (val >= room) {
				if (hr > lr)
					v = val - lr + pred;
				else
					v = pred - val + hr - 1;
			} else if (val & 1) {
				v = pred - ((val + 1) >> 1); /* impaire : predicted - (val + 1) / 2 */
			} else {
				v = pred + (val >> 1);
			}
		} else {
			st[i] = 0;
			v = pred;
		}
		fy[i] = (s32)v;
	}
	/* tracé dans l'ordre des X : segments entre points step2 */
	u32 mult = (u32)f->mult;
	s32 lx = 0, ly = (s32)((u32)fy[0] * mult);
	s32 hx = 0, hy = ly;
	for (u64 k = 1; k < nv; k++) {
		u8 pt = f->sort[k];
		if (!st[pt])
			continue;
		hy = (s32)((u32)fy[pt] * mult);
		hx = X[pt];
		vfl_line(spec, (u32)n2, lx, ly, hx, hy);
		lx = hx;
		ly = hy;
	}
	if ((u32)hx < (u32)n2)
		vfl_line(spec, (u32)n2, hx, hy, (s32)n2, hy);
}

/* livre, résidu (type 0 : entrelacé ; 1, 2 : à la suite), début de la
 * partition, taille, lecteur -> 0, < 0 : fin du paquet */
static long vrs_partition(const struct vcb *b, const struct vrs *rs, float *v, u64 size, struct lr *r)
{
	u64 dim = b->dim;
	if (!b->vq) /* (livre sans vecteurs : invalide) */
		return -1;
	if (!rs->type) {
		/* type 0 : pas = taille / dim ; v[j + k pas] += vec[k] */
		u64 step = ak_udiv(size, dim);
		for (u64 j = 0; j < step; j++) {
			s64 e = vcb_decode(b, r);
			if (e < 0)
				return -1;
			if (lr_eop(r))
				return -1;
			const float *q = b->vq + (u64)e * dim;
			float *t = v + j;
			for (u64 k = 0; k < dim; k++) {
				*t = *t + q[k];
				t += step;
			}
		}
		return 0;
	}
	u64 i = 0;
	for (;;) {
		if (i >= size)
			return 0;
		s64 e = vcb_decode(b, r);
		if (e < 0)
			return -1;
		if (lr_eop(r))
			return -1;
		const float *q = b->vq + (u64)e * dim;
		for (u64 k = 0; k < dim; k++) {
			if (i >= size)
				return 0;
			v[i] = v[i] + q[k];
			i++;
		}
	}
}

/* corps du résidu : voies, à ne pas décoder, vecteurs, longueur d'une voie
 * (une fin de paquet arrête le décodage : le reste vaut zéro) */
static void vrs_body(const struct vb *s, const struct vrs *rs, u64 lanes, const u8 *dn, float *const *vec, u64 len,
		     struct lr *r)
{
	/* borne : début, fin dans la longueur */
	u64 b = rs->begin;
	u64 e = rs->end < len ? rs->end : len;
	if (b >= e)
		return;
	u64 ps = rs->psize;
	u64 parts = ak_udiv(e - b, ps); /* partitions à lire */
	if (!parts)
		return;
	if (rs->classbook >= s->ncb) /* écart voulu (l'asm lirait hors des livres) */
		return;
	const struct vcb *cb = &s->cb[rs->classbook]; /* livre des classes */
	u64 cdim = cb->dim;			       /* mots de classe par code */
	for (u64 pass = 0; pass < 8; pass++) {
		u64 p = 0; /* partition */
		while (p < parts) {
			if (!pass) /* passe 0 : classifications de chaque voie */
				for (u64 l = 0; l < lanes; l++) {
					if (dn[l])
						continue;
					s64 t = vcb_decode(cb, r);
					if (t < 0) /* (fin ou code invalide : fini) */
						return;
					if (lr_eop(r))
						return;
					u64 nc = rs->nclass, tv = (u64)t;
					for (s64 i = (s64)cdim - 1; i >= 0; i--) {
						u64 q = ak_udiv(tv, nc);
						u64 c = tv - q * nc; /* temp % classes */
						tv = q;
						if (p + (u64)i < parts)
							s->classif[(l << 13) + p + (u64)i] = (u8)c;
					}
				}
			/* une partition de chaque voie */
			for (u64 i = 0; i < cdim; i++) {
				if (p >= parts)
					goto next;
				for (u64 l = 0; l < lanes; l++) {
					if (dn[l])
						continue;
					u8 cls = s->classif[(l << 13) + p];
					s32 bk = (s16)rs->books[cls][pass]; /* livre de la passe */
					if (bk < 0)
						continue;
					if ((u64)bk >= s->ncb) /* écart voulu : comme une fin de paquet */
						return;
					float *v = vec[l] + (rs->begin + p * ps);
					if (vrs_partition(&s->cb[bk], rs, v, ps, r) < 0)
						return;
				}
				p++;
			}
		}
	next:;
	}
}

/* résidu des canaux du sous-plan k (n : taille du bloc) */
static void vrs_decode(struct vb *s, const struct vbm *m, u64 k, u64 n, struct lr *r)
{
	const struct vrs *rs = &s->res[m->res[k]];
	/* canaux de ce sous-plan : ceux à décoder (donot = 0) ; type 2 : tous s'il y en a un */
	u8 chans[8], dn[8];
	float *vec[8];
	u64 nch = 0, any = 0;
	for (u64 c = 0; c < s->ch; c++) {
		if (m->mux[c] != k)
			continue;
		chans[nch] = (u8)c;
		dn[nch] = s->donot[c];
		if (!dn[nch])
			any = 1;
		vec[nch] = (float *)((u8 *)s->resid + c * (s->bs1 << 1));
		nch++;
	}
	if (!nch || !any) /* rien à décoder */
		return;
	if (rs->type == 2) {
		/* type 2 : un vecteur entrelacé de canaux x n/2 (dans VB_TMP) */
		u64 h = n >> 1;
		memset(s->tmp, 0, (h * nch) << 2);
		dn[0] = 0; /* une voie, à décoder */
		vec[0] = s->tmp;
		vrs_body(s, rs, 1, dn, vec, h * nch, r);
		/* désentrelacement : v_c[i] = t[i * canaux + c] */
		for (u64 i = 0; i < h; i++)
			for (u64 c = 0; c < nch; c++) {
				float *o = (float *)((u8 *)s->resid + chans[c] * (s->bs1 << 1));
				o[i] = s->tmp[i * nch + c];
			}
		return;
	}
	vrs_body(s, rs, nch, dn, vec, n >> 1, r);
}

/* fenêtre, recouvrement avec la moitié droite précédente -> d->pcm[c], puis
 * la nouvelle moitié droite (n : taille du bloc, VB_TMP : IMDCT) */
static void vb_window(struct akd *d, struct vb *s, u64 c, u64 n)
{
	float *t = s->tmp;
	u64 fl = s->flags;
	u64 n2 = n >> 1, n4 = n >> 2, b4 = s->bs0 >> 2;
	/* gauche : [ls, le[ avec la pente de longueur le - ls */
	u64 ls = 0, le = n2;
	const float *sl = s->slope0;
	if (fl & 1) {
		sl = s->slope1;
		if (!(fl & 2)) { /* précédent court */
			sl = s->slope0;
			ls = n4 - b4;
			le = n4 + b4;
		}
	}
	u64 i;
	for (i = 0; i < ls; i++)
		t[i] = 0.0f;
	for (u64 k = 0; i < le; i++, k++)
		t[i] = t[i] * sl[k];
	/* droite : [rs, re[ avec la pente renversée */
	u64 rs = n2, re = n;
	sl = s->slope0;
	if (fl & 1) {
		sl = s->slope1;
		if (!(fl & 4)) { /* suivant court */
			sl = s->slope0;
			u64 c3 = n2 + n4; /* 3n/4 */
			rs = c3 - b4;
			re = c3 + b4;
		}
	}
	u64 k = re - rs;
	for (i = rs; i < re; i++) {
		k--;
		t[i] = t[i] * sl[k];
	}
	for (; i < n; i++)
		t[i] = 0.0f;
	/* recouvrement : sortie[i] = prec[i] (i < prevn/2) + tmp[i + n/4 - prevn/4] */
	float *prev = (float *)((u8 *)s->prev + c * (s->bs1 << 1));
	u64 pn = s->prevn;
	if (pn) {
		float *out = d->pcm[c];
		u64 p4 = pn >> 2;
		u64 fr = p4 + n4; /* trames */
		u64 p2 = pn >> 1;
		u64 off = n4 - p4;
		for (u64 j = 0; j < fr; j++) {
			float a = 0.0f;
			if (j < p2)
				a = prev[j];
			u64 q = j + off;
			if ((s64)q >= 0 && q < n2)
				a = a + t[q];
			out[j] = a;
		}
	}
	/* nouvelle moitié droite */
	memmove(prev, t + n2, n2 << 2);
}

/* décodeur, paquet, octets -> trames rendues (0 : premier paquet ou paquet
 * d'en-tête, < 0 : erreur) */
long vb_packet(struct akd *d, const u8 *pkt, u64 n)
{
	struct vb *s = vb_state(d);
	if (!n)
		return 0;
	if (s->hdr != 7)
		return AKE_CORRUPT;
	if (pkt[0] & 1) /* (paquet d'en-tête : rien) */
		return 0;
	struct lr r;
	r.p = (uintptr_t)pkt;
	r.end = (uintptr_t)pkt + n;
	r.cache = 0;
	r.n = 0;
	lr_get(&r, 1); /* type 0 */
	u64 mode = 0;
	if (s->modebits)
		mode = lr_get(&r, s->modebits);
	if (mode >= s->nmode)
		return AKE_CORRUPT;
	u64 bf = s->modes[mode].blockflag;
	const struct vbm *m = &s->map[s->modes[mode].mapping];
	u64 nb = s->bs0;
	u64 fl = 0; /* drapeaux de fenêtre */
	if (bf) {
		nb = s->bs1;
		fl = 1;
		if (lr_get(&r, 1))
			fl |= 2;
		if (lr_get(&r, 1))
			fl |= 4;
	}
	s->n = nb;
	s->flags = fl;
	u64 ch = s->ch, bs1 = s->bs1;
	/* planchers */
	for (u64 c = 0; c < ch; c++) {
		const struct vfl *f = &s->floor[m->floor[m->mux[c]]];
		long u = vfl_decode(s, f, s->y + c * 65, &r);
		if (u < 0)
			return AKE_CORRUPT;
		s->used[c] = (u8)u;
		s->donot[c] = (u8)(u ^ 1);
	}
	/* couplage : les deux canaux d'un pas sont décodés si l'un l'est */
	for (u64 i = 0; i < m->steps; i++) {
		u8 a = m->mag[i], b = m->ang[i];
		u8 v = s->donot[a] & s->donot[b];
		s->donot[a] = v;
		s->donot[b] = v;
	}
	/* spectres à zéro */
	memset(s->resid, 0, (bs1 * ch) << 1);
	/* résidus, par sous-plan */
	for (u64 k = 0; k < m->submaps; k++)
		vrs_decode(s, m, k, nb, &r);
	/* couplage inverse (pas dans l'ordre inverse) */
	for (u64 i = m->steps; i;) {
		i--;
		float *M = (float *)((u8 *)s->resid + m->mag[i] * (bs1 << 1));
		float *A = (float *)((u8 *)s->resid + m->ang[i] * (bs1 << 1));
		for (u64 j = nb >> 1; j; j--) {
			float mv = *M, av = *A;
			if (!(mv <= 0.0f)) { /* (fcmp + b.le : NaN -> branche « négatif ») */
				if (!(av <= 0.0f)) {
					*A = mv - av; /* A = m - a */
				} else {
					*A = mv; /* A = m, M = m + a */
					*M = mv + av;
				}
			} else {
				if (!(av <= 0.0f)) {
					*A = mv + av; /* A = m + a */
				} else {
					*A = mv; /* A = m, M = m - a */
					*M = mv - av;
				}
			}
			M++;
			A++;
		}
	}
	/* plancher x résidu, IMDCT, fenêtre, recouvrement */
	for (u64 c = 0; c < ch; c++) {
		float *sp = (float *)((u8 *)s->resid + c * (bs1 << 1)); /* spectre du canal */
		if (!s->used[c])
			memset(sp, 0, nb << 1); /* plancher inutilisé : silence */
		else
			vfl_render(&s->floor[m->floor[m->mux[c]]], s->y + c * 65, sp, nb >> 1);
		ak_imdct(nb == s->bs0 ? &s->md0 : &s->md1, sp, s->tmp);
		vb_window(d, s, c, nb);
	}
	/* trames rendues : prev/4 + n/4 (0 au premier bloc) */
	u64 pv = s->prevn;
	s->prevn = nb;
	if (!pv)
		return 0;
	return (long)((pv >> 2) + (nb >> 2));
}

/* ---- API par paquets (MediaKit) : en-têtes en laçage Xiph */

/* décodeur, données (02, tailles, 3 en-têtes), octets -> 0 / < 0 */
long vb_init_pkt(struct akd *d, const u8 *extra, u64 n)
{
	uintptr_t p = (uintptr_t)extra, end = (uintptr_t)extra + n;
	d->codec = 0;
	long e = vb_new(d);
	if (e < 0)
		return e;
	if (p >= end)
		return AKE_CORRUPT;
	if (*(const u8 *)p++ != 2)
		return AKE_CORRUPT;
	/* deux tailles en laçage (255 + ...) */
	u64 s1 = 0, s2 = 0;
	u8 b;
	do {
		if (p >= end)
			return AKE_CORRUPT;
		b = *(const u8 *)p++;
		s1 += b;
	} while (b == 255);
	do {
		if (p >= end)
			return AKE_CORRUPT;
		b = *(const u8 *)p++;
		s2 += b;
	} while (b == 255);
	if (p + s1 + s2 > end)
		return AKE_CORRUPT;
	if ((e = vb_header(d, (const u8 *)p, s1)) < 0)
		return e;
	p += s1;
	if ((e = vb_header(d, (const u8 *)p, s2)) < 0)
		return e;
	p += s2;
	if ((e = vb_header(d, (const u8 *)p, end - p)) < 0)
		return e;
	d->pktfn = vb_packet;
	d->resetfn = vb_reset;
	return 0;
}

/* floor1_inverse_dB_table (spécification Vorbis I 10.1, ak_vbtab.inc) */
static const u32 vb_invdb[256] = {
	0x33E4B43E, 0x33F39109, 0x3401B28B, 0x340A203C, 0x34131A23, 0x341CA960,
	0x3426D7A7, 0x3431AF4B, 0x343D3B50, 0x34498770, 0x3456A023, 0x346492B8,
	0x34736D55, 0x34819F88, 0x348A0BFC, 0x34930493, 0x349C9269, 0x34A6BF32,
	0x34B1953F, 0x34BD1F93, 0x34C969E4, 0x34D680AD, 0x34E47136, 0x34F349A6,
	0x35018C88, 0x3509F7C0, 0x3512EF06, 0x351C7B76, 0x3526A6C0, 0x35317B37,
	0x353D03DA, 0x35494C5E, 0x3556613B, 0x35644FB9, 0x357325FC, 0x3581798A,
	0x3589E386, 0x3592D97C, 0x359C6485, 0x35A68E52, 0x35B16133, 0x35BCE825,
	0x35C92EDC, 0x35D641CE, 0x35E42E41, 0x35F30257, 0x3601668F, 0x3609CF4F,
	0x3612C3F5, 0x361C4D98, 0x362675E8, 0x36314732, 0x363CCC74, 0x3649115E,
	0x36562265, 0x36640CCE, 0x3672DEB8, 0x36815397, 0x3689BB1C, 0x3692AE72,
	0x369C36AF, 0x36A65D81, 0x36B12D35, 0x36BCB0C7, 0x36C8F3E4, 0x36D60301,
	0x36E3EB60, 0x36F2BB1E, 0x370140A2, 0x3709A6EB, 0x371298F1, 0x371C1FC9,
	0x3726451E, 0x3731133D, 0x373C951E, 0x3748D66F, 0x3755E3A2, 0x3763C9F7,
	0x37729789, 0x37812DAF, 0x378992BE, 0x37928374, 0x379C08E6, 0x37A62CBE,
	0x37B0F947, 0x37BC7979, 0x37C8B8FE, 0x37D5C447, 0x37E3A892, 0x37F273F8,
	0x38011AC0, 0x38097E93, 0x38126DF9, 0x381BF206, 0x38261462, 0x3830DF56,
	0x383C5DD8, 0x38489B92, 0x3855A4F2, 0x38638733, 0x3872506E, 0x388107D3,
	0x38896A6B, 0x38925882, 0x389BDB2A, 0x38A5FC09, 0x38B0C568, 0x38BC423B,
	0x38C87E29, 0x38D585A0, 0x38E365D9, 0x38F22CE8, 0x3900F4E9, 0x39095646,
	0x3912430E, 0x391BC451, 0x3925E3B5, 0x3930AB7F, 0x393C26A2, 0x394860C5,
	0x39556653, 0x39634483, 0x39720968, 0x3980E201, 0x39894224, 0x39922D9D,
	0x399BAD7B, 0x39A5CB63, 0x39B09199, 0x39BC0B0D, 0x39C84366, 0x39D5470B,
	0x39E32332, 0x39F1E5ED, 0x3A00CF1D, 0x3A092E05, 0x3A121830, 0x3A1B96A9,
	0x3A25B315, 0x3A3077B7, 0x3A3BEF7C, 0x3A48260A, 0x3A5527C7, 0x3A6301E6,
	0x3A71C278, 0x3A80BC3B, 0x3A8919E9, 0x3A9202C6, 0x3A9B7FDB, 0x3AA59ACB,
	0x3AB05DD8, 0x3ABBD3EF, 0x3AC808B3, 0x3AD50888, 0x3AE2E09F, 0x3AF19F07,
	0x3B00A95C, 0x3B0905D0, 0x3B11ED5E, 0x3B1B690F, 0x3B258284, 0x3B3043FD,
	0x3B3BB867, 0x3B47EB61, 0x3B54E94D, 0x3B62BF5D, 0x3B717B9C, 0x3B80967F,
	0x3B88F1BA, 0x3B91D7F9, 0x3B9B5247, 0x3BA56A41, 0x3BB02A27, 0x3BBB9CE2,
	0x3BC7CE12, 0x3BD4CA17, 0x3BE29E20, 0x3BF15835, 0x3C0083A6, 0x3C08DDA7,
	0x3C11C298, 0x3C1B3B82, 0x3C255201, 0x3C301054, 0x3C3B8161, 0x3C47B0C8,
	0x3C54AAE5, 0x3C627CE8, 0x3C7134D4, 0x3C8070CF, 0x3C88C996, 0x3C91AD3A,
	0x3C9B24C0, 0x3CA539C5, 0x3CAFF685, 0x3CBB65E5, 0x3CC79382, 0x3CD48BB9,
	0x3CE25BB4, 0x3CF11179, 0x3D005DFB, 0x3D08B589, 0x3D1197DF, 0x3D1B0E02,
	0x3D25218D, 0x3D2FDCB9, 0x3D3B4A6D, 0x3D477640, 0x3D546C91, 0x3D623A85,
	0x3D70EE22, 0x3D804B2A, 0x3D88A17F, 0x3D918288, 0x3D9AF748, 0x3DA50958,
	0x3DAFC2F2, 0x3DBB2EF8, 0x3DC75903, 0x3DD44D6D, 0x3DE2195C, 0x3DF0CAD1,
	0x3E00385B, 0x3E088D77, 0x3E116D33, 0x3E1AE090, 0x3E24F127, 0x3E2FA92E,
	0x3E3B1387, 0x3E473BCA, 0x3E542E4D, 0x3E61F837, 0x3E70A784, 0x3E80258F,
	0x3E887973, 0x3E9157E2, 0x3E9AC9DC, 0x3EA4D8F9, 0x3EAF8F6D, 0x3EBAF81B,
	0x3EC71E95, 0x3ED40F33, 0x3EE1D717, 0x3EF0843D, 0x3F0012C6, 0x3F086572,
	0x3F114293, 0x3F1AB32B, 0x3F24C0CE, 0x3F2F75B1, 0x3F3ADCB2, 0x3F470165,
	0x3F53F01D, 0x3F61B5FB, 0x3F7060FB, 0x3F800000,
};
