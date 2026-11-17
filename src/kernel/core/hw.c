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
	if (arch_phys_is_fw_ram(phys, size))
		return mm_map_device(phys, size, 1); /* mémoire du micrologiciel (ACPI, NVS) : en cache */
	if (arch_phys_is_ram(phys, size))
		return E_PERM; /* registres seulement, jamais la mémoire */
	return mm_map_device(phys, size, 0);
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
	if (line >= IRQ_MSI_BASE && arch_irq_msi_owner((unsigned)line) != p->pid)
		return E_PERM; /* une ligne MSI n'est qu'à celui qui l'a demandée */
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
	if (!left) {
		arch_irq_line_enable((unsigned)line, 0);
		if (line >= IRQ_MSI_BASE)
			arch_irq_msi_free((unsigned)line); /* le vecteur est rendu */
	}
	return 0;
}

/* IRQ_MSI : des vecteurs pour les MSI ou MSI-X d'un périphérique PCIe. Le
 * pilote écrit lui-même adresse et donnée dans la capacité MSI ou la table
 * MSI-X (sa BAR) ; chaque ligne se lie ensuite par IRQ_BIND comme une autre. */
int64_t sys_irq_msi(uint64_t count, uint64_t out)
{
	struct process *p = sched_current()->proc;
	if (!p->rights)
		return E_PERM;
	if (kernel_option("no_irq"))
		return E_NOSYS;
	if (count == 0 || count > IRQ_MSI_MAX)
		return E_INVAL;
	uint64_t ad[IRQ_MSI_MAX * 2];
	int64_t first = arch_irq_msi_alloc((unsigned)count, p->pid, ad);
	if (first < 0)
		return first;
	if (out && copy_to_user(out, ad, count * 16)) {
		for (uint64_t i = 0; i < count; i++)
			arch_irq_msi_free((unsigned)(first + (int64_t)i));
		return E_FAULT;
	}
	return first;
}

/* ACPI_TABLE : copie d'une table du micrologiciel (pcid : MCFG ; plus tard
 * ACPICA : DSDT, SSDT…) ; rend sa taille entière, même si max est plus petit */
int64_t sys_acpi_table(uint64_t sig, uint64_t idx, uint64_t buf, uint64_t max)
{
	struct process *p = sched_current()->proc;
	if (!p->rights)
		return E_PERM;
	char s[4];
	if (copy_from_user(s, sig, 4))
		return E_FAULT;
	uint32_t len = 0;
	const void *t = arch_firmware_table(s, (unsigned)idx, &len);
	if (!t)
		return E_NOENT;
	uint64_t n = max < len ? max : len;
	if (n && copy_to_user(buf, t, n))
		return E_FAULT;
	return len;
}

/* IO_PORT : 8, 16 ou 32 bits lus ou écrits sur un port d'E/S, pour un pilote en
 * service (ps2kbd : le contrôleur i8042 ; acpid : l'AML, l'EC, les registres PM).
 * Seuls les ports que l'architecture permet ; ni plage, ni bitmap d'E/S du TSS :
 * un appel par accès (quelques-uns par touche, par lecture de l'EC). */
int64_t sys_io_port(uint64_t port, uint64_t op, uint64_t value)
{
	struct process *p = sched_current()->proc;
	if (!p->rights)
		return E_PERM;
	if (port > 0xFFFF || op > 5)
		return E_INVAL;
	unsigned width = 1u << (op >> 1); /* 1, 2, 4 octets */
	if ((port & (width - 1)) || port + width > 0x10000)
		return E_INVAL;
	return arch_io_port((unsigned)port, (unsigned)op, (uint32_t)value);
}

/* POWER : extinction ou redémarrage, pour init2 (src/svc/init2/init2_x86_64.c), qui a
 * d'abord arrêté les programmes, vidé et démonté les volumes, arrêté les disques ;
 * ne revient pas (sauf refus). POWER_PROFILE (3) : le profil d'énergie des cœurs
 * (acpid quand la batterie faiblit, Réglages) ; revient. */
int64_t sys_power(uint64_t op, int64_t arg)
{
	struct process *p = sched_current()->proc;
	if (!p->rights)
		return E_PERM;
	if (op == 3) {
		if (arg > 2)
			return E_INVAL;
		return arch_power_profile(arg < 0 ? -1 : (int)arg);
	}
	if (op != 1 && op != 2)
		return E_INVAL;
	kprintf("%s par le process %d (%s)\n", op == 1 ? "Extinction demandée" : "Redémarrage demandé", p->pid,
		p->name);
	if (op == 1)
		arch_power_off();
	arch_reboot();
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
		if (line >= IRQ_MSI_BASE && arch_irq_msi_owner(line) == p->pid)
			arch_irq_msi_free(line); /* aussi celles qui n'étaient pas liées */
	}
}
