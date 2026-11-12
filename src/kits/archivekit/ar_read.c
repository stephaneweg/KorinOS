/* ar_read.c - lire une archive : ZIP (avec ZIP64), tar, tar.gz, gz, sur la
 * carte ou dans un tampon (d'après ar_read.inc). Les entrées sont listées à
 * l'ouverture ; rien n'est cru sans vérification : tailles et positions sont
 * bornées par la source, et le contenu est vérifié par son CRC à la lecture.
 *
 * Un tar.gz est décompressé en mémoire à l'ouverture (on ne peut pas sauter
 * dans un flux deflate) ; un .gz d'un seul fichier reste en place : il n'est
 * décodé qu'à la lecture. */
#include "ar.h"

#define RD_SIZE (4096 - 16)
#define RD_LONG 65536
#define RD_POOL (1 << 20)

#define ZIP_LOCAL 0x04034B50u
#define ZIP_CENTRAL 0x02014B50u
#define ZIP_END 0x06054B50u
#define ZIP_END64 0x06064B50u
#define ZIP_LOC64 0x07064B50u

/* "ustar\0" "00" */
#define USTAR_MAGIC 0x3030007261747375ULL

static const char s_path[] = "path";
static const char s_size[] = "size";
static const char s_mtime[] = "mtime";
static const char s_vfs[] = "vfs";
static const char s_data[] = "data";

static const u16 cp437_tab[128] = {
	0xC7,   0xFC,   0xE9,   0xE2,   0xE4,   0xE0,   0xE5,   0xE7,   0xEA,   0xEB,   0xE8,   0xEF,   0xEE,
	0xEC,   0xC4,   0xC5,   0xC9,   0xE6,   0xC6,   0xF4,   0xF6,   0xF2,   0xFB,   0xF9,   0xFF,   0xD6,
	0xDC,   0xA2,   0xA3,   0xA5,   0x20A7, 0x192,  0xE1,   0xED,   0xF3,   0xFA,   0xF1,   0xD1,   0xAA,
	0xBA,   0xBF,   0x2310, 0xAC,   0xBD,   0xBC,   0xA1,   0xAB,   0xBB,   0x2591, 0x2592, 0x2593, 0x2502,
	0x2524, 0x2561, 0x2562, 0x2556, 0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510, 0x2514,
	0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F, 0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550,
	0x256C, 0x2567, 0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B, 0x256A, 0x2518, 0x250C,
	0x2588, 0x2584, 0x258C, 0x2590, 0x2580, 0x3B1,  0xDF,   0x393,  0x3C0,  0x3A3,  0x3C3,  0xB5,   0x3C4,
	0x3A6,  0x398,  0x3A9,  0x3B4,  0x221E, 0x3C6,  0x3B5,  0x2229, 0x2261, 0xB1,   0x2265, 0x2264, 0x2320,
	0x2321, 0xF7,   0x2248, 0xB0,   0x2219, 0xB7,   0x221A, 0x207F, 0xB2,   0x25A0, 0xA0,
};

static inline u16 rd16(const u8 *p)
{
	return (u16)(p[0] | p[1] << 8);
}

