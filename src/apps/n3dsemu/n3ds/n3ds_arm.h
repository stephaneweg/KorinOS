//
// n3ds/n3ds_arm.h -- l'ARM11 de la 3DS (ARMv6K, VFPv2), derriere l'interface Cpu de n3ds.h : un interprete
// (n3ds_arm.cpp) et un recompilateur vers A64 (n3ds_jit.cpp), ecrit d'apres celui du NDS. Les deux comptent le
// temps de la meme facon : un tick par instruction, sautee ou non, compte AVANT de la faire ; le recompilateur ne
// lance un bloc que s'il tient dans ce qui reste (docs/EMULATEURS.md). Une machine ne depend donc pas du mode.
//
#ifndef ONYX_N3DS_ARM_H
#define ONYX_N3DS_ARM_H

#include "n3ds/n3ds.h"

namespace n3ds {

struct Jit;

struct Arm11 final : Cpu
{
	// (le recompilateur lit ces champs par leur place : ils restent au debut)
	u32 r[16];
	u32 cpsr;			// NZCVQ, GE, E, T ; en mode utilisateur, rien d'autre ne bouge
	u32 fpscr;
	u32 vfp[32];			// d0-d15, la 3DS n'en a pas plus : s[n] = vfp[n], d[n] = vfp[2n] + vfp[2n + 1] << 32
	u32 tls, urw;			// CP15 c13 : TPIDRURO, TPIDRURW
	u32 exclOn, exclAddr;		// le moniteur d'exclusivite : l'adresse marquee (par 16 octets), la valeur lue
	u64 exclVal;
	u32 key;			// a la sortie d'un bloc : pc | Thumb
	u32 forceStep;			// ... et l'instruction de pc est pour l'interprete
	s64 ticksOut;			// ... et les ticks qui restent
	void *fast;			// la table des blocs, lue par le code produit
	u8 **pages;

	Machine *m;
	Jit *jit;
	bool halted;
	bool noJit;			// (essais : l'interprete seul)
	u64 steps, blocksRun;		// (essais)
	u64 traceFrom, traceTo;		// (essais : N3DS_TRACE_RANGE, a l'interprete)

	explicit Arm11 (Machine *machine);
	~Arm11 () override;
	void run () override;
	void halt () override { halted = true; }
	u32 *regs () override { return r; }
	void save (CpuState &s) override;
	void load (const CpuState &s) override;
	void invalidate (u32 va, u32 size) override;

	void step ();			// une instruction, tick compte
	void jitInit ();
	void jitRun ();
};

}

#endif
