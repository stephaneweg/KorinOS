/* json.c -- lire et écrire le JSON de korin-mcp (json.h). */
#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- lecture : descente récursive ---- */
struct jp {
	const char *s, *e;
	int depth;
};

static void ws(struct jp *p)
{
	while (p->s < p->e && (*p->s == ' ' || *p->s == '\t' || *p->s == '\n' || *p->s == '\r'))
		p->s++;
}

static struct jv *node(enum jtype t)
{
	struct jv *v = calloc(1, sizeof *v);
	if (v)
		v->t = t;
	return v;
}

static int hex4(const char *s, unsigned *out)
{
	unsigned v = 0;
	for (int i = 0; i < 4; i++) {
		char c = s[i];
		v <<= 4;
		if (c >= '0' && c <= '9')
			v |= (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f')
			v |= (unsigned)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			v |= (unsigned)(c - 'A' + 10);
		else
			return -1;
	}
	*out = v;
	return 0;
}

static size_t put_utf8(char *d, unsigned c)
{
	if (c < 0x80) {
		d[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		d[0] = (char)(0xC0 | (c >> 6));
		d[1] = (char)(0x80 | (c & 63));
		return 2;
	}
	if (c < 0x10000) {
		d[0] = (char)(0xE0 | (c >> 12));
		d[1] = (char)(0x80 | ((c >> 6) & 63));
		d[2] = (char)(0x80 | (c & 63));
		return 3;
	}
	d[0] = (char)(0xF0 | (c >> 18));
	d[1] = (char)(0x80 | ((c >> 12) & 63));
	d[2] = (char)(0x80 | ((c >> 6) & 63));
	d[3] = (char)(0x80 | (c & 63));
	return 4;
}

/* "..." -> chaîne décodée (malloc) */
static char *pstring(struct jp *p, size_t *len)
{
	if (p->s >= p->e || *p->s != '"')
		return 0;
	p->s++;
	const char *a = p->s;
	size_t max = 0;
	while (a < p->e && *a != '"') { /* (taille au plus : celle du texte) */
		if (*a == '\\')
			a++;
		a++;
		max++;
	}
	char *d = malloc(max * 4 + 1), *o = d;
	if (!d)
		return 0;
	while (p->s < p->e && *p->s != '"') {
		char c = *p->s++;
		if ((unsigned char)c < 0x20)
			goto bad;
		if (c != '\\') {
			*o++ = c;
			continue;
		}
		if (p->s >= p->e)
			goto bad;
		c = *p->s++;
		switch (c) {
		case '"': *o++ = '"'; break;
		case '\\': *o++ = '\\'; break;
		case '/': *o++ = '/'; break;
		case 'b': *o++ = '\b'; break;
		case 'f': *o++ = '\f'; break;
		case 'n': *o++ = '\n'; break;
		case 'r': *o++ = '\r'; break;
		case 't': *o++ = '\t'; break;
		case 'u': {
			unsigned u;
			if (p->e - p->s < 4 || hex4(p->s, &u))
				goto bad;
			p->s += 4;
			if (u >= 0xD800 && u < 0xDC00 && p->e - p->s >= 6 && p->s[0] == '\\' && p->s[1] == 'u') {
				unsigned lo;
				if (!hex4(p->s + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) {
					p->s += 6;
					u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				}
			}
			o += put_utf8(o, u);
			break;
		}
		default:
			goto bad;
		}
	}
	if (p->s >= p->e)
		goto bad;
	p->s++;
	*o = 0;
	*len = (size_t)(o - d);
	return d;
bad:
	free(d);
	return 0;
}

static struct jv *pvalue(struct jp *p);

static struct jv *pvalue(struct jp *p)
{
	ws(p);
	if (p->s >= p->e || ++p->depth > 64)
		return 0;
	const char *start = p->s;
	struct jv *v = 0;
	char c = *p->s;
	if (c == '{' || c == '[') {
		int obj = c == '{';
		v = node(obj ? J_OBJ : J_ARR);
		if (!v)
			return 0;
		p->s++;
		struct jv **tail = &v->kid;
		ws(p);
		if (p->s < p->e && *p->s == (obj ? '}' : ']')) {
			p->s++;
			goto done;
		}
		for (;;) {
			char *key = 0;
			size_t kl;
			if (obj) {
				ws(p);
				key = pstring(p, &kl);
				if (!key)
					goto bad;
				ws(p);
				if (p->s >= p->e || *p->s != ':') {
					free(key);
					goto bad;
				}
				p->s++;
			}
			struct jv *k = pvalue(p);
			if (!k) {
				free(key);
				goto bad;
			}
			k->key = key;
			*tail = k;
			tail = &k->next;
			ws(p);
			if (p->s < p->e && *p->s == ',') {
				p->s++;
				continue;
			}
			if (p->s < p->e && *p->s == (obj ? '}' : ']')) {
				p->s++;
				break;
			}
			goto bad;
		}
	} else if (c == '"') {
		v = node(J_STR);
		if (!v || !(v->s = pstring(p, &v->len)))
			goto bad;
	} else if (c == 't' && p->e - p->s >= 4 && !memcmp(p->s, "true", 4)) {
		v = node(J_BOOL);
		if (!v)
			return 0;
		v->b = 1;
		p->s += 4;
	} else if (c == 'f' && p->e - p->s >= 5 && !memcmp(p->s, "false", 5)) {
		v = node(J_BOOL);
		if (!v)
			return 0;
		p->s += 5;
	} else if (c == 'n' && p->e - p->s >= 4 && !memcmp(p->s, "null", 4)) {
		v = node(J_NULL);
		if (!v)
			return 0;
		p->s += 4;
	} else if (c == '-' || (c >= '0' && c <= '9')) {
		char tmp[64];
		size_t n = 0;
		while (p->s < p->e && n < sizeof tmp - 1 && strchr("+-0123456789.eE", *p->s))
			tmp[n++] = *p->s++;
		tmp[n] = 0;
		v = node(J_NUM);
		if (!v)
			return 0;
		char *end;
		v->num = strtod(tmp, &end);
		if (end == tmp)
			goto bad;
	} else
		return 0;
done:
	v->raw = start;
	v->rawlen = (size_t)(p->s - start);
	p->depth--;
	return v;
bad:
	json_free(v);
	return 0;
}

struct jv *json_parse(const char *text, size_t len)
{
	struct jp p = { text, text + len, 0 };
	struct jv *v = pvalue(&p);
	if (!v)
		return 0;
	ws(&p);
	if (p.s != p.e) {
		json_free(v);
		return 0;
	}
	return v;
}

void json_free(struct jv *v)
{
	while (v) {
		struct jv *n = v->next;
		json_free(v->kid);
		free(v->s);
		free(v->key);
		free(v);
		v = n;
	}
}

struct jv *json_get(const struct jv *obj, const char *key)
{
	if (!obj || obj->t != J_OBJ)
		return 0;
	for (struct jv *k = obj->kid; k; k = k->next)
		if (k->key && !strcmp(k->key, key))
			return k;
	return 0;
}

const char *json_str(const struct jv *obj, const char *key, const char *def)
{
	struct jv *v = json_get(obj, key);
	return v && v->t == J_STR ? v->s : def;
}

long long json_int(const struct jv *obj, const char *key, long long def)
{
	struct jv *v = json_get(obj, key);
	if (v && v->t == J_NUM)
		return (long long)v->num;
	if (v && v->t == J_STR && v->s[0]) { /* (un nombre passé en texte : accepté) */
		char *e;
		long long r = strtoll(v->s, &e, 0);
		if (!*e)
			return r;
	}
	return def;
}

int json_bool(const struct jv *obj, const char *key, int def)
{
	struct jv *v = json_get(obj, key);
	if (v && v->t == J_BOOL)
		return v->b;
	if (v && v->t == J_STR)
		return !strcmp(v->s, "true") || !strcmp(v->s, "1");
	return def;
}

/* ---- écriture ---- */
void jb_init(struct jb *b)
{
	memset(b, 0, sizeof *b);
}

void jb_free(struct jb *b)
{
	free(b->p);
	memset(b, 0, sizeof *b);
}

static int grow(struct jb *b, size_t more)
{
	if (b->oom)
		return -1;
	if (b->n + more + 1 <= b->cap)
		return 0;
	size_t cap = b->cap ? b->cap : 4096;
	while (cap < b->n + more + 1)
		cap *= 2;
	char *p = realloc(b->p, cap);
	if (!p) {
		b->oom = 1;
		return -1;
	}
	b->p = p;
	b->cap = cap;
	return 0;
}

void jb_raw(struct jb *b, const char *s, size_t n)
{
	if (grow(b, n))
		return;
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

void jb_rawz(struct jb *b, const char *s)
{
	jb_raw(b, s, strlen(s));
}

static void comma(struct jb *b)
{
	if (b->need_comma)
		jb_raw(b, ",", 1);
	b->need_comma = 1;
}

void jb_obj(struct jb *b)
{
	comma(b);
	jb_raw(b, "{", 1);
	b->need_comma = 0;
}

void jb_obj_end(struct jb *b)
{
	jb_raw(b, "}", 1);
	b->need_comma = 1;
}

void jb_arr(struct jb *b)
{
	comma(b);
	jb_raw(b, "[", 1);
	b->need_comma = 0;
}

void jb_arr_end(struct jb *b)
{
	jb_raw(b, "]", 1);
	b->need_comma = 1;
}

static void esc(struct jb *b, const char *s, size_t n)
{
	static const char hx[] = "0123456789abcdef";
	jb_raw(b, "\"", 1);
	size_t run = 0; /* octets à copier tels quels */
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		const char *r = 0;
		char u[7];
		if (c == '"')
			r = "\\\"";
		else if (c == '\\')
			r = "\\\\";
		else if (c == '\n')
			r = "\\n";
		else if (c == '\r')
			r = "\\r";
		else if (c == '\t')
			r = "\\t";
		else if (c < 0x20 || c == 0x7F) {
			u[0] = '\\';
			u[1] = 'u';
			u[2] = '0';
			u[3] = '0';
			u[4] = hx[c >> 4];
			u[5] = hx[c & 15];
			u[6] = 0;
			r = u;
		}
		if (!r) {
			run++;
			continue;
		}
		jb_raw(b, s + i - run, run);
		run = 0;
		jb_rawz(b, r);
	}
	jb_raw(b, s + n - run, run);
	jb_raw(b, "\"", 1);
}

void jb_key(struct jb *b, const char *k)
{
	comma(b);
	esc(b, k, strlen(k));
	jb_raw(b, ":", 1);
	b->need_comma = 0;
}

/* une chaîne qui n'est pas de l'UTF-8 valide : les octets faux deviennent U+FFFD */
static void strn_clean(struct jb *b, const char *s, size_t n)
{
	if (utf8_valid((const uint8_t *)s, n)) {
		esc(b, s, n);
		return;
	}
	char *t = malloc(n * 3 + 1);
	if (!t) {
		esc(b, "", 0);
		return;
	}
	size_t o = 0;
	for (size_t i = 0; i < n;) {
		size_t k = 1;
		unsigned char c = (unsigned char)s[i];
		if (c >= 0xC2 && c <= 0xF4) {
			size_t need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
			if (i + need <= n && utf8_valid((const uint8_t *)s + i, need))
				k = need;
		}
		if (c < 0x80 || k > 1) {
			if (c == 0) {
				i++;
				continue;
			}
			memcpy(t + o, s + i, k);
			o += k;
		} else {
			memcpy(t + o, "\xEF\xBF\xBD", 3);
			o += 3;
		}
		i += k;
	}
	esc(b, t, o);
	free(t);
}

void jb_strn(struct jb *b, const char *s, size_t n)
{
	comma(b);
	strn_clean(b, s ? s : "", s ? n : 0);
}

void jb_str(struct jb *b, const char *s)
{
	jb_strn(b, s, s ? strlen(s) : 0);
}

void jb_int(struct jb *b, long long v)
{
	char t[32];
	comma(b);
	jb_raw(b, t, (size_t)snprintf(t, sizeof t, "%lld", v));
}

void jb_uint(struct jb *b, unsigned long long v)
{
	char t[32];
	comma(b);
	jb_raw(b, t, (size_t)snprintf(t, sizeof t, "%llu", v));
}

void jb_num(struct jb *b, double v, int decimals)
{
	char t[48];
	comma(b);
	if (v != v || v > 1e300 || v < -1e300) { /* (NaN, infini : pas de JSON) */
		jb_raw(b, "null", 4);
		return;
	}
	jb_raw(b, t, (size_t)snprintf(t, sizeof t, "%.*f", decimals, v));
}

void jb_bool(struct jb *b, int v)
{
	comma(b);
	jb_rawz(b, v ? "true" : "false");
}

void jb_null(struct jb *b)
{
	comma(b);
	jb_rawz(b, "null");
}

void jb_json(struct jb *b, const char *s, size_t n)
{
	comma(b);
	jb_raw(b, s, n);
}

void jb_kstr(struct jb *b, const char *k, const char *s)
{
	jb_key(b, k);
	jb_str(b, s);
}

void jb_kint(struct jb *b, const char *k, long long v)
{
	jb_key(b, k);
	jb_int(b, v);
}

void jb_kbool(struct jb *b, const char *k, int v)
{
	jb_key(b, k);
	jb_bool(b, v);
}

void jb_knum(struct jb *b, const char *k, double v, int decimals)
{
	jb_key(b, k);
	jb_num(b, v, decimals);
}

void jb_base64(struct jb *b, const uint8_t *d, size_t n)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	comma(b);
	if (grow(b, (n + 2) / 3 * 4 + 2))
		return;
	char *o = b->p + b->n;
	*o++ = '"';
	size_t i = 0;
	for (; i + 2 < n; i += 3) {
		unsigned v = (unsigned)d[i] << 16 | (unsigned)d[i + 1] << 8 | d[i + 2];
		*o++ = t[v >> 18];
		*o++ = t[(v >> 12) & 63];
		*o++ = t[(v >> 6) & 63];
		*o++ = t[v & 63];
	}
	if (i < n) {
		unsigned v = (unsigned)d[i] << 16 | (i + 1 < n ? (unsigned)d[i + 1] << 8 : 0);
		*o++ = t[v >> 18];
		*o++ = t[(v >> 12) & 63];
		*o++ = i + 1 < n ? t[(v >> 6) & 63] : '=';
		*o++ = '=';
	}
	*o++ = '"';
	b->n = (size_t)(o - b->p);
	b->p[b->n] = 0;
}

int utf8_valid(const uint8_t *s, size_t n)
{
	for (size_t i = 0; i < n;) {
		uint8_t c = s[i];
		if (c == 0)
			return 0;
		if (c < 0x80) {
			i++;
			continue;
		}
		size_t k;
		unsigned min;
		if ((c & 0xE0) == 0xC0) {
			k = 2;
			min = 0x80;
		} else if ((c & 0xF0) == 0xE0) {
			k = 3;
			min = 0x800;
		} else if ((c & 0xF8) == 0xF0) {
			k = 4;
			min = 0x10000;
		} else
			return 0;
		if (i + k > n)
			return 0;
		unsigned v = c & (0x7F >> k);
		for (size_t j = 1; j < k; j++) {
			if ((s[i + j] & 0xC0) != 0x80)
				return 0;
			v = v << 6 | (s[i + j] & 63);
		}
		if (v < min || v > 0x10FFFF || (v >= 0xD800 && v < 0xE000))
			return 0;
		i += k;
	}
	return 1;
}
