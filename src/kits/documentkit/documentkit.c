/* DocumentKit : la plomberie des formats bureautiques (docs/KITS.md), en C
 * commun au Pi et au PC ; remplace documentkit.asm, même table
 * (documentkit.abi), mêmes résultats.
 *
 * Ce que Letter, Sheet, Present et Cardfile ont en commun, en entiers et sur
 * des tampons : lecteur XML par événements et échappement, UTF-8, CP1252,
 * longueurs Office et ODF, couleurs, dates civiles et séries Excel, CSV,
 * reconnaissance d'un paquet, relations OOXML, enveloppes OOXML et ODF.
 * Le ZIP est à ArchiveKit ; les moteurs de documents restent dans les apps.
 *
 * Sans kit importé ni tas : les lecteurs sont des cases d'une table du
 * process, réservées par échange atomique (une poignée = rang + 1).
 * Sortie texte comme snprintf (documentkit.abi). Les calculs se font en
 * 64 bits qui débordent en boucle, comme l'asm (kit.conf : -fwrapv).
 * Aucun pointeur dans les données initialisées : les textes sont des
 * tableaux de taille fixe, les tables des décalages. */
#include "../common/korin_kit.h"

/* documentkit.inc */
#define DKE_NOTFOUND -2
#define DKE_BADF -9
#define DKE_NOMEM -12
#define DKE_INVAL -22
#define DKE_CORRUPT -74

#define DKX_EOF 0
#define DKX_START 1
#define DKX_END 2
#define DKX_TEXT 3

#define DKU_EMU 0
#define DKU_TWIP 1
#define DKU_HMM 2
#define DKU_PT 3
#define DKU_IN 4
#define DKU_CM 5
#define DKU_MM 6
#define DKU_PC 7
#define DKU_PX 8
#define DKU_COUNT 9

#define DKV_FIELD 1
#define DKV_EOL 2
#define DKV_QUOTED 4

#define DKK_UNKNOWN 0
#define DKK_DOCX 1
#define DKK_XLSX 2
#define DKK_PPTX 3
#define DKK_ODT 4
#define DKK_ODS 5
#define DKK_ODP 6
#define DKK_RTF 7
#define DKK_ZIP 8
#define DKK_OLE 9

#define DKR_ID 0
#define DKR_TYPE 1
#define DKR_TARGET 2
#define DKR_MODE 3

#define DKS_COUNT 7

/* ---- une case de lecteur (XML ou CSV), 128 octets comme dans l'asm */

#define DK_SLOTS 64

#define FL_EMPTY 1 /* ce START vient de <a/> */
#define FL_PEND 2  /* un END suit tout seul */
#define FL_RAW 4   /* texte d'un CDATA : rien à décoder */
#define FL_SEP 8   /* CSV : le dernier champ finissait par le séparateur */

#define KIND_XML 1
#define KIND_CSV 2
#define KIND_RESERVED 3

typedef const uint8_t *cptr;

struct slot {
	uint64_t used; /* 0 libre, 1 XML, 2 CSV, 3 réservée */
	cptr p;        /* position */
	cptr e;        /* fin du tampon */
	cptr base;     /* début du tampon */
	uint64_t ev;   /* événement courant */
	uint64_t fl;   /* FL_* */
	cptr nm;       /* nom de l'élément (avec son préfixe) */
	uint64_t nml;
	cptr at;       /* ses attributs (texte brut) */
	cptr ate;
	cptr ts;       /* texte courant (non décodé) ; CSV : début du champ */
	cptr te;
	long sep;      /* CSV : séparateur */
	uint64_t pad[3];
};

static struct slot dk_slots[DK_SLOTS] __attribute__((aligned(8)));

/* -> poignée, *out = case ; DKE_NOMEM si plein */
static long slot_new(uint64_t kind, struct slot **out)
{
	for (long i = 0; i < DK_SLOTS; i++) {
		struct slot *s = &dk_slots[i];
		uint64_t zero = 0;
		if (__atomic_compare_exchange_n(&s->used, &zero, KIND_RESERVED, 0, __ATOMIC_ACQUIRE,
						__ATOMIC_RELAXED)) {
			memset((uint8_t *)s + 8, 0, sizeof(*s) - 8); /* (tout le reste à zéro) */
			s->used = kind;
			*out = s;
			return i + 1;
		}
	}
	return DKE_NOMEM;
}

/* poignée -> case de ce genre, 0 : poignée inconnue */
static struct slot *slot_get(long h, uint64_t kind)
{
	unsigned long i = (unsigned long)h - 1;
	if (i >= DK_SLOTS)
		return 0;
	struct slot *s = &dk_slots[i];
	if (__atomic_load_n(&s->used, __ATOMIC_RELAXED) != kind)
		return 0;
	return s;
}

/* ---- sortie (out, cap) : x10 écriture (0 : rien), x11 place restante, x12
 * longueur complète dans l'asm */

struct sink {
	uint8_t *w;
	unsigned long room;
	long len;
};

static void sink_init(struct sink *k, void *out, long cap)
{
	k->len = 0;
	if (cap <= 0)
		out = 0;
	k->w = out;
	k->room = out ? (unsigned long)cap - 1 : 0;
}

static void sink_end(struct sink *k)
{
	if (k->w)
		*k->w = 0;
}

static inline void emit(struct sink *k, uint32_t c)
{
	k->len++;
	if (k->room) {
		*k->w++ = (uint8_t)c;
		k->room--;
	}
}

/* point de code -> UTF-8 (invalide : U+FFFD) */
static void sk_cp(struct sink *k, uint32_t c)
{
	if (c < 0x80) {
		emit(k, c);
		return;
	}
	if (c > 0x10FFFF || c - 0xD800 < 0x800)
		c = 0xFFFD;
	if (c < 0x800) {
		emit(k, (c >> 6) | 0xC0);
	} else {
		if (c < 0x10000) {
			emit(k, (c >> 12) | 0xE0);
		} else {
			emit(k, (c >> 18) | 0xF0);
			emit(k, ((c >> 12) & 0x3F) | 0x80);
		}
		emit(k, ((c >> 6) & 0x3F) | 0x80);
	}
	emit(k, (c & 0x3F) | 0x80);
}

/* texte fini par 0 */
static void sk_str(struct sink *k, const char *s)
{
	for (const uint8_t *p = (const uint8_t *)s; *p; p++)
		emit(k, *p);
}

static void sk_mem(struct sink *k, cptr s, long n)
{
	for (; n > 0; n--)
		emit(k, *s++);
}

/* nombre sans signe en décimal, pad chiffres au moins */
static void sk_num_pad(struct sink *k, uint64_t v, long pad)
{
	uint8_t b[32];
	int n = 0;
	do {
		b[n++] = (uint8_t)('0' + v % 10);
		v /= 10;
		pad--;
	} while (v || pad > 0);
	while (n)
		emit(k, b[--n]);
}

