/*
 * armpi_drm.c -- le faux libdrm d'ARMPI, c'est-a-dire le winsys du pilote v3d de Mesa (docs/MESA.md).
 *
 * Linux fait pour Mesa trois choses, ici dans le process :
 *   - les BO : tranches de grands tampons partages sous 1 Go (le GPU n'a pas de MMU : bo->offset de Mesa
 *     est une adresse physique). Non caches pour le processeur, comme les BO de Linux (write-combine) ;
 *   - les jobs : V3D_OP_SUBMIT au service v3d, qui rend la main quand le GPU a fini ;
 *   - la synchronisation : tout job est fini au retour, donc tout syncobj est signale.
 *
 * Sans service v3d (QEMU), armpi_drm_open donne un mode « a blanc » : jobs comptes et pas lances. Tout
 * Mesa tourne alors, compilateur et listes compris, sans GPU ; memoire ordinaire (ARMPI_DRM_DRY) ou les
 * vrais tampons non caches sous 1 Go (ARMPI_DRM_DRY_BUF).
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include "xf86drm.h"
#include "drm-uapi/v3d_drm.h"
#include "armpi_drm.h"

#define SYS_BUF_CREATE	0x01
#define SYS_BUF_GRANT	0x02
#define SYS_BUF_FREE	0x04
#define SYS_BUF_INFO	0x0D
#define SYS_SVC_OPEN	0x31
#define SYS_SVC_CALL	0x32
#define SYS_WS_CALL	0x51
#define WS_OP_SURFACE_ATTACH	25
#define WS_OP_SURFACE_DAMAGE	26
#define WS_SF_NOREDRAW	256	/* meme tampon, meme zone : DAMAGE dit quoi recomposer */
#define E_PERM		(-1)
#define BUF_LOW		(1UL << 62)	/* pages sous 1 Go */
#define BUF_NOCACHE	(1UL << 61)	/* vue non cachee (src/kernel/ws.asm) */
#define BUF_VA_BASE	0x6800000000UL
#define BUF_SLOT	0x4000000UL	/* 64 Mo : le plus grand tampon */

/* src/inc/v3d.inc */
#define V3D_OP_INFO	1
#define V3D_OP_SUBMIT	3
#define V3DI_STATE	8
#define V3DI_UIFCFG	64
#define V3DR_BCL	16
#define V3DR_BCL_LEN	24
#define V3DR_RCL	48
#define V3DR_RCL_LEN	56
#define V3DR_TALLOC	64
#define V3DR_TALLOC_LEN	72
#define V3DR_TSTATE	80

#define PAGE		4096UL
#define CHUNK		(32UL << 20)	/* un morceau : 128 tampons dans tout le systeme */
#define MAX_CHUNKS	16
#define FAKE_FD		0x7F000

struct block {
	struct block *next, *prev;
	uint32_t off, bytes;
	int used;
};
struct chunk {
	long buf;			/* numero du tampon, -1 : a blanc */
	uint8_t *va;
	uint32_t phys, bytes;
	struct block *blocks;		/* blocs tries par adresse, libres ou non */
	int imported;			/* une image de fenetre : ni morceau ni allocateur */
};
struct bo {
	struct chunk *c;
	struct block *b;
};

static struct chunk chunks[MAX_CHUNKS];
static int nchunks;
static struct bo *bos;
static uint32_t nbos;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static long chan = -1;
static int dry;
static uint32_t next_syncobj = 1, dry_phys = 0x10000000;
static long jobs;
static uint32_t idents[7];

static inline long sys2 (long n, long a, long b)
{
	register long x0 __asm__ ("x0") = a;
	register long x1 __asm__ ("x1") = b;
	register long x8 __asm__ ("x8") = n;
	__asm__ volatile ("svc 0" : "+r" (x0) : "r" (x1), "r" (x8) : "memory");
	return x0;
}

