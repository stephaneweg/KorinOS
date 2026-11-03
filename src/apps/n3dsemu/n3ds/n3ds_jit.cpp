//
// n3ds/n3ds_jit.cpp -- le recompilateur de l'ARM11 vers A64, ecrit d'apres celui du NDS (src/apps/ndsemu/nds/nds_jit.cpp) :
// meme assembleur, memes operandes, Thumb recrit en ARM, drapeaux de l'invite dans ceux de l'hote. Ce qui change :
//
//   * les 15 registres de l'invite sont fixes dans ceux de l'hote, tout le temps : un bloc n'a rien a charger ni a
//     ranger, et il n'y a aucun appel C dans le code produit ;
//   * tout cas lent (page absente, transfert a cheval sur deux pages) sort AVANT l'instruction, et l'interprete la
//     fait (Arm11::forceStep) : rien n'est a moitie fait ;
//   * ce qui n'est pas traduit forme un bloc d'une instruction, pour l'interprete ;
//   * VFP scalaire traduit tel quel (A64 et VFP calculent pareil, FPCR reglee comme le FPSCR). Les vecteurs courts
//     (LEN != 0) restent a l'interprete : le dispatcher ne lance aucun bloc tant qu'ils sont actifs ;
//   * le temps : un bloc de n instructions retire n des ticks a son entree, et ne part pas s'il n'y tient pas ; une
//     sortie en cours de bloc rend ce qui n'a pas ete execute. Interprete et blocs donnent donc les memes ticks.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "n3ds/n3ds_arm.h"

