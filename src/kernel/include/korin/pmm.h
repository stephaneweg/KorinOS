/* Allocateur de pages physiques (4 Ko) par bitmap. */
#ifndef KORIN_PMM_H
#define KORIN_PMM_H

#include <stdint.h>

#define PAGE_SIZE 4096ULL

struct mem_region {
	uint64_t base;
	uint64_t size;
};

/* regions : mémoire libre, déjà privée de ce que le noyau occupe */
void pmm_init(const struct mem_region *regions, int count);
uint64_t pmm_alloc(void);          /* adresse physique, 0 si plus rien */
uint64_t pmm_alloc_contig(uint64_t pages); /* pages contiguës, 0 si aucune place */
void pmm_free(uint64_t phys);
uint64_t pmm_free_pages(void);
uint64_t pmm_total_pages(void);

#endif
