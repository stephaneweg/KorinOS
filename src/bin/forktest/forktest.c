/* forktest - SYS_FORK (docs/ABI.md, 0x6A ; étude docs/TERMINAL-ET-FORK.md),
 * comme forktest.asm : valeurs rendues, mémoire séparée, registres (FP,
 * TPIDR_EL0) du fils, tube ouvert avant le fork entre père et fils (fin quand
 * les deux écrivains ferment), wait, deuxième génération, poignée d'IPC
 * héritée, fichier ouvert avant le fork (position partagée), fuites de pages
 * sur N fork + exit.
 *   forktest            les essais (PASS / FAIL, résumé à la fin, code = FAIL)
 *   forktest <n>        idem, n fork + exit dans la boucle de fuites (1000)
 *
 * Comme l'asm :
 *  - le fork des essais est le fork brut du noyau (SYS_FORK par svc/syscall,
 *    sans AppKit) ; seul l'essai du fichier partagé passe par f_fork d'AppKit
 *    (la macro sys de l'asm, ici ksys) ;
 *  - n vient de la ligne brute du noyau (krt_line), chiffres en tête ;
 *  - la longueur du message du canal est celle de « canal » et des chaînes qui
 *    le suivent dans l'asm (s_canal_n est calculée après s_abc) : 46 octets,
 *    « canal\0RAM0:/forktest.tmp\0SD0:/forktest.tmp\0ABC ».
 * L'essai des registres du fils est en asm : AArch64 x21, d9 (s9) et
 * TPIDR_EL0 ; x86-64 r12 et xmm9 (pas de TPIDR_EL0 : la base de fs n'est pas
 * à la main d'un programme). */
#include "korinrt.h"

#define LOOP_DEF 1000
#define PROC_RIGHTS 0x100
#define IPC_SOCK_SEQPACKET 5
#define IPCM_SIZE 32
#define VS_FREE_LOW 48
#define VS_FREE_HIGH 56

/* (dans l'ordre des chaînes de l'asm : s_canal_n court jusqu'à la fin de « ABC ») */
static const char s_blob[] = "canal\0RAM0:/forktest.tmp\0SD0:/forktest.tmp\0ABC";
#define S_CANAL s_blob
#define S_CANAL_N (sizeof(s_blob) - 1)
#define S_FIC_RAM (s_blob + 6)
#define S_FIC_SD (s_blob + 25)
#define S_ABC (s_blob + 43)
static const char s_pere[] = "du pere ; ";
static const char s_fils[] = "du fils.\n";
#define S_PERE_N (sizeof(s_pere) - 1)
#define S_FILS_N (sizeof(s_fils) - 1)

static long loops;
static volatile long marque;
static long fds[2], fds2[2];
static u32 chan[2];
static u64 iov[2];
static u8 ipcm[IPCM_SIZE] __attribute__((aligned(8)));
static long n_pass, n_fail;
static u64 stats[8];
static char buf[1024];
static volatile u8 gros[8192];

/* fork brut du noyau : pid (père), 0 (fils), < 0 erreur (macro fork) */
static long kfork(void)
{
	return korin_sys0(SYS_FORK);
}

/* l'essai 1 en asm : registres posés, fork brut ; le fils vérifie et sort
 * (42 : tout est bon, 1 : x21/r12, 2 : d9/xmm9, 3 : TPIDR_EL0) ; le père
 * rend le pid et *kept = 1 si son registre est gardé */
