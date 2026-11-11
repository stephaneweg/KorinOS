/* ar_inflate.c - la décompression deflate (RFC 1951), zlib (RFC 1950), gzip
 * (RFC 1952) d'ArchiveKit (d'après ar_inflate.inc, pas à pas : mêmes
 * résultats et mêmes erreurs sur des données abîmées).
 *
 * if_run décode tout d'un coup : l'entrée est un bloc de mémoire (ou tirée par
 * morceaux d'une archive, if_pull), la sortie un tampon de l'appelant (pas de
 * puits : l'historique est ce tampon, et rien ne déborde : AR_E_SPACE) ou un
 * puits (fenêtre de 32 Ko d'historique + IF_OCHUNK, passée au puits quand elle
 * est pleine). Le puits peut arrêter le décodage (AR_STOP).
 *
 * Tables : une consultation sur IF_LBITS (littéraux et longueurs) ou IF_DBITS
 * bits (distances) ; les codes plus longs sont décodés en canonique, bit à bit.
 * Bits : registre de 64 bits rempli par mots tant que l'entrée a 8 octets
 * devant elle, octet par octet ensuite ; après la fin de l'entrée on lit des
 * zéros comptés (over) : un flux tronqué est vu à la fin du bloc au plus
 * tard, ou après IF_OVERMAX octets inventés. */
#include "ar.h"

static const u16 if_lbase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
				 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u16 if_dbase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
				 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const u8 if_lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u8 if_dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

/* les registres du décodeur de l'asm */
struct dec {
	struct infl *s;
	u64 bits;            /* x20 */
	u64 n;               /* x21 : nombre de bits */
	uintptr_t ip, iend;  /* x22, x23 */
	uintptr_t iend8;     /* x24 : fin - 8 */
	u8 *op, *oend;       /* x25, x26 */
};

#define SHL(v, k) ((v) << ((k) & 63))
#define SHR(v, k) ((v) >> ((k) & 63))

/* entrée, octets, sortie (0 : celle du puits), capacité, puits (0 : la sortie),
 * enveloppe */
void if_begin(struct infl *s, const u8 *in, u64 n, u8 *out, u64 cap, struct sink *k, u64 wrap)
{
	s->ip = (uintptr_t)in;
	s->iend = (uintptr_t)in + n;
	s->over = 0;
	s->wrap = wrap;
	s->sink = k;
	s->src = 0;
	s->total = 0;
	s->gztime = 0;
	s->gzname[0] = 0;
	s->members = 0;
	if (k) {
		out = s->obuf;
		cap = IF_OCAP;
	}
	s->obase = out;
	s->oend = out + cap;
	s->flushed = out;
}

/* l'entrée continue dans l'archive -> octets reçus (0 : plus rien) */
static u64 if_pull(struct dec *d)
{
	struct infl *s = d->s;
	if (!s->src || !s->srcleft)
		return 0;
	u64 c = s->srcleft > IF_ICHUNK ? IF_ICHUNK : s->srcleft;
	if (src_read(s->src, s->srcoff, s->ibuf, c) < 0)
		return 0;
	s->srcoff += c;
	s->srcleft -= c;
	d->ip = (uintptr_t)s->ibuf;
	d->iend = d->ip + c;
	d->iend8 = d->iend - 8;
	return c;
}

/* remplit les bits octet par octet jusqu'à plus de 56 -> 0, 1 : trop d'octets
 * inventés */
static int if_fill(struct dec *d)
{
	while (d->n <= 56) {
		if (d->ip < d->iend) {
			d->bits |= SHL((u64) * (u8 *)d->ip, d->n);
			d->ip++;
			d->n += 8;
			continue;
		}
		if (if_pull(d))
			continue;
		if (++d->s->over > IF_OVERMAX)
			return 1;
		d->n += 8;
	}
	return 0;
}

/* au moins 48 bits -> 0, 1 : plus d'entrée */
static inline int if_need(struct dec *d)
{
	if (d->n < 48)
		return if_fill(d);
	return 0;
}

