/*
 * armgl.c -- le frontal Gallium d'ARMPI : ce qu'etait OSMesa (retire de Mesa 25.1), avec le pilote v3d.
 * Pas de fenetre ici : une surface se recopie ou l'on veut (armgl_read).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "drm-uapi/drm_fourcc.h"
#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "frontend/api.h"
#include "frontend/winsys_handle.h"
#include "glapi/glapi.h"
#include "state_tracker/st_context.h"
#include "util/driconf.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/xmlconfig.h"
#ifdef ARMGL_IRIS
/* Korin PC : le pilote iris du GPU Intel (tools/mesa/build-iris.sh, docs/etude/GPU-INTEL.md) */
struct pipe_screen *iris_screen_create (int fd, const struct pipe_screen_config *config);
/* sans GPU Intel (service intelgpu absent) : softpipe, le rendu par le processeur (korin_i915.c) */
#include "softpipe/sp_public.h"
struct sw_winsys *korin_sw_winsys (void);
static int soft_mode;
#else
#include "v3d/v3d_screen.h"
#endif
#include "armpi_drm.h"
#include "armgl.h"

#define COLOR	PIPE_FORMAT_B8G8R8A8_UNORM	/* en memoire : 0xAARRGGBB, comme une surface d'Elegant */
#ifdef ARMGL_IRIS
#define ZS	PIPE_FORMAT_Z24_UNORM_S8_UINT	/* profondeur et stencil d'iris (Gen12 : deux surfaces) */
#else
#define ZS	PIPE_FORMAT_S8_UINT_Z24_UNORM	/* le D24S8 du V3D ; Z24_UNORM_S8_UINT n'est pas dans sa table : le GPU bloquait a la premiere ecriture du Z (docs/SDL.md) */
#endif

static const driOptionDescription driconf[] = {
#include "pipe-loader/driinfo_gallium.h"
#ifdef ARMGL_IRIS
#include "iris/driinfo_iris.h"
#else
#include "v3d/driinfo_v3d.h"
#endif
};

struct surface {
	struct pipe_frontend_drawable base;
	struct st_visual visual;
	int width, height;
	struct pipe_resource *tex[ST_ATTACHMENT_COUNT];
	void *window;			/* une surface d'Elegant : les deux images */
	struct pipe_resource *image[3];
	int back;			/* l'image ou le GPU dessine */
	void *buf[3];			/* (armgl_buffers) */
	unsigned long bytes;
	long ns_finish, frames;
#ifdef ARMGL_IRIS
	int nimg;			/* images de la fenetre (2 ou 3) */
	struct presenter *pr;		/* l'echange sans glFinish (3 images), NULL : glFinish a chaque image */
#endif
};

#ifdef ARMGL_IRIS
static void presenter_start (struct surface *s);
static void presenter_drain (struct surface *s);
static void presenter_stop (struct surface *s);
#endif

static struct pipe_frontend_screen fscreen;
static driOptionCache options, option_info;
static uint32_t next_id;

static int fs_get_param (struct pipe_frontend_screen *s, enum st_manager_param p)
{
	(void) s; (void) p;
	return 0;
}

/* une EGLImage : une texture UIF R G B A qui n'est pas a Mesa (une texture de GpuKit) */
struct image {
	struct pipe_resource *res;
};

static bool fs_get_egl_image (struct pipe_frontend_screen *s, void *egl_image, struct st_egl_image *out)
{
	struct image *i = egl_image;
	(void) s;
	memset (out, 0, sizeof *out);
	pipe_resource_reference (&out->texture, i->res);
	out->format = i->res->format;
	out->internalformat = 0x8058;	/* GL_RGBA8 */
	return true;
}

static bool fs_validate_egl_image (struct pipe_frontend_screen *s, void *egl_image)
{
	(void) s;
	return egl_image != NULL;
}

unsigned armgl_import (void *va, unsigned phys, unsigned bytes)
{
	return armpi_drm_import (va, phys, bytes);
}