long fork_regs(long *kept);
#if defined(__aarch64__)
__asm__(".text\n"
	".p2align 2\n"
	".type fork_regs, %function\n"
	"fork_regs:\n"
	"	stp x29, x30, [sp, -48]!\n"
	"	stp x20, x21, [sp, 16]\n"
	"	str d9, [sp, 32]\n"
	"	str x0, [sp, 40]\n"
	"	movz w0, #0xF5C3\n"
	"	movk w0, #0x4048, lsl 16\n"
	"	fmov s9, w0\n"		/* FP : d9 du fils = celui du père */
	"	mrs x20, tpidr_el0\n"	/* TPIDR_EL0 : celui du père */
	"	movz x0, #0xAB1E\n"
	"	movk x0, #0x7E57, lsl 16\n"
	"	msr tpidr_el0, x0\n"
	"	movz x21, #0x7788\n"	/* registre gardé */
	"	movk x21, #0x5566, lsl 16\n"
	"	movk x21, #0x3344, lsl 32\n"
	"	movk x21, #0x1122, lsl 48\n"
	"	mov x8, #0x6A\n"	/* SYS_FORK */
	"	svc 0\n"
	"	cbz x0, 1f\n"
	"	msr tpidr_el0, x20\n"	/* (père : le sien) */
	"	movz x2, #0x7788\n"
	"	movk x2, #0x5566, lsl 16\n"
	"	movk x2, #0x3344, lsl 32\n"
	"	movk x2, #0x1122, lsl 48\n"
	"	cmp x21, x2\n"
	"	cset x1, eq\n"
	"	ldr x3, [sp, 40]\n"
	"	str x1, [x3]\n"
	"	ldr d9, [sp, 32]\n"
	"	ldp x20, x21, [sp, 16]\n"
	"	ldp x29, x30, [sp], 48\n"
	"	ret\n"
	"1:	mov x1, #42\n"		/* fils */
	"	movz x2, #0x7788\n"
	"	movk x2, #0x5566, lsl 16\n"
	"	movk x2, #0x3344, lsl 32\n"
	"	movk x2, #0x1122, lsl 48\n"
	"	cmp x21, x2\n"
	"	mov x2, #1\n"
	"	csel x1, x2, x1, ne\n"
	"	fmov w2, s9\n"
	"	movz w3, #0xF5C3\n"
	"	movk w3, #0x4048, lsl 16\n"
	"	cmp w2, w3\n"
	"	mov x2, #2\n"
	"	csel x1, x2, x1, ne\n"
	"	mrs x2, tpidr_el0\n"
	"	movz x3, #0xAB1E\n"
	"	movk x3, #0x7E57, lsl 16\n"
	"	cmp x2, x3\n"
	"	mov x2, #3\n"
	"	csel x1, x2, x1, ne\n"
	"	mov x0, x1\n"
	"	mov x8, #0x10\n"	/* SYS_EXIT */
	"	svc 0\n"
	"	b .\n"
	".size fork_regs, . - fork_regs\n");
#else
__asm__(".text\n"
	".p2align 4\n"
	".type fork_regs, @function\n"
	"fork_regs:\n"
	"	push %r12\n"
	"	push %rdi\n"
	"	mov $0x4048F5C3, %eax\n"
	"	movd %eax, %xmm9\n"	/* FP : xmm9 du fils = celui du père */
	"	movabs $0x1122334455667788, %r12\n" /* registre gardé */
	"	mov $0x6A, %eax\n"	/* SYS_FORK */
	"	syscall\n"
	"	test %rax, %rax\n"
	"	jz 1f\n"
	"	movabs $0x1122334455667788, %rdx\n"
	"	xor %ecx, %ecx\n"
	"	cmp %rdx, %r12\n"
	"	sete %cl\n"
	"	pop %rdi\n"
	"	mov %rcx, (%rdi)\n"
	"	pop %r12\n"
	"	ret\n"
	"1:	mov $42, %edi\n"	/* fils */
	"	movabs $0x1122334455667788, %rdx\n"
	"	mov $1, %ecx\n"
	"	cmp %rdx, %r12\n"
	"	cmovne %ecx, %edi\n"
	"	movd %xmm9, %edx\n"
	"	mov $2, %ecx\n"
	"	cmp $0x4048F5C3, %edx\n"
	"	cmovne %ecx, %edi\n"
	"	mov $0x10, %eax\n"	/* SYS_EXIT */
	"	syscall\n"
	"	jmp .\n"
	".size fork_regs, . - fork_regs\n");
#endif
_Static_assert(SYS_FORK == 0x6A && SYS_EXIT == 0x10, "numéros de fork_regs");

static void check(int ok, const char *name)
{
	ak_puts(ok ? "PASS " : "FAIL ");
	ak_puts(name);
	ak_puts("\n");
	if (ok)
		n_pass++;
	else
		n_fail++;
}

static int starts(const char *s, const char *p)
{
	while (*p)
		if (*p++ != *s++)
			return 0;
	return 1;
}

static long sc(long nr, long a, long b, long c)
{
	return ksys(nr, a, b, c, 0, 0, 0);
}

static void ipcm_set(const void *p, u64 n)
{
	iov[0] = (u64)p;
	iov[1] = n;
	*(u64 *)(ipcm + 0) = (u64)iov; /* IPCM_IOV */
	*(u32 *)(ipcm + 8) = 1;        /* IPCM_IOVCNT */
	*(u32 *)(ipcm + 12) = 0;       /* IPCM_NH */
	*(u64 *)(ipcm + 16) = 0;
	*(u64 *)(ipcm + 24) = 0;
}

/* chemin : écrit A, fork (f_fork d'AppKit), le fils écrit B, le père attend
 * puis écrit C ; relu depuis 0 -> 1 si « ABC » (fichier effacé ensuite) */
