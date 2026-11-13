/*
 * korin_i915.c -- le faux libdrm du PC de Korin, c'est-a-dire le winsys d'iris (docs/etude/GPU-INTEL.md, § 3).
 *
 * Linux fait pour iris (pilote i915 du noyau) : BO, contextes, espaces d'adresses, soumission des lots,
 * synchronisation, et les renseignements sur la puce (GETPARAM, QUERY). Ici, deux modes :
 *   - a blanc (ARMPI_DRM_DRY) : tout est servi dans le process. Les BO sont de la memoire ordinaire, les lots
 *     sont comptes et ne partent pas, toute attente est finie. Les renseignements sont ceux du ZBook (Tiger Lake
 *     GT1, 8086:9A60, 32 EU) : Mesa et le compilateur d'iris vont au bout sous QEMU, sans GPU ;
 *   - le GPU : le service intelgpu (etape 2, pas encore ecrit). Sans lui : armpi_drm_open rend -1 (pas de GPU),
 *     comme le Pi sans v3d.
 * Les reponses du mode a blanc sont celles que le service devra donner (GPU-INTEL.md, tableau des ioctls).
 * La meme interface armpi_drm_* que tools/mesa/libdrm (armgl.c) ; les fenetres ne sont pas encore faites.
 */
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <armpi/syscall.h>
#include "xf86drm.h"
#include "i915_drm.h"
#include "armpi_drm.h"

#define SYS_SVC_OPEN	0x31
#define FAKE_FD		0x7F000
#define PAGE		4096UL

/* le ZBook : Tiger Lake-H GT1 */
#define TGL_ID		0x9A60
#define TGL_REV		1
#define TGL_EU		32
#define TGL_DSS_MASK	0x3
#define TGL_TS_HZ	19200000

struct bo {
	void *va;
	uint64_t bytes;
	int used, imported;
};

static struct bo *bos;
static uint32_t nbos;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int dry, debug, drm_fd = -1;
static long jobs;
static uint32_t next_id = 1, next_syncobj = 1;

/* -------- les BO ------------------------------------------------------------------------------------ */
static uint32_t new_handle (void)
{
	uint32_t h = 1;
	while (h < nbos && bos[h].used)
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
	return h && h < nbos && bos[h].used ? &bos[h] : NULL;
}

static int bo_create (uint64_t size, uint32_t *handle)
{
	uint64_t bytes = (size + PAGE - 1) & ~(PAGE - 1);
	void *p = NULL;
	if (!bytes || posix_memalign (&p, PAGE, bytes))
		return -ENOMEM;
	memset (p, 0, bytes);			/* Linux rend un BO a zero */
	uint32_t h = new_handle ();
	bos[h].va = p;
	bos[h].bytes = bytes;
	bos[h].used = 1;
	bos[h].imported = 0;
	*handle = h;
	return 0;
}

static int bo_close (uint32_t h)
{
	struct bo *b = lookup (h);
	if (!b)
		return -ENOENT;
	if (!b->imported)
		free (b->va);
	memset (b, 0, sizeof *b);
	return 0;
}

/* -------- renseignements ---------------------------------------------------------------------------- */
static int get_param (struct drm_i915_getparam *g)
{
	int v;
	switch (g->param) {
	case I915_PARAM_CHIPSET_ID: v = TGL_ID; break;
	case I915_PARAM_REVISION: v = TGL_REV; break;
	case I915_PARAM_EU_TOTAL: v = TGL_EU; break;
	case I915_PARAM_SLICE_MASK: v = 1; break;
	case I915_PARAM_SUBSLICE_MASK: v = TGL_DSS_MASK; break;
	case I915_PARAM_CS_TIMESTAMP_FREQUENCY: v = TGL_TS_HZ; break;
	case I915_PARAM_HAS_CONTEXT_ISOLATION: v = 1; break;
	case I915_PARAM_HAS_EXEC_BATCH_FIRST:
	case I915_PARAM_HAS_EXEC_FENCE_ARRAY:
	case I915_PARAM_HAS_EXEC_HANDLE_LUT:
	case I915_PARAM_HAS_EXEC_NO_RELOC:
	case I915_PARAM_HAS_EXEC_SOFTPIN:
	case I915_PARAM_HAS_EXEC_TIMELINE_FENCES: v = 1; break;
	case I915_PARAM_MMAP_GTT_VERSION: v = 4; break;
	case I915_PARAM_HAS_USERPTR_PROBE: v = 0; break;
	case I915_PARAM_PXP_STATUS: return -ENODEV;	/* pas de contenu protege */
	default: return -EINVAL;
	}
	*g->value = v;
	return 0;
}