void *armgl_image (unsigned name, int width, int height, int stride)
{
	struct pipe_screen *screen = fscreen.screen;
	struct pipe_resource t;
	struct winsys_handle h;
	memset (&t, 0, sizeof t);
	memset (&h, 0, sizeof h);
	t.target = PIPE_TEXTURE_2D;
	t.format = PIPE_FORMAT_R8G8B8A8_UNORM;
	t.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
	t.width0 = width;
	t.height0 = height;
	t.depth0 = 1;
	t.array_size = 1;
	t.usage = PIPE_USAGE_DEFAULT;
	h.type = WINSYS_HANDLE_TYPE_SHARED;
	h.handle = name;
	h.stride = stride;
#ifdef ARMGL_IRIS
	h.modifier = DRM_FORMAT_MOD_LINEAR;
#else
	h.modifier = DRM_FORMAT_MOD_BROADCOM_UIF;
#endif
	struct pipe_resource *res = screen->resource_from_handle (screen, &t, &h, 0);
	if (!res)
		return NULL;
	struct image *i = CALLOC_STRUCT (image);
	i->res = res;
	return i;
}

void armgl_image_free (void *image)
{
	struct image *i = image;
	pipe_resource_reference (&i->res, NULL);
	FREE (i);
}

static bool surface_flush_front (struct st_context *st, struct pipe_frontend_drawable *d, enum st_attachment_type a)
{
	(void) st; (void) d; (void) a;
	return true;
}

static bool surface_validate (struct st_context *st, struct pipe_frontend_drawable *d,
			      const enum st_attachment_type *atts, unsigned count,
			      struct pipe_resource **out, struct pipe_resource **resolve)
{
	struct surface *s = (struct surface *) d;
	struct pipe_screen *screen = fscreen.screen;
	(void) st;
	if (resolve)
		*resolve = NULL;
	for (unsigned i = 0; i < count; i++) {
		enum st_attachment_type a = atts[i];
		if (a == ST_ATTACHMENT_BACK_LEFT && (s->window || s->buf[0])) {
			pipe_resource_reference (&out[i], s->image[s->back]);
			continue;
		}
		if (!s->tex[a]) {
			struct pipe_resource t;
			memset (&t, 0, sizeof t);
			t.target = PIPE_TEXTURE_2D;
			t.format = a == ST_ATTACHMENT_DEPTH_STENCIL ? ZS : COLOR;
			t.bind = a == ST_ATTACHMENT_DEPTH_STENCIL ? PIPE_BIND_DEPTH_STENCIL
								  : PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
			t.width0 = s->width;
			t.height0 = s->height;
			t.depth0 = 1;
			t.array_size = 1;
			t.usage = PIPE_USAGE_DEFAULT;
			s->tex[a] = screen->resource_create (screen, &t);
			if (!s->tex[a])
				return false;
		}
		pipe_resource_reference (&out[i], s->tex[a]);
	}
	return true;
}

int armgl_init (int flags)
{
	int mode = (flags & ARMGL_F_DRY_BUF) ? ARMPI_DRM_DRY_BUF : (flags & ARMGL_F_DRY) ? ARMPI_DRM_DRY : 0;
#ifdef ARMGL_IRIS
	if (fscreen.screen)
		return mode ? ARMGL_DRY : soft_mode ? ARMGL_SOFT : ARMGL_GPU;
#else
	if (fscreen.screen)
		return mode ? ARMGL_DRY : ARMGL_GPU;
#endif
	int fd = armpi_drm_open (mode | (mode && (flags & ARMGL_F_V71) ? ARMPI_DRM_V71 : 0));
#ifdef ARMGL_IRIS
	/* pas de GPU Intel : softpipe, sauf si l'appelant a son propre rendu (ARMGL_F_NOSOFT, KORIN_GL=nosoft) */
	if (fd < 0 && !mode && !(flags & ARMGL_F_NOSOFT)) {
		const char *e = getenv ("KORIN_GL");
		if (e && !strcmp (e, "nosoft"))
			return ARMGL_NONE;
		driParseOptionInfo (&option_info, driconf, ARRAY_SIZE (driconf));
		struct pipe_screen *sp = softpipe_create_screen (korin_sw_winsys ());
		if (!sp)
			return ARMGL_NONE;
		soft_mode = 1;
		fscreen.screen = sp;
		fscreen.get_param = fs_get_param;
		fscreen.get_egl_image = fs_get_egl_image;
		fscreen.validate_egl_image = fs_validate_egl_image;
		return ARMGL_SOFT;
	}
#endif
	if (fd < 0)
		return ARMGL_NONE;
	driParseOptionInfo (&option_info, driconf, ARRAY_SIZE (driconf));
#ifdef ARMGL_IRIS
	driParseConfigFiles (&options, &option_info, &(driConfigFileParseParams) { .driverName = "iris" });
	struct pipe_screen_config config = { .options = &options, .options_info = &option_info };
	struct pipe_screen *screen = iris_screen_create (fd, &config);
#else
	driParseConfigFiles (&options, &option_info, &(driConfigFileParseParams) { .driverName = "v3d" });
	struct pipe_screen_config config = { .options = &options, .options_info = &option_info };
	struct pipe_screen *screen = v3d_screen_create (fd, &config, NULL);
#endif
	if (!screen)
		return ARMGL_NONE;
	fscreen.screen = screen;
	fscreen.get_param = fs_get_param;
	fscreen.get_egl_image = fs_get_egl_image;
	fscreen.validate_egl_image = fs_validate_egl_image;
	return mode ? ARMGL_DRY : ARMGL_GPU;
}