static long shared_file(const char *path)
{
	long r = 0;
	long f = sys_open(path, O_READ | O_WRITE | O_CREATE | O_TRUNC);
	if (f < 0)
		return 0;
	sys_write(f, S_ABC, 1);
	long p = ksys(SYS_FORK, 0, 0, 0, 0, 0, 0);
	if (p == 0) { /* fils : B à la position du père */
		long w = sys_write(f, S_ABC + 1, 1);
		_exit(w != 1 ? 95 : 5);
	}
	if (p >= 0) {
		long code = korin_sys1(SYS_WAIT, p);
		sys_write(f, S_ABC + 2, 1);
		sys_seek(f, 0, 0);
		*(u64 *)buf = 0;
		if (sys_read(f, buf, 8) == 3 && code == 5)
			r = starts(buf, S_ABC);
	}
	sys_close(f);
	sys_unlink(path);
	return r;
}

/* tours : fork, le fils sort (code 5), wait -> fork refusés, *bad = codes faux */
static long fork_loop(long n, long *bad)
{
	long refused = 0, wrong = 0;
	for (; n; n--) {
		long p = kfork();
		if (p == 0)
			_exit(5);
		if (p < 0) {
			refused++;
			sys_sleep(10);
		} else if (korin_sys1(SYS_WAIT, p) != 5)
			wrong++;
		sys_sleep(1); /* (les avis de fin aux services) */
	}
	*bad = wrong;
	return refused;
}

/* -> pages libres du système (VM_STATS) */
static long free_pages(void)
{
	korin_sys2(SYS_VM_STATS, 0, stats);
	return (long)(stats[VS_FREE_LOW / 8] + stats[VS_FREE_HIGH / 8]);
}

static void print_sdec(long v)
{
	if (v < 0) {
		ak_puts("-");
		v = -v;
	}
	print_dec(v);
}

