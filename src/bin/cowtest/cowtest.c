/* cowtest - fork en copie sur écriture (docs/MICRO-NOYAU.md, « Mémoire » ; étape 9 de
 * docs/TERMINAL-ET-FORK.md), comme cowtest.asm : mémoire séparée dans les deux sens (le fils écrit,
 * le père écrit, les deux), pages en lecture seule et PTE_COW des deux côtés, dernière référence :
 * rendue inscriptible sans copie (VS_COWS inchangé), fork d'un process de 16 Mo (durée), fork d'un
 * fork, sbrk après fork, mprotect d'une page partagée, exec après fork, père à deux threads qui
 * écrit pendant les fork, fuites de pages (fork + exit et fork + exec + wait, en deux moitiés).
 *   cowtest             les essais (PASS / FAIL, résumé à la fin, code = FAIL)
 *   cowtest <n>         idem, n fork + exit dans la boucle de fuites (200)
 *   cowtest -z          (après exec) sortie, code 7 (comme l'asm : tout « -x » sauf « -m »)
 *   cowtest -m          écrit les pages libres du système sur la sortie standard (essais de fuite
 *                       depuis bash)
 * Comme l'asm : seule la ligne brute du noyau compte (krt_line) ; <n> est lu chiffre par chiffre
 * (« 12x » : 12 ; « x » ou « 0 » : 200) ; messages par puts d'AppKit, sans tampon. Les fork passent
 * par file_sys d'AppKit (ksys(SYS_FORK)), comme la macro sys. */
#include "korinrt.h"

#define LOOP_DEF 200
#define NP 4096 /* pages de la région (16 Mo) */
#define TIMED_N 20 /* fork chronométrés */
#define VM_POPULATE 0x8000
#define PROT_READ_T 1
#define PI_COW 4
#define PI_WRITE 8
#define PI_FORKED 128 /* (VM_PAGEINFO) partagée par fork */
#define VS_COWS 32
#define VS_FREE_LOW 48
#define VS_FREE_HIGH 56

static long rounds;
static volatile u64 *region;
static volatile u64 stop, lost;
static const char *argvx[4];
static long n_pass, n_fail;
static u64 stats[8];
static u8 stack[16384] __attribute__((aligned(16)));

static long fork(void)
{
	return ksys(SYS_FORK, 0, 0, 0, 0, 0, 0);
}

static long wait_pid(long pid)
{
	return korin_sys1(SYS_WAIT, pid);
}

static void sleep_ms(long ms)
{
	korin_sys1(SYS_SLEEP, ms);
}

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

/* page -> son adresse */
static volatile u64 *page(u64 i)
{
	return (volatile u64 *)((u8 *)region + (i << 12));
}

/* page i : i (toutes) */
static void fill(void)
{
	for (u64 i = 0; i < NP; i++)
		*page(i) = i;
}

static u64 read_page(u64 i) { return *page(i); }
static void write_page(u64 i, u64 v) { *page(i) = v; }

/* première page, pages, base : page i <- base + i */
static void write_pages(u64 first, u64 n, u64 base)
{
	for (; n; n--, first++)
		*page(first) = base + first;
}

/* première page, pages -> pages dont le mot 0 n'est pas i */
static u64 check_pages(u64 first, u64 n)
{
	u64 bad = 0;
	for (; n; n--, first++)
		if (*page(first) != first)
			bad++;
	return bad;
}

/* première page, pages -> somme des mots +16 */
static u64 sum_pages(u64 first, u64 n)
{
	u64 s = 0;
	for (; n; n--, first++)
		s += page(first)[2];
	return s;
}

/* page -> bits PI_* */
static u64 pageinfo_page(u64 i)
{
	return korin_sys1(SYS_VM_PAGEINFO, page(i));
}

/* -> pages copiées sur écriture (VM_STATS, soi) */
static u64 cows(void)
{
	korin_sys2(SYS_VM_STATS, 0, stats);
	return stats[VS_COWS / 8];
}

/* -> pages libres du système (VM_STATS) */
static u64 free_pages(void)
{
	korin_sys2(SYS_VM_STATS, 0, stats);
	return stats[VS_FREE_LOW / 8] + stats[VS_FREE_HIGH / 8];
}

static void print_sdec(long v)
{
	if (v < 0) {
		ak_puts("-");
		v = -v;
	}
	print_dec(v);
}

NORETURN static void exec_self_z(void)
{
	argvx[0] = "/bin/cowtest";
	argvx[1] = "-z";
	argvx[2] = 0;
	f_exec(argvx[0], (char *const *)argvx, 0, 0);
	_exit(99);
}

/* tours : fork, le fils écrit 16 pages et sort (code 5), wait, le père écrit 16 pages
 * -> fork refusés + codes faux */
