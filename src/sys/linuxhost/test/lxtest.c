/*
 * lxtest.c -- module d'essai de liblinux : une bibliothèque partagée de LINUX (construite par la gcc
 * de WSL, avec les en-têtes et l'ABI de glibc : tools/linux/build.sh), que linuxhost --self-test
 * charge dans Korin. Elle touche ce que le module Widevine demandera : stdio, mémoire, errno de
 * Linux, appels système par syscall (), horloges, ctype de glibc, mutex et pthread_once,
 * constructeurs et destructeurs, relocations de pointeurs, canari de pile (%fs:0x28),
 * _FORTIFY_SOURCE (__printf_chk…).
 */
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static int s_constructed;
static int s_fails;

__attribute__ ((constructor)) static void lx_ctor (void)
{
	s_constructed = 42;
}

__attribute__ ((destructor)) static void lx_dtor (void)
{
	printf ("lxtest : destructeur appelé\n");
	fflush (stdout);
}

static void check (int ok, const char *what)
{
	printf ("  [%s] %s\n", ok ? "ok" : "ÉCHEC", what);
	if (!ok)
		s_fails++;
}

/* des pointeurs dans les données : relocations R_X86_64_RELATIVE */
static int add (int a, int b) { return a + b; }
static int mul (int a, int b) { return a * b; }
static int (*const s_ops[]) (int, int) = { add, mul };
static const char *const s_names[] = { "addition", "multiplication" };

static int s_once_calls;
static void once_fn (void) { s_once_calls++; }

static double since (const struct timespec *a, const struct timespec *b)
{
	return (double) (b->tv_sec - a->tv_sec) + (double) (b->tv_nsec - a->tv_nsec) / 1e9;
}

int lx_run (int argc, char **argv)
{
	printf ("lxtest : bonjour depuis un .so de Linux (glibc), argc = %d\n", argc);
	check (argc == 3 && !strcmp (argv[1], "un") && !strcmp (argv[2], "deux"), "arguments reçus");
	check (s_constructed == 42, "constructeur (DT_INIT_ARRAY) appelé avant");

	/* pointeurs relogés */
	check (s_ops[0] (6, 7) == 13 && s_ops[1] (6, 7) == 42 && !strcmp (s_names[1], "multiplication"),
	       "tables de pointeurs (RELATIVE)");

	/* mémoire et chaînes */
	char *p = malloc (16);
	strcpy (p, "Korin");
	p = realloc (p, 4096);
	strcat (p, " OS");
	char buf[64];
	int n = snprintf (buf, sizeof buf, "%s %d %.2f %x", p, 2026, 3.14159, 0xbeef);
	check (n == (int) strlen (buf) && !strcmp (buf, "Korin OS 2026 3.14 beef"), "malloc, realloc, snprintf");
	free (p);

	/* appels système par syscall (), numéros de Linux */
	long pid = syscall (SYS_getpid);
	check (pid > 0 && pid == getpid (), "syscall (SYS_getpid) = getpid ()");
	const char msg[] = "  (écrit par syscall (SYS_write, 1, …))\n";
	fflush (stdout);
	check (syscall (SYS_write, 1, msg, sizeof msg - 1) == (long) sizeof msg - 1, "syscall (SYS_write)");

	/* errno de Linux */
	errno = 0;
	check (close (-1) == -1 && errno == EBADF, "close (-1) : EBADF");
	errno = 0;
	check (syscall (4095) == -1 && errno == ENOSYS && ENOSYS == 38, "appel inconnu : ENOSYS de Linux (38)");

	/* horloges de Linux */
	struct timespec t0, t1, rt;
	check (clock_gettime (CLOCK_MONOTONIC, &t0) == 0 && clock_gettime (CLOCK_REALTIME, &rt) == 0, "clock_gettime");
	struct timespec nap = { 0, 20 * 1000 * 1000 };
	nanosleep (&nap, 0);
	syscall (SYS_clock_gettime, CLOCK_MONOTONIC, &t1);
	double dt = since (&t0, &t1);
	check (dt >= 0.015 && dt < 2.0, "nanosleep 20 ms, CLOCK_MONOTONIC par syscall");
	printf ("  (%.1f ms ; heure réelle %ld s depuis 1970)\n", dt * 1000, (long) rt.tv_sec);
	check (rt.tv_sec > 1700000000L, "CLOCK_REALTIME après 2023");

	/* ctype de glibc (__ctype_b_loc) */
	check (isalpha ('a') && isdigit ('5') && !isalpha ('5') && isspace ('\t') && toupper ('q') == 'Q'
	       && tolower ('Z') == 'z' && isxdigit ('F') && ispunct ('!'), "ctype (tables de glibc)");

	/* pthread */
	static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
	static pthread_once_t once = PTHREAD_ONCE_INIT;
	pthread_mutex_lock (&mu);
	int busy = pthread_mutex_trylock (&mu);
	pthread_mutex_unlock (&mu);
	check (busy == EBUSY && pthread_mutex_trylock (&mu) == 0, "pthread_mutex (40 octets de glibc)");
	pthread_mutex_unlock (&mu);
	pthread_once (&once, once_fn);
	pthread_once (&once, once_fn);
	check (s_once_calls == 1, "pthread_once");

	/* aléa */
	unsigned char rnd[16] = { 0 };
	int nz = 0;
	check (getrandom (rnd, sizeof rnd, 0) == (ssize_t) sizeof rnd, "getrandom");
	for (int i = 0; i < 16; i++)
		nz += rnd[i] != 0;
	check (nz > 0, "getrandom : pas que des zéros");

	fprintf (stderr, "lxtest : (sur stderr) %d échec(s)\n", s_fails);
	return s_fails;
}