/* -------- les morceaux ------------------------------------------------------------------------- */
static struct chunk *chunk_new (uint32_t bytes)
{
	if (nchunks == MAX_CHUNKS)
		return NULL;
	struct chunk *c = &chunks[nchunks];
	if (dry == ARMPI_DRM_DRY) {
		void *p = NULL;
		if (posix_memalign (&p, PAGE, bytes))
			return NULL;
		memset (p, 0, bytes);
		c->buf = -1;
		c->va = p;
		c->phys = dry_phys;
		dry_phys += bytes;
	} else {
		long info[4];
		long n = sys2 (SYS_BUF_CREATE, (long) ((unsigned long) bytes | BUF_LOW | BUF_NOCACHE), 0);
		if (n < 0)
			return NULL;
		if (sys2 (SYS_BUF_INFO, n, (long) info) < 0 || info[0] + bytes > 0x40000000L) {
			sys2 (SYS_BUF_FREE, n, 0);
			return NULL;
		}
		c->buf = n;
		c->va = (uint8_t *) (BUF_VA_BASE + n * BUF_SLOT);
		c->phys = (uint32_t) info[0];
	}
	c->bytes = bytes;
	struct block *b = calloc (1, sizeof *b);
	b->bytes = bytes;
	c->blocks = b;
	nchunks++;
	return c;
}

static struct block *chunk_alloc (struct chunk *c, uint32_t bytes)
{
	for (struct block *b = c->blocks; b; b = b->next) {
		if (b->used || b->bytes < bytes)
			continue;
		if (b->bytes > bytes) {
			struct block *r = calloc (1, sizeof *r);
			r->off = b->off + bytes;
			r->bytes = b->bytes - bytes;
			r->next = b->next;
			r->prev = b;
			if (r->next)
				r->next->prev = r;
			b->next = r;
			b->bytes = bytes;
		}
		b->used = 1;
		return b;
	}
	return NULL;
}

static void chunk_free (struct block *b)
{
	struct block *n = b->next, *p = b->prev;
	b->used = 0;
	if (n && !n->used) {
		b->bytes += n->bytes;
		b->next = n->next;
		if (b->next)
			b->next->prev = b;
		free (n);
	}
	if (p && !p->used) {
		p->bytes += b->bytes;
		p->next = b->next;
		if (p->next)
			p->next->prev = p;
		free (b);
	}
}

static uint32_t new_handle (void);

/* -------- les ioctl ----------------------------------------------------------------------------- */
static int create_bo (struct drm_v3d_create_bo *a)
{
	uint32_t bytes = (a->size + PAGE - 1) & ~(PAGE - 1);
	struct block *b = NULL;
	struct chunk *c = NULL;
	for (int i = 0; i < nchunks && !b; i++) {
		c = &chunks[i];
		b = chunk_alloc (c, bytes);
	}
	if (!b) {
		/* un seul bloc de pages sous 1 Go : plus petit si la memoire basse est morcelee */
		uint32_t size = bytes > CHUNK ? bytes : CHUNK;
		if (size > BUF_SLOT)
			return -ENOMEM;
		while (!(c = chunk_new (size))) {
			if (size / 2 < bytes || size / 2 < (1U << 20))
				return -ENOMEM;
			size /= 2;
		}
		b = chunk_alloc (c, bytes);
	}
	/* Linux rend un BO a zero (un morceau neuf l'est, pas un bloc rendu) */
	memset (c->va + b->off, 0, bytes);
	uint32_t h = new_handle ();
	bos[h].c = c;
	bos[h].b = b;
	a->handle = h;
	a->offset = c->phys + b->off;
	return 0;
}

static uint32_t new_handle (void)
{
	uint32_t h = 1;
	while (h < nbos && bos[h].b)
		h++;
	if (h >= nbos) {
		uint32_t n = nbos ? nbos * 2 : 256;
		bos = realloc (bos, n * sizeof *bos);
		memset (bos + nbos, 0, (n - nbos) * sizeof *bos);
		nbos = n;
	}
	return h;
}

static struct bo *lookup (uint32_t h)
{
	return h && h < nbos && bos[h].b ? &bos[h] : NULL;
}

static int get_param (struct drm_v3d_get_param *p)
{
	switch (p->param) {
	case DRM_V3D_PARAM_V3D_UIFCFG:
	case DRM_V3D_PARAM_V3D_HUB_IDENT1:
	case DRM_V3D_PARAM_V3D_HUB_IDENT2:
	case DRM_V3D_PARAM_V3D_HUB_IDENT3:
	case DRM_V3D_PARAM_V3D_CORE0_IDENT0:
	case DRM_V3D_PARAM_V3D_CORE0_IDENT1:
	case DRM_V3D_PARAM_V3D_CORE0_IDENT2:
		p->value = idents[p->param - DRM_V3D_PARAM_V3D_UIFCFG];
		return 0;
	default:	/* TFU, CSD, perfmon, multisync, file CPU... : facultatifs pour v3d, absents ici */
		p->value = 0;
		return 0;
	}
}

