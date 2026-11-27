/* Images des exécutables, comme src/kernel/preload.asm du Pi (docs/ABI.md,
 * « Images des exécutables ») : un exécutable lu une fois, dont les process
 * lancés mappent directement les pages :
 *   code et lecture seule partagés tels quels (jamais copiés ni rendus par eux),
 *   lecture / écriture en copie sur écriture, .bss en région paresseuse ;
 *   une page de segment qui n'est pas une page entière du fichier au même
 *   décalage est recopiée dans une page à soi.
 * Construction sans tampon : IMAGE_CREATE donne une fenêtre (les pages du
 * fichier, à zéro, chez l'appelant), l'appelant y lit le fichier, IMAGE_SEAL
 * vérifie et met l'image en place. Clé : chemin résolu (aux majuscules près)
 * et taille. Épinglée (IMG_PIN) : gardée ; passagère : rendue avec son dernier
 * process. Le noyau ne lit aucun fichier. Portable. */
#include <korin/arch.h>
#include <korin/boot.h>
#include <korin/errno.h>
#include <korin/image.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/sha256.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/vm.h>

#define IMG_MAX 128
#define PATH IMAGE_PATH_MAX
#define IML_PATH 80 /* le chemin dans une entrée d'IMAGE_LIST (128 o, ABI) : tronqué */
#define WIN_BASE IMG_WIN_BASE /* (mm.h ; 0x6C00000000 avant : recouvrait WS_FB_VA et MAPC_BASE) */
#define WIN_SIZE IMG_WIN_SIZE
#define WIN_MAX IMG_WIN_MAX
#define IMG_PIN 1
#define IMG_FORUID 4 /* (uid 0) l'image d'un lancement sous un autre uid (SPAWN_AS) : uid dans les bits 32 à 63 */
#define IMS_ABORT 1
#define IML_PIN 1
#define IML_SYS 8

struct image {
	char path[PATH];
	uint64_t fsize, npages;
	uint64_t *pages;
	int pinned, sys;
	int shared;    /* une image de tous les comptes : épinglée par le système (préchargement de /apps), seule */
	int uid;       /* le compte dont les process la prennent (son créateur, ou l'uid de SPAWN_AS) */
	int valid;     /* dans la table : trouvée par SPAWN_PRELOADED */
	int loading;   /* pid qui la remplit (fenêtre), 0 : scellée */
	int window;
	int refs;      /* process lancés d'elle + 1 si épinglée */
	uint64_t rw_pages, lazy;
	uint8_t hash[32]; /* SHA-256 du fichier, calculé au premier SYS_PROC_EXE (le coffre) */
	volatile int hashed;
};

static struct image *imgs[IMG_MAX];
static spinlock_t img_lock = SPINLOCK_INIT;

struct __attribute__((packed)) ehdr {
	uint8_t ident[16];
	uint16_t type, machine;
	uint32_t version;
	uint64_t entry, phoff, shoff;
	uint32_t flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};

struct __attribute__((packed)) phdr {
	uint32_t type, flags;
	uint64_t offset, vaddr, paddr, filesz, memsz, align;
};

static int same_path(const char *a, const char *b)
{
	for (;; a++, b++) {
		char x = *a >= 'a' && *a <= 'z' ? *a - 32 : *a;
		char y = *b >= 'a' && *b <= 'z' ? *b - 32 : *b;
		if (x != y)
			return 0;
		if (!x)
			return 1;
	}
}

static void img_read(struct image *im, uint64_t off, void *dst, uint64_t len)
{
	uint8_t *d = dst;
	while (len) {
		uint64_t pg = off / PAGE_SIZE, in = off % PAGE_SIZE;
		uint64_t n = PAGE_SIZE - in < len ? PAGE_SIZE - in : len;
		memcpy(d, (uint8_t *)arch_phys_to_virt(im->pages[pg]) + in, n);
		d += n, off += n, len -= n;
	}
}

static void img_free(struct image *im)
{
	for (uint64_t i = 0; i < im->npages; i++)
		if (im->pages[i])
			pmm_free(im->pages[i]);
	kfree(im->pages);
	kfree(im);
}

void image_hold(struct image *im)
{
	if (!im)
		return;
	uint64_t f = spin_lock_irqsave(&img_lock);
	im->refs++;
	spin_unlock_irqrestore(&img_lock, f);
}

