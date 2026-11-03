//
// gxes_gl.cpp -- le lecteur du GX (gxes.h) : tout ce qui parle a GL. Le TEV d'une configuration en GLSL ES 3.00,
// les memes calculs entiers que gxgl.cpp d'Onyx, mais seulement ce que les etages lisent : Mesa compile dans l'OS,
// sur le Pi, et chaque ligne de source se paie.
//
//   cache : un programme par configuration, gardes tant que l'app vit ; repli : au-dela de budgetUs de compilation
//   par image, le lot passe par un programme simple (carte x couleur), le vrai viendra a l'image suivante.
//   EFB : un FBO 640 x 528, ligne 0 = rangee 0 de GL. Copies : un FBO par emplacement, le format du jeu converti
//   par le shader de copie. Image : le rectangle de l'EFB tire sur la fenetre.
//
#include "gxes.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <GLES3/gl3.h>

namespace gxes
{
using gc::Machine; using gc::GxCopy; using gc::GX_TEX_COPY; using gc::GX_COPIES;

// ---- les fonctions de GL : par pointeur, armgl n'en exporte aucune ----------------------------------------------
#define GL_LIST(X) \
	X (void, ActiveTexture, (GLenum)) X (void, AttachShader, (GLuint, GLuint)) X (void, BindBuffer, (GLenum, GLuint)) \
	X (void, BindFramebuffer, (GLenum, GLuint)) X (void, BindSampler, (GLuint, GLuint)) X (void, BindTexture, (GLenum, GLuint)) \
	X (void, BindVertexArray, (GLuint)) X (void, BlendEquation, (GLenum)) X (void, BlendFunc, (GLenum, GLenum)) \
	X (void, BufferData, (GLenum, GLsizeiptr, const void *, GLenum)) X (GLenum, CheckFramebufferStatus, (GLenum)) \
	X (void, Clear, (GLbitfield)) X (void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) X (void, ClearDepthf, (GLfloat)) \
	X (void, ColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) X (void, CompileShader, (GLuint)) X (GLuint, CreateProgram, (void)) \
	X (GLuint, CreateShader, (GLenum)) X (void, CullFace, (GLenum)) X (void, DeleteShader, (GLuint)) X (void, DepthFunc, (GLenum)) \
	X (void, DepthMask, (GLboolean)) X (void, Disable, (GLenum)) X (void, DrawArrays, (GLenum, GLint, GLsizei)) \
	X (void, Enable, (GLenum)) X (void, EnableVertexAttribArray, (GLuint)) X (void, Flush, (void)) \
	X (void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) X (void, GenBuffers, (GLsizei, GLuint *)) \
	X (void, GenFramebuffers, (GLsizei, GLuint *)) X (void, GenSamplers, (GLsizei, GLuint *)) X (void, GenTextures, (GLsizei, GLuint *)) \
	X (void, GenVertexArrays, (GLsizei, GLuint *)) X (GLenum, GetError, (void)) X (void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X (void, GetProgramiv, (GLuint, GLenum, GLint *)) X (void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *)) \
	X (void, GetShaderiv, (GLuint, GLenum, GLint *)) X (GLint, GetUniformLocation, (GLuint, const GLchar *)) \
	X (void, InvalidateFramebuffer, (GLenum, GLsizei, const GLenum *)) X (void, LineWidth, (GLfloat)) X (void, LinkProgram, (GLuint)) \
	X (void, PixelStorei, (GLenum, GLint)) X (void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *)) \
	X (void, SamplerParameterf, (GLuint, GLenum, GLfloat)) X (void, SamplerParameteri, (GLuint, GLenum, GLint)) \
	X (void, Scissor, (GLint, GLint, GLsizei, GLsizei)) X (void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) \
	X (void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *)) X (void, TexParameteri, (GLenum, GLenum, GLint)) \
	X (void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) \
	X (void, Uniform1i, (GLint, GLint)) X (void, Uniform2f, (GLint, GLfloat, GLfloat)) X (void, Uniform3i, (GLint, GLint, GLint, GLint)) \
	X (void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) X (void, Uniform4fv, (GLint, GLsizei, const GLfloat *)) \
	X (void, Uniform4i, (GLint, GLint, GLint, GLint, GLint)) X (void, Uniform4iv, (GLint, GLsizei, const GLint *)) X (void, UseProgram, (GLuint)) \
	X (void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) X (void, Viewport, (GLint, GLint, GLsizei, GLsizei))
#define X(ret, name, args) typedef ret (*T_##name) args; static T_##name g##name;
GL_LIST (X)
#undef X
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif

// ---- le fragment shader d'une configuration ---------------------------------------------------------------------
struct Out
{
	char *p; int n, cap;
	void put (const char *fmt, ...) __attribute__ ((format (printf, 2, 3)))
	{
		va_list a; va_start (a, fmt);
		int k = vsnprintf (p + n, (size_t) (cap - n), fmt, a);
		va_end (a);
		if (k > 0 && n + k < cap) n += k;
	}
};

static const char HEAD[] =
"#version 300 es\n"
"precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";

// les fonctions du TEV : seulement celles qu'un etage appelle
static const char F_CMP[] =
"int cmpKey (ivec3 v, int m) { return m == 0 ? v.r : m == 1 ? (v.g << 8) | v.r : (v.b << 16) | (v.g << 8) | v.r; }\n";
static const char F_IND[] =
"ivec2 fixUV (vec3 t, bool pr, vec2 sc) { vec2 uv = pr && t.z != 0.0 ? t.xy / t.z : t.xy; return ivec2 (uv * 128.0); }\n"
"int s11i (int v) { v &= 0x7FF; return v >= 0x400 ? v - 0x800 : v; }\n"
"void indMatrix (int m, out ivec3 r0, out ivec3 r1, out int sh)\n{\n"
"	int k = m * 3;\n"
"	int a = uIM[k >> 2][k & 3], b = uIM[(k + 1) >> 2][(k + 1) & 3], c = uIM[(k + 2) >> 2][(k + 2) & 3];\n"
"	r0 = ivec3 (s11i (a), s11i (b), s11i (c));\n"
"	r1 = ivec3 (s11i (a >> 11), s11i (b >> 11), s11i (c >> 11));\n"
"	sh = 17 - (((a >> 22) & 3) | (((b >> 22) & 3) << 2) | (((c >> 22) & 1) << 4));\n}\n"
"ivec2 shiftBy (ivec2 v, int sh) { return sh >= 0 ? v >> sh : v << (-sh); }\n"
"int wrapc (int v, int w) { return w == 0 || w == 7 ? v : w == 6 ? 0 : v & (((256 >> (w - 1)) << 7) - 1); }\n";

// la composante k des varyings (apres la position)
static const char *comp (int k, char *b)
{
	snprintf (b, 8, "v%d.%c", k / 4 + 1, "xyzw"[k & 3]);
	return b;
}
static void coordExpr (const Prog &p, int coord, char *o, int cap)
{
	char a[8], b[8], c[8];
	int s = -1, q = -1;
	for (int i = 0; i < p.nVary; i++)
		if (p.vary[i].a == coord)
		{
			if (p.vary[i].kind == V_ST && p.vary[i].b == 0) s = i;
			else if (p.vary[i].kind == V_Q) q = i;
		}
	if (s < 0) { snprintf (o, (size_t) cap, "vec3 (0.0, 0.0, 1.0)"); return; }
	if (q >= 0) snprintf (o, (size_t) cap, "vec3 (%s, %s, %s)", comp (s, a), comp (s + 1, b), comp (q, c));
	else snprintf (o, (size_t) cap, "vec3 (%s, %s, 1.0)", comp (s, a), comp (s + 1, b));
}
// la coordonnee de la lecture d'une carte, 0..1
static void uvExpr (const Prog &p, int coord, int map, char *o, int cap)
{
	char t[64];
	coordExpr (p, coord, t, sizeof t);
	bool pr = (p.cfg.projMask >> coord) & 1;
	char base[96];
	if (pr) snprintf (base, sizeof base, "pj (%s)", t);
	else { t[strlen (t)] = 0; snprintf (base, sizeof base, "%s.xy", t); }
	if (p.normMap[coord] >= 0) snprintf (o, (size_t) cap, "%s", base);
	else snprintf (o, (size_t) cap, "%s * uIS[%d].xy", base, map);
}

