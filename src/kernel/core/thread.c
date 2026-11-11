/* Threads des process et futex (docs/ABI.md, 0x20). Portable ; le registre
 * de thread (TLS) est celui de l'architecture (TPIDR_EL0 sur le Pi, FS sur
 * PC), donné par arch_set_tls. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/proc.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/vm.h>

#define PRIO_MAX 2
#define THL_SIZE 64

static void user_thread_start(void *arg)
{
	(void)arg;
	struct thread *t = sched_current();
	arch_set_tls(t->user_tls);
	arch_enter_user(t->user_entry, t->user_stack, t->user_arg);
}

int64_t sys_thread_create(uint64_t entry, uint64_t stack, uint64_t arg, uint64_t tls, uint64_t prio, uint64_t name)
{
	struct process *p = sched_current()->proc;
	if (!p->vm || entry >= vm_user_top || stack >= vm_user_top)
		return E_INVAL;
	if (prio > PRIO_MAX)
		return E_INVAL;
	if (prio == PRIO_MAX && !p->rights)
		return E_PERM;
	char nm[THREAD_NAME];
	memcpy(nm, p->name, THREAD_NAME - 1);
	nm[THREAD_NAME - 1] = 0;
	if (name && copy_string_from_user(nm, name, THREAD_NAME) < 0)
		nm[THREAD_NAME - 1] = 0;
	struct thread *t = thread_create(p, nm, user_thread_start, 0);
	if (!t)
		return E_NOMEM;
	t->user_entry = entry;
	t->user_stack = (stack & ~15ULL) - 8; /* comme après un call (System V) */
	t->user_arg = arg;
	t->user_tls = tls;
	t->priority = (int)prio;
	thread_start(t);
	return t->tid;
}

struct thread *thread_find(struct process *p, int tid)
{
	for (struct thread *t = sched_threads(); t; t = t->next)
		if (t->tid == tid && t->proc == p && t->idle_cpu < 0)
			return t;
	return 0;
}

int64_t sys_thread_join(int64_t tid)
{
	struct thread *me = sched_current();
	struct process *p = me->proc;
	if (tid == me->tid)
		return E_DEADLK;
	for (;;) {
		uint64_t f = sched_lock_take();
		struct thread *t = thread_find(p, (int)tid);
		if (!t || t->state == T_DEAD) {
			for (int i = 0; i < JOIN_KEEP; i++)
				if (p->join_tid[i] == tid && tid) {
					int code = p->join_code[i];
					sched_lock_give(f);
					return code;
				}
			sched_lock_give(f);
			return E_SRCH;
		}
		sched_lock_give(f);
		/* réveillé à sa fin ; revoit toutes les 50 ms (fin entre deux regards) */
		sched_prepare_block(50, t);
		if (sched_block() == WAKE_KILLED)
			return E_INTR;
	}
}

int64_t sys_thread_set_priority(int64_t tid, int64_t prio)
{
	struct thread *me = sched_current();
	if (prio < 0 || prio > PRIO_MAX)
		return E_INVAL;
	if (prio == PRIO_MAX && !me->proc->rights)
		return E_PERM;
	uint64_t f = sched_lock_take();
	struct thread *t = tid ? thread_find(me->proc, (int)tid) : me;
	int64_t old = t ? t->priority : E_SRCH;
	if (t)
		t->priority = (int)prio;
	sched_lock_give(f);
	return old;
}

int64_t sys_thread_set_name(int64_t tid, uint64_t name)
{
	struct thread *me = sched_current();
	char nm[THREAD_NAME];
	if (copy_string_from_user(nm, name, THREAD_NAME) < 0)
		nm[THREAD_NAME - 1] = 0;
	uint64_t f = sched_lock_take();
	struct thread *t = tid ? thread_find(me->proc, (int)tid) : me;
	if (t)
		memcpy(t->name, nm, THREAD_NAME);
	sched_lock_give(f);
	return t ? 0 : E_SRCH;
}

