/* IDT : 256 portes vers isr.asm, exceptions affichées puis arrêt,
 * interruptions vers les gestionnaires enregistrés. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/sched.h>
#include <korin/signal.h>

#include "cpu.h"

struct __attribute__((packed)) gate {
	uint16_t off_lo;
	uint16_t sel;
	uint8_t ist;
	uint8_t type;
	uint16_t off_mid;
	uint32_t off_hi;
	uint32_t zero;
};

struct __attribute__((packed)) idtr {
	uint16_t limit;
	uint64_t base;
};

extern const uint64_t isr_table[256];
static struct gate idt[256] __attribute__((aligned(16)));
static irq_handler handlers[256];

static const char *const exc_names[32] = {
	"division par zéro", "débogage", "NMI", "point d'arrêt", "débordement",
	"hors limites", "instruction invalide", "pas de FPU", "faute double",
	"segment du coprocesseur", "TSS invalide", "segment absent", "faute de pile",
	"protection générale", "faute de page", "réservée", "erreur x87",
	"alignement", "machine check", "erreur SIMD", "virtualisation",
	"protection de contrôle", "réservée", "réservée", "réservée", "réservée",
	"réservée", "réservée", "injection de l'hyperviseur", "VMM", "sécurité",
	"réservée",
};

void idt_init(void)
{
	for (int v = 0; v < 256; v++) {
		uint64_t a = isr_table[v];
		idt[v].off_lo = a & 0xFFFF;
		idt[v].sel = 0x08;
		idt[v].ist = v == 8 ? 1 : v == 2 ? 2 : 0;
		idt[v].type = 0x8E; /* porte d'interruption, présente, anneau 0 */
		idt[v].off_mid = (a >> 16) & 0xFFFF;
		idt[v].off_hi = a >> 32;
		idt[v].zero = 0;
	}
	idt_load();
}

void idt_load(void)
{
	struct idtr r = {sizeof idt - 1, (uint64_t)idt};
	__asm__ volatile("lidt %0" ::"m"(r));
}

void idt_set_handler(uint8_t vector, irq_handler h)
{
	handlers[vector] = h;
}

int msr_fault_fixup(struct regs *r); /* (arch/pc/pstate.c) */

static void exception(struct regs *r)
{
	/* une lecture ou écriture « sûre » d'un MSR qui n'existe pas (pstate.c) : sautée */
	if (r->vector == 13 && !(r->cs & 3) && msr_fault_fixup(r))
		return;
	/* faute de page : région paresseuse du process (aussi quand le noyau lit
	 * ou écrit sa mémoire) */
	if (r->vector == 14) {
		uint64_t a = read_cr2();
		struct thread *t = sched_current();
		if (a < 0x0000800000000000ULL && t->proc->mm &&
		    mm_fault(t->proc, a, (r->error >> 1) & 1, (r->error >> 4) & 1) == 0)
			return;
		if (!(r->cs & 3) && a < 0x0000800000000000ULL && t->proc->vm) {
			/* le noyau touchait la mémoire du process : le process meurt */
			kprintf("\nprocess %d (%s) arrêté : adresse %lx inaccessible pendant un appel\n",
				t->proc->pid, t->proc->name, a);
			proc_exit(-11);
		}
	}
	if (r->cs & 3) { /* faute d'un process : son gestionnaire, sinon il s'arrête */
		static const int sigs[32] = {[0] = SIGFPE, [1] = SIGTRAP, [3] = SIGTRAP, [4] = SIGSEGV, [5] = SIGSEGV,
					     [6] = SIGILL, [7] = SIGFPE, [12] = SIGSEGV, [13] = SIGSEGV, [14] = SIGSEGV,
					     [16] = SIGFPE, [17] = SIGBUS, [19] = SIGFPE};
		int sig = sigs[r->vector] ? sigs[r->vector] : SIGSEGV;
		uint64_t a = r->vector == 14 ? read_cr2() : r->rip;
		int code = r->vector == 14 ? ((r->error & 1) ? 2 : 1) : 0;
		if (signal_fault(r, sig, code, a))
			return;
		struct thread *t = sched_current();
		console_color(CON_WARN);
		kprintf("\nprocess %d (%s) arrêté : exception %lu (%s) à %lx, cr2 %lx\n", t->proc->pid,
			t->proc->name, r->vector, exc_names[r->vector], r->rip, read_cr2());
		console_color(CON_NORMAL);
		proc_exit(-sig);
	}
	console_color(CON_ERROR);
	kprintf("\nException %lu (%s), code %lx\n", r->vector, exc_names[r->vector], r->error);
	console_color(CON_NORMAL);
	kprintf("  rip %016lx  cs %lx  rflags %016lx\n", r->rip, r->cs, r->rflags);
	kprintf("  rsp %016lx  ss %lx  cr2 %016lx\n", r->rsp, r->ss, read_cr2());
	kprintf("  rax %016lx rbx %016lx rcx %016lx rdx %016lx\n", r->rax, r->rbx, r->rcx, r->rdx);
	kprintf("  rsi %016lx rdi %016lx rbp %016lx\n", r->rsi, r->rdi, r->rbp);
	kprintf("  r8  %016lx r9  %016lx r10 %016lx r11 %016lx\n", r->r8, r->r9, r->r10, r->r11);
	kprintf("  r12 %016lx r13 %016lx r14 %016lx r15 %016lx\n", r->r12, r->r13, r->r14, r->r15);
	arch_halt_forever();
}

void isr_dispatch(struct regs *r)
{
	if (r->vector < 32) {
		if (handlers[r->vector]) {
			handlers[r->vector](r);
			return;
		}
		exception(r);
		if (r->cs & 3)
			signal_user_return(r);
		return;
	}
	if (handlers[r->vector])
		handlers[r->vector](r);
	if (r->vector != 0xFF) /* l'interruption parasite de l'APIC ne se termine pas */
		irq_eoi();
	sched_irq_exit();
	if (r->cs & 3)
		signal_user_return(r); /* mort différée, arrêt, signaux : au retour en mode utilisateur */
}