static void sk_num(struct sink *k, uint64_t v)
{
	sk_num_pad(k, v, 1);
}

static const char s_amp[] = "&amp;";
static const char s_lt[] = "&lt;";
static const char s_gt[] = "&gt;";
static const char s_quot[] = "&quot;";

/* texte, octets (< 0 : jusqu'au 0), attr = 1 : valeur d'attribut */
static void sk_esc(struct sink *k, const char *t, long n, int attr)
{
	cptr p = (cptr)t;
	if (n < 0)
		n = (long)strlen(t);
	cptr e = p + n;
	while (p < e) {
		uint32_t c = *p++;
		if (c == '&') {
			sk_str(k, s_amp);
		} else if (c == '<') {
			sk_str(k, s_lt);
		} else if (c == '>') {
			sk_str(k, s_gt);
		} else if (c == '"') {
			sk_str(k, s_quot);
		} else if (c >= 32) {
			emit(k, c);
		} else if (c == 9 || c == 10 || c == 13) {
			if (!attr) {
				emit(k, c);
				continue;
			}
			emit(k, '&');
			emit(k, '#');
			if (c >= 10) {
				emit(k, '1');
				c -= 10;
			}
			emit(k, c + '0');
			emit(k, ';');
		} /* autres caractères de contrôle : retirés */
	}
}

/* valeur d'attribut finie par 0 (0 : rien) */
static void sk_attr(struct sink *k, const char *s)
{
	if (s)
		sk_esc(k, s, -1, 1);
}

/* ---- XML : décodage */

/* p sur '&', q sur son ';' (nom de 1 à 10 octets) -> après l'entité */
static cptr xml_entity(struct sink *k, cptr p, cptr q)
{
	unsigned long n = (unsigned long)(q - p) - 1; /* longueur du nom */
	if (n == 0)
		goto other;
	uint32_t c1 = p[1], c2 = p[2]; /* (si n = 1 : le ';') */
	uint32_t v;
	if (n == 2) {
		if (c2 == 't') {
			if (c1 == 'l') {
				v = '<';
				goto one;
			}
			if (c1 == 'g') {
				v = '>';
				goto one;
			}
		}
		goto num;
	}
	if (n == 3) {
		if (c1 == 'a' && c2 == 'm' && p[3] == 'p') {
			v = '&';
			goto one;
		}
		goto num;
	}
	if (n == 4) {
		uint32_t c3 = p[3], c4 = p[4];
		if (c1 == 'q') {
			if (c2 == 'u' && c3 == 'o' && c4 == 't') {
				v = '"';
				goto one;
			}
			goto num;
		}
		if (c1 == 'a' && c2 == 'p' && c3 == 'o' && c4 == 's') {
			v = 39;
			goto one;
		}
	}
num:
	if (n <= 1 || c1 != '#')
		goto other;
	{
		cptr x = p + 2; /* après &# */
		v = 0;
		if (c2 == 'x' || c2 == 'X') {
			x++;
			for (;;) {
				uint32_t b = *x++;
				uint32_t d = b - '0';
				if (d > 9) {
					b |= 32;
					d = b - 'a';
					if (d > 5)
						break;
					d += 10;
				}
				v = d + (v << 4);
				if (v > 0x110000)
					v = 0x110000;
			}
		} else {
			for (;;) {
				uint32_t d = (uint32_t)*x++ - '0';
				if (d > 9)
					break;
				v = v * 10 + d;
				if (v > 0x110000)
					v = 0x110000;
			}
		}
		if (v) /* (&#0; : rien) */
			sk_cp(k, v);
		return q + 1;
	}
one:
	emit(k, v);
	return q + 1;
other: /* & ... ; tels quels */
	while (p <= q)
		emit(k, *p++);
	return p;
}

/* texte, fin -> sortie (entités, références numériques). Une entité inconnue
 * ou sans ';' reste telle quelle. */
static void xml_decode(struct sink *k, cptr p, cptr end)
{
	while (p < end) {
		uint32_t c = *p;
		if (c == '&') {
			cptr q = p + 1;
			for (;;) {
				if (q >= end)
					goto lit;
				if (*q == ';')
					break;
				if ((unsigned long)(q - p) >= 12)
					goto lit;
				q++;
			}
			p = xml_entity(k, p, q);
			continue;
		}
lit:
		emit(k, c);
		p++;
	}
}

/* ---- XML : lecteur */

static const char s_cdata[] = "![CDATA[";

static int is_ws(uint32_t c)
{
	return c == ' ' || c == 9 || c == 10 || c == 13;
}

/* case -> événement suivant */
static long xml_step(struct slot *s)
{
	cptr p = s->p, e = s->e;
	uint64_t fl = s->fl;
	if (fl & FL_PEND) {
		s->fl = fl ^ FL_PEND;
		s->at = 0;
		s->ate = 0;
		s->ev = DKX_END;
		return DKX_END;
	}
	for (;;) {
		if (p >= e) {
			s->p = e;
			s->ev = DKX_EOF;
			return DKX_EOF;
		}
		if (*p != '<') { /* texte */
			cptr t = p;
			do
				p++;
			while (p < e && *p != '<');
			s->ts = t;
			s->te = p;
			fl &= FL_EMPTY;
			goto text;
		}
		unsigned long n = (unsigned long)(e - p);
		if (n < 2)
			goto open;
		uint32_t c = p[1];
		if (c == '/')
			goto close;
		if (c == '?')
			goto decl;
		if (c != '!')
			goto open;
		if (n < 4)
			goto decl;
		if (p[2] == '-') {
			if (p[3] != '-')
				goto decl;
			p += 4; /* <!-- ... --> */
			for (;;) {
				if ((unsigned long)(e - p) < 3) {
					p = e;
					break;
				}
				if (p[0] == '-' && p[1] == '-' && p[2] == '>') {
					p += 3;
					break;
				}
				p++;
			}
			continue;
		}
		if (n < 9) /* <![CDATA[ ... ]]> */
			goto decl;
		{
			cptr a = p + 1;
			for (int i = 0; i < 8; i++)
				if (a[i] != (uint8_t)s_cdata[i])
					goto decl;
			a += 8;
			cptr z = a;
			for (;;) {
				if ((unsigned long)(e - z) < 3) {
					p = e;
					break;
				}
				if (z[0] == ']' && z[1] == ']' && z[2] == '>') {
					p = z + 3;
					break;
				}
				z++;
			}
			s->ts = a;
			s->te = z;
			fl = (fl & FL_EMPTY) | FL_RAW;
			goto text;
		}
decl: /* <? ... > et <! ... > */
		while (p < e)
			if (*p++ == '>')
				break;
		continue;
	}
text:
	s->p = p;
	s->fl = fl;
	s->ev = DKX_TEXT;
	return DKX_TEXT;
close: /* </nom ... > */
	{
		cptr a = p + 2, z = a;
		while (z < e && *z != '>' && !is_ws(*z))
			z++;
		s->nm = a;
		s->nml = (uint64_t)(z - a);
		while (z < e)
			if (*z++ == '>')
				break;
		s->p = z;
		s->at = 0;
		s->ate = 0;
		s->ev = DKX_END;
		return DKX_END;
	}
open: /* <nom attributs> */
	{
		cptr a = p + 1, z = a;
		while (z < e && *z != '>' && *z != '/' && !is_ws(*z))
			z++;
		s->nm = a;
		s->nml = (uint64_t)(z - a);
		a = z; /* début des attributs */
		uint32_t q = 0; /* guillemet ouvert */
		for (; z < e; z++) {
			uint32_t c = *z;
			if (q) {
				if (c == q)
					q = 0;
			} else if (c == '>') {
				break;
			} else if (c == '"' || c == 39) {
				q = c;
			}
		}
		fl = 0;
		cptr ae = z; /* fin des attributs */
		if (z > a && z[-1] == '/') {
			ae = z - 1;
			fl = FL_EMPTY | FL_PEND;
		}
		s->at = a;
		s->ate = ae;
		s->fl = fl;
		if (z < e)
			z++;
		s->p = z;
		s->ev = DKX_START;
		return DKX_START;
	}
}

