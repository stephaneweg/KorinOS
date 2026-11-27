/* Ordonnanceur : tourniquet préemptif à priorités sur tous les cœurs (quantum
 * de 10 ms), une file commune protégée par un verrou, un thread inactif par
 * cœur. Le verrou est pris avant une bascule et rendu par le thread qui
 * reprend (après la bascule, ou dans sched_thread_start pour un thread neuf) :
 * aucun autre cœur ne peut reprendre un thread dont la pile n'est pas sauvée.
 *
 * Priorités (comme le Pi) : à chaque passage, chaque thread prêt gagne un
 * point ; on prend la plus grande somme avance (0, 4 ou 16) + points, et le
 * thread choisi repart de 0 : pas de famine.
 *
 * Portable : la pile, l'état FPU et la bascule viennent de l'architecture. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/errno.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/vm.h>

#define QUANTUM_MS 10

struct process kernel_process = {.pid = 0, .name = "noyau", .state = P_ALIVE, .rights = RIGHTS_ALL};

struct sched_cpu {
	struct thread *current;
	struct thread *idle;
	struct thread *prev; /* thread quitté, à marquer libre après la bascule */
	int quantum;
	volatile int need_resched;
	/* charge, tenue par le cœur lui-même à chaque coup de minuterie (sans verrou) :
	 * occupé = le coup tombe sur un autre thread que l'inactif */
	int ticking;
	uint64_t last_ms;     /* le coup d'avant */
	uint64_t win_start;   /* début de la seconde en cours */
	uint64_t win_busy;    /* ms occupées dans la seconde en cours */
	volatile uint64_t busy_ms; /* ms occupées depuis le démarrage */
	volatile uint32_t load;    /* % de la dernière seconde entière */
	volatile int kicked;       /* appelé par un autre cœur (arch_cpu_kick), pas encore passé par schedule */
};

static struct sched_cpu sc[MAX_CPUS];
static struct thread idle0;
static spinlock_t sched_lock = SPINLOCK_INIT;
static struct thread *threads;
static int next_tid;
static volatile int nready; /* threads prêts qui attendent un cœur */
static volatile uint64_t switches;
static const int prio_bonus[3] = {0, 4, 16};

static void kill_locked(struct thread *t, int code);

uint64_t sched_lock_take(void)
{
	return spin_lock_irqsave(&sched_lock);
}

void sched_lock_give(uint64_t f)
{
	spin_unlock_irqrestore(&sched_lock, f);
}

struct thread *sched_threads(void)
{
	return threads;
}

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

static void no_start(void *arg)
{
	(void)arg;
}

__attribute__((weak)) void *arch_kstack_alloc(void)
{
	return kmalloc(KSTACK_SIZE);
}

__attribute__((weak)) void arch_kstack_free(void *stack)
{
	kfree(stack);
}

struct thread *thread_create_raw(struct process *p, const char *name)
{
	return thread_create(p, name, no_start, 0);
}

