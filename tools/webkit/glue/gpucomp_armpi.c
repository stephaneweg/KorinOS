/*
 * gpucomp_armpi.c -- le GPU de kapi d'Onyx (gpu_info, gpu_texture, gpu_texture_rect, gpu_render,
 * gpu_vbuf : paliers v53-v72) sur GpuKit (SD0:/lib/gpukit.so, docs/V3D.md), pour le gpucomp d'Onyx
 * (src/extra/onyx-libs/gpucomp/gpucomp.c, compile tel quel avec tools/webkit/glue/include) ; plus
 * l'horloge et les threads de GPC_F_ASYNC (pas de threads ici : gpucomp compose sur le thread
 * appelant, comme sur un noyau d'Onyx plus ancien que v67 ; WebKit ne le demande pas).
 *
 * Les structures sont celles de GpuKit (src/kits/gpukit/gpukit.inc) octet pour octet. GpuKit
 * prend moins par image que le noyau d'Onyx (1024 lots, 49152 sommets contre 4096 et 196608) :
 * une image plus grande est coupee en images successives, les suivantes par-dessus
 * (KAPI_GPU_F_KEEP), dans l'ordre des lots -- le meme resultat.
 */
#include <stddef.h>
#include <string.h>
#include <time.h>
#include <armpi/kit.h>
#include "appkit/appkit.h"

/* rangs de GpuKit (src/kits/gpukit/gpukit.abi) */
enum { GK_INFO = 0, GK_ALLOC = 1, GK_TEXTURE = 6, GK_TEXTURE_RECT = 7, GK_RENDER = 8, GK_COUNT = 10 };
#define GK_MAX_BATCHES	1024
#define GK_MAX_VERTS	(3 * 16384)
#define GKI_GPU		0			/* gk_info (mots de 64 bits) */
#define GKI_STATE	1
#define GKI_VERSION	2

static void *gk;
static int gk_tried;

static void *fn (int rank)
{
	if (!gk_tried) {
		gk_tried = 1;
		gk = armpi_kit_open ("gpukit", GK_COUNT);
	}
	return gk ? armpi_kit_fn (gk, rank) : NULL;
}

int kapi_abi_version (void) { return 72; }

unsigned kapi_clock_us (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned) (ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
}

static void put (char *buf, unsigned cap, const char *s)
{
	unsigned i = 0;
	if (cap == 0) return;
	for (; s[i] && i + 1 < cap; i++) buf[i] = s[i];
	buf[i] = 0;
}

int kapi_gpu_info (char *buf, unsigned cap)
{
	long (*info) (long *) = (long (*) (long *)) fn (GK_INFO);
	long r[8] = { 0 };
	if (info == NULL) { put (buf, cap, "GpuKit absent"); return 0; }
	if (info (r) != 1) {
		put (buf, cap, r[GKI_STATE] < 0 ? "V3D stopped after a hang" : "no v3d service: QEMU or no_gpu");
		return 0;
	}
	put (buf, cap, "V3D 4.2 (GpuKit, service v3d)");
	return 1;
}

static int last_texture = -1;

int kapi_gpu_texture (int handle, const unsigned *pixels, int w, int h, int stride)
{
	int (*f) (int, const unsigned *, int, int, int) = (int (*) (int, const unsigned *, int, int, int)) fn (GK_TEXTURE);
	int r = f ? f (handle, pixels, w, h, stride) : -1;
	if (r >= 0)
		last_texture = r;
	return r;
}

/* la derniere texture faite (gpc_tex_create d'une seule tuile) : son numero pour GpuKit */
int armpi_gpu_last_texture (void)
{
	return last_texture;
}

/* physique, octets, rangement, largeur, hauteur, adresse (gk_texture_info, rang 11) -> 0 */
int armpi_gpu_texture_info (int handle, long *out)
{
	long (*f) (int, long *) = (long (*) (int, long *)) fn (11);
	return f ? (int) f (handle, out) : -1;
}

int kapi_gpu_texture_rect (int handle, int x, int y, int w, int h, const unsigned *pixels, int stride)
{
	int (*f) (int, int, int, int, int, const unsigned *, int) =
		(int (*) (int, int, int, int, int, const unsigned *, int)) fn (GK_TEXTURE_RECT);
	return f ? f (handle, x, y, w, h, pixels, stride) : -1;
}

void *kapi_gpu_vbuf (unsigned bytes)
{
	void *(*f) (unsigned long) = (void *(*) (unsigned long)) fn (GK_ALLOC);
	return f ? f (bytes) : NULL;
}

int kapi_gpu_render (const struct kapi_gpu_frame *F, const struct kapi_gpu_vertex3 *v, unsigned nv,
		     const struct kapi_gpu_batch *b, unsigned nb)
{
	int (*render) (const struct kapi_gpu_frame *, const struct kapi_gpu_vertex3 *, unsigned,
		       const struct kapi_gpu_batch *, unsigned) =
		(int (*) (const struct kapi_gpu_frame *, const struct kapi_gpu_vertex3 *, unsigned,
			  const struct kapi_gpu_batch *, unsigned)) fn (GK_RENDER);
	static struct kapi_gpu_batch part[GK_MAX_BATCHES];
	if (render == NULL) return -1;
	if (nb <= GK_MAX_BATCHES && nv <= GK_MAX_VERTS)
		return render (F, v, nv, b, nb);
	/* coupee : des lots consecutifs dont les sommets tiennent ensemble, renumerotes */
	struct kapi_gpu_frame f = *F;
	unsigned i = 0;
	do {
		unsigned lo = ~0u, hi = 0, n = 0;
		for (; i < nb && n < GK_MAX_BATCHES; i++) {
			if (b[i].count == 0) continue;
			unsigned l = b[i].first < lo ? b[i].first : lo;
			unsigned h = b[i].first + b[i].count > hi ? b[i].first + b[i].count : hi;
			if (b[i].count > GK_MAX_VERTS || b[i].first + b[i].count > nv) return -2;
			if (h - l > GK_MAX_VERTS) break;
			lo = l; hi = h;
			part[n++] = b[i];
		}
		if (n == 0) lo = hi = 0;
		for (unsigned k = 0; k < n; k++) part[k].first -= lo;
		int r = render (&f, v + lo, hi - lo, part, n);
		if (r) return r;
		f.flags |= KAPI_GPU_F_KEEP;
	} while (i < nb);
	return 0;
}

/* GPC_F_ASYNC : pas de threads (gpucomp compose sur le thread appelant) */
int kapi_event_create (int manual_reset, int initial) { (void) manual_reset; (void) initial; return -1; }
int kapi_event_set (int h) { (void) h; return -1; }
int kapi_event_wait (int h, unsigned timeout_ms) { (void) h; (void) timeout_ms; return -1; }
int kapi_thread_create (int (*f) (void *), void *arg, unsigned stack_size, const char *name)
{ (void) f; (void) arg; (void) stack_size; (void) name; return -1; }
int kapi_thread_join (int tid, unsigned timeout_ms, int *code) { (void) tid; (void) timeout_ms; if (code) *code = 0; return -1; }
int kapi_sync_close (int h) { (void) h; return -1; }
