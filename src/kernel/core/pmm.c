/* Allocateur de pages physiques : un bit par page de 4 Ko (1 = occupée).
 * Le bitmap est pris dans la première région libre assez grande. Portable :
 * il accède à la mémoire par arch_phys_to_virt().
 * Vérifications (toujours, peu coûteuses) : un second bitmap marque les pages du tas du noyau
 * (kheap) ; pmm_free et pmm_ref d'une page du tas, pmm_ref d'une page libre, une page donnée qui
 * a encore des détenteurs arrêtent le noyau en donnant la pile d'appel (la première erreur, pas
 * sa conséquence lointaine « page déjà libre »). */
#include <korin/arch.h>
#include <korin/lib.h>
#include <korin/spinlock.h>
#include <korin/pmm.h>

static spinlock_t pmm_lock = SPINLOCK_INIT;

static uint64_t *bitmap;
static uint16_t *extra; /* détenteurs en plus du premier (fork) */
static uint64_t *heapmap; /* pages du tas du noyau (kheap) : rendues seulement par kfree */
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
	uint64_t map_bytes = ALIGN_UP((bitmap_pages + 63) / 64 * 8, PAGE_SIZE);
	uint64_t bytes = 2 * map_bytes + ALIGN_UP(bitmap_pages * 2, PAGE_SIZE);

	uint64_t where = 0;
	for (int i = 0; i < count && !where; i++)
		if (regions[i].size >= bytes)
			where = regions[i].base;
	if (!where)
		kpanic("pas de place pour le bitmap des pages (%lu octets)", bytes);

	bitmap = arch_phys_to_virt(where);
	memset(bitmap, 0xFF, map_bytes);
	heapmap = (uint64_t *)((uint8_t *)bitmap + map_bytes);
	extra = (uint16_t *)((uint8_t *)heapmap + map_bytes);
	memset(heapmap, 0, bytes - map_bytes);
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

static int is_heap(uint64_t page)
{
	return (heapmap[page >> 6] >> (page & 63)) & 1;
}

/* une erreur d'usage des pages : la pile, puis l'arrêt */
static __attribute__((noinline)) void misuse(const char *what, uint64_t phys)
{
	uint64_t *fp = __builtin_frame_address(0);
	kprintf("\npmm : %s (%lx), pile :", what, phys);
	for (int i = 0; i < 8 && fp && ((uint64_t)fp >> 47) && !((uint64_t)fp & 7); i++) {
		kprintf(" %lx", fp[1]);
		fp = (uint64_t *)fp[0];
	}
	kprintf("\n");
	kpanic("pmm : %s (%lx)", what, phys);
}

/* kheap : pages qui deviennent (on) ou cessent d'être (off) des pages du tas */
void pmm_heap_mark(uint64_t phys, uint64_t pages, int on)
{
	uint64_t f = spin_lock_irqsave(&pmm_lock);
	for (uint64_t i = 0; i < pages; i++) {
		uint64_t page = phys / PAGE_SIZE + i;
		if (page >= bitmap_pages)
			break;
		if (on)
			heapmap[page >> 6] |= 1ULL << (page & 63);
		else
			heapmap[page >> 6] &= ~(1ULL << (page & 63));
	}
	spin_unlock_irqrestore(&pmm_lock, f);
}

static void free_one(uint64_t phys)
{
	uint64_t page = phys / PAGE_SIZE;
	if (page >= bitmap_pages || !is_used(page))
		misuse("pmm_free : page déjà libre ou hors carte", phys);
	if (is_heap(page))
		misuse("pmm_free : une page du tas du noyau rendue hors de kfree", phys);
	if (extra[page]) { /* encore un détenteur */
		extra[page]--;
		return;
	}
	set_free(page);
	nfree++;
	if (page / 64 < hint)
		hint = page / 64;
}

uint64_t pmm_alloc(void)
{
	uint64_t f = spin_lock_irqsave(&pmm_lock);
	uint64_t p = alloc_one();
	if (p && extra[p / PAGE_SIZE])
		misuse("pmm_alloc : une page libre avait encore des détenteurs", p);
	spin_unlock_irqrestore(&pmm_lock, f);
	return p;
}

uint64_t pmm_alloc_contig(uint64_t pages)
{
	uint64_t f = spin_lock_irqsave(&pmm_lock);
	uint64_t p = alloc_contig(pages);
	spin_unlock_irqrestore(&pmm_lock, f);
	return p;
}

void pmm_free(uint64_t phys)
{
	uint64_t f = spin_lock_irqsave(&pmm_lock);
	free_one(phys);
	spin_unlock_irqrestore(&pmm_lock, f);
}

void pmm_ref(uint64_t phys)
{
	uint64_t f = spin_lock_irqsave(&pmm_lock);
	uint64_t page = phys / PAGE_SIZE;
	if (page >= bitmap_pages || !is_used(page))
		misuse("pmm_ref : une page libre ou hors carte", phys);
	if (is_heap(page))
		misuse("pmm_ref : une page du tas du noyau", phys);
	extra[page]++;
	spin_unlock_irqrestore(&pmm_lock, f);
}

unsigned pmm_extra_refs(uint64_t phys)
{
	return extra[phys / PAGE_SIZE];
}

uint64_t pmm_free_pages(void)
{
	return nfree;
}

uint64_t pmm_total_pages(void)
{
	return total;
}