static int lost;

int armpi_drm_lost (void)
{
	return lost;
}

static int submit_cl (struct drm_v3d_submit_cl *s)
{
	long msg[64];
	jobs++;
	if (dry)
		return 0;
	memset (msg, 0, sizeof msg);
	msg[0] = V3D_OP_SUBMIT;
	msg[V3DR_BCL / 8] = s->bcl_start;
	msg[V3DR_BCL_LEN / 8] = (uint32_t) (s->bcl_end - s->bcl_start);	/* la fin peut etre dans un autre BO */
	msg[V3DR_RCL / 8] = s->rcl_start;
	msg[V3DR_RCL_LEN / 8] = (uint32_t) (s->rcl_end - s->rcl_start);
	msg[V3DR_TALLOC / 8] = s->qma;
	msg[V3DR_TALLOC_LEN / 8] = s->qms;
	msg[V3DR_TSTATE / 8] = s->qts;
	__asm__ volatile ("dsb st" : : : "memory");	/* les ecritures non cachees, avant le GPU */
	if (sys2 (SYS_SVC_CALL, chan, (long) msg) < 0 || msg[0] != 0) {
		lost = 1;
		return -EIO;
	}
	return 0;
}

int drmIoctl (int fd, unsigned long request, void *arg)
{
	int r = -ENOTTY;
	(void) fd;
	pthread_mutex_lock (&lock);
	switch (request) {
	case DRM_IOCTL_V3D_CREATE_BO:
		r = create_bo (arg);
		break;
	case DRM_IOCTL_V3D_GET_BO_OFFSET: {
		struct drm_v3d_get_bo_offset *a = arg;
		struct bo *b = lookup (a->handle);
		r = -ENOENT;
		if (b) {
			a->offset = b->c->phys + b->b->off;
			r = 0;
		}
		break;
	}
	case DRM_IOCTL_V3D_MMAP_BO:
		((struct drm_v3d_mmap_bo *) arg)->offset = 0;
		r = 0;
		break;
	case DRM_IOCTL_GEM_OPEN: {	/* le nom d'une image de fenetre est son handle */
		struct drm_gem_open *a = arg;
		struct bo *b = lookup ((uint32_t) a->name);
		r = -ENOENT;
		if (b) {
			a->handle = (uint32_t) a->name;
			a->size = b->b->bytes;
			r = 0;
		}
		break;
	}
	case DRM_IOCTL_GEM_CLOSE: {
		struct bo *b = lookup (((struct drm_gem_close *) arg)->handle);
		r = -ENOENT;
		if (b) {
			if (b->c->imported) {
				free (b->c);
				free (b->b);
			} else {
				chunk_free (b->b);
			}
			b->b = NULL;
			r = 0;
		}
		break;
	}
	case DRM_IOCTL_V3D_WAIT_BO:	/* le service a rendu la main : tout est fini */
		r = 0;
		break;
	case DRM_IOCTL_V3D_GET_PARAM:
		r = get_param (arg);
		break;
	case DRM_IOCTL_V3D_SUBMIT_CL:
		r = submit_cl (arg);
		break;
	default:			/* TFU, CSD... : Mesa revient au rendu */
		r = -ENODEV;
		break;
	}
	pthread_mutex_unlock (&lock);
	if (r < 0) {
		errno = -r;
		return -1;
	}
	return 0;
}

void *armpi_v3d_bo_map (uint32_t handle)
{
	void *p;
	pthread_mutex_lock (&lock);
	struct bo *b = lookup (handle);
	p = b ? b->c->va + b->b->off : MAP_FAILED;
	pthread_mutex_unlock (&lock);
	return p;
}

/* -------- les images d'une fenetre ---------------------------------------------------------------
 * Le GPU rend directement dans une surface d'Elegant. Un seul tampon partage sous 1 Go, deux images :
 * Elegant compose a tout moment, il montre l'une pendant que le GPU ecrit l'autre, et rattacher le
 * tampon avec un autre decalage bascule de l'une a l'autre (req_surface_attach). Le tampon reste cache
 * pour qu'Elegant le lise vite : invalide apres chaque image, comme les cibles de GpuKit.
 */
struct win {
	long buf, id;
	uint8_t *va;
	uint32_t phys, pitch, frame;
	int x, y, w, h;			/* la zone montree */
	int height;			/* la hauteur de l'image */
	long ns_cache, ns_attach, frames;
};

