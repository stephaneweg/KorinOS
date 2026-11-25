/* Petites fonctions du noyau : mémoire, chaînes, sortie formatée. */
#ifndef KORIN_LIB_H
#define KORIN_LIB_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);

/* %s %c %d %i %u %x %X %p %%, longueurs l ll z, largeur et remplissage par 0 */
int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int ksnprintf(char *buf, size_t size, const char *fmt, ...);
/* vers la console et la sortie de débogage ; le texte est en UTF-8 */
void kprintf(const char *fmt, ...);
__attribute__((noreturn)) void kpanic(const char *fmt, ...);

#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((uint64_t)(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((uint64_t)(a) - 1))

/* [off, off + len) tient dans [0, size) sans débordement (en-têtes ELF lus d'un fichier) */
static inline int range_in(uint64_t off, uint64_t len, uint64_t size)
{
	return off <= size && len <= size - off;
}

#endif
