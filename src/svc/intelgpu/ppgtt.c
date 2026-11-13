/* ppgtt.c : les espaces d'adresses des clients (PPGTT à 4 niveaux, 48 bits),
 * docs/etude/GPU-INTEL.md § 4.
 *
 * Tables en mémoire du système (DMA_ALLOC du service, par blocs de 2 Mo,
 * pages recyclées). Entrée de Gen12 : bit 0 présente, bit 1 écriture,
 * adresse 51:12, index PAT (bits 3, 4, 7) à 0 : WB. Tout trou pointe vers
 * une chaîne de pages de garde (une page à zéro, une table de pages, un
 * répertoire, un PDP, chacun plein de l'entrée du niveau du dessous) : une
 * lecture ou une écriture du GPU hors des BO tombe dans la page de garde au
 * lieu de faire une faute (comme i915). Les clients ne voient jamais leurs
 * tables. */
#include "igpu.h"

#define BLOCK_PAGES 512
#define MAX_BLOCKS 64

struct vm vms[VM_MAX];

static struct {
	u64 phys;
	u8 *va;
} blocks[MAX_BLOCKS];
static int nblocks;
static u64 *free_stack; /* pages libres (adresses physiques) */
static long nfree, cap_free;
static u64 used_pages;
static u64 scratch[4]; /* 0 la page, 1 la table de pages, 2 le répertoire, 3 le PDP : adresses physiques */

void *pt_va(u64 phys)
{
	for (int i = 0; i < nblocks; i++)
		if (phys >= blocks[i].phys && phys < blocks[i].phys + BLOCK_PAGES * 4096ull)
			return blocks[i].va + (phys - blocks[i].phys);
	return 0;
}

u64 pt_pages_used(void)
{
	return used_pages;
}

u64 scratch_phys(void)
{
	return scratch[0];
}

static int grow(void)
{
	if (nblocks == MAX_BLOCKS)
		return -1;
	u64 phys;
	long va = korin_sys2(SYS_DMA_ALLOC, BLOCK_PAGES * 4096ul, &phys);
	if (va < 0)
		return -1;
	blocks[nblocks].phys = phys;
	blocks[nblocks].va = (u8 *)va;
	nblocks++;
	if (G.simu)
		simu_note(phys, (void *)va, BLOCK_PAGES * 4096ull);
	if (nfree + BLOCK_PAGES > cap_free) {
		long nc = cap_free + BLOCK_PAGES * 4;
		u64 *ns = ak_malloc((u64)nc * 8);
		if (!ns)
			return -1;
		if (free_stack)
			memcpy(ns, free_stack, (u64)nfree * 8);
		free_stack = ns; /* (l'ancienne pile est perdue : quelques Ko, rarement) */
		cap_free = nc;
	}
	for (long i = BLOCK_PAGES - 1; i >= 0; i--)
		free_stack[nfree++] = phys + (u64)i * 4096;
	return 0;
}

static u64 page_get(void)
{
	if (!nfree && grow())
		return 0;
	u64 p = free_stack[--nfree];
	memset(pt_va(p), 0, 4096);
	used_pages++;
	return p;
}

static void page_put(u64 p)
{
	free_stack[nfree++] = p;
	used_pages--;
}

static u64 pde(u64 phys)
{
	return (phys & PTE_ADDR) | PTE_PRESENT | PTE_RW;
}

/* une table neuve dont toutes les entrées pointent vers la garde du niveau du dessous */
static u64 table_new(int below)
{
	u64 p = page_get();
	if (!p)
		return 0;
	u64 *t = pt_va(p), e = pde(scratch[below]);
	for (int i = 0; i < 512; i++)
		t[i] = e;
	return p;
}

int pt_init(void)
{
	scratch[0] = page_get(); /* à zéro, en écriture : les écritures perdues y vont */
	if (!scratch[0])
		return -1;
	for (int l = 1; l < 4; l++) {
		scratch[l] = table_new(l - 1);
		if (!scratch[l])
			return -1;
	}
	return 0;
}

int vm_create(int pid)
{
	for (int v = 1; v < VM_MAX; v++)
		if (!vms[v].used) {
			u64 p = table_new(3);
			if (!p)
				return E_NOMEM;
			vms[v].used = 1;
			vms[v].owner = pid;
			vms[v].pml4 = p;
			vms[v].tables = 1;
			return v;
		}
	return E_NOMEM;
}

static void free_level(u64 table, int level, long *n)
{
	u64 *t = pt_va(table);
	if (level > 1)
		for (int i = 0; i < 512; i++) {
			u64 e = t[i] & PTE_ADDR;
			if (e != scratch[level - 1])
				free_level(e, level - 1, n);
		}
	page_put(table);
	(*n)++;
}

void vm_destroy(int v)
{
	if (v <= 0 || v >= VM_MAX || !vms[v].used)
		return;
	long n = 0;
	free_level(vms[v].pml4, 4, &n);
	memset(&vms[v], 0, sizeof vms[v]);
}

/* l'entrée de la table de pages pour gpu (tables créées au besoin) -> son adresse, 0 */
static u64 *pte_slot(struct vm *vm, u64 gpu, int create)
{
	u64 table = vm->pml4;
	for (int level = 4; level > 1; level--) {
		int shift = 12 + 9 * (level - 1);
		u64 *t = pt_va(table);
		int i = (int)((gpu >> shift) & 511);
		u64 e = t[i] & PTE_ADDR;
		if (e == scratch[level - 1]) {
			if (!create)
				return 0;
			u64 nt = table_new(level - 2);
			if (!nt)
				return 0;
			t[i] = pde(nt);
			vm->tables++;
			e = nt;
		}
		table = e;
	}
	return (u64 *)pt_va(table) + ((gpu >> 12) & 511);
}

int vm_bind(int v, u64 gpu, u64 phys, u64 bytes)
{
	if (v <= 0 || v >= VM_MAX || !vms[v].used || (gpu & 0xFFF) || (phys & 0xFFF))
		return E_INVAL;
	gpu &= GPU_VA_MASK;
	if (gpu + bytes > (1ull << 48) || gpu + bytes < gpu)
		return E_INVAL;
	for (u64 o = 0; o < bytes; o += 4096) {
		u64 *e = pte_slot(&vms[v], gpu + o, 1);
		if (!e)
			return E_NOMEM;
		*e = ((phys + o) & PTE_ADDR) | PTE_PRESENT | PTE_RW;
	}
	return 0;
}

void vm_unbind(int v, u64 gpu, u64 bytes)
{
	if (v <= 0 || v >= VM_MAX || !vms[v].used)
		return;
	gpu &= GPU_VA_MASK;
	for (u64 o = 0; o < bytes; o += 4096) {
		u64 *e = pte_slot(&vms[v], gpu + o, 0);
		if (e)
			*e = pde(scratch[0]);
	}
}

u64 vm_translate(u64 pml4, u64 gpu)
{
	u64 table = pml4;
	gpu &= GPU_VA_MASK;
	for (int level = 4; level >= 1; level--) {
		int shift = 12 + 9 * (level - 1);
		u64 *t = pt_va(table);
		if (!t)
			return ~0ull;
		u64 e = t[(gpu >> shift) & 511];
		if (!(e & PTE_PRESENT))
			return ~0ull;
		table = e & PTE_ADDR;
	}
	return table | (gpu & 0xFFF);
}
