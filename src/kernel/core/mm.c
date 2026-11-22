/* Mémoire des process, comme sur le Pi (docs/ABI.md, 0x00) :
 *   régions paresseuses (VM_MAP, .bss, pile) remplies à la première touche ;
 *   tas de SBRK peuplé d'avance, borné à HEAP_LIMIT ;
 *   tampons partagés (BUF_*), mémoire contiguë pour le DMA, registres (DEV_MAP).
 * Un verrou par process ; on ne l'attend jamais longtemps (ni entrée / sortie,
 * ni invalidation des autres cœurs sous le verrou : les pages retirées sont
 * rendues après vm_tlb_shootdown). Portable. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/ipc.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/vm.h>

#define MAX_REGIONS 4096
#define VM_FIXED 0x10
#define VM_NORESERVE 0x4000
#define VM_POPULATE 0x8000
#define VM_FIXED_NOREPLACE 0x100000
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#define MADV_FREE 8
#define BUF_LOW (1ULL << 62)
#define BUF_NOCACHE (1ULL << 61)
#define DMA_CACHED (1ULL << 63)

struct region {
	uint64_t start, end;
	uint32_t prot, kind;
	struct ipc_obj *obj; /* VMK_SHARED : l'objet projeté (une référence) */
	uint64_t off;        /* décalage dans l'objet de start */
	int cow;             /* VM_COW : écriture privée */
};

#define VM_COW_MAP 0x200000

struct mm {
	spinlock_t lock;
	struct region *r;
	int n, cap;
	uint64_t brk_start, brk;
	uint64_t img_lo; /* la plus basse adresse de l'image du programme (0 : pas d'image) */
	uint64_t owned, faults;
	uint64_t dma_next, dev_next;
	uint8_t bufs[BUF_MAX / 8];
};

struct buf {
	int used;
	uint64_t phys, pages;
	int owner, grant;
	int refs;
};

static struct buf bufs[BUF_MAX];
static spinlock_t buf_lock = SPINLOCK_INIT;

static struct process *cur_proc(void)
{
	return sched_current()->proc;
}

static int vm_prot(uint32_t prot)
{
	return (prot & PROT_WRITE ? VM_W : 0) | (prot & PROT_EXEC ? VM_X : 0);
}

struct mm *mm_create(void)
{
	struct mm *mm = kzalloc(sizeof *mm);
	if (!mm)
		return 0;
	mm->cap = 16;
	mm->r = kmalloc(sizeof(struct region) * (size_t)mm->cap);
	if (!mm->r) {
		kfree(mm);
		return 0;
	}
	mm->dma_next = DMA_BASE;
	mm->dev_next = DEV_BASE;
	return mm;
}

void mm_count_owned(struct mm *mm, int64_t delta)
{
	if (mm)
		__atomic_add_fetch(&mm->owned, (uint64_t)delta, __ATOMIC_RELAXED);
}

/* un segment de l'image du programme commence à lo (elf_load, image_map) : la zone basse de
 * VM_FIXED s'arrête sous la plus basse */
void mm_note_image(struct mm *mm, uint64_t lo)
{
	if (mm && (!mm->img_lo || lo < mm->img_lo))
		mm->img_lo = lo;
}

void mm_set_brk(struct mm *mm, uint64_t start)
{
	mm->brk_start = mm->brk = ALIGN_UP(start, PAGE_SIZE);
}

uint64_t mm_heap_kb(struct process *p)
{
	return p->mm ? (p->mm->brk - p->mm->brk_start) >> 10 : 0;
}

/* ---- table des régions (sous le verrou) */

static int find(struct mm *mm, uint64_t a)
{
	for (int i = 0; i < mm->n; i++)
		if (a >= mm->r[i].start && a < mm->r[i].end)
			return i;
	return -1;
}

static int insert_at(struct mm *mm, int idx, struct region reg)
{
	if (mm->n >= MAX_REGIONS)
		return E_NOMEM;
	if (mm->n == mm->cap) {
		int cap = mm->cap * 2;
		struct region *r = kmalloc(sizeof(struct region) * (size_t)cap);
		if (!r)
			return E_NOMEM;
		memcpy(r, mm->r, sizeof(struct region) * (size_t)mm->n);
		kfree(mm->r);
		mm->r = r;
		mm->cap = cap;
	}
	memmove(&mm->r[idx + 1], &mm->r[idx], sizeof(struct region) * (size_t)(mm->n - idx));
	mm->r[idx] = reg;
	mm->n++;
	return 0;
}

static void remove_at(struct mm *mm, int idx)
{
	memmove(&mm->r[idx], &mm->r[idx + 1], sizeof(struct region) * (size_t)(mm->n - idx - 1));
	mm->n--;
}

static int insert_sorted(struct mm *mm, struct region reg)
{
	int i = 0;
	while (i < mm->n && mm->r[i].start < reg.start)
		i++;
	return insert_at(mm, i, reg);
}

/* coupe la région qui contient a en deux, à a */
static int split_at(struct mm *mm, uint64_t a)
{
	int i = find(mm, a);
	if (i < 0 || mm->r[i].start == a)
		return 0;
	struct region hi = mm->r[i];
	hi.start = a;
	if (hi.obj) {
		hi.off += a - mm->r[i].start;
		ipc_obj_get(hi.obj);
		ipc_shm_mapped(hi.obj, 1);
	}
	mm->r[i].end = a;
	return insert_at(mm, i + 1, hi);
}