void *armgl_context (int major, int minor, void *share)
{
	struct st_context_attribs a;
	enum st_context_error err;
	memset (&a, 0, sizeof a);
	a.profile = API_OPENGLES2;
	a.major = major;
	a.minor = minor;
	a.visual.buffer_mask = ST_ATTACHMENT_BACK_LEFT_MASK | ST_ATTACHMENT_DEPTH_STENCIL_MASK;
	a.visual.color_format = COLOR;
	a.visual.depth_stencil_format = ZS;
	a.visual.samples = 1;
	return st_api_create_context (&fscreen, &a, &err, share);
}

void *armgl_surface (int width, int height)
{
	struct surface *s = CALLOC_STRUCT (surface);
	s->width = width;
	s->height = height;
	s->visual.buffer_mask = ST_ATTACHMENT_BACK_LEFT_MASK | ST_ATTACHMENT_DEPTH_STENCIL_MASK;
	s->visual.color_format = COLOR;
	s->visual.depth_stencil_format = ZS;
	s->visual.samples = 1;
	s->base.flush_front = surface_flush_front;
	s->base.validate = surface_validate;
	s->base.stamp = 1;
	s->base.ID = ++next_id;
	s->base.fscreen = &fscreen;
	s->base.visual = &s->visual;
	return s;
}

void *armgl_window (long win, int x, int y, int width, int height)
{
	struct surface *s = armgl_surface (width, height);
	struct pipe_screen *screen = fscreen.screen;
	unsigned names[3], pitch;
	s->window = armpi_drm_window (win, x, y, width, height, names, &pitch);
	if (!s->window) {
		FREE (s);
		return NULL;
	}
	int n = 2;
#ifdef ARMGL_IRIS
	n = s->nimg = armpi_drm_window_images (s->window);
#endif
	for (int i = 0; i < n; i++) {
		struct pipe_resource t;
		struct winsys_handle h;
		memset (&t, 0, sizeof t);
		memset (&h, 0, sizeof h);
		t.target = PIPE_TEXTURE_2D;
		t.format = COLOR;
		t.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
		t.width0 = width;
		t.height0 = height;
		t.depth0 = 1;
		t.array_size = 1;
		t.usage = PIPE_USAGE_DEFAULT;
		h.type = WINSYS_HANDLE_TYPE_SHARED;
		h.handle = names[i];
		h.stride = pitch;
		h.modifier = DRM_FORMAT_MOD_LINEAR;
		s->image[i] = screen->resource_from_handle (screen, &t, &h, 0);
		if (!s->image[i])
			return NULL;
	}
#ifdef ARMGL_IRIS
	presenter_start (s);
#endif
	return s;
}

/* a appeler apres avoir rendu un autre tampon courant */
void armgl_window_free (void *surface)
{
	struct surface *s = surface;
#ifdef ARMGL_IRIS
	presenter_stop (s);		/* (l'image en attente est montree d'abord) */
#endif
	st_api_destroy_drawable (&s->base);
	for (int i = 0; i < ST_ATTACHMENT_COUNT; i++)
		pipe_resource_reference (&s->tex[i], NULL);
	pipe_resource_reference (&s->image[0], NULL);
	pipe_resource_reference (&s->image[1], NULL);
	pipe_resource_reference (&s->image[2], NULL);
	armpi_drm_window_free (s->window);
	FREE (s);
}

