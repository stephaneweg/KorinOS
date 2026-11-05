/*
 * glkit_host.c -- GlKit cote programme (libglkit.a, docs/MESA.md « GlKit ») : au premier appel
 * d'une fonction de GL ou d'armgl, ouvre le kit (SD0:/lib/glkit.so, lu par AppKit la premiere
 * fois), le lie a ce process (glkit_init : la libc du programme, sa zone par thread) et remplit
 * les relais (glkit_stubs.S). Un kit absent ou incompatible : message et fin du programme.
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <armpi/kit.h>

#define GLKIT_VERSION 4			/* fonctions de la table (src/kits/glkit/glkit.abi) */
#define GLKIT_TLS_AREA 256		/* les variables par thread du kit, ici (glkit.c le verifie) */

struct glkit_import { const char *name; uintptr_t value; long kind; };

extern const long glkit_import_count;
long glkit_imports_fill (struct glkit_import *t);
extern void *glkit_slots[];
extern const char *const glkit_names[];
extern const long glkit_count;

/* la zone par thread du kit : a un decalage fixe de TPIDR_EL0 dans un programme statique */
static __thread char glkit_tls_area[GLKIT_TLS_AREA] __attribute__ ((aligned (64)));

static pthread_mutex_t s_lock = PTHREAD_MUTEX_INITIALIZER;
static volatile int s_ready;

static void die (const char *a, const char *b)
{
	fprintf (stderr, "%s%s\n", a, b ? b : "");
	fflush (stderr);
	exit (127);
}

/* (glkit_lazy) : le kit, lie a ce process, et tous les relais remplis */
void glkit_load (void)
{
	if (s_ready)
		return;
	pthread_mutex_lock (&s_lock);
	if (!s_ready)
	{
		void *k = armpi_kit_open ("glkit", GLKIT_VERSION);
		if (k == 0)
			die ("glkit : SD0:/lib/glkit.so absent ou trop ancien", 0);
		long (*init) (const struct glkit_import *, long, long, long) = armpi_kit_fn (k, 1);
		void *(*sym) (const char *) = armpi_kit_fn (k, 2);
		const char *(*err) (void) = armpi_kit_fn (k, 3);
		static struct glkit_import *imp;
		imp = malloc (glkit_import_count * sizeof *imp);
		if (imp == 0)
			die ("glkit : plus de memoire", 0);
		long n = glkit_imports_fill (imp);
		long off = glkit_tls_area - (char *) __builtin_thread_pointer ();
		if (init (imp, n, off, sizeof glkit_tls_area) != 0)
			die (err (), 0);
		for (long i = 0; i < glkit_count; i++)
		{
			void *f = sym (glkit_names[i]);
			if (f == 0)
				die ("glkit : fonction absente du kit : ", glkit_names[i]);
			glkit_slots[i] = f;
		}
		__atomic_store_n (&s_ready, 1, __ATOMIC_RELEASE);
	}
	pthread_mutex_unlock (&s_lock);
}