static long now_ns (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000L + t.tv_nsec;
}

static void cache_flush (uint8_t *p, unsigned long bytes)
{
	uint8_t *end = p + bytes;
	for (p = (uint8_t *) ((uintptr_t) p & ~63UL); p < end; p += 64)
		__asm__ volatile ("dc civac, %0" : : "r" (p));
	__asm__ volatile ("dsb sy" : : : "memory");
}

static uint32_t import_image (struct win *w, uint32_t off)
{
	struct chunk *c = calloc (1, sizeof *c);
	struct block *b = calloc (1, sizeof *b);
	c->imported = 1;
	c->va = w->va + off;
	c->phys = w->phys + off;
	c->bytes = w->frame;
	b->bytes = w->frame;
	b->used = 1;
	pthread_mutex_lock (&lock);
	uint32_t h = new_handle ();
	bos[h].c = c;
	bos[h].b = b;
	pthread_mutex_unlock (&lock);
	return h;
}

/* une zone qui n'est pas a nous (une texture de GpuKit) : un BO de plus -> son nom pour GEM_OPEN */
unsigned armpi_drm_import (void *va, unsigned phys, unsigned bytes)
{
	struct chunk *c = calloc (1, sizeof *c);
	struct block *b = calloc (1, sizeof *b);
	c->imported = 1;
	c->va = va;
	c->phys = phys;
	c->bytes = bytes;
	b->bytes = bytes;
	b->used = 1;
	pthread_mutex_lock (&lock);
	uint32_t h = new_handle ();
	bos[h].c = c;
	bos[h].b = b;
	pthread_mutex_unlock (&lock);
	return h;
}

void *armpi_drm_buffer (unsigned long bytes, unsigned *name)
{
	long info[4], buf;
	if (bytes > BUF_SLOT)
		return NULL;
	buf = sys2 (SYS_BUF_CREATE, (long) (bytes | BUF_LOW), 0);
	if (buf < 0)
		return NULL;
	if (sys2 (SYS_BUF_INFO, buf, (long) info) < 0 || info[0] + (long) bytes > 0x40000000L) {
		sys2 (SYS_BUF_FREE, buf, 0);
		return NULL;
	}
	void *va = (void *) (BUF_VA_BASE + buf * BUF_SLOT);
	cache_flush (va, bytes);
	*name = armpi_drm_import (va, (uint32_t) info[0], bytes);
	return va;
}

void armpi_drm_flush (void *va, unsigned long bytes)
{
	cache_flush (va, bytes);
}

static long attach (struct win *w, int image)
{
	long msg[64];
	for (int i = 0; i < 2; i++) {
		memset (msg, 0, sizeof msg);
		msg[0] = WS_OP_SURFACE_ATTACH;
		msg[1] = w->id;
		msg[2] = w->buf;
		msg[3] = (long) w->x | (long) w->y << 32;
		msg[4] = (long) w->w | (long) w->h << 32;
		((uint32_t *) msg)[10] = 16;		/* longueur des donnees */
		((uint32_t *) msg)[12] = WS_SF_NOREDRAW;	/* WS_SF_XRGB8888 */
		((uint32_t *) msg)[13] = w->pitch;
		msg[7] = (long) image * w->frame + (long) (w->height - w->h) * w->pitch;
		long r = sys2 (SYS_WS_CALL, (long) msg, 0);
		if (r != E_PERM)
			return r;
		sys2 (SYS_BUF_GRANT, w->buf, msg[1]);	/* la reponse donne le pid d'Elegant */
	}
	return E_PERM;
}

/* tout ce qui est dans la zone est a recomposer */
static long damage (struct win *w)
{
	long msg[64];
	memset (msg, 0, sizeof msg);
	msg[0] = WS_OP_SURFACE_DAMAGE;
	msg[1] = w->id;
	return sys2 (SYS_WS_CALL, (long) msg, 0);
}

/* la zone montree : le coin haut gauche x, y dans le contenu ; l'image du GL est le bas de l'image entiere
 * (Skia compte depuis le bas, et sa hauteur est celle de la zone), sa largeur en commence a gauche */
void armpi_drm_zone (void *window, long id, int x, int y, int width, int height)
{
	struct win *w = window;
	w->id = id;
	w->x = x;
	w->y = y;
	w->w = width < (int) (w->pitch / 4) ? width : (int) (w->pitch / 4);
	w->h = height < w->height ? height : w->height;
}

