/* format.c : printf limité (docs/BIN-C.md) : %d %i %u %x %X %o %c %s %p %%,
 * longueurs h hh l ll z j t, largeur et précision (nombres ou *), drapeaux
 * - 0 + espace #. Pas de flottants. */
#include "korinrt.h"

typedef void (*put_fn)(void *ctx, const char *s, size_t n);

static void pad(put_fn put, void *ctx, char c, int n)
{
	char t[16];
	memset(t, c, sizeof t);
	while (n > 0) {
		int k = n > 16 ? 16 : n;
		put(ctx, t, (size_t)k);
		n -= k;
	}
}

int vformat(put_fn put, void *ctx, const char *fmt, va_list ap)
{
	int total = 0;
	while (*fmt) {
		const char *p = fmt;
		while (*p && *p != '%')
			p++;
		if (p != fmt) {
			put(ctx, fmt, (size_t)(p - fmt));
			total += (int)(p - fmt);
		}
		if (!*p)
			break;
		fmt = p + 1;
		int left = 0, zero = 0, plus = 0, space = 0, alt = 0;
		for (;; fmt++) {
			if (*fmt == '-')
				left = 1;
			else if (*fmt == '0')
				zero = 1;
			else if (*fmt == '+')
				plus = 1;
			else if (*fmt == ' ')
				space = 1;
			else if (*fmt == '#')
				alt = 1;
			else
				break;
		}
		int width = 0, prec = -1;
		if (*fmt == '*') {
			width = va_arg(ap, int);
			if (width < 0)
				left = 1, width = -width;
			fmt++;
		} else
			while (isdigit((u8)*fmt))
				width = width * 10 + (*fmt++ - '0');
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				prec = va_arg(ap, int);
				fmt++;
			} else
				while (isdigit((u8)*fmt))
					prec = prec * 10 + (*fmt++ - '0');
		}
		int lng = 0; /* 0 int, 1 long, -1 short, -2 char */
		for (;; fmt++) {
			if (*fmt == 'l' || *fmt == 'z' || *fmt == 'j' || *fmt == 't')
				lng = 1;
			else if (*fmt == 'h')
				lng = lng < 0 ? -2 : -1;
			else
				break;
		}
		char conv = *fmt;
		if (!conv)
			break;
		fmt++;
		char tmp[72];
		const char *s = tmp;
		int n = 0;
		char sign = 0;
		const char *prefix = "";
		switch (conv) {
		case 'c':
			tmp[0] = (char)va_arg(ap, int);
			n = 1;
			break;
		case 's':
			s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			n = 0;
			while (s[n] && (prec < 0 || n < prec))
				n++;
			prec = -1;
			break;
		case 'd':
		case 'i':
		case 'u':
		case 'x':
		case 'X':
		case 'o':
		case 'p': {
			u64 v;
			int base = conv == 'o' ? 8 : (conv == 'x' || conv == 'X' || conv == 'p') ? 16 : 10;
			if (conv == 'p') {
				v = (u64)va_arg(ap, void *);
				prefix = "0x";
			} else if (conv == 'd' || conv == 'i') {
				long sv = lng > 0 ? va_arg(ap, long) : va_arg(ap, int);
				if (lng == -1)
					sv = (short)sv;
				else if (lng == -2)
					sv = (signed char)sv;
				if (sv < 0)
					sign = '-', v = -(u64)sv;
				else {
					v = (u64)sv;
					sign = plus ? '+' : space ? ' ' : 0;
				}
			} else {
				v = lng > 0 ? va_arg(ap, u64) : va_arg(ap, unsigned);
				if (lng == -1)
					v = (u16)v;
				else if (lng == -2)
					v = (u8)v;
				if (alt && v && base == 16)
					prefix = conv == 'X' ? "0X" : "0x";
				else if (alt && base == 8)
					prefix = "0";
			}
			const char *dig = conv == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
			char *e = tmp + sizeof tmp;
			char *b = e;
			while (v) {
				*--b = dig[v % (u64)base];
				v /= (u64)base;
			}
			if (prec < 0 && b == e)
				*--b = '0';
			while (e - b < prec)
				*--b = '0';
			s = b;
			n = (int)(e - b);
			if (prec >= 0)
				zero = 0;
			break;
		}
		default: /* %% et l'inconnu : tel quel */
			tmp[0] = conv;
			n = 1;
			break;
		}
		int pl = (int)strlen(prefix) + (sign != 0);
		int fill = width - n - pl;
		if (!left && !zero)
			pad(put, ctx, ' ', fill);
		if (sign)
			put(ctx, &sign, 1);
		if (*prefix)
			put(ctx, prefix, strlen(prefix));
		if (!left && zero)
			pad(put, ctx, '0', fill);
		put(ctx, s, (size_t)n);
		if (left)
			pad(put, ctx, ' ', fill);
		total += n + pl + (fill > 0 ? fill : 0);
	}
	return total;
}

static void put_out(void *ctx, const char *s, size_t n)
{
	(void)ctx;
	out_write(s, n);
}

static void put_err(void *ctx, const char *s, size_t n)
{
	(void)ctx;
	sys_write(2, s, n);
}

struct sbuf {
	char *p;
	size_t n, i;
};

static void put_buf(void *ctx, const char *s, size_t n)
{
	struct sbuf *b = ctx;
	for (size_t k = 0; k < n; k++, b->i++)
		if (b->i + 1 < b->n)
			b->p[b->i] = s[k];
}

int printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vformat(put_out, 0, fmt, ap);
	va_end(ap);
	return r;
}

int eprintf(const char *fmt, ...)
{
	out_flush();
	va_list ap;
	va_start(ap, fmt);
	int r = vformat(put_err, 0, fmt, ap);
	va_end(ap);
	return r;
}

int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap)
{
	struct sbuf b = { buf, n, 0 };
	int r = vformat(put_buf, &b, fmt, ap);
	if (n)
		buf[b.i < n ? b.i : n - 1] = 0;
	return r;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = vsnprintf(buf, n, fmt, ap);
	va_end(ap);
	return r;
}
