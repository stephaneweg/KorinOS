#ifndef KORIN_X86_PAGING_H
#define KORIN_X86_PAGING_H

#include <stdint.h>

#define PHYS_MAP_BASE 0xFFFF800000000000ULL

/* cover : octets de mémoire physique à rendre visibles (arrondi au Go) */
void paging_init(uint64_t kernel_phys, uint64_t kernel_virt, uint64_t kernel_size,
		 uint64_t cover, uint64_t fb_base, uint64_t fb_size);
int paging_pool_used(void);

#endif