/* chiffres en tête -> nombre (0 si aucun) (lire_dec) */
static long read_dec(const char *s)
{
	long v = 0;
	while ((u8)(*s - '0') <= 9)
		v = v * 10 + (*s++ - '0');
	return v;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long n = LOOP_DEF;
	if (krt_line) {
		n = read_dec(krt_line);
		if (n <= 0)
			n = LOOP_DEF;
	}
	loops = n;
	ak_puts("forktest : SYS_FORK (copie sur ecriture)\n");

	/* --- 0. droits : un programme de SD0:/sys est refusé */
	if (korin_sys0(SYS_PROC_FLAGS) & PROC_RIGHTS)
		check(kfork() == -1, "fork avec les droits de SD0:/sys : E_PERM");
	else
		ak_puts("forktest : sans droits de SD0:/sys (refus E_PERM non essaye)\n");

	/* --- 1. valeurs rendues, registres du fils */
	long kept = 0;
	long p = fork_regs(&kept);
	check(p > 0, "fork rend le pid du fils au pere");
	check(kept == 1, "pere : registres gardes");
	check(korin_sys1(SYS_WAIT, p) == 42, "fils : fork rend 0, x21, d9 et TPIDR_EL0 du pere (code 42 par wait)");

	/* --- 2. mémoire séparée */
	marque = 1;
	gros[4000] = 0x55; /* une page du .bss déjà touchée */
	p = kfork();
	if (p == 0) { /* fils : lit 1, écrit 2, relit */
		if (marque != 1)
			_exit(99);
		marque = 2;
		if (gros[4000] != 0x55)
			_exit(99);
		gros[4000] = 0xAA;
		_exit((int)marque);
	}
	check(korin_sys1(SYS_WAIT, p) == 2, "fils : sa copie de la memoire (code 2)");
	check(marque == 1, "pere : sa memoire inchangee apres l'ecriture du fils");
	check(gros[4000] == 0x55, "pere : page du .bss inchangee");

	/* --- 3. tubes ouverts avant le fork ; fds : du fils vers le père ;
	 * fds2 : du père vers le fils */
	long r1 = f_pipe(fds);
	long r2 = f_pipe(fds2);
	check((r1 | r2) == 0, "deux f_pipe avant le fork");
	p = kfork();
	if (p == 0) { /* fils : ses bouts inutiles fermés */
		sys_close(fds[0]);
		sys_close(fds2[1]);
		long got = 0, r; /* le texte du père, jusqu'à la fin */
		for (;;) {
			r = sc(SYS_READ, fds2[0], (long)(buf + got), 100);
			if (r <= 0)
				break;
			got += r;
			if (got >= 900)
				break;
		}
		sys_close(fds2[0]);
		sys_sleep(300); /* (le père attend déjà) */
		sc(SYS_WRITE, fds[1], (long)s_fils, S_FILS_N);
		sys_close(fds[1]);
		int code = 3; /* 98 : pas le texte, 97 : pas de fin */
		if (got != (long)S_PERE_N)
			code = 98;
		if (r != 0)
			code = 97;
		_exit(code);
	}
	check(p > 0, "fork avec des tubes ouverts");
	sys_close(fds[1]);  /* père : son écrivain de fds fermé tout de suite (la fin attend celui du fils) */
	sys_close(fds2[0]); /* et son lecteur de fds2 */
	check(sc(SYS_WRITE, fds2[1], (long)s_pere, S_PERE_N) == (long)S_PERE_N,
	      "pere : ecriture dans le tube vers le fils");
	sys_close(fds2[1]);
	long got = 0, r;
	for (;;) { /* lecture jusqu'à la fin */
		r = sc(SYS_READ, fds[0], (long)(buf + got), 100);
		if (r <= 0)
			break;
		got += r;
		if (got >= 900)
			break;
	}
	check(r == 0, "pere : fin du tube (0) quand le fils ferme");
	check(got == (long)S_FILS_N, "pere : octets du fils (pas de fin a sa propre fermeture)");
	check(starts(buf, s_fils), "pere : texte du fils");
	sys_close(fds[0]);
	check(korin_sys1(SYS_WAIT, p) == 3, "fils : texte du pere lu jusqu'a la fin, ecriture (code 3)");

	/* --- 4. deuxième génération */
	p = kfork();
	if (p == 0) {
		long q = kfork();
		if (q == 0)
			_exit(9);
		if (q < 0)
			_exit(97);
		_exit((int)(korin_sys1(SYS_WAIT, q) + 1));
	}
	check(korin_sys1(SYS_WAIT, p) == 10, "petit-fils : fork du fils, wait dans le fils (code 9 + 1)");

	/* --- 4 bis. poignées d'IPC : mêmes numéros dans le fils */
	check(korin_sys2(SYS_CHAN_CREATE, IPC_SOCK_SEQPACKET, chan) == 0, "chan_create avant le fork");
	p = kfork();
	if (p == 0) {
		ipcm_set(S_CANAL, S_CANAL_N);
		long s = korin_sys4(SYS_CHAN_SEND, chan[1], ipcm, 0, 5000);
		_exit(s != (long)S_CANAL_N ? 96 : 6);
	}
	ipcm_set(buf, 64); /* réception sur le bout 0 */
	check(korin_sys4(SYS_CHAN_RECV, chan[0], ipcm, 0, 5000) == (long)S_CANAL_N,
	      "pere : message du fils par le canal herite");
	check(starts(buf, S_CANAL), "pere : texte du message");
	check(korin_sys1(SYS_WAIT, p) == 6, "fils : envoi par la poignee de meme numero (code 6)");
	korin_sys1(SYS_HANDLE_CLOSE, chan[0]);
	korin_sys1(SYS_HANDLE_CLOSE, chan[1]);

	/* --- 4 ter. fichier ouvert avant le fork : position partagée (f_fork
	 * d'AppKit ; RAM0:, puis SD0:) */
	check(shared_file(S_FIC_RAM) == 1, "RAM0: : le fils ecrit a la position du pere, le pere apres lui (ABC)");
	check(shared_file(S_FIC_SD) == 1, "SD0: : le fils ecrit a la position du pere, le pere apres lui (ABC)");

	/* --- 5. fuites : N fork + exit, en deux moitiés. Les pages que les
	 * services gardent au premier passage (fiches, files, journal) se voient
	 * dans la première moitié ; la seconde doit être à plat : une fuite d'une
	 * page par fork y perdrait N / 2 pages. */
	long bad, bad2;
	fork_loop(10, &bad); /* (premiers tours : caches des services) */
	sys_sleep(1000);
	long p0 = free_pages();
	long t = sys_time_ms();
	long refused = fork_loop(loops >> 1, &bad);
	sys_sleep(1000);
	long p1 = free_pages();
	refused += fork_loop(loops >> 1, &bad2);
	bad += bad2;
	t = sys_time_ms() - t;
	check(refused == 0, "boucle : aucun fork refuse");
	check(bad == 0, "boucle : chaque fils rend son code (5)");
	sys_sleep(1000); /* le faucheur rend les fils */
	long lost2 = p1 - free_pages(); /* pages perdues, 2e moitié */
	long lost1 = p0 - p1;           /* 1re moitié */
	ak_puts("forktest : ");
	print_dec(loops);
	ak_puts(" fork + exit en ");
	print_dec(t);
	ak_puts(" ms ; pages libres perdues : ");
	print_sdec(lost1);
	ak_puts(" (1re moitie), ");
	print_sdec(lost2);
	ak_puts(" (2e moitie)\n");
	check(lost2 < (long)((u64)loops >> 4) + 16, "boucle : pas de fuite de pages (2e moitie)"); /* moins d'une page pour 8 fork */

	ak_puts("forktest : "); /* résumé */
	print_dec(n_pass);
	ak_puts(" PASS, ");
	print_dec(n_fail);
	ak_puts(" FAIL\n");
	_exit((int)n_fail);
}