namespace n3ds {

#define OFF(f) ((u32) __builtin_offsetof (Arm11, f))

enum { CODE_SIZE = 48 << 20, MAX_BLOCKS = 1 << 18, HASH = 1 << 17, FAST_BITS = 16, MAX_INS = 64, PEND_MAX = 1 << 18 };
enum { F_N = 8, F_Z = 4, F_C = 2, F_V = 1, FALL = 15 };

struct FastEntry { u32 key, pad; u32 *code; };
struct Block { u32 key; u32 n; u32 *code; Block *next; bool interp; };
struct Pend { u32 key; u32 *site; int next; };

struct Jit
{
	u32 *buf; u32 size, pos, base;
	void (*enter) (Arm11 *, u32 *code, s64 ticks);
	u32 *exitKey, *lookup;
	Block *blocks; int nBlocks;
	Block *hash[HASH];
	FastEntry *fast;
	Pend *pend; int nPend; int pendHash[1 << 12];
	u8 *codePage;				// [1 << 17] un bit par page de l'invite qui a du code traduit
	u64 flushes, compiled;
	u32 interpOps[256][2];
};

// ---- l'assembleur (celui du NDS) ---------------------------------------------------------------------------------------
enum { XS = 19, XPT = 20, XTK = 21, WZR = 31, SP = 31, T0 = 0, T1 = 1, T2 = 2, T3 = 3, T4 = 4, T5 = 5, T6 = 27, WTGT = 28, TZ = 29, IP0 = 16, IP1 = 17 };
enum { EQ = 0, NE, HS, LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL };
static const u8 HOST[15] = { 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 22, 23, 24, 25, 26 };

struct Asm
{
	u32 *p;
	void e (u32 v) { *p++ = v; }
	void movw (int rd, u32 v)
	{
		u32 lo = v & 0xFFFF, hi = v >> 16;
		if (!hi) { e (0x52800000 | (lo << 5) | (u32) rd); return; }
		if (!lo) { e (0x52A00000 | (hi << 5) | (u32) rd); return; }
		if (hi == 0xFFFF) { e (0x12800000 | ((~lo & 0xFFFF) << 5) | (u32) rd); return; }
		e (0x52800000 | (lo << 5) | (u32) rd);
		e (0x72A00000 | (hi << 5) | (u32) rd);
	}
	void movx (int rd, u64 v)
	{
		e (0xD2800000 | ((u32) (v & 0xFFFF) << 5) | (u32) rd);
		for (int k = 1; k < 4; k++) { u32 h = (u32) (v >> (k * 16)) & 0xFFFF; if (h) e (0xF2800000 | ((u32) k << 21) | (h << 5) | (u32) rd); }
	}
	void mov (int rd, int rm) { if (rd != rm) e (0x2A0003E0 | ((u32) rm << 16) | (u32) rd); }
	void movx64 (int rd, int rm) { e (0xAA0003E0 | ((u32) rm << 16) | (u32) rd); }
	void dp (u32 base, int rd, int rn, int rm, int sh = 0, int amt = 0) { e (base | ((u32) sh << 22) | ((u32) rm << 16) | ((u32) amt << 10) | ((u32) rn << 5) | (u32) rd); }
	void addi (int rd, int rn, u32 imm, bool s = false) { e ((s ? 0x31000000u : 0x11000000u) | (imm << 10) | ((u32) rn << 5) | (u32) rd); }
	void subi (int rd, int rn, u32 imm, bool s = false) { e ((s ? 0x71000000u : 0x51000000u) | (imm << 10) | ((u32) rn << 5) | (u32) rd); }
	void addxi (int rd, int rn, u32 imm) { e (0x91000000 | (imm << 10) | ((u32) rn << 5) | (u32) rd); }
	void subxi (int rd, int rn, u32 imm) { e (0xD1000000 | (imm << 10) | ((u32) rn << 5) | (u32) rd); }
	void ubfm (int rd, int rn, int immr, int imms) { e (0x53000000 | ((u32) immr << 16) | ((u32) imms << 10) | ((u32) rn << 5) | (u32) rd); }
	void sbfm (int rd, int rn, int immr, int imms) { e (0x13000000 | ((u32) immr << 16) | ((u32) imms << 10) | ((u32) rn << 5) | (u32) rd); }
	void lsli (int rd, int rn, int n) { ubfm (rd, rn, (32 - n) & 31, 31 - n); }
	void lsri (int rd, int rn, int n) { ubfm (rd, rn, n, 31); }
	void asri (int rd, int rn, int n) { sbfm (rd, rn, n, 31); }
	void rori (int rd, int rn, int n) { e (0x13800000 | ((u32) rn << 16) | ((u32) n << 10) | ((u32) rn << 5) | (u32) rd); }
	void ubfx (int rd, int rn, int lsb, int w) { ubfm (rd, rn, lsb, lsb + w - 1); }
	void var (u32 base, int rd, int rn, int rm) { e (base | ((u32) rm << 16) | ((u32) rn << 5) | (u32) rd); }
	void ldrw (int rt, int rn, u32 off) { e (0xB9400000 | ((off >> 2) << 10) | ((u32) rn << 5) | (u32) rt); }
	void strw (int rt, int rn, u32 off) { e (0xB9000000 | ((off >> 2) << 10) | ((u32) rn << 5) | (u32) rt); }
	void ldrx (int rt, int rn, u32 off) { e (0xF9400000 | ((off >> 3) << 10) | ((u32) rn << 5) | (u32) rt); }
	void strx (int rt, int rn, u32 off) { e (0xF9000000 | ((off >> 3) << 10) | ((u32) rn << 5) | (u32) rt); }
	void ldrs (int rt, int rn, u32 off) { e (0xBD400000 | ((off >> 2) << 10) | ((u32) rn << 5) | (u32) rt); }
	void strs (int rt, int rn, u32 off) { e (0xBD000000 | ((off >> 2) << 10) | ((u32) rn << 5) | (u32) rt); }
	void ldrd (int rt, int rn, u32 off) { e (0xFD400000 | ((off >> 3) << 10) | ((u32) rn << 5) | (u32) rt); }
	void strd (int rt, int rn, u32 off) { e (0xFD000000 | ((off >> 3) << 10) | ((u32) rn << 5) | (u32) rt); }
	void mrs (int rt) { e (0xD53B4200 | (u32) rt); }
	void msr (int rt) { e (0xD51B4200 | (u32) rt); }
	void br (int rn) { e (0xD61F0000 | ((u32) rn << 5)); }
	u32 *b () { u32 *at = p; e (0x14000000); return at; }
	u32 *bcond (int cond) { u32 *at = p; e (0x54000000 | (u32) cond); return at; }
	u32 *cbz (int rt, bool x64 = false) { u32 *at = p; e ((x64 ? 0xB4000000u : 0x34000000u) | (u32) rt); return at; }
	u32 *cbnz (int rt, bool x64 = false) { u32 *at = p; e ((x64 ? 0xB5000000u : 0x35000000u) | (u32) rt); return at; }
	u32 *tbz (int rt, int bit) { u32 *at = p; e (0x36000000 | ((u32) (bit & 31) << 19) | (bit >= 32 ? 0x80000000u : 0) | (u32) rt); return at; }
	u32 *tbnz (int rt, int bit) { u32 *at = p; e (0x37000000 | ((u32) (bit & 31) << 19) | (bit >= 32 ? 0x80000000u : 0) | (u32) rt); return at; }
	static void patch (u32 *at, u32 *to)
	{
		s32 d = (s32) (to - at);
		u32 op = *at;
		if ((op & 0xFC000000) == 0x14000000) *at = 0x14000000 | ((u32) d & 0x3FFFFFF);
		else if ((op & 0xFF000010) == 0x54000000 || (op & 0x7E000000) == 0x34000000) *at = (op & 0xFF00001F) | (((u32) d & 0x7FFFF) << 5);
		else *at = (op & 0xFFF8001F) | (((u32) d & 0x3FFF) << 5);
	}
	void bTo (u32 *to) { u32 *at = b (); patch (at, to); }
};

// ---- un bloc, decode ---------------------------------------------------------------------------------------------------
enum Kind { K_DP, K_MUL, K_MULL, K_LDST, K_LDSTH, K_LDRD, K_BLOCK, K_B, K_BX, K_CLZ, K_THUMB_BL, K_TBRANCH, K_NOP, K_EXT, K_REV, K_TLS, K_LDREX, K_STREX,
	K_VDATA, K_VMOV, K_VMOV2, K_VMRS, K_VLS, K_VLM, K_INTERP };

struct Ins
{
	u32 addr, op, raw, pcv;
	int kind, cond;
	u8 flagsUse, flagsDef, flagsNeed, liveAfter;
	bool ends;
	u32 target;
	bool link, toArm, toThumb;
};

static const u8 COND_USE[16] = { F_Z, F_Z, F_C, F_C, F_N, F_N, F_V, F_V, F_C | F_Z, F_C | F_Z, F_N | F_V, F_N | F_V, F_N | F_Z | F_V, F_N | F_Z | F_V, 0, 0 };
static inline u32 ror32 (u32 v, int n) { n &= 31; return n ? (v >> n) | (v << (32 - n)) : v; }

// une instruction Thumb en ARM : 0 = pas d'equivalent
u32 thumbToArm (u32 t)
{
	u32 rd = t & 7, rs = (t >> 3) & 7;
	switch (t >> 13)
	{
	case 0:
		if ((t >> 11) == 3)
		{
			u32 rn = (t >> 6) & 7;
			if (t & 0x400) return ((t & 0x200) ? 0xE2500000u : 0xE2900000u) | (rs << 16) | (rd << 12) | rn;
			return ((t & 0x200) ? 0xE0500000u : 0xE0900000u) | (rs << 16) | (rd << 12) | rn;
		}
		return 0xE1B00000u | (rd << 12) | (((t >> 6) & 31) << 7) | (((t >> 11) & 3) << 5) | rs;
	case 1:
	{
		u32 r = (t >> 8) & 7, imm = t & 0xFF;
		switch ((t >> 11) & 3)
		{
		case 0: return 0xE3B00000u | (r << 12) | imm;
		case 1: return 0xE3500000u | (r << 16) | imm;
		case 2: return 0xE2900000u | (r << 16) | (r << 12) | imm;
		default: return 0xE2500000u | (r << 16) | (r << 12) | imm;
		}
	}
	case 2:
		if ((t >> 10) == 0x10)
		{
			switch ((t >> 6) & 15)
			{
			case 0: return 0xE0100000u | (rd << 16) | (rd << 12) | rs;
			case 1: return 0xE0300000u | (rd << 16) | (rd << 12) | rs;
			case 2: return 0xE1B00010u | (rd << 12) | (rs << 8) | (0 << 5) | rd;
			case 3: return 0xE1B00010u | (rd << 12) | (rs << 8) | (1 << 5) | rd;
			case 4: return 0xE1B00010u | (rd << 12) | (rs << 8) | (2 << 5) | rd;
			case 5: return 0xE0B00000u | (rd << 16) | (rd << 12) | rs;
			case 6: return 0xE0D00000u | (rd << 16) | (rd << 12) | rs;
			case 7: return 0xE1B00010u | (rd << 12) | (rs << 8) | (3 << 5) | rd;
			case 8: return 0xE1100000u | (rd << 16) | rs;
			case 9: return 0xE2700000u | (rs << 16) | (rd << 12);
			case 10: return 0xE1500000u | (rd << 16) | rs;
			case 11: return 0xE1700000u | (rd << 16) | rs;
			case 12: return 0xE1900000u | (rd << 16) | (rd << 12) | rs;
			case 13: return 0xE0100090u | (rd << 16) | (rd << 8) | rs;
			case 14: return 0xE1D00000u | (rd << 16) | (rd << 12) | rs;
			default: return 0xE1F00000u | (rd << 12) | rs;
			}
		}
		if ((t >> 10) == 0x11)
		{
			u32 hd = (t & 7) | ((t >> 4) & 8), hs = (t >> 3) & 15;
			switch ((t >> 8) & 3)
			{
			case 0: return 0xE0800000u | (hd << 16) | (hd << 12) | hs;
			case 1: return 0xE1500000u | (hd << 16) | hs;
			case 2: return 0xE1A00000u | (hd << 12) | hs;
			default: return ((t & 0x80) ? 0xE12FFF30u : 0xE12FFF10u) | hs;
			}
		}
		if ((t >> 11) == 9) return 0xE59F0000u | (((t >> 8) & 7) << 12) | ((t & 0xFF) * 4);
		{
			u32 ro = (t >> 6) & 7, base = (rs << 16) | (rd << 12) | ro;
			switch ((t >> 9) & 7)
			{
			case 0: return 0xE7800000u | base;
			case 1: return 0xE18000B0u | base;
			case 2: return 0xE7C00000u | base;
			case 3: return 0xE19000D0u | base;
			case 4: return 0xE7900000u | base;
			case 5: return 0xE19000B0u | base;
			case 6: return 0xE7D00000u | base;
			default: return 0xE19000F0u | base;
			}
		}
	case 3:
	{
		u32 imm = (t >> 6) & 31;
		bool byte = t & 0x1000, load = t & 0x800;
		u32 base = (rs << 16) | (rd << 12);
		if (byte) return (load ? 0xE5D00000u : 0xE5C00000u) | base | imm;
		return (load ? 0xE5900000u : 0xE5800000u) | base | (imm * 4);
	}
	case 4:
		if (!(t & 0x1000))
		{
			u32 off = ((t >> 6) & 31) * 2;
			return ((t & 0x800) ? 0xE1D000B0u : 0xE1C000B0u) | (rs << 16) | (rd << 12) | ((off & 0xF0) << 4) | (off & 0xF);
		}
		return ((t & 0x800) ? 0xE59D0000u : 0xE58D0000u) | (((t >> 8) & 7) << 12) | ((t & 0xFF) * 4);
	case 5:
		if (!(t & 0x1000)) return ((t & 0x800) ? 0xE28D0F00u : 0xE28F0F00u) | (((t >> 8) & 7) << 12) | (t & 0xFF);
		if ((t & 0x0F00) == 0x0000) return ((t & 0x80) ? 0xE24DDF00u : 0xE28DDF00u) | (t & 0x7F);
		if ((t & 0x0F00) == 0x0200)						// SXTH, SXTB, UXTH, UXTB
		{
			static const u32 X[4] = { 0xE6BF0070u, 0xE6AF0070u, 0xE6FF0070u, 0xE6EF0070u };
			return X[(t >> 6) & 3] | (rd << 12) | rs;
		}
		if ((t & 0x0F00) == 0x0A00)						// REV, REV16, REVSH
		{
			static const u32 X[4] = { 0xE6BF0F30u, 0xE6BF0FB0u, 0, 0xE6FF0FB0u };
			return X[(t >> 6) & 3] ? X[(t >> 6) & 3] | (rd << 12) | rs : 0;
		}
		if ((t & 0x0600) == 0x0400)
		{
			u32 list = t & 0xFF;
			if (t & 0x800) return 0xE8BD0000u | list | ((t & 0x100) ? 0x8000u : 0);
			return 0xE92D0000u | list | ((t & 0x100) ? 0x4000u : 0);
		}
		return 0;
	case 6:
		if (!(t & 0x1000))
		{
			if (!(t & 0xFF)) return 0;
			u32 rb = (t >> 8) & 7, list = t & 0xFF;
			// (LDMIA avec la base dans la liste : pas d'ecriture de la base)
			if ((t & 0x800) && (list >> rb & 1)) return 0xE8900000u | (rb << 16) | list;
			return ((t & 0x800) ? 0xE8B00000u : 0xE8A00000u) | (rb << 16) | list;
		}
		return 0;
	default: return 0;
	}
}

static void interp (Ins &in) { in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; }

// la nature de l'instruction ARM, ses drapeaux, si elle finit le bloc
static void classify (Ins &in)
{
	const u32 op = in.op;
	in.cond = (int) (op >> 28);
	in.flagsUse = in.cond < 14 ? COND_USE[in.cond] : 0;
	in.flagsDef = 0; in.ends = false; in.link = in.toArm = in.toThumb = false;
	if (in.cond == 15)
	{
		if ((op & 0xFD70F000) == 0xF550F000) { in.kind = K_NOP; in.cond = 14; return; }		// PLD
		if ((op & 0xFE000000) == 0xFA000000)							// BLX immediat
		{
			in.kind = K_B; in.cond = 14; in.ends = true; in.link = true; in.toThumb = true; in.flagsUse = 0;
			in.target = in.pcv + (u32) ((s32) (op << 8) >> 6) + (op >> 23 & 2);
			return;
		}
		interp (in);
		return;
	}
	switch ((op >> 25) & 7)
	{
	case 0: case 1:
		if ((op & 0x0E000090) == 0x00000090)
		{
			if ((op & 0x60) == 0)
			{
				if ((op & 0x0FC000F0) == 0x00000090) { in.kind = K_MUL; if (op & 0x00100000) in.flagsDef = F_N | F_Z; return; }
				if ((op & 0x0F8000F0) == 0x00800090) { in.kind = K_MULL; if (op & 0x00100000) in.flagsDef = F_N | F_Z; return; }
				if ((op & 0x0FF00FF0) == 0x01800F90) { in.kind = K_STREX; return; }
				if ((op & 0x0FF00FFF) == 0x01900F9F) { in.kind = K_LDREX; return; }
				interp (in);
				return;
			}
			const u32 sh = (op >> 5) & 3;
			const bool L = op & 0x00100000;
			if (!L && sh >= 2)							// LDRD, STRD
			{
				if ((op >> 12 & 1) || (op >> 12 & 15) == 14) { interp (in); return; }
				in.kind = K_LDRD;
				return;
			}
			in.kind = K_LDSTH;
			if (L && ((op >> 12) & 15) == 15) { in.ends = true; in.flagsUse = FALL; }
			return;
		}
		if ((op & 0x01900000) == 0x01000000 && !(op & 0x02000000))			// l'espace des divers
		{
			if ((op & 0x0FFFFFD0) == 0x012FFF10) { in.kind = K_BX; in.ends = true; in.flagsUse |= 0; in.link = op & 0x20; return; }
			if ((op & 0x0FFF0FF0) == 0x016F0F10) { in.kind = K_CLZ; return; }
			interp (in);
			return;
		}
		if ((op & 0x0FFFFF00) == 0x0320F000) { in.kind = K_NOP; return; }		// NOP, YIELD, WFE, WFI, SEV
		if ((op & 0x0FB00000) == 0x03200000 || (op & 0x0FB00000) == 0x03000000) { interp (in); return; }
		{
			in.kind = K_DP;
			const int opc = (int) (op >> 21) & 15, rd = (int) (op >> 12) & 15;
			const bool S = op & 0x00100000, regShift = !(op & 0x02000000) && (op & 0x10);
			const bool rrx = !(op & 0x02000000) && !(op & 0x10) && ((op >> 5) & 3) == 3 && !((op >> 7) & 31);
			if (rrx) in.flagsUse |= F_C;
			if (opc == 5 || opc == 6 || opc == 7) in.flagsUse |= F_C;
			if (rd == 15 && (opc < 8 || opc > 11))
			{
				if (S) { interp (in); return; }
				in.ends = true;
			}
			if (S)
			{
				const bool logical = opc < 2 || opc == 8 || opc == 9 || opc >= 12;
				if (!logical) in.flagsDef = FALL;
				else
				{
					in.flagsDef = F_N | F_Z;
					if (op & 0x02000000) { if ((op >> 8) & 15) in.flagsDef |= F_C; }
					else if (regShift) { in.flagsUse |= F_C; in.flagsDef |= F_C; }
					else if (((op >> 7) & 31) || ((op >> 5) & 3)) in.flagsDef |= F_C;
				}
			}
			return;
		}
	case 2: case 3:
		if ((op & 0x02000010) == 0x02000010)
		{
			const u32 f = op >> 20 & 0x1F, sub = op >> 5 & 7;
			if (sub == 3 && (f & 0x18) == 0x08 && (f & 3) != 0) { in.kind = K_EXT; return; }	// SXTB, UXTH, ... (sans les 16 x 2)
			if (f == 0x0B && (sub == 1 || sub == 5)) { in.kind = K_REV; return; }
			if (f == 0x0F && sub == 5) { in.kind = K_REV; return; }
			interp (in);
			return;
		}
		in.kind = K_LDST;
		if (((op >> 12) & 15) == 15 && (op & 0x00100000)) { in.ends = true; in.flagsUse = FALL; }
		if ((op & 0x02000000) && ((op >> 5) & 3) == 3 && !((op >> 7) & 31)) in.flagsUse |= F_C;
		if (!(op & 0x01000000) && (op & 0x00200000)) { interp (in); return; }			// (LDRT, STRT)
		return;
	case 4:
		if ((op & 0x00400000) || !(op & 0xFFFF)) { interp (in); return; }
		if ((op & 0x000F0000) == 0x000F0000) { interp (in); return; }
		in.kind = K_BLOCK;
		if ((op & 0x00108000) == 0x00108000) { in.ends = true; in.flagsUse = FALL; }
		return;
	case 5:
		in.kind = K_B;
		in.target = in.pcv + (u32) ((s32) (op << 8) >> 6);
		in.link = op & 0x01000000;
		if (in.cond == 14) in.ends = true;
		return;
	case 6:
		if ((op & 0x0E00) == 0x0A00)
		{
			if ((op & 0x0FE00000) == 0x0C400000)
			{
				if ((op >> 12 & 15) == 15 || (op >> 16 & 15) == 15) { interp (in); return; }
				in.kind = K_VMOV2;
				return;
			}
			if ((op & 0x01200000) == 0x01000000) { in.kind = K_VLS; return; }		// VLDR, VSTR
			const u32 imm = op & 0xFF, rn = op >> 16 & 15;
			if (rn == 15 || !imm || imm & 1 && (op & 0x100) || imm > 32) { interp (in); return; }
			if ((op & 0x01200000) == 0x01200000 || (op & 0x01800000) == 0x00800000 ) { in.kind = K_VLM; return; }
			interp (in);
			return;
		}
		interp (in);
		return;
	default:
		if ((op & 0x0F000E10) == 0x0E000A00)
		{
			// (la double precision n'a ici que d0-d15)
			const u32 opc1 = op >> 20 & 0xB, opc2 = op >> 16 & 15, N = op >> 7 & 1, dbl = op >> 8 & 1;
			if (opc1 != 0xB)
			{
				const u32 k = opc1 << 1 | (op >> 6 & 1);
				if ((k > 7 && k != 0x10) || (dbl && ((op >> 22 & 1) || N || (op >> 5 & 1)))) { interp (in); return; }
				in.kind = K_VDATA;
				return;
			}
			if (!(op >> 6 & 1)) { interp (in); return; }
			if (opc2 > 13 || opc2 == 2 || opc2 == 3 || opc2 == 6 || (opc2 >= 9 && opc2 <= 11)) { interp (in); return; }
			if (opc2 == 7 && !N) { interp (in); return; }
			in.kind = K_VDATA;
			return;
		}
		if ((op & 0x0FE00F10) == 0x0E000A10 && (op >> 12 & 15) != 15) { in.kind = K_VMOV; return; }	// VMOV Sn <-> Rt
		if ((op & 0x0FFFFFFF) == 0x0EF1FA10) { in.kind = K_VMRS; in.flagsDef = FALL; return; }	// VMRS APSR_nzcv, FPSCR
		if ((op & 0x0FFF0FFF) == 0x0E1D0F70 && (op >> 12 & 15) != 15) { in.kind = K_TLS; return; }	// MRC p15, 0, Rt, c13, c0, 3
		if ((op & 0x0FFF0F10) == 0x0E070F10) { in.kind = K_NOP; return; }			// MCR p15, 0, Rt, c7, ... : barrieres, cache
		interp (in);
		return;
	}
}

// ---- la traduction -----------------------------------------------------------------------------------------------------
struct Gen
{
	Arm11 *c; Jit *j;
	Asm a;
	Ins ins[MAX_INS]; int n;
	bool thumb;
	struct Stub { u32 *from[3]; int nFrom; int k; } stubs[MAX_INS * 2]; int nStubs;
	u32 *undo;