static void kcolour (int sel, char *o)
{
	static const int frac[8] = { 255, 223, 191, 159, 128, 96, 64, 32 };
	if (sel < 8) sprintf (o, "ivec3 (%d)", frac[sel]);
	else if (sel >= 12 && sel < 16) sprintf (o, "uK[%d].rgb", sel - 12);
	else if (sel >= 16) sprintf (o, "ivec3 (uK[%d][%d])", (sel - 16) & 3, (sel - 16) >> 2);
	else sprintf (o, "ivec3 (0)");
}
static void kalpha (int sel, char *o)
{
	static const int frac[8] = { 255, 223, 191, 159, 128, 96, 64, 32 };
	if (sel < 8) sprintf (o, "%d", frac[sel]);
	else if (sel >= 16) sprintf (o, "uK[%d][%d]", (sel - 16) & 3, (sel - 16) >> 2);
	else sprintf (o, "0");
}
// une entree de couleur ; maskit : & 255 si la source peut sortir de 0..255
static const char *cin (int s, bool maskit, char *o, const char *ks)
{
	static const char *reg[12] = { "P.rgb", "ivec3 (P.a)", "C0.rgb", "ivec3 (C0.a)", "C1.rgb", "ivec3 (C1.a)", "C2.rgb", "ivec3 (C2.a)",
				       "T.rgb", "ivec3 (T.a)", "R.rgb", "ivec3 (R.a)" };
	if (s < 8) { if (maskit) sprintf (o, "(%s & 255)", reg[s]); else sprintf (o, "%s", reg[s]); }
	else if (s < 12) sprintf (o, "%s", reg[s]);
	else if (s == 12) sprintf (o, "ivec3 (255)");
	else if (s == 13) sprintf (o, "ivec3 (128)");
	else if (s == 14) sprintf (o, "%s", ks);
	else sprintf (o, "ivec3 (0)");
	return o;
}
static const char *ain (int s, bool maskit, char *o, const char *ks)
{
	static const char *reg[6] = { "P.a", "C0.a", "C1.a", "C2.a", "T.a", "R.a" };
	if (s < 4) { if (maskit) sprintf (o, "(%s & 255)", reg[s]); else sprintf (o, "%s", reg[s]); }
	else if (s < 6) sprintf (o, "%s", reg[s]);
	else if (s == 6) sprintf (o, "%s", ks);
	else sprintf (o, "0");
	return o;
}

// (d + biais +- lerp (a, b, c)) x echelle, comme le materiel : voir tevReg de gxgl.cpp. isz : a b c valent 0 ; one : c vaut 255
static void tevExpr (Out &f, const char *dst, const char *type, const char *a, const char *b, const char *c, const char *d,
		     bool a0, bool b0, bool c0, bool c255, bool d0, int bias, int sub, int scale)
{
	int sh = scale == 1 ? 1 : scale == 2 ? 2 : 0;
	int rnd = sub == 0 ? (scale == 3 ? 128 : 0) : (scale == 3 ? 0 : 127);
	char l[256], dd[160];
	const char *one = type[0] == 'i' && type[1] == 'n' ? "" : "";
	(void) one;
	if ((a0 && b0) || (a0 && c0) || (b0 && c255)) snprintf (l, sizeof l, "%s", "");		// (0)
	else if (c0) snprintf (l, sizeof l, "(%s << %d)", a, sh);
	else if (c255) snprintf (l, sizeof l, "(%s << %d)", b, sh);
	else if (a0) snprintf (l, sizeof l, "(((%s * (%s + (%s >> 7))) << %d) + %d) >> 8", b, c, c, sh, rnd);
	else snprintf (l, sizeof l, "((((%s << 8) + (%s - %s) * (%s + (%s >> 7))) << %d) + %d) >> 8", a, b, a, c, c, sh, rnd);
	int bv = bias == 1 ? 128 : bias == 2 ? -128 : 0;
	if (d0 && !bv) dd[0] = 0;
	else if (d0) snprintf (dd, sizeof dd, "%s (%d)", type, bv * (1 << sh));
	else if (bv) snprintf (dd, sizeof dd, "((%s + %d) << %d)", d, bv, sh);
	else if (sh) snprintf (dd, sizeof dd, "(%s << %d)", d, sh);
	else snprintf (dd, sizeof dd, "%s", d);
	const char *rs = scale == 3 ? " >> 1" : "";
	if (!l[0] && !dd[0]) f.put ("\t%s %s = %s (0);\n", type, dst, type);
	else if (!l[0]) f.put ("\t%s %s = (%s)%s;\n", type, dst, dd, rs);
	else if (!dd[0]) f.put ("\t%s %s = (%s(%s))%s;\n", type, dst, sub ? "-" : "", l, rs);
	else f.put ("\t%s %s = (%s %s (%s))%s;\n", type, dst, dd, sub ? "-" : "+", l, rs);
}

static void stageIndirect (Out &f, const Prog &p, int st, u32 tr, u32 ic)
{
	const TevCfg &c = p.cfg;
	int coord = (tr >> 3) & 7;
	char t[64]; coordExpr (p, coord, t, sizeof t);
	f.put ("\t{\n\t\tivec2 fuv = fixUV (%s, %s, vec2 (0.0)), trans = ivec2 (0);\n", t, ((c.projMask >> coord) & 1) ? "true" : "false");
	int bt = ic & 3, fmt = (ic >> 2) & 3;
	if (bt < (int) c.nInd)
	{
		f.put ("\t\tivec3 raw = I%d, crd = raw & %d;\n", bt, fmt == 0 ? 255 : fmt == 1 ? 31 : fmt == 2 ? 15 : 7);
		int bias = (ic >> 4) & 7, badd = fmt == 0 ? -128 : 1;
		if (bias & 1) f.put ("\t\tcrd.x += %d;\n", badd);
		if (bias & 2) f.put ("\t\tcrd.y += %d;\n", badd);
		if (bias & 4) f.put ("\t\tcrd.z += %d;\n", badd);
		int bs = (ic >> 7) & 3;
		if (bs) f.put ("\t\tB = raw[%d] & %d;\n", bs - 1, fmt == 1 ? 224 : fmt == 2 ? 240 : 248);
		int mid = (ic >> 9) & 15;
		if (mid >= 1 && mid <= 3)
			f.put ("\t\tivec3 r0, r1; int sh; indMatrix (%d, r0, r1, sh);\n\t\ttrans = shiftBy (ivec2 (r0.x * crd.x + r0.y * crd.y + r0.z * crd.z, r1.x * crd.x + r1.y * crd.y + r1.z * crd.z) >> 3, sh);\n", mid - 1);
		else if (mid >= 5 && mid <= 7)
			f.put ("\t\tivec3 r0, r1; int sh; indMatrix (%d, r0, r1, sh);\n\t\ttrans = shiftBy ((fuv * crd.xx) >> 8, sh);\n", mid - 5);
		else if (mid >= 9 && mid <= 11)
			f.put ("\t\tivec3 r0, r1; int sh; indMatrix (%d, r0, r1, sh);\n\t\ttrans = shiftBy ((fuv * crd.yy) >> 8, sh);\n", mid - 9);
	}
	f.put ("\t\tivec2 wr = ivec2 (wrapc (fuv.x, %d), wrapc (fuv.y, %d));\n", (ic >> 13) & 7, (ic >> 16) & 7);
	f.put ("\t\tTC = %s;\n\t\tTC = (TC << 8) >> 8;\n\t}\n", (ic & 0x100000) ? "TC + wr + trans" : "wr + trans");
	(void) st;
}

