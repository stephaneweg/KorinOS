/* fputest - 4 threads qui calculent en flottant double précision et
 * vérifient leurs registres vectoriels pendant 3 s (préemptions entre
 * threads et entre process), comme fputest.asm. Le thread principal travaille
 * aussi (thread 0), puis attend la fin des autres par futex. Sortie : une
 * ligne par thread (somme de 1/i² x 1e6, erreurs de registres), puis le total
 * des erreurs, qui est aussi le code de sortie.
 *
 * La boucle est en asm en ligne, comme l'asm : sur AArch64, les registres
 * témoins v8, v9, v15, v31 portent le motif du thread (v8.d[1], v15.d[0] et
 * v31.d[1] vérifiés à chaque tour, v9 seulement posé, comme l'asm) ; sur
 * x86-64, xmm8, xmm9, xmm15 (moitié haute de xmm8, basse de xmm15, haute de
 * xmm9 à la place de v31). Le calcul est fait en d0-d3 (xmm0-xmm3), l'heure
 * lue par un appel système dans la boucle toutes les 65 536 itérations.
 * « ² » est l'octet 0xB2 (latin-1), comme l'asm. */
#include "korinrt.h"

#define NB_THREADS 4
#define STACK_SIZE 16384

#if defined(__x86_64__)
#define THREAD_ATTR __attribute__((force_align_arg_pointer, noreturn))
#else
#define THREAD_ATTR __attribute__((noreturn))
#endif

static u32 done_count;
static u64 total_errors;

/* motif -> erreurs ; *sum = la somme */
static u64 fpu_loop(u64 pat, double *sum)
{
	u64 err, dl;
	double s;
#if defined(__aarch64__)
	__asm__ volatile(
		"dup	v8.2d, %[pat]\n\t" /* registres « témoins » */
		"mov	v9.16b, v8.16b\n\t"
		"mov	v15.16b, v8.16b\n\t"
		"mov	v31.16b, v8.16b\n\t"
		"mov	x8, %[nr]\n\t"
		"svc	0\n\t"
		"add	%[dl], x0, 3000\n\t" /* échéance */
		"mov	%[err], 0\n\t"
		"fmov	d0, 1.0\n\t" /* somme de 1/i^2 (-> pi^2/6) */
		"fmov	d1, 1.0\n\t"
		"fmov	d2, 1.0\n\t" /* terme i=1 */
		"1:	fadd	d1, d1, d0\n\t" /* i += 1 */
		"fmul	d3, d1, d1\n\t"
		"fdiv	d3, d0, d3\n\t"
		"fadd	d2, d2, d3\n\t"
		"umov	x0, v8.d[1]\n\t"
		"umov	x1, v15.d[0]\n\t"
		"umov	x2, v31.d[1]\n\t"
		"cmp	x0, %[pat]\n\t"
		"ccmp	x1, %[pat], 0, eq\n\t"
		"ccmp	x2, %[pat], 0, eq\n\t"
		"b.eq	2f\n\t"
		"add	%[err], %[err], 1\n\t"
		"dup	v8.2d, %[pat]\n\t"
		"mov	v15.16b, v8.16b\n\t"
		"mov	v31.16b, v8.16b\n\t"
		"2:	fcvtzs	x0, d1\n\t"
		"tst	x0, 0xFFFF\n\t"
		"b.ne	1b\n\t"
		"mov	x8, %[nr]\n\t"
		"svc	0\n\t"
		"cmp	x0, %[dl]\n\t"
		"b.lo	1b\n\t"
		"fmov	%d[s], d2\n\t"
		: [err] "=&r"(err), [dl] "=&r"(dl), [s] "=w"(s)
		: [pat] "r"(pat), [nr] "i"(SYS_TIME_MS)
		: "x0", "x1", "x2", "x8", "v0", "v1", "v2", "v3", "v8", "v9", "v15", "v31", "cc", "memory");
#else
	__asm__ volatile(
		"movq	%[pat], %%xmm8\n\t" /* registres « témoins » */
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		"movdqa	%%xmm8, %%xmm9\n\t"
		"movdqa	%%xmm8, %%xmm15\n\t"
		"mov	%[nr], %%eax\n\t"
		"syscall\n\t"
		"lea	3000(%%rax), %[dl]\n\t" /* échéance */
		"xor	%[err], %[err]\n\t"
		"mov	$0x3FF0000000000000, %%rax\n\t" /* 1.0 */
		"movq	%%rax, %%xmm0\n\t"
		"movq	%%rax, %%xmm1\n\t"
		"movq	%%rax, %%xmm2\n\t"
		"1:	addsd	%%xmm0, %%xmm1\n\t" /* i += 1 */
		"movapd	%%xmm1, %%xmm3\n\t"
		"mulsd	%%xmm1, %%xmm3\n\t"
		"movapd	%%xmm0, %%xmm4\n\t"
		"divsd	%%xmm3, %%xmm4\n\t"
		"addsd	%%xmm4, %%xmm2\n\t"
		"pshufd	$0xEE, %%xmm8, %%xmm5\n\t"
		"movq	%%xmm5, %%rax\n\t"
		"movq	%%xmm15, %%rdx\n\t"
		"pshufd	$0xEE, %%xmm9, %%xmm5\n\t"
		"movq	%%xmm5, %%rsi\n\t"
		"cmp	%[pat], %%rax\n\t"
		"jne	3f\n\t"
		"cmp	%[pat], %%rdx\n\t"
		"jne	3f\n\t"
		"cmp	%[pat], %%rsi\n\t"
		"je	2f\n\t"
		"3:	inc	%[err]\n\t"
		"movq	%[pat], %%xmm8\n\t"
		"punpcklqdq %%xmm8, %%xmm8\n\t"
		"movdqa	%%xmm8, %%xmm9\n\t"
		"movdqa	%%xmm8, %%xmm15\n\t"
		"2:	cvttsd2si %%xmm1, %%rax\n\t"
		"test	$0xFFFF, %%eax\n\t"
		"jne	1b\n\t"
		"mov	%[nr], %%eax\n\t"
		"syscall\n\t"
		"cmp	%[dl], %%rax\n\t"
		"jb	1b\n\t"
		"movsd	%%xmm2, %[s]\n\t"
		: [err] "=&r"(err), [dl] "=&r"(dl), [s] "=x"(s)
		: [pat] "r"(pat), [nr] "i"(SYS_TIME_MS)
		: "rax", "rcx", "rdx", "rsi", "r11", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm8", "xmm9",
		  "xmm15", "cc", "memory");
#endif
	*sum = s;
	return err;
}

