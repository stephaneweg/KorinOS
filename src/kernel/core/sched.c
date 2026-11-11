/* Ordonnanceur : tourniquet préemptif sur tous les cœurs (quantum de 10 ms),
 * une file commune protégée par un verrou, un thread inactif par cœur.
 * Le verrou est pris avant une bascule et rendu par le thread qui reprend
 * (après la bascule, ou dans sched_thread_start pour un thread neuf) : aucun
 * autre cœur ne peut reprendre un thread dont la pile n'est pas encore sauvée.
 * Portable : la pile, l'état FPU et la bascule viennent de l'architecture. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/elf.h>
#include <korin/errno.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/pmm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/vm.h>

#define QUANTUM_MS 10

struct process kernel_process = {.pid = 0, .name = "noyau", .state = P_ALIVE};

struct sched_cpu {
	struct thread *current;
	struct thread *idle;
	struct thread *prev; /* thread quitté, à marquer libre après la bascule */
	int quantum;
	volatile int need_resched;
};

static struct sched_cpu sc[MAX_CPUS];
static struct thread idle0;
static spinlock_t sched_lock = SPINLOCK_INIT;
static struct thread *threads;
static struct process *procs = &kernel_process;
static int next_tid, next_pid = 1;
static volatile int nready; /* threads prêts qui attendent un cœur */

/* sous le verrou */
static void set_state(struct thread *t, enum thread_state s)
{
	if (t->idle_cpu < 0) { /* les threads inactifs ne comptent pas */
		if (t->state == T_READY)
			nready--;
		if (s == T_READY)
			nready++;
	}
	t->state = s;
}

void sched_init_cpu(unsigned cpu, uint64_t stack_top)
{
	struct thread *t = cpu == 0 ? &idle0 : kzalloc(sizeof *t);
	if (!t)
		kpanic("sched : pas de mémoire pour le cœur %u", cpu);
	t->idle_cpu = (int)cpu;
	t->last_cpu = (int)cpu;
	t->state = T_RUNNING;
	t->on_cpu = 1;
	t->proc = &kernel_process;
	t->kstack_top = stack_top;
	ksnprintf(t->name, sizeof t->name, "inactif %u", cpu);
	if (arch_thread_init(t))
		kpanic("sched : état du thread inactif %u", cpu);
	uint64_t f = spin_lock_irqsave(&sched_lock);
	t->tid = next_tid++;
	t->next = threads;
	threads = t;
	kernel_process.threads++;
	sc[cpu].current = sc[cpu].idle = t;
	sc[cpu].quantum = QUANTUM_MS;
	spin_unlock_irqrestore(&sched_lock, f);
}

struct thread *sched_current(void)
{
	uint64_t f = arch_irq_save();
	struct thread *t = sc[arch_cpu_id()].current;
	arch_irq_restore(f);
	return t;
}

struct thread *thread_create(struct process *p, const char *name, void (*fn)(void *), void *arg)
{
	struct thread *t = kzalloc(sizeof *t);
	if (!t)
		return 0;
	void *stack = kmalloc(KSTACK_SIZE);
	if (!stack || arch_thread_init(t)) {
		kfree(stack);
		kfree(t);
		return 0;
	}
	t->kstack = (uint64_t)stack;
	t->kstack_top = ((uint64_t)stack + KSTACK_SIZE) & ~15ULL;
	t->proc = p;
	t->idle_cpu = -1;
	t->last_cpu = -1;
	size_t n = strlen(name);
	if (n >= sizeof t->name)
		n = sizeof t->name - 1;
	memcpy(t->name, name, n);
	arch_thread_setup(t, fn, arg);

	uint64_t f = spin_lock_irqsave(&sched_lock);
	t->tid = next_tid++;
	t->state = T_NEW;
	p->threads++;
	t->next = threads;
	threads = t;
	spin_unlock_irqrestore(&sched_lock, f);
	return t;
}

void thread_start(struct thread *t)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	if (t->state == T_NEW)
		set_state(t, T_READY);
	spin_unlock_irqrestore(&sched_lock, f);
}

