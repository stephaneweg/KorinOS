//
// n3ds/n3ds_gl.cpp -- le GPU de la 3DS sur Mesa : une cible du jeu est un FBO, tenu d'une image a l'autre ; un lot est
// un programme (GLSL genere par configuration, n3ds_glsl.cpp), ses uniformes, ses textures et son etat. Un programme
// nouveau est compile tant que l'image n'a pas depasse son budget ; sinon le lot passe par son frere reduit, et le
// vrai attend l'image suivante : le jeu ne saccade pas. Cible et lecture sont en octets B G R A, ceux de 0xAARRGGBB.
// Le contexte vit sur le thread de la machine. Partage entre ARMPI (armgl) et le rejeu sur le PC (n3ds_replay.cpp) :
// n3ds_gl_begin prend la fonction qui donne les fonctions de GL.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#ifdef N3DS_ARMPI
#include "armgl.h"
#endif
#define GL_GLES_PROTOTYPES 0
#include <GLES3/gl3.h>
#include "n3ds/n3ds_gl.h"

using namespace n3ds;

#define GLFN(X) X(ACTIVETEXTURE, ActiveTexture) X(ATTACHSHADER, AttachShader) X(BINDATTRIBLOCATION, BindAttribLocation) X(BINDBUFFER, BindBuffer) \
	X(BINDFRAMEBUFFER, BindFramebuffer) X(BINDRENDERBUFFER, BindRenderbuffer) X(BINDTEXTURE, BindTexture) X(BINDVERTEXARRAY, BindVertexArray) X(BLENDCOLOR, BlendColor) \
	X(BLENDEQUATIONSEPARATE, BlendEquationSeparate) X(BLENDFUNCSEPARATE, BlendFuncSeparate) X(BUFFERDATA, BufferData) X(BUFFERSUBDATA, BufferSubData) \
	X(CHECKFRAMEBUFFERSTATUS, CheckFramebufferStatus) X(CLEAR, Clear) X(CLEARCOLOR, ClearColor) X(CLEARDEPTHF, ClearDepthf) X(CLEARSTENCIL, ClearStencil) \
	X(COLORMASK, ColorMask) X(COMPILESHADER, CompileShader) X(CREATEPROGRAM, CreateProgram) X(CREATESHADER, CreateShader) X(CULLFACE, CullFace) \
	X(DELETEFRAMEBUFFERS, DeleteFramebuffers) X(DELETEPROGRAM, DeleteProgram) X(DELETERENDERBUFFERS, DeleteRenderbuffers) X(DELETESHADER, DeleteShader) X(DELETETEXTURES, DeleteTextures) \
	X(DEPTHFUNC, DepthFunc) X(DEPTHMASK, DepthMask) X(DISABLE, Disable) X(DISABLEVERTEXATTRIBARRAY, DisableVertexAttribArray) X(DRAWARRAYS, DrawArrays) X(DRAWELEMENTS, DrawElements) \
	X(ENABLE, Enable) X(ENABLEVERTEXATTRIBARRAY, EnableVertexAttribArray) X(FINISH, Finish) X(FRAMEBUFFERRENDERBUFFER, FramebufferRenderbuffer) X(FRAMEBUFFERTEXTURE2D, FramebufferTexture2D) \
	X(FRONTFACE, FrontFace) X(GENBUFFERS, GenBuffers) X(GENFRAMEBUFFERS, GenFramebuffers) X(GENRENDERBUFFERS, GenRenderbuffers) X(GENTEXTURES, GenTextures) X(GENVERTEXARRAYS, GenVertexArrays) \
	X(GETERROR, GetError) X(GETPROGRAMINFOLOG, GetProgramInfoLog) X(GETPROGRAMIV, GetProgramiv) X(GETSHADERINFOLOG, GetShaderInfoLog) X(GETSHADERIV, GetShaderiv) X(GETUNIFORMLOCATION, GetUniformLocation) \
	X(LINKPROGRAM, LinkProgram) X(PIXELSTOREI, PixelStorei) X(READPIXELS, ReadPixels) X(RENDERBUFFERSTORAGE, RenderbufferStorage) X(SCISSOR, Scissor) X(SHADERSOURCE, ShaderSource) \
	X(STENCILFUNC, StencilFunc) X(STENCILMASK, StencilMask) X(STENCILOP, StencilOp) X(TEXIMAGE2D, TexImage2D) X(TEXPARAMETERI, TexParameteri) X(TEXSUBIMAGE2D, TexSubImage2D) \
	X(UNIFORM1I, Uniform1i) X(UNIFORM1IV, Uniform1iv) X(UNIFORM4FV, Uniform4fv) X(USEPROGRAM, UseProgram) X(VERTEXATTRIBPOINTER, VertexAttribPointer) X(VIEWPORT, Viewport)
