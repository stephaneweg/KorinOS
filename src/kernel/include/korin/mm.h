/* Mémoire d'un process (core/mm.c) : régions paresseuses (VM_MAP, .bss, pile),
 * tas de SBRK, tampons partagés, DMA, accès du noyau à la mémoire du process.
 * Mêmes appels et mêmes adresses que le Pi (docs/ABI.md, 0x00). */
#ifndef KORIN_MM_H
#define KORIN_MM_H

#include <stdint.h>

struct process;
struct vm_space;
struct mm;

/* adresses fixes de l'ABI (src/inc/syscalls.inc, docs/ABI.md) */
#define VM_ARENA_BASE 0x5800000000ULL
#define VM_ARENA_END 0x6800000000ULL
#define BUF_BASE 0x6800000000ULL      /* + n x 64 Mo */
#define BUF_SLOT (64ULL << 20)
#define BUF_MAX 128
#define DMA_BASE 0x6A00000000ULL      /* DMA_ALLOC, puis DEV_MAP */
#define DEV_BASE 0x6B00000000ULL
#define HEAP_LIMIT 0x5000000000ULL
#define USER_STACK_TOP 0x00007FFFFFFF0000ULL
#define USER_STACK_SIZE (256ULL << 10) /* paresseuse, sauf les 16 Ko du haut */

/* sortes de régions (VMK_* de syscalls.inc) */
#define VMK_ANON 1
#define VMK_BSS 2
#define VMK_STACK 3
#define VMK_SHARED 4

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

struct mm *mm_create(void);
void mm_destroy(struct process *p); /* tampons rendus, régions libérées (pas la vm) */
void mm_free(struct mm *mm);
int mm_add_region(struct mm *mm, uint64_t start, uint64_t end, uint32_t prot, uint32_t kind);
void mm_set_brk(struct mm *mm, uint64_t start);
int mm_fork(struct process *parent, struct process *child);

/* faute de page : 0 si la page est maintenant là */
int mm_fault(struct process *p, uint64_t addr, int write, int exec);

/* accès du noyau à la mémoire du process courant : pages des régions amenées
 * d'abord ; E_FAULT si une page manque ou n'a pas les droits */
int user_check(uint64_t addr, uint64_t len, int write);
int copy_from_user(void *dst, uint64_t src, uint64_t len);
int copy_to_user(uint64_t dst, const void *src, uint64_t len);
/* chaîne terminée par 0, max octets avec le 0 ; longueur ou E_FAULT / E_INVAL */
int64_t copy_string_from_user(char *dst, uint64_t src, uint64_t max);

/* appels système de 0x00 à 0x0D */
int64_t sys_sbrk(int64_t inc);
int64_t sys_buf_create(uint64_t size);
int64_t sys_buf_grant(uint64_t n, int64_t pid);
int64_t sys_buf_map(uint64_t n);
int64_t sys_buf_free(uint64_t n);
int64_t sys_buf_info(uint64_t n, uint64_t out);
int64_t sys_dma_alloc(uint64_t size, uint64_t phys_out);
int64_t sys_vm_map(uint64_t addr, uint64_t size, uint64_t prot, uint64_t flags);
int64_t sys_vm_unmap(uint64_t addr, uint64_t size);
int64_t sys_vm_protect(uint64_t addr, uint64_t size, uint64_t prot);
int64_t sys_vm_advise(uint64_t addr, uint64_t size, uint64_t advice);
int64_t sys_vm_query(uint64_t addr, uint64_t out);
int64_t sys_vm_stats(int64_t pid, uint64_t out);
int64_t sys_vm_pageinfo(uint64_t addr);
/* mappe des pages physiques (registres d'un périphérique) chez le process courant */
int64_t mm_map_device(uint64_t phys, uint64_t size);

/* pages du process (VM_STATS) */
void mm_count_owned(struct mm *mm, int64_t delta);
uint64_t mm_heap_kb(struct process *p);

#endif