void armgl_window_zone (void *surface, long win, int x, int y, int width, int height)
{
	struct surface *s = surface;
#ifdef ARMGL_IRIS
	presenter_drain (s);		/* (le thread montre l'image d'avant avec l'ancienne zone) */
#endif
	armpi_drm_zone (s->window, win, x, y, width, height);
}

void *armgl_buffers (void **px, int n, unsigned long bytes, int width, int height, int pitch)
{
	struct surface *s = armgl_surface (width, height);
	struct pipe_screen *screen = fscreen.screen;
	s->bytes = bytes;
	for (int i = 0; i < n; i++) {
		unsigned name;
		struct pipe_resource t;
		struct winsys_handle h;
		px[i] = s->buf[i] = armpi_drm_buffer (bytes, &name);
		if (!px[i])
			return NULL;
		memset (&t, 0, sizeof t);
		memset (&h, 0, sizeof h);
		t.target = PIPE_TEXTURE_2D;
		t.format = COLOR;
		t.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
		t.width0 = width;
		t.height0 = height;
		t.depth0 = 1;
		t.array_size = 1;
		t.usage = PIPE_USAGE_DEFAULT;
		h.type = WINSYS_HANDLE_TYPE_SHARED;
		h.handle = name;
		h.stride = pitch;
		h.modifier = DRM_FORMAT_MOD_LINEAR;
		s->image[i] = screen->resource_from_handle (screen, &t, &h, 0);
		if (!s->image[i])
			return NULL;
	}
	return s;
}

void armgl_buffer_bind (void *surface, int i)
{
	struct surface *s = surface;
	armpi_drm_flush (s->buf[i], s->bytes);
	s->back = i;
	p_atomic_inc (&s->base.stamp);
}

void armgl_buffer_done (void *surface)
{
	struct surface *s = surface;
	armgl_finish ();
	armpi_drm_flush (s->buf[s->back], s->bytes);
}

static long now_ns (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000000000L + t.tv_nsec;
}

#ifdef ARMGL_IRIS
#include <pthread.h>
#include <syslog.h>
#include <unistd.h>

/* L'echange sans glFinish (PC, 16/11) : une fenetre a trois images, une a l'ecran (chez Elegant, qui la lit
 * quand il compose, donc intouchable jusqu'a l'echange suivant), une que le GPU finit, une ou le programme
 * dessine. armgl_swap envoie les commandes avec une barriere et rend la main ; un thread par fenetre attend la
 * barriere puis donne l'image a Elegant (armpi_drm_present). Le processeur prepare donc l'image suivante pendant
 * que le GPU finit celle-ci ; armgl_swap n'attend que si l'image d'avant n'est pas encore montree (une image en
 * vol au plus). Une image est montree des que le GPU l'a finie : pas de retard d'une image, rien ne reste en
 * attente quand le programme cesse de dessiner. Repli sur glFinish a chaque image : softpipe, fenetre trop
 * grande pour trois images, ou /etc/armgl-sync (ARMGL_SYNC) pour comparer. */
struct presenter {
	pthread_t th;
	pthread_mutex_t mu;
	pthread_cond_t cv;
	int stop, err;
	int queued;			/* l'image a montrer, -1 : rien */
	int shown;			/* l'image a l'ecran, -1 : aucune encore */
	struct pipe_fence_handle *fence;	/* la barriere de l'image a montrer */
	long ns_gpu, ns_show;		/* (mesure : attente du GPU, attache chez Elegant, dans le thread) */
};

static void *present_main (void *arg)
{
	struct surface *s = arg;
	struct presenter *p = s->pr;
	struct pipe_screen *screen = fscreen.screen;
	pthread_mutex_lock (&p->mu);
	for (;;) {
		while (!p->stop && p->queued < 0)
			pthread_cond_wait (&p->cv, &p->mu);
		if (p->queued < 0)
			break;			/* (arret, plus rien a montrer) */
		int img = p->queued;
		struct pipe_fence_handle *f = p->fence;
		p->fence = NULL;
		pthread_mutex_unlock (&p->mu);
		long t0 = now_ns ();
		if (f) {
			screen->fence_finish (screen, NULL, f, OS_TIMEOUT_INFINITE);
			screen->fence_reference (screen, &f, NULL);
		}
		long t1 = now_ns ();
		int r = armpi_drm_present (s->window, img);
		long t2 = now_ns ();
		pthread_mutex_lock (&p->mu);
		p->ns_gpu += t1 - t0;
		p->ns_show += t2 - t1;
		if (r)
			p->err = 1;
		p->shown = img;
		p->queued = -1;
		pthread_cond_broadcast (&p->cv);
	}
	pthread_mutex_unlock (&p->mu);
	return NULL;
}