static inline u32 rd32(const u8 *p)
{
	return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline u64 rd64(const u8 *p)
{
	return (u64)rd32(p) | (u64)rd32(p + 4) << 32;
}

/* poignée -> archive, 0 si ce n'en est pas une */
static struct reader *rd_handle(long h)
{
	if (h <= 0)
		return 0;
	struct reader *a = (struct reader *)h;
	if (a->magic != AR_MAGIC_RD)
		return 0;
	return a;
}

/* archive, indice -> entrée, 0 si hors bornes */
static struct entry *rd_index(struct reader *a, long i)
{
	if ((u64)i >= a->n)
		return 0;
	return a->tbl + i;
}

/* archive, position, tampon, octets -> 0, erreur */
long src_read(struct reader *a, u64 pos, void *buf, u64 n)
{
	if (pos > a->len || n > a->len - pos)
		return AR_E_CORRUPT;
	if (a->mem) {
		ar_memcpy(buf, a->mem + pos, n);
		return 0;
	}
	long r = f_seek(a->fd, (long)pos, 0);
	if (r < 0)
		return r;
	u8 *p = buf;
	while (n) {
		r = f_read(a->fd, (long)p, (long)n);
		if (r <= 0)
			return r < 0 ? r : AR_E_CORRUPT;
		p += r;
		n -= (u64)r;
	}
	return 0;
}

/* -> une entrée de plus (à zéro), 0 si plus de mémoire */
static struct entry *rd_add(struct reader *a)
{
	if (a->n >= a->cap) {
		u64 nc = a->cap << 1;
		if (!nc)
			nc = 256;
		struct entry *t = ar_alloc(sizeof(struct entry) * nc);
		if (!t)
			return 0;
		if (a->tbl) {
			ar_memcpy(t, a->tbl, a->n * sizeof(struct entry));
			ar_mfree(a->tbl);
		}
		a->tbl = t;
		a->cap = nc;
	}
	return a->tbl + a->n++;
}

/* octets -> place dans les noms, 0 si plus de mémoire */
static u8 *pool_room(struct reader *a, u64 n)
{
	if (a->pool && a->poolpos + n <= RD_POOL)
		return a->pool + a->poolpos;
	u8 *p = ar_alloc(RD_POOL);
	if (!p)
		return 0;
	*(u8 **)p = a->pool;
	a->pool = p;
	return p + 8;
}

/* octets -> 1 : UTF-8 valable */
static int utf8_ok(const u8 *p, u64 n)
{
	const u8 *e = p + n;
	while (p < e) {
		u32 c = *p++;
		if (c < 0x80)
			continue;
		u64 k = 1;
		if (c < 0xC2)
			return 0;
		if (c >= 0xE0) {
			k = 2;
			if (c >= 0xF0) {
				k = 3;
				if (c >= 0xF5)
					return 0;
			}
		}
		do {
			if (p >= e)
				return 0;
			if ((*p++ & 0xC0) != 0x80)
				return 0;
		} while (--k);
	}
	return 1;
}

/* nom brut, octets, 1 : UTF-8 sûr -> nom propre (dans les noms de l'archive),
 * *slash = 1 : il finissait par / ; 0 si plus de mémoire.
 * \ devient /, sans / ni ./ au début, sans // ; CP437 converti en UTF-8. */
static u8 *rd_name(struct reader *a, const u8 *raw, u64 n, int utf8, int *slash)
{
	if (!utf8)
		utf8 = utf8_ok(raw, n);
	u8 *start = pool_room(a, n * 3 + 2);
	if (!start) {
		*slash = 0;
		return 0;
	}
	u8 *w = start;
	for (u64 i = 0; i < n;) {
		u32 c = raw[i++];
		if (!c)
			break;
		if (c == '\\')
			c = '/';
		if (c >= 0x80 && !utf8) {
			c = cp437_tab[c - 0x80];
			if (c >= 0x80) {
				if (c >= 0x800) {
					*w++ = (u8)(c >> 12 | 0xE0);
					*w++ = (u8)((c >> 6 & 0x3F) | 0x80);
				} else {
					*w++ = (u8)(c >> 6 | 0xC0);
				}
				c = (c & 0x3F) | 0x80;
			}
		}
		*w++ = (u8)c;
	}
	*w = 0;
	/* nettoyage sur place */
	u8 *r = start;
	w = start;
	int sl = 0;
	for (;;) {
		if (*r == '/') {
			r++;
			continue;
		}
		if (*r == '.' && r[1] == '/') {
			r += 2;
			continue;
		}
		break;
	}
	for (;;) {
		u8 c = *r++;
		if (!c)
			break;
		if (c == '/' && (w == start || w[-1] == '/'))
			continue;
		*w++ = c;
	}
	if (w != start && w[-1] == '/') {
		w--;
		sl = 1;
	}
	*w = 0;
	a->poolpos = (u64)(start - a->pool) + (u64)(w - start) + 1; /* (start est dans le morceau en cours) */
	*slash = sl;
	return start;
}

/* --- dates --------------------------------------------------------------------- */

/* année, mois, jour -> jours depuis 1970 (année >= 0) */
static u64 days_civil(u64 y, u64 m, u64 d)
{
	if (m <= 2) {
		y--;
		m += 12;
	}
	m -= 3; /* mois depuis mars */
	u64 era = y / 400;
	u64 yoe = y - era * 400;
	u64 doy = (m * 153 + 2) / 5 + d - 1;
	u64 doe = yoe * 365 + (yoe >> 2) - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

/* date DOS (date << 16 | heure) -> secondes UTC, 0 : pas une date */
static u64 dos_unix(u32 w)
{
	u32 mo = w >> 21 & 15, dd = w >> 16 & 31;
	if (!mo || mo > 12 || !dd)
		return 0;
	u64 t = days_civil((w >> 25) + 1980, mo, dd) * 86400;
	t += (u64)(w >> 11 & 31) * 3600;
	t += (u64)(w >> 5 & 63) * 60;
	t += (u64)(w & 31) << 1;
	return t;
}

/* --- ZIP ------------------------------------------------------------------------- */

static long rd_zip(struct reader *a)
{
	u8 *tbuf = 0, *cbuf = 0; /* blocs de la fin, du répertoire (fichier) */
	u8 e64[56];
	const u8 *p;
	long r;
	u64 len = a->len;
	u64 look = len < 65557 ? len : 65557; /* octets de fin regardés */
	u64 at, cnt, csize, cpos, epos, delta;
	if (look < 22) {
		r = AR_E_FORMAT;
		goto out;
	}
	if (a->mem) {
		p = a->mem + len - look;
	} else {
		tbuf = ar_alloc(look);
		if (!tbuf) {
			r = AR_E_NOMEM;
			goto out;
		}
		p = tbuf;
		r = src_read(a, len - look, tbuf, look);
		if (r)
			goto out;
	}
	/* la fin du répertoire, en partant de la fin */
	at = look - 22;
	for (;;) {
		if (rd32(p + at) == ZIP_END && at + 22 + rd16(p + at + 20) <= look)
			break;
		if (!at) {
			r = AR_E_FORMAT;
			goto out;
		}
		at--;
	}
	{
		const u8 *e = p + at;
		cnt = rd16(e + 10);   /* entrées */
		csize = rd32(e + 12); /* taille du répertoire */
		cpos = rd32(e + 16);  /* position du répertoire */
		epos = len - look + at; /* position de cette fin */
		/* ZIP64 ? */
		if (at >= 20 && rd32(e - 20) == ZIP_LOC64) {
			u64 p64 = rd64(e - 20 + 8); /* position de la fin ZIP64 */
			if (p64 >= len) {
				r = AR_E_CORRUPT;
				goto out;
			}
			r = src_read(a, p64, e64, 56);
			if (r)
				goto out;
			if (rd32(e64) != ZIP_END64) {
				r = AR_E_CORRUPT;
				goto out;
			}
			cnt = rd64(e64 + 32);
			csize = rd64(e64 + 40);
			cpos = rd64(e64 + 48);
			epos = p64;
		}
	}
	/* décalage possible : données avant l'archive */
	if (csize > epos) {
		r = AR_E_CORRUPT;
		goto out;
	}
	{
		u64 real = epos - csize; /* où le répertoire est vraiment */
		if (real < cpos) {
			r = AR_E_CORRUPT;
			goto out;
		}
		delta = real - cpos;
		cpos = real;
	}
	if (!csize)
		goto done; /* archive vide */
	if (a->mem) {
		p = a->mem + cpos;
	} else {
		cbuf = ar_alloc(csize);
		if (!cbuf) {
			r = AR_E_NOMEM;
			goto out;
		}
		p = cbuf;
		r = src_read(a, cpos, cbuf, csize);
		if (r)
			goto out;
	}
	while (csize >= 46 && rd32(p) == ZIP_CENTRAL) {
		u64 nlen = rd16(p + 28), xlen = rd16(p + 30), clen = rd16(p + 32);
		u64 rec = nlen + xlen + clen + 46;
		if (rec > csize) {
			r = AR_E_CORRUPT;
			goto out;
		}
		struct entry *en = rd_add(a);
		if (!en) {
			r = AR_E_NOMEM;
			goto out;
		}
		u32 fl = rd16(p + 8); /* drapeaux */
		en->method = rd16(p + 10);
		en->crc = rd32(p + 16);
		en->pack = rd32(p + 20);
		en->size = rd32(p + 24);
		en->off = rd32(p + 42);
		en->flags = (fl & 1) ? 2 : 0;
		en->time = dos_unix(rd32(p + 12));
		/* ZIP64 : extra 1 */
		const u8 *x = p + 46 + nlen, *xe = x + xlen; /* fin de l'extra */
		while (x + 4 <= xe) {
			u32 id = rd16(x), sz = rd16(x + 2);
			const u8 *q = x + 4 + sz;
			if (q > xe)
				break;
			if (id == 1) {
				/* tailles et position, dans cet ordre, chacune seulement si son
				 * champ est plein */
				const u8 *v = x + 4;
				if (en->size == 0xFFFFFFFFu) {
					if (v + 8 > q)
						goto corrupt;
					en->size = rd64(v);
					v += 8;
				}
				if (en->pack == 0xFFFFFFFFu) {
					if (v + 8 > q)
						goto corrupt;
					en->pack = rd64(v);
					v += 8;
				}
				if (en->off == 0xFFFFFFFFu) {
					if (v + 8 > q)
						goto corrupt;
					en->off = rd64(v);
				}
			}
			x = q;
		}
		/* décalage, bornes */
		{
			u64 o = en->off + delta;
			if (o < delta)
				goto corrupt;
			en->off = o;
			if (o >= len || en->pack > len)
				goto corrupt;
		}
		/* le nom */
		int sl;
		u8 *nm = rd_name(a, p + 46, nlen, (rd16(p + 8) >> 11) & 1, &sl);
		if (!nm) {
			r = AR_E_NOMEM;
			goto out;
		}
		en->name = nm;
		if (sl)
			en->flags |= 1;
		p += rec;
		csize -= rec;
	}
	/* ce qui est annoncé doit y être (au-delà, c'est le champ de 16 bits qui a
	 * bouclé) */
	if (a->n < cnt)
		goto corrupt;
done:
	a->fmt = AR_FMT_ZIP;
	r = 0;
	goto out;
corrupt:
	r = AR_E_CORRUPT;
out:
	ar_mfree(tbuf);
	ar_mfree(cbuf);
	return r;
}

/* --- tar ---------------------------------------------------------------------------- */

/* bloc de 512 octets -> 1 : la somme de l'en-tête tar est bonne */
static int tar_sum_ok(const u8 *b)
{
	u64 sum = 0;
	long ssum = 0; /* somme signée (vieux tar) */
	for (u64 i = 0; i < 512; i++) {
		u8 c = b[i];
		if (i - 148 < 8)
			c = ' ';
		sum += c;
		ssum += (int8_t)c;
	}
	/* le champ : octal, blancs et 0 autour */
	const u8 *f = b + 148;
	u64 v = 0, digits = 0;
	for (int k = 8; k; k--) {
		u32 c = *f++;
		if (c == ' ' || !c) {
			if (digits)
				break;
			continue;
		}
		c -= '0';
		if (c > 7)
			return 0;
		v = c + (v << 3);
		digits++;
	}
	if (!digits)
		return 0;
	return v == sum || v == (u64)ssum;
}

/* bloc de 512 octets -> 1 : tout à zéro */
static int tar_zero(const u8 *b)
{
	for (int i = 0; i < 512; i++)
		if (b[i])
			return 0;
	return 1;
}

/* champ, octets -> nombre (octal, ou base 256 de GNU) */
static u64 tar_num(const u8 *f, u64 n)
{
	const u8 *e = f + n;
	u64 v;
	if (f[0] & 0x80) {
		v = f[0] & 0x7F;
		for (f++; f < e; f++)
			v = *f | v << 8;
		return v;
	}
	v = 0;
	while (f < e && *f == ' ')
		f++;
	while (f < e) {
		u32 c = (u32)*f++ - '0';
		if (c > 7)
			break;
		v = c + (v << 3);
	}
	return v;
}

/* clé (chaîne), début, fin -> 1 si le texte [début, fin[ est cette clé */
static int pax_key(const char *k, const u8 *s, const u8 *e)
{
	while (*k) {
		if (s >= e || (u8)*k++ != *s++)
			return 0;
	}
	return s == e;
}

/* octets de l'en-tête pax dans a->lng -> longueur du nom (en a->lng), *size
 * (-1 : absente), *mtime (-1 : absente) */
static u64 tar_pax(struct reader *a, u64 n, u64 *size, u64 *mtime)
{
	u8 *b = a->lng, *end = b + n;
	u64 nl = 0, sz = (u64)-1, mt = (u64)-1;
	u8 *r = b;
	while (r < end) {
		u8 *p = r;
		u64 L = 0;
		for (;;) {
			if (p >= end)
				goto e;
			u32 c = *p++;
			if (c == ' ')
				break;
			c -= '0';
			if (c > 9)
				goto e;
			L = L * 10 + c;
		}
		if (!L || L <= (u64)(p - r))
			goto e; /* (trop court pour contenir sa clé) */
		/* fin de la fiche (addition sur 64 bits, comme l'asm) */
		u8 *re = (u8 *)((uintptr_t)r + L);
		if ((uintptr_t)re > (uintptr_t)end)
			goto e;
		u8 *k = p;
		while (k < re && *k != '=')
			k++;
		if (k < re) {
			u8 *v = k + 1;
			if (pax_key(s_path, p, k)) {
				if (v < re) {
					u64 vl = (u64)(re - v) - 1; /* (sans le \n) */
					ar_memcpy(b, v, vl);
					b[vl] = 0;
					nl = vl;
					/* le nom écrase le début : la fiche suivante est intacte
					 * puisqu'on recule */
				}
			} else if (pax_key(s_size, p, k)) {
				sz = 0;
				while (v < re) {
					u32 c = (u32)*v++ - '0';
					if (c > 9)
						break;
					sz = sz * 10 + c;
				}
			} else if (pax_key(s_mtime, p, k)) {
				mt = 0;
				while (v < re) {
					u32 c = (u32)*v++ - '0';
					if (c > 9)
						break;
					mt = mt * 10 + c;
				}
			}
		}
		r = re;
	}
e:
	*size = sz;
	*mtime = mt;
	return nl;
}

/* les données de l'en-tête spécial (taille sz, en pos) dans a->lng -> 0, erreur ;
 * *got = octets */
static long tar_getlong(struct reader *a, u64 pos, u64 sz, u64 *got)
{
	if (!a->lng) {
		a->lng = ar_alloc(RD_LONG);
		if (!a->lng)
			return AR_E_NOMEM;
	}
	u64 c = sz > RD_LONG - 1 ? RD_LONG - 1 : sz;
	a->lng[c] = 0;
	*got = c;
	return src_read(a, pos, a->lng, c);
}

static long rd_tar(struct reader *a)
{
	u8 h[512], nb[368];
	u64 pos = 0;          /* position */
	u64 lng = 0;          /* nom long en attente (octets) */
	u64 psize = (u64)-1;  /* taille pax */
	u64 ptime = (u64)-1;  /* date pax */
	long r;
	for (;;) {
		u64 rem = a->len - pos;
		if (!rem)
			return 0;
		if (rem < 512)
			return AR_E_CORRUPT;
		r = src_read(a, pos, h, 512);
		if (r)
			return r;
		if (tar_zero(h))
			return 0;
		if (!tar_sum_ok(h))
			return AR_E_CORRUPT;
		u64 size = tar_num(h + 124, 12);
		if (psize != (u64)-1)
			size = psize;
		psize = (u64)-1;
		if (size > a->len - pos - 512)
			return AR_E_CORRUPT; /* tronqué */
		u64 data = pos + 512;
		pos = data + ((size + 511) & ~(u64)511); /* en-tête suivant */
		u8 type = h[156];
		if (type == 'L') {
			u64 got;
			r = tar_getlong(a, data, size, &got);
			if (r)
				return r;
			lng = got;
			continue;
		}
		if (type == 'x') {
			u64 got, nl;
			r = tar_getlong(a, data, size, &got);
			if (r)
				return r;
			nl = tar_pax(a, got, &psize, &ptime);
			if (nl)
				lng = nl;
			continue;
		}
		if (type == 'K' || type == 'g')
			continue;
		if (type != '5' && type != '0' && type != '7' && type) {
			lng = 0; /* liens, périphériques : laissés */
			ptime = (u64)-1;
			continue;
		}
		struct entry *en = rd_add(a);
		if (!en)
			return AR_E_NOMEM;
		en->size = size;
		en->pack = size;
		en->off = data;
		u64 t = tar_num(h + 136, 12);
		if (ptime != (u64)-1)
			t = ptime;
		ptime = (u64)-1;
		en->time = t;
		en->flags = type == '5' ? 1 : 0;
		const u8 *nm;
		u64 nlen;
		if (lng) {
			nm = a->lng;
			nlen = lng;
			lng = 0;
		} else {
			/* préfixe ustar + nom */
			u8 *w = nb;
			if (rd64(h + 257) == USTAR_MAGIC) {
				const u8 *s = h + 345;
				for (int k = 155; k && *s; k--)
					*w++ = *s++;
				if (w != nb)
					*w++ = '/';
			}
			const u8 *s = h;
			for (int k = 100; k && *s; k--)
				*w++ = *s++;
			nm = nb;
			nlen = (u64)(w - nb);
		}
		int sl;
		u8 *name = rd_name(a, nm, nlen, 1, &sl);
		if (!name)
			return AR_E_NOMEM;
		en->name = name;
		if (sl)
			en->flags |= 1;
	}
}

/* --- gzip ----------------------------------------------------------------------- */

/* -> état d'inflate, 0 si plus de mémoire */
static struct infl *rd_infl(struct reader *a)
{
	if (!a->ifs)
		a->ifs = ar_alloc(sizeof(struct infl));
	return a->ifs;
}

/* prêt pour if_run, l'entrée venant de la source */
static void rd_ifbegin(struct reader *a, struct infl *s, u64 pos, u64 n, u8 *out, u64 cap, struct sink *k, u64 wrap)
{
	if (a->mem) {
		if_begin(s, a->mem + pos, n, out, cap, k, wrap);
		return;
	}
	if_begin(s, 0, 0, out, cap, k, wrap);
	s->src = a;
	s->srcoff = pos;
	s->srcleft = n;
	s->ip = s->iend = (uintptr_t)s->ibuf;
}

/* un gz : tar.gz, ou un seul fichier */
static long rd_gz(struct reader *a)
{
	u8 head[512];
	u8 isz[16];
	struct sink k = {SKK_PROBE, (u64)head, 0, 512};
	long r;
	struct infl *s = rd_infl(a);
	if (!s)
		return AR_E_NOMEM;
	rd_ifbegin(a, s, 0, a->len, 0, 0, &k, AR_GZIP);
	r = if_run(s);
	if (r < 0)
		return r;
	/* ISIZE : les 4 derniers octets (taille d'un seul membre) */
	u32 size = 0;
	if (a->len >= 18) {
		isz[0] = isz[1] = isz[2] = isz[3] = 0;
		r = src_read(a, a->len - 4, isz, 4);
		if (r < 0)
			return r;
		size = rd32(isz);
	}
	if (k.pos < 512)
		goto single;
	if (tar_sum_ok(head))
		goto tgz;
	/* un tar vide : que des zéros, par enregistrements */
	if (!tar_zero(head) || size % 10240 || !size)
		goto single;
tgz:
	a->fmt = AR_FMT_TGZ;
	if (a->probe)
		return 0;
	/* tout en mémoire */
	k.kind = SKK_GROW;
	k.ptr = 0;
	k.pos = 0;
	k.cap = 0;
	if (size) {
		k.cap = (u64)size + 1;
		k.ptr = (u64)ar_alloc((u64)size + 1);
		if (!k.ptr)
			k.cap = 0;
	}
	rd_ifbegin(a, s, 0, a->len, 0, 0, &k, AR_GZIP);
	r = if_run(s);
	a->inner = (u8 *)k.ptr;
	if (r < 0)
		return r;
	a->rawlen = a->len;
	if (!k.ptr)
		return AR_E_CORRUPT;
	a->mem = (u8 *)k.ptr;
	a->len = k.pos;
	r = rd_tar(a);
	if (r)
		return r;
	/* part de chaque entrée dans le fichier compressé */
	for (u64 i = 0; i < a->n; i++)
		a->tbl[i].pack = a->tbl[i].size * a->rawlen / a->len;
	return 0;
single:
	a->fmt = AR_FMT_GZIP;
	if (a->probe)
		return 0;
	struct entry *en = rd_add(a);
	if (!en)
		return AR_E_NOMEM;
	en->size = size;
	en->pack = a->len;
	en->method = 8;
	en->time = s->gztime;
	/* le nom : celui de l'en-tête, sinon le fichier sans .gz, sinon « data » */
	const char *nm = (const char *)s->gzname;
	if (!*nm) {
		nm = a->base;
		if (!*nm) {
			nm = s_data;
		} else {
			u64 l = (u64)str_len((long)nm);
			if (l > 3) {
				char *x = a->base + l - 3;
				if (x[0] == '.' && (x[1] | 0x20) == 'g' && (x[2] | 0x20) == 'z')
					x[0] = 0;
			}
		}
	}
	int sl;
	u8 *name = rd_name(a, (const u8 *)nm, (u64)str_len((long)nm), 1, &sl);
	if (!name)
		return AR_E_NOMEM;
	en->name = name;
	return 0;
}

/* --- ouverture ---------------------------------------------------------------------- */

/* source en mémoire (0 : fichier), descripteur, octets, chemin (0 : aucun),
 * 1 : le format seulement -> archive, erreur (descripteur fermé) */
static long rd_open(u8 *mem, long fd, u64 len, const char *path, u64 probe)
{
	u8 head[512];
	long r;
	struct reader *a = ar_alloc(RD_SIZE);
	if (!a) {
		if (fd >= 0)
			f_close(fd);
		return AR_E_NOMEM;
	}
	a->magic = AR_MAGIC_RD;
	a->mem = mem;
	a->fd = fd;
	a->len = len;
	a->probe = probe;
	if (path) {
		/* le nom de base */
		const char *b = path, *p = path;
		for (;;) {
			char c = *p++;
			if (!c)
				break;
			if (c == '/' || c == ':')
				b = p;
		}
		char *d = a->base;
		for (int k = 255;;) {
			char c = *b++;
			*d++ = c;
			if (!c)
				break;
			if (!--k) {
				*d = 0;
				break;
			}
		}
	}
	/* les premiers octets */
	u64 n = len < 512 ? len : 512;
	memset(head, 0, 8);
	r = src_read(a, 0, head, n);
	if (r)
		goto fail;
	u32 w = rd32(head);
	if (w == ZIP_LOCAL || w == ZIP_END || w == 0x08074B50u) { /* (archive découpée, un seul morceau) */
		r = rd_zip(a);
	} else if ((w & 0xFFFF) == 0x8B1F) {
		r = rd_gz(a);
	} else if (n >= 512 && (tar_sum_ok(head) || (tar_zero(head) && !(len & 511) && len >= 1024))) {
		a->fmt = AR_FMT_TAR;
		r = a->probe ? 0 : rd_tar(a);
	} else {
		r = rd_zip(a); /* (ZIP avec des données devant : auto-extractible) */
	}
	if (!r)
		return (long)a;
fail:
	ar_close((long)a);
	return r;
}

/* chemin, 1 : format seulement -> archive */
static long rd_path(long path, u64 probe)
{
	long fd = f_open(path, O_READ);
	if (fd < 0)
		return fd;
	long size = f_seek(fd, 0, 2);
	if (size < 0) {
		f_close(fd);
		return size;
	}
	f_seek(fd, 0, 0);
	return rd_open(0, fd, (u64)size, (const char *)path, probe);
}

long ar_open(long path)
{
	return rd_path(path, 0);
}

long ar_open_mem(long buf, long n)
{
	if (!buf)
		return AR_E_INVAL;
	return rd_open((u8 *)buf, -1, (u64)n, 0, 0);
}

long ar_probe(long path)
{
	long a = rd_path(path, 1);
	if (a < 0)
		return a;
	long f = (long)((struct reader *)a)->fmt;
	ar_close(a);
	return f;
}

long ar_close(long h)
{
	if (!h)
		return 0;
	struct reader *a = rd_handle(h);
	if (!a)
		return AR_E_INVAL;
	a->magic = 0;
	if (a->fd >= 0)
		f_close(a->fd);
	ar_mfree(a->tbl);
	ar_mfree(a->ifs);
	ar_mfree(a->inner);
	ar_mfree(a->lng);
	for (u8 *p = a->pool; p;) {
		u8 *nx = *(u8 **)p;
		ar_mfree(p);
		p = nx;
	}
	ar_mfree(a);
	return 0;
}

long ar_format(long h)
{
	struct reader *a = rd_handle(h);
	if (!a)
		return AR_E_INVAL;
	return (long)a->fmt;
}

long ar_count(long h)
{
	struct reader *a = rd_handle(h);
	if (!a)
		return AR_E_INVAL;
	return (long)a->n;
}

long ar_entry(long h, long i, long buf)
{
	struct reader *a = rd_handle(h);
	if (!a)
		return AR_E_INVAL;
	struct entry *e = rd_index(a, i);
	if (!e || !buf)
		return AR_E_INVAL;
	u64 *o = (u64 *)buf;
	o[0] = (u64)e->name;
	o[1] = e->size;
	o[2] = e->pack;
	o[3] = e->time;
	o[4] = e->flags & 1;
	o[5] = e->method;
	o[6] = e->crc;
	o[7] = e->flags >> 1 & 1;
	return 0;
}

long ar_name(long h, long i)
{
	struct reader *a = rd_handle(h);
	if (!a)
		return AR_E_INVAL;
	struct entry *e = rd_index(a, i);
	if (!e)
		return AR_E_INVAL;
	return (long)e->name;
}

long ar_entry_size(long h, long i)
{
	struct reader *a = rd_handle(h);
	if (!a)
		return AR_E_INVAL;
	struct entry *e = rd_index(a, i);
	if (!e)
		return AR_E_INVAL;
	return (long)e->size;
}

/* chaînes -> 0 : égales ; nocase = 1 : sans la casse ASCII */
static int str_cmp(const u8 *a, const u8 *b, int nocase)
{
	for (;;) {
		u32 c = *a++, d = *b++;
		if (nocase) {
			if (c - 'A' < 26)
				c += 32;
			if (d - 'A' < 26)
				d += 32;
		}
		if (c != d)
			return 1;
		if (!c)
			return 0;
	}
}

long ar_find(long h, long name)
{
	struct reader *a = rd_handle(h);
	if (!a || !name)
		return AR_E_INVAL;
	for (int nc = 0; nc < 2; nc++)
		for (u64 i = 0; i < a->n; i++)
			if (!str_cmp(a->tbl[i].name, (const u8 *)name, nc))
				return (long)i;
	return E_NOENT;
}

/* archive, indice, sortie, capacité, puits (0 : la sortie) -> octets */
static long rd_entry(long h, long i, u8 *out, u64 cap, struct sink *k)
{
	struct reader *a = rd_handle(h);
	if (!a)
		return AR_E_INVAL;
	struct entry *e = rd_index(a, i);
	if (!e)
		return AR_E_INVAL;
	if (e->flags & 1)
		return 0;
	if (e->flags & 2)
		return AR_E_CRYPT;
	u64 off = e->off;   /* données */
	u64 pack = e->pack; /* octets dans la source */
	u32 method = e->method;
	u32 crc;
	long r;
	if (a->fmt == AR_FMT_ZIP) {
		u8 lh[32];
		r = src_read(a, off, lh, 30);
		if (r)
			return r;
		if (rd32(lh) != ZIP_LOCAL)
			return AR_E_CORRUPT;
		off += 30 + rd16(lh + 26) + rd16(lh + 28);
	} else if (a->fmt == AR_FMT_GZIP) {
		off = 0;
		pack = a->len;
		goto defl;
	} else {
		pack = e->size;
		method = 0;
	}
	if (off > a->len || pack > a->len - off)
		return AR_E_CORRUPT;
	if (method == 8)
		goto defl;
	if (method)
		return AR_E_METHOD;
	/* stocké */
	if (e->size != pack)
		return AR_E_CORRUPT;
	if (!k) {
		if (pack > cap)
			return AR_E_SPACE;
		r = src_read(a, off, out, pack);
		if (r)
			return r;
		crc = crc32_upd(0, out, pack);
	} else {
		/* vers un puits, par morceaux */
		struct infl *s = rd_infl(a);
		if (!s)
			return AR_E_NOMEM;
		u8 *b = s->ibuf;
		crc = 0;
		for (u64 left = pack; left;) {
			u64 c = left < IF_ICHUNK ? left : IF_ICHUNK;
			r = src_read(a, off, b, c);
			if (r)
				return r;
			crc = crc32_upd(crc, b, c);
			r = sink_write(k, b, c);
			if (r == AR_STOP)
				return (long)pack;
			if (r)
				return r;
			off += c;
			left -= c;
		}
	}
	if (a->fmt == AR_FMT_ZIP && crc != e->crc)
		return AR_E_CRC;
	return (long)pack;
defl:
	if (a->fmt == AR_FMT_ZIP && !k && e->size > cap)
		return AR_E_SPACE;
	{
		struct infl *s = rd_infl(a);
		if (!s)
			return AR_E_NOMEM;
		rd_ifbegin(a, s, off, pack, out, cap, k, a->fmt == AR_FMT_ZIP ? AR_RAW : AR_GZIP);
		r = if_run(s);
		if (r < 0)
			return r;
		if (a->fmt != AR_FMT_ZIP)
			return r;
		if ((u64)r != e->size)
			return AR_E_CORRUPT;
		if ((u32)s->crc != e->crc)
			return AR_E_CRC;
		return r;
	}
}

long ar_read(long h, long i, long buf, long cap)
{
	if (!buf)
		return AR_E_INVAL;
	return rd_entry(h, i, (u8 *)buf, (u64)cap, 0);
}

long ar_test(long h, long i)
{
	struct sink k = {0, 0, 0, 0};
	long r = rd_entry(h, i, 0, 0, &k);
	return r < 0 ? r : 0;
}

long ar_load(long h, long i)
{
	struct arbuf *b = buf_new();
	if (!b)
		return AR_E_NOMEM;
	/* (une réservation d'après la taille annoncée, si elle est plausible) */
	long sz = ar_entry_size(h, i);
	if (sz >= 0 && (u64)sz + 1 <= (u64)1 << 28) {
		b->sink.cap = (u64)sz + 1;
		b->sink.ptr = (u64)ar_alloc((u64)sz + 1);
		if (!b->sink.ptr)
			b->sink.cap = 0;
	}
	long r = rd_entry(h, i, 0, 0, &b->sink);
	if (r < 0) {
		ar_free((long)b);
		return r;
	}
	return buf_done(b);
}

/* chemin : crée les dossiers parents (l'existant ne gêne pas) ; le dernier
 * composant n'est créé que si last = 1 */
static void ar_mkdirs(const char *path, int last)
{
	char c[1024]; /* (copie : le chemin de l'appelant peut être constant) */
	u64 i = 0;
	for (;;) {
		c[i] = path[i];
		if (!c[i])
			break;
		if (++i >= 1023)
			return;
	}
	for (char *p = c; *p; p++) {
		if (*p == '/' && p != c && p[-1] != ':') {
			*p = 0;
			f_mkdir((long)c);
			*p = '/';
		}
	}
	if (last)
		f_mkdir((long)c);
}

/* message rempli : au service vfs -> résultat */
long ar_vfs(void *msg)
{
	long h = korin_sys2(SYS_SVC_OPEN, s_vfs, 0);
	if (h < 0)
		return E_NOENT;
	return korin_sys2(SYS_SVC_CALL, h, msg);
}

/* chemin, secondes UTC : la date d'un fichier */
static void ar_utime(const char *path, u64 t)
{
	if (!t)
		return;
	u64 m[MSG_SIZE / 8];
	u8 *msg = (u8 *)m;
	m[0] = V_UTIME;
	m[1] = 0;
	m[2] = t;
	m[3] = 0;
	m[4] = 0;
	u8 *d = msg + MSG_DATA;
	u64 i = 0;
	for (;;) {
		if (i >= MSG_SIZE - MSG_DATA - 1)
			return;
		u8 c = (u8)path[i];
		d[i++] = c;
		if (!c)
			break;
	}
	*(u32 *)(msg + MSG_LEN) = (u32)i;
	ar_vfs(msg);
}

long ar_extract(long h, long i, long path)
{
	struct reader *a = rd_handle(h);
	if (!a)
		return AR_E_INVAL;
	struct entry *e = rd_index(a, i);
	if (!e || !path)
		return AR_E_INVAL;
	if (e->flags & 1) {
		ar_mkdirs((const char *)path, 1);
		return 0;
	}
	ar_mkdirs((const char *)path, 0);
	long fd = f_open(path, O_WRITE | O_CREATE | O_TRUNC);
	if (fd < 0)
		return fd;
	struct sink k = {SKK_FD, (u64)fd, 0, 0};
	long r = rd_entry(h, i, 0, 0, &k);
	f_close(fd);
	if (r < 0) {
		f_unlink(path); /* rien de faux sur la carte */
		return r;
	}
	ar_utime((const char *)path, e->time);
	return r;
}

/* nom -> 1 : un composant .. (ou vide) */
static int name_unsafe(const u8 *s)
{
	if (!*s)
		return 1;
	for (;;) {
		if (s[0] == '.' && s[1] == '.' && (!s[2] || s[2] == '/'))
			return 1;
		for (;;) {
			u8 c = *s++;
			if (!c)
				return 0;
			if (c == '/')
				break;
		}
	}
}

long ar_extract_all(long h, long dir)
{
	struct reader *a = rd_handle(h);
	if (!a || !dir)
		return AR_E_INVAL;
	u8 *buf = ar_alloc(4096);
	if (!buf)
		return AR_E_NOMEM;
	long files = 0, r;
	ar_mkdirs((const char *)dir, 1);
	for (u64 i = 0; i < a->n; i++) {
		const u8 *nm = (const u8 *)ar_name(h, (long)i);
		if (name_unsafe(nm))
			continue;
		/* dossier/nom */
		u8 *w = buf;
		const u8 *s = (const u8 *)dir;
		for (;;) {
			u8 c = *s++;
			if (!c)
				break;
			*w++ = c;
			if (w - buf >= 3000)
				goto nx;
		}
		if (w[-1] != '/')
			*w++ = '/';
		for (;;) {
			u8 c = *nm++;
			*w++ = c;
			if (!c)
				break;
			if (w - buf >= 4000)
				goto nx;
		}
		r = ar_extract(h, (long)i, (long)buf);
		if (r < 0) {
			files = r;
			break;
		}
		if (!(a->tbl[i].flags & 1))
			files++;
	nx:;
	}
	ar_mfree(buf);
	return files;
}
