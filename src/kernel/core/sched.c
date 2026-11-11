/* Ordonnanceur : tourniquet préemptif sur un cœur (quantum de 10 ms),
 * sommeil à la milliseconde, threads et process finis libérés par le thread
 * inactif. Portable : la pile et la bascule viennent de l'architecture. */
#include <korin/arch.h>
#include <korin/console.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/sched.h>
#include <korin/vm.h>

#define QUANTUM_MS 10

struct process kernel_process = {.pid = 0, .name = "noyau", .state = P_ALIVE};

static struct thread idle_thread;
static struct thread *current = &idle_thread;
static struct thread *threads = &idle_thread;
static struct process *procs = &kernel_process;
static int next_tid = 1, next_pid = 1;
static int quantum = QUANTUM_MS;
static volatile int need_resched;

void sched_init(void)
{
	idle_thread.tid = 0;
	idle_thread.state = T_RUNNING;
	idle_thread.proc = &kernel_process;
	memcpy(idle_thread.name, "inactif", 8);
	kernel_process.threads = 1;
}

struct thread *sched_current(void)
{
	return current;
}

struct thread *thread_create(struct process *p, const char *name, void (*fn)(void *), void *arg)
{
	struct thread *t = kzalloc(sizeof *t);
	if (!t)
		return 0;
	void *stack = kmalloc(KSTACK_SIZE);
	if (!stack) {
		kfree(t);
		return 0;
	}
	t->kstack = (uint64_t)stack;
	t->kstack_top = (uint64_t)stack + KSTACK_SIZE - 16; /* l'en-tête du tas est avant */
	t->kstack_top &= ~15ULL;
	t->proc = p;
	size_t n = strlen(name);
	if (n >= sizeof t->name)
		n = sizeof t->name - 1;
	memcpy(t->name, name, n);
	arch_thread_setup(t, fn, arg);

	uint64_t f = arch_irq_save();
	t->tid = next_tid++;
	p->threads++;
	t->state = T_READY;
	t->next = threads;
	threads = t;
	arch_irq_restore(f);
	return t;
}

/* prochain thread prêt après le courant, le thread inactif en dernier */
static struct thread *pick(void)
{
	struct thread *t = current->next ? current->next : threads;
	for (struct thread *start = t;;) {
		if (t->state == T_READY && t != &idle_thread)
			return t;
		t = t->next ? t->next : threads;
		if (t == start)
			break;
	}
	if (current->state == T_RUNNING && current != &idle_thread)
		return current;
	return &idle_thread;
}

void schedule(void)
{
	uint64_t f = arch_irq_save();
	struct thread *prev = current;
	struct thread *next = pick();
	if (prev->state == T_RUNNING)
		prev->state = T_READY;
	next->state = T_RUNNING;
	quantum = QUANTUM_MS;
	if (next != prev) {
		current = next;
		vm_activate(next->proc->vm);
		arch_context_switch(prev, next);
	}
	arch_irq_restore(f);
}

void sched_yield(void)
{
	schedule();
}

void sched_sleep(uint64_t ms)
{
	uint64_t f = arch_irq_save();
	current->wake_ms = arch_ticks_ms() + ms;
	current->state = T_SLEEPING;
	schedule();
	arch_irq_restore(f);
}

void sched_tick(void)
{
	uint64_t now = arch_ticks_ms();
	for (struct thread *t = threads; t; t = t->next)
		if (t->state == T_SLEEPING && now >= t->wake_ms)
			t->state = T_READY;
	if (--quantum <= 0 || current == &idle_thread)
		need_resched = 1;
}

void sched_irq_exit(void)
{
	if (need_resched) {
		need_resched = 0;
		schedule();
	}
}

void thread_exit(int code)
{
	arch_irq_disable();
	struct process *p = current->proc;
	current->state = T_DEAD;
	if (--p->threads == 0 && p != &kernel_process) {
		p->state = P_ZOMBIE;
		p->exit_code = code;
	}
	schedule();
	kpanic("thread_exit : retour d'un thread fini");
}

void proc_exit(int code)
{
	arch_irq_disable();
	struct process *p = current->proc;
	for (struct thread *t = threads; t; t = t->next)
		if (t->proc == p && t != current && t->state != T_DEAD) {
			t->state = T_DEAD;
			p->threads--;
		}
	thread_exit(code);
}

void sched_reap(void)
{
	uint64_t f = arch_irq_save();
	struct thread **pt = &threads;
	while (*pt) {
		struct thread *t = *pt;
		if (t->state == T_DEAD && t != current) {
			*pt = t->next;
			kfree((void *)t->kstack);
			kfree(t);
			continue;
		}
		pt = &t->next;
	}
	struct process **pp = &procs;
	while (*pp) {
		struct process *p = *pp;
		if (p->state == P_ZOMBIE && p->threads == 0) {
			*pp = p->next;
			arch_irq_restore(f);
			vm_destroy(p->vm);
			console_color(CON_DIM);
			kprintf("[process %d (%s) fini, code %d]\n", p->pid, p->name, p->exit_code);
			console_color(CON_NORMAL);
			kfree(p);
			f = arch_irq_save();
			pp = &procs;
			continue;
		}
		pp = &p->next;
	}
	arch_irq_restore(f);
}

/* ---- process */

#define USER_STACK_TOP 0x00007FFFFFFF0000ULL
#define USER_STACK_PAGES 16

#include <korin/elf.h>
#include <korin/errno.h>
#include <korin/pmm.h>

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
	if (r == 0) {
		for (int i = 1; i <= USER_STACK_PAGES && r == 0; i++) {
			uint64_t pg = pmm_alloc();
			if (!pg) {
				r = E_NOMEM;
				break;
			}
			memset(arch_phys_to_virt(pg), 0, PAGE_SIZE);
			r = vm_map_page(p->vm, USER_STACK_TOP - i * PAGE_SIZE, pg, VM_W);
		}
	}
	if (r) {
		vm_destroy(p->vm);
		kfree(p);
		return r;
	}

	/* interruptions coupées : le thread ne part pas avant d'avoir son entrée */
	uint64_t f = arch_irq_save();
	p->pid = next_pid++;
	p->state = P_ALIVE;
	p->next = procs;
	procs = p;
	struct thread *t = thread_create(p, p->name, user_start, 0);
	if (t) {
		t->user_entry = entry;
		t->user_stack = USER_STACK_TOP - 16;
	} else
		p->state = P_ZOMBIE; /* sans thread : libéré par sched_reap */
	arch_irq_restore(f);
	return t ? p->pid : E_NOMEM;
}

static const char *const state_names[] = {"prêt", "actif", "dort", "fini"};

void proc_list(void)
{
	uint64_t f = arch_irq_save();
	kprintf("  pid  nom              threads\n");
	for (struct process *p = procs; p; p = p->next) {
		kprintf("  %3d  %-16s %d%s\n", p->pid, p->name, p->threads,
			p->state == P_ZOMBIE ? " (fini)" : "");
		for (struct thread *t = threads; t; t = t->next)
			if (t->proc == p)
				kprintf("         tid %d %s : %s\n", t->tid, t->name, state_names[t->state]);
	}
	arch_irq_restore(f);
}