static int genFragment (const Prog &p, char *out, int cap)
{
	const TevCfg &c = p.cfg;
	Out f = { out, 0, cap };
	f.put ("%s", HEAD);
	for (int i = 0; i < p.chunks; i++) f.put ("in vec4 v%d;\n", i + 1);
	for (int m = 0; m < 8; m++) if (p.mapMask & (1u << m)) f.put ("uniform sampler2D s%d;\n", m);
	f.put ("uniform ivec4 uR[4];\nuniform ivec4 uK[4];\nuniform ivec4 uA;\nuniform vec4 uIS[8];\nout vec4 oCol;\n");
	bool anyInd = false, anyCmp = false, anyProj = c.projMask != 0, reads[16];
	for (u32 st = 0; st < c.nStages; st++) { if (c.ind[st]) anyInd = true; if (((c.cenv[st] >> 16) & 3) == 3 || ((c.aenv[st] >> 16) & 3) == 3) anyCmp = true; }
	(void) reads;
	if (c.nInd || anyInd) { f.put ("uniform ivec4 uIM[3];\n%s", F_IND); }
	if (anyCmp) f.put ("%s", F_CMP);
	if (anyProj) f.put ("vec2 pj (vec3 t) { return t.z != 0.0 ? t.xy / t.z : t.xy; }\n");
	if (c.fogSel) f.put ("uniform ivec4 uFI;\nuniform vec4 uF0;\nuniform vec4 uF1;\nuniform vec4 uF2[3];\n");
	f.put ("void main ()\n{\n\tivec4 P = uR[0], C0 = uR[1], C1 = uR[2], C2 = uR[3], T = ivec4 (255), R = ivec4 (0);\n");
	char a[8], b[8], d[8], e[8];
	if (p.c0At >= 0) f.put ("\tivec4 r0 = ivec4 (vec4 (%s, %s, %s, %s) * 255.0 + 0.5);\n", comp (p.c0At, a), comp (p.c0At + 1, b), comp (p.c0At + 2, d), comp (p.c0At + 3, e));
	if (p.c1At >= 0) f.put ("\tivec4 r1 = ivec4 (vec4 (%s, %s, %s, %s) * 255.0 + 0.5);\n", comp (p.c1At, a), comp (p.c1At + 1, b), comp (p.c1At + 2, d), comp (p.c1At + 3, e));
	if (c.nInd || anyInd) f.put ("\tivec2 TC = ivec2 (0);\n\tint B = 0;\n");
	for (u32 i = 0; i < c.nInd; i++)
	{
		int ir = (int) (c.iref >> (6 * i)), ss = (int) ((i < 2 ? c.iss0 : c.iss1) >> ((i & 1) * 8));
		int map = ir & 7, coord = (ir >> 3) & 7;
		char t[64]; coordExpr (p, coord, t, sizeof t);
		f.put ("\tivec3 I%u;\n\t{\n\t\tivec2 q = fixUV (%s, %s, vec2 (0.0)) >> ivec2 (%d, %d);\n", i, t, ((c.projMask >> coord) & 1) ? "true" : "false", ss & 15, (ss >> 4) & 15);
		f.put ("\t\tvec4 t = texture (s%d, vec2 (q) * uIS[%d].xy / 128.0);\n\t\tI%u = ivec3 (t.abg * 255.0 + 0.5);\n\t}\n", map, map, i);
	}
	for (u32 st = 0; st < c.nStages; st++)
	{
		u32 ce = c.cenv[st], ae = c.aenv[st], tr = c.tref[st], ks = c.ksel[st], ic = c.ind[st];
		int map = tr & 7, coord = (tr >> 3) & 7, chn = (tr >> 7) & 7;
		f.put ("\t// etage %u\n", st);
		if (ic) stageIndirect (f, p, (int) st, tr, ic);
		int ts = (ae >> 2) & 3, rs = ae & 3;
		static const char S[] = "rgba";
		if (tr & 64)
		{
			char uv[160];
			if (ic) snprintf (uv, sizeof uv, "vec2 (TC) * uIS[%d].xy / 128.0", map);
			else uvExpr (p, coord, map, uv, sizeof uv);
			char bs[32] = "";
			if (c.biasMask & (1 << map)) snprintf (bs, sizeof bs, ", uIS[%d].z", map);
			f.put ("\tT = ivec4 (texture (s%d, %s%s) * 255.0 + 0.5).%c%c%c%c;\n", map, uv, bs, S[c.swap[ts * 4]], S[c.swap[ts * 4 + 1]], S[c.swap[ts * 4 + 2]], S[c.swap[ts * 4 + 3]]);
		}
		else f.put ("\tT = ivec4 (255);\n");
		const char *ras = chn == 0 && p.c0At >= 0 ? "r0" : chn == 1 && p.c1At >= 0 ? "r1" : chn == 5 ? "ivec4 (B)" : chn == 6 ? "ivec4 (B | (B >> 5))" : "ivec4 (0)";
		if ((chn == 5 || chn == 6) && !(c.nInd || anyInd)) ras = "ivec4 (0)";
		f.put ("\tR = %s.%c%c%c%c;\n", ras, S[c.swap[rs * 4]], S[c.swap[rs * 4 + 1]], S[c.swap[rs * 4 + 2]], S[c.swap[rs * 4 + 3]]);
		char kc[32], ka[32];
		kcolour (ks & 31, kc); kalpha ((ks >> 5) & 31, ka);
		int sa = (ce >> 12) & 15, sb = (ce >> 8) & 15, sc = (ce >> 4) & 15, sd = ce & 15;
		char xa[48], xb[48], xc[48], xd[48];
		cin (sa, true, xa, kc); cin (sb, true, xb, kc); cin (sc, true, xc, kc); cin (sd, false, xd, kc);
		int cb = (ce >> 16) & 3, co = (ce >> 18) & 1, cs = (ce >> 20) & 3;
		f.put ("\t{\n");
		if (cb != 3) tevExpr (f, "rc", "ivec3", xa, xb, xc, xd, sa == 15, sb == 15, sc == 15, sc == 12, sd == 15, cb, co, cs);
		else
		{
			int mode = (cs << 1) | co, eq = mode & 1, m = mode >> 1;
			if (m == 3)
			{
				const char *op = eq ? "==" : ">";
				f.put ("\tivec3 ca = %s, cb = %s, cc = %s;\n\tivec3 rc = %s + ivec3 (ca.r %s cb.r ? cc.r : 0, ca.g %s cb.g ? cc.g : 0, ca.b %s cb.b ? cc.b : 0);\n", xa, xb, xc, xd, op, op, op);
			}
			else f.put ("\tivec3 rc = %s + (cmpKey (%s, %d) %s cmpKey (%s, %d) ? %s : ivec3 (0));\n", xd, xa, m, eq ? "==" : ">", xb, m, xc);
		}
		if (ce & 0x80000) f.put ("\trc = clamp (rc, 0, 255);\n"); else f.put ("\trc = clamp (rc, -1024, 1023);\n");
		int aa = (ae >> 13) & 7, ab = (ae >> 10) & 7, ac = (ae >> 7) & 7, ad = (ae >> 4) & 7;
		char ya[48], yb[48], yc[48], yd[48];
		ain (aa, true, ya, ka); ain (ab, true, yb, ka); ain (ac, true, yc, ka); ain (ad, false, yd, ka);
		int xbias = (ae >> 16) & 3, xo = (ae >> 18) & 1, xs = (ae >> 20) & 3;
		if (xbias != 3) tevExpr (f, "ra", "int", ya, yb, yc, yd, aa == 7, ab == 7, ac == 7, false, ad == 7, xbias, xo, xs);
		else
		{
			int mode = (xs << 1) | xo, eq = mode & 1, m = mode >> 1;
			if (m == 3) f.put ("\tint ra = %s + (%s %s %s ? %s : 0);\n", yd, ya, eq ? "==" : ">", yb, yc);
			else f.put ("\tint ra = %s + (cmpKey (%s, %d) %s cmpKey (%s, %d) ? %s : 0);\n", yd, xa, m, eq ? "==" : ">", xb, m, yc);
		}
		if (ae & 0x80000) f.put ("\tra = clamp (ra, 0, 255);\n"); else f.put ("\tra = clamp (ra, -1024, 1023);\n");
		if (st == c.nStages - 1) f.put ("\tP = ivec4 (rc, ra);\n");
		else
		{
			static const char *D[4] = { "P", "C0", "C1", "C2" };
			f.put ("\t%s.rgb = rc;\n\t%s.a = ra;\n", D[(ce >> 22) & 3], D[(ae >> 22) & 3]);
		}
		f.put ("\t}\n");
	}
	f.put ("\tP &= 255;\n");
	// le test d'alpha
	int f0 = c.alphaFunc[0], f1 = c.alphaFunc[1], lg = c.alphaLogic;
	bool always = (lg == 0 && f0 == 7 && f1 == 7) || (lg == 1 && (f0 == 7 || f1 == 7));
	if (!always)
	{
		static const char *OP[8] = { "false", "(P.a < uA.%c)", "(P.a == uA.%c)", "(P.a <= uA.%c)", "(P.a > uA.%c)", "(P.a != uA.%c)", "(P.a >= uA.%c)", "true" };
		char t0[32], t1[32];
		snprintf (t0, sizeof t0, OP[f0], 'x'); snprintf (t1, sizeof t1, OP[f1], 'y');
		static const char *L[4] = { "&&", "||", "!=", "==" };
		f.put ("\tif (!(%s %s %s)) discard;\n", t0, L[lg], t1);
	}
	if (c.fogSel || c.zop) f.put ("\tint zc = min (int (gl_FragCoord.z * 16777216.0), 16777215);\n");
	if (c.zop)
	{
		static const char *W[3] = { "ivec4 (0, 0, 0, 1)", "ivec4 (256, 0, 0, 1)", "ivec4 (65536, 256, 1, 0)" };
		f.put ("\t{\n\tivec4 w = %s;\n\tint z = T.r * w.x + T.g * w.y + T.b * w.z + T.a * w.w + uA.w;\n", W[c.zfmt > 2 ? 2 : c.zfmt]);
		if (c.zop == 1) f.put ("\tz += zc;\n");
		f.put ("\tzc = z & 0xFFFFFF;\n\tgl_FragDepth = float (zc) / 16777216.0;\n\t}\n");
	}
	else if (0) {}
	if (c.fogSel)
	{
		if (!c.fogProj) f.put ("\tfloat ze = (uF0.x * 16777216.0) / float (uFI.z - (zc >> uFI.w));\n");
		else f.put ("\tfloat ze = uF0.x * float (zc) / 16777216.0;\n");
		if (c.fogRange)
			f.put ("\t{\n\tfloat off = 2.0 * (gl_FragCoord.x / uF0.w) - 1.0 - uF0.z;\n\tfloat fi = clamp (9.0 - abs (off) * 9.0, 0.0, 9.0);\n"
			       "\tint il = int (fi), iu = min (il + 1, 9);\n\tfloat k = mix (uF2[il >> 2][il & 3], uF2[iu >> 2][iu & 3], fract (fi));\n"
			       "\tif (k != 0.0) ze *= sqrt (off * off + k * k) / k;\n\t}\n");
		f.put ("\tfloat fg = clamp (ze - uF0.y, 0.0, 1.0);\n");
		if (c.fogSel == 4) f.put ("\tfg = 1.0 - exp2 (-8.0 * fg);\n");
		else if (c.fogSel == 5) f.put ("\tfg = 1.0 - exp2 (-8.0 * fg * fg);\n");
		else if (c.fogSel == 6) f.put ("\tfg = exp2 (-8.0 * (1.0 - fg));\n");
		else if (c.fogSel == 7) f.put ("\tfg = 1.0 - fg;\n\tfg = exp2 (-8.0 * fg * fg);\n");
		f.put ("\tint fi2 = int (fg * 256.0 + 0.5);\n\tP.rgb = (P.rgb * (256 - fi2) + ivec3 (uF1.rgb) * fi2) >> 8;\n");
	}
	if (c.efbFmt == 1) f.put ("\tvec4 col = vec4 (P >> 2) / 63.0;\n");
	else if (c.efbFmt == 2) f.put ("\tvec4 col = vec4 (vec3 (P.r >> 3, P.g >> 2, P.b >> 3) / vec3 (31.0, 63.0, 31.0), 1.0);\n");
	else f.put ("\tvec4 col = vec4 (P) / 255.0;\n");
	if (c.dstAlpha) f.put ("\tcol.a = float (uA.z) / 255.0;\n");
	f.put ("\toCol = col;\n}\n");
	return f.n + 1 < cap ? f.n : 0;
}
int fragmentSource (const Prog &p, char *out, int cap) { return genFragment (p, out, cap); }

