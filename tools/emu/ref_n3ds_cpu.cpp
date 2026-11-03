// ref_n3ds_cpu.cpp -- la reference du banc 3DS : le processeur du coeur d'Onyx (n3ds_cpu.cpp, Dynarmic), mais pas a
// pas : un tick par instruction, compte avant de la faire. C'est la regle de l'interprete et du recompilateur du port
// (src/apps/n3dsemu/n3ds/n3ds_arm.h) : les deux machines voient donc le meme temps, a l'instruction pres.
#include "n3ds/n3ds.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <cstdlib>
#include "dynarmic/interface/A32/a32.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/A32/coprocessor.h"
#include "dynarmic/interface/exclusive_monitor.h"

namespace n3ds {
namespace {

using Dynarmic::A32::CoprocReg;

struct Cp15 final : Dynarmic::A32::Coprocessor
{
	u32 tls = 0, urw = 0;
	std::optional<Callback> CompileInternalOperation (bool, unsigned, CoprocReg, CoprocReg, CoprocReg, unsigned) override { return std::nullopt; }
	CallbackOrAccessOneWord CompileSendOneWord (bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm, unsigned opc2) override
	{
		if (two || opc1 != 0) return std::monostate {};
		if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc2 == 2) return &urw;
		if (CRn == CoprocReg::C7) return Callback { [] (void *, u32, u32) -> std::uint64_t { return 0; }, std::nullopt };
		return std::monostate {};
	}
	CallbackOrAccessTwoWords CompileSendTwoWords (bool, unsigned, CoprocReg) override { return std::monostate {}; }
	CallbackOrAccessOneWord CompileGetOneWord (bool two, unsigned opc1, CoprocReg CRn, CoprocReg CRm, unsigned opc2) override
	{
		if (two || opc1 != 0) return std::monostate {};
		if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc2 == 3) return &tls;
		if (CRn == CoprocReg::C13 && CRm == CoprocReg::C0 && opc2 == 2) return &urw;
		return std::monostate {};
	}
	CallbackOrAccessTwoWords CompileGetTwoWords (bool, unsigned, CoprocReg) override { return std::monostate {}; }
	std::optional<Callback> CompileLoadWords (bool, bool, CoprocReg, std::optional<std::uint8_t>) override { return std::nullopt; }
	std::optional<Callback> CompileStoreWords (bool, bool, CoprocReg, std::optional<std::uint8_t>) override { return std::nullopt; }
};

struct RefCpu final : Cpu, Dynarmic::A32::UserCallbacks
{
	Machine *m;
	std::shared_ptr<Cp15> cp15;
	Dynarmic::ExclusiveMonitor monitor { 1 };
	std::unique_ptr<Dynarmic::A32::Jit> jit;
	bool stop = false;
	FILE *trace = 0; u64 traceFrom = 0, traceTo = 0;

	explicit RefCpu (Machine *machine) : m (machine), cp15 (std::make_shared<Cp15> ())
	{
		Dynarmic::A32::UserConfig cfg;
		cfg.callbacks = this;
		cfg.arch_version = Dynarmic::A32::ArchVersion::v6K;
		cfg.coprocessors[15] = cp15;
		cfg.global_monitor = &monitor;
		cfg.page_table = reinterpret_cast<std::array<std::uint8_t *, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES> *> (m->mem.pages);
		cfg.code_cache_size = 64 * 1024 * 1024;
		jit = std::make_unique<Dynarmic::A32::Jit> (cfg);
		if (const char *t = getenv ("N3DS_TRACE_RANGE")) { unsigned long long a, b; if (sscanf (t, "%llu-%llu", &a, &b) == 2) { traceFrom = a; traceTo = b; trace = stdout; } }
	}
	void run () override
	{
		stop = false;
		while (m->ticksLeft > 0 && !stop)
		{
			if (trace && m->now >= traceFrom && m->now < traceTo)
			{
				const auto &r = jit->Regs ();
				fprintf (trace, "%llu %08x %08x", (unsigned long long) m->now, (unsigned) r[15], (unsigned) (jit->Cpsr () & 0xF00F0020));
				for (int i = 0; i < 15; i++) fprintf (trace, " %08x", (unsigned) r[i]);
				fprintf (trace, " %08x\n", (unsigned) (jit->Fpscr () & 0xF0000000));
			}
			m->now++; m->ticksLeft--;
			jit->ClearHalt ();
			jit->Step ();
		}
	}
	void halt () override { stop = true; jit->HaltExecution (); }
	u32 *regs () override { return jit->Regs ().data (); }
	void save (CpuState &s) override
	{
		std::memcpy (s.r, jit->Regs ().data (), sizeof s.r);
		std::memcpy (s.vfp, jit->ExtRegs ().data (), sizeof s.vfp);
		s.cpsr = jit->Cpsr (); s.fpscr = jit->Fpscr (); s.tls = cp15->tls;
	}
	void load (const CpuState &s) override
	{
		std::memcpy (jit->Regs ().data (), s.r, sizeof s.r);
		std::memcpy (jit->ExtRegs ().data (), s.vfp, sizeof s.vfp);
		jit->SetCpsr (s.cpsr); jit->SetFpscr (s.fpscr); cp15->tls = s.tls;
		jit->ClearExclusiveState ();
	}
	void invalidate (u32 va, u32 size) override { jit->InvalidateCacheRange (va, size); }

