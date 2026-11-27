/* dmatest : les pages DMA d'un process fini, mises de côté par le noyau jusqu'à DMA_RELEASE (étape 3 de
 * docs/etude/DURCISSEMENT-N3-N5.md). À lancer par le moniteur, avec droits :
 *   « run dmatest »            DMA_ALLOC de 4 pages, remplies, puis fin : le noyau les met de côté (10 s au plus)
 *   « run dmatest release N »  DMA_RELEASE (N) : les pages du process N rendues -> 0 (E_SRCH : rien de mis de côté)
 * Sans bibliothèque. */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;

static inline i64 sc(u64 n, u64 a, u64 b, u64 c)
{
	i64 ret;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
	return ret;
}

static void put(const char *s)
{
	u64 n = 0;
	while (s[n])
		n++;
	sc(SYS_PRINT, (u64)s, n, 0);
}

static void putn(i64 v)
{
	char b[24];
	int i = 23, neg = v < 0;
	u64 u = neg ? (u64)-v : (u64)v;
	b[i] = 0;
	do
		b[--i] = (char)('0' + u % 10);
	while (u /= 10);
	if (neg)
		b[--i] = '-';
	put(b + i);
}

__attribute__((force_align_arg_pointer, noreturn)) void _start(const char *line)
{
	if (line && line[0] == 'r') { /* « release N » */
		const char *p = line;
		while (*p && *p != ' ')
			p++;
		while (*p == ' ')
			p++;
		i64 pid = 0;
		while (*p >= '0' && *p <= '9')
			pid = pid * 10 + (*p++ - '0');
		i64 r = sc(SYS_DMA_RELEASE, (u64)pid, 0, 0);
		put("dmatest : DMA_RELEASE (");
		putn(pid);
		put(") -> ");
		putn(r);
		put("\n");
		sc(SYS_EXIT, r == 0 ? 0 : 1, 0, 0);
		for (;;)
			;
	}
	u64 phys = 0;
	i64 va = sc(SYS_DMA_ALLOC, 4 * 4096, (u64)&phys, 0);
	put("dmatest : pid ");
	putn(sc(SYS_GETPID, 0, 0, 0));
	put(", DMA_ALLOC -> ");
	putn(va > 0 ? 0 : va);
	put(" ; fin : les 4 pages mises de côté\n");
	if (va > 0)
		for (int i = 0; i < 4 * 4096; i++)
			((volatile uint8_t *)va)[i] = 0x5A;
	sc(SYS_EXIT, va > 0 ? 0 : 1, 0, 0);
	for (;;)
		;
}
