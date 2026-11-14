/* Espaces d'adresses des process : la moitié basse, propre à chacun ; la
 * moitié haute (noyau) est commune. Fourni par l'architecture. */
#ifndef KORIN_VM_H
#define KORIN_VM_H

#include <stdint.h>

#define VM_W 1        /* écriture */
#define VM_X 2        /* exécution */
#define VM_SHARED 4   /* page qui n'est pas au process : jamais rendue par vm_destroy */
#define VM_NOCACHE 8  /* registres de périphérique : non cachée */
#define VM_NOREAD 16  /* (sortie de vm_page_flags) */
#define VM_PRESENT 32 /* (sortie de vm_page_flags) */
#define VM_COW 64     /* (sortie de vm_page_flags) copie sur écriture */
#define VM_WC 128     /* écriture combinée (framebuffer) */

struct vm_space;

/* fin (exclue) de la moitié des process */
extern const uint64_t vm_user_top;

struct vm_space *vm_create(void);
void vm_destroy(struct vm_space *vm);   /* rend les pages qui sont au process */
int vm_map_page(struct vm_space *vm, uint64_t va, uint64_t phys, int flags);
/* retire la page : son adresse physique (0 si absente) ; *flags : VM_* qu'elle avait */
uint64_t vm_unmap_page(struct vm_space *vm, uint64_t va, int *flags);
/* droits d'une page présente ; 0 si absente */
int vm_protect_page(struct vm_space *vm, uint64_t va, int flags);
int vm_page_flags(struct vm_space *vm, uint64_t va); /* VM_PRESENT | VM_W | VM_X | VM_SHARED, 0 */
uint64_t vm_translate(struct vm_space *vm, uint64_t va); /* physique, 0 si absente */
void vm_activate(struct vm_space *vm);  /* 0 : noyau seul */
/* les autres cœurs oublient les traductions de cet espace (après retrait ou
 * changement de droits) ; à appeler sans verrou tenu, interruptions permises */
void vm_tlb_shootdown(struct vm_space *vm);
/* répond à une invalidation demandée à ce cœur (pour qui attend, interruptions coupées) */
void vm_tlb_poll(void);
uint64_t vm_table_pages(struct vm_space *vm);
/* première adresse >= va (< end) qui peut porter une page (saute les zones vides) */
uint64_t vm_next_candidate(struct vm_space *vm, uint64_t va, uint64_t end);
/* fork : le fils voit les mêmes pages ; les pages privées passent en lecture
 * seule + copie sur écriture chez les deux (une référence de plus) */
int vm_fork(struct vm_space *parent, struct vm_space *child);
/* écriture sur une page en copie : 0 si elle est maintenant inscriptible */
int vm_cow_break(struct vm_space *vm, uint64_t va); /* 0 sur place, 1 copiée, -1 */
/* (copiée : l'ancienne page dans *old_out, à rendre après l'invalidation des TLB) */
int vm_cow_break_keep(struct vm_space *vm, uint64_t va, uint64_t *old_out);

#endif