/* -> prochain octet de l'entrée (après alignement), négatif : fin de l'entrée */
static long if_byte(struct dec *d)
{
	u64 a = d->n & 7;
	d->bits = SHR(d->bits, a);
	d->n -= a;
	u64 inv = d->s->over << 3;
	if (d->n > inv) {
		long b = (long)(d->bits & 0xFF);
		d->bits >>= 8;
		d->n -= 8;
		return b;
	}
	if (inv)
		return -1;
	d->bits = 0; /* (rien de valable dans les bits) */
	d->n = 0;
	while (d->ip >= d->iend)
		if (!if_pull(d))
			return -1;
	return *(u8 *)d->ip++;
}

/* -> 0 : il reste de l'entrée (un autre membre gzip), 1 : c'est fini */
static int if_more(struct dec *d)
{
	if ((d->n & ~(u64)7) > d->s->over << 3)
		return 0;
	if (d->ip < d->iend)
		return 0;
	return if_pull(d) == 0;
}

/* somme et puits pour ce qui n'est pas encore passé ; glisse la fenêtre si
 * slide -> 0, ou la réponse du puits */
static long if_flush(struct dec *d, int slide)
{
	struct infl *s = d->s;
	u8 *f = s->flushed;
	u64 c = (u64)(d->op - f);
	if (c) {
		s->total += c;
		s->flushed = d->op;
		if (s->want == 1)
			s->crc = crc32_upd((u32)s->crc, f, c);
		else if (s->want == 2)
			s->adler = adler32_upd((u32)s->adler, f, c);
		if (!s->sink)
			return 0;
		long r = sink_write(s->sink, f, c);
		if (r)
			return r;
	}
	if (!slide || !s->sink)
		return 0;
	/* ne garder que l'historique */
	u64 used = (u64)(d->op - s->obase);
	if (used <= IF_HISTORY)
		return 0;
	u8 *from = s->obase + (used - IF_HISTORY);
	d->op = s->obase + IF_HISTORY;
	s->flushed = d->op;
	ar_memcpy(s->obase, from, IF_HISTORY);
	return 0;
}

/* la sortie est pleine : de la place, ou AR_E_SPACE sans puits -> 0, erreur */
static long if_flush_room(struct dec *d)
{
	if (!d->s->sink)
		return AR_E_SPACE;
	return if_flush(d, 1);
}

/* bits du code (u8 par symbole), symboles, comptes (u16[16]), symboles triés,
 * table, bits de la table, 1 : code incomplet à un seul symbole permis -> 0,
 * 1 : code invalide */
static int if_build(const u8 *lens, u64 n, u16 *count, u16 *syms, u16 *table, u64 tbits, int allow)
{
	u16 offs[16];
	for (int i = 0; i < 16; i++)
		count[i] = 0;
	u64 i = 0;
	do
		count[lens[i]]++;
	while (++i < n);
	/* code valide ? */
	long left = 1;
	u64 maxl = 0;
	for (i = 1; i < 16; i++) {
		if (count[i])
			maxl = i;
		left <<= 1;
		left -= count[i];
		if (left < 0)
			return 1;
	}
	if (left) {
		/* incomplet : seulement rien du tout, ou un symbole de 1 bit */
		if (maxl && maxl != 1)
			return 1;
		if (!allow)
			return 1;
	}
	/* décalages et symboles triés */
	offs[1] = 0;
	u32 acc = 0;
	for (i = 1; i < 16; i++) {
		offs[i] = (u16)acc;
		acc += count[i];
	}
	i = 0;
	do {
		u32 l = lens[i];
		if (l)
			syms[offs[l]++] = (u16)i;
	} while (++i < n);
	/* table : vide, puis les codes assez courts */
	u64 entries = (u64)1 << tbits;
	i = 0;
	do
		table[i] = 0;
	while (++i < entries);
	u32 code = 0;
	u64 at = 0;
	for (u64 len = 1; len <= tbits; len++) {
		for (u32 c = count[len]; c; c--) {
			u64 r = ar_rev32(code) >> (32 - len); /* code inversé */
			u16 e = (u16)((u32)syms[at] << 4 | (u32)len);
			u64 step = (u64)1 << len;
			do {
				table[r] = e;
				r += step;
			} while (r < entries);
			code++;
			at++;
		}
		code <<= 1;
	}
	return 0;
}

