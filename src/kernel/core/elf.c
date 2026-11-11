/* Chargement d'un ELF64 : chaque segment PT_LOAD dans des pages neuves,
 * mises à zéro, copiées depuis l'image ; la partie au-delà du fichier (.bss)
 * en région paresseuse quand le process a une mémoire (mm). Portable. */
#include <korin/arch.h>
#include <korin/elf.h>
#include <korin/errno.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/vm.h>

struct __attribute__((packed)) ehdr {
	uint8_t ident[16];
	uint16_t type, machine;
	uint32_t version;
	uint64_t entry, phoff, shoff;
	uint32_t flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct __attribute__((packed)) phdr {
	uint32_t type, flags;
	uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2

int elf_load(struct vm_space *vm, struct mm *mm, const void *image, uint64_t size, uint64_t *entry,
	     uint64_t *end_out)
{
	const uint8_t *img = image;
	const struct ehdr *e = image;
	if (size < sizeof *e || memcmp(e->ident, "\x7f" "ELF", 4) || e->ident[4] != 2 ||
	    e->machine != arch_elf_machine() || e->type != 2 /* ET_EXEC */)
		return E_NOEXEC;
	if (e->phentsize < sizeof(struct phdr) || e->phoff + (uint64_t)e->phnum * e->phentsize > size)
		return E_NOEXEC;

	uint64_t top = 0;
	for (int i = 0; i < e->phnum; i++) {
		const struct phdr *ph = (const void *)(img + e->phoff + (uint64_t)i * e->phentsize);
		if (ph->type != PT_LOAD || ph->memsz == 0)
			continue;
		if (ph->filesz > ph->memsz || ph->offset + ph->filesz > size ||
		    ph->vaddr + ph->memsz < ph->vaddr || ph->vaddr + ph->memsz > HEAP_LIMIT || ph->vaddr < 0x10000)
			return E_NOEXEC;
		int flags = (ph->flags & PF_W ? VM_W : 0) | (ph->flags & PF_X ? VM_X : 0);
		uint64_t first = ALIGN_DOWN(ph->vaddr, PAGE_SIZE);
		uint64_t end = ALIGN_UP(ph->vaddr + ph->memsz, PAGE_SIZE);
		/* pages entièrement au-delà du fichier : .bss paresseux */
		uint64_t lazy = mm ? ALIGN_UP(ph->vaddr + ph->filesz, PAGE_SIZE) : end;
		if (lazy < first)
			lazy = first;
		if (end > top)
			top = end;
		for (uint64_t va = first; va < end && va < lazy; va += PAGE_SIZE) {
			uint64_t phys = vm_translate(vm, va);
			if (!phys) {
				phys = pmm_alloc();
				if (!phys)
					return E_NOMEM;
				memset(arch_phys_to_virt(phys), 0, PAGE_SIZE);
				int r = vm_map_page(vm, va, phys, flags);
				if (r) {
					pmm_free(phys);
					return r;
				}
				mm_count_owned(mm, 1);
			} else
				vm_map_page(vm, va, phys, VM_W | VM_X); /* page de deux segments : droits réunis */
			uint64_t lo = va > ph->vaddr ? va : ph->vaddr;
			uint64_t hi = va + PAGE_SIZE;
			if (hi > ph->vaddr + ph->filesz)
				hi = ph->vaddr + ph->filesz;
			if (lo < hi)
				memcpy((uint8_t *)arch_phys_to_virt(phys & ~(PAGE_SIZE - 1)) + (lo - va),
				       img + ph->offset + (lo - ph->vaddr), hi - lo);
		}
		if (lazy < end) {
			uint32_t prot = 1 | (ph->flags & PF_W ? 2 : 0) | (ph->flags & PF_X ? 4 : 0);
			int r = mm_add_region(mm, lazy, end, prot, VMK_BSS);
			if (r)
				return r;
		}
	}
	*entry = e->entry;
	if (end_out)
		*end_out = top;
	return 0;
}