	u32 voff (u32 s) const { return OFF (vfp) + s * 4; }
	u32 keyOf (u32 pc) const { return pc | (thumb ? 1u : 0u); }
	int rd (int g, int tmp, u32 pcv) { if (g == 15) { a.movw (tmp, pcv); return tmp; } return HOST[g]; }
	int dst (int g, int tmp) { return g < 15 ? HOST[g] : tmp; }

	void refund (int k) { const int left = n - k - 1; if (left) a.addxi (XTK, XTK, (u32) left); }
	// sortie vers une adresse connue : directement au bloc s'il existe, sinon a retoucher quand il existera
	void exitTo (u32 key, int k);
	void exitReg (int k, bool interwork)					// WTGT = l'adresse
	{
		refund (k);
		if (interwork)
		{
			u32 *odd = a.tbnz (WTGT, 0);
			a.ubfm (WTGT, WTGT, 2, 31); a.lsli (WTGT, WTGT, 2);
			Asm::patch (odd, a.p);
			a.mov (T0, WTGT);
		}
		else if (thumb) { a.movw (T1, 1); a.dp (0x2A000000, T0, WTGT, T1); }
		else { a.ubfm (T0, WTGT, 2, 31); a.lsli (T0, T0, 2); }
		a.bTo (j->lookup);
	}
	// le cas lent : avant l'instruction k, pour l'interprete
	u32 *&slow (int k)
	{
		if (nStubs) { Stub &s = stubs[nStubs - 1]; if (s.k == k && s.nFrom < 3) return s.from[s.nFrom++]; }
		Stub &t = stubs[nStubs++];
		t.k = k; t.nFrom = 1;
		return t.from[0];
	}
	void emitStubs ()
	{
		for (int i = 0; i < nStubs; i++)
		{
			const Stub &s = stubs[i];
			for (int f = 0; f < s.nFrom; f++) Asm::patch (s.from[f], a.p);
			a.addxi (XTK, XTK, (u32) (n - s.k));
			a.movw (T0, 1); a.strw (T0, XS, OFF (forceStep));
			a.movw (T0, keyOf (ins[s.k].addr));
			a.bTo (j->exitKey);
		}
		Asm::patch (undo, a.p);
		a.addxi (XTK, XTK, (u32) n);
		a.movw (T0, keyOf (ins[0].addr));
		a.bTo (j->exitKey);
	}