/* recolle les régions anonymes voisines de mêmes droits */
static void merge(struct mm *mm)
{
	for (int i = 0; i + 1 < mm->n;) {
		struct region *a = &mm->r[i], *b = &mm->r[i + 1];
		if (a->kind == VMK_ANON && b->kind == VMK_ANON && a->end == b->start && a->prot == b->prot) {
			a->end = b->end;
			remove_at(mm, i + 1);
		} else
			i++;
	}
}

int mm_add_region(struct mm *mm, uint64_t start, uint64_t end, uint32_t prot, uint32_t kind)
{
	uint64_t f = spin_lock_irqsave(&mm->lock);
	int r = insert_sorted(mm, (struct region){start, end, prot, kind, 0, 0, 0});
	spin_unlock_irqrestore(&mm->lock, f);
	return r;
}

/* ---- pages retirées : rendues après l'invalidation des autres cœurs */

struct freelist {
	uint64_t *pages;
	uint64_t n, cap;
	uint64_t uncounted; /* pages de la liste que mm->owned ne compte pas (fl_add_page) */
};

/* -> 1 si la page est dans la liste */
static int fl_add(struct freelist *fl, uint64_t phys)
{
	if (fl->n == fl->cap) {
		uint64_t cap = fl->cap ? fl->cap * 2 : 64;
		uint64_t *p = kmalloc(cap * 8);
		if (!p) { /* sans place : on garde la page (fuite plutôt que corruption) */
			return 0;
		}
		if (fl->pages) {
			memcpy(p, fl->pages, fl->n * 8);
			kfree(fl->pages);
		}
		fl->pages = p;
		fl->cap = cap;
	}
	fl->pages[fl->n++] = phys;
	return 1;
}

static void fl_release(struct process *p, struct freelist *fl, int shoot)
{
	if (shoot)
		vm_tlb_shootdown(p->vm);
	for (uint64_t i = 0; i < fl->n; i++)
		pmm_free(fl->pages[i]);
	mm_count_owned(p->mm, -(int64_t)(fl->n - fl->uncounted));
	kfree(fl->pages);
}

/* une page retirée de va (pas VM_SHARED) : à rendre ; celle d'un objet (fichier, mémoire partagée)
 * encore en copie sur écriture n'est pas comptée dans mm->owned (seulement épinglée : pmm_ref),
 * la rendre ne le décompte pas (sous le verrou, la région encore là) */
static void fl_add_page(struct mm *mm, struct freelist *fl, uint64_t va, uint64_t phys, int fl_flags)
{
	if (!fl_add(fl, phys))
		return;
	if (fl_flags & VM_COW) {
		int ri = find(mm, va);
		if (ri >= 0 && mm->r[ri].obj)
			fl->uncounted++;
	}
}

/* retire les pages présentes de [s, e[ (sous le verrou) */
static int drop_pages(struct process *p, uint64_t s, uint64_t e, struct freelist *fl)
{
	int any = 0;
	for (uint64_t va = vm_next_candidate(p->vm, s, e); va < e; va = vm_next_candidate(p->vm, va + PAGE_SIZE, e)) {
		int fl_flags;
		uint64_t phys = vm_unmap_page(p->vm, va, &fl_flags);
		if (!phys)
			continue;
		any = 1;
		if (!(fl_flags & VM_SHARED))
			fl_add_page(p->mm, fl, va, phys, fl_flags);
	}
	return any;
}

/* ---- fautes de page et accès du noyau */

int mm_fault(struct process *p, uint64_t addr, int write, int exec)
{
	struct mm *mm = p->mm;
	if (!mm || !p->vm || addr >= vm_user_top)
		return -1;
	uint64_t va = ALIGN_DOWN(addr, PAGE_SIZE);
	uint64_t f = spin_lock_irqsave(&mm->lock);
	int ret = -1;
	if (write && (vm_page_flags(p->vm, va) & VM_COW)) { /* copie sur écriture */
		uint64_t old;
		ret = vm_cow_break_keep(p->vm, va, &old);
		/* copiée : un autre thread du process, sur un autre cœur, peut garder la traduction
		 * (lecture seule) vers l'ancienne page, et y lire encore ce que les écritures faites
		 * ici ne changent plus (tas de malloc lu de travers après un fork) : invalidée partout,
		 * sous le verrou (permis : arch_cpu_relax), puis l'ancienne page rendue */
		if (ret == 1 && p->threads > 1)
			vm_tlb_shootdown(p->vm);
		if (old)
			pmm_free(old);
		/* compte : une page d'objet (fichier, mémoire partagée) projetée en privé n'était pas à
		 * lui avant sa copie (+1) ; une page anonyme ou du programme l'était déjà, chez le père
		 * comme chez le fils (mm_fork copie owned) : copiée ou rendue inscriptible sur place, il
		 * en a toujours une (avant : +1 à chaque fois, le compteur montait sans fin après un fork,
		 * jusqu'à dépasser la mémoire de la machine) */
		if (ret >= 0) {
			int ri = find(mm, va);
			if (ri >= 0 && mm->r[ri].obj)
				mm->owned++;
			ret = 0;
		}
		spin_unlock_irqrestore(&mm->lock, f);
		return ret;
	}
	/* déjà là, avec les droits voulus : un autre thread l'a amenée, ou a fait la copie sur
	 * écriture pendant que celui-ci attendait le verrou (deux threads qui écrivent la même page
	 * juste après un fork) ; vaut aussi pour les pages d'un programme, qui n'ont pas de région
	 * (avant : SIGSEGV) */
	int now = vm_page_flags(p->vm, va);
	if ((now & VM_PRESENT) && (!write || (now & VM_W)) && (!exec || (now & VM_X))) {
		spin_unlock_irqrestore(&mm->lock, f);
		return 0;
	}
	int i = find(mm, addr);
	if (i >= 0) {
		uint32_t prot = mm->r[i].prot;
		if (prot && !(write && !(prot & PROT_WRITE)) && !(exec && !(prot & PROT_EXEC))) {
			int have = vm_page_flags(p->vm, va);
			if (have & VM_PRESENT)
				ret = (!write || (have & VM_W)) ? 0 : -1; /* un autre cœur l'a amenée */
			else if (mm->r[i].obj) {
				struct region *rg = &mm->r[i];
				uint64_t phys = ipc_shm_page(rg->obj, rg->off + (va - rg->start));
				if (phys) { /* au-delà de la taille de l'objet : faute (SIGBUS) */
					int fl = vm_prot(prot);
					if (rg->cow) {
						pmm_ref(phys); /* page privée à la première écriture */
						ret = vm_map_page(p->vm, va, phys, fl | VM_COW);
						if (ret)
							pmm_free(phys);
					} else
						ret = vm_map_page(p->vm, va, phys, fl | VM_SHARED);
					if (ret == 0 && write && rg->cow) {
						ret = vm_cow_break(p->vm, va) < 0 ? -1 : 0;
						mm->owned++;
					}
				}
			} else {
				uint64_t phys = pmm_alloc();
				if (phys) {
					memset(arch_phys_to_virt(phys), 0, PAGE_SIZE);
					if (vm_map_page(p->vm, va, phys, vm_prot(prot)) == 0) {
						mm->owned++;
						mm->faults++;
						ret = 0;
					} else
						pmm_free(phys);
				}
			}
		}
	}
	spin_unlock_irqrestore(&mm->lock, f);
	return ret;
}

