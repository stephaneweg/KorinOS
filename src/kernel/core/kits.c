/* Kits (bibliothèques partagées), comme src/kernel/kits.asm du Pi :
 *   un kit est un ELF64 lié à 0 : un segment lecture + exécution (code, table
 *   d'exportation : « AKIT », version, décalages), un segment lecture +
 *   écriture (variables) sur d'autres pages ; code sans adresse absolue ;
 *   chargé une fois pour tout le système à une place fixe, KIT_BASE + n x 64 Mo,
 *   la même dans tous les process ; dans chaque process, le code est mappé
 *   partagé et les variables recopiées dans des pages à lui.
 * KIT_REGISTER (nom, image, taille) : un kit lu par AppKit (droits de
 * SD0:/sys) ; LIB_OPEN (nom, version minimale) : un kit déjà en mémoire, ou
 * « <nom>.so » de l'initrd. Le noyau ne lit aucun fichier. Portable. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/initrd.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/vm.h>

#define KIT_MAX 16
#define KIT_BASE 0x5000000000ULL
#define KIT_SLOT 0x4000000ULL /* 64 Mo */
#define KIT_NAME 24
#define KIT_MAGIC 0x54494B41u /* « AKIT » */

struct kit {
	char name[KIT_NAME];
	int used;
	uint64_t rx_phys, rx_size;  /* pages contiguës du code */
	uint64_t rw_off, rw_size;   /* variables : décalage dans la place, octets (pages) */
	uint8_t *rw_init;           /* copie initiale des variables */
	uint64_t entry;             /* décalage de la table d'exportation */
	uint32_t version;
};

static struct kit kits[KIT_MAX];
static spinlock_t kit_lock = SPINLOCK_INIT;

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

static int find(const char *name)
{
	for (int i = 0; i < KIT_MAX; i++)
		if (kits[i].used && !strcmp(kits[i].name, name))
			return i;
	return -1;
}

/* charge l'image dans une place libre ; indice ou erreur */
static int load(const char *name, const uint8_t *img, uint64_t size)
{
	const struct ehdr *e = (const void *)img;
	if (size < sizeof *e || memcmp(e->ident, "\x7f" "ELF", 4) || e->ident[4] != 2 ||
	    e->machine != arch_elf_machine() || e->phentsize < sizeof(struct phdr) ||
	    e->phoff + (uint64_t)e->phnum * e->phentsize > size)
		return E_NOEXEC;
	struct kit k = {0};
	memcpy(k.name, name, KIT_NAME);
	k.entry = e->entry;
	for (int i = 0; i < e->phnum; i++) {
		const struct phdr *ph = (const void *)(img + e->phoff + (uint64_t)i * e->phentsize);
		if (ph->type != 1 || !ph->memsz)
			continue;
		if (ph->offset + ph->filesz > size || ph->filesz > ph->memsz || ph->vaddr + ph->memsz > KIT_SLOT)
			return E_NOEXEC;
		uint64_t first = ALIGN_DOWN(ph->vaddr, PAGE_SIZE), end = ALIGN_UP(ph->vaddr + ph->memsz, PAGE_SIZE);
		uint8_t *dst;
		if (!(ph->flags & 2)) { /* code (et lecture seule) : pages partagées */
			if (k.rx_size)
				return E_NOEXEC;
			k.rx_size = end; /* depuis 0 : les en-têtes avec */
			k.rx_phys = pmm_alloc_contig(end / PAGE_SIZE);
			if (!k.rx_phys)
				return E_NOMEM;
			dst = arch_phys_to_virt(k.rx_phys);
			memset(dst, 0, end);
			memcpy(dst + ph->vaddr, img + ph->offset, ph->filesz);
		} else { /* variables : copie initiale */
			if (k.rw_size)
				return E_NOEXEC;
			k.rw_off = first;
			k.rw_size = end - first;
			k.rw_init = kzalloc(k.rw_size);
			if (!k.rw_init)
				return E_NOMEM;
			memcpy(k.rw_init + (ph->vaddr - first), img + ph->offset, ph->filesz);
		}
	}
	if (!k.rx_size || k.entry + 8 > k.rx_size)
		return E_NOEXEC;
	const uint32_t *tab = (const uint32_t *)((uint8_t *)arch_phys_to_virt(k.rx_phys) + k.entry);
	if (tab[0] != KIT_MAGIC)
		return E_NOEXEC;
	k.version = tab[1];
	if (k.rw_size && k.rw_off < k.rx_size)
		return E_NOEXEC;
	k.used = 1;
	uint64_t f = spin_lock_irqsave(&kit_lock);
	int slot = find(name);
	if (slot < 0)
		for (int i = 0; i < KIT_MAX && slot < 0; i++)
			if (!kits[i].used) {
				kits[i] = k;
				slot = i;
				k.used = 0; /* gardé */
			}
	spin_unlock_irqrestore(&kit_lock, f);
	if (k.used) { /* déjà là (un autre l'a chargé) ou plus de place : rendu */
		for (uint64_t i = 0; i < k.rx_size / PAGE_SIZE; i++)
			pmm_free(k.rx_phys + i * PAGE_SIZE);
		kfree(k.rw_init);
	}
	return slot >= 0 ? slot : E_NOSPC;
}

