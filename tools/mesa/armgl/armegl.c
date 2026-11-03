/*
 * armegl.c -- le plus petit EGL qui suffise au backend GL d'ANGLE (src/libANGLE/renderer/gl/egl), sur armgl.
 *
 * Un seul ecran, une seule configuration. Rien n'est exporte sous le nom egl* : dans un meme programme
 * statique, ANGLE a son propre EGL. Tout passe par armegl_get_proc, l'equivalent de eglGetProcAddress.
 * Fenetres : pas ici, WebGL rend dans des FBO. Une pbuffer est une surface armgl hors ecran.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "armgl.h"

typedef int EGLint;
typedef unsigned EGLBoolean;
typedef unsigned EGLenum;
typedef void *EGLDisplay, *EGLConfig, *EGLContext, *EGLSurface;

#define EGL_TRUE		1
#define EGL_FALSE		0
#define EGL_SUCCESS		0x3000
#define EGL_BAD_ALLOC		0x3003
#define EGL_BAD_ATTRIBUTE	0x3004
#define EGL_BAD_PARAMETER	0x300C
#define EGL_BUFFER_SIZE		0x3020
#define EGL_ALPHA_SIZE		0x3021
#define EGL_BLUE_SIZE		0x3022
#define EGL_GREEN_SIZE		0x3023
#define EGL_RED_SIZE		0x3024
#define EGL_DEPTH_SIZE		0x3025
#define EGL_STENCIL_SIZE	0x3026
#define EGL_CONFIG_CAVEAT	0x3027
#define EGL_CONFIG_ID		0x3028
#define EGL_LEVEL		0x3029
#define EGL_MAX_PBUFFER_HEIGHT	0x302A
#define EGL_MAX_PBUFFER_PIXELS	0x302B
#define EGL_MAX_PBUFFER_WIDTH	0x302C
#define EGL_NATIVE_RENDERABLE	0x302D
#define EGL_NATIVE_VISUAL_ID	0x302E
#define EGL_NATIVE_VISUAL_TYPE	0x302F
#define EGL_SAMPLES		0x3031
#define EGL_SAMPLE_BUFFERS	0x3032
#define EGL_SURFACE_TYPE	0x3033
#define EGL_TRANSPARENT_TYPE	0x3034
#define EGL_NONE		0x3038
#define EGL_BIND_TO_TEXTURE_RGB	0x3039
#define EGL_BIND_TO_TEXTURE_RGBA 0x303A
#define EGL_MIN_SWAP_INTERVAL	0x303B
#define EGL_MAX_SWAP_INTERVAL	0x303C
#define EGL_LUMINANCE_SIZE	0x303D
#define EGL_ALPHA_MASK_SIZE	0x303E
#define EGL_COLOR_BUFFER_TYPE	0x303F
#define EGL_RENDERABLE_TYPE	0x3040
#define EGL_CONFORMANT		0x3042
#define EGL_RGB_BUFFER		0x308E
#define EGL_VENDOR		0x3053
#define EGL_VERSION		0x3054
#define EGL_EXTENSIONS		0x3055
#define EGL_CLIENT_APIS		0x308D
#define EGL_HEIGHT		0x3056
#define EGL_WIDTH		0x3057
#define EGL_PBUFFER_BIT		0x0001
#define EGL_OPENGL_ES2_BIT	0x0004
#define EGL_OPENGL_ES3_BIT	0x0040
#define EGL_OPENGL_ES_API	0x30A0
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_CONTEXT_MINOR_VERSION 0x30FB
#define EGL_DRAW		0x3059
#define EGL_LINUX_DMA_BUF_EXT	0x3270
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#define EGL_READ		0x305A

static int err = EGL_SUCCESS;
static void *cur_ctx, *cur_surf;
static const char display_tag;
#define THE_DISPLAY	((EGLDisplay) &display_tag)
static const char config_tag;
#define THE_CONFIG	((EGLConfig) &config_tag)

struct pbuf {
	void *surface;
	int w, h;
};

static EGLint config_value (EGLint a)
{
	switch (a) {
	case EGL_BUFFER_SIZE: return 32;
	case EGL_RED_SIZE: case EGL_GREEN_SIZE: case EGL_BLUE_SIZE: case EGL_ALPHA_SIZE: return 8;
	case EGL_DEPTH_SIZE: return 24;
	case EGL_STENCIL_SIZE: return 8;
	case EGL_CONFIG_ID: return 1;
	case EGL_SURFACE_TYPE: return EGL_PBUFFER_BIT;
	case EGL_RENDERABLE_TYPE: case EGL_CONFORMANT: return EGL_OPENGL_ES2_BIT | EGL_OPENGL_ES3_BIT;
	case EGL_COLOR_BUFFER_TYPE: return EGL_RGB_BUFFER;
	case EGL_CONFIG_CAVEAT: case EGL_TRANSPARENT_TYPE: return EGL_NONE;
	case EGL_MAX_PBUFFER_WIDTH: case EGL_MAX_PBUFFER_HEIGHT: return 4096;
	case EGL_MAX_PBUFFER_PIXELS: return 4096 * 4096;
	case EGL_MIN_SWAP_INTERVAL: case EGL_MAX_SWAP_INTERVAL: return 1;
	default: return 0;
	}
}

static EGLint egl_GetError (void) { EGLint e = err; err = EGL_SUCCESS; return e; }
static EGLDisplay egl_GetDisplay (void *native) { (void) native; return THE_DISPLAY; }
static EGLBoolean egl_Initialize (EGLDisplay d, EGLint *major, EGLint *minor)
{
	(void) d;
	if (major) *major = 1;
	if (minor) *minor = 4;
	/* essais de WebKit sous QEMU : a blanc si ce fichier existe */
	return armgl_init (access ("SD0:/etc/armgl_dry", 0) ? 0 : ARMGL_F_DRY) == ARMGL_NONE ? EGL_FALSE : EGL_TRUE;
}
static EGLBoolean egl_Terminate (EGLDisplay d) { (void) d; return EGL_TRUE; }
static const char *egl_QueryString (EGLDisplay d, EGLint name)
{
	(void) d;
	switch (name) {
	case EGL_VENDOR: return "ARMPI";
	case EGL_VERSION: return "1.4 armgl";
	case EGL_CLIENT_APIS: return "OpenGL_ES";
	case EGL_EXTENSIONS: return "EGL_KHR_surfaceless_context EGL_KHR_create_context EGL_KHR_image EGL_KHR_image_base EGL_EXT_image_dma_buf_import";
	default: return NULL;
	}
}
static EGLBoolean egl_BindAPI (EGLenum api) { return api == EGL_OPENGL_ES_API; }
static EGLBoolean egl_GetConfigs (EGLDisplay d, EGLConfig *c, EGLint size, EGLint *n)
{
	(void) d;
	if (c && size > 0) c[0] = THE_CONFIG;
	*n = 1;
	return EGL_TRUE;
}
static EGLBoolean egl_ChooseConfig (EGLDisplay d, const EGLint *attrs, EGLConfig *c, EGLint size, EGLint *n)
{
	(void) d; (void) attrs;
	return egl_GetConfigs (d, c, size, n);
}
static EGLBoolean egl_GetConfigAttrib (EGLDisplay d, EGLConfig c, EGLint a, EGLint *v)
{
	(void) d; (void) c;
	*v = config_value (a);
	return EGL_TRUE;
}
static EGLContext egl_CreateContext (EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attrs)
{
	int major = 3, minor = 0;
	(void) d; (void) c;
	for (; attrs && *attrs != EGL_NONE; attrs += 2) {
		if (attrs[0] == EGL_CONTEXT_MAJOR_VERSION) major = attrs[1];
		if (attrs[0] == EGL_CONTEXT_MINOR_VERSION) minor = attrs[1];
	}
	void *ctx = armgl_context (major, minor, share);
	if (!ctx)
		err = EGL_BAD_ATTRIBUTE;
	return ctx;
}
static EGLBoolean egl_DestroyContext (EGLDisplay d, EGLContext c) { (void) d; (void) c; return EGL_TRUE; }
static EGLSurface egl_CreatePbufferSurface (EGLDisplay d, EGLConfig c, const EGLint *attrs)
{
	struct pbuf *p = calloc (1, sizeof *p);
	(void) d; (void) c;
	p->w = p->h = 1;
	for (; attrs && *attrs != EGL_NONE; attrs += 2) {
		if (attrs[0] == EGL_WIDTH) p->w = attrs[1] > 0 ? attrs[1] : 1;
		if (attrs[0] == EGL_HEIGHT) p->h = attrs[1] > 0 ? attrs[1] : 1;
	}
	p->surface = armgl_surface (p->w, p->h);
	return p;
}
static EGLSurface egl_CreateWindowSurface (EGLDisplay d, EGLConfig c, void *win, const EGLint *attrs)
{
	(void) d; (void) c; (void) win; (void) attrs;
	err = EGL_BAD_PARAMETER;
	return NULL;
}
static EGLBoolean egl_DestroySurface (EGLDisplay d, EGLSurface s) { (void) d; free (s); return EGL_TRUE; }
static EGLBoolean egl_MakeCurrent (EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext c)
{
	struct pbuf *p = draw;
	(void) d; (void) read;
	if (!armgl_make_current (c, p ? p->surface : NULL))
		return EGL_FALSE;
	cur_ctx = c;
	cur_surf = draw;
	return EGL_TRUE;
}
static EGLContext egl_GetCurrentContext (void) { return cur_ctx; }
static EGLSurface egl_GetCurrentSurface (EGLint which) { (void) which; return cur_surf; }
static EGLBoolean egl_QuerySurface (EGLDisplay d, EGLSurface s, EGLint a, EGLint *v)
{
	struct pbuf *p = s;
	(void) d;
	*v = a == EGL_WIDTH ? p->w : a == EGL_HEIGHT ? p->h : 0;
	return EGL_TRUE;
}
static EGLBoolean egl_SwapBuffers (EGLDisplay d, EGLSurface s) { (void) d; (void) s; armgl_finish (); return EGL_TRUE; }
static EGLBoolean egl_SwapInterval (EGLDisplay d, EGLint i) { (void) d; (void) i; return EGL_TRUE; }
static EGLBoolean egl_SurfaceAttrib (EGLDisplay d, EGLSurface s, EGLint a, EGLint v)
{
	(void) d; (void) s; (void) a; (void) v;
	return EGL_TRUE;
}
static EGLBoolean egl_BindTexImage (EGLDisplay d, EGLSurface s, EGLint b)
{
	(void) d; (void) s; (void) b;
	err = EGL_BAD_PARAMETER;
	return EGL_FALSE;
}
static EGLBoolean egl_ReleaseTexImage (EGLDisplay d, EGLSurface s, EGLint b)
{
	(void) d; (void) s; (void) b;
	return EGL_TRUE;
}