/* prochain thread prêt après le courant ; sinon le courant ; sinon l'inactif */
static struct thread *pick(unsigned cpu)
{
	struct thread *cur = sc[cpu].current;
	struct thread *t = cur->next ? cur->next : threads;
	for (struct thread *start = t;;) {
		if (t->state == T_READY && t->idle_cpu < 0 && (!t->on_cpu || t == cur))
			return t;
		t = t->next ? t->next : threads;
		if (t == start)
			break;
	}
	return sc[cpu].idle;
}

/* après une bascule, sur le cœur où l'on reprend */
static void finish_switch(void)
{
	struct sched_cpu *c = &sc[arch_cpu_id()];
	if (c->prev) {
		c->prev->on_cpu = 0;
		c->prev = 0;
	}
}

/* verrou pris, interruptions coupées ; le verrou est encore pris au retour */
static void schedule_locked(void)
{
	unsigned cpu = arch_cpu_id();
	struct thread *prev = sc[cpu].current;
	if (prev->state == T_RUNNING)
		set_state(prev, T_READY);
	struct thread *next = pick(cpu);
	set_state(next, T_RUNNING);
	sc[cpu].quantum = QUANTUM_MS;
	sc[cpu].need_resched = 0;
	if (next == prev)
		return;
	next->on_cpu = 1;
	next->last_cpu = (int)cpu;
	sc[cpu].current = next;
	sc[cpu].prev = prev;
	vm_activate(next->proc->vm);
	arch_context_switch(prev, next);
	finish_switch(); /* peut-être sur un autre cœur */
}

void schedule(void)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	schedule_locked();
	spin_unlock_irqrestore(&sched_lock, f);
}

void sched_thread_start(void (*fn)(void *), void *arg)
{
	finish_switch();
	spin_unlock(&sched_lock);
	arch_irq_enable();
	fn(arg);
	thread_exit(0);
}

void sched_yield(void)
{
	schedule();
}

void sched_sleep(uint64_t ms)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	struct thread *t = sc[arch_cpu_id()].current;
	t->wake_ms = arch_ticks_ms() + ms;
	set_state(t, T_SLEEPING);
	schedule_locked();
	spin_unlock_irqrestore(&sched_lock, f);
}

void sched_tick(void)
{
	unsigned cpu = arch_cpu_id();
	if (cpu == 0) { /* le cœur 0 réveille les dormeurs */
		uint64_t now = arch_ticks_ms();
		spin_lock(&sched_lock);
		for (struct thread *t = threads; t; t = t->next)
			if (t->state == T_SLEEPING && now >= t->wake_ms)
				set_state(t, T_READY);
		spin_unlock(&sched_lock);
	}
	struct sched_cpu *c = &sc[cpu];
	if (c->current == c->idle) {
		if (nready > 0)
			c->need_resched = 1;
	} else if (--c->quantum <= 0)
		c->need_resched = 1;
}

void sched_irq_exit(void)
{
	if (sc[arch_cpu_id()].need_resched)
		schedule();
}

static void kill_locked(struct thread *t, int code)
{
	struct process *p = t->proc;
	set_state(t, T_DEAD);
	if (--p->threads == 0 && p != &kernel_process) {
		p->state = P_ZOMBIE;
		p->exit_code = code;
	}
}

void thread_exit(int code)
{
	arch_irq_disable();
	spin_lock(&sched_lock);
	kill_locked(sc[arch_cpu_id()].current, code);
	schedule_locked();
	kpanic("thread_exit : retour d'un thread fini");
}

void proc_exit(int code)
{
	arch_irq_disable();
	spin_lock(&sched_lock);
	struct thread *cur = sc[arch_cpu_id()].current;
	/* les autres threads du process : morts ; un thread encore sur un autre
	 * cœur le quitte à sa prochaine interruption */
	for (struct thread *t = threads; t; t = t->next)
		if (t->proc == cur->proc && t != cur && t->state != T_DEAD)
			kill_locked(t, code);
	kill_locked(cur, code);
	schedule_locked();
	kpanic("proc_exit : retour d'un process fini");
}