/* (SVC_MAP_CALLER) la traduction lue et la page épinglée sous le verrou : avant, la lecture et
 * pmm_ref se faisaient sans lui, et un autre thread de l'appelant pouvait entre-temps copier la
 * page (copie sur écriture) puis le fils la rendre : le service épinglait une page libre, l'écrivait,
 * et la rendait une seconde fois */
uint64_t mm_pin_page(struct process *p, uint64_t va, int write)
{
	for (int tries = 0; tries < 8; tries++) {
		if (!p->mm || !p->vm)
			return 0;
		int fl = vm_page_flags(p->vm, va);
		if ((!(fl & VM_PRESENT) || (write && !(fl & VM_W))) && mm_fault(p, va, write, 0))
			return 0;
		uint64_t f = spin_lock_irqsave(&p->mm->lock);
		fl = vm_page_flags(p->vm, va);
		uint64_t phys = 0;
		if ((fl & VM_PRESENT) && (!write || (fl & VM_W))) {
			phys = vm_translate(p->vm, va) & ~(PAGE_SIZE - 1);
			if (phys)
				pmm_ref(phys);
		}
		spin_unlock_irqrestore(&p->mm->lock, f);
		if (phys)
			return phys;
	}
	return 0;
}

int user_check(uint64_t addr, uint64_t len, int write)
{
	struct process *p = cur_proc();
	if (!p->vm || addr + len < addr || addr + len > vm_user_top)
		return E_FAULT;
	if (len == 0)
		return 0;
	for (uint64_t va = ALIGN_DOWN(addr, PAGE_SIZE); va < addr + len; va += PAGE_SIZE) {
		int fl = vm_page_flags(p->vm, va);
		if ((fl & VM_PRESENT) && (!write || (fl & VM_W)))
			continue;
		if (mm_fault(p, va, write, 0))
			return E_FAULT;
	}
	return 0;
}

int copy_from_user(void *dst, uint64_t src, uint64_t len)
{
	int r = user_check(src, len, 0);
	if (r)
		return r;
	memcpy(dst, (const void *)src, len);
	return 0;
}

int copy_to_user(uint64_t dst, const void *src, uint64_t len)
{
	int r = user_check(dst, len, 1);
	if (r)
		return r;
	memcpy((void *)dst, src, len);
	return 0;
}

int64_t copy_string_from_user(char *dst, uint64_t src, uint64_t max)
{
	for (uint64_t i = 0; i < max; i++) {
		if ((i == 0 || ((src + i) & (PAGE_SIZE - 1)) == 0) && user_check(src + i, 1, 0))
			return E_FAULT;
		dst[i] = ((const char *)src)[i];
		if (!dst[i])
			return (int64_t)i;
	}
	return E_INVAL;
}

/* ---- tas */

