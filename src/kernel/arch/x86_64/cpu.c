/* Cœurs x86-64 : structure de chaque cœur (GS), syscall/sysret, PAT, NX, FPU,
 * interruptions, piles et bascule des threads. */
#include <korin/arch.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/sched.h>

#include "cpu.h"

struct cpu cpus[MAX_CPUS];
static volatile unsigned online_count;
static uint64_t bsp_efer;

#define FPU_SIZE 512

void syscall_entry(void);
void thread_trampoline(void);
void context_switch(uint64_t *save_sp, uint64_t new_sp);

unsigned arch_cpu_id(void)
{
	uint64_t id;
	__asm__ volatile("mov %%gs:16, %0" : "=r"(id));
	return (unsigned)id;
}

unsigned arch_cpu_count(void)
{
	return online_count;
}

void arch_cpu_relax(void)
{
	pause();
}

struct cpu *cpu_get(unsigned idx)
{
	return &cpus[idx];
}

void cpu_init(unsigned idx, uint32_t apic_id)
{
	struct cpu *c = &cpus[idx];
	c->index = idx;
	c->apic_id = apic_id;
	wrmsr(MSR_GS_BASE, (uint64_t)c);
	wrmsr(MSR_KERNEL_GS_BASE, 0);

	if (idx == 0)
		bsp_efer = rdmsr(MSR_EFER);
	/* SCE : syscall ; NXE comme le cœur 0 (réglé par paging_init) */
	wrmsr(MSR_EFER, rdmsr(MSR_EFER) | 1 | (bsp_efer & (1ULL << 11)));
	/* sysret : CS = 0x10 + 16, SS = 0x10 + 8 (| 3) ; syscall : CS 0x08, SS 0x10 */
	wrmsr(MSR_STAR, (0x10ULL << 48) | (0x08ULL << 32));
	wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);
	wrmsr(MSR_FMASK, 0x40700); /* IF, TF, DF, AC coupés à l'entrée */

	/* FPU et SSE pour les process : CR0.EM éteint, MP et NE ; CR4.OSFXSR, OSXMMEXCPT */
	uint64_t cr0, cr4;
	__asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
	cr0 = (cr0 & ~(1ULL << 2)) | (1ULL << 1) | (1ULL << 5);
	__asm__ volatile("mov %0, %%cr0" ::"r"(cr0));
	__asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
	cr4 |= (1ULL << 9) | (1ULL << 10);
	__asm__ volatile("mov %0, %%cr4" ::"r"(cr4));
	__asm__ volatile("fninit");
}

void cpu_set_online(unsigned idx)
{
	cpus[idx].online = 1;
	__atomic_add_fetch(&online_count, 1, __ATOMIC_SEQ_CST);
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

/* état FPU/SSE d'un thread (FXSAVE, 512 octets alignés sur 16) */
int arch_thread_init(struct thread *t)
{
	uint8_t *fpu = kzalloc(FPU_SIZE); /* blocs du tas alignés sur 16 */
	if (!fpu)
		return -1;
	*(uint16_t *)(fpu + 0) = 0x037F;  /* FCW : exceptions masquées */
	*(uint32_t *)(fpu + 24) = 0x1F80; /* MXCSR : idem */
	t->arch_state = fpu;
	return 0;
}

void arch_thread_free(struct thread *t)
{
	kfree(t->arch_state);
	t->arch_state = 0;
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
	/* le noyau n'utilise pas la FPU (-mgeneral-regs-only) : on sauve celle du
	 * process à chaque bascule */
	__asm__ volatile("fxsave64 (%0)" ::"r"(prev->arch_state) : "memory");
	__asm__ volatile("fxrstor64 (%0)" ::"r"(next->arch_state) : "memory");
	cpus[arch_cpu_id()].kstack_top = next->kstack_top;
	tss_set_rsp0(next->kstack_top);
	context_switch(&prev->arch_sp, next->arch_sp);
}
