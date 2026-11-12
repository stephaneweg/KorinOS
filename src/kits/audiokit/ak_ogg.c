/* AudioKit : Ogg (RFC 3533, ak_ogg.inc) : pages, paquets ; Vorbis, Opus.
 *
 * Le premier flux logique (numéro de série de la première page) est lu,
 * les autres sautés. Un paquet peut s'étendre sur plusieurs pages ; la
 * position (granule) d'une page vaut pour le dernier paquet qui s'y termine.
 * Durée : granule de la dernière page (cherchée dans les 128 derniers Ko).
 * Recherche : bissection sur les pages, puis « calibrage » : on décode en
 * jetant le son jusqu'au premier paquet dont la page donne la position. */
#include "ak.h"
#include "ak_opus.h"
#include "ak_vorbis.h"

/* OG_* : 160 octets (ak_vorbis.c et ak_opus.c lisent preroll, granoff, state
 * à leurs décalages) */
struct og {
	s64 serial;  /* 0 (-1 : le premier vu) */
	u8 *page;    /* 8 corps de la page courante (copie, 65 Ko) */
	u8 *lace;    /* 16 table des segments (copie) */
	u64 nseg;    /* 24 */
	u64 segi;    /* 32 segment suivant */
	u64 bodyi;   /* 40 octet suivant du corps */
	s64 gran;    /* 48 granule de la page courante */
	s64 lastseg; /* 56 dernier segment qui termine un paquet (-1 : aucun) */
	u8 *pkt;     /* 64 paquet assemblé */
	u64 pktcap;  /* 72 */
	s64 codec;   /* 80 AKF_VORBIS / AKF_OPUS */
	s64 cal;     /* 88 1 : calibrage après une recherche */
	s64 target;  /* 96 */
	s64 preroll; /* 104 trames à décoder avant la cible */
	s64 granoff; /* 112 à retirer des granules (avance d'Opus) */
	s64 skippkt; /* 120 1 : jeter la suite d'un paquet commencé avant */
	s64 data0;   /* 128 première page de son */
	void *state; /* 136 état du codec (Vorbis, Opus) */
	s64 eos;     /* 144 */
	u64 pad;
};
_Static_assert(sizeof(struct og) == 160, "og");
AK_STATIC_OFF(struct og, preroll, 104);
AK_STATIC_OFF(struct og, granoff, 112);
AK_STATIC_OFF(struct og, state, 136);

#define OG_PAGEMAX (65536 + 512)
#define OGGS 0x5367674F

static long ogg_next(struct akd *d);
static long ogg_seek(struct akd *d, u64 frame);

/* lit la page suivante du flux (les autres flux sont sautés) -> 0 / -1 (fin) */
static long ogg_page(struct akd *d, struct og *g)
{
	u64 skipped = 0; /* octets sautés */
	for (;;) {
		if (d->dataend - d->off < 27)
			return -1;
		u64 av;
		const u8 *p = akd_need(d, 27 + 255, &av);
		if (av < 27)
			return -1;
		if (ak_rd32le(p) != OGGS || p[4]) { /* version 0 */
			akd_skip(d, 1);
			if (++skipped < 1048576)
				continue;
			return -1;
		}
		u64 nseg = p[26]; /* segments */
		if (av < nseg + 27)
			return -1;
		u64 body = 0; /* taille du corps */
		for (u64 i = 0; i < nseg; i++)
			body += p[27 + i];
		s64 ser = (s64)ak_rd32le(p + 14); /* numéro de série */
		if (g->serial == -1)
			g->serial = ser;
		if (ser != g->serial) {
			akd_skip(d, 27 + nseg + body);
			continue;
		}
		/* page du flux : table des segments, granule, corps */
		memmove(g->lace, p + 27, nseg);
		g->nseg = nseg;
		g->segi = 0;
		g->bodyi = 0;
		g->gran = (s64)ak_rd64le(p + 6);
		g->skippkt |= p[5] & 1; /* suite d'un paquet commencé avant ? */
		g->eos = p[5] & 4;
		s64 last = -1; /* dernier segment qui termine un paquet */
		for (u64 i = 0; i < nseg; i++)
			if (g->lace[i] < 255)
				last = (s64)i;
		g->lastseg = last;
		akd_skip(d, nseg + 27);
		const u8 *q = akd_need(d, body, &av);
		if (av < body)
			return -1;
		memmove(g->page, q, body);
		akd_skip(d, body);
		return 0;
	}
}

