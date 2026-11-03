//
// n64/n64_jit.cpp -- le recompilateur R4300 vers AArch64 : le code MIPS en RDRAM, traduit un bloc a la fois (d'une
// adresse a son branchement et sa case de retard, 64 instructions au plus), des blocs chaines entre eux.
//
//   * Aucun appel C dans un bloc : tout ce qui sort du chemin rapide (une adresse hors de kseg0 / kseg1 ou de la RDRAM,
//     un acces mal aligne, un debordement, COP0, une page de code ecrite, une instruction non traduite) est une SORTIE
//     avant l'instruction, avec l'etat exact de l'interprete : Machine::step () la fait, puis on revient aux blocs.
//   * Les cycles sont ceux de l'interprete, a l'instruction pres : un bloc ne part que s'il tient avant le prochain
//     evenement (sinon l'interprete avance). Les deux modes donnent donc la meme machine : tools/emu/ref_n64.cpp compare.
//   * x19 = la machine (a partir de cycles), x20 = cycles, 15 registres MIPS vivent dans des registres A64 (table PIN) ;
//     x7 = RDRAM, x6 = pages de code, x5 = le branchement en cours ; x0-x4, x16, x17 : de passage.
//   * Le code MIPS du TLB (hors kseg0 / kseg1) et celui de la cartouche restent a l'interprete.
//   * Une ecriture dans une page traduite (Machine::jitInvalidate : memoire, DMA) retire ses blocs et leurs chainages.
//
#include "n64/n64.h"