int64_t sys_sbrk(int64_t inc)
{
	struct process *p = cur_proc();
	struct mm *mm = p->mm;
	if (!mm || !mm->brk_start)
		return E_NOMEM;
	struct freelist fl = {0};
	int shoot = 0;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	uint64_t old = mm->brk, nb = old + (uint64_t)inc;
	int64_t ret = (int64_t)old;
	if ((inc > 0 && (nb < old || nb > HEAP_LIMIT)) || (inc < 0 && nb < mm->brk_start)) {
		ret = E_NOMEM;
		goto out;
	}
	uint64_t a = ALIGN_UP(old, PAGE_SIZE), b = ALIGN_UP(nb, PAGE_SIZE);
	if (b > a) {
		for (uint64_t va = a; va < b; va += PAGE_SIZE) {
			uint64_t phys = pmm_alloc();
			if (!phys || vm_map_page(p->vm, va, phys, VM_W)) {
				if (phys)
					pmm_free(phys);
				drop_pages(p, a, va, &fl);
				ret = E_NOMEM;
				goto out;
			}
			memset(arch_phys_to_virt(phys), 0, PAGE_SIZE);
			mm->owned++;
		}
	} else if (b < a)
		shoot = drop_pages(p, b, a, &fl);
	mm->brk = nb;
out:
	spin_unlock_irqrestore(&mm->lock, f);
	fl_release(p, &fl, shoot);
	return ret;
}

/* ---- régions (VM_MAP, VM_UNMAP, VM_PROTECT, VM_ADVISE, VM_QUERY) */

static int in_arena(uint64_t a, uint64_t len)
{
	if (a + len <= a)
		return 0;
	return (a >= VM_ARENA_BASE && a + len <= VM_ARENA_END) || (a >= VM_HIGH_BASE && a + len <= VM_HIGH_END);
}

/* VM_FIXED : l'arène, l'arène haute, et depuis le 21/11 la zone basse [VM_LOW_BASE, image[ : un
 * exécutable Linux non PIE de l'hôte Linux (lié à 0x40_0000_0000) se charge à ses adresses
 * (0x200000, 0x400000…) ; find_gap n'y cherche jamais */
static int in_fixed(struct mm *mm, uint64_t a, uint64_t len)
{
	if (in_arena(a, len))
		return 1;
	return a + len > a && a >= VM_LOW_BASE && mm->img_lo && a + len <= ALIGN_DOWN(mm->img_lo, PAGE_SIZE);
}

static int overlaps(struct mm *mm, uint64_t s, uint64_t e)
{
	for (int i = 0; i < mm->n; i++)
		if (mm->r[i].start < e && mm->r[i].end > s)
			return 1;
	return 0;
}

/* retire régions et pages de [s, e[ dans l'arène (sous le verrou) */
static int unmap_range(struct process *p, uint64_t s, uint64_t e, struct freelist *fl)
{
	struct mm *mm = p->mm;
	if (split_at(mm, s) || split_at(mm, e))
		return E_NOMEM;
	/* les pages d'abord : drop_pages lit encore les régions (fl_add_page) */
	int any = drop_pages(p, s, e, fl);
	for (int i = 0; i < mm->n;) {
		if (mm->r[i].start >= s && mm->r[i].end <= e && (mm->r[i].kind == VMK_ANON || mm->r[i].kind == VMK_SHARED)) {
			if (mm->r[i].obj) {
				ipc_shm_mapped(mm->r[i].obj, -1);
				ipc_obj_put(mm->r[i].obj);
			}
			remove_at(mm, i);
		} else
			i++;
	}
	return any;
}

/* le premier trou de len octets dans [lo, hi[ (régions triées), 0 s'il n'y en a pas */
static uint64_t gap_in(struct mm *mm, uint64_t lo, uint64_t hi, uint64_t len)
{
	uint64_t a = lo;
	for (int i = 0; i < mm->n; i++) {
		struct region *r = &mm->r[i];
		if (r->end <= a)
			continue;
		if (r->start >= hi || r->start >= a + len)
			break; /* le trou avant cette région suffit */
		a = r->end;
	}
	return a + len <= hi && a + len > a ? a : 0;
}

static uint64_t find_gap(struct mm *mm, uint64_t hint, uint64_t len)
{
	if (hint && !(hint & (PAGE_SIZE - 1)) && in_arena(hint, len) && !overlaps(mm, hint, hint + len))
		return hint;
	uint64_t a = gap_in(mm, VM_ARENA_BASE, VM_ARENA_END, len);
	/* (l'arène haute : seulement ce qui ne tient pas dans l'arène ; les adresses habituelles ne changent pas) */
	return a ? a : gap_in(mm, VM_HIGH_BASE, VM_HIGH_END, len);
}

int64_t sys_vm_map(uint64_t addr, uint64_t size, uint64_t prot, uint64_t flags)
{
	struct process *p = cur_proc();
	struct mm *mm = p->mm;
	if (!mm || size == 0 || (prot & ~7ULL))
		return E_INVAL;
	uint64_t len = ALIGN_UP(size, PAGE_SIZE);
	int fixed = (flags & (VM_FIXED | VM_FIXED_NOREPLACE)) != 0;
	if (fixed && ((addr & (PAGE_SIZE - 1)) || !in_fixed(mm, addr, len)))
		return E_INVAL;
	struct freelist fl = {0};
	int shoot = 0;
	int64_t ret;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	if (flags & VM_FIXED_NOREPLACE) {
		if (overlaps(mm, addr, addr + len)) {
			ret = E_EXIST;
			goto out;
		}
	} else if (flags & VM_FIXED) {
		int r = unmap_range(p, addr, addr + len, &fl);
		if (r < 0) {
			ret = r;
			goto out;
		}
		shoot = r;
	} else {
		addr = find_gap(mm, addr, len);
		if (!addr) {
			ret = E_NOMEM;
			goto out;
		}
	}
	ret = insert_sorted(mm, (struct region){addr, addr + len, (uint32_t)prot, VMK_ANON, 0, 0, 0});
	if (ret == 0) {
		merge(mm);
		ret = (int64_t)addr;
	}
out:
	spin_unlock_irqrestore(&mm->lock, f);
	fl_release(p, &fl, shoot);
	if (ret > 0 && (flags & VM_POPULATE) && prot)
		for (uint64_t va = addr; va < addr + len; va += PAGE_SIZE)
			if (mm_fault(p, va, (prot & PROT_WRITE) != 0, 0))
				return E_NOMEM;
	return ret;
}