	// ---- la memoire : IP0 = la page, IP1 = le decalage
	void pageOf (int addr, int k)
	{
		a.lsri (IP0, addr, PAGE_BITS);
		a.e (0xF8607800 | ((u32) XPT << 5) | ((u32) IP0 << 16) | (u32) IP0);	// ldr x16, [x20, x16, lsl 3]
		slow (k) = a.cbz (IP0, true);
		a.ubfx (IP1, addr, 0, PAGE_BITS);
	}
	void load (int addr, int d, int size, bool sgn, int k)
	{
		pageOf (addr, k);
		u32 enc = size == 4 ? 0xB8606800u : size == 2 ? (sgn ? 0x78E06800u : 0x78606800u) : (sgn ? 0x38E06800u : 0x38606800u);
		a.e (enc | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) d);
	}
	void store (int addr, int v, int size, int k)
	{
		pageOf (addr, k);
		u32 enc = size == 4 ? 0xB8206800u : size == 2 ? 0x78206800u : 0x38206800u;
		a.e (enc | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) v);
	}

	// ---- l'operande 2 -> un registre ; la retenue du decaleur -> T5
	int operand2 (const Ins &in, int tmp, bool wantCarry, bool &carryKnown, int &carryConst)
	{
		const u32 op = in.op;
		carryKnown = false; carryConst = -1;
		if (op & 0x02000000)
		{
			const int rot = (int) ((op >> 8) & 15) * 2;
			const u32 v = ror32 (op & 0xFF, rot);
			a.movw (tmp, v);
			if (rot) carryConst = (int) (v >> 31);
			return tmp;
		}
		const int rm = (int) op & 15, type = (int) (op >> 5) & 3;
		if (op & 0x10)
		{
			const int rs = (int) (op >> 8) & 15;
			const int vm = rd (rm, T0, in.pcv), vs = rd (rs, T1, in.pcv);
			a.ubfx (T2, vs, 0, 8);
			auto clamp = [&] (int K) { a.subi (T3, T2, (u32) K); a.asri (T3, T3, 31); a.dp (0x0A000000, T2, T2, T3); a.movw (T4, (u32) K); a.dp (0x0A200000, T4, T4, T3); a.dp (0x2A000000, T2, T2, T4); };
			if (wantCarry) { a.mrs (T6); a.ubfx (T6, T6, 29, 1); a.subi (TZ, T2, 1); a.lsri (TZ, TZ, 31); }
			switch (type)
			{
			case 0:
				clamp (33);
				a.ubfm (IP0, vm, 0, 31);
				a.e (0x9AC02000 | ((u32) T2 << 16) | ((u32) IP0 << 5) | (u32) IP0);
				if (wantCarry) a.e (0xD3608000 | ((u32) IP0 << 5) | (u32) T5);
				a.mov (tmp, IP0);
				break;
			case 1: case 2:
				clamp (type == 1 ? 33 : 32);
				if (type == 1) a.ubfm (IP0, vm, 0, 31); else a.e (0x93407C00 | ((u32) vm << 5) | (u32) IP0);
				a.e (0xD37FF800 | ((u32) IP0 << 5) | (u32) IP0);
				a.e ((type == 1 ? 0x9AC02400u : 0x9AC02800u) | ((u32) T2 << 16) | ((u32) IP0 << 5) | (u32) IP0);
				if (wantCarry) a.ubfx (T5, IP0, 0, 1);
				a.e (0xD341FC00 | ((u32) IP0 << 5) | (u32) tmp);
				break;
			default:
				a.var (0x1AC02C00, tmp, vm, T2);
				if (wantCarry) a.ubfx (T5, tmp, 31, 1);
				break;
			}
			if (wantCarry)
			{
				a.dp (0x4A000000, T3, T5, T6);
				a.dp (0x0A000000, T3, T3, TZ);
				a.dp (0x4A000000, T5, T5, T3);
				carryKnown = true;
			}
			return tmp;
		}
		int amt = (int) (op >> 7) & 31;
		const int vm = rd (rm, T0, in.pcv);
		switch (type)
		{
		case 0:
			if (!amt) return vm;
			if (wantCarry) { a.ubfx (T5, vm, 32 - amt, 1); carryKnown = true; }
			a.lsli (tmp, vm, amt); return tmp;
		case 1:
			if (!amt) { if (wantCarry) { a.ubfx (T5, vm, 31, 1); carryKnown = true; } a.movw (tmp, 0); return tmp; }
			if (wantCarry) { a.ubfx (T5, vm, amt - 1, 1); carryKnown = true; }
			a.lsri (tmp, vm, amt); return tmp;
		case 2:
			if (!amt) amt = 32;
			if (wantCarry) { a.ubfx (T5, vm, amt - 1, 1); carryKnown = true; }
			a.asri (tmp, vm, amt == 32 ? 31 : amt); return tmp;
		default:
			if (!amt)
			{
				a.mrs (T6);
				a.ubfx (T6, T6, 29, 1);
				if (wantCarry) { a.ubfx (T5, vm, 0, 1); carryKnown = true; }
				a.lsri (tmp, vm, 1);
				a.dp (0x2A000000, tmp, tmp, T6, 0, 31);
				return tmp;
			}
			if (wantCarry) { a.ubfx (T5, vm, amt - 1, 1); carryKnown = true; }
			a.rori (tmp, vm, amt); return tmp;
		}
	}

	// N et Z de res ; C de T5 (carryKnown) ou de la constante, sinon garde ; V garde
	void setNZ (int res, u8 live, bool carryKnown, int carryConst, u8 defs)
	{
		const bool keepC = (live & F_C) && !(defs & F_C), newC = (live & F_C) && (defs & F_C), keepV = live & F_V;
		if (!keepC && !newC && !keepV) { a.dp (0x6A000000, WZR, res, res); return; }
		if (keepC || keepV) a.mrs (IP1);
		a.dp (0x6A000000, WZR, res, res);
		a.mrs (IP0);
		if (newC)
		{
			if (carryKnown) a.dp (0xAA000000, IP0, IP0, T5, 0, 29);
			else if (carryConst == 1) { a.movw (T6, 0x20000000); a.dp (0xAA000000, IP0, IP0, T6); }
		}
		else if (keepC) { a.ubfm (T6, IP1, 29, 29); a.dp (0xAA000000, IP0, IP0, T6, 0, 29); }
		if (keepV) { a.ubfm (T6, IP1, 28, 28); a.dp (0xAA000000, IP0, IP0, T6, 0, 28); }
		a.msr (IP0);
	}

	bool genDP (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		const int opc = (int) (op >> 21) & 15, rn = (int) (op >> 16) & 15, rdg = (int) (op >> 12) & 15;
		const bool S = op & 0x00100000;
		const u8 need = S ? in.flagsNeed : 0;
		const bool logical = opc < 2 || opc == 8 || opc == 9 || opc >= 12;
		bool carryKnown; int carryConst;
		const bool wantC = logical && S && (need & F_C);
		const int b = operand2 (in, T1, wantC, carryKnown, carryConst);
		const int av = (opc == 13 || opc == 15) ? -1 : rd (rn, T2, in.pcv);
		const int d = (opc >= 8 && opc <= 11) ? T3 : (rdg == 15 ? WTGT : HOST[rdg]);
		const bool setF = S && need;
		switch (opc)
		{
		case 0: case 8: a.dp (0x0A000000, d, av, b); break;
		case 1: case 9: a.dp (0x4A000000, d, av, b); break;
		case 2: case 10: a.dp (setF ? 0x6B000000u : 0x4B000000u, opc == 10 ? WZR : d, av, b); break;
		case 3: a.dp (setF ? 0x6B000000u : 0x4B000000u, d, b, av); break;
		case 4: case 11: a.dp (setF ? 0x2B000000u : 0x0B000000u, opc == 11 ? WZR : d, av, b); break;
		case 5: a.dp (setF ? 0x3A000000u : 0x1A000000u, d, av, b); break;
		case 6: a.dp (setF ? 0x7A000000u : 0x5A000000u, d, av, b); break;
		case 7: a.dp (setF ? 0x7A000000u : 0x5A000000u, d, b, av); break;
		case 12: a.dp (0x2A000000, d, av, b); break;
		case 13: a.mov (d, b); break;
		case 14: a.dp (0x0A200000, d, av, b); break;
		default: a.dp (0x2A200000, d, WZR, b); break;
		}
		if (setF && logical) setNZ (d, in.liveAfter, carryKnown, carryConst, in.flagsDef);
		if (rdg == 15 && (opc < 8 || opc > 11)) exitReg (k, false);
		return true;
	}

	bool genMul (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		const int rdg = (int) (op >> 16) & 15, rn = (int) (op >> 12) & 15, rs = (int) (op >> 8) & 15, rm = (int) op & 15;
		if (rdg == 15) return false;
		const int vm = rd (rm, T0, in.pcv), vs = rd (rs, T1, in.pcv), acc = (op & 0x00200000) ? rd (rn, T2, in.pcv) : WZR;
		const int d = HOST[rdg];
		a.e (0x1B000000 | ((u32) vs << 16) | ((u32) acc << 10) | ((u32) vm << 5) | (u32) d);
		if ((op & 0x00100000) && in.flagsNeed) setNZ (d, in.liveAfter, false, -1, F_N | F_Z);
		return true;
	}

	bool genMull (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		const int hi = (int) (op >> 16) & 15, lo = (int) (op >> 12) & 15, rs = (int) (op >> 8) & 15, rm = (int) op & 15;
		if (hi == 15 || lo == 15 || hi == lo) return false;
		const int vm = rd (rm, T0, in.pcv), vs = rd (rs, T1, in.pcv);
		const bool sgn = op & 0x00400000;
		if (op & 0x00200000)
		{
			const int vl = HOST[lo], vh = HOST[hi];
			a.ubfm (T4, vl, 0, 31);
			a.e (0xAA000000 | ((u32) vh << 16) | (32u << 10) | ((u32) T4 << 5) | (u32) T4);
			a.e ((sgn ? 0x9B200000u : 0x9BA00000u) | ((u32) vs << 16) | ((u32) T4 << 10) | ((u32) vm << 5) | (u32) T4);
		}
		else a.e ((sgn ? 0x9B207C00u : 0x9BA07C00u) | ((u32) vs << 16) | ((u32) vm << 5) | (u32) T4);
		if ((op & 0x00100000) && in.flagsNeed)
		{
			const bool keep = in.liveAfter & (F_C | F_V);
			if (keep) a.mrs (T5);
			a.e (0xEA00001F | ((u32) T4 << 16) | ((u32) T4 << 5));
			if (keep)
			{
				a.mrs (T3);
				a.ubfm (T5, T5, 28, 29);
				a.dp (0xAA000000, T3, T3, T5, 0, 28);
				a.msr (T3);
			}
		}
		a.mov (HOST[lo], T4);
		a.e (0xD360FC00 | ((u32) T4 << 5) | (u32) HOST[hi]);
		return true;
	}

	bool genLdst (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		const bool I = op & 0x02000000, P = op & 0x01000000, U = op & 0x00800000, B = op & 0x00400000, W = op & 0x00200000, L = op & 0x00100000;
		const int rn = (int) (op >> 16) & 15, rdg = (int) (op >> 12) & 15;
		const bool wb = W || !P;
		if (wb && rn == 15) return false;
		int offReg = -1; u32 offImm = 0;
		if (I)
		{
			bool ck; int cc;
			Ins tmp = in; tmp.op = op & ~0x02000010u;
			offReg = operand2 (tmp, T1, false, ck, cc);
		}
		else offImm = op & 0xFFF;
		const int base = rd (rn, T2, in.pcv);
		if (offReg >= 0) a.dp (U ? 0x0B000000u : 0x4B000000u, T3, base, offReg);
		else if (offImm) { if (U) a.addi (T3, base, offImm); else a.subi (T3, base, offImm); }
		else a.mov (T3, base);
		const int addr = P ? T3 : base;
		if (L)
		{
			const int d = rdg == 15 ? WTGT : (wb && rn == rdg ? T0 : HOST[rdg]);
			// (d peut etre la base d'un post-indexe : T3 garde la nouvelle valeur)
			load (addr, d, B ? 1 : 4, false, k);
			if (wb && rn != rdg) a.mov (HOST[rn], T3);
			if (rdg == 15) { exitReg (k, true); return true; }
			if (d == T0) a.mov (HOST[rdg], T0);
		}
		else
		{
			const int v = rd (rdg, T0, in.pcv);
			store (addr, v, B ? 1 : 4, k);
			if (wb) a.mov (HOST[rn], T3);
		}
		return true;
	}

	bool genLdstH (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		const bool P = op & 0x01000000, U = op & 0x00800000, W = op & 0x00200000, L = op & 0x00100000;
		const int rn = (int) (op >> 16) & 15, rdg = (int) (op >> 12) & 15, sh = (int) (op >> 5) & 3;
		const bool wb = W || !P;
		if (wb && rn == 15) return false;
		int offReg = -1; u32 offImm = 0;
		if (op & 0x00400000) offImm = ((op >> 4) & 0xF0) | (op & 0xF);
		else offReg = rd ((int) op & 15, T1, in.pcv);
		const int base = rd (rn, T2, in.pcv);
		if (offReg >= 0) a.dp (U ? 0x0B000000u : 0x4B000000u, T3, base, offReg);
		else if (offImm) { if (U) a.addi (T3, base, offImm); else a.subi (T3, base, offImm); }
		else a.mov (T3, base);
		const int addr = P ? T3 : base;
		if (L)
		{
			const int d = rdg == 15 ? WTGT : (wb && rn == rdg ? T0 : HOST[rdg]);
			if (sh == 1) load (addr, d, 2, false, k);
			else if (sh == 2) load (addr, d, 1, true, k);
			else load (addr, d, 2, true, k);
			if (wb && rn != rdg) a.mov (HOST[rn], T3);
			if (rdg == 15) { exitReg (k, true); return true; }
			if (d == T0) a.mov (HOST[rdg], T0);
		}
		else
		{
			if (sh != 1) return false;
			const int v = rd (rdg, T0, in.pcv);
			store (addr, v, 2, k);
			if (wb) a.mov (HOST[rn], T3);
		}
		return true;
	}

	// LDRD, STRD, sur une seule page
	bool genLdrd (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		const bool P = op & 0x01000000, U = op & 0x00800000, W = op & 0x00200000, ld = !(op & 0x20);
		const int rn = (int) (op >> 16) & 15, rdg = (int) (op >> 12) & 15;
		const bool wb = W || !P;
		if (wb && (rn == 15 || rn == rdg || rn == rdg + 1)) return false;
		int offReg = -1; u32 offImm = 0;
		if (op & 0x00400000) offImm = ((op >> 4) & 0xF0) | (op & 0xF);
		else offReg = rd ((int) op & 15, T1, in.pcv);
		const int base = rd (rn, T2, in.pcv);
		if (offReg >= 0) a.dp (U ? 0x0B000000u : 0x4B000000u, T3, base, offReg);
		else if (offImm) { if (U) a.addi (T3, base, offImm); else a.subi (T3, base, offImm); }
		else a.mov (T3, base);
		const int addr = P ? T3 : base;
		pageOf (addr, k);
		a.addi (T4, IP1, 7);
		slow (k) = a.tbnz (T4, PAGE_BITS);
		if (ld)
		{
			a.e (0xF8606800 | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) T4);
			a.mov (HOST[rdg], T4);
			a.e (0xD360FC00 | ((u32) T4 << 5) | (u32) HOST[rdg + 1]);
		}
		else
		{
			a.ubfm (T4, HOST[rdg], 0, 31);
			a.e (0xAA000000 | ((u32) HOST[rdg + 1] << 16) | (32u << 10) | ((u32) T4 << 5) | (u32) T4);
			a.e (0xF8206800 | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) T4);
		}
		if (wb) a.mov (HOST[rn], T3);
		return true;
	}

	bool genBlock (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		const bool P = op & 0x01000000, U = op & 0x00800000, W = op & 0x00200000, L = op & 0x00100000;
		const int rn = (int) (op >> 16) & 15;
		const u32 list = op & 0xFFFF;
		const u32 bytes = (u32) __builtin_popcount (list) * 4;
		const int base = HOST[rn];
		a.mov (T3, base);
		if (U) { if (P) a.addi (T4, T3, 4); else a.mov (T4, T3); }
		else a.subi (T4, T3, bytes - (P ? 0 : 4));
		pageOf (T4, k);
		a.addi (T5, IP1, bytes - 1);
		slow (k) = a.tbnz (T5, PAGE_BITS);
		a.e (0x8B000000 | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) IP0);	// add x16, x16, x17
		int i = 0;
		for (int g = 0; g < 16; g++)
		{
			if (!(list >> g & 1)) continue;
			const u32 off = (u32) i * 4;
			if (L) a.ldrw (g == 15 ? (int) WTGT : (g == rn ? (int) T0 : (int) HOST[g]), IP0, off);
			else
			{
				int v;
				if (g == 15) { a.movw (T0, in.pcv); v = T0; }
				else if (g == rn)
				{
					v = T3;
					if (W && i) { if (U) a.addi (T0, T3, bytes); else a.subi (T0, T3, bytes); v = T0; }
				}
				else v = HOST[g];
				a.strw (v, IP0, off);
			}
			i++;
		}
		if (L && (list >> rn & 1)) a.mov (HOST[rn], T0);
		else if (W) { if (U) a.addi (HOST[rn], T3, bytes); else a.subi (HOST[rn], T3, bytes); }
		if (L && (list & 0x8000)) exitReg (k, true);
		return true;
	}

	bool genB (int k)
	{
		Ins &in = ins[k];
		if (in.link) { a.movw (T0, thumb ? (in.addr + 2) | 1 : in.addr + 4); a.mov (HOST[14], T0); }
		exitTo (in.target | (in.toThumb || (thumb && !in.toArm) ? 1 : 0), k);
		return true;
	}

	bool genBX (int k)
	{
		Ins &in = ins[k];
		const int v = rd ((int) in.op & 15, T0, in.pcv);
		a.mov (WTGT, v);
		if (in.link) { a.movw (T1, thumb ? (in.addr + 2) | 1 : in.addr + 4); a.mov (HOST[14], T1); }
		exitReg (k, true);
		return true;
	}

	bool genExt (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op, f = op >> 20 & 7;
		const int rn = (int) (op >> 16) & 15, rdg = (int) (op >> 12) & 15, rot = (int) (op >> 10 & 3) * 8;
		if (rdg == 15) return false;
		int v = rd ((int) op & 15, T0, in.pcv);
		if (rot) { a.rori (T0, v, rot); v = T0; }
		switch (f)
		{
		case 2: a.sbfm (T0, v, 0, 7); break;
		case 3: a.sbfm (T0, v, 0, 15); break;
		case 6: a.ubfm (T0, v, 0, 7); break;
		case 7: a.ubfm (T0, v, 0, 15); break;
		default: return false;
		}
		if (rn == 15) a.mov (HOST[rdg], T0);
		else a.dp (0x0B000000, HOST[rdg], HOST[rn], T0);
		return true;
	}

	bool genRev (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op, f = op >> 20 & 0x1F, sub = op >> 5 & 7;
		const int rdg = (int) (op >> 12) & 15, v = rd ((int) op & 15, T0, in.pcv);
		if (rdg == 15) return false;
		if (f == 0x0B && sub == 1) a.e (0x5AC00800 | ((u32) v << 5) | (u32) HOST[rdg]);
		else if (f == 0x0B) a.e (0x5AC00400 | ((u32) v << 5) | (u32) HOST[rdg]);
		else { a.e (0x5AC00400 | ((u32) v << 5) | (u32) T0); a.sbfm (HOST[rdg], T0, 0, 15); }
		return true;
	}

	bool genLdrex (int k)
	{
		Ins &in = ins[k];
		const int rn = (int) (in.op >> 16) & 15, rdg = (int) (in.op >> 12) & 15;
		if (rn == 15 || rdg == 15) return false;
		a.mov (T3, HOST[rn]);
		load (T3, T0, 4, false, k);
		a.movw (T1, 1); a.strw (T1, XS, OFF (exclOn));
		a.lsri (T1, T3, 4); a.lsli (T1, T1, 4); a.strw (T1, XS, OFF (exclAddr));
		a.strx (T0, XS, OFF (exclVal));				// (T0 : les 32 bits du haut sont a zero)
		a.mov (HOST[rdg], T0);
		return true;
	}

	bool genStrex (int k)
	{
		Ins &in = ins[k];
		const int rn = (int) (in.op >> 16) & 15, rdg = (int) (in.op >> 12) & 15, rm = (int) in.op & 15;
		if (rn == 15 || rdg == 15 || rm == 15) return false;
		const int addr = HOST[rn];
		pageOf (addr, k);
		a.ldrw (T0, XS, OFF (exclOn));
		u32 *f1 = a.cbz (T0);
		a.ldrw (T0, XS, OFF (exclAddr));
		a.lsri (T1, addr, 4); a.lsli (T1, T1, 4);
		a.dp (0x4A000000, T0, T0, T1);
		u32 *f2 = a.cbnz (T0);
		a.e (0xB8606800 | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) T0);
		a.ldrw (T1, XS, OFF (exclVal));
		a.dp (0x4A000000, T0, T0, T1);
		u32 *f3 = a.cbnz (T0);
		a.e (0xB8206800 | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) HOST[rm]);
		a.strw (WZR, XS, OFF (exclOn));
		a.movw (HOST[rdg], 0);
		u32 *done = a.b ();
		Asm::patch (f1, a.p); Asm::patch (f2, a.p); Asm::patch (f3, a.p);
		a.strw (WZR, XS, OFF (exclOn));
		a.movw (HOST[rdg], 1);
		Asm::patch (done, a.p);
		return true;
	}

	// ---- le VFP scalaire : tout en memoire, s0-s3 de l'hote comme bloc-notes
	void fls (int h, u32 v, bool dbl) { if (dbl) a.ldrd (h, XS, voff (v * 2)); else a.ldrs (h, XS, voff (v)); }
	void fst (int h, u32 v, bool dbl) { if (dbl) a.strd (h, XS, voff (v * 2)); else a.strs (h, XS, voff (v)); }
	bool genVData (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		const bool dbl = op & 0x100;
		const u32 D = op >> 22 & 1, N = op >> 7 & 1, M = op >> 5 & 1, opc1 = op >> 20 & 0xB, s = op >> 6 & 1;
		const u32 rd4 = op >> 12 & 15, rn4 = op >> 16 & 15, rm4 = op & 15;
		const u32 vd = dbl ? rd4 : rd4 * 2 + D, vn = dbl ? rn4 : rn4 * 2 + N, vm = dbl ? rm4 : rm4 * 2 + M;
		const u32 ty = dbl ? 0x00400000 : 0;
		auto f3 = [&] (u32 base, int d, int x, int y) { a.e (base | ty | ((u32) y << 16) | ((u32) x << 5) | (u32) d); };
		auto f2 = [&] (u32 base, int d, int x) { a.e (base | ty | ((u32) x << 5) | (u32) d); };
		enum : u32 { FADD = 0x1E202800, FSUB = 0x1E203800, FMUL = 0x1E200800, FDIV = 0x1E201800, FNEG = 0x1E214000, FABS = 0x1E20C000, FSQRT = 0x1E21C000 };
		if (opc1 != 0xB)
		{
			const u32 w = opc1 << 1 | s;
			fls (1, vn, dbl); fls (2, vm, dbl);
			if (w <= 3)
			{
				fls (0, vd, dbl);
				f3 (FMUL, 1, 1, 2);
				if (w == 1 || w == 3) f2 (FNEG, 1, 1);
				if (w == 2 || w == 3) f2 (FNEG, 0, 0);
				f3 (FADD, 0, 0, 1);
			}
			else switch (w)
			{
			case 4: f3 (FMUL, 0, 1, 2); break;
			case 5: f3 (FMUL, 0, 1, 2); f2 (FNEG, 0, 0); break;
			case 6: f3 (FADD, 0, 1, 2); break;
			case 7: f3 (FSUB, 0, 1, 2); break;
			default: f3 (FDIV, 0, 1, 2); break;
			}
			fst (0, vd, dbl);
			return true;
		}
		switch (rn4)
		{
		case 0: case 1:
		{
			const u32 kind = rn4 << 1 | N;
			if (kind == 0)					// VMOV : les mots tels quels
			{
				if (dbl) { a.ldrx (T0, XS, voff (vm * 2)); a.strx (T0, XS, voff (vd * 2)); }
				else { a.ldrw (T0, XS, voff (vm)); a.strw (T0, XS, voff (vd)); }
				return true;
			}
			fls (1, vm, dbl);
			f2 (kind == 1 ? FABS : kind == 2 ? FNEG : FSQRT, 0, 1);
			fst (0, vd, dbl);
			return true;
		}
		case 4: case 5:
			a.mrs (T1);
			fls (0, vd, dbl);
			if (rn4 == 4) { fls (1, vm, dbl); a.e ((N ? 0x1E202010u : 0x1E202000u) | ty | (1u << 16)); }
			else a.e ((N ? 0x1E202018u : 0x1E202008u) | ty);
			a.mrs (T0);
			a.msr (T1);
			a.ldrw (T1, XS, OFF (fpscr));
			a.ubfm (T1, T1, 0, 27);
			a.dp (0x2A000000, T1, T1, T0);
			a.strw (T1, XS, OFF (fpscr));
			return true;
		case 7:
			if (dbl) { a.ldrd (1, XS, voff (vm * 2)); a.e (0x1E624000 | (1u << 5)); a.strs (0, XS, voff (rd4 * 2 + D)); }
			else { a.ldrs (1, XS, voff (vm)); a.e (0x1E22C000 | (1u << 5)); a.strd (0, XS, voff (rd4 * 2)); }
			return true;
		case 8:
			a.ldrw (T0, XS, voff (rm4 * 2 + M));
			a.e ((N ? 0x1E220000u : 0x1E230000u) | ty | ((u32) T0 << 5));	// scvtf, ucvtf s0 / d0, w0
			fst (0, vd, dbl);
			return true;
		case 12: case 13:
			fls (1, vm, dbl);
			if (!N) { f2 (0x1E27C000, 1, 1); }				// frinti : l'arrondi de FPCR
			a.e ((rn4 == 13 ? 0x1E380000u : 0x1E390000u) | ty | (1u << 5) | (u32) T0);	// fcvtzs, fcvtzu w0, s1 / d1
			a.strw (T0, XS, voff (rd4 * 2 + D));
			return true;
		}
		return false;
	}

	bool genVLS (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op, imm = (op & 0xFF) * 4;
		const bool U = op & 0x00800000, L = op & 0x00100000, dbl = op & 0x100;
		const int rn = (int) (op >> 16) & 15;
		const u32 vd = op >> 12 & 15, sd = vd * 2 + (op >> 22 & 1);
		if (rn == 15) a.movw (T3, (in.pcv & ~3u));
		else a.mov (T3, HOST[rn]);
		if (imm) { if (U) a.addi (T3, T3, imm); else a.subi (T3, T3, imm); }
		if (!dbl)
		{
			if (L) { load (T3, T0, 4, false, k); a.strw (T0, XS, voff (sd)); }
			else { a.ldrw (T0, XS, voff (sd)); store (T3, T0, 4, k); }
			return true;
		}
		pageOf (T3, k);
		a.addi (T4, IP1, 7);
		slow (k) = a.tbnz (T4, PAGE_BITS);
		if (L) { a.e (0xF8606800 | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) T0); a.strx (T0, XS, voff (vd * 2)); }
		else { a.ldrx (T0, XS, voff (vd * 2)); a.e (0xF8206800 | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) T0); }
		return true;
	}

	bool genVLM (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op, imm = op & 0xFF;
		const bool P = op & 0x01000000, U = op & 0x00800000, W = op & 0x00200000, L = op & 0x00100000, dbl = op & 0x100;
		const int rn = (int) (op >> 16) & 15;
		const u32 vd = op >> 12 & 15, first = dbl ? vd * 2 : vd * 2 + (op >> 22 & 1), bytes = imm * 4;
		if (first + imm > 32) return false;
		a.mov (T3, HOST[rn]);
		if (P) a.subi (T4, T3, bytes); else a.mov (T4, T3);
		pageOf (T4, k);
		a.addi (T5, IP1, bytes - 1);
		slow (k) = a.tbnz (T5, PAGE_BITS);
		a.e (0x8B000000 | ((u32) IP1 << 16) | ((u32) IP0 << 5) | (u32) IP0);
		for (u32 i = 0; i < imm; i++)
		{
			if (L) { a.ldrw (T0, IP0, i * 4); a.strw (T0, XS, voff (first + i)); }
			else { a.ldrw (T0, XS, voff (first + i)); a.strw (T0, IP0, i * 4); }
		}
		if (W) { if (U) a.addi (HOST[rn], T3, bytes); else a.subi (HOST[rn], T3, bytes); }
		return true;
	}

	bool genOne (int k)
	{
		Ins &in = ins[k];
		const u32 op = in.op;
		switch (in.kind)
		{
		case K_DP: return genDP (k);
		case K_MUL: return genMul (k);
		case K_MULL: return genMull (k);
		case K_LDST: return genLdst (k);
		case K_LDSTH: return genLdstH (k);
		case K_LDRD: return genLdrd (k);
		case K_BLOCK: return genBlock (k);
		case K_B: case K_TBRANCH: case K_THUMB_BL: return genB (k);
		case K_BX: return genBX (k);
		case K_CLZ: { const int rdg = (int) (op >> 12) & 15; if (rdg == 15) return false; a.e (0x5AC01000 | ((u32) rd ((int) op & 15, T0, in.pcv) << 5) | (u32) HOST[rdg]); return true; }
		case K_NOP: return true;
		case K_EXT: return genExt (k);
		case K_REV: return genRev (k);
		case K_TLS: a.ldrw (HOST[op >> 12 & 15], XS, OFF (tls)); return true;
		case K_LDREX: return genLdrex (k);
		case K_STREX: return genStrex (k);
		case K_VDATA: return genVData (k);
		case K_VMOV:
		{
			const u32 s = (op >> 16 & 15) * 2 + (op >> 7 & 1);
			const int rt = (int) (op >> 12) & 15;
			if (op & 0x00100000) a.ldrw (HOST[rt], XS, voff (s)); else a.strw (HOST[rt], XS, voff (s));
			return true;
		}
		case K_VMOV2:
		{
			const u32 s = (op & 0x100) ? (op & 15) * 2 : (op & 15) * 2 + (op >> 5 & 1);
			const int rt = (int) (op >> 12) & 15, rt2 = (int) (op >> 16) & 15;
			if (s + 2 > 32) return false;
			if (op & 0x00100000) { a.ldrw (HOST[rt], XS, voff (s)); a.ldrw (HOST[rt2], XS, voff (s + 1)); }
			else { a.strw (HOST[rt], XS, voff (s)); a.strw (HOST[rt2], XS, voff (s + 1)); }
			return true;
		}
		case K_VMRS:
			a.ldrw (T0, XS, OFF (fpscr));
			a.lsri (T0, T0, 28); a.lsli (T0, T0, 28);
			a.msr (T0);
			return true;
		case K_VLS: return genVLS (k);
		case K_VLM: return genVLM (k);
		default: return false;
		}
	}
};