namespace n64 {

#if defined(__aarch64__)

#define OFF(f) ((int) (__builtin_offsetof (Machine, f) - __builtin_offsetof (Machine, cycles)))

enum { JP = 8, CODE_SIZE = 48 << 20, MAX_BLOCKS = 1 << 19, MAX_INS = 64, MAX_WORST = 3000, MAX_LINKS = 1 << 20 };

// ---- l'etat du recompilateur ------------------------------------------------------------------------------------
struct BlockRec { u32 va, entry; };			// lu par le code : w0 -> bloc
struct BlockInfo { u32 pa, worst; int in; };		// in : ses chainages entrants
struct Link { u32 site, word; int next; };		// un `b` pose en code + site, et ce qu'il remplace

struct Jit
{
	u32 *code; u32 *cp;
	BlockRec *rec; BlockInfo *info; int nblocks;
	Link *links; int nlinks;
	u32 *enter; u32 *exitChain, *exitNorm, *exitSide, *exitSlot, *exitJr;
	u32 start;					// premier mot libre apres les trampolines
	unsigned long compiled, flushes;
};
static Jit J;

static void flushCode (const void *a, const void *b)
{
	unsigned long ctr;
	__asm__ volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	unsigned long dl = 4ul << ((ctr >> 16) & 15), il = 4ul << (ctr & 15);
	unsigned long s = (unsigned long) a, e = (unsigned long) b;
	for (unsigned long x = s & ~(dl - 1); x < e; x += dl) __asm__ volatile ("dc cvau, %0" :: "r" (x) : "memory");
	__asm__ volatile ("dsb ish" ::: "memory");
	for (unsigned long x = s & ~(il - 1); x < e; x += il) __asm__ volatile ("ic ivau, %0" :: "r" (x) : "memory");
	__asm__ volatile ("dsb ish\n\tisb" ::: "memory");
}

// ---- l'assembleur -----------------------------------------------------------------------------------------------
static void aMOV32 (int d, u32 v);
enum { X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, X16, X17, X18, X19, X20, X21, X22, X23, X24,
       X25, X26, X27, X28, X29, X30, XZR };
enum { EQ, NE, HS, LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE };
static inline void E (u32 w) { *J.cp++ = w; }

// le registre A64 d'un registre MIPS, -1 : en memoire
static const signed char PIN[32] = { -1, -1, X23, X24, X25, X26, X9, X10, X11, X12, X13, X14, X15, -1, -1, -1,
				     X27, X28, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, X21, -1, X22 };

static inline void aADD (int d, int n, int m, bool sf = true) { E ((sf ? 0x8B000000 : 0x0B000000) | m << 16 | n << 5 | d); }
static inline void aSUB (int d, int n, int m, bool sf = true) { E ((sf ? 0xCB000000 : 0x4B000000) | m << 16 | n << 5 | d); }
static inline void aADDS (int d, int n, int m, bool sf) { E ((sf ? 0xAB000000 : 0x2B000000) | m << 16 | n << 5 | d); }
static inline void aSUBS (int d, int n, int m, bool sf) { E ((sf ? 0xEB000000 : 0x6B000000) | m << 16 | n << 5 | d); }
static inline void aAND (int d, int n, int m, bool sf = true) { E ((sf ? 0x8A000000 : 0x0A000000) | m << 16 | n << 5 | d); }
static inline void aORR (int d, int n, int m, bool sf = true) { E ((sf ? 0xAA000000 : 0x2A000000) | m << 16 | n << 5 | d); }
static inline void aEOR (int d, int n, int m, bool sf = true) { E ((sf ? 0xCA000000 : 0x4A000000) | m << 16 | n << 5 | d); }
static inline void aORN (int d, int n, int m) { E (0xAA200000 | m << 16 | n << 5 | d); }
static inline void aMOV (int d, int m, bool sf = true) { aORR (d, XZR, m, sf); }
static inline void aADDI (int d, int n, u32 imm, bool sf = true) { E ((sf ? 0x91000000 : 0x11000000) | imm << 10 | n << 5 | d); }
static inline void aSUBI (int d, int n, u32 imm, bool sf = true) { E ((sf ? 0xD1000000 : 0x51000000) | imm << 10 | n << 5 | d); }
// w d = w n + imm (n peut etre XZR)
static void addImm32 (int d, int n, s32 imm)
{
	if (n == XZR) { aMOV32 (d, (u32) imm); return; }
	if (imm >= 0 && imm < 4096) aADDI (d, n, (u32) imm, false);
	else if (imm < 0 && imm > -4096) aSUBI (d, n, (u32) -imm, false);
	else { aMOV32 (X1, (u32) imm); aADD (d, n, X1, false); }
}
static inline void aCMPI (int n, u32 imm, bool sf = true) { E ((sf ? 0xF100001F : 0x7100001F) | imm << 10 | n << 5); }
static inline void aCMP (int n, int m, bool sf = true) { aSUBS (XZR, n, m, sf); }
static inline void aCSET (int d, int cc, bool sf = false) { E ((sf ? 0x9A9F07E0 : 0x1A9F07E0) | (cc ^ 1) << 12 | d); }
static inline void aSBFM (int d, int n, int r, int s, bool sf = true) { E ((sf ? 0x93400000 : 0x13000000) | r << 16 | s << 10 | n << 5 | d); }
static inline void aUBFM (int d, int n, int r, int s, bool sf = true) { E ((sf ? 0xD3400000 : 0x53000000) | r << 16 | s << 10 | n << 5 | d); }
static inline void aSXTW (int d, int n) { aSBFM (d, n, 0, 31); }
static inline void aLSRW (int d, int n, int sa) { aUBFM (d, n, sa, 31, false); }
static inline void aLSLW (int d, int n, int sa) { aUBFM (d, n, (32 - sa) & 31, 31 - sa, false); }
static inline void aLSL (int d, int n, int sa) { aUBFM (d, n, (64 - sa) & 63, 63 - sa); }
static inline void aLSR (int d, int n, int sa) { aUBFM (d, n, sa, 63); }
static inline void aASR (int d, int n, int sa) { aSBFM (d, n, sa, 63); }
static inline void aSHV (int kind, int d, int n, int m, bool sf) { E ((sf ? 0x9AC02000 : 0x1AC02000) | kind << 10 | m << 16 | n << 5 | d); }	// 0 lsl, 1 lsr, 2 asr
static inline void aROR32 (int d, int n) { E (0x93C08000 | n << 16 | n << 5 | d); }			// ror x, x, 32
static inline void aMOVZ (int d, u32 v, int hw, bool sf = true) { E ((sf ? 0xD2800000 : 0x52800000) | hw << 21 | v << 5 | d); }
static inline void aMOVK (int d, u32 v, int hw, bool sf = true) { E ((sf ? 0xF2800000 : 0x72800000) | hw << 21 | v << 5 | d); }
static inline void aMOVN (int d, u32 v, int hw) { E (0x92800000 | hw << 21 | v << 5 | d); }
static void aMOV32 (int d, u32 v)
{
	aMOVZ (d, v & 0xFFFF, 0, false);
	if (v >> 16) aMOVK (d, v >> 16, 1, false);
}
// x d = la valeur 64 bits signee d'un mot
static void aMOVS (int d, s32 v)
{
	if (v >= 0) { aMOV32 (d, (u32) v); return; }
	aMOVN (d, (u32) ~v & 0xFFFF, 0);
	if (((u32) v >> 16) != 0xFFFF) aMOVK (d, (u32) v >> 16, 1);
}
static inline void aLDRX (int t, int n, int off) { E (0xF9400000 | (off / 8) << 10 | n << 5 | t); }
static inline void aSTRX (int t, int n, int off) { E (0xF9000000 | (off / 8) << 10 | n << 5 | t); }
static inline void aLDRW (int t, int n, int off) { E (0xB9400000 | (off / 4) << 10 | n << 5 | t); }
static inline void aSTRW (int t, int n, int off) { E (0xB9000000 | (off / 4) << 10 | n << 5 | t); }
static inline void aSTRB (int t, int n, int off) { E (0x39000000 | off << 10 | n << 5 | t); }
static inline void aLDRS (int t, int n, int off) { E (0xBD400000 | (off / 4) << 10 | n << 5 | t); }	// s
static inline void aSTRS (int t, int n, int off) { E (0xBD000000 | (off / 4) << 10 | n << 5 | t); }
static inline void aLDRD (int t, int n, int off) { E (0xFD400000 | (off / 8) << 10 | n << 5 | t); }	// d
static inline void aSTRD (int t, int n, int off) { E (0xFD000000 | (off / 8) << 10 | n << 5 | t); }
// [xn, xm] : base des codes, sans decalage (option LSL #0 : 011)
static inline void aLSX (u32 base, int t, int n, int m) { E (base | m << 16 | 0x6800 | n << 5 | t); }
enum { R_LDRB = 0x38600000, R_LDRSBX = 0x38A00000, R_LDRH = 0x78600000, R_LDRSHX = 0x78A00000, R_LDRW = 0xB8600000,
       R_LDRSW = 0xB8A00000, R_LDRX = 0xF8600000, R_STRB = 0x38200000, R_STRH = 0x78200000, R_STRW = 0xB8200000, R_STRX = 0xF8200000 };
static inline void aBR (int n) { E (0xD61F0000 | n << 5); }
static inline void aRET () { E (0xD65F03C0); }
static inline void aB (const u32 *to) { E (0x14000000 | ((u32) (to - J.cp) & 0x3FFFFFF)); }
static inline void aCBZ (int t, const u32 *to, bool sf = false) { E ((sf ? 0xB4000000 : 0x34000000) | ((u32) (to - J.cp) & 0x7FFFF) << 5 | t); }
static inline void patchBcc (u32 *at, const u32 *to) { *at = (*at & 0xFF00001F) | ((u32) (to - at) & 0x7FFFF) << 5; }
static inline void patchCb (u32 *at, const u32 *to) { patchBcc (at, to); }
static inline void patchB (u32 *at, const u32 *to) { *at = 0x14000000 | ((u32) (to - at) & 0x3FFFFFF); }
static inline void aADR (int d, const u32 *to)
{
	long o = (const char *) to - (const char *) J.cp;
	E (0x10000000 | ((u32) o & 3) << 29 | (((u32) o >> 2) & 0x7FFFF) << 5 | d);
}
static inline void aMUL (int d, int n, int m, bool sf) { E ((sf ? 0x9B007C00 : 0x1B007C00) | m << 16 | n << 5 | d); }
static inline void aMSUB (int d, int n, int m, int a, bool sf) { E ((sf ? 0x9B008000 : 0x1B008000) | m << 16 | a << 10 | n << 5 | d); }
static inline void aSMULL (int d, int n, int m) { E (0x9B207C00 | m << 16 | n << 5 | d); }
static inline void aUMULL (int d, int n, int m) { E (0x9BA07C00 | m << 16 | n << 5 | d); }
static inline void aSMULH (int d, int n, int m) { E (0x9B407C00 | m << 16 | n << 5 | d); }
static inline void aUMULH (int d, int n, int m) { E (0x9BC07C00 | m << 16 | n << 5 | d); }
static inline void aSDIV (int d, int n, int m, bool sf) { E ((sf ? 0x9AC00C00 : 0x1AC00C00) | m << 16 | n << 5 | d); }
static inline void aUDIV (int d, int n, int m, bool sf) { E ((sf ? 0x9AC00800 : 0x1AC00800) | m << 16 | n << 5 | d); }
// logiques immediats 32 bits : les masques utilises
static inline void aEORK (int d, int n, u32 immr, u32 imms) { E (0x52000000 | immr << 16 | imms << 10 | n << 5 | d); }
static inline void aANDK (int d, int n, u32 immr, u32 imms) { E (0x12000000 | immr << 16 | imms << 10 | n << 5 | d); }
static inline void aTSTK (int n, u32 immr, u32 imms) { E (0x7200001F | immr << 16 | imms << 10 | n << 5); }
// virgule flottante (dbl : d, sinon s)
static inline void aF2 (u32 op, int d, int n, int m, bool dbl) { E (op | (dbl ? 0x400000 : 0) | m << 16 | n << 5 | d); }
static inline void aF1 (u32 op, int d, int n, bool dbl) { E (op | (dbl ? 0x400000 : 0) | n << 5 | d); }
enum { F_ADD = 0x1E202800, F_SUB = 0x1E203800, F_MUL = 0x1E200800, F_DIV = 0x1E201800, F_SQRT = 0x1E21C000, F_ABS = 0x1E20C000,
       F_NEG = 0x1E214000, F_CMP = 0x1E202000, F_FRINTN = 0x1E244000, F_FRINTZ = 0x1E25C000, F_FRINTP = 0x1E24C000,
       F_FRINTM = 0x1E254000, F_TOD = 0x1E22C000 /* s -> d */, F_TOS = 0x1E624000 /* d -> s */ };

// ---- un bloc ------------------------------------------------------------------------------------------------------
enum { MAX_FIX = 256 };
struct Stub { u32 at; int sofar; bool slot; u32 *fix[24]; int nfix; };
struct Exit { u32 *site; u32 target; };
static Stub g_st[MAX_INS * 2]; static int g_nst;
static Exit g_ex[8]; static int g_nex;
static int g_cur;					// le stub de l'instruction en cours, -1 : pas encore
static u32 g_at; static int g_sofar; static bool g_slot, g_fpChecked, g_fr;
static int g_extra;					// cycles en plus de l'instruction
static int g_slotKind; static u32 g_taken, g_fall;	// 0 fixe (x5 sans objet), 1 condition w5, 2 cible w5

static void sideRef (u32 *where)
{
	if (g_cur < 0)
	{
		Stub &s = g_st[g_nst]; s.at = g_at; s.sofar = g_sofar; s.slot = g_slot; s.nfix = 0;
		g_cur = g_nst++;
	}
	Stub &s = g_st[g_cur];
	if (s.nfix < 24) s.fix[s.nfix++] = where;
}
// b.cc vers la sortie de cette instruction
static void sideIf (int cc) { sideRef (J.cp); E (0x54000000 | cc); }
static void sideCbnz (int t) { sideRef (J.cp); E (0x35000000 | t); }
static void sideAlways () { sideRef (J.cp); E (0x54000000 | 14); }		// b.al

static inline int RD (int g, int tmp)			// lire un registre MIPS -> le registre A64 qui le tient
{
	if (g == 0) return XZR;
	if (PIN[g] >= 0) return PIN[g];
	aLDRX (tmp, X19, OFF (r) + g * 8);
	return tmp;
}
static inline int DST (int g, int tmp) { return g && PIN[g] >= 0 ? PIN[g] : tmp; }
static inline void WR (int g, int h)			// apres : h tient la valeur du registre g
{
	if (g == 0) return;
	if (PIN[g] >= 0) { if (PIN[g] != h) aMOV (PIN[g], h); }
	else aSTRX (h, X19, OFF (r) + g * 8);
}

// w1 = l'adresse physique de w0 ; hors RDRAM, mal alignee : sortie. mask : 0, 1, 3, 7 ; store : page de code ?
static void effAddr (u32 op, int mask, bool store)
{
	int rs = (op >> 21) & 31, base = RD (rs, X0);
	s32 imm = (s16) (op & 0xFFFF);
	addImm32 (X0, base, imm);
	aEORK (X1, X0, 1, 0);				// 0x80000000
	aANDK (X1, X1, 2, 30);				// 0xDFFFFFFF
	E (0x7140001F | 0x800 << 10 | X1 << 5);		// cmp w1, #0x800, lsl 12
	sideIf (HS);
	if (mask) { aTSTK (X1, 0, mask == 1 ? 0 : mask == 3 ? 1 : 2); sideIf (NE); }
	if (store)
	{
		aLSRW (X2, X1, JP);
		aLSX (R_LDRB, X2, X6, X2);
		sideCbnz (X2);
	}
}

static bool checkFp ()
{
	if (g_fpChecked) return true;
	g_fpChecked = true;
	aLDRX (X0, X19, OFF (cp0) + 12 * 8);		// Status : CU1, et FR comme a la traduction
	aMOVZ (X1, 0x2400, 1, false);			// CU1 | FR
	aAND (X0, X0, X1, false);
	aMOVZ (X2, g_fr ? 0x2400 : 0x2000, 1, false);
	aCMP (X0, X2, false);
	sideIf (NE);
	return true;
}
static inline int fp32 (int n) { return g_fr ? OFF (fpr) + n * 8 : (n & 1) ? OFF (fpr) + (n & ~1) * 8 + 4 : OFF (fpr) + n * 8; }
static inline int fp64 (int n) { return OFF (fpr) + (g_fr ? n : (n & ~1)) * 8; }

static bool genCop1 (u32 op)
{
	u32 fmt = (op >> 21) & 31, ft = (op >> 16) & 31, fs = (op >> 11) & 31, fd = (op >> 6) & 31, fn = op & 63;
	switch (fmt)
	{
	case 0x00: { checkFp (); int d = DST (ft, X2); if (!ft) return true; aLDRW (X2, X19, fp32 (fs)); aSBFM (d, X2, 0, 31); WR (ft, d); return true; }	// MFC1
	case 0x01: { checkFp (); if (!ft) return true; aLDRX (X2, X19, fp64 (fs)); WR (ft, X2); return true; }	// DMFC1
	case 0x04: { checkFp (); int v = RD (ft, X2); aSTRW (v, X19, fp32 (fs)); return true; }			// MTC1
	case 0x05: { checkFp (); int v = RD (ft, X2); aSTRX (v, X19, fp64 (fs)); return true; }			// DMTC1
	default: break;
	}
	if (fmt == 0x10 || fmt == 0x11)
	{
		bool dbl = fmt == 0x11;
		checkFp ();
		auto load = [&] (int t, int n) { if (dbl) aLDRD (t, X19, fp64 (n)); else aLDRS (t, X19, fp32 (n)); };
		auto store = [&] (int t, int n) { if (dbl) aSTRD (t, X19, fp64 (n)); else aSTRS (t, X19, fp32 (n)); };
		switch (fn)
		{
		case 0x00: load (0, fs); load (1, ft); aF2 (F_ADD, 0, 0, 1, dbl); store (0, fd); return true;
		case 0x01: load (0, fs); load (1, ft); aF2 (F_SUB, 0, 0, 1, dbl); store (0, fd); return true;
		case 0x02: load (0, fs); load (1, ft); aF2 (F_MUL, 0, 0, 1, dbl); store (0, fd); g_extra = dbl ? 7 : 4; return true;
		case 0x03: load (0, fs); load (1, ft); aF2 (F_DIV, 0, 0, 1, dbl); store (0, fd); g_extra = dbl ? 114 : 56; return true;
		case 0x04: load (0, fs); aF1 (F_SQRT, 0, 0, dbl); store (0, fd); g_extra = dbl ? 114 : 56; return true;
		case 0x06:
			if (dbl) { aLDRX (X0, X19, fp64 (fs)); aSTRX (X0, X19, fp64 (fd)); }
			else { aLDRW (X0, X19, fp32 (fs)); aSTRW (X0, X19, fp32 (fd)); }
			return true;
		case 0x07: load (0, fs); aF1 (F_NEG, 0, 0, dbl); store (0, fd); return true;
		case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x24:		// vers un entier 32 bits
		{
			if (fn == 0x24)					// CVT.W : l'arrondi de FCR31, au plus pres seulement
			{
				aLDRW (X0, X19, OFF (fcr31));
				aTSTK (X0, 0, 1);
				sideIf (NE);
			}
			load (0, fs);
			u32 m = fn == 0x0D ? F_FRINTZ : fn == 0x0E ? F_FRINTP : fn == 0x0F ? F_FRINTM : F_FRINTN;
			if (m != F_FRINTZ) aF1 (m, 0, 0, dbl);
			E ((dbl ? 0x1E780000 : 0x1E380000) | 0 << 5 | X0);	// fcvtzs w0, (s|d)0
			aSTRW (X0, X19, fp32 (fd));
			return true;
		}
		case 0x20: if (!dbl) return false; load (0, fs); aF1 (F_TOS & ~0x400000u, 0, 0, true); aSTRS (0, X19, fp32 (fd)); return true;	// CVT.S.D
		case 0x21: if (dbl) return false; load (0, fs); aF1 (F_TOD, 0, 0, false); aSTRD (0, X19, fp64 (fd)); return true;		// CVT.D.S
		default: break;
		}
		if (fn >= 0x30)						// C.cond ; un NaN : l'interprete
		{
			load (0, fs); load (1, ft);
			aF2 (F_CMP, 0, 0, 1, dbl);
			sideIf (VS);
			int cc = (fn & 6) == 6 ? LS : (fn & 6) == 4 ? MI : EQ;
			if (fn & 6) aCSET (X2, cc); else aMOV (X2, XZR, false);
			aLDRW (X0, X19, OFF (fcr31));
			aMOV32 (X1, ~(0x3F000u | 1u << 23));
			aAND (X0, X0, X1, false);
			E (0x2A000000 | X2 << 16 | 23 << 10 | X0 << 5 | X0);	// orr w0, w0, w2, lsl 23
			aSTRW (X0, X19, OFF (fcr31));
			return true;
		}
		return false;
	}
	if (fmt == 0x14 && (fn == 0x20 || fn == 0x21))				// CVT.S.W, CVT.D.W
	{
		checkFp ();
		aLDRW (X0, X19, fp32 (fs));
		E (fn == 0x20 ? 0x1E220000 | X0 << 5 : 0x1E620000 | X0 << 5);	// scvtf (s|d)0, w0
		if (fn == 0x20) aSTRS (0, X19, fp32 (fd)); else aSTRD (0, X19, fp64 (fd));
		return true;
	}
	return false;
}

// une instruction sans branchement -> false : pas traduite
static bool gen (u32 op)
{
	u32 rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31, sa = (op >> 6) & 31;
	s32 imm = (s16) (op & 0xFFFF);
	u32 uimm = op & 0xFFFF;
	switch (op >> 26)
	{
	case 0x00:
		switch (op & 63)
		{
		case 0x00: case 0x02: case 0x03:			// SLL, SRL, SRA
		{
			if (!rd) return true;
			int s = RD (rt, X0), d = DST (rd, X2);
			if ((op & 63) == 0) { if (sa) aLSLW (d, s, (int) sa); else aMOV (d, s, false); }
			else if ((op & 63) == 2) aLSRW (d, s, (int) sa);
			else { aASR (d, s, (int) sa); }
			aSXTW (d, d);
			WR (rd, d);
			return true;
		}
		case 0x04: case 0x06: case 0x07:			// SLLV, SRLV, SRAV
		{
			if (!rd) return true;
			int s = RD (rt, X0), n = RD (rs, X1), d = DST (rd, X2);
			if ((op & 63) == 7) { E (0x12001000 | n << 5 | X16); aSHV (2, d, s, X16, true); }	// (and w16, n, 31)
			else aSHV ((op & 63) == 4 ? 0 : 1, d, s, n, false);
			aSXTW (d, d);
			WR (rd, d);
			return true;
		}
		case 0x0F: return true;					// SYNC
		case 0x10: { if (!rd) return true; int d = DST (rd, X2); aLDRX (d, X19, OFF (hi)); WR (rd, d); return true; }
		case 0x11: { int v = RD (rs, X0); aSTRX (v, X19, OFF (hi)); return true; }
		case 0x12: { if (!rd) return true; int d = DST (rd, X2); aLDRX (d, X19, OFF (lo)); WR (rd, d); return true; }
		case 0x13: { int v = RD (rs, X0); aSTRX (v, X19, OFF (lo)); return true; }
		case 0x14: case 0x16: case 0x17:			// DSLLV, DSRLV, DSRAV
		{
			if (!rd) return true;
			int s = RD (rt, X0), n = RD (rs, X1), d = DST (rd, X2);
			aSHV ((op & 63) == 0x14 ? 0 : (op & 63) == 0x16 ? 1 : 2, d, s, n, true);
			WR (rd, d);
			return true;
		}
		case 0x18: case 0x19:					// MULT, MULTU
		{
			int a = RD (rs, X0), b = RD (rt, X1);
			if ((op & 63) == 0x18) aSMULL (X2, a, b); else aUMULL (X2, a, b);
			aSXTW (X3, X2);
			aSTRX (X3, X19, OFF (lo));
			aASR (X3, X2, 32);
			aSXTW (X3, X3);
			aSTRX (X3, X19, OFF (hi));
			g_extra = 8;
			return true;
		}
		case 0x1A: case 0x1B:					// DIV, DIVU : par zero, debordement : l'interprete
		{
			int a = RD (rs, X0), b = RD (rt, X1);
			aCMP (b, XZR, false);
			sideIf (EQ);
			if ((op & 63) == 0x1A)
			{
				aMOVN (X2, 0, 0);
				aCMP (b, X2, false);
				sideIf (EQ);				// (b == -1 : assez rare)
				aSDIV (X2, a, b, false);
			}
			else aUDIV (X2, a, b, false);
			aMSUB (X3, X2, b, a, false);
			aSXTW (X2, X2);
			aSTRX (X2, X19, OFF (lo));
			aSXTW (X3, X3);
			aSTRX (X3, X19, OFF (hi));
			g_extra = 70;
			return true;
		}
		case 0x1C: case 0x1D:					// DMULT, DMULTU
		{
			int a = RD (rs, X0), b = RD (rt, X1);
			aMUL (X2, a, b, true);
			if ((op & 63) == 0x1C) aSMULH (X3, a, b); else aUMULH (X3, a, b);
			aSTRX (X2, X19, OFF (lo));
			aSTRX (X3, X19, OFF (hi));
			g_extra = 14;
			return true;
		}
		case 0x20: case 0x22:					// ADD, SUB (debordement : l'interprete)
		{
			int a = RD (rs, X0), b = RD (rt, X1);
			if ((op & 63) == 0x20) aADDS (X2, a, b, false); else aSUBS (X2, a, b, false);
			sideIf (VS);
			if (!rd) return true;
			int d = DST (rd, X2);
			aSBFM (d, X2, 0, 31);
			WR (rd, d);
			return true;
		}
		case 0x21: case 0x23:					// ADDU, SUBU
		{
			if (!rd) return true;
			int a = RD (rs, X0), b = RD (rt, X1), d = DST (rd, X2);
			if ((op & 63) == 0x21) aADD (d, a, b, false); else aSUB (d, a, b, false);
			aSBFM (d, d, 0, 31);
			WR (rd, d);
			return true;
		}
		case 0x24: case 0x25: case 0x26: case 0x27:
		{
			if (!rd) return true;
			int a = RD (rs, X0), b = RD (rt, X1), d = DST (rd, X2);
			switch (op & 63)
			{
			case 0x24: aAND (d, a, b); break;
			case 0x25: aORR (d, a, b); break;
			case 0x26: aEOR (d, a, b); break;
			default: aORR (d, a, b); aORN (d, XZR, d); break;
			}
			WR (rd, d);
			return true;
		}
		case 0x2A: case 0x2B:					// SLT, SLTU
		{
			if (!rd) return true;
			int a = RD (rs, X0), b = RD (rt, X1), d = DST (rd, X2);
			aCMP (a, b);
			aCSET (d, (op & 63) == 0x2A ? LT : LO, true);
			WR (rd, d);
			return true;
		}
		case 0x2C: case 0x2E:					// DADD, DSUB
		{
			int a = RD (rs, X0), b = RD (rt, X1);
			if ((op & 63) == 0x2C) aADDS (X2, a, b, true); else aSUBS (X2, a, b, true);
			sideIf (VS);
			WR (rd, X2);
			return true;
		}
		case 0x2D: case 0x2F:					// DADDU, DSUBU
		{
			if (!rd) return true;
			int a = RD (rs, X0), b = RD (rt, X1), d = DST (rd, X2);
			if ((op & 63) == 0x2D) aADD (d, a, b); else aSUB (d, a, b);
			WR (rd, d);
			return true;
		}
		case 0x38: case 0x3A: case 0x3B: case 0x3C: case 0x3E: case 0x3F:	// DSLL...
		{
			if (!rd) return true;
			int s = RD (rt, X0), d = DST (rd, X2);
			int n = (int) sa + ((op & 4) ? 32 : 0);
			if ((op & 3) == 0) { if (n) aLSL (d, s, n); else aMOV (d, s); }
			else if ((op & 3) == 2) aLSR (d, s, n);
			else aASR (d, s, n);
			WR (rd, d);
			return true;
		}
		default: return false;
		}
	case 0x08:							// ADDI
	{
		int a = RD (rs, X0);
		aMOV32 (X1, (u32) imm);
		aADDS (X2, a, X1, false);
		sideIf (VS);
		if (!rt) return true;
		int d = DST (rt, X2);
		aSBFM (d, X2, 0, 31);
		WR (rt, d);
		return true;
	}
	case 0x09:							// ADDIU
	{
		if (!rt) return true;
		int a = RD (rs, X0), d = DST (rt, X2);
		addImm32 (d, a, imm);
		aSBFM (d, d, 0, 31);
		WR (rt, d);
		return true;
	}
	case 0x0A: case 0x0B:						// SLTI, SLTIU
	{
		if (!rt) return true;
		int a = RD (rs, X0), d = DST (rt, X2);
		aMOVS (X1, imm);
		aCMP (a, X1);
		aCSET (d, (op >> 26) == 0x0A ? LT : LO, true);
		WR (rt, d);
		return true;
	}
	case 0x0C: case 0x0D: case 0x0E:				// ANDI, ORI, XORI
	{
		if (!rt) return true;
		int a = RD (rs, X0), d = DST (rt, X2);
		aMOVZ (X1, uimm, 0);
		if ((op >> 26) == 0x0C) aAND (d, a, X1);
		else if ((op >> 26) == 0x0D) aORR (d, a, X1);
		else aEOR (d, a, X1);
		WR (rt, d);
		return true;
	}
	case 0x0F:							// LUI
	{
		if (!rt) return true;
		int d = DST (rt, X2);
		aMOVS (d, (s32) (uimm << 16));
		WR (rt, d);
		return true;
	}
	case 0x11: return genCop1 (op);
	case 0x18:							// DADDI
	{
		int a = RD (rs, X0);
		aMOVS (X1, imm);
		aADDS (X2, a, X1, true);
		sideIf (VS);
		WR (rt, X2);
		return true;
	}
	case 0x19:							// DADDIU
	{
		if (!rt) return true;
		int a = RD (rs, X0), d = DST (rt, X2);
		aMOVS (X1, imm);
		aADD (d, a, X1);
		WR (rt, d);
		return true;
	}
	case 0x20: case 0x21: case 0x23: case 0x24: case 0x25: case 0x27: case 0x37:	// LB LH LW LBU LHU LWU LD
	{
		u32 o = op >> 26;
		effAddr (op, o == 0x20 || o == 0x24 ? 0 : o == 0x21 || o == 0x25 ? 1 : o == 0x37 ? 7 : 3, false);
		if (o == 0x20 || o == 0x24) aEORK (X1, X1, 0, 1);	// ^ 3
		if (o == 0x21 || o == 0x25) aEORK (X1, X1, 31, 0);	// ^ 2
		int d = rt ? DST (rt, X2) : X2;
		switch (o)
		{
		case 0x20: aLSX (R_LDRSBX, d, X7, X1); break;
		case 0x21: aLSX (R_LDRSHX, d, X7, X1); break;
		case 0x23: aLSX (R_LDRSW, d, X7, X1); break;
		case 0x24: aLSX (R_LDRB, d, X7, X1); break;
		case 0x25: aLSX (R_LDRH, d, X7, X1); break;
		case 0x27: aLSX (R_LDRW, d, X7, X1); break;
		default: aLSX (R_LDRX, d, X7, X1); aROR32 (d, d); break;
		}
		WR (rt, d);
		return true;
	}
	case 0x28: case 0x29: case 0x2B: case 0x3F:			// SB SH SW SD
	{
		u32 o = op >> 26;
		effAddr (op, o == 0x28 ? 0 : o == 0x29 ? 1 : o == 0x3F ? 7 : 3, true);
		if (o == 0x28) aEORK (X1, X1, 0, 1);
		if (o == 0x29) aEORK (X1, X1, 31, 0);
		int v = RD (rt, X3);
		switch (o)
		{
		case 0x28: aLSX (R_STRB, v, X7, X1); break;
		case 0x29: aLSX (R_STRH, v, X7, X1); break;
		case 0x2B: aLSX (R_STRW, v, X7, X1); break;
		default: E (0x93C08000 | v << 16 | v << 5 | X3); aLSX (R_STRX, X3, X7, X1); break;
		}
		return true;
	}
	case 0x2F: return true;						// CACHE
	case 0x31: case 0x35:						// LWC1, LDC1
	{
		checkFp ();
		bool w = (op >> 26) == 0x31;
		effAddr (op, w ? 3 : 7, false);
		if (w) { aLSX (R_LDRW, X2, X7, X1); aSTRW (X2, X19, fp32 (rt)); }
		else { aLSX (R_LDRX, X2, X7, X1); aROR32 (X2, X2); aSTRX (X2, X19, fp64 (rt)); }
		return true;
	}
	case 0x39: case 0x3D:						// SWC1, SDC1
	{
		checkFp ();
		bool w = (op >> 26) == 0x39;
		effAddr (op, w ? 3 : 7, true);
		if (w) { aLDRW (X2, X19, fp32 (rt)); aLSX (R_STRW, X2, X7, X1); }
		else { aLDRX (X2, X19, fp64 (rt)); aROR32 (X2, X2); aLSX (R_STRX, X2, X7, X1); }
		return true;
	}
	default: return false;
	}
}

// un branchement ? -> 1 relatif, 2 J / JAL, 3 JR / JALR
static int branchKind (u32 op)
{
	u32 o = op >> 26;
	if (o == 0x02 || o == 0x03) return 2;
	if (o == 0 && ((op & 63) == 8 || (op & 63) == 9)) return 3;
	if (o >= 0x04 && o <= 0x07) return 1;
	if (o >= 0x14 && o <= 0x17) return 1;
	if (o == 0x01)
	{
		u32 rt = (op >> 16) & 31;
		if (rt <= 3 || (rt >= 0x10 && rt <= 0x13)) return 1;
	}
	if (o == 0x11 && ((op >> 21) & 31) == 8) return 1;
	return 0;
}

static void exitTo (u32 target, int cyc)
{
	if (cyc) aADDI (X20, X20, (u32) cyc);
	Exit &e = g_ex[g_nex++];
	e.site = J.cp; e.target = target;
	E (0);						// (le `b` vers sa sortie, pose apres le corps du bloc)
}

// w5 = la condition du branchement relatif op
static void genCond (u32 op)
{
	u32 o = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31;
	if (o == 0x11)						// BC1
	{
		checkFp ();
		aLDRW (X0, X19, OFF (fcr31));
		aUBFM (X5, X0, 23, 23, false);
		if (!(rt & 1)) aEORK (X5, X5, 0, 0);
		return;
	}
	if (o == 0x01)
	{
		int a = RD (rs, X0);
		aCMP (a, XZR);
		aCSET (X5, (rt & 1) ? GE : LT);
		return;
	}
	u32 k = o & 3;
	if (k <= 1)
	{
		int a = RD (rs, X0), b = RD (rt, X1);
		aCMP (a, b);
		aCSET (X5, k == 0 ? EQ : NE);
		return;
	}
	int a = RD (rs, X0);
	aCMP (a, XZR);
	aCSET (X5, k == 2 ? LE : GT);
}

// ---- une traduction -------------------------------------------------------------------------------------------------
static void flushAll (Machine *m)
{
	J.cp = J.code + J.start;
	J.nblocks = 1; J.nlinks = 0; J.flushes++;
	for (u32 i = 0; i < RDRAM_SIZE / 4; i++) m->jitTab[i] = 0;
	for (u32 i = 0; i < RDRAM_SIZE >> JP; i++) m->jitPage[i] = 0;
}

// le bloc de va, 0 : a l'interprete
static int compile (Machine *m, u32 va, u32 pa)
{
	if ((J.cp - J.code) * 4 > CODE_SIZE - 0x10000 || J.nblocks >= MAX_BLOCKS - 2 || J.nlinks >= MAX_LINKS - 16) flushAll (m);
	int id = J.nblocks;
	u32 *start = J.cp;
	u32 *entryFix = 0;
	g_nst = 0; g_nex = 0; g_fpChecked = false;
	g_fr = (m->cp0[12] & (1 << 26)) != 0;
	// l'entree : le bloc tient-il avant le prochain evenement ?  (le pire : corrige a la fin)
	u32 *entryAdd = J.cp;
	E (0);						// add x0, x20, #worst
	aLDRX (X1, X19, OFF (nextEvent));
	aCMP (X0, X1);
	entryFix = J.cp;
	E (0x54000000 | HI);
	int sofar = 0, worst = 0, ins = 0;
	u32 at = va, p = pa;
	bool open = true;
	while (open)
	{
		if (ins >= MAX_INS || worst > MAX_WORST)
		{
			exitTo (at, sofar);
			break;
		}
		if (p + 8 > RDRAM_SIZE) { g_at = at; g_sofar = sofar; g_slot = false; g_cur = -1; sideAlways (); break; }
		u32 op = m->rdram[p >> 2];
		int bk = branchKind (op);
		g_at = at; g_sofar = sofar; g_slot = false; g_cur = -1; g_extra = 0;
		if (op == 0x10000000 + 0xFFFF)			// « b . » : le saut d'attente de l'interprete
		{
			sideAlways ();
			break;
		}
		if (!bk)
		{
			u32 *save = J.cp; int nst = g_nst; bool fc = g_fpChecked;
			if (op != 0 && !gen (op))
			{
				J.cp = save; g_nst = nst; g_fpChecked = fc; g_cur = -1;
				sideAlways ();			// pas traduite : l'interprete, puis un autre bloc
				break;
			}
			sofar += 2 + g_extra; worst += 2 + g_extra;
			at += 4; p += 4; ins++;
			continue;
		}
		// un branchement et sa case de retard
		u32 slotOp = m->rdram[(p + 4) >> 2];
		u32 *save = J.cp; int nst = g_nst; bool fc = g_fpChecked; int nex = g_nex;
		if (branchKind (slotOp)) { sideAlways (); break; }
		u32 target = 0;
		bool likely = false, link = false;
		s32 imm = (s16) (op & 0xFFFF);
		u32 o = op >> 26;
		if (bk == 1)
		{
			target = at + 4 + (u32) (imm << 2);
			if (o >= 0x14 && o <= 0x17) likely = true;
			if (o == 0x01) { u32 k = (op >> 16) & 31; likely = (k & 2) != 0; link = k >= 0x10; }
			if (o == 0x11) likely = ((op >> 16) & 2) != 0;
			genCond (op);
			g_slotKind = 1;
		}
		else if (bk == 2)
		{
			target = (at & 0xF0000000) | ((op & 0x3FFFFFF) << 2);
			link = o == 0x03;
			g_slotKind = 0;
		}
		else
		{
			int a = RD ((op >> 21) & 31, X0);
			aMOV (X5, a, false);
			g_slotKind = 2;
		}
		if (link)						// (le lien part avant la case, meme non pris)
		{
			int d = DST (31, X2);
			aMOVS (d, (s32) (at + 8));
			WR (31, d);
		}
		if (bk == 3 && (op & 63) == 9)				// JALR
		{
			u32 rd = (op >> 11) & 31;
			if (rd) { int d = DST (rd, X2); aMOVS (d, (s32) (at + 8)); WR (rd, d); }
		}
		g_taken = target; g_fall = at + 8;
		sofar += 2; worst += 2;
		u32 *likelyFix = 0;
		if (likely)						// non pris : la case est sautee
		{
			likelyFix = J.cp;
			E (0x35000000 | X5);				// cbnz w5, .pris
			exitTo (at + 8, sofar);
			patchCb (likelyFix, J.cp);
		}
		// la case
		g_at = at + 4; g_sofar = sofar; g_slot = true; g_cur = -1; g_extra = 0;
		if (slotOp != 0 && !gen (slotOp))
		{
			J.cp = save; g_nst = nst; g_fpChecked = fc; g_nex = nex;
			g_at = at; g_sofar = sofar - 2; g_slot = false; g_cur = -1;
			sideAlways ();
			break;
		}
		sofar += 2 + g_extra; worst += 2 + g_extra;
		if (bk == 2 || likely) exitTo (target, sofar);
		else if (bk == 1)
		{
			u32 *f = J.cp;
			E (0x34000000 | X5);				// cbz w5, .non_pris
			exitTo (target, sofar);
			patchCb (f, J.cp);
			exitTo (at + 8, sofar);
		}
		else
		{
			if (sofar) aADDI (X20, X20, (u32) sofar);
			aMOV (X0, X5, false);
			aB (J.exitJr);
		}
		open = false;
	}
	// les sorties
	*entryAdd = 0x91000000 | (u32) (worst ? worst : 2) << 10 | X20 << 5 | X0;
	patchBcc (entryFix, J.cp);
	aMOV32 (X0, va);
	aB (J.exitNorm);
	for (int i = 0; i < g_nst; i++)
	{
		Stub &s = g_st[i];
		for (int k = 0; k < s.nfix; k++) patchBcc (s.fix[k], J.cp);
		if (s.nfix == 0) continue;
		if (s.sofar) aADDI (X20, X20, (u32) s.sofar);
		aMOV32 (X0, s.at);
		if (s.slot)
		{
			if (g_slotKind == 0) aMOV32 (X1, g_taken);
			else if (g_slotKind == 2) aMOV (X1, X5, false);
			else
			{
				aMOV32 (X1, g_taken);
				aMOV32 (X2, g_fall);
				aCMPI (X5, 0, false);
				E (0x1A820000 | X1 << 5 | NE << 12 | X1);	// csel w1, w1, w2, ne
			}
			aB (J.exitSlot);
		}
		else aB (J.exitSide);
	}
	for (int i = 0; i < g_nex; i++)
	{
		Exit &e = g_ex[i];
		patchB (e.site, J.cp);
		aMOV32 (X0, e.target);
		aADR (X1, e.site);
		aB (J.exitChain);
	}
	flushCode (start, J.cp);
	J.rec[id].va = va; J.rec[id].entry = (u32) (start - J.code) * 4;
	J.info[id].pa = pa; J.info[id].worst = (u32) (worst ? worst : 2); J.info[id].in = -1;
	J.nblocks++;
	J.compiled++;
	m->jitTab[pa >> 2] = (u32) id;
	for (u32 q = pa >> JP; q <= ((p + 8) >> JP) && q < (RDRAM_SIZE >> JP); q++) m->jitPage[q] = 1;
	return id;
}

// les trampolines, au debut de la memoire de code
static void genStubs (Machine *)
{
	J.cp = J.code;
	static const signed char pins[] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 17, 29, 31 };
	J.enter = J.cp;						// (x0 = code, x1 = base)
	E (0xA9BA7BFD);						// stp x29, x30, [sp, -96]!
	E (0xA90153F3);						// stp x19, x20, [sp, 16]
	E (0xA9025BF5);						// stp x21, x22, [sp, 32]
	E (0xA90363F7);						// stp x23, x24, [sp, 48]
	E (0xA9046BF9);						// stp x25, x26, [sp, 64]
	E (0xA90573FB);						// stp x27, x28, [sp, 80]
	aMOV (X19, X1);
	for (unsigned i = 0; i < sizeof pins; i++) aLDRX (PIN[(int) pins[i]], X19, OFF (r) + pins[i] * 8);
	aLDRX (X20, X19, OFF (cycles));
	aLDRX (X7, X19, OFF (rdram));
	aLDRX (X6, X19, OFF (jitPage));
	aBR (X0);
	u32 *leave = J.cp;
	for (unsigned i = 0; i < sizeof pins; i++) aSTRX (PIN[(int) pins[i]], X19, OFF (r) + pins[i] * 8);
	aSTRX (X20, X19, OFF (cycles));
	E (0xA94153F3);						// ldp x19, x20, [sp, 16]
	E (0xA9425BF5);
	E (0xA94363F7);
	E (0xA9446BF9);
	E (0xA94573FB);
	E (0xA8C67BFD);						// ldp x29, x30, [sp], 96
	aRET ();
	J.exitChain = J.cp;					// w0 = cible, x1 = le site a chainer
	aSTRX (X1, X19, OFF (jitSite));
	J.exitNorm = J.cp;					// w0 = pc
	aSTRW (X0, X19, OFF (pc));
	aADDI (X2, X0, 4, false);
	aSTRW (X2, X19, OFF (npc));
	aSTRB (XZR, X19, OFF (nextDelay));
	aB (leave);
	J.exitSide = J.cp;					// w0 = pc : l'interprete fera cette instruction
	aSTRW (X0, X19, OFF (pc));
	aADDI (X2, X0, 4, false);
	aSTRW (X2, X19, OFF (npc));
	aSTRB (XZR, X19, OFF (nextDelay));
	aMOVZ (X2, 1, 0, false);
	aSTRW (X2, X19, OFF (jitStep));
	aB (leave);
	J.exitSlot = J.cp;					// w0 = la case, w1 = npc
	aSTRW (X0, X19, OFF (pc));
	aSTRW (X1, X19, OFF (npc));
	aMOVZ (X2, 1, 0, false);
	aSTRB (X2, X19, OFF (nextDelay));
	aSTRW (X2, X19, OFF (jitStep));
	aB (leave);
	J.exitJr = J.cp;					// w0 = cible : le bloc, s'il existe
	aEORK (X1, X0, 1, 0);
	aANDK (X1, X1, 2, 30);
	E (0x7140001F | 0x800 << 10 | X1 << 5);
	u32 *f1 = J.cp; E (0x54000000 | HS);
	aTSTK (X1, 0, 1);
	u32 *f2 = J.cp; E (0x54000000 | NE);
	aLDRX (X2, X19, OFF (jitTab));
	aLSX (R_LDRW, X2, X2, X1);
	u32 *f3 = J.cp; E (0x34000000 | X2);
	aLDRX (X3, X19, OFF (jitRec));
	E (0x8B000000 | X2 << 16 | 3 << 10 | X3 << 5 | X3);	// add x3, x3, x2, lsl 3
	aLDRW (X16, X3, 0);
	aLDRW (X17, X3, 4);
	aCMP (X16, X0, false);
	u32 *f4 = J.cp; E (0x54000000 | NE);
	aLDRX (X3, X19, OFF (jitCode));
	aADD (X3, X3, X17);
	aBR (X3);
	patchBcc (f1, J.cp); patchBcc (f2, J.cp); patchCb (f3, J.cp); patchBcc (f4, J.cp);
	aB (J.exitNorm);
	J.start = (u32) (J.cp - J.code);
	flushCode (J.code, J.cp);
}

