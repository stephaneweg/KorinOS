/* Chargement d'un exécutable ELF64 dans un espace d'adresses. */
#ifndef KORIN_ELF_H
#define KORIN_ELF_H

#include <stdint.h>

struct vm_space;

/* 0 et *entry, ou erreur négative */
int elf_load(struct vm_space *vm, const void *image, uint64_t size, uint64_t *entry);

#endif