/* une requete DRM_I915_QUERY : longueur d'abord (length = 0), puis les donnees */
static int32_t query_item (struct drm_i915_query_item *it)
{
	uint8_t buf[512];
	int32_t n = 0;
	memset (buf, 0, sizeof buf);
	switch (it->query_id) {
	case DRM_I915_QUERY_TOPOLOGY_INFO: {
		/* une tranche, 6 sous-tranches doubles possibles (Gen12 LP), 16 EU chacune ; deux allumees */
		struct drm_i915_query_topology_info *t = (void *) buf;
		t->max_slices = 1;
		t->max_subslices = 6;
		t->max_eus_per_subslice = 16;
		t->subslice_offset = 1;
		t->subslice_stride = 1;
		t->eu_offset = 2;
		t->eu_stride = 2;
		t->data[0] = 1;
		t->data[1] = TGL_DSS_MASK;
		for (int ss = 0; ss < 6; ss++)
			if (TGL_DSS_MASK & (1 << ss))
				t->data[2 + 2 * ss] = t->data[3 + 2 * ss] = 0xFF;
		n = (int32_t) (sizeof *t + 2 + 6 * 2);
		break;
	}
	case DRM_I915_QUERY_ENGINE_INFO: {
		/* rendu, copie, video, amelioration video (Tiger Lake GT1 : un de chaque pour iris) */
		static const uint16_t cls[4] = { I915_ENGINE_CLASS_RENDER, I915_ENGINE_CLASS_COPY,
						 I915_ENGINE_CLASS_VIDEO, I915_ENGINE_CLASS_VIDEO_ENHANCE };
		struct drm_i915_query_engine_info *e = (void *) buf;
		e->num_engines = 4;
		for (int i = 0; i < 4; i++) {
			e->engines[i].engine.engine_class = cls[i];
			e->engines[i].engine.engine_instance = 0;
			e->engines[i].logical_instance = 0;
		}
		n = (int32_t) (sizeof *e + 4 * sizeof e->engines[0]);
		break;
	}
	case DRM_I915_QUERY_MEMORY_REGIONS: {
		/* la memoire du systeme seulement (GPU integre) */
		struct drm_i915_query_memory_regions *m = (void *) buf;
		uint64_t total = (uint64_t) sysconf (_SC_PHYS_PAGES) * (uint64_t) sysconf (_SC_PAGESIZE);
		m->num_regions = 1;
		m->regions[0].region.memory_class = I915_MEMORY_CLASS_SYSTEM;
		m->regions[0].region.memory_instance = 0;
		m->regions[0].probed_size = total;
		m->regions[0].unallocated_size = total / 2;
		n = (int32_t) (sizeof *m + sizeof m->regions[0]);
		break;
	}
	default:			/* HWCONFIG, GEOMETRY_SUBSLICES (Gen12.5+), GuC, perf : absents */
		return -EINVAL;
	}
	if (it->length == 0)
		return n;
	if (it->length < n)
		return -EINVAL;
	memcpy ((void *) (uintptr_t) it->data_ptr, buf, (size_t) n);
	return n;
}

static int query (struct drm_i915_query *q)
{
	struct drm_i915_query_item *items = (void *) (uintptr_t) q->items_ptr;
	for (uint32_t i = 0; i < q->num_items; i++)
		items[i].length = query_item (&items[i]);
	return 0;
}