static int genVertex (int chunks, char *out, int cap)
{
	Out f = { out, 0, cap };
	f.put ("#version 300 es\nlayout (location = 0) in vec4 a0;\n");
	for (int i = 1; i <= chunks; i++) f.put ("layout (location = %d) in vec4 a%d;\nout vec4 v%d;\n", i, i, i);
	f.put ("uniform float uPt;\nvoid main ()\n{\n\tgl_Position = a0;\n\tgl_PointSize = uPt;\n");
	for (int i = 1; i <= chunks; i++) f.put ("\tv%d = a%d;\n", i, i);
	f.put ("}\n");
	return f.n;
}

// le repli : carte 0 x premiere couleur, tous les varyings en tableau, le shader dit ou lire
static const char FALLBACK_VS[] =
"#version 300 es\n"
"layout (location = 0) in vec4 a0; layout (location = 1) in vec4 a1; layout (location = 2) in vec4 a2; layout (location = 3) in vec4 a3;\n"
"layout (location = 4) in vec4 a4; layout (location = 5) in vec4 a5; layout (location = 6) in vec4 a6;\n"
"out vec4 v1; out vec4 v2; out vec4 v3; out vec4 v4; out vec4 v5; out vec4 v6;\n"
"void main () { gl_Position = a0; v1 = a1; v2 = a2; v3 = a3; v4 = a4; v5 = a5; v6 = a6; }\n";
static const char FALLBACK_FS[] =
"#version 300 es\nprecision highp float;\nprecision highp int;\n"
"in vec4 v1; in vec4 v2; in vec4 v3; in vec4 v4; in vec4 v5; in vec4 v6;\n"
"uniform sampler2D s0;\nuniform ivec3 uAt;\nuniform vec2 uSc;\nout vec4 oCol;\n"
"float w (int k) { vec4 c = k < 4 ? v1 : k < 8 ? v2 : k < 12 ? v3 : k < 16 ? v4 : k < 20 ? v5 : v6; return c[k & 3]; }\n"
"void main ()\n{\n\tvec4 c = uAt.x >= 0 ? vec4 (w (uAt.x), w (uAt.x + 1), w (uAt.x + 2), w (uAt.x + 3)) : vec4 (1.0);\n"
"\tif (uAt.y >= 0) c *= texture (s0, vec2 (w (uAt.y), w (uAt.y + 1)) * uSc);\n\toCol = c;\n}\n";

