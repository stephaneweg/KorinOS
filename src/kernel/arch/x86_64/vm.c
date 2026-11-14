/* Espaces d'adresses x86-64 : une PML4 par process ; les entrées 256 à 511
 * (moitié haute : carte de la mémoire, noyau) sont recopiées de celle du
 * noyau et pointent sur les mêmes tables. Les appels sur un process vivant se
 * font sous le verrou de sa mémoire (core/mm.c). */
#include <korin/arch.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/pmm.h>
#include <korin/vm.h>

#include "cpu.h"
#include "paging.h"

#define PTE_P (1ULL << 0)
#define PTE_W (1ULL << 1)
#define PTE_U (1ULL << 2)
#define PTE_PWT (1ULL << 3)
#define PTE_PCD (1ULL << 4)
#define PTE_PS (1ULL << 7)
#define PTE_SHARED (1ULL << 9) /* bit libre : page qui n'est pas au process */
#define PTE_COW (1ULL << 10)    /* bit libre : copie sur écriture */
#define PTE_NX (1ULL << 63)
#define ADDR_MASK 0x000FFFFFFFFFF000ULL

const uint64_t vm_user_top = 0x0000800000000000ULL;

struct vm_space {
	uint64_t pml4; /* physique */
	uint64_t tables;
};

void smp_tlb_shootdown(uint64_t root);

static uint64_t *table(uint64_t phys)
{
	return arch_phys_to_virt(phys & ADDR_MASK);
}

static uint64_t new_table(struct vm_space *vm)
{
	uint64_t p = pmm_alloc();
	if (p) {
		memset(arch_phys_to_virt(p), 0, PAGE_SIZE);
		if (vm)
			vm->tables++;
	}
	return p;
}

struct vm_space *vm_create(void)
{
	struct vm_space *vm = kzalloc(sizeof *vm);
	if (!vm)
		return 0;
	vm->pml4 = new_table(vm);
	if (!vm->pml4) {
		kfree(vm);
		return 0;
	}
	uint64_t *dst = table(vm->pml4), *src = table(paging_kernel_pml4());
	for (int i = 256; i < 512; i++)
		dst[i] = src[i];
	return vm;
}

static uint64_t *next_level(struct vm_space *vm, uint64_t *t, int idx, int create)
{
	if (!(t[idx] & PTE_P)) {
		if (!create)
			return 0;
		uint64_t p = new_table(vm);
		if (!p)
			return 0;
		t[idx] = p | PTE_P | PTE_W | PTE_U;
	}
	return table(t[idx]);
}

static uint64_t *walk(struct vm_space *vm, uint64_t va, int create)
{
	uint64_t *t = table(vm->pml4);
	t = next_level(vm, t, (va >> 39) & 511, create);
	if (t)
		t = next_level(vm, t, (va >> 30) & 511, create);
	if (t)
		t = next_level(vm, t, (va >> 21) & 511, create);
	return t ? &t[(va >> 12) & 511] : 0;
}

static void flush_local(struct vm_space *vm, uint64_t va)
{
	if (read_cr3() == vm->pml4)
		__asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
}

static uint64_t pte_bits(int flags)
{
	uint64_t e = PTE_P | PTE_U;
	if (flags & VM_W)
		e |= PTE_W;
	if (!(flags & VM_X))
		e |= PTE_NX;
	if (flags & VM_SHARED)
		e |= PTE_SHARED;
	if (flags & VM_NOCACHE)
		e |= PTE_PCD | PTE_PWT; /* PAT 3 : non cachée */
	else if (flags & VM_WC)
		e |= PTE_PWT; /* PAT 1 : écriture combinée (paging_init) */
	if (flags & VM_COW)
		e = (e & ~PTE_W) | PTE_COW;
	return e;
}

static int pte_flags(uint64_t e)
{
	if (!(e & PTE_P))
		return 0;
	int f = VM_PRESENT;
	if (e & PTE_W)
		f |= VM_W;
	if (!(e & PTE_NX))
		f |= VM_X;
	if (e & PTE_SHARED)
		f |= VM_SHARED;
	if (e & PTE_PCD)
		f |= VM_NOCACHE;
	if (e & PTE_COW)
		f |= VM_COW;
	return f;
}

int vm_map_page(struct vm_space *vm, uint64_t va, uint64_t phys, int flags)
{
	if (va >= vm_user_top)
		return -22;
	uint64_t *pte = walk(vm, va, 1);
	if (!pte)
		return -12;
	*pte = (phys & ADDR_MASK) | pte_bits(flags);
	flush_local(vm, va);
	return 0;
}

uint64_t vm_unmap_page(struct vm_space *vm, uint64_t va, int *flags)
{
	if (va >= vm_user_top)
		return 0;
	uint64_t *pte = walk(vm, va, 0);
	if (!pte || !(*pte & PTE_P)) {
		if (flags)
			*flags = 0;
		return 0;
	}
	uint64_t e = *pte;
	*pte = 0;
	flush_local(vm, va);
	if (flags)
		*flags = pte_flags(e);
	return e & ADDR_MASK;
}

int vm_protect_page(struct vm_space *vm, uint64_t va, int flags)
{
	uint64_t *pte = walk(vm, va, 0);
	if (!pte || !(*pte & PTE_P))
		return 0;
	uint64_t keep = *pte & (ADDR_MASK | PTE_SHARED | PTE_PCD | PTE_PWT | PTE_COW);
	*pte = keep | (pte_bits(flags) & ~(PTE_SHARED | PTE_PCD | PTE_PWT));
	if (*pte & PTE_COW)
		*pte &= ~PTE_W; /* reste en copie : l'écriture copiera */
	flush_local(vm, va);
	return 1;
}

