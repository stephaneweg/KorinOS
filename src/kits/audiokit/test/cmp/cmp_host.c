/* Banc différentiel d'AudioKit : chargement de la référence en asm, relais,
 * mémoire des deux côtés, sorties et comparaison (voir cmp.h). */
#define _GNU_SOURCE
#include <sys/mman.h>

#include "cmp.h"

int cmp_has_ref;

/* ---- mémoire du C (comme ak_alloc de audiokit.asm : pages à zéro, taille
 * rangée 16 octets avant) */
void *ak_alloc(uint64_t n)
{
	uint64_t sz = (n + 16 + 4095) & ~(uint64_t)4095;
	void *p = mmap(0, sz, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return 0;
	*(uint64_t *)p = sz;
	return (uint8_t *)p + 16;
}

void ak_free(void *p)
{
	if (!p)
		return;
	uint8_t *b = (uint8_t *)p - 16;
	munmap(b, *(uint64_t *)b);
}

/* fonctions de audiokit.c que les codecs appellent (pas de son ici) */
long ak_sf_default(void)
{
	return 0;
}
long ak_fm_live(long fm, long out)
{
	(void)fm;
	(void)out;
	return -38;
}

#if defined(__aarch64__)

static uint8_t *ref_base;

void *refaddr(int idx)
{
	if (!ref_base)
		return 0;
	return ref_base + ((uint32_t *)ref_base)[2 + idx];
}

/* refcall(fn, a0..a6, [sp] = a7) : garde x18-x28, d8-d15 autour de l'asm */
long refcall(void *fn, long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7);
__asm__(".text\n"
	".global refcall\n"
	"refcall:\n"
	"	sub sp, sp, #192\n"
	"	stp x29, x30, [sp]\n"
	"	mov x29, sp\n"
	"	stp x18, x19, [sp, #16]\n"
	"	stp x20, x21, [sp, #32]\n"
	"	stp x22, x23, [sp, #48]\n"
	"	stp x24, x25, [sp, #64]\n"
	"	stp x26, x27, [sp, #80]\n"
	"	str x28, [sp, #96]\n"
	"	stp d8, d9, [sp, #112]\n"
	"	stp d10, d11, [sp, #128]\n"
	"	stp d12, d13, [sp, #144]\n"
	"	stp d14, d15, [sp, #160]\n"
	"	ldr x9, [sp, #192]\n"
	"	mov x16, x0\n"
	"	mov x0, x1\n"
	"	mov x1, x2\n"
	"	mov x2, x3\n"
	"	mov x3, x4\n"
	"	mov x4, x5\n"
	"	mov x5, x6\n"
	"	mov x6, x7\n"
	"	mov x7, x9\n"
	"	blr x16\n"
	"	ldp x18, x19, [sp, #16]\n"
	"	ldp x20, x21, [sp, #32]\n"
	"	ldp x22, x23, [sp, #48]\n"
	"	ldp x24, x25, [sp, #64]\n"
	"	ldp x26, x27, [sp, #80]\n"
	"	ldr x28, [sp, #96]\n"
	"	ldp d8, d9, [sp, #112]\n"
	"	ldp d10, d11, [sp, #128]\n"
	"	ldp d12, d13, [sp, #144]\n"
	"	ldp d14, d15, [sp, #160]\n"
	"	ldp x29, x30, [sp]\n"
	"	add sp, sp, #192\n"
	"	ret\n");

/* mémoire de l'asm : comme audiokit.asm (appel système qui ne touche que x0) */
void host_alloc(void);
void host_free(void);
void host_nosys(void);
void host_ret(void);
__asm__(".text\n"
	".global host_alloc\n"
	"host_alloc:\n"
	"	stp x1, x2, [sp, #-64]!\n"
	"	stp x3, x4, [sp, #16]\n"
	"	stp x5, x8, [sp, #32]\n"
	"	add x1, x0, #16\n"
	"	add x1, x1, #4095\n"
	"	and x1, x1, #~4095\n"
	"	str x1, [sp, #48]\n"
	"	mov x0, #0\n"
	"	mov x2, #3\n"
	"	mov x3, #0x22\n"
	"	mov x4, #-1\n"
	"	mov x5, #0\n"
	"	mov x8, #222\n"
	"	svc #0\n"
	"	cmn x0, #4096\n"
	"	b.hi 1f\n"
	"	ldr x1, [sp, #48]\n"
	"	str x1, [x0]\n"
	"	add x0, x0, #16\n"
	"	b 2f\n"
	"1:	mov x0, #0\n"
	"2:	ldp x3, x4, [sp, #16]\n"
	"	ldp x5, x8, [sp, #32]\n"
	"	ldp x1, x2, [sp], #64\n"
	"	ret\n"
	".global host_free\n"
	"host_free:\n"
	"	cbz x0, 1f\n"
	"	stp x1, x8, [sp, #-16]!\n"
	"	sub x0, x0, #16\n"
	"	ldr x1, [x0]\n"
	"	mov x8, #215\n"
	"	svc #0\n"
	"	ldp x1, x8, [sp], #16\n"
	"1:	ret\n"
	".global host_nosys\n"
	"host_nosys:\n"
	"	mov x0, #-38\n"
	"	ret\n"
	".global host_ret\n"
	"host_ret:\n"
	"	ret\n");

/* relais des rappels : garde x1-x18 et q0-q31, appelle cb_tab[slot] */
void *cb_tab[8];
#define CBTHUNK(n)                                   \
	".global cbthunk" #n "\n"                    \
	"cbthunk" #n ":\n"                           \
	"	mov x16, #" #n "\n"                  \
	"	b cbthunk_common\n"
void cbthunk0(void), cbthunk1(void), cbthunk2(void), cbthunk3(void);
void cbthunk4(void), cbthunk5(void), cbthunk6(void), cbthunk7(void);
__asm__(".text\n" CBTHUNK(0) CBTHUNK(1) CBTHUNK(2) CBTHUNK(3) CBTHUNK(4) CBTHUNK(5) CBTHUNK(6) CBTHUNK(7)
	"cbthunk_common:\n"
	"	sub sp, sp, #672\n"
	"	stp x29, x30, [sp]\n"
	"	mov x29, sp\n"
	"	stp x1, x2, [sp, #16]\n"
	"	stp x3, x4, [sp, #32]\n"
	"	stp x5, x6, [sp, #48]\n"
	"	stp x7, x8, [sp, #64]\n"
	"	stp x9, x10, [sp, #80]\n"
	"	stp x11, x12, [sp, #96]\n"
	"	stp x13, x14, [sp, #112]\n"
	"	stp x15, x16, [sp, #128]\n"
	"	stp x17, x18, [sp, #144]\n"
	"	add x17, sp, #160\n"
	"	st1 {v0.2d-v3.2d}, [x17], #64\n"
	"	st1 {v4.2d-v7.2d}, [x17], #64\n"
	"	st1 {v8.2d-v11.2d}, [x17], #64\n"
	"	st1 {v12.2d-v15.2d}, [x17], #64\n"
	"	st1 {v16.2d-v19.2d}, [x17], #64\n"
	"	st1 {v20.2d-v23.2d}, [x17], #64\n"
	"	st1 {v24.2d-v27.2d}, [x17], #64\n"
	"	st1 {v28.2d-v31.2d}, [x17], #64\n"
	"	adrp x17, cb_tab\n"
	"	add x17, x17, :lo12:cb_tab\n"
	"	ldr x17, [x17, x16, lsl #3]\n"
	"	blr x17\n"
	"	add x17, sp, #160\n"
	"	ld1 {v0.2d-v3.2d}, [x17], #64\n"
	"	ld1 {v4.2d-v7.2d}, [x17], #64\n"
	"	ld1 {v8.2d-v11.2d}, [x17], #64\n"
	"	ld1 {v12.2d-v15.2d}, [x17], #64\n"
	"	ld1 {v16.2d-v19.2d}, [x17], #64\n"
	"	ld1 {v20.2d-v23.2d}, [x17], #64\n"
	"	ld1 {v24.2d-v27.2d}, [x17], #64\n"
	"	ld1 {v28.2d-v31.2d}, [x17], #64\n"
	"	ldp x1, x2, [sp, #16]\n"
	"	ldp x3, x4, [sp, #32]\n"
	"	ldp x5, x6, [sp, #48]\n"
	"	ldp x7, x8, [sp, #64]\n"
	"	ldp x9, x10, [sp, #80]\n"
	"	ldp x11, x12, [sp, #96]\n"
	"	ldp x13, x14, [sp, #112]\n"
	"	ldp x15, x16, [sp, #128]\n"
	"	ldp x17, x18, [sp, #144]\n"
	"	ldp x29, x30, [sp]\n"
	"	add sp, sp, #672\n"
	"	ret\n");

void *refcb(int ref, int slot, void *fn)
{
	static void (*const th[8])(void) = {cbthunk0, cbthunk1, cbthunk2, cbthunk3,
					    cbthunk4, cbthunk5, cbthunk6, cbthunk7};
	if (!ref)
		return fn;
	cb_tab[slot] = fn;
	return (void *)th[slot];
}

long akcall(void *fn, int ref, long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7)
{
	if (ref)
		return refcall(fn, a0, a1, a2, a3, a4, a5, a6, a7);
	return ((long (*)(long, long, long, long, long, long, long, long))fn)(a0, a1, a2, a3, a4, a5, a6, a7);
}

static void ref_load(void)
{
	size_t n;
	uint8_t *f = load("build/audiokit_cmp/akref.bin", &n);
	if (!f) {
		fprintf(stderr, "build/audiokit_cmp/akref.bin absent (mkref.py)\n");
		exit(2);
	}
	ref_base = mmap(0, (n + 4095) & ~4095UL, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS,
			-1, 0);
	memcpy(ref_base, f, n);
	free(f);
	if (((uint32_t *)ref_base)[0] != REF_COUNT) {
		fprintf(stderr, "akref.bin et akref_syms.h ne vont pas ensemble\n");
		exit(2);
	}
	void **host = (void **)(ref_base + REF_HOST_OFF);
	for (int i = 0; i < REF_HOSTS; i++)
		host[i] = (void *)host_nosys;
	host[REFH_ak_alloc] = (void *)host_alloc;
	host[REFH_ak_free] = (void *)host_free;
	host[REFH_h_dbg] = (void *)host_ret;
	__builtin___clear_cache((char *)ref_base, (char *)ref_base + n);
	cmp_has_ref = 1;
}

#else /* x86-64 : le C seulement */

void *refaddr(int idx)
{
	(void)idx;
	return 0;
}
void *refcb(int ref, int slot, void *fn)
{
	(void)ref;
	(void)slot;
	return fn;
}
long akcall(void *fn, int ref, long a0, long a1, long a2, long a3, long a4, long a5, long a6, long a7)
{
	(void)ref;
	return ((long (*)(long, long, long, long, long, long, long, long))fn)(a0, a1, a2, a3, a4, a5, a6, a7);
}
static void ref_load(void)
{
}

#endif

/* ---- sorties */

uint32_t crc32(const void *p, size_t n)
{
	static uint32_t t[256];
	if (!t[1])
		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;
			for (int k = 0; k < 8; k++)
				c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			t[i] = c;
		}
	uint32_t c = ~0u;
	const uint8_t *b = p;
	for (size_t i = 0; i < n; i++)
		c = t[(c ^ b[i]) & 255] ^ (c >> 8);
	return ~c;
}

