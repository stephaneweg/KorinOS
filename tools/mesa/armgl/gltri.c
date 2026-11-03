/*
 * gltri -- un triangle OpenGL ES qui tourne dans une fenetre d'ARMPI, rendu par le GPU (docs/MESA.md).
 * Le GPU rend directement dans la surface d'Elegant ; la ligne de commande donne les images par seconde.
 *     gltri [--dry] [--frames N] [--copy]
 * --copy : l'ancien chemin, l'image relue et recopiee, pour comparer.
 * Sans GPU : le dit et rend 0. --dry : la fenetre s'ouvre, Mesa tourne, les jobs ne partent pas (image noire).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uikit.h>
#include "armgl.h"

#define W 400
#define H 400

#define GL_TRIANGLES		0x0004
#define GL_FLOAT		0x1406
#define GL_FRAGMENT_SHADER	0x8B30
#define GL_VERTEX_SHADER	0x8B31
#define GL_COMPILE_STATUS	0x8B81
#define GL_LINK_STATUS		0x8B82
#define GL_COLOR_BUFFER_BIT	0x4000
#define GL_ARRAY_BUFFER		0x8892
#define GL_STATIC_DRAW		0x88E4

static const char *vs_src =
	"#version 300 es\n"
	"layout(location = 0) in vec2 pos;\n"
	"layout(location = 1) in vec3 col;\n"
	"uniform float angle;\n"
	"out vec3 v;\n"
	"void main () {\n"
	"	float c = cos (angle), s = sin (angle);\n"
	"	v = col;\n"
	"	gl_Position = vec4 (c * pos.x - s * pos.y, s * pos.x + c * pos.y, 0.0, 1.0);\n"
	"}\n";
static const char *fs_src =
	"#version 300 es\n"
	"precision mediump float;\n"
	"in vec3 v;\n"
	"out vec4 c;\n"
	"void main () { c = vec4 (v, 1.0); }\n";

static const float verts[] = {
	 0.0f,  0.8f,  1, 0, 0,
	-0.7f, -0.5f,  0, 1, 0,
	 0.7f, -0.5f,  0, 0, 1,
};

static struct ui_vars ui;

#define FN(ret, name, ...) ret (*name) (__VA_ARGS__) = armgl_proc (#name); if (!name) { printf ("gltri : %s manque\n", #name); return 1; }

/* ui_surface_pixels rend quatre valeurs : les relais generes n'en rendent qu'une */
static unsigned *surface_pixels (long s, long *pitch)
{
	register long x0 __asm__ ("x0") = s;
	register long x1 __asm__ ("x1");
	__asm__ volatile ("bl ui_surface_pixels" : "+r" (x0), "=r" (x1) : : "x2", "x3", "x16", "x17", "x30", "memory", "cc");
	*pitch = x1;
	return (unsigned *) x0;
}

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int main (int argc, char **argv)
{
	int dry = 0, frames = 0, copy = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--dry")) dry = 1;
		if (!strcmp (argv[i], "--copy")) copy = 1;
		if (!strcmp (argv[i], "--frames") && i + 1 < argc) frames = atoi (argv[++i]);
	}
	int mode = armgl_init (dry ? ARMGL_F_DRY : 0);
	if (mode == ARMGL_NONE) {
		printf ("gltri : pas de GPU\n");
		return 0;
	}
	if (!uikit_open (&ui) || ui_window (W, H, (long) "gltri") <= 0) {
		printf ("gltri : pas de fenetre\n");
		return 1;
	}
	void *ctx = armgl_context (3, 0, NULL);
	void *surf = copy ? armgl_surface (W, H) : armgl_window (ui.win, 0, 0, W, H);
	if (!ctx || !surf || !armgl_make_current (ctx, surf)) {
		printf ("gltri : pas de surface GL\n");
		return 1;
	}
	FN (unsigned, glCreateShader, unsigned)
	FN (void, glShaderSource, unsigned, int, const char *const *, const int *)
	FN (void, glCompileShader, unsigned)
	FN (void, glGetShaderiv, unsigned, unsigned, int *)
	FN (void, glGetShaderInfoLog, unsigned, int, int *, char *)
	FN (unsigned, glCreateProgram, void)
	FN (void, glAttachShader, unsigned, unsigned)
	FN (void, glLinkProgram, unsigned)
	FN (void, glGetProgramiv, unsigned, unsigned, int *)
	FN (void, glUseProgram, unsigned)
	FN (int, glGetUniformLocation, unsigned, const char *)
	FN (void, glUniform1f, int, float)
	FN (void, glGenBuffers, int, unsigned *)
	FN (void, glBindBuffer, unsigned, unsigned)
	FN (void, glBufferData, unsigned, long, const void *, unsigned)
	FN (void, glGenVertexArrays, int, unsigned *)
	FN (void, glBindVertexArray, unsigned)
	FN (void, glVertexAttribPointer, unsigned, int, unsigned, unsigned char, int, const void *)
	FN (void, glEnableVertexAttribArray, unsigned)
	FN (void, glViewport, int, int, int, int)
	FN (void, glClearColor, float, float, float, float)
	FN (void, glClear, unsigned)
	FN (void, glDrawArrays, unsigned, int, int)

	const char *srcs[2] = { vs_src, fs_src };
	unsigned kinds[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	unsigned prog = glCreateProgram (), vao, vbo;
	int ok = 0;
	for (int i = 0; i < 2; i++) {
		char log[512];
		unsigned s = glCreateShader (kinds[i]);
		glShaderSource (s, 1, &srcs[i], NULL);
		glCompileShader (s);
		glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			glGetShaderInfoLog (s, sizeof log, NULL, log);
			printf ("gltri : shader refuse : %s\n", log);
			return 1;
		}
		glAttachShader (prog, s);
	}
	glLinkProgram (prog);
	glGetProgramiv (prog, GL_LINK_STATUS, &ok);
	if (!ok)
		return 1;
	glGenVertexArrays (1, &vao);
	glBindVertexArray (vao);
	glGenBuffers (1, &vbo);
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glBufferData (GL_ARRAY_BUFFER, sizeof verts, verts, GL_STATIC_DRAW);
	glVertexAttribPointer (0, 2, GL_FLOAT, 0, 20, (void *) 0);
	glVertexAttribPointer (1, 3, GL_FLOAT, 0, 20, (void *) 8);
	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	glViewport (0, 0, W, H);
	glUseProgram (prog);
	int angle = glGetUniformLocation (prog, "angle");

	long s = 0, pitch = 0;
	unsigned *px = NULL;
	if (copy) {
		s = ui_surface (0, 0, W, H, WS_SF_XRGB8888);
		if (!s) {
			printf ("gltri : pas de surface\n");
			return 1;
		}
		px = surface_pixels (s, &pitch);
	}
	double t0 = now (), last = t0;
	int n = 0, shown = 0;
	while (ui_event (0) != WEV_CLOSE) {
		glClearColor (0.0f, 0.0f, 0.2f, 1.0f);
		glClear (GL_COLOR_BUFFER_BIT);
		glUniform1f (angle, (float) ((now () - t0) / 1000.0));
		glDrawArrays (GL_TRIANGLES, 0, 3);
		if (copy) {
			if (armgl_read (surf, px, (int) pitch))
				break;
			ui_surface_damage (s, 0, 0, 0, 0);
		} else if (armgl_swap (surf)) {
			printf ("gltri : image refusee\n");
			break;
		}
		n++;
		double t = now ();
		if (t - last >= 2000) {
			printf ("gltri : %.1f images/s\n", (n - shown) * 1000.0 / (t - last));
			fflush (stdout);
			last = t;
			shown = n;
		}
		if (frames && n >= frames)
			break;
	}
	if (!copy) {
		double f, c, a;
		armgl_times (surf, &f, &c, &a);
		printf ("gltri : par image : GPU %.2f ms, cache %.2f ms, rattachement %.2f ms\n", f, c, a);
	}
	printf ("gltri : %d images, %ld jobs. fin\n", n, armgl_jobs ());
	return 0;
}
