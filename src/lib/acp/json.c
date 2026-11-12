/*
 * json.c -- libacp : lecteur et écrivain JSON (json.h).
 */
#include "json.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	const char *p, *end;
	int depth;
} reader;

static void ws (reader *r)
{
	while (r->p < r->end && (*r->p == ' ' || *r->p == '\n' || *r->p == '\r' || *r->p == '\t')) r->p++;
}

static void put_utf8 (jbuf *o, unsigned u)
{
	if (u < 0x80) jb_putc (o, (char) u);
	else if (u < 0x800) { jb_putc (o, (char) (0xC0 | (u >> 6))); jb_putc (o, (char) (0x80 | (u & 63))); }
	else if (u < 0x10000)
	{
		jb_putc (o, (char) (0xE0 | (u >> 12)));
		jb_putc (o, (char) (0x80 | ((u >> 6) & 63)));
		jb_putc (o, (char) (0x80 | (u & 63)));
	}
	else
	{
		jb_putc (o, (char) (0xF0 | (u >> 18)));
		jb_putc (o, (char) (0x80 | ((u >> 12) & 63)));
		jb_putc (o, (char) (0x80 | ((u >> 6) & 63)));
		jb_putc (o, (char) (0x80 | (u & 63)));
	}
}

static unsigned hex4 (reader *r)
{
	unsigned u = 0;
	for (int i = 0; i < 4 && r->p < r->end; i++, r->p++)
	{
		char c = *r->p;
		u = u * 16 + (unsigned) (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
			: c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
	}
	return u;
}

static int rstr (reader *r, char **out, size_t *len)
{
	jbuf o;
	jb_init (&o);
	if (r->p >= r->end || *r->p != '"') return 0;
	r->p++;
	while (r->p < r->end && *r->p != '"')
	{
		const char *q = r->p;
		while (q < r->end && *q != '"' && *q != '\\') q++;
		jb_putn (&o, r->p, (size_t) (q - r->p));
		r->p = q;
		if (q >= r->end || *q == '"') break;
		if (++r->p >= r->end) break;
		char c = *r->p++;
		switch (c)
		{
		case 'n': jb_putc (&o, '\n'); break;
		case 'r': jb_putc (&o, '\r'); break;
		case 't': jb_putc (&o, '\t'); break;
		case 'b': jb_putc (&o, '\b'); break;
		case 'f': jb_putc (&o, '\f'); break;
		case 'u':
		{
			unsigned u = hex4 (r);
			if (u >= 0xD800 && u < 0xDC00 && r->p + 6 <= r->end && r->p[0] == '\\' && r->p[1] == 'u')
			{
				r->p += 2;
				unsigned lo = hex4 (r);
				u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
			}
			put_utf8 (&o, u);
			break;
		}
		default: jb_putc (&o, c);
		}
	}
	if (r->p >= r->end) { jb_free (&o); return 0; }
	r->p++;
	jb_cstr (&o);
	*out = o.p;
	*len = o.n;
	return 1;
}

static json_t *grow (json_t *v)
{
	if (v->n == v->cap)
	{
		v->cap = v->cap ? v->cap * 2 : 4;
		v->items = realloc (v->items, (size_t) v->cap * sizeof *v->items);
		if (v->kind == JSON_OBJ) v->keys = realloc (v->keys, (size_t) v->cap * sizeof *v->keys);
	}
	json_t *e = &v->items[v->n];
	memset (e, 0, sizeof *e);
	if (v->kind == JSON_OBJ) v->keys[v->n] = NULL;
	v->n++;
	return e;
}

static int value (reader *r, json_t *v)
{
	ws (r);
	if (r->p >= r->end || ++r->depth > 64) return 0;
	int ok = 1;
	if (*r->p == '{')
	{
		v->kind = JSON_OBJ;
		r->p++;
		ws (r);
		if (r->p < r->end && *r->p == '}') r->p++;
		else for (;;)
		{
			char *k;
			size_t kl;
			ws (r);
			if (!rstr (r, &k, &kl)) { ok = 0; break; }
			ws (r);
			if (r->p >= r->end || *r->p != ':') { free (k); ok = 0; break; }
			r->p++;
			json_t *e = grow (v);
			v->keys[v->n - 1] = k;
			if (!value (r, e)) { ok = 0; break; }
			ws (r);
			if (r->p < r->end && *r->p == ',') { r->p++; continue; }
			if (r->p < r->end && *r->p == '}') { r->p++; break; }
			ok = 0;
			break;
		}
	}
	else if (*r->p == '[')
	{
		v->kind = JSON_ARR;
		r->p++;
		ws (r);
		if (r->p < r->end && *r->p == ']') r->p++;
		else for (;;)
		{
			if (!value (r, grow (v))) { ok = 0; break; }
			ws (r);
			if (r->p < r->end && *r->p == ',') { r->p++; continue; }
			if (r->p < r->end && *r->p == ']') { r->p++; break; }
			ok = 0;
			break;
		}
	}
	else if (*r->p == '"') { v->kind = JSON_STR; ok = rstr (r, &v->s, &v->len); }
	else if (r->end - r->p >= 4 && !strncmp (r->p, "true", 4)) { v->kind = JSON_BOOL; v->b = 1; r->p += 4; }
	else if (r->end - r->p >= 5 && !strncmp (r->p, "false", 5)) { v->kind = JSON_BOOL; r->p += 5; }
	else if (r->end - r->p >= 4 && !strncmp (r->p, "null", 4)) { v->kind = JSON_NULL; r->p += 4; }
	else
	{
		const char *b = r->p;
		while (r->p < r->end && (strchr ("+-.eE", *r->p) || (*r->p >= '0' && *r->p <= '9'))) r->p++;
		v->kind = JSON_NUM;
		v->len = (size_t) (r->p - b);
		v->s = malloc (v->len + 1);
		memcpy (v->s, b, v->len);
		v->s[v->len] = 0;
		ok = r->p > b;
	}
	r->depth--;
	return ok;
}

static void clear (json_t *v)
{
	for (int i = 0; i < v->n; i++)
	{
		clear (&v->items[i]);
		if (v->keys) free (v->keys[i]);
	}
	free (v->items);
	free (v->keys);
	free (v->s);
}

json_t *json_parse (const char *text, size_t len)
{
	reader r = { text, text + len, 0 };
	json_t *v = calloc (1, sizeof *v);
	if (!value (&r, v)) { json_free (v); return NULL; }
	return v;
}

void json_free (json_t *v)
{
	if (!v) return;
	clear (v);
	free (v);
}

const json_t *json_get (const json_t *v, const char *key)
{
	if (!v || v->kind != JSON_OBJ) return NULL;
	for (int i = 0; i < v->n; i++) if (v->keys[i] && !strcmp (v->keys[i], key)) return &v->items[i];
	return NULL;
}

const json_t *json_at (const json_t *v, int i)
{
	return v && (v->kind == JSON_ARR || v->kind == JSON_OBJ) && i >= 0 && i < v->n ? &v->items[i] : NULL;
}

const json_t *json_path (const json_t *v, const char *path)
{
	char k[128];
	while (v && *path)
	{
		const char *d = strchr (path, '.');
		size_t n = d ? (size_t) (d - path) : strlen (path);
		if (n >= sizeof k) return NULL;
		memcpy (k, path, n);
		k[n] = 0;
		v = json_get (v, k);
		path += n + (d ? 1 : 0);
	}
	return v;
}

const char *json_text (const json_t *v)
{
	return v && (v->kind == JSON_STR || v->kind == JSON_NUM) && v->s ? v->s : "";
}

const char *json_gets (const json_t *v, const char *key)
{
	return json_text (json_get (v, key));
}

long long json_int (const json_t *v, long long def)
{
	return v && v->kind == JSON_NUM ? strtoll (v->s, NULL, 10) : def;
}

int json_len (const json_t *v)
{
	return v && (v->kind == JSON_ARR || v->kind == JSON_OBJ) ? v->n : 0;
}

/* --- écriture ---------------------------------------------------------------------------------------------------- */

void jb_init (jbuf *b)
{
	b->p = NULL;
	b->n = b->cap = 0;
}

void jb_free (jbuf *b)
{
	free (b->p);
	jb_init (b);
}

void jb_putn (jbuf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap)
	{
		size_t c = b->cap ? b->cap : 64;
		while (c < b->n + n + 1) c *= 2;
		b->p = realloc (b->p, c);
		b->cap = c;
	}
	memcpy (b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

void jb_puts (jbuf *b, const char *s) { jb_putn (b, s, strlen (s)); }
void jb_putc (jbuf *b, char c) { jb_putn (b, &c, 1); }

void jb_printf (jbuf *b, const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	va_start (ap, fmt);
	int n = vsnprintf (tmp, sizeof tmp, fmt, ap);
	va_end (ap);
	if (n < 0) return;
	if ((size_t) n < sizeof tmp) { jb_putn (b, tmp, (size_t) n); return; }
	char *big = malloc ((size_t) n + 1);
	va_start (ap, fmt);
	vsnprintf (big, (size_t) n + 1, fmt, ap);
	va_end (ap);
	jb_putn (b, big, (size_t) n);
	free (big);
}

void jb_strn (jbuf *b, const char *s, size_t n)
{
	static const char hex[] = "0123456789abcdef";
	jb_putc (b, '"');
	const char *run = s;
	for (size_t i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c >= 0x80)
		{
			/* une suite UTF-8 complète passe telle quelle ; un octet isolé devient U+FFFD (l'agent refuserait la ligne) */
			size_t k = c >= 0xF0 && c < 0xF8 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c < 0xE0 ? 2 : 0;
			size_t j = 1;
			while (k && j < k && i + j < n && ((unsigned char) s[i + j] & 0xC0) == 0x80) j++;
			if (k && j == k) { i += k - 1; continue; }
			jb_putn (b, run, (size_t) (s + i - run));
			jb_puts (b, "\\ufffd");
			run = s + i + 1;
			continue;
		}
		if (c >= 0x20 && c != '"' && c != '\\') continue;
		jb_putn (b, run, (size_t) (s + i - run));
		run = s + i + 1;
		if (c == '"') jb_puts (b, "\\\"");
		else if (c == '\\') jb_puts (b, "\\\\");
		else if (c == '\n') jb_puts (b, "\\n");
		else if (c == '\r') jb_puts (b, "\\r");
		else if (c == '\t') jb_puts (b, "\\t");
		else { char e[7] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15], 0 }; jb_puts (b, e); }
	}
	jb_putn (b, run, (size_t) (s + n - run));
	jb_putc (b, '"');
}

void jb_str (jbuf *b, const char *s) { jb_strn (b, s ? s : "", s ? strlen (s) : 0); }

void jb_json (jbuf *b, const json_t *v)
{
	if (!v) { jb_puts (b, "null"); return; }
	switch (v->kind)
	{
	case JSON_NULL: jb_puts (b, "null"); break;
	case JSON_BOOL: jb_puts (b, v->b ? "true" : "false"); break;
	case JSON_NUM: jb_puts (b, v->s); break;
	case JSON_STR: jb_strn (b, v->s, v->len); break;
	case JSON_ARR:
	case JSON_OBJ:
		jb_putc (b, v->kind == JSON_ARR ? '[' : '{');
		for (int i = 0; i < v->n; i++)
		{
			if (i) jb_putc (b, ',');
			if (v->kind == JSON_OBJ) { jb_str (b, v->keys[i]); jb_putc (b, ':'); }
			jb_json (b, &v->items[i]);
		}
		jb_putc (b, v->kind == JSON_ARR ? ']' : '}');
		break;
	}
}

const char *jb_cstr (jbuf *b)
{
	if (!b->p) jb_putn (b, "", 0);
	return b->p;
}