static long rounds_fork(long n)
{
	long bad = 0;
	u64 pg = 0; /* pages : tournent */
	if (!n)
		return 0;
	do {
		long pid = fork();
		if (!pid) {
			write_pages((pg + 2048) & (NP - 1), 16, 0x55);
			_exit(5);
		}
		if (pid < 0) {
			bad++;
			sleep_ms(10);
		} else {
			if (wait_pid(pid) != 5)
				bad++;
			write_pages(pg, 16, 0);
		}
		pg = (pg + 16) & (NP - 1);
		sleep_ms(1);
	} while (--n);
	return bad;
}

/* tours : fork, exec « -z » (code 7), wait -> refus + codes faux */
static long rounds_exec(long n)
{
	long bad = 0;
	do {
		long pid = fork();
		if (!pid)
			exec_self_z();
		if (pid < 0) {
			bad++;
			sleep_ms(10);
		} else if (wait_pid(pid) != 7)
			bad++;
		sleep_ms(1);
	} while (--n);
	return bad;
}

/* thread écrivain : pages 100..1099, mot +16 : un compteur, relu aussitôt (une écriture perdue :
 * lost += 1) ; jusqu'à stop -> code = tours */
static void writer(void)
{
	u64 t = 0;
	do {
		t++;
		for (u64 i = 100; i < 1100; i++) {
			volatile u64 *p = page(i);
			p[2] = t;
			if (p[2] != t)
				lost++;
		}
	} while (!stop);
	korin_sys1(SYS_THREAD_EXIT, t);
	__builtin_unreachable();
}