void *armpi_drm_window (long id, int x, int y, int width, int height, uint32_t names[2], uint32_t *pitch)
{
	struct win *w = calloc (1, sizeof *w);
	long info[4];
	w->id = id;
	w->x = x;
	w->y = y;
	w->w = width;
	w->h = height;
	w->height = height;
	w->pitch = ((uint32_t) width * 4 + 63) & ~63U;
	w->frame = (w->pitch * (uint32_t) height + PAGE - 1) & ~(PAGE - 1);
	if (2UL * w->frame > BUF_SLOT)
		goto fail;
	w->buf = sys2 (SYS_BUF_CREATE, (long) (2UL * w->frame | BUF_LOW), 0);
	if (w->buf < 0)
		goto fail;
	if (sys2 (SYS_BUF_INFO, w->buf, (long) info) < 0 || info[0] + 2L * w->frame > 0x40000000L) {
		sys2 (SYS_BUF_FREE, w->buf, 0);
		goto fail;
	}
	w->va = (uint8_t *) (BUF_VA_BASE + w->buf * BUF_SLOT);
	w->phys = (uint32_t) info[0];
	cache_flush (w->va, 2UL * w->frame);		/* les zeros du noyau, avant le GPU */
	names[0] = import_image (w, 0);
	names[1] = import_image (w, w->frame);
	*pitch = w->pitch;
	return w;		/* attachee par la premiere image : avant, elle serait noire a l'ecran */
fail:
	free (w);
	return NULL;
}

/* detachee de sa fenetre, tampon rendu ; les images de Mesa d'abord */
void armpi_drm_window_free (void *window)
{
	struct win *w = window;
	long msg[64];
	memset (msg, 0, sizeof msg);
	msg[0] = WS_OP_SURFACE_ATTACH;
	msg[1] = w->id;
	msg[2] = -1;
	sys2 (SYS_WS_CALL, (long) msg, 0);
	sys2 (SYS_BUF_FREE, w->buf, 0);
	free (w);
}

/* montre l'image <image>, que le GPU a finie */
int armpi_drm_present (void *window, int image)
{
	struct win *w = window;
	uint8_t *p = w->va + (unsigned long) image * w->frame;
	if (dry) {					/* a blanc, de quoi voir la bascule sous QEMU */
		uint32_t c = image ? 0x0000C040 : 0x00C04000;
		for (uint32_t i = 0; i < w->frame / 4; i++)
			((uint32_t *) p)[i] = c;
	}
	long t0 = now_ns ();
	cache_flush (p + (unsigned long) (w->height - w->h) * w->pitch, (unsigned long) w->h * w->pitch);
	long t1 = now_ns ();
	long r = attach (w, image);
	if (r >= 0)
		r = damage (w);
	w->ns_cache += t1 - t0;
	w->ns_attach += now_ns () - t1;
	w->frames++;
	return r < 0 ? -1 : 0;
}

void armpi_drm_times_reset (void *window)
{
	struct win *w = window;
	w->ns_cache = w->ns_attach = w->frames = 0;
}

/* temps moyens par image (ms) : cache, puis rattachement */
void armpi_drm_times (void *window, double *cache, double *attach_ms)
{
	struct win *w = window;
	long n = w->frames ? w->frames : 1;
	*cache = w->ns_cache / 1e6 / n;
	*attach_ms = w->ns_attach / 1e6 / n;
}

/* -------- la synchronisation : tout job est fini au retour --------------------------------------- */
int drmSyncobjCreate (int fd, uint32_t flags, uint32_t *handle)
{
	(void) fd; (void) flags;
	*handle = __atomic_fetch_add (&next_syncobj, 1, __ATOMIC_RELAXED);
	return 0;
}
int drmSyncobjDestroy (int fd, uint32_t handle) { (void) fd; (void) handle; return 0; }
int drmSyncobjWait (int fd, uint32_t *handles, unsigned count, int64_t timeout_nsec, unsigned flags,
		    uint32_t *first_signaled)
{
	(void) fd; (void) handles; (void) count; (void) timeout_nsec; (void) flags;
	if (first_signaled)
		*first_signaled = 0;
	return 0;
}
int drmSyncobjImportSyncFile (int fd, uint32_t handle, int sync_file_fd)
{
	(void) fd; (void) handle; (void) sync_file_fd;
	return 0;
}
/* un numero qui n'est celui d'aucun fichier : Mesa ne fera que le refermer (EBADF) */
int drmSyncobjExportSyncFile (int fd, uint32_t handle, int *sync_file_fd)
{
	(void) fd; (void) handle;
	*sync_file_fd = FAKE_FD;
	return 0;
}