/* le chemin déclaré et l'empreinte SHA-256 du contenu (calculée une fois : les pages d'une image scellée ne
 * changent plus) ; l'appelant tient une référence */
/* l'image vient-elle d'un créateur aux droits de /sys (vfs, qui charge le fichier à chaque exec) ? Une image
 * créée par un compte (IMAGE_CREATE) au chemin de son choix : 0 */
int image_is_sys(const struct image *im)
{
	return im->sys != 0;
}

void image_identity(struct image *im, char *path, uint64_t n, uint8_t hash[32])
{
	memcpy(path, im->path, n < PATH ? n : PATH);
	path[n - 1] = 0;
	if (!im->hashed) {
		struct sha256 s;
		sha256_init(&s);
		for (uint64_t i = 0; i < im->npages; i++) {
			uint64_t n = im->fsize - i * PAGE_SIZE;
			if (n > PAGE_SIZE)
				n = PAGE_SIZE;
			sha256_update(&s, arch_phys_to_virt(im->pages[i]), n);
		}
		sha256_final(&s, im->hash);
		__atomic_store_n(&im->hashed, 1, __ATOMIC_RELEASE);
	}
	memcpy(hash, im->hash, 32);
}

/* une référence de moins (process fini, exec) ; rendue à la dernière */
void image_put(struct image *im)
{
	if (!im)
		return;
	uint64_t f = spin_lock_irqsave(&img_lock);
	--im->refs;
	if (!im->pinned && im->valid && im->refs == 1) { /* passagère : part avec son dernier process */
		im->valid = 0;
		im->refs = 0;
	}
	int last = im->refs <= 0;
	if (last)
		for (int i = 0; i < IMG_MAX; i++)
			if (imgs[i] == im)
				imgs[i] = 0;
	spin_unlock_irqrestore(&img_lock, f);
	if (last)
		img_free(im);
}

/* en-têtes : la table des segments tient dans la première page */
static int check_elf(struct image *im, struct ehdr *e)
{
	if (im->fsize < sizeof *e)
		return E_NOEXEC;
	img_read(im, 0, e, sizeof *e);
	if (memcmp(e->ident, "\x7f" "ELF", 4) || e->ident[4] != 2 || e->machine != arch_elf_machine() ||
	    e->type != 2 || e->phentsize < sizeof(struct phdr) ||
	    !range_in(e->phoff, (uint64_t)e->phnum * e->phentsize, PAGE_SIZE) ||
	    !range_in(e->phoff, (uint64_t)e->phnum * e->phentsize, im->fsize) || !user_addr_ok(e->entry))
		return E_NOEXEC;
	im->rw_pages = im->lazy = 0;
	for (int i = 0; i < e->phnum; i++) {
		struct phdr ph;
		img_read(im, e->phoff + (uint64_t)i * e->phentsize, &ph, sizeof ph);
		if (ph.type != 1 || !ph.memsz)
			continue;
		if (ph.filesz > ph.memsz || (ph.filesz && !range_in(ph.offset, ph.filesz, im->fsize)) || ph.vaddr < 0x10000 ||
		    ph.vaddr + ph.memsz > HEAP_LIMIT || ph.vaddr + ph.memsz < ph.vaddr)
			return E_NOEXEC;
		if (ph.flags & 2)
			im->rw_pages += ALIGN_UP(ph.vaddr + ph.filesz, PAGE_SIZE) / PAGE_SIZE -
					ALIGN_DOWN(ph.vaddr, PAGE_SIZE) / PAGE_SIZE;
		uint64_t lazy = ALIGN_UP(ph.vaddr + ph.filesz, PAGE_SIZE), end = ALIGN_UP(ph.vaddr + ph.memsz, PAGE_SIZE);
		if (end > lazy)
			im->lazy += end - lazy;
	}
	return 0;
}

