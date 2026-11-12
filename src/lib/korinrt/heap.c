/* heap.c : le tas des commandes (docs/BIN-C.md). Blocs par classes de
 * puissances de deux (32 o à 1 Mo, en-tête de 16 o compris), pris au tas du
 * process (ak_malloc = SYS_SBRK) et gardés dans une liste par classe quand
 * ils sont rendus ; au-delà, une région à soi (SYS_VM_MAP), rendue par
 * free. Un seul thread : pas de verrou. */
#include "korinrt.h"

#define CLASS_MIN 5  /* 32 octets */
#define CLASS_MAX 20 /* 1 Mo */
#define BIG 0xFF

struct head {
	u64 size;  /* octets utiles (demandés) */
	u32 cls;   /* classe, BIG : région */
	u32 magic;
};
#define MAGIC 0x4B524848

static struct head *free_list[CLASS_MAX + 1];

void *malloc(size_t n)
{
	u64 need = (u64)n + sizeof(struct head);
	if (n > ((u64)1 << 40))
		return 0;
	struct head *h;
	u32 c = CLASS_MIN;
	while (c <= CLASS_MAX && ((u64)1 << c) < need)
		c++;
	if (c > CLASS_MAX) {
		u64 sz = (need + 4095) & ~(u64)4095;
		long r = korin_sys4(SYS_VM_MAP, 0, sz, PROT_READ | PROT_WRITE, 0);
		if (r < 0 && r > -4096)
			return 0;
		h = (struct head *)r;
		c = BIG;
	} else if (free_list[c]) {
		h = free_list[c];
		free_list[c] = *(struct head **)(h + 1);
	} else {
		h = ak_malloc((u64)1 << c);
		if (!h)
			return 0;
	}
	h->size = n;
	h->cls = c;
	h->magic = MAGIC;
	return h + 1;
}

void free(void *p)
{
	if (!p)
		return;
	struct head *h = (struct head *)p - 1;
	if (h->magic != MAGIC)
		return;
	h->magic = 0;
	if (h->cls == BIG) {
		korin_sys2(SYS_VM_UNMAP, h, (h->size + sizeof(struct head) + 4095) & ~(u64)4095);
		return;
	}
	*(struct head **)(h + 1) = free_list[h->cls];
	free_list[h->cls] = h;
}

void *calloc(size_t n, size_t m)
{
	if (m && n > ((u64)1 << 40) / m)
		return 0;
	void *p = malloc(n * m);
	if (p)
		memset(p, 0, n * m);
	return p;
}

void *realloc(void *p, size_t n)
{
	if (!p)
		return malloc(n);
	struct head *h = (struct head *)p - 1;
	if (h->cls != BIG && n + sizeof(struct head) <= ((u64)1 << h->cls)) {
		h->size = n;
		return p;
	}
	void *q = malloc(n);
	if (!q)
		return 0;
	memcpy(q, p, h->size < n ? h->size : n);
	free(p);
	return q;
}
