//
// n3ds/n3ds_glsl.cpp -- les shaders GLSL ES du PICA200, generes par configuration.
//
// Le fragment shader refait le combineur en entiers, comme le rendu logiciel du coeur (n3ds_pica.cpp) : memes
// operandes, memes modes, meme division exacte par 255, le tampon du combineur deux etages en retard, le test
// alpha ; le resultat sort en .bgra, pour que les octets soient ceux de 0xAARRGGBB. Chaque programme a aussi un
// frere reduit (la premiere texture, sinon la couleur du sommet) : il sert le temps que le vrai soit compile.
//
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "n3ds/n3ds.h"
#include "n3ds/pica_gen.h"

namespace {
struct Text
{
	char *p; int n, cap;
	void add (const char *fmt, ...)
	{
		va_list ap; va_start (ap, fmt);
		const int k = vsnprintf (p + n, (size_t) (cap - n), fmt, ap);
		va_end (ap);
		if (k > 0) n += k;
		if (n > cap - 1) n = cap - 1;
	}
};
}

namespace picatev {

unsigned long long key (const Config &c)
{
	unsigned long long h = 1469598103934665603ull;
	const unsigned char *p = (const unsigned char *) &c;
	for (unsigned i = 0; i < sizeof c; i++) h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

static const signed char ARGS[16] = { 1, 2, 2, 2, 3, 2, 2, 2, 3, 3, 1, 1, 1, 1, 1, 1 };

static void colorOf (Text &t, const char *src, unsigned op)
{
	if (op == 0 || op > 13 || ((op & 3) > 1 && op > 3)) { t.add ("%s.rgb", src); return; }
	if (op == 1) { t.add ("(255-%s.rgb)", src); return; }
	const char c = op < 4 ? 'a' : op < 8 ? 'r' : op < 12 ? 'g' : 'b';
	if (op & 1) t.add ("ivec3(255-%s.%c)", src, c); else t.add ("ivec3(%s.%c)", src, c);
}
static void alphaOf (Text &t, const char *src, unsigned op)
{
	const char c = op < 2 ? 'a' : op < 4 ? 'r' : op < 6 ? 'g' : 'b';
	if (op & 1) t.add ("(255-%s.%c)", src, c); else t.add ("%s.%c", src, c);
}

static void combine (Text &t, unsigned mode, const char *a, const char *b, const char *c, bool vec)
{
	const char *cl = vec ? "C3" : "C1", *di = vec ? "D3" : "D1", *fd = vec ? "F3" : "F1";
	switch (mode)
	{
	case 1: t.add ("%s(%s*%s)", di, a, b); break;
	case 2: t.add ("%s(%s+%s)", cl, a, b); break;
	case 3: t.add ("%s(%s+%s-128)", cl, a, b); break;
	case 4: t.add ("%s(%s*%s+%s*(255-%s))", di, a, c, b, c); break;
	case 5: t.add ("%s(%s-%s)", cl, a, b); break;
	case 8: t.add ("%s(%s(%s*%s)+%s)", cl, di, a, b, c); break;
	case 9: t.add ("%s(%s((%s+%s)*%s))", cl, fd, a, b, c); break;
	default: t.add ("%s", a); break;
	}
}

static int vary (Shader &s, unsigned char kind, unsigned char a, unsigned char b)
{
	Vary &v = s.vary[s.nVary];
	v.kind = kind; v.a = a; v.b = b;
	return s.nVary++;
}
static int uniform (Shader &s, unsigned char kind, unsigned char a, unsigned char b, unsigned value = 0)
{
	Uni &u = s.uni[s.nUni];
	u.kind = kind; u.a = a; u.b = b; u.value = value;
	return s.nUni++;
}
static inline char comp (int i) { return "xyzw"[i & 3]; }

bool build (const Config &c, Shader &s)
{
	s.nUni = s.nVary = s.nLook = 0; s.err = 0; s.unsupported = false; s.flags = 0; s.nRegs = 0; s.nClaim = 0;
	// ce que lisent les etages
	unsigned used = 0;
	int end = 0;
	for (int i = 0; i < 6; i++)
	{
		const Config::Stage &g = c.stage[i];
		if (g.pass) continue;
		end = i + 1;
		for (int k = 0; k < ARGS[g.mode & 15]; k++) used |= 1u << g.src[k];
		if (g.mode != 7) for (int k = 0; k < ARGS[g.modeA & 15]; k++) used |= 1u << g.srcA[k];
	}
	const bool primary = !end || (used & 1) || (used >> 15 & 1) || (used >> 7 & 0x3F) || (!c.lit && (used & 6));
	// les entrees, dans l'ordre de GProg::vary
	int vP = -1, vL = -1, vS = -1, vT[3] = { -1, -1, -1 };
	if (primary) { vP = s.nVary; for (int k = 0; k < 4; k++) vary (s, V_PRIMARY, (unsigned char) k, 0); }
	if (c.lit && (used & 2)) { vL = s.nVary; for (int k = 0; k < 4; k++) vary (s, V_LITP, (unsigned char) k, 0); }
	if (c.lit && (used & 4)) { vS = s.nVary; for (int k = 0; k < 4; k++) vary (s, V_LITS, (unsigned char) k, 0); }
	for (int u = 0; u < 3; u++)
		if ((c.texOn >> u & 1) && (used >> (3 + u) & 1))
		{
			const int look = s.nLook++;
			s.look[look].unit = (unsigned char) u;
			vT[u] = s.nVary;
			vary (s, V_COORD, (unsigned char) look, 0); vary (s, V_COORD, (unsigned char) look, 1);
			uniform (s, U_TEXP0, (unsigned char) look, 0);
		}
	int vProc = -1, uProcW = 0, uProcO = 0;
	if (c.procOn && (used >> 6 & 1))
	{
		vProc = s.nVary;
		vary (s, V_PROC, c.procCoord, 0); vary (s, V_PROC, c.procCoord, 1);
		uProcW = uniform (s, U_PROCW, 0, 0); uProcO = uniform (s, U_PROCO, 0, 0);
	}
	Text t = { s.prog.text (), 0, qpu::Prog::MAX - 2048 };
	t.add ("#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n");
	const int nv = (s.nVary + 3) / 4;
	for (int i = 0; i < nv; i++) t.add ("in vec4 v%d;\n", i);
	for (int i = 0; i < s.nLook; i++) t.add ("uniform sampler2D t%d;\n", i);
	if (vProc >= 0) t.add ("uniform sampler2D tm;\nuniform sampler2D tc;\n");
	const int uAt = t.n;
	t.add ("uniform int u[000];\n");
	t.add ("layout(location=0) out vec4 o;\n"
	       "int D1(int x){return (x+1+(x>>8))>>8;}\nivec3 D3(ivec3 x){return (x+1+(x>>8))>>8;}\n"
	       "int C1(int x){return clamp(x,0,255);}\nivec3 C3(ivec3 x){return clamp(x,0,255);}\n"
	       "int F1(int x){return int(float(x)/255.0+0.001);}\nivec3 F3(ivec3 x){return ivec3(vec3(x)/255.0+0.001);}\n"
	       "ivec4 I(vec4 c){return ivec4(clamp(c,0.0,1.0)*255.0+0.5);}\n"
	       "%s"
	       "void main(){\n", vProc >= 0 ? "float M(float x,int t){x*=128.0;int i=clamp(int(x),0,127);"
	       "return clamp(texelFetch(tm,ivec2(i,t*2),0).r+texelFetch(tm,ivec2(i,t*2+1),0).r*(x-float(i)),0.0,1.0);}\n"
	       "float Q(float c,int m){c=abs(c);if(m==0)return c>1.0?0.0:c;if(m==1)return min(c,1.0);if(m==2)return fract(c);"
	       "if(m==3){int i=int(c);float f=c-float(i);return (i&1)!=0?1.0-f:f;}return c>0.5?1.0:0.0;}\n"
	       "float G(float u,float v,int f){if(f==0)return u;if(f==1)return u*u;if(f==2)return v;if(f==3)return v*v;if(f==4)return (u+v)*0.5;"
	       "if(f==5)return (u*u+v*v)*0.5;float d=sqrt(u*u+v*v);if(f==6)return min(d,1.0);if(f==7)return min(u,v);if(f==8)return max(u,v);"
	       "return min(max((u+v)*0.5,d),1.0);}\n" : "");
	#define VEC4(at) t.add ("vec4(v%d.%c,v%d.%c,v%d.%c,v%d.%c)", (at) / 4, comp ((at)), ((at) + 1) / 4, comp ((at) + 1), ((at) + 2) / 4, comp ((at) + 2), ((at) + 3) / 4, comp ((at) + 3))
	if (primary) { t.add ("ivec4 s0=I("); VEC4 (vP); t.add (");\n"); }
	else t.add ("ivec4 s0=ivec4(0,0,0,255);\n");
	if (used & 2) { if (vL >= 0) { t.add ("ivec4 s1=I("); VEC4 (vL); t.add (");\n"); } else t.add ("ivec4 s1=s0;\n"); }
	if (used & 4) { if (vS >= 0) { t.add ("ivec4 s2=I("); VEC4 (vS); t.add (");\n"); } else t.add ("ivec4 s2=ivec4(0,0,0,255);\n"); }
	for (int u = 0; u < 3; u++)
	{
		if (!(used >> (3 + u) & 1)) continue;
		if (vT[u] < 0) { t.add ("ivec4 s%d=ivec4(0,0,0,255);\n", 3 + u); continue; }
		int look = 0;
		for (int l = 0; l < s.nLook; l++) if (s.look[l].unit == u) look = l;
		t.add ("ivec4 s%d=ivec4(texture(t%d,vec2(v%d.%c,v%d.%c))*255.0+0.5);\n", 3 + u, look, vT[u] / 4, comp (vT[u]), (vT[u] + 1) / 4, comp (vT[u] + 1));
	}
	if (vProc >= 0)
	{
		t.add ("float pu=Q(v%d.%c,%d),pv=Q(v%d.%c,%d);\n", vProc / 4, comp (vProc), c.procClampU, (vProc + 1) / 4, comp (vProc + 1), c.procClampV);
		t.add ("float px=M(G(pu,pv,%d),0);\nint pi=min(u[%d]+int(px*float(max(u[%d]-1,0))+0.5),255);\n", c.procFuncRgb, uProcO, uProcW);
		t.add ("ivec4 s6=ivec4(texelFetch(tc,ivec2(pi,0),0)*255.0+0.5);\n");
		if (c.procSeparateA) t.add ("s6.a=int(M(G(pu,pv,%d),1)*255.0+0.5);\n", c.procFuncA);
	}
	else if (used >> 6 & 1) t.add ("ivec4 s6=ivec4(0,0,0,255);\n");
	const bool usesBuffer = used >> 13 & 1;
	t.add ("ivec4 p=s0;\n");
	if (usesBuffer)
	{
		const int ub = s.nUni;
		for (int k = 0; k < 4; k++) uniform (s, U_BUFFER, 0, (unsigned char) k);
		t.add ("ivec4 b=ivec4(0);\nivec4 n=ivec4(u[%d],u[%d],u[%d],u[%d]);\n", ub, ub + 1, ub + 2, ub + 3);
	}
	for (int st = 0; st < end; st++)
	{
		const Config::Stage &g = c.stage[st];
		if (!g.pass)
		{
			t.add ("{\n");
			bool wantK = false;
			for (int k = 0; k < 3; k++) if (g.src[k] == 14 || g.srcA[k] == 14) wantK = true;
			if (wantK)
			{
				const int uk = s.nUni;
				for (int k = 0; k < 4; k++) uniform (s, U_KONST, (unsigned char) st, (unsigned char) k);
				t.add ("ivec4 k%d=ivec4(u[%d],u[%d],u[%d],u[%d]);\n", st, uk, uk + 1, uk + 2, uk + 3);
			}
			char name[3][6], kn[6];
			snprintf (kn, sizeof kn, "k%d", st);
			auto srcName = [&] (unsigned sc, char *d)
			{
				if (sc == 13) strcpy (d, "b");
				else if (sc == 14) strcpy (d, kn);
				else if (sc == 15 || (sc >= 7 && sc <= 12)) strcpy (d, "p");
				else snprintf (d, 6, "s%u", sc);
			};
			const int na = g.mode == 7 ? 0 : ARGS[g.modeA & 15], nc = ARGS[g.mode & 15];
			static const char *CN[3] = { "c0", "c1", "c2" }, *AN[3] = { "a0", "a1", "a2" };
			for (int j = 0; j < nc; j++)
			{
				srcName (g.src[j], name[j]);
				t.add ("ivec3 c%d=", j); colorOf (t, name[j], g.op[j]); t.add (";\n");
			}
			for (int j = 0; j < na; j++)
			{
				srcName (g.srcA[j], name[j]);
				t.add ("int a%d=", j); alphaOf (t, name[j], g.opA[j]); t.add (";\n");
			}
			t.add ("ivec3 r=");
			if (g.mode == 6 || g.mode == 7)
				t.add ("ivec3(F1(max((c0.r*2-255)*(c1.r*2-255)+(c0.g*2-255)*(c1.g*2-255)+(c0.b*2-255)*(c1.b*2-255),0)))");
			else combine (t, g.mode, CN[0], CN[1], CN[2], true);
			t.add (";\nint e=");
			if (g.mode == 7) t.add ("r.r");
			else combine (t, g.modeA, AN[0], AN[1], AN[2], false);
			t.add (";\n");
			if (g.scale != 1) t.add ("r=C3(r*%d);\n", g.scale);
			if (g.scaleA != 1) t.add ("e=C1(e*%d);\n", g.scaleA);
			t.add ("p=ivec4(r,e);\n}\n");
		}
		if (usesBuffer)
		{
			t.add ("b=n;\n");
			if (st < 4)
			{
				if (c.updateRgb >> st & 1) t.add ("n.rgb=p.rgb;\n");
				if (c.updateA >> st & 1) t.add ("n.a=p.a;\n");
			}
		}
	}
	if (c.alphaTest && c.alphaFunc != 1)
	{
		if (c.alphaFunc == 0) t.add ("discard;\n");
		else
		{
			static const char *CMP[8] = { "", "", "==", "!=", "<", "<=", ">", ">=" };
			const int ua = uniform (s, U_ALPHAREF, 0, 0);
			t.add ("if(!(p.a%su[%d]))discard;\n", CMP[c.alphaFunc & 7], ua);
		}
	}
	t.add ("o=vec4(p.b,p.g,p.r,p.a)/255.0;\n}\n");
	{ char num[8]; snprintf (num, sizeof num, "%3d", s.nUni ? s.nUni : 1); memcpy (t.p + uAt + 14, num, 3); }
	// le frere reduit, apres le zero final : la premiere texture du jeu, sinon la couleur du sommet
	s.prog.n = 0;
	t.p[t.n++] = 0;
	t.add ("#version 300 es\nprecision highp float;\nprecision highp sampler2D;\n");
	for (int i = 0; i < nv; i++) t.add ("in vec4 v%d;\n", i);
	for (int i = 0; i < s.nLook; i++) t.add ("uniform sampler2D t%d;\n", i);
	t.add ("uniform int u[%d];\nlayout(location=0) out vec4 o;\nvoid main(){vec4 c=", s.nUni ? s.nUni : 1);
	if (s.nLook) t.add ("texture(t0,vec2(v%d.%c,v%d.%c))", vT[s.look[0].unit] / 4, comp (vT[s.look[0].unit]), (vT[s.look[0].unit] + 1) / 4, comp (vT[s.look[0].unit] + 1));
	else if (primary) VEC4 (vP);
	else t.add ("vec4(0.0,0.0,0.0,1.0)");
	t.add (";\n");
	if (s.nLook && primary) { t.add ("c*="); VEC4 (vP); t.add (";\n"); }
	if (c.alphaTest && c.alphaFunc > 1) t.add ("if(c.a<0.004)discard;\n");
	t.add ("o=c.bgra;}\n");
	t.p[t.n] = 0;
	s.prog.n = (t.n + 8) / 8;
	s.prog.bad = t.n >= t.cap - 1;
	if (s.prog.bad) { s.err = "un shader trop long"; return false; }
	return true;
}
}

namespace picavs {

// Le vertex shader d'une liste specialisee (Spec) : la liste d'instructions telle quelle, les constantes et la vue
// en uniformes, l'eclairage du sommet (la meme arithmetique que picavs_ref.h). La position sort y retourne, comme
// le vertex shader des lots ombres par le processeur (n3ds_gl.cpp).
namespace {
struct Gen
{
	Text t; Uni *uni; u32 nUni, uniMax; bool over;
	int slotAt[4096 / 4];
	u32 val (u8 kind, u8 comp, u16 slot)
	{
		for (u32 i = 0; i < nUni; i++) if (uni[i].kind == kind && uni[i].comp == comp && uni[i].slot == slot) return i;
		if (nUni >= uniMax) { over = true; return 0; }
		uni[nUni].kind = kind; uni[nUni].comp = comp; uni[nUni].slot = slot;
		return nUni++;
	}
	void u1 (u32 i) { t.add ("c[%u].%c", i / 4, "xyzw"[i & 3]); }
	void light (int at) { u1 (val (U_LIGHT, 0, (u16) at)); }
	void view (int which) { u1 (val (U_VIEW, 0, (u16) which)); }
	// une constante de la liste : ses quatre composantes de suite, un vec4 de c[]
	u32 slotOf (u16 slot)
	{
		if (slot < sizeof slotAt / sizeof slotAt[0] && slotAt[slot] >= 0) return (u32) slotAt[slot];
		while (nUni & 3) { if (nUni >= uniMax) { over = true; return 0; } uni[nUni].kind = U_TINY; uni[nUni].comp = 0; uni[nUni].slot = 0; nUni++; }
		const u32 at = nUni;
		if (nUni + 4 > uniMax) { over = true; return 0; }
		for (int i = 0; i < 4; i++) { uni[nUni].kind = U_CONST; uni[nUni].comp = (u8) i; uni[nUni].slot = slot; nUni++; }
		if (slot < sizeof slotAt / sizeof slotAt[0]) slotAt[slot] = (int) at;
		return at;
	}
	void operand (const Op &o, int k)
	{
		if (o.kind[k]) { t.add ("c[%u]", slotOf (o.slot[k]) / 4); return; }
		t.add ("%sr%u.%c%c%c%c", o.neg[k] ? "-" : "", (unsigned) o.reg[k], "xyzw"[o.sw[k][0] & 3], "xyzw"[o.sw[k][1] & 3], "xyzw"[o.sw[k][2] & 3], "xyzw"[o.sw[k][3] & 3]);
	}
	void table (const Lighting &L, int k, const char *x)
	{
		const int at = tableAt (k);
		t.add ("(");
		light (at); t.add ("+%s*", x); light (at + 1);
		for (int j = 0; j < LUT_KNOTS; j++) { t.add ("+max(%s-", x); light (at + 2 + 2 * j); t.add (",0.0)*"); light (at + 3 + 2 * j); }
		t.add (")");
		(void) L;
	}
	void lighting (const Lighting &L)
	{
		auto S = [&] (const Sem &q) { if (q.reg == 0xFF) t.add ("0.0"); else t.add ("o%u.%c", (unsigned) (q.reg & 15), "xyzw"[q.comp & 3]); };
		t.add ("vec4 q=vec4("); S (L.quat[0]); t.add (","); S (L.quat[1]); t.add (","); S (L.quat[2]); t.add (","); S (L.quat[3]); t.add (");\n");
		t.add ("q*=R(dot(q,q));\nvec3 n=vec3(2.0*(q.x*q.z+q.w*q.y),2.0*(q.y*q.z-q.w*q.x),1.0-2.0*(q.x*q.x+q.y*q.y));\n");
		t.add ("vec3 raw=vec3("); S (L.view[0]); t.add (","); S (L.view[1]); t.add (","); S (L.view[2]); t.add (");\nvec3 v=raw*R(dot(raw,raw));\n");
		t.add ("vec3 dif=vec3("); light (0); t.add (","); light (1); t.add (","); light (2); t.add (");\nvec3 spe=vec3(0.0);\nfloat aP=1.0,aS=1.0;\n");
		for (int li = 0; li < L.count && li < LIGHTS_MAX; li++)
		{
			const int g = lightAt (li);
			auto V3 = [&] (int at) { t.add ("vec3("); light (at); t.add (","); light (at + 1); t.add (","); light (at + 2); t.add (")"); };
			t.add ("{\nvec3 l="); V3 (g); t.add (L.light[li].directional ? ";\n" : "+raw;\n");
			t.add ("float ld=dot(l,l),linv=R(ld),att=1.0;\n");
			if (L.light[li].distance)
			{
				t.add ("float dx=clamp(ld*linv*"); light (g + 16); t.add ("+"); light (g + 15); t.add (",0.0,1.0);\natt="); table (L, 6 + li, "dx"); t.add (";\n");
			}
			t.add ("l*=linv;\nvec3 h=v+l;h*=R(dot(h,h));\nfloat nl=dot(n,l);\nfloat on=%s;\n", L.light[li].twoSided ? "abs(nl)" : "max(nl,0.0)");
			t.add ("float in0=dot(n,h),in1=dot(v,h),in2=dot(n,v),in3=nl,zero=0.0;\n");
			auto look = [&] (int k)
			{
				if (!L.table[k].on) { t.add ("1.0"); return; }
				char x[16];
				if (L.table[k].input < 4) snprintf (x, sizeof x, L.table[k].abs ? "abs(in%d)" : "in%d", (int) L.table[k].input); else strcpy (x, "zero");
				table (L, k, x);
			};
			t.add ("float d0="); look (0); t.add (",d1="); look (1); t.add (",rr="); look (3); t.add (",rg=");
			if (L.table[4].on) look (4); else t.add ("rr");
			t.add (",rb=");
			if (L.table[5].on) look (5); else t.add ("rr");
			t.add (";\nfloat high=%s;\n", L.clampHighlights ? "nl>0.0?1.0:0.0" : "1.0");
			t.add ("dif+=("); V3 (g + 3); t.add ("*on+"); V3 (g + 6); t.add (")*att;\n");
			t.add ("spe+=("); V3 (g + 9); t.add ("*d0+"); V3 (g + 12); t.add ("*d1*vec3(rr,rg,rb))*high*att;\n");
			if (li == L.count - 1 && L.table[2].on)
			{
				t.add ("float fr="); look (2); t.add (";\n");
				if (L.fresnel & 1) t.add ("aP=fr;\n");
				if (L.fresnel & 2) t.add ("aS=fr;\n");
			}
			t.add ("}\n");
		}
		t.add ("vec4 lp=clamp(vec4(dif,aP),0.0,1.0),ls=clamp(vec4(spe,aS),0.0,1.0);\n");
	}
};
}

bool build (qpu::Prog &p, const List &l, const Write *w, u32 nw, Uni *uni, u32 uniMax, In *in, u32 inMax, Result &r, const Sem *pos, const Lighting *lit, u32 inPre)
{
	(void) inPre;
	r.ok = true; r.why = 0; r.nUni = 0; r.nIn = 0; r.values = 0; r.regsPeak = 0;
	// (le premier appel est celui du coordinate shader du V3D : OpenGL n'en a pas besoin)
	if (nw == 6 && w[0].kind == W_XC) { p.n = 1; p.w[0] = 0; return true; }
	static Gen g;
	g.t.p = p.text (); g.t.n = 0; g.t.cap = qpu::Prog::MAX - 1024;
	g.uni = uni; g.nUni = 0; g.uniMax = uniMax & ~3u; g.over = false;
	memset (g.slotAt, 0xFF, sizeof g.slotAt);
	Text &t = g.t;
	// les entrees : les composantes des registres d'entree que la liste lit avant de les ecrire
	u8 wrote[32] = { 0 }; int inAt[16][4];
	memset (inAt, 0xFF, sizeof inAt);
	u32 nIn = 0;
	auto need = [&] (u32 reg, u32 comp)
	{
		if (reg >= 16 || (wrote[reg] >> comp & 1) || inAt[reg][comp] >= 0 || nIn >= inMax) return;
		inAt[reg][comp] = (int) nIn; in[nIn].reg = (u8) reg; in[nIn].comp = (u8) comp; nIn++;
	};
	bool bad = false;
	for (u32 n = 0; n < l.nOps; n++)
	{
		const Op &o = l.ops[n];
		if (o.op == OP_GUARD || o.op == OP_FLAG || o.op == OP_PUT) { if (o.op == OP_PUT && !o.dOut && !o.guard) for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) wrote[o.d & 31] |= (u8) (1 << i); continue; }
		for (int k = 0; k < 3; k++) if (!o.kind[k]) for (int i = 0; i < 4; i++) need (o.reg[k], o.sw[k][i] & 3);
		if (o.op != OP_CMP && !o.dOut && !o.guard) for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) wrote[o.d & 31] |= (u8) (1 << i);
	}
	if (nIn >= inMax) bad = true;
	const u32 stride = nIn < 4 ? 4 : nIn, nv = (stride + 3) / 4, nOut = nw > 4 ? nw - 4 : 0;
	t.add ("#version 300 es\nprecision highp float;\n");
	for (u32 i = 0; i < nv; i++) t.add ("in vec4 i%u;\n", i);
	for (u32 i = 0; i < (nOut + 3) / 4; i++) t.add ("out vec4 v%u;\n", i);
	const int cAt = t.n;
	t.add ("uniform vec4 c[0000];\nfloat R(float s){return 1.0/sqrt(max(s,1e-30));}\nvoid main(){\n");
	for (u32 i = 0; i < 16; i++)
	{
		bool any = false;
		for (int k = 0; k < 4; k++) if (inAt[i][k] >= 0) any = true;
		t.add ("vec4 o%u=vec4(0.0);\n", i);
		if (!any) { t.add ("vec4 r%u=vec4(0.0);\n", i); continue; }
		t.add ("vec4 r%u=vec4(", i);
		for (int k = 0; k < 4; k++)
		{
			if (inAt[i][k] >= 0) t.add ("i%d.%c", inAt[i][k] / 4, "xyzw"[inAt[i][k] & 3]); else t.add ("0.0");
			if (k < 3) t.add (",");
		}
		t.add (");\n");
	}
	for (u32 i = 16; i < 32; i++) t.add ("vec4 r%u=vec4(0.0);\n", i);
	t.add ("bool f0=false,f1=false;\n");
	bool gSeen[64];
	memset (gSeen, 0, sizeof gSeen);
	for (u32 n = 0; n < l.nOps && !g.over; n++)
	{
		const Op &o = l.ops[n];
		if (o.op == OP_GUARD)
		{
			static const char *PICK[4] = { "(%s||%s)", "(%s&&%s)", "%s", "%s" };
			char x[16], y[16], c[40];
			snprintf (x, sizeof x, (o.reg[2] >> 1 & 1) ? "f0" : "!f0");
			snprintf (y, sizeof y, (o.reg[2] & 1) ? "f1" : "!f1");
			if (o.reg[1] == 2) snprintf (c, sizeof c, "%s", x);
			else if (o.reg[1] == 3) snprintf (c, sizeof c, "%s", y);
			else snprintf (c, sizeof c, PICK[o.reg[1] & 1], x, y);
			t.add ("%sbool g%u=", gSeen[o.d & 63] ? "" : "", (unsigned) (o.d & 63));
			if (o.reg[0]) t.add ("g%u&&", (unsigned) o.reg[0]);
			t.add ("%s(%s);\n", o.shape ? "!" : "", c);
			gSeen[o.d & 63] = true;
			continue;
		}
		if (o.op == OP_FLAG) { t.add ("f%d=%s;\n", o.d & 1, o.reg[0] ? "true" : "false"); continue; }
		if (o.guard) t.add ("if(g%u)", (unsigned) o.guard);
		t.add ("{\n");
		if (o.op == OP_CMP)
		{
			static const char *C[6] = { "==", "!=", "<", "<=", ">", ">=" };
			t.add ("vec4 A="); g.operand (o, 0); t.add (";vec4 B="); g.operand (o, 1); t.add (";\n");
			const u32 cx = o.slot[2] >> 3 & 7, cy = o.slot[2] & 7;
			if (cx < 6) t.add ("f0=A.x%sB.x;\n", C[cx]); else t.add ("f0=true;\n");
			if (cy < 6) t.add ("f1=A.y%sB.y;\n", C[cy]); else t.add ("f1=true;\n");
			t.add ("}\n");
			continue;
		}
		t.add ("vec4 s=");
		if (o.op == OP_PUT) t.add ("c[%u]", g.slotOf (o.slot[0]) / 4);
		else if (o.shape == 2) { g.operand (o, 0); t.add ("*("); g.operand (o, 1); t.add (")+("); g.operand (o, 2); t.add (")"); }
		else switch (o.op)
		{
		case 0x00: t.add ("("); g.operand (o, 0); t.add (")+("); g.operand (o, 1); t.add (")"); break;
		case 0x08: t.add ("("); g.operand (o, 0); t.add (")*("); g.operand (o, 1); t.add (")"); break;
		case 0x0C: t.add ("max("); g.operand (o, 0); t.add (","); g.operand (o, 1); t.add (")"); break;
		case 0x0D: t.add ("min("); g.operand (o, 0); t.add (","); g.operand (o, 1); t.add (")"); break;
		case 0x0B: t.add ("floor("); g.operand (o, 0); t.add (")"); break;
		case 0x13: g.operand (o, 0); break;
		case 0x01: t.add ("vec4(dot(("); g.operand (o, 0); t.add (").xyz,("); g.operand (o, 1); t.add (").xyz))"); break;
		case 0x02: t.add ("vec4(dot("); g.operand (o, 0); t.add (","); g.operand (o, 1); t.add ("))"); break;
		case 0x03: case 0x18: t.add ("vec4(dot(("); g.operand (o, 0); t.add (").xyz,("); g.operand (o, 1); t.add (").xyz)+("); g.operand (o, 1); t.add (").w)"); break;
		case 0x05: t.add ("vec4(exp2(("); g.operand (o, 0); t.add (").x))"); break;
		case 0x06: t.add ("vec4(log2(("); g.operand (o, 0); t.add (").x))"); break;
		case 0x0E: t.add ("vec4(1.0/("); g.operand (o, 0); t.add (").x)"); break;
		case 0x0F: t.add ("vec4(1.0/sqrt(("); g.operand (o, 0); t.add (").x))"); break;
		case 0x09: case 0x1A: t.add ("vec4(greaterThanEqual("); g.operand (o, 0); t.add (","); g.operand (o, 1); t.add ("))"); break;
		case 0x0A: case 0x1B: t.add ("vec4(lessThan("); g.operand (o, 0); t.add (","); g.operand (o, 1); t.add ("))"); break;
		default: bad = true; t.add ("vec4(0.0)"); break;
		}
		t.add (";\n");
		char dst[8]; snprintf (dst, sizeof dst, "%c%u", o.dOut ? 'o' : 'r', (unsigned) (o.dOut ? o.d & 15 : o.d & 31));
		char mask[5]; int m = 0;
		for (int i = 0; i < 4; i++) if (o.writes & (8u >> i)) mask[m++] = "xyzw"[i];
		mask[m] = 0;
		if (m) t.add ("%s.%s=s.%s;\n", dst, mask, mask);
		t.add ("}\n");
	}
	if (lit) g.lighting (*lit);
	// la position, puis les varyings du fragment shader
	auto S = [&] (int c) { if (!pos || pos[c].reg == 0xFF) t.add (c == 3 ? "1.0" : "0.0"); else t.add ("o%u.%c", (unsigned) (pos[c].reg & 15), "xyzw"[pos[c].comp & 3]); };
	t.add ("float px="); S (0); t.add (",py="); S (1); t.add (",pz="); S (2); t.add (",pw="); S (3); t.add (";\n");
	t.add ("gl_Position=vec4(px*"); g.view (V_KX); t.add ("+pw*"); g.view (V_CX);
	t.add (",-(py*"); g.view (V_KY); t.add ("+pw*"); g.view (V_CY);
	t.add ("),pz*"); g.view (V_KZ); t.add ("+pw*"); g.view (V_CZ); t.add (",pw);\n");
	for (u32 i = 0; i < nOut; i++)
	{
		const Write &x = w[4 + i];
		t.add ("v%u.%c=", i / 4, "xyzw"[i & 3]);
		switch (x.kind)
		{
		case W_RAW: t.add ("o%u.%c", (unsigned) (x.reg & 15), "xyzw"[x.comp & 3]); break;
		case W_ONE_MINUS: t.add ("1.0-o%u.%c", (unsigned) (x.reg & 15), "xyzw"[x.comp & 3]); break;
		case W_ONE: t.add ("1.0"); break;
		case W_LITP: t.add (lit ? "lp.%c" : "0.0", "xyzw"[x.comp & 3]); break;
		case W_LITS: t.add (lit ? "ls.%c" : "0.0", "xyzw"[x.comp & 3]); break;
		default: t.add ("0.0"); break;
		}
		t.add (";\n");
	}
	t.add ("}\n");
	while (g.nUni & 3) { uni[g.nUni].kind = U_TINY; uni[g.nUni].comp = 0; uni[g.nUni].slot = 0; g.nUni++; }
	{ char num[8]; snprintf (num, sizeof num, "%4u", g.nUni / 4 ? g.nUni / 4 : 1); memcpy (t.p + cAt + 15, num, 4); }
	t.p[t.n] = 0;
	p.n = (t.n + 8) / 8;
	r.nIn = nIn; r.nUni = g.nUni;
	if (bad || g.over || t.n >= t.cap - 1) { r.ok = false; r.why = bad ? "une instruction inconnue" : "trop long"; return false; }
	return true;
}
}