static int ctx_getparam (struct drm_i915_gem_context_param *p)
{
	switch (p->param) {
	case I915_CONTEXT_PARAM_GTT_SIZE: p->value = 1ULL << 48; return 0;	/* PPGTT a 4 niveaux */
	case I915_CONTEXT_PARAM_PRIORITY: p->value = 0; return 0;
	case I915_CONTEXT_PARAM_RECOVERABLE: p->value = 1; return 0;
	case I915_CONTEXT_PARAM_VM: p->value = 1; return 0;
	default: return -EINVAL;
	}
}

static uint64_t now_ns (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (uint64_t) t.tv_sec * 1000000000ULL + (uint64_t) t.tv_nsec;
}

/* -------- les ioctl --------------------------------------------------------------------------------- */
static int ioctl_locked (unsigned long request, void *arg)
{
	switch (request) {
	/* renseignements */
	case DRM_IOCTL_I915_GETPARAM: return get_param (arg);
	case DRM_IOCTL_I915_QUERY: return query (arg);
	case DRM_IOCTL_I915_REG_READ: {			/* l'horodatage du rendu */
		struct drm_i915_reg_read *r = arg;
		r->val = now_ns () * (TGL_TS_HZ / 1000000) / 1000;
		return 0;
	}
	case DRM_IOCTL_I915_GET_RESET_STATS: {
		struct drm_i915_reset_stats *r = arg;
		r->reset_count = r->batch_active = r->batch_pending = 0;
		return 0;
	}
	case DRM_IOCTL_I915_GEM_GET_APERTURE: {
		struct drm_i915_gem_get_aperture *a = arg;
		a->aper_size = a->aper_available_size = 256ULL << 20;
		return 0;
	}
	/* contextes, espaces d'adresses */
	case DRM_IOCTL_I915_GEM_CONTEXT_CREATE:
	case DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT:
		((struct drm_i915_gem_context_create_ext *) arg)->ctx_id = next_id++;
		return 0;
	case DRM_IOCTL_I915_GEM_CONTEXT_DESTROY: return 0;
	case DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM: return ctx_getparam (arg);
	case DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM: return 0;
	case DRM_IOCTL_I915_GEM_VM_CREATE:
		((struct drm_i915_gem_vm_control *) arg)->vm_id = next_id++;
		return 0;
	case DRM_IOCTL_I915_GEM_VM_DESTROY: return 0;
	/* BO */
	case DRM_IOCTL_I915_GEM_CREATE: {
		struct drm_i915_gem_create *c = arg;
		return bo_create (c->size, &c->handle);
	}
	case DRM_IOCTL_I915_GEM_CREATE_EXT: {		/* extensions (regions, PAT) : la memoire du systeme */
		struct drm_i915_gem_create_ext *c = arg;
		return bo_create (c->size, &c->handle);
	}
	case DRM_IOCTL_GEM_CLOSE: return bo_close (((struct drm_gem_close *) arg)->handle);
	case DRM_IOCTL_I915_GEM_MMAP_OFFSET: {		/* la vue vient de korin_i915_bo_map */
		struct drm_i915_gem_mmap_offset *m = arg;
		if (!lookup (m->handle))
			return -ENOENT;
		m->offset = (uint64_t) m->handle << 32;
		return 0;
	}
	case DRM_IOCTL_I915_GEM_MMAP: {
		struct drm_i915_gem_mmap *m = arg;
		struct bo *b = lookup (m->handle);
		if (!b)
			return -ENOENT;
		m->addr_ptr = (uint64_t) (uintptr_t) b->va + m->offset;
		return 0;
	}
	case DRM_IOCTL_I915_GEM_SET_DOMAIN:
	case DRM_IOCTL_I915_GEM_SET_CACHING:
	case DRM_IOCTL_I915_GEM_SET_TILING: return 0;
	case DRM_IOCTL_I915_GEM_GET_TILING: {
		struct drm_i915_gem_get_tiling *t = arg;
		t->tiling_mode = I915_TILING_NONE;
		t->swizzle_mode = t->phys_swizzle_mode = I915_BIT_6_SWIZZLE_NONE;
		return 0;
	}
	case DRM_IOCTL_I915_GEM_MADVISE: ((struct drm_i915_gem_madvise *) arg)->retained = 1; return 0;
	case DRM_IOCTL_I915_GEM_BUSY: ((struct drm_i915_gem_busy *) arg)->busy = 0; return 0;
	case DRM_IOCTL_I915_GEM_WAIT: return 0;		/* a blanc : tout est fini */
	case DRM_IOCTL_I915_GEM_USERPTR: return -ENODEV;
	case DRM_IOCTL_GEM_FLINK: {			/* le nom d'un BO est son numero (comme le Pi) */
		struct drm_gem_flink *f = arg;
		if (!lookup (f->handle))
			return -ENOENT;
		f->name = f->handle;
		return 0;
	}
	case DRM_IOCTL_GEM_OPEN: {
		struct drm_gem_open *o = arg;
		struct bo *b = lookup (o->name);
		if (!b)
			return -ENOENT;
		o->handle = o->name;
		o->size = b->bytes;
		return 0;
	}
	/* soumission */
	case DRM_IOCTL_I915_GEM_EXECBUFFER2:
	case DRM_IOCTL_I915_GEM_EXECBUFFER2_WR:
		jobs++;
		return dry ? 0 : -EIO;
	/* synchronisation : a blanc, tout est signale */
	case DRM_IOCTL_SYNCOBJ_CREATE:
		((struct drm_syncobj_create *) arg)->handle = next_syncobj++;
		return 0;
	case DRM_IOCTL_SYNCOBJ_WAIT:
		((struct drm_syncobj_wait *) arg)->first_signaled = 0;
		return 0;
	case DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT:
		((struct drm_syncobj_timeline_wait *) arg)->first_signaled = 0;
		return 0;
	case DRM_IOCTL_SYNCOBJ_QUERY: {
		struct drm_syncobj_timeline_array *a = arg;
		uint64_t *pts = (void *) (uintptr_t) a->points;
		for (uint32_t i = 0; i < a->count_handles; i++)
			pts[i] = UINT64_MAX >> 1;
		return 0;
	}
	case DRM_IOCTL_SYNCOBJ_DESTROY:
	case DRM_IOCTL_SYNCOBJ_RESET:
	case DRM_IOCTL_SYNCOBJ_SIGNAL:
	case DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL:
	case DRM_IOCTL_SYNCOBJ_TRANSFER: return 0;
	default:			/* perf, PRIME, fichiers de synchronisation : absents */
		return -ENODEV;
	}
}