/* -------- le reste de libdrm : pour Vulkan ou le partage entre process, jamais pour ce pilote ------ */
int drmSyncobjFDToHandle (int fd, int obj_fd, uint32_t *h) { (void) fd; (void) obj_fd; (void) h; return -ENOSYS; }
int drmSyncobjHandleToFD (int fd, uint32_t h, int *obj_fd) { (void) fd; (void) h; (void) obj_fd; return -ENOSYS; }
int drmSyncobjReset (int fd, const uint32_t *h, uint32_t n) { (void) fd; (void) h; (void) n; return 0; }
int drmSyncobjSignal (int fd, const uint32_t *h, uint32_t n) { (void) fd; (void) h; (void) n; return 0; }
int drmSyncobjTimelineSignal (int fd, const uint32_t *h, uint64_t *p, uint32_t n)
{
	(void) fd; (void) h; (void) p; (void) n;
	return 0;
}
int drmSyncobjTimelineWait (int fd, uint32_t *h, uint64_t *p, unsigned n, int64_t t, unsigned f, uint32_t *first)
{
	(void) fd; (void) h; (void) p; (void) n; (void) t; (void) f; (void) first;
	return 0;
}
int drmSyncobjQuery2 (int fd, uint32_t *h, uint64_t *p, uint32_t n, uint32_t f)
{
	(void) fd; (void) h; (void) p; (void) n; (void) f;
	return -ENOSYS;
}
int drmSyncobjTransfer (int fd, uint32_t d, uint64_t dp, uint32_t s, uint64_t sp, uint32_t f)
{
	(void) fd; (void) d; (void) dp; (void) s; (void) sp; (void) f;
	return -ENOSYS;
}
int drmGetCap (int fd, uint64_t c, uint64_t *v) { (void) fd; (void) c; *v = 0; return -ENOSYS; }
int drmPrimeFDToHandle (int fd, int p, uint32_t *h) { (void) fd; (void) p; (void) h; return -ENOSYS; }
int drmPrimeHandleToFD (int fd, uint32_t h, uint32_t f, int *p) { (void) fd; (void) h; (void) f; (void) p; return -ENOSYS; }
drmVersionPtr drmGetVersion (int fd) { (void) fd; return NULL; }
void drmFreeVersion (drmVersionPtr v) { (void) v; }

/* -------- ouverture ------------------------------------------------------------------------------ */
int armpi_drm_open (int mode)
{
	/* valeurs de V3D 4.2 du Pi 4 et de V3D 7.1 du Pi 5 (src/broadcom/drm-shim/v3d_noop.c), pour le mode a blanc */
	static const uint32_t pi4[7] = { 0x45, 0x000e1124, 0x100, 0xe00, 0x04443356, 0x81001422, 0x40078121 };
	static const uint32_t pi5[7] = { 0x45, 0x00081117, 0x1900, 0x700, 0x07443356, 0x81001441, 0xc0078101 };
	long msg[64];
	memcpy (idents, (mode & ARMPI_DRM_V71) ? pi5 : pi4, sizeof idents);
	mode &= ~ARMPI_DRM_V71;
	if (mode) {
		dry = mode;
		return FAKE_FD;
	}
	chan = sys2 (SYS_SVC_OPEN, (long) "v3d", 0);
	if (chan < 0)
		return -1;
	memset (msg, 0, sizeof msg);
	msg[0] = V3D_OP_INFO;
	if (sys2 (SYS_SVC_CALL, chan, (long) msg) < 0 || msg[V3DI_STATE / 8] != 1)
		return -1;
	/* un ancien service ne remplit pas ces mots : il faut alors un service a jour */
	if (msg[V3DI_UIFCFG / 8 + 4] == 0)
		return -1;
	for (int i = 0; i < 7; i++)
		idents[i] = (uint32_t) msg[V3DI_UIFCFG / 8 + i];
	return FAKE_FD;
}

long armpi_drm_jobs (void)
{
	return jobs;
}

/* adresse physique du premier morceau, 0 s'il n'y en a pas */
unsigned armpi_drm_first_phys (void)
{
	return nchunks ? chunks[0].phys : 0;
}
