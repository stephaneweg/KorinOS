/* Chargement d'un exécutable ELF64 dans un espace d'adresses. */
#ifndef KORIN_ELF_H
#define KORIN_ELF_H

#include <stdint.h>

struct vm_space;
struct mm;

/* 0, *entry et *end (fin de la plus haute page chargée), ou erreur négative ;
 * mm : .bss en région paresseuse (0 : tout peuplé) */
int elf_load(struct vm_space *vm, struct mm *mm, const void *image, uint64_t size, uint64_t *entry,
	     uint64_t *end);

#endif
