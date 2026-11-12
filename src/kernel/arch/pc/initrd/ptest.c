/* ptest : essais des process du noyau x86-64 (signaux, minuterie, fork avec
 * copie sur écriture, exec, groupes, arrêt et reprise). Sans bibliothèque. */
#include <stdint.h>

#include <korin/syscall_nr.h>

typedef uint64_t u64;
typedef int64_t i64;

static inline i64 sc(u64 n, u64 a, u64 b, u64 c, u64 d, u64 e)
{
	register u64 r10 __asm__("r10") = d;
	register u64 r8 __asm__("r8") = e;
	i64 ret;
	__asm__ volatile("syscall" : "=a"(ret) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
			 : "rcx", "r11", "memory");
	return ret;
}
#define S0(n) sc(n, 0, 0, 0, 0, 0)
#define S1(n, a) sc(n, (u64)(a), 0, 0, 0, 0)
#define S2(n, a, b) sc(n, (u64)(a), (u64)(b), 0, 0, 0)
#define S3(n, a, b, c) sc(n, (u64)(a), (u64)(b), (u64)(c), 0, 0)
#define S4(n, a, b, c, d) sc(n, (u64)(a), (u64)(b), (u64)(c), (u64)(d), 0)
#define S5(n, a, b, c, d, e) sc(n, (u64)(a), (u64)(b), (u64)(c), (u64)(d), (u64)(e))

/* retour d'un gestionnaire : SIG_RETURN (0x2D) */
__asm__(".globl sig_restorer\nsig_restorer:\n\tmov $0x2D, %eax\n\tsyscall\n");
void sig_restorer(void);

static void put(const char *s)
{
	u64 n = 0;
	while (s[n])
		n++;
	S2(SYS_PRINT, s, n);
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
	put(&b[i]);
}

static int fails, passes;

static void check(const char *what, int ok, i64 got)
{
	if (ok) {
		passes++;
		return;
	}
	fails++;
	put("ptest : ÉCHEC ");
	put(what);
	put(" (");
	putn(got);
	put(")\n");
}

#define SIGINT 2
#define SIGSEGV 11
#define SIGALRM 14
#define SIGTERM 15
#define SIGSTOP 17
#define SIGCONT 19
#define SIGCHLD 20
#define SIGUSR1 30
#define WAIT_EX(o) ((0x57414954ULL << 32) | (o))

static volatile int got_usr1, got_alrm, got_chld, got_segv;
static volatile u64 seen_sig, seen_addr;
static volatile u64 shared_word = 1;

static void on_sig(int sig, void *si, void *uc)
{
	(void)uc;
	seen_sig = (u64)sig;
	if (sig == SIGUSR1)
		got_usr1++;
	if (sig == SIGALRM)
		got_alrm++;
	if (sig == SIGCHLD)
		got_chld++;
	(void)si;
}

static void after_fault(void);

static void on_segv(int sig, void *si, void *uc)
{
	(void)sig;
	got_segv++;
	seen_addr = *(u64 *)((char *)si + 16);
	/* reprendre ailleurs : rip de l'ucontext (gregs[16]) */
	*(u64 *)((char *)uc + 40 + 16 * 8) = (u64)after_fault;
}

static void handle(int sig, void (*h)(int, void *, void *))
{
	S5(SYS_SIG_ACTION, sig, h, 0, 0, sig_restorer);
}

/* un ELF minuscule : exit(42) */
static const uint8_t tiny_elf[132] = {
	0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	2, 0, 0x3E, 0, 1, 0, 0, 0, 0x78, 0, 0x40, 0, 0, 0, 0, 0,  /* type, machine, version, entrée */
	64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,          /* phoff, shoff */
	0, 0, 0, 0, 64, 0, 56, 0, 1, 0, 0, 0, 0, 0, 0, 0,          /* flags, ehsize, phentsize, phnum */
	1, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,            /* PT_LOAD, R|X, offset 0 */
	0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 0x40, 0, 0, 0, 0, 0,     /* vaddr, paddr */
	132, 0, 0, 0, 0, 0, 0, 0, 132, 0, 0, 0, 0, 0, 0, 0,        /* filesz, memsz */
	0, 0x10, 0, 0, 0, 0, 0, 0,                                 /* align */
	0xBF, 42, 0, 0, 0, 0xB8, 0x10, 0, 0, 0, 0x0F, 0x05,       /* mov edi,42 ; mov eax,0x10 ; syscall */
};

