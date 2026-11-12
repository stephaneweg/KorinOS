/* sigtest - essai des signaux du noyau (src/kernel/signal.asm, docs/ABI.md)
 * et de membarrier, sans libposix, comme sigtest.asm. Chaque essai affiche
 * son nom s'il échoue ; sortie : « sigtest : PASS / FAIL », code = nombre
 * d'échecs.
 *
 * Les fautes sont provoquées en asm (fonctions de ce fichier) :
 *  - lecture à une adresse hors du programme (0x10) : le gestionnaire (en
 *    asm : il change un registre flottant) relève l'adresse, change un
 *    registre gardé (x19 / rbx) dans l'ucontext, saute l'instruction (pc) et
 *    écrase v8 / xmm8, qui doit revenir à sa valeur après le gestionnaire ;
 *  - instruction illégale (UDF #0 / ud2) ;
 *  - débordement de pile : sp / rsp = 0x10 et une écriture, le gestionnaire
 *    tourne sur la pile de signaux (SA_ONSTACK).
 * L'ucontext suit Linux : AArch64 x19 à +184+19*8, pc à +440 ; x86-64
 * (src/kernel/arch/x86_64/sigframe.c) rbx à +40+11*8, rip à +40+16*8. Les
 * instructions sautées font 4 octets sur AArch64 ; sur x86-64, 3 (lecture),
 * 2 (ud2) et 8 (écriture sur la pile), écrites en octets. */
#include "korinrt.h"

#define ST_SIG 30 /* (SIGUSR1 de newlib) */
#define ST_CHLD 20 /* (SIGCHLD de newlib) */
#define SIGILL 4
#define SIGSEGV 11
#define E_INTR -4
#define BAD_ADDR 0x10 /* hors de l'espace du programme */
#define ALT_SIZE 16384
#define TH_STACK 32768
#define SIGM_SET 0
#define SIGM_BLOCK 1
#define SIGM_UNBLOCK 2
#define SIGM_PENDING 3
#define SIGM_SUSPEND 4
#define SIGM_WAIT 5
#define SA_ONSTACK 4
#define MB_QUERY 0
#define MB_PRIVATE 8
#define MB_REGISTER 16
#define MB_KNOWN (8 | 16 | 32 | 64)
#define SIF_ADDR 16

#if defined(__aarch64__)
#define UC_PC 440
#define ILL_LEN 4
#define STACK_LEN 4
#else
#define UC_PC (40 + 16 * 8)
#define ILL_LEN 2
#define STACK_LEN 8
#endif

/* les compteurs (seen de l'asm), lus par l'asm du gestionnaire de faute */
struct seen {
	long count, arg, addr, ill, alt, ready, last, tid, stop, go, fails;
};
volatile struct seen st_seen;
static long main_tid;
u8 st_alt_stack[ALT_SIZE] __attribute__((aligned(16)));
static u8 th_stack[TH_STACK] __attribute__((aligned(16)));

/* en asm : sig_return (le retour des gestionnaires), on_segv (gestionnaire de
 * faute), et les fautes elles-mêmes */
void st_sig_return(void);
void st_on_segv(long sig, void *info, void *uc);
long st_fault_read(u32 *fp_byte); /* -> x19 / rbx après la faute ; *fp_byte = octet 3 de v8 / xmm8 */
void st_fault_ill(void);
void st_fault_stack(void);
_Static_assert(SYS_SIG_RETURN == 0x2D, "numéro de st_sig_return");
_Static_assert(__builtin_offsetof(struct seen, addr) == 16, "seen.addr");

