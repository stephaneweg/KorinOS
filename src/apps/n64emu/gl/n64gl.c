/*
 * n64gl.c -- les lots du coeur N64 en OpenGL ES 3.0 : un shader de fragment par reglage du RDP, le combineur
 * par pixel et ses deux cycles, deux vraies textures, filtre a trois points, le blender autant que GL le
 * permet. Reglages deja vus : compiles d'avance (n64gl_keys_load) ; nouveaux : compiles au premier lot, de
 * l'ordre de 10 ms sur le Pi. Rien ici ne connait ARMPI : voir n64gl.h.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define GL_GLES_PROTOTYPES 0
#include <GLES3/gl3.h>
#include "n64gl.h"

#define GLFN(X) \
	X (PFNGLACTIVETEXTUREPROC, glActiveTexture) X (PFNGLATTACHSHADERPROC, glAttachShader) \
	X (PFNGLBINDBUFFERPROC, glBindBuffer) X (PFNGLBINDSAMPLERPROC, glBindSampler) \
	X (PFNGLBINDTEXTUREPROC, glBindTexture) X (PFNGLBINDVERTEXARRAYPROC, glBindVertexArray) \
	X (PFNGLBLENDFUNCPROC, glBlendFunc) X (PFNGLBUFFERDATAPROC, glBufferData) X (PFNGLCLEARPROC, glClear) \
	X (PFNGLCLEARCOLORPROC, glClearColor) X (PFNGLCLEARDEPTHFPROC, glClearDepthf) \
	X (PFNGLCOMPILESHADERPROC, glCompileShader) X (PFNGLCREATEPROGRAMPROC, glCreateProgram) \
	X (PFNGLCREATESHADERPROC, glCreateShader) X (PFNGLCULLFACEPROC, glCullFace) \
	X (PFNGLDELETESHADERPROC, glDeleteShader) X (PFNGLDEPTHFUNCPROC, glDepthFunc) \
	X (PFNGLDEPTHMASKPROC, glDepthMask) X (PFNGLDISABLEPROC, glDisable) \
	X (PFNGLDRAWARRAYSPROC, glDrawArrays) X (PFNGLENABLEPROC, glEnable) \
	X (PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) X (PFNGLFRONTFACEPROC, glFrontFace) \
	X (PFNGLGENBUFFERSPROC, glGenBuffers) X (PFNGLGENSAMPLERSPROC, glGenSamplers) \
	X (PFNGLGENTEXTURESPROC, glGenTextures) X (PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays) \
	X (PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog) X (PFNGLGETPROGRAMIVPROC, glGetProgramiv) \
	X (PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X (PFNGLGETSHADERIVPROC, glGetShaderiv) \
	X (PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X (PFNGLLINKPROGRAMPROC, glLinkProgram) \
	X (PFNGLSAMPLERPARAMETERIPROC, glSamplerParameteri) X (PFNGLSCISSORPROC, glScissor) \
	X (PFNGLSHADERSOURCEPROC, glShaderSource) X (PFNGLTEXIMAGE2DPROC, glTexImage2D) \
	X (PFNGLTEXPARAMETERIPROC, glTexParameteri) X (PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D) \
	X (PFNGLUNIFORM1IPROC, glUniform1i) X (PFNGLUNIFORM4FPROC, glUniform4f) X (PFNGLUNIFORM4IPROC, glUniform4i) \
	X (PFNGLUSEPROGRAMPROC, glUseProgram) X (PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) \
	X (PFNGLVIEWPORTPROC, glViewport)
#define X(t, n) static t n;
GLFN (X)
#undef X

/* key[2] */
#define K_CYCLE		3		/* 0 un cycle, 1 deux, 2 copie, 3 plein */
#define K_T0		4
#define K_T1		8
#define K_ACMP(k)	((k) >> 4 & 3)	/* comme G_SETOTHERMODE : 1 seuil, 3 tramage */
#define K_CVGSEL	0x40
#define K_CVGXA		0x80
#define K_FILT(k)	((k) >> 8 & 3)	/* 0 au plus proche, 1 bilineaire du TMU, 2 trois points */
/* key[3] */
#define K_B0(k)		((k) & 0xFF)		/* P A M B, 2 bits chacun, poids fort : P */
#define K_B1(k)		((k) >> 8 & 0xFF)
#define K_BLEND		0x10000

