/*
 * gkgl.c -- GpuKit par le GPU Intel sur Korin PC (gpukit_gl.h ; docs/etude/PC-PILOTES.md « GPU Intel, étape 3 »).
 * Les fonctions de GpuKit écrites sur OpenGL ES 3 (GlKit, Mesa iris) : un programme de shaders (texel × couleur +
 * couleur 2, test d'alpha), les états de chaque lot (Z, faces, 13 mélanges, filtre, répétition, matrice), une
 * surface hors écran de la taille de la cible, relue à la fin dans la cible (0x00RRGGBB). GpuKit (le kit, gpukit.c)
 * appelle ces fonctions par la table que gk_gl_attach lui donne ; ce qu'elles refusent, il le fait au processeur.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GLES3/gl3.h>
#include <gpukit_abi.h>
#include "armgl.h"
#include "gpukit_gl.h"

struct gk_vertex { float x, y, z, w, s, t; unsigned char rgba[4], rgba2[4]; };
struct gk_batch { unsigned first, count; int tex; unsigned flags; float m[16]; };
struct gk_frame { unsigned *px; int w, h, stride; unsigned clear, flags, pad; };
struct gk_tex { unsigned *px; int w, h, id, pad; };
_Static_assert (sizeof (struct gk_vertex) == GKV_SIZE && sizeof (struct gk_batch) == GKB_SIZE
		&& sizeof (struct gk_frame) == GKF_SIZE && sizeof (struct gk_tex) == GKT_SIZE, "gpukit.inc");

#define MAX_TEX GK_MAX_TEXTURES
#define PX_CACHE 64
#define SURFS 4

static void *s_ctx;
static struct { void *surf; int w, h; } s_surf[SURFS];
static int s_mode;
static GLuint s_prog, s_vbo, s_vao, s_bg;
static GLint u_m, u_use, u_aref;
static struct { GLuint name; int w, h; } s_tex[MAX_TEX];
static struct { const void *px; int w, h; GLuint name; unsigned age; } s_pxc[PX_CACHE];
static unsigned s_age;
static unsigned *s_read;
static size_t s_readCap;
static char s_desc[160];

static const char *vs_src =
	"#version 300 es\n"
	"layout(location = 0) in vec4 p;\n"
	"layout(location = 1) in vec2 st;\n"
	"layout(location = 2) in vec4 c;\n"
	"layout(location = 3) in vec4 c2;\n"
	"uniform mat4 M;\n"
	"out vec2 v_st;\n"
	"out vec4 v_c, v_c2;\n"
	"void main () { v_st = st; v_c = c; v_c2 = c2; gl_Position = M * p; }\n";
static const char *fs_src =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec2 v_st;\n"
	"in vec4 v_c, v_c2;\n"
	"uniform sampler2D T;\n"
	"uniform int useT;\n"
	"uniform float aref;\n"
	"out vec4 o;\n"
	"void main () {\n"
	"	vec4 c = v_c;\n"
	"	if (useT != 0) c *= texture (T, v_st).bgra;\n"	/* (texels 0xAARRGGBB envoyes en RGBA) */
	"	c = min (c + v_c2, vec4 (1.0));\n"
	"	if (c.a < aref) discard;\n"
	"	o = c;\n"
	"}\n";

static GLuint shader (GLenum kind, const char *src)
{
	GLuint s = glCreateShader (kind);
	glShaderSource (s, 1, &src, NULL);
	glCompileShader (s);
	GLint ok = 0;
	glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		glGetShaderInfoLog (s, sizeof log, NULL, log);
		fprintf (stderr, "gpukit_gl : shader refusé : %s\n", log);
		return 0;
	}
	return s;
}

static void *surface (int w, int h)
{
	int free_i = -1;
	for (int i = 0; i < SURFS; i++) {
		if (s_surf[i].surf && s_surf[i].w == w && s_surf[i].h == h)
			return s_surf[i].surf;
		if (!s_surf[i].surf && free_i < 0)
			free_i = i;
	}
	if (free_i < 0)
		free_i = (int) (s_age % SURFS);		/* (l'ancienne reste allouée : rare, des tailles qui changent) */
	s_surf[free_i].surf = armgl_surface (w, h);
	s_surf[free_i].w = w;
	s_surf[free_i].h = h;
	return s_surf[free_i].surf;
}

static void tex_params (unsigned flags)
{
	static const GLint wrap[4] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT, GL_REPEAT };
	GLint f = (flags & GK_B_LINEAR) ? GL_LINEAR : GL_NEAREST;
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap[(flags >> GK_B_WRAP_S_SHIFT) & 3]);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap[(flags >> GK_B_WRAP_T_SHIFT) & 3]);
}

