/* mediakit : les appels systeme d'ARMPI et AppKit (docs/ABI.md, src/inc/syscalls.inc) */
#ifndef MK_SYS_H
#define MK_SYS_H
#include <stddef.h>
#include <stdint.h>

#define MK_SYS_EXIT		0x10
#define MK_SYS_VM_MAP		0x06
#define MK_SYS_VM_UNMAP		0x07
#define MK_SYS_SLEEP		0x17
#define MK_SYS_YIELD		0x18
#define MK_SYS_LIB_OPEN		0x64
#define MK_SYS_PRINT		0x60
/* fichiers : servis par AppKit (fichier_sys, rang 13 de sa table) */
#define MK_SYS_OPEN		0x80
#define MK_SYS_READ		0x81
#define MK_SYS_SEEK		0x83
#define MK_SYS_CLOSE		0x84
#define MK_O_READ		1

static inline long mk_svc(long n, long a, long b, long c, long d)
{
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x8 __asm__("x8") = n;
	__asm__ volatile("svc 0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) : "r"(x8) : "memory", "cc");
	return x0;
}

static inline long mk_vm_map(size_t n) { return mk_svc(MK_SYS_VM_MAP, 0, (long)n, 3, 0); }
static inline void mk_vm_unmap(void *p, size_t n) { mk_svc(MK_SYS_VM_UNMAP, (long)p, (long)n, 0, 0); }
static inline void mk_yield(void) { mk_svc(MK_SYS_YIELD, 0, 0, 0, 0); }
static inline void mk_exit(int c) { for (;;) mk_svc(MK_SYS_EXIT, c, 0, 0, 0); }

long mk_fsys(long n, long a, long b, long c);	/* fichier_sys d'AppKit */
void mk_log(const char *s);			/* une ligne sur la sortie standard */
#endif