static const char QUAD_VS[] =
"#version 300 es\nout vec2 vUV;\n"
"void main () { vec2 p = vec2 ((gl_VertexID & 1) != 0 ? 1.0 : 0.0, (gl_VertexID & 2) != 0 ? 1.0 : 0.0); vUV = p; gl_Position = vec4 (p * 2.0 - 1.0, 0.0, 1.0); }\n";
static const char COPY_FS[] =
"#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
"uniform sampler2D src;\nuniform vec4 rect;\nuniform vec2 srcSize;\nuniform ivec4 mode;\nin vec2 vUV;\nout vec4 o;\n"
"float x4 (float v) { return floor (v / 16.0) * 17.0; }\n"
"float x5 (float v) { float q = floor (v / 8.0); return q * 8.0 + floor (q / 4.0); }\n"
"float x6 (float v) { float q = floor (v / 4.0); return q * 4.0 + floor (q / 16.0); }\n"
"float x3 (float v) { float q = floor (v / 32.0); return q * 32.0 + q * 4.0 + floor (q / 2.0); }\n"
"void main ()\n{\n\tvec4 t = texture (src, (rect.xy + vUV * rect.zw) / srcSize);\n\tint f = mode.x;\n\tvec4 c;\n"
"\tif (mode.y != 0)\n\t{\n\t\tuint z = uint (clamp (t.r, 0.0, 1.0) * 16777215.0 + 0.5);\n"
"\t\tfloat hi = float (z >> 16), mid = float ((z >> 8) & 255u), lo = float (z & 255u);\n"
"\t\tif (f == 0) c = vec4 (x4 (hi));\n\t\telse if (f == 1 || f == 8) c = vec4 (hi);\n\t\telse if (f == 2) c = vec4 (vec3 (x4 (mid)), x4 (hi));\n"
"\t\telse if (f == 3) c = vec4 (vec3 (mid), hi);\n\t\telse if (f == 9) c = vec4 (mid);\n\t\telse if (f == 10) c = vec4 (lo);\n"
"\t\telse if (f == 11 || f == 12) c = vec4 (vec3 (lo), mid);\n\t\telse c = vec4 (hi, mid, lo, 255.0);\n\t}\n"
"\telse\n\t{\n\t\tvec4 e = floor (t * 255.0 + 0.5);\n\t\tif (mode.w == 0) e.a = 255.0;\n"
"\t\tfloat y = min (255.0, floor ((66.0 * e.r + 129.0 * e.g + 25.0 * e.b + 128.0) / 256.0) + 16.0);\n"
"\t\tfloat i = mode.z != 0 ? y : e.r;\n"
"\t\tif (f == 0) c = vec4 (x4 (i));\n\t\telse if (f == 1 || f == 8) c = vec4 (i);\n\t\telse if (f == 2) c = vec4 (vec3 (x4 (i)), x4 (e.a));\n"
"\t\telse if (f == 3) c = vec4 (vec3 (i), e.a);\n\t\telse if (f == 4) c = vec4 (x5 (e.r), x6 (e.g), x5 (e.b), 255.0);\n"
"\t\telse if (f == 5) c = e.a >= 224.0 ? vec4 (x5 (e.r), x5 (e.g), x5 (e.b), 255.0) : vec4 (x4 (e.r), x4 (e.g), x4 (e.b), x3 (e.a));\n"
"\t\telse if (f == 7) c = vec4 (e.a);\n\t\telse if (f == 9) c = vec4 (e.g);\n\t\telse if (f == 10) c = vec4 (e.b);\n"
"\t\telse if (f == 11) c = vec4 (vec3 (e.g), e.r);\n\t\telse if (f == 12) c = vec4 (vec3 (e.b), e.g);\n\t\telse c = e;\n\t}\n\to = c / 255.0;\n}\n";
static const char PRESENT_FS[] =
"#version 300 es\nprecision mediump float;\nuniform sampler2D src;\nuniform vec4 uv;\nin vec2 vUV;\nout vec4 o;\n"
"void main () { o = vec4 (texture (src, uv.xy + vec2 (vUV.x, 1.0 - vUV.y) * uv.zw).rgb, 1.0); }\n";

static inline u64 usNow () { timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (u64) t.tv_sec * 1000000 + (u64) t.tv_nsec / 1000; }

// ---- le lecteur -------------------------------------------------------------------------------------------------
struct Samp { u64 key; GLuint s; };
struct Player::Impl
{
	Player *o;
	GLuint efbColor, efbDepth, efbFbo, vbo, quadVao, vao[MAX_CHUNKS + 1], dummy, fbTex;
	GLuint vshader[MAX_CHUNKS + 1];
	GLuint fallback, copyProg, presentProg;
	GLint uAt, uSc, uFbS0, uCopyRect, uCopySize, uCopyMode, uPresentUv;
	GLuint texObj[Machine::MAX_TEX]; int texLevels[Machine::MAX_TEX];
	struct CopyTex { GLuint tex, fbo; int w, h; } copies[GX_COPIES];
	Samp samp[1024]; int nSamp;
	int fbW, fbH;
	u64 budgetLeft;
	char fsBuf[60000];
	// l'etat GL courant
	GLuint cProg; int cZon, cZf, cZw, cBlend, cBs, cBd, cBq, cCull, cCw, cAw, cSc[4]; GLuint cTex[8], cSamp[8]; int cVao; GLuint cUniProg; u32 cUni;
	void reset () { cProg = 0; cZon = cZf = cZw = cBlend = cBs = cBd = cBq = cCull = cCw = cAw = -1; cSc[0] = -2; for (int i = 0; i < 8; i++) { cTex[i] = ~0u; cSamp[i] = ~0u; } cVao = -1; cUniProg = 0; }

	GLuint sampler (u32 m0, u32 m1, int levels);
	GLuint compile (GLuint vs, const char *fss, bool quad = false);
	GLuint vshaderFor (int chunks);
	bool program (Prog &p, bool force);
	void upload (Machine &m, int t);
	void bindTextures (Machine &m, const Batch &b, const Prog *pr);
	void drawBatch (Rec &r, Machine &m, const Frame &F, const Batch &b);
	void doCopy (const GxCopy &c, void (*present) (void *), void *ctx, int winW, int winH, unsigned dfbo, bool show);
	void quadTo (GLuint src, float u, float v, float us, float vs, void (*present) (void *), void *ctx, int winW, int winH, unsigned dfbo);
};

GLuint Player::Impl::sampler (u32 m0, u32 m1, int levels)
{
	u64 key = (u64) (m0 & 0x1FFFF) | (u64) (m1 & 0xFFFF) << 17 | (u64) (levels > 1) << 33;
	for (int i = 0; i < nSamp; i++) if (samp[i].key == key) return samp[i].s;
	GLuint sm = 0;
	gGenSamplers (1, &sm);
	static const GLint wrap[4] = { GL_CLAMP_TO_EDGE, GL_REPEAT, GL_MIRRORED_REPEAT, GL_REPEAT };
	gSamplerParameteri (sm, GL_TEXTURE_WRAP_S, wrap[m0 & 3]);
	gSamplerParameteri (sm, GL_TEXTURE_WRAP_T, wrap[(m0 >> 2) & 3]);
	int minf = (int) (m0 >> 5) & 7;
	static const GLint mins[8] = { GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST, GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR };
	gSamplerParameteri (sm, GL_TEXTURE_MIN_FILTER, levels > 1 ? mins[minf] : (minf >= 4 ? GL_LINEAR : GL_NEAREST));
	gSamplerParameteri (sm, GL_TEXTURE_MAG_FILTER, (m0 & 16) ? GL_LINEAR : GL_NEAREST);
	if (levels > 1)
	{
		gSamplerParameterf (sm, GL_TEXTURE_MIN_LOD, (float) (m1 & 0xFF) / 16.f);
		gSamplerParameterf (sm, GL_TEXTURE_MAX_LOD, (float) ((m1 >> 8) & 0xFF) / 16.f);
	}
	if (nSamp < 1024) { samp[nSamp].key = key; samp[nSamp].s = sm; nSamp++; }
	return sm;
}