int64_t sys_vm_unmap(uint64_t addr, uint64_t size)
{
	struct process *p = cur_proc();
	struct mm *mm = p->mm;
	uint64_t len = ALIGN_UP(size, PAGE_SIZE);
	if (!mm || (addr & (PAGE_SIZE - 1)) || !in_fixed(mm, addr, len))
		return E_INVAL;
	struct freelist fl = {0};
	uint64_t f = spin_lock_irqsave(&mm->lock);
	int r = unmap_range(p, addr, addr + len, &fl);
	spin_unlock_irqrestore(&mm->lock, f);
	fl_release(p, &fl, r > 0);
	return r < 0 ? r : 0;
}

/* [s, e[ couvert sans trou par des régions (anon_only : anonymes ; 2 : anonymes ou projections
 * d'un objet partagé, fichier ou mémoire partagée) ? (sous le verrou) */
static int covered(struct mm *mm, uint64_t s, uint64_t e, int anon_only)
{
	uint64_t a = s;
	while (a < e) {
		int i = find(mm, a);
		if (i < 0)
			return 0;
		int k = mm->r[i].kind;
		if (anon_only == 1 && k != VMK_ANON)
			return 0;
		if (anon_only == 2 && k != VMK_ANON && k != VMK_SHARED)
			return 0;
		a = mm->r[i].end;
	}
	return 1;
}

int64_t sys_vm_protect(uint64_t addr, uint64_t size, uint64_t prot)
{
	struct process *p = cur_proc();
	struct mm *mm = p->mm;
	uint64_t len = ALIGN_UP(size, PAGE_SIZE);
	if (!mm || (addr & (PAGE_SIZE - 1)) || (prot & ~7ULL) || len == 0)
		return E_INVAL;
	int64_t ret = 0;
	int shoot = 0;
	struct freelist fl = {0};
	uint64_t f = spin_lock_irqsave(&mm->lock);
	/* régions anonymes, et depuis le 14/11 les projections d'un objet partagé (fichier par
	 * __posix_file_share, mémoire partagée) : les pages en copie sur écriture le restent (une
	 * écriture copiera), celles de l'objet changent de droits sur place. PROT_NONE sur une page déjà
	 * copiée d'une projection privée : refusé (x86 n'a pas de page présente sans accès : elle
	 * partirait, et ses écritures avec) */
	if (!covered(mm, addr, addr + len, 2)) {
		ret = E_INVAL;
		goto out;
	}
	if (!prot)
		for (uint64_t va = vm_next_candidate(p->vm, addr, addr + len); va < addr + len;
		     va = vm_next_candidate(p->vm, va + PAGE_SIZE, addr + len)) {
			int k = find(mm, va), fl2 = vm_page_flags(p->vm, va);
			if (k >= 0 && mm->r[k].kind == VMK_SHARED && (fl2 & VM_PRESENT) && !(fl2 & (VM_SHARED | VM_COW))) {
				ret = E_INVAL;
				goto out;
			}
		}
	if (split_at(mm, addr) || split_at(mm, addr + len)) {
		ret = E_INVAL;
		goto out;
	}
	for (int i = 0; i < mm->n; i++)
		if (mm->r[i].start >= addr && mm->r[i].end <= addr + len)
			mm->r[i].prot = (uint32_t)prot;
	for (uint64_t va = vm_next_candidate(p->vm, addr, addr + len); va < addr + len;
	     va = vm_next_candidate(p->vm, va + PAGE_SIZE, addr + len)) {
		if (!prot) { /* PROT_NONE : x86 n'a pas de page présente sans accès : la page part
			      * et reviendra à zéro (écart avec le Pi, qui la garde) */
			int fl2;
			uint64_t phys = vm_unmap_page(p->vm, va, &fl2);
			if (phys && !(fl2 & VM_SHARED))
				fl_add_page(mm, &fl, va, phys, fl2);
			shoot |= phys != 0;
		} else
			shoot |= vm_protect_page(p->vm, va, vm_prot((uint32_t)prot));
	}
	merge(mm);
out:
	spin_unlock_irqrestore(&mm->lock, f);
	fl_release(p, &fl, shoot);
	return ret;
}

int64_t sys_vm_advise(uint64_t addr, uint64_t size, uint64_t advice)
{
	struct process *p = cur_proc();
	struct mm *mm = p->mm;
	uint64_t len = ALIGN_UP(size, PAGE_SIZE);
	if (!mm || (addr & (PAGE_SIZE - 1)))
		return E_INVAL;
	if (advice <= 2)
		return 0;
	if (advice == MADV_WILLNEED) {
		for (uint64_t va = addr; va < addr + len; va += PAGE_SIZE) {
			uint64_t f = spin_lock_irqsave(&mm->lock);
			int i = find(mm, va);
			uint32_t prot = i >= 0 ? mm->r[i].prot : 0;
			spin_unlock_irqrestore(&mm->lock, f);
			if (prot && mm_fault(p, va, (prot & PROT_WRITE) != 0, 0))
				return E_NOMEM;
		}
		return 0;
	}
	if (advice != MADV_DONTNEED && advice != MADV_FREE)
		return E_INVAL;
	struct freelist fl = {0};
	int64_t ret = 0;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	int shoot = 0;
	if (!covered(mm, addr, addr + len, 0))
		ret = E_INVAL;
	else
		shoot = drop_pages(p, addr, addr + len, &fl);
	spin_unlock_irqrestore(&mm->lock, f);
	fl_release(p, &fl, shoot);
	return ret;
}