/* décodage canonique, bit à bit, d'un code long (au moins 15 bits) : comptes,
 * symboles triés -> symbole, négatif : code invalide */
static long if_canon(struct dec *d, const u16 *count, const u16 *syms)
{
	long code = 0, first = 0, index = 0;
	u64 b = d->bits;
	for (u64 len = 1; len <= 15; len++) {
		code |= (long)(b & 1);
		b >>= 1;
		long c = count[len];
		if (code - c < first) {
			index += code - first;
			d->bits = SHR(d->bits, len);
			d->n -= len;
			return syms[index];
		}
		index += c;
		first += c;
		first <<= 1;
		code <<= 1;
	}
	return -1;
}

/* les tables des codes fixes -> 0 */
static int if_fixed(struct dec *d)
{
	struct infl *s = d->s;
	for (u64 i = 0; i < 288; i++)
		s->lens[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8;
	if (if_build(s->lens, 288, s->lcount, s->lsym, s->ltab, IF_LBITS, 0))
		return 1;
	for (u64 i = 0; i < 32; i++)
		s->lens[i] = 5;
	return if_build(s->lens, 32, s->dcount, s->dsym, s->dtab, IF_DBITS, 0);
}

/* l'en-tête d'un bloc dynamique -> 0, erreur */
static long if_dynamic(struct dec *d)
{
	struct infl *s = d->s;
	if (if_need(d))
		return AR_E_CORRUPT;
	u64 hlit = (d->bits & 31) + 257;
	u64 hdist = (d->bits >> 5 & 31) + 1;
	u64 hclen = (d->bits >> 10 & 15) + 4;
	d->bits >>= 14;
	d->n -= 14;
	if (hlit > 286 || hdist > 30)
		return AR_E_CORRUPT;
	for (u64 i = 0; i < 19; i++)
		s->lens[i] = 0;
	for (u64 i = 0; i < hclen; i++) {
		if (if_need(d))
			return AR_E_CORRUPT;
		s->lens[df_clorder[i]] = (u8)(d->bits & 7);
		d->bits >>= 3;
		d->n -= 3;
	}
	if (if_build(s->lens, 19, s->dcount, s->dsym, s->dtab, 7, 0)) /* (les longueurs tiennent dans la table) */
		return AR_E_CORRUPT;
	/* les longueurs des deux codes */
	u64 total = hlit + hdist, i = 0;
	while (i < total) {
		if (if_need(d))
			return AR_E_CORRUPT;
		u32 t = s->dtab[d->bits & 127];
		if (!t)
			return AR_E_CORRUPT;
		d->bits = SHR(d->bits, t & 15);
		d->n -= t & 15;
		t >>= 4;
		if (t < 16) {
			s->lens[i++] = (u8)t;
			continue;
		}
		u8 v = 0; /* valeur répétée */
		u64 rep;
		if (t == 16) {
			if (!i)
				return AR_E_CORRUPT;
			v = s->lens[i - 1];
			rep = (d->bits & 3) + 3;
			d->bits >>= 2;
			d->n -= 2;
		} else if (t == 17) {
			rep = (d->bits & 7) + 3;
			d->bits >>= 3;
			d->n -= 3;
		} else {
			rep = (d->bits & 127) + 11;
			d->bits >>= 7;
			d->n -= 7;
		}
		if (i + rep > total)
			return AR_E_CORRUPT;
		do
			s->lens[i++] = v;
		while (--rep);
	}
	if (!s->lens[256])
		return AR_E_CORRUPT; /* pas de fin de bloc */
	/* distances d'abord : leurs longueurs suivent celles des littéraux */
	if (if_build(s->lens + hlit, hdist, s->dcount, s->dsym, s->dtab, IF_DBITS, 1))
		return AR_E_CORRUPT;
	if (if_build(s->lens, hlit, s->lcount, s->lsym, s->ltab, IF_LBITS, 1))
		return AR_E_CORRUPT;
	return 0;
}

/* un octet à sortir -> 0, erreur */
static long if_put(struct dec *d, u8 b)
{
	if (d->op >= d->oend) {
		long r = if_flush_room(d);
		if (r)
			return r;
	}
	*d->op++ = b;
	return 0;
}

/* un bloc stocké -> 0, erreur */
static long if_stored(struct dec *d)
{
	/* LEN, NLEN, en passant par if_byte (aligné) */
	long b0 = if_byte(d);
	if (b0 < 0)
		return AR_E_CORRUPT;
	long b1 = if_byte(d);
	if (b1 < 0)
		return AR_E_CORRUPT;
	u64 len = (u64)b0 | (u64)b1 << 8;
	long c0 = if_byte(d);
	if (c0 < 0)
		return AR_E_CORRUPT;
	long c1 = if_byte(d);
	if (c1 < 0)
		return AR_E_CORRUPT;
	u64 nlen = (u64)c0 | (u64)c1 << 8;
	if ((nlen ^ len) != 0xFFFF)
		return AR_E_CORRUPT;
	/* ce qui reste dans le registre de bits */
	while (len) {
		if (d->n < 8)
			goto bulk;
		long b = if_byte(d);
		if (b < 0)
			return AR_E_CORRUPT;
		long r = if_put(d, (u8)b);
		if (r)
			return r;
		len--;
	}
	return 0;
bulk:
	d->bits = 0;
	d->n = 0;
	while (len) {
		if (d->ip >= d->iend) {
			if (if_pull(d))
				continue;
			return AR_E_CORRUPT;
		}
		if (d->op >= d->oend) {
			long r = if_flush_room(d);
			if (r)
				return r;
		}
		u64 c = d->iend - d->ip;
		if (c > len)
			c = len;
		u64 room = (u64)(d->oend - d->op);
		if (c > room)
			c = room;
		ar_memcpy(d->op, (const u8 *)d->ip, c);
		d->op += c;
		d->ip += c;
		len -= c;
	}
	return 0;
}

/* tous les blocs d'un flux deflate -> 0, erreur */
static long if_stream(struct dec *d)
{
	struct infl *s = d->s;
	const u16 *lt = s->ltab, *dt = s->dtab;
	u32 t, sym;
	u64 len, dist;
	long r;
block:
	if (if_need(d))
		return AR_E_CORRUPT;
	s->final = d->bits & 1;
	{
		u64 type = d->bits >> 1 & 3;
		d->bits >>= 3;
		d->n -= 3;
		if (type == 0) {
			r = if_stored(d);
			if (r)
				return r;
			goto next;
		}
		if (type == 1) {
			if (if_fixed(d))
				return AR_E_CORRUPT;
		} else if (type == 2) {
			r = if_dynamic(d);
			if (r)
				return r;
		} else {
			return AR_E_CORRUPT;
		}
	}
	/* --- décodage d'un bloc */
dec:
	if (d->ip >= d->iend8)
		goto rs;
	{
		u64 w;
		__builtin_memcpy(&w, (const void *)d->ip, 8);
		d->bits |= SHL(w, d->n);
		d->ip += (63 - d->n) >> 3;
		d->n |= 56;
	}
rd:
	t = lt[d->bits & ((1 << IF_LBITS) - 1)];
	if (!t)
		goto lslow;
	d->bits = SHR(d->bits, t & 15);
	d->n -= t & 15;
	sym = t >> 4;
symb:
	if (sym >= 256)
		goto nlit;
	if (d->op >= d->oend)
		goto ofull;
olit:
	*d->op++ = (u8)sym;
	/* (deux de plus sans recharger : au moins 36 bits restent) */
	for (int k = 0; k < 2; k++) {
		t = lt[d->bits & ((1 << IF_LBITS) - 1)];
		if (t >= 256 << 4 || !t || d->op >= d->oend)
			goto dec;
		d->bits = SHR(d->bits, t & 15);
		d->n -= t & 15;
		*d->op++ = (u8)(t >> 4);
	}
	goto dec;
rs:
	if (if_fill(d))
		return AR_E_CORRUPT;
	goto rd;
lslow:
	r = if_canon(d, s->lcount, s->lsym);
	if (r < 0)
		return AR_E_CORRUPT;
	sym = (u32)r;
	goto symb;
ofull:
	r = if_flush_room(d);
	if (r)
		return r;
	goto olit;
nlit:
	if (sym == 256)
		goto eob;
	sym -= 257;
	if (sym >= 29)
		return AR_E_CORRUPT;
	{
		u64 eb = if_lext[sym];
		len = if_lbase[sym] + (d->bits & (SHL((u64)1, eb) - 1));
		d->bits = SHR(d->bits, eb);
		d->n -= eb;
	}
	/* distance */
	t = dt[d->bits & ((1 << IF_DBITS) - 1)];
	if (!t)
		goto dslow;
	d->bits = SHR(d->bits, t & 15);
	d->n -= t & 15;
	sym = t >> 4;
dsym:
	if (sym >= 30)
		return AR_E_CORRUPT;
	{
		u64 eb = if_dext[sym];
		dist = if_dbase[sym] + (d->bits & (SHL((u64)1, eb) - 1));
		d->bits = SHR(d->bits, eb);
		d->n -= eb;
	}
chk:
	if (dist > (u64)(d->op - s->obase))
		return AR_E_CORRUPT;
	if ((uintptr_t)d->op + len + 16 > (uintptr_t)d->oend)
		goto near;
	{
		u8 *from = d->op - dist;
		if (dist < 8)
			goto shortc;
		/* par 8 octets (peut écrire jusqu'à 7 octets de trop, la place y est) */
		u8 *o = d->op;
		long rem = (long)len;
		do {
			u64 w;
			__builtin_memcpy(&w, from, 8);
			__builtin_memcpy(o, &w, 8);
			from += 8;
			o += 8;
			rem -= 8;
		} while (rem > 0);
		d->op += len;
		goto dec;
	shortc:
		do
			*d->op++ = *from++;
		while (--len);
		goto dec;
	}
dslow:
	r = if_canon(d, s->dcount, s->dsym);
	if (r < 0)
		return AR_E_CORRUPT;
	sym = (u32)r;
	goto dsym;
near: /* près de la fin de la sortie */
	if (s->sink) {
		r = if_flush(d, 1);
		if (r)
			return r;
		goto chk;
	}
	if ((uintptr_t)d->op + len > (uintptr_t)d->oend)
		return AR_E_SPACE;
	{
		u8 *from = d->op - dist;
		do
			*d->op++ = *from++;
		while (--len);
	}
	goto dec;
eob:
	if (s->over << 3 > d->n)
		return AR_E_CORRUPT; /* des bits inventés ont servi */
next:
	if (!s->final)
		goto block;
	return 0;
}

/* les 4 octets qui suivent, petit-boutien (be = 0) ou grand (1) -> valeur,
 * négatif : fin */
static long if_u32(struct dec *d, int be)
{
	u64 v = 0;
	for (u64 sh = 0; sh < 32; sh += 8) {
		long b = if_byte(d);
		if (b < 0)
			return b;
		if (be)
			v = v << 8 | (u64)b;
		else
			v |= (u64)b << sh;
	}
	return (long)v;
}

/* l'en-tête gzip (après 1F 8B : déjà lus) -> 0, erreur */
static long if_gzhead(struct dec *d)
{
	struct infl *s = d->s;
	if (if_byte(d) != 8)
		return AR_E_CORRUPT;
	long fl = if_byte(d); /* drapeaux */
	if (fl < 0 || (fl & 0xE0))
		return AR_E_CORRUPT;
	long t = if_u32(d, 0);
	if (t < 0)
		return AR_E_CORRUPT;
	if (!s->members)
		s->gztime = (u64)t;
	if_byte(d);
	if (if_byte(d) < 0)
		return AR_E_CORRUPT;
	if (fl & 4) { /* FEXTRA */
		long a = if_byte(d);
		if (a < 0)
			return AR_E_CORRUPT;
		long b = if_byte(d);
		if (b < 0)
			return AR_E_CORRUPT;
		for (u64 x = (u64)a | (u64)b << 8; x; x--)
			if (if_byte(d) < 0)
				return AR_E_CORRUPT;
	}
	if (fl & 8) { /* FNAME */
		u64 k = 0;
		long c;
		do {
			c = if_byte(d);
			if (c < 0)
				return AR_E_CORRUPT;
			if (!s->members && k < 255)
				s->gzname[k++] = (u8)c;
		} while (c);
		if (!s->members)
			s->gzname[k] = 0;
	}
	if (fl & 16) { /* FCOMMENT */
		long c;
		do {
			c = if_byte(d);
			if (c < 0)
				return AR_E_CORRUPT;
		} while (c);
	}
	if (fl & 2) { /* FHCRC */
		if_byte(d);
		if (if_byte(d) < 0)
			return AR_E_CORRUPT;
	}
	return 0;
}

/* état préparé (if_begin) -> octets décodés, erreur (AR_STOP : le puits s'est
 * arrêté) */
long if_run(struct infl *s)
{
	struct dec dd, *d = &dd;
	long r, b0, b1, v;
	d->s = s;
	d->bits = 0;
	d->n = 0;
	d->ip = s->ip;
	d->iend = s->iend;
	d->iend8 = d->iend - 8;
	d->op = s->obase;
	d->oend = s->oend;
	s->crc = 0;
	s->adler = 1;
	s->want = 0;
	if (!s->wrap) {
		s->want = 1;
		r = if_stream(d);
		if (r)
			return r;
		r = if_flush(d, 0);
		if (r)
			return r;
		return (long)s->total;
	}
	b0 = if_byte(d); /* premiers octets */
	if (b0 < 0)
		return AR_E_CORRUPT;
	b1 = if_byte(d);
	if (b1 < 0)
		return AR_E_CORRUPT;
	if (s->wrap == AR_AUTO)
		s->wrap = (b0 == 0x1F && b1 == 0x8B) ? AR_GZIP : AR_ZLIB;
	if (s->wrap == AR_GZIP)
		goto gz;
	if (s->wrap != AR_ZLIB)
		return AR_E_CORRUPT;
	/* zlib */
	if ((b0 & 15) != 8 || (b0 >> 4) > 7 || (b1 & 0x20) || ((u64)b0 << 8 | (u64)b1) % 31)
		return AR_E_CORRUPT; /* (FDICT : pas pris en charge) */
	s->want = 2;
	r = if_stream(d);
	if (r)
		return r;
	r = if_flush(d, 0);
	if (r)
		return r;
	v = if_u32(d, 1);
	if (v < 0)
		return AR_E_CORRUPT;
	if ((u64)v != s->adler)
		return AR_E_CRC;
	return (long)s->total;
gz: /* gzip : un ou plusieurs membres */
	if (b0 != 0x1F || b1 != 0x8B)
		return AR_E_CORRUPT;
	for (;;) {
		r = if_gzhead(d);
		if (r)
			return r;
		s->want = 1;
		s->crc = 0;
		u64 start = s->total; /* octets au début du membre */
		r = if_stream(d);
		if (r)
			return r;
		r = if_flush(d, 0);
		if (r)
			return r;
		v = if_u32(d, 0);
		if (v < 0)
			return AR_E_CORRUPT;
		if ((u64)v != s->crc)
			return AR_E_CRC;
		v = if_u32(d, 0);
		if (v < 0)
			return AR_E_CORRUPT;
		if ((u64)v != (u32)(s->total - start))
			return AR_E_CRC;
		s->members++;
		if (if_more(d))
			break;
		if (if_byte(d) != 0x1F) /* un autre membre, ou des octets de bourrage */
			break;
		if (if_byte(d) != 0x8B)
			break;
	}
	return (long)s->total;
}