int vm_page_flags(struct vm_space *vm, uint64_t va)
{
	if (va >= vm_user_top)
		return 0;
	uint64_t *pte = walk(vm, va, 0);
	return pte ? pte_flags(*pte) : 0;
}

uint64_t vm_translate(struct vm_space *vm, uint64_t va)
{
	if (va >= vm_user_top)
		return 0;
	uint64_t *pte = walk(vm, va, 0);
	if (!pte || !(*pte & PTE_P))
		return 0;
	return (*pte & ADDR_MASK) | (va & 0xFFF);
}

static void free_level(uint64_t phys, int level)
{
	uint64_t *t = table(phys);
	for (int i = 0; i < 512; i++) {
		if (!(t[i] & PTE_P))
			continue;
		if (level > 1)
			free_level(t[i], level - 1);
		else if (!(t[i] & PTE_SHARED))
			pmm_free(t[i] & ADDR_MASK);
	}
	pmm_free(phys & ADDR_MASK);
}

void vm_destroy(struct vm_space *vm)
{
	if (!vm)
		return;
	if (read_cr3() == vm->pml4)
		write_cr3(paging_kernel_pml4());
	uint64_t *t = table(vm->pml4);
	for (int i = 0; i < 256; i++)
		if (t[i] & PTE_P)
			free_level(t[i], 3);
	pmm_free(vm->pml4);
	kfree(vm);
}

void vm_activate(struct vm_space *vm)
{
	uint64_t want = vm ? vm->pml4 : paging_kernel_pml4();
	if (read_cr3() != want)
		write_cr3(want);
}

void smp_tlb_poll(void); /* (arch/pc/smp.c) */

void vm_tlb_poll(void)
{
	smp_tlb_poll();
}

void vm_tlb_shootdown(struct vm_space *vm)
{
	if (read_cr3() == vm->pml4)
		write_cr3(vm->pml4);
	smp_tlb_shootdown(vm->pml4);
}

int vm_fork(struct vm_space *parent, struct vm_space *child)
{
	for (uint64_t va = vm_next_candidate(parent, 0, vm_user_top); va < vm_user_top;
	     va = vm_next_candidate(parent, va + PAGE_SIZE, vm_user_top)) {
		uint64_t *pte = walk(parent, va, 0);
		if (!pte || !(*pte & PTE_P))
			continue;
		uint64_t *cpte = walk(child, va, 1);
		if (!cpte)
			return -12;
		if (!(*pte & PTE_SHARED)) {
			if (*pte & (PTE_W | PTE_COW))
				*pte = (*pte & ~PTE_W) | PTE_COW;
			pmm_ref(*pte & ADDR_MASK);
		}
		*cpte = *pte;
	}
	return 0;
}

int vm_cow_break_keep(struct vm_space *vm, uint64_t va, uint64_t *old_out);

int vm_cow_break(struct vm_space *vm, uint64_t va)
{
	uint64_t old = 0;
	int r = vm_cow_break_keep(vm, va, &old);
	if (old)
		pmm_free(old);
	return r;
}

/* comme vm_cow_break, mais une page copiée : l'ancienne n'est pas rendue, *old_out la donne (à
 * rendre par pmm_free après l'invalidation des TLB des autres cœurs) */
int vm_cow_break_keep(struct vm_space *vm, uint64_t va, uint64_t *old_out)
{
	*old_out = 0;
	uint64_t *pte = walk(vm, va, 0);
	if (!pte || !(*pte & PTE_P) || !(*pte & PTE_COW))
		return -1;
	uint64_t old = *pte & ADDR_MASK;
	if (pmm_extra_refs(old) == 0) { /* dernier détenteur : inscriptible sur place */
		*pte = (*pte & ~PTE_COW) | PTE_W;
	} else {
		uint64_t n = pmm_alloc();
		if (!n)
			return -1;
		memcpy(arch_phys_to_virt(n), arch_phys_to_virt(old), PAGE_SIZE);
		*pte = (*pte & ~(ADDR_MASK | PTE_COW)) | n | PTE_W;
		*old_out = old; /* une référence de moins, plus tard */
		flush_local(vm, va);
		return 1; /* (copiée : les autres cœurs peuvent encore lire l'ancienne) */
	}
	flush_local(vm, va);
	return 0;
}

uint64_t vm_table_pages(struct vm_space *vm)
{
	return vm->tables;
}

/* première adresse >= va (et < end) qui peut avoir une page : saute les
 * zones dont la table de niveau supérieur est absente */
uint64_t vm_next_candidate(struct vm_space *vm, uint64_t va, uint64_t end)
{
	while (va < end) {
		uint64_t *t = table(vm->pml4);
		if (!(t[(va >> 39) & 511] & PTE_P)) {
			va = (va | ((1ULL << 39) - 1)) + 1;
			continue;
		}
		t = table(t[(va >> 39) & 511]);
		if (!(t[(va >> 30) & 511] & PTE_P)) {
			va = (va | ((1ULL << 30) - 1)) + 1;
			continue;
		}
		t = table(t[(va >> 30) & 511]);
		if (!(t[(va >> 21) & 511] & PTE_P)) {
			va = (va | ((1ULL << 21) - 1)) + 1;
			continue;
		}
		return va;
	}
	return end;
}
