/*
 * util.c -- rg (docs/RG.md) : tampons qui grandissent, la sortie standard par paquets, les messages
 * d'erreur « rg: ... », les chaines JSON (comme serde_json : texte UTF-8, sinon octets en base64).
 */
#include "rg.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

sbuf g_out;
int g_line_buffered;
int g_errored;
int g_no_messages;

void *xmalloc (size_t n)
{
	void *p = malloc (n ? n : 1);
	if (!p)
	{
		fputs ("rg: memoire epuisee\n", stderr);
		exit (2);
	}
	return p;
}

void *xrealloc (void *p, size_t n)
{
	p = realloc (p, n ? n : 1);
	if (!p)
	{
		fputs ("rg: memoire epuisee\n", stderr);
		exit (2);
	}
	return p;
}

char *xstrndup (const char *s, size_t n)
{
	char *d = xmalloc (n + 1);
	memcpy (d, s, n);
	d[n] = 0;
	return d;
}

char *xstrdup (const char *s) { return xstrndup (s, strlen (s)); }

void sb_grow (sbuf *b, size_t need)
{
	if (b->n + need + 1 <= b->cap)
		return;
	size_t c = b->cap ? b->cap : 256;
	while (c < b->n + need + 1)
		c *= 2;
	b->p = xrealloc (b->p, c);
	b->cap = c;
}

void sb_put (sbuf *b, const void *s, size_t n)
{
	sb_grow (b, n);
	memcpy (b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

void sb_putc (sbuf *b, char c)
{
	sb_grow (b, 1);
	b->p[b->n++] = c;
	b->p[b->n] = 0;
}

void sb_puts (sbuf *b, const char *s) { sb_put (b, s, strlen (s)); }

void sb_printf (sbuf *b, const char *fmt, ...)
{
	va_list ap;
	char tmp[256];
	va_start (ap, fmt);
	int n = vsnprintf (tmp, sizeof tmp, fmt, ap);
	va_end (ap);
	if (n < 0)
		return;
	if ((size_t) n < sizeof tmp)
	{
		sb_put (b, tmp, n);
		return;
	}
	sb_grow (b, n);
	va_start (ap, fmt);
	vsnprintf (b->p + b->n, n + 1, fmt, ap);
	va_end (ap);
	b->n += n;
}

void sb_free (sbuf *b)
{
	free (b->p);
	b->p = 0;
	b->n = b->cap = 0;
}

/* ecrit tout ; un tube ferme (EPIPE : « rg ... | head ») : on s'arrete sans bruit, comme ripgrep */
static void write_all (int fd, const char *p, size_t n)
{
	while (n)
	{
		ssize_t w = write (fd, p, n);
		if (w < 0)
		{
			if (errno == EINTR)
				continue;
			if (errno == EPIPE)
				_exit (0);
			_exit (2);
		}
		p += w;
		n -= w;
	}
}

void out_flush (void)
{
	if (g_out.n)
		write_all (1, g_out.p, g_out.n);
	g_out.n = 0;
}

void out_maybe_flush (void)
{
	if (g_line_buffered || g_out.n >= (64 << 10))
		out_flush ();
}

void rg_err (const char *fmt, ...)
{
	char tmp[4096];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (tmp, sizeof tmp, fmt, ap);
	va_end (ap);
	out_flush ();
	write_all (2, "rg: ", 4);
	write_all (2, tmp, strlen (tmp));
	write_all (2, "\n", 1);
}

/* ---- UTF-8 et JSON ---- */

int utf8_valid (const unsigned char *s, size_t n)
{
	size_t i = 0;
	while (i < n)
	{
		unsigned c = s[i];
		if (c < 0x80)
		{
			i++;
			continue;
		}
		int k;
		unsigned min;
		if ((c & 0xE0) == 0xC0) { k = 1; min = 0x80; c &= 0x1F; }
		else if ((c & 0xF0) == 0xE0) { k = 2; min = 0x800; c &= 0x0F; }
		else if ((c & 0xF8) == 0xF0) { k = 3; min = 0x10000; c &= 0x07; }
		else return 0;
		if (i + k >= n)
			return 0;
		for (int j = 1; j <= k; j++)
		{
			if ((s[i + j] & 0xC0) != 0x80)
				return 0;
			c = (c << 6) | (s[i + j] & 0x3F);
		}
		if (c < min || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF))
			return 0;
		i += k + 1;
	}
	return 1;
}

static void b64 (sbuf *b, const unsigned char *s, size_t n)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t i = 0;
	for (; i + 3 <= n; i += 3)
	{
		unsigned v = (s[i] << 16) | (s[i + 1] << 8) | s[i + 2];
		char q[4] = { t[v >> 18], t[(v >> 12) & 63], t[(v >> 6) & 63], t[v & 63] };
		sb_put (b, q, 4);
	}
	if (n - i == 1)
	{
		unsigned v = s[i] << 16;
		char q[4] = { t[v >> 18], t[(v >> 12) & 63], '=', '=' };
		sb_put (b, q, 4);
	}
	else if (n - i == 2)
	{
		unsigned v = (s[i] << 16) | (s[i + 1] << 8);
		char q[4] = { t[v >> 18], t[(v >> 12) & 63], t[(v >> 6) & 63], '=' };
		sb_put (b, q, 4);
	}
}

static void json_text (sbuf *b, const char *s, size_t n)
{
	static const char hx[] = "0123456789abcdef";
	sb_putc (b, '"');
	size_t i = 0, run = 0;
	for (i = 0; i < n; i++)
	{
		unsigned char c = s[i];
		const char *e = 0;
		char u[6];
		if (c == '"') e = "\\\"";
		else if (c == '\\') e = "\\\\";
		else if (c == '\n') e = "\\n";
		else if (c == '\r') e = "\\r";
		else if (c == '\t') e = "\\t";
		else if (c == '\b') e = "\\b";
		else if (c == '\f') e = "\\f";
		else if (c < 0x20)
		{
			u[0] = '\\'; u[1] = 'u'; u[2] = '0'; u[3] = '0'; u[4] = hx[c >> 4]; u[5] = hx[c & 15];
			sb_put (b, s + run, i - run);
			sb_put (b, u, 6);
			run = i + 1;
			continue;
		}
		if (e)
		{
			sb_put (b, s + run, i - run);
			sb_puts (b, e);
			run = i + 1;
		}
	}
	sb_put (b, s + run, n - run);
	sb_putc (b, '"');
}

void json_str (sbuf *b, const char *s, size_t n)
{
	if (utf8_valid ((const unsigned char *) s, n))
	{
		sb_puts (b, "{\"text\":");
		json_text (b, s, n);
	}
	else
	{
		sb_puts (b, "{\"bytes\":\"");
		b64 (b, (const unsigned char *) s, n);
		sb_putc (b, '"');
	}
	sb_putc (b, '}');
}
