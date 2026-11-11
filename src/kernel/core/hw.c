/* Pilotes en espace utilisateur (comme src/kernel/irq.asm du Pi) : quand une
 * ligne se lève, le noyau la masque puis lève les bits donnés dans les
 * signaux du service (vus par SVC_NEXT, la ligne rendue par IRQ_ACK), ou
 * réveille le thread dans IRQ_WAIT. Une ligne partagée (IRQ_SHARED) peut être
 * tenue par plusieurs pilotes. Lignes rendues à la fin du process. Portable. */
#include <korin/arch.h>
#include <korin/boot.h>
#include <korin/errno.h>
#include <korin/hw.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/svc.h>

#define IRQ_SHARED (1ULL << 63)
#define HOLDERS 4

struct holder {
	int pid;       /* 0 : libre */
	int chan;      /* -1 : IRQ_WAIT */
	uint64_t bits;
	int shared;
	volatile int raised;
};

static struct holder lines[IRQ_LINES][HOLDERS];
static spinlock_t irq_lock = SPINLOCK_INIT;

int64_t sys_dev_map(uint64_t phys, uint64_t size)
{
	struct process *p = sched_current()->proc;
	if (!p->rights)
		return E_PERM;
	if (size == 0 || phys + size < phys)
		return E_INVAL;
	if (arch_phys_is_ram(phys, size))
		return E_PERM; /* registres seulement, jamais la mémoire */
	return mm_map_device(phys, size);
}

static struct holder *mine(unsigned line, int pid)
{
	for (int i = 0; i < HOLDERS; i++)
		if (lines[line][i].pid == pid)
			return &lines[line][i];
	return 0;
}

int64_t sys_irq_bind(uint64_t line, int64_t chan, uint64_t bits)
{
	struct process *p = sched_current()->proc;
	if (!p->rights)
		return E_PERM;
	if (kernel_option("no_irq"))
		return E_NOSYS; /* les pilotes sondent */
	if (line >= IRQ_LINES || !arch_irq_line_valid((unsigned)line))
		return E_INVAL;
	int shared = (bits & IRQ_SHARED) != 0;
	bits &= ~IRQ_SHARED;
	uint64_t f = spin_lock_irqsave(&irq_lock);
	int64_t r = 0;
	struct holder *h = mine((unsigned)line, p->pid), *free_h = 0;
	for (int i = 0; i < HOLDERS && !h; i++) {
		struct holder *x = &lines[line][i];
		if (!x->pid) {
			if (!free_h)
				free_h = x;
		} else if (!(x->shared && shared))
			r = E_BUSY;
	}
	if (!h && r == 0) {
		if (!free_h)
			r = E_BUSY;
		else
			h = free_h;
	}
	if (h && r == 0) {
		h->pid = p->pid;
		h->chan = (int)chan;
		h->bits = bits;
		h->shared = shared;
		h->raised = 0;
	}
	spin_unlock_irqrestore(&irq_lock, f);
	if (r == 0 && chan >= 0)
		arch_irq_line_enable((unsigned)line, 1);
	return r;
}

int64_t sys_irq_ack(uint64_t line)
{
	if (line >= IRQ_LINES)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&irq_lock);
	struct holder *h = mine((unsigned)line, sched_current()->proc->pid);
	if (h)
		h->raised = 0;
	spin_unlock_irqrestore(&irq_lock, f);
	if (!h)
		return E_PERM;
	arch_irq_line_enable((unsigned)line, 1);
	return 0;
}

int64_t sys_irq_wait(uint64_t line, uint64_t ms)
{
	if (line >= IRQ_LINES)
		return E_INVAL;
	int pid = sched_current()->proc->pid;
	uint64_t f = spin_lock_irqsave(&irq_lock);
	struct holder *h = mine((unsigned)line, pid);
	if (!h) {
		spin_unlock_irqrestore(&irq_lock, f);
		return E_PERM;
	}
	h->raised = 0;
	sched_prepare_block(ms, &lines[line]);
	spin_unlock_irqrestore(&irq_lock, f);
	arch_irq_line_enable((unsigned)line, 1); /* démasquée à l'appel */
	int why = sched_block();
	f = spin_lock_irqsave(&irq_lock);
	h = mine((unsigned)line, pid);
	int raised = h && h->raised;
	if (h)
		h->raised = 0;
	spin_unlock_irqrestore(&irq_lock, f);
	if (raised)
		return 1;
	return why == WAKE_KILLED ? E_INTR : 0;
}

int64_t sys_irq_unbind(uint64_t line)
{
	if (line >= IRQ_LINES)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&irq_lock);
	struct holder *h = mine((unsigned)line, sched_current()->proc->pid);
	int left = 0;
	if (h)
		h->pid = 0;
	for (int i = 0; i < HOLDERS; i++)
		left |= lines[line][i].pid != 0;
	spin_unlock_irqrestore(&irq_lock, f);
	if (!h)
		return E_PERM;
	if (!left)
		arch_irq_line_enable((unsigned)line, 0);
	return 0;
}

void hw_irq_fired(unsigned line)
{
	if (line >= IRQ_LINES)
		return;
	spin_lock(&irq_lock); /* interruptions déjà coupées */
	int wake = 0;
	for (int i = 0; i < HOLDERS; i++) {
		struct holder *h = &lines[line][i];
		if (!h->pid)
			continue;
		h->raised = 1;
		if (h->chan >= 0)
			svc_signal_kernel(h->chan, h->bits);
		else
			wake = 1;
	}
	spin_unlock(&irq_lock);
	if (wake)
		sched_wake_all(&lines[line]);
}

void hw_proc_exit(struct process *p)
{
	for (unsigned line = 0; line < IRQ_LINES; line++) {
		uint64_t f = spin_lock_irqsave(&irq_lock);
		int had = 0, left = 0;
		for (int i = 0; i < HOLDERS; i++) {
			if (lines[line][i].pid == p->pid) {
				lines[line][i].pid = 0;
				had = 1;
			}
			left |= lines[line][i].pid != 0;
		}
		spin_unlock_irqrestore(&irq_lock, f);
		if (had && !left)
			arch_irq_line_enable(line, 0);
	}
}
