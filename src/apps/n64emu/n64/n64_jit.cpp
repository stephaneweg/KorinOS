//
// n64/n64_jit.cpp -- le recompilateur R4300 : le code MIPS en RDRAM, traduit un bloc a la fois (d'une adresse a son
// branchement et sa case de retard, 64 instructions au plus), des blocs chaines entre eux. Deux generateurs, un pour
// chaque machine : AArch64 (n64_jit_a64.h, le Pi) et x86-64 (n64_jit_x64.h, le PC). Ce fichier-ci est commun : il
// decode le MIPS, forme les blocs, compte les cycles, pose les sorties, chaine et invalide ; le generateur ne fait
// qu'ecrire les instructions de sa machine (fonctions be_*).
//
//   * Aucun appel C dans un bloc : tout ce qui sort du chemin rapide (une adresse hors de kseg0 / kseg1 ou de la RDRAM,
//     un acces mal aligne, un debordement, COP0, une page de code ecrite, une instruction non traduite) est une SORTIE
//     avant l'instruction, avec l'etat exact de l'interprete : Machine::step () la fait, puis on revient aux blocs.
//   * Les cycles sont ceux de l'interprete, a l'instruction pres : un bloc ne part que s'il tient avant le prochain
//     evenement (sinon l'interprete avance). Les deux modes donnent donc la meme machine : tools/emu/ref_n64.cpp compare,
//     sur chaque machine (tools/emu/check_n64_jit.sh).
//   * Le code MIPS du TLB (hors kseg0 / kseg1) et celui de la cartouche restent a l'interprete.
//   * Une ecriture dans une page traduite (Machine::jitWrote : memoire, DMA) retire ses blocs et leurs chainages.
//   * Le generateur AArch64 ecrit, octet pour octet, le code de l'ancien recompilateur d'un seul tenant
//     (tools/emu/n64ref/n64/n64_jit.cpp) : le Pi ne perd rien a la separation.
//
#include "n64/n64.h"

namespace n64 {

#if defined(__aarch64__) || defined(__x86_64__)

#define OFF(f) ((int) (__builtin_offsetof (Machine, f) - __builtin_offsetof (Machine, cycles)))

enum { JP = 8, CODE_SIZE = 48 << 20, MAX_BLOCKS = 1 << 19, MAX_INS = 64, MAX_WORST = 3000, MAX_LINKS = 1 << 20 };

// ---- l'etat du recompilateur ------------------------------------------------------------------------------------
struct BlockRec { u32 va, entry; };			// lu par le code : w0 -> bloc (entry : octets depuis J.code)
struct BlockInfo { u32 pa, worst; int in; };		// in : ses chainages entrants
struct Link { u32 site; int next; u64 word; };		// un saut pose en code + site, et ce qu'il remplace

struct Jit
{
	u8 *code; u8 *cp;
	BlockRec *rec; BlockInfo *info; int nblocks;
	Link *links; int nlinks;
	u8 *enter; u8 *exitChain, *exitNorm, *exitSide, *exitSlot, *exitJr;
	u32 start;					// premier octet libre apres les trampolines
	unsigned long compiled, flushes;
};
static Jit J;

// ---- un bloc ------------------------------------------------------------------------------------------------------
struct Stub { u32 at; int sofar; bool slot; u8 *fix[24]; int nfix; };
struct Exit { u8 *site; u32 target; };
static Stub g_st[MAX_INS * 2]; static int g_nst;
static Exit g_ex[8]; static int g_nex;
static int g_cur;					// le stub de l'instruction en cours, -1 : pas encore
static u32 g_at; static int g_sofar; static bool g_slot, g_fpChecked, g_fr;
static int g_extra;					// cycles en plus de l'instruction
static int g_slotKind; static u32 g_taken, g_fall;	// 0 fixe, 1 condition (registre de condition), 2 cible (idem)

// un saut vers la sortie de cette instruction, pose en `where` (le generateur le corrige a la fin du bloc)
static void sideRef (u8 *where)
{
	if (g_cur < 0)
	{
		Stub &s = g_st[g_nst]; s.at = g_at; s.sofar = g_sofar; s.slot = g_slot; s.nfix = 0;
		g_cur = g_nst++;
	}
	Stub &s = g_st[g_cur];
	if (s.nfix < 24) s.fix[s.nfix++] = where;
}

// les registres de COP1 (FR du Status, comme a la traduction)
static inline int fp32 (int n) { return g_fr ? OFF (fpr) + n * 8 : (n & 1) ? OFF (fpr) + (n & ~1) * 8 + 4 : OFF (fpr) + n * 8; }
static inline int fp64 (int n) { return OFF (fpr) + (g_fr ? n : (n & ~1)) * 8; }

} // namespace n64