#define DECL(U, N) static PFNGL##U##PROC gl##N;
GLFN (DECL)

enum { TARGET_MAX = 8, PROG_HASH = 2048, TEX_MAX = 96, ATTR_MAX = 16 };
// (le budget d'une image : au-dela, les programmes nouveaux attendent)
static double g_budgetMs = 3.0;

struct Target { u32 color; int w, h; GLuint fbo, tex, depth; u64 used; };
struct Prog { u32 key; GLuint p; bool ok, tried; GLint uU, uC; };
static struct
{
	Target t[TARGET_MAX]; u64 clock;
	GLuint vbo, vao; u32 vboBytes;
	Prog prog[PROG_HASH];
	GLuint tex[TEX_MAX]; u32 texSerial[TEX_MAX], texW[TEX_MAX], texH[TEX_MAX], texWrap[TEX_MAX]; bool texMade[TEX_MAX];
	GLuint procMap[8], procColor[8]; u32 procSerial[8];
	bool on, nearest;
	int attrs;
	GLint lastC;
	u64 spent;					// le temps de compilation de cette image
	u64 frames, compiled, simple, usDraw, usRead, vsNotReady;
} g;

static u64 usNow () { struct timeval t; gettimeofday (&t, 0); return (u64) t.tv_sec * 1000000ull + (u64) t.tv_usec; }

static GLuint shader (GLenum kind, const char *text)
{
	GLuint s = glCreateShader (kind);
	glShaderSource (s, 1, &text, 0);
	glCompileShader (s);
	GLint ok = 0;
	glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	if (ok) return s;
	static char log[1024];
	glGetShaderInfoLog (s, sizeof log, 0, log);
	fprintf (stderr, "n3ds_gl : shader refuse : %s\n%s\n", log, text);
	glDeleteShader (s);
	return 0;
}

// Le vertex shader des lots que le processeur a ombres : la position, puis les varyings, par vec4. y retourne :
// la ligne 0 du FBO est le haut de l'image.
static void vertexText (char *d, int cap, int nv)
{
	int n = snprintf (d, (size_t) cap, "#version 300 es\nin vec4 a;\n");
	for (int i = 0; i < nv; i++) n += snprintf (d + n, (size_t) (cap - n), "in vec4 a%d;\nout vec4 v%d;\n", i, i);
	n += snprintf (d + n, (size_t) (cap - n), "void main(){gl_Position=vec4(a.x,-a.y,a.z,a.w);\n");
	for (int i = 0; i < nv; i++) n += snprintf (d + n, (size_t) (cap - n), "v%d=a%d;\n", i, i);
	snprintf (d + n, (size_t) (cap - n), "}\n");
}

// nv : les vec4 de varyings, vi : les vec4 d'entrees d'un vertex shader du jeu (-1 : un lot deja ombre)
static GLuint link (const char *vs, const char *fs, int nv, GLint *uU, int vi = -1)
{
	GLuint v = shader (GL_VERTEX_SHADER, vs), f = v ? shader (GL_FRAGMENT_SHADER, fs) : 0;
	if (!f) { if (v) glDeleteShader (v); return 0; }
	GLuint p = glCreateProgram ();
	glAttachShader (p, v); glAttachShader (p, f);
	if (vi >= 0) for (int i = 0; i < vi; i++) { char nm[8]; snprintf (nm, sizeof nm, "i%d", i); glBindAttribLocation (p, (GLuint) i, nm); }
	else
	{
		glBindAttribLocation (p, 0, "a");
		for (int i = 0; i < nv; i++) { char nm[8]; snprintf (nm, sizeof nm, "a%d", i); glBindAttribLocation (p, (GLuint) (i + 1), nm); }
	}
	glLinkProgram (p);
	glDeleteShader (v); glDeleteShader (f);
	GLint ok = 0;
	glGetProgramiv (p, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		static char log[1024];
		glGetProgramInfoLog (p, sizeof log, 0, log);
		fprintf (stderr, "n3ds_gl : lien refuse : %s\n", log);
		glDeleteProgram (p);
		return 0;
	}
	glUseProgram (p);
	*uU = glGetUniformLocation (p, "u");
	g.lastC = glGetUniformLocation (p, "c");
	for (int i = 0; i < 3; i++) { char nm[4] = { 't', (char) ('0' + i), 0, 0 }; GLint l = glGetUniformLocation (p, nm); if (l >= 0) glUniform1i (l, i); }
	{ GLint l = glGetUniformLocation (p, "tm"); if (l >= 0) glUniform1i (l, 3); l = glGetUniformLocation (p, "tc"); if (l >= 0) glUniform1i (l, 4); }
	return p;
}

