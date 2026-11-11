/* Espaces d'adresses des process : la moitié basse, propre à chacun ; la
 * moitié haute (noyau) est commune. Fourni par l'architecture. */
#ifndef KORIN_VM_H
#define KORIN_VM_H

#include <stdint.h>

#define VM_W 1 /* écriture */
#define VM_X 2 /* exécution */

struct vm_space;

/* fin (exclue) de la moitié des process */
extern const uint64_t vm_user_top;

struct vm_space *vm_create(void);
void vm_destroy(struct vm_space *vm);   /* rend aussi les pages qui y sont */
int vm_map_page(struct vm_space *vm, uint64_t va, uint64_t phys, int flags);
uint64_t vm_translate(struct vm_space *vm, uint64_t va); /* physique, 0 si absente */
void vm_activate(struct vm_space *vm);  /* 0 : noyau seul */

#endif