struct thread *thread_create(struct process *p, const char *name, void (*fn)(void *), void *arg)
{
	struct thread *t = kzalloc(sizeof *t);
	if (!t)
		return 0;
	void *stack = arch_kstack_alloc();
	if (!stack || arch_thread_init(t)) {
		if (stack)
			arch_kstack_free(stack);
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

/* le thread prêt de plus grand score ; sinon le courant ; sinon l'inactif */
static struct thread *pick(unsigned cpu)
{
	struct thread *cur = sc[cpu].current, *best = 0;
	int best_score = -1;
	for (struct thread *t = threads; t; t = t->next) {
		if (t->state != T_READY || t->idle_cpu >= 0 || (t->on_cpu && t != cur))
			continue;
		int score = prio_bonus[t->priority] + t->points;
		t->points++;
		if (score > best_score) {
			best_score = score;
			best = t;
		}
	}
	if (best) {
		best->points = 0;
		return best;
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
	sc[cpu].kicked = 0;
	uint64_t now = arch_ticks_ms();
	prev->runtime_ms += now - prev->ran_since; /* (aussi quand il garde le cœur : un thread qui
						    * tourne sans lâcher le cœur a son temps à jour) */
	if (next == prev) {
		prev->ran_since = now;
		return;
	}
	next->ran_since = now;
	switches++;
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

void sched_fork_start(void *frame)
{
	(void)frame;
	finish_switch();
	spin_unlock(&sched_lock); /* interruptions coupées jusqu'à iretq */
}

void sched_yield(void)
{
	schedule();
}

void sched_prepare_block(uint64_t timeout_ms, void *obj)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	struct thread *t = sc[arch_cpu_id()].current;
	t->wake_ms = timeout_ms ? arch_ticks_ms() + timeout_ms : 0;
	t->wake_reason = WAKE_OK;
	t->wait_obj = obj;
	if (t->kill_pending)
		t->wake_reason = WAKE_KILLED; /* ne pas dormir : on meurt */
	else
		set_state(t, T_BLOCKED);
	spin_unlock_irqrestore(&sched_lock, f);
}

int sched_block(void)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	struct thread *t = sc[arch_cpu_id()].current;
	if (t->state == T_BLOCKED)
		schedule_locked();
	t->wait_obj = 0;
	int r = t->wake_reason;
	spin_unlock_irqrestore(&sched_lock, f);
	return r;
}

/* un thread vient d'être prêt (verrou pris) : un cœur inactif le prend tout de suite, au lieu d'attendre son
 * prochain coup de minuterie (jusqu'à 1 ms) ; comme cpu_kick du noyau du Pi. Le cœur courant, s'il est inactif
 * (réveil depuis une interruption), repasse par l'ordonnanceur en sortant de l'interruption ; sinon une
 * interruption (arch_cpu_kick) à un cœur inactif qui n'en a pas déjà reçu une. */
static void kick_idle_locked(void)
{
	unsigned me = arch_cpu_id();
	if (sc[me].current == sc[me].idle) {
		sc[me].need_resched = 1;
		return;
	}
	unsigned n = arch_cpu_count();
	for (unsigned i = 0, seen = 0; i < MAX_CPUS && seen < n; i++) {
		if (!sc[i].idle)
			continue;
		seen++;
		if (i == me || sc[i].current != sc[i].idle || sc[i].kicked)
			continue;
		sc[i].kicked = 1;
		sc[i].need_resched = 1;
		arch_cpu_kick(i);
		return;
	}
}

static void wake_locked(struct thread *t, int reason)
{
	if (t->state == T_BLOCKED || t->state == T_SLEEPING) {
		t->wake_reason = reason;
		set_state(t, T_READY);
		kick_idle_locked();
	}
}

void sched_wake(struct thread *t)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	wake_locked(t, WAKE_OK);
	spin_unlock_irqrestore(&sched_lock, f);
}

void sched_wake_signal(struct thread *t)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	wake_locked(t, WAKE_SIGNAL);
	spin_unlock_irqrestore(&sched_lock, f);
}

/* (sched_lock pris : sched_lock_take) */
void sched_wake_signal_locked(struct thread *t)
{
	wake_locked(t, WAKE_SIGNAL);
}

void sched_wake_all(void *obj)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	for (struct thread *t = threads; t; t = t->next)
		if (t->state == T_BLOCKED && t->wait_obj == obj)
			wake_locked(t, WAKE_OK);
	spin_unlock_irqrestore(&sched_lock, f);
}

void sched_kill_thread(struct thread *t, int code)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	if (t->state == T_NEW)
		kill_locked(t, code);
	else if (t->state != T_DEAD) {
		t->kill_pending = 1;
		t->kill_code = code;
		wake_locked(t, WAKE_KILLED);
	}
	spin_unlock_irqrestore(&sched_lock, f);
}

void sched_kill_process(struct process *p, int code)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	if (p->state != P_ZOMBIE)
		p->exit_code = code;
	for (struct thread *t = threads; t; t = t->next) {
		if (t->proc != p || t->state == T_DEAD)
			continue;
		if (t->state == T_NEW) {
			kill_locked(t, code);
			continue;
		}
		t->kill_pending = 1;
		t->kill_code = code;
		wake_locked(t, WAKE_KILLED);
	}
	spin_unlock_irqrestore(&sched_lock, f);
}

int sched_sleep(uint64_t ms)
{
	uint64_t f = spin_lock_irqsave(&sched_lock);
	struct thread *t = sc[arch_cpu_id()].current;
	if (!t->kill_pending) {
		t->wake_ms = arch_ticks_ms() + ms;
		t->wake_reason = WAKE_OK;
		set_state(t, T_SLEEPING);
		schedule_locked();
	}
	int intr = t->kill_pending || t->wake_reason == WAKE_SIGNAL;
	spin_unlock_irqrestore(&sched_lock, f);
	return intr ? E_INTR : 0;
}