static void upload (GLuint name, int w, int h, const void *px, int pitch, int fresh)
{
	glBindTexture (GL_TEXTURE_2D, name);
	glPixelStorei (GL_UNPACK_ROW_LENGTH, pitch);
	glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
	if (fresh)
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	else
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
}

/* une image du programme (pas une texture de GpuKit) : recopiée à chaque rendu, sa texture gardée */
static GLuint px_texture (const struct gk_tex *t)
{
	int old = 0;
	for (int i = 0; i < PX_CACHE; i++) {
		if (s_pxc[i].name && s_pxc[i].px == t->px && s_pxc[i].w == t->w && s_pxc[i].h == t->h) {
			if (s_pxc[i].age != s_age)
				upload (s_pxc[i].name, t->w, t->h, t->px, t->w, 0);
			s_pxc[i].age = s_age;
			return s_pxc[i].name;
		}
		if (s_pxc[old].name && (!s_pxc[i].name || s_pxc[i].age < s_pxc[old].age))
			old = i;			/* une place libre, sinon la plus ancienne */
	}
	if (!s_pxc[old].name)
		glGenTextures (1, &s_pxc[old].name);
	s_pxc[old].px = t->px;
	s_pxc[old].w = t->w;
	s_pxc[old].h = t->h;
	s_pxc[old].age = s_age;
	upload (s_pxc[old].name, t->w, t->h, t->px, t->w, 1);
	return s_pxc[old].name;
}

static long be_texture (long t, long px, long w, long h, long pitch)
{
	if (t >= MAX_TEX)
		return GK_E_INVAL;
	if (t >= 0 && !px) {				/* liberee */
		if (s_tex[t].name)
			glDeleteTextures (1, &s_tex[t].name);
		s_tex[t].name = 0;
		return 0;
	}
	if (!px || w <= 0 || h <= 0 || w > GK_MAX_TEX || h > GK_MAX_TEX || pitch < w)
		return GK_E_INVAL;
	if (t < 0) {
		for (t = 0; t < MAX_TEX && s_tex[t].name; t++)
			;
		if (t == MAX_TEX)
			return GK_E_NOMEM;
	}
	armgl_make_current (s_ctx, s_surf[0].surf);
	if (!s_tex[t].name)
		glGenTextures (1, &s_tex[t].name);
	s_tex[t].w = (int) w;
	s_tex[t].h = (int) h;
	upload (s_tex[t].name, (int) w, (int) h, (const void *) px, (int) pitch, 1);
	return armgl_lost () ? GK_E_LOST : t;
}

static long be_texture_rect (long t, long x, long y, long w, long h, long px, long pitch)
{
	if (t < 0 || t >= MAX_TEX || !s_tex[t].name || !px || x < 0 || y < 0 || w <= 0 || h <= 0
	    || x + w > s_tex[t].w || y + h > s_tex[t].h)
		return GK_E_INVAL;
	armgl_make_current (s_ctx, s_surf[0].surf);
	glBindTexture (GL_TEXTURE_2D, s_tex[t].name);
	glPixelStorei (GL_UNPACK_ROW_LENGTH, (GLint) pitch);
	glTexSubImage2D (GL_TEXTURE_2D, 0, (GLint) x, (GLint) y, (GLsizei) w, (GLsizei) h, GL_RGBA, GL_UNSIGNED_BYTE,
			 (const void *) px);
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	return armgl_lost () ? GK_E_LOST : 0;
}

static void blend (unsigned mode)
{
	if (mode == GK_BLEND_NONE || mode > GK_BLEND_LAST) {
		glDisable (GL_BLEND);
		return;
	}
	glEnable (GL_BLEND);
	glBlendEquation (GL_FUNC_ADD);
	switch (mode) {
	case GK_BLEND_ALPHA: glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
	case GK_BLEND_ADD: glBlendFunc (GL_SRC_ALPHA, GL_ONE); break;
	case GK_BLEND_MUL: glBlendFunc (GL_DST_COLOR, GL_ZERO); break;
	case GK_BLEND_PREMUL: glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA); break;
	case GK_BLEND_MULCOL: glBlendFuncSeparate (GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE); break;
	case GK_BLEND_UNDER: glBlendFuncSeparate (GL_ONE_MINUS_DST_ALPHA, GL_ONE, GL_ONE_MINUS_DST_ALPHA, GL_ONE); break;
	case GK_BLEND_SCREEN: glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_COLOR); break;
	case GK_BLEND_PLUS: glBlendFunc (GL_ONE, GL_ONE); break;
	case GK_BLEND_RSUB:
		glBlendEquationSeparate (GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
		glBlendFuncSeparate (GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
		break;
	case GK_BLEND_LIGHTEN: glBlendEquation (GL_MAX); glBlendFunc (GL_ONE, GL_ONE); break;
	case GK_BLEND_DSTIN: glBlendFunc (GL_ZERO, GL_SRC_ALPHA); break;
	case GK_BLEND_DSTOUT: glBlendFunc (GL_ZERO, GL_ONE_MINUS_SRC_ALPHA); break;
	}
}

