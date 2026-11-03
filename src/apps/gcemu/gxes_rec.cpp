//
// gxes_rec.cpp -- l'enregistreur du GX (gxes.h) : le meme calcul que Rec de gxv3d.h d'Onyx, les memes plans, mais
// des coordonnees pretes pour GL (EFB : ligne 0 en bas de l'espace de clip, comme gxgl.cpp), les copies d'EFB
// gardees dans le flux, le TEV entier dans la cle (indirect, brouillard, texture de z).
//
#include "gxes.h"
#include <math.h>
#if defined (__aarch64__)
#include <sched.h>
#else
#include <unistd.h>
#endif

namespace gxes
{
static inline u64 ticks ()
{
#if defined (__aarch64__)
	u64 t; asm volatile ("mrs %0, cntvct_el0" : "=r" (t)); return t;
#else
	return 0;
#endif
}
static inline float f (const gc::Machine &m, u32 a) { float r; memcpy (&r, &m.xfRegs[a], 4); return r; }
static void rgba (u32 c, float o[4]) { o[0] = (float) (c >> 24); o[1] = (float) ((c >> 16) & 255); o[2] = (float) ((c >> 8) & 255); o[3] = (float) (c & 255); }
static inline float dot3 (const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static inline void norm3 (float *v) { float l = sqrtf (dot3 (v, v)); if (l > 0) { v[0] /= l; v[1] /= l; v[2] /= l; } }
static inline int lightMask (int ctl) { return (ctl & 2) ? (((ctl >> 2) & 15) | ((ctl >> 7) & 0xF0)) : 0; }

bool Rec::init ()
{
	prog = new Prog[MAX_PROGS];
	tmp = new float[0x10000 * 40]; oc = new u8[0x10000]; eyes = new float[0x10000 * 4];
	for (int i = 0; i < 256; i++) inv255[i] = (float) i / 255.0f;
	pl.valid = false;
	for (int i = 0; i < NFRAMES; i++)
	{
		Frame &F = frame[i];
		F.v = new float[MAX_FLOATS]; F.b = new Batch[MAX_BATCHES]; F.u = new Uni[MAX_BATCHES]; F.c = new gc::GxCopy[MAX_COPIES];
		if (!F.v || !F.b || !F.u || !F.c) return false;
		F.reset ();
	}
	return prog && tmp && oc;
}

// ---- le programme d'une configuration : la cle ici, le shader au thread GL ----------------------------------------
static u64 cfgKey (const TevCfg &c)
{
	u64 h = 1469598103934665603ull;
	const u32 *w = (const u32 *) &c;
	for (size_t i = 0; i < sizeof c / 4; i++) { h ^= w[i]; h *= 1099511628211ull; }
	return h;
}

// ce que le fragment shader lit : les couleurs, les coordonnees, les cartes -> l'ordre des varyings
static void layout (Prog &p)
{
	const TevCfg &c = p.cfg;
	bool c0 = false, c1 = false;
	u32 coords = 0;
	int coordMap[8];
	bool raw[8];
	for (int i = 0; i < 8; i++) { coordMap[i] = -1; raw[i] = false; }
	p.mapMask = 0;
	for (u32 st = 0; st < c.nStages; st++)
	{
		u32 ce = c.cenv[st], ae = c.aenv[st], tr = c.tref[st];
		bool readsRas = false;
		for (int k = 0; k < 4; k++) { u32 s = (ce >> (k * 4)) & 15; if (s == 10 || s == 11) readsRas = true; }
		for (int k = 0; k < 4; k++) { u32 s = (ae >> (4 + k * 3)) & 7; if (s == 5) readsRas = true; }
		u32 chn = (tr >> 7) & 7;
		if (readsRas) { if (chn == 0) c0 = true; else if (chn == 1) c1 = true; }
		int coord = (tr >> 3) & 7, map = tr & 7;
		if (c.ind[st]) { coords |= 1u << coord; raw[coord] = true; }
		if (tr & 64)
		{
			coords |= 1u << coord;
			p.mapMask |= 1u << map;
			if (coordMap[coord] >= 0 && coordMap[coord] != map) raw[coord] = true;	// (deux cartes : pas de division commune)
			coordMap[coord] = map;
		}
	}
	for (u32 i = 0; i < c.nInd; i++)
	{
		int ir = (c.iref >> (6 * i)) & 63, map = ir & 7, coord = (ir >> 3) & 7;
		coords |= 1u << coord; raw[coord] = true; p.mapMask |= 1u << map;
	}
	int n = 0;
	p.c0At = p.c1At = -1;
	if (c0) { p.c0At = (gc::s8) n; for (int k = 0; k < 4; k++) { Vary &v = p.vary[n++]; v.kind = V_C0; v.a = (u8) k; v.b = 0; } }
	if (c1) { p.c1At = (gc::s8) n; for (int k = 0; k < 4; k++) { Vary &v = p.vary[n++]; v.kind = V_C1; v.a = (u8) k; v.b = 0; } }
	for (int i = 0; i < 8; i++)
	{
		p.normMap[i] = raw[i] ? -1 : (gc::s8) coordMap[i];
		if (!(coords & (1u << i))) continue;
		for (int k = 0; k < 2; k++) { Vary &v = p.vary[n++]; v.kind = V_ST; v.a = (u8) i; v.b = (u8) k; }
		if (c.projMask & (1 << i)) { Vary &v = p.vary[n++]; v.kind = V_Q; v.a = (u8) i; v.b = 0; }
	}
	p.nVary = n;
	p.chunks = (n + 3) / 4;
}

int Rec::newProg (const TevCfg &c, u64 k)
{
	int n = nProg;
	if (n >= MAX_PROGS) return -1;
	Prog &p = prog[n];
	memset (&p, 0, sizeof p);
	p.cfg = c; p.key = k;
	layout (p);
	__sync_synchronize ();
	nProg = n + 1;
	return n;
}
int Rec::addCfg (const TevCfg &c) { return newProg (c, cfgKey (c)); }
int Rec::findProg (const TevCfg &c)
{
	if (lastProg >= 0 && !memcmp (&lastCfg, &c, sizeof c)) return lastProg;
	u64 k = cfgKey (c);
	int pi = -1;
	for (int i = nProg - 1; i >= 0; i--)
		if (prog[i].key == k && !memcmp (&prog[i].cfg, &c, sizeof c)) { pi = i; break; }
	if (pi < 0) pi = newProg (c, k);
	if (pi >= 0) { lastCfg = c; lastProg = pi; }
	return pi;
}

// ---- le plan d'un etat ------------------------------------------------------------------------------------------
void Rec::planState (gc::Machine &m, const gc::GxState &s)
{
	(void) m;
	pl.valid = true; pl.serial = s.serial; pl.skip = false; pl.lastNb = ~0u;
	TevCfg c; memset (&c, 0, sizeof c);
	c.nStages = (u32) (s.gen[0] < 1 ? 1 : s.gen[0] > 16 ? 16 : s.gen[0]);
	c.nInd = (u32) s.gen[1] & 7;
	if (c.nInd > 4) c.nInd = 4;
	if (c.nInd) { c.iref = (u32) s.indRef[0]; c.iss0 = (u32) s.indRef[1]; c.iss1 = (u32) s.indRef[2]; }
	for (u32 st = 0; st < c.nStages; st++)
	{
		c.cenv[st] = (u32) s.tevC[st] & 0xFFFFFF; c.aenv[st] = (u32) s.tevA[st] & 0xFFFFFF;
		c.tref[st] = (u32) s.tref[st]; c.ksel[st] = (u32) s.ksel[st]; c.ind[st] = (u32) s.ind[st] & 0x1FFFFF;
	}
	for (int k = 0; k < 16; k++) c.swap[k] = (u8) (s.swap[k] & 3);
	c.alphaFunc[0] = (u8) (s.alpha[0] & 7); c.alphaFunc[1] = (u8) (s.alpha[1] & 7); c.alphaLogic = (u8) (s.gen[2] & 3);
	int fmt = (int) (s.peCtrl & 7);
	c.efbFmt = (u8) (fmt <= 2 ? fmt : 0);
	bool hasAlpha = fmt == 1;
	// le melange (gxgl.cpp) ; pas de double source en ES 3.0 : l'alpha de destination seulement si le melange ne lit pas celui du TEV
	u32 cm = s.cmode0;
	Batch &b = pl.b;
	b.kind = B_DRAW; b.blendOn = b.blendSub = 0; b.blendSrc = 1; b.blendDst = 0;
	bool readsSrcA = false;
	if (cm & 0x800) { b.blendOn = 1; b.blendSub = 1; b.blendSrc = 1; b.blendDst = 1; }
	else if (cm & 1)
	{
		int bs = (cm >> 8) & 7, bd = (cm >> 5) & 7;
		if (!hasAlpha)				// (l'EFB sans alpha : il se lit 1)
		{
			if (bs == 6) bs = 1; else if (bs == 7) bs = 0;
			if (bd == 6) bd = 1; else if (bd == 7) bd = 0;
		}
		readsSrcA = bs == 4 || bs == 5 || bd == 4 || bd == 5;
		b.blendOn = 1; b.blendSrc = (u8) bs; b.blendDst = (u8) bd;
	}
	c.dstAlpha = (s.zenv[3] & 0x100) && !readsSrcA;
	for (int i = 0; i < 8; i++) if (s.tcScale[i][2] != 0) c.projMask |= (u8) (1 << i);
	c.fogSel = (u8) (s.fogI[0] & 7); c.fogProj = (u8) (s.fogI[1] & 1); c.fogRange = c.fogSel && s.fogK[10] != 0;
	c.zop = (u8) (s.zenv[0] & 3); c.zfmt = (u8) (s.zenv[1] & 3);
	// le biais de LOD : pas d'etat pour lui en ES 3.0, le shader le passe a texture ()
	for (int k = 0; k < 8; k++) if ((s.texMode[k][0] >> 9) & 0xFF) c.biasMask |= (u8) (1 << k);
	int pi = findProg (c);
	if (pi < 0) { pl.skip = true; skipped++; return; }
	pl.pi = pi;
	const Prog &P = prog[pi];
	// ses textures : celles des cartes lues
	for (int k = 0; k < 8; k++)
	{
		b.tex[k] = -1; b.mode0[k] = s.texMode[k][0]; b.mode1[k] = s.texMode[k][1];
		if (P.mapMask & (1u << k))
		{
			b.tex[k] = s.tex[k];
			if (s.tex[k] < 0) { pl.skip = true; skipped++; return; }
		}
	}
	b.prog = pi; b.chunks = P.chunks;
	u32 zm = s.zmode;
	b.zon = zm & 1; b.zfunc = (zm >> 1) & 7; b.zwrite = b.zon && (zm & 0x10);
	if (b.zon && !b.zfunc) { pl.skip = true; return; }	// (jamais)
	b.cull = (u8) s.cull;
	if (s.cull == 3) { pl.skip = true; return; }
	b.cmask = (cm >> 3) & 1; b.amask = hasAlpha && ((cm >> 4) & 1);
	if (!b.cmask && !b.amask && !b.zwrite) { pl.skip = true; return; }
	for (int k = 0; k < 4; k++) b.scissor[k] = s.scissor[k];
	Uni &u = pl.u;
	memcpy (u.regs, s.regs, sizeof u.regs); memcpy (u.konst, s.konst, sizeof u.konst);
	u.alpha[0] = s.alpha[2]; u.alpha[1] = s.alpha[3]; u.alpha[2] = s.zenv[3] & 255; u.alpha[3] = s.zenv[2];
	memcpy (u.fogI, s.fogI, sizeof u.fogI); memcpy (u.fogF, s.fogF, sizeof u.fogF); memcpy (u.fogColor, s.fogColor, sizeof u.fogColor);
	memcpy (u.fogK, s.fogK, sizeof u.fogK); memcpy (u.indMtx, s.indMtx, sizeof u.indMtx);
	for (int k = 0; k < 8; k++)
	{
		float w = s.texSize[k][0], h = s.texSize[k][1];
		u.texInv[k][0] = w > 0 ? 1.0f / w : 0; u.texInv[k][1] = h > 0 ? 1.0f / h : 0; u.texInv[k][2] = (float) (gc::s8) ((s.texMode[k][0] >> 9) & 0xFF) / 32.f; u.texInv[k][3] = 0;
	}
	// l'etage des sommets : tout l'EFB en clip, x -1..1 = colonnes 0..640, y -1..1 = lignes 0..528
	const float *vpf = s.viewport;
	float vx0 = vpf[0], vy0 = vpf[1], vw = vpf[2], vh = vpf[3], d0 = vpf[4], d1 = vpf[5];
	pl.ax = 2 * vx0 / EFB_W - 1; pl.bx = vw / EFB_W;		// x' = ax W + bx (X + W)
	pl.ay = 2 * vy0 / EFB_H - 1; pl.by = vh / EFB_H;		// y' = ay W + by (Y + W)
	pl.az = 2 * d0 - 1; pl.bz = d1 - d0;				// z' = az W + bz (Z + W)
	bool hasN = (s.vtx[0] & gc::GX_VCD_NRM) != 0, h1 = (s.vtx[0] & gc::GX_VCD_C1) != 0;
	pl.hasN = hasN; pl.h0 = (s.vtx[0] & gc::GX_VCD_C0) != 0; pl.h1 = h1;
	rgba (s.matAmb[0], pl.m0); rgba (s.matAmb[1], pl.m1); rgba (s.matAmb[2], pl.a0); rgba (s.matAmb[3], pl.a1);
	pl.proj = s.proj[6] != 0;
	int tMax = -1;
	for (int k = 0; k < P.nVary; k++) if (P.vary[k].kind >= V_ST && P.vary[k].a > tMax) tMax = P.vary[k].a;
	if (tMax >= s.vtx[2]) tMax = s.vtx[2] - 1;
	pl.tMax = tMax;
	bool u0 = false, u0a = false, u1 = false, u1a = false;
	for (int k = 0; k < P.nVary; k++)
	{
		const Vary &vy = P.vary[k];
		if (vy.kind == V_C0) { if (vy.a < 3) u0 = true; else u0a = true; }
		else if (vy.kind == V_C1) { if (vy.a < 3) u1 = true; else u1a = true; }
	}
	for (int t = 0; t <= tMax; t++)
	{
		int type = (int) ((u32) s.texgen[t][0] >> 4) & 7;
		if (type == 2) u0 = true; else if (type == 3) u1 = true;
	}
	if (s.vtx[1] < 2 && !h1) { u0 = u0 || u1; u0a = u0a || u1a; }	// (COLOR1 depuis COLOR0)
	bool lit0 = s.vtx[1] != 0, lit1 = s.vtx[1] >= 2;
	bool same0 = s.chan[2] == s.chan[0], same1 = s.chan[3] == s.chan[1];
	int c0End = u0 ? (u0a && same0 ? 4 : 3) : 0, c1End = u1 ? (u1a && same1 ? 4 : 3) : 0;
	bool a0Own = lit0 && u0a && !(u0 && same0), a1Own = lit1 && u1a && !(u1 && same1);
	pl.lmask = (lit0 && u0 ? lightMask (s.chan[0]) : 0) | (a0Own ? lightMask (s.chan[2]) : 0)
		 | (lit1 && u1 ? lightMask (s.chan[1]) : 0) | (a1Own ? lightMask (s.chan[3]) : 0);
	pl.needN = hasN && pl.lmask != 0;
	pl.lit0 = lit0; pl.lit1 = lit1; pl.c0End = c0End; pl.c1End = c1End; pl.a0Own = a0Own; pl.a1Own = a1Own;
	pl.lit0c = lit0 && c0End; pl.lit1c = lit1 && c1End;
	pl.lit0L = pl.lit0c && (s.chan[0] & 2); pl.lit0aL = lit0 && a0Own && (s.chan[2] & 2);
	pl.lit1L = pl.lit1c && (s.chan[1] & 2); pl.lit1aL = lit1 && a1Own && (s.chan[3] & 2);
	pl.P0 = s.proj[0]; pl.P1 = s.proj[1]; pl.P2 = s.proj[2]; pl.P3 = s.proj[3]; pl.P4 = s.proj[4]; pl.P5 = s.proj[5];
	pl.VP0 = s.vp[0]; pl.VP1 = s.vp[1];
	pl.vtx1 = s.vtx[1]; pl.dual = s.vtx[3] != 0;
	for (int t = 0; t <= tMax; t++)
	{
		TgPlan &q = pl.tp[t];
		q.info = (u32) s.texgen[t][0]; q.srcSel = (int) (q.info >> 7) & 31; q.type = (int) (q.info >> 4) & 7;
		q.sr = (int) (q.info >> 12) & 7; q.three = (q.info & 2) != 0;
		u32 pinfo = (u32) s.texgen[t][1];
		q.pr = pinfo & 63; q.norm = (pinfo & 256) != 0;
	}
	pl.nVary = P.nVary;
	for (int k = 0; k < P.nVary; k++)
	{
		const Vary &vy = P.vary[k];
		VyPlan &q = pl.vp[k];
		if (vy.kind == V_C0) { q.kind = 0; q.a = vy.a; }
		else if (vy.kind == V_C1) { q.kind = 1; q.a = vy.a; }
		else if (vy.kind == V_Q) { q.kind = 2; q.coord = vy.a; }
		else
		{
			q.kind = 3; q.coord = vy.a; q.a = vy.b;
			q.scale = s.tcScale[vy.a][vy.b];
			int nm = P.normMap[vy.a];
			q.ts = nm >= 0 ? s.texSize[nm][vy.b] : 1.0f;
			q.inv = q.ts > 0 ? 1.0f / q.ts : 0;
		}
	}
}

void Rec::readLights (const gc::Machine &m, int mask)
{
	for (int L = 0; L < 8; L++)
	{
		if (!(mask & (1 << L))) continue;
		u32 b = 0x600 + (u32) L * 16;
		Light &l = lt[L];
		rgba (m.xfRegs[b + 3], l.col);
		for (int k = 0; k < 3; k++) { l.cosA[k] = f (m, b + 4 + k); l.distA[k] = f (m, b + 7 + k); l.pos[k] = f (m, b + 10 + k); l.dir[k] = f (m, b + 13 + k); }
		for (int k = 0; k < 3; k++) l.daN[k] = l.distA[k];
		if (dot3 (l.distA, l.distA) != 0) norm3 (l.daN);
	}
}
void Rec::chanPlan (ChanPlan &c, int ctl) const
{
	c.ctl = ctl; c.dfn = (ctl >> 7) & 3; c.atn = (ctl >> 9) & 3; c.matV = (ctl & 1) != 0; c.ambV = (ctl & 64) != 0;
	c.nL = 0;
	int mask = lightMask (ctl);
	for (int L = 0; L < 8; L++) if (mask & (1 << L)) c.L[c.nL++] = lt[L];
}
void Rec::chanEval (const ChanPlan &c, const float regMat[4], const float regAmb[4], const float vcol[4],
		    const float pos[3], const float n[3], float out[4], int c0, int c1)
{
	const float *mat = c.matV ? vcol : regMat;
	float lacc[4];
	for (int k = c0; k < c1; k++) lacc[k] = c.ambV ? vcol[k] : regAmb[k];
	int dfn = c.dfn, atn = c.atn;
	for (int j = 0; j < c.nL; j++)
	{
		const Light &li = c.L[j];
		const float *lcol = li.col, *cosA = li.cosA, *distA = li.distA, *lpos = li.pos, *ldir = li.dir;
		float ld[3] = { lpos[0] - pos[0], lpos[1] - pos[1], lpos[2] - pos[2] }, at = 1.0f;
		if (atn == 3)
		{
			float d2 = dot3 (ld, ld), d = sqrtf (d2);
			if (d > 0) { ld[0] /= d; ld[1] /= d; ld[2] /= d; }
			float cs = dot3 (ld, ldir); if (cs < 0) cs = 0;
			float den = distA[0] + distA[1] * d + distA[2] * d2;
			float num = cosA[0] + cosA[1] * cs + cosA[2] * cs * cs; if (num < 0) num = 0;
			at = den != 0 ? num / den : 0;
		}
		else if (atn == 1)
		{
			if (dot3 (ld, ld) > 0) norm3 (ld); else { ld[0] = n[0]; ld[1] = n[1]; ld[2] = n[2]; }
			float cs = 0;
			if (dot3 (n, ld) >= 0) { cs = dot3 (n, ldir); if (cs < 0) cs = 0; }
			const float *da = dfn == 0 ? distA : li.daN;
			float den = da[0] + da[1] * cs + da[2] * cs * cs;
			float num = cosA[0] + cosA[1] * cs + cosA[2] * cs * cs; if (num < 0) num = 0;
			at = den != 0 ? num / den : 0;
		}
		else { if (dot3 (ld, ld) > 0) norm3 (ld); else { ld[0] = n[0]; ld[1] = n[1]; ld[2] = n[2]; } }
		float df = dfn == 0 ? 1.0f : dfn == 1 ? dot3 (ld, n) : (dot3 (ld, n) > 0 ? dot3 (ld, n) : 0);
		for (int k = c0; k < c1; k++) { float x = at * df * lcol[k]; lacc[k] += roundf (x); }
	}
	for (int k = c0; k < c1; k++)
	{
		float l = lacc[k] < 0 ? 0 : lacc[k] > 255 ? 255 : lacc[k];
		out[k] = floorf (mat[k] * (l + floorf (l / 128.0f)) / 256.0f);
	}
}
void Rec::planXf (gc::Machine &m, const gc::GxState &s)
{
	pl.xfSerial = m.xfSerial;
	readLights (m, pl.lmask);
	if (pl.lit0c) chanPlan (pl.cp0, s.chan[0]);
	if (pl.lit0 && pl.a0Own) chanPlan (pl.cp0a, s.chan[2]);
	if (pl.lit1c) chanPlan (pl.cp1, s.chan[1]);
	if (pl.lit1 && pl.a1Own) chanPlan (pl.cp1a, s.chan[3]);
	for (int t = 0; t <= pl.tMax; t++)
	{
		TgPlan &q = pl.tp[t];
		for (int k = 0; k < 3; k++) for (int c = 0; c < 4; c++) q.post[k * 4 + c] = f (m, 0x500 + ((q.pr + (u32) k) & 63) * 4 + (u32) c);
	}
	pl.lastPm = pl.lastPn = ~0u;
	for (int t = 0; t < 8; t++) pl.lastTm[t] = ~0u;
}

#ifndef GX_ASM
// (gx_positions de gx_asm.asm, en C++ : la reference, et le PC)
static void positions (GxPos &g)
{
	const float *xf = (const float *) g.xf;
	u8 *oc = g.oc;
	float *o = g.out, *eye = g.eye;
	for (int i = 0; i < g.nv; i++, eye += 4, o = (float *) ((char *) o + g.stride))
	{
		const gc::GxVertex &v = g.vx[i];
		u32 pm = v.mtx[0];
		if (pm != g.lastPm)
		{
			g.lastPm = pm;
			for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) g.cols[c][r] = xf[((pm + (u32) r) & 63) * 4 + (u32) c];
		}
		for (int r = 0; r < 3; r++) eye[r] = g.cols[0][r] * v.pos[0] + g.cols[1][r] * v.pos[1] + g.cols[2][r] * v.pos[2] + g.cols[3][r];
		float X, Y, Z, W;
		if (g.proj) { X = g.P[0] * eye[0] + g.P[1]; Y = g.P[2] * eye[1] + g.P[3]; Z = g.P[4] * eye[2] + g.P[5]; W = 1; }
		else { X = g.P[0] * eye[0] + g.P[1] * eye[2]; Y = g.P[2] * eye[1] + g.P[3] * eye[2]; Z = g.P[4] * eye[2] + g.P[5]; W = -eye[2]; }
		X *= g.VP0; Y *= g.VP1; Z = 2 * Z + W;
		o[0] = g.ax * W + g.bx * (X + W); o[1] = g.ay * W + g.by * (Y + W); o[2] = g.az * W + g.bz * (Z + W); o[3] = W;
		oc[i] = (u8) ((o[2] + W >= 0 ? 0 : 1) | (W > 0 ? 0 : 2));	// (derriere le plan proche, derriere l'oeil ; NaN : dehors)
	}
}
#endif

// ---- un draw ----------------------------------------------------------------------------------------------------
void Rec::draw (gc::Machine &m, const gc::GxState &s, const gc::GxVertex *vx, int nv, const u32 *idx, int ni, int prim)
{
	u64 t0 = ticks ();
	if (ni < 1) return;
	Frame &F = cur ();
	bool fresh = !pl.valid || pl.serial != s.serial;
	if (fresh) planState (m, s);
	if (pl.skip) return;
	if (fresh || pl.xfSerial != m.xfSerial) planXf (m, s);
	const Prog &P = prog[pl.pi];
	if (F.nb >= MAX_BATCHES - MAX_COPIES) { dropped++; return; }
	Batch b = pl.b;
	int stride = 4 + P.chunks * 4;
	if (nv > 0x10000) nv = 0x10000;
	const float ax = pl.ax, bx = pl.bx, ay = pl.ay, by = pl.by, az = pl.az, bz = pl.bz;
	const bool hasN = pl.hasN, h0 = pl.h0, h1 = pl.h1, proj = pl.proj;
	float m0[4], m1[4], a0[4], a1[4];
	for (int k = 0; k < 4; k++) { m0[k] = pl.m0[k]; m1[k] = pl.m1[k]; a0[k] = pl.a0[k]; a1[k] = pl.a1[k]; }
	const int tMax = pl.tMax, c0End = pl.c0End, c1End = pl.c1End;
	const bool lit0 = pl.lit0, lit1 = pl.lit1, a0Own = pl.a0Own, a1Own = pl.a1Own, needN = pl.needN;
	const bool lit0c = pl.lit0c, lit1c = pl.lit1c, lit0L = pl.lit0L, lit0aL = pl.lit0aL, lit1L = pl.lit1L, lit1aL = pl.lit1aL;
	const ChanPlan &cp0 = pl.cp0, &cp0a = pl.cp0a, &cp1 = pl.cp1, &cp1a = pl.cp1a;
	const float P0 = pl.P0, P1 = pl.P1, P2 = pl.P2, P3 = pl.P3, P4 = pl.P4, P5 = pl.P5;
	const float VP0 = pl.VP0, VP1 = pl.VP1;
	const int vtx1 = pl.vtx1; const bool dual = pl.dual;
	const TgPlan *tp = pl.tp; const VyPlan *vp = pl.vp;
	const int nVary = pl.nVary;
	float Mn[9]; u32 lastPn = pl.lastPn;
	if (lastPn != ~0u) for (int k = 0; k < 9; k++) Mn[k] = pl.Mn[k];
	// les positions : l'oeil et le clip de chaque sommet, d'abord
	GxPos gp;
	gp.vx = vx; gp.xf = m.xfRegs; gp.out = tmp; gp.oc = oc; gp.eye = eyes; gp.nv = nv; gp.stride = stride * 4; gp.proj = proj; gp.lastPm = pl.lastPm;
	gp.ax = ax; gp.bx = bx; gp.ay = ay; gp.by = by; gp.az = az; gp.bz = bz;
	gp.P[0] = P0; gp.P[1] = P1; gp.P[2] = P2; gp.P[3] = P3; gp.P[4] = P4; gp.P[5] = P5; gp.VP0 = VP0; gp.VP1 = VP1;
	memcpy (gp.cols, pl.cols, sizeof gp.cols);
#ifdef GX_ASM
	gx_positions (&gp);
#else
	positions (gp);
#endif
	memcpy (pl.cols, gp.cols, sizeof gp.cols);
	pl.lastPm = gp.lastPm;
	float Mt[8][12]; u32 lastTm[8];
	for (int t = 0; t < 8; t++)
	{
		lastTm[t] = pl.lastTm[t];
		if (t <= tMax && lastTm[t] != ~0u) for (int k = 0; k < 12; k++) Mt[t][k] = pl.Mt[t][k];
	}
	float tg[8][3];
	for (int t = 0; t < 8; t++) { tg[t][0] = tg[t][1] = 0; tg[t][2] = 1; }
	for (int i = 0; i < nv; i++)
	{
		const gc::GxVertex &v = vx[i];
		float *o = tmp + (size_t) i * (size_t) stride;
		const float *eye = eyes + (size_t) i * 4;
		float n[3] = { 0, 0, 0 };
		if (needN)
		{
			u32 pm = v.mtx[0];
			if (pm != lastPn)
			{
				lastPn = pm;
				u32 nb = (pm & 31) * 3;
				for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) Mn[r * 3 + c] = f (m, 0x400 + ((nb + (u32) r * 3 + (u32) c) % 96));
			}
			for (int r = 0; r < 3; r++) n[r] = Mn[r * 3] * v.nrm[0] + Mn[r * 3 + 1] * v.nrm[1] + Mn[r * 3 + 2] * v.nrm[2];
			norm3 (n);
		}
		float raw0[4], raw1[4], v0[4], v1[4], col0[4] = { 0, 0, 0, 0 }, col1[4] = { 0, 0, 0, 0 };
		for (int k = 0; k < 4; k++) { raw0[k] = v.c0[k]; raw1[k] = v.c1[k]; v0[k] = h0 ? raw0[k] : 255; }
		for (int k = 0; k < 4; k++) v1[k] = h1 ? raw1[k] : v0[k];
		if (lit0c) { if (lit0L) chanEval (cp0, m0, a0, v0, eye, n, col0, 0, c0End); else for (int k = 0; k < c0End; k++) col0[k] = cp0.matV ? v0[k] : m0[k]; }
		if (lit0 && a0Own) { if (lit0aL) chanEval (cp0a, m0, a0, v0, eye, n, col0, 3, 4); else col0[3] = cp0a.matV ? v0[3] : m0[3]; }
		if (lit1c) { if (lit1L) chanEval (cp1, m1, a1, v1, eye, n, col1, 0, c1End); else for (int k = 0; k < c1End; k++) col1[k] = cp1.matV ? v1[k] : m1[k]; }
		if (lit1 && a1Own) { if (lit1aL) chanEval (cp1a, m1, a1, v1, eye, n, col1, 3, 4); else col1[3] = cp1a.matV ? v1[3] : m1[3]; }
		if (vtx1 == 0) for (int k = 0; k < 4; k++) col0[k] = h0 ? raw0[k] : 255;
		if (vtx1 < 2) for (int k = 0; k < 4; k++) col1[k] = h1 ? raw1[k] : col0[k];
		for (int t = 0; t <= tMax; t++)
		{
			const TgPlan &q = tp[t];
			float in[4] = { 0, 0, 1, 1 };
			if (q.srcSel == 0) { in[0] = v.pos[0]; in[1] = v.pos[1]; in[2] = v.pos[2]; }
			else if (q.srcSel == 1) { if (hasN) { in[0] = v.nrm[0]; in[1] = v.nrm[1]; in[2] = v.nrm[2]; } else in[2] = 0; }
			else if (q.srcSel >= 5 && q.srcSel <= 12) { in[0] = v.tc[q.srcSel - 5][0]; in[1] = v.tc[q.srcSel - 5][1]; }
			if (!(q.info & 4)) in[2] = 1;
			if (q.type == 0)
			{
				u32 r = v.mtx[1 + t];
				float *T = Mt[t];
				if (r != lastTm[t])
				{
					lastTm[t] = r;
					for (int k = 0; k < 3; k++) for (int c = 0; c < 4; c++) T[k * 4 + c] = f (m, ((r + (u32) k) & 63) * 4 + (u32) c);
				}
				for (int k = 0; k < 2; k++) tg[t][k] = T[k * 4] * in[0] + T[k * 4 + 1] * in[1] + T[k * 4 + 2] * in[2] + T[k * 4 + 3] * in[3];
				if (q.three) tg[t][2] = T[8] * in[0] + T[9] * in[1] + T[10] * in[2] + T[11] * in[3];
				else tg[t][2] = 1;
				if (dual)
				{
					float qv[3] = { tg[t][0], tg[t][1], tg[t][2] };
					if (q.norm && dot3 (qv, qv) > 0) norm3 (qv);
					for (int k = 0; k < 3; k++) tg[t][k] = q.post[k * 4] * qv[0] + q.post[k * 4 + 1] * qv[1] + q.post[k * 4 + 2] * qv[2] + q.post[k * 4 + 3];
				}
			}
			else if (q.type == 1)
			{
				if (q.sr >= t) { tg[t][0] = 0; tg[t][1] = 0; tg[t][2] = 1; }
				else { tg[t][0] = tg[q.sr][0]; tg[t][1] = tg[q.sr][1]; tg[t][2] = tg[q.sr][2]; }
			}
			else { const float *cc = q.type == 2 ? col0 : col1; tg[t][0] = cc[0] / 255.0f; tg[t][1] = cc[1] / 255.0f; tg[t][2] = 1; }
		}
		for (int k = 0; k < nVary; k++)
		{
			const VyPlan &q = vp[k];
			float x;
			if (q.kind == 0) x = inv255[(int) col0[q.a]];
			else if (q.kind == 1) x = inv255[(int) col1[q.a]];
			else if (q.kind == 2) x = tg[q.coord][2];
			else x = tg[q.coord][q.a] * q.scale * q.inv;
			o[4 + k] = x;
		}
		for (int k = nVary; k < stride - 4; k++) o[4 + k] = 0;
	}
	drawVerts = drawVerts + (u64) nv;
	pl.lastPn = lastPn;
	if (lastPn != ~0u) for (int k = 0; k < 9; k++) pl.Mn[k] = Mn[k];
	for (int t = 0; t <= tMax; t++)
	{
		pl.lastTm[t] = lastTm[t];
		if (lastTm[t] != ~0u) for (int k = 0; k < 12; k++) pl.Mt[t][k] = Mt[t][k];
	}
	// ses primitives : les sommets recopies en ordre, un tampon sans indices (Mesa n'a qu'un memcpy a faire)
	int per = prim == gc::GX_TRIANGLES ? 3 : prim == gc::GX_LINES ? 2 : 1;
	u32 aligned = (F.nf + (u32) stride - 1) / (u32) stride * (u32) stride;
	bool again = !fresh && pl.lastWr == wr && F.nb && pl.lastNb == F.nb - 1 && pl.lastPrim == prim;
	if (again)
	{
		Batch &l = F.b[F.nb - 1];
		if (l.kind != B_DRAW || (l.off + l.count) * (u32) stride != F.nf) again = false;
	}
	u32 start = again ? F.nf : aligned;
	if (start + (u32) ni * (u32) stride > MAX_FLOATS) { dropped++; return; }
	u32 np = (u32) ni / (u32) per, kept = 0;
	float *d = F.v + start;
#ifdef GX_ASM
	kept = gx_expand (d, tmp, idx, np, (u32) per, (u32) stride * 4, (u32) nv, oc);
#else
	for (u32 t = 0; t < np; t++)
	{
		u32 k[3];
		u8 all = 0xFF;
		for (int c = 0; c < per; c++)
		{
			k[c] = idx[t * per + c];
			if (k[c] >= (u32) nv) k[c] = 0;
			all &= oc[k[c]];
		}
		if (all) continue;
		for (int c = 0; c < per; c++)
		{
			const float *sv = tmp + (size_t) k[c] * (size_t) stride;
			for (int j = 0; j < stride; j++) d[j] = sv[j];
			d += stride;
		}
		kept++;
	}
#endif
	u32 cnt = kept * (u32) per;
	if (!cnt) return;
	F.nf = start + cnt * (u32) stride;
	if (again) { F.b[F.nb - 1].count += cnt; drawTicks = drawTicks + (ticks () - t0); return; }
	b.prim = prim == gc::GX_TRIANGLES ? P_TRI : prim == gc::GX_LINES ? P_LINE : P_POINT;
	b.lp = prim == gc::GX_LINES ? (float) (s.lpSize & 0xFF) / 6.f : (float) ((s.lpSize >> 8) & 0xFF) / 6.f;
	b.off = start / (u32) stride; b.count = cnt;
	// (le meme etat juste apres, par un autre chemin : un seul lot)
	if (F.nb)
	{
		Batch &l = F.b[F.nb - 1];
		if (l.kind == B_DRAW && l.prog == b.prog && l.prim == b.prim && l.off + l.count == b.off && l.lp == b.lp && !memcmp (&F.u[l.uni], &pl.u, sizeof pl.u)
		    && l.zon == b.zon && l.zfunc == b.zfunc && l.zwrite == b.zwrite && l.cull == b.cull && l.cmask == b.cmask && l.amask == b.amask
		    && l.blendOn == b.blendOn && l.blendSrc == b.blendSrc && l.blendDst == b.blendDst && l.blendSub == b.blendSub
		    && !memcmp (l.scissor, b.scissor, sizeof b.scissor) && !memcmp (l.tex, b.tex, sizeof b.tex) && !memcmp (l.mode0, b.mode0, sizeof b.mode0) && !memcmp (l.mode1, b.mode1, sizeof b.mode1))
		{
			l.count += cnt; pl.lastWr = wr; pl.lastNb = F.nb - 1; pl.lastPrim = prim;
			drawTicks = drawTicks + (ticks () - t0);
			return;
		}
	}
	b.uni = F.nu;
	F.u[F.nu++] = pl.u;
	F.b[F.nb++] = b;
	pl.lastWr = wr; pl.lastNb = F.nb - 1; pl.lastPrim = prim;
	drawTicks = drawTicks + (ticks () - t0);
}

// ---- une copie d'EFB : un ordre du flux ; vers le XFB, la fin de l'image -----------------------------------------
void Rec::copy (gc::Machine &, const gc::GxCopy &c)
{
	Frame &F = cur ();
	if (F.nc >= MAX_COPIES || F.nb >= MAX_BATCHES) { dropped++; return; }
	Batch &b = F.b[F.nb++];
	memset (&b, 0, sizeof b);
	b.kind = B_COPY; b.copy = (int) F.nc;
	F.c[F.nc++] = c;
	pl.lastNb = ~0u;
	if (!c.toXfb) return;
	// l'image est finie ; la suivante attend sa place : on ne saute jamais une image, ses copies peuvent servir longtemps
	__sync_synchronize ();
	u64 t0 = ticks ();
	while (wr - rd >= NFRAMES - 1 && !quit)
	{
#if defined (__aarch64__)
		sched_yield ();
#else
		usleep (100);
#endif
	}
	waitTicks = waitTicks + (ticks () - t0);
	frame[(wr + 1) % NFRAMES].reset ();
	__sync_synchronize ();
	wr = wr + 1;
}

} // namespace gxes