static GLuint shader (GLenum type, const char *src, const char **err)
{
	GLuint s = gCreateShader (type);
	gShaderSource (s, 1, &src, 0);
	gCompileShader (s);
	GLint ok = 0;
	gGetShaderiv (s, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		static char log[1024];
		gGetShaderInfoLog (s, sizeof log, 0, log);
		*err = log;
		gDeleteShader (s);
		return 0;
	}
	return s;
}
static GLuint linkp (GLuint vs, GLuint fs, const char **err)
{
	GLuint p = gCreateProgram ();
	gAttachShader (p, vs); gAttachShader (p, fs);
	gLinkProgram (p);
	GLint ok = 0;
	gGetProgramiv (p, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		static char log[1024];
		gGetProgramInfoLog (p, sizeof log, 0, log);
		*err = log;
		return 0;
	}
	return p;
}
GLuint Player::Impl::vshaderFor (int chunks)
{
	if (!vshader[chunks])
	{
		char b[2048];
		genVertex (chunks, b, sizeof b);
		vshader[chunks] = shader (GL_VERTEX_SHADER, b, &o->st.err);
	}
	return vshader[chunks];
}

// le programme d'une configuration : compile si le budget le permet (force : quoi qu'il en coute)
bool Player::Impl::program (Prog &p, bool force)
{
	if (p.program == ~0u) return false;
	if (p.program) return true;
	if (!force && budgetLeft == 0) return false;
	u64 t0 = usNow ();
	bool ok = false;
	if (genFragment (p, fsBuf, sizeof fsBuf))
	{
		GLuint fs = shader (GL_FRAGMENT_SHADER, fsBuf, &o->st.err);
		GLuint vs = vshaderFor (p.chunks);
		if (fs && vs)
		{
			GLuint g = linkp (vs, fs, &o->st.err);
			if (g)
			{
				gUseProgram (g); cProg = g;
				for (int m = 0; m < 8; m++) if (p.mapMask & (1u << m)) { char n[4] = { 's', (char) ('0' + m), 0, 0 }; gUniform1i (gGetUniformLocation (g, n), m); }
				p.uR = gGetUniformLocation (g, "uR"); p.uK = gGetUniformLocation (g, "uK"); p.uA = gGetUniformLocation (g, "uA");
				p.uIS = gGetUniformLocation (g, "uIS"); p.uF0 = gGetUniformLocation (g, "uF0"); p.uF1 = gGetUniformLocation (g, "uF1");
				p.uF2 = gGetUniformLocation (g, "uF2"); p.uFI = gGetUniformLocation (g, "uFI"); p.uIM = gGetUniformLocation (g, "uIM");
				p.program = g;
				ok = true;
			}
		}
		if (fs) gDeleteShader (fs);
	}
	u64 dt = usNow () - t0;
	o->st.compileUs += dt;
	if (dt > o->st.maxCompileUs) o->st.maxCompileUs = dt;
	budgetLeft = budgetLeft > dt ? budgetLeft - dt : 0;
	if (ok) { o->st.compiled++; return true; }
	p.program = ~0u; o->st.failed++;
	if (o->st.err) fprintf (stderr, "gxes : shader refuse : %s\n%s\n", o->st.err, fsBuf);
	return false;
}

void Player::Impl::upload (Machine &m, int t)
{
	gc::GTexture &T = m.tex[t];
	if (!texObj[t]) gGenTextures (1, &texObj[t]);
	gActiveTexture (GL_TEXTURE0);
	gBindTexture (GL_TEXTURE_2D, texObj[t]); cTex[0] = texObj[t];
	gPixelStorei (GL_UNPACK_ALIGNMENT, 4);
	const u32 *px = T.px;
	int levels = T.levels < 1 ? 1 : T.levels;
	for (int l = 0; l < levels; l++)
	{
		int lw = T.w >> l ? T.w >> l : 1, lh = T.h >> l ? T.h >> l : 1;
		gTexImage2D (GL_TEXTURE_2D, l, GL_BGRA_EXT, lw, lh, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px);
		px += lw * lh;
	}
	gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, levels - 1);
	texLevels[t] = levels;
	T.dirty = false;
	o->st.uploads++;
}

void Player::Impl::bindTextures (Machine &m, const Batch &b, const Prog *pr)
{
	(void) m;
	for (int k = 0; k < 8; k++)
	{
		if (pr && !(pr->mapMask & (1u << k))) continue;
		int t = b.tex[k];
		if (t < 0) continue;
		GLuint obj = dummy; int levels = 1;
		if (t >= GX_TEX_COPY) { if (copies[t - GX_TEX_COPY].tex) obj = copies[t - GX_TEX_COPY].tex; }
		else if (texObj[t]) { obj = texObj[t]; levels = texLevels[t]; }
		GLuint sm = sampler (b.mode0[k], b.mode1[k], levels);
		if (cTex[k] != obj) { gActiveTexture (GL_TEXTURE0 + k); gBindTexture (GL_TEXTURE_2D, obj); cTex[k] = obj; }
		if (cSamp[k] != sm) { gBindSampler ((GLuint) k, sm); cSamp[k] = sm; }
	}
}

static const GLenum ZFUNC[8] = { GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS };
static const GLenum BSRC[8] = { GL_ZERO, GL_ONE, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };
static const GLenum BDST[8] = { GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA };

void Player::Impl::drawBatch (Rec &r, Machine &m, const Frame &F, const Batch &b)
{
	Prog &p = r.prog[b.prog];
	bool real = program (p, false);
	const Uni &u = F.u[b.uni];
	GLuint g = real ? p.program : fallback;
	if (g != cProg) { gUseProgram (g); cProg = g; cUniProg = 0; }
	if (!real)
	{
		// le repli : la premiere carte lue, unite 0 du shader de repli
		int m0 = -1, at = -1;
		for (int k = 0; k < 8; k++) if ((p.mapMask >> k) & 1) { m0 = k; break; }
		for (int i = 0; i < p.nVary; i++)
			if (p.vary[i].kind == V_ST && p.vary[i].b == 0) { at = i; break; }
		if (at >= 24) at = -1;
		o->st.fallback++;
		gUniform1i (uFbS0, m0 >= 0 ? m0 : 0);
		gUniform3i (uAt, p.c0At, m0 >= 0 ? at : -1, 0);
		int cd = at >= 0 ? p.vary[at].a : 0;
		gUniform2f (uSc, m0 >= 0 && p.normMap[cd] < 0 ? u.texInv[m0][0] : 1.f, m0 >= 0 && p.normMap[cd] < 0 ? u.texInv[m0][1] : 1.f);
		bindTextures (m, b, &p);
	}
	else
	{
		bindTextures (m, b, &p);
		if (cUniProg != g || cUni != b.uni)
		{
			gUniform4iv (p.uR, 4, u.regs); gUniform4iv (p.uK, 4, u.konst); gUniform4iv (p.uA, 1, u.alpha);
			if (p.uIS >= 0) gUniform4fv (p.uIS, 8, &u.texInv[0][0]);
			if (p.uF0 >= 0) gUniform4f (p.uF0, u.fogF[0], u.fogF[1], u.fogF[2], u.fogF[3]);
			if (p.uF1 >= 0) gUniform4fv (p.uF1, 1, u.fogColor);
			if (p.uF2 >= 0) gUniform4fv (p.uF2, 3, u.fogK);
			if (p.uFI >= 0) gUniform4iv (p.uFI, 1, u.fogI);
			if (p.uIM >= 0) gUniform4iv (p.uIM, 3, u.indMtx);
			cUniProg = g; cUni = b.uni;
		}
	}
	int sc[4] = { b.scissor[0], b.scissor[1], b.scissor[2] - b.scissor[0], b.scissor[3] - b.scissor[1] };
	if (sc[0] < 0) { sc[2] += sc[0]; sc[0] = 0; }
	if (sc[1] < 0) { sc[3] += sc[1]; sc[1] = 0; }
	if (sc[2] < 0) sc[2] = 0;
	if (sc[3] < 0) sc[3] = 0;
	if (memcmp (sc, cSc, sizeof sc)) { gEnable (GL_SCISSOR_TEST); gScissor (sc[0], sc[1], sc[2], sc[3]); memcpy (cSc, sc, sizeof sc); }
	if (b.zon != cZon) { if (b.zon) gEnable (GL_DEPTH_TEST); else gDisable (GL_DEPTH_TEST); cZon = b.zon; }
	if (b.zon && b.zfunc != cZf) { gDepthFunc (ZFUNC[b.zfunc]); cZf = b.zfunc; }
	if (b.zwrite != cZw) { gDepthMask ((GLboolean) b.zwrite); cZw = b.zwrite; }
	if (b.cmask != cCw || b.amask != cAw) { gColorMask (b.cmask, b.cmask, b.cmask, b.amask); cCw = b.cmask; cAw = b.amask; }
	if (b.blendOn != cBlend) { if (b.blendOn) gEnable (GL_BLEND); else gDisable (GL_BLEND); cBlend = b.blendOn; }
	if (b.blendOn)
	{
		int bs = b.blendSrc, bd = b.blendDst;
		if (bs != cBs || bd != cBd) { gBlendFunc (BSRC[bs], BDST[bd]); cBs = bs; cBd = bd; }
		if (b.blendSub != cBq) { gBlendEquation (b.blendSub ? GL_FUNC_REVERSE_SUBTRACT : GL_FUNC_ADD); cBq = b.blendSub; }
	}
	int cull = b.prim == P_TRI ? b.cull : 0;
	if (cull != cCull)
	{
		if (!cull) gDisable (GL_CULL_FACE);
		else { gEnable (GL_CULL_FACE); gCullFace (cull == 1 ? GL_BACK : GL_FRONT); }
		cCull = cull;
	}
	if (b.chunks != cVao) { gBindVertexArray (vao[b.chunks]); cVao = b.chunks; }
	GLenum mode = GL_TRIANGLES;
	if (b.prim == P_LINE) { mode = GL_LINES; gLineWidth (b.lp); }
	else if (b.prim == P_POINT) { mode = GL_POINTS; }
	gDrawArrays (mode, (GLint) b.off, (GLsizei) b.count);
	o->st.batches++;
}