enum { IN, MEM, BL, FOG };			/* P, M */

struct prog {
	uint32_t key[4];
	int tried;
	GLuint p;
	GLint uT0, uT1, uPrim, uEnv, uFog, uBlend, uMisc, uSize;
	struct { float t0[4], t1[4], c[4][4], m[4], s[4]; } last;
};

#define NPROG 2048
static struct prog progs[NPROG];
static int nprog, ncompiled, nskipped;
static GLuint vshader, vao, vbo, empty, samplers[18];
static GLuint gltex[N64GL_MAX_TEX];
static int gltw[N64GL_MAX_TEX], glth[N64GL_MAX_TEX];
static int target_w, target_h, f3pt = 1;
static FILE *keyfile;
static float noise_seed;

static const char vs_text[] =
	"#version 300 es\n"
	"precision highp float;\n"
	"layout(location=0) in vec4 aP;\n"
	"layout(location=1) in vec2 aT;\n"
	"layout(location=2) in vec4 aS;\n"
	"uniform vec4 uT0,uT1;\n"
	"out vec2 vT0,vT1;\n"
	"out vec4 vS;\n"
	"void main(){gl_Position=aP;vS=aS;vT0=aT*uT0.xy-uT0.zw;vT1=aT*uT1.xy-uT1.zw;}\n";

/* commun aux fragments ; uMisc : lod de la primitive, pixels N64 par pixel, grain du bruit, seuil alpha */
static const char fs_head[] =
	"#version 300 es\n"
	"precision highp float;\n"
	"in vec2 vT0,vT1;\n"
	"in vec4 vS;\n"
	"uniform sampler2D uTex0,uTex1;\n"
	"uniform vec4 uPrim,uEnv,uFog,uBlend,uMisc,uSize;\n"
	"layout(location=0) out vec4 o;\n"
	"float noise(){return fract(sin(dot(gl_FragCoord.xy,vec2(12.9898,78.233))+uMisc.z)*43758.5453);}\n"
	"float lodf(){vec2 d=max(abs(dFdx(vT0)),abs(dFdy(vT0)))*uMisc.y;return clamp(max(d.x,d.y)-1.,0.,1.);}\n"
	"vec4 near(sampler2D t,vec2 st,vec2 is){return texture(t,(floor(st)+.5)*is);}\n"
	"vec4 line(sampler2D t,vec2 st,vec2 is){return texture(t,(st+.5)*is);}\n"
	"vec4 tri3(sampler2D t,vec2 st,vec2 is){\n"
	" vec2 b=floor(st),f=st-b;float m=step(1.,f.x+f.y);vec2 g=mix(f,1.-f,m);b=(b+.5)*is;\n"
	" vec4 c=texture(t,b+m*is),x=texture(t,b+vec2(1.-m,m)*is),y=texture(t,b+vec2(m,1.-m)*is);\n"
	" return c+g.x*(x-c)+g.y*(y-c);}\n";

/* -------- le texte -------------------------------------------------------------------------------- */

struct text { char *p; int n, size; };

static void put (struct text *t, const char *s)
{
	int l = strlen (s);
	if (t->n + l < t->size) {
		memcpy (t->p + t->n, s, l + 1);
		t->n += l;
	}
}

static void putf (struct text *t, const char *f, const char *a, const char *b, const char *c, const char *d)
{
	char line[256];
	snprintf (line, sizeof line, f, a, b, c, d);
	put (t, line);
}

