/* pipetest - tubes et lancement des programmes (docs/FICHIERS.md, étape 4),
 * comme pipetest.asm : tubes de vfs, sortie d'un fils lue pendant qu'il
 * tourne, code de sortie par wait, entrée standard, petit-fils qui hérite de
 * la sortie, erreurs.
 *   pipetest            les essais (PASS / FAIL, résumé à la fin, code = FAIL)
 *   pipetest child      (fils) 5 lignes, 200 ms entre elles, code 7
 *   pipetest cat        (fils) recopie l'entrée standard sur la sortie, code 3
 *   pipetest parent     (fils) lance « pipetest child » (sortie héritée), code 5
 * Comme l'asm : le mode vient de la ligne brute du noyau (krt_line), un autre
 * mot lance les essais ; sortie par puts et print_dec d'AppKit (sans tampon). */
#include "korinrt.h"

static long fd, pid;
static long fds[2], fds2[2], sio[3];
static long n_pass, n_fail;
static char buf[1024];

static const char s_cat[] = "Bonjour, le tube !\nDeuxieme ligne.\n";
#define S_CAT_N (sizeof(s_cat) - 1)
#define P_SELF "/bin/pipetest"

static long sc(long nr, long a, long b, long c)
{
	return ksys(nr, a, b, c, 0, 0, 0);
}

/* une vérification : « PASS nom » ou « FAIL nom » (reussite, echec) */
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

/* chaîne, préfixe -> 1 si la chaîne commence par le préfixe (commence) */
static int starts(const char *s, const char *p)
{
	while (*p)
		if (*p++ != *s++)
			return 0;
	return 1;
}

/* lit fd (O_NONBLOCK) jusqu'à la fin, dans buf -> octets ; *first = ms de la
 * première donnée, *end = ms de la fin (lire_tout) */
static long read_all(long *first, long *end)
{
	long n = 0, t1 = 0;
	for (;;) {
		long r = sc(SYS_READ, fd, (long)(buf + n), 200);
		if (r == E_AGAIN) {
			sys_sleep(10);
			continue;
		}
		if (r <= 0)
			break;
		n += r;
		if (!t1)
			t1 = sys_time_ms();
	}
	sys_close(fd);
	buf[n] = 0;
	*end = sys_time_ms();
	*first = t1;
	return n;
}

/* (fils) 5 lignes de 8 octets, 200 ms entre elles, code 7 */
static NORETURN void mode_child(void)
{
	for (long i = 1; i <= 5; i++) {
		char *p = str_cat(buf, "ligne ");
		*p++ = (char)('0' + i);
		*p++ = '\n';
		*p = 0;
		ak_puts(buf);
		sys_sleep(200);
	}
	_exit(7);
}

/* (fils) entrée -> sortie jusqu'à la fin, code 3 */
static NORETURN void mode_cat(void)
{
	for (;;) {
		long r = sys_read(0, buf, 100);
		if (r <= 0)
			break;
		sys_write(1, buf, (u64)r);
	}
	_exit(3);
}