void Player::Impl::quadTo (GLuint src, float u, float v, float us, float vs, void (*present) (void *), void *ctx, int winW, int winH, unsigned dfbo)
{
	gBindFramebuffer (GL_FRAMEBUFFER, dfbo);
	gViewport (0, 0, winW, winH);
	gDisable (GL_SCISSOR_TEST); gDisable (GL_DEPTH_TEST); gDisable (GL_BLEND); gDisable (GL_CULL_FACE);
	gColorMask (1, 1, 1, 1);
	gUseProgram (presentProg);
	gBindSampler (0, 0);
	gActiveTexture (GL_TEXTURE0);
	gBindTexture (GL_TEXTURE_2D, src);
	gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	gUniform4f (uPresentUv, u, v, us, vs);
	gBindVertexArray (quadVao);
	gDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
	reset ();
	if (present) present (ctx);
}

void Player::Impl::doCopy (const GxCopy &c, void (*present) (void *), void *ctx, int winW, int winH, unsigned dfbo, bool show)
{
	int x = c.x, y = c.y, w = c.w, h = c.h;
	if (c.toXfb)
	{
		if (x + w > EFB_W) w = EFB_W - x;
		if (y + h > EFB_H) h = EFB_H - y;
		if (o->grab) o->readEfb (o->grab, x, y, w, h);
		if (show)
		{
			quadTo (efbColor, (float) x / EFB_W, (float) y / EFB_H, (float) w / EFB_W, (float) h / EFB_H, present, ctx, winW, winH, dfbo);
			o->lastW = w; o->lastH = h;
		}
		gBindFramebuffer (GL_FRAMEBUFFER, efbFbo);
		gViewport (0, 0, EFB_W, EFB_H);
	}
	else if (c.slot >= 0 && c.slot < GX_COPIES)
	{
		CopyTex &t = copies[c.slot];
		int dw = c.half ? c.w / 2 : c.w, dh = c.half ? c.h / 2 : c.h;
		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;
		if (!t.tex || t.w != dw || t.h != dh)
		{
			if (!t.tex) { gGenTextures (1, &t.tex); gGenFramebuffers (1, &t.fbo); }
			gActiveTexture (GL_TEXTURE0);
			gBindTexture (GL_TEXTURE_2D, t.tex); cTex[0] = t.tex;
			gTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, dw, dh, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
			gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
			gBindFramebuffer (GL_FRAMEBUFFER, t.fbo);
			gFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
			t.w = dw; t.h = dh;
		}
		gBindFramebuffer (GL_FRAMEBUFFER, t.fbo);
		gViewport (0, 0, dw, dh);
		gDisable (GL_SCISSOR_TEST); gDisable (GL_DEPTH_TEST); gDisable (GL_BLEND); gDisable (GL_CULL_FACE);
		gColorMask (1, 1, 1, 1);
		gUseProgram (copyProg);
		gBindSampler (0, 0);
		gActiveTexture (GL_TEXTURE0);
		GLuint src = c.depth ? efbDepth : efbColor;
		gBindTexture (GL_TEXTURE_2D, src);
		GLint fl = c.half && !c.depth ? GL_LINEAR : GL_NEAREST;
		gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fl); gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, fl);
		gUniform4f (uCopyRect, (float) x, (float) y, (float) w, (float) h);
		gUniform2f (uCopySize, (float) EFB_W, (float) EFB_H);
		gUniform4i (uCopyMode, (int) c.fmt, c.depth ? 1 : 0, c.intensity ? 1 : 0, c.efbFmt == 1 ? 1 : 0);
		gBindVertexArray (quadVao);
		gDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
		reset ();
		gBindFramebuffer (GL_FRAMEBUFFER, efbFbo);
		gViewport (0, 0, EFB_W, EFB_H);
		o->st.copies++;
	}
	if (c.clear)
	{
		bool ca = c.alphaMask && c.efbFmt == 1;
		GLbitfield mask = (c.colorMask || ca ? GL_COLOR_BUFFER_BIT : 0) | (c.zMask ? GL_DEPTH_BUFFER_BIT : 0);
		if (mask)
		{
			gEnable (GL_SCISSOR_TEST); gScissor (x, y, w, h); cSc[0] = -2;
			gColorMask (c.colorMask, c.colorMask, c.colorMask, ca); cCw = -1;
			gDepthMask (c.zMask); cZw = -1;
			u32 cc = c.clearColor;
			gClearColor ((float) ((cc >> 16) & 255) / 255.f, (float) ((cc >> 8) & 255) / 255.f, (float) (cc & 255) / 255.f, (float) (cc >> 24) / 255.f);
			gClearDepthf ((float) c.clearZ / 16777216.0f);
			gClear (mask);
		}
	}
}