// ---- le cache ----------------------------------------------------------------------------------------------------------
static inline void pendInit (Jit *j) { j->nPend = 0; memset (j->pendHash, 0xFF, sizeof j->pendHash); }

static void flushCode (void *from, void *to)
{
	u64 ctr; __asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	const u64 dl = 4u << ((ctr >> 16) & 15), il = 4u << (ctr & 15), b = (u64) from, e = (u64) to;
	for (u64 x = b & ~(dl - 1); x < e; x += dl) __asm__ volatile ("dc cvau, %0" : : "r" (x) : "memory");
	__asm__ volatile ("dsb ish" : : : "memory");
	for (u64 x = b & ~(il - 1); x < e; x += il) __asm__ volatile ("ic ivau, %0" : : "r" (x) : "memory");
	__asm__ volatile ("dsb ish\n\tisb" : : : "memory");
}

static void flushAll (Jit *j)
{
	j->flushes++;
	memset (j->hash, 0, sizeof j->hash);
	for (int i = 0; i < (1 << FAST_BITS); i++) { j->fast[i].key = 0xFFFFFFFF; j->fast[i].code = 0; }
	memset (j->codePage, 0, 1 << 17);
	j->nBlocks = 0;
	j->pos = j->base;
	pendInit (j);
}

static inline u32 hashOf (u32 key) { return (key >> 1 ^ key >> 17) & (HASH - 1); }
static Block *find (Jit *j, u32 key)
{
	for (Block *b = j->hash[hashOf (key)]; b; b = b->next) if (b->key == key) return b;
	return 0;
}

