/*
 * webkit2_armpi.c -- la glue de WebKit2 propre a Onyx, pour ARMPI (docs/WEBKIT.md) : ce qu'un
 * programme lie a libWebKit.a doit fournir, d'apres Source/WebKit/Shared/onyx/OnyxSurface.h,
 * OnyxCores.h et l'en-tete de gpucomp d'Onyx.
 *
 *  - Le compositeur d'Onyx sur le V3D (gpucomp, USE(GRAPHICS_LAYER_ONYX)) : glue/gpucomp_armpi.c,
 *    le gpucomp d'Onyx tel quel sur GpuKit (SD0:/lib/gpukit.so), repli CPU sans GPU.
 *  - Les « app cores » d'Onyx (des coeurs pris hors de l'ordonnanceur pour le trace des tuiles) : sur
 *    ARMPI les threads tournent deja sur les quatre coeurs ; onyx_cores_start rend 0 (aucun) et le
 *    compositeur, s'il venait, trace sur le thread principal.
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

/* ---- gpucomp : glue/gpucomp_armpi.c (le gpucomp d'Onyx sur GpuKit) ---- */

/* ---- app cores : aucun ---- */
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
const char *onyx_cores_failure (void) { return NULL; }
unsigned long long onyx_cores_now_ns (void)
{
	unsigned long long t, f;
	__asm__ volatile ("mrs %0, cntpct_el0" : "=r" (t));
	__asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	return f ? t / f * 1000000000ull + t % f * 1000000000ull / f : 0;
}
unsigned onyx_cores_jobs_done (int worker) { (void) worker; return 0; }
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