long dk_xml_open(const void *buf, long n)
{
	if (n < 0)
		return DKE_INVAL;
	struct slot *s;
	long h = slot_new(KIND_XML, &s);
	if (h < 0)
		return h;
	s->p = buf;
	s->base = buf;
	s->e = (cptr)buf + n;
	return h;
}

long dk_xml_close(long h)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	__atomic_store_n(&s->used, 0, __ATOMIC_RELEASE);
	return 0;
}

long dk_xml_next(long h)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	return xml_step(s);
}

long dk_xml_event(long h)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	return (long)s->ev;
}

long dk_xml_empty(long h)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	return (long)(s->fl & FL_EMPTY);
}

/* nom fini par 0, texte, octets -> 1 si égaux */
static long nm_eq(const char *name, cptr t, uint64_t n)
{
	cptr a = (cptr)name;
	for (; n; n--) {
		uint32_t c = *a++;
		if (!c || c != *t++)
			return 0;
	}
	return *a == 0;
}

/* 1 si le nom sans préfixe (après le premier ':') est celui-là */
static long xml_is(struct slot *s, const char *name)
{
	cptr t = s->nm;
	uint64_t n = s->nml;
	for (uint64_t i = 0; i < n;) {
		uint32_t c = t[i++];
		if (c == ':') {
			t += i;
			n -= i;
			break;
		}
	}
	return nm_eq(name, t, n);
}

long dk_xml_is(long h, const char *name)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	return xml_is(s, name);
}

long dk_xml_isq(long h, const char *name)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	return nm_eq(name, s->nm, s->nml);
}

long dk_xml_name(long h, char *out, long cap)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	struct sink k;
	sink_init(&k, out, cap);
	sk_mem(&k, s->nm, (long)s->nml);
	sink_end(&k);
	return k.len;
}

/* nom -> valeur (et *ve sa fin), 0 : absent. Le nom exact d'abord, où qu'il
 * soit ; sinon le premier à préfixe (id : pas r:id si id existe aussi) */
static cptr attr_find(struct slot *s, const char *name, cptr *ve)
{
	if (s->ev != DKX_START)
		return 0;
	cptr a = s->at, e = s->ate;
	if (!a)
		return 0;
	cptr nmb = (cptr)name;
	unsigned long nl = 0; /* longueur du nom cherché */
	int bare = 1;         /* sans préfixe ? */
	while (nmb[nl]) {
		if (nmb[nl] == ':')
			bare = 0;
		nl++;
	}
	cptr cv = 0, cve = 0; /* le premier à préfixe trouvé */
	for (;;) {
		if (a >= e)
			break;
		if (is_ws(*a)) {
			a++;
			continue;
		}
		cptr an = a; /* nom de l'attribut */
		while (a < e && *a != '=' && !is_ws(*a))
			a++;
		unsigned long al = (unsigned long)(a - an);
		for (;;) {
			if (a >= e)
				goto done;
			if (*a++ == '=')
				break;
		}
		uint32_t q;
		for (;;) {
			if (a >= e)
				goto done;
			q = *a++;
			if (q == '"' || q == 39)
				break;
		}
		cptr v = a; /* valeur */
		while (a < e && *a != q)
			a++;
		cptr vz = a;
		if (a < e)
			a++;
		if (al == nl) { /* nom exact ? */
			unsigned long i = 0;
			while (i < nl && an[i] == nmb[i])
				i++;
			if (i >= nl) {
				*ve = vz;
				return v;
			}
		}
		if (!bare)
			continue;
		unsigned long i = 0; /* après le premier ':' de l'attribut */
		int colon = 0;
		while (i < al) {
			uint32_t c = an[i++];
			if (c == ':') {
				colon = 1;
				break;
			}
		}
		if (!colon || al - i != nl)
			continue;
		if (i == 6 && an[0] == 'x' && an[1] == 'm' && an[2] == 'l' && an[3] == 'n' && an[4] == 's')
			continue; /* xmlns:... : jamais */
		cptr r = an + i;
		unsigned long j = 0;
		while (j < nl && r[j] == nmb[j])
			j++;
		if (j < nl)
			continue;
		if (!cv) {
			cv = v;
			cve = vz;
		}
	}
done:
	*ve = cve;
	return cv;
}

/* -> longueur décodée, DKE_NOTFOUND */
static long xml_attr_s(struct slot *s, const char *name, void *out, long cap)
{
	cptr ve;
	cptr v = attr_find(s, name, &ve);
	if (!v)
		return DKE_NOTFOUND;
	struct sink k;
	sink_init(&k, out, cap);
	xml_decode(&k, v, ve);
	sink_end(&k);
	return k.len;
}

long dk_xml_attr(long h, const char *name, char *out, long cap)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	return xml_attr_s(s, name, out, cap);
}

long dk_xml_attr_int(long h, const char *name, long def)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	uint8_t b[40];
	if (xml_attr_s(s, name, b, 40) < 0)
		return def;
	cptr p = b;
	while (is_ws(*p))
		p++;
	int neg = 0;
	if (*p == '-') {
		neg = 1;
		p++;
	} else if (*p == '+') {
		p++;
	}
	uint64_t v = 0;
	for (;;) {
		uint32_t d = (uint32_t)*p++ - '0';
		if (d > 9)
			break;
		v = v * 10 + d;
	}
	return neg ? -(long)v : (long)v;
}

long dk_xml_attr_bool(long h, const char *name, long def)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	uint8_t b[8];
	long r = xml_attr_s(s, name, b, 8);
	if (r < 0)
		return def;
	if (r == 0)
		return 0;
	return b[0] == '1' || b[0] == 't' || b[0] == 'T';
}

static const char s_val[] = "val";

