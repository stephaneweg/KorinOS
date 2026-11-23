/* mem.c : memset, memcpy, memmove, memcmp pour les programmes d'essai de l'initrd du PC (sans bibliothèque).
 * GCC peut appeler memset et memcpy même avec -ffreestanding (un tableau mis à zéro : « u64 m[64] = {99} ») ;
 * selon sa version il les fait en ligne ou non (23/11/2026 : le GCC d'une Fedora plus récente que la 43 appelait
 * memset dans kbtest). Compilé avec -fno-tree-loop-distribute-patterns : ces boucles ne redeviennent pas des
 * appels à elles-mêmes. */
typedef unsigned long size_t;

void *memset(void *d, int c, size_t n)
{
	unsigned char *p = d;
	while (n--)
		*p++ = (unsigned char)c;
	return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
	unsigned char *p = d;
	const unsigned char *q = s;
	while (n--)
		*p++ = *q++;
	return d;
}

void *memmove(void *d, const void *s, size_t n)
{
	unsigned char *p = d;
	const unsigned char *q = s;
	if (p < q)
		while (n--)
			*p++ = *q++;
	else
		while (n--)
			p[n] = q[n];
	return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
	const unsigned char *p = a, *q = b;
	for (; n; n--, p++, q++)
		if (*p != *q)
			return *p - *q;
	return 0;
}