uint8_t *load(const char *path, size_t *n)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t *b = malloc(sz + 64);
	if (fread(b, 1, sz, f) != (size_t)sz) {
		fclose(f);
		free(b);
		return 0;
	}
	memset(b + sz, 0, 64);
	fclose(f);
	*n = sz;
	return b;
}

struct rec {
	char name[64];
	uint8_t *data;
	size_t n;
};
static struct rec *recs[2];
static int nrec[2], caprec[2];
static int pass; /* 0 : asm (ref), 1 : C */
static int ndiff, nout;

void OUT(const char *name, const void *p, size_t n)
{
	if (nrec[pass] == caprec[pass]) {
		caprec[pass] = caprec[pass] ? caprec[pass] * 2 : 256;
		recs[pass] = realloc(recs[pass], caprec[pass] * sizeof(struct rec));
	}
	struct rec *r = &recs[pass][nrec[pass]++];
	snprintf(r->name, sizeof r->name, "%s", name);
	r->data = malloc(n ? n : 1);
	if (n)
		memcpy(r->data, p, n);
	r->n = n;
}

void OUTV(const char *name, long v)
{
	OUT(name, &v, 8);
}

static void show(const struct rec *r)
{
	if (r->n == 8)
		printf("%s = %ld\n", r->name, *(long *)r->data);
	else
		printf("%s : %zu octets, crc %08x\n", r->name, r->n, crc32(r->data, r->n));
}

