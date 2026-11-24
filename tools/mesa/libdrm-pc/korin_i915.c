/*
 * korin_i915.c -- le faux libdrm du PC de Korin, c'est-a-dire le winsys d'iris (docs/etude/GPU-INTEL.md, § 3 ;
 * PC-PILOTES.md « GPU Intel, etape 2 »).
 *
 * Linux fait pour iris (pilote i915 du noyau) : BO, contextes, espaces d'adresses, soumission des lots,
 * synchronisation, et les renseignements sur la puce (GETPARAM, QUERY). Ici, deux modes :
 *   - le GPU : le service intelgpu (src/svc/intelgpu, protocole igproto.h). Les BO sont des tranches de
 *     « morceaux » : des tampons partages (BUF_CREATE, contigus, en ecriture differee : le LLC est commun au
 *     GPU et aux coeurs) donnes au service (BUF_GRANT, IG_OP_CHUNK). Iris choisit l'adresse GPU de chaque BO
 *     (softpin) : a la premiere soumission qui le cite, IG_OP_BIND ecrit ses PTE dans la PPGTT du process. Un
 *     lot : IG_OP_EXEC rend son numero ; la page d'etat partagee du service (IGS_DONE) dit sans appel s'il est
 *     fini, IG_OP_WAIT l'attend. Un syncobj garde le numero du dernier lot qui le signale ;
 *   - a blanc (ARMPI_DRM_DRY) : tout est servi dans le process, les lots sont comptes et ne partent pas.
 * Sans service : armpi_drm_open rend -1 (pas de GPU), comme le Pi sans v3d.
 * Le copieur (BCS) : iris n'y met que l'initialisation de son lot (et des copies PRIME, jamais ici) ; le
 * service ne sert que le rendu : ces lots sont tenus pour finis sans partir.
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
#include <armpi/syscall.h>
#include "xf86drm.h"
#include "i915_drm.h"
#include "armpi_drm.h"
#include "igproto.h"

#define SYS_BUF_CREATE	0x01
#define SYS_BUF_GRANT	0x02
#define SYS_BUF_MAP	0x03
#define SYS_BUF_FREE	0x04
#define SYS_BUF_INFO	0x0D
#define SYS_SVC_OPEN	0x31
#define SYS_SVC_CALL	0x32
#define BUF_VA_BASE	0x6800000000UL
#define BUF_SLOT	0x4000000UL	/* 64 Mo : le plus grand tampon */
#define E_TIMEDOUT	(-110)
#define FAKE_FD		0x7F000
#define PAGE		4096UL
#define CHUNK		(32UL << 20)
#define MAX_CHUNKS	64
#define VA_MASK		((1ULL << 48) - 1)

/* le ZBook : Tiger Lake-H GT1 (valeurs du mode a blanc ; le service donne les vraies) */
#define TGL_ID		0x9A60
#define TGL_REV		1
#define TGL_EU		32
#define TGL_DSS_MASK	0x3
#define TGL_TS_HZ	19200000

struct block {
	struct block *next, *prev;
	uint64_t off, bytes;
	int used;
};
struct chunk {
	long buf;			/* numero du (premier) tampon partage, -1 : a blanc */
	uint8_t *va;
	uint64_t bytes;			/* (plus de BUF_SLOT : des tampons de numeros consecutifs, buf, buf + 1...) */
	struct block *blocks;
};
struct bo {
	int used, imported;
	void *va;
	uint64_t bytes;
	struct chunk *c;
	struct block *b;
	uint64_t gpu;			/* adresse liee dans la PPGTT (0 : pas encore) */
	uint32_t vm, last_seq;
};
#define SLOTS 8
struct gctx {
	int used, nslots;
	uint16_t cls[SLOTS];		/* classe du moteur par rang (contexte a moteurs) ; 0 : sans */
	long hw[SLOTS];			/* contexte du service par rang (-1 : pas encore) */
	uint32_t vm;
};

static struct chunk chunks[MAX_CHUNKS];
static int nchunks;
static struct bo *bos;
static uint32_t nbos;
static struct gctx *ctxs;
static uint32_t nctxs;
static uint32_t *syncs;			/* numero de lot par syncobj (0 : signale) */
static uint32_t nsyncs;
/* un syncobj chronologique : ses points en attente (point, lot), du plus ancien au plus recent, et le plus
 * grand point atteint. Les lots finissent dans l'ordre : un point est atteint quand son lot est fini. Blender
 * ne libere ses ressources qu'a la valeur lue (vkGetSemaphoreCounterValue) : elle doit avancer pendant que des
 * lots plus recents tournent encore (24/11 : avant, 0 tant que le dernier lot n'etait pas fini -> la memoire
 * du GPU epuisee en quelques secondes de dessin continu). */
#define TL_PENDING 32
struct tl {
	uint64_t reached;
	uint32_t n;
	struct { uint64_t pt; uint32_t seq; } p[TL_PENDING];
};
static struct tl *tls;			/* (a cote de syncs) */
static void sync_signal (uint32_t h, uint64_t pt, uint32_t seq);
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int dry, soft, debug, drm_fd = -1, lost;
static long jobs, chan = -1;
static struct ig_info info;
static volatile uint32_t *status;
static uint32_t vm_default, next_vm_fake = 1;

static long sys2 (long n, long a, long b) { return armpi_syscall (n, a, b, 0, 0, 0, 0); }

/* un appel au service : m[0] op, a b c d -> resultat (0, E_*) ; les valeurs rendues restent dans m */
static long call (uint64_t *m)
{
	long r = sys2 (SYS_SVC_CALL, chan, (long) m);
	if (r < 0)
		return r;
	return (long) m[0];
}

static long call4 (uint64_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d, uint64_t out[4])
{
	uint64_t m[64];
	memset (m, 0, 48);
	m[0] = op; m[1] = a; m[2] = b; m[3] = c; m[4] = d;
	long r = call (m);
	if (out)
		memcpy (out, m + 1, 32);
	return r;
}

static int done (uint32_t seq)
{
	return !seq || !status || (int32_t) (status[IGS_DONE] - seq) >= 0;
}

static uint64_t now_ns (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (uint64_t) t.tv_sec * 1000000000ULL + (uint64_t) t.tv_nsec;
}

/* attend le lot seq (appele verrou pris : le rend le temps de l'attente) ; timeout en ns depuis maintenant,
 * < 0 : sans fin -> 0, -ETIME */
static int wait_seq (uint32_t seq, int64_t timeout)
{
	if (done (seq) || dry)
		return 0;
	if (timeout == 0)
		return -ETIME;
	uint64_t end = timeout < 0 ? UINT64_MAX : now_ns () + (uint64_t) timeout;
	int r = 0;
	pthread_mutex_unlock (&lock);
	while (!done (seq)) {
		uint64_t t = now_ns ();
		if (t >= end) {
			r = -ETIME;
			break;
		}
		uint64_t ms = end == UINT64_MAX ? 10000 : (end - t) / 1000000 + 1;
		if (ms > 10000)
			ms = 10000;
		uint64_t out[4];
		long res = call4 (IG_OP_WAIT, seq, ms, 0, 0, out);
		if (res == 0) {
			if (out[2])
				lost = 1;
			break;
		}
		if (res != E_TIMEDOUT) {	/* le service est parti : plus de GPU */
			lost = 1;
			r = -EIO;
			break;
		}
	}
	pthread_mutex_lock (&lock);
	return r;
}

/* -------- les morceaux ------------------------------------------------------------------------------ */
/* plus de 64 Mo (BUF_SLOT, le plus grand tampon du noyau ; 24/11 : Blender en demande plus) : des tampons de
 * numeros consecutifs, tous pleins sauf le dernier, que le noyau mappe bout a bout (BUF_VA_BASE + n x BUF_SLOT).
 * Le noyau donne le plus petit numero libre : on en prend jusqu'a avoir une suite, puis on rend les autres.
 * -> le premier numero, < 0 */