static Prog *slot (u32 key)
{
	u32 h = (key * 2654435761u) >> 21;
	for (int i = 0; i < PROG_HASH; i++)
	{
		Prog &p = g.prog[(h + (u32) i) & (PROG_HASH - 1)];
		if (!p.tried || p.key == key) { p.key = key; return &p; }
	}
	return 0;
}

// Le programme d'un lot. Le budget depasse : le frere reduit, sauf si c'est la derniere image a l'attendre.
static Prog *program (const GpuFrame *f, const GpuBatch &b, u64 t0, bool &reduced)
{
	const u32 key = b.program << 12 | (u32) (b.vprog + 1);
	Prog *p = slot (key << 1);
	reduced = false;
	if (!p) return 0;
	if (p->tried) return p->ok ? p : 0;
	const GpuProgramInfo &P = f->programs[b.program];
	const char *text = (const char *) P.words;
	(void) t0;
	if ((double) g.spent > g_budgetMs * 1000.0 && b.vprog < 0)
	{
		Prog *q = slot (key << 1 | 1);
		reduced = true;
		if (!q) return 0;
		if (!q->tried)
		{
			static char vs[4096];
			vertexText (vs, sizeof vs, (int) (P.nVary + 3) / 4);
			q->p = link (vs, text + strlen (text) + 1, (int) (P.nVary + 3) / 4, &q->uU);
			q->tried = true; q->ok = q->p != 0;
			g.simple++;
		}
		return q->ok ? q : 0;
	}
	p->tried = true;
	const u64 t1 = usNow ();
	if (b.vprog >= 0)
	{
		const GpuVProgramInfo &V = f->vprograms[b.vprog];
		p->p = link ((const char *) V.vs, text, (int) (P.nVary + 3) / 4, &p->uU, (int) (V.inputs + 3) / 4);
		p->uC = g.lastC;
	}
	else
	{
		static char vs[4096];
		vertexText (vs, sizeof vs, (int) (P.nVary + 3) / 4);
		p->p = link (vs, text, (int) (P.nVary + 3) / 4, &p->uU);
	}
	p->ok = p->p != 0;
	g.compiled++;
	g.spent += usNow () - t1;
	return p->ok ? p : 0;
}

// (Machine::gpuVSReady) le vertex shader d'un lot : compile tant que l'image a du temps, sinon le processeur ombre
static bool vsReady (void *, u32 vprog, const GpuVProgramInfo *v, u32 program, const GpuProgramInfo *f)
{
	Prog *p = slot ((program << 12 | (vprog + 1)) << 1);
	if (!p) return false;
	if (p->tried) return p->ok;
	if ((double) g.spent > g_budgetMs * 1000.0) { g.vsNotReady++; return false; }
	p->tried = true;
	const u64 t1 = usNow ();
	p->p = link ((const char *) v->vs, (const char *) f->words, (int) (f->nVary + 3) / 4, &p->uU, (int) (v->inputs + 3) / 4);
	p->uC = g.lastC;
	p->ok = p->p != 0;
	g.compiled++;
	g.spent += usNow () - t1;
	return p->ok;
}

