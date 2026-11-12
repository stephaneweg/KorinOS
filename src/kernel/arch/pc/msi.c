/* MSI et MSI-X des périphériques PCIe (docs/etude/PC-PILOTES.md).
 *
 * Une ligne MSI (IRQ_MSI_BASE + n) est un vecteur d'un APIC local : le
 * périphérique écrit la donnée (le vecteur) à l'adresse 0xFEE0_0000 | id << 12
 * et l'interruption arrive sur ce cœur-là. Les vecteurs MSI_VEC0..0xEF de
 * chaque cœur sont à prendre ; les lignes sont réparties sur les cœurs en
 * tournant. Une MSI ne se masque pas dans un IOAPIC : le masque est tenu ici
 * (une interruption arrivée masquée est gardée et rendue au démasquage) ;
 * comme une MSI est un front, rien ne se répète en attendant. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/hw.h>
#include <korin/lib.h>
#include <korin/spinlock.h>

#include "../x86_64/cpu.h"
#include "apic.h"

#define MSI_VECS (VEC_MSI_END - VEC_MSI0)

struct msi {
	int pid; /* 0 : libre */
	uint16_t cpu;
	uint8_t vec;
	uint8_t masked, pending;
};

static struct msi msi[IRQ_MSI_LINES];
static uint8_t vec_line[MAX_CPUS][MSI_VECS]; /* ligne + 1 (0 : vecteur libre) */
static spinlock_t msi_lock = SPINLOCK_INIT;
static unsigned next_cpu;

static void msi_irq(struct regs *r)
{
	unsigned c = arch_cpu_id(), v = (unsigned)(r->vector - VEC_MSI0);
	spin_lock(&msi_lock);
	unsigned l = vec_line[c][v];
	int fire = 0;
	if (l) {
		struct msi *m = &msi[l - 1];
		if (m->masked)
			m->pending = 1;
		else {
			m->masked = 1; /* jusqu'à IRQ_ACK / IRQ_WAIT */
			fire = 1;
		}
	}
	spin_unlock(&msi_lock);
	if (fire)
		hw_irq_fired(IRQ_MSI_BASE + l - 1);
}

void msi_init(void)
{
	for (unsigned v = VEC_MSI0; v < VEC_MSI_END; v++)
		idt_set_handler((uint8_t)v, msi_irq);
}

/* un cœur qui a encore un vecteur libre (et un id d'APIC que l'adresse MSI
 * peut viser sans remappage : moins de 256), en tournant */
static int pick(unsigned *cpu, unsigned *vec)
{
	unsigned n = arch_cpu_count();
	for (unsigned k = 0; k < n; k++) {
		unsigned c = (next_cpu + k) % n;
		struct cpu *pc = cpu_get(c);
		if (!pc || !pc->online || pc->apic_id > 255)
			continue;
		for (unsigned v = 0; v < MSI_VECS; v++)
			if (!vec_line[c][v]) {
				*cpu = c;
				*vec = v;
				next_cpu = c + 1;
				return 0;
			}
	}
	return -1;
}

int64_t arch_irq_msi_alloc(unsigned count, int pid, uint64_t *out)
{
	uint64_t f = spin_lock_irqsave(&msi_lock);
	int64_t first = E_NOMEM;
	for (unsigned s = 0; s + count <= IRQ_MSI_LINES && first < 0; s++) {
		unsigned i = 0;
		while (i < count && !msi[s + i].pid)
			i++;
		if (i == count)
			first = s;
		else
			s += i;
	}
	if (first >= 0) {
		for (unsigned i = 0; i < count; i++) {
			unsigned c, v;
			if (pick(&c, &v)) { /* plus de vecteurs : on rend ce qui est pris */
				for (unsigned j = 0; j < i; j++) {
					struct msi *m = &msi[first + j];
					vec_line[m->cpu][m->vec] = 0;
					m->pid = 0;
				}
				first = E_NOMEM;
				break;
			}
			struct msi *m = &msi[first + i];
			m->pid = pid;
			m->cpu = (uint16_t)c;
			m->vec = (uint8_t)v;
			m->masked = 1; /* jusqu'à IRQ_BIND */
			m->pending = 0;
			vec_line[c][v] = (uint8_t)(first + i + 1);
			out[2 * i] = 0xFEE00000ULL | (uint64_t)cpu_get(c)->apic_id << 12;
			out[2 * i + 1] = VEC_MSI0 + v; /* front, mode fixe */
		}
	}
	spin_unlock_irqrestore(&msi_lock, f);
	return first < 0 ? first : IRQ_MSI_BASE + first;
}

int arch_irq_msi_owner(unsigned line)
{
	if (line < IRQ_MSI_BASE || line >= IRQ_LINES)
		return 0;
	return msi[line - IRQ_MSI_BASE].pid;
}

void arch_irq_msi_free(unsigned line)
{
	if (line < IRQ_MSI_BASE || line >= IRQ_LINES)
		return;
	uint64_t f = spin_lock_irqsave(&msi_lock);
	struct msi *m = &msi[line - IRQ_MSI_BASE];
	if (m->pid) {
		vec_line[m->cpu][m->vec] = 0;
		m->pid = 0;
		m->masked = 1;
		m->pending = 0;
	}
	spin_unlock_irqrestore(&msi_lock, f);
}

int msi_line_valid(unsigned line)
{
	return line >= IRQ_MSI_BASE && line < IRQ_LINES && msi[line - IRQ_MSI_BASE].pid != 0;
}

void msi_line_enable(unsigned line, int on)
{
	if (!msi_line_valid(line))
		return;
	uint64_t f = spin_lock_irqsave(&msi_lock);
	struct msi *m = &msi[line - IRQ_MSI_BASE];
	int fire = 0;
	if (!on)
		m->masked = 1;
	else if (m->pending) { /* arrivée pendant le masque : rendue maintenant */
		m->pending = 0;
		m->masked = 1;
		fire = 1;
	} else
		m->masked = 0;
	spin_unlock(&msi_lock);
	if (fire)
		hw_irq_fired(line); /* (interruptions encore coupées) */
	arch_irq_restore(f);
}

void msi_print_info(void)
{
	unsigned used = 0;
	for (unsigned i = 0; i < IRQ_MSI_LINES; i++)
		used += msi[i].pid != 0;
	kprintf("  MSI : vecteurs %x-%x de chaque cœur, %u ligne(s) prise(s) sur %u\n", VEC_MSI0,
		VEC_MSI_END - 1, used, IRQ_MSI_LINES);
}