int drmIoctl (int fd, unsigned long request, void *arg)
{
	(void) fd;
	pthread_mutex_lock (&lock);
	int r = ioctl_locked (request, arg);
	pthread_mutex_unlock (&lock);
	if (debug && r < 0)	/* KORIN_DRM_DEBUG : les refus */
		fprintf (stderr, "korin_i915 : ioctl %08lx refusé (%d)\n", request, r);
	if (r < 0) {
		errno = -r;
		return -1;
	}
	return 0;
}

void *korin_i915_bo_map (uint32_t handle)
{
	pthread_mutex_lock (&lock);
	struct bo *b = lookup (handle);
	void *p = b ? b->va : MAP_FAILED;
	pthread_mutex_unlock (&lock);
	return p;
}

/* -------- le reste de libdrm ------------------------------------------------------------------------ */
static char drv_name[] = "i915", drv_date[] = "20261113", drv_desc[] = "Korin intelgpu";

drmVersionPtr drmGetVersion (int fd)
{
	(void) fd;
	drmVersionPtr v = calloc (1, sizeof *v);
	if (!v)
		return NULL;
	v->version_major = 1;
	v->version_minor = 6;
	v->name = drv_name;
	v->name_len = (int) strlen (drv_name);
	v->date = drv_date;
	v->date_len = (int) strlen (drv_date);
	v->desc = drv_desc;
	v->desc_len = (int) strlen (drv_desc);
	return v;
}