int64_t sys_thread_kill(int64_t tid, int64_t code)
{
	struct thread *me = sched_current();
	if (tid == 0 || tid == me->tid)
		thread_exit((int)code);
	uint64_t f = sched_lock_take();
	struct thread *t = thread_find(me->proc, (int)tid);
	sched_lock_give(f);
	if (!t)
		return E_SRCH;
	sched_kill_thread(t, (int)code);
	return 0;
}

int64_t sys_thread_list(int64_t pid, uint64_t buf, uint64_t max)
{
	if (max > 64)
		max = 64;
	uint64_t *e = kzalloc(max * THL_SIZE + THL_SIZE);
	if (!e)
		return E_NOMEM;
	uint64_t n = 0;
	uint64_t f = sched_lock_take();
	struct process *p = pid ? proc_get((int)pid) : sched_current()->proc;
	if (p)
		for (struct thread *t = sched_threads(); t && n < max; t = t->next) {
			if (t->proc != p || t->idle_cpu >= 0)
				continue;
			uint64_t *x = &e[n++ * 8];
			x[0] = (uint64_t)t->tid;
			x[1] = t->state == T_SLEEPING ? 1 : t->state == T_BLOCKED ? 2 : t->state == T_DEAD ? 3 : 0;
			x[2] = (uint64_t)t->priority;
			x[3] = (uint64_t)(int64_t)t->last_cpu;
			x[4] = t->runtime_ms;
			memcpy(&x[5], t->name, THREAD_NAME);
		}
	sched_lock_give(f);
	int64_t r = !p ? E_SRCH : copy_to_user(buf, e, n * THL_SIZE) ? E_FAULT : (int64_t)n;
	kfree(e);
	return r;
}

/* ---- futex : la clé est l'adresse physique du mot (vaut aussi pour la
 * mémoire partagée entre process) */

struct futex_wait {
	uint64_t key;
	struct thread *t;
	struct futex_wait *next;
};

static spinlock_t futex_lock = SPINLOCK_INIT;
static struct futex_wait *waiters;

static uint64_t futex_key(uint64_t addr)
{
	struct process *p = sched_current()->proc;
	return p->vm ? vm_translate(p->vm, addr) : 0;
}

int64_t sys_futex_wait(uint64_t addr, uint64_t val, uint64_t ms)
{
	if (addr & 3)
		return E_INVAL;
	if (user_check(addr, 4, 0))
		return E_FAULT;
	struct futex_wait w = {futex_key(addr), sched_current(), 0};
	if (!w.key)
		return E_FAULT;
	uint64_t f = spin_lock_irqsave(&futex_lock);
	if (*(volatile uint32_t *)addr != (uint32_t)val) {
		spin_unlock_irqrestore(&futex_lock, f);
		return E_AGAIN;
	}
	w.next = waiters;
	waiters = &w;
	sched_prepare_block(ms, &w);
	spin_unlock_irqrestore(&futex_lock, f);
	int r = sched_block();
	f = spin_lock_irqsave(&futex_lock);
	for (struct futex_wait **pw = &waiters; *pw; pw = &(*pw)->next)
		if (*pw == &w) {
			*pw = w.next;
			break;
		}
	spin_unlock_irqrestore(&futex_lock, f);
	return r == WAKE_TIMEOUT ? E_TIMEDOUT : r == WAKE_KILLED ? E_INTR : 0;
}

int64_t sys_futex_wake(uint64_t addr, uint64_t n)
{
	if (addr & 3)
		return E_INVAL;
	if (user_check(addr, 4, 0))
		return E_FAULT;
	uint64_t key = futex_key(addr);
	int64_t woken = 0;
	uint64_t f = spin_lock_irqsave(&futex_lock);
	for (struct futex_wait **pw = &waiters; *pw && (uint64_t)woken < n;) {
		struct futex_wait *w = *pw;
		if (w->key == key) {
			*pw = w->next;
			sched_wake(w->t);
			woken++;
		} else
			pw = &w->next;
	}
	spin_unlock_irqrestore(&futex_lock, f);
	return woken;
}
