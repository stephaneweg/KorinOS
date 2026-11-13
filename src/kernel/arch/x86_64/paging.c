/* Tables de pages définitives du noyau x86-64 :
 *   0xFFFF800000000000 + p  ->  p     toute la mémoire physique (pages de 1 Go,
 *                                     sinon 2 Mo), framebuffer en écriture combinée
 *   0xFFFFFFFF80000000 + o  ->  bloc du noyau (pages de 4 Ko)
 * La moitié basse reste vide : elle servira aux process.
 * Les tables viennent d'une réserve dans le BSS du noyau, pour ne toucher à
 * aucune page de l'UEFI tant que ses tables sont encore en service. */
#include <korin/lib.h>

#include "cpu.h"
#include "paging.h"

#define PTE_P (1ULL << 0)
#define PTE_W (1ULL << 1)
#define PTE_PWT (1ULL << 3)
#define PTE_PS (1ULL << 7)
#define PTE_G (1ULL << 8)

#define POOL_PAGES 96
#define GIB (1ULL << 30)
#define MIB2 (1ULL << 21)

static uint64_t pool[POOL_PAGES][512] __attribute__((aligned(4096)));
static int pool_used;
static uint64_t k_phys, k_virt;
static uint64_t kernel_pml4;
static int ready;

static uint64_t *pool_page(void)
{
	if (pool_used >= POOL_PAGES)
		kpanic("paging : réserve de tables épuisée");
	return pool[pool_used++];
}

/* adresse physique d'une table de la réserve (le BSS est dans le bloc du noyau) */
static uint64_t kv2p(const void *v)
{
	return (uint64_t)v - k_virt + k_phys;
}

void *arch_phys_to_virt(uint64_t phys)
{
	if (!ready)
		return (void *)phys; /* identité de l'UEFI, avant le changement de tables */
	return (void *)(phys + PHYS_MAP_BASE);
}

static int has_1g_pages(void)
{
	uint32_t r[4];
	cpuid(0x80000000, 0, r);
	if (r[0] < 0x80000001)
		return 0;
	cpuid(0x80000001, 0, r);
	return (r[3] >> 26) & 1;
}

void paging_init(uint64_t kernel_phys, uint64_t kernel_virt, uint64_t kernel_size,
		 uint64_t cover, uint64_t fb_base, uint64_t fb_size)
{
	k_phys = kernel_phys;
	k_virt = kernel_virt;

	/* PAT : entrée 1 (PWT seul) = écriture combinée au lieu de write-through */
	wrmsr(MSR_PAT, KORIN_PAT);

	uint64_t *pml4 = pool_page();
	uint64_t *pdpt = pool_page();
	pml4[256] = kv2p(pdpt) | PTE_P | PTE_W;

	cover = ALIGN_UP(cover, GIB);
	if (cover > 512 * GIB)
		cover = 512 * GIB;
	int big = has_1g_pages();
	uint64_t fb_first = ALIGN_DOWN(fb_base, MIB2), fb_end = ALIGN_UP(fb_base + fb_size, MIB2);

	for (uint64_t g = 0; g < cover / GIB; g++) {
		uint64_t gbase = g * GIB;
		int has_fb = fb_size && fb_first < gbase + GIB && fb_end > gbase;
		if (big && !has_fb) {
			pdpt[g] = gbase | PTE_P | PTE_W | PTE_PS | PTE_G;
			continue;
		}
		uint64_t *pd = pool_page();
		for (int i = 0; i < 512; i++) {
			uint64_t a = gbase + (uint64_t)i * MIB2;
			pd[i] = a | PTE_P | PTE_W | PTE_PS | PTE_G;
			if (fb_size && a >= fb_first && a < fb_end)
				pd[i] |= PTE_PWT; /* PAT 1 : écriture combinée */
		}
		pdpt[g] = kv2p(pd) | PTE_P | PTE_W;
	}

	/* noyau : PML4[511] -> PDPT[510] -> PD -> PT */
	uint64_t *kpdpt = pool_page();
	uint64_t *kpd = pool_page();
	pml4[511] = kv2p(kpdpt) | PTE_P | PTE_W;
	kpdpt[510] = kv2p(kpd) | PTE_P | PTE_W;
	uint64_t pages = kernel_size / 4096;
	for (uint64_t i = 0; i < pages; i += 512) {
		uint64_t *pt = pool_page();
		for (uint64_t j = 0; j < 512 && i + j < pages; j++)
			pt[j] = (kernel_phys + (i + j) * 4096) | PTE_P | PTE_W | PTE_G;
		kpd[((kernel_virt >> 21) & 511) + i / 512] = kv2p(pt) | PTE_P | PTE_W;
	}

	/* NX : les pages des process sans droit d'exécution */
	uint32_t r[4];
	cpuid(0x80000001, 0, r);
	if ((r[3] >> 20) & 1)
		wrmsr(MSR_EFER, rdmsr(MSR_EFER) | (1ULL << 11));

	kernel_pml4 = kv2p(pml4);
	write_cr3(kernel_pml4);
	ready = 1;
}

uint64_t paging_kernel_pml4(void)
{
	return kernel_pml4;
}

int paging_pool_used(void)
{
	return pool_used;
}
