//
// n3ds/n3ds_arm.cpp -- l'interprete de l'ARM11 : tout ARMv6K et VFPv2 en mode utilisateur, vecteurs courts du VFP
// compris. Il sert de repli au recompilateur (n3ds_jit.cpp) : ce que celui-ci ne traduit pas passe ici, une
// instruction a la fois. Les flottants se font avec les instructions de l'hote, FPCR reglee comme le FPSCR de la
// console : A64 et VFP arrondissent, mettent a zero et propagent de la meme facon.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds_arm.h"

namespace n3ds {

typedef u32 __attribute__ ((aligned (1), may_alias)) ua32;
typedef u16 __attribute__ ((aligned (1), may_alias)) ua16;
typedef u64 __attribute__ ((aligned (1), may_alias)) ua64;

enum : u32 { FN = 0x80000000u, FZ = 0x40000000u, FC = 0x20000000u, FV = 0x10000000u, FQ = 0x08000000u, FE = 0x200, FT = 0x20 };

// ---- la memoire : la table de pages, sinon Memory (une page absente peut revenir : Memory::missing)
#define ENDIAN_BIG (S->cpsr & FE)
static inline u32 rd32 (Arm11 *S, u32 a)
{
	u8 *p = S->pages[a >> PAGE_BITS];
	u32 v = p ? *(ua32 *) (p + (a & (PAGE_SIZE - 1))) : S->m->mem.r32 (a);
	return ENDIAN_BIG ? __builtin_bswap32 (v) : v;
}
static inline u32 rd16 (Arm11 *S, u32 a)
{
	u8 *p = S->pages[a >> PAGE_BITS];
	u32 v = p ? *(ua16 *) (p + (a & (PAGE_SIZE - 1))) : S->m->mem.r16 (a);
	return ENDIAN_BIG ? __builtin_bswap16 ((u16) v) : v;
}
static inline u32 rd8 (Arm11 *S, u32 a)
{
	u8 *p = S->pages[a >> PAGE_BITS];
	return p ? p[a & (PAGE_SIZE - 1)] : S->m->mem.r8 (a);
}
static inline u64 rd64 (Arm11 *S, u32 a)
{
	u64 lo = rd32 (S, a), hi = rd32 (S, a + 4);
	return lo | hi << 32;
}
static inline void wr32 (Arm11 *S, u32 a, u32 v)
{
	if (ENDIAN_BIG) v = __builtin_bswap32 (v);
	u8 *p = S->pages[a >> PAGE_BITS];
	if (p) *(ua32 *) (p + (a & (PAGE_SIZE - 1))) = v; else S->m->mem.w32 (a, v);
}
static inline void wr16 (Arm11 *S, u32 a, u32 v)
{
	if (ENDIAN_BIG) v = __builtin_bswap16 ((u16) v);
	u8 *p = S->pages[a >> PAGE_BITS];
	if (p) *(ua16 *) (p + (a & (PAGE_SIZE - 1))) = (u16) v; else S->m->mem.w16 (a, (u16) v);
}
static inline void wr8 (Arm11 *S, u32 a, u32 v)
{
	u8 *p = S->pages[a >> PAGE_BITS];
	if (p) p[a & (PAGE_SIZE - 1)] = (u8) v; else S->m->mem.w8 (a, (u8) v);
}

static inline u32 ror (u32 v, u32 n) { n &= 31; return n ? v >> n | v << (32 - n) : v; }
static inline bool condOk (u32 c, u32 f)
{
	const bool n = f >> 31, z = f >> 30 & 1, cy = f >> 29 & 1, v = f >> 28 & 1;
	switch (c & 15)
	{
	case 0: return z; case 1: return !z; case 2: return cy; case 3: return !cy; case 4: return n; case 5: return !n;
	case 6: return v; case 7: return !v; case 8: return cy && !z; case 9: return !cy || z; case 10: return n == v;
	case 11: return n != v; case 12: return !z && n == v; case 13: return z || n != v; default: return true;
	}
}

// ---- l'etat d'une instruction en cours : pcv, ce que PC lit
namespace {
struct Ex
{
	Arm11 *S; u32 pcv;
	u32 &cpsr;
	explicit Ex (Arm11 *s, u32 v) : S (s), pcv (v), cpsr (s->cpsr) {}
	u32 R (u32 n) const { return n == 15 ? pcv : S->r[n]; }
	void W (u32 n, u32 v) { if (n == 15) jump (v & (cpsr & FT ? ~1u : ~3u)); else S->r[n] = v; }
	void WX (u32 n, u32 v)					// une lecture vers PC change d'etat (ARMv5 et plus)
	{
		if (n != 15) { S->r[n] = v; return; }
		if (v & 1) { cpsr |= FT; jump (v & ~1u); } else { cpsr &= ~FT; jump (v & ~3u); }
	}
	void jump (u32 v) { S->r[15] = v; }
	void undefined (u32 op);
	void nz (u32 v) { cpsr = (cpsr & ~(FN | FZ)) | (v & FN) | (v ? 0 : FZ); }
	void setc (bool b) { cpsr = b ? cpsr | FC : cpsr & ~FC; }
	void setv (bool b) { cpsr = b ? cpsr | FV : cpsr & ~FV; }
	u32 carry () const { return cpsr >> 29 & 1; }
	u32 shiftImm (u32 op, bool &c);
	u32 shiftReg (u32 op, bool &c);
	void arm (u32 op);
	void dataProc (u32 op);
	void media (u32 op);
	void parallel (u32 op);
	void extraLoad (u32 op);
	void misc (u32 op);
	void block (u32 op);
	void ldst (u32 op);
	void exclusive (u32 op);
	void cp15 (u32 op);
	void vfp (u32 op);
	void vfpData (u32 op);
	void vfpXfer (u32 op);
	void thumb (u32 op);
	u32 sget (u32 n) const { return S->vfp[n]; }
	u64 dget (u32 n) const { return (u64) S->vfp[2 * n] | (u64) S->vfp[2 * n + 1] << 32; }
	void sset (u32 n, u32 v) { S->vfp[n] = v; }
	void dset (u32 n, u64 v) { S->vfp[2 * n] = (u32) v; S->vfp[2 * n + 1] = (u32) (v >> 32); }
};
}

void Ex::undefined (u32 op)
{
	S->m->fail ("undefined instruction at %08x (%08x)", (unsigned) (S->r[15] - (cpsr & FT ? 2 : 4)), (unsigned) op);
}

u32 Ex::shiftImm (u32 op, bool &c)
{
	u32 v = R (op & 15), amt = op >> 7 & 31;
	switch (op >> 5 & 3)
	{
	case 0: if (amt) { c = v >> (32 - amt) & 1; v <<= amt; } return v;
	case 1: if (amt) { c = v >> (amt - 1) & 1; return v >> amt; } c = v >> 31; return 0;
	case 2: if (amt) { c = (s32) v >> (amt - 1) & 1; return (u32) ((s32) v >> amt); } c = v >> 31; return (u32) ((s32) v >> 31);
	default:
		if (amt) { c = v >> (amt - 1) & 1; return ror (v, amt); }
		{ const u32 res = v >> 1 | carry () << 31; c = v & 1; return res; }
	}
}

u32 Ex::shiftReg (u32 op, bool &c)
{
	u32 v = R (op & 15), amt = R (op >> 8 & 15) & 0xFF;
	if (!amt) return v;
	switch (op >> 5 & 3)
	{
	case 0:
		if (amt < 32) { c = v >> (32 - amt) & 1; return v << amt; }
		c = amt == 32 ? v & 1 : 0; return 0;
	case 1:
		if (amt < 32) { c = v >> (amt - 1) & 1; return v >> amt; }
		c = amt == 32 ? v >> 31 : 0; return 0;
	case 2:
		if (amt < 32) { c = (s32) v >> (amt - 1) & 1; return (u32) ((s32) v >> amt); }
		c = v >> 31; return (u32) ((s32) v >> 31);
	default:
		amt &= 31;
		if (!amt) { c = v >> 31; return v; }
		c = v >> (amt - 1) & 1; return ror (v, amt);
	}
}

void Ex::dataProc (u32 op)
{
	const u32 opc = op >> 21 & 15, rn = op >> 16 & 15, rd = op >> 12 & 15;
	const bool S_ = op >> 20 & 1;
	bool c = carry ();
	u32 b;
	if (op & 0x02000000)
	{
		const u32 rot = (op >> 8 & 15) * 2;
		b = ror (op & 0xFF, rot);
		if (rot) c = b >> 31;
	}
	else if (op & 0x10) b = shiftReg (op, c);
	else b = shiftImm (op, c);
	const u32 a = R (rn);
	u32 res = 0;
	bool arith = false, cOut = c, vOut = false;
	switch (opc)
	{
	case 0: case 8: res = a & b; break;
	case 1: case 9: res = a ^ b; break;
	case 2: case 10: res = a - b; arith = true; cOut = a >= b; vOut = ((a ^ b) & (a ^ res)) >> 31; break;
	case 3: res = b - a; arith = true; cOut = b >= a; vOut = ((b ^ a) & (b ^ res)) >> 31; break;
	case 4: case 11: res = a + b; arith = true; cOut = res < a; vOut = (~(a ^ b) & (a ^ res)) >> 31; break;
	case 5: { const u64 t = (u64) a + b + carry (); res = (u32) t; arith = true; cOut = t >> 32; vOut = (~(a ^ b) & (a ^ res)) >> 31; break; }
	case 6: { const u32 nc = 1 - carry (); res = a - b - nc; arith = true; cOut = (u64) a >= (u64) b + nc; vOut = ((a ^ b) & (a ^ res)) >> 31; break; }
	case 7: { const u32 nc = 1 - carry (); res = b - a - nc; arith = true; cOut = (u64) b >= (u64) a + nc; vOut = ((b ^ a) & (b ^ res)) >> 31; break; }
	case 12: res = a | b; break;
	case 13: res = b; break;
	case 14: res = a & ~b; break;
	default: res = ~b; break;
	}
	const bool test = opc >= 8 && opc <= 11;
	if (S_ && !(rd == 15))
	{
		nz (res); setc (cOut);
		if (arith) setv (vOut);
	}
	if (!test) W (rd, res);
}

// ---- les operations paralleles (S / Q / SH / U / UQ / UH, ADD16 ASX SAX SUB16 ADD8 SUB8)
void Ex::parallel (u32 op)
{
	const u32 rn = op >> 16 & 15, rd = op >> 12 & 15, rm = op & 15, kind = op >> 20 & 7, what = op >> 5 & 7;
	const u32 a = R (rn), b = R (rm);
	const bool sgn = kind < 4, sat = (kind & 3) == 2, halve = (kind & 3) == 3;
	if (what == 5 || what == 6 || (kind & 3) == 0) { undefined (op); return; }
	u32 res = 0, ge = 0;
	auto lane = [&] (int bits, int shift, s32 x, s32 y, bool sub, int geBits)
	{
		s32 v = sub ? x - y : x + y;
		const u32 mask = (1u << bits) - 1;
		if (!sat && !halve)
		{
			const bool on = sgn ? v >= 0 : (sub ? v >= 0 : v > (s32) mask);
			if (on) ge |= (u32) geBits;
		}
		if (sat)
		{
			const s32 hi = sgn ? (1 << (bits - 1)) - 1 : (s32) mask, lo = sgn ? -(1 << (bits - 1)) : 0;
			if (v > hi) v = hi;
			if (v < lo) v = lo;
		}
		if (halve) v >>= 1;
		res |= ((u32) v & mask) << shift;
	};
	auto h = [&] (u32 v, int i) { return sgn ? (s32) (s16) (v >> (i * 16)) : (s32) (v >> (i * 16) & 0xFFFF); };
	auto y8 = [&] (u32 v, int i) { return sgn ? (s32) (s8) (v >> (i * 8)) : (s32) (v >> (i * 8) & 0xFF); };
	switch (what)
	{
	case 0: lane (16, 0, h (a, 0), h (b, 0), false, 3); lane (16, 16, h (a, 1), h (b, 1), false, 12); break;
	case 1: lane (16, 0, h (a, 0), h (b, 1), true, 3); lane (16, 16, h (a, 1), h (b, 0), false, 12); break;
	case 2: lane (16, 0, h (a, 0), h (b, 1), false, 3); lane (16, 16, h (a, 1), h (b, 0), true, 12); break;
	case 3: lane (16, 0, h (a, 0), h (b, 0), true, 3); lane (16, 16, h (a, 1), h (b, 1), true, 12); break;
	case 4: for (int i = 0; i < 4; i++) lane (8, i * 8, y8 (a, i), y8 (b, i), false, 1 << i); break;
	default: for (int i = 0; i < 4; i++) lane (8, i * 8, y8 (a, i), y8 (b, i), true, 1 << i); break;
	}
	if (!sat && !halve) cpsr = (cpsr & ~0x000F0000u) | ge << 16;
	W (rd, res);
}

static inline s32 satS (s64 v, int bits, bool &q)
{
	const s64 hi = (1ll << (bits - 1)) - 1, lo = -(1ll << (bits - 1));
	if (v > hi) { q = true; return (s32) hi; }
	if (v < lo) { q = true; return (s32) lo; }
	return (s32) v;
}
static inline u32 satU (s64 v, int bits, bool &q)
{
	const s64 hi = (1ll << bits) - 1;
	if (v > hi) { q = true; return (u32) hi; }
	if (v < 0) { q = true; return 0; }
	return (u32) v;
}

void Ex::media (u32 op)
{
	const u32 f = op >> 20 & 0x1F, rd = op >> 16 & 15, ra = op >> 12 & 15, rs = op >> 8 & 15, sub = op >> 5 & 7, rm = op & 15;
	if (f < 8) { parallel (op); return; }
	if (f < 0x10)
	{
		const u32 rn = rd, rdd = ra;
		if (sub == 3)								// SXTAB16 ... UXTAH
		{
			const u32 v = ror (R (rm), (op >> 10 & 3) * 8), base = rn == 15 ? 0 : R (rn);
			switch (f & 7)
			{
			case 0: { const u32 lo = (u32) (s32) (s8) v + base, hi = (u32) (s32) (s8) (v >> 16) + (base >> 16); W (rdd, (lo & 0xFFFF) | hi << 16); return; }
			case 2: W (rdd, (u32) (s32) (s8) v + base); return;
			case 3: W (rdd, (u32) (s32) (s16) v + base); return;
			case 4: { const u32 lo = (v & 0xFF) + base, hi = (v >> 16 & 0xFF) + (base >> 16); W (rdd, (lo & 0xFFFF) | hi << 16); return; }
			case 6: W (rdd, (v & 0xFF) + base); return;
			case 7: W (rdd, (v & 0xFFFF) + base); return;
			}
			undefined (op);
			return;
		}
		if (f == 0x08 && !(sub & 1))						// PKHBT, PKHTB
		{
			bool c = false;
			const u32 b = shiftImm (op, c);
			W (rdd, (sub & 2) ? (R (rn) & 0xFFFF0000u) | (b & 0xFFFF) : (R (rn) & 0xFFFF) | (b & 0xFFFF0000u));
			return;
		}
		if (f == 0x08 && sub == 5)						// SEL
		{
			const u32 ge = cpsr >> 16 & 15, a = R (rn), b = R (rm);
			u32 res = 0;
			for (int i = 0; i < 4; i++) res |= ((ge >> i & 1) ? a : b) & 0xFFu << (i * 8);
			W (rdd, res);
			return;
		}
		if ((f == 0x0A || f == 0x0E) && sub == 1)				// SSAT16, USAT16
		{
			const u32 v = R (rm);
			bool q = false;
			u32 lo, hi;
			if (f == 0x0A) { lo = (u32) satS ((s16) v, (int) rn + 1, q); hi = (u32) satS ((s16) (v >> 16), (int) rn + 1, q); }
			else { lo = satU ((s16) v, (int) rn, q); hi = satU ((s16) (v >> 16), (int) rn, q); }
			if (q) cpsr |= FQ;
			W (rdd, (lo & 0xFFFF) | hi << 16);
			return;
		}
		if ((f & 0x1A) == 0x0A && !(sub & 1))					// SSAT, USAT
		{
			const u32 sh = op >> 7 & 31, sat = op >> 16 & 31;
			s32 v = (s32) R (rm);
			if (op & 0x40) v >>= sh ? sh : 31; else v = (s32) ((u32) v << sh);
			bool q = false;
			const u32 res = (f & 4) ? satU (v, (int) sat, q) : (u32) satS (v, (int) sat + 1, q);
			if (q) cpsr |= FQ;
			W (rdd, res);
			return;
		}
		if (f == 0x0B && sub == 1) { W (rdd, __builtin_bswap32 (R (rm))); return; }	// REV
		if (f == 0x0B && sub == 5) { const u32 v = R (rm); W (rdd, (v >> 8 & 0x00FF00FF) | (v & 0x00FF00FF) << 8); return; }	// REV16
		if (f == 0x0F && sub == 5) { const u32 v = R (rm); W (rdd, (u32) (s32) (s16) ((v & 0xFF) << 8 | (v >> 8 & 0xFF))); return; }	// REVSH
		undefined (op);
		return;
	}
	if (f == 0x18 && sub == 0)							// USAD8, USADA8
	{
		const u32 a = R (rm), b = R (rs);
		u32 v = ra == 15 ? 0 : R (ra);
		for (int i = 0; i < 4; i++) { const s32 d = (s32) (a >> (i * 8) & 0xFF) - (s32) (b >> (i * 8) & 0xFF); v += (u32) (d < 0 ? -d : d); }
		W (rd, v);
		return;
	}
	if (f != 0x10 && f != 0x14 && f != 0x15) { undefined (op); return; }
	const u32 a = R (rm), b0 = R (rs), b = (sub & 1) ? ror (b0, 16) : b0;
	const s32 p1 = (s32) (s16) a * (s32) (s16) b, p2 = (s32) (s16) (a >> 16) * (s32) (s16) (b >> 16);
	const bool minus = sub & 2;
	if (f == 0x10)									// SMLAD, SMLSD, SMUAD, SMUSD
	{
		s64 v = minus ? (s64) p1 - p2 : (s64) p1 + p2;
		if (ra != 15) v += (s32) R (ra);
		if (v != (s32) v) cpsr |= FQ;
		W (rd, (u32) v);
		return;
	}
	if (f == 0x14)									// SMLALD, SMLSLD
	{
		const s64 v = (minus ? (s64) p1 - p2 : (s64) p1 + p2) + (s64) ((u64) R (rd) << 32 | R (ra));
		S->r[ra] = (u32) v; S->r[rd] = (u32) ((u64) v >> 32);
		return;
	}
	const s64 pr = (s64) (s32) a * (s32) b0;					// SMMUL, SMMLA, SMMLS
	s64 v;
	if ((sub & 6) == 6) v = (s64) ((u64) R (ra) << 32) - pr;
	else v = (ra != 15 ? (s64) ((u64) R (ra) << 32) : 0) + pr;
	if (sub & 1) v += 0x80000000ll;
	W (rd, (u32) ((u64) v >> 32));
}

void Ex::exclusive (u32 op)
{
	const u32 rn = op >> 16 & 15, rd = op >> 12 & 15, rm = op & 15, sz = op >> 21 & 3;
	const u32 a = R (rn);
	if (op & 0x00100000)								// LDREX ...
	{
		S->exclOn = 1; S->exclAddr = a & ~15u;
		switch (sz)
		{
		case 0: { const u32 v = rd32 (S, a); S->exclVal = v; W (rd, v); break; }
		case 1: { const u64 v = rd64 (S, a); S->exclVal = v; S->r[rd] = (u32) v; S->r[rd + 1] = (u32) (v >> 32); break; }
		case 2: { const u32 v = rd8 (S, a); S->exclVal = v; W (rd, v); break; }
		default: { const u32 v = rd16 (S, a); S->exclVal = v; W (rd, v); break; }
		}
		return;
	}
	bool ok = S->exclOn && S->exclAddr == (a & ~15u);
	if (ok)
		switch (sz)
		{
		case 0: ok = rd32 (S, a) == (u32) S->exclVal; if (ok) wr32 (S, a, R (rm)); break;
		case 1: ok = rd64 (S, a) == S->exclVal; if (ok) { wr32 (S, a, R (rm)); wr32 (S, a + 4, R (rm + 1)); } break;
		case 2: ok = rd8 (S, a) == (u8) S->exclVal; if (ok) wr8 (S, a, R (rm)); break;
		default: ok = rd16 (S, a) == (u16) S->exclVal; if (ok) wr16 (S, a, R (rm)); break;
		}
	S->exclOn = 0;
	W (rd, ok ? 0 : 1);
}

void Ex::extraLoad (u32 op)
{
	const bool P = op & 0x01000000, U = op & 0x00800000, W_ = op & 0x00200000, L = op & 0x00100000;
	const u32 rn = op >> 16 & 15, rd = op >> 12 & 15, sh = op >> 5 & 3;
	const u32 off = (op & 0x00400000) ? ((op >> 4 & 0xF0) | (op & 15)) : R (op & 15);
	const u32 base = R (rn), eff = U ? base + off : base - off, a = P ? eff : base;
	const bool wb = W_ || !P;
	if (L)
	{
		u32 v;
		if (sh == 1) v = rd16 (S, a);
		else if (sh == 2) v = (u32) (s32) (s8) rd8 (S, a);
		else v = (u32) (s32) (s16) rd16 (S, a);
		if (wb) W (rn, eff);
		WX (rd, v);
		return;
	}
	if (sh == 1) { const u32 v = R (rd); wr16 (S, a, v); if (wb) W (rn, eff); return; }
	if (sh == 2)								// LDRD
	{
		const u32 lo = rd32 (S, a), hi = rd32 (S, a + 4);
		if (wb) W (rn, eff);
		S->r[rd] = lo; W (rd + 1, hi);
		return;
	}
	wr32 (S, a, R (rd)); wr32 (S, a + 4, R (rd + 1));			// STRD
	if (wb) W (rn, eff);
}

void Ex::ldst (u32 op)
{
	const bool I = op & 0x02000000, P = op & 0x01000000, U = op & 0x00800000, B = op & 0x00400000, W_ = op & 0x00200000, L = op & 0x00100000;
	const u32 rn = op >> 16 & 15, rd = op >> 12 & 15;
	bool c = carry ();
	const u32 off = I ? shiftImm (op, c) : op & 0xFFF;
	const u32 base = R (rn), eff = U ? base + off : base - off, a = P ? eff : base;
	const bool wb = W_ || !P;
	if (L)
	{
		const u32 v = B ? rd8 (S, a) : rd32 (S, a);
		if (wb) W (rn, eff);
		WX (rd, v);
		return;
	}
	const u32 v = R (rd);
	if (B) wr8 (S, a, v); else wr32 (S, a, v);
	if (wb) W (rn, eff);
}

void Ex::block (u32 op)
{
	const bool P = op & 0x01000000, U = op & 0x00800000, W_ = op & 0x00200000, L = op & 0x00100000;
	const u32 rn = op >> 16 & 15, list = op & 0xFFFF;
	if (op & 0x00400000) { undefined (op); return; }			// (^ : pas en mode utilisateur)
	const u32 n = (u32) __builtin_popcount (list), base = R (rn);
	u32 a = U ? (P ? base + 4 : base) : (P ? base - 4 * n : base - 4 * n + 4);
	const u32 wbv = U ? base + 4 * n : base - 4 * n;
	if (W_ && !(L && (list >> rn & 1))) W (rn, wbv);
	for (u32 i = 0; i < 16; i++)
	{
		if (!(list >> i & 1)) continue;
		if (L) { const u32 v = rd32 (S, a); WX (i, v); }
		else wr32 (S, a, i == rn ? (W_ && i != (u32) __builtin_ctz (list) ? wbv : base) : R (i));
		a += 4;
	}
}

// (CP15 : les registres de thread ; les barrieres et le cache ne font rien)
void Ex::cp15 (u32 op)
{
	const u32 crn = op >> 16 & 15, rt = op >> 12 & 15, crm = op & 15, opc2 = op >> 5 & 7, opc1 = op >> 21 & 7;
	if (opc1) { undefined (op); return; }
	if (op & 0x00100000)
	{
		if (crn == 13 && crm == 0 && opc2 == 3) { W (rt, S->tls); return; }
		if (crn == 13 && crm == 0 && opc2 == 2) { W (rt, S->urw); return; }
		undefined (op);
		return;
	}
	if (crn == 13 && crm == 0 && opc2 == 2) { S->urw = R (rt); return; }
	if (crn == 7) return;
	undefined (op);
}

// ---- le VFP ------------------------------------------------------------------------------------------------------
static inline void fpcrSet (u64 v) { __asm__ volatile ("msr fpcr, %0" : : "r" (v)); }
static inline u64 fpcrGet () { u64 v; __asm__ volatile ("mrs %0, fpcr" : "=r" (v)); return v; }
static inline u64 fpsrGet () { u64 v; __asm__ volatile ("mrs %0, fpsr" : "=r" (v)); return v; }
static inline void fpsrSet (u64 v) { __asm__ volatile ("msr fpsr, %0" : : "r" (v)); }
namespace { struct FpEnv
{
	Arm11 *S; u64 cr, sr;
	explicit FpEnv (Arm11 *s) : S (s) { cr = fpcrGet (); sr = fpsrGet (); fpcrSet (s->fpscr & 0x07C00000u); fpsrSet (0); }
	~FpEnv () { S->fpscr |= (u32) fpsrGet () & 0x9F; fpcrSet (cr); fpsrSet (sr); }
}; }

#define F2(ins, d, a) __asm__ volatile (ins " %s0, %s1" : "=w" (d) : "w" (a))
#define F3(ins, d, a, b) __asm__ volatile (ins " %s0, %s1, %s2" : "=w" (d) : "w" (a), "w" (b))
#define D2(ins, d, a) __asm__ volatile (ins " %d0, %d1" : "=w" (d) : "w" (a))
#define D3(ins, d, a, b) __asm__ volatile (ins " %d0, %d1, %d2" : "=w" (d) : "w" (a), "w" (b))
static inline float asF (u32 v) { float f; memcpy (&f, &v, 4); return f; }
static inline u32 asU (float f) { u32 v; memcpy (&v, &f, 4); return v; }
static inline double asD (u64 v) { double f; memcpy (&f, &v, 8); return f; }
static inline u64 asU (double f) { u64 v; memcpy (&v, &f, 8); return v; }

static u32 sop (u32 which, u32 d, u32 n, u32 m)
{
	float a = asF (n), b = asF (m), c = asF (d), p, r;
	switch (which)
	{
	case 0: F3 ("fmul", p, a, b); F3 ("fadd", r, c, p); return asU (r);	// VMLA
	case 1: F3 ("fmul", p, a, b); F2 ("fneg", p, p); F3 ("fadd", r, c, p); return asU (r);	// VMLS
	case 2: F3 ("fmul", p, a, b); F2 ("fneg", c, c); F3 ("fadd", r, c, p); return asU (r);	// VNMLS
	case 3: F3 ("fmul", p, a, b); F2 ("fneg", p, p); F2 ("fneg", c, c); F3 ("fadd", r, c, p); return asU (r);	// VNMLA
	case 4: F3 ("fmul", r, a, b); return asU (r);
	case 5: F3 ("fmul", r, a, b); F2 ("fneg", r, r); return asU (r);	// VNMUL
	case 6: F3 ("fadd", r, a, b); return asU (r);
	case 7: F3 ("fsub", r, a, b); return asU (r);
	default: F3 ("fdiv", r, a, b); return asU (r);
	}
}
static u64 dop (u32 which, u64 d, u64 n, u64 m)
{
	double a = asD (n), b = asD (m), c = asD (d), p, r;
	switch (which)
	{
	case 0: D3 ("fmul", p, a, b); D3 ("fadd", r, c, p); return asU (r);
	case 1: D3 ("fmul", p, a, b); D2 ("fneg", p, p); D3 ("fadd", r, c, p); return asU (r);
	case 2: D3 ("fmul", p, a, b); D2 ("fneg", c, c); D3 ("fadd", r, c, p); return asU (r);
	case 3: D3 ("fmul", p, a, b); D2 ("fneg", p, p); D2 ("fneg", c, c); D3 ("fadd", r, c, p); return asU (r);
	case 4: D3 ("fmul", r, a, b); return asU (r);
	case 5: D3 ("fmul", r, a, b); D2 ("fneg", r, r); return asU (r);
	case 6: D3 ("fadd", r, a, b); return asU (r);
	case 7: D3 ("fsub", r, a, b); return asU (r);
	default: D3 ("fdiv", r, a, b); return asU (r);
	}
}

// Les vecteurs courts : le prochain registre d'un vecteur, dans sa banque de 8 (singles) ou 4 (doubles).
static inline u32 vecNext (u32 r, u32 bank, u32 stride) { const u32 first = r & ~(bank - 1); return first + ((r - first + stride) & (bank - 1)); }

void Ex::vfpData (u32 op)
{
	const bool dbl = op & 0x100;
	const u32 D = op >> 22 & 1, N = op >> 7 & 1, M = op >> 5 & 1, opc1 = op >> 20 & 0xB, opc3 = op >> 6 & 1;
	const u32 rd4 = op >> 12 & 15, rn4 = op >> 16 & 15, rm4 = op & 15;
	u32 vd = dbl ? rd4 : rd4 * 2 + D, vn = dbl ? rn4 : rn4 * 2 + N, vm = dbl ? rm4 : rm4 * 2 + M;
	const u32 len = (S->fpscr >> 16 & 7) + 1, stride = (S->fpscr >> 20 & 3) == 3 ? 2 : 1;
	const u32 bank = dbl ? 4 : 8;
	const bool scalarD = vd < bank, scalarM = vm < bank;
	const u32 n = scalarD ? 1 : len;
	FpEnv env (S);
	if (opc1 != 0xB)
	{
		static const signed char WHICH[0x12] = { 0, 1, 2, 3, 4, 5, 6, 7, -1, -1, -1, -1, -1, -1, -1, -1, 8, -1 };
		const u32 k = opc1 << 1 | opc3;
		const int which = k < 0x12 ? WHICH[k] : -1;
		if (which < 0) { undefined (op); return; }
		for (u32 i = 0; i < n; i++)
		{
			if (dbl) dset (vd, dop ((u32) which, dget (vd), dget (vn), dget (vm)));
			else sset (vd, sop ((u32) which, sget (vd), sget (vn), sget (vm)));
			vd = vecNext (vd, bank, stride); vn = vecNext (vn, bank, stride);
			if (!scalarM) vm = vecNext (vm, bank, stride);
		}
		return;
	}
	if (!opc3) { undefined (op); return; }
	switch (rn4)
	{
	case 0: case 1:								// VMOV, VABS, VNEG, VSQRT
	{
		const u32 kind = rn4 << 1 | N;
		for (u32 i = 0; i < n; i++)
		{
			if (dbl)
			{
				const u64 v = dget (vm); u64 o = v;
				if (kind == 1) o = v & ~(1ull << 63);
				else if (kind == 2) o = v ^ 1ull << 63;
				else if (kind == 3) { double a = asD (v), q; D2 ("fsqrt", q, a); o = asU (q); }
				dset (vd, o);
			}
			else
			{
				const u32 v = sget (vm); u32 o = v;
				if (kind == 1) o = v & 0x7FFFFFFF;
				else if (kind == 2) o = v ^ 0x80000000u;
				else if (kind == 3) { float a = asF (v), q; F2 ("fsqrt", q, a); o = asU (q); }
				sset (vd, o);
			}
			vd = vecNext (vd, bank, stride);
			if (!scalarM) vm = vecNext (vm, bank, stride);
		}
		return;
	}
	case 4: case 5:								// VCMP, VCMPE (5 : contre zero)
	{
		u64 nzcv, keep;
		__asm__ volatile ("mrs %0, nzcv" : "=r" (keep));
		if (dbl)
		{
			const double a = asD (dget (vd)), b = rn4 == 5 ? 0.0 : asD (dget (vm));
			if (N) __asm__ volatile ("fcmpe %d0, %d1" : : "w" (a), "w" (b) : "cc"); else __asm__ volatile ("fcmp %d0, %d1" : : "w" (a), "w" (b) : "cc");
		}
		else
		{
			const float a = asF (sget (vd)), b = rn4 == 5 ? 0.0f : asF (sget (vm));
			if (N) __asm__ volatile ("fcmpe %s0, %s1" : : "w" (a), "w" (b) : "cc"); else __asm__ volatile ("fcmp %s0, %s1" : : "w" (a), "w" (b) : "cc");
		}
		__asm__ volatile ("mrs %0, nzcv" : "=r" (nzcv));
		__asm__ volatile ("msr nzcv, %0" : : "r" (keep) : "cc");
		S->fpscr = (S->fpscr & 0x0FFFFFFF) | (u32) nzcv;
		return;
	}
	case 7:									// VCVT double <-> simple
		if (!N) break;
		if (dbl) { const double a = asD (dget (vm)); float o; __asm__ volatile ("fcvt %s0, %d1" : "=w" (o) : "w" (a)); sset (rd4 * 2 + D, asU (o)); }
		else { const float a = asF (sget (vm)); double o; __asm__ volatile ("fcvt %d0, %s1" : "=w" (o) : "w" (a)); dset (rd4, asU (o)); }
		return;
	case 8:									// VCVT entier -> flottant
	{
		const u32 v = sget (rm4 * 2 + M);
		if (dbl) { double o; if (N) __asm__ volatile ("scvtf %d0, %w1" : "=w" (o) : "r" (v)); else __asm__ volatile ("ucvtf %d0, %w1" : "=w" (o) : "r" (v)); dset (vd, asU (o)); }
		else { float o; if (N) __asm__ volatile ("scvtf %s0, %w1" : "=w" (o) : "r" (v)); else __asm__ volatile ("ucvtf %s0, %w1" : "=w" (o) : "r" (v)); sset (vd, asU (o)); }
		return;
	}
	case 12: case 13:							// VCVT flottant -> entier ; N : vers zero, sinon l'arrondi du FPSCR
	{
		const bool sgn = rn4 == 13;
		const u32 mode = N ? 3 : S->fpscr >> 22 & 3;
		const u32 di = rd4 * 2 + D;
		u32 o = 0;
		#define CV(ins, reg) __asm__ volatile (ins " %w0, %" reg "1" : "=r" (o) : "w" (a))
		if (dbl)
		{
			const double a = asD (dget (vm));
			switch (mode) { case 0: if (sgn) CV ("fcvtns", "d"); else CV ("fcvtnu", "d"); break; case 1: if (sgn) CV ("fcvtps", "d"); else CV ("fcvtpu", "d"); break;
			case 2: if (sgn) CV ("fcvtms", "d"); else CV ("fcvtmu", "d"); break; default: if (sgn) CV ("fcvtzs", "d"); else CV ("fcvtzu", "d"); break; }
		}
		else
		{
			const float a = asF (sget (vm));
			switch (mode) { case 0: if (sgn) CV ("fcvtns", "s"); else CV ("fcvtnu", "s"); break; case 1: if (sgn) CV ("fcvtps", "s"); else CV ("fcvtpu", "s"); break;
			case 2: if (sgn) CV ("fcvtms", "s"); else CV ("fcvtmu", "s"); break; default: if (sgn) CV ("fcvtzs", "s"); else CV ("fcvtzu", "s"); break; }
		}
		#undef CV
		sset (di, o);
		return;
	}
	}
	undefined (op);
}

// VMOV, VMRS, VMSR, VLDR, VSTR, VLDM, VSTM
void Ex::vfpXfer (u32 op)
{
	const u32 cp = op >> 8 & 15;
	if ((op & 0x0F000010) == 0x0E000010)					// MCR / MRC p10, p11
	{
		const u32 rt = op >> 12 & 15, L = op >> 20 & 1, opc1 = op >> 21 & 7, n = (op >> 16 & 15) * 2 + (op >> 7 & 1);
		if (cp == 10 && opc1 == 0) { if (L) W (rt, sget (n)); else sset (n, R (rt)); return; }
		if (cp == 10 && opc1 == 7)
		{
			const u32 reg = op >> 16 & 15;
			if (L)
			{
				if (reg == 1)
				{
					if (rt == 15) cpsr = (cpsr & 0x0FFFFFFF) | (S->fpscr & 0xF0000000);
					else W (rt, S->fpscr);
				}
				else if (reg == 0) W (rt, 0x41012034);		// FPSID : une VFPv2 (ARM11 MPCore)
				else if (reg == 8) W (rt, 0);
				else undefined (op);
			}
			else if (reg == 1) S->fpscr = R (rt) & 0xF7F79F9F;
			else if (reg != 8) undefined (op);
			return;
		}
		if (cp == 11 && !(opc1 & 6))					// VMOV Dn[x]
		{
			const u32 d = (op >> 16 & 15);
			if (L) W (rt, S->vfp[d * 2 + (opc1 & 1)]); else S->vfp[d * 2 + (opc1 & 1)] = R (rt);
			return;
		}
		undefined (op);
		return;
	}
	if ((op & 0x0FE00000) == 0x0C400000)					// VMOV a deux registres
	{
		const u32 rt = op >> 12 & 15, rt2 = op >> 16 & 15, L = op >> 20 & 1, M = op >> 5 & 1;
		if (cp == 10)
		{
			const u32 sm = (op & 15) * 2 + M;
			if (L) { W (rt, sget (sm)); W (rt2, sget (sm + 1)); } else { sset (sm, R (rt)); sset (sm + 1, R (rt2)); }
		}
		else
		{
			const u32 dm = op & 15;
			if (L) { W (rt, S->vfp[dm * 2]); W (rt2, S->vfp[dm * 2 + 1]); } else { S->vfp[dm * 2] = R (rt); S->vfp[dm * 2 + 1] = R (rt2); }
		}
		return;
	}
	const bool P = op & 0x01000000, U = op & 0x00800000, D = op & 0x00400000, W_ = op & 0x00200000, L = op & 0x00100000;
	const u32 rn = op >> 16 & 15, vd = op >> 12 & 15, imm = op & 0xFF;
	if (P && !W_)								// VLDR, VSTR
	{
		const u32 base = rn == 15 ? pcv & ~3u : R (rn), a = U ? base + imm * 4 : base - imm * 4;
		if (cp == 10)
		{
			const u32 sd = vd * 2 + D;
			if (L) sset (sd, rd32 (S, a)); else wr32 (S, a, sget (sd));
		}
		else
		{
			if (L) { const u32 lo = rd32 (S, a), hi = rd32 (S, a + 4); S->vfp[vd * 2] = lo; S->vfp[vd * 2 + 1] = hi; }
			else { wr32 (S, a, S->vfp[vd * 2]); wr32 (S, a + 4, S->vfp[vd * 2 + 1]); }
		}
		return;
	}
	// VLDM, VSTM (FLDMX compris : imm impair, un mot de plus)
	const u32 base = R (rn);
	u32 a = P ? base - imm * 4 : base;
	if (W_) W (rn, U ? base + imm * 4 : base - imm * 4);
	if (cp == 10)
	{
		u32 sd = vd * 2 + D;
		for (u32 i = 0; i < imm; i++) { if (L) sset (sd + i & 31, rd32 (S, a)); else wr32 (S, a, sget (sd + i & 31)); a += 4; }
	}
	else
	{
		for (u32 i = 0; i < imm / 2; i++)
		{
			const u32 k = (vd + i) & 15;
			if (L) { S->vfp[k * 2] = rd32 (S, a); S->vfp[k * 2 + 1] = rd32 (S, a + 4); }
			else { wr32 (S, a, S->vfp[k * 2]); wr32 (S, a + 4, S->vfp[k * 2 + 1]); }
			a += 8;
		}
	}
}

void Ex::vfp (u32 op)
{
	if ((op & 0x0F000010) == 0x0E000000) vfpData (op);
	else vfpXfer (op);
}

// ---- le reste du decodage ARM -----------------------------------------------------------------------------------------
void Ex::misc (u32 op)
{
	const u32 rd = op >> 12 & 15, rm = op & 15, rn = op >> 16 & 15, rs = op >> 8 & 15;
	if ((op & 0x0FBF0FFF) == 0x010F0000) { W (rd, (op & 0x00400000) ? 0 : (cpsr & 0xF80F0220) | 0x10); return; }	// MRS
	if ((op & 0x0FB0FFF0) == 0x0120F000 || (op & 0x0FB0F000) == 0x0320F000)		// MSR
	{
		if (op & 0x00400000) return;
		u32 v;
		if (op & 0x02000000) v = ror (op & 0xFF, (op >> 8 & 15) * 2); else v = R (rm);
		u32 mask = 0;
		if (op & 0x00080000) mask |= 0xF8000000;
		if (op & 0x00040000) mask |= 0x000F0000;
		if (op & 0x00020000) mask |= 0x00000200;
		cpsr = (cpsr & ~mask) | (v & mask);
		return;
	}
	if ((op & 0x0FFFFFF0) == 0x012FFF10) { const u32 v = R (rm); if (v & 1) { cpsr |= FT; jump (v & ~1u); } else { cpsr &= ~FT; jump (v & ~3u); } return; }	// BX
	if ((op & 0x0FFFFFF0) == 0x012FFF30)							// BLX
	{
		const u32 v = R (rm);
		S->r[14] = S->r[15] | (cpsr & FT ? 1 : 0);
		if (v & 1) { cpsr |= FT; jump (v & ~1u); } else { cpsr &= ~FT; jump (v & ~3u); }
		return;
	}
	if ((op & 0x0FFF0FF0) == 0x012FFF20) { undefined (op); return; }			// BXJ
	if ((op & 0x0FFF0FF0) == 0x016F0F10) { const u32 v = R (rm); W (rd, v ? (u32) __builtin_clz (v) : 32); return; }	// CLZ
	if ((op & 0x0F900FF0) == 0x01000050)							// QADD, QSUB, QDADD, QDSUB
	{
		bool q = false;
		s32 a = (s32) R (rm), b = (s32) R (rn);
		if (op & 0x00400000) b = satS ((s64) b * 2, 32, q);
		const s32 res = satS ((op & 0x00200000) ? (s64) a - b : (s64) a + b, 32, q);
		if (q) cpsr |= FQ;
		W (rd, (u32) res);
		return;
	}
	if ((op & 0x0FF000F0) == 0x01200070) { S->m->fail ("breakpoint at %08x", (unsigned) (S->r[15] - 4)); return; }	// BKPT
	if ((op & 0x0F900090) == 0x01000080)							// les multiplications 16 x 16
	{
		const u32 dst = op >> 16 & 15, acc = op >> 12 & 15;
		const s32 a = (op & 0x20) ? (s32) R (rm) >> 16 : (s32) (s16) R (rm);
		const s32 b = (op & 0x40) ? (s32) R (rs) >> 16 : (s32) (s16) R (rs);
		switch (op >> 21 & 3)
		{
		case 0:
		{
			const s64 res = (s64) (a * b) + (s32) R (acc);
			if (res != (s32) res) cpsr |= FQ;
			W (dst, (u32) res);
			break;
		}
		case 1:
		{
			const s32 res = (s32) (((s64) (s32) R (rm) * b) >> 16);
			if (op & 0x20) W (dst, (u32) res);
			else { const s64 t = (s64) res + (s32) R (acc); if (t != (s32) t) cpsr |= FQ; W (dst, (u32) t); }
			break;
		}
		case 2:
		{
			const s64 res = (s64) ((u64) R (dst) << 32 | R (acc)) + (s64) (a * b);
			S->r[acc] = (u32) res; S->r[dst] = (u32) ((u64) res >> 32);
			break;
		}
		default: W (dst, (u32) (a * b)); break;
		}
		return;
	}
	undefined (op);
}

void Ex::arm (u32 op)
{
	const u32 cond = op >> 28;
	if (cond == 15)
	{
		if ((op & 0xFE000000) == 0xFA000000)						// BLX immediat
		{
			S->r[14] = S->r[15];
			cpsr |= FT;
			jump (pcv + (u32) ((s32) (op << 8) >> 6) + (op >> 23 & 2));
			return;
		}
		if ((op & 0xFD70F000) == 0xF550F000) return;					// PLD
		if ((op & 0xFFF1FE20) == 0xF1000000) return;					// CPS : rien en mode utilisateur
		if ((op & 0xFFFFFDFF) == 0xF1010000) { if (op & 0x200) cpsr |= FE; else cpsr &= ~FE; return; }	// SETEND
		if (op == 0xF57FF01F) { S->exclOn = 0; return; }				// CLREX
		undefined (op);
		return;
	}
	switch (op >> 25 & 7)
	{
	case 0: case 1:
		if ((op & 0x0E000090) == 0x00000090)
		{
			if (!(op & 0x60))
			{
				const u32 hi = op >> 16 & 15, lo = op >> 12 & 15, rs = op >> 8 & 15, rm = op & 15;
				if ((op & 0x0FC00000) == 0x00000000)				// MUL, MLA
				{
					u32 res = R (rm) * R (rs);
					if (op & 0x00200000) res += R (lo);
					if (op & 0x00100000) nz (res);
					W (hi, res);
					return;
				}
				if ((op & 0x0FF000F0) == 0x00400090)				// UMAAL
				{
					const u64 res = (u64) R (rm) * R (rs) + R (hi) + R (lo);
					S->r[lo] = (u32) res; S->r[hi] = (u32) (res >> 32);
					return;
				}
				if ((op & 0x0F800000) == 0x00800000)				// UMULL ... SMLAL
				{
					u64 res = (op & 0x00400000) ? (u64) ((s64) (s32) R (rm) * (s64) (s32) R (rs)) : (u64) R (rm) * R (rs);
					if (op & 0x00200000) res += (u64) R (hi) << 32 | R (lo);
					if (op & 0x00100000) cpsr = (cpsr & ~(FN | FZ)) | ((u32) (res >> 32) & FN) | (res ? 0 : FZ);
					S->r[lo] = (u32) res; S->r[hi] = (u32) (res >> 32);
					return;
				}
				if ((op & 0x0FB00FF0) == 0x01000090)				// SWP, SWPB
				{
					const u32 a = R (hi);
					if (op & 0x00400000) { const u32 v = rd8 (S, a); wr8 (S, a, R (rm)); W (lo, v); }
					else { const u32 v = rd32 (S, a); wr32 (S, a, R (rm)); W (lo, v); }
					return;
				}
				if ((op & 0x0F8000F0) == 0x01800090) { exclusive (op); return; }
				undefined (op);
				return;
			}
			extraLoad (op);
			return;
		}
		if ((op & 0x01900000) == 0x01000000 && !(op & 0x02000000)) { misc (op); return; }
		if ((op & 0x0FB00000) == 0x03200000)						// MSR immediat, les indications
		{
			if ((op & 0x0FFFFF00) == 0x0320F000) return;				// NOP, YIELD, WFE, WFI, SEV
			misc (op);
			return;
		}
		if ((op & 0x0FB00000) == 0x03000000) { undefined (op); return; }
		dataProc (op);
		return;
	case 2: ldst (op); return;
	case 3:
		if (op & 0x10) { media (op); return; }
		ldst (op);
		return;
	case 4: block (op); return;
	case 5:
		if (op & 0x01000000) S->r[14] = S->r[15];
		jump (pcv + (u32) ((s32) (op << 8) >> 6));
		return;
	case 6:
		if ((op & 0x0E00) == 0x0A00) { vfpXfer (op); return; }
		undefined (op);
		return;
	default:
		if ((op & 0x0F000000) == 0x0F000000) { S->m->svc (op & 0xFF); return; }
		if ((op & 0x0E00) == 0x0A00) { vfp (op); return; }
		if ((op & 0x0F000F10) == 0x0E000F10) { cp15 (op); return; }
		undefined (op);
		return;
	}
}

u32 thumbToArm (u32 t);

void Ex::thumb (u32 t)
{
	const u32 pc = S->r[15] - 2;
	if ((t >> 12) == 0xD)
	{
		const u32 c = t >> 8 & 15;
		if (c == 15) { S->m->svc (t & 0xFF); return; }
		if (c == 14) { undefined (t); return; }
		if (condOk (c, cpsr)) jump (pcv + (u32) ((s32) (s8) (t & 0xFF) * 2));
		return;
	}
	if ((t >> 11) == 0x1C) { jump (pcv + (u32) ((s32) (t << 21) >> 20)); return; }
	if ((t >> 11) == 0x1E)						// BL, BLX : les deux moities comptent pour une instruction
	{
		const u32 t2 = rd16 (S, pc + 2);
		if ((t2 >> 11) == 0x1F || (t2 >> 11) == 0x1D)
		{
			const u32 lr = pcv + (u32) ((s32) (t << 21) >> 9);
			S->r[14] = (pc + 4) | 1;
			if ((t2 >> 11) == 0x1F) jump ((lr + ((t2 & 0x7FF) << 1)) & ~1u);
			else { cpsr &= ~FT; jump ((lr + ((t2 & 0x7FF) << 1)) & ~3u); }
			return;
		}
		undefined (t);
		return;
	}
	if ((t & 0xFF00) == 0xBE00) { S->m->fail ("breakpoint at %08x", (unsigned) pc); return; }
	if ((t & 0xFF00) == 0xBF00) return;				// les indications
	if ((t & 0xFFE0) == 0xB660) return;				// CPS
	if ((t & 0xFFF7) == 0xB650) { if (t & 8) cpsr |= FE; else cpsr &= ~FE; return; }
	const u32 a = thumbToArm (t);
	if (!a) { undefined (t); return; }
	if ((t >> 11) == 9 || (t >> 12) == 0xA) pcv &= ~3u;		// (relatif a PC : aligne)
	arm (a);
}

void Arm11::step ()
{
	Machine *M = m;
	if (traceTo && M->now >= traceFrom && M->now < traceTo)			// (essais : le format de tools/emu/ref_n3ds_cpu.cpp)
	{
		printf ("%llu %08x %08x", (unsigned long long) M->now, (unsigned) r[15], (unsigned) (cpsr & 0xF00F0020));
		for (int i = 0; i < 15; i++) printf (" %08x", (unsigned) r[i]);
		printf (" %08x%c", (unsigned) (fpscr & 0xF0000000), 10);
	}
	M->now++; M->ticksLeft--;
	steps++;
	const u32 pc = r[15];
	if (cpsr & FT)
	{
		const u32 t = rd16 (this, pc);
		r[15] = pc + 2;
		Ex x (this, pc + 4);
		x.thumb (t);
		return;
	}
	const u32 op = rd32 (this, pc);
	r[15] = pc + 4;
	if (op >> 28 != 15 && !condOk (op >> 28, cpsr)) return;
	Ex x (this, pc + 8);
	x.arm (op);
}

Arm11::Arm11 (Machine *machine)
{
	memset (r, 0, sizeof r); memset (vfp, 0, sizeof vfp);
	cpsr = 0x10; fpscr = 0; tls = urw = 0; exclOn = exclAddr = 0; exclVal = 0; key = 0; forceStep = 0; fast = 0;
	m = machine; pages = m->mem.pages; jit = 0; halted = false; noJit = getenv ("N3DS_NOJIT") != 0; steps = blocksRun = 0; ticksOut = 0;
	traceFrom = traceTo = 0;
	if (const char *t = getenv ("N3DS_TRACE_RANGE")) { unsigned long long a, b; if (sscanf (t, "%llu-%llu", &a, &b) == 2) { traceFrom = a; traceTo = b; } }
	jitInit ();
}

void Arm11::save (CpuState &s)
{
	memcpy (s.r, r, sizeof s.r);
	memcpy (s.vfp, vfp, sizeof vfp);
	s.cpsr = cpsr; s.fpscr = fpscr; s.tls = tls;
}

void Arm11::load (const CpuState &s)
{
	memcpy (r, s.r, sizeof s.r);
	memcpy (vfp, s.vfp, sizeof vfp);
	cpsr = s.cpsr; fpscr = s.fpscr; tls = s.tls;
	exclOn = 0;
}

void Arm11::run ()
{
	halted = false;
	jitRun ();
}

Cpu *Cpu::create (Machine *m) { return new Arm11 (m); }

}