bool Player::init (void *(*proc) (const char *))
{
#define X(ret, name, args) g##name = (T_##name) proc ("gl" #name); if (!g##name) return false;
	GL_LIST (X)
#undef X
	p = new Impl;
	memset (p, 0, sizeof *p);
	memset (&st, 0, sizeof st);
	Impl &I = *p;
	I.o = this;
	gGenTextures (1, &I.efbColor); gGenTextures (1, &I.efbDepth); gGenTextures (1, &I.dummy); gGenTextures (1, &I.fbTex);
	gBindTexture (GL_TEXTURE_2D, I.efbColor);
	gTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, EFB_W, EFB_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gBindTexture (GL_TEXTURE_2D, I.efbDepth);
	gTexImage2D (GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, EFB_W, EFB_H, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, 0);
	gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gGenFramebuffers (1, &I.efbFbo);
	gBindFramebuffer (GL_FRAMEBUFFER, I.efbFbo);
	gFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, I.efbColor, 0);
	gFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, I.efbDepth, 0);
	if (gCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { st.err = "EFB incomplet"; return false; }
	gDisable (GL_SCISSOR_TEST);
	gClearColor (0, 0, 0, 1); gClearDepthf (1.0f);
	gClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	gBindTexture (GL_TEXTURE_2D, I.dummy);
	u32 black = 0;
	gTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &black);
	gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	gGenBuffers (1, &I.vbo);
	gBindBuffer (GL_ARRAY_BUFFER, I.vbo);
	gGenVertexArrays (MAX_CHUNKS + 1, I.vao);
	for (int c = 0; c <= MAX_CHUNKS; c++)
	{
		gBindVertexArray (I.vao[c]);
		for (int k = 0; k <= c; k++)
		{
			gVertexAttribPointer ((GLuint) k, 4, GL_FLOAT, GL_FALSE, (c + 1) * 16, (const void *) (size_t) (k * 16));
			gEnableVertexAttribArray ((GLuint) k);
		}
	}
	gGenVertexArrays (1, &I.quadVao);
	gBindVertexArray (0);
	const char *e = 0;
	GLuint vs = shader (GL_VERTEX_SHADER, FALLBACK_VS, &e), fs = shader (GL_FRAGMENT_SHADER, FALLBACK_FS, &e);
	I.fallback = vs && fs ? linkp (vs, fs, &e) : 0;
	GLuint qv = shader (GL_VERTEX_SHADER, QUAD_VS, &e), cf = shader (GL_FRAGMENT_SHADER, COPY_FS, &e), pf = shader (GL_FRAGMENT_SHADER, PRESENT_FS, &e);
	I.copyProg = qv && cf ? linkp (qv, cf, &e) : 0;
	I.presentProg = qv && pf ? linkp (qv, pf, &e) : 0;
	if (!I.fallback || !I.copyProg || !I.presentProg) { st.err = e; if (e) fprintf (stderr, "gxes : %s\n", e); return false; }
	I.uAt = gGetUniformLocation (I.fallback, "uAt"); I.uSc = gGetUniformLocation (I.fallback, "uSc"); I.uFbS0 = gGetUniformLocation (I.fallback, "s0");
	gUseProgram (I.copyProg);
	gUniform1i (gGetUniformLocation (I.copyProg, "src"), 0);
	I.uCopyRect = gGetUniformLocation (I.copyProg, "rect"); I.uCopySize = gGetUniformLocation (I.copyProg, "srcSize"); I.uCopyMode = gGetUniformLocation (I.copyProg, "mode");
	gUseProgram (I.presentProg);
	gUniform1i (gGetUniformLocation (I.presentProg, "src"), 0);
	I.uPresentUv = gGetUniformLocation (I.presentProg, "uv");
	gUseProgram (0);
	gBindFramebuffer (GL_FRAMEBUFFER, 0);
	I.reset ();
	return true;
}

int Player::pump (Rec &r, Machine &m, void (*present) (void *), void *ctx, int winW, int winH, unsigned dfbo)
{
	Impl &I = *p;
	if (r.rd == r.wr) return 0;
	int shown = 0;
	gBindFramebuffer (GL_FRAMEBUFFER, I.efbFbo);
	gViewport (0, 0, EFB_W, EFB_H);
	gBindBuffer (GL_ARRAY_BUFFER, I.vbo);
	I.reset ();
	while (r.rd != r.wr)
	{
		Frame &F = r.frame[r.rd % NFRAMES];
		bool late = r.wr - r.rd > 1;			// (du retard : seulement ce dont les copies ont besoin)
		u32 last = F.nb;
		if (late)
		{
			last = 0;
			for (u32 i = 0; i < F.nb; i++) if (F.b[i].kind == B_COPY && !F.c[F.b[i].copy].toXfb) last = i + 1;
			st.skipped++;
		}
		// les textures neuves : le GX attend s'il en fait une en meme temps
		m.gxLock ();
		for (u32 i = 0; i < last; i++)
		{
			const Batch &b = F.b[i];
			if (b.kind != B_DRAW) continue;
			for (int k = 0; k < 8; k++)
			{
				int t = b.tex[k];
				if (t < 0 || t >= GX_TEX_COPY) continue;
				gc::GTexture &T = m.tex[t];
				if ((T.dirty || !I.texObj[t]) && T.px && T.w > 0 && T.h > 0) I.upload (m, t);
			}
		}
		m.gxUnlock ();
		gBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) F.nf * 4, F.v, GL_STREAM_DRAW);
		I.budgetLeft = (u64) budgetUs;
		for (u32 i = 0; i < F.nb; i++)
		{
			const Batch &b = F.b[i];
			if (b.kind == B_COPY) I.doCopy (F.c[b.copy], present, ctx, winW, winH, dfbo, !late);
			else if (i < last) I.drawBatch (r, m, F, b);
		}
		__sync_synchronize ();
		r.rd = r.rd + 1;
		st.frames++;
		if (!late) shown = 1;
	}
	gBindFramebuffer (GL_FRAMEBUFFER, dfbo);
	gBindVertexArray (0);
	return shown;
}

void Player::showFb (const unsigned *px, int w, int h, int pitch, void (*present) (void *), void *ctx, int winW, int winH, unsigned dfbo)
{
	Impl &I = *p;
	gActiveTexture (GL_TEXTURE0);
	gBindTexture (GL_TEXTURE_2D, I.fbTex);
	gPixelStorei (GL_UNPACK_ROW_LENGTH, pitch);
	gPixelStorei (GL_UNPACK_ALIGNMENT, 4);
	if (I.fbW != w || I.fbH != h)
	{
		gTexImage2D (GL_TEXTURE_2D, 0, GL_BGRA_EXT, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px);
		gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); gTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		I.fbW = w; I.fbH = h;
	}
	else gTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, w, h, GL_BGRA_EXT, GL_UNSIGNED_BYTE, px);
	gPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
	I.quadTo (I.fbTex, 0, 0, 1, 1, present, ctx, winW, winH, dfbo);
}

void Player::readEfb (unsigned *px, int x, int y, int w, int h)
{
	gBindFramebuffer (GL_FRAMEBUFFER, p->efbFbo);
	gPixelStorei (GL_PACK_ALIGNMENT, 4);
	gReadPixels (x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (int i = 0; i < w * h; i++) { u32 c = px[i]; px[i] = (c & 0xFF00FF00) | ((c & 0xFF) << 16) | ((c >> 16) & 0xFF); }
}

// v3d ne compile vraiment qu'au premier dessin, pour le format de la cible : un dessin sur l'EFB, rien d'ecrit
int Player::precompile (Rec &r, int index)
{
	Impl &I = *p;
	if (index >= r.nProg) return 0;
	Prog &g = r.prog[index];
	if (!I.program (g, true)) return 0;
	gBindFramebuffer (GL_FRAMEBUFFER, I.efbFbo);
	gViewport (0, 0, EFB_W, EFB_H);
	gBindBuffer (GL_ARRAY_BUFFER, I.vbo);
	static float zero[3 * 36];
	gBufferData (GL_ARRAY_BUFFER, sizeof zero, zero, GL_STREAM_DRAW);
	gUseProgram (g.program);
	gBindVertexArray (I.vao[g.chunks]);
	gDisable (GL_BLEND); gDisable (GL_CULL_FACE); gDisable (GL_DEPTH_TEST);
	gEnable (GL_SCISSOR_TEST); gScissor (0, 0, 1, 1);
	gColorMask (0, 0, 0, 0);
	gDrawArrays (GL_TRIANGLES, 0, 3);
	gColorMask (1, 1, 1, 1);
	gBindVertexArray (0);
	I.reset ();
	return 1;
}

} // namespace gxes
