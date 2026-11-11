/* Espaces d'adresses x86-64 : une PML4 par process ; les entrées 256 à 511
 * (moitié haute : carte de la mémoire, noyau) sont recopiées de celle du
 * noyau et pointent sur les mêmes tables. */
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
#define PTE_PS (1ULL << 7)
#define PTE_NX (1ULL << 63)
#define ADDR_MASK 0x000FFFFFFFFFF000ULL

const uint64_t vm_user_top = 0x0000800000000000ULL;

struct vm_space {
	uint64_t pml4; /* physique */
};

static uint64_t *table(uint64_t phys)
{
	return arch_phys_to_virt(phys & ADDR_MASK);
}

static uint64_t new_table(void)
{
	uint64_t p = pmm_alloc();
	if (p)
		memset(arch_phys_to_virt(p), 0, PAGE_SIZE);
	return p;
}

struct vm_space *vm_create(void)
{
	struct vm_space *vm = kzalloc(sizeof *vm);
	if (!vm)
		return 0;
	vm->pml4 = new_table();
	if (!vm->pml4) {
		kfree(vm);
		return 0;
	}
	uint64_t *dst = table(vm->pml4), *src = table(paging_kernel_pml4());
	for (int i = 256; i < 512; i++)
		dst[i] = src[i];
	return vm;
}

/* entrée de la table de niveau inférieur, créée au besoin */
static uint64_t *next_level(uint64_t *t, int idx, int create)
{
	if (!(t[idx] & PTE_P)) {
		if (!create)
			return 0;
		uint64_t p = new_table();
		if (!p)
			return 0;
		t[idx] = p | PTE_P | PTE_W | PTE_U;
	}
	return table(t[idx]);
}

static uint64_t *walk(struct vm_space *vm, uint64_t va, int create)
{
	uint64_t *t = table(vm->pml4);
	t = next_level(t, (va >> 39) & 511, create);
	if (t)
		t = next_level(t, (va >> 30) & 511, create);
	if (t)
		t = next_level(t, (va >> 21) & 511, create);
	return t ? &t[(va >> 12) & 511] : 0;
}

int vm_map_page(struct vm_space *vm, uint64_t va, uint64_t phys, int flags)
{
	if (va >= vm_user_top)
		return -22;
	uint64_t *pte = walk(vm, va, 1);
	if (!pte)
		return -12;
	uint64_t e = (phys & ADDR_MASK) | PTE_P | PTE_U;
	if (flags & VM_W)
		e |= PTE_W;
	if (!(flags & VM_X))
		e |= PTE_NX;
	*pte = e;
	if (read_cr3() == vm->pml4)
		__asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
	return 0;
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
		else
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
