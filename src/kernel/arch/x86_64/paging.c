/* Tables de pages définitives du noyau x86-64 :
 *   0xFFFF800000000000 + p  ->  p     toute la mémoire physique (pages de 1 Go,
 *                                     sinon 2 Mo), framebuffer en écriture combinée
 *   0xFFFFFFFF80000000 + o  ->  bloc du noyau (pages de 4 Ko)
 * La moitié basse reste vide : elle servira aux process.
 * Les tables viennent d'une réserve dans le BSS du noyau, pour ne toucher à
 * aucune page de l'UEFI tant que ses tables sont encore en service. */
#include <korin/lib.h>
#include <korin/kheap.h>
#include <korin/pmm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>

#include "cpu.h"
#include "paging.h"

#define PTE_P (1ULL << 0)
#define PTE_W (1ULL << 1)
#define PTE_PWT (1ULL << 3)
#define PTE_PS (1ULL << 7)
#define PTE_G (1ULL << 8)
#define PTE_NX (1ULL << 63)

extern char __text_end[], __rodata_end[]; /* (kernel.ld) */

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

/* ---- piles du noyau avec pages de garde (E10) : une place de 32 Ko par pile, 16 Ko non projetés puis la pile
 * (KSTACK_SIZE, 16 Ko) ; un débordement touche la garde : faute de page, puis #DF sur sa pile IST, lisible, au
 * lieu d'écraser le tas. Une pile rendue garde ses pages et reste projetée (liste des libres) : rien n'est
 * jamais retiré, donc pas d'invalidation des TLB des autres cœurs */
#define KSTK_BASE 0xFFFFC00000000000ULL /* PML4[384] */
#define KSTK_SLOT (8 * 4096ULL)
#define KSTK_MAX 32768 /* (un PD : 1 Go) */
static uint64_t *kstk_pd;
static uint64_t kstk_nx;
static spinlock_t kstk_lock;
static uint32_t kstk_next;
static void *kstk_free;

void *arch_kstack_alloc(void)
{
	uint64_t f = spin_lock_irqsave(&kstk_lock);
	void *s = kstk_free;
	if (s) {
		kstk_free = *(void **)s;
		spin_unlock_irqrestore(&kstk_lock, f);
		return s;
	}
	if (!kstk_pd) { /* (avant paging_init) */
		spin_unlock_irqrestore(&kstk_lock, f);
		return kmalloc(KSTACK_SIZE);
	}
	if (kstk_next >= KSTK_MAX) {
		spin_unlock_irqrestore(&kstk_lock, f);
		return 0;
	}
	uint64_t va = KSTK_BASE + (uint64_t)kstk_next * KSTK_SLOT + KSTK_SLOT - KSTACK_SIZE;
	uint64_t pages[KSTACK_SIZE / 4096];
	unsigned n = 0;
	for (; n < KSTACK_SIZE / 4096; n++)
		if (!(pages[n] = pmm_alloc()))
			break;
	unsigned pdi = (unsigned)((va >> 21) & 511);
	if (n == KSTACK_SIZE / 4096 && !kstk_pd[pdi]) {
		uint64_t pt = pmm_alloc();
		if (pt) {
			memset(arch_phys_to_virt(pt), 0, 4096);
			kstk_pd[pdi] = pt | PTE_P | PTE_W;
		}
	}
	if (n < KSTACK_SIZE / 4096 || !kstk_pd[pdi]) {
		spin_unlock_irqrestore(&kstk_lock, f);
		while (n)
			pmm_free(pages[--n]);
		return 0;
	}
	kstk_next++;
	uint64_t *pt = arch_phys_to_virt(kstk_pd[pdi] & 0x000FFFFFFFFFF000ULL);
	for (unsigned i = 0; i < n; i++)
		pt[((va >> 12) & 511) + i] = pages[i] | PTE_P | PTE_W | PTE_G | kstk_nx;
	spin_unlock_irqrestore(&kstk_lock, f);
	return (void *)va;
}

void arch_kstack_free(void *s)
{
	if (!s)
		return;
	if ((uint64_t)s < KSTK_BASE || (uint64_t)s >= KSTK_BASE + KSTK_MAX * KSTK_SLOT) { /* (kmalloc : avant paging_init) */
		kfree(s);
		return;
	}
	uint64_t f = spin_lock_irqsave(&kstk_lock);
	*(void **)s = kstk_free;
	kstk_free = s;
	spin_unlock_irqrestore(&kstk_lock, f);
}

void paging_init(uint64_t kernel_phys, uint64_t kernel_virt, uint64_t kernel_size,
		 uint64_t cover, uint64_t fb_base, uint64_t fb_size)
{
	k_phys = kernel_phys;
	k_virt = kernel_virt;

	/* PAT : entrée 1 (PWT seul) = écriture combinée au lieu de write-through */
	wrmsr(MSR_PAT, KORIN_PAT);

	/* NX : la carte physique (données seulement : pas de services de l'UEFI, l'AML dans acpid, le
	 * trampoline des cœurs est recopié) ; les données du noyau. NXE posé avant write_cr3 (plus bas) et
	 * passé au trampoline (smp.c) : sans lui, le bit 63 est réservé */
	uint32_t r[4];
	cpuid(0x80000001, 0, r);
	int nx_ok = (r[3] >> 20) & 1;
	uint64_t nx = nx_ok ? PTE_NX : 0;

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
			pdpt[g] = gbase | PTE_P | PTE_W | PTE_PS | PTE_G | nx;
			continue;
		}
		uint64_t *pd = pool_page();
		for (int i = 0; i < 512; i++) {
			uint64_t a = gbase + (uint64_t)i * MIB2;
			pd[i] = a | PTE_P | PTE_W | PTE_PS | PTE_G | nx;
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
	/* .text en lecture et exécution, .rodata en lecture (NX), le reste en écriture (NX) : une écriture
	 * dans le code du noyau, ou un saut dans ses données, fait une faute (CR0.WP : cpu_init) */
	uint64_t text_end = ((uint64_t)__text_end - kernel_virt) / 4096;
	uint64_t ro_end = ((uint64_t)__rodata_end - kernel_virt) / 4096;
	for (uint64_t i = 0; i < pages; i += 512) {
		uint64_t *pt = pool_page();
		for (uint64_t j = 0; j < 512 && i + j < pages; j++) {
			uint64_t k = i + j;
			uint64_t prot = k < text_end ? 0 : k < ro_end ? nx : PTE_W | nx;
			pt[j] = (kernel_phys + k * 4096) | PTE_P | PTE_G | prot;
		}
		kpd[((kernel_virt >> 21) & 511) + i / 512] = kv2p(pt) | PTE_P | PTE_W;
	}

	/* les piles du noyau (E10) : PML4[384] -> un PDPT -> un PD (1 Go) ; les PT viennent plus tard
	 * (arch_kstack_alloc), l'entrée PML4 est là dès maintenant : les espaces des process la recopient */
	uint64_t *spdpt = pool_page();
	kstk_pd = pool_page();
	pml4[384] = kv2p(spdpt) | PTE_P | PTE_W;
	spdpt[0] = kv2p(kstk_pd) | PTE_P | PTE_W;
	kstk_nx = nx;

	/* NX : les pages des process sans droit d'exécution, et celles du noyau ci-dessus */
	if (nx_ok)
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