/* les lettres du combineur : a, b, d rgb */
static const char *in_rgb (int v, int tab, int cycle)
{
	static const char *const names[8] = { "c.rgb", "t0.rgb", "t1.rgb", "uPrim.rgb", "vS.rgb", "uEnv.rgb", "vec3(1.)", "vec3(noise())" };
	if (cycle && (v == 1 || v == 2))	/* au 2e cycle, le RDP a avance d'une tuile */
		v ^= 3;
	if (tab == 1 && v == 7)
		return "vec3(0.)";		/* b : le centre de la clef, pas de clef ici */
	if (tab == 1 && v == 6)
		return "vec3(0.)";
	if (tab == 3)
		return v == 7 ? "vec3(0.)" : names[v];
	return v < 8 ? names[v] : "vec3(0.)";
}

static const char *in_c (int v, int cycle)
{
	static const char *const names[16] = { "c.rgb", "t0.rgb", "t1.rgb", "uPrim.rgb", "vS.rgb", "uEnv.rgb", "vec3(0.)", "c.aaa",
		"t0.aaa", "t1.aaa", "uPrim.aaa", "vS.aaa", "uEnv.aaa", "vec3(lodf())", "uMisc.xxx", "vec3(0.)" };
	if (cycle && (v == 1 || v == 2 || v == 8 || v == 9))
		v = v < 8 ? v ^ 3 : v ^ 1;
	return v < 16 ? names[v] : "vec3(0.)";
}

static const char *in_a (int v, int cycle)
{
	static const char *const names[8] = { "c.a", "t0.a", "t1.a", "uPrim.a", "vS.a", "uEnv.a", "1.", "0." };
	if (cycle && (v == 1 || v == 2))
		v ^= 3;
	return names[v];
}

static const char *in_ac (int v, int cycle)
{
	static const char *const names[8] = { "lodf()", "t0.a", "t1.a", "uPrim.a", "vS.a", "uEnv.a", "uMisc.x", "0." };
	if (cycle && (v == 1 || v == 2))
		v ^= 3;
	return names[v];
}

static void gen_cycle (struct text *t, uint32_t h, uint32_t l, int cycle)
{
	unsigned a, b, c, d, aa, ab, ac, ad;
	if (!cycle) {
		a = h >> 20 & 15; b = l >> 28; c = h >> 15 & 31; d = l >> 15 & 7;
		aa = h >> 12 & 7; ab = l >> 12 & 7; ac = h >> 9 & 7; ad = l >> 9 & 7;
	} else {
		a = h >> 5 & 15; b = l >> 24 & 15; c = h & 31; d = l >> 6 & 7;
		aa = l >> 21 & 7; ab = l >> 3 & 7; ac = l >> 18 & 7; ad = l & 7;
	}
	putf (t, " c=vec4(clamp((%s-%s)*%s+%s,0.,1.),", in_rgb (a, 0, cycle), in_rgb (b, 1, cycle), in_c (c, cycle), in_rgb (d, 3, cycle));
	putf (t, "clamp((%s-%s)*%s+%s,0.,1.));\n", in_a (aa, cycle), in_a (ab, cycle), in_ac (ac, cycle), in_a (ad, cycle));
}

static const char *const bl_col[4] = { "p", "p", "uBlend.rgb", "uFog.rgb" };	/* (MEM au 1er cycle : le pixel lui-meme) */
static const char *const bl_a[4] = { "c.a", "uFog.a", "vS.a", "0." };

#define B_P(x)	((x) >> 6 & 3)
#define B_A(x)	((x) >> 4 & 3)
#define B_M(x)	((x) >> 2 & 3)
#define B_B(x)	((x) & 3)

/* la sortie du blender vue par GL : ce que le shader sort, et les facteurs de glBlendFunc */
enum { OUT_P, OUT_SRC_DST, OUT_DST_SRC, OUT_CALC, OUT_DST2, OUT_KEEP };

static int final_kind (uint32_t k3)
{
	int x = K_B1 (k3), pm = B_P (x) == MEM, mm = B_M (x) == MEM;
	if (!(k3 & K_BLEND))
		return pm ? OUT_KEEP : OUT_P;
	if (!pm && mm)
		return OUT_SRC_DST;
	if (pm && !mm)
		return OUT_DST_SRC;
	return pm ? OUT_DST2 : OUT_CALC;
}

static const char *bl_b (int b)
{
	return b == 0 ? "(1.-a)" : b == 3 ? "0." : "1.";
}

