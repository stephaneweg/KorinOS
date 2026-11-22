/*
 * gpucomp_gl.c -- le GPU de kapi d'Onyx (gpu_info, gpu_texture, gpu_texture_rect, gpu_render, gpu_vbuf : les
 * paliers v53-v72) sur OpenGL ES 3 par GlKit, pour Korin PC (docs/WEBKIT.md, « Le compositeur sur le GPU du PC ») :
 * le gpucomp d'Onyx (src/extra/onyx-libs/gpucomp/gpucomp.c, compile tel quel) compose alors les couches de WebKit
 * (LayerTreeHostOnyx) sur le GPU Intel (Mesa iris) au lieu du processeur. Le Pi garde gpucomp_armpi.c (GpuKit,
 * le V3D).
 *
 * Ce que gpucomp demande (gpc_gpu_layer, gpc_flush) : des triangles en coordonnees normalisees (KAPI_GPU_B_NOMATRIX
 * toujours), une texture par lot, les coordonnees de texture dans 0..1 (ligne 0 en haut), la couleur du sommet =
 * l'opacite (multiplie le texel premultiplie), les melanges premultiplies de kapi v72, la cible : des pixels
 * 0xAARRGGBB (octets B, G, R, A) avec leur pas, effacee a `clear` ou gardee (KAPI_GPU_F_KEEP).
 *
 * Ici : une texture GL par poignee, les octets B, G, R, A televerses tels quels comme R, G, B, A (le melange ne
 * regarde que l'alpha, a sa place : rien a permuter), une cible hors ecran (un FBO de la taille de la partie
 * composee) relue dans les pixels de la cible (glReadPixels, GL_PACK_ROW_LENGTH = son pas) : la ligne 0 du FBO est
 * le haut de la cible (y retourne dans le nuanceur de sommets).
 *
 * Le contexte : le sien (armgl_context), dans le process web et son fil principal, celui de WebGL (ANGLE sur
 * armegl) ; celui d'ANGLE est remis courant apres chaque appel (eglGetCurrentContext / eglMakeCurrent d'armegl),
 * ANGLE ne voit rien. Pas de GPU Intel (QEMU) : pas de GPU pour gpucomp (il compose sur le processeur), sauf
 * /etc/web-gpu-soft ou KORIN_GPUCOMP=soft : softpipe, pour les essais.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "appkit/appkit.h"

/* GlKit (tools/mesa/armgl/armgl.h) */
#define ARMGL_NONE	0
#define ARMGL_GPU	1
#define ARMGL_SOFT	3
int armgl_init (int flags);
void *armgl_context (int major, int minor, void *share);
void *armgl_surface (int width, int height);
int armgl_make_current (void *context, void *surface);
void *armgl_proc (const char *name);
int armgl_lost (void);
void *armegl_get_proc (const char *name);

/* ---- OpenGL ES 3 : ce qui sert ici ---- */
typedef unsigned GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef long GLsizeiptr;
typedef char GLchar;
#define GL_TEXTURE_2D			0x0DE1
#define GL_RGBA				0x1908
#define GL_RGBA8			0x8058
#define GL_UNSIGNED_BYTE		0x1401
#define GL_FLOAT			0x1406
#define GL_TEXTURE_MIN_FILTER		0x2801
#define GL_TEXTURE_MAG_FILTER		0x2800
#define GL_TEXTURE_WRAP_S		0x2802
#define GL_TEXTURE_WRAP_T		0x2803
#define GL_NEAREST			0x2600
#define GL_LINEAR			0x2601
#define GL_CLAMP_TO_EDGE		0x812F
#define GL_REPEAT			0x2901
#define GL_UNPACK_ROW_LENGTH		0x0CF2
#define GL_PACK_ROW_LENGTH		0x0D02
#define GL_UNPACK_ALIGNMENT		0x0CF5
#define GL_PACK_ALIGNMENT		0x0D05
#define GL_FRAMEBUFFER			0x8D40
#define GL_COLOR_ATTACHMENT0		0x8CE0
#define GL_FRAMEBUFFER_COMPLETE		0x8CD5
#define GL_ARRAY_BUFFER			0x8892
#define GL_STREAM_DRAW			0x88E0
#define GL_TRIANGLES			0x0004
#define GL_BLEND			0x0BE2
#define GL_DEPTH_TEST			0x0B71
#define GL_SCISSOR_TEST			0x0C11
#define GL_CULL_FACE			0x0B44
#define GL_COLOR_BUFFER_BIT		0x4000
#define GL_ZERO				0
#define GL_ONE				1
#define GL_SRC_COLOR			0x0300
#define GL_ONE_MINUS_SRC_COLOR		0x0301
#define GL_SRC_ALPHA			0x0302
#define GL_ONE_MINUS_SRC_ALPHA		0x0303
#define GL_DST_ALPHA			0x0304
#define GL_ONE_MINUS_DST_ALPHA		0x0305
#define GL_DST_COLOR			0x0306
#define GL_FUNC_ADD			0x8006
#define GL_MAX				0x8008
#define GL_FUNC_REVERSE_SUBTRACT	0x800B
#define GL_VERTEX_SHADER		0x8B31
#define GL_FRAGMENT_SHADER		0x8B30
#define GL_COMPILE_STATUS		0x8B81
#define GL_LINK_STATUS			0x8B82
#define GL_RENDERER			0x1F01
#define GL_NO_ERROR			0