void Gen::exitTo (u32 key, int k)
{
	refund (k);
	Block *t = find (j, key);
	if (t && !t->interp) { a.bTo (t->code); return; }
	a.movw (T0, key);
	if (!t && j->nPend < PEND_MAX)
	{
		Pend &p = j->pend[j->nPend];
		const u32 h = (key >> 1) & 4095;
		p.key = key; p.site = a.p; p.next = j->pendHash[h]; j->pendHash[h] = j->nPend++;
	}
	a.bTo (j->lookup);
}

static int decode (Arm11 &c, u32 pc, bool thumb, Ins *ins)
{
	Memory &mem = c.m->mem;
	int n = 0;
	u32 a = pc;
	while (n < MAX_INS)
	{
		Ins &in = ins[n];
		const u8 *p = mem.ptr (a);
		if (!p) break;
		in.addr = a; in.target = 0; in.link = in.toArm = in.toThumb = false;
		if (thumb)
		{
			const u32 t = *(const u16 *) p;
			in.raw = t; in.pcv = a + 4; in.ends = false; in.flagsDef = 0; in.cond = 14;
			if ((t >> 11) == 0x1E)
			{
				const u8 *q = mem.ptr (a + 2);
				const u32 t2 = q ? *(const u16 *) q : 0;
				if ((t2 >> 11) == 0x1F || (t2 >> 11) == 0x1D)
				{
					const u32 lr = a + 4 + (u32) ((s32) (t << 21) >> 9);
					in.op = 0; in.kind = K_THUMB_BL; in.flagsUse = 0; in.ends = true; in.link = true;
					in.addr = a; in.pcv = a + 4;
					if ((t2 >> 11) == 0x1F) in.target = (lr + ((t2 & 0x7FF) << 1)) & ~1u;
					else { in.target = (lr + ((t2 & 0x7FF) << 1)) & ~3u; in.toArm = true; }
					n++;
					break;
				}
				in.op = 0; interp (in); n++; break;
			}
			if ((t >> 12) == 0xD && ((t >> 8) & 15) < 14)
			{
				in.op = 0; in.kind = K_TBRANCH; in.cond = (int) (t >> 8) & 15; in.flagsUse = COND_USE[in.cond];
				in.target = a + 4 + (u32) ((s32) (s8) (t & 0xFF) * 2);
				n++; a += 2;
				continue;
			}
			if ((t >> 11) == 0x1C)
			{
				in.op = 0; in.kind = K_TBRANCH; in.flagsUse = 0; in.ends = true;
				in.target = a + 4 + (u32) ((s32) (t << 21) >> 20);
				n++;
				break;
			}
			const u32 arm = thumbToArm (t);
			if (!arm) { in.op = 0; interp (in); n++; break; }
			in.op = arm;
			if ((t >> 11) == 9 || ((t >> 12) == 0xA && !(t & 0x800))) in.pcv = (a + 4) & ~3u;
			classify (in);
			n++; a += 2;
			if (in.ends) break;
		}
		else
		{
			const u32 op = *(const u32 *) p;
			in.raw = op; in.op = op; in.pcv = a + 8;
			classify (in);
			n++; a += 4;
			if (in.ends) break;
		}
	}
	// le dernier traduit s'arrete devant ce qui est a l'interprete (sauf si c'est le seul)
	for (int k = 0; k < n; k++) if (ins[k].kind == K_INTERP) { n = k ? k : 1; break; }
	u8 live = FALL;
	for (int k = n - 1; k >= 0; k--)
	{
		Ins &in = ins[k];
		in.flagsNeed = in.flagsDef & live;
		in.liveAfter = live;
		if (in.cond < 14) live = (u8) (in.flagsUse | live);
		else live = (u8) (in.flagsUse | (live & ~in.flagsDef));
	}
	return n;
}

