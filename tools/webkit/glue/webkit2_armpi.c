/*
 * webkit2_armpi.c -- la glue de WebKit2 propre a Onyx, pour ARMPI (docs/WEBKIT.md) : ce qu'un
 * programme lie a libWebKit.a doit fournir, d'apres Source/WebKit/Shared/onyx/OnyxSurface.h,
 * OnyxCores.h et l'en-tete de gpucomp d'Onyx.
 *
 *  - Le compositeur d'Onyx sur le V3D (gpucomp, USE(GRAPHICS_LAYER_ONYX)) : glue/gpucomp_armpi.c,
 *    le gpucomp d'Onyx tel quel sur GpuKit (/lib/gpukit.so), repli CPU sans GPU.
 *  - Les « app cores » d'Onyx (des coeurs pris hors de l'ordonnanceur pour le trace des tuiles) : sur
 *    le Pi, onyx_cores_start rend 0 (aucun) et le compositeur trace sur le thread principal. Sur le PC
 *    (20/11/2026) : un groupe de fils (pthreads de libposix), un par coeur moins un (7 au plus ;
 *    KORIN_WEB_RASTER_THREADS en donne le nombre, 0 : aucun) ; les parties des tuiles (512 x 256, des
 *    SkPicture rejouees) s'y tracent en parallele, avec le fil principal.
 *  - Les surfaces partagees entre le process UI et le process web (onyxsurface.c d'Onyx) : sur les
 *    tampons partages d'ARMPI (BUF_CREATE / BUF_MAP / BUF_FREE, docs/ABI.md). Le numero rendu porte
 *    la taille (le process web la retrouve sans appel) : bits 0-6 le tampon + 1, 7-18 la largeur,
 *    19-30 la hauteur (4095 au plus). Le tampon est sous 1 Go (BUF_LOW : le GPU y compose
 *    directement) et ouvert a tous les process (BUF_GRANT pid -1 : le process web, Elegant).
 */
#include <stddef.h>

/* ---- appels systeme d'ARMPI (src/inc/syscalls.inc) ---- */
#define SYS_BUF_CREATE	0x01
#define SYS_BUF_GRANT	0x02
#define BUF_LOW		(1L << 62)
#define SYS_BUF_MAP	0x03
#define SYS_BUF_FREE	0x04
#define SYS_SCREEN_INFO	0x57

#if defined(__x86_64__)
/* Korin PC : syscall (armpi/syscall.h) ; l'horloge par clock_gettime (le TSC de libposix) */
#include <time.h>
#include <armpi/syscall.h>
static long armpi_svc (long n, long a) { return armpi_syscall (n, a, 0, 0, 0, 0, 0); }
static long armpi_svc2 (long n, long a, long b) { return armpi_syscall (n, a, b, 0, 0, 0, 0); }
#else
static long armpi_svc (long n, long a)
{
	register long x0 __asm__ ("x0") = a;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x8) : "memory");
	return x0;
}

static long armpi_svc2 (long n, long a, long b)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x8) : "memory");
	return x0;
}
#endif

/* ---- gpucomp : glue/gpucomp_armpi.c (le gpucomp d'Onyx sur GpuKit) ---- */

#if defined(__x86_64__)
/* ---- app cores du PC : un groupe de fils ----
 * Les fils sont faits au premier onyx_cores_start et restent ensuite (endormis sur une condition
 * quand rien n'est a tracer) ; start et stop ne font que compter les utilisateurs (le compositeur, le
 * lecteur video). Un lot (onyx_cores_run) : chaque fil, et le fil appelant si mainToo, prend le
 * travail suivant par un compteur atomique ; l'appelant attend la fin du dernier. Un seul lot a la
 * fois (le compositeur n'en lance que depuis le fil principal ; le verrou des lots le garantit). */
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>

#define CORES_MAX	7

static pthread_mutex_t cores_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cores_work = PTHREAD_COND_INITIALIZER;
static pthread_cond_t cores_done = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t cores_batch_lock = PTHREAD_MUTEX_INITIALIZER;
static int cores_threads;		/* fils faits (-1 : on n'en fera pas) */
static int cores_users;
static unsigned cores_generation;	/* un lot de plus a chaque onyx_cores_run */
static void (*cores_fn) (void *);
static void **cores_args;
static int cores_count;
static int cores_next;			/* le travail suivant (atomique) */
static int cores_finished;		/* travaux finis (atomique) */
static int cores_busy;			/* fils encore dans le lot (sous cores_lock) */
static unsigned cores_jobs[CORES_MAX];

static int cores_wanted (void)
{
	const char *e = getenv ("KORIN_WEB_RASTER_THREADS");
	long n;
	if (e && *e)
		n = atol (e);
	else
		n = sysconf (_SC_NPROCESSORS_ONLN) - 1;
	return n < 0 ? 0 : n > CORES_MAX ? CORES_MAX : (int) n;
}

/* les travaux du lot en cours, jusqu'au dernier ; -> combien ce fil en a fait */
static unsigned cores_take (void)
{
	unsigned done = 0;
	for (;;) {
		int i = __atomic_fetch_add (&cores_next, 1, __ATOMIC_ACQ_REL);
		if (i >= cores_count)
			return done;
		cores_fn (cores_args[i]);
		__atomic_fetch_add (&cores_finished, 1, __ATOMIC_ACQ_REL);
		done++;
	}
}

static void *cores_worker (void *arg)
{
	int me = (int) (long) arg;
	unsigned seen = 0;
	pthread_mutex_lock (&cores_lock);
	for (;;) {
		while (cores_generation == seen)
			pthread_cond_wait (&cores_work, &cores_lock);
		seen = cores_generation;
		pthread_mutex_unlock (&cores_lock);
		unsigned done = cores_take ();
		pthread_mutex_lock (&cores_lock);
		cores_jobs[me] += done;
		if (!--cores_busy)
			pthread_cond_signal (&cores_done);
	}
	return NULL;
}