static void gen_samples (struct text *t, uint32_t k2)
{
	static const char *const f[3] = { "near", "line", "tri3" };
	const char *fn = f[K_FILT (k2)];
	if (k2 & K_T0)
		putf (t, " vec4 t0=%s(uTex0,vT0,uSize.xy);\n", fn, 0, 0, 0);
	else
		put (t, " vec4 t0=vec4(0.);\n");
	if (k2 & K_T1)
		putf (t, " vec4 t1=%s(uTex1,vT1,uSize.zw);\n", fn, 0, 0, 0);
	else
		put (t, " vec4 t1=vec4(0.);\n");
}

static void gen_end (struct text *t, uint32_t k2, uint32_t k3)
{
	int x = K_B1 (k3), kind = final_kind (k3);
	if (K_ACMP (k2) == 1)
		put (t, " if(c.a<uMisc.w)discard;\n");
	else if (K_ACMP (k2) == 3)
		put (t, " if(c.a<noise())discard;\n");
	if ((k2 & K_CVGSEL) && (k2 & K_CVGXA))
		put (t, " if(c.a<.5)discard;\n");
	else if (k2 & K_CVGSEL)
		put (t, " c.a=1.;\n");
	put (t, " vec3 p=c.rgb;float a,b;\n");
	if ((k2 & K_CYCLE) == 1) {
		int y = K_B0 (k3);
		putf (t, " a=%s;b=%s;\n", bl_a[B_A (y)], bl_b (B_B (y)), 0, 0);
		putf (t, " p=%s*a+%s*b;\n", bl_col[B_P (y)], bl_col[B_M (y)], 0, 0);
	}
	putf (t, " a=%s;b=%s;\n", bl_a[B_A (x)], bl_b (B_B (x)), 0, 0);
	switch (kind) {
	case OUT_P:
	case OUT_SRC_DST:
		putf (t, " o=vec4(%s,a);\n", bl_col[B_P (x)], 0, 0, 0);
		break;
	case OUT_DST_SRC:
		putf (t, " o=vec4(%s,a);\n", bl_col[B_M (x)], 0, 0, 0);
		break;
	case OUT_CALC:
		putf (t, " o=vec4(%s*a+%s*b,a);\n", bl_col[B_P (x)], bl_col[B_M (x)], 0, 0);
		break;
	case OUT_DST2:
		put (t, " o=vec4(vec3(a),a);\n");
		break;
	default:
		put (t, " o=vec4(0.);\n");
	}
}

int n64gl_fs_source (const uint32_t key[4], char *out, int size)
{
	struct text t = { out, 0, size };
	int cyc = key[2] & K_CYCLE;
	out[0] = 0;
	put (&t, fs_head);
	put (&t, "void main(){\n");
	if (cyc == 3) {
		put (&t, " o=vS;\n}\n");
		return t.n;
	}
	gen_samples (&t, key[2]);
	if (cyc == 2) {
		put (&t, " vec4 c=t0;\n");
		if (key[2] >> 4 & 1)
			put (&t, " if(c.a<uMisc.w)discard;\n");
		put (&t, " o=c;\n}\n");
		return t.n;
	}
	put (&t, " vec4 c=vec4(0.);\n");
	gen_cycle (&t, key[0], key[1], 0);
	if (cyc == 1)
		gen_cycle (&t, key[0], key[1], 1);
	gen_end (&t, key[2], key[3]);
	put (&t, "}\n");
	return t.n;
}

/* -------- les programmes -------------------------------------------------------------------------- */

static GLuint shader (GLenum type, const char *text)
{
	GLuint s = glCreateShader (type);
	GLint ok = 0;
	glShaderSource (s, 1, &text, NULL);
	glCompileShader (s);
	glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];
		log[0] = 0;
		glGetShaderInfoLog (s, sizeof log, NULL, log);
		printf ("n64gl : shader refuse : %s\n%s\n", log, text);
		glDeleteShader (s);
		return 0;
	}
	return s;
}