void drmFreeVersion (drmVersionPtr v)
{
	free (v);
}

/* la fonction PCIe 0000:00:02.0 (celle que pcid donne au service) */
int drmGetDevice2 (int fd, uint32_t flags, drmDevicePtr *device)
{
	(void) fd; (void) flags;
	struct {
		drmDevice d;
		drmPciBusInfo bus;
		drmPciDeviceInfo dev;
	} *p = calloc (1, sizeof *p);
	if (!p)
		return -ENOMEM;
	p->d.bustype = DRM_BUS_PCI;
	p->d.businfo.pci = &p->bus;
	p->d.deviceinfo.pci = &p->dev;
	p->bus.dev = 2;
	p->dev.vendor_id = 0x8086;
	p->dev.device_id = TGL_ID;
	p->dev.revision_id = TGL_REV;
	*device = &p->d;
	return 0;
}

int drmGetDevice (int fd, drmDevicePtr *device)
{
	return drmGetDevice2 (fd, 0, device);
}

void drmFreeDevice (drmDevicePtr *device)
{
	if (device) {
		free (*device);
		*device = NULL;
	}
}

int drmGetDevices2 (uint32_t flags, drmDevicePtr devices[], int max_devices)
{
	if (!devices || max_devices < 1)
		return 1;
	return drmGetDevice2 (0, flags, &devices[0]) ? 0 : 1;
}

void drmFreeDevices (drmDevicePtr devices[], int count)
{
	for (int i = 0; i < count; i++)
		drmFreeDevice (&devices[i]);
}

int drmGetCap (int fd, uint64_t c, uint64_t *v) { (void) fd; (void) c; *v = 0; return -EINVAL; }
int drmPrimeFDToHandle (int fd, int p, uint32_t *h) { (void) fd; (void) p; (void) h; return -ENOSYS; }
int drmPrimeHandleToFD (int fd, uint32_t h, uint32_t f, int *p) { (void) fd; (void) h; (void) f; (void) p; return -ENOSYS; }
int drmSyncobjCreate (int fd, uint32_t flags, uint32_t *handle)
{
	(void) fd; (void) flags;
	*handle = __atomic_fetch_add (&next_syncobj, 1, __ATOMIC_RELAXED);
	return 0;
}
int drmSyncobjDestroy (int fd, uint32_t h) { (void) fd; (void) h; return 0; }
int drmSyncobjWait (int fd, uint32_t *h, unsigned n, int64_t t, unsigned f, uint32_t *first)
{
	(void) fd; (void) h; (void) n; (void) t; (void) f;
	if (first)
		*first = 0;
	return 0;
}
int drmSyncobjImportSyncFile (int fd, uint32_t h, int s) { (void) fd; (void) h; (void) s; return 0; }
int drmSyncobjExportSyncFile (int fd, uint32_t h, int *s) { (void) fd; (void) h; *s = FAKE_FD; return 0; }
int drmSyncobjFDToHandle (int fd, int o, uint32_t *h) { (void) fd; (void) o; (void) h; return -ENOSYS; }
int drmSyncobjHandleToFD (int fd, uint32_t h, int *o) { (void) fd; (void) h; (void) o; return -ENOSYS; }
int drmSyncobjReset (int fd, const uint32_t *h, uint32_t n) { (void) fd; (void) h; (void) n; return 0; }
int drmSyncobjSignal (int fd, const uint32_t *h, uint32_t n) { (void) fd; (void) h; (void) n; return 0; }
int drmSyncobjTimelineSignal (int fd, const uint32_t *h, uint64_t *p, uint32_t n)
{
	(void) fd; (void) h; (void) p; (void) n;
	return 0;
}
int drmSyncobjTimelineWait (int fd, uint32_t *h, uint64_t *p, unsigned n, int64_t t, unsigned f, uint32_t *first)
{
	(void) fd; (void) h; (void) p; (void) n; (void) t; (void) f;
	if (first)
		*first = 0;
	return 0;
}
int drmSyncobjQuery2 (int fd, uint32_t *h, uint64_t *p, uint32_t n, uint32_t f)
{
	(void) fd; (void) h; (void) f;
	for (uint32_t i = 0; i < n; i++)
		p[i] = UINT64_MAX >> 1;
	return 0;
}
int drmSyncobjTransfer (int fd, uint32_t d, uint64_t dp, uint32_t s, uint64_t sp, uint32_t f)
{
	(void) fd; (void) d; (void) dp; (void) s; (void) sp; (void) f;
	return 0;
}