static const float I16[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

static long be_draw (long frame, long verts, long n, long batches, long nb, long texs)
{
	const struct gk_frame *F = (const struct gk_frame *) frame;
	const struct gk_vertex *v = (const struct gk_vertex *) verts;
	const struct gk_batch *B = (const struct gk_batch *) batches;
	const struct gk_tex *T = (const struct gk_tex *) texs;
	void *surf = surface (F->w, F->h);
	if (!surf || !armgl_make_current (s_ctx, surf))
		return GK_E_NOGPU;
	s_age++;
	glUseProgram (s_prog);
	glBindVertexArray (s_vao);
	glBindBuffer (GL_ARRAY_BUFFER, s_vbo);
	glViewport (0, 0, F->w, F->h);
	glDisable (GL_SCISSOR_TEST);
	glDepthMask (GL_TRUE);
	glClearDepthf (1.0f);
	glClearColor ((F->clear >> 16 & 255) / 255.0f, (F->clear >> 8 & 255) / 255.0f, (F->clear & 255) / 255.0f, 1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	/* les sommets du programme, puis les 6 du fond (GK_F_KEEP : l'image de la cible d'abord) */
	glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (n + 6) * 32, NULL, GL_STREAM_DRAW);
	if (n)
		glBufferSubData (GL_ARRAY_BUFFER, 0, (GLsizeiptr) n * 32, v);
	glActiveTexture (GL_TEXTURE0);
	if (F->flags & GK_F_KEEP) {
		static const float q[6][2] = { {-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1} };
		struct gk_vertex bg[6];
		memset (bg, 0, sizeof bg);
		for (int i = 0; i < 6; i++) {
			bg[i].x = q[i][0];
			bg[i].y = q[i][1];
			bg[i].w = 1;
			bg[i].s = (q[i][0] + 1) * 0.5f;
			bg[i].t = (1 - q[i][1]) * 0.5f;		/* t = 0 : la ligne du haut */
			memset (bg[i].rgba, 255, 4);
		}
		glBufferSubData (GL_ARRAY_BUFFER, (GLintptr) n * 32, sizeof bg, bg);
		upload (s_bg, F->w, F->h, F->px, F->stride, 1);
		tex_params (0);
		glDisable (GL_DEPTH_TEST);
		glDisable (GL_CULL_FACE);
		glDisable (GL_BLEND);
		glUniformMatrix4fv (u_m, 1, GL_FALSE, I16);
		glUniform1i (u_use, 1);
		glUniform1f (u_aref, -1.0f);
		glDrawArrays (GL_TRIANGLES, (GLint) n, 6);
	}
	static const GLenum zfunc[8] = { GL_LESS, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL,
					 GL_ALWAYS };
	glEnable (GL_DEPTH_TEST);
	for (long i = 0; i < nb; i++) {
		const struct gk_batch *b = &B[i];
		unsigned fl = b->flags;
		if (b->first >= (unsigned long) n || !b->count)
			continue;
		unsigned count = b->count / 3 * 3;
		if (b->first + count > (unsigned long) n)
			count = (unsigned) ((n - b->first) / 3 * 3);
		unsigned zf = fl & GK_B_ZFUNC;
		glDepthFunc (zfunc[zf]);
		glDepthMask ((fl & GK_B_NOZWRITE) || zf == GK_Z_ALWAYS ? GL_FALSE : GL_TRUE);
		if (fl & (GK_B_CULL_BACK | GK_B_CULL_FRONT)) {
			glEnable (GL_CULL_FACE);
			glCullFace ((fl & GK_B_CULL_BACK) && (fl & GK_B_CULL_FRONT) ? GL_FRONT_AND_BACK
				    : (fl & GK_B_CULL_BACK) ? GL_BACK : GL_FRONT);
		} else
			glDisable (GL_CULL_FACE);
		blend ((fl >> GK_B_BLEND_SHIFT) & 15);
		glUniformMatrix4fv (u_m, 1, (fl & GK_B_NOMATRIX) ? GL_FALSE : GL_TRUE, (fl & GK_B_NOMATRIX) ? I16 : b->m);
		glUniform1f (u_aref, (fl & GK_B_ALPHATEST) ? (float) ((fl >> GK_B_AT_SHIFT) & 255) / 255.0f - 1e-4f : -1.0f);
		GLuint tn = 0;
		if (b->tex >= 0) {
			if (T) {
				const struct gk_tex *t = &T[b->tex];
				if (t->id >= 0 && t->id < MAX_TEX && s_tex[t->id].name)
					tn = s_tex[t->id].name;
				else if (t->px && t->w > 0 && t->h > 0)
					tn = px_texture (t);
			} else if (b->tex < MAX_TEX)
				tn = s_tex[b->tex].name;
		}
		glUniform1i (u_use, tn != 0);
		if (tn) {
			glBindTexture (GL_TEXTURE_2D, tn);
			tex_params (fl);
		}
		glDrawArrays (GL_TRIANGLES, (GLint) b->first, (GLsizei) count);
	}
	/* relue dans la cible : 0xAARRGGBB, ligne du haut d'abord -> 0x00RRGGBB */
	size_t need = (size_t) F->w * F->h;
	if (need > s_readCap) {
		free (s_read);
		s_read = malloc (need * 4);
		s_readCap = s_read ? need : 0;
		if (!s_read)
			return GK_E_NOMEM;
	}
	if (armgl_read (surf, s_read, F->w * 4) || armgl_lost ())
		return GK_E_LOST;
	for (int y = 0; y < F->h; y++) {
		const unsigned *s = s_read + (size_t) y * F->w;
		unsigned *d = F->px + (long) y * F->stride;
		for (int x = 0; x < F->w; x++)
			d[x] = s[x] & 0xFFFFFF;
	}
	return 1;
}

static long be_info (long *out)
{
	out[GKI_VERSION / 8] = s_mode == ARMGL_SOFT ? 0 : 12;	/* (Gen12 LP) */
	return 0;
}

static const struct gk_backend s_backend = { GK_BACKEND_VERSION, be_texture, be_texture_rect, be_draw, be_info };

const char *gk_gl_describe (void)
{
	return s_desc[0] ? s_desc : "le processeur (GpuKit sans GlKit)";
}

long gk_gl_attach (void)
{
	if (s_ctx)
		return 1;
	const char *e = getenv ("KORIN_GPUKIT");
	int soft = e && !strcmp (e, "soft");
	if (e && !strcmp (e, "cpu"))
		return 0;
	s_mode = armgl_init (soft ? 0 : ARMGL_F_NOSOFT);
	if (s_mode != ARMGL_GPU && s_mode != ARMGL_SOFT)
		return 0;
	s_ctx = armgl_context (3, 0, NULL);
	if (!s_ctx || !surface (64, 64) || !armgl_make_current (s_ctx, s_surf[0].surf)) {
		s_ctx = NULL;
		return 0;
	}
	GLuint vs = shader (GL_VERTEX_SHADER, vs_src), fs = shader (GL_FRAGMENT_SHADER, fs_src);
	if (!vs || !fs) {
		s_ctx = NULL;
		return 0;
	}
	s_prog = glCreateProgram ();
	glAttachShader (s_prog, vs);
	glAttachShader (s_prog, fs);
	glLinkProgram (s_prog);
	GLint ok = 0;
	glGetProgramiv (s_prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		s_ctx = NULL;
		return 0;
	}
	u_m = glGetUniformLocation (s_prog, "M");
	u_use = glGetUniformLocation (s_prog, "useT");
	u_aref = glGetUniformLocation (s_prog, "aref");
	glUseProgram (s_prog);
	glUniform1i (glGetUniformLocation (s_prog, "T"), 0);
	glGenVertexArrays (1, &s_vao);
	glBindVertexArray (s_vao);
	glGenBuffers (1, &s_vbo);
	glBindBuffer (GL_ARRAY_BUFFER, s_vbo);
	glVertexAttribPointer (0, 4, GL_FLOAT, GL_FALSE, 32, (void *) 0);
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 32, (void *) 16);
	glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *) 24);
	glVertexAttribPointer (3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *) 28);
	for (int i = 0; i < 4; i++)
		glEnableVertexAttribArray (i);
	glGenTextures (1, &s_bg);
	if (gk_debug (GK_DBG_BACKEND, (long) &s_backend, 0) != 0) {
		s_ctx = NULL;
		return 0;
	}
	extern const char *korin_i915_describe (void);
	snprintf (s_desc, sizeof s_desc, "%s", korin_i915_describe ());
	return 1;
}