/* -> paquet (0 : fin), *n = octets, *gran = granule (s'il est le dernier
 * paquet terminé sur sa page, -1 sinon) */
static u8 *ogg_packet(struct akd *d, struct og *g, u64 *n, s64 *gran)
{
	u64 len = 0; /* octets du paquet */
	for (;;) {
		if (g->segi >= g->nseg) { /* page suivante */
			if (ogg_page(d, g) < 0)
				return 0;
			if (len) /* (un paquet commence : rien à jeter) */
				g->skippkt = 0;
			continue;
		}
		u64 l = g->lace[g->segi]; /* longueur du segment */
		g->segi++;
		if (len + l > g->pktcap) { /* agrandir le tampon du paquet (une fois, comme l'asm) */
			g->pktcap <<= 1;
			u8 *nb = akd_alloc(d, g->pktcap);
			if (!nb)
				return 0;
			memmove(nb, g->pkt, len);
			g->pkt = nb;
		}
		memmove(g->pkt + len, g->page + g->bodyi, l);
		g->bodyi += l;
		len += l;
		if (l == 255) /* le paquet continue */
			continue;
		if (g->skippkt) { /* suite d'un paquet commencé avant : jeter */
			g->skippkt = 0;
			len = 0;
			continue;
		}
		*gran = (s64)(g->segi - 1) == g->lastseg ? g->gran : -1;
		*n = len;
		return g->pkt;
	}
}

/* -> granule de la dernière page du flux (< 0 : aucune) ; la position de
 * lecture est perdue */
static s64 ogg_last_granule(struct akd *d)
{
	struct og *g = d->codec;
	s64 best = -1;
	u64 span = (u64)(d->dataend - g->data0);
	if (span >= 65536 + 65536)
		span = 65536 + 65536;
	akd_seek(d, (u64)d->dataend - span); /* début de la recherche */
	u64 av;
	const u8 *p = akd_need(d, 65536 + 65536, &av);
	u64 lim = (u64)(d->dataend - d->off); /* (pas au-delà des données) */
	if (av > lim)
		av = lim;
	for (u64 i = 0; i + 27 <= av; i++) {
		if (ak_rd32le(p + i) != OGGS || ak_rd32le(p + i + 14) != (u32)g->serial)
			continue;
		s64 gr = (s64)ak_rd64le(p + i + 6);
		if (gr != -1)
			best = gr;
	}
	return best;
}

/* décodeur (lecture sur "OggS") -> 0 / < 0 */
long ogg_open(struct akd *d)
{
	struct og *g = akd_alloc(d, sizeof(struct og));
	if (!g)
		return AKE_NOMEM;
	u8 *pg = akd_alloc(d, OG_PAGEMAX + 256);
	if (!pg)
		return AKE_NOMEM;
	g->page = pg;
	g->lace = pg + OG_PAGEMAX;
	g->pkt = akd_alloc(d, 65536);
	if (!g->pkt)
		return AKE_NOMEM;
	g->pktcap = 65536;
	g->serial = -1;
	/* le codec du premier paquet */
	u64 n;
	s64 gr;
	u8 *p = ogg_packet(d, g, &n, &gr);
	if (!p || n < 8)
		return AKE_CORRUPT;
	u64 w = ak_rd64le(p);
	long r;
	if ((w & 0xFFFFFFFFFFFFFFULL) == 0x736962726F7601ULL) { /* "\x01vorbis" (7 octets) */
		d->codec = g;
		g->codec = AKF_VORBIS;
		r = vb_new(d);
		if (r < 0)
			return r;
		for (int k = 0;;) { /* les trois en-têtes */
			r = vb_header(d, p, n);
			if (r < 0)
				return r;
			if (++k >= 3)
				break;
			p = ogg_packet(d, g, &n, &gr);
			if (!p)
				return AKE_CORRUPT;
		}
	} else if (w == 0x646165487375704FULL) { /* "OpusHead" */
		d->codec = g;
		g->codec = AKF_OPUS;
		r = ops_head(d, p, n); /* OpusHead, puis OpusTags */
		if (r < 0)
			return r;
		p = ogg_packet(d, g, &n, &gr);
		if (!p)
			return AKE_CORRUPT;
		ops_tags(d, p, n);
	} else {
		return AKE_FORMAT;
	}
	/* début du son : la page qui suit (les en-têtes finissent leur page) */
	g->data0 = d->off;
	d->data0 = d->off;
	s64 last = ogg_last_granule(d); /* durée : granule de la dernière page */
	if (last >= 0) {
		s64 t = last - g->granoff;
		if (t < 0)
			t = 0;
		d->total = t;
		d->limit = 1;
		if (t) /* débit moyen */
			d->bitrate = (s64)((((u64)(d->dataend - g->data0) << 3) * (u64)d->rate) / (u64)t);
	}
	akd_seek(d, (u64)g->data0); /* retour au début du son */
	g->segi = g->nseg;	    /* (page courante finie) */
	d->next = ogg_next;
	d->seekfn = ogg_seek;
	return 0;
}