/* -------- l'interface d'armgl (comme tools/mesa/libdrm/armpi_drm.c) --------------------------------- */
/* iris veut un vrai descripteur (fstat, puis une copie par F_DUPFD_CLOEXEC) : un tube, que personne ne lit */
static int open_fd (void)
{
	int p[2];
	if (drm_fd < 0 && pipe (p) == 0)
		drm_fd = p[0];
	return drm_fd;
}

int armpi_drm_open (int mode)
{
	mode &= ~ARMPI_DRM_V71;
	debug = getenv ("KORIN_DRM_DEBUG") != NULL;
	if (mode) {
		dry = mode;
		return open_fd ();
	}
	/* le service du GPU (etape 2) : absent pour l'instant, donc pas de GPU */
	long chan = armpi_syscall (SYS_SVC_OPEN, (long) "intelgpu", 0, 0, 0, 0, 0);
	if (chan < 0)
		return -1;
	return -1;
}

int armpi_drm_lost (void) { return 0; }
long armpi_drm_jobs (void) { return jobs; }
unsigned armpi_drm_first_phys (void) { return 0; }

unsigned armpi_drm_import (void *va, unsigned phys, unsigned bytes)
{
	(void) phys;
	pthread_mutex_lock (&lock);
	uint32_t h = new_handle ();
	bos[h].va = va;
	bos[h].bytes = bytes;
	bos[h].used = 1;
	bos[h].imported = 1;
	pthread_mutex_unlock (&lock);
	return h;
}

void *armpi_drm_buffer (unsigned long bytes, unsigned *name)
{
	void *p = NULL;
	if (posix_memalign (&p, PAGE, (bytes + PAGE - 1) & ~(PAGE - 1)))
		return NULL;
	memset (p, 0, bytes);
	*name = armpi_drm_import (p, 0, (unsigned) bytes);
	return p;
}

/* le GPU d'Intel et le processeur partagent le dernier niveau de cache (LLC) : rien a vider pour le GPU ;
 * pour Elegant, les ecritures du processeur suffisent */
void armpi_drm_flush (void *va, unsigned long bytes)
{
	(void) va; (void) bytes;
}

/* les fenetres : etape 2 (le service, puis la presentation par Elegant ou par le copieur du GPU) */
void *armpi_drm_window (long id, int x, int y, int width, int height, unsigned names[2], unsigned *pitch)
{
	(void) id; (void) x; (void) y; (void) width; (void) height; (void) names; (void) pitch;
	return NULL;
}
void armpi_drm_window_free (void *window) { (void) window; }
void armpi_drm_zone (void *window, long id, int x, int y, int width, int height)
{
	(void) window; (void) id; (void) x; (void) y; (void) width; (void) height;
}
int armpi_drm_present (void *window, int image) { (void) window; (void) image; return -1; }
void armpi_drm_times (void *window, double *cache, double *attach)
{
	(void) window;
	*cache = *attach = 0;
}
void armpi_drm_times_reset (void *window) { (void) window; }

/* intel_measure (INTEL_MEASURE=...) cree un tube nomme ; libposix n'a pas encore mkfifoat : refuse, faible pour
 * ceder la place a celui de libposix le jour ou il y sera */
#include <sys/stat.h>
__attribute__ ((weak)) int mkfifoat (int dirfd, const char *path, mode_t mode)
{
	(void) dirfd; (void) path; (void) mode;
	errno = ENOSYS;
	return -1;
}