int image_map(struct image *im, struct vm_space *vm, struct mm *mm, uint64_t *entry, uint64_t *top)
{
	struct ehdr e;
	if (check_elf(im, &e))
		return E_NOEXEC;
	*top = 0;
	for (int i = 0; i < e.phnum; i++) {
		struct phdr ph;
		img_read(im, e.phoff + (uint64_t)i * e.phentsize, &ph, sizeof ph);
		if (ph.type != 1 || !ph.memsz)
			continue;
		int w = (ph.flags & 2) != 0, x = (ph.flags & 1) != 0;
		uint64_t first = ALIGN_DOWN(ph.vaddr, PAGE_SIZE), end = ALIGN_UP(ph.vaddr + ph.memsz, PAGE_SIZE);
		uint64_t lazy = ALIGN_UP(ph.vaddr + ph.filesz, PAGE_SIZE);
		if (lazy < first)
			lazy = first;
		if (end > *top)
			*top = end;
		mm_note_image(mm, first);
		int congruent = (ph.vaddr % PAGE_SIZE) == (ph.offset % PAGE_SIZE);
		for (uint64_t va = first; va < lazy && va < end; va += PAGE_SIZE) {
			int fl = (w ? VM_W : 0) | (x ? VM_X : 0);
			if (vm_translate(vm, va)) /* page de deux segments : déjà là, droits réunis */
				continue;
			uint64_t fo = va - ph.vaddr + ph.offset;
			if (congruent && va >= ph.vaddr && va + PAGE_SIZE <= ph.vaddr + ph.filesz) {
				uint64_t pg = im->pages[fo / PAGE_SIZE];
				if (w) { /* copie sur écriture : une référence de plus */
					pmm_ref(pg);
					if (vm_map_page(vm, va, pg, fl | VM_COW)) {
						pmm_free(pg);
						return E_NOMEM;
					}
				} else if (vm_map_page(vm, va, pg, fl | VM_SHARED))
					return E_NOMEM;
				continue;
			}
			/* page à soi : la partie du fichier recopiée */
			uint64_t pg = pmm_alloc();
			if (!pg)
				return E_NOMEM;
			uint8_t *d = arch_phys_to_virt(pg);
			memset(d, 0, PAGE_SIZE);
			uint64_t lo = va > ph.vaddr ? va : ph.vaddr, hi = va + PAGE_SIZE;
			if (hi > ph.vaddr + ph.filesz)
				hi = ph.vaddr + ph.filesz;
			if (lo < hi)
				img_read(im, ph.offset + (lo - ph.vaddr), d + (lo - va), hi - lo);
			if (vm_map_page(vm, va, pg, fl)) {
				pmm_free(pg);
				return E_NOMEM;
			}
			mm_count_owned(mm, 1);
		}
		if (lazy < end) {
			int r = mm_add_region(mm, lazy, end, 1 | (w ? 2 : 0) | (x ? 4 : 0), VMK_BSS);
			if (r)
				return r;
		}
	}
	*entry = e.entry;
	return 0;
}

/* l'image valide de ce chemin et de cette taille, avec une référence de plus */
struct image *image_get(const char *path, uint64_t fsize, int need_sys, int uid)
{
	uint64_t f = spin_lock_irqsave(&img_lock);
	struct image *im = 0;
	for (int i = 0; i < IMG_MAX && !im; i++)
		if (imgs[i] && imgs[i]->valid && imgs[i]->fsize == fsize && same_path(imgs[i]->path, path) &&
		    (!need_sys || imgs[i]->sys) && (imgs[i]->shared || imgs[i]->uid == uid))
			im = imgs[i];
	if (im)
		im->refs++;
	spin_unlock_irqrestore(&img_lock, f);
	return im;
}