#define GLF(ret, name, args) static ret (*name) args
GLF (void, glGenTextures, (GLsizei, GLuint *));
GLF (void, glDeleteTextures, (GLsizei, const GLuint *));
GLF (void, glBindTexture, (GLenum, GLuint));
GLF (void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
GLF (void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *));
GLF (void, glTexParameteri, (GLenum, GLenum, GLint));
GLF (void, glPixelStorei, (GLenum, GLint));
GLF (void, glGenFramebuffers, (GLsizei, GLuint *));
GLF (void, glBindFramebuffer, (GLenum, GLuint));
GLF (void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint));
GLF (GLenum, glCheckFramebufferStatus, (GLenum));
GLF (void, glViewport, (GLint, GLint, GLsizei, GLsizei));
GLF (void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
GLF (void, glClear, (GLbitfield));
GLF (void, glEnable, (GLenum));
GLF (void, glDisable, (GLenum));
GLF (void, glBlendFunc, (GLenum, GLenum));
GLF (void, glBlendEquation, (GLenum));
GLF (void, glGenBuffers, (GLsizei, GLuint *));
GLF (void, glBindBuffer, (GLenum, GLuint));
GLF (void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum));
GLF (void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *));
GLF (void, glEnableVertexAttribArray, (GLuint));
GLF (void, glDrawArrays, (GLenum, GLint, GLsizei));
GLF (void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
GLF (GLuint, glCreateShader, (GLenum));
GLF (void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *));
GLF (void, glCompileShader, (GLuint));
GLF (void, glGetShaderiv, (GLuint, GLenum, GLint *));
GLF (GLuint, glCreateProgram, (void));
GLF (void, glAttachShader, (GLuint, GLuint));
GLF (void, glBindAttribLocation, (GLuint, GLuint, const GLchar *));
GLF (void, glLinkProgram, (GLuint));
GLF (void, glGetProgramiv, (GLuint, GLenum, GLint *));
GLF (void, glUseProgram, (GLuint));
GLF (GLint, glGetUniformLocation, (GLuint, const GLchar *));
GLF (void, glUniform1i, (GLint, GLint));
GLF (void, glActiveTexture, (GLenum));
GLF (GLenum, glGetError, (void));
GLF (const unsigned char *, glGetString, (GLenum));

/* armegl : le contexte d'ANGLE, remis courant apres chaque appel */
static void *(*egl_GetCurrentContext) (void);
static void *(*egl_GetCurrentSurface) (int);
static unsigned (*egl_MakeCurrent) (void *, void *, void *, void *);
#define EGL_DRAW 0x3059

static int s_state;			/* 0 pas essaye, 1 pret, -1 refuse */
static char s_info[80];
static void *s_ctx, *s_surface;
static GLuint s_program, s_vbo, s_fbo, s_fboTex;
static int s_fboW, s_fboH;
static void *s_savedCtx, *s_savedSurface;

#define MAX_TEX	16384
static GLuint s_tex[MAX_TEX];		/* poignee -> texture GL (0 : libre) */
static unsigned short s_texFlags[MAX_TEX];	/* le filtre et le repli posés (pour ne pas les reposer a chaque lot) */

static void info (const char *s) { snprintf (s_info, sizeof s_info, "%s", s); }