int64_t sys_vm_query(uint64_t addr, uint64_t out)
{
	struct process *p = cur_proc();
	struct mm *mm = p->mm;
	if (!mm)
		return E_NOMEM;
	uint64_t o[4] = {0};
	int64_t ret = E_NOMEM;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	int i = find(mm, addr);
	if (i >= 0)
		ret = 0;
	else {
		for (int j = 0; j < mm->n; j++)
			if (mm->r[j].start > addr) {
				i = j;
				ret = 1;
				break;
			}
	}
	if (i >= 0) {
		struct region *r = &mm->r[i];
		uint32_t present = 0;
		for (uint64_t va = vm_next_candidate(p->vm, r->start, r->end); va < r->end;
		     va = vm_next_candidate(p->vm, va + PAGE_SIZE, r->end))
			present += (vm_page_flags(p->vm, va) & VM_PRESENT) != 0;
		o[0] = r->start;
		o[1] = r->end;
		o[2] = (uint64_t)r->prot | ((uint64_t)r->kind << 32);
		o[3] = present;
	}
	spin_unlock_irqrestore(&mm->lock, f);
	if (ret >= 0 && copy_to_user(out, o, 32))
		return E_FAULT;
	return ret;
}

int64_t sys_vm_stats(int64_t pid, uint64_t out)
{
	struct process *p = cur_proc();
	if (pid && pid != p->pid) {
		uint64_t f = sched_lock_take();
		p = proc_get((int)pid);
		sched_lock_give(f);
		if (!p)
			return E_SRCH;
	}
	uint64_t o[8] = {0};
	if (p->mm) {
		uint64_t f = spin_lock_irqsave(&p->mm->lock);
		o[0] = p->mm->owned;
		for (int i = 0; i < p->mm->n; i++)
			o[2] += p->mm->r[i].end - p->mm->r[i].start;
		o[3] = p->mm->faults;
		for (int b = 0; b < BUF_MAX; b++)
			if (p->mm->bufs[b / 8] & (1 << (b % 8)))
				o[1] += bufs[b].pages;
		spin_unlock_irqrestore(&p->mm->lock, f);
	}
	if (p->vm)
		o[5] = vm_table_pages(p->vm);
	o[7] = pmm_free_pages(); /* une seule zone sur PC */
	return copy_to_user(out, o, 64) ? E_FAULT : 0;
}

int64_t sys_vm_pageinfo(uint64_t addr)
{
	struct process *p = cur_proc();
	if (!p->vm || addr >= vm_user_top)
		return 0;
	int fl = vm_page_flags(p->vm, addr);
	int64_t r = 0;
	if (fl & VM_PRESENT)
		r |= 1 | 64; /* PI_PRESENT, PI_READ */
	if (fl & VM_SHARED)
		r |= 2;
	if (fl & VM_W)
		r |= 8;
	if (fl & VM_COW)
		r |= 4;
	if ((fl & VM_PRESENT) && !(fl & VM_SHARED) && pmm_extra_refs(vm_translate(p->vm, addr) & ~(PAGE_SIZE - 1)))
		r |= 128; /* PI_FORKED */
	if (fl & VM_X)
		r |= 16;
	if (p->mm) {
		uint64_t f = spin_lock_irqsave(&p->mm->lock);
		if (find(p->mm, addr) >= 0)
			r |= 32; /* PI_LAZY */
		spin_unlock_irqrestore(&p->mm->lock, f);
	}
	return r;
}

/* ---- tampons partagés */

static int buf_mapped(struct mm *mm, int n)
{
	return (mm->bufs[n / 8] >> (n % 8)) & 1;
}

static int map_buf(struct process *p, int n)
{
	struct mm *mm = p->mm;
	uint64_t base = BUF_BASE + (uint64_t)n * BUF_SLOT;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	for (uint64_t i = 0; i < bufs[n].pages; i++)
		if (vm_map_page(p->vm, base + i * PAGE_SIZE, bufs[n].phys + i * PAGE_SIZE, VM_W | VM_SHARED)) {
			spin_unlock_irqrestore(&mm->lock, f);
			return E_NOMEM;
		}
	mm->bufs[n / 8] |= (uint8_t)(1 << (n % 8));
	spin_unlock_irqrestore(&mm->lock, f);
	return 0;
}

static void unmap_buf(struct process *p, int n)
{
	struct mm *mm = p->mm;
	uint64_t base = BUF_BASE + (uint64_t)n * BUF_SLOT;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	for (uint64_t i = 0; i < bufs[n].pages; i++)
		vm_unmap_page(p->vm, base + i * PAGE_SIZE, 0);
	mm->bufs[n / 8] &= (uint8_t)~(1 << (n % 8));
	spin_unlock_irqrestore(&mm->lock, f);
}

/* une référence de moins : le tampon part avec la dernière (buf_lock pris) */
static void buf_put(int n)
{
	if (--bufs[n].refs > 0)
		return;
	for (uint64_t i = 0; i < bufs[n].pages; i++)
		pmm_free(bufs[n].phys + i * PAGE_SIZE);
	bufs[n].used = 0;
}