static Block *newBlock (Jit *j, u32 key, u32 n, u32 *code, bool itp)
{
	Block *b = &j->blocks[j->nBlocks++];
	b->key = key; b->n = n; b->code = code; b->interp = itp;
	Block **slot = &j->hash[hashOf (key)];
	b->next = *slot; *slot = b;
	return b;
}

static Block *compile (Arm11 &c, u32 key)
{
	Jit *j = c.jit;
	const u32 pc = key & ~1u;
	const bool thumb = key & 1;
	j->compiled++;
	if (j->nBlocks >= MAX_BLOCKS - 1 || j->size - j->pos < 32768) flushAll (j);
	static Gen g;
	g.c = &c; g.j = j; g.thumb = thumb; g.nStubs = 0;
	g.n = decode (c, pc, thumb, g.ins);
	if (!g.n) return newBlock (j, key, 1, 0, true);
	if (g.ins[0].kind == K_INTERP)
	{
		u32 h = (g.ins[0].raw * 2654435761u) >> 24;
		j->interpOps[h][0] = g.ins[0].raw; j->interpOps[h][1]++;
		return newBlock (j, key, 1, 0, true);
	}
	u32 *start = j->buf + j->pos;
	Asm &a = g.a;
	a.p = start;
	a.subxi (XTK, XTK, (u32) g.n);
	g.undo = a.tbnz (XTK, 63);
	bool ended = false;
	for (int k = 0; k < g.n && !ended; k++)
	{
		Ins &in = g.ins[k];
		u32 *skip = in.cond < 14 ? a.bcond (in.cond ^ 1) : 0;
		bool exits;
		if (in.kind == K_THUMB_BL)
		{
			a.movw (T0, (in.addr + 4) | 1); a.mov (HOST[14], T0);
			g.exitTo (in.target | (in.toArm ? 0 : 1), k);
			exits = true;
		}
		else if (g.genOne (k)) exits = in.ends;
		else
		{
			// pas traduit, malgre son genre : l'interprete
			g.refund (k - 1);
			a.movw (T0, 1); a.strw (T0, XS, OFF (forceStep));
			a.movw (T0, g.keyOf (in.addr));
			a.bTo (j->exitKey);
			exits = true;
		}
		if (skip) Asm::patch (skip, a.p);
		else if (exits) ended = true;
	}
	if (!ended)
	{
		const Ins &last = g.ins[g.n - 1];
		g.exitTo (g.keyOf (last.addr + (thumb ? 2 : 4)), g.n - 1);
	}
	g.emitStubs ();
	flushCode (start, a.p);
	j->pos = (u32) (a.p - j->buf);
	Block *b = newBlock (j, key, (u32) g.n, start, false);
	for (u32 p = pc >> PAGE_BITS; p <= (g.ins[g.n - 1].addr + 1) >> PAGE_BITS; p++) j->codePage[p >> 3] |= (u8) (1 << (p & 7));
	FastEntry &fe = j->fast[(key >> 1) & ((1 << FAST_BITS) - 1)];
	fe.key = key; fe.code = start;
	// les sorties qui attendaient ce bloc y vont tout droit
	const u32 h = (key >> 1) & 4095;
	int *link = &j->pendHash[h];
	while (*link >= 0)
	{
		Pend &p = j->pend[*link];
		if (p.key == key)
		{
			*p.site = 0x14000000 | ((u32) (start - p.site) & 0x3FFFFFF);
			flushCode (p.site, p.site + 1);
			*link = p.next;
		}
		else link = &p.next;
	}
	return b;
}