static int link_prog (struct prog *g, const char *fs_text)
{
	GLuint fs = shader (GL_FRAGMENT_SHADER, fs_text), p;
	GLint ok = 0;
	g->p = 0;
	if (!fs)
		return 0;
	p = glCreateProgram ();
	glAttachShader (p, vshader);
	glAttachShader (p, fs);
	glLinkProgram (p);
	glDeleteShader (fs);
	glGetProgramiv (p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512];
		log[0] = 0;
		glGetProgramInfoLog (p, sizeof log, NULL, log);
		printf ("n64gl : programme refuse : %s\n", log);
		return 0;
	}
	g->p = p;
	glUseProgram (p);
	glUniform1i (glGetUniformLocation (p, "uTex0"), 0);
	glUniform1i (glGetUniformLocation (p, "uTex1"), 1);
	g->uT0 = glGetUniformLocation (p, "uT0");
	g->uT1 = glGetUniformLocation (p, "uT1");
	g->uPrim = glGetUniformLocation (p, "uPrim");
	g->uEnv = glGetUniformLocation (p, "uEnv");
	g->uFog = glGetUniformLocation (p, "uFog");
	g->uBlend = glGetUniformLocation (p, "uBlend");
	g->uMisc = glGetUniformLocation (p, "uMisc");
	g->uSize = glGetUniformLocation (p, "uSize");
	memset (&g->last, 0xFF, sizeof g->last);
	return 1;
}

static struct prog *find (const uint32_t key[4])
{
	uint32_t h = (key[0] * 31 + key[1]) * 2654435761u ^ (key[2] * 31 + key[3]) * 40503u;
	for (int i = 0; i < NPROG; i++) {
		struct prog *g = &progs[(h + i) % NPROG];
		if (!g->tried || !memcmp (g->key, key, 16))
			return g;
	}
	return NULL;
}

static int compile (struct prog *g, const uint32_t key[4])
{
	static char text[8192];
	memcpy (g->key, key, 16);
	nprog++;
	ncompiled++;
	n64gl_fs_source (key, text, sizeof text);
	if (keyfile) {
		fwrite (key, 16, 1, keyfile);
		fflush (keyfile);
	}
	return link_prog (g, text);
}

void n64gl_batch_key (const struct n64gl_batch *b, uint32_t k[4])
{
	int cyc = b->omH >> 20 & 3, zmode = b->omL >> 10 & 3, linear = b->flags >> 12 & 1;
	k[0] = b->combH;
	k[1] = b->combL;
	k[2] = cyc | (b->tex0 >= 0 ? K_T0 : 0) | (b->tex1 >= 0 ? K_T1 : 0);
	k[2] |= (linear ? (f3pt ? 2 : 1) : 0) << 8;
	k[3] = 0;
	if (cyc >= 2) {
		k[0] = k[1] = 0;
		k[2] &= ~(K_T1);
		if (cyc == 2)
			k[2] |= (b->omL & 1) << 4;
		else
			k[2] &= ~(K_T0 | 0x300);
		return;
	}
	k[2] |= (b->omL & 3) << 4;
	k[2] |= b->omL >> 13 & 1 ? K_CVGSEL : 0;
	k[2] |= b->omL >> 12 & 1 ? K_CVGXA : 0;
	if (cyc == 0) {
		k[0] &= 0xFFFE00;		/* un cycle : les reglages du second ne servent pas */
		k[1] &= 0xF003FE00;
	}
	uint32_t l = b->omL;
	if (cyc == 1) {
		k[3] = (l >> 30 & 3) << 6 | (l >> 26 & 3) << 4 | (l >> 22 & 3) << 2 | (l >> 18 & 3);
		k[3] |= ((l >> 28 & 3) << 6 | (l >> 24 & 3) << 4 | (l >> 20 & 3) << 2 | (l >> 16 & 3)) << 8;
	} else
		k[3] = ((l >> 30 & 3) << 6 | (l >> 26 & 3) << 4 | (l >> 22 & 3) << 2 | (l >> 18 & 3)) << 8;
	if ((l >> 14 & 1) || zmode == 2)
		k[3] |= K_BLEND;
}