int64_t sys_buf_create(uint64_t size)
{
	struct process *p = cur_proc();
	size &= ~(BUF_LOW | BUF_NOCACHE);
	if (!p->mm || size == 0 || size > BUF_SLOT)
		return E_INVAL;
	uint64_t pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
	uint64_t phys = pmm_alloc_contig(pages);
	if (!phys)
		return E_NOMEM;
	memset(arch_phys_to_virt(phys), 0, pages * PAGE_SIZE);
	uint64_t f = spin_lock_irqsave(&buf_lock);
	int n = -1;
	for (int i = 0; i < BUF_MAX && n < 0; i++)
		if (!bufs[i].used)
			n = i;
	if (n >= 0)
		bufs[n] = (struct buf){1, phys, pages, p->pid, 0, 1};
	spin_unlock_irqrestore(&buf_lock, f);
	if (n < 0) {
		for (uint64_t i = 0; i < pages; i++)
			pmm_free(phys + i * PAGE_SIZE);
		return E_NOMEM;
	}
	if (map_buf(p, n)) {
		f = spin_lock_irqsave(&buf_lock);
		buf_put(n);
		spin_unlock_irqrestore(&buf_lock, f);
		return E_NOMEM;
	}
	return n;
}

int64_t sys_buf_grant(uint64_t n, int64_t pid)
{
	struct process *p = cur_proc();
	if (n >= BUF_MAX)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&buf_lock);
	int64_t r = 0;
	if (!bufs[n].used)
		r = E_INVAL;
	else if (bufs[n].owner != p->pid)
		r = E_PERM;
	else
		bufs[n].grant = (int)pid;
	spin_unlock_irqrestore(&buf_lock, f);
	return r;
}

int64_t sys_buf_map(uint64_t n)
{
	struct process *p = cur_proc();
	if (n >= BUF_MAX || !p->mm)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&buf_lock);
	int64_t r = 0;
	if (!bufs[n].used)
		r = E_INVAL;
	else if (bufs[n].owner != p->pid && bufs[n].grant != p->pid && bufs[n].grant != -1)
		r = E_PERM;
	else if (!buf_mapped(p->mm, (int)n))
		bufs[n].refs++;
	else
		r = 1; /* déjà là */
	spin_unlock_irqrestore(&buf_lock, f);
	if (r < 0)
		return r;
	if (r == 0 && map_buf(p, (int)n)) {
		f = spin_lock_irqsave(&buf_lock);
		buf_put((int)n);
		spin_unlock_irqrestore(&buf_lock, f);
		return E_NOMEM;
	}
	return (int64_t)(BUF_BASE + n * BUF_SLOT);
}

int64_t sys_buf_free(uint64_t n)
{
	struct process *p = cur_proc();
	if (n >= BUF_MAX || !p->mm || !buf_mapped(p->mm, (int)n))
		return E_INVAL;
	unmap_buf(p, (int)n);
	vm_tlb_shootdown(p->vm);
	uint64_t f = spin_lock_irqsave(&buf_lock);
	buf_put((int)n);
	spin_unlock_irqrestore(&buf_lock, f);
	return 0;
}

int64_t sys_buf_info(uint64_t n, uint64_t out)
{
	if (n >= BUF_MAX)
		return E_INVAL;
	uint64_t o[4];
	uint64_t f = spin_lock_irqsave(&buf_lock);
	int used = bufs[n].used;
	o[0] = bufs[n].phys;
	o[1] = bufs[n].pages * PAGE_SIZE;
	o[2] = (uint64_t)(int64_t)bufs[n].owner;
	o[3] = (uint64_t)(int64_t)bufs[n].grant;
	spin_unlock_irqrestore(&buf_lock, f);
	if (!used)
		return E_INVAL;
	return copy_to_user(out, o, 32) ? E_FAULT : 0;
}

/* ---- DMA et registres */

int64_t sys_dma_alloc(uint64_t size, uint64_t phys_out)
{
	struct process *p = cur_proc();
	if (!p->rights)
		return E_PERM;
	size &= ~DMA_CACHED; /* le DMA du PC est cohérent avec les caches */
	if (!p->mm || size == 0)
		return E_INVAL;
	uint64_t pages = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
	uint64_t phys = pmm_alloc_contig(pages);
	if (!phys)
		return E_NOMEM;
	memset(arch_phys_to_virt(phys), 0, pages * PAGE_SIZE);
	struct mm *mm = p->mm;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	uint64_t va = mm->dma_next;
	mm->dma_next += pages * PAGE_SIZE + PAGE_SIZE; /* une page de garde */
	for (uint64_t i = 0; i < pages; i++)
		vm_map_page(p->vm, va + i * PAGE_SIZE, phys + i * PAGE_SIZE, VM_W);
	mm->owned += pages;
	spin_unlock_irqrestore(&mm->lock, f);
	if (phys_out && copy_to_user(phys_out, &phys, 8))
		return E_FAULT;
	return (int64_t)va;
}