void Arm11::jitInit ()
{
	Jit *j = (Jit *) calloc (1, sizeof (Jit));
	void *code = mmap (0, CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (!j || code == MAP_FAILED) { free (j); noJit = true; return; }
	j->buf = (u32 *) code; j->size = CODE_SIZE / 4;
	j->blocks = (Block *) malloc (sizeof (Block) * MAX_BLOCKS);
	j->fast = (FastEntry *) malloc (sizeof (FastEntry) << FAST_BITS);
	j->pend = (Pend *) malloc (sizeof (Pend) * PEND_MAX);
	j->codePage = (u8 *) calloc (1, 1 << 17);
	jit = j;
	fast = j->fast;
	Asm a; a.p = j->buf;
	// l'entree : (Arm11 *, code, ticks)
	u32 *enter = a.p;
	a.e (0xA9B77BFD);						// stp x29, x30, [sp, #-144]!
	a.e (0xA90153F3);						// stp x19, x20, [sp, #16]
	a.e (0xA9025BF5);
	a.e (0xA90363F7);
	a.e (0xA9046BF9);
	a.e (0xA90573FB);
	a.movx64 (XS, 0);
	a.ldrx (XPT, XS, OFF (pages));
	a.movx64 (XTK, 2);
	a.e (0xD53B4403);						// mrs x3, fpcr
	a.e (0xF90033E3);						// str x3, [sp, #96]
	a.ldrw (T0, XS, OFF (fpscr));
	a.movw (T3, 0x07C00000);
	a.dp (0x0A000000, T0, T0, T3);
	a.e (0xD51B4400);						// msr fpcr, x0
	a.e (0xD51B443F);						// msr fpsr, xzr
	a.ldrw (T0, XS, OFF (cpsr));
	a.lsri (T0, T0, 28); a.lsli (T0, T0, 28);
	a.msr (T0);
	for (int g = 0; g < 15; g++) a.ldrw (HOST[g], XS, OFF (r) + (u32) g * 4);
	a.br (1);
	// la sortie : w0 = pc | Thumb
	j->exitKey = a.p;
	a.strw (T0, XS, OFF (key));
	for (int g = 0; g < 15; g++) a.strw (HOST[g], XS, OFF (r) + (u32) g * 4);
	a.mrs (T0);
	a.ldrw (T1, XS, OFF (cpsr));
	a.ubfm (T1, T1, 0, 27);
	a.dp (0x2A000000, T1, T1, T0);
	a.strw (T1, XS, OFF (cpsr));
	a.e (0xD53B4420);						// mrs x0, fpsr
	a.ldrw (T1, XS, OFF (fpscr));
	a.movw (T2, 0x9F);
	a.dp (0x0A000000, T0, T0, T2);
	a.dp (0x2A000000, T1, T1, T0);
	a.strw (T1, XS, OFF (fpscr));
	a.strx (XTK, XS, OFF (ticksOut));
	a.e (0xF94033E0);						// ldr x0, [sp, #96]
	a.e (0xD51B4400);						// msr fpcr, x0
	a.e (0xA94153F3);
	a.e (0xA9425BF5);
	a.e (0xA94363F7);
	a.e (0xA9446BF9);
	a.e (0xA94573FB);
	a.e (0xA8C97BFD);						// ldp x29, x30, [sp], #144
	a.e (0xD65F03C0);
	// le chainage : w0 = pc | Thumb, sans toucher aux drapeaux
	j->lookup = a.p;
	a.ldrx (T1, XS, OFF (fast));
	a.ubfx (T2, T0, 1, FAST_BITS);
	a.e (0x8B000000 | ((u32) T2 << 16) | (4u << 10) | ((u32) T1 << 5) | (u32) T1);	// add x1, x1, x2, lsl 4
	a.ldrw (T2, T1, 0);
	a.dp (0x4A000000, T2, T2, T0);
	Asm::patch (a.cbnz (T2), j->exitKey);
	a.ldrx (T1, T1, 8);
	a.br (T1);
	flushCode (j->buf, a.p);
	j->enter = (void (*) (Arm11 *, u32 *, s64)) (void *) enter;
	j->base = (u32) (a.p - j->buf);
	flushAll (j);
}

void Arm11::jitRun ()
{
	Jit *j = jit;
	Machine *M = m;
	while (M->ticksLeft > 0 && !halted)
	{
		if (noJit || forceStep || (fpscr & 0x00370000) || (cpsr & 0x200))
		{
			forceStep = 0;
			step ();
			continue;
		}
		const u32 k = r[15] | (cpsr >> 5 & 1);
		Block *b = find (j, k);
		if (!b) b = compile (*this, k);
		if (b->interp || (s64) b->n > M->ticksLeft) { step (); continue; }
		const s64 t0 = M->ticksLeft;
		blocksRun++;
		j->enter (this, b->code, t0);
		M->now += (u64) (t0 - ticksOut);
		M->ticksLeft = ticksOut;
		r[15] = key & ~1u;
		cpsr = (cpsr & ~0x20u) | (key & 1) << 5;
	}
}

void Arm11::invalidate (u32 va, u32 size)
{
	Jit *j = jit;
	if (!j || !size) return;
	for (u32 p = va >> PAGE_BITS; p <= (va + size - 1) >> PAGE_BITS; p++)
		if (j->codePage[p >> 3] >> (p & 7) & 1) { flushAll (j); return; }
}

Arm11::~Arm11 ()
{
	if (!jit) return;
	munmap (jit->buf, CODE_SIZE);
	free (jit->blocks); free (jit->fast); free (jit->pend); free (jit->codePage); free (jit);
}

void arm11Say (Cpu *cpu, FILE *o)
{
	Arm11 *c = (Arm11 *) cpu;
	if (!c->jit) return;
	fprintf (o, "arm11 : %llu blocs lances, %llu instructions a l'interprete, %llu blocs traduits, %llu vidages, %u mots de code\n", (unsigned long long) c->blocksRun, (unsigned long long) c->steps, (unsigned long long) c->jit->compiled, (unsigned long long) c->jit->flushes, c->jit->pos);
	for (int i = 0; i < 256; i++) if (c->jit->interpOps[i][1] > 8) fprintf (o, "  %08x x %u\n", c->jit->interpOps[i][0], c->jit->interpOps[i][1]);
}

}