static void presenter_start (struct surface *s)
{
	if (s->nimg < 3 || soft_mode || getenv ("ARMGL_SYNC") || access ("/etc/armgl-sync", F_OK) == 0)
		return;
	struct presenter *p = CALLOC_STRUCT (presenter);
	if (!p)
		return;
	p->queued = p->shown = -1;
	pthread_mutex_init (&p->mu, NULL);
	pthread_cond_init (&p->cv, NULL);
	s->pr = p;
	if (pthread_create (&p->th, NULL, present_main, s)) {
		s->pr = NULL;
		pthread_cond_destroy (&p->cv);
		pthread_mutex_destroy (&p->mu);
		FREE (p);
	}
}

/* attend que l'image en vol soit montree */
static void presenter_drain (struct surface *s)
{
	struct presenter *p = s->pr;
	if (!p)
		return;
	pthread_mutex_lock (&p->mu);
	while (p->queued >= 0)
		pthread_cond_wait (&p->cv, &p->mu);
	pthread_mutex_unlock (&p->mu);
}

static void presenter_stop (struct surface *s)
{
	struct presenter *p = s->pr;
	if (!p)
		return;
	pthread_mutex_lock (&p->mu);
	p->stop = 1;
	pthread_cond_broadcast (&p->cv);
	pthread_mutex_unlock (&p->mu);
	pthread_join (p->th, NULL);
	pthread_cond_destroy (&p->cv);
	pthread_mutex_destroy (&p->mu);
	FREE (p);
	s->pr = NULL;
}

/* Mesure de l'echange d'images (PC), au journal toutes les 5 s quand /etc/armgl-stats existe (ou ARMGL_STATS).
 * Avec glFinish a chaque image : « appli » = du retour de l'echange precedent a l'appel de celui-ci (le travail
 * du programme et l'envoi des commandes), « GPU » = l'attente de glFinish, « affichage » = l'attache chez
 * Elegant ; les trois s'additionnent. Sans glFinish : « appli », « envoi » (les commandes au GPU), « attente »
 * (l'image d'avant pas encore montree) s'additionnent ; « GPU » et « affichage » se passent dans le thread,
 * pendant que le programme prepare l'image suivante. */
static void swap_stats (long t0, long t1, long t2, struct presenter *p)
{
	static int on = -1;
	static long last, since, n, app, a, b, pg, ps;
	if (on < 0)
		on = getenv ("ARMGL_STATS") || access ("/etc/armgl-stats", F_OK) == 0;
	if (!on)
		return;
	if (last) {
		app += t0 - last;
		a += t1 - t0;
		b += t2 - t1;
		n++;
	} else
		since = t2;
	last = t2;
	if (n && t2 - since >= 5000000000L) {
		double k = 1e6 * n;
		if (p) {
			pthread_mutex_lock (&p->mu);
			long g = p->ns_gpu - pg, s = p->ns_show - ps;
			pg = p->ns_gpu;
			ps = p->ns_show;
			pthread_mutex_unlock (&p->mu);
			syslog (LOG_INFO, "armgl : %.1f images/s (sans glFinish) ; par image, appli %.2f ms, envoi %.2f ms, "
				"attente %.2f ms ; thread : GPU %.2f ms, affichage %.2f ms\n", n * 1e9 / (t2 - since),
				app / k, a / k, b / k, g / k, s / k);
		} else
			syslog (LOG_INFO, "armgl : %.1f images/s ; par image, appli %.2f ms, GPU %.2f ms, affichage %.2f ms\n",
				n * 1e9 / (t2 - since), app / k, a / k, b / k);
		since = t2;
		n = app = a = b = 0;
	}
}

