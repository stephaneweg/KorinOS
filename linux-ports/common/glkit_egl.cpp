//
// glkit_egl.cpp -- GlKit (armgl, tools/mesa/armgl/armgl.h : OpenGL ES 3 sans EGL pour l'app) refait sur l'EGL du
// systeme (libglvnd : Mesa, ou le pilote NVIDIA) pour Linux.
//
// Ce qu'il faut a TerraCube : un contexte OpenGL ES 3.0 et des surfaces hors ecran RGBA8 avec profondeur et stencil
// (le « tampon par defaut » de GL quand elles sont courantes), relues par glReadPixels ou armgl_read. TerraCube rend
// hors ecran puis recopie l'image dans la fenetre (ui.surf, ui_update_all), comme sous Korin depuis sa 1.1.2 : il ne
// se sert pas de armgl_window. Les textes de UIKit sont dessines dans l'image avant la recopie : le HUD reste
// par-dessus la 3D.
//
//   display   EGL_PLATFORM_SURFACELESS_MESA (sans fenetre, sans serveur graphique), sinon EGL_DEFAULT_DISPLAY
//   surface   un pbuffer de la taille demandee ; s'il n'y en a pas (pilote sans pbuffer), un FBO (couleur +
//             profondeur/stencil) lie a armgl_make_current, sur un contexte sans surface (EGL_KHR_surfaceless_context)
//   mode      ARMGL_GPU, ou ARMGL_SOFT si le rendu est par le processeur (llvmpipe, softpipe :
//             LIBGL_ALWAYS_SOFTWARE=1 le force)
//
// armgl_window, armgl_buffers, armgl_import, armgl_image : pas faits (0).
//
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <armgl.h>
#include "korin_port.h"