/* -> trames (0 : fin) */
static long ogg_next(struct akd *d)
{
	struct og *g = d->codec;
	for (;;) {
		u64 n;
		s64 gr;
		u8 *p = ogg_packet(d, g, &n, &gr);
		if (!p)
			return 0;
		long r = g->codec == AKF_OPUS ? ops_packet(d, p, n) : vb_packet(d, p, n);
		if (r <= 0) /* paquet invalide, ou rien à rendre : le suivant */
			continue;
		if (!g->cal)
			return r;
		/* calibrage : le son est jeté jusqu'à une position connue */
		if (gr < 0)
			continue;
		g->cal = 0;
		s64 st = gr - g->granoff - r; /* début de ce paquet */
		s64 sk = g->target - st;
		if (sk >= 0) {
			d->skip = sk;
			d->pos = g->target;
		} else {
			d->skip = 0;
			d->pos = st < 0 ? 0 : st;
		}
		return r;
	}
}

/* page trouvée à partir de pos -> sa position (< 0 : aucune dans les 64 Ko),
 * *gran = sa granule (-1 possible) */
static s64 ogg_find(struct akd *d, u64 pos, s64 *gran)
{
	struct og *g = d->codec;
	akd_seek(d, pos);
	u64 av;
	const u8 *p = akd_need(d, 65536, &av);
	for (u64 i = 0; i + 27 <= av; i++) {
		if (ak_rd32le(p + i) != OGGS || ak_rd32le(p + i + 14) != (u32)g->serial)
			continue;
		*gran = (s64)ak_rd64le(p + i + 6);
		return d->off + (s64)i;
	}
	return -1;
}

/* trame voulue -> 0 : bissection sur les pages, puis calibrage (ogg_next) */
static long ogg_seek(struct akd *d, u64 frame)
{
	struct og *g = d->codec;
	g->target = (s64)frame;
	/* granule visée : cible - avance de décodage, en granule */
	s64 aim = (s64)frame + g->granoff - g->preroll;
	if (aim < 0)
		aim = 0;
	u64 lo = (u64)g->data0; /* bas : début (granule <= visée) */
	u64 best = lo;		/* meilleure position */
	u64 hi = (u64)d->dataend;
	int it = 48;
	while (hi - lo >= 8192 && --it) {
		s64 gr;
		s64 pos = ogg_find(d, (lo + hi) >> 1, &gr);
		if (pos < 0) {
			hi = (lo + hi) >> 1;
		} else if (gr == -1) { /* page sans fin de paquet : plus loin */
			lo = (lo + hi) >> 1;
		} else if ((u64)gr > (u64)aim) {
			hi = (lo + hi) >> 1;
		} else { /* cette page finit avant la cible : on peut commencer après elle */
			lo = (u64)pos;
			best = (u64)pos;
		}
	}
	/* repartir de la page best : ses paquets précèdent la cible ; le premier
	 * paquet complet amorce le décodeur */
	akd_seek(d, best);
	g->segi = g->nseg;
	g->skippkt = 1; /* (un paquet à cheval est jeté) */
	g->cal = 1;
	d->pos = (s64)frame;
	if (g->codec == AKF_OPUS)
		ops_reset(d);
	else
		vb_reset(d);
	return 0;
}