static Target *target (u32 color, int w, int h)
{
	Target *o = &g.t[0];
	g.clock++;
	for (int i = 0; i < TARGET_MAX; i++)
	{
		Target &t = g.t[i];
		if (t.color == color && t.w == w && t.h == h && t.fbo) { t.used = g.clock; return &t; }
		if (t.used < o->used) o = &t;
	}
	Target &t = *o;
	if (t.fbo) { glDeleteFramebuffers (1, &t.fbo); glDeleteTextures (1, &t.tex); glDeleteRenderbuffers (1, &t.depth); }
	t.color = color; t.w = w; t.h = h; t.used = g.clock;
	glGenFramebuffers (1, &t.fbo); glGenTextures (1, &t.tex); glGenRenderbuffers (1, &t.depth);
	glActiveTexture (GL_TEXTURE0);
	glBindTexture (GL_TEXTURE_2D, t.tex);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glBindRenderbuffer (GL_RENDERBUFFER, t.depth);
	glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
	glBindFramebuffer (GL_FRAMEBUFFER, t.fbo);
	glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
	glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.depth);
	if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fprintf (stderr, "n3ds_gl : FBO incomplet\n");
	return &t;
}

static void texture (int unit, const GpuTextureInfo &T, int slotIdx, u32 flags)
{
	glActiveTexture (GL_TEXTURE0 + (GLenum) unit);
	if (!g.tex[slotIdx]) glGenTextures (1, &g.tex[slotIdx]);
	glBindTexture (GL_TEXTURE_2D, g.tex[slotIdx]);
	if (!g.texMade[slotIdx] || g.texW[slotIdx] != T.w || g.texH[slotIdx] != T.h)
	{
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei) T.w, (GLsizei) T.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, T.px);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		g.texMade[slotIdx] = true; g.texW[slotIdx] = T.w; g.texH[slotIdx] = T.h; g.texSerial[slotIdx] = T.serial; g.texWrap[slotIdx] = ~0u;
	}
	else if (g.texSerial[slotIdx] != T.serial)
	{
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, (GLsizei) T.w, (GLsizei) T.h, GL_RGBA, GL_UNSIGNED_BYTE, T.px);
		g.texSerial[slotIdx] = T.serial;
	}
	const u32 wrap = (flags >> 13 & 15) | (g.nearest ? 0 : (flags & 3) << 4);
	if (g.texWrap[slotIdx] != wrap)
	{
		static const GLint W[3] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT };
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, W[wrap & 3]);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, W[wrap >> 2 & 3]);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (wrap & 16) ? GL_LINEAR : GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (wrap & 32) ? GL_LINEAR : GL_NEAREST);
		g.texWrap[slotIdx] = wrap;
	}
}

// les tables de la texture procedurale : deux textures de plus, unites 3 et 4
static void procTables (const GpuProcInfo &P, int s)
{
	const bool fresh = !g.procMap[s];
	if (fresh) { glGenTextures (1, &g.procMap[s]); glGenTextures (1, &g.procColor[s]); }
	glActiveTexture (GL_TEXTURE3);
	glBindTexture (GL_TEXTURE_2D, g.procMap[s]);
	if (fresh)
	{
		glTexImage2D (GL_TEXTURE_2D, 0, GL_R32F, 128, 4, 0, GL_RED, GL_FLOAT, P.map);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	else if (g.procSerial[s] != P.serial) glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 128, 4, GL_RED, GL_FLOAT, P.map);
	glActiveTexture (GL_TEXTURE4);
	glBindTexture (GL_TEXTURE_2D, g.procColor[s]);
	if (fresh)
	{
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, P.colors);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	}
	else if (g.procSerial[s] != P.serial) glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, P.colors);
	g.procSerial[s] = P.serial;
}