static GLenum factor_b (int b)
{
	return b == 0 ? GL_ONE_MINUS_SRC_ALPHA : b == 3 ? GL_ZERO : GL_ONE;
}

/* -------- l'etat ----------------------------------------------------------------------------------- */

static struct { int blend; GLenum sf, df; int depth_func, depth_mask, cull; uint32_t sc0, sc1; GLuint prog, tex[2], samp[2]; } cur;

static void set_blend (uint32_t k3)
{
	int x = K_B1 (k3), kind = final_kind (k3), on = kind != OUT_P && kind != OUT_CALC;
	GLenum sf = GL_ONE, df = GL_ZERO;
	if (kind == OUT_SRC_DST)
		sf = GL_SRC_ALPHA, df = factor_b (B_B (x));
	else if (kind == OUT_DST_SRC)
		sf = factor_b (B_B (x)), df = GL_SRC_ALPHA;
	else if (kind == OUT_DST2)
		sf = GL_DST_COLOR, df = factor_b (B_B (x));
	else if (kind == OUT_KEEP)
		sf = GL_ZERO, df = GL_ONE;
	if (on != cur.blend) {
		cur.blend = on;
		(on ? glEnable : glDisable) (GL_BLEND);
	}
	if (on && (sf != cur.sf || df != cur.df)) {
		cur.sf = sf;
		cur.df = df;
		glBlendFunc (sf, df);
	}
}