#if defined(__aarch64__)
#include "n64/n64_jit_a64.h"
#else
#include "n64/n64_jit_x64.h"
#endif

namespace n64 {

static bool checkFp ()
{
	if (g_fpChecked) return true;
	g_fpChecked = true;
	be_checkFp (g_fr);
	return true;
}

// ---- le decodage ------------------------------------------------------------------------------------------------------
static bool genCop1 (u32 op)
{
	u32 fmt = (op >> 21) & 31, ft = (op >> 16) & 31, fs = (op >> 11) & 31, fd = (op >> 6) & 31, fn = op & 63;
	switch (fmt)
	{
	case 0x00: checkFp (); if (ft) be_mfc1 (ft, fs); return true;	// MFC1
	case 0x01: checkFp (); if (ft) be_dmfc1 (ft, fs); return true;	// DMFC1
	case 0x04: checkFp (); be_mtc1 (ft, fs); return true;		// MTC1
	case 0x05: checkFp (); be_dmtc1 (ft, fs); return true;		// DMTC1
	default: break;
	}
	if (fmt == 0x10 || fmt == 0x11)
	{
		bool dbl = fmt == 0x11;
		checkFp ();
		switch (fn)
		{
		case 0x00: case 0x01: case 0x02: case 0x03:
			if (!be_fpArith (fn, dbl, fd, fs, ft)) return false;
			if (fn == 2) g_extra = dbl ? 7 : 4;
			if (fn == 3) g_extra = dbl ? 114 : 56;
			return true;
		case 0x04: if (!be_fpSqrt (dbl, fd, fs)) return false; g_extra = dbl ? 114 : 56; return true;
		case 0x06: be_fpMov (dbl, fd, fs); return true;
		case 0x07: be_fpNeg (dbl, fd, fs); return true;
		case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x24:		// vers un entier 32 bits
			return be_fpToW (fn, dbl, fd, fs);
		case 0x20: if (!dbl) return false; be_cvtSD (fd, fs); return true;	// CVT.S.D
		case 0x21: if (dbl) return false; be_cvtDS (fd, fs); return true;	// CVT.D.S
		default: break;
		}
		if (fn >= 0x30) { be_fpCmp (fn, dbl, fs, ft); return true; }	// C.cond ; un NaN : l'interprete
		return false;
	}
	if (fmt == 0x14 && (fn == 0x20 || fn == 0x21))				// CVT.S.W, CVT.D.W
	{
		checkFp ();
		be_cvtFromW (fn == 0x21, fd, fs);
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
			if (rd) be_shiftImm32 (op & 63, rd, rt, sa);
			return true;
		case 0x04: case 0x06: case 0x07:			// SLLV, SRLV, SRAV
			if (rd) be_shiftVar32 (op & 63, rd, rt, rs);
			return true;
		case 0x0F: return true;					// SYNC
		case 0x10: if (rd) be_fromHiLo (rd, true); return true;	// MFHI
		case 0x11: be_toHiLo (rs, true); return true;		// MTHI
		case 0x12: if (rd) be_fromHiLo (rd, false); return true;	// MFLO
		case 0x13: be_toHiLo (rs, false); return true;		// MTLO
		case 0x14: case 0x16: case 0x17:			// DSLLV, DSRLV, DSRAV
			if (rd) be_shiftVar64 (op & 63, rd, rt, rs);
			return true;
		case 0x18: case 0x19:					// MULT, MULTU
			be_mult ((op & 63) == 0x18, rs, rt);
			g_extra = 8;
			return true;
		case 0x1A: case 0x1B:					// DIV, DIVU : par zero, debordement : l'interprete
			be_div ((op & 63) == 0x1A, rs, rt);
			g_extra = 70;
			return true;
		case 0x1C: case 0x1D:					// DMULT, DMULTU
			be_dmult ((op & 63) == 0x1C, rs, rt);
			g_extra = 14;
			return true;
		case 0x20: case 0x22:					// ADD, SUB (debordement : l'interprete)
			be_addTrap32 ((op & 63) == 0x22, rd, rs, rt);
			return true;
		case 0x21: case 0x23:					// ADDU, SUBU
			if (rd) be_add32 ((op & 63) == 0x23, rd, rs, rt);
			return true;
		case 0x24: case 0x25: case 0x26: case 0x27:		// AND, OR, XOR, NOR
			if (rd) be_logic (op & 63, rd, rs, rt);
			return true;
		case 0x2A: case 0x2B:					// SLT, SLTU
			if (rd) be_slt ((op & 63) == 0x2B, rd, rs, rt);
			return true;
		case 0x2C: case 0x2E:					// DADD, DSUB
			be_addTrap64 ((op & 63) == 0x2E, rd, rs, rt);
			return true;
		case 0x2D: case 0x2F:					// DADDU, DSUBU
			if (rd) be_add64 ((op & 63) == 0x2F, rd, rs, rt);
			return true;
		case 0x38: case 0x3A: case 0x3B: case 0x3C: case 0x3E: case 0x3F:	// DSLL...
			if (rd) be_shiftImm64 (op & 3, rd, rt, (int) sa + ((op & 4) ? 32 : 0));
			return true;
		default: return false;
		}
	case 0x08: be_addiTrap32 (rt, rs, imm); return true;		// ADDI
	case 0x09: if (rt) be_addiu32 (rt, rs, imm); return true;	// ADDIU
	case 0x0A: case 0x0B:						// SLTI, SLTIU
		if (rt) be_slti ((op >> 26) == 0x0B, rt, rs, imm);
		return true;
	case 0x0C: case 0x0D: case 0x0E:				// ANDI, ORI, XORI
		if (rt) be_logicImm (op >> 26, rt, rs, uimm);
		return true;
	case 0x0F: if (rt) be_li (rt, (s32) (uimm << 16)); return true;	// LUI
	case 0x11: return genCop1 (op);
	case 0x18: be_daddiTrap (rt, rs, imm); return true;		// DADDI
	case 0x19: if (rt) be_daddiu (rt, rs, imm); return true;	// DADDIU
	case 0x20: case 0x21: case 0x23: case 0x24: case 0x25: case 0x27: case 0x37:	// LB LH LW LBU LHU LWU LD
		be_load (op >> 26, rt, rs, imm);
		return true;
	case 0x28: case 0x29: case 0x2B: case 0x3F:			// SB SH SW SD
		be_store (op >> 26, rt, rs, imm);
		return true;
	case 0x2F: return true;						// CACHE
	case 0x31: case 0x35:						// LWC1, LDC1
		checkFp ();
		be_loadFp ((op >> 26) == 0x31, rt, rs, imm);
		return true;
	case 0x39: case 0x3D:						// SWC1, SDC1
		checkFp ();
		be_storeFp ((op >> 26) == 0x39, rt, rs, imm);
		return true;
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
	if (cyc) be_addCycles (cyc);
	Exit &e = g_ex[g_nex++];
	e.target = target;
	e.site = be_jumpPlaceholder ();			// (le saut vers sa sortie, pose apres le corps du bloc)
}

// le registre de condition = la condition du branchement relatif op
static void genCond (u32 op)
{
	u32 o = op >> 26, rs = (op >> 21) & 31, rt = (op >> 16) & 31;
	if (o == 0x11)						// BC1T, BC1F
	{
		checkFp ();
		be_condFp ((rt & 1) != 0);
		return;
	}
	if (o == 0x01) { be_condSign (rs, (rt & 1) != 0); return; }	// BLTZ, BGEZ... : >= 0 si rt & 1
	u32 k = o & 3;
	if (k <= 1) { be_condEq (rs, rt, k == 1); return; }	// BEQ, BNE
	be_condGtz (rs, k == 3);				// BLEZ (k 2), BGTZ (k 3)
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
	if (J.cp - J.code > CODE_SIZE - 0x10000 || J.nblocks >= MAX_BLOCKS - 2 || J.nlinks >= MAX_LINKS - 16) flushAll (m);
	int id = J.nblocks;
	u8 *start = J.cp;
	g_nst = 0; g_nex = 0; g_fpChecked = false;
	g_fr = (m->cp0[12] & (1 << 26)) != 0;
	// l'entree : le bloc tient-il avant le prochain evenement ?  (le pire : corrige a la fin)
	EntryFix ef = be_entryOpen ();
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
		if (p + 8 > RDRAM_SIZE) { g_at = at; g_sofar = sofar; g_slot = false; g_cur = -1; be_sideAlways (); break; }
		u32 op = m->rdram[p >> 2];
		int bk = branchKind (op);
		g_at = at; g_sofar = sofar; g_slot = false; g_cur = -1; g_extra = 0;
		if (op == 0x10000000 + 0xFFFF)			// « b . » : le saut d'attente de l'interprete
		{
			be_sideAlways ();
			break;
		}
		if (!bk)
		{
			u8 *save = J.cp; int nst = g_nst; bool fc = g_fpChecked;
			if (op != 0 && !gen (op))
			{
				J.cp = save; g_nst = nst; g_fpChecked = fc; g_cur = -1;
				be_sideAlways ();			// pas traduite : l'interprete, puis un autre bloc
				break;
			}
			sofar += 2 + g_extra; worst += 2 + g_extra;
			at += 4; p += 4; ins++;
			continue;
		}
		// un branchement et sa case de retard
		u32 slotOp = m->rdram[(p + 4) >> 2];
		u8 *save = J.cp; int nst = g_nst; bool fc = g_fpChecked; int nex = g_nex;
		if (branchKind (slotOp)) { be_sideAlways (); break; }
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
			be_jrTarget ((op >> 21) & 31);
			g_slotKind = 2;
		}
		if (link) be_li (31, (s32) (at + 8));		// (le lien part avant la case, meme non pris)
		if (bk == 3 && (op & 63) == 9)				// JALR
		{
			u32 rd = (op >> 11) & 31;
			if (rd) be_li (rd, (s32) (at + 8));
		}
		g_taken = target; g_fall = at + 8;
		sofar += 2; worst += 2;
		if (likely)						// non pris : la case est sautee
		{
			u8 *likelyFix = be_condBranch (true);		// pris : on passe a la case
			exitTo (at + 8, sofar);
			be_patchRel (likelyFix, J.cp);
		}
		// la case
		g_at = at + 4; g_sofar = sofar; g_slot = true; g_cur = -1; g_extra = 0;
		if (slotOp != 0 && !gen (slotOp))
		{
			J.cp = save; g_nst = nst; g_fpChecked = fc; g_nex = nex;
			g_at = at; g_sofar = sofar - 2; g_slot = false; g_cur = -1;
			be_sideAlways ();
			break;
		}
		sofar += 2 + g_extra; worst += 2 + g_extra;
		if (bk == 2 || likely) exitTo (target, sofar);
		else if (bk == 1)
		{
			u8 *f = be_condBranch (false);			// non pris
			exitTo (target, sofar);
			be_patchRel (f, J.cp);
			exitTo (at + 8, sofar);
		}
		else be_jrExit (sofar);
		open = false;
	}
	// les sorties
	be_entryClose (ef, (u32) (worst ? worst : 2), va);
	for (int i = 0; i < g_nst; i++)
	{
		Stub &s = g_st[i];
		for (int k = 0; k < s.nfix; k++) be_patchRel (s.fix[k], J.cp);
		if (s.nfix == 0) continue;
		be_stub (s.at, s.sofar, s.slot, g_slotKind, g_taken, g_fall);
	}
	for (int i = 0; i < g_nex; i++)
	{
		Exit &e = g_ex[i];
		be_patchRel (e.site, J.cp);
		be_chainStub (e.target, e.site);
	}
	be_flush (start, J.cp);
	J.rec[id].va = va; J.rec[id].entry = (u32) (start - J.code);
	J.info[id].pa = pa; J.info[id].worst = (u32) (worst ? worst : 2); J.info[id].in = -1;
	J.nblocks++;
	J.compiled++;
	m->jitTab[pa >> 2] = (u32) id;
	for (u32 q = pa >> JP; q <= ((p + 8) >> JP) && q < (RDRAM_SIZE >> JP); q++) m->jitPage[q] = 1;
	return id;
}