__attribute__((force_align_arg_pointer, noreturn)) void _start(void)
{
	put("ptest : essais des process\n");


	/* signaux à soi */
	handle(SIGUSR1, on_sig);
	S3(SYS_SIG_SEND, 0, 0, SIGUSR1);
	check("signal à soi", got_usr1 == 1 && seen_sig == SIGUSR1, got_usr1);
	S2(SYS_SIG_MASK, 1, 1ULL << SIGUSR1);
	S3(SYS_SIG_SEND, 0, 0, SIGUSR1);
	check("signal masqué : en attente", got_usr1 == 1 && (S2(SYS_SIG_MASK, 3, 0) & (1ULL << SIGUSR1)), 0);
	S2(SYS_SIG_MASK, 2, 1ULL << SIGUSR1);
	check("démasqué : livré", got_usr1 == 2, got_usr1);
	S5(SYS_SIG_ACTION, SIGUSR1, 1, 0, 0, 0);
	S3(SYS_SIG_SEND, 0, 0, SIGUSR1);
	check("signal ignoré", got_usr1 == 2, got_usr1);
	check("SIGKILL ne se prend pas", S5(SYS_SIG_ACTION, 9, on_sig, 0, 0, 0) == -22, 0);

	/* faute prise par un gestionnaire */
	handle(SIGSEGV, on_segv);
	*(volatile int *)0x1234 = 1;
	__builtin_unreachable();
}

static void after_fault(void)
{
	check("SIGSEGV pris, adresse", got_segv == 1 && seen_addr == 0x1234, (i64)seen_addr);

	/* minuterie : SIGALRM toutes les 20 ms, interrompt un SLEEP */
	handle(SIGALRM, on_sig);
	S4(SYS_SETITIMER, 0, 20000, 20000, 0);
	i64 sl = S1(SYS_SLEEP, 1000);
	check("SLEEP interrompu (E_INTR)", sl == -4 && got_alrm >= 1, sl);
	while (got_alrm < 3)
		S0(SYS_YIELD);
	u64 old[2];
	S4(SYS_SETITIMER, 0, 0, 0, old);
	check("SETITIMER périodique", got_alrm >= 3 && old[1] == 20000, (i64)old[1]);

	/* fork : copie sur écriture */
	handle(SIGCHLD, on_sig);
	shared_word = 5;
	i64 pid = S0(SYS_FORK);
	if (pid == 0) {
		shared_word = 99; /* copie pour le fils seulement */
		S1(SYS_EXIT, (i64)shared_word - 66);
	}
	check("fork", pid > 0, pid);
	check("WAIT du fils", S3(SYS_WAIT, pid, 0, 0) == 33, 0);
	check("copie sur écriture : le père garde sa valeur", shared_word == 5, (i64)shared_word);
	for (int i = 0; i < 50 && !got_chld; i++)
		S1(SYS_SLEEP, 2);
	check("SIGCHLD au père", got_chld >= 1, got_chld);
	check("GETPPID dans le père", S0(SYS_GETPPID) >= 1, 0);

	/* fork + exec d'une image en mémoire */
	pid = S0(SYS_FORK);
	if (pid == 0) {
		u64 params[9] = {(u64) "tiny", 0, 0, 0, 0, 0, (u64)tiny_elf, 0, 0};
		S3(SYS_EXEC, "tiny", sizeof tiny_elf, params);
		S1(SYS_EXIT, 1); /* exec raté */
	}
	check("exec : code du programme neuf", S3(SYS_WAIT, pid, 0, 0) == 42, 0);

	/* groupes, arrêt et reprise, SIGTERM */
	pid = S0(SYS_FORK);
	if (pid == 0) {
		for (;;)
			S1(SYS_SLEEP, 5);
	}
	check("SETPGID du fils", S2(SYS_SETPGID, pid, pid) == 0 && S1(SYS_GETPGID, pid) == pid, S1(SYS_GETPGID, pid));
	check("même session", S1(SYS_GETPGID, pid | (1ULL << 62)) == S1(SYS_GETPGID, (1ULL << 62)), 0);
	S3(SYS_SIG_SEND, pid, 0, SIGSTOP);
	i64 st = 0;
	i64 w = S3(SYS_WAIT, pid, WAIT_EX(2), &st);
	check("arrêt vu par WAIT (WR_STOPPED)", w == 2 && st == SIGSTOP, w);
	S3(SYS_SIG_SEND, pid, 0, SIGCONT);
	w = S3(SYS_WAIT, pid, WAIT_EX(8), &st);
	check("reprise vue par WAIT (WR_CONTINUED)", w == 3, w);
	check("WEX_NOHANG : rien", S3(SYS_WAIT, pid, WAIT_EX(1), &st) == 0, 0);
	S3(SYS_SIG_SEND, -pid, 0, SIGTERM); /* au groupe */
	check("SIGTERM au groupe : code -15", S3(SYS_WAIT, pid, 0, 0) == -SIGTERM, 0);
	check("pid fini : plus de signal", S3(SYS_SIG_SEND, pid, 0, 0) == -3, 0);
	check("setsid refusé au meneur de groupe", S2(SYS_SETPGID, -1, 0) == -1, 0);

	put("ptest : ");
	putn(passes);
	put(" ok, ");
	putn(fails);
	put(" échec(s)\n");
	S1(SYS_EXIT, fails);
	for (;;)
		;
}
