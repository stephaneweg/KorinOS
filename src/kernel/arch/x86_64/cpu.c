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

/* état d'architecture d'un thread : FXSAVE (aligné sur 16) puis la base de FS */
struct arch_thread {
	uint8_t fpu[FPU_SIZE];
	uint64_t fs_base;
};

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
	/* PAT : la même que le cœur 0 (paging_init) ; sans elle, l'entrée 1 d'un autre cœur reste
	 * write-through et chaque écriture à l'écran y est lente (0,25 s par défilement sur le ZBook) */
	wrmsr(MSR_PAT, KORIN_PAT);
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

void arch_set_tls(uint64_t v)
{
	struct thread *t = sched_current();
	((struct arch_thread *)t->arch_state)->fs_base = v;
	wrmsr(MSR_FS_BASE, v);
}

uint64_t arch_get_tls(void)
{
	return rdmsr(MSR_FS_BASE);
}

int arch_hw_random(uint64_t *v)
{
	uint32_t r[4];
	cpuid(1, 0, r);
	if (!((r[2] >> 30) & 1))
		return 0;
	unsigned char ok;
	__asm__ volatile("rdrand %0; setc %1" : "=r"(*v), "=qm"(ok));
	return ok;
}

int arch_elf_machine(void)
{
	return 0x3E; /* EM_X86_64 */
}

/* état FPU/SSE d'un thread (FXSAVE, 512 octets alignés sur 16) */
int arch_thread_init(struct thread *t)
{
	struct arch_thread *a = kzalloc(sizeof *a); /* blocs du tas alignés sur 16 */
	if (!a)
		return -1;
	*(uint16_t *)(a->fpu + 0) = 0x037F;  /* FCW : exceptions masquées */
	*(uint32_t *)(a->fpu + 24) = 0x1F80; /* MXCSR : idem */
	t->arch_state = a;
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

void fork_trampoline(void);

/* fils de fork : il repart de la trame de l'appel du père, rax = 0, avec la
 * même FPU et le même registre de thread */
void arch_fork_setup(struct thread *child, const void *parent_frame)
{
	struct regs *r = (struct regs *)(child->kstack_top - sizeof(struct regs));
	memcpy(r, parent_frame, sizeof *r);
	r->rax = 0;
	struct arch_thread *ca = child->arch_state;
	__asm__ volatile("fxsave64 (%0)" ::"r"(ca->fpu) : "memory");
	ca->fs_base = rdmsr(MSR_FS_BASE);
	uint64_t *sp = (uint64_t *)r;
	*--sp = (uint64_t)fork_trampoline;
	*--sp = 0;            /* rbx */
	*--sp = 0;            /* rbp */
	*--sp = (uint64_t)r;  /* r12 : la trame */
	*--sp = 0;            /* r13 */
	*--sp = 0;            /* r14 */
	*--sp = 0;            /* r15 */
	child->arch_sp = (uint64_t)sp;
}

/* exec : registres du programme neuf (entrée, pile, argument), FPU neuve */
void arch_exec_frame(void *frame, uint64_t entry, uint64_t sp, uint64_t arg)
{
	struct regs *r = frame;
	uint64_t vec = r->vector;
	memset(r, 0, sizeof *r);
	r->vector = vec;
	r->rip = entry;
	r->rsp = sp;
	r->rdi = arg;
	r->rflags = 0x202;
	r->cs = 0x23;
	r->ss = 0x1B;
	__asm__ volatile("fninit");
	uint32_t mxcsr = 0x1F80;
	__asm__ volatile("ldmxcsr %0" ::"m"(mxcsr));
	arch_set_tls(0);
}

void arch_context_switch(struct thread *prev, struct thread *next)
{
	/* le noyau n'utilise pas la FPU (-mgeneral-regs-only) : on sauve celle du
	 * process à chaque bascule */
	struct arch_thread *pa = prev->arch_state, *na = next->arch_state;
	__asm__ volatile("fxsave64 (%0)" ::"r"(pa->fpu) : "memory");
	__asm__ volatile("fxrstor64 (%0)" ::"r"(na->fpu) : "memory");
	pa->fs_base = rdmsr(MSR_FS_BASE);
	if (na->fs_base != pa->fs_base)
		wrmsr(MSR_FS_BASE, na->fs_base);
	cpus[arch_cpu_id()].kstack_top = next->kstack_top;
	tss_set_rsp0(next->kstack_top);
	context_switch(&prev->arch_sp, next->arch_sp);
}
