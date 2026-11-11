/* Mémoire, chaînes et sortie formatée du noyau. Portable. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/lib.h>
#include <korin/proc.h>
#include <korin/spinlock.h>

static spinlock_t debug_lock = SPINLOCK_INIT;

void *memset(void *d, int c, size_t n)
{
	unsigned char *p = d;
	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	while (n--)
		*a++ = *b++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	if (a == b || n == 0)
		return d;
	if (a < b) {
		while (n--)
			*a++ = *b++;
	} else {
		a += n;
		b += n;
		while (n--)
			*--a = *--b;
	}
	return d;
}

int memcmp(const void *x, const void *y, size_t n)
{
	const unsigned char *a = x, *b = y;
	for (; n; n--, a++, b++)
		if (*a != *b)
			return *a - *b;
	return 0;
}

size_t strlen(const char *s)
{
	size_t n = 0;
	while (s[n])
		n++;
	return n;
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (unsigned char)*a - (unsigned char)*b;
		if (!*a)
			return 0;
	}
	return 0;
}

struct out {
	char *buf;
	size_t size, len;
};

static void out_c(struct out *o, char c)
{
	if (o->len + 1 < o->size)
		o->buf[o->len] = c;
	o->len++;
}

static void out_num(struct out *o, uint64_t v, unsigned base, int upper, int width, int zero, int neg)
{
	char tmp[24];
	const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	int n = 0;
	do {
		tmp[n++] = digits[v % base];
		v /= base;
	} while (v);
	int len = n + (neg ? 1 : 0);
	if (!zero)
		for (; width > len; width--)
			out_c(o, ' ');
	if (neg)
		out_c(o, '-');
	if (zero)
		for (; width > len; width--)
			out_c(o, '0');
	while (n)
		out_c(o, tmp[--n]);
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
	struct out o = {buf, size, 0};
	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			out_c(&o, *fmt);
			continue;
		}
		fmt++;
		int zero = 0, width = 0, lng = 0, left = 0;
		if (*fmt == '-') {
			left = 1;
			fmt++;
		}
		if (*fmt == '0') {
			zero = 1;
			fmt++;
		}
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		while (*fmt == 'l' || *fmt == 'z') {
			lng++;
			fmt++;
		}
		switch (*fmt) {
		case 'd':
		case 'i': {
			int64_t v = lng ? va_arg(ap, int64_t) : va_arg(ap, int);
			out_num(&o, v < 0 ? (uint64_t)-v : (uint64_t)v, 10, 0, width, zero, v < 0);
			break;
		}
		case 'u':
			out_num(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 10, 0, width, zero, 0);
			break;
		case 'x':
		case 'X':
			out_num(&o, lng ? va_arg(ap, uint64_t) : va_arg(ap, unsigned), 16, *fmt == 'X', width, zero, 0);
			break;
		case 'p':
			out_c(&o, '0');
			out_c(&o, 'x');
			out_num(&o, (uint64_t)(uintptr_t)va_arg(ap, void *), 16, 0, 16, 1, 0);
			break;
		case 'c':
			out_c(&o, (char)va_arg(ap, int));
			break;
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			size_t n = strlen(s);
			if (!left)
				for (; width > (int)n; width--)
					out_c(&o, ' ');
			while (*s)
				out_c(&o, *s++);
			for (; left && width > (int)n; width--)
				out_c(&o, ' ');
			break;
		}
		case '%':
			out_c(&o, '%');
			break;
		case 0:
			fmt--;
			break;
		default:
			out_c(&o, '%');
			out_c(&o, *fmt);
		}
	}
	if (size)
		buf[o.len < size ? o.len : size - 1] = 0;
	return (int)o.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = kvsnprintf(buf, size, fmt, ap);
	va_end(ap);
	return n;
}

static void kvprintf(const char *fmt, va_list ap)
{
	char buf[512];
	kvsnprintf(buf, sizeof buf, fmt, ap);
	uint64_t f = spin_lock_irqsave(&debug_lock);
	for (const char *p = buf; *p; p++)
		arch_debug_putc(*p);
	spin_unlock_irqrestore(&debug_lock, f);
	klog_write(buf);
	console_write(buf);
}

void kprintf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	kvprintf(fmt, ap);
	va_end(ap);
}

void kpanic(const char *fmt, ...)
{
	va_list ap;
	arch_irq_disable();
	console_color(CON_ERROR);
	kprintf("\nPANIQUE : ");
	va_start(ap, fmt);
	kvprintf(fmt, ap);
	va_end(ap);
	kprintf("\n");
	arch_halt_forever();
}