// ---- la machine -------------------------------------------------------------------------------------------------------
bool Machine::jitEnable (void *(*codeAlloc) (unsigned long))
{
	J.code = (u32 *) codeAlloc (CODE_SIZE);
	if (!J.code) return false;
	J.rec = new BlockRec[MAX_BLOCKS];
	J.info = new BlockInfo[MAX_BLOCKS];
	J.links = new Link[MAX_LINKS];
	J.nblocks = 1; J.nlinks = 0;
	jitTab = new u32[RDRAM_SIZE / 4];
	jitPage = new u8[RDRAM_SIZE >> JP];
	jitRec = J.rec;
	jitCode = J.code;
	genStubs (this);
	J.cp = J.code + J.start;
	flushAll (this);
	useJit = true;
	return true;
}

// les blocs d'une page ecrite : retires, leurs chainages defaits
void Machine::jitDropPage (u32 page)
{
	jitPage[page] = 0;
	u32 from = page << (JP - 2), to = from + (1 << (JP - 2));
	from = from >= MAX_INS + 2 ? from - (MAX_INS + 2) : 0;		// (un bloc qui deborde sur cette page)
	for (u32 i = from; i < to && i < RDRAM_SIZE / 4; i++)
	{
		int id = (int) jitTab[i];
		if (!id) continue;
		jitTab[i] = 0;
		for (int l = J.info[id].in; l >= 0; l = J.links[l].next)
		{
			u32 *site = J.code + J.links[l].site;
			*site = J.links[l].word;
			flushCode (site, site + 1);
		}
		J.info[id].in = -1;
		J.rec[id].va = 0;
	}
}

