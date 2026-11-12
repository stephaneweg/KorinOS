/* string.c : mémoire, chaînes, classes de caractères (ASCII), nombres
 * (docs/BIN-C.md). Le compilateur peut appeler memcpy, memset, memmove et
 * memcmp de lui-même. */
#include "korinrt.h"

void *memcpy(void *d, const void *s, size_t n)
{
	u8 *a = d;
	const u8 *b = s;
	if (n >= 16 && !(((uintptr_t)a | (uintptr_t)b) & 7)) {
		for (; n >= 8; n -= 8, a += 8, b += 8)
			*(u64 *)a = *(const u64 *)b;
	}
	while (n--)
		*a++ = *b++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	u8 *a = d;
	const u8 *b = s;
	if (a <= b || a >= b + n)
		return memcpy(d, s, n);
	while (n--)
		a[n] = b[n];
	return d;
}

void *memset(void *d, int c, size_t n)
{
	u8 *a = d;
	if (n >= 16 && !((uintptr_t)a & 7)) {
		u64 v = (u8)c * 0x0101010101010101ULL;
		for (; n >= 8; n -= 8, a += 8)
			*(u64 *)a = v;
	}
	while (n--)
		*a++ = (u8)c;
	return d;
}

int memcmp(const void *x, const void *y, size_t n)
{
	const u8 *a = x, *b = y;
	for (; n; n--, a++, b++)
		if (*a != *b)
			return *a - *b;
	return 0;
}

void *memchr(const void *s, int c, size_t n)
{
	const u8 *p = s;
	for (; n; n--, p++)
		if (*p == (u8)c)
			return (void *)p;
	return 0;
}

size_t strlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
	while (*a && *a == *b)
		a++, b++;
	return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		if (*a != *b)
			return (u8)*a - (u8)*b;
		if (!*a)
			return 0;
	}
	return 0;
}

char *strcpy(char *d, const char *s)
{
	char *o = d;
	while ((*o++ = *s++))
		;
	return d;
}

char *strncpy(char *d, const char *s, size_t n)
{
	size_t i = 0;
	for (; i < n && s[i]; i++)
		d[i] = s[i];
	for (; i < n; i++)
		d[i] = 0;
	return d;
}

size_t strlcpy(char *d, const char *s, size_t n)
{
	size_t l = strlen(s);
	if (n) {
		size_t k = l < n - 1 ? l : n - 1;
		memcpy(d, s, k);
		d[k] = 0;
	}
	return l;
}

char *strcat(char *d, const char *s)
{
	strcpy(d + strlen(d), s);
	return d;
}

char *strchr(const char *s, int c)
{
	for (;; s++) {
		if (*s == (char)c)
			return (char *)s;
		if (!*s)
			return 0;
	}
}

char *strrchr(const char *s, int c)
{
	const char *r = 0;
	for (;; s++) {
		if (*s == (char)c)
			r = s;
		if (!*s)
			return (char *)r;
	}
}

char *strstr(const char *h, const char *n)
{
	size_t l = strlen(n);
	for (; *h; h++)
		if (!strncmp(h, n, l))
			return (char *)h;
	return l ? 0 : (char *)h;
}

char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

int isdigit(int c) { return (unsigned)c - '0' < 10; }
int isalpha(int c) { return ((unsigned)c | 32) - 'a' < 26; }
int isalnum(int c) { return isdigit(c) || isalpha(c); }
int isspace(int c) { return c == ' ' || (unsigned)c - 9 < 5; }
int isupper(int c) { return (unsigned)c - 'A' < 26; }
int islower(int c) { return (unsigned)c - 'a' < 26; }
int isxdigit(int c) { return isdigit(c) || ((unsigned)c | 32) - 'a' < 6; }
int isprint(int c) { return (unsigned)c - 0x20 < 0x5F; }
int tolower(int c) { return isupper(c) ? c | 32 : c; }
int toupper(int c) { return islower(c) ? c & ~32 : c; }

/* ---- nombres */

u64 strtoul(const char *s, char **end, int base)
{
	const char *p = s;
	while (isspace((u8)*p))
		p++;
	int neg = 0;
	if (*p == '+' || *p == '-')
		neg = *p++ == '-';
	if ((!base || base == 16) && p[0] == '0' && (p[1] | 32) == 'x' && isxdigit((u8)p[2]))
		p += 2, base = 16;
	else if (!base)
		base = *p == '0' ? 8 : 10;
	u64 v = 0;
	const char *d0 = p;
	for (;; p++) {
		int c = (u8)*p, k;
		if (isdigit(c))
			k = c - '0';
		else if (isalpha(c))
			k = (c | 32) - 'a' + 10;
		else
			break;
		if (k >= base)
			break;
		v = v * (u64)base + (u64)k;
	}
	if (end)
		*end = (char *)(p == d0 ? s : p);
	return neg ? -v : v;
}

long strtol(const char *s, char **end, int base)
{
	return (long)strtoul(s, end, base);
}

int atoi(const char *s)
{
	return (int)strtol(s, 0, 10);
}

char *u64_to_str(u64 v, char *buf)
{
	char t[24];
	int n = 0;
	do
		t[n++] = (char)('0' + v % 10);
	while (v /= 10);
	while (n)
		*buf++ = t[--n];
	*buf = 0;
	return buf;
}