/* ---- le temps de la composition par le GPU (22/11/2026 : 15 à 45 ms par image sur le ZBook, le processeur fait la
 * même en 1 ms) : avec ONYX_WEB_GPU_DEBUG ou /etc/web-gpu-debug, une ligne toutes les 2 s : compositions, dessin (jusqu'avant la relecture),
 * relecture (glReadPixels : elle attend le GPU), envois de textures, changements de contexte (ANGLE courant) */
static int s_timesOn = -1;
static double s_tDraw, s_tRead, s_tUpload, s_tEnter, s_since;
static unsigned s_nRender, s_nUpload, s_nSwitch, s_pxRead;

static double now_ms (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void times_log (void)
{
	if (s_timesOn < 0) {
		const char *e = getenv ("ONYX_WEB_GPU_DEBUG");
		s_timesOn = (e && *e && *e != '0') || access ("/etc/web-gpu-debug", F_OK) == 0;
	}
	if (!s_timesOn)
		return;
	double t = now_ms ();
	if (!s_since)
		s_since = t;
	if (t - s_since < 2000)
		return;
	fprintf (stderr, "web: gpu: gl: %.1f s: %u compositions (dessin %.1f ms, relecture %.1f ms pour %u kpx), %u envois (%.1f ms), "
		 "contexte pris %.1f ms (%u fois depuis ANGLE)\n", (t - s_since) / 1e3, s_nRender, s_tDraw, s_tRead, s_pxRead / 1000,
		 s_nUpload, s_tUpload, s_tEnter, s_nSwitch);
	s_since = t;
	s_tDraw = s_tRead = s_tUpload = s_tEnter = 0;
	s_nRender = s_nUpload = s_nSwitch = s_pxRead = 0;
}

static int load (void)
{
#define L(n) if (!(*(void **) &n = armgl_proc (#n))) return 0
	L (glGenTextures); L (glDeleteTextures); L (glBindTexture); L (glTexImage2D); L (glTexSubImage2D);
	L (glTexParameteri); L (glPixelStorei); L (glGenFramebuffers); L (glBindFramebuffer); L (glFramebufferTexture2D);
	L (glCheckFramebufferStatus); L (glViewport); L (glClearColor); L (glClear); L (glEnable); L (glDisable);
	L (glBlendFunc); L (glBlendEquation); L (glGenBuffers); L (glBindBuffer); L (glBufferData);
	L (glVertexAttribPointer); L (glEnableVertexAttribArray); L (glDrawArrays); L (glReadPixels);
	L (glCreateShader); L (glShaderSource); L (glCompileShader); L (glGetShaderiv); L (glCreateProgram);
	L (glAttachShader); L (glBindAttribLocation); L (glLinkProgram); L (glGetProgramiv); L (glUseProgram);
	L (glGetUniformLocation); L (glUniform1i); L (glActiveTexture); L (glGetError); L (glGetString);
#undef L
	*(void **) &egl_GetCurrentContext = armegl_get_proc ("eglGetCurrentContext");
	*(void **) &egl_GetCurrentSurface = armegl_get_proc ("eglGetCurrentSurface");
	*(void **) &egl_MakeCurrent = armegl_get_proc ("eglMakeCurrent");
	return egl_GetCurrentContext && egl_GetCurrentSurface && egl_MakeCurrent;
}

/* le contexte de gpucomp courant (celui d'avant garde) -> 1 */
static int enter (void)
{
	double t = now_ms ();
	s_savedCtx = egl_GetCurrentContext ();
	s_savedSurface = egl_GetCurrentSurface (EGL_DRAW);
	if (s_savedCtx)
		s_nSwitch++;
	int r = armgl_make_current (s_ctx, s_surface);
	s_tEnter += now_ms () - t;
	return r;
}

static void leave (void)
{
	if (s_savedCtx)
		egl_MakeCurrent (NULL, s_savedSurface, s_savedSurface, s_savedCtx);
	else
		armgl_make_current (NULL, NULL);
}

static GLuint shader (GLenum kind, const char *src)
{
	GLuint s = glCreateShader (kind);
	GLint ok = 0;
	glShaderSource (s, 1, &src, NULL);
	glCompileShader (s);
	glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	return ok ? s : 0;
}

static const char vs_src[] =
	"attribute vec4 a_pos; attribute vec2 a_tc; attribute vec4 a_col;\n"
	"varying vec2 v_tc; varying vec4 v_col;\n"
	"void main () { gl_Position = vec4 (a_pos.x, -a_pos.y, a_pos.z, a_pos.w); v_tc = a_tc; v_col = a_col; }\n";
static const char fs_src[] =
	"precision mediump float; varying vec2 v_tc; varying vec4 v_col; uniform sampler2D u_tex;\n"
	"void main () { gl_FragColor = texture2D (u_tex, v_tc) * v_col; }\n";

static int setup (void)
{
	const char *e = getenv ("KORIN_GPUCOMP");
	int soft = (e && !strcmp (e, "soft")) || access ("/etc/web-gpu-soft", F_OK) == 0;
	if (e && !strcmp (e, "cpu")) { info ("KORIN_GPUCOMP=cpu"); return 0; }
	/* (ARMGL_F_NOSOFT = 8 : sans GPU Intel, pas de softpipe : le processeur de gpucomp va plus vite) */
	int mode = armgl_init (soft ? 0 : 8);
	if (mode != ARMGL_GPU && !(soft && mode == ARMGL_SOFT)) {
		info (mode == ARMGL_SOFT ? "softpipe only (/etc/web-gpu-soft to use it)" : "GlKit: no Intel GPU");
		return 0;
	}
	s_ctx = armgl_context (3, 0, NULL);
	s_surface = s_ctx ? armgl_surface (16, 16) : NULL;
	if (!s_ctx || !s_surface || !load ()) { info ("GlKit: no context"); return 0; }
	if (!enter ()) { info ("GlKit: context not current"); return 0; }
	GLuint vs = shader (GL_VERTEX_SHADER, vs_src), fs = shader (GL_FRAGMENT_SHADER, fs_src);
	GLint ok = 0;
	if (vs && fs) {
		s_program = glCreateProgram ();
		glAttachShader (s_program, vs);
		glAttachShader (s_program, fs);
		glBindAttribLocation (s_program, 0, "a_pos");
		glBindAttribLocation (s_program, 1, "a_tc");
		glBindAttribLocation (s_program, 2, "a_col");
		glLinkProgram (s_program);
		glGetProgramiv (s_program, GL_LINK_STATUS, &ok);
	}
	if (ok) {
		glUseProgram (s_program);
		glUniform1i (glGetUniformLocation (s_program, "u_tex"), 0);
		glGenBuffers (1, &s_vbo);
		glGenFramebuffers (1, &s_fbo);
		const char *r = (const char *) glGetString (GL_RENDERER);
		snprintf (s_info, sizeof s_info, "OpenGL ES 3 (GlKit: %s)", r ? r : "?");
	} else
		info ("GlKit: shaders refused");
	leave ();
	return ok;
}

static int ready (void)
{
	if (s_state == 0)
		s_state = setup () ? 1 : -1;
	return s_state == 1 && !armgl_lost ();
}

int kapi_abi_version (void) { return 72; }

unsigned kapi_clock_us (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return (unsigned) (ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
}

int kapi_gpu_info (char *buf, unsigned cap)
{
	int ok = ready ();
	snprintf (buf, cap, "%s", s_info);
	return ok;
}

/* -1 : une nouvelle texture -> sa poignee ; pixels 0 et w 0 : la poignee rendue. -4 plus de poignee, -1 GPU perdu */
int kapi_gpu_texture (int handle, const unsigned *pixels, int w, int h, int stride)
{
	if (!ready ())
		return -1;
	if (!enter ())
		return -1;
	double t0 = now_ms ();
	int r = -4;
	if (handle >= 0 && handle < MAX_TEX && w == 0 && pixels == NULL) {
		if (s_tex[handle])
			glDeleteTextures (1, &s_tex[handle]);
		s_tex[handle] = 0;
		r = 0;
	} else {
		int i = handle;
		if (i < 0)
			for (i = 1; i < MAX_TEX && s_tex[i]; i++)
				;
		if (i > 0 && i < MAX_TEX) {
			if (!s_tex[i])
				glGenTextures (1, &s_tex[i]);
			glBindTexture (GL_TEXTURE_2D, s_tex[i]);
			glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
			glPixelStorei (GL_UNPACK_ROW_LENGTH, pixels ? stride : 0);
			glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
			glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
			s_texFlags[i] = 0xFFFF;
			r = glGetError () == GL_NO_ERROR ? i : -4;
			if (pixels) {
				s_nUpload++;
				s_tUpload += now_ms () - t0;
			}
		}
	}
	leave ();
	return r;
}

int kapi_gpu_texture_rect (int handle, int x, int y, int w, int h, const unsigned *pixels, int stride)
{
	if (!ready () || handle <= 0 || handle >= MAX_TEX || !s_tex[handle])
		return -2;
	if (!enter ())
		return -1;
	double t0 = now_ms ();
	glBindTexture (GL_TEXTURE_2D, s_tex[handle]);
	glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
	glPixelStorei (GL_UNPACK_ROW_LENGTH, stride);
	glTexSubImage2D (GL_TEXTURE_2D, 0, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	s_nUpload++;
	s_tUpload += now_ms () - t0;
	leave ();
	return 0;
}

/* (gpucomp prend alors ses tampons a l'allocateur de l'appelant ; le GPU relit la cible) */
void *kapi_gpu_vbuf (unsigned bytes) { (void) bytes; return NULL; }

static void blend (unsigned mode)
{
	glBlendEquation (GL_FUNC_ADD);
	switch (mode) {
	case KAPI_GPU_BLEND_NONE: glDisable (GL_BLEND); return;
	case KAPI_GPU_BLEND_ALPHA: glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	case KAPI_GPU_BLEND_ADD: case KAPI_GPU_BLEND_PLUS: glBlendFunc (GL_ONE, GL_ONE); break;
	case KAPI_GPU_BLEND_MUL: glBlendFunc (GL_DST_COLOR, GL_ZERO); break;
	case KAPI_GPU_BLEND_MULCOL: glBlendFunc (GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); break;
	case KAPI_GPU_BLEND_UNDER: glBlendFunc (GL_ONE_MINUS_DST_ALPHA, GL_ONE); break;
	case KAPI_GPU_BLEND_SCREEN: glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
	case KAPI_GPU_BLEND_RSUB: glBlendEquation (GL_FUNC_REVERSE_SUBTRACT); glBlendFunc (GL_ONE, GL_ONE); break;
	case KAPI_GPU_BLEND_LIGHTEN: glBlendEquation (GL_MAX); glBlendFunc (GL_ONE, GL_ONE); break;
	case KAPI_GPU_BLEND_DSTIN: glBlendFunc (GL_ZERO, GL_SRC_ALPHA); break;
	case KAPI_GPU_BLEND_DSTOUT: glBlendFunc (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); break;
	default: glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;	/* PREMUL */
	}
	glEnable (GL_BLEND);
}

int kapi_gpu_render (const struct kapi_gpu_frame *F, const struct kapi_gpu_vertex3 *v, unsigned nv,
		     const struct kapi_gpu_batch *b, unsigned nb)
{
	if (!ready ())
		return -1;
	if (F->w <= 0 || F->h <= 0 || F->w > 4096 || F->h > 4096)
		return -2;
	if (!enter ())
		return -1;
	double t0 = now_ms ();
	/* la cible : un FBO de la taille de la partie (refait quand elle grandit) */
	if (F->w > s_fboW || F->h > s_fboH || !s_fboTex) {
		int w = F->w > s_fboW ? F->w : s_fboW, h = F->h > s_fboH ? F->h : s_fboH;
		if (s_fboTex)
			glDeleteTextures (1, &s_fboTex);
		glGenTextures (1, &s_fboTex);
		glBindTexture (GL_TEXTURE_2D, s_fboTex);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindFramebuffer (GL_FRAMEBUFFER, s_fbo);
		glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_fboTex, 0);
		s_fboW = w;
		s_fboH = h;
	}
	glBindFramebuffer (GL_FRAMEBUFFER, s_fbo);
	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		leave ();
		return -2;
	}
	glViewport (0, 0, F->w, F->h);
	glDisable (GL_DEPTH_TEST);
	glDisable (GL_SCISSOR_TEST);
	glDisable (GL_CULL_FACE);
	if (F->flags & KAPI_GPU_F_KEEP) {
		/* ce que la cible a deja : dans le FBO (sa ligne 0 en bas du FBO : le haut de la cible) */
		glBindTexture (GL_TEXTURE_2D, s_fboTex);
		glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
		glPixelStorei (GL_UNPACK_ROW_LENGTH, F->stride);
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, F->w, F->h, GL_RGBA, GL_UNSIGNED_BYTE, F->pixels);
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	} else {
		/* 0xAARRGGBB : les octets B, G, R, A, que GL voit comme R, G, B, A */
		unsigned c = F->clear;
		glClearColor ((c & 255) / 255.0f, ((c >> 8) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f, ((c >> 24) & 255) / 255.0f);
		glClear (GL_COLOR_BUFFER_BIT);
	}
	glUseProgram (s_program);
	glActiveTexture (0x84C0);	/* GL_TEXTURE0 */
	glBindBuffer (GL_ARRAY_BUFFER, s_vbo);
	glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) nv * (GLsizeiptr) sizeof *v, v, GL_STREAM_DRAW);
	glVertexAttribPointer (0, 4, GL_FLOAT, 0, sizeof *v, (const void *) offsetof (struct kapi_gpu_vertex3, x));
	glVertexAttribPointer (1, 2, GL_FLOAT, 0, sizeof *v, (const void *) offsetof (struct kapi_gpu_vertex3, s));
	glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, 1, sizeof *v, (const void *) offsetof (struct kapi_gpu_vertex3, r));
	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	glEnableVertexAttribArray (2);
	unsigned lastBlend = ~0u;
	for (unsigned i = 0; i < nb; i++) {
		const struct kapi_gpu_batch *B = &b[i];
		if (!B->count || B->first + B->count > nv)
			continue;
		int t = B->texture;
		if (t <= 0 || t >= MAX_TEX || !s_tex[t])
			continue;
		glBindTexture (GL_TEXTURE_2D, s_tex[t]);
		unsigned short tf = (unsigned short) (((B->flags & KAPI_GPU_B_LINEAR) ? 1 : 0) | ((B->flags >> 13) & 0xF) << 1);
		if (s_texFlags[t] != tf) {
			GLint filter = (B->flags & KAPI_GPU_B_LINEAR) ? GL_LINEAR : GL_NEAREST;
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ((B->flags >> 13) & 3) == KAPI_GPU_WRAP_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
			glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ((B->flags >> 15) & 3) == KAPI_GPU_WRAP_CLAMP ? GL_CLAMP_TO_EDGE : GL_REPEAT);
			s_texFlags[t] = tf;
		}
		unsigned mode = (B->flags >> 8) & 15;
		if (mode != lastBlend) {
			blend (mode);
			lastBlend = mode;
		}
		glDrawArrays (GL_TRIANGLES, (GLint) B->first, (GLsizei) B->count);
	}
	/* la cible relue : la ligne 0 du FBO est le haut */
	double t1 = now_ms ();
	glPixelStorei (GL_PACK_ALIGNMENT, 4);
	glPixelStorei (GL_PACK_ROW_LENGTH, F->stride);
	glReadPixels (0, 0, F->w, F->h, GL_RGBA, GL_UNSIGNED_BYTE, F->pixels);
	glPixelStorei (GL_PACK_ROW_LENGTH, 0);
	glBindFramebuffer (GL_FRAMEBUFFER, 0);
	int err = glGetError () != GL_NO_ERROR;
	double t2 = now_ms ();
	s_nRender++;
	s_tDraw += t1 - t0;
	s_tRead += t2 - t1;
	s_pxRead += (unsigned) (F->w * F->h);
	leave ();
	times_log ();
	return armgl_lost () ? -1 : err ? -2 : 0;
}

/* (comme le Pi : armpi_gpu_last_texture / armpi_gpu_texture_info servent a la toile WebGL du V3D ; ici aucune
 * texture n'est en memoire partagee avec Mesa : la toile n'a pas de couche sur le PC, elle est peinte dans sa tuile) */
int armpi_gpu_last_texture (void) { return -1; }
int armpi_gpu_texture_info (int handle, long *out) { (void) handle; (void) out; return -1; }

/* GPC_F_ASYNC : pas de threads (gpucomp compose sur le thread appelant) */
int kapi_event_create (int manual_reset, int initial) { (void) manual_reset; (void) initial; return -1; }
int kapi_event_set (int h) { (void) h; return -1; }
int kapi_event_wait (int h, unsigned timeout_ms) { (void) h; (void) timeout_ms; return -1; }
int kapi_thread_create (int (*f) (void *), void *arg, unsigned stack_size, const char *name)
{ (void) f; (void) arg; (void) stack_size; (void) name; return -1; }
int kapi_thread_join (int tid, unsigned timeout_ms, int *code) { (void) tid; (void) timeout_ms; if (code) *code = 0; return -1; }
int kapi_sync_close (int h) { (void) h; return -1; }