/* le fd d'un dma-buf est ici le nom rendu par armgl_import */
static void *egl_CreateImageKHR (EGLDisplay d, EGLContext c, EGLenum target, void *buffer, const EGLint *attrs)
{
	int w = 0, h = 0, name = 0, pitch = 0;
	(void) d; (void) c; (void) buffer;
	if (target != EGL_LINUX_DMA_BUF_EXT) {
		err = EGL_BAD_PARAMETER;
		return NULL;
	}
	for (; attrs && *attrs != EGL_NONE; attrs += 2) {
		if (attrs[0] == EGL_WIDTH) w = attrs[1];
		if (attrs[0] == EGL_HEIGHT) h = attrs[1];
		if (attrs[0] == EGL_DMA_BUF_PLANE0_FD_EXT) name = attrs[1];
		if (attrs[0] == EGL_DMA_BUF_PLANE0_PITCH_EXT) pitch = attrs[1];
	}
	void *i = armgl_image ((unsigned) name, w, h, pitch);
	if (!i)
		err = EGL_BAD_ALLOC;
	return i;
}
static EGLBoolean egl_DestroyImageKHR (EGLDisplay d, void *image)
{
	(void) d;
	armgl_image_free (image);
	return EGL_TRUE;
}

void *armegl_get_proc (const char *name)
{
	static const struct { const char *name; void *fn; } table[] = {
#define E(n) { "egl" #n, (void *) egl_##n }
		E (GetError), E (GetDisplay), E (Initialize), E (Terminate), E (QueryString), E (BindAPI),
		E (GetConfigs), E (ChooseConfig), E (GetConfigAttrib), E (CreateContext), E (DestroyContext),
		E (CreatePbufferSurface), E (CreateWindowSurface), E (DestroySurface), E (MakeCurrent),
		E (GetCurrentContext), E (GetCurrentSurface), E (QuerySurface), E (SwapBuffers), E (SwapInterval),
		E (SurfaceAttrib), E (BindTexImage), E (ReleaseTexImage), E (CreateImageKHR), E (DestroyImageKHR),
		{ "eglGetProcAddress", (void *) armegl_get_proc },
	};
	if (name[0] == 'e' && name[1] == 'g' && name[2] == 'l') {
		for (unsigned i = 0; i < sizeof table / sizeof table[0]; i++)
			if (!strcmp (table[i].name, name))
				return table[i].fn;
		return NULL;
	}
	return armgl_proc (name);
}
