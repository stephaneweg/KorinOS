/* Allocateur de pages physiques : un bit par page de 4 Ko (1 = occupée).
 * Le bitmap est pris dans la première région libre assez grande. Portable :
 * il accède à la mémoire par arch_phys_to_virt(). */
#include <korin/arch.h>
#include <korin/lib.h>
#include <korin/pmm.h>

static uint64_t *bitmap;
static uint64_t bitmap_pages; /* pages couvertes par le bitmap */
static uint64_t total, nfree;
static uint64_t hint;         /* premier mot où chercher */

static void set_used(uint64_t page)
{
	bitmap[page >> 6] |= 1ULL << (page & 63);
}

static void set_free(uint64_t page)
{
	bitmap[page >> 6] &= ~(1ULL << (page & 63));
}

static int is_used(uint64_t page)
{
	return (bitmap[page >> 6] >> (page & 63)) & 1;
}

void pmm_init(const struct mem_region *regions, int count)
{
	uint64_t top = 0;
	for (int i = 0; i < count; i++)
		if (regions[i].base + regions[i].size > top)
			top = regions[i].base + regions[i].size;
	bitmap_pages = top / PAGE_SIZE;
	uint64_t bytes = ALIGN_UP((bitmap_pages + 63) / 64 * 8, PAGE_SIZE);

	uint64_t where = 0;
	for (int i = 0; i < count && !where; i++)
		if (regions[i].size >= bytes)
			where = regions[i].base;
	if (!where)
		kpanic("pas de place pour le bitmap des pages (%lu octets)", bytes);

	bitmap = arch_phys_to_virt(where);
	memset(bitmap, 0xFF, bytes);
	for (int i = 0; i < count; i++) {
		uint64_t first = ALIGN_UP(regions[i].base, PAGE_SIZE) / PAGE_SIZE;
		uint64_t end = ALIGN_DOWN(regions[i].base + regions[i].size, PAGE_SIZE) / PAGE_SIZE;
		for (uint64_t p = first; p < end; p++) {
			set_free(p);
			total++;
			nfree++;
		}
	}
	for (uint64_t p = where / PAGE_SIZE; p < (where + bytes) / PAGE_SIZE; p++) {
		set_used(p);
		nfree--;
	}
}

static uint64_t alloc_one(void)
{
	uint64_t words = (bitmap_pages + 63) / 64;
	for (uint64_t w = hint; w < words; w++) {
		if (bitmap[w] == ~0ULL)
			continue;
		uint64_t page = w * 64 + (uint64_t)__builtin_ctzll(~bitmap[w]);
		if (page >= bitmap_pages)
			break;
		set_used(page);
		nfree--;
		hint = w;
		return page * PAGE_SIZE;
	}
	return 0;
}

static uint64_t alloc_contig(uint64_t pages)
{
	if (pages == 0)
		return 0;
	if (pages == 1)
		return alloc_one();
	uint64_t run = 0;
	for (uint64_t p = hint * 64; p < bitmap_pages; p++) {
		if (is_used(p)) {
			run = 0;
			continue;
		}
		if (++run == pages) {
			uint64_t first = p + 1 - pages;
			for (uint64_t q = first; q <= p; q++)
				set_used(q);
			nfree -= pages;
			return first * PAGE_SIZE;
		}
	}
	return 0;
}

static void free_one(uint64_t phys)
{
	uint64_t page = phys / PAGE_SIZE;
	if (page >= bitmap_pages || !is_used(page))
		kpanic("pmm_free : page %lx déjà libre ou hors carte", phys);
	set_free(page);
	nfree++;
	if (page / 64 < hint)
		hint = page / 64;
}

uint64_t pmm_alloc(void)
{
	uint64_t f = arch_irq_save();
	uint64_t p = alloc_one();
	arch_irq_restore(f);
	return p;
}

uint64_t pmm_alloc_contig(uint64_t pages)
{
	uint64_t f = arch_irq_save();
	uint64_t p = alloc_contig(pages);
	arch_irq_restore(f);
	return p;
}

void pmm_free(uint64_t phys)
{
	uint64_t f = arch_irq_save();
	free_one(phys);
	arch_irq_restore(f);
}

uint64_t pmm_free_pages(void)
{
	return nfree;
}

uint64_t pmm_total_pages(void)
{
	return total;
}
