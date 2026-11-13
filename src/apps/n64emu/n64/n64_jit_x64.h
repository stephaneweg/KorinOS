//
// n64/n64_jit_x64.h -- le generateur x86-64 du recompilateur R4300 (n64_jit.cpp, le PC) : les memes fonctions be_* que
// n64_jit_a64.h, pour la meme machine, cycle pour cycle (tools/emu/check_n64_jit.sh compare l'interprete et les blocs).
//
//   rbx = la machine (a partir de cycles), r15 = cycles, rbp = RDRAM, r13 = pages de code, r14d = le registre de
//   condition (branchement en cours) ; rax, rcx, rdx, rsi : de passage. Les registres MIPS restent en memoire
//   ([rbx + r]) : x86-64 lit et ecrit la memoire dans l'instruction meme, et le PC a de la marge.
//   SSE2 seulement (pas de SSE4.1) : ROUND.W et FLOOR.W / CEIL.W restent a l'interprete.
//
#include <string.h>

namespace n64 {

static inline void be_flush (const void *, const void *) {}	// (x86 : le code ecrit est vu tel quel)

// ---- l'assembleur -----------------------------------------------------------------------------------------------
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
enum { C_O, C_NO, C_B, C_AE, C_E, C_NE, C_BE, C_A, C_S, C_NS, C_P, C_NP, C_L, C_GE, C_LE, C_G };
static inline void B1 (u32 b) { *J.cp++ = (u8) b; }
static inline void B4 (u32 v) { memcpy (J.cp, &v, 4); J.cp += 4; }
static inline void B8 (u64 v) { memcpy (J.cp, &v, 8); J.cp += 8; }

struct Mem { int base, index, scale; s32 disp; };	// [base + index << scale + disp] ; index -1 : sans
static inline Mem mB (int base, s32 disp) { Mem m = { base, -1, 0, disp }; return m; }
static inline Mem mBI (int base, int index, int scale = 0, s32 disp = 0) { Mem m = { base, index, scale, disp }; return m; }
static inline Mem mR (s32 off) { return mB (RBX, off); }		// un champ de la machine

static void rexOp (u32 pre, bool w, int r, int x, int b, u32 opc, int nopc)
{
	if (pre) B1 (pre);
	u32 rex = 0x40 | (w ? 8 : 0) | (r & 8 ? 4 : 0) | (x >= 0 && (x & 8) ? 2 : 0) | (b & 8 ? 1 : 0);
	if (rex != 0x40) B1 (rex);
	for (int i = nopc - 1; i >= 0; i--) B1 (opc >> (i * 8));
}
// opc reg, rm (deux registres)
static void opRR (u32 pre, bool w, u32 opc, int nopc, int reg, int rm)
{
	rexOp (pre, w, reg, -1, rm, opc, nopc);
	B1 (0xC0 | (reg & 7) << 3 | (rm & 7));
}
// opc reg, [m]
static void opRM (u32 pre, bool w, u32 opc, int nopc, int reg, Mem m)
{
	rexOp (pre, w, reg, m.index, m.base, opc, nopc);
	bool sib = m.index >= 0 || (m.base & 7) == 4;
	int mod = (m.disp == 0 && (m.base & 7) != 5) ? 0 : (m.disp >= -128 && m.disp < 128) ? 1 : 2;
	B1 (mod << 6 | (reg & 7) << 3 | (sib ? 4 : (m.base & 7)));
	if (sib) B1 (m.scale << 6 | (m.index >= 0 ? (m.index & 7) : 4) << 3 | (m.base & 7));
	if (mod == 1) B1 ((u32) m.disp & 0xFF);
	else if (mod == 2) B4 ((u32) m.disp);
}

// les instructions entieres
static inline void movRM (bool w, int r, Mem m) { opRM (0, w, 0x8B, 1, r, m); }		// r = [m]
static inline void movMR (bool w, Mem m, int r) { opRM (0, w, 0x89, 1, r, m); }		// [m] = r
static inline void movRR (bool w, int d, int s) { opRR (0, w, 0x89, 1, s, d); }
static inline void movsxdRM (int r, Mem m) { opRM (0, true, 0x63, 1, r, m); }
static inline void movsxdRR (int d, int s) { opRR (0, true, 0x63, 1, d, s); }
static inline void movMI8 (Mem m, u32 v) { opRM (0, false, 0xC6, 1, 0, m); B1 (v); }
static inline void movMI32 (Mem m, u32 v) { opRM (0, false, 0xC7, 1, 0, m); B4 (v); }
static void movRI32 (int r, u32 v) { if (r & 8) B1 (0x41); B1 (0xB8 + (r & 7)); B4 (v); }	// r32 = v (le haut a 0)
static void movRIS (int r, s32 v) { opRR (0, true, 0xC7, 1, 0, r); B4 ((u32) v); }		// r64 = v signe
enum { A_ADD = 0, A_OR = 1, A_AND = 4, A_SUB = 5, A_XOR = 6, A_CMP = 7 };
static const u32 aluRR[8] = { 0x01, 0x09, 0, 0, 0x21, 0x29, 0x31, 0x39 };	// rm op= reg
static inline void aluR (int k, bool w, int d, int s) { opRR (0, w, aluRR[k], 1, s, d); }
static inline void aluI (int k, bool w, int d, s32 v) { opRR (0, w, 0x81, 1, k, d); B4 ((u32) v); }
static inline void testRR (bool w, int a, int b) { opRR (0, w, 0x85, 1, b, a); }
enum { S_ROL = 0, S_ROR = 1, S_SHL = 4, S_SHR = 5, S_SAR = 7 };
static inline void shI (int k, bool w, int r, int n) { opRR (0, w, 0xC1, 1, k, r); B1 (n); }
static inline void shCL (int k, bool w, int r) { opRR (0, w, 0xD3, 1, k, r); }
static inline void setcc (int cc, int r8) { opRR (0, false, 0x0F90 + cc, 2, 0, r8); }	// al, cl, dl, bl
static inline void movzxB (int d, int s8) { opRR (0, false, 0x0FB6, 2, d, s8); }
static inline void jmpR (int r) { opRR (0, false, 0xFF, 1, 4, r); }
static inline void push (int r) { if (r & 8) B1 (0x41); B1 (0x50 + (r & 7)); }
static inline void pop (int r) { if (r & 8) B1 (0x41); B1 (0x58 + (r & 7)); }
static inline void jmpTo (const u8 *to) { B1 (0xE9); B4 ((u32) (to - (J.cp + 4))); }
static inline u8 *jcc (int cc) { u8 *s = J.cp; B1 (0x0F); B1 (0x80 + cc); B4 (0); return s; }

// la virgule flottante : xmm0 (et xmm1)
static inline void sseRM (u32 pre, u32 opc, int x, Mem m) { opRM (pre, false, opc, 2, x, m); }
enum { P_SS = 0xF3, P_SD = 0xF2 };
enum { O_MOVLD = 0x0F10, O_MOVST = 0x0F11, O_ADD = 0x0F58, O_MUL = 0x0F59, O_SUB = 0x0F5C, O_DIV = 0x0F5E, O_SQRT = 0x0F51,
       O_CVT = 0x0F5A, O_CVTSI = 0x0F2A, O_CVTT = 0x0F2C, O_CVTN = 0x0F2D, O_UCOMI = 0x0F2E };

// ---- les sorties ----------------------------------------------------------------------------------------------------
static void sideIf (int cc) { sideRef (J.cp); jcc (cc); }
static void be_sideAlways () { sideRef (J.cp); B1 (0xE9); B4 (0); }

// un saut pose plus tot (jcc rel32 ou jmp rel32) vers `to`
static void be_patchRel (u8 *site, const u8 *to)
{
	if (site[0] == 0x0F) { u32 r = (u32) (to - (site + 6)); memcpy (site + 2, &r, 4); }
	else { u32 r = (u32) (to - (site + 5)); memcpy (site + 1, &r, 4); }
}
static const int LINK_BYTES = 5;
static inline u64 be_linkWord (const u8 *site) { u64 w = 0; memcpy (&w, site, 5); return w; }
static inline void be_linkSet (u8 *site, const u8 *to) { site[0] = 0xE9; u32 r = (u32) (to - (site + 5)); memcpy (site + 1, &r, 4); }
static inline void be_linkUndo (u8 *site, u64 word) { memcpy (site, &word, 5); }

static inline void be_addCycles (int n) { aluI (A_ADD, true, R15, n); }
static inline u8 *be_jumpPlaceholder () { u8 *s = J.cp; B1 (0xE9); B4 (0); return s; }
static void be_chainStub (u32 target, u8 *site)
{
	movRI32 (RAX, target);
	B1 (0x48); B1 (0x8D); B1 (0x0D); B4 ((u32) (site - (J.cp + 4)));	// lea rcx, [rip + site]
	jmpTo (J.exitChain);
}

struct EntryFix { u8 *imm, *jcc; };
static EntryFix be_entryOpen ()
{
	EntryFix f;
	opRM (0, true, 0x8D, 1, RAX, mB (R15, 0x7FFFFFFF));	// lea rax, [r15 + worst] (disp32, corrige a la fin)
	f.imm = J.cp - 4;
	opRM (0, true, 0x3B, 1, RAX, mR (OFF (nextEvent)));	// cmp rax, [nextEvent]
	f.jcc = jcc (C_A);
	return f;
}
static void be_entryClose (EntryFix f, u32 worst, u32 va)
{
	memcpy (f.imm, &worst, 4);
	be_patchRel (f.jcc, J.cp);
	movRI32 (RAX, va);
	jmpTo (J.exitNorm);
}
static void be_stub (u32 at, int sofar, bool slot, int slotKind, u32 taken, u32 fall)
{
	if (sofar) be_addCycles (sofar);
	movRI32 (RAX, at);
	if (slot)
	{
		if (slotKind == 0) movRI32 (RCX, taken);
		else if (slotKind == 2) movRR (false, RCX, R14);
		else
		{
			movRI32 (RCX, taken);
			movRI32 (RDX, fall);
			testRR (false, R14, R14);
			opRR (0, false, 0x0F44, 2, RCX, RDX);		// cmove ecx, edx
		}
		jmpTo (J.exitSlot);
	}
	else jmpTo (J.exitSide);
}
static u8 *be_condBranch (bool ifNonZero) { testRR (false, R14, R14); return jcc (ifNonZero ? C_NE : C_E); }
static void be_jrExit (int sofar)
{
	if (sofar) be_addCycles (sofar);
	movRR (false, RAX, R14);
	jmpTo (J.exitJr);
}

// ---- les registres MIPS, en memoire ------------------------------------------------------------------------------------
static inline Mem gpr (int g) { return mR (OFF (r) + g * 8); }
static void ld64 (int x, int g) { if (g) movRM (true, x, gpr (g)); else aluR (A_XOR, false, x, x); }
static void ld32 (int x, int g) { if (g) movRM (false, x, gpr (g)); else aluR (A_XOR, false, x, x); }
static void st64 (int g, int x) { if (g) movMR (true, gpr (g), x); }
static void be_li (int g, s32 v) { movRIS (RAX, v); st64 (g, RAX); }

// ecx = l'adresse physique de rs + imm ; hors RDRAM, mal alignee : sortie. mask : 0, 1, 3, 7 ; store : page de code ?
static void effAddr (int rs, s32 imm, int mask, bool store)
{
	ld32 (RAX, rs);
	if (imm) aluI (A_ADD, false, RAX, imm);
	movRR (false, RCX, RAX);
	aluI (A_XOR, false, RCX, (s32) 0x80000000);
	aluI (A_AND, false, RCX, (s32) 0xDFFFFFFF);
	aluI (A_CMP, false, RCX, (s32) RDRAM_SIZE);
	sideIf (C_AE);
	if (mask) { B1 (0xF6); B1 (0xC1); B1 ((u32) mask); sideIf (C_NE); }	// test cl, mask
	if (store)
	{
		movRR (false, RDX, RCX);
		shI (S_SHR, false, RDX, JP);
		opRM (0, false, 0x80, 1, 7, mBI (R13, RDX)); B1 (0);	// cmp byte [r13 + rdx], 0
		sideIf (C_NE);
	}
}
static inline Mem ram () { return mBI (RBP, RCX); }		// [rdram + ecx]

// ---- les instructions -------------------------------------------------------------------------------------------------
static void be_checkFp (bool fr)
{
	movRM (true, RAX, mR (OFF (cp0) + 12 * 8));		// Status : CU1, et FR comme a la traduction
	aluI (A_AND, false, RAX, 0x24000000);			// CU1 | FR
	aluI (A_CMP, false, RAX, fr ? 0x24000000 : 0x20000000);
	sideIf (C_NE);
}
static void be_mfc1 (int ft, int fs) { movsxdRM (RAX, mR (fp32 (fs))); st64 (ft, RAX); }
static void be_dmfc1 (int ft, int fs) { movRM (true, RAX, mR (fp64 (fs))); st64 (ft, RAX); }
static void be_mtc1 (int ft, int fs) { ld32 (RAX, ft); movMR (false, mR (fp32 (fs)), RAX); }
static void be_dmtc1 (int ft, int fs) { ld64 (RAX, ft); movMR (true, mR (fp64 (fs)), RAX); }
static inline Mem fpm (int n, bool dbl) { return mR (dbl ? fp64 (n) : fp32 (n)); }
static bool be_fpArith (int fn, bool dbl, int fd, int fs, int ft)
{
	static const u32 ops[4] = { O_ADD, O_SUB, O_MUL, O_DIV };
	u32 p = dbl ? P_SD : P_SS;
	sseRM (p, O_MOVLD, 0, fpm (fs, dbl));
	sseRM (p, ops[fn], 0, fpm (ft, dbl));
	sseRM (p, O_MOVST, 0, fpm (fd, dbl));
	return true;
}
static bool be_fpSqrt (bool dbl, int fd, int fs)
{
	u32 p = dbl ? P_SD : P_SS;
	sseRM (p, O_SQRT, 0, fpm (fs, dbl));
	sseRM (p, O_MOVST, 0, fpm (fd, dbl));
	return true;
}
static void be_fpMov (bool dbl, int fd, int fs) { movRM (dbl, RAX, fpm (fs, dbl)); movMR (dbl, fpm (fd, dbl), RAX); }
static void be_fpNeg (bool dbl, int fd, int fs)
{
	movRM (dbl, RAX, fpm (fs, dbl));
	if (dbl) { opRR (0, true, 0x0FBA, 2, 7, RAX); B1 (63); }	// btc rax, 63
	else aluI (A_XOR, false, RAX, (s32) 0x80000000);
	movMR (dbl, fpm (fd, dbl), RAX);
}
static bool be_fpToW (int fn, bool dbl, int fd, int fs)
{
	if (fn == 0x0E || fn == 0x0F) return false;		// CEIL, FLOOR : l'interprete (pas de SSE4.1)
	if (fn == 0x24)						// CVT.W : l'arrondi de FCR31, au plus pres seulement
	{
		movRM (false, RAX, mR (OFF (fcr31)));
		B1 (0xA8); B1 (3);				// test al, 3
		sideIf (C_NE);
	}
	opRM (dbl ? P_SD : P_SS, false, fn == 0x0D ? O_CVTT : O_CVTN, 2, RAX, fpm (fs, dbl));	// cvt(t)s(s|d)2si eax
	movMR (false, mR (fp32 (fd)), RAX);
	return true;
}
static void be_cvtSD (int fd, int fs) { sseRM (P_SD, O_CVT, 0, mR (fp64 (fs))); sseRM (P_SS, O_MOVST, 0, mR (fp32 (fd))); }
static void be_cvtDS (int fd, int fs) { sseRM (P_SS, O_CVT, 0, mR (fp32 (fs))); sseRM (P_SD, O_MOVST, 0, mR (fp64 (fd))); }
static void be_fpCmp (int fn, bool dbl, int fs, int ft)
{
	sseRM (dbl ? P_SD : P_SS, O_MOVLD, 0, fpm (fs, dbl));
	opRM (dbl ? 0x66 : 0, false, O_UCOMI, 2, 0, fpm (ft, dbl));	// ucomis(s|d) xmm0, [ft]
	sideIf (C_P);						// un NaN : l'interprete
	if (fn & 6) { setcc ((fn & 6) == 6 ? C_BE : (fn & 6) == 4 ? C_B : C_E, RDX); movzxB (RDX, RDX); }
	else aluR (A_XOR, false, RDX, RDX);
	movRM (false, RAX, mR (OFF (fcr31)));
	aluI (A_AND, false, RAX, (s32) ~(0x3F000u | 1u << 23));
	shI (S_SHL, false, RDX, 23);
	aluR (A_OR, false, RAX, RDX);
	movMR (false, mR (OFF (fcr31)), RAX);
}
static void be_cvtFromW (bool toDouble, int fd, int fs)
{
	opRM (toDouble ? P_SD : P_SS, false, O_CVTSI, 2, 0, mR (fp32 (fs)));	// cvtsi2s(s|d) xmm0, dword [fs]
	sseRM (toDouble ? P_SD : P_SS, O_MOVST, 0, toDouble ? mR (fp64 (fd)) : mR (fp32 (fd)));
}

static void be_shiftImm32 (int fn, int rd, int rt, int sa)
{
	if (fn == 3) { ld64 (RAX, rt); if (sa) shI (S_SAR, true, RAX, sa); }
	else { ld32 (RAX, rt); if (sa) shI (fn == 0 ? S_SHL : S_SHR, false, RAX, sa); }
	movsxdRR (RAX, RAX);
	st64 (rd, RAX);
}
static void be_shiftVar32 (int fn, int rd, int rt, int rs)
{
	ld32 (RCX, rs);
	if (fn == 7) { ld64 (RAX, rt); aluI (A_AND, false, RCX, 31); shCL (S_SAR, true, RAX); }
	else { ld32 (RAX, rt); shCL (fn == 4 ? S_SHL : S_SHR, false, RAX); }
	movsxdRR (RAX, RAX);
	st64 (rd, RAX);
}
static void be_fromHiLo (int rd, bool hi) { movRM (true, RAX, mR (hi ? OFF (hi) : OFF (lo))); st64 (rd, RAX); }
static void be_toHiLo (int rs, bool hi) { ld64 (RAX, rs); movMR (true, mR (hi ? OFF (hi) : OFF (lo)), RAX); }
static void be_shiftVar64 (int fn, int rd, int rt, int rs)
{
	ld64 (RAX, rt); ld32 (RCX, rs);
	shCL (fn == 0x14 ? S_SHL : fn == 0x16 ? S_SHR : S_SAR, true, RAX);
	st64 (rd, RAX);
}
static void be_mult (bool sgn, int rs, int rt)
{
	if (sgn) { ld64 (RAX, rs); movsxdRR (RAX, RAX); ld64 (RCX, rt); movsxdRR (RCX, RCX); }
	else { ld32 (RAX, rs); ld32 (RCX, rt); }
	opRR (0, true, 0x0FAF, 2, RAX, RCX);			// imul rax, rcx
	movsxdRR (RDX, RAX);
	movMR (true, mR (OFF (lo)), RDX);
	shI (S_SAR, true, RAX, 32);
	movsxdRR (RAX, RAX);
	movMR (true, mR (OFF (hi)), RAX);
}
static void be_div (bool sgn, int rs, int rt)
{
	ld32 (RAX, rs); ld32 (RCX, rt);
	testRR (false, RCX, RCX);
	sideIf (C_E);
	if (sgn)
	{
		aluI (A_CMP, false, RCX, -1);
		sideIf (C_E);					// (b == -1 : assez rare)
		B1 (0x99);					// cdq
		opRR (0, false, 0xF7, 1, 7, RCX);		// idiv ecx
	}
	else { aluR (A_XOR, false, RDX, RDX); opRR (0, false, 0xF7, 1, 6, RCX); }	// div ecx
	movsxdRR (RAX, RAX);
	movMR (true, mR (OFF (lo)), RAX);
	movsxdRR (RDX, RDX);
	movMR (true, mR (OFF (hi)), RDX);
}
static void be_dmult (bool sgn, int rs, int rt)
{
	ld64 (RAX, rs); ld64 (RCX, rt);
	opRR (0, true, 0xF7, 1, sgn ? 5 : 4, RCX);		// imul / mul rcx : rdx:rax
	movMR (true, mR (OFF (lo)), RAX);
	movMR (true, mR (OFF (hi)), RDX);
}
static void be_addTrap32 (bool sub, int rd, int rs, int rt)
{
	ld32 (RAX, rs); ld32 (RCX, rt);
	aluR (sub ? A_SUB : A_ADD, false, RAX, RCX);
	sideIf (C_O);
	if (!rd) return;
	movsxdRR (RAX, RAX);
	st64 (rd, RAX);
}
static void be_add32 (bool sub, int rd, int rs, int rt)
{
	ld32 (RAX, rs); ld32 (RCX, rt);
	aluR (sub ? A_SUB : A_ADD, false, RAX, RCX);
	movsxdRR (RAX, RAX);
	st64 (rd, RAX);
}
static void be_logic (int fn, int rd, int rs, int rt)
{
	ld64 (RAX, rs); ld64 (RCX, rt);
	aluR (fn == 0x24 ? A_AND : fn == 0x26 ? A_XOR : A_OR, true, RAX, RCX);
	if (fn == 0x27) opRR (0, true, 0xF7, 1, 2, RAX);	// not rax
	st64 (rd, RAX);
}
static void be_slt (bool uns, int rd, int rs, int rt)
{
	ld64 (RAX, rs); ld64 (RCX, rt);
	aluR (A_CMP, true, RAX, RCX);
	setcc (uns ? C_B : C_L, RAX);
	movzxB (RAX, RAX);
	st64 (rd, RAX);
}
static void be_addTrap64 (bool sub, int rd, int rs, int rt)
{
	ld64 (RAX, rs); ld64 (RCX, rt);
	aluR (sub ? A_SUB : A_ADD, true, RAX, RCX);
	sideIf (C_O);
	st64 (rd, RAX);
}
static void be_add64 (bool sub, int rd, int rs, int rt)
{
	ld64 (RAX, rs); ld64 (RCX, rt);
	aluR (sub ? A_SUB : A_ADD, true, RAX, RCX);
	st64 (rd, RAX);
}
static void be_shiftImm64 (int kind, int rd, int rt, int n)
{
	ld64 (RAX, rt);
	if (n) shI (kind == 0 ? S_SHL : kind == 2 ? S_SHR : S_SAR, true, RAX, n);
	st64 (rd, RAX);
}
static void be_addiTrap32 (int rt, int rs, s32 imm)
{
	ld32 (RAX, rs);
	aluI (A_ADD, false, RAX, imm);
	sideIf (C_O);
	if (!rt) return;
	movsxdRR (RAX, RAX);
	st64 (rt, RAX);
}
static void be_addiu32 (int rt, int rs, s32 imm)
{
	ld32 (RAX, rs);
	if (imm) aluI (A_ADD, false, RAX, imm);
	movsxdRR (RAX, RAX);
	st64 (rt, RAX);
}
static void be_slti (bool uns, int rt, int rs, s32 imm)
{
	ld64 (RAX, rs);
	aluI (A_CMP, true, RAX, imm);
	setcc (uns ? C_B : C_L, RAX);
	movzxB (RAX, RAX);
	st64 (rt, RAX);
}
static void be_logicImm (int o, int rt, int rs, u32 uimm)
{
	ld64 (RAX, rs);
	aluI (o == 0x0C ? A_AND : o == 0x0D ? A_OR : A_XOR, true, RAX, (s32) uimm);
	st64 (rt, RAX);
}
static void be_daddiTrap (int rt, int rs, s32 imm)
{
	ld64 (RAX, rs);
	aluI (A_ADD, true, RAX, imm);
	sideIf (C_O);
	st64 (rt, RAX);
}
static void be_daddiu (int rt, int rs, s32 imm)
{
	ld64 (RAX, rs);
	aluI (A_ADD, true, RAX, imm);
	st64 (rt, RAX);
}
static void be_load (u32 o, int rt, int rs, s32 imm)
{
	effAddr (rs, imm, o == 0x20 || o == 0x24 ? 0 : o == 0x21 || o == 0x25 ? 1 : o == 0x37 ? 7 : 3, false);
	if (o == 0x20 || o == 0x24) aluI (A_XOR, false, RCX, 3);
	if (o == 0x21 || o == 0x25) aluI (A_XOR, false, RCX, 2);
	switch (o)
	{
	case 0x20: opRM (0, true, 0x0FBE, 2, RAX, ram ()); break;	// movsx rax, byte
	case 0x21: opRM (0, true, 0x0FBF, 2, RAX, ram ()); break;	// movsx rax, word
	case 0x23: movsxdRM (RAX, ram ()); break;
	case 0x24: opRM (0, false, 0x0FB6, 2, RAX, ram ()); break;	// movzx eax, byte
	case 0x25: opRM (0, false, 0x0FB7, 2, RAX, ram ()); break;	// movzx eax, word
	case 0x27: movRM (false, RAX, ram ()); break;
	default: movRM (true, RAX, ram ()); shI (S_ROR, true, RAX, 32); break;
	}
	st64 (rt, RAX);
}
static void be_store (u32 o, int rt, int rs, s32 imm)
{
	effAddr (rs, imm, o == 0x28 ? 0 : o == 0x29 ? 1 : o == 0x3F ? 7 : 3, true);
	if (o == 0x28) aluI (A_XOR, false, RCX, 3);
	if (o == 0x29) aluI (A_XOR, false, RCX, 2);
	ld64 (RAX, rt);
	switch (o)
	{
	case 0x28: opRM (0, false, 0x88, 1, RAX, ram ()); break;	// mov byte [], al
	case 0x29: opRM (0x66, false, 0x89, 1, RAX, ram ()); break;	// mov word [], ax
	case 0x2B: movMR (false, ram (), RAX); break;
	default: shI (S_ROR, true, RAX, 32); movMR (true, ram (), RAX); break;
	}
}
static void be_loadFp (bool w, int ft, int rs, s32 imm)
{
	effAddr (rs, imm, w ? 3 : 7, false);
	if (w) { movRM (false, RAX, ram ()); movMR (false, mR (fp32 (ft)), RAX); }
	else { movRM (true, RAX, ram ()); shI (S_ROR, true, RAX, 32); movMR (true, mR (fp64 (ft)), RAX); }
}
static void be_storeFp (bool w, int ft, int rs, s32 imm)
{
	effAddr (rs, imm, w ? 3 : 7, true);
	if (w) { movRM (false, RAX, mR (fp32 (ft))); movMR (false, ram (), RAX); }
	else { movRM (true, RAX, mR (fp64 (ft))); shI (S_ROR, true, RAX, 32); movMR (true, ram (), RAX); }
}

// ---- les branchements : r14d ------------------------------------------------------------------------------------------
static void be_condFp (bool ifTrue)
{
	movRM (false, R14, mR (OFF (fcr31)));
	shI (S_SHR, false, R14, 23);
	aluI (A_AND, false, R14, 1);
	if (!ifTrue) aluI (A_XOR, false, R14, 1);
}
static void condSet (int cc) { setcc (cc, RAX); opRR (0, false, 0x0FB6, 2, R14, RAX); }	// movzx r14d, al
static void be_condSign (int rs, bool geZero) { ld64 (RAX, rs); testRR (true, RAX, RAX); condSet (geZero ? C_GE : C_L); }
static void be_condEq (int rs, int rt, bool ne) { ld64 (RAX, rs); ld64 (RCX, rt); aluR (A_CMP, true, RAX, RCX); condSet (ne ? C_NE : C_E); }
static void be_condGtz (int rs, bool gt) { ld64 (RAX, rs); testRR (true, RAX, RAX); condSet (gt ? C_G : C_LE); }
static void be_jrTarget (int rs) { ld32 (RAX, rs); movRR (false, R14, RAX); }

// ---- les trampolines, au debut de la memoire de code -------------------------------------------------------------------
static void be_genStubs ()
{
	J.cp = J.code;
	J.enter = J.cp;						// (rdi = code, rsi = base)
	push (RBX); push (RBP); push (R12); push (R13); push (R14); push (R15);
	movRR (true, RBX, RSI);
	movRM (true, R15, mR (OFF (cycles)));
	movRM (true, RBP, mR (OFF (rdram)));
	movRM (true, R13, mR (OFF (jitPage)));
	jmpR (RDI);
	u8 *leave = J.cp;
	movMR (true, mR (OFF (cycles)), R15);
	pop (R15); pop (R14); pop (R13); pop (R12); pop (RBP); pop (RBX);
	B1 (0xC3);						// ret
	J.exitChain = J.cp;					// eax = cible, rcx = le site a chainer
	movMR (true, mR (OFF (jitSite)), RCX);
	J.exitNorm = J.cp;					// eax = pc
	movMR (false, mR (OFF (pc)), RAX);
	opRM (0, false, 0x8D, 1, RDX, mB (RAX, 4));		// lea edx, [rax + 4]
	movMR (false, mR (OFF (npc)), RDX);
	movMI8 (mR (OFF (nextDelay)), 0);
	jmpTo (leave);
	J.exitSide = J.cp;					// eax = pc : l'interprete fera cette instruction
	movMR (false, mR (OFF (pc)), RAX);
	opRM (0, false, 0x8D, 1, RDX, mB (RAX, 4));
	movMR (false, mR (OFF (npc)), RDX);
	movMI8 (mR (OFF (nextDelay)), 0);
	movMI32 (mR (OFF (jitStep)), 1);
	jmpTo (leave);
	J.exitSlot = J.cp;					// eax = la case, ecx = npc
	movMR (false, mR (OFF (pc)), RAX);
	movMR (false, mR (OFF (npc)), RCX);
	movMI8 (mR (OFF (nextDelay)), 1);
	movMI32 (mR (OFF (jitStep)), 1);
	jmpTo (leave);
	J.exitJr = J.cp;					// eax = cible : le bloc, s'il existe
	movRR (false, RCX, RAX);
	aluI (A_XOR, false, RCX, (s32) 0x80000000);
	aluI (A_AND, false, RCX, (s32) 0xDFFFFFFF);
	aluI (A_CMP, false, RCX, (s32) RDRAM_SIZE);
	u8 *f1 = jcc (C_AE);
	B1 (0xF6); B1 (0xC1); B1 (3);				// test cl, 3
	u8 *f2 = jcc (C_NE);
	movRM (true, RDX, mR (OFF (jitTab)));
	movRM (false, RDX, mBI (RDX, RCX));			// edx = jitTab[pa >> 2]
	testRR (false, RDX, RDX);
	u8 *f3 = jcc (C_E);
	movRM (true, RSI, mR (OFF (jitRec)));
	opRM (0, false, 0x39, 1, RAX, mBI (RSI, RDX, 3));	// cmp [rsi + rdx * 8], eax
	u8 *f4 = jcc (C_NE);
	movRM (false, RDX, mBI (RSI, RDX, 3, 4));		// edx = l'entree
	opRM (0, true, 0x03, 1, RDX, mR (OFF (jitCode)));	// add rdx, [jitCode]
	jmpR (RDX);
	be_patchRel (f1, J.cp); be_patchRel (f2, J.cp); be_patchRel (f3, J.cp); be_patchRel (f4, J.cp);
	jmpTo (J.exitNorm);
	J.start = (u32) (J.cp - J.code);
}

} // namespace n64