#if defined(__aarch64__)
__asm__(".text\n"
	".p2align 2\n"
	".global st_sig_return\n"
	"st_sig_return:\n"
	"	mov x8, #0x2D\n"
	"	svc 0\n"
	"	b .\n"
	".p2align 2\n"
	".global st_on_segv\n"
	"st_on_segv:\n"
	"	adrp x9, st_seen\n"
	"	add x9, x9, :lo12:st_seen\n"
	"	ldr x3, [x1, 16]\n"		/* SIF_ADDR */
	"	str x3, [x9, 16]\n"		/* seen.addr */
	"	mov x3, #0x66\n"
	"	str x3, [x2, 184 + 19 * 8]\n"	/* UCX_REGS + x19 */
	"	ldr x3, [x2, 440]\n"		/* UCX_PC : après l'instruction fautive */
	"	add x3, x3, 4\n"
	"	str x3, [x2, 440]\n"
	"	movi v8.16b, 0x11\n"
	"	ret\n"
	".p2align 2\n"
	".global st_fault_read\n"
	"st_fault_read:\n"
	"	stp x19, x0, [sp, -32]!\n"
	"	str d8, [sp, 16]\n"
	"	mov x19, #0x55\n"
	"	mov x1, #0x10\n"		/* BAD_ADDR */
	"	movi v8.16b, 0x5A\n"		/* (le gestionnaire les écrase) */
	"	ldr x0, [x1]\n"			/* faute : l'instruction est sautée */
	"	umov w0, v8.b[3]\n"
	"	ldr x1, [sp, 8]\n"
	"	str w0, [x1]\n"
	"	mov x0, x19\n"
	"	ldr d8, [sp, 16]\n"
	"	ldp x19, x1, [sp], 32\n"
	"	ret\n"
	".p2align 2\n"
	".global st_fault_ill\n"
	"st_fault_ill:\n"
	"	.word 0\n"			/* (UDF) */
	"	ret\n"
	".p2align 2\n"
	".global st_fault_stack\n"
	"st_fault_stack:\n"
	"	mov x9, sp\n"
	"	mov x10, #0x10\n"
	"	mov sp, x10\n"
	"	str xzr, [sp]\n"		/* faute : la pile est perdue */
	"	mov sp, x9\n"
	"	ret\n");
#else
__asm__(".text\n"
	".p2align 4\n"
	".global st_sig_return\n"
	"st_sig_return:\n"
	"	mov $0x2D, %eax\n"
	"	syscall\n"
	"	jmp .\n"
	".p2align 4\n"
	".global st_on_segv\n"
	"st_on_segv:\n"
	"	mov 16(%rsi), %rax\n"		/* adresse de la faute */
	"	mov %rax, st_seen+16(%rip)\n"
	"	movq $0x66, 40+11*8(%rdx)\n"	/* gregs : rbx */
	"	addq $3, 40+16*8(%rdx)\n"	/* gregs : rip, après la lecture fautive */
	"	mov $0x11111111, %eax\n"
	"	movd %eax, %xmm8\n"
	"	pshufd $0, %xmm8, %xmm8\n"
	"	ret\n"
	".p2align 4\n"
	".global st_fault_read\n"
	"st_fault_read:\n"
	"	push %rbx\n"
	"	mov $0x55, %ebx\n"
	"	mov $0x5A5A5A5A, %eax\n"
	"	movd %eax, %xmm8\n"
	"	pshufd $0, %xmm8, %xmm8\n"
	"	mov $0x10, %ecx\n"
	"	.byte 0x48, 0x8b, 0x01\n"	/* mov (%rcx), %rax : faute, sautée (3 octets) */
	"	movd %xmm8, %eax\n"
	"	shr $24, %eax\n"
	"	mov %eax, (%rdi)\n"
	"	mov %rbx, %rax\n"
	"	pop %rbx\n"
	"	ret\n"
	".p2align 4\n"
	".global st_fault_ill\n"
	"st_fault_ill:\n"
	"	ud2\n"
	"	ret\n"
	".p2align 4\n"
	".global st_fault_stack\n"
	"st_fault_stack:\n"
	"	mov %rsp, %r9\n"
	"	mov $0x10, %esp\n"
	"	.byte 0x48, 0xc7, 0x04, 0x24, 0, 0, 0, 0\n" /* movq $0, (%rsp) : faute (8 octets) */
	"	mov %r9, %rsp\n"
	"	ret\n");
