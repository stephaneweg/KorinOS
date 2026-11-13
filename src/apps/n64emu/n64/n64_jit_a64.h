//
// n64/n64_jit_a64.h -- le generateur AArch64 du recompilateur R4300 (n64_jit.cpp, le Pi) : les fonctions be_* que le
// decodage appelle. Inclus par n64_jit.cpp seulement. Les sequences sont celles de l'ancien recompilateur d'un seul
// tenant, a l'instruction pres (le code produit est le meme, octet pour octet : tools/emu/check_n64_jit.sh).
//
//   x19 = la machine (a partir de cycles), x20 = cycles, 15 registres MIPS vivent dans des registres A64 (table PIN) ;
//   x7 = RDRAM, x6 = pages de code, x5 = le registre de condition (branchement en cours) ; x0-x4, x16, x17 : de passage.
//
namespace n64 {

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
static inline void be_flush (const void *a, const void *b) { flushCode (a, b); }

// ---- l'assembleur -----------------------------------------------------------------------------------------------
static void aMOV32 (int d, u32 v);
enum { X0, X1, X2, X3, X4, X5, X6, X7, X8, X9, X10, X11, X12, X13, X14, X15, X16, X17, X18, X19, X20, X21, X22, X23, X24,
       X25, X26, X27, X28, X29, X30, XZR };
enum { EQ, NE, HS, LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE };
static inline u32 *W () { return (u32 *) J.cp; }
static inline void E (u32 w) { *(u32 *) J.cp = w; J.cp += 4; }

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
static inline void aB (const u8 *to) { E (0x14000000 | ((u32) ((const u32 *) to - W ()) & 0x3FFFFFF)); }
static inline void patchBcc (u32 *at, const u32 *to) { *at = (*at & 0xFF00001F) | ((u32) (to - at) & 0x7FFFF) << 5; }
static inline void patchB (u32 *at, const u32 *to) { *at = 0x14000000 | ((u32) (to - at) & 0x3FFFFFF); }
static inline void aADR (int d, const u8 *to)
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

// ---- les sorties ----------------------------------------------------------------------------------------------------
static void sideIf (int cc) { sideRef (J.cp); E (0x54000000 | cc); }
static void sideCbnz (int t) { sideRef (J.cp); E (0x35000000 | t); }
static void be_sideAlways () { sideRef (J.cp); E (0x54000000 | 14); }		// b.al

// un saut pose plus tot vers `to` : b.cond, cbz, cbnz (imm19) ou la place d'un `b` (0)
static void be_patchRel (u8 *site, const u8 *to)
{
	u32 *at = (u32 *) site;
	if (*at == 0) patchB (at, (const u32 *) to);
	else patchBcc (at, (const u32 *) to);
}
static const int LINK_BYTES = 4;
static inline u64 be_linkWord (const u8 *site) { return *(const u32 *) site; }
static inline void be_linkSet (u8 *site, const u8 *to) { patchB ((u32 *) site, (const u32 *) to); }
static inline void be_linkUndo (u8 *site, u64 word) { *(u32 *) site = (u32) word; }

static inline void be_addCycles (int n) { aADDI (X20, X20, (u32) n); }
static inline u8 *be_jumpPlaceholder () { u8 *s = J.cp; E (0); return s; }
static void be_chainStub (u32 target, u8 *site)
{
	aMOV32 (X0, target);
	aADR (X1, site);
	aB (J.exitChain);
}

struct EntryFix { u32 *add, *bcc; };
static EntryFix be_entryOpen ()
{
	EntryFix f;
	f.add = W ();
	E (0);						// add x0, x20, #worst
	aLDRX (X1, X19, OFF (nextEvent));
	aCMP (X0, X1);
	f.bcc = W ();
	E (0x54000000 | HI);
	return f;
}
static void be_entryClose (EntryFix f, u32 worst, u32 va)
{
	*f.add = 0x91000000 | worst << 10 | X20 << 5 | X0;
	patchBcc (f.bcc, W ());
	aMOV32 (X0, va);
	aB (J.exitNorm);
}
static void be_stub (u32 at, int sofar, bool slot, int slotKind, u32 taken, u32 fall)
{
	if (sofar) aADDI (X20, X20, (u32) sofar);
	aMOV32 (X0, at);
	if (slot)
	{
		if (slotKind == 0) aMOV32 (X1, taken);
		else if (slotKind == 2) aMOV (X1, X5, false);
		else
		{
			aMOV32 (X1, taken);
			aMOV32 (X2, fall);
			aCMPI (X5, 0, false);
			E (0x1A820000 | X1 << 5 | NE << 12 | X1);	// csel w1, w1, w2, ne
		}
		aB (J.exitSlot);
	}
	else aB (J.exitSide);
}
// cbnz / cbz w5 vers plus loin (be_patchRel)
static u8 *be_condBranch (bool ifNonZero) { u8 *s = J.cp; E ((ifNonZero ? 0x35000000 : 0x34000000) | X5); return s; }
static void be_jrExit (int sofar)
{
	if (sofar) aADDI (X20, X20, (u32) sofar);
	aMOV (X0, X5, false);
	aB (J.exitJr);
}

// ---- les registres ----------------------------------------------------------------------------------------------------
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
static void be_li (int g, s32 v) { int d = DST (g, X2); aMOVS (d, v); WR (g, d); }

// w1 = l'adresse physique de w0 ; hors RDRAM, mal alignee : sortie. mask : 0, 1, 3, 7 ; store : page de code ?
static void effAddr (int rs, s32 imm, int mask, bool store)
{
	int base = RD (rs, X0);
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

// ---- les instructions -------------------------------------------------------------------------------------------------
static void be_checkFp (bool fr)
{
	aLDRX (X0, X19, OFF (cp0) + 12 * 8);		// Status : CU1, et FR comme a la traduction
	aMOVZ (X1, 0x2400, 1, false);			// CU1 | FR
	aAND (X0, X0, X1, false);
	aMOVZ (X2, fr ? 0x2400 : 0x2000, 1, false);
	aCMP (X0, X2, false);
	sideIf (NE);
}
static void be_mfc1 (int ft, int fs) { int d = DST (ft, X2); aLDRW (X2, X19, fp32 (fs)); aSBFM (d, X2, 0, 31); WR (ft, d); }
static void be_dmfc1 (int ft, int fs) { aLDRX (X2, X19, fp64 (fs)); WR (ft, X2); }
static void be_mtc1 (int ft, int fs) { int v = RD (ft, X2); aSTRW (v, X19, fp32 (fs)); }
static void be_dmtc1 (int ft, int fs) { int v = RD (ft, X2); aSTRX (v, X19, fp64 (fs)); }
static void fload (int t, int n, bool dbl) { if (dbl) aLDRD (t, X19, fp64 (n)); else aLDRS (t, X19, fp32 (n)); }
static void fstore (int t, int n, bool dbl) { if (dbl) aSTRD (t, X19, fp64 (n)); else aSTRS (t, X19, fp32 (n)); }
static bool be_fpArith (int fn, bool dbl, int fd, int fs, int ft)
{
	static const u32 ops[4] = { F_ADD, F_SUB, F_MUL, F_DIV };
	fload (0, fs, dbl); fload (1, ft, dbl); aF2 (ops[fn], 0, 0, 1, dbl); fstore (0, fd, dbl);
	return true;
}
static bool be_fpSqrt (bool dbl, int fd, int fs) { fload (0, fs, dbl); aF1 (F_SQRT, 0, 0, dbl); fstore (0, fd, dbl); return true; }
static void be_fpMov (bool dbl, int fd, int fs)
{
	if (dbl) { aLDRX (X0, X19, fp64 (fs)); aSTRX (X0, X19, fp64 (fd)); }
	else { aLDRW (X0, X19, fp32 (fs)); aSTRW (X0, X19, fp32 (fd)); }
}
static void be_fpNeg (bool dbl, int fd, int fs) { fload (0, fs, dbl); aF1 (F_NEG, 0, 0, dbl); fstore (0, fd, dbl); }
static bool be_fpToW (int fn, bool dbl, int fd, int fs)
{
	if (fn == 0x24)						// CVT.W : l'arrondi de FCR31, au plus pres seulement
	{
		aLDRW (X0, X19, OFF (fcr31));
		aTSTK (X0, 0, 1);
		sideIf (NE);
	}
	fload (0, fs, dbl);
	u32 m = fn == 0x0D ? F_FRINTZ : fn == 0x0E ? F_FRINTP : fn == 0x0F ? F_FRINTM : F_FRINTN;
	if (m != F_FRINTZ) aF1 (m, 0, 0, dbl);
	E ((dbl ? 0x1E780000 : 0x1E380000) | 0 << 5 | X0);	// fcvtzs w0, (s|d)0
	aSTRW (X0, X19, fp32 (fd));
	return true;
}
static void be_cvtSD (int fd, int fs) { fload (0, fs, true); aF1 (F_TOS & ~0x400000u, 0, 0, true); aSTRS (0, X19, fp32 (fd)); }
static void be_cvtDS (int fd, int fs) { fload (0, fs, false); aF1 (F_TOD, 0, 0, false); aSTRD (0, X19, fp64 (fd)); }
static void be_fpCmp (int fn, bool dbl, int fs, int ft)
{
	fload (0, fs, dbl); fload (1, ft, dbl);
	aF2 (F_CMP, 0, 0, 1, dbl);
	sideIf (VS);
	int cc = (fn & 6) == 6 ? LS : (fn & 6) == 4 ? MI : EQ;
	if (fn & 6) aCSET (X2, cc); else aMOV (X2, XZR, false);
	aLDRW (X0, X19, OFF (fcr31));
	aMOV32 (X1, ~(0x3F000u | 1u << 23));
	aAND (X0, X0, X1, false);
	E (0x2A000000 | X2 << 16 | 23 << 10 | X0 << 5 | X0);	// orr w0, w0, w2, lsl 23
	aSTRW (X0, X19, OFF (fcr31));
}
static void be_cvtFromW (bool toDouble, int fd, int fs)
{
	aLDRW (X0, X19, fp32 (fs));
	E (!toDouble ? 0x1E220000 | X0 << 5 : 0x1E620000 | X0 << 5);	// scvtf (s|d)0, w0
	if (!toDouble) aSTRS (0, X19, fp32 (fd)); else aSTRD (0, X19, fp64 (fd));
}

static void be_shiftImm32 (int fn, int rd, int rt, int sa)
{
	int s = RD (rt, X0), d = DST (rd, X2);
	if (fn == 0) { if (sa) aLSLW (d, s, sa); else aMOV (d, s, false); }
	else if (fn == 2) aLSRW (d, s, sa);
	else { aASR (d, s, sa); }
	aSXTW (d, d);
	WR (rd, d);
}
static void be_shiftVar32 (int fn, int rd, int rt, int rs)
{
	int s = RD (rt, X0), n = RD (rs, X1), d = DST (rd, X2);
	if (fn == 7) { E (0x12001000 | n << 5 | X16); aSHV (2, d, s, X16, true); }	// (and w16, n, 31)
	else aSHV (fn == 4 ? 0 : 1, d, s, n, false);
	aSXTW (d, d);
	WR (rd, d);
}
static void be_fromHiLo (int rd, bool hi) { int d = DST (rd, X2); aLDRX (d, X19, hi ? OFF (hi) : OFF (lo)); WR (rd, d); }
static void be_toHiLo (int rs, bool hi) { int v = RD (rs, X0); aSTRX (v, X19, hi ? OFF (hi) : OFF (lo)); }
static void be_shiftVar64 (int fn, int rd, int rt, int rs)
{
	int s = RD (rt, X0), n = RD (rs, X1), d = DST (rd, X2);
	aSHV (fn == 0x14 ? 0 : fn == 0x16 ? 1 : 2, d, s, n, true);
	WR (rd, d);
}
static void be_mult (bool sgn, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1);
	if (sgn) aSMULL (X2, a, b); else aUMULL (X2, a, b);
	aSXTW (X3, X2);
	aSTRX (X3, X19, OFF (lo));
	aASR (X3, X2, 32);
	aSXTW (X3, X3);
	aSTRX (X3, X19, OFF (hi));
}
static void be_div (bool sgn, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1);
	aCMP (b, XZR, false);
	sideIf (EQ);
	if (sgn)
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
}
static void be_dmult (bool sgn, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1);
	aMUL (X2, a, b, true);
	if (sgn) aSMULH (X3, a, b); else aUMULH (X3, a, b);
	aSTRX (X2, X19, OFF (lo));
	aSTRX (X3, X19, OFF (hi));
}
static void be_addTrap32 (bool sub, int rd, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1);
	if (!sub) aADDS (X2, a, b, false); else aSUBS (X2, a, b, false);
	sideIf (VS);
	if (!rd) return;
	int d = DST (rd, X2);
	aSBFM (d, X2, 0, 31);
	WR (rd, d);
}
static void be_add32 (bool sub, int rd, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1), d = DST (rd, X2);
	if (!sub) aADD (d, a, b, false); else aSUB (d, a, b, false);
	aSBFM (d, d, 0, 31);
	WR (rd, d);
}
static void be_logic (int fn, int rd, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1), d = DST (rd, X2);
	switch (fn)
	{
	case 0x24: aAND (d, a, b); break;
	case 0x25: aORR (d, a, b); break;
	case 0x26: aEOR (d, a, b); break;
	default: aORR (d, a, b); aORN (d, XZR, d); break;
	}
	WR (rd, d);
}
static void be_slt (bool uns, int rd, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1), d = DST (rd, X2);
	aCMP (a, b);
	aCSET (d, !uns ? LT : LO, true);
	WR (rd, d);
}
static void be_addTrap64 (bool sub, int rd, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1);
	if (!sub) aADDS (X2, a, b, true); else aSUBS (X2, a, b, true);
	sideIf (VS);
	WR (rd, X2);
}
static void be_add64 (bool sub, int rd, int rs, int rt)
{
	int a = RD (rs, X0), b = RD (rt, X1), d = DST (rd, X2);
	if (!sub) aADD (d, a, b); else aSUB (d, a, b);
	WR (rd, d);
}
static void be_shiftImm64 (int kind, int rd, int rt, int n)
{
	int s = RD (rt, X0), d = DST (rd, X2);
	if (kind == 0) { if (n) aLSL (d, s, n); else aMOV (d, s); }
	else if (kind == 2) aLSR (d, s, n);
	else aASR (d, s, n);
	WR (rd, d);
}
static void be_addiTrap32 (int rt, int rs, s32 imm)
{
	int a = RD (rs, X0);
	aMOV32 (X1, (u32) imm);
	aADDS (X2, a, X1, false);
	sideIf (VS);
	if (!rt) return;
	int d = DST (rt, X2);
	aSBFM (d, X2, 0, 31);
	WR (rt, d);
}
static void be_addiu32 (int rt, int rs, s32 imm)
{
	int a = RD (rs, X0), d = DST (rt, X2);
	addImm32 (d, a, imm);
	aSBFM (d, d, 0, 31);
	WR (rt, d);
}
static void be_slti (bool uns, int rt, int rs, s32 imm)
{
	int a = RD (rs, X0), d = DST (rt, X2);
	aMOVS (X1, imm);
	aCMP (a, X1);
	aCSET (d, !uns ? LT : LO, true);
	WR (rt, d);
}
static void be_logicImm (int o, int rt, int rs, u32 uimm)
{
	int a = RD (rs, X0), d = DST (rt, X2);
	aMOVZ (X1, uimm, 0);
	if (o == 0x0C) aAND (d, a, X1);
	else if (o == 0x0D) aORR (d, a, X1);
	else aEOR (d, a, X1);
	WR (rt, d);
}
static void be_daddiTrap (int rt, int rs, s32 imm)
{
	int a = RD (rs, X0);
	aMOVS (X1, imm);
	aADDS (X2, a, X1, true);
	sideIf (VS);
	WR (rt, X2);
}
static void be_daddiu (int rt, int rs, s32 imm)
{
	int a = RD (rs, X0), d = DST (rt, X2);
	aMOVS (X1, imm);
	aADD (d, a, X1);
	WR (rt, d);
}
static void be_load (u32 o, int rt, int rs, s32 imm)
{
	effAddr (rs, imm, o == 0x20 || o == 0x24 ? 0 : o == 0x21 || o == 0x25 ? 1 : o == 0x37 ? 7 : 3, false);
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
}
static void be_store (u32 o, int rt, int rs, s32 imm)
{
	effAddr (rs, imm, o == 0x28 ? 0 : o == 0x29 ? 1 : o == 0x3F ? 7 : 3, true);
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
}
static void be_loadFp (bool w, int ft, int rs, s32 imm)
{
	effAddr (rs, imm, w ? 3 : 7, false);
	if (w) { aLSX (R_LDRW, X2, X7, X1); aSTRW (X2, X19, fp32 (ft)); }
	else { aLSX (R_LDRX, X2, X7, X1); aROR32 (X2, X2); aSTRX (X2, X19, fp64 (ft)); }
}
static void be_storeFp (bool w, int ft, int rs, s32 imm)
{
	effAddr (rs, imm, w ? 3 : 7, true);
	if (w) { aLDRW (X2, X19, fp32 (ft)); aLSX (R_STRW, X2, X7, X1); }
	else { aLDRX (X2, X19, fp64 (ft)); aROR32 (X2, X2); aLSX (R_STRX, X2, X7, X1); }
}