/* index : 3 s de calcul + vérification des registres, puis la ligne du
 * résultat, le cumul des erreurs et le signalement de fin */
static void worker_body(long idx)
{
	u64 pat = (u64)(idx + 1) * 0x1111111111111111UL; /* motif propre au thread */
	double sum;
	u64 err = fpu_loop(pat, &sum);
	char line[128];
	char *o = str_cat(line, "fputest : thread ");
	*o++ = (char)('0' + idx);
	*o = 0;
	o = str_cat(o, "  somme 1/i\xB2 x1e6 = ");
	o = fmt_dec((u64)(long)(sum * 1000000.0), o);
	o = str_cat(o, "  erreurs registres = ");
	o = fmt_dec(err, o);
	str_cat(o, "\n");
	ak_puts(line);
	__atomic_fetch_add(&total_errors, err, __ATOMIC_SEQ_CST);
	__atomic_fetch_add(&done_count, 1, __ATOMIC_SEQ_CST);
	korin_sys2(SYS_FUTEX_WAKE, &done_count, NB_THREADS);
}

THREAD_ATTR static void worker(long idx)
{
	worker_body(idx);
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	ak_puts("fputest : 4 threads NEON pendant 3 s...\n");
	for (long i = 1; i < NB_THREADS; i++) {
		u8 *stack = ak_malloc(STACK_SIZE); /* pile du thread sur le tas */
		if (!stack || korin_sys3(SYS_THREAD_CREATE, worker, stack + STACK_SIZE, i) < 0) {
			ak_puts("fputest : echec creation de thread\n");
			korin_sys1(SYS_EXIT, 1);
		}
	}
	worker_body(0); /* le thread principal travaille aussi */
	for (;;) { /* attente des autres (futex) */
		u32 n = __atomic_load_n(&done_count, __ATOMIC_SEQ_CST);
		if (n >= NB_THREADS)
			break;
		korin_sys3(SYS_FUTEX_WAIT, &done_count, n, 0); /* (sans délai) */
	}
	ak_puts("fputest : total erreurs = ");
	print_dec((long)total_errors);
	ak_puts("\n");
	for (;;)
		korin_sys1(SYS_EXIT, (long)total_errors);
}
