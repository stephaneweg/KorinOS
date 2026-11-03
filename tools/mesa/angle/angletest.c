/*
 * angletest -- ANGLE sur Mesa (docs/MESA.md) : l'EGL et le traducteur d'ANGLE, puis armegl, Mesa, le V3D.
 * Le chemin que suit WebGL. Un FBO, un triangle, pixels verifies par glReadPixels.
 *     angletest         le GPU
 *     angletest --dry   sans GPU : tout passe, les jobs ne partent pas
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <GLES3/gl3.h>
#include "armgl.h"

#define W 256
#define H 256

static const char *vs_src =
	"#version 300 es\n"
	"layout(location = 0) in vec2 pos;\n"
	"layout(location = 1) in vec3 col;\n"
	"out vec3 v;\n"
	"void main () { v = col; gl_Position = vec4 (pos, 0.0, 1.0); }\n";
static const char *fs_src =
	"#version 300 es\n"
	"precision mediump float;\n"
	"in vec3 v;\n"
	"uniform float gain;\n"
	"out vec4 c;\n"
	"void main () { c = vec4 (v * gain, 1.0); }\n";

static const float verts[] = {
	-0.8f, -0.8f,  1, 0, 0,
	 0.8f, -0.8f,  0, 1, 0,
	 0.0f,  0.8f,  0, 0, 1,
};

static int fails;

static void expect (const unsigned char *px, int x, int y, int r, int g, int b, const char *what)
{
	const unsigned char *p = px + (y * W + x) * 4;
	int bad = abs (p[0] - r) > 12 || abs (p[1] - g) > 12 || abs (p[2] - b) > 12;
	printf ("angletest : %-14s %02x%02x%02x, attendu %02x%02x%02x : %s\n", what, p[0], p[1], p[2], r, g, b, bad ? "FAIL" : "PASS");
	fails += bad;
}

int main (int argc, char **argv)
{
	int dry = argc > 1 && !strcmp (argv[1], "--dry");
	int mode = armgl_init (dry ? ARMGL_F_DRY : 0);
	printf ("angletest : armgl_init %d\n", mode);
	if (mode == ARMGL_NONE) {
		printf ("angletest : pas de GPU : pas de WebGL. PASS\n");
		return 0;
	}
	/* ni X11 ni GBM : il faut dire a ANGLE que son backend est un EGL natif */
	EGLint attrs[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_OPENGLES_ANGLE,
			   EGL_PLATFORM_ANGLE_DEVICE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_DEVICE_TYPE_EGL_ANGLE, EGL_NONE };
	EGLDisplay d = eglGetPlatformDisplayEXT (EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attrs);
	EGLint major = 0, minor = 0;
	if (d == EGL_NO_DISPLAY || !eglInitialize (d, &major, &minor)) {
		printf ("angletest : eglInitialize a echoue, erreur %x. FAIL\n", eglGetError ());
		return 1;
	}
	printf ("angletest : EGL d'ANGLE %d.%d, %s\n", major, minor, eglQueryString (d, EGL_VENDOR));
	EGLint cfga[] = { EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_NONE };
	EGLConfig cfg = 0;
	EGLint n = 0;
	eglChooseConfig (d, cfga, &cfg, 1, &n);
	{
		EGLConfig all[8];
		EGLint na = 0, rt = 0, st = 0;
		eglGetConfigs (d, all, 8, &na);
		if (na) {
			eglGetConfigAttrib (d, all[0], EGL_RENDERABLE_TYPE, &rt);
			eglGetConfigAttrib (d, all[0], EGL_SURFACE_TYPE, &st);
		}
		printf ("angletest : %d configs, choisies %d, renderable %x, surface %x\n", na, n, rt, st);
		if (!n && na) {
			cfg = all[0];
			n = 1;
		}
	}
	EGLint ca[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE };
	eglBindAPI (EGL_OPENGL_ES_API);
	EGLContext ctx = eglCreateContext (d, n ? cfg : (EGLConfig) 0, EGL_NO_CONTEXT, ca);
	if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent (d, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
		printf ("angletest : pas de contexte, erreur %x. FAIL\n", eglGetError ());
		return 1;
	}
	printf ("angletest : %s / %s\n", glGetString (GL_RENDERER), glGetString (GL_VERSION));

	GLuint tex, fbo;
	glGenTextures (1, &tex);
	glBindTexture (GL_TEXTURE_2D, tex);
	glTexStorage2D (GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
	glGenFramebuffers (1, &fbo);
	glBindFramebuffer (GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		printf ("angletest : FBO incomplet. FAIL\n");
		return 1;
	}
	const char *srcs[2] = { vs_src, fs_src };
	GLenum kinds[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	GLuint prog = glCreateProgram ();
	char log[1024];
	GLint ok = 0;
	for (int i = 0; i < 2; i++) {
		GLuint s = glCreateShader (kinds[i]);
		glShaderSource (s, 1, &srcs[i], NULL);
		glCompileShader (s);
		glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			glGetShaderInfoLog (s, sizeof log, NULL, log);
			printf ("angletest : shader %d refuse : %s. FAIL\n", i, log);
			return 1;
		}
		glAttachShader (prog, s);
	}
	glLinkProgram (prog);
	glGetProgramiv (prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		glGetProgramInfoLog (prog, sizeof log, NULL, log);
		printf ("angletest : liens refuses : %s. FAIL\n", log);
		return 1;
	}
	GLuint vao, vbo;
	glGenVertexArrays (1, &vao);
	glBindVertexArray (vao);
	glGenBuffers (1, &vbo);
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glBufferData (GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 20, (void *) 0);
	glVertexAttribPointer (1, 3, GL_FLOAT, GL_FALSE, 20, (void *) 8);
	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	glViewport (0, 0, W, H);
	glUseProgram (prog);
	glUniform1f (glGetUniformLocation (prog, "gain"), 1.0f);
	glClearColor (0, 0, 0.25f, 1);
	glClear (GL_COLOR_BUFFER_BIT);
	glDrawArrays (GL_TRIANGLES, 0, 3);
	glFinish ();
	static unsigned char px[W * H * 4];
	glReadPixels (0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf ("angletest : jobs %ld, glGetError %x\n", armgl_jobs (), glGetError ());
	if (mode == ARMGL_DRY) {
		printf ("angletest : a blanc : ANGLE et Mesa vont au bout. PASS\n");
		return 0;
	}
	/* dans un FBO, la ligne 0 est en bas */
	expect (px, 2, 2, 0, 0, 0x40, "fond");
	expect (px, 32, 30, 0xF3, 6, 6, "sommet rouge");
	expect (px, 224, 30, 4, 0xF5, 6, "sommet vert");
	expect (px, 128, 223, 4, 5, 0xF6, "sommet bleu");
	printf ("angletest : %s\n", fails ? "FAIL" : "PASS");
	return fails != 0;
}