/* (fils) lance « pipetest child » (sortie héritée), l'attend, code 5 */
static NORETURN void mode_parent(void)
{
	sys_sleep(300); /* (rien pendant 300 ms : E_AGAIN) */
	long p = sc(SYS_SPAWN, (long)P_SELF, (long)"child", 0);
	if (p >= 0)
		korin_sys1(SYS_WAIT, p);
	_exit(5);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *a = krt_line;
	if (a) {
		if (str_eq(a, "child"))
			mode_child();
		if (str_eq(a, "cat"))
			mode_cat();
		if (str_eq(a, "parent"))
			mode_parent();
	}
	long t1, t2, n, r;
	ak_puts("pipetest : tubes de vfs (etape 4)\n");

	/* --- 1. spawn_pipe de hello (comme avant) */
	pid = sc(SYS_SPAWN_PIPE, (long)"/bin/hello", (long)"arguments de test", (long)&fd);
	check(pid > 0, "spawn_pipe hello");
	check(fd >= 64, "descripteur du tube dans AppKit (>= 64)");
	n = read_all(&t1, &t2);
	check(n > 0, "sortie de hello lue");
	check(korin_sys1(SYS_WAIT, pid) == 0, "hello : code 0 par wait");

	/* --- 2. sortie lue pendant que le fils tourne, code de sortie */
	pid = sc(SYS_SPAWN_PIPE, (long)P_SELF, (long)"child", (long)&fd);
	check(pid > 0, "spawn_pipe pipetest child");
	n = read_all(&t1, &t2);
	long dt = t2 - t1;
	ak_puts("pipetest : ");
	print_dec(n);
	ak_puts(" octets, ");
	print_dec(dt);
	ak_puts(" ms entre la 1re ligne et la fin\n");
	check(n == 5 * 8, "5 lignes recues");
	check(dt >= 500, "sortie en continu (1re ligne >= 500 ms avant la fin)");
	check(starts(buf, "ligne "), "contenu : ligne 1");
	check(korin_sys1(SYS_WAIT, pid) == 7, "code de sortie 7 par wait");

	/* --- 3. f_pipe, lecture bloquante, f_spawn_stdio */
	check(f_pipe(fds) == 0, "f_pipe");
	check(sc(SYS_READ, fds[1], (long)buf, 4) == E_BADF, "read du bout d'ecriture : E_BADF");
	check(sc(SYS_WRITE, fds[0], (long)buf, 4) == E_BADF, "write du bout de lecture : E_BADF");
	sio[0] = -1; /* sortie et erreur du fils : le tube */
	sio[1] = sio[2] = fds[1];
	pid = f_spawn_stdio(P_SELF, "child", sio);
	check(pid > 0, "f_spawn_stdio (sortie dans le tube)");
	sys_close(fds[1]); /* le père ferme son bout d'écriture */
	n = 0; /* lecture bloquante jusqu'à la fin */
	for (;;) {
		r = sc(SYS_READ, fds[0], (long)(buf + n), 100);
		if (r <= 0)
			break;
		n += r;
	}
	check(r == 0, "lecture bloquante : fin (0), pas E_AGAIN");
	check(n == 5 * 8, "lecture bloquante : 5 lignes");
	sys_close(fds[0]);
	check(korin_sys1(SYS_WAIT, pid) == 7, "code 7");

	/* --- 4. entrée standard d'un fils (cat) */
	check(f_pipe(fds) == 0, "f_pipe (entree)");
	check(f_pipe(fds2) == 0, "f_pipe (sortie)");
	sio[0] = fds[0]; /* entrée : lecture du 1er tube ; sortie, erreur : 2e tube */
	sio[1] = sio[2] = fds2[1];
	pid = f_spawn_stdio(P_SELF, "cat", sio);
	check(pid > 0, "f_spawn_stdio (cat)");
	sys_close(fds[0]); /* nos bouts du fils : fermés */
	sys_close(fds2[1]);
	check(sc(SYS_WRITE, fds[1], (long)s_cat, S_CAT_N) == (long)S_CAT_N, "write vers l'entree du fils");
	sys_close(fds[1]); /* fin de son entrée */
	n = 0;
	for (;;) {
		r = sc(SYS_READ, fds2[0], (long)(buf + n), 100);
		if (r <= 0)
			break;
		n += r;
	}
	check(n == (long)S_CAT_N, "cat : meme longueur");
	buf[n] = 0;
	check(str_eq(buf, s_cat) == 1, "cat : meme contenu");
	sys_close(fds2[0]);
	check(korin_sys1(SYS_WAIT, pid) == 3, "cat : code 3");

	/* --- 5. petit-fils : sortie héritée */
	pid = sc(SYS_SPAWN_PIPE, (long)P_SELF, (long)"parent", (long)&fd);
	check(pid > 0, "spawn_pipe pipetest parent");
	check(read_all(&t1, &t2) == 5 * 8, "petit-fils : ses 5 lignes dans le tube du grand-pere");
	check(korin_sys1(SYS_WAIT, pid) == 5, "parent : code 5");

	/* --- 6. écrire sans lecteur : E_PIPE ; tube vide sans attente */
	f_pipe(fds);
	sys_close(fds[0]);
	check(sc(SYS_WRITE, fds[1], (long)s_cat, 4) == E_PIPE, "write sans lecteur : E_PIPE");
	check(sys_close(fds[1]) == 0, "close");
	check(sys_close(fds[1]) == E_BADF, "close d'un descripteur ferme : E_BADF");
	pid = sc(SYS_SPAWN_PIPE, (long)P_SELF, (long)"parent", (long)&fd); /* un fils qui ne dit rien tout de suite */
	check(sc(SYS_READ, fd, (long)buf, 10) == E_AGAIN, "tube vide (O_NONBLOCK) : E_AGAIN");
	read_all(&t1, &t2);
	korin_sys1(SYS_WAIT, pid);
	check(sc(SYS_SPAWN, (long)"pas_de_programme", 0, 0) == E_NOENT, "spawn d'un programme absent : E_NOENT");

	ak_puts("pipetest : "); /* résumé */
	print_dec(n_pass);
	ak_puts(" PASS, ");
	print_dec(n_fail);
	ak_puts(" FAIL\n");
	_exit((int)n_fail);
}
