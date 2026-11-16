/* exectest - SYS_EXEC (docs/ABI.md, 0x6B ; étude docs/TERMINAL-ET-FORK.md,
 * 4.4) par f_exec d'AppKit, comme exectest.asm : même pid et même père après
 * exec, bout de tube et poignée d'IPC gardés, argv et environnement, exec
 * d'un fichier absent ou qui n'est pas un ELF (erreur, le programme
 * continue), exec d'un process à 4 threads (les autres disparaissent),
 * signaux (géré -> défaut, ignoré gardé), N fork + exec + wait sans fuite de
 * pages ni manque d'ASID.
 *   exectest            les essais (PASS / FAIL, résumé à la fin, code = FAIL)
 *   exectest <n>        idem, n fork + exec + wait dans la boucle (500)
 *   exectest -<mode> .. le programme après exec (modes plus bas)
 * Comme l'asm : tout vient de la ligne brute du noyau (krt_line) ; le fork
 * est celui d'AppKit (sys SYS_FORK de l'asm : f_fork, ici ksys) ; un mode
 * inconnu vaut « -z » (code 7) ; « -a » regarde l'objet de f_start_blob. */
#include "korinrt.h"

#define LOOP_DEF 500
#define SIGUSR1 30
#define SIGUSR2 31
#define IPC_SOCK_SEQPACKET 5
#define IPCM_SIZE 32
#define VS_FREE_LOW 48
#define VS_FREE_HIGH 56
#define P_MOI "/bin/exectest"

static const char s_deux[] = "deux mots";
static const char s_guill[] = "x\"y";
static const char s_env1[] = "EXECTEST=oui";
static const char s_env2[] = "B=2";
static const char s_tube[] = "tube";
static const char s_canal[] = "canal";
#define S_TUBE_N (sizeof(s_tube) - 1)
#define S_CANAL_N (sizeof(s_canal) - 1)

static long loops;
static long fds[2];
static u32 chan[2];
static u32 mot[2];
static u64 iov[2];
static u8 ipcm[IPCM_SIZE] __attribute__((aligned(8)));
static const char *xargv[6];
static const char *envp[4];
static long n_pass, n_fail;
static u64 stats[8];
static char s_n1[24], s_n2[24];
static char buf[1024];
static char buf2[64];
static u8 pile1[16384] __attribute__((aligned(16)));
static u8 pile2[16384] __attribute__((aligned(16)));
static u8 pile3[16384] __attribute__((aligned(16)));