namespace {

EGLDisplay g_dpy = EGL_NO_DISPLAY;
EGLConfig g_cfg;
bool g_pbuf;			// des pbuffers (sinon des FBO)
int g_mode = -1;
EGLContext g_first = EGL_NO_CONTEXT;	// (le contexte fait par armgl_init pour lire GL_RENDERER : rendu au premier armgl_context)
bool g_firstGiven;

struct Surf
{
	EGLSurface pb = EGL_NO_SURFACE;
	GLuint fbo = 0, rbc = 0, rbd = 0;
	EGLContext fboCtx = EGL_NO_CONTEXT;	// (un FBO appartient au contexte qui l'a fait)
	int w = 0, h = 0;
};
Surf *g_curSurf;
EGLContext g_curCtx = EGL_NO_CONTEXT;

bool verbose () { static int v = getenv ("KORIN_GL_VERBOSE") ? 1 : 0; return v; }

bool open_display ()
{
	if (g_dpy != EGL_NO_DISPLAY) return true;
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd = (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress ("eglGetPlatformDisplayEXT");
	const char *ext = eglQueryString (EGL_NO_DISPLAY, EGL_EXTENSIONS);
	if (gpd && ext && strstr (ext, "EGL_MESA_platform_surfaceless") && !getenv ("KORIN_GL_DEFAULT_DISPLAY"))
	{
		EGLDisplay d = gpd (EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, 0);
		EGLint ma, mi;
		if (d != EGL_NO_DISPLAY && eglInitialize (d, &ma, &mi)) g_dpy = d;
	}
	if (g_dpy == EGL_NO_DISPLAY)
	{
		EGLDisplay d = eglGetDisplay (EGL_DEFAULT_DISPLAY);
		EGLint ma, mi;
		if (d != EGL_NO_DISPLAY && eglInitialize (d, &ma, &mi)) g_dpy = d;
	}
	if (g_dpy == EGL_NO_DISPLAY) { if (verbose ()) fprintf (stderr, "glkit : pas d'affichage EGL\n"); return false; }
	if (!eglBindAPI (EGL_OPENGL_ES_API)) return false;
	EGLint n = 0;
	const EGLint pb[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE };
	if (eglChooseConfig (g_dpy, pb, &g_cfg, 1, &n) && n > 0) g_pbuf = true;
	else
	{
		const EGLint any[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
		if (!eglChooseConfig (g_dpy, any, &g_cfg, 1, &n) || n < 1)
		{
			// (EGL_KHR_no_config_context)
			g_cfg = (EGLConfig) 0;
		}
		g_pbuf = false;
	}
	if (verbose ()) fprintf (stderr, "glkit : EGL %s (%s), %s\n", eglQueryString (g_dpy, EGL_VENDOR), eglQueryString (g_dpy, EGL_VERSION),
		g_pbuf ? "pbuffers" : "FBO");
	return true;
}

EGLContext make_context (int major, int minor, EGLContext share)
{
	const EGLint at[] = { EGL_CONTEXT_MAJOR_VERSION, major, EGL_CONTEXT_MINOR_VERSION, minor, EGL_NONE };
	return eglCreateContext (g_dpy, g_cfg, share, at);
}

bool bind_fbo (Surf *s, EGLContext ctx)
{
	if (s->fbo && s->fboCtx == ctx) { glBindFramebuffer (GL_FRAMEBUFFER, s->fbo); return true; }
	if (s->fbo)
	{
		// (un autre contexte : le FBO de l'ancien n'y est pas ; on en fait un neuf, l'image n'est pas gardee)
		s->fbo = s->rbc = s->rbd = 0;
	}
	glGenFramebuffers (1, &s->fbo);
	glGenRenderbuffers (1, &s->rbc);
	glGenRenderbuffers (1, &s->rbd);
	glBindRenderbuffer (GL_RENDERBUFFER, s->rbc);
	glRenderbufferStorage (GL_RENDERBUFFER, GL_RGBA8, s->w, s->h);
	glBindRenderbuffer (GL_RENDERBUFFER, s->rbd);
	glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, s->w, s->h);
	glBindFramebuffer (GL_FRAMEBUFFER, s->fbo);
	glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, s->rbc);
	glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, s->rbd);
	s->fboCtx = ctx;
	return glCheckFramebufferStatus (GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

// (uikit_gtk.cpp, apres un tour de GTK : le contexte de l'app redevient courant)
void restore_current ()
{
	if (g_curCtx == EGL_NO_CONTEXT) return;
	if (g_curSurf && g_curSurf->pb != EGL_NO_SURFACE) eglMakeCurrent (g_dpy, g_curSurf->pb, g_curSurf->pb, g_curCtx);
	else
	{
		eglMakeCurrent (g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, g_curCtx);
		if (g_curSurf) glBindFramebuffer (GL_FRAMEBUFFER, g_curSurf->fbo);
	}
}

}

extern "C" {

int armgl_init (int flags)
{
	if (g_mode >= 0) return g_mode;
	g_mode = ARMGL_NONE;
	if (!open_display ()) return g_mode;
	// (le rendu : GPU ou processeur ? un contexte d'essai, rendu ensuite par le premier armgl_context)
	EGLContext c = make_context (3, 0, EGL_NO_CONTEXT);
	if (c == EGL_NO_CONTEXT) { if (verbose ()) fprintf (stderr, "glkit : pas de contexte OpenGL ES 3.0 (0x%x)\n", eglGetError ()); return g_mode; }
	if (!eglMakeCurrent (g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, c))
	{
		// (sans EGL_KHR_surfaceless_context : un petit pbuffer)
		const EGLint a[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
		EGLSurface s = g_pbuf ? eglCreatePbufferSurface (g_dpy, g_cfg, a) : EGL_NO_SURFACE;
		if (s == EGL_NO_SURFACE || !eglMakeCurrent (g_dpy, s, s, c)) { eglDestroyContext (g_dpy, c); return g_mode; }
	}
	const char *r = (const char *) glGetString (GL_RENDERER);
	bool soft = r && (strstr (r, "llvmpipe") || strstr (r, "softpipe") || strstr (r, "lavapipe") || strstr (r, "Software"));
	if (verbose ()) fprintf (stderr, "glkit : %s (%s)\n", r ? r : "?", (const char *) glGetString (GL_VERSION));
	eglMakeCurrent (g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	g_first = c;
	g_mode = soft ? ((flags & ARMGL_F_NOSOFT) ? ARMGL_NONE : ARMGL_SOFT) : ARMGL_GPU;
	return g_mode;
}

void *armgl_context (int major, int minor, void *share)
{
	if (armgl_init (0) == ARMGL_NONE) return 0;
	if (!g_firstGiven && !share && major == 3 && minor == 0 && g_first != EGL_NO_CONTEXT) { g_firstGiven = true; return g_first; }
	EGLContext c = make_context (major, minor, share ? (EGLContext) share : EGL_NO_CONTEXT);
	return c == EGL_NO_CONTEXT ? 0 : c;
}

void *armgl_surface (int width, int height)
{
	if (armgl_init (0) == ARMGL_NONE || width < 1 || height < 1) return 0;
	Surf *s = new Surf;
	s->w = width; s->h = height;
	if (g_pbuf)
	{
		const EGLint a[] = { EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE };
		s->pb = eglCreatePbufferSurface (g_dpy, g_cfg, a);
		if (s->pb == EGL_NO_SURFACE) { delete s; return 0; }
	}
	return s;
}

int armgl_make_current (void *context, void *surface)
{
	Surf *s = (Surf *) surface;
	EGLContext c = (EGLContext) context;
	if (!c || g_dpy == EGL_NO_DISPLAY) return 0;
	bool ok;
	if (s && s->pb != EGL_NO_SURFACE) ok = eglMakeCurrent (g_dpy, s->pb, s->pb, c);
	else
	{
		ok = eglMakeCurrent (g_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
		if (ok && s) ok = bind_fbo (s, c);
	}
	if (!ok) return 0;
	g_curSurf = s; g_curCtx = c;
	kport_gl_restore = restore_current;
	if (s) glViewport (0, 0, s->w, s->h);
	return 1;
}

int armgl_swap (void *) { glFlush (); return 1; }
void armgl_finish (void) { glFinish (); }
void *armgl_proc (const char *name) { return (void *) eglGetProcAddress (name); }
int armgl_lost (void) { return 0; }
long armgl_jobs (void) { return 0; }
unsigned armgl_first_phys (void) { return 0; }
void armgl_times (void *, double *a, double *b, double *c) { if (a) *a = 0; if (b) *b = 0; if (c) *c = 0; }
void armgl_times_reset (void *) {}

// la surface, ligne du haut d'abord, en 0xAARRGGBB ; elle doit etre courante (ou le devient le temps de la lecture)
int armgl_read (void *surface, void *pixels, int pitch)
{
	Surf *s = (Surf *) surface;
	if (!s || !pixels) return -1;
	Surf *was = g_curSurf;
	if (was != s && g_curCtx != EGL_NO_CONTEXT) armgl_make_current (g_curCtx, s);
	uint8_t *tmp = (uint8_t *) malloc ((size_t) s->w * s->h * 4);
	if (!tmp) return -1;
	glPixelStorei (GL_PACK_ALIGNMENT, 4);
	glReadPixels (0, 0, s->w, s->h, GL_RGBA, GL_UNSIGNED_BYTE, tmp);
	for (int y = 0; y < s->h; y++)
	{
		const uint8_t *p = tmp + (size_t) (s->h - 1 - y) * s->w * 4;
		uint32_t *d = (uint32_t *) ((uint8_t *) pixels + (size_t) y * pitch);
		for (int x = 0; x < s->w; x++, p += 4) d[x] = (uint32_t) p[3] << 24 | (uint32_t) p[0] << 16 | (uint32_t) p[1] << 8 | p[2];
	}
	free (tmp);
	if (was != s && was && g_curCtx != EGL_NO_CONTEXT) armgl_make_current (g_curCtx, was);
	return 0;
}

void *armgl_window (long, int, int, int, int) { return 0; }
void armgl_window_free (void *) {}
void armgl_window_zone (void *, long, int, int, int, int) {}
void *armgl_buffers (void **, int, unsigned long, int, int, int) { return 0; }
void armgl_buffer_bind (void *, int) {}
void armgl_buffer_done (void *) {}
unsigned armgl_import (void *, unsigned, unsigned) { return 0; }
void *armgl_image (unsigned, int, int, int) { return 0; }
void armgl_image_free (void *) {}

}
