/* Démarrage des autres cœurs (SMP) : tous les cœurs logiques actifs de la
 * MADT, autant qu'il y en a (MAX_CPUS au plus). Chacun passe par le
 * trampoline (smp_trampoline.asm) puis ap_entry et ap_main. */
#include <korin/arch.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/sched.h>

#include "../x86_64/cpu.h"
#include "../x86_64/paging.h"
#include "acpi.h"
#include "apic.h"
#include "smp.h"
#include <korin/spinlock.h>

/* smp_trampoline.bin, mis en objet par tools/pc/build.sh (objcopy) */
extern const uint8_t _binary_smp_trampoline_bin_start[];
extern const uint8_t _binary_smp_trampoline_bin_end[];

struct tramp_params { /* à T + 8, voir smp_trampoline.asm */
	uint32_t pml4;
	uint32_t pad;
	uint64_t stack;
	uint64_t cr3;
	uint64_t entry;
	uint64_t arg;
};

void ap_entry(void);

#define IST_SIZE 8192

static uint64_t tramp; /* adresse physique sous 1 Mo, 4 pages ; 0 : pas de SMP */
static uint64_t ap_stack_top[MAX_CPUS];
static int cpu_failed;

/* ---- invalidation des TLB : une interruption à chaque autre cœur en marche ;
 * celui qui a cet espace actif recharge CR3. Chaque demande est
 * aussi marquée pour le cœur (shoot_pending) : un cœur qui attend, interruptions
 * coupées, un verrou (arch_cpu_relax) ou une autre invalidation, répond en regardant ce
 * drapeau (smp_tlb_poll) : pas d'étreinte fatale. Permis sous un verrou (mm_fork). */

static spinlock_t shoot_lock = SPINLOCK_INIT;
static volatile uint64_t shoot_root;
static volatile unsigned shoot_acks;
static volatile int shoot_pending[MAX_CPUS];

void smp_tlb_poll(void)
{
	unsigned me = arch_cpu_id();
	if (!__atomic_load_n(&shoot_pending[me], __ATOMIC_ACQUIRE) ||
	    !__atomic_exchange_n(&shoot_pending[me], 0, __ATOMIC_ACQ_REL))
		return;
	uint64_t cr3 = read_cr3();
	if ((cr3 & ~0xFFFULL) == shoot_root)
		write_cr3(cr3);
	__atomic_add_fetch(&shoot_acks, 1, __ATOMIC_RELEASE);
}

static void tlb_irq(struct regs *r)
{
	(void)r;
	smp_tlb_poll();
}

void smp_tlb_shootdown(uint64_t root)
{
	unsigned n = arch_cpu_count();
	if (n <= 1)
		return;
	/* interruptions coupées tant que le verrou est tenu : sinon le thread pouvait être préempté (ou
	 * repris sur un autre cœur, « me » faux) en tenant shoot_lock, et les cœurs qui l'attendaient, les
	 * interruptions coupées, ne le laissaient jamais revenir (blocage complet vu sous 4 cœurs) ; en
	 * attendant le verrou, on répond aux invalidations des autres (arch_cpu_relax) */
	uint64_t f = spin_lock_irqsave(&shoot_lock);
	shoot_root = root;
	shoot_acks = 0;
	unsigned me = arch_cpu_id(), sent = 0;
	for (unsigned i = 0; i < MAX_CPUS && sent < n - 1; i++) {
		struct cpu *c = cpu_get(i);
		if (i == me || !c->online)
			continue;
		__atomic_store_n(&shoot_pending[i], 1, __ATOMIC_RELEASE);
		lapic_send_ipi(c->apic_id, VEC_TLB);
		sent++;
	}
	while (__atomic_load_n(&shoot_acks, __ATOMIC_ACQUIRE) < sent)
		arch_cpu_relax();
	spin_unlock_irqrestore(&shoot_lock, f);
}

void smp_init_ipis(void)
{
	idt_set_handler(VEC_TLB, tlb_irq);
}

