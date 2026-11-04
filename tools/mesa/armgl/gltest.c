/*
 * gltest -- le premier essai d'OpenGL ES sur ARMPI (docs/MESA.md).
 *     gltest              le GPU : un triangle Gouraud puis une texture, pixels verifies, SD0:/gltest.ppm
 *     gltest --dry        sans GPU : Mesa et son compilateur vont au bout, les jobs ne partent pas
 *     gltest --buf        de meme, sur les vrais tampons non caches sous 1 Go
 *     gltest --dry --qpu  et le code QPU des shaders sur la sortie (V3D_DEBUG=vs,fs,qpu)
 *     gltest --debug X    V3D_DEBUG=X (cl : les listes de commandes)
 *     gltest --dry --v71  a blanc, Mesa se croit sur le V3D 7.1 du Pi 5 (shaders et listes du Pi 5)
 * Sans GPU et sans --dry : dit qu'il n'y a pas de GPU et rend 0, c'est le repli d'une application.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "armgl.h"

#define W 256
#define H 256

typedef unsigned int GLuint;
typedef int GLint;
#define GL_TRIANGLES		0x0004
#define GL_FLOAT		0x1406
#define GL_FRAGMENT_SHADER	0x8B30
#define GL_VERTEX_SHADER	0x8B31
#define GL_COMPILE_STATUS	0x8B81
#define GL_LINK_STATUS		0x8B82
#define GL_COLOR_BUFFER_BIT	0x4000
#define GL_ARRAY_BUFFER		0x8892
#define GL_STATIC_DRAW		0x88E4
#define GL_VENDOR		0x1F00
#define GL_RENDERER		0x1F01
#define GL_VERSION		0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_TEXTURE_2D		0x0DE1
#define GL_RGBA			0x1908
#define GL_RGBA8		0x8058
#define GL_UNSIGNED_BYTE	0x1401
#define GL_TEXTURE_MIN_FILTER	0x2801
#define GL_TEXTURE_MAG_FILTER	0x2800
#define GL_NEAREST		0x2600

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

/* un shader a la maniere de ceux de Skia : texture, couverture d'un disque, teinte */
static const char *vs2_src =
	"#version 300 es\n"
	"layout(location = 0) in vec2 pos;\n"
	"out vec2 uv;\n"
	"void main () { uv = pos * 0.5 + 0.5; gl_Position = vec4 (pos, 0.0, 1.0); }\n";
static const char *fs2_src =
	"#version 300 es\n"
	"precision mediump float;\n"
	"in vec2 uv;\n"
	"uniform sampler2D tex;\n"
	"uniform vec4 tint;\n"
	"out vec4 c;\n"
	"void main () {\n"
	"	float d = length (uv - vec2 (0.5));\n"
	"	float cov = 1.0 - smoothstep (0.3, 0.4, d);\n"
	"	c = texture (tex, uv) * tint * cov;\n"
	"}\n";

static const float verts[] = {
	-0.8f, -0.8f,  1, 0, 0,
	 0.8f, -0.8f,  0, 1, 0,
	 0.0f,  0.8f,  0, 0, 1,
};

static int fails;

/* un pixel de l'image lue, a <tol> pres par canal */
static void expect (const unsigned *px, int x, int y, unsigned want, int tol, const char *what)
{
	unsigned got = px[y * W + x];
	int bad = 0;
	for (int k = 0; k < 24; k += 8) {
		int d = (int) (got >> k & 255) - (int) (want >> k & 255);
		if (d < -tol || d > tol)
			bad = 1;
	}
	printf ("gltest : %-30s (%3d,%3d) %06x, attendu %06x : %s\n", what, x, y, got & 0xFFFFFF, want, bad ? "FAIL" : "PASS");
	fails += bad;
}

#define FN(ret, name, ...) ret (*name) (__VA_ARGS__) = armgl_proc (#name); if (!name) { printf ("gltest : %s manque\n", #name); return 1; }

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