long dk_xml_attr_on(long h, const char *name)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	if (!name)
		name = s_val;
	uint8_t b[16];
	if (xml_attr_s(s, name, b, 16) < 0)
		return 1;
	uint32_t c = b[0];
	if (c == '0' || c == 'f' || c == 'F')
		return 0;
	if (c == 'o' && b[1] == 'f')
		return 0;
	if (c == 'n' && b[1] == 'o')
		return 0;
	return 1;
}

long dk_xml_text(long h, char *out, long cap)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	struct sink k;
	sink_init(&k, out, cap);
	if (s->ev == DKX_TEXT) {
		if (s->fl & FL_RAW)
			sk_mem(&k, s->ts, (long)(s->te - s->ts));
		else
			xml_decode(&k, s->ts, s->te);
	}
	sink_end(&k);
	return k.len;
}

long dk_xml_skip(long h)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	if (s->ev != DKX_START)
		return 0;
	long depth = 1;
	for (;;) {
		long ev = xml_step(s);
		if (ev == DKX_EOF)
			break;
		if (ev == DKX_START)
			depth++;
		else if (ev == DKX_END && --depth == 0)
			break;
	}
	return 0;
}

long dk_xml_pos(long h)
{
	struct slot *s = slot_get(h, KIND_XML);
	if (!s)
		return DKE_BADF;
	return s->p - s->base;
}

long dk_xml_esc(const char *t, long n, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	sk_esc(&k, t, n, 0);
	sink_end(&k);
	return k.len;
}

long dk_xml_esc_attr(const char *t, long n, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	sk_esc(&k, t, n, 1);
	sink_end(&k);
	return k.len;
}

/* ---- UTF-8, CP1252, Latin-1 */

/* p < e -> *cp = point de code, octets ; 0 : invalide */
static long u8_next(cptr p, cptr e, uint32_t *cp)
{
	uint32_t b = p[0];
	if (b < 0x80) {
		*cp = b;
		return 1;
	}
	if (b < 0xC2)
		return 0;
	unsigned long more = 1;
	uint32_t c = b & 0x1F;
	if (b >= 0xE0) {
		more = 2;
		c &= 0x0F;
		if (b >= 0xF0) {
			more = 3;
			c &= 0x07;
			if (b >= 0xF5)
				return 0;
		}
	}
	if ((unsigned long)(e - p) <= more)
		return 0;
	unsigned long i;
	for (i = 1; i <= more; i++) {
		if ((p[i] & 0xC0) != 0x80)
			return 0;
		c = (p[i] & 0x3F) | (c << 6);
	}
	if (i == 3) {
		if (c < 0x800 || c - 0xD800 < 0x800)
			return 0;
	} else if (i == 4) {
		if (c < 0x10000 || c > 0x10FFFF)
			return 0;
	}
	*cp = c;
	return (long)i;
}

long dk_utf8_encode(long cp, char *out)
{
	struct sink k = {(uint8_t *)out, 5, 0}; /* (4 octets au plus, sans 0 final) */
	sk_cp(&k, (uint32_t)cp);
	return k.len;
}

long dk_utf8_decode(const char *t, long n, uint32_t *cp)
{
	if (n <= 0)
		return 0;
	uint32_t c;
	long r = u8_next((cptr)t, (cptr)t + n, &c);
	if (!r)
		return DKE_CORRUPT;
	if (cp)
		*cp = c;
	return r;
}

long dk_utf8_valid(const char *t, long n)
{
	if (n <= 0)
		return 1;
	cptr p = (cptr)t, e = p + n;
	while (p < e) {
		uint32_t c;
		long r = u8_next(p, e, &c);
		if (!r)
			return 0;
		p += r;
	}
	return 1;
}

long dk_utf8_count(const char *t, long n)
{
	long cnt = 0;
	if (n <= 0)
		return 0;
	cptr p = (cptr)t, e = p + n;
	while (p < e) {
		uint32_t c;
		long r = u8_next(p, e, &c);
		p += r ? r : 1;
		cnt++;
	}
	return cnt;
}

long dk_utf8_to_cp(const char *t, long n, uint32_t *out, long cap)
{
	long cnt = 0;
	if (n <= 0)
		return 0;
	cptr p = (cptr)t, e = p + n;
	while (p < e && cnt < cap) {
		uint32_t c;
		long r = u8_next(p, e, &c);
		if (!r) { /* un octet invalide : lu comme Latin-1 */
			c = *p;
			r = 1;
		}
		p += r;
		out[cnt++] = c;
	}
	return cnt;
}

long dk_cp_to_utf8(const uint32_t *cps, long n, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	for (; n > 0; n--)
		sk_cp(&k, *cps++);
	sink_end(&k);
	return k.len;
}