void smp_set_trampoline(uint64_t phys)
{
	tramp = phys;
}

__attribute__((noreturn)) void ap_main(struct cpu *c)
{
	unsigned idx = (unsigned)c->index;
	cpu_init(idx, c->apic_id);
	uint8_t *ist = kmalloc(2 * IST_SIZE);
	if (!ist)
		arch_halt_forever();
	gdt_init(idx, (uint64_t)ist + IST_SIZE, (uint64_t)ist + 2 * IST_SIZE);
	idt_load();
	pstate_init_cpu(idx); /* (après l'IDT : un MSR absent fait une #GP, rattrapée) */
	apic_init_ap();
	sched_init_cpu(idx, ap_stack_top[idx]);
	cpu_set_online(idx);
	arch_irq_enable();
	for (;;) {
		sched_reap();
		arch_wait();
	}
}

static int start_one(unsigned idx, uint32_t apic_id)
{
	struct cpu *c = cpu_get(idx);
	c->index = idx;
	c->apic_id = apic_id;
	void *stack = kmalloc(KSTACK_SIZE);
	if (!stack)
		return -1;
	ap_stack_top[idx] = ((uint64_t)stack + KSTACK_SIZE) & ~15ULL;

	struct tramp_params *p = (struct tramp_params *)((uint8_t *)arch_phys_to_virt(tramp) + 8);
	p->pml4 = (uint32_t)(tramp + 0x1000);
	p->stack = ap_stack_top[idx];
	p->cr3 = paging_kernel_pml4();
	p->entry = (uint64_t)ap_entry;
	p->arg = (uint64_t)c;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);

	lapic_send_ipi(apic_id, 0x4500); /* INIT */
	udelay(10000);
	for (int sipi = 0; sipi < 2 && !c->online; sipi++) {
		lapic_send_ipi(apic_id, 0x4600 | (uint32_t)(tramp >> 12)); /* SIPI */
		for (int i = 0; i < 200 && !c->online; i++)
			udelay(sipi ? 1000 : 1); /* 200 µs, puis jusqu'à 200 ms */
	}
	return c->online ? 0 : -1;
}

void smp_start(void)
{
	if (acpi.cpu_count <= 1)
		return;
	if (!tramp) {
		kprintf("SMP : pas de page libre sous 1 Mo pour le trampoline, un seul cœur\n");
		return;
	}
	/* trampoline et tables provisoires : PML4, PDPT, PD (identité des 2 premiers Mo) */
	uint8_t *t = arch_phys_to_virt(tramp);
	uint64_t size = (uint64_t)(_binary_smp_trampoline_bin_end - _binary_smp_trampoline_bin_start);
	memset(t, 0, 0x4000);
	memcpy(t, _binary_smp_trampoline_bin_start, size);
	uint64_t *pml4 = (uint64_t *)(t + 0x1000), *pdpt = (uint64_t *)(t + 0x2000), *pd = (uint64_t *)(t + 0x3000);
	const uint64_t *kpml4 = arch_phys_to_virt(paging_kernel_pml4());
	pml4[0] = (tramp + 0x2000) | 3;
	pdpt[0] = (tramp + 0x3000) | 3;
	pd[0] = 0x83; /* 2 Mo, présent, écriture */
	for (int i = 256; i < 512; i++)
		pml4[i] = kpml4[i];

	uint32_t me = lapic_id();
	unsigned next = 1;
	for (uint32_t i = 0; i < acpi.cpu_count && next < MAX_CPUS; i++) {
		uint32_t id = acpi.cpu_apic_id[i];
		if (id == me)
			continue;
		if (start_one(next, id) == 0)
			next++;
		else
			cpu_failed++;
	}
	kprintf("SMP : %u cœur(s) en marche", arch_cpu_count());
	if (cpu_failed)
		kprintf(", %d sans réponse", cpu_failed);
	kprintf("\n");
}