static long bufs_run (uint64_t bytes)
{
	long k = (long) ((bytes + BUF_SLOT - 1) / BUF_SLOT), got[48], first = -1, run = 0;
	uint64_t last = bytes - (uint64_t) (k - 1) * BUF_SLOT;
	int ng = 0;
	while (run < k && ng < 48) {
		uint64_t sz = run == k - 1 ? last : BUF_SLOT;
		long n = sys2 (SYS_BUF_CREATE, (long) sz, 0);
		if (n < 0)
			break;
		got[ng++] = n;
		if (run && n != first + run) {	/* suite cassee : un tampon plein en commence une autre */
			first = sz == BUF_SLOT ? n : -1;
			run = sz == BUF_SLOT;
			continue;
		}
		if (!run)
			first = n;
		run++;
	}
	for (int i = 0; i < ng; i++)
		if (run < k || got[i] < first || got[i] >= first + k)
			sys2 (SYS_BUF_FREE, got[i], 0);
	return run == k ? first : -1;
}

static struct chunk *chunk_new (uint64_t bytes)
{
	if (nchunks == MAX_CHUNKS)
		return NULL;
	struct chunk *c = &chunks[nchunks];
	if (dry || soft) {	/* (softpipe : la memoire ordinaire du process) */
		void *p = NULL;
		if (posix_memalign (&p, PAGE, bytes))
			return NULL;
		memset (p, 0, bytes);
		c->buf = -1;
		c->va = p;
	} else {
		long n = bytes > BUF_SLOT ? bufs_run (bytes) : sys2 (SYS_BUF_CREATE, (long) bytes, 0);
		if (n < 0)
			return NULL;
		long k = (long) ((bytes + BUF_SLOT - 1) / BUF_SLOT);
		for (long i = 0; i < k; i++)
			if (sys2 (SYS_BUF_GRANT, n + i, info.pid) < 0 || call4 (IG_OP_CHUNK, n + i, 0, 0, 0, NULL) < 0) {
				for (long j = 0; j < k; j++)
					sys2 (SYS_BUF_FREE, n + j, 0);
				return NULL;
			}
		c->buf = n;
		c->va = (uint8_t *) (BUF_VA_BASE + (unsigned long) n * BUF_SLOT);
		if (debug)
			fprintf (stderr, "korin_i915 : morceau %ld de %lu Ko\n", n, (unsigned long) (bytes >> 10));
	}
	c->bytes = bytes;
	struct block *b = calloc (1, sizeof *b);
	b->bytes = bytes;
	c->blocks = b;
	nchunks++;
	return c;
}

static struct block *chunk_alloc (struct chunk *c, uint64_t bytes)
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
	if (!bytes || bytes > 16 * BUF_SLOT)	/* (1 Go : plus de 64 Mo, voir bufs_run) */
		return -ENOMEM;
	struct block *b = NULL;
	struct chunk *c = NULL;
	for (int i = 0; i < nchunks && !b; i++) {
		c = &chunks[i];
		b = chunk_alloc (c, bytes);
	}
	if (!b) {
		/* un tampon contigu : plus petit si la memoire est morcelee */
		uint64_t want = bytes > CHUNK ? bytes : CHUNK;
		while (!(c = chunk_new (want))) {
			if (want / 2 < bytes || want / 2 < (1U << 20))
				return -ENOMEM;
			want /= 2;
		}
		b = chunk_alloc (c, bytes);
	}
	memset (c->va + b->off, 0, bytes);	/* Linux rend un BO a zero */
	uint32_t h = new_handle ();
	struct bo *o = &bos[h];
	memset (o, 0, sizeof *o);
	o->used = 1;
	o->va = c->va + b->off;
	o->bytes = bytes;
	o->c = c;
	o->b = b;
	*handle = h;
	return 0;
}

static int bo_close (uint32_t h)
{
	struct bo *b = lookup (h);
	if (!b)
		return -ENOENT;
	if (!done (b->last_seq))	/* (iris ne ferme qu'un BO au repos ; par prudence) */
		wait_seq (b->last_seq, -1);
	b = lookup (h);
	if (!b)
		return -ENOENT;
	if (b->b)
		chunk_free (b->b);	/* ses PTE restent : elles pointent dans notre morceau, garde jusqu'a la fin */
	memset (b, 0, sizeof *b);
	return 0;
}

/* -------- renseignements ---------------------------------------------------------------------------- */
static int get_param (struct drm_i915_getparam *g)
{
	int v;
	switch (g->param) {
	case I915_PARAM_CHIPSET_ID: v = dry ? TGL_ID : (int) info.device; break;
	case I915_PARAM_REVISION: v = dry ? TGL_REV : (int) info.revision; break;
	case I915_PARAM_EU_TOTAL: v = dry ? TGL_EU : (int) info.eu_total; break;
	case I915_PARAM_SLICE_MASK: v = 1; break;
	case I915_PARAM_SUBSLICE_MASK: v = dry ? TGL_DSS_MASK : (int) info.dss_mask; break;
	case I915_PARAM_CS_TIMESTAMP_FREQUENCY: v = dry ? TGL_TS_HZ : (int) info.ts_hz; break;
	case I915_PARAM_HAS_CONTEXT_ISOLATION: v = 1; break;
	case I915_PARAM_HAS_EXEC_BATCH_FIRST:
	case I915_PARAM_HAS_EXEC_FENCE_ARRAY:
	case I915_PARAM_HAS_EXEC_HANDLE_LUT:
	case I915_PARAM_HAS_EXEC_NO_RELOC:
	case I915_PARAM_HAS_EXEC_SOFTPIN:
	case I915_PARAM_HAS_EXEC_TIMELINE_FENCES:
	/* (anv les demande en plus : l'attente avec delai, execbuf2, les lots sans attente implicite, la capture
	 * d'un BO au journal d'un blocage, sans effet ici) */
	case I915_PARAM_HAS_WAIT_TIMEOUT:
	case I915_PARAM_HAS_EXECBUF2:
	case I915_PARAM_HAS_EXEC_ASYNC:
	case I915_PARAM_HAS_EXEC_CAPTURE: v = 1; break;
	case I915_PARAM_MMAP_GTT_VERSION: v = 4; break;
	case I915_PARAM_HAS_USERPTR_PROBE: v = 0; break;
	case I915_PARAM_PXP_STATUS: return -ENODEV;	/* pas de contenu protege */
	default:
		if (debug)
			fprintf (stderr, "korin_i915 : GETPARAM %d inconnu\n", g->param);
		return -EINVAL;
	}
	*g->value = v;
	return 0;
}