/* mappe le kit chez le process courant (une fois) ; adresse de la table */
static int64_t map_into(int i)
{
	struct process *p = sched_current()->proc;
	if (!p->vm)
		return E_INVAL;
	struct kit *k = &kits[i];
	uint64_t base = KIT_BASE + (uint64_t)i * KIT_SLOT;
	if (!(vm_page_flags(p->vm, base) & VM_PRESENT)) {
		for (uint64_t off = 0; off < k->rx_size; off += PAGE_SIZE)
			if (vm_map_page(p->vm, base + off, k->rx_phys + off, VM_X | VM_SHARED))
				return E_NOMEM;
		for (uint64_t off = 0; off < k->rw_size; off += PAGE_SIZE) {
			uint64_t pg = pmm_alloc();
			if (!pg)
				return E_NOMEM;
			memcpy(arch_phys_to_virt(pg), k->rw_init + off, PAGE_SIZE);
			if (vm_map_page(p->vm, base + k->rw_off + off, pg, VM_W)) {
				pmm_free(pg);
				return E_NOMEM;
			}
			mm_count_owned(p->mm, 1);
		}
	}
	return (int64_t)(base + k->entry);
}

int64_t sys_kit_register(uint64_t uname, uint64_t image, uint64_t size)
{
	if (!sched_current()->proc->rights)
		return E_PERM;
	char name[KIT_NAME];
	if (copy_string_from_user(name, uname, KIT_NAME) <= 0)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&kit_lock);
	int i = find(name);
	spin_unlock_irqrestore(&kit_lock, f);
	if (i < 0) {
		if (size < 64 || size > (KIT_SLOT))
			return E_INVAL;
		uint8_t *img = kmalloc(size);
		if (!img)
			return E_NOMEM;
		int r = copy_from_user(img, image, size);
		i = r ? r : load(name, img, size);
		kfree(img);
		if (i < 0)
			return i;
	}
	return map_into(i);
}

int64_t sys_lib_open(uint64_t uname, uint64_t vmin)
{
	char name[KIT_NAME];
	if (copy_string_from_user(name, uname, KIT_NAME) <= 0)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&kit_lock);
	int i = find(name);
	spin_unlock_irqrestore(&kit_lock, f);
	if (i < 0) { /* un kit de l'initrd (appkit.so, comme le module du Pi) */
		char file[KIT_NAME + 4];
		ksnprintf(file, sizeof file, "%s.so", name);
		const void *img;
		uint64_t size;
		if (initrd_find(file, &img, &size))
			return E_NOENT;
		i = load(name, img, size);
		if (i < 0)
			return i;
	}
	if (vmin && kits[i].version < vmin)
		return E_INVAL;
	return map_into(i);
}
