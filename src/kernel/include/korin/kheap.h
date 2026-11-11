/* Tas du noyau : petits blocs par classes de taille, gros blocs en pages
 * physiques contiguës (vues par la carte de toute la mémoire). */
#ifndef KORIN_KHEAP_H
#define KORIN_KHEAP_H

#include <stddef.h>

void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *p);

#endif