/* l'echange sans glFinish : la barriere et l'image au thread -> 0, -1 (une attache a echoue) */
static int swap_async (struct surface *s, struct st_context *st, long t0)
{
	struct presenter *p = s->pr;
	struct pipe_fence_handle *f = NULL;
	st_context_flush (st, ST_FLUSH_END_OF_FRAME, &f, NULL, NULL);
	long t1 = now_ns ();
	pthread_mutex_lock (&p->mu);
	while (p->queued >= 0)			/* une image en vol au plus */
		pthread_cond_wait (&p->cv, &p->mu);
	int err = p->err, shown = p->shown;
	p->err = 0;				/* (une attache ratee se dit une fois, comme avant) */
	p->queued = s->back;
	p->fence = f;
	pthread_cond_broadcast (&p->cv);
	pthread_mutex_unlock (&p->mu);
	long t2 = now_ns ();
	s->ns_finish += t2 - t0;
	s->frames++;
	swap_stats (t0, t1, t2, p);
	if (err)
		return -1;
	/* la suivante : ni celle que le thread va montrer, ni celle qui est a l'ecran */
	int next = 0;
	while (next == s->back || next == shown)
		next++;
	s->back = next;
	p_atomic_inc (&s->base.stamp);		/* GL redemande ses tampons : l'image libre */
	return 0;
}
#endif

int armgl_swap (void *surface)
{
	struct surface *s = surface;
	long t0 = now_ns ();
	/* le Z et le stencil de l'image finie ne servent plus : jetes avant la fin du job, comme le fait
	   le frontal DRI de Linux (__DRI2_FLUSH_INVALIDATE_ANCILLARY), sinon le GPU les ecrit en memoire a
	   chaque image (docs/SDL.md, le blocage du GPU avec profondeur) */
	struct st_context *st = st_api_get_current ();
	if (st && s->tex[ST_ATTACHMENT_DEPTH_STENCIL] && st->pipe->invalidate_resource)
		st->pipe->invalidate_resource (st->pipe, s->tex[ST_ATTACHMENT_DEPTH_STENCIL]);
#ifdef ARMGL_IRIS
	if (s->pr && st)
		return swap_async (s, st, t0);
#endif
	armgl_finish ();
	long t1 = now_ns ();
	s->ns_finish += t1 - t0;
	s->frames++;
	if (armpi_drm_present (s->window, s->back))
		return -1;
#ifdef ARMGL_IRIS
	swap_stats (t0, t1, now_ns (), NULL);
#endif
	s->back ^= 1;
	p_atomic_inc (&s->base.stamp);		/* GL redemande ses tampons : l'autre image */
	return 0;
}

void armgl_times (void *surface, double *finish, double *cache, double *attach)
{
	struct surface *s = surface;
	*finish = s->ns_finish / 1e6 / (s->frames ? s->frames : 1);
	armpi_drm_times (s->window, cache, attach);
}

void armgl_times_reset (void *surface)
{
	struct surface *s = surface;
	s->ns_finish = s->frames = 0;
	armpi_drm_times_reset (s->window);
}

int armgl_make_current (void *context, void *surface)
{
	struct pipe_frontend_drawable *d = surface;
	return st_api_make_current (context, d, d);
}

void *armgl_proc (const char *name)
{
	return (void *) _mesa_glapi_get_proc_address (name);
}

void armgl_finish (void)
{
	void (*finish) (void) = armgl_proc ("glFinish");
	finish ();
}

int armgl_read (void *surface, void *pixels, int pitch)
{
	struct surface *s = surface;
	struct pipe_resource *res = s->window ? s->image[s->back] : s->tex[ST_ATTACHMENT_BACK_LEFT];
	struct st_context *st = st_api_get_current ();
	if (!res || !st)
		return -1;
	armgl_finish ();
	struct pipe_context *pipe = st->pipe;
	struct pipe_transfer *t;
	struct pipe_box box;
	u_box_2d (0, 0, s->width, s->height, &box);
	const uint8_t *src = pipe->texture_map (pipe, res, 0, PIPE_MAP_READ, &box, &t);
	if (!src)
		return -1;
	for (int y = 0; y < s->height; y++) {
		/* (Mesa range les tampons d'une fenetre la ligne du haut d'abord) */
		const uint32_t *p = (const uint32_t *) (src + y * t->stride);
		memcpy ((uint8_t *) pixels + y * pitch, p, s->width * 4);
	}
	pipe->texture_unmap (pipe, t);
	return 0;
}

int armgl_lost (void)
{
	return armpi_drm_lost ();
}

long armgl_jobs (void)
{
	return armpi_drm_jobs ();
}

unsigned armgl_first_phys (void)
{
	return armpi_drm_first_phys ();
}
