/* Cœur x86-64 : structure du cœur (GS), syscall/sysret, interruptions,
 * piles et bascule des threads. */
#include <korin/arch.h>
#include <korin/sched.h>

#include "cpu.h"

/* lue par switch.asm : ne pas déplacer les deux premiers champs */
struct cpu {
	uint64_t kstack_top; /* +0 */
	uint64_t user_rsp;   /* +8 */
	uint32_t id;
};

static struct cpu cpu0;

void syscall_entry(void);
void thread_trampoline(void);
void context_switch(uint64_t *save_sp, uint64_t new_sp);

void cpu_init(void)
{
	wrmsr(MSR_GS_BASE, (uint64_t)&cpu0);
	wrmsr(MSR_KERNEL_GS_BASE, 0);
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1); /* SCE : syscall permis */
	/* sysret : CS = 0x10 + 16, SS = 0x10 + 8 (| 3) ; syscall : CS 0x08, SS 0x10 */
	wrmsr(MSR_STAR, (0x10ULL << 48) | (0x08ULL << 32));
	wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
	wrmsr(MSR_FMASK, 0x40700); /* IF, TF, DF, AC coupés à l'entrée */
}

uint64_t arch_irq_save(void)
{
	uint64_t f;
	__asm__ volatile("pushfq; pop %0; cli" : "=r"(f)::"memory");
	return f;
}

void arch_irq_restore(uint64_t f)
{
	if (f & 0x200)
		sti();
}

int arch_elf_machine(void)
{
	return 0x3E; /* EM_X86_64 */
}

void arch_thread_setup(struct thread *t, void (*fn)(void *), void *arg)
{
	uint64_t *sp = (uint64_t *)t->kstack_top;
	*--sp = (uint64_t)thread_trampoline;
	*--sp = 0;              /* rbx */
	*--sp = 0;              /* rbp */
	*--sp = (uint64_t)fn;   /* r12 */
	*--sp = (uint64_t)arg;  /* r13 */
	*--sp = 0;              /* r14 */
	*--sp = 0;              /* r15 */
	t->arch_sp = (uint64_t)sp;
}

void arch_context_switch(struct thread *prev, struct thread *next)
{
	cpu0.kstack_top = next->kstack_top;
	tss_set_rsp0(next->kstack_top);
	context_switch(&prev->arch_sp, next->arch_sp);
}