static void set_flags (uint32_t f)
{
	static const GLenum funcs[8] = { GL_LESS, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
	int z = f & 7, mask = !(f >> 3 & 1), cull = f >> 4 & 3;
	if (z != cur.depth_func) {
		cur.depth_func = z;
		glDepthFunc (funcs[z]);
	}
	if (mask != cur.depth_mask) {
		cur.depth_mask = mask;
		glDepthMask (mask);
	}
	if (cull != cur.cull) {
		if (!cull)
			glDisable (GL_CULL_FACE);
		else {
			if (!cur.cull)
				glEnable (GL_CULL_FACE);
			glCullFace (cull == 1 ? GL_BACK : cull == 2 ? GL_FRONT : GL_FRONT_AND_BACK);
		}
		cur.cull = cull;
	}
}

static void set_scissor (const struct n64gl_frame *f, const struct n64gl_batch *b)
{
	if (b->sc0 == cur.sc0 && b->sc1 == cur.sc1)
		return;
	cur.sc0 = b->sc0;
	cur.sc1 = b->sc1;
	int x0 = (b->sc0 & 0xFFFF) * f->w / f->gw, y0 = (b->sc0 >> 16) * f->h / f->gh;
	int x1 = (b->sc1 & 0xFFFF) * f->w / f->gw, y1 = (b->sc1 >> 16) * f->h / f->gh;
	if (x1 > f->w)
		x1 = f->w;
	if (y1 > f->h)
		y1 = f->h;
	if (x1 < x0)
		x1 = x0;
	if (y1 < y0)
		y1 = y0;
	glScissor (x0, f->h - y1, x1 - x0, y1 - y0);
}

static GLuint sampler_for (uint32_t wrap, int linear)
{
	static const GLenum modes[3] = { GL_REPEAT, GL_CLAMP_TO_EDGE, GL_MIRRORED_REPEAT };
	int ws = wrap >> 13 & 3, wt = wrap >> 15 & 3;
	if (ws > 2)
		ws = 0;
	if (wt > 2)
		wt = 0;
	int i = ws * 3 + wt + (linear ? 9 : 0);
	if (!samplers[i]) {
		glGenSamplers (1, &samplers[i]);
		glSamplerParameteri (samplers[i], GL_TEXTURE_WRAP_S, modes[ws]);
		glSamplerParameteri (samplers[i], GL_TEXTURE_WRAP_T, modes[wt]);
		glSamplerParameteri (samplers[i], GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
		glSamplerParameteri (samplers[i], GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
	}
	return samplers[i];
}

static void set_tex (int unit, int t, uint32_t wrap, int linear)
{
	GLuint id = t >= 0 && gltex[t] ? gltex[t] : empty, s = sampler_for (wrap, linear);
	if (id != cur.tex[unit]) {
		cur.tex[unit] = id;
		glActiveTexture (GL_TEXTURE0 + unit);
		glBindTexture (GL_TEXTURE_2D, id);
	}
	if (s != cur.samp[unit]) {
		cur.samp[unit] = s;
		glBindSampler (unit, s);
	}
}

static void u4 (GLint loc, float *last, float a, float b, float c, float d)
{
	if (last[0] == a && last[1] == b && last[2] == c && last[3] == d)
		return;
	last[0] = a; last[1] = b; last[2] = c; last[3] = d;
	glUniform4f (loc, a, b, c, d);
}

static void u4c (GLint loc, float *last, uint32_t c)
{
	u4 (loc, last, (c >> 24) / 255.0f, (c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f, (c & 255) / 255.0f);
}

static float shift_scale (int s)
{
	return s == 0 ? 1.0f : s <= 10 ? 1.0f / (1 << s) : (float) (1 << (16 - s));
}

static void u_tile (GLint loc, float *last, uint32_t t)
{
	u4 (loc, last, shift_scale (t >> 24 & 15), shift_scale (t >> 28 & 15), (t & 0xFFF) / 4.0f, (t >> 12 & 0xFFF) / 4.0f);
}

static GLuint new_texture (void)
{
	GLuint t;
	glGenTextures (1, &t);
	glBindTexture (GL_TEXTURE_2D, t);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);		/* 0xAARRGGBB en memoire : B G R A */
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	return t;
}

static void upload (struct n64gl_tex *t, int i)
{
	if (!gltex[i])
		gltex[i] = new_texture ();
	else
		glBindTexture (GL_TEXTURE_2D, gltex[i]);
	cur.tex[0] = cur.tex[1] = 0;
	if (gltw[i] == t->w && glth[i] == t->h)
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, t->w, t->h, GL_RGBA, GL_UNSIGNED_BYTE, t->px);
	else {
		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->px);
		gltw[i] = t->w;
		glth[i] = t->h;
	}
	t->dirty = 0;
}

static struct prog *prog_for (const uint32_t key[4])
{
	struct prog *g = find (key);
	if (!g)
		return NULL;
	if (!g->tried) {
		g->tried = 1;
		compile (g, key);
	}
	return g->p ? g : NULL;
}

void n64gl_render (const struct n64gl_frame *f)
{
	float sx = (float) f->w / f->gw;
	noise_seed += 0.137f;
	if (noise_seed > 100.0f)
		noise_seed = 0;
	for (int i = 0; i < N64GL_MAX_TEX; i++)
		if (f->tex[i].dirty && f->tex[i].px && f->tex[i].w > 0 && f->tex[i].h > 0)
			upload (&f->tex[i], i);
	glViewport (0, 0, f->w, f->h);
	glDisable (GL_SCISSOR_TEST);
	glDepthMask (GL_TRUE);
	cur.depth_mask = 1;
	glClearColor ((f->clear >> 16 & 255) / 255.0f, (f->clear >> 8 & 255) / 255.0f, (f->clear & 255) / 255.0f, 1.0f);
	glClearDepthf (1.0f);
	glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable (GL_SCISSOR_TEST);
	cur.sc0 = cur.sc1 = 0xFFFFFFFF;
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glBufferData (GL_ARRAY_BUFFER, f->nv * sizeof f->v[0], f->v, GL_STREAM_DRAW);
	for (int i = 0; i < f->nb; i++) {
		const struct n64gl_batch *b = &f->b[i];
		uint32_t key[4];
		n64gl_batch_key (b, key);
		struct prog *g = prog_for (key);
		if (!g) {
			nskipped++;
			continue;
		}
		if (g->p != cur.prog) {
			cur.prog = g->p;
			glUseProgram (g->p);
		}
		int linear = K_FILT (key[2]) == 1;
		set_tex (0, b->tex0, b->wrap0, linear);
		set_tex (1, b->tex1, b->wrap1, linear);
		set_blend (key[3]);
		set_flags (b->flags);
		set_scissor (f, b);
		u_tile (g->uT0, g->last.t0, b->tile0);
		u_tile (g->uT1, g->last.t1, b->tile1);
		if (g->uPrim >= 0) u4c (g->uPrim, g->last.c[0], b->prim);
		if (g->uEnv >= 0) u4c (g->uEnv, g->last.c[1], b->env);
		if (g->uFog >= 0) u4c (g->uFog, g->last.c[2], b->fog);
		if (g->uBlend >= 0) u4c (g->uBlend, g->last.c[3], b->blend);
		if (g->uMisc >= 0) {
			float th = (b->blend & 255) / 255.0f;
			if ((key[2] & K_CYCLE) == 2)
				th = 1 / 255.0f;
			u4 (g->uMisc, g->last.m, b->primLod / 255.0f, sx, noise_seed, th < 1 / 255.0f ? 1 / 255.0f : th);
		}
		if (g->uSize >= 0) {
			float s0 = 1, t0 = 1, s1 = 1, t1 = 1;
			if (b->tex0 >= 0) s0 = 1.0f / f->tex[b->tex0].w, t0 = 1.0f / f->tex[b->tex0].h;
			if (b->tex1 >= 0) s1 = 1.0f / f->tex[b->tex1].w, t1 = 1.0f / f->tex[b->tex1].h;
			u4 (g->uSize, g->last.s, s0, t0, s1, t1);
		}
		glDrawArrays (GL_TRIANGLES, b->first, b->count);
	}
}

void n64gl_filter (int on)
{
	f3pt = on;
}

void n64gl_stats (int *programs, int *compiled, int *skipped)
{
	*programs = nprog;
	*compiled = ncompiled;
	*skipped = nskipped;
	nskipped = 0;
}

int n64gl_start (void *(*proc) (const char *), int w, int h)
{
#define X(t, n) n = (t) proc (#n); if (!n) { printf ("n64gl : manque " #n "\n"); return 0; }
	GLFN (X)
#undef X
	target_w = w;
	target_h = h;
	vshader = shader (GL_VERTEX_SHADER, vs_text);
	if (!vshader)
		return 0;
	glGenVertexArrays (1, &vao);
	glBindVertexArray (vao);
	glGenBuffers (1, &vbo);
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	glEnableVertexAttribArray (2);
	glVertexAttribPointer (0, 4, GL_FLOAT, GL_FALSE, 32, (void *) 0);
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 32, (void *) 16);
	glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 32, (void *) 24);
	glEnable (GL_DEPTH_TEST);
	glFrontFace (GL_CCW);
	cur.depth_mask = 1;
	cur.depth_func = -1;
	empty = new_texture ();			/* pour une unite que le shader ne lit pas */
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, "\0\0\0\0");
	return 1;
}