void run_test(const char *name, void (*t)(int ref))
{
	for (int p = 0; p < 2; p++) {
		for (int i = 0; i < nrec[p]; i++)
			free(recs[p][i].data);
		nrec[p] = 0;
	}
	if (cmp_has_ref) {
		pass = 0;
		t(1);
	}
	pass = 1;
	t(0);
	printf("== %s\n", name);
	for (int i = 0; i < nrec[1]; i++) {
		struct rec *c = &recs[1][i];
		show(c);
		nout++;
		if (!cmp_has_ref)
			continue;
		if (i >= nrec[0]) {
			printf("!! DIFF %s : absent de l'asm\n", c->name);
			ndiff++;
			continue;
		}
		struct rec *a = &recs[0][i];
		if (strcmp(a->name, c->name) || a->n != c->n || memcmp(a->data, c->data, a->n)) {
			ndiff++;
			printf("!! DIFF %s : asm ", c->name);
			show(a);
			size_t k = 0, m = a->n < c->n ? a->n : c->n;
			while (k < m && a->data[k] == c->data[k])
				k++;
			printf("!!   premier écart à l'octet %zu (asm %zu octets, C %zu)", k, a->n, c->n);
			if (k < m) {
				printf(" : asm");
				for (size_t j = k; j < k + 16 && j < a->n; j++)
					printf(" %02x", a->data[j]);
				printf(" / C");
				for (size_t j = k; j < k + 16 && j < c->n; j++)
					printf(" %02x", c->data[j]);
			}
			printf("\n");
		}
	}
	if (cmp_has_ref && nrec[0] > nrec[1]) {
		printf("!! DIFF %s : %d sorties de plus dans l'asm\n", name, nrec[0] - nrec[1]);
		ndiff++;
	}
	fflush(stdout);
}

int cmp_end(void)
{
	if (cmp_has_ref)
		printf("== %d sorties, %d écarts\n", nout, ndiff);
	else
		printf("== %d sorties (C seulement)\n", nout);
	return ndiff != 0;
}

__attribute__((constructor)) static void cmp_init(void)
{
	ref_load();
}
