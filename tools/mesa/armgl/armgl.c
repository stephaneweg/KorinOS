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
#include "v3d/v3d_screen.h"
#include "armpi_drm.h"
#include "armgl.h"

#define COLOR	PIPE_FORMAT_B8G8R8A8_UNORM	/* en memoire : 0xAARRGGBB, comme une surface d'Elegant */
#define ZS	PIPE_FORMAT_S8_UINT_Z24_UNORM	/* le D24S8 du V3D ; Z24_UNORM_S8_UINT n'est pas dans sa table : le GPU bloquait a la premiere ecriture du Z (docs/SDL.md) */

static const driOptionDescription driconf[] = {
#include "pipe-loader/driinfo_gallium.h"
#include "v3d/driinfo_v3d.h"
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
};

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
	h.modifier = DRM_FORMAT_MOD_BROADCOM_UIF;
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
	if (fscreen.screen)
		return mode ? ARMGL_DRY : ARMGL_GPU;
	int fd = armpi_drm_open (mode | (mode && (flags & ARMGL_F_V71) ? ARMPI_DRM_V71 : 0));
	if (fd < 0)
		return ARMGL_NONE;
	driParseOptionInfo (&option_info, driconf, ARRAY_SIZE (driconf));
	driParseConfigFiles (&options, &option_info, &(driConfigFileParseParams) { .driverName = "v3d" });
	struct pipe_screen_config config = { .options = &options, .options_info = &option_info };
	struct pipe_screen *screen = v3d_screen_create (fd, &config, NULL);
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
	unsigned names[2], pitch;
	s->window = armpi_drm_window (win, x, y, width, height, names, &pitch);
	if (!s->window) {
		FREE (s);
		return NULL;
	}
	for (int i = 0; i < 2; i++) {
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
	return s;
}

/* a appeler apres avoir rendu un autre tampon courant */
void armgl_window_free (void *surface)
{
	struct surface *s = surface;
	st_api_destroy_drawable (&s->base);
	for (int i = 0; i < ST_ATTACHMENT_COUNT; i++)
		pipe_resource_reference (&s->tex[i], NULL);
	pipe_resource_reference (&s->image[0], NULL);
	pipe_resource_reference (&s->image[1], NULL);
	armpi_drm_window_free (s->window);
	FREE (s);
}

void armgl_window_zone (void *surface, long win, int x, int y, int width, int height)
{
	struct surface *s = surface;
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
	armgl_finish ();
	s->ns_finish += now_ns () - t0;
	s->frames++;
	if (armpi_drm_present (s->window, s->back))
		return -1;
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
