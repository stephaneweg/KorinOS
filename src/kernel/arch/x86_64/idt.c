/* IDT : 256 portes vers isr.asm, exceptions affichées puis arrêt,
 * interruptions vers les gestionnaires enregistrés. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/lib.h>

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
	struct idtr r = {sizeof idt - 1, (uint64_t)idt};
	__asm__ volatile("lidt %0" ::"m"(r));
}

void idt_set_handler(uint8_t vector, irq_handler h)
{
	handlers[vector] = h;
}

static void exception(struct regs *r)
{
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
	}
	if (handlers[r->vector])
		handlers[r->vector](r);
	if (r->vector != 0xFF) /* l'interruption parasite de l'APIC ne se termine pas */
		irq_eoi();
}
