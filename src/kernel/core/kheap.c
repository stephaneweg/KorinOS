/* Tas du noyau. Chaque bloc commence par un en-tête de 16 octets :
 *   petits blocs (<= 2 Ko en tout) : classes de 32 à 2048 octets, découpées
 *   dans des pages entières, listes de blocs libres par classe ;
 *   gros blocs : pages physiques contiguës, rendues à kfree.
 * Portable : la mémoire est vue par arch_phys_to_virt(). */
#include <korin/arch.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/spinlock.h>
#include <korin/pmm.h>

static spinlock_t heap_lock = SPINLOCK_INIT;

#define MAGIC 0x4B48454Bu /* "KEHK" */
#define CLASSES 7
#define BIG 0xFFu

struct hdr {
	uint32_t magic;
	uint32_t cls;
	uint64_t reserved;   /* gros bloc : voir struct big */
};

struct big {
	uint32_t magic;
	uint32_t cls;
	uint32_t pages;
	uint32_t phys_page; /* adresse physique / 4096 */
};

_Static_assert(sizeof(struct hdr) == 16 && sizeof(struct big) == 16, "en-tête de 16 octets");

static void *free_list[CLASSES];

static uint32_t class_size(int c)
{
	return 32u << c;
}

void *kmalloc(size_t size)
{
	size_t need = size + 16;
	uint64_t f = spin_lock_irqsave(&heap_lock);
	void *ret = 0;
	if (need <= 2048) {
		int c = 0;
		while (class_size(c) < need)
			c++;
		if (!free_list[c]) {
			uint64_t phys = pmm_alloc();
			if (!phys)
				goto out;
			uint8_t *page = arch_phys_to_virt(phys);
			for (uint32_t off = 0; off + class_size(c) <= PAGE_SIZE; off += class_size(c)) {
				void **blk = (void **)(page + off);
				*blk = free_list[c];
				free_list[c] = blk;
			}
		}
		struct hdr *h = free_list[c];
		free_list[c] = *(void **)h;
		h->magic = MAGIC;
		h->cls = (uint32_t)c;
		ret = h + 1;
	} else {
		uint64_t pages = ALIGN_UP(need, PAGE_SIZE) / PAGE_SIZE;
		uint64_t phys = pmm_alloc_contig(pages);
		if (!phys)
			goto out;
		struct big *b = arch_phys_to_virt(phys);
		b->magic = MAGIC;
		b->cls = BIG;
		b->pages = (uint32_t)pages;
		b->phys_page = (uint32_t)(phys / PAGE_SIZE);
		ret = b + 1;
	}
out:
	spin_unlock_irqrestore(&heap_lock, f);
	return ret;
}

void *kzalloc(size_t size)
{
	void *p = kmalloc(size);
	if (p)
		memset(p, 0, size);
	return p;
}

void kfree(void *p)
{
	if (!p)
		return;
	struct hdr *h = (struct hdr *)p - 1;
	if (h->magic != MAGIC)
		kpanic("kfree : bloc %p invalide", p);
	uint64_t f = spin_lock_irqsave(&heap_lock);
	h->magic = 0;
	if (h->cls == BIG) {
		struct big *b = (struct big *)h;
		uint64_t phys = (uint64_t)b->phys_page * PAGE_SIZE;
		for (uint32_t i = 0; i < b->pages; i++)
			pmm_free(phys + i * PAGE_SIZE);
	} else {
		*(void **)h = free_list[h->cls];
		free_list[h->cls] = h;
	}
	spin_unlock_irqrestore(&heap_lock, f);
}