#endif

static long sig_action(long sig, void *h, long flags)
{
	return korin_sys6(SYS_SIG_ACTION, sig, (long)h, flags, 0, (long)st_sig_return, 0);
}

static long sig_send(long pid, long tid, long sig)
{
	return korin_sys3(SYS_SIG_SEND, pid, tid, sig);
}

static long sig_mask(long how, long set)
{
	return korin_sys2(SYS_SIG_MASK, how, set);
}

static void failed(const char *name)
{
	ak_puts("sigtest : echec : ");
	ak_puts(name);
	ak_puts("\n");
	st_seen.fails++;
}

static void check(int ok, const char *name)
{
	if (!ok)
		failed(name);
}

/* (gestionnaires : signal, siginfo, ucontext ; retour par st_sig_return) */
static void on_count(long sig, void *info, void *uc)
{
	(void)info;
	(void)uc;
	st_seen.arg = sig;
	__atomic_fetch_add(&st_seen.count, 1, __ATOMIC_SEQ_CST);
	st_seen.last = korin_sys0(SYS_THREAD_SELF);
}

static void on_ill(long sig, void *info, u8 *uc)
{
	(void)sig;
	(void)info;
	st_seen.ill = 1;
	*(u64 *)(uc + UC_PC) += ILL_LEN;
}

static void on_stack(long sig, void *info, u8 *uc)
{
	(void)sig;
	(void)info;
	u64 sp = (u64)__builtin_frame_address(0); /* sur la pile de signaux ? */
	if (sp - (u64)st_alt_stack < ALT_SIZE)
		st_seen.alt = 1;
	*(u64 *)(uc + UC_PC) += STACK_LEN;
}