// ---- les branchements : w5 -------------------------------------------------------------------------------------------
static void be_condFp (bool ifTrue)
{
	aLDRW (X0, X19, OFF (fcr31));
	aUBFM (X5, X0, 23, 23, false);
	if (!ifTrue) aEORK (X5, X5, 0, 0);
}
static void be_condSign (int rs, bool geZero)
{
	int a = RD (rs, X0);
	aCMP (a, XZR);
	aCSET (X5, geZero ? GE : LT);
}
static void be_condEq (int rs, int rt, bool ne)
{
	int a = RD (rs, X0), b = RD (rt, X1);
	aCMP (a, b);
	aCSET (X5, !ne ? EQ : NE);
}
static void be_condGtz (int rs, bool gt)
{
	int a = RD (rs, X0);
	aCMP (a, XZR);
	aCSET (X5, !gt ? LE : GT);
}
static void be_jrTarget (int rs) { int a = RD (rs, X0); aMOV (X5, a, false); }

// ---- les trampolines, au debut de la memoire de code -------------------------------------------------------------------
static void be_genStubs ()
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
	u8 *leave = J.cp;
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
	u32 *f1 = W (); E (0x54000000 | HS);
	aTSTK (X1, 0, 1);
	u32 *f2 = W (); E (0x54000000 | NE);
	aLDRX (X2, X19, OFF (jitTab));
	aLSX (R_LDRW, X2, X2, X1);
	u32 *f3 = W (); E (0x34000000 | X2);
	aLDRX (X3, X19, OFF (jitRec));
	E (0x8B000000 | X2 << 16 | 3 << 10 | X3 << 5 | X3);	// add x3, x3, x2, lsl 3
	aLDRW (X16, X3, 0);
	aLDRW (X17, X3, 4);
	aCMP (X16, X0, false);
	u32 *f4 = W (); E (0x54000000 | NE);
	aLDRX (X3, X19, OFF (jitCode));
	aADD (X3, X3, X17);
	aBR (X3);
	patchBcc (f1, W ()); patchBcc (f2, W ()); patchBcc (f3, W ()); patchBcc (f4, W ());
	aB (J.exitNorm);
	J.start = (u32) (J.cp - J.code);
	flushCode (J.code, J.cp);
}

} // namespace n64