/* une requete DRM_I915_QUERY : longueur d'abord (length = 0), puis les donnees */
static int32_t query_item (struct drm_i915_query_item *it)
{
	uint8_t buf[512];
	int32_t n = 0;
	uint32_t dss = dry ? TGL_DSS_MASK : info.dss_mask;
	uint32_t eus = dry ? 16 : info.eu_per_dss;
	memset (buf, 0, sizeof buf);
	switch (it->query_id) {
	case DRM_I915_QUERY_TOPOLOGY_INFO: {
		/* une tranche, 6 sous-tranches doubles possibles (Gen12 LP), 16 EU chacune */
		struct drm_i915_query_topology_info *t = (void *) buf;
		t->max_slices = 1;
		t->max_subslices = 6;
		t->max_eus_per_subslice = 16;
		t->subslice_offset = 1;
		t->subslice_stride = 1;
		t->eu_offset = 2;
		t->eu_stride = 2;
		t->data[0] = 1;
		t->data[1] = (uint8_t) dss;
		uint16_t eu_mask = (uint16_t) ((1u << eus) - 1);
		for (int ss = 0; ss < 6; ss++)
			if (dss & (1u << ss)) {
				t->data[2 + 2 * ss] = (uint8_t) eu_mask;
				t->data[3 + 2 * ss] = (uint8_t) (eu_mask >> 8);
			}
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

/* -------- contextes ---------------------------------------------------------------------------------- */
static struct gctx *ctx_lookup (uint32_t id)
{
	return id < nctxs && ctxs[id].used ? &ctxs[id] : NULL;
}

/* les espaces d'adresses crees par ce process (vm_default compris). Les numeros du service sont communs a tout
 * le systeme ; iris (Mesa 26.2, i915/iris_batch.c, iris_hw_context_set_vm_id) passe a SETPARAM VM le booleen
 * iris_bufmgr_use_global_vm_id(), soit 1, au lieu de l'espace : sous Linux le premier espace d'un fd vaut 1 et
 * l'erreur ne se voit pas ; ici, tout programme GL lance apres un autre demandait l'espace 1 d'un autre process
 * (15/11 : l'editeur de Godot relance par le gestionnaire de projets, « execbuf keeps returning ENOMEM »). */
#define OWN_VMS 64
static uint32_t own_vms[OWN_VMS];
static int nown_vms;

static void vm_own (uint32_t v)
{
	if (nown_vms < OWN_VMS)
		own_vms[nown_vms++] = v;
}

static void vm_disown (uint32_t v)
{
	for (int i = 0; i < nown_vms; i++)
		if (own_vms[i] == v) {
			own_vms[i] = own_vms[--nown_vms];
			return;
		}
}

static int vm_is_own (uint32_t v)
{
	for (int i = 0; i < nown_vms; i++)
		if (own_vms[i] == v)
			return 1;
	return 0;
}

static void ctx_param (struct gctx *g, struct drm_i915_gem_context_param *p)
{
	if (p->param == I915_CONTEXT_PARAM_ENGINES && p->value && p->size >= 8) {
		struct i915_context_param_engines *e = (void *) (uintptr_t) p->value;
		int n = (int) ((p->size - 8) / sizeof e->engines[0]);
		g->nslots = n > SLOTS ? SLOTS : n;
		for (int i = 0; i < g->nslots; i++)
			g->cls[i] = e->engines[i].engine_class;
	} else if (p->param == I915_CONTEXT_PARAM_VM && p->value) {
		/* (un espace qui n'est pas a nous : celui de ce process, voir vm_own) */
		g->vm = dry || vm_is_own ((uint32_t) p->value) ? (uint32_t) p->value : vm_default;
	}
}

static int ctx_create (struct drm_i915_gem_context_create_ext *a, int ext)
{
	uint32_t id = 1;
	while (id < nctxs && ctxs[id].used)
		id++;
	if (id >= nctxs) {
		uint32_t n = nctxs ? nctxs * 2 : 16;
		ctxs = realloc (ctxs, n * sizeof *ctxs);
		memset (ctxs + nctxs, 0, (n - nctxs) * sizeof *ctxs);
		nctxs = n;
	}
	struct gctx *g = &ctxs[id];
	memset (g, 0, sizeof *g);
	g->used = 1;
	g->vm = vm_default;
	for (int i = 0; i < SLOTS; i++)
		g->hw[i] = -1;
	if (ext && (a->flags & I915_CONTEXT_CREATE_FLAGS_USE_EXTENSIONS)) {
		for (struct i915_user_extension *u = (void *) (uintptr_t) a->extensions; u;
		     u = (void *) (uintptr_t) u->next_extension)
			if (u->name == I915_CONTEXT_CREATE_EXT_SETPARAM)
				ctx_param (g, &((struct drm_i915_gem_context_create_ext_setparam *) u)->param);
	}
	a->ctx_id = id;
	return 0;
}

static void ctx_destroy (uint32_t id)
{
	struct gctx *g = ctx_lookup (id);
	if (!g)
		return;
	for (int i = 0; i < SLOTS; i++)
		if (g->hw[i] >= 0 && !dry)
			call4 (IG_OP_CTX_DESTROY, (uint64_t) g->hw[i], 0, 0, 0, NULL);
	g->used = 0;
}

static int ctx_getparam (struct drm_i915_gem_context_param *p)
{
	switch (p->param) {
	case I915_CONTEXT_PARAM_GTT_SIZE: p->value = 1ULL << 48; return 0;	/* PPGTT a 4 niveaux */
	case I915_CONTEXT_PARAM_PRIORITY: p->value = 0; return 0;
	case I915_CONTEXT_PARAM_RECOVERABLE: p->value = 0; return 0;
	case I915_CONTEXT_PARAM_VM: {
		struct gctx *g = ctx_lookup (p->ctx_id);
		p->value = g ? g->vm : vm_default;
		return 0;
	}
	default:
		if (debug)
			fprintf (stderr, "korin_i915 : CONTEXT_GETPARAM %llu inconnu\n", (unsigned long long) p->param);
		return -EINVAL;
	}
}

/* -------- la soumission ------------------------------------------------------------------------------ */
static int bind_flush (uint32_t vm, struct ig_bind *e, int n)
{
	if (!n)
		return 0;
	uint64_t m[64];
	memset (m, 0, sizeof m);
	m[0] = IG_OP_BIND;
	m[1] = vm;
	m[2] = (uint64_t) n;
	memcpy ((uint8_t *) m + 48, e, n * sizeof *e);
	((uint32_t *) m)[10] = n * sizeof *e;
	return call (m) < 0 ? -EINVAL : 0;
}

static void signal_fences (struct drm_i915_gem_execbuffer2 *x, uint32_t seq)
{
	struct drm_i915_gem_exec_fence *f = NULL;
	uint64_t *v = NULL;		/* les points (syncobj chronologiques ; 0 : binaire) */
	uint32_t n = 0;
	if (x->flags & I915_EXEC_FENCE_ARRAY) {
		f = (void *) (uintptr_t) x->cliprects_ptr;
		n = x->num_cliprects;
	} else if (x->flags & I915_EXEC_USE_EXTENSIONS) {
		for (struct i915_user_extension *u = (void *) (uintptr_t) x->cliprects_ptr; u;
		     u = (void *) (uintptr_t) u->next_extension)
			if (u->name == DRM_I915_GEM_EXECBUFFER_EXT_TIMELINE_FENCES) {
				struct drm_i915_gem_execbuffer_ext_timeline_fences *t = (void *) u;
				f = (void *) (uintptr_t) t->handles_ptr;
				v = (void *) (uintptr_t) t->values_ptr;
				n = (uint32_t) t->fence_count;
			}
	}
	/* les attentes : tout passe dans l'ordre sur le seul moteur de rendu, deja satisfaites */
	for (uint32_t i = 0; i < n; i++)
		if (f[i].flags & I915_EXEC_FENCE_SIGNAL)
			sync_signal (f[i].handle, v ? v[i] : 0, seq);
}

static int execbuf (struct drm_i915_gem_execbuffer2 *x)
{
	jobs++;
	struct drm_i915_gem_exec_object2 *o = (void *) (uintptr_t) x->buffers_ptr;
	uint32_t n = x->buffer_count;
	if (!n)
		return -EINVAL;
	if (dry) {
		signal_fences (x, 0);
		return 0;
	}
	struct gctx *g = ctx_lookup ((uint32_t) x->rsvd1);
	if (!g)
		return -ENOENT;
	unsigned ring = (unsigned) (x->flags & I915_EXEC_RING_MASK);
	int copy;
	if (g->nslots) {
		if (ring >= (unsigned) g->nslots)
			return -EINVAL;
		copy = g->cls[ring] == I915_ENGINE_CLASS_COPY;
	} else {
		copy = ring == I915_EXEC_BLT;
		ring = 0;
	}
	if (copy) {		/* le copieur : son lot d'initialisation, tenu pour fini (voir en tete) */
		uint32_t s = status ? status[IGS_SUBMITTED] : 0;
		for (uint32_t i = 0; i < n; i++) {
			struct bo *b = lookup (o[i].handle);
			if (b)
				b->last_seq = s;
		}
		signal_fences (x, s);
		return 0;
	}
	if (ring >= SLOTS)
		return -EINVAL;
	if (g->hw[ring] < 0) {
		uint64_t out[4];
		long r = call4 (IG_OP_CTX_CREATE, g->vm, 0, 0, 0, out);
		if (r < 0) {
			static int said;
			if (said++ < 3)	/* (iris recommence sans fin : trois fois suffisent) */
				fprintf (stderr, "korin_i915 : contexte refuse par intelgpu (%ld, espace %u) : voir le journal (REFUS)\n", r, g->vm);
			return -ENOMEM;
		}
		g->hw[ring] = (long) out[0];
	}
	/* les BO pas encore lies a leur adresse (ou deplaces par iris) */
	struct ig_bind e[IG_BIND_MAX];
	int ne = 0;
	for (uint32_t i = 0; i < n; i++) {
		struct bo *b = lookup (o[i].handle);
		if (!b)
			return -ENOENT;
		uint64_t a = o[i].offset & VA_MASK;
		if (b->gpu == a && b->vm == g->vm)
			continue;
		if (!b->c || b->c->buf < 0)
			return -EINVAL;	/* (une zone importee d'ailleurs : pas dans nos morceaux) */
		b->gpu = a;
		b->vm = g->vm;
		/* (un morceau de plusieurs tampons : une liaison par tampon touche) */
		for (uint64_t at = 0; at < b->bytes;) {
			uint64_t off = b->b->off + at, in = off % BUF_SLOT;
			uint64_t len = b->bytes - at < BUF_SLOT - in ? b->bytes - at : BUF_SLOT - in;
			e[ne].gpu = a + at;
			e[ne].bytes = len;
			e[ne].buf = (uint32_t) (b->c->buf + (long) (off / BUF_SLOT));
			e[ne].pad = 0;
			e[ne].off = in;
			at += len;
			if (++ne == IG_BIND_MAX) {
				if (bind_flush (g->vm, e, ne))
					return -EINVAL;
				ne = 0;
			}
		}
	}
	if (bind_flush (g->vm, e, ne))
		return -EINVAL;
	struct drm_i915_gem_exec_object2 *batch = (x->flags & I915_EXEC_BATCH_FIRST) ? &o[0] : &o[n - 1];
	uint64_t ba = (batch->offset & VA_MASK) + x->batch_start_offset, out[4];
	long r;
	for (int tries = 0;; tries++) {
		r = call4 (IG_OP_EXEC, (uint64_t) g->hw[ring], ba, x->batch_len, 0, out);
		if (r != -11 || tries > 100)	/* E_AGAIN : l'anneau est plein, un peu d'attente */
			break;
		wait_seq (status[IGS_SUBMITTED] - 64, 100000000);
		/* (le verrou a ete rendu pendant l'attente : le contexte peut avoir ete detruit, ou ctxs deplace par
		 * realloc ; 24/11 : faute a la fermeture de Blender dans le fil de soumission d'anv) */
		g = ctx_lookup ((uint32_t) x->rsvd1);
		if (!g || ring >= SLOTS || g->hw[ring] < 0)
			return -ENOENT;
	}
	if (r < 0) {
		if (debug)
			fprintf (stderr, "korin_i915 : IG_OP_EXEC refuse (%ld)\n", r);
		if (r == -5)
			return -EIO;	/* contexte banni apres un blocage : iris en refait un */
		lost = 1;
		return -EIO;
	}
	uint32_t seq = (uint32_t) out[0];
	for (uint32_t i = 0; i < n; i++) {
		struct bo *b = lookup (o[i].handle);
		if (b)		/* (ferme pendant une attente de l'anneau plein) */
			b->last_seq = seq;
	}
	signal_fences (x, seq);
	return 0;
}

/* -------- la synchronisation ------------------------------------------------------------------------- */
static int sync_new (uint32_t flags, uint32_t *h)
{
	uint32_t i = 1;
	while (i < nsyncs && syncs[i] != UINT32_MAX)
		i++;
	if (i >= nsyncs) {
		uint32_t n = nsyncs ? nsyncs * 2 : 256;
		syncs = realloc (syncs, n * sizeof *syncs);
		tls = realloc (tls, n * sizeof *tls);
		for (uint32_t k = nsyncs; k < n; k++)
			syncs[k] = UINT32_MAX;	/* libre */
		if (!nsyncs)
			syncs[0] = 0;
		nsyncs = n;
	}
	(void) flags;
	syncs[i] = 0;			/* signale (et tant qu'aucun lot ne le signale) */
	memset (&tls[i], 0, sizeof tls[i]);
	*h = i;
	return 0;
}

static int sync_ok (uint32_t h)
{
	return h && h < nsyncs && syncs[h] != UINT32_MAX;
}

static uint32_t sync_seq (uint32_t h)
{
	return sync_ok (h) ? syncs[h] : 0;
}

/* les points dont le lot est fini passent a « atteint » */
static void tl_retire (struct tl *t)
{
	uint32_t k = 0;
	while (k < t->n && done (t->p[k].seq)) {
		if (t->p[k].pt > t->reached)
			t->reached = t->p[k].pt;
		k++;
	}
	if (k) {
		memmove (t->p, t->p + k, (t->n - k) * sizeof t->p[0]);
		t->n -= k;
	}
}

/* le syncobj h signale par le lot seq (0 : tout de suite, par l'hote), au point pt (0 : binaire) */
static void sync_signal (uint32_t h, uint64_t pt, uint32_t seq)
{
	if (!sync_ok (h))
		return;
	syncs[h] = seq;
	if (!pt)
		return;
	struct tl *t = &tls[h];
	tl_retire (t);
	if (done (seq)) {
		if (pt > t->reached)
			t->reached = pt;
		return;
	}
	if (t->n == TL_PENDING) {	/* le plus ancien s'en va : son point sera atteint avec le suivant */
		memmove (t->p, t->p + 1, (TL_PENDING - 1) * sizeof t->p[0]);
		t->n--;
	}
	t->p[t->n].pt = pt;
	t->p[t->n].seq = seq;
	t->n++;
}

static uint64_t sync_value (uint32_t h)
{
	if (!sync_ok (h))
		return 0;
	tl_retire (&tls[h]);
	return tls[h].reached;
}

/* le lot a attendre pour le point pt de h (pt 0 : binaire, son dernier lot) ; 1 : point pas encore soumis */
static int sync_target (uint32_t h, uint64_t pt, uint32_t *seq)
{
	*seq = 0;
	if (!sync_ok (h))
		return 0;
	if (!pt) {
		*seq = syncs[h];
		return 0;
	}
	struct tl *t = &tls[h];
	tl_retire (t);
	if (pt <= t->reached)
		return 0;
	for (uint32_t k = 0; k < t->n; k++)
		if (t->p[k].pt >= pt) {
			*seq = t->p[k].seq;
			return 0;
		}
	return 1;
}

/* timeout absolu (CLOCK_MONOTONIC) ; pts : les points (NULL : binaires) ; all : tous -> 0, -ETIME. Un point pas
 * encore soumis (attente avant soumission) : on regarde de nouveau toutes les millisecondes. */
static int sync_wait (const uint32_t *h, const uint64_t *pts, uint32_t n, int64_t abs_ns, int all, uint32_t *first)
{
	for (;;) {
		int64_t rel = abs_ns <= 0 ? 0 : abs_ns - (int64_t) now_ns ();
		if (abs_ns == INT64_MAX)
			rel = -1;
		else if (rel < 0)
			rel = 0;
		int ready = 1, later = 0, have = 0;
		uint32_t target = 0;
		for (uint32_t i = 0; i < n; i++) {
			uint32_t seq;
			if (sync_target (h[i], pts ? pts[i] : 0, &seq)) {
				later = 1;
				ready = 0;
				continue;
			}
			if (done (seq)) {
				if (!all) {
					if (first)
						*first = i;
					return 0;
				}
				continue;
			}
			ready = 0;
			/* tous : n'importe lequel des restants ; le premier : le plus petit numero (fini avant les autres) */
			if (!have || (!all && (int32_t) (seq - target) < 0))
				target = seq;
			have = 1;
		}
		if (all && ready) {
			if (first)
				*first = 0;
			return 0;
		}
		if (!have && !later)
			return -EINVAL;		/* (aucun syncobj) */
		if (rel == 0)
			return -ETIME;
		int64_t step = later && (rel < 0 || rel > 1000000) ? 1000000 : rel;
		if (have) {
			int r = wait_seq (target, step);
			if (r && r != -ETIME)
				return r;
		} else {
			pthread_mutex_unlock (&lock);
			usleep ((useconds_t) (step / 1000));
			pthread_mutex_lock (&lock);
		}
	}
}

/* le point sp de s (0 : binaire) recopie au point dp de d */
static void sync_transfer (uint32_t d, uint64_t dp, uint32_t s, uint64_t sp)
{
	uint32_t seq;
	if (sync_target (s, sp, &seq))
		seq = sync_seq (s);	/* (pas encore soumis : son dernier lot, au mieux) */
	sync_signal (d, dp, seq);
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
		uint64_t out[4];
		if (!dry && call4 (IG_OP_TIMESTAMP, 0, 0, 0, 0, out) == 0)
			r->val = out[0];
		else
			r->val = now_ns () * (TGL_TS_HZ / 1000000) / 1000;
		return 0;
	}
	case DRM_IOCTL_I915_GET_RESET_STATS: {
		struct drm_i915_reset_stats *r = arg;
		r->reset_count = r->batch_active = r->batch_pending = 0;
		struct gctx *g = ctx_lookup (r->ctx_id);
		uint64_t out[4];
		if (!dry && g && g->hw[0] >= 0 && call4 (IG_OP_RESET_STATS, (uint64_t) g->hw[0], 0, 0, 0, out) == 0) {
			r->reset_count = (uint32_t) out[0];
			r->batch_active = (uint32_t) out[1];
		}
		return 0;
	}
	case DRM_IOCTL_I915_GEM_GET_APERTURE: {
		struct drm_i915_gem_get_aperture *a = arg;
		a->aper_size = a->aper_available_size = 256ULL << 20;
		return 0;
	}
	/* contextes, espaces d'adresses */
	case DRM_IOCTL_I915_GEM_CONTEXT_CREATE: return ctx_create (arg, 0);
	case DRM_IOCTL_I915_GEM_CONTEXT_CREATE_EXT: return ctx_create (arg, 1);
	case DRM_IOCTL_I915_GEM_CONTEXT_DESTROY:
		ctx_destroy (((struct drm_i915_gem_context_destroy *) arg)->ctx_id);
		return 0;
	case DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM: return ctx_getparam (arg);
	case DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM: {
		struct drm_i915_gem_context_param *p = arg;
		struct gctx *g = ctx_lookup (p->ctx_id);
		if (g)
			ctx_param (g, p);
		return 0;
	}
	case DRM_IOCTL_I915_GEM_VM_CREATE: {
		uint64_t out[4];
		if (dry) {
			((struct drm_i915_gem_vm_control *) arg)->vm_id = ++next_vm_fake;
			return 0;
		}
		if (call4 (IG_OP_VM_CREATE, 0, 0, 0, 0, out) < 0)
			return -ENOMEM;
		vm_own ((uint32_t) out[0]);
		((struct drm_i915_gem_vm_control *) arg)->vm_id = (uint32_t) out[0];
		return 0;
	}
	case DRM_IOCTL_I915_GEM_VM_DESTROY: {
		uint32_t v = ((struct drm_i915_gem_vm_control *) arg)->vm_id;
		if (!dry && v != vm_default && vm_is_own (v)) {
			call4 (IG_OP_VM_DESTROY, v, 0, 0, 0, NULL);
			vm_disown (v);
		}
		return 0;
	}
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
	case DRM_IOCTL_I915_GEM_BUSY: {
		struct drm_i915_gem_busy *b = arg;
		struct bo *o = lookup (b->handle);
		if (!o)
			return -ENOENT;
		b->busy = !done (o->last_seq);
		return 0;
	}
	case DRM_IOCTL_I915_GEM_WAIT: {
		struct drm_i915_gem_wait *w = arg;
		struct bo *o = lookup (w->bo_handle);
		if (!o)
			return -ENOENT;
		uint32_t s = o->last_seq;
		uint64_t t0 = now_ns ();
		int r = wait_seq (s, w->timeout_ns);
		if (w->timeout_ns > 0) {
			int64_t left = w->timeout_ns - (int64_t) (now_ns () - t0);
			w->timeout_ns = left > 0 ? left : 0;
		}
		return r;
	}
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
		return execbuf (arg);
	/* synchronisation */
	case DRM_IOCTL_SYNCOBJ_CREATE: {
		struct drm_syncobj_create *c = arg;
		return sync_new (c->flags, &c->handle);
	}
	case DRM_IOCTL_SYNCOBJ_DESTROY: {
		uint32_t h = ((struct drm_syncobj_destroy *) arg)->handle;
		if (h && h < nsyncs)
			syncs[h] = UINT32_MAX;
		return 0;
	}
	case DRM_IOCTL_SYNCOBJ_WAIT: {
		struct drm_syncobj_wait *w = arg;
		return sync_wait ((uint32_t *) (uintptr_t) w->handles, NULL, w->count_handles, w->timeout_nsec,
				  (w->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) != 0, &w->first_signaled);
	}
	case DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT: {
		struct drm_syncobj_timeline_wait *w = arg;
		return sync_wait ((uint32_t *) (uintptr_t) w->handles, (uint64_t *) (uintptr_t) w->points,
				  w->count_handles, w->timeout_nsec,
				  (w->flags & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) != 0, &w->first_signaled);
	}
	case DRM_IOCTL_SYNCOBJ_QUERY: {
		struct drm_syncobj_timeline_array *a = arg;
		uint64_t *pts = (void *) (uintptr_t) a->points;
		uint32_t *h = (void *) (uintptr_t) a->handles;
		for (uint32_t i = 0; i < a->count_handles; i++)
			pts[i] = sync_value (h[i]);
		return 0;
	}
	case DRM_IOCTL_SYNCOBJ_RESET:
	case DRM_IOCTL_SYNCOBJ_SIGNAL: {		/* un syncobj sans lot est tenu pour signale */
		struct drm_syncobj_array *a = arg;
		uint32_t *h = (void *) (uintptr_t) a->handles;
		for (uint32_t i = 0; i < a->count_handles; i++)
			sync_signal (h[i], 0, 0);
		return 0;
	}
	case DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL: {
		struct drm_syncobj_timeline_array *a = arg;
		uint32_t *h = (void *) (uintptr_t) a->handles;
		uint64_t *pts = (void *) (uintptr_t) a->points;
		for (uint32_t i = 0; i < a->count_handles; i++)
			sync_signal (h[i], pts ? pts[i] : 0, 0);
		return 0;
	}
	case DRM_IOCTL_SYNCOBJ_TRANSFER: {
		struct drm_syncobj_transfer *t = arg;
		sync_transfer (t->dst_handle, t->dst_point, t->src_handle, t->src_point);
		return 0;
	}
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
		char *nodes[DRM_NODE_MAX];
	} *p = calloc (1, sizeof *p);
	if (!p)
		return -ENOMEM;
	/* un noeud de rendu (anv le veut ; il ne l'ouvre pas : korin_anv_open, korin-pc.patch) */
	static char render[] = "/dev/dri/renderD128";
	p->nodes[DRM_NODE_RENDER] = render;
	p->d.nodes = p->nodes;
	p->d.available_nodes = 1 << DRM_NODE_RENDER;
	p->d.bustype = DRM_BUS_PCI;
	p->d.businfo.pci = &p->bus;
	p->d.deviceinfo.pci = &p->dev;
	p->bus.dev = 2;
	p->dev.vendor_id = 0x8086;
	p->dev.device_id = dry || !info.device ? TGL_ID : info.device;
	p->dev.revision_id = dry || !info.device ? TGL_REV : info.revision;
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

/* un seul GPU : deux descriptions PCI sont le meme s'ils sont tous deux en PCI (la WSI de Vulkan) */
int drmDevicesEqual (drmDevicePtr a, drmDevicePtr b)
{
	return a && b && a->bustype == b->bustype;
}

void drmFreeDevices (drmDevicePtr devices[], int count)
{
	for (int i = 0; i < count; i++)
		drmFreeDevice (&devices[i]);
}

/* les syncobj, et leur forme chronologique (anv : sans elle, pas de semaphore chronologique de Vulkan). Un
 * syncobj garde le numero du dernier lot qui le signale ; un point d'attente vaut ce lot (une seule file, qui
 * finit dans l'ordre) */
int drmGetCap (int fd, uint64_t c, uint64_t *v)
{
	(void) fd;
	if (c == DRM_CAP_SYNCOBJ || c == DRM_CAP_SYNCOBJ_TIMELINE) {
		*v = 1;
		return 0;
	}
	*v = 0;
	return -EINVAL;
}
int drmPrimeFDToHandle (int fd, int p, uint32_t *h) { (void) fd; (void) p; (void) h; return -ENOSYS; }
int drmPrimeHandleToFD (int fd, uint32_t h, uint32_t f, int *p) { (void) fd; (void) h; (void) f; (void) p; return -ENOSYS; }

static int locked (int r)
{
	if (r < 0) {
		errno = -r;
		return -1;
	}
	return 0;
}

int drmSyncobjCreate (int fd, uint32_t flags, uint32_t *handle)
{
	(void) fd;
	pthread_mutex_lock (&lock);
	int r = sync_new (flags, handle);
	pthread_mutex_unlock (&lock);
	return locked (r);
}
int drmSyncobjDestroy (int fd, uint32_t h)
{
	(void) fd;
	pthread_mutex_lock (&lock);
	if (h && h < nsyncs)
		syncs[h] = UINT32_MAX;
	pthread_mutex_unlock (&lock);
	return 0;
}
int drmSyncobjWait (int fd, uint32_t *h, unsigned n, int64_t t, unsigned f, uint32_t *first)
{
	(void) fd;
	pthread_mutex_lock (&lock);
	int r = sync_wait (h, NULL, n, t, (f & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) != 0, first);
	pthread_mutex_unlock (&lock);
	return locked (r);
}
int drmSyncobjImportSyncFile (int fd, uint32_t h, int s) { (void) fd; (void) h; (void) s; return 0; }
int drmSyncobjExportSyncFile (int fd, uint32_t h, int *s) { (void) fd; (void) h; *s = FAKE_FD; return 0; }
int drmSyncobjFDToHandle (int fd, int o, uint32_t *h) { (void) fd; (void) o; (void) h; return -ENOSYS; }
int drmSyncobjHandleToFD (int fd, uint32_t h, int *o) { (void) fd; (void) h; (void) o; return -ENOSYS; }
int drmSyncobjTimelineSignal (int fd, const uint32_t *h, uint64_t *p, uint32_t n)
{
	(void) fd;
	pthread_mutex_lock (&lock);
	for (uint32_t i = 0; i < n; i++)
		sync_signal (h[i], p ? p[i] : 0, 0);
	pthread_mutex_unlock (&lock);
	return 0;
}
int drmSyncobjReset (int fd, const uint32_t *h, uint32_t n) { return drmSyncobjTimelineSignal (fd, h, NULL, n); }
int drmSyncobjSignal (int fd, const uint32_t *h, uint32_t n) { return drmSyncobjTimelineSignal (fd, h, NULL, n); }
int drmSyncobjTimelineWait (int fd, uint32_t *h, uint64_t *p, unsigned n, int64_t t, unsigned f, uint32_t *first)
{
	(void) fd;
	pthread_mutex_lock (&lock);
	int r = sync_wait (h, p, n, t, (f & DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL) != 0, first);
	pthread_mutex_unlock (&lock);
	return locked (r);
}
int drmSyncobjQuery2 (int fd, uint32_t *h, uint64_t *p, uint32_t n, uint32_t f)
{
	(void) fd; (void) f;
	pthread_mutex_lock (&lock);
	for (uint32_t i = 0; i < n; i++)
		p[i] = sync_value (h[i]);
	pthread_mutex_unlock (&lock);
	return 0;
}
int drmSyncobjTransfer (int fd, uint32_t d, uint64_t dp, uint32_t s, uint64_t sp, uint32_t f)
{
	(void) fd; (void) f;
	pthread_mutex_lock (&lock);
	sync_transfer (d, dp, s, sp);
	pthread_mutex_unlock (&lock);
	return 0;
}

/* -------- l'interface d'armgl (comme tools/mesa/libdrm/armpi_drm.c) --------------------------------- */
/* iris veut un vrai descripteur (fstat, puis une copie par F_DUPFD_CLOEXEC) : un tube, que personne ne lit */
static int open_fd (void)
{
	int p[2];
	if (drm_fd >= 0 && fcntl (drm_fd, F_GETFD) < 0)	/* (ferme par anv a la fin d'une instance) */
		drm_fd = -1;
	if (drm_fd < 0 && pipe (p) == 0)
		drm_fd = p[0];
	if (drm_fd < 0)		/* (pas de tube : le moniteur du noyau, sans le systeme ; tous nos appels l'ignorent) */
		return FAKE_FD;
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
	/* le service du GPU (src/svc/intelgpu), lance a la main : /sys/svc/intelgpu start */
	chan = armpi_syscall (SYS_SVC_OPEN, (long) IG_SVC_NAME, 0, 0, 0, 0, 0);
	if (chan < 0)
		return -1;
	uint64_t m[64];
	memset (m, 0, sizeof m);
	m[0] = IG_OP_INFO;
	if (call (m) < 0)
		return -1;
	memcpy (&info, (uint8_t *) m + 48, sizeof info);
	if (info.version != IG_VERSION || !info.state) {
		if (debug)
			fprintf (stderr, "korin_i915 : service version %u, etat %u : pas de GPU\n", info.version, info.state);
		return -1;
	}
	long va = sys2 (SYS_BUF_MAP, info.status_buf, 0);
	if (va < 0)
		return -1;
	status = (volatile uint32_t *) va;
	uint64_t out[4];
	if (call4 (IG_OP_VM_CREATE, 0, 0, 0, 0, out) < 0)
		return -1;
	vm_default = (uint32_t) out[0];
	vm_own (vm_default);
	if (debug || info.state == 2)
		fprintf (stderr, "korin_i915 : service intelgpu%s, %04x rev. %u, %u EU, espace %u\n",
			 info.state == 2 ? " en SIMU (faux GPU : pas de dessin)" : "", info.device, info.revision,
			 info.eu_total, vm_default);
	return open_fd ();
}

/* anv (korin-pc.patch, anv_physical_device.c) : le GPU a la place d'open("/dev/dri/renderD128") ; KORIN_VK_DRY :
 * a blanc (sans service, lots comptes et pas lances). Deja ouvert (GL dans le meme process) : le meme. */
int korin_anv_open (void)
{
	if (getenv ("KORIN_DRM_DEBUG"))
		fprintf (stderr, "korin_i915 : ouverture pour anv%s\n", getenv ("KORIN_VK_DRY") ? " (a blanc)" : "");
	if (drm_fd >= 0 && (dry || chan >= 0))
		return open_fd ();
	return armpi_drm_open (getenv ("KORIN_VK_DRY") ? ARMPI_DRM_DRY : 0);
}

int armpi_drm_lost (void) { return lost; }
long armpi_drm_jobs (void) { return jobs; }
unsigned armpi_drm_first_phys (void) { return 0; }

/* le service est en simu (faux GPU) : les lots passent, rien n'est dessine */
__attribute__ ((visibility ("default"))) int korin_i915_simu (void) { return info.state == 2; }

/* une zone d'ailleurs : a blanc ou softpipe seulement (avec le GPU, seuls nos morceaux sont vus du GPU) */
unsigned armpi_drm_import (void *va, unsigned phys, unsigned bytes)
{
	(void) phys;
	if (!dry && !soft)
		return 0;
	pthread_mutex_lock (&lock);
	uint32_t h = new_handle ();
	memset (&bos[h], 0, sizeof bos[h]);
	bos[h].va = va;
	bos[h].bytes = bytes;
	bos[h].used = 1;
	bos[h].imported = 1;
	pthread_mutex_unlock (&lock);
	return h;
}

/* une image vue du GPU et du processeur (un BO d'un morceau) */
void *armpi_drm_buffer (unsigned long bytes, unsigned *name)
{
	uint32_t h;
	pthread_mutex_lock (&lock);
	int r = bo_create (bytes, &h);
	void *p = r ? NULL : bos[h].va;
	pthread_mutex_unlock (&lock);
	if (p)
		*name = h;
	return p;
}

static void cache_flush (uint8_t *p, unsigned long bytes);

/* le GPU d'Intel et le processeur partagent le dernier niveau de cache (LLC), mais iris ecrit une image
 * importee sans le LLC (MOCS non cache) : ses lignes sont chassees des caches du processeur, avant que le GPU y
 * touche et avant que le processeur la relise (armgl_buffer_bind, armgl_buffer_done) */
void armpi_drm_flush (void *va, unsigned long bytes)
{
	if (!soft && !dry && va)
		cache_flush (va, bytes);
}

/* -------- les images d'une fenetre ------------------------------------------------------------------
 * Comme le Pi (tools/mesa/libdrm/armpi_drm.c) : le GPU rend directement dans une surface d'Elegant. Un tampon
 * partage de deux images (BUF_CREATE), donne au service (BUF_GRANT, IG_OP_CHUNK : c'est un morceau, ses deux
 * moities sont deux BO) puis a Elegant (BUF_GRANT a la premiere presentation, WS_OP_SURFACE_ATTACH) : Elegant
 * montre l'une pendant que le GPU ecrit l'autre. Iris ecrit une image importee sans passer par le LLC (MOCS non
 * cache) : avant de la montrer, ses lignes sont chassees des caches du processeur (clflush), pour qu'Elegant ne
 * lise pas d'anciennes valeurs. softpipe : le meme tampon, ecrit par le processeur (korin_sw_winsys).
 */
#define SYS_WS_CALL		0x51
#define WS_OP_SURFACE_ATTACH	25
#define WS_OP_SURFACE_DAMAGE	26
#define WS_SF_NOREDRAW		256
#define E_PERM			(-1)

struct win {
	long buf, id;
	uint8_t *va;
	uint32_t pitch, frame;
	int x, y, w, h;			/* la zone montree */
	int height;			/* la hauteur de l'image */
	uint32_t names[3];
	int nimg;			/* images : 3 (une a l'ecran, une au GPU, une ou l'on dessine), 2 si trop grand */
	struct chunk *c;		/* le morceau (GPU), NULL : softpipe */
	long ns_cache, ns_attach, frames;
};

static long now_ns_l (void) { return (long) now_ns (); }

static void cache_flush (uint8_t *p, unsigned long bytes)
{
	uint8_t *end = p + bytes;
	__asm__ volatile ("mfence" ::: "memory");
	for (p = (uint8_t *) ((uintptr_t) p & ~63UL); p < end; p += 64)
		__asm__ volatile ("clflush (%0)" : : "r" (p) : "memory");
	__asm__ volatile ("mfence" ::: "memory");
}

/* un BO sur une tranche d'un tampon qui n'est pas aux morceaux de bo_create (verrou pris) */
static uint32_t bo_wrap (struct chunk *c, uint8_t *va, uint64_t off, uint64_t bytes)
{
	struct block *b = NULL;
	if (c) {
		b = calloc (1, sizeof *b);
		b->off = off;
		b->bytes = bytes;
		b->used = 1;
	}
	uint32_t h = new_handle ();
	struct bo *o = &bos[h];
	memset (o, 0, sizeof *o);
	o->used = 1;
	o->imported = c == NULL;
	o->va = va;
	o->bytes = bytes;
	o->c = c;
	o->b = b;
	return h;
}

void *armpi_drm_window (long id, int x, int y, int width, int height, unsigned names[3], unsigned *pitch)
{
	struct win *w = calloc (1, sizeof *w);
	if (!w)
		return NULL;
	w->id = id;
	w->x = x;
	w->y = y;
	w->w = width;
	w->h = height;
	w->height = height;
	w->pitch = ((uint32_t) width * 4 + 255) & ~255U;	/* (iris : lignes alignees pour le rendu lineaire) */
	w->frame = (w->pitch * (uint32_t) height + PAGE - 1) & ~(PAGE - 1);
	w->nimg = 3UL * w->frame <= BUF_SLOT ? 3 : 2;	/* (la troisieme : l'echange sans glFinish, armgl.c) */
	if ((unsigned long) w->nimg * w->frame > BUF_SLOT || width <= 0 || height <= 0)
		goto fail;
	w->buf = sys2 (SYS_BUF_CREATE, (long) ((unsigned long) w->nimg * w->frame), 0);
	if (w->buf < 0)
		goto fail;
	w->va = (uint8_t *) (BUF_VA_BASE + (unsigned long) w->buf * BUF_SLOT);
	pthread_mutex_lock (&lock);
	if (!soft && !dry) {
		/* un morceau a lui seul : le service voit ses pages, iris y lie les deux images */
		if (nchunks == MAX_CHUNKS || sys2 (SYS_BUF_GRANT, w->buf, info.pid) < 0
		    || call4 (IG_OP_CHUNK, (uint64_t) w->buf, 0, 0, 0, NULL) < 0) {
			pthread_mutex_unlock (&lock);
			sys2 (SYS_BUF_FREE, w->buf, 0);
			goto fail;
		}
		struct chunk *c = &chunks[nchunks++];
		c->buf = w->buf;
		c->va = w->va;
		c->bytes = (unsigned long) w->nimg * w->frame;
		c->blocks = NULL;		/* (rien a y prendre pour bo_create) */
		w->c = c;
	}
	for (int i = 0; i < w->nimg; i++)
		w->names[i] = names[i] = bo_wrap (w->c, w->va + (unsigned long) i * w->frame,
						  (uint64_t) i * w->frame, w->frame);
	pthread_mutex_unlock (&lock);
	*pitch = w->pitch;
	if (debug)
		fprintf (stderr, "korin_i915 : fenetre %ld, %dx%d, tampon %ld (%s)\n", id, width, height, w->buf,
			 w->c ? "GPU" : "processeur");
	return w;		/* attachee par la premiere image : avant, elle serait noire a l'ecran */
fail:
	free (w);
	return NULL;
}

static long ws_attach (struct win *w, int image)
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

static long ws_damage (struct win *w)
{
	long msg[64];
	memset (msg, 0, sizeof msg);
	msg[0] = WS_OP_SURFACE_DAMAGE;
	msg[1] = w->id;
	return sys2 (SYS_WS_CALL, (long) msg, 0);
}

/* la zone montree : coin haut gauche x, y dans le contenu ; l'image du GL est le bas de l'image entiere */
void armpi_drm_zone (void *window, long id, int x, int y, int width, int height)
{
	struct win *w = window;
	w->id = id;
	w->x = x;
	w->y = y;
	w->w = width < (int) (w->pitch / 4) ? width : (int) (w->pitch / 4);
	w->h = height < w->height ? height : w->height;
}

/* detachee de sa fenetre ; les images de Mesa d'abord (armgl_window_free). Le morceau du GPU reste au
 * service jusqu'a la fin du process (ses PTE y pointent peut-etre encore) : ses pages ne sont pas rendues avant. */
void armpi_drm_window_free (void *window)
{
	struct win *w = window;
	long msg[64];
	memset (msg, 0, sizeof msg);
	msg[0] = WS_OP_SURFACE_ATTACH;
	msg[1] = w->id;
	msg[2] = -1;
	sys2 (SYS_WS_CALL, (long) msg, 0);
	pthread_mutex_lock (&lock);
	for (int i = 0; i < w->nimg; i++) {
		struct bo *b = lookup (w->names[i]);
		if (b) {
			free (b->b);
			memset (b, 0, sizeof *b);
		}
	}
	pthread_mutex_unlock (&lock);
	if (!w->c)
		sys2 (SYS_BUF_FREE, w->buf, 0);
	free (w);
}

/* montre l'image <image>, que le GPU (ou softpipe) a finie */
int armpi_drm_present (void *window, int image)
{
	struct win *w = window;
	uint8_t *p = w->va + (unsigned long) image * w->frame;
	if (dry) {					/* a blanc, de quoi voir la bascule sous QEMU */
		uint32_t c = image ? 0x0000C040 : 0x00C04000;
		for (uint32_t i = 0; i < w->frame / 4; i++)
			((uint32_t *) p)[i] = c;
	}
	long t0 = now_ns_l ();
	if (w->c)
		cache_flush (p + (unsigned long) (w->height - w->h) * w->pitch, (unsigned long) w->h * w->pitch);
	long t1 = now_ns_l ();
	long r = ws_attach (w, image);
	if (r >= 0)
		r = ws_damage (w);
	w->ns_cache += t1 - t0;
	w->ns_attach += now_ns_l () - t1;
	w->frames++;
	return r < 0 ? -1 : 0;
}

int armpi_drm_window_images (void *window)
{
	return ((struct win *) window)->nimg;
}

void armpi_drm_times (void *window, double *cache, double *attach)
{
	struct win *w = window;
	long n = w && w->frames ? w->frames : 1;
	*cache = w ? w->ns_cache / 1e6 / n : 0;
	*attach = w ? w->ns_attach / 1e6 / n : 0;
}

void armpi_drm_times_reset (void *window)
{
	struct win *w = window;
	if (w)
		w->ns_cache = w->ns_attach = w->frames = 0;
}

#ifndef KORIN_I915_VK	/* (anv, build-anv.sh : pas de Gallium, donc pas de softpipe) */
/* -------- softpipe : le repli processeur de GlKit sans GPU Intel (QEMU, un autre PC) -------------------
 * Un sw_winsys de Gallium : les images d'une fenetre (et d'armgl_buffers) sont des BO de ce faux libdrm,
 * dans la memoire du process ; softpipe y rend directement par displaytarget_from_handle. */
#include <stdbool.h>
#include "frontend/sw_winsys.h"
#include "frontend/winsys_handle.h"
#include "util/format/u_format.h"

struct kdt {
	void *va;
	unsigned stride;
	int owned;
};

static bool ksw_format (struct sw_winsys *ws, unsigned bind, enum pipe_format f)
{
	(void) ws; (void) bind;
	return f == PIPE_FORMAT_B8G8R8A8_UNORM || f == PIPE_FORMAT_B8G8R8X8_UNORM
	       || f == PIPE_FORMAT_R8G8B8A8_UNORM || f == PIPE_FORMAT_R8G8B8X8_UNORM;
}

static struct sw_displaytarget *ksw_create (struct sw_winsys *ws, unsigned bind, enum pipe_format f, unsigned width,
					    unsigned height, unsigned align, const void *front, unsigned *stride)
{
	(void) ws; (void) bind; (void) front;
	struct kdt *d = calloc (1, sizeof *d);
	unsigned a = align < 64 ? 64 : align;
	d->stride = (width * util_format_get_blocksize (f) + a - 1) / a * a;
	if (posix_memalign (&d->va, 64, (size_t) d->stride * height + 64)) {
		free (d);
		return NULL;
	}
	d->owned = 1;
	*stride = d->stride;
	return (struct sw_displaytarget *) d;
}

static struct sw_displaytarget *ksw_from_handle (struct sw_winsys *ws, const struct pipe_resource *t,
						 struct winsys_handle *h, unsigned *stride)
{
	(void) ws; (void) t;
	pthread_mutex_lock (&lock);
	struct bo *b = lookup (h->handle);
	void *va = b ? b->va : NULL;
	pthread_mutex_unlock (&lock);
	if (!va)
		return NULL;
	struct kdt *d = calloc (1, sizeof *d);
	d->va = va;
	d->stride = h->stride;
	*stride = h->stride;
	return (struct sw_displaytarget *) d;
}

static bool ksw_get_handle (struct sw_winsys *ws, struct sw_displaytarget *dt, struct winsys_handle *h)
{
	(void) ws; (void) dt; (void) h;
	return false;
}

static void *ksw_map (struct sw_winsys *ws, struct sw_displaytarget *dt, unsigned flags)
{
	(void) ws; (void) flags;
	return ((struct kdt *) dt)->va;
}

static void ksw_unmap (struct sw_winsys *ws, struct sw_displaytarget *dt) { (void) ws; (void) dt; }

static void ksw_display (struct sw_winsys *ws, struct sw_displaytarget *dt, void *ctx, unsigned nboxes,
			 struct pipe_box *box)
{
	(void) ws; (void) dt; (void) ctx; (void) nboxes; (void) box;
}

static void ksw_destroy_dt (struct sw_winsys *ws, struct sw_displaytarget *dt)
{
	(void) ws;
	struct kdt *d = (struct kdt *) dt;
	if (d->owned)
		free (d->va);
	free (d);
}

static void ksw_destroy (struct sw_winsys *ws) { (void) ws; }

/* armgl_init (ARMGL_IRIS) sans service : le faux libdrm passe en mode processeur, voici son winsys */
struct sw_winsys *korin_sw_winsys (void)
{
	static struct sw_winsys ws;
	soft = 1;
	debug = getenv ("KORIN_DRM_DEBUG") != NULL;
	ws.destroy = ksw_destroy;
	ws.is_displaytarget_format_supported = ksw_format;
	ws.displaytarget_create = ksw_create;
	ws.displaytarget_from_handle = ksw_from_handle;
	ws.displaytarget_get_handle = ksw_get_handle;
	ws.displaytarget_map = ksw_map;
	ws.displaytarget_unmap = ksw_unmap;
	ws.displaytarget_display = ksw_display;
	ws.displaytarget_destroy = ksw_destroy_dt;
	return &ws;
}

#else /* KORIN_I915_VK */
/* -------- la WSI de Vulkan (tools/mesa/wsi/wsi_common_korin.c) ---------------------------------------------
 * Le tampon lineaire d'une image de la chaine d'echange est un BO d'ici : une tranche d'un morceau, donc d'un
 * tampon partage du noyau. Elegant compose directement depuis lui : attache du morceau a son decalage (la premiere
 * fois, le droit de le projeter : BUF_GRANT a Elegant, dont la reponse E_PERM donne le pid). */
#define SYS_BUF_GRANT_WS	0x02

static long ws_call_attach (long window, long buf, int x, int y, int w, int h, unsigned pitch, long off)
{
	long msg[64];
	for (int i = 0; i < 2; i++) {
		memset (msg, 0, sizeof msg);
		msg[0] = WS_OP_SURFACE_ATTACH;
		msg[1] = window;
		msg[2] = buf;
		msg[3] = (long) x | (long) y << 32;
		msg[4] = (long) w | (long) h << 32;
		((uint32_t *) msg)[10] = 16;		/* longueur des donnees */
		((uint32_t *) msg)[12] = WS_SF_NOREDRAW;	/* WS_SF_XRGB8888 : B, G, R, X en memoire */
		((uint32_t *) msg)[13] = pitch;
		msg[7] = off;
		long r = sys2 (SYS_WS_CALL, (long) msg, 0);
		if (r != E_PERM || buf < 0)
			return r;
		sys2 (SYS_BUF_GRANT_WS, buf, msg[1]);	/* la reponse donne le pid d'Elegant */
	}
	return E_PERM;
}

__attribute__ ((visibility ("default"))) int korin_wsi_present (long window, int x, int y, int w, int h,
								 const void *va, unsigned pitch)
{
	const uint8_t *p = va;
	long buf = -1, off = 0;
	pthread_mutex_lock (&lock);
	for (int i = 0; i < nchunks; i++)
		if (chunks[i].buf >= 0 && p >= chunks[i].va && p < chunks[i].va + chunks[i].bytes) {
			off = (long) (p - chunks[i].va);
			buf = chunks[i].buf + off / (long) BUF_SLOT;	/* (morceau de plusieurs tampons) */
			off %= (long) BUF_SLOT;
			break;
		}
	pthread_mutex_unlock (&lock);
	if (buf < 0) {
		if (debug)
			fprintf (stderr, "korin_i915 : image de la chaine d'echange hors de nos morceaux (%p)\n", va);
		return -1;
	}
	long r = ws_call_attach (window, buf, x, y, w, h, pitch, off);
	if (r >= 0) {
		long msg[64];
		memset (msg, 0, sizeof msg);
		msg[0] = WS_OP_SURFACE_DAMAGE;
		msg[1] = window;
		r = sys2 (SYS_WS_CALL, (long) msg, 0);
	}
	if (r < 0 && debug)
		fprintf (stderr, "korin_i915 : fenetre %ld : attache refusee (%ld)\n", window, r);
	return r < 0 ? -1 : 0;
}

__attribute__ ((visibility ("default"))) void korin_wsi_detach (long window)
{
	ws_call_attach (window, -1, 0, 0, 0, 0, 0, 0);
}

/* anv : les expressions regulieres ne servent qu'a INTEL_DEBUG=attach (attendre un debogueur, par le nom du
 * programme) ; libposix n'en a pas : jamais de correspondance (faibles : cedent la place a de vraies) */
#include <regex.h>
__attribute__ ((weak)) int regcomp (regex_t *r, const char *p, int f) { (void) r; (void) p; (void) f; return REG_ESPACE; }
__attribute__ ((weak)) int regexec (const regex_t *r, const char *s, size_t n, regmatch_t m[], int f)
{
	(void) r; (void) s; (void) n; (void) m; (void) f;
	return REG_NOMATCH;
}
__attribute__ ((weak)) void regfree (regex_t *r) { (void) r; }
__attribute__ ((weak)) size_t regerror (int e, const regex_t *r, char *b, size_t n)
{
	(void) e; (void) r;
	if (n)
		b[0] = 0;
	return 1;
}
#endif /* KORIN_I915_VK */

/* ce qui rend, en clair : pour les messages des programmes (gltest, emulateurs) */
__attribute__ ((visibility ("default"))) const char *korin_i915_describe (void)
{
	static char t[160];
	if (soft)
		snprintf (t, sizeof t, "le processeur (softpipe de Mesa : service intelgpu absent, pas de GPU Intel)");
	else if (dry)
		snprintf (t, sizeof t, "a blanc (iris sans GPU : rien n'est dessine)");
	else if (info.state == 2)
		snprintf (t, sizeof t, "le faux GPU du service intelgpu (simu : rien n'est dessine)");
	else
		snprintf (t, sizeof t, "le GPU Intel %04x rev. %u, %u EU (iris, service intelgpu)", info.device,
			  info.revision, info.eu_total);
	return t;
}

/* intel_measure (INTEL_MEASURE=...) cree un tube nomme ; libposix n'a pas encore mkfifoat : refuse, faible pour
 * ceder la place a celui de libposix le jour ou il y sera */
#include <sys/stat.h>
__attribute__ ((weak)) int mkfifoat (int dirfd, const char *path, mode_t mode)
{
	(void) dirfd; (void) path; (void) mode;
	errno = ENOSYS;
	return -1;
}