static void stateOf (const GpuBatch &b)
{
	static const GLenum ZF[8] = { GL_LESS, GL_ALWAYS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
	const u32 zf = b.flags & 7;
	if (zf == 7 && (b.flags & 8)) glDisable (GL_DEPTH_TEST);
	else { glEnable (GL_DEPTH_TEST); glDepthFunc (ZF[zf]); }
	glDepthMask ((b.flags & 8) ? GL_FALSE : GL_TRUE);
	if (b.flags & (16 | 32)) { glEnable (GL_CULL_FACE); glCullFace ((b.flags & 16) ? GL_BACK : GL_FRONT); }
	else glDisable (GL_CULL_FACE);
	// (cible en B G R A : le rouge du jeu est le bit 2 d'ici)
	glColorMask ((b.wmask & 4) ? GL_FALSE : GL_TRUE, (b.wmask & 2) ? GL_FALSE : GL_TRUE, (b.wmask & 1) ? GL_FALSE : GL_TRUE, (b.wmask & 8) ? GL_FALSE : GL_TRUE);
	if (b.blend)
	{
		static const GLenum F[15] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
			GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_SRC_ALPHA_SATURATE };
		static const GLenum E[5] = { GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX };
		glEnable (GL_BLEND);
		glBlendFuncSeparate (F[b.blend >> 4 & 15 & 15] , F[b.blend >> 8 & 15], F[b.blend >> 12 & 15], F[b.blend >> 16 & 15]);
		glBlendEquationSeparate (E[b.blend >> 20 & 7], E[b.blend >> 24 & 7]);
		glBlendColor ((float) (b.blendColor >> 16 & 255) / 255.0f, (float) (b.blendColor >> 8 & 255) / 255.0f, (float) (b.blendColor & 255) / 255.0f, (float) (b.blendColor >> 24) / 255.0f);
	}
	else glDisable (GL_BLEND);
	if (b.scissor[2] || b.scissor[3]) { glEnable (GL_SCISSOR_TEST); glScissor (b.scissor[0], b.scissor[1], b.scissor[2], b.scissor[3]); }
	else glDisable (GL_SCISSOR_TEST);
	if (b.stencil[0])
	{
		static const GLenum SF[8] = { GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL, GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL };
		static const GLenum SO[8] = { GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP };
		glEnable (GL_STENCIL_TEST);
		glStencilFunc (SF[b.stencil[0] >> 4 & 7], (GLint) (b.stencil[0] >> 8 & 255), b.stencil[0] >> 16 & 255);
		glStencilMask (b.stencil[1] & 255);
		glStencilOp (SO[b.stencil[1] >> 8 & 7], SO[b.stencil[1] >> 12 & 7], SO[b.stencil[1] >> 16 & 7]);
	}
	else glDisable (GL_STENCIL_TEST);
}