	std::optional<u32> MemoryReadCode (u32 va) override
	{
		if (!m->mem.mapped (va, 4)) return std::nullopt;
		u32 v; m->mem.read (va, &v, 4);
		return v;
	}
	u8 MemoryRead8 (u32 va) override { return m->mem.r8 (va); }
	u16 MemoryRead16 (u32 va) override { return m->mem.r16 (va); }
	u32 MemoryRead32 (u32 va) override { return m->mem.r32 (va); }
	u64 MemoryRead64 (u32 va) override { return m->mem.r64 (va); }
	void MemoryWrite8 (u32 va, u8 v) override { m->mem.w8 (va, v); }
	void MemoryWrite16 (u32 va, u16 v) override { m->mem.w16 (va, v); }
	void MemoryWrite32 (u32 va, u32 v) override { m->mem.w32 (va, v); }
	void MemoryWrite64 (u32 va, u64 v) override { m->mem.w64 (va, v); }
	bool MemoryWriteExclusive8 (u32 va, u8 v, u8 expected) override { if (m->mem.r8 (va) != expected) return false; m->mem.w8 (va, v); return true; }
	bool MemoryWriteExclusive16 (u32 va, u16 v, u16 expected) override { if (m->mem.r16 (va) != expected) return false; m->mem.w16 (va, v); return true; }
	bool MemoryWriteExclusive32 (u32 va, u32 v, u32 expected) override { if (m->mem.r32 (va) != expected) return false; m->mem.w32 (va, v); return true; }
	bool MemoryWriteExclusive64 (u32 va, u64 v, u64 expected) override { if (m->mem.r64 (va) != expected) return false; m->mem.w64 (va, v); return true; }
	void InterpreterFallback (u32 pc, size_t) override { m->fail ("the JIT cannot run the instruction at %08x (%08x)", (unsigned) pc, (unsigned) m->mem.r32 (pc)); }
	// (le systeme et le GPU emules calculent sur l'hote : sous Dynarmic ils le feraient avec la FPCR de l'invite,
	// arrondi vers zero et mise a zero, ce que le port ne fait pas. Le coeur tourne ici avec la FPCR de l'hote.)
	void CallSVC (u32 swi) override
	{
		u64 fpcr; __asm__ volatile ("mrs %0, fpcr" : "=r" (fpcr));
		__asm__ volatile ("msr fpcr, xzr");
		m->svc (swi & 0xFF);
		__asm__ volatile ("msr fpcr, %0" : : "r" (fpcr));
	}
	void ExceptionRaised (u32 pc, Dynarmic::A32::Exception e) override
	{
		using E = Dynarmic::A32::Exception;
		if (e == E::Yield || e == E::WaitForInterrupt || e == E::WaitForEvent || e == E::SendEvent || e == E::SendEventLocal
		    || e == E::PreloadData || e == E::PreloadDataWithIntentToWrite || e == E::PreloadInstruction) return;
		m->fail ("exception %d at %08x (%08x)", (int) e, (unsigned) pc, (unsigned) m->mem.r32 (pc));
	}
	void AddTicks (u64) override {}
	u64 GetTicksRemaining () override { return 1; }
};

}
Cpu *Cpu::create (Machine *m) { return new RefCpu (m); }
}