/* le code du GPU ne se fait qu'au premier dessin : un triangle vide, les etats d'un vrai lot */
static void warm (struct prog *g)
{
	static const struct n64gl_vert v[3];
	glBindBuffer (GL_ARRAY_BUFFER, vbo);
	glBufferData (GL_ARRAY_BUFFER, sizeof v, v, GL_STREAM_DRAW);
	set_tex (0, -1, 0, 0);
	set_tex (1, -1, 0, 0);
	glUseProgram (g->p);
	cur.prog = g->p;
	glDrawArrays (GL_TRIANGLES, 0, 3);
}

/* les reglages deja vus pour cette ROM, une clef de 16 octets par programme ; ceux de cette partie s'y ajoutent.
 * La cible doit etre courante */
int n64gl_keys_load (const char *path)
{
	uint32_t key[4];
	int n = 0;
	FILE *f = fopen (path, "rb");
	if (f) {
		while (fread (key, 16, 1, f) == 1) {
			struct prog *g = find (key);
			if (g && !g->tried) {
				g->tried = 1;
				if (compile (g, key)) {
					warm (g);
					n++;
				}
			}
		}
		fclose (f);
	}
	ncompiled = 0;
	keyfile = NULL;
	keyfile = fopen (path, "ab");
	return n;
}