/* -m : pages libres, en décimal, sur la sortie standard */
NORETURN static void show_free(void)
{
	char b[64];
	char *p = b + 63;
	u64 v = free_pages();
	*p = '\n';
	do {
		*--p = '0' + v % 10;
		v /= 10;
	} while (v);
	sys_write(1, p, b + 64 - p);
	_exit(0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *a = krt_line;
	long n = 0;
	if (a) {
		if (a[0] == '-') {
			if (a[1] == 'm')
				show_free();
			_exit(7); /* -z : après exec, code 7 */
		}
		for (;;) {
			u32 d = (u8)*a++ - '0';
			if (d > 9)
				break;
			n = n * 10 + d;
		}
	}
	rounds = n > 0 ? n : LOOP_DEF;
	ak_puts("cowtest : fork en copie sur ecriture\n");

	/* --- région de 16 Mo, peuplée, page i : i */
	long r = korin_sys4(SYS_VM_MAP, 0, NP * 4096L, PROT_READ | PROT_WRITE, VM_POPULATE);
	region = (volatile u64 *)r;
	check(r > 0, "region de 16 Mo (VM_MAP, VM_POPULATE)");
	fill();

	/* --- 1. le fils écrit */
	long pid = fork();
	if (!pid) {
		u64 pi = pageinfo_page(2);
		if (read_page(1) != 1 || read_page(NP - 1) != NP - 1)
			_exit(90);
		write_page(0, 0xC0DE);
		write_page(7, 0xC0DE7);
		if (read_page(0) != 0xC0DE)
			_exit(91);
		if (!(pi & PI_COW) || (pi & PI_WRITE)) /* 2, ou 12 : pas en copie sur écriture */
			_exit(12);
		_exit(2);
	}
	long code = wait_pid(pid);
	check((u64)(code - 90) > 1, "fils : memoire du pere lue, sa propre ecriture relue"); /* 90, 91 : faux */
	check(code == 2, "fils : page partagee en lecture seule, PTE_COW (code 2)");
	check(read_page(0) == 0, "pere : page 0 intacte apres l'ecriture du fils");
	check(read_page(7) == 7, "pere : page 7 intacte apres l'ecriture du fils");

	/* --- 2. le père écrit, le fils lit après */
	pid = fork();
	if (!pid) {
		sleep_ms(100);
		_exit(read_page(5) != 5 ? 92 : 3);
	}
	write_page(5, 0xBAD);
	check(wait_pid(pid) == 3, "fils : page 5 d'avant le fork, le pere l'a ecrite (code 3)");
	check(read_page(5) == 0xBAD, "pere : sa propre ecriture");
	write_page(5, 5);

	/* --- 3. les deux écrivent la même page */
	pid = fork();
	if (!pid) {
		write_page(6, 0xC6);
		sleep_ms(50);
		_exit(read_page(6) != 0xC6 ? 93 : 4);
	}
	write_page(6, 0xB6);
	check(wait_pid(pid) == 4, "les deux ecrivent la page 6 : le fils garde la sienne (code 4)");
	check(read_page(6) == 0xB6, "les deux ecrivent la page 6 : le pere garde la sienne");
	write_page(6, 6);

	/* --- 4. compteurs : copie tant que le fils vit, sans copie ensuite */
	pid = fork();
	if (!pid) {
		sleep_ms(400);
		_exit(check_pages(0, 256) == 0 ? 5 : 94); /* le fils relit ce que le père écrit */
	}
	u64 pi = pageinfo_page(10);
	check((pi & PI_COW) && !(pi & PI_WRITE) && (pi & PI_FORKED),
	      "pere : page en lecture seule, PTE_COW, partagee par fork");
	u64 c0 = cows();
	write_pages(0, 256, 0x10000); /* 256 pages écrites : 256 copies */
	check(cows() - c0 == 256, "pere : 256 pages ecrites, fils vivant : 256 copies (VS_COWS)");
	check(wait_pid(pid) == 5, "fils : ses 256 pages d'avant les ecritures du pere (code 5)");
	sleep_ms(100); /* (le faucheur rend le fils) */
	check(!(pageinfo_page(300) & PI_FORKED), "fils parti : plus partagee (compteur rendu)");
	c0 = cows();
	fill(); /* 4096 pages écrites : aucune copie */
	u64 dc = cows() - c0;
	ak_puts("cowtest : copies apres le depart du fils (4096 ecritures) : ");
	print_dec(dc);
	ak_puts("\n");
	check(dc == 0, "pere seul : 4096 ecritures sans copie (derniere reference)");
	check((pageinfo_page(300) & PI_WRITE) != 0, "pere seul : page de nouveau inscriptible");

	/* --- 5. durée d'un fork de 16 Mo */
	long t0 = korin_sys0(SYS_TIME_MS);
	long in_fork = 0; /* ms dans fork (côté père) */
	for (int k = TIMED_N; k; k--) {
		long t1 = korin_sys0(SYS_TIME_MS);
		pid = fork();
		if (!pid)
			_exit(0);
		in_fork += korin_sys0(SYS_TIME_MS) - t1;
		wait_pid(pid);
	}
	t0 = korin_sys0(SYS_TIME_MS) - t0;
	ak_puts("cowtest : ");
	print_dec(in_fork);
	ak_puts(" ms dans 20 fork d'un process de 16 Mo (");
	print_dec(t0);
	ak_puts(" ms avec exit + wait)\n");
	t0 = korin_sys0(SYS_TIME_MS); /* le fils écrit les 16 Mo (4096 copies) */
	pid = fork();
	if (!pid) {
		u64 c = cows();
		write_pages(0, NP, 0x20000);
		/* (+ la page de stats, celle de la pile) : 13 : 4096 copies, 14 sinon */
		_exit(cows() - c - NP > 4 ? 14 : 13);
	}
	code = wait_pid(pid);
	t0 = korin_sys0(SYS_TIME_MS) - t0;
	ak_puts("cowtest : fork + le fils ecrit 16 Mo + wait : ");
	print_dec(t0);
	ak_puts(" ms\n");

	/* --- 6. fork d'un fork */
	check(code == 13, "fils : 16 Mo ecrits, 4096 copies (a 4 pres : pile, stats)");
	fill();
	pid = fork();
	if (!pid) {
		long g = fork(); /* fils : un petit-fils */
		if (!g) {
			sleep_ms(50); /* petit-fils : la page 21 d'avant, écrit la 20 */
			if (read_page(21) != 21)
				_exit(95);
			write_page(20, 0x6020);
			_exit(9);
		}
		if (g < 0)
			_exit(96);
		write_page(21, 0x6021);
		long c = wait_pid(g);
		if (c != 9)
			_exit(c + 100);
		if (read_page(20) != 20 || read_page(21) != 0x6021)
			_exit(96);
		_exit(10);
	}
	check(wait_pid(pid) == 10, "fork d'un fork : chacun sa page 20 et 21 (code 9 + 1)");
	{
		u64 p20 = read_page(20);
		check(read_page(21) == 21 && p20 == 20, "fork d'un fork : pages du pere intactes");
	}

	/* --- 7. sbrk après fork */
	volatile u8 *edge = (volatile u8 *)korin_sys1(SYS_SBRK, 100) + 99; /* un tas, son dernier octet marqué */
	*edge = 0x5A;
	pid = fork();
	if (!pid) {
		/* fils : le tas grandit, le dernier octet du père écrit, puis le neuf */
		if (korin_sys1(SYS_SBRK, 8192) < 0)
			_exit(97);
		*edge = 0xA5;
		edge[4096] = 0xA5;
		_exit(*edge != 0xA5 ? 97 : 11);
	}
	check(wait_pid(pid) == 11, "sbrk dans le fils : le tas grandit, la page du bord se copie (code 11)");
	check(*edge == 0x5A, "sbrk dans le fils : la page du bord du pere intacte");

	/* --- 8. mprotect d'une page partagée */
	pid = fork();
	if (!pid) {
		sleep_ms(200);
		_exit(read_page(40) != 40 ? 98 : 12);
	}
	{
		volatile u64 *p40 = page(40);
		long e = korin_sys3(SYS_VM_PROTECT, p40, 4096, PROT_READ_T);
		e |= korin_sys3(SYS_VM_PROTECT, p40, 4096, PROT_READ | PROT_WRITE);
		u64 info = korin_sys1(SYS_VM_PAGEINFO, p40);
		check(e == 0, "mprotect lecture puis lecture + ecriture d'une page partagee");
		check(!(info & PI_WRITE), "mprotect : la page partagee reste en lecture seule (copie a l'ecriture)");
	}
	write_page(40, 0x8040);
	check(wait_pid(pid) == 12, "mprotect puis ecriture du pere : le fils garde la page d'avant (code 12)");
	write_page(40, 40);

	/* --- 9. exec après fork : les pages reviennent au père seul */
	pid = fork();
	if (!pid)
		exec_self_z();
	check(wait_pid(pid) == 7, "exec apres fork (code 7)");
	sleep_ms(100);
	c0 = cows();
	write_pages(1000, 64, 0x90000);
	check(cows() - c0 == 0, "exec apres fork : 64 ecritures du pere sans copie");

	/* --- 10. père à deux threads qui écrit pendant les fork */
	fill();
	stop = 0;
	lost = 0;
	long tid = korin_sys3(SYS_THREAD_CREATE, writer, stack + sizeof stack, 0);
	check(tid > 0, "thread ecrivain");
	sleep_ms(20);
	long moved = 0; /* fils dont l'image a bougé */
	for (int k = 30; k; k--) {
		pid = fork();
		if (!pid) {
			/* somme des pages 100..1099, deux fois à 20 ms d'écart */
			u64 s = sum_pages(100, 1000);
			sleep_ms(20);
			_exit(sum_pages(100, 1000) != s ? 9 : 8);
		}
		if (wait_pid(pid) != 8)
			moved++;
	}
	stop = 1;
	long turns = korin_sys1(SYS_THREAD_JOIN, tid); /* tours de l'écrivain */
	check(moved == 0, "pere a deux threads, 30 fork : l'image du fils ne bouge pas");
	check(lost == 0, "pere a deux threads : aucune ecriture de l'ecrivain perdue");
	check((u64)turns > 30, "pere a deux threads : l'ecrivain a tourne pendant les fork");

	/* --- 11. fuites : N fork + exit (le fils écrit 16 pages, le père aussi) */
	rounds_fork(10);
	sleep_ms(1000);
	long p0 = free_pages();
	t0 = korin_sys0(SYS_TIME_MS);
	long bad = rounds_fork((u64)rounds >> 1);
	sleep_ms(1000);
	long p1 = free_pages();
	bad += rounds_fork((u64)rounds >> 1);
	t0 = korin_sys0(SYS_TIME_MS) - t0;
	check(bad == 0, "boucle fork + exit : aucun refus, chaque fils rend son code");
	sleep_ms(1000);
	long lost2 = p1 - (long)free_pages();
	long lost1 = p0 - p1;
	ak_puts("cowtest : ");
	print_dec(rounds);
	ak_puts(" fork + exit (16 Mo) en ");
	print_dec(t0);
	ak_puts(" ms ; pages libres perdues : ");
	print_sdec(lost1);
	ak_puts(" (1re moitie), ");
	print_sdec(lost2);
	ak_puts(" (2e moitie)\n");
	check(lost2 < 16, "boucle fork + exit d'un process de 16 Mo : pas de fuite (2e moitie)");

	/* --- 12. fuites : fork + exec + wait */
	rounds_exec(5);
	sleep_ms(1000);
	p0 = free_pages();
	bad = rounds_exec(50);
	sleep_ms(1000);
	p1 = free_pages();
	bad += rounds_exec(50);
	check(bad == 0, "boucle fork + exec + wait : chaque exec aboutit");
	sleep_ms(1000);
	lost2 = p1 - (long)free_pages();
	lost1 = p0 - p1;
	ak_puts("cowtest : 100 fork + exec + wait ; pages libres perdues : ");
	print_sdec(lost1);
	ak_puts(" (1re moitie), ");
	print_sdec(lost2);
	ak_puts(" (2e moitie)\n");
	check(lost2 < 16, "boucle fork + exec + wait : pas de fuite (2e moitie)");

	ak_puts("cowtest : "); /* résumé */
	print_dec(n_pass);
	ak_puts(" PASS, ");
	print_dec(n_fail);
	ak_puts(" FAIL\n");
	_exit(n_fail);
}