static long kfork(void)
{
	return ksys(SYS_FORK, 0, 0, 0, 0, 0, 0); /* (f_fork d'AppKit, comme la macro sys) */
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

/* mode (« -p »...) : argv = { chemin, mode, 0, 0, 0 } */
static void argv_mode(const char *m)
{
	xargv[0] = P_MOI;
	xargv[1] = m;
	xargv[2] = xargv[3] = xargv[4] = 0;
}

static void argv_two(const char *a, const char *b)
{
	xargv[2] = a;
	xargv[3] = b;
	xargv[4] = 0;
}

/* f_exec(chemin, argv, 0, 0) -> erreur */
static long exec_self(void)
{
	return f_exec(P_MOI, (char *const *)xargv, 0, 0);
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

/* tours : fork, le fils fait exec « -z » (code 7), wait -> fork refusés,
 * *bad = codes faux */
static long exec_loop(long n, long *bad)
{
	long refused = 0, wrong = 0;
	for (; n; n--) {
		long p = kfork();
		if (p == 0) {
			exec_self();
			_exit(90);
		}
		if (p < 0) {
			refused++;
			sys_sleep(10);
		} else if (korin_sys1(SYS_WAIT, p) != 7)
			wrong++;
		sys_sleep(1); /* (les avis de fin aux services) */
	}
	*bad = wrong;
	return refused;
}

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

/* chaîne (espaces en tête sautés) -> nombre décimal (0 si aucun), *end = après lui */
static long read_dec(const char *s, const char **end)
{
	while (*s == ' ')
		s++;
	long v = 0;
	while ((u8)(*s - '0') <= 9)
		v = v * 10 + (*s++ - '0');
	if (end)
		*end = s;
	return v;
}

static const char *next_str(const char *s)
{
	while (*s++)
		;
	return s;
}

/* ---- le programme après exec : « -<mode> arguments » */

/* -p <pid> <père> : code 40 si getpid et getppid sont ceux d'avant exec */
static int mode_p(const char *s)
{
	long pid = read_dec(s, &s);
	long ppid = read_dec(s, 0);
	if (korin_sys0(SYS_GETPID) != pid)
		return 1;
	if (korin_sys0(SYS_GETPPID) != ppid)
		return 2;
	return 40;
}

/* -t <descripteur> <poignée> : écrit « tube » sur le descripteur d'AppKit
 * hérité, le ferme ; « canal » par la poignée ; code 41 */
static int mode_t(const char *s)
{
	long d = read_dec(s, &s);
	long h = read_dec(s, 0);
	if (sys_write(d, s_tube, S_TUBE_N) != (long)S_TUBE_N)
		return 1;
	sys_close(d);
	ipcm_set(s_canal, S_CANAL_N);
	if (korin_sys4(SYS_CHAN_SEND, h, ipcm, 0, 5000) != (long)S_CANAL_N)
		return 2;
	return 41;
}

/* -a : argv = { chemin, -a, « deux mots », x"y }, environnement = s_env1,
 * s_env2 (objet de f_start_blob) : code 42 */
static int mode_a(void)
{
	/* l'objet du père : crt0 l'a déjà pris (f_start_blob ne le rend qu'une
	 * fois : marques effacées) ; argv[0] y est à +16 (krt_env : 0 sans objet) */
	u8 *b = krt_env ? (u8 *)krt_argv[0] - 16 : 0;
	if (!b)
		return 1;
	if (*(u32 *)(b + 4) != 4)
		return 2;
	if (*(u32 *)(b + 8) != 2)
		return 3;
	const char *s = next_str((const char *)b + 16); /* argv[0] (le chemin), argv[1] */
	if (!str_eq(s, "-a"))
		return 4;
	s = next_str(s);
	if (!str_eq(s, s_deux))
		return 5;
	s = next_str(s);
	if (!str_eq(s, s_guill))
		return 6;
	s = next_str(s);
	if (!str_eq(s, s_env1))
		return 7;
	s = next_str(s);
	if (!str_eq(s, s_env2))
		return 8;
	return 42;
}

/* -x : un seul thread (THREAD_LIST) : code 43, sinon 50 + n */
static int mode_x(void)
{
	long n = korin_sys3(SYS_THREAD_LIST, 0, buf, 8);
	return n == 1 ? 43 : (int)(n + 50);
}

/* -s : SIGUSR2 (ignoré avant exec) à soi, puis SIGUSR1 (géré avant : par
 * défaut maintenant, fin du process, code -30) ; 44 si on survit */
static int mode_s(void)
{
	korin_sys3(SYS_SIG_SEND, 0, 0, SIGUSR2);
	sys_sleep(20);
	korin_sys3(SYS_SIG_SEND, 0, 0, SIGUSR1);
	sys_sleep(20);
	return 44;
}

static NORETURN void mode(const char *line)
{
	const char *a = line + 2;
	switch (line[1]) {
	case 'p':
		_exit(mode_p(a));
	case 't':
		_exit(mode_t(a));
	case 'a':
		_exit(mode_a());
	case 'x':
		_exit(mode_x());
	case 's':
		_exit(mode_s());
	}
	_exit(7); /* -z : sortie, code 7 */
}

/* (jamais appelé après exec ; c'est aussi son propre retour) */
static void handler(void)
{
	korin_sys0(SYS_SIG_RETURN);
}

/* threads de l'essai 5 */
#if defined(__x86_64__)
#define TH_ENTRY __attribute__((force_align_arg_pointer))
#else
#define TH_ENTRY
#endif
TH_ENTRY static NORETURN void th_sleep(void)
{
	for (;;)
		sys_sleep(100000);
}
TH_ENTRY static NORETURN void th_read(void)
{
	for (;;) /* tube vide, écrivain ouvert : la requête attend chez vfs */
		ksys(SYS_READ, fds[0], (long)buf2, 16, 0, 0, 0);
}
TH_ENTRY static NORETURN void th_futex(void)
{
	for (;;)
		korin_sys3(SYS_FUTEX_WAIT, mot, 0, 0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *line = krt_line;
	long n = LOOP_DEF;
	if (line) {
		if (line[0] == '-')
			mode(line);
		n = read_dec(line, 0);
		if (n <= 0)
			n = LOOP_DEF;
	}
	loops = n;
	ak_puts("exectest : SYS_EXEC (meme pid, nouvel espace)\n");

	/* --- 1. même pid, même père */
	long p = kfork();
	if (p == 0) { /* fils : exec « -p <pid> <père> » */
		argv_mode("-p");
		*fmt_dec((u64)korin_sys0(SYS_GETPID), s_n1) = 0;
		*fmt_dec((u64)korin_sys0(SYS_GETPPID), s_n2) = 0;
		argv_two(s_n1, s_n2);
		exec_self();
		_exit(90);
	}
	check(p > 0, "fork avant exec");
	check(korin_sys1(SYS_WAIT, p) == 40, "exec garde le pid et le pere (getpid, getppid ; code 40)");

	/* --- 2. bout de tube et poignée d'IPC gardés */
	long r1 = f_pipe(fds);
	long r2 = korin_sys2(SYS_CHAN_CREATE, IPC_SOCK_SEQPACKET, chan);
	check((r1 | r2) == 0, "tube et canal avant fork + exec");
	p = kfork();
	if (p == 0) { /* fils : son lecteur fermé ; exec « -t <écrivain> <bout du canal> » */
		sys_close(fds[0]);
		argv_mode("-t");
		*fmt_dec((u64)fds[1], s_n1) = 0;
		*fmt_dec((u64)chan[1], s_n2) = 0;
		argv_two(s_n1, s_n2);
		exec_self();
		_exit(90);
	}
	sys_close(fds[1]); /* père : son écrivain fermé */
	long got = 0, r;
	for (;;) { /* lecture jusqu'à la fin */
		r = ksys(SYS_READ, fds[0], (long)(buf + got), 100, 0, 0, 0);
		if (r <= 0)
			break;
		got += r;
		if (got >= 900)
			break;
	}
	buf[got] = 0;
	check(str_eq(buf, s_tube) == 1, "apres exec : ecriture par le descripteur du tube herite");
	check(r == 0, "fin du tube (0) quand le nouveau programme le ferme");
	sys_close(fds[0]);
	ipcm_set(buf, 64);
	check(korin_sys4(SYS_CHAN_RECV, chan[0], ipcm, 0, 5000) == (long)S_CANAL_N,
	      "apres exec : message par la poignee d'IPC de meme numero");
	check(korin_sys1(SYS_WAIT, p) == 41, "nouveau programme : tube et canal (code 41)");
	korin_sys1(SYS_HANDLE_CLOSE, chan[0]);
	korin_sys1(SYS_HANDLE_CLOSE, chan[1]);

	/* --- 3. argv et environnement */
	p = kfork();
	if (p == 0) {
		argv_mode("-a");
		argv_two(s_deux, s_guill);
		envp[0] = s_env1; /* environnement : deux variables */
		envp[1] = s_env2;
		envp[2] = 0;
		f_exec(P_MOI, (char *const *)xargv, (char *const *)envp, 0);
		_exit(90);
	}
	check(korin_sys1(SYS_WAIT, p) == 42,
	      "nouveau programme : argv (espaces, guillemets) et environnement (code 42)");

	/* --- 4. fichier absent, fichier qui n'est pas un ELF */
	long me = korin_sys0(SYS_GETPID);
	argv_mode("-z");
	xargv[2] = 0;
	check(f_exec("/bin/exectest_absent", (char *const *)xargv, 0, 0) < 0,
	      "exec d'un fichier absent : erreur, le programme continue");
	check(f_exec("/etc/startup.txt", (char *const *)xargv, 0, 0) < 0,
	      "exec d'un fichier qui n'est pas un ELF : erreur, le programme continue");
	check(korin_sys0(SYS_GETPID) == me, "apres ces echecs : meme pid");
	check(f_pipe(fds) == 0, "apres ces echecs : f_pipe"); /* ses descripteurs marchent encore */
	sys_close(fds[0]);
	sys_close(fds[1]);

	/* --- 5. exec d'un process à 4 threads */
	p = kfork();
	if (p == 0) {
		f_pipe(fds); /* un tube : un thread y lit (requête à vfs en attente) */
		korin_sys3(SYS_THREAD_CREATE, th_sleep, pile1 + 16384, 0);
		korin_sys3(SYS_THREAD_CREATE, th_read, pile2 + 16384, 0);
		korin_sys3(SYS_THREAD_CREATE, th_futex, pile3 + 16384, 0);
		sys_sleep(100);
		long nt = korin_sys3(SYS_THREAD_LIST, 0, buf, 8);
		argv_mode("-x");
		xargv[2] = 0;
		long code = 4; /* (4 threads avant : sinon code 60) */
		if (nt == 4) {
			exec_self();
			code = 90;
		}
		_exit((int)(code + 56));
	}
	check(korin_sys1(SYS_WAIT, p) == 43,
	      "exec avec 4 threads (sommeil, lecture de tube, futex) : un seul apres (code 43)");

	/* --- 6. signaux */
	p = kfork();
	if (p == 0) {
		korin_sys6(SYS_SIG_ACTION, SIGUSR1, (long)handler, 0, 0, (long)handler, 0); /* géré */
		korin_sys6(SYS_SIG_ACTION, SIGUSR2, 1, 0, 0, 0, 0);                         /* ignoré */
		argv_mode("-s");
		xargv[2] = 0;
		exec_self();
		_exit(90);
	}
	check(korin_sys1(SYS_WAIT, p) == -SIGUSR1,
	      "signaux apres exec : ignore garde, gere remis par defaut (fin par SIGUSR1)");

	/* --- 7. fuites : N fork + exec + wait, en deux moitiés */
	argv_mode("-z");
	xargv[2] = 0;
	long bad, bad2;
	exec_loop(10, &bad); /* (premiers tours : caches des services) */
	sys_sleep(1000);
	long p0 = free_pages();
	long t = sys_time_ms();
	long refused = exec_loop(loops >> 1, &bad);
	sys_sleep(1000);
	long p1 = free_pages();
	refused += exec_loop(loops >> 1, &bad2);
	bad += bad2;
	t = sys_time_ms() - t;
	check(refused == 0, "boucle : aucun fork refuse (ni ASID ni memoire)");
	check(bad == 0, "boucle : chaque exec aboutit (code 7)");
	sys_sleep(1000); /* le faucheur rend les fils */
	long lost2 = p1 - free_pages();
	long lost1 = p0 - p1;
	ak_puts("exectest : ");
	print_dec(loops);
	ak_puts(" fork + exec + wait en ");
	print_dec(t);
	ak_puts(" ms ; pages libres perdues : ");
	print_sdec(lost1);
	ak_puts(" (1re moitie), ");
	print_sdec(lost2);
	ak_puts(" (2e moitie)\n");
	check(lost2 < (long)((u64)loops >> 4) + 16, "boucle : pas de fuite de pages (2e moitie)"); /* moins d'une page pour 8 exec */

	ak_puts("exectest : ");
	print_dec(n_pass);
	ak_puts(" PASS, ");
	print_dec(n_fail);
	ak_puts(" FAIL\n");
	_exit((int)n_fail);
}
