/*
 * rdlhost : hôte C (-rdynamic) qui charge la cdylib Rust librdl.so par dlopen, comme python3
 * chargera un module pyo3 (docs/RUST.md). Essais : symboles, constructeur, thread_local! du module
 * sur le thread principal et sur 4 pthreads (réserve TLS de l'hôte), destructeurs de TLS à la
 * fin d'un thread, panique rattrapée dans le module, println! du module, thread de std lancé par
 * le module.
 *     rdlhost [chemin de librdl.so]
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned (*bump_t) (void);
typedef int (*name_t) (const char *);
static bump_t bump;
static name_t name;
static int fails;

#define CHECK(c, ...) do { if (c) printf ("  ok      " __VA_ARGS__); \
	else { fails++; printf ("  ÉCHEC   " __VA_ARGS__); } printf ("\n"); } while (0)

static void *worker (void *arg)
{
	long i = (long) arg;
	unsigned last = 0;
	char buf[32];
	for (int k = 0; k < 1000; k++)
		last = bump ();
	snprintf (buf, sizeof buf, "thread-%ld", i);
	int same = name (buf);
	return (void *) (long) (last == 1000 && same);
}

int main (int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "librdl.so";
	void *h = dlopen (path, RTLD_NOW);
	if (!h) {
		printf ("  ÉCHEC   dlopen (%s) : %s\n", path, dlerror ());
		return 1;
	}
	CHECK (1, "dlopen (%s)", path);
	bump = (bump_t) dlsym (h, "rdl_bump");
	name = (name_t) dlsym (h, "rdl_name");
	void (*stats) (unsigned *, unsigned *, unsigned *) = dlsym (h, "rdl_stats");
	int (*panic) (void) = dlsym (h, "rdl_panic");
	char *(*hello) (unsigned) = dlsym (h, "rdl_hello");
	void (*rfree) (char *) = dlsym (h, "rdl_free");
	unsigned (*spawn) (void) = dlsym (h, "rdl_spawn");
	CHECK (bump && name && stats && panic && hello && rfree && spawn, "dlsym des 7 fonctions");
	if (!(bump && name && stats && panic && hello && rfree && spawn))
		return 1;

	unsigned log, drops, ctor;
	stats (&log, &drops, &ctor);
	CHECK (ctor == 1234, "constructeur du module (.init_array) : %u", ctor);

	unsigned a = bump (), b = bump (), c = bump ();
	CHECK (a == 1 && b == 2 && c == 3, "thread_local! du module, thread principal : %u %u %u", a, b, c);
	CHECK (name ("principal"), "thread_local! String (RefCell) du module");

	pthread_t t[4];
	for (long i = 0; i < 4; i++)
		pthread_create (&t[i], NULL, worker, (void *) i);
	int good = 0;
	for (int i = 0; i < 4; i++) {
		void *r;
		pthread_join (t[i], &r);
		good += (int) (long) r;
	}
	CHECK (good == 4, "4 pthreads de l'hôte : chacun son compteur (1000) : %d sur 4", good);
	CHECK (bump () == 4, "le compteur du thread principal n'a pas bougé");
	stats (&log, &drops, &ctor);
	CHECK (log == 5, "journal partagé (Mutex du module) : %u entrées", log);
	CHECK (drops == 4, "destructeurs de thread_local! à la fin des 4 pthreads : %u", drops);

	CHECK (panic () == 1, "panique levée et rattrapée dans le module (catch_unwind)");

	char *s = hello (21);
	CHECK (s && strcmp (s, "rdl:42") == 0, "chaîne allouée par le module : %s", s ? s : "(nul)");
	rfree (s);

	unsigned n = spawn ();
	CHECK (n == 2, "std::thread::spawn dans le module : %u", n);

	Dl_info info;
	CHECK (dladdr ((void *) bump, &info) && info.dli_sname && strcmp (info.dli_sname, "rdl_bump") == 0,
	       "dladdr : %s", info.dli_sname ? info.dli_sname : "(nul)");
	printf ("rdlhost : %d échec(s)\n", fails);
	return fails;
}