/* le thread d'essai : tourne en attendant, puis envoie les signaux demandés */
#if defined(__x86_64__)
__attribute__((force_align_arg_pointer))
#endif
static NORETURN void worker(void)
{
	st_seen.ready = 1;
	long t;
	while (!(t = st_seen.last)) /* signal reçu ici ? */
		;
	st_seen.tid = t;
	while (!st_seen.stop)
		;
	while (!st_seen.go)
		;
	sys_sleep(5);
	sig_send(0, main_tid, ST_SIG); /* le thread principal */
	while (st_seen.go != 2)
		;
	sys_sleep(20);
	sig_send(0, main_tid, ST_SIG);
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

static void finish(void)
{
	long f = st_seen.fails;
	ak_puts(f ? "sigtest : FAIL\n" : "sigtest : PASS\n");
	_exit((int)f);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long r;
	main_tid = korin_sys0(SYS_THREAD_SELF);

	/* --- un gestionnaire, le signal envoyé à soi */
	check(sig_action(ST_SIG, on_count, 0) == 0, "sig_action");
	sig_send(0, 0, ST_SIG);
	check(st_seen.count == 1, "signal a soi");
	check(st_seen.arg == ST_SIG, "argument du gestionnaire");

	/* --- masque : en attente, puis livré au démasquage */
	sig_mask(SIGM_BLOCK, 1L << ST_SIG);
	sig_send(0, 0, ST_SIG);
	check(st_seen.count == 1, "signal masque livre");
	check((sig_mask(SIGM_PENDING, 0) & (1L << ST_SIG)) != 0, "signal masque pas en attente");
	sig_mask(SIGM_UNBLOCK, 1L << ST_SIG);
	check(st_seen.count == 2, "demasquage");

	/* --- faute : adresse, registres et pc changés par l'ucontext */
	sig_action(SIGSEGV, st_on_segv, 0);
	u32 fp = 0;
	long reg = st_fault_read(&fp);
	check(st_seen.addr == BAD_ADDR, "adresse de la faute");
	check(reg == 0x66, "registre change par ucontext"); /* x19 / rbx changé par le gestionnaire */
	check(fp == 0x5A, "FP rendu apres le gestionnaire");

	/* --- instruction illégale */
	sig_action(SIGILL, on_ill, 0);
	st_fault_ill();
	check(st_seen.ill == 1, "instruction illegale");

	/* --- débordement de pile : sur la pile de signaux */
	korin_sys3(SYS_SIG_ALTSTACK, st_alt_stack, ALT_SIZE, 0);
	sig_action(SIGSEGV, on_stack, SA_ONSTACK);
	st_fault_stack();
	check(st_seen.alt == 1, "pile de signaux");

	/* --- signal vers un thread précis, sommeil et attente interrompus */
	st_seen.last = 0;
	long th = korin_sys3(SYS_THREAD_CREATE, worker, th_stack + TH_STACK, 0);
	if (th < 0) {
		failed("thread_create");
		finish();
	}
	while (!st_seen.ready) /* il tourne */
		;
	check(sig_send(0, th, ST_SIG) == 0, "envoi a un thread");
	long t;
	while (!(t = st_seen.tid))
		;
	check(t == th, "signal recu par le bon thread");
	check(sig_send(0, 99999, ST_SIG) == -3, "thread inconnu"); /* pas un thread inconnu */

	/* --- membarrier, un thread tourne sur un autre cœur */
	check(korin_sys2(SYS_MEMBARRIER, MB_QUERY, 0) == MB_KNOWN, "membarrier query");
	check(korin_sys2(SYS_MEMBARRIER, MB_REGISTER, 0) == 0, "membarrier register");
	for (int i = 1000; i; i--)
		check(korin_sys2(SYS_MEMBARRIER, MB_PRIVATE, 0) == 0, "membarrier private");
	st_seen.stop = 1; /* le thread s'arrête de tourner */

	/* --- SIGM_SUSPEND : son signal, envoyé par le thread */
	long old = sig_mask(SIGM_BLOCK, 1L << ST_SIG);
	st_seen.go = 1;
	check(sig_mask(SIGM_SUSPEND, 0) == E_INTR, "sigsuspend");
	check(st_seen.count == 4, "sigsuspend : gestionnaire"); /* (2 + le thread + celui-ci) */
	r = sig_mask(SIGM_SET, old); /* masque revenu : encore bloqué */
	check((r & (1L << ST_SIG)) != 0, "sigsuspend : masque rendu");

	/* --- sommeil interrompu */
	st_seen.go = 2;
	check(sys_sleep(5000) == E_INTR, "sommeil interrompu");

	/* --- SIGM_WAIT */
	sig_mask(SIGM_BLOCK, 1L << ST_SIG);
	sig_send(0, 0, ST_SIG);
	check(sig_mask(SIGM_WAIT, 1L << ST_SIG) == ST_SIG, "sigwait");

	/* --- SIGCHLD : avec gestionnaire, puis sans (rien ne doit l'interrompre) */
	sig_mask(SIGM_UNBLOCK, 1L << ST_SIG);
	sig_action(ST_CHLD, on_count, 0);
	st_seen.count = 0;
	st_seen.arg = 0;
	long c = ksys(SYS_SPAWN, (long)"SD0:/bin/echo", 0, 0, 0, 0, 0);
	if (c < 0) {
		failed("SIGCHLD");
		finish();
	}
	korin_sys1(SYS_WAIT, c);
	for (int i = 50; i && !st_seen.count; i--)
		sys_sleep(10);
	check(st_seen.arg == ST_CHLD, "SIGCHLD");
	sig_action(ST_CHLD, 0, 0);
	c = ksys(SYS_SPAWN, (long)"SD0:/bin/echo", 0, 0, 0, 0, 0);
	if (c < 0) {
		failed("SIGCHLD");
		finish();
	}
	check(sys_sleep(300) != E_INTR, "SIGCHLD sans gestionnaire");
	korin_sys1(SYS_WAIT, c);
	finish();
}