int main (int argc, char **argv)
{
	int dry = 0, qpu = 0, buf = 0, v71 = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--dry")) dry = 1;
		if (!strcmp (argv[i], "--qpu")) qpu = 1;
		if (!strcmp (argv[i], "--buf")) buf = 1;
		if (!strcmp (argv[i], "--v71")) v71 = ARMGL_F_V71;
		if (!strcmp (argv[i], "--debug") && i + 1 < argc) setenv ("V3D_DEBUG", argv[++i], 1);
	}
	if (qpu)
		setenv ("V3D_DEBUG", "vs,fs,qpu", 1);
	double t0 = now ();
	int mode = armgl_init ((buf ? ARMGL_F_DRY_BUF : dry ? ARMGL_F_DRY : 0) | v71);
	printf ("gltest : armgl_init %d (%s) %.0f ms\n", mode,
		mode == ARMGL_GPU ? "GPU" : mode == ARMGL_DRY ? "a blanc" : "pas de GPU", now () - t0);
	if (mode == ARMGL_NONE) {
		printf ("gltest : repli : pas de GPU, l'application reste sur le processeur. PASS\n");
		return 0;
	}
	void *ctx = armgl_context (3, 0, NULL);
	void *surf = armgl_surface (W, H);
	if (!ctx || !surf || !armgl_make_current (ctx, surf)) {
		printf ("gltest : pas de contexte\n");
		return 1;
	}
	FN (const unsigned char *, glGetString, unsigned)
	FN (GLuint, glCreateShader, unsigned)
	FN (void, glShaderSource, GLuint, int, const char *const *, const int *)
	FN (void, glCompileShader, GLuint)
	FN (void, glGetShaderiv, GLuint, unsigned, int *)
	FN (void, glGetShaderInfoLog, GLuint, int, int *, char *)
	FN (GLuint, glCreateProgram, void)
	FN (void, glAttachShader, GLuint, GLuint)
	FN (void, glLinkProgram, GLuint)
	FN (void, glGetProgramiv, GLuint, unsigned, int *)
	FN (void, glGetProgramInfoLog, GLuint, int, int *, char *)
	FN (void, glUseProgram, GLuint)
	FN (GLint, glGetUniformLocation, GLuint, const char *)
	FN (void, glUniform1f, GLint, float)
	FN (void, glGenBuffers, int, GLuint *)
	FN (void, glBindBuffer, unsigned, GLuint)
	FN (void, glBufferData, unsigned, long, const void *, unsigned)
	FN (void, glGenVertexArrays, int, GLuint *)
	FN (void, glBindVertexArray, GLuint)
	FN (void, glVertexAttribPointer, GLuint, int, unsigned, unsigned char, int, const void *)
	FN (void, glEnableVertexAttribArray, GLuint)
	FN (void, glViewport, int, int, int, int)
	FN (void, glClearColor, float, float, float, float)
	FN (void, glClear, unsigned)
	FN (void, glDrawArrays, unsigned, int, int)
	FN (unsigned, glGetError, void)

	printf ("gltest : %s / %s / %s / %s\n", glGetString (GL_VENDOR), glGetString (GL_RENDERER),
		glGetString (GL_VERSION), glGetString (GL_SHADING_LANGUAGE_VERSION));

	const char *srcs[2] = { vs_src, fs_src };
	unsigned kinds[2] = { GL_VERTEX_SHADER, GL_FRAGMENT_SHADER };
	GLuint prog = glCreateProgram ();
	char log[1024];
	t0 = now ();
	for (int i = 0; i < 2; i++) {
		GLuint s = glCreateShader (kinds[i]);
		int ok = 0;
		glShaderSource (s, 1, &srcs[i], NULL);
		glCompileShader (s);
		glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			glGetShaderInfoLog (s, sizeof log, NULL, log);
			printf ("gltest : shader %d refuse : %s\n", i, log);
			return 1;
		}
		glAttachShader (prog, s);
	}
	int ok = 0;
	glLinkProgram (prog);
	glGetProgramiv (prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		glGetProgramInfoLog (prog, sizeof log, NULL, log);
		printf ("gltest : edition de liens refusee : %s\n", log);
		return 1;
	}
	printf ("gltest : shaders GLSL ES compiles et lies en %.0f ms\n", now () - t0);

	GLuint vao, vbo;
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
	glUniform1f (glGetUniformLocation (prog, "gain"), 1.0f);
	t0 = now ();
	glClearColor (0.0f, 0.0f, 0.25f, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT);
	glDrawArrays (GL_TRIANGLES, 0, 3);	/* le premier dessin produit le code QPU */
	armgl_finish ();
	printf ("gltest : dessin %.0f ms, jobs soumis : %ld, glGetError %u\n", now () - t0, armgl_jobs (), glGetError ());

	/* le triangle, avant que le second programme ne le recouvre */
	static unsigned pixels[W * H];
	if (armgl_read (surf, pixels, W * 4)) {
		printf ("gltest : lecture impossible\n");
		return 1;
	}
	if (mode == ARMGL_GPU) {
		expect (pixels, 2, 2, 0x000040, 2, "fond (glClear)");
		expect (pixels, 32, 225, 0xE0100F, 40, "sommet rouge, en bas a gauche");
		expect (pixels, 224, 225, 0x0FE010, 40, "sommet vert, en bas a droite");
		expect (pixels, 128, 32, 0x0F0FE0, 40, "sommet bleu, en haut");
		expect (pixels, 128, 140, 0x555555, 40, "centre : melange");
	}
	FILE *f = mode == ARMGL_GPU ? fopen ("SD0:/gltest.ppm", "wb") : NULL;
	if (f) {
		fprintf (f, "P6\n%d %d\n255\n", W, H);
		for (int i = 0; i < W * H; i++) {
			unsigned char c[3] = { pixels[i] >> 16, pixels[i] >> 8, pixels[i] };
			fwrite (c, 3, 1, f);
		}
		fclose (f);
		printf ("gltest : SD0:/gltest.ppm\n");
	}

	/* le second programme : texture 2 x 2 et disque (la verification : tools/mesa/qpucheck.cpp) */
	{
		FN (void, glGenTextures, int, GLuint *)
		FN (void, glBindTexture, unsigned, GLuint)
		FN (void, glTexImage2D, unsigned, int, int, int, int, int, unsigned, unsigned, const void *)
		FN (void, glTexParameteri, unsigned, unsigned, int)
		FN (void, glUniform4f, GLint, float, float, float, float)
		FN (void, glUniform1i, GLint, int)
		FN (void, glGenerateMipmap, unsigned)
		static const unsigned char texels[16] = { 255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255 };
		const char *s2[2] = { vs2_src, fs2_src };
		GLuint p2 = glCreateProgram (), tex;
		for (int i = 0; i < 2; i++) {
			GLuint sh = glCreateShader (kinds[i]);
			glShaderSource (sh, 1, &s2[i], NULL);
			glCompileShader (sh);
			glGetShaderiv (sh, GL_COMPILE_STATUS, &ok);
			if (!ok) {
				glGetShaderInfoLog (sh, sizeof log, NULL, log);
				printf ("gltest : shader 2.%d refuse : %s\n", i, log);
				return 1;
			}
			glAttachShader (p2, sh);
		}
		glLinkProgram (p2);
		glGetProgramiv (p2, GL_LINK_STATUS, &ok);
		if (!ok)
			return 1;
		glGenTextures (1, &tex);
		glBindTexture (GL_TEXTURE_2D, tex);
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
		glGenerateMipmap (GL_TEXTURE_2D);	/* sans TFU : par le rendu */
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glUseProgram (p2);
		glUniform1i (glGetUniformLocation (p2, "tex"), 0);
		glUniform4f (glGetUniformLocation (p2, "tint"), 1.0f, 0.5f, 0.25f, 1.0f);
		glDrawArrays (GL_TRIANGLES, 0, 3);
		armgl_finish ();
		printf ("gltest : programme texture, jobs %ld, glGetError %u\n", armgl_jobs (), glGetError ());
		glUseProgram (prog);
	}

	if (armgl_read (surf, pixels, W * 4)) {
		printf ("gltest : lecture impossible\n");
		return 1;
	}
	if (mode == ARMGL_DRY) {
		if (armgl_jobs () < 1) {
			printf ("gltest : aucun job : FAIL\n");
			return 1;
		}
		if (buf && (armgl_first_phys () == 0 || armgl_first_phys () >= 0x40000000)) {
			printf ("gltest : tampons hors du premier Go (%x) : FAIL\n", armgl_first_phys ());
			return 1;
		}
		if (buf)
			printf ("gltest : tampons non caches a %x\n", armgl_first_phys ());
		printf ("gltest : a blanc : Mesa va au bout. PASS\n");
		return 0;
	}
	/* le disque teinte (1, 0.5, 0.25) sur la texture rouge, vert, bleu, blanc */
	expect (pixels, 102, 153, 0xFF0000, 4, "texture : texel rouge");
	expect (pixels, 153, 153, 0x008000, 4, "texture : texel vert teinte");
	expect (pixels, 140, 76, 0xFF8040, 4, "texture : texel blanc teinte");
	printf ("gltest : %d ecart(s) : %s\n", fails, fails ? "FAIL" : "PASS");
	return fails ? 1 : 0;
}