void sched_tick(void)
{
	unsigned cpu = arch_cpu_id();
	if (cpu == 0) { /* le cœur 0 réveille les dormeurs et les attentes échues */
		uint64_t now = arch_ticks_ms();
		spin_lock(&sched_lock);
		for (struct thread *t = threads; t; t = t->next)
			if ((t->state == T_SLEEPING || t->state == T_BLOCKED) && t->wake_ms && now >= t->wake_ms)
				wake_locked(t, t->state == T_BLOCKED ? WAKE_TIMEOUT : WAKE_OK);
		spin_unlock(&sched_lock);
	}
	struct sched_cpu *c = &sc[cpu];
	uint64_t now = arch_ticks_ms();
	if (!c->ticking) { /* premier coup du cœur */
		c->ticking = 1;
		c->last_ms = c->win_start = now;
	}
	uint64_t dt = now - c->last_ms;
	c->last_ms = now;
	if (c->current != c->idle) {
		c->win_busy += dt;
		c->busy_ms += dt;
	}
	uint64_t win = now - c->win_start;
	if (win >= 1000) {
		uint64_t l = (c->win_busy * 100 + win / 2) / win;
		c->load = (uint32_t)(l > 100 ? 100 : l);
		c->win_start = now;
		c->win_busy = 0;
	}
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

/* sous le verrou */
static void kill_locked(struct thread *t, int code)
{
	struct process *p = t->proc;
	set_state(t, T_DEAD);
	if (p != &kernel_process) {
		p->join_tid[p->join_next] = t->tid;
		p->join_code[p->join_next] = code;
		p->join_next = (p->join_next + 1) % JOIN_KEEP;
		/* threads qui attendent sa fin (THREAD_JOIN) */
		for (struct thread *w = threads; w; w = w->next)
			if (w->state == T_BLOCKED && w->wait_obj == (void *)t)
				wake_locked(w, WAKE_OK);
	}
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
	/* les autres threads : tués à leur prochain retour en mode utilisateur ;
	 * endormis ou bloqués : réveillés pour mourir ; pas encore partis : morts */
	for (struct thread *t = threads; t; t = t->next) {
		if (t->proc != cur->proc || t == cur || t->state == T_DEAD)
			continue;
		if (t->state == T_NEW)
			kill_locked(t, code);
		else {
			t->kill_pending = 1;
			t->kill_code = code;
			wake_locked(t, WAKE_KILLED);
		}
	}
	cur->proc->exit_code = code;
	kill_locked(cur, code);
	if (cur->proc->state == P_ZOMBIE)
		cur->proc->exit_code = code;
	schedule_locked();
	kpanic("proc_exit : retour d'un process fini");
}

void sched_user_return(void)
{
	struct thread *t = sched_current();
	if (t->kill_pending) {
		struct process *p = t->proc;
		if (p->state != P_ZOMBIE && p->threads == 1)
			p->exit_code = t->kill_code;
		thread_exit(t->kill_code);
	}
}

void sched_reap(void)
{
	struct thread *dead = 0;
	struct process *gone = 0;
	uint64_t f = spin_lock_irqsave(&sched_lock);
	for (struct thread **pt = &threads; *pt;) {
		struct thread *t = *pt;
		/* (un process tenu, proc_hold : ses threads finis restent, un pointeur de thread_find pris
		 * sous ce verrou reste valable ; THREAD_KILL, SIG_SEND à un thread) */
		if (t->state == T_DEAD && !t->on_cpu && t->idle_cpu < 0 &&
		    !(t->proc && __atomic_load_n(&t->proc->pins, __ATOMIC_ACQUIRE))) {
			*pt = t->next;
			t->next = dead;
			dead = t;
			continue;
		}
		pt = &t->next;
	}
	/* process finis dont plus aucun thread n'est dans la liste */
	for (struct process **pp = proc_list_head(); *pp;) {
		struct process *p = *pp;
		/* (pins : un service épingle encore ses pages, SVC_MAP_CALLER : au prochain passage) */
		int busy = p->state != P_ZOMBIE || __atomic_load_n(&p->pins, __ATOMIC_ACQUIRE);
		for (struct thread *t = threads; t && !busy; t = t->next)
			busy = t->proc == p;
		if (!busy) {
			p->reaping = 1;
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
		if (t->kstack)
			arch_kstack_free((void *)t->kstack);
		kfree(t);
	}
	while (gone) {
		struct process *p = gone;
		gone = p->next;
		proc_finished(p);
	}
}

unsigned sched_ready_count(void)
{
	return (unsigned)nready;
}

uint64_t sched_switches(void)
{
	return switches;
}

void sched_cpu_load(unsigned cpu, uint64_t *load, uint64_t *busy_ms)
{
	*load = cpu < MAX_CPUS ? sc[cpu].load : 0;
	*busy_ms = cpu < MAX_CPUS ? sc[cpu].busy_ms : 0;
}