int onyx_cores_start (int wanted)
{
	(void) wanted;		/* (Onyx en demande 2 : ici, tous les coeurs moins un) */
	pthread_mutex_lock (&cores_lock);
	if (!cores_threads) {
		int n = cores_wanted ();
		pthread_attr_t a;
		pthread_attr_init (&a);
		pthread_attr_setstacksize (&a, 1 << 20);
		for (int i = 0; i < n; i++) {
			pthread_t t;
			if (pthread_create (&t, &a, cores_worker, (void *) (long) i))
				break;
			pthread_detach (t);
			cores_threads++;
		}
		pthread_attr_destroy (&a);
		if (!cores_threads)
			cores_threads = -1;
	}
	int n = cores_threads > 0 ? cores_threads : 0;
	if (n)
		cores_users++;
	pthread_mutex_unlock (&cores_lock);
	return n;
}

void onyx_cores_stop (void)
{
	pthread_mutex_lock (&cores_lock);
	if (cores_users)
		cores_users--;
	pthread_mutex_unlock (&cores_lock);
}

int onyx_cores_count (void)
{
	return cores_threads > 0 ? cores_threads : 0;
}

int onyx_cores_run (void (*fn) (void *), void (*reset) (void *), void **args, int count, int mainToo)
{
	(void) reset;		/* (un fil ne s'arrete pas en cours de travail : rien a refaire) */
	if (count <= 0)
		return 0;
	if (cores_threads <= 0 || count == 1) {
		for (int i = 0; i < count; i++)
			fn (args[i]);
		return 0;
	}
	pthread_mutex_lock (&cores_batch_lock);
	pthread_mutex_lock (&cores_lock);
	cores_fn = fn;
	cores_args = args;
	cores_count = count;
	__atomic_store_n (&cores_finished, 0, __ATOMIC_RELEASE);
	__atomic_store_n (&cores_next, 0, __ATOMIC_RELEASE);
	cores_busy = cores_threads;
	cores_generation++;
	pthread_cond_broadcast (&cores_work);
	pthread_mutex_unlock (&cores_lock);
	if (mainToo)
		cores_take ();
	/* tous les fils sortis du lot : le suivant ne peut pas commencer sous eux */
	pthread_mutex_lock (&cores_lock);
	while (cores_busy)
		pthread_cond_wait (&cores_done, &cores_lock);
	pthread_mutex_unlock (&cores_lock);
	pthread_mutex_unlock (&cores_batch_lock);
	return 0;
}

unsigned onyx_cores_jobs_done (int worker)
{
	return worker >= 0 && worker < CORES_MAX ? cores_jobs[worker] : 0;
}
#else
/* ---- app cores du Pi : aucun ---- */
int onyx_cores_start (int wanted) { (void) wanted; return 0; }
void onyx_cores_stop (void) { }
int onyx_cores_count (void) { return 0; }
int onyx_cores_run (void (*fn) (void *), void (*reset) (void *), void **args, int count, int mainToo)
{
	(void) reset; (void) mainToo;
	for (int i = 0; i < count; i++)		/* (tout sur le thread appelant) */
		fn (args[i]);
	return 0;
}
unsigned onyx_cores_jobs_done (int worker) { (void) worker; return 0; }
#endif
const char *onyx_cores_failure (void) { return NULL; }
unsigned long long onyx_cores_now_ns (void)
{
#if defined(__x86_64__)
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned long long) ts.tv_sec * 1000000000ull + (unsigned long long) ts.tv_nsec;
#else
	unsigned long long t, f;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (t));
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? t / f * 1000000000ull + t % f * 1000000000ull / f : 0;
#endif
}
/* (le decodeur video a son propre fil, qui tourne deja sur un autre coeur : -1, il decode lui-meme) */
int onyx_cores_offload (void (*job) (void *), void *arg) { (void) job; (void) arg; return -1; }
unsigned onyx_cores_side_jobs_done (void) { return 0; }

/* ---- surfaces : tampons partages d'ARMPI ---- */
void onyx_screen_size (int *width, int *height)
{
	long info[4] = { 0 };
	armpi_svc (SYS_SCREEN_INFO, (long) info);
	if (width) *width = (int) info[0];
	if (height) *height = (int) info[1];
}

int onyx_surface_create (int width, int height)
{
	if (width <= 0 || height <= 0 || width > 4095 || height > 4095)
		return 0;
	long n = armpi_svc (SYS_BUF_CREATE, (long) width * height * 4 | BUF_LOW);
	if (n < 0)
		n = armpi_svc (SYS_BUF_CREATE, (long) width * height * 4);
	if (n < 0 || n > 126)
		return 0;
	armpi_svc2 (SYS_BUF_GRANT, n, -1);
	return (int) ((n + 1) | (long) width << 7 | (long) height << 19);
}

unsigned *onyx_surface_map (int id, int *width, int *height)
{
	if (id <= 0)
		return NULL;
	long p = armpi_svc (SYS_BUF_MAP, (id & 0x7F) - 1);
	if (p <= 0)
		return NULL;
	if (width) *width = (id >> 7) & 0xFFF;
	if (height) *height = (id >> 19) & 0xFFF;
	return (unsigned *) p;
}

/* son tampon partage (ui_surface_source) */
int onyx_surface_buffer (int id)
{
	return (id & 0x7F) - 1;
}

void onyx_surface_destroy (int id)
{
	if (id > 0)
		armpi_svc (SYS_BUF_FREE, (id & 0x7F) - 1);
}