// ---- la machine -------------------------------------------------------------------------------------------------------
bool Machine::jitEnable (void *(*codeAlloc) (unsigned long))
{
	J.code = (u8 *) codeAlloc (CODE_SIZE);
	if (!J.code) return false;
	J.rec = new BlockRec[MAX_BLOCKS];
	J.info = new BlockInfo[MAX_BLOCKS];
	J.links = new Link[MAX_LINKS];
	J.nblocks = 1; J.nlinks = 0;
	jitTab = new u32[RDRAM_SIZE / 4];
	jitPage = new u8[RDRAM_SIZE >> JP];
	jitRec = J.rec;
	jitCode = J.code;
	be_genStubs ();
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
			u8 *site = J.code + J.links[l].site;
			be_linkUndo (site, J.links[l].word);
			be_flush (site, site + LINK_BYTES);
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

// (les essais : la somme du code produit, tools/emu/ref_n64.cpp ; meme somme que l'ancien recompilateur sur le Pi)
unsigned long n64_jit_code_hash ()
{
	unsigned long h = 1469598103934665603ul;
	for (u8 *p = J.code; p && p < J.cp; p++) h = (h ^ *p) * 1099511628211ul;
	return h;
}

void Machine::jitRun ()
{
	typedef void (*Enter) (void *code, void *base);
	Enter enter = (Enter) (void *) J.enter;
	while (cycles < nextEvent)
	{
		if (excPending || nextDelay || jitStep) { jitStep = 0; step (); continue; }
		u32 va = pc, pa = va & 0x1FFFFFFF;
		if ((va >> 30) != 2 || pa >= RDRAM_SIZE || (va & 3)) { step (); continue; }
		int id = (int) jitTab[pa >> 2];
		if (id && J.rec[id].va != va) { jitTab[pa >> 2] = 0; id = 0; }	// (l'autre segment : a refaire)
		if (!id) id = compile (this, va, pa);
		if (cycles + J.info[id].worst > nextEvent) { step (); continue; }
		jitSite = 0;
		enter (J.code + J.rec[id].entry, &cycles);
		if (jitSite && !jitStep && cycles < nextEvent)			// chainer ce site a sa cible
		{
			u8 *site = (u8 *) jitSite;
			u32 tva = pc, tpa = tva & 0x1FFFFFFF;
			if ((tva >> 30) == 2 && tpa < RDRAM_SIZE && !(tva & 3))
			{
				int t = (int) jitTab[tpa >> 2];
				if (t && J.rec[t].va != tva) t = 0;
				if (!t && J.cp - J.code < CODE_SIZE - 0x20000)
				{
					int fl = (int) J.flushes;
					t = compile (this, tva, tpa);
					if ((int) J.flushes != fl) site = 0;	// (tout a ete jete, ce site aussi)
				}
				if (t && site && J.nlinks < MAX_LINKS - 1)
				{
					Link &l = J.links[J.nlinks];
					l.site = (u32) (site - J.code); l.word = be_linkWord (site); l.next = J.info[t].in;
					J.info[t].in = J.nlinks++;
					be_linkSet (site, J.code + J.rec[t].entry);
					be_flush (site, site + LINK_BYTES);
				}
			}
		}
	}
}

#else	// (une autre machine : l'interprete)
bool Machine::jitEnable (void *(*) (unsigned long)) { return false; }
void Machine::jitRun () { while (cycles < nextEvent) step (); }
void Machine::jitDropPage (u32) {}
void Machine::jitWrote (u32, u32) {}
void Machine::jitFlush () {}
unsigned long Machine::jitStats (unsigned long *) { return 0; }
unsigned long n64_jit_code_hash () { return 0; }
#endif

} // namespace n64