static const uint16_t cp1252_tab[32] = {
	0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
	0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
	0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
	0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static long to_utf8(const char *t, long n, char *out, long cap, int cp1252)
{
	struct sink k;
	sink_init(&k, out, cap);
	cptr p = (cptr)t;
	for (; n > 0; n--) {
		uint32_t c = *p++;
		if (cp1252 && c - 0x80 < 0x20)
			c = cp1252_tab[c - 0x80];
		sk_cp(&k, c);
	}
	sink_end(&k);
	return k.len;
}

long dk_latin1_to_utf8(const char *t, long n, char *out, long cap)
{
	return to_utf8(t, n, out, cap, 0);
}

long dk_cp1252_to_utf8(const char *t, long n, char *out, long cap)
{
	return to_utf8(t, n, out, cap, 1);
}

long dk_cp1252_cp(long b)
{
	unsigned long c = (unsigned long)b & 0xFF;
	if (c - 0x80 < 0x20)
		return cp1252_tab[c - 0x80];
	return (long)c;
}

/* point de code -> octet, DKE_NOTFOUND */
static long cp_to_1252(unsigned long c)
{
	if (c < 0x80 || c - 0xA0 < 0x60)
		return (long)c;
	for (unsigned long i = 0; i < 0x20; i++)
		if (cp1252_tab[i] == c)
			return (long)(i + 0x80);
	return DKE_NOTFOUND;
}

long dk_cp_cp1252(long cp)
{
	return cp_to_1252((unsigned long)cp);
}

static long from_utf8(const char *t, long n, char *out, long cap, long repl, int cp1252)
{
	struct sink k;
	sink_init(&k, out, cap);
	if (n > 0) {
		cptr p = (cptr)t, e = p + n;
		while (p < e) {
			uint32_t c;
			long r = u8_next(p, e, &c);
			if (!r) { /* (octet invalide : tel quel) */
				c = *p;
				r = 1;
			}
			p += r;
			long b;
			if (cp1252)
				b = cp_to_1252(c);
			else
				b = c < 0x100 ? (long)c : DKE_NOTFOUND;
			if (b < 0) {
				b = repl;
				if (!b)
					continue;
			}
			emit(&k, (uint32_t)b);
		}
	}
	sink_end(&k);
	return k.len;
}

long dk_utf8_to_latin1(const char *t, long n, char *out, long cap, long repl)
{
	return from_utf8(t, n, out, cap, repl, 0);
}

long dk_utf8_to_cp1252(const char *t, long n, char *out, long cap, long repl)
{
	return from_utf8(t, n, out, cap, repl, 1);
}

/* ---- longueurs, couleurs */

static const uint32_t unit_tab[DKU_COUNT] = {1, 635, 360, 12700, 914400, 360000, 36000, 152400, 9525};
static const char unit_suffix[DKU_COUNT][4] = {"", "", "", "pt", "in", "cm", "mm", "pc", "px"};

/* DKU_* -> EMU de l'unité (inconnue : 1) */
static uint64_t unit_emu(unsigned long u)
{
	return u < DKU_COUNT ? unit_tab[u] : 1;
}

/* EMU (positif) -> valeur arrondie dans l'unité u */
static uint64_t emu_to(uint64_t v, unsigned long u)
{
	uint64_t m = unit_emu(u);
	return (v + (m >> 1)) / m;
}

long dk_len_conv(long v, long unit, long to)
{
	int neg = v < 0;
	uint64_t a = neg ? -(uint64_t)v : (uint64_t)v;
	uint64_t r = emu_to(a * unit_emu((unsigned long)unit), (unsigned long)to);
	return neg ? -(long)r : (long)r;
}

#define SUF(a, b) ((uint32_t)(a) | (uint32_t)(b) << 8)

long dk_len_parse(const char *t, long unit, long bare, long def)
{
	cptr p = (cptr)t;
	if (!p)
		return def;
	while (*p == ' ')
		p++;
	int neg = 0;
	if (*p == '-') {
		neg = 1;
		p++;
	} else if (*p == '+') {
		p++;
	}
	uint64_t ip = 0;   /* partie entière */
	uint64_t fr = 0;   /* 1e-6 */
	uint64_t seen = 0; /* chiffres vus */
	uint32_t c;
	for (;;) {
		c = *p;
		uint32_t d = c - '0';
		if (d > 9)
			break;
		p++;
		seen++;
		if (ip >> 31)
			continue; /* (au-delà : sature) */
		ip = ip * 10 + d;
	}
	if (c == '.' || c == ',') {
		p++;
		unsigned long kept = 0; /* décimales gardées */
		for (;;) {
			c = *p;
			uint32_t d = c - '0';
			if (d > 9)
				break;
			p++;
			seen++;
			if (kept >= 6)
				continue;
			fr = fr * 10 + d;
			kept++;
		}
		for (; kept < 6; kept++)
			fr *= 10;
	}
	if (!seen)
		return def;
	c = p[0];
	if (c == '%')
		return def;
	uint32_t c2 = c ? p[1] : 0;
	uint32_t suf = c | c2 << 8;
	unsigned long u;
	if (suf == SUF('c', 'm'))
		u = DKU_CM;
	else if (suf == SUF('m', 'm'))
		u = DKU_MM;
	else if (suf == SUF('i', 'n'))
		u = DKU_IN;
	else if (suf == SUF('p', 't'))
		u = DKU_PT;
	else if (suf == SUF('p', 'c'))
		u = DKU_PC;
	else if (suf == SUF('p', 'x'))
		u = DKU_PX;
	else
		u = (unsigned long)bare;
	uint64_t m = unit_emu(u);
	uint64_t v = ip * m + (fr * m + 500000) / 1000000; /* EMU */
	v = emu_to(v, (unsigned long)unit);
	return neg ? -(long)v : (long)v;
}

long dk_len_write(long v, long unit, long wunit, long dec, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	unsigned long nd = (unsigned long)dec;
	if (nd > 6)
		nd = 6;
	uint64_t a = (uint64_t)v;
	if (v < 0) {
		a = -a;
		emit(&k, '-');
	}
	uint64_t e = a * unit_emu((unsigned long)unit); /* EMU */
	uint64_t m = unit_emu((unsigned long)wunit);   /* EMU par unité écrite */
	uint64_t q = e / m;
	uint64_t r = e - q * m; /* reste */
	uint64_t p10 = 1;       /* 10^décimales */
	for (unsigned long i = nd; i; i--)
		p10 *= 10;
	r = (r * p10 + (m >> 1)) / m; /* décimales arrondies */
	if (r >= p10) {
		q++;
		r = 0;
	}
	sk_num(&k, q);
	if (nd) {
		emit(&k, '.');
		sk_num_pad(&k, r, (long)nd);
	}
	if ((unsigned long)wunit < DKU_COUNT)
		sk_str(&k, unit_suffix[wunit]);
	sink_end(&k);
	return k.len;
}

long dk_color_parse(const char *t, long def)
{
	cptr p = (cptr)t;
	if (!p)
		return def;
	if (*p == '#')
		p++;
	unsigned long n = 0; /* chiffres */
	uint64_t six = 0;    /* les 6 premiers */
	uint64_t all = 0;    /* les 8 (AARRGGBB) */
	for (; n < 8; n++) {
		uint32_t c = p[n];
		uint32_t d = c - '0';
		if (d > 9) {
			c |= 32;
			d = c - 'a';
			if (d > 5)
				break;
			d += 10;
		}
		if (n < 6)
			six = d + (six << 4);
		all = d + (all << 4);
	}
	if (n < 6)
		return def;
	if (n == 8)
		return (long)(all & 0xFFFFFF);
	return (long)six;
}

static const char s_hex_up[] = "0123456789ABCDEF";
static const char s_hex_low[] = "0123456789abcdef";

long dk_color_write(long rgb, long fl, char *out)
{
	const char *hx = (fl & 2) ? s_hex_low : s_hex_up;
	char *w = out;
	if (fl & 1)
		*w++ = '#';
	if (fl & 4) {
		char f = (fl & 2) ? 'f' : 'F';
		*w++ = f;
		*w++ = f;
	}
	for (int sh = 20; sh >= 0; sh -= 4)
		*w++ = hx[((unsigned long)rgb >> sh) & 15];
	*w = 0;
	return w - out;
}

/* ---- dates */

/* année, mois (hors bornes : reporté), jour -> jours ; *mo = mois ramené,
 * *yo = année ramenée */
static long civil2days(long y, long m, long d, long *mo, long *yo)
{
	long m0 = m - 1;
	long q = m0 / 12, r = m0 % 12;
	if (r < 0) {
		q--;
		r += 12;
	}
	y += q;
	m = r + 1;
	*yo = y;
	*mo = m;
	if (m <= 2)
		y--;
	long era = y / 400, yoe = y % 400;
	if (yoe < 0) {
		era--;
		yoe += 400;
	}
	uint64_t mp = m > 2 ? (uint64_t)m - 3 : (uint64_t)m + 9;
	long doy = (long)((mp * 153 + 2) / 5) + d - 1;                              /* jour de l'année */
	long doe = yoe * 365 + (long)((uint64_t)yoe >> 2) - (long)((uint64_t)yoe / 100) + doy; /* jour de l'ère */
	return doe + era * 146097 - 719468;
}

/* jours -> année, mois, jour */
static void days2civil(long z, long *py, long *pm, long *pd)
{
	z += 719468;
	long era = z / 146097, doe = z % 146097;
	if (doe < 0) {
		era--;
		doe += 146097;
	}
	uint64_t de = (uint64_t)doe;
	uint64_t yoe = (de - de / 1460 + de / 36524 - de / 146096) / 365;
	long y = era * 400 + (long)yoe;
	uint64_t doy = de - (yoe * 365 + (yoe >> 2) - yoe / 100);
	uint64_t mp = (doy * 5 + 2) / 153; /* mois depuis mars */
	*pd = (long)(doy - (mp * 153 + 2) / 5 + 1);
	if (mp < 10) {
		*pm = (long)mp + 3;
	} else {
		*pm = (long)mp - 9;
		y++;
	}
	*py = y;
}

static long put_ymd(int32_t *py, int32_t *pm, int32_t *pd, long y, long m, long d)
{
	if (py)
		*py = (int32_t)y;
	if (pm)
		*pm = (int32_t)m;
	if (pd)
		*pd = (int32_t)d;
	return 0;
}

long dk_days_from_civil(long y, long m, long d)
{
	long mo, yo;
	return civil2days(y, m, d, &mo, &yo);
}

long dk_civil_from_days(long days, int32_t *py, int32_t *pm, int32_t *pd)
{
	long y, m, d;
	days2civil(days, &y, &m, &d);
	return put_ymd(py, pm, pd, y, m, d);
}

long dk_weekday(long days)
{
	long r = (days + 4) % 7; /* 1970-01-01 : un jeudi */
	if (r < 0)
		r += 7;
	return r;
}

long dk_excel_from_civil(long y, long m, long d, long s1904)
{
	if ((unsigned long)y < 1900) /* (années 0..1899, comme DATE) */
		y += 1900;
	long mo, yo;
	long x = civil2days(y, m, 1, &mo, &yo);
	x += d - 1;
	x += 25569;
	if (x < 61)
		x--;
	if (yo == 1900 && mo == 2 && d == 29) /* 29 février 1900 : 60 */
		x = 60;
	if (s1904)
		x -= 1462;
	return x;
}

long dk_civil_from_excel(long serial, long s1904, int32_t *py, int32_t *pm, int32_t *pd)
{
	if (s1904)
		serial += 1462;
	if (serial == 60)
		return put_ymd(py, pm, pd, 1900, 2, 29);
	if (serial <= 0)
		return put_ymd(py, pm, pd, 1900, 1, 0);
	if (serial < 60)
		serial++;
	long y, m, d;
	days2civil(serial - 25569, &y, &m, &d);
	return put_ymd(py, pm, pd, y, m, d);
}

/* ---- CSV */

/* *pp = position, e = fin, sep -> *pp après le champ, *flags = DKV_EOL +
 * DKV_QUOTED ; 1 si fini par le séparateur. Un guillemet n'ouvre qu'au début
 * du champ (rien encore sorti). */
static long csv_scan(struct sink *k, cptr *pp, cptr e, uint32_t sep, uint64_t *flags)
{
	cptr p = *pp;
	uint64_t f = 0;
	int inq = 0; /* entre guillemets */
	long r = 0;
	for (;;) {
		if (p >= e) {
			f |= DKV_EOL;
			break;
		}
		uint32_t c = *p;
		if (inq) {
			if (c == '"') {
				if (p + 1 < e && p[1] == '"') {
					p += 2;
					emit(k, '"');
					continue;
				}
				inq = 0;
				p++;
				continue;
			}
			p++;
			emit(k, c);
			continue;
		}
		if (c == '"' && k->len == 0) {
			inq = 1;
			f |= DKV_QUOTED;
			p++;
			continue;
		}
		if (c == sep) {
			p++;
			r = 1;
			break;
		}
		if (c == 10) {
			p++;
			f |= DKV_EOL;
			break;
		}
		if (c == 13 && p + 1 < e && p[1] == 10) {
			p += 2;
			f |= DKV_EOL;
			break;
		}
		p++;
		emit(k, c);
	}
	*pp = p;
	*flags = f;
	return r;
}

static const uint8_t csv_seps[4] = {',', ';', 9, 0};

/* texte, fin -> le séparateur le plus régulier des 20 premières lignes */
static long csv_guess(cptr b, cptr e)
{
	unsigned long best = 0;
	long score = -1;
	for (unsigned long k = 0; k < 3; k++) {
		uint32_t sep = csv_seps[k];
		cptr p = b;
		long lines = 0, first = -1, same = 0, in = 0;
		int quote = 0;
		while (p < e && lines < 20) {
			uint32_t c = *p++;
			if (c == '"') {
				quote ^= 1;
				continue;
			}
			if (quote)
				continue;
			if (c == sep) {
				in++;
				continue;
			}
			if (c != 10)
				continue;
			if (first < 0)
				first = in;
			else if (in == first && in)
				same++;
			lines++;
			in = 0;
		}
		long sc = first > 0 ? same * 100 + first : 0;
		if (sc > score) {
			score = sc;
			best = k;
		}
	}
	return csv_seps[best];
}

long dk_csv_open(const void *buf, long n, long sep)
{
	if (n < 0)
		return DKE_INVAL;
	cptr b = buf;
	if ((unsigned long)n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) { /* BOM */
		b += 3;
		n -= 3;
	}
	cptr e = b + n;
	if (!sep)
		sep = csv_guess(b, e);
	struct slot *s;
	long h = slot_new(KIND_CSV, &s);
	if (h < 0)
		return h;
	s->p = b;
	s->base = b;
	s->e = e;
	s->sep = sep;
	return h;
}

long dk_csv_close(long h)
{
	struct slot *s = slot_get(h, KIND_CSV);
	if (!s)
		return DKE_BADF;
	__atomic_store_n(&s->used, 0, __ATOMIC_RELEASE);
	return 0;
}

long dk_csv_next(long h)
{
	struct slot *s = slot_get(h, KIND_CSV);
	if (!s)
		return DKE_BADF;
	cptr p = s->p;
	if (p >= s->e && !(s->fl & FL_SEP)) /* (a,b, : un dernier champ vide) */
		return 0;
	s->ts = p;
	struct sink k = {0, 0, 0};
	uint64_t f;
	long r = csv_scan(&k, &p, s->e, (uint32_t)s->sep, &f);
	s->p = p;
	s->fl = r ? FL_SEP : 0;
	return (long)(f | DKV_FIELD);
}

long dk_csv_text(long h, char *out, long cap)
{
	struct slot *s = slot_get(h, KIND_CSV);
	if (!s)
		return DKE_BADF;
	struct sink k;
	sink_init(&k, out, cap);
	cptr p = s->ts;
	if (p) {
		uint64_t f;
		csv_scan(&k, &p, s->e, (uint32_t)s->sep, &f);
	}
	sink_end(&k);
	return k.len;
}

long dk_csv_sep(long h)
{
	struct slot *s = slot_get(h, KIND_CSV);
	if (!s)
		return DKE_BADF;
	return s->sep;
}

long dk_csv_esc(const char *t, long n, long sep, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	cptr p = (cptr)t;
	if (n < 0)
		n = (long)strlen(t);
	cptr e = p + n;
	uint32_t sc = (uint32_t)sep;
	int need = 0;
	for (cptr q = p; q < e; q++) {
		uint32_t c = *q;
		if (c == sc || c == '"' || c == 10 || c == 13) {
			need = 1;
			break;
		}
	}
	if (need) {
		emit(&k, '"');
		while (p < e) {
			uint32_t c = *p++;
			if (c == '"')
				emit(&k, c);
			emit(&k, c);
		}
		emit(&k, '"');
	} else {
		sk_mem(&k, p, n);
	}
	sink_end(&k);
	return k.len;
}

/* ---- paquets */

static uint32_t rd16(cptr p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8;
}

static uint32_t rd32(cptr p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int mem_eq(cptr a, const char *b, unsigned long n)
{
	for (unsigned long i = 0; i < n; i++)
		if (a[i] != (uint8_t)b[i])
			return 0;
	return 1;
}

static const char s_mimetype[] = "mimetype";
static const char s_template[] = "-template";
static const char s_ct_name[] = "[Content_Types].xml";

static const char mime_tab[DKK_ODP + 1][80] = {
	"",
	"application/vnd.openxmlformats-officedocument.wordprocessingml.document",
	"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
	"application/vnd.openxmlformats-officedocument.presentationml.presentation",
	"application/vnd.oasis.opendocument.text",
	"application/vnd.oasis.opendocument.spreadsheet",
	"application/vnd.oasis.opendocument.presentation",
};

long dk_doc_kind_mime(const char *t, long n)
{
	cptr s = (cptr)t;
	if (n < 0)
		n = (long)strlen(t);
	for (; n > 0; n--) /* blancs de fin */
		if (s[n - 1] > 32)
			break;
	if (n <= 0)
		return DKK_UNKNOWN;
	for (long k = DKK_DOCX; k <= DKK_ODP; k++) {
		cptr m = (cptr)mime_tab[k];
		unsigned long i;
		for (i = 0; i < (unsigned long)n; i++) {
			if (!m[i]) /* reste du texte : -template ? */
				break;
			if (m[i] != s[i])
				goto next;
		}
		if (i >= (unsigned long)n) {
			if (!m[i])
				return k;
		} else if ((unsigned long)n - i == 9 && mem_eq(s + i, s_template, 9)) {
			return k;
		}
next:;
	}
	return DKK_UNKNOWN;
}

long dk_doc_kind(const void *buf, long n)
{
	cptr b = buf;
	if (n < 4)
		return DKK_UNKNOWN;
	uint32_t w = rd32(b);
	if (w == 0xE011CFD0)
		return DKK_OLE;
	if (w == 0x74725C7B) { /* {\rt */
		if (n < 5 || b[4] != 'f')
			return DKK_UNKNOWN;
		return DKK_RTF;
	}
	if (w == 0x06054B50) /* ZIP vide */
		return DKK_ZIP;
	if (w != 0x04034B50)
		return DKK_UNKNOWN;
	/* mimetype d'abord, non compressé : ODF */
	if (n >= 38 && rd16(b + 26) == 8 && rd16(b + 8) == 0 && mem_eq(b + 30, s_mimetype, 8)) {
		uint64_t off = (uint64_t)rd16(b + 28) + 38; /* données */
		uint64_t len = rd32(b + 18);
		if (off + len <= (uint64_t)n) {
			long r = dk_doc_kind_mime((const char *)b + off, (long)len);
			if (r)
				return r;
		}
	}
	/* EOCD, depuis la fin */
	long i = n - 22;
	long lo = i - 65535;
	if (lo < 0)
		lo = 0;
	for (;; i--) {
		if (i < lo)
			return DKK_ZIP;
		if (rd32(b + i) == 0x06054B50)
			break;
	}
	uint64_t pos = rd32(b + i + 16); /* début du répertoire central */
	int ct = 0;                      /* [Content_Types].xml vu */
	long fam = 0;                    /* famille d'après un nom */
	for (;;) {
		if (pos + 46 > (uint64_t)n)
			break;
		cptr h = b + pos;
		if (rd32(h) != 0x02014B50)
			break;
		uint64_t nl = rd16(h + 28), xl = rd16(h + 30), cl = rd16(h + 32);
		pos += 46;
		if (pos + nl > (uint64_t)n)
			break;
		cptr nm = b + pos;
		if (nl == 19) {
			if (mem_eq(nm, s_ct_name, 19))
				ct = 1;
		} else if (!fam && nl >= 5) {
			uint32_t v = rd32(nm);
			if (v == 0x64726F77) { /* word */
				if (nm[4] == '/')
					fam = DKK_DOCX;
			} else if ((v & 0xFFFFFF) == 0x2F6C78) { /* xl/ */
				fam = DKK_XLSX;
			} else if (v == 0x2F747070) { /* ppt/ */
				fam = DKK_PPTX;
			}
		}
		pos += nl + xl + cl;
	}
	if (!ct || !fam)
		return DKK_ZIP;
	return fam;
}

const char *dk_doc_mime(long kind)
{
	if ((unsigned long)kind - 1 >= DKK_ODP)
		return 0;
	return mime_tab[kind];
}

long dk_part_path(const char *base, const char *target, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	cptr bs = (cptr)base, t = (cptr)target;
	if (t[0] == '/') {
		sk_str(&k, (const char *)t + 1);
	} else {
		unsigned long cut = 0; /* après le dernier '/' */
		for (unsigned long i = 0;;) {
			uint32_t c = bs[i++];
			if (!c)
				break;
			if (c == '/')
				cut = i;
		}
		for (;;) { /* ../ et ./ */
			if (t[0] != '.')
				break;
			if (t[1] == '/') {
				t += 2;
				continue;
			}
			if (t[1] != '.' || t[2] != '/')
				break;
			t += 3;
			if (!cut)
				continue;
			cut--;
			while (cut && bs[cut - 1] != '/')
				cut--;
		}
		sk_mem(&k, bs, (long)cut);
		sk_str(&k, (const char *)t);
	}
	sink_end(&k);
	return k.len;
}

static const char s_rels_dir[] = "_rels/";
static const char s_rels_ext[] = ".rels";

long dk_rels_path(const char *part, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	cptr bs = (cptr)part;
	unsigned long cut = 0;
	for (unsigned long i = 0;;) {
		uint32_t c = bs[i++];
		if (!c)
			break;
		if (c == '/')
			cut = i;
	}
	sk_mem(&k, bs, (long)cut);
	sk_str(&k, s_rels_dir);
	sk_str(&k, part + cut);
	sk_str(&k, s_rels_ext);
	sink_end(&k);
	return k.len;
}

/* un lecteur XML sur la pile pour parcourir un .rels */
static void rel_enter(struct slot *s, const void *buf, long n)
{
	memset(s, 0, sizeof(*s));
	s->p = buf;
	s->base = buf;
	s->e = (cptr)buf + (n > 0 ? n : 0);
}

static const char s_Relationship[] = "Relationship";
static const char rel_attrs[4][12] = {"Id", "Type", "Target", "TargetMode"};

/* -> 1 : sur un <Relationship>, 0 : fin */
static long rel_next(struct slot *s)
{
	for (;;) {
		long ev = xml_step(s);
		if (ev == DKX_EOF)
			return 0;
		if (ev == DKX_START && xml_is(s, s_Relationship))
			return 1;
	}
}

long dk_rel_count(const void *buf, long n)
{
	struct slot s;
	rel_enter(&s, buf, n);
	long c = 0;
	while (rel_next(&s))
		c++;
	return c;
}

long dk_rel_get(const void *buf, long n, long rank, long field, char *out, long cap)
{
	struct slot s;
	rel_enter(&s, buf, n);
	if (rank < 0)
		return DKE_NOTFOUND;
	for (;;) {
		if (!rel_next(&s))
			return DKE_NOTFOUND;
		if (--rank < 0)
			break;
	}
	if ((unsigned long)field > DKR_MODE)
		return DKE_NOTFOUND;
	cptr ve;
	cptr v = attr_find(&s, rel_attrs[field], &ve);
	struct sink k;
	sink_init(&k, out, cap);
	if (v) {
		if (field == DKR_TYPE) { /* dernier composant */
			cptr z = ve;
			while (z > v && z[-1] != '/')
				z--;
			v = z;
		}
		xml_decode(&k, v, ve);
	}
	sink_end(&k);
	return k.len;
}

/* sur un Relationship -> sa cible dans out */
static long rel_put_target(struct slot *s, char *out, long cap)
{
	cptr ve;
	cptr v = attr_find(s, rel_attrs[DKR_TARGET], &ve);
	struct sink k;
	sink_init(&k, out, cap);
	if (v)
		xml_decode(&k, v, ve);
	sink_end(&k);
	return k.len;
}

long dk_rel_target(const void *buf, long n, const char *id, char *out, long cap)
{
	struct slot s;
	rel_enter(&s, buf, n);
	while (rel_next(&s)) {
		cptr ve;
		cptr v = attr_find(&s, rel_attrs[DKR_ID], &ve);
		if (!v)
			continue;
		cptr q = (cptr)id; /* compare la valeur brute à l'Id */
		while (v < ve)
			if (*v++ != *q++)
				goto next;
		if (*q)
			continue;
		return rel_put_target(&s, out, cap);
next:;
	}
	return DKE_NOTFOUND;
}

static uint32_t lower(uint32_t c)
{
	return c - 'A' <= 25 ? c + 32 : c;
}

long dk_rel_by_type(const void *buf, long n, const char *type, char *out, long cap)
{
	struct slot s;
	rel_enter(&s, buf, n);
	while (rel_next(&s)) {
		cptr ve;
		cptr v = attr_find(&s, rel_attrs[DKR_TYPE], &ve);
		if (!v)
			continue;
		cptr z = ve; /* dernier composant */
		while (z > v && z[-1] != '/')
			z--;
		cptr q = (cptr)type;
		while (z < ve)
			if (lower(*z++) != lower(*q++))
				goto next;
		if (*q)
			continue;
		v = attr_find(&s, rel_attrs[DKR_MODE], &ve); /* External : non */
		if (v && ve - v == 8 && lower(*v) == 'e')
			continue;
		return rel_put_target(&s, out, cap);
next:;
	}
	return DKE_NOTFOUND;
}

static const char env_tab[DKS_COUNT][256] = {
	"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n",
	"<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">",
	"</Relationships>",
	"<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
	"<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
	"<Default Extension=\"xml\" ContentType=\"application/xml\"/>",
	"</Types>",
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" "
	"manifest:version=\"1.3\">",
	"</manifest:manifest>",
};

const char *dk_env_text(long which)
{
	if ((unsigned long)which >= DKS_COUNT)
		return 0;
	return env_tab[which];
}

static const char s_rel_ns[] = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
static const char s_rw1[] = "<Relationship Id=\"rId";
static const char s_rw2[] = "\" Type=\"";
static const char s_rw3[] = "\" Target=\"";
static const char s_rw4[] = "\" TargetMode=\"External";
static const char s_rw5[] = "\"/>";
static const char s_od1[] = "<Default Extension=\"";
static const char s_od2[] = "\" ContentType=\"";
static const char s_ov1[] = "<Override PartName=\"";
static const char s_me1[] = "<manifest:file-entry manifest:full-path=\"";
static const char s_me2[] = "\" manifest:version=\"1.3";
static const char s_me3[] = "\" manifest:media-type=\"";

long dk_rel_write(long num, const char *type, const char *target, long external, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	sk_str(&k, s_rw1);
	sk_num(&k, (uint64_t)num);
	sk_str(&k, s_rw2);
	const char *c = type; /* un ':' dans le type : URI complète */
	while (*c && *c != ':')
		c++;
	if (!*c)
		sk_str(&k, s_rel_ns);
	sk_attr(&k, type);
	sk_str(&k, s_rw3);
	sk_attr(&k, target);
	if (external)
		sk_str(&k, s_rw4);
	sk_str(&k, s_rw5);
	sink_end(&k);
	return k.len;
}

static long opc_tail(struct sink *k, const char *ct)
{
	sk_str(k, s_od2);
	sk_attr(k, ct);
	sk_str(k, s_rw5);
	sink_end(k);
	return k->len;
}

long dk_opc_default(const char *ext, const char *ct, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	sk_str(&k, s_od1);
	sk_attr(&k, ext);
	return opc_tail(&k, ct);
}

long dk_opc_override(const char *part, const char *ct, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	sk_str(&k, s_ov1);
	if (part[0] != '/')
		emit(&k, '/');
	sk_attr(&k, part);
	return opc_tail(&k, ct);
}

long dk_odf_entry(const char *path, const char *type, char *out, long cap)
{
	struct sink k;
	sink_init(&k, out, cap);
	sk_str(&k, s_me1);
	sk_attr(&k, path);
	if (path[0] == '/' && path[1] == 0)
		sk_str(&k, s_me2);
	sk_str(&k, s_me3);
	sk_attr(&k, type);
	sk_str(&k, s_rw5);
	sink_end(&k);
	return k.len;
}

/* ---- divers */

long dk_hash(const void *buf, long n)
{
	uint32_t h = 2166136261u;
	cptr p = buf;
	for (long i = n; i > 0; i--) {
		h ^= *p++;
		h *= 16777619u;
	}
	return (long)(h ^ (uint32_t)n);
}
