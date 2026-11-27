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
/* PC : l'arène haute, quand une demande ne tient pas dans l'arène (réservations de plusieurs dizaines de
 * Go : PartitionAlloc du module Widevine, 32 Go alignés sur 32 Go) ; aussi pour MAP_FIXED */
#define VM_HIGH_BASE 0x100000000000ULL
#define VM_HIGH_END 0x700000000000ULL
#define BUF_BASE 0x6800000000ULL      /* + n x 64 Mo */
#define BUF_SLOT (64ULL << 20)
#define BUF_MAX 128
#define DMA_BASE 0x6A00000000ULL      /* DMA_ALLOC, puis DEV_MAP */
#define DEV_BASE 0x6B00000000ULL
#define DEV_END 0x6C00000000ULL       /* (WS_FB_VA 0x6D00000000, MAPC_BASE 0x6E00000000 au-dessus) */
/* fenêtres d'IMAGE_CREATE (core/image.c) : 4 x 4 Go, au-dessus de MAPC_BASE ; jamais recopiées par
 * fork (la page d'une image en cours de chargement ne passe pas au fils) */
#define IMG_WIN_BASE 0x7000000000ULL
#define IMG_WIN_SIZE (4ULL << 30)
#define IMG_WIN_MAX 4
#define IMG_WIN_END (IMG_WIN_BASE + IMG_WIN_MAX * IMG_WIN_SIZE)
#define HEAP_LIMIT 0x5000000000ULL
#define VM_LOW_BASE 0x10000ULL       /* VM_FIXED sous l'image du programme (hôte Linux) */
#define USER_STACK_TOP 0x00007FFFFFFF0000ULL
#define USER_STACK_SIZE (64ULL << 20) /* paresseuse, sauf les 16 Ko du haut (64 Mo, comme le Pi) */
/* ASLR (étape 1 de docs/etude/DURCISSEMENT-N3-N5.md) : l'arène commence ses recherches jusqu'à 8 Go plus haut ; les
 * kits sont décalés ensemble jusqu'à 30 Go dans leur zone (KIT_BASE 0x5000000000, 16 x 64 Mo, sous l'arène) ; le
 * pointeur de pile part jusqu'à 1 Mo sous USER_STACK_TOP (le sommet reste : les programmes d'avant le prennent pour
 * borne de la pile du thread principal, src/lib/posix/pthread.c) */
#define ARENA_RND_PAGES ((8ULL << 30) / 4096)
#define KIT_RND_PAGES ((30ULL << 30) / 4096)
#define STACK_RND_MAX (1ULL << 20)

/* sortes de régions (VMK_* de syscalls.inc) */
#define VMK_ANON 1
#define VMK_BSS 2
#define VMK_STACK 3
#define VMK_SHARED 4

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

uint64_t mm_kit_off(struct mm *mm); /* ASLR des kits (kits.c) */
struct mm *mm_create(void);
void mm_destroy(struct process *p); /* tampons rendus, régions libérées (pas la vm) */
void mm_free(struct mm *mm);
int mm_add_region(struct mm *mm, uint64_t start, uint64_t end, uint32_t prot, uint32_t kind);
void mm_set_brk(struct mm *mm, uint64_t start);
void mm_note_image(struct mm *mm, uint64_t lo);
int mm_fork(struct process *parent, struct process *child);
struct ipc_obj;
int64_t mm_map_shared(struct ipc_obj *obj, uint64_t addr, uint64_t size, uint64_t prot, uint64_t flags,
		      uint64_t off);

/* faute de page : 0 si la page est maintenant là */
int mm_fault(struct process *p, uint64_t addr, int write, int exec);
/* la page de va chez p, amenée (inscriptible si write) et épinglée (pmm_ref) sous le verrou de
 * p : son adresse physique, 0 si elle manque */
uint64_t mm_pin_page(struct process *p, uint64_t va, int write);

/* accès du noyau à la mémoire du process courant : pages des régions amenées
 * d'abord ; E_FAULT si une page manque ou n'a pas les droits */
int user_check(uint64_t addr, uint64_t len, int write);
/* (switch.asm) copie rattrapée : 0, ou E_FAULT si une page manque ; après user_check seulement */
int arch_copy_user(void *dst, const void *src, uint64_t len);
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
/* mappe des pages physiques (registres d'un périphérique, non cachés ; cached : mémoire du
 * micrologiciel, en cache) chez le process courant */
int64_t mm_map_device(uint64_t phys, uint64_t size, int cached);

/* pages du process (VM_STATS) */
void mm_count_owned(struct mm *mm, int64_t delta);
uint64_t mm_heap_kb(struct process *p);

#endif