static bool glDraw (void *, const GpuFrame *f, u32 *pixels)
{
	const u64 t0 = usNow ();
	Target *t = target (f->color, f->w, f->h);
	glBindFramebuffer (GL_FRAMEBUFFER, t->fbo);
	glViewport (0, 0, f->w, f->h);
	glDisable (GL_SCISSOR_TEST);
	glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask (GL_TRUE);
	glStencilMask (255);
	GLbitfield clear = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
	if (f->stale && f->fillOn)
	{
		glClearColor ((float) (f->fill & 255) / 255.0f, (float) (f->fill >> 8 & 255) / 255.0f, (float) (f->fill >> 16 & 255) / 255.0f, (float) (f->fill >> 24) / 255.0f);
		clear |= GL_COLOR_BUFFER_BIT;
	}
	else if (f->stale)
	{
		glActiveTexture (GL_TEXTURE0);
		glBindTexture (GL_TEXTURE_2D, t->tex);
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, f->w, f->h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	}
	glClearDepthf (1.0f); glClearStencil (0);
	glClear (clear);
	glFrontFace (GL_CW);						// (y retourne dans le vertex shader)
	const u32 bytes = f->nFloats * 4;
	glBindBuffer (GL_ARRAY_BUFFER, g.vbo);
	if (bytes > g.vboBytes) { g.vboBytes = bytes + bytes / 2; glBufferData (GL_ARRAY_BUFFER, g.vboBytes, 0, GL_STREAM_DRAW); }
	glBufferSubData (GL_ARRAY_BUFFER, 0, bytes, f->v);
	GLuint last = 0;
	for (u32 i = 0; i < f->nb; i++)
	{
		const GpuBatch &b = f->b[i];
		if (!b.count) continue;
		bool reduced;
		Prog *p = program (f, b, t0, reduced);
		if (!p) return false;
		if (p->p != last) { glUseProgram (p->p); last = p->p; }
		if (p->uU >= 0 && b.nUni) glUniform1iv (p->uU, (GLsizei) b.nUni, (const GLint *) (f->u + b.uni));
		if (b.vprog >= 0 && p->uC >= 0 && b.vsNUni) glUniform4fv (p->uC, (GLsizei) (b.vsNUni / 4), (const GLfloat *) (f->u + b.vsUni));
		stateOf (b);
		for (int k = 0; k < 3; k++) if (b.tex[k] >= 0) texture (k, f->textures[b.tex[k]], b.tex[k], b.texFlags[k]);
		if (b.proc >= 0 && (u32) b.proc < f->nProcs) procTables (f->procs[b.proc], b.proc);
		const GLsizei stride = (GLsizei) b.stride * 4;
		const char *base = (const char *) (size_t) (b.off * 4);
		const int attrs = (int) (b.stride + 3) / 4;
		for (int k = g.attrs; k < attrs; k++) glEnableVertexAttribArray ((GLuint) k);
		for (int k = attrs; k < g.attrs; k++) glDisableVertexAttribArray ((GLuint) k);
		g.attrs = attrs;
		for (int k = 0; k < attrs && k < ATTR_MAX; k++)
		{
			const int left = (int) b.stride - 4 * k;
			glVertexAttribPointer ((GLuint) k, left < 4 ? left : 4, GL_FLOAT, GL_FALSE, stride, base + 16 * k);
		}
		if (b.nVerts) glDrawElements (GL_TRIANGLES, (GLsizei) b.count, GL_UNSIGNED_INT, (const void *) (size_t) ((b.off + b.nVerts * b.stride) * 4));
		else glDrawArrays (GL_TRIANGLES, 0, (GLsizei) b.count);
	}
	glDisable (GL_SCISSOR_TEST);
	const u64 t1 = usNow ();
	glPixelStorei (GL_PACK_ALIGNMENT, 4);
	glReadPixels (0, 0, f->w, f->h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	g.frames++; g.usDraw += t1 - t0; g.usRead += usNow () - t1; g.spent = 0;
	return glGetError () == GL_NO_ERROR;
}

bool n3ds_gl_draw (const GpuFrame *f, u32 *pixels) { return glDraw (0, f, pixels); }
void n3ds_gl_reset () { for (int i = 0; i < PROG_HASH; i++) { if (g.prog[i].p) glDeleteProgram (g.prog[i].p); memset (&g.prog[i], 0, sizeof g.prog[i]); } }

bool n3ds_gl_begin (void *(*proc) (const char *), Machine *m)
{
	bool ok = true;
#define LOAD(U, N) gl##N = (PFNGL##U##PROC) proc ("gl" #N); if (!gl##N) { fprintf (stderr, "n3ds_gl : pas de gl" #N "\n"); ok = false; }
	GLFN (LOAD)
	if (!ok) return false;
	memset (&g, 0, sizeof g);
	glGenVertexArrays (1, &g.vao); glBindVertexArray (g.vao);
	glGenBuffers (1, &g.vbo);
	glBindBuffer (GL_ARRAY_BUFFER, g.vbo);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, g.vbo);
	g.on = true;
	if (getenv ("N3DS_GL_NOBUDGET")) g_budgetMs = 1e9;
	g.nearest = getenv ("N3DS_GL_NEAREST") != 0;		// (pour comparer au rendu logiciel, qui ne filtre pas)
	if (m)
	{
		m->gpuDraw = glDraw; m->gpuUser = 0;
		m->gpuVS = !getenv ("N3DS_GL_NOVS"); m->gpuIndexed = m->gpuVS; m->gpuVSReady = vsReady;
		m->parallelBegin = [] (void *, void (*) (void *, int), void *) { return false; };	// (pas d'aides : ce thread seul)
	}
	return true;
}

void n3ds_gl_say (FILE *o)
{
	if (g.frames) fprintf (o, "n3ds_gl : %llu images, %llu programmes, %llu reduits, %.2f ms par image, lecture %.2f ms\n", (unsigned long long) g.frames, (unsigned long long) g.compiled,
			      (unsigned long long) g.simple, (double) g.usDraw / (double) g.frames / 1000.0, (double) g.usRead / (double) g.frames / 1000.0);
}

int n3ds_gl_dry;
#ifdef N3DS_ARMPI
static void *g_ctx, *g_surf;
int n3ds_gl_setup (Machine *m)
{
	if (!g_ctx)
	{
		const int mode = armgl_init (n3ds_gl_dry ? ARMGL_F_DRY : 0);
		if (mode == ARMGL_NONE) { fprintf (stderr, "n3ds_gl : pas de GPU, rendu logiciel\n"); return 0; }
		g_ctx = armgl_context (3, 0, 0);
		g_surf = armgl_surface (16, 16);
		if (!g_ctx || !g_surf || !armgl_make_current (g_ctx, g_surf)) { g_ctx = 0; return 0; }
	}
	return n3ds_gl_begin (armgl_proc, m) ? 1 : 0;
}
void n3ds_gl_quit (Machine *) { n3ds_gl_say (stderr); }
#else
int n3ds_gl_setup (Machine *) { return 0; }
void n3ds_gl_quit (Machine *) {}
#endif