void Machine::jitWrote (u32 pa, u32 len)
{
	if (!jitPage) return;
	u32 last = pa + len - 1;
	if (last >= RDRAM_SIZE) last = RDRAM_SIZE - 1;
	for (u32 q = pa >> JP; q <= last >> JP; q++) if (jitPage[q]) jitDropPage (q);
}
void Machine::jitFlush () { if (jitPage) flushAll (this); }
unsigned long Machine::jitStats (unsigned long *flushes) { if (flushes) *flushes = J.flushes; return J.compiled; }

#ifdef N64_JITSTAT
unsigned long g_stepOp[128], g_steps, g_enters, g_tlb, g_late, g_delay;
static inline void statStep (Machine *m)
{
	g_steps++;
	u32 va = m->pc, pa = va & 0x1FFFFFFF;
	if ((va >> 30) != 2) { g_tlb++; return; }
	if (pa >= RDRAM_SIZE) return;
	u32 op = m->rdram[pa >> 2];
	g_stepOp[(op >> 26) == 0 ? 64 + (op & 63) : (op >> 26)]++;
}
#define STAT(x) x
#else
#define STAT(x)
#endif

void Machine::jitRun ()
{
	typedef void (*Enter) (void *code, void *base);
	Enter enter = (Enter) (void *) J.enter;
	while (cycles < nextEvent)
	{
		if (excPending || nextDelay || jitStep) { STAT (if (nextDelay) g_delay++); STAT (statStep (this)); jitStep = 0; step (); continue; }
		u32 va = pc, pa = va & 0x1FFFFFFF;
		if ((va >> 30) != 2 || pa >= RDRAM_SIZE || (va & 3)) { STAT (statStep (this)); step (); continue; }
		int id = (int) jitTab[pa >> 2];
		if (id && J.rec[id].va != va) { jitTab[pa >> 2] = 0; id = 0; }	// (l'autre segment : a refaire)
		if (!id) id = compile (this, va, pa);
		if (cycles + J.info[id].worst > nextEvent) { STAT (g_late++); STAT (statStep (this)); step (); continue; }
		STAT (g_enters++);
		jitSite = 0;
		enter ((char *) J.code + J.rec[id].entry, &cycles);
		if (jitSite && !jitStep && cycles < nextEvent)			// chainer ce site a sa cible
		{
			u32 *site = (u32 *) jitSite;
			u32 tva = pc, tpa = tva & 0x1FFFFFFF;
			if ((tva >> 30) == 2 && tpa < RDRAM_SIZE && !(tva & 3))
			{
				int t = (int) jitTab[tpa >> 2];
				if (t && J.rec[t].va != tva) t = 0;
				if (!t && (char *) J.cp - (char *) J.code < CODE_SIZE - 0x20000)
				{
					u32 *mark = J.code + J.start;
					int fl = (int) J.flushes;
					t = compile (this, tva, tpa);
					if ((int) J.flushes != fl) { (void) mark; site = 0; }	// (tout a ete jete, ce site aussi)
				}
				if (t && site && J.nlinks < MAX_LINKS - 1)
				{
					Link &l = J.links[J.nlinks];
					l.site = (u32) (site - J.code); l.word = *site; l.next = J.info[t].in;
					J.info[t].in = J.nlinks++;
					patchB (site, (const u32 *) ((char *) J.code + J.rec[t].entry));
					flushCode (site, site + 1);
				}
			}
		}
	}
}

#else	// (le PC : l'interprete)
bool Machine::jitEnable (void *(*) (unsigned long)) { return false; }
void Machine::jitRun () { while (cycles < nextEvent) step (); }
void Machine::jitDropPage (u32) {}
void Machine::jitWrote (u32, u32) {}
void Machine::jitFlush () {}
unsigned long Machine::jitStats (unsigned long *) { return 0; }
#endif

} // namespace n64
