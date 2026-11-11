/* Petites fonctions de mémoire et de chaînes des kits en C (sans
 * bibliothèque) ; le compilateur peut aussi les appeler lui-même. */
#include "korin_kit.h"

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	while (n--)
		*a++ = *b++;
	return d;
}

void *memset(void *d, int c, size_t n)
{
	unsigned char *a = d;
	while (n--)
		*a++ = (unsigned char)c;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *a = d;
	const unsigned char *b = s;
	if (a < b)
		while (n--)
			*a++ = *b++;
	else
		while (n--)
			a[n] = b[n];
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
