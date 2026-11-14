/* Tas du noyau. Chaque bloc commence par un en-tête de 16 octets :
 *   petits blocs (<= 2 Ko en tout) : classes de 32 à 2048 octets, découpées
 *   dans des pages entières, listes de blocs libres par classe ;
 *   gros blocs : pages physiques contiguës, rendues à kfree.
 * Vérification des gros blocs (toujours) : chaque gros bloc vivant est dans une table, avec la pile
 * d'appel de son allocation ; un kfree d'un gros bloc qui n'y est pas (rendu deux fois, ou jamais
 * donné), ou dont l'en-tête a changé depuis l'allocation, arrête le noyau en donnant sa pile, et celle
 * de la libération précédente du même bloc
 * (anneau des dernières libérations). Les petits blocs : l'en-tête est effacé à kfree (une
 * seconde libération y voit un bloc invalide).
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

/* ---- vérification des gros blocs (sous heap_lock) */
#define LIVE_MAX 8192 /* (adressage ouvert ; bien plus que les gros blocs vivants) */
#define FREED_MAX 512
#define STACK_DEPTH 6

struct trace {
	uint64_t ra[STACK_DEPTH];
};

static struct live {
	void *p;            /* 0 : libre ; (void *)1 : effacé */
	uint32_t pages, phys_page; /* ce que dit l'en-tête à l'allocation */
	struct trace alloc;
} live[LIVE_MAX];
static struct {
	void *p;
	struct trace alloc, free;
} freed[FREED_MAX];
static unsigned freed_next;

static void trace_here(struct trace *t)
{
	uint64_t *fp = __builtin_frame_address(0);
	for (int i = 0; i < STACK_DEPTH; i++) {
		int ok = fp && ((uint64_t)fp >> 47) && !((uint64_t)fp & 7);
		t->ra[i] = ok ? fp[1] : 0;
		fp = ok ? (uint64_t *)fp[0] : 0;
	}
}

static void trace_print(const char *what, const struct trace *t)
{
	kprintf("kheap : %s :", what);
	for (int i = 0; i < STACK_DEPTH && t->ra[i]; i++)
		kprintf(" %lx", t->ra[i]);
	kprintf("\n");
}

static unsigned live_slot(void *p)
{
	return (unsigned)(((uint64_t)p >> 12) * 2654435761u) % LIVE_MAX;
}

static void live_add(void *p, uint32_t pages, uint32_t phys_page)
{
	for (unsigned i = live_slot(p), n = 0; n < LIVE_MAX; i = (i + 1) % LIVE_MAX, n++)
		if (live[i].p == 0 || live[i].p == (void *)1) {
			live[i].p = p;
			live[i].pages = pages;
			live[i].phys_page = phys_page;
			trace_here(&live[i].alloc);
			return;
		}
}

/* -> 1 s'il y était (retiré, sa trace dans *t, sa taille dans *pages, *phys_page), 0 sinon */
static int live_take(void *p, struct trace *t, uint32_t *pages, uint32_t *phys_page)
{
	for (unsigned i = live_slot(p), n = 0; n < LIVE_MAX && live[i].p; i = (i + 1) % LIVE_MAX, n++)
		if (live[i].p == p) {
			*t = live[i].alloc;
			*pages = live[i].pages;
			*phys_page = live[i].phys_page;
			live[i].p = (void *)1;
			return 1;
		}
	return 0;
}

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
			pmm_heap_mark(phys, 1, 1); /* (page des petits blocs : au tas pour toujours) */
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
		pmm_heap_mark(phys, pages, 1);
		struct big *b = arch_phys_to_virt(phys);
		b->magic = MAGIC;
		b->cls = BIG;
		b->pages = (uint32_t)pages;
		b->phys_page = (uint32_t)(phys / PAGE_SIZE);
		ret = b + 1;
		live_add(ret, b->pages, b->phys_page);
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
		struct trace at, now;
		trace_here(&now);
		uint32_t lpages = 0, lphys = 0;
		int found = live_take(p, &at, &lpages, &lphys);
		if (found && (lpages != b->pages || lphys != b->phys_page)) {
			kprintf("\nkheap : en-tête du gros bloc %p abîmé : %u pages à %x (donné : %u pages à %x)\n", p,
				b->pages, b->phys_page, lpages, lphys);
			trace_print("allocation", &at);
			trace_print("cette libération", &now);
			kpanic("kfree : en-tête du gros bloc %p abîmé", p);
		} else if (!found) { /* rendu deux fois (ou jamais donné) */
			kprintf("\nkheap : gros bloc %p rendu deux fois (ou inconnu)\n", p);
			trace_print("cette libération", &now);
			for (unsigned i = 0; i < FREED_MAX; i++)
				if (freed[i].p == p) {
					trace_print("allocation d'avant", &freed[i].alloc);
					trace_print("libération d'avant", &freed[i].free);
				}
			kpanic("kfree : gros bloc %p rendu deux fois", p);
		}
		unsigned k = freed_next++ % FREED_MAX;
		freed[k].p = p;
		freed[k].alloc = at;
		freed[k].free = now;
		/* la taille lue AVANT de rendre la première page (celle de l'en-tête) : un autre cœur peut
		 * la reprendre aussitôt (pmm_alloc ne passe pas par heap_lock) et l'écrire ; la boucle relisait
		 * b->pages dans une page déjà rendue, et rendait les pages d'un autre bloc (la double
		 * libération de page vue sous 4 cœurs : pile d'un thread, table des poignées d'IPC) */
		uint32_t pages = b->pages;
		pmm_heap_mark(phys, pages, 0);
		for (uint32_t i = 0; i < pages; i++)
			pmm_free(phys + i * PAGE_SIZE);
	} else {
		*(void **)h = free_list[h->cls];
		free_list[h->cls] = h;
	}
	spin_unlock_irqrestore(&heap_lock, f);
}