int64_t sys_image_create(uint64_t upath, uint64_t fsize, uint64_t flags)
{
	struct process *p = sched_current()->proc;
	if (kernel_option("no_preload"))
		return E_NOSYS;
	char path[PATH];
	if (copy_string_from_user(path, upath, PATH) <= 0)
		return E_INVAL;
	if (fsize < 64 || fsize > WIN_SIZE || !p->vm)
		return E_INVAL;
	/* décision du 25/11/2026 (tâche « préchargement », avec E33) : une image partagée entre les comptes, épinglée
	 * (gardée sans process), ne se crée que par le système (uid 0 avec droits : init2 au démarrage, d'après
	 * /etc/preload.txt, que seule l'élévation change) et seulement pour une app de /apps ; avant, n'importe quel
	 * process épinglait de la mémoire sans limite, et une image d'un créateur avec droits servait à tous */
	int pin = (flags & IMG_PIN) != 0;
	if (pin) {
		static const char apps[] = "/apps/";
		int in_apps = 1;
		for (int k = 0; apps[k] && in_apps; k++)
			in_apps = (path[k] | (path[k] >= 'A' && path[k] <= 'Z' ? 0x20 : 0)) == apps[k];
		for (const char *c = path; *c && in_apps; c++)
			in_apps = !(c[0] == '/' && c[1] == '.' && c[2] == '.');
		if (!rights_full(p) || p->uid != 0 || !in_apps)
			return E_PERM;
	}
	int uid = p->uid;
	if (flags & IMG_FORUID) {
		if (p->uid != 0)
			return E_PERM; /* (comme SPAWN_AS) */
		uid = (int)(uint32_t)(flags >> 32);
	}
	struct image *im = kzalloc(sizeof *im);
	if (!im)
		return E_NOMEM;
	memcpy(im->path, path, PATH);
	im->fsize = fsize;
	im->npages = ALIGN_UP(fsize, PAGE_SIZE) / PAGE_SIZE;
	im->pages = kzalloc(im->npages * 8);
	im->pinned = pin;
	im->sys = rights_full(p);
	im->shared = pin;
	im->uid = uid;
	im->loading = p->pid;
	if (!im->pages) {
		kfree(im);
		return E_NOMEM;
	}
	for (uint64_t i = 0; i < im->npages; i++) {
		im->pages[i] = pmm_alloc();
		if (!im->pages[i]) {
			img_free(im);
			return E_NOMEM;
		}
		memset(arch_phys_to_virt(im->pages[i]), 0, PAGE_SIZE);
	}
	/* une place dans la table, une fenêtre chez l'appelant */
	uint64_t f = spin_lock_irqsave(&img_lock);
	int slot = -1, used = 0;
	for (int i = 0; i < IMG_MAX; i++) {
		if (!imgs[i] && slot < 0)
			slot = i;
		else if (imgs[i] && imgs[i]->loading == p->pid)
			used |= 1 << imgs[i]->window;
	}
	int win = -1;
	for (int w = 0; w < WIN_MAX && win < 0; w++)
		if (!(used & (1 << w)))
			win = w;
	if (slot >= 0 && win >= 0) {
		im->window = win;
		imgs[slot] = im;
	}
	spin_unlock_irqrestore(&img_lock, f);
	if (slot < 0 || win < 0) {
		img_free(im);
		return E_NOSPC;
	}
	uint64_t base = WIN_BASE + (uint64_t)win * WIN_SIZE;
	for (uint64_t i = 0; i < im->npages; i++)
		vm_map_page(p->vm, base + i * PAGE_SIZE, im->pages[i], VM_W | VM_SHARED);
	return (int64_t)base;
}

int64_t sys_image_seal(uint64_t window, uint64_t flags)
{
	struct process *p = sched_current()->proc;
	uint64_t f = spin_lock_irqsave(&img_lock);
	struct image *im = 0;
	int slot = -1;
	for (int i = 0; i < IMG_MAX && !im; i++)
		if (imgs[i] && imgs[i]->loading == p->pid && WIN_BASE + (uint64_t)imgs[i]->window * WIN_SIZE == window)
			im = imgs[i], slot = i;
	spin_unlock_irqrestore(&img_lock, f);
	if (!im)
		return E_INVAL;
	for (uint64_t i = 0; i < im->npages; i++)
		vm_unmap_page(p->vm, window + i * PAGE_SIZE, 0);
	vm_tlb_shootdown(p->vm);
	struct ehdr e;
	int bad = (flags & IMS_ABORT) || check_elf(im, &e);
	f = spin_lock_irqsave(&img_lock);
	im->loading = 0;
	if (bad)
		imgs[slot] = 0;
	else {
		/* la même image d'avant, de la même classe (système, ou du même uid), n'est plus prise (les
		 * process lancés la gardent) ; un compte ne retire pas l'image d'un autre */
		for (int i = 0; i < IMG_MAX; i++)
			if (imgs[i] && imgs[i] != im && imgs[i]->valid && same_path(imgs[i]->path, im->path) &&
			    (im->shared ? imgs[i]->shared
					: !imgs[i]->shared && imgs[i]->sys == im->sys && imgs[i]->uid == im->uid)) {
				imgs[i]->valid = 0;
				if (--imgs[i]->refs <= 0) { /* la référence de la table */
					img_free(imgs[i]);
					imgs[i] = 0;
				}
			}
		im->valid = 1;
		im->refs = 1; /* la table */
	}
	spin_unlock_irqrestore(&img_lock, f);
	if (bad) {
		img_free(im);
		return E_INVAL;
	}
	return 0;
}

