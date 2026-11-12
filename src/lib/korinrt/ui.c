/* ui.c : lot « ui » (docs/BIN-C.md) : appel d'une fonction de kit qui rend
 * plusieurs registres, comme un programme en assembleur qui lit x0..x3 après
 * « bl ui_w_event » ou « bl ui_surface_pixels ».
 *
 * Sur AArch64, le relais du kit (asm, ou C avec kit.conf « ret = ») rend
 * x0..x3 et garde les autres registres ; on appelle par une ligne d'asm et
 * on lit les quatre. Sur x86-64, la fonction C du kit rend une structure de
 * quatre long en mémoire (premier argument caché) : on l'appelle avec ce
 * type-là (le relais d'importation garde rdi et saute à la fonction). */
#include "korinrt.h"

struct krt_r4 krt_call_r4(long (*fn)(), long a0, long a1, long a2, long a3)
{
	struct krt_r4 r;
#if defined(__aarch64__)
	register long x0 __asm__("x0") = a0;
	register long x1 __asm__("x1") = a1;
	register long x2 __asm__("x2") = a2;
	register long x3 __asm__("x3") = a3;
	register long (*x9)() __asm__("x9") = fn;
	__asm__ volatile("blr x9"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x9)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x10", "x11", "x12", "x13", "x14", "x15", "x16",
			   "x17", "x30", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17",
			   "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28",
			   "v29", "v30", "v31", "memory", "cc");
	r.r0 = x0;
	r.r1 = x1;
	r.r2 = x2;
	r.r3 = x3;
#else
	r = ((struct krt_r4(*)(long, long, long, long))(void *)fn)(a0, a1, a2, a3);
#endif
	return r;
}