int64_t mm_map_device(uint64_t phys, uint64_t size, int cached)
{
	struct process *p = cur_proc();
	if (!p->mm || size == 0)
		return E_INVAL;
	uint64_t off = phys & (PAGE_SIZE - 1);
	uint64_t pages = ALIGN_UP(size + off, PAGE_SIZE) / PAGE_SIZE;
	struct mm *mm = p->mm;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	uint64_t va = mm->dev_next;
	mm->dev_next += pages * PAGE_SIZE + PAGE_SIZE;
	for (uint64_t i = 0; i < pages; i++)
		vm_map_page(p->vm, va + i * PAGE_SIZE, ALIGN_DOWN(phys, PAGE_SIZE) + i * PAGE_SIZE,
			    VM_W | VM_SHARED | (cached ? 0 : VM_NOCACHE));
	spin_unlock_irqrestore(&mm->lock, f);
	return (int64_t)(va + off);
}

/* ---- fork : régions, tas et tampons du père ; pages en copie sur écriture */

int mm_fork(struct process *parent, struct process *child)
{
	struct mm *pm = parent->mm, *cm = child->mm;
	uint64_t f = spin_lock_irqsave(&pm->lock);
	int r = 0;
	if (cm->cap < pm->n) {
		struct region *nr = kmalloc(sizeof(struct region) * (size_t)pm->n);
		if (!nr)
			r = E_NOMEM;
		else {
			kfree(cm->r);
			cm->r = nr;
			cm->cap = pm->n;
		}
	}
	if (r == 0) {
		memcpy(cm->r, pm->r, sizeof(struct region) * (size_t)pm->n);
		cm->n = pm->n;
		for (int i = 0; i < cm->n; i++)
			if (cm->r[i].obj) {
				ipc_obj_get(cm->r[i].obj);
				ipc_shm_mapped(cm->r[i].obj, 1);
			}
		cm->brk_start = pm->brk_start;
		cm->brk = pm->brk;
		cm->img_lo = pm->img_lo;
		cm->dma_next = pm->dma_next;
		cm->dev_next = pm->dev_next;
		cm->owned = pm->owned;
		memcpy(cm->bufs, pm->bufs, sizeof cm->bufs);
		r = vm_fork(parent->vm, child->vm);
		/* les autres threads du père : lecture seule, AVANT de rendre le verrou ; sinon un thread
		 * pouvait faire la copie d'une page (faute, sous ce verrou) pendant qu'un autre, sur un
		 * autre cœur, écrivait encore par son ancienne traduction inscriptible dans la page
		 * laissée au fils : écriture perdue pour le père (tas de malloc abîmé, plantages de free
		 * sous 8 et 16 cœurs). Sûr sous le verrou : un cœur qui attend un verrou répond aux
		 * invalidations en attendant (arch_cpu_relax) */
		vm_tlb_shootdown(parent->vm);
	}
	spin_unlock_irqrestore(&pm->lock, f);
	if (r)
		return r;
	f = spin_lock_irqsave(&buf_lock);
	for (int n = 0; n < BUF_MAX; n++)
		if (buf_mapped(cm, n))
			bufs[n].refs++;
	spin_unlock_irqrestore(&buf_lock, f);
	return 0;
}

/* ---- fin d'un process */

void mm_destroy(struct process *p)
{
	mm_free(p->mm);
	p->mm = 0;
}

void mm_free(struct mm *mm)
{
	if (!mm)
		return;
	for (int n = 0; n < BUF_MAX; n++)
		if (buf_mapped(mm, n)) {
			uint64_t f = spin_lock_irqsave(&buf_lock);
			buf_put(n);
			spin_unlock_irqrestore(&buf_lock, f);
		}
	for (int i = 0; i < mm->n; i++)
		if (mm->r[i].obj) {
			ipc_shm_mapped(mm->r[i].obj, -1);
			ipc_obj_put(mm->r[i].obj);
		}
	kfree(mm->r);
	kfree(mm);
}

/* SHM_MAP : région VMK_SHARED dans l'arène ; obj : une référence déjà prise
 * pour la région */
int64_t mm_map_shared(struct ipc_obj *obj, uint64_t addr, uint64_t size, uint64_t prot, uint64_t flags, uint64_t off)
{
	struct process *p = cur_proc();
	struct mm *mm = p->mm;
	if (!mm || size == 0 || (prot & ~7ULL) || (off & (PAGE_SIZE - 1)))
		return E_INVAL;
	uint64_t len = ALIGN_UP(size, PAGE_SIZE);
	int fixed = (flags & (VM_FIXED | VM_FIXED_NOREPLACE)) != 0;
	if (fixed && ((addr & (PAGE_SIZE - 1)) || !in_fixed(mm, addr, len)))
		return E_INVAL;
	struct freelist fl = {0};
	int shoot = 0;
	int64_t ret;
	uint64_t f = spin_lock_irqsave(&mm->lock);
	if (flags & VM_FIXED_NOREPLACE) {
		if (overlaps(mm, addr, addr + len)) {
			ret = E_EXIST;
			goto out;
		}
	} else if (flags & VM_FIXED) {
		int r = unmap_range(p, addr, addr + len, &fl);
		if (r < 0) {
			ret = r;
			goto out;
		}
		shoot = r;
	} else {
		addr = find_gap(mm, addr, len);
		if (!addr) {
			ret = E_NOMEM;
			goto out;
		}
	}
	ret = insert_sorted(mm, (struct region){addr, addr + len, (uint32_t)prot, VMK_SHARED, obj, off,
						(flags & VM_COW_MAP) != 0});
	if (ret == 0) {
		ipc_shm_mapped(obj, 1);
		ret = (int64_t)addr;
	}
out:
	spin_unlock_irqrestore(&mm->lock, f);
	fl_release(p, &fl, shoot);
	return ret;
}