int64_t sys_image_forget(uint64_t upath)
{
	struct process *p = sched_current()->proc;
	int all = rights_used(p, RU_IMAGE_FORGET, "IMAGE_FORGET (de tous)") || p->uid == 0; /* les autres : seulement leurs images */
	char path[PATH];
	if (copy_string_from_user(path, upath, PATH) <= 0)
		return E_INVAL;
	int n = 0;
	struct image *drop[IMG_MAX];
	int nd = 0;
	uint64_t f = spin_lock_irqsave(&img_lock);
	for (int i = 0; i < IMG_MAX; i++)
		if (imgs[i] && imgs[i]->valid && same_path(imgs[i]->path, path) &&
		    (all || (!imgs[i]->shared && imgs[i]->uid == p->uid))) {
			imgs[i]->valid = 0;
			n++;
			if (--imgs[i]->refs <= 0) { /* la référence de la table */
				drop[nd++] = imgs[i];
				imgs[i] = 0;
			}
		}
	spin_unlock_irqrestore(&img_lock, f);
	for (int i = 0; i < nd; i++)
		img_free(drop[i]);
	return n;
}

int64_t sys_image_list(uint64_t buf, uint64_t max)
{
	if (max > IMG_MAX)
		max = IMG_MAX;
	uint64_t *e = kzalloc(max * 128 + 128);
	if (!e)
		return E_NOMEM;
	uint64_t n = 0;
	uint64_t f = spin_lock_irqsave(&img_lock);
	for (int pass = 0; pass < 2; pass++) /* les épinglées d'abord */
		for (int i = 0; i < IMG_MAX && n < max; i++) {
			struct image *im = imgs[i];
			if (!im || !im->valid || im->pinned != !pass)
				continue;
			uint64_t *x = &e[n++ * 16];
			x[0] = im->fsize;
			x[1] = im->npages;
			x[2] = im->rw_pages;
			x[3] = (uint64_t)im->refs;
			x[4] = im->lazy;
			x[5] = (im->pinned ? IML_PIN : 0) | (im->sys ? IML_SYS : 0);
			memcpy(&x[6], im->path, IML_PATH);
			((char *)&x[6])[IML_PATH - 1] = 0;
		}
	spin_unlock_irqrestore(&img_lock, f);
	int64_t r = copy_to_user(buf, e, n * 128) ? E_FAULT : (int64_t)n;
	kfree(e);
	return r;
}

/* IMAGE_PRELOAD (chemin, image, taille) : l'ancien chemin, depuis un tampon */
int64_t sys_image_preload(uint64_t upath, uint64_t image, uint64_t size)
{
	struct process *p = sched_current()->proc;
	if (!rights_used(p, RU_IMAGE_PRELOAD, "IMAGE_PRELOAD"))
		return E_PERM;
	if (kernel_option("no_preload"))
		return E_NOSYS;
	if (!image)
		return 0; /* le préchargement est actif */
	int64_t win = sys_image_create(upath, size, IMG_PIN);
	if (win < 0)
		return win;
	uint8_t chunk[512];
	for (uint64_t off = 0; off < size; off += sizeof chunk) {
		uint64_t n = size - off < sizeof chunk ? size - off : sizeof chunk;
		if (copy_from_user(chunk, image + off, n) || copy_to_user((uint64_t)win + off, chunk, n)) {
			sys_image_seal((uint64_t)win, IMS_ABORT);
			return E_FAULT;
		}
	}
	return sys_image_seal((uint64_t)win, 0);
}

/* fin d'un process : ses chargements jamais scellés sont rendus */
void image_proc_exit(struct process *p)
{
	struct image *drop[IMG_MAX];
	int nd = 0;
	uint64_t f = spin_lock_irqsave(&img_lock);
	for (int i = 0; i < IMG_MAX; i++)
		if (imgs[i] && imgs[i]->loading == p->pid) {
			drop[nd++] = imgs[i];
			imgs[i] = 0;
		}
	spin_unlock_irqrestore(&img_lock, f);
	for (int i = 0; i < nd; i++)
		img_free(drop[i]);
	uint64_t g = sched_lock_take(); /* (SYS_PROC_EXE lit p->image sous ce verrou) */
	struct image *im = p->image;
	p->image = 0;
	sched_lock_give(g);
	image_put(im);
}