void sched_reap(void)
{
	struct thread *dead = 0;
	struct process *gone = 0;
	uint64_t f = spin_lock_irqsave(&sched_lock);
	for (struct thread **pt = &threads; *pt;) {
		struct thread *t = *pt;
		if (t->state == T_DEAD && !t->on_cpu && t->idle_cpu < 0) {
			*pt = t->next;
			t->next = dead;
			dead = t;
			continue;
		}
		pt = &t->next;
	}
	for (struct process **pp = &procs; *pp;) {
		struct process *p = *pp;
		int busy = p->state != P_ZOMBIE;
		for (struct thread *t = threads; t && !busy; t = t->next)
			busy = t->proc == p;
		if (!busy) {
			*pp = p->next;
			p->next = gone;
			gone = p;
			continue;
		}
		pp = &p->next;
	}
	spin_unlock_irqrestore(&sched_lock, f);

	while (dead) {
		struct thread *t = dead;
		dead = t->next;
		arch_thread_free(t);
		kfree((void *)t->kstack);
		kfree(t);
	}
	while (gone) {
		struct process *p = gone;
		gone = p->next;
		vm_destroy(p->vm);
		console_color(CON_DIM);
		kprintf("[process %d (%s) fini, code %d]\n", p->pid, p->name, p->exit_code);
		console_color(CON_NORMAL);
		kfree(p);
	}
}

unsigned sched_running(void)
{
	return (unsigned)nready;
}

/* ---- process */

#define USER_STACK_TOP 0x00007FFFFFFF0000ULL
#define USER_STACK_PAGES 16

static void user_start(void *arg)
{
	(void)arg;
	struct thread *t = sched_current();
	arch_enter_user(t->user_entry, t->user_stack, 0);
}

int proc_spawn(const char *name, const void *image, uint64_t size)
{
	struct process *p = kzalloc(sizeof *p);
	if (!p)
		return E_NOMEM;
	size_t n = strlen(name);
	if (n >= sizeof p->name)
		n = sizeof p->name - 1;
	memcpy(p->name, name, n);
	p->vm = vm_create();
	if (!p->vm) {
		kfree(p);
		return E_NOMEM;
	}
	uint64_t entry;
	int r = elf_load(p->vm, image, size, &entry);
	for (int i = 1; r == 0 && i <= USER_STACK_PAGES; i++) {
		uint64_t pg = pmm_alloc();
		if (!pg) {
			r = E_NOMEM;
			break;
		}
		memset(arch_phys_to_virt(pg), 0, PAGE_SIZE);
		r = vm_map_page(p->vm, USER_STACK_TOP - i * PAGE_SIZE, pg, VM_W);
		if (r)
			pmm_free(pg);
	}
	if (r) {
		vm_destroy(p->vm);
		kfree(p);
		return r;
	}

	uint64_t f = spin_lock_irqsave(&sched_lock);
	p->pid = next_pid++;
	p->state = P_ALIVE;
	p->next = procs;
	procs = p;
	spin_unlock_irqrestore(&sched_lock, f);

	struct thread *t = thread_create(p, p->name, user_start, 0);
	if (!t) {
		f = spin_lock_irqsave(&sched_lock);
		p->state = P_ZOMBIE; /* sans thread : libéré par sched_reap */
		spin_unlock_irqrestore(&sched_lock, f);
		return E_NOMEM;
	}
	t->user_entry = entry;
	t->user_stack = USER_STACK_TOP - 16;
	thread_start(t);
	return p->pid;
}

static const char *const state_names[] = {"neuf", "prêt", "actif", "dort", "fini"};

void proc_list(void)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	kprintf("  pid  nom              threads\n");
	for (struct process *p = procs; p; p = p->next) {
		if (p == &kernel_process && p->threads > 4) {
			kprintf("  %3d  %-16s %d (dont %u inactifs, un par cœur)\n", p->pid, p->name, p->threads,
				arch_cpu_count());
		} else
			kprintf("  %3d  %-16s %d%s\n", p->pid, p->name, p->threads,
				p->state == P_ZOMBIE ? " (fini)" : "");
		for (struct thread *t = threads; t; t = t->next) {
			if (t->proc != p || (t->idle_cpu >= 0 && t->state != T_RUNNING))
				continue;
			if (t->state == T_RUNNING)
				kprintf("         tid %d %s : actif sur le cœur %d\n", t->tid, t->name, t->last_cpu);
			else
				kprintf("         tid %d %s : %s\n", t->tid, t->name, state_names[t->state]);
		}
	}
	spin_unlock_irqrestore(&sched_lock, f);
}
