/* Signaux et contrôle des tâches, comme le Pi (docs/ABI.md 0x2B-0x2F,
 * « Contrôle des tâches ») :
 *   livrés au retour en mode utilisateur, un par retour, sur la pile du
 *   thread (ou celle de SIG_ALTSTACK avec SA_ONSTACK) ;
 *   défaut : URG, CONT, CHLD, WINCH ignorés ; STOP, TSTP, TTIN, TTOU arrêtent
 *   le process ; les autres le finissent avec le code -signal ;
 *   un thread endormi ou bloqué qui ne masque pas le signal est réveillé
 *   (SLEEP, attentes : E_INTR ; SVC_CALL, JOIN, WAIT : non interrompus) ;
 *   arrêt : chaque thread s'arrête à son retour en mode utilisateur ;
 *   SIGCONT reprend, même masqué ou ignoré.
 * Verrou sig_lock, puis celui de l'ordonnanceur. Portable. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/svc.h>
#include <korin/proc.h>
#include <korin/sched.h>
#include <korin/signal.h>
#include <korin/spinlock.h>

#define BIT(s) (1ULL << (s)) /* comme le Pi (signal.asm : 1 << signal) */
#define STOPS (BIT(SIGSTOP) | BIT(SIGTSTP) | BIT(SIGTTIN) | BIT(SIGTTOU))
#define UNBLOCKABLE (BIT(SIGKILL) | BIT(SIGSTOP))

enum { SIGM_SET, SIGM_BLOCK, SIGM_UNBLOCK, SIGM_PENDING, SIGM_SUSPEND, SIGM_WAIT };

static spinlock_t sig_lock = SPINLOCK_INIT;
static int stop_key;   /* threads arrêtés : attendent ici */
static int sigwait_key;

static int default_ignored(int s)
{
	return s == SIGURG || s == SIGCONT || s == SIGCHLD || s == SIGWINCH;
}

static int ignored(struct process *p, int s)
{
	uint64_t h = p->act[s - 1].handler;
	return h == 1 || (h == 0 && default_ignored(s));
}

int64_t sys_sig_action(uint64_t sig, uint64_t handler, uint64_t flags, uint64_t mask, uint64_t restorer)
{
	if (sig < 1 || sig > NSIG || sig == SIGKILL || sig == SIGSTOP)
		return E_INVAL;
	struct process *p = sched_current()->proc;
	uint64_t f = spin_lock_irqsave(&sig_lock);
	p->act[sig - 1] = (struct sigact){handler, flags, mask & ~UNBLOCKABLE};
	if (restorer)
		p->restorer = restorer;
	if (ignored(p, (int)sig))
		p->sigpending &= ~BIT(sig); /* un signal ignoré n'attend plus */
	spin_unlock_irqrestore(&sig_lock, f);
	return 0;
}

/* le père est-il averti ? (sous sig_lock) */
static void job_event(struct process *p, int ev)
{
	p->jobev = ev;
	uint64_t f = sched_lock_take();
	struct process *parent = proc_get(p->parent);
	sched_lock_give(f);
	if (parent && parent->act[SIGCHLD - 1].handler > 1 && !(parent->act[SIGCHLD - 1].flags & SA_NOCLDSTOP))
		parent->sigpending |= BIT(SIGCHLD);
	proc_wake_waiters();
}

/* le thread à réveiller pour un signal du process : d'abord celui qui l'attend (SIGM_WAIT, ou
 * SIGM_SUSPEND qui le démasque), puis le premier thread du process (le plus petit tid, comme le
 * chef du groupe de Linux), puis un autre qui ne le masque pas. Avant : le premier de la liste
 * (le plus récent) ; un thread auxiliaire qui ne masquait pas SIGCHLD était réveillé et prenait
 * le signal, et le thread principal restait dans sigsuspend (réveil perdu). */
static void wake_for(struct process *p, int sig)
{
	struct thread *waiting = 0, *first = 0;
	uint64_t f = sched_lock_take();
	for (struct thread *t = sched_threads(); t; t = t->next) {
		if (t->proc != p || t->state == T_DEAD)
			continue;
		if ((t->sigwait_set & BIT(sig)) || (t->wait_obj == &sigwait_key && !(t->sigmask & BIT(sig)))) {
			waiting = t;
			break;
		}
		if (!(t->sigmask & BIT(sig)) && (!first || t->tid < first->tid))
			first = t;
	}
	if (waiting || first) /* (sous le verrou : le thread ne peut pas être rendu entre-temps) */
		sched_wake_signal_locked(waiting ? waiting : first);
	sched_lock_give(f);
}

static void resume(struct process *p)
{
	p->stop_sig = 0;
	sched_wake_all(&stop_key);
}

/* envoi à un process (sig_lock pris) ; tid : un thread précis, ou 0 ; from : pid de l'envoyeur (si_pid du
 * gestionnaire, 0 : le noyau) */
static int send_locked(struct process *p, struct thread *t, int sig, int from)
{
	if (sig == 0)
		return 0;
	if (sig == SIGKILL) {
		if (p->stop_sig)
			resume(p);
		sched_kill_process(p, -SIGKILL);
		return 0;
	}
	if (sig == SIGCONT) {
		p->sigpending &= ~STOPS;
		if (p->stop_sig) {
			resume(p);
			job_event(p, JEV_CONT);
		}
		if (p->act[SIGCONT - 1].handler <= 1)
			return 0;
	}
	if ((BIT(sig) & STOPS) && (sig == SIGSTOP || p->act[sig - 1].handler == 0)) {
		/* arrêt décidé à l'envoi si un thread ne le masque pas */
		int unmasked = sig == SIGSTOP;
		uint64_t f = sched_lock_take();
		for (struct thread *x = sched_threads(); x && !unmasked; x = x->next)
			unmasked = x->proc == p && x->state != T_DEAD && !(x->sigmask & BIT(sig));
		sched_lock_give(f);
		if (unmasked) {
			p->sigpending &= ~BIT(SIGCONT);
			if (!p->stop_sig) {
				p->stop_sig = sig;
				job_event(p, JEV_STOP | sig);
			}
			return 0;
		}
	}
	if (ignored(p, sig))
		return 0;
	if (t) {
		t->sigpending |= BIT(sig);
		t->sigfrom[sig] = from;
	} else {
		p->sigpending |= BIT(sig);
		p->sigfrom[sig] = from;
	}
	wake_for(p, sig);
	return 0;
}

int signal_send_proc(struct process *p, int sig)
{
	uint64_t f = spin_lock_irqsave(&sig_lock);
	int r = send_locked(p, 0, sig, 0);
	spin_unlock_irqrestore(&sig_lock, f);
	return r;
}

#define SIGF_TTY 0x10000

/* le signal est-il masqué par tous les threads vivants de p ? */
static int masked_by_all(struct process *p, int sig)
{
	int all = 1;
	uint64_t f = sched_lock_take();
	for (struct thread *t = sched_threads(); t; t = t->next)
		if (t->proc == p && t->state != T_DEAD && !(t->sigmask & BIT(sig))) {
			all = 0;
			break;
		}
	sched_lock_give(f);
	return all;
}

/* groupe orphelin : aucun de ses process vivants n'a un père vivant dans un autre groupe de la
 * même session (pg_orphan de jobs.asm) */
static int pg_orphan(int pgid)
{
	int orphan = 1;
	uint64_t f = sched_lock_take();
	for (struct process *q = *proc_list_head(); q && orphan; q = q->next) {
		if (q->pgid != pgid || q->state != P_ALIVE || !q->parent)
			continue;
		struct process *par = proc_get(q->parent);
		if (par && par->state == P_ALIVE && par->pgid != pgid && par->sid == q->sid)
			orphan = 0;
	}
	sched_lock_give(f);
	return orphan;
}

int64_t sys_sig_send(int64_t pid, int64_t tid, uint64_t sig)
{
	struct process *me = sched_current()->proc;
	int tty = (sig & SIGF_TTY) != 0;
	sig &= 0xFFFF;
	if (sig > NSIG)
		return E_INVAL;
	if (pid < 0) { /* un groupe : chaque process, sauf ceux qui ont les droits */
		int hit = 0;
		uint64_t f = sched_lock_take();
		struct process *list[64];
		int n = 0;
		for (struct process *p = *proc_list_head(); p && n < 64; p = p->next)
			if (p->pgid == -pid && p->state == P_ALIVE && !p->rights && p != &kernel_process)
				list[n++] = p;
		sched_lock_give(f);
		for (int i = 0; i < n; i++, hit++)
			signal_send_proc(list[i], (int)sig);
		return hit ? 0 : E_SRCH;
	}
	uint64_t f = sched_lock_take();
	struct process *p = pid ? proc_get((int)pid) : me;
	struct thread *t = tid && p ? thread_find(p, (int)tid) : 0;
	sched_lock_give(f);
	if (!p || p->state != P_ALIVE || (tid && !t))
		return E_SRCH;
	if (tty) { /* signal d'un terminal pour un lecteur / écrivain en arrière-plan (comme jobs.asm du Pi) */
		if (sig != SIGTTIN && sig != SIGTTOU)
			return E_INVAL;
		if (ignored(p, (int)sig) || masked_by_all(p, (int)sig))
			return 1; /* ignoré, ou masqué par tous ses threads : rien d'envoyé (un shell qui
				   * fait tcsetpgrp avec SIGTTOU masqué ne doit pas s'arrêter ensuite) */
		if (pg_orphan(p->pgid))
			return 2; /* groupe orphelin : rien d'envoyé, le terminal rend E_IO */
		sys_sig_send(-p->pgid, 0, sig);
		return 0;
	}
	f = spin_lock_irqsave(&sig_lock);
	int r = send_locked(p, t, (int)sig, me->pid);
	spin_unlock_irqrestore(&sig_lock, f);
	return r;
}

/* ---- livraison */

static int fault_sig_code;
static uint64_t fault_sig_addr;

static int deliver(void *frame, struct thread *t, int sig, int code, uint64_t addr)
{
	struct process *p = t->proc;
	struct sigact a = p->act[sig - 1];
	uint64_t old = t->restore_mask ? t->saved_mask : t->sigmask;
	t->restore_mask = 0;
	uint64_t top = 0;
	if ((a.flags & SA_ONSTACK) && t->alt_size) {
		uint64_t sp = arch_frame_sp(frame);
		if (!(sp > t->alt_sp && sp <= t->alt_sp + t->alt_size))
			top = t->alt_sp + t->alt_size;
	}
	if (arch_signal_frame(frame, sig, code, addr, a.handler, p->restorer, old, top))
		return -1;
	t->sigmask |= a.mask | (a.flags & SA_NODEFER ? 0 : BIT(sig));
	t->sigmask &= ~UNBLOCKABLE;
	if (a.flags & SA_RESETHAND)
		p->act[sig - 1].handler = 0;
	return 0;
}

void signal_user_return(void *frame)
{
	struct thread *t = sched_current();
	struct process *p = t->proc;
	if (!p->vm)
		return;
	for (;;) {
		sched_user_return(); /* THREAD_KILL, KILL */
		if (p->stop_sig) {    /* arrêté : attend SIGCONT (ou SIGKILL) */
			sched_prepare_block(0, &stop_key);
			if (p->stop_sig)
				sched_block();
			else
				sched_wake(t), sched_block();
			continue;
		}
		uint64_t f = spin_lock_irqsave(&sig_lock);
		uint64_t ready = (t->sigpending | p->sigpending) & ~t->sigmask;
		if (!ready) {
			spin_unlock_irqrestore(&sig_lock, f);
			return;
		}
		int sig = __builtin_ctzll(ready);
		int from;
		if (t->sigpending & BIT(sig)) {
			t->sigpending &= ~BIT(sig);
			from = t->sigfrom[sig];
		} else {
			p->sigpending &= ~BIT(sig);
			from = p->sigfrom[sig];
		}
		uint64_t h = p->act[sig - 1].handler;
		if (h == 1 || (h == 0 && default_ignored(sig))) {
			spin_unlock_irqrestore(&sig_lock, f);
			continue;
		}
		if (h == 0 && (BIT(sig) & STOPS)) { /* arrêt décidé à la livraison */
			if (!p->stop_sig) {
				p->stop_sig = sig;
				job_event(p, JEV_STOP | sig);
			}
			spin_unlock_irqrestore(&sig_lock, f);
			continue;
		}
		if (h == 0) {
			spin_unlock_irqrestore(&sig_lock, f);
			proc_exit(-sig);
		}
		/* si_pid (à la place de l'adresse d'une faute, comme Linux) : .NET n'accepte l'injection
		 * d'activation (suspension pour le GC) que de son propre process */
		int r = deliver(frame, t, sig, 0, (uint64_t)(uint32_t)from);
		spin_unlock_irqrestore(&sig_lock, f);
		if (r) /* pile illisible : tué */
			proc_exit(-SIGSEGV);
		return;
	}
}

int signal_fault(void *frame, int sig, int code, uint64_t addr)
{
	struct thread *t = sched_current();
	struct process *p = t->proc;
	uint64_t f = spin_lock_irqsave(&sig_lock);
	int ok = p->act[sig - 1].handler > 1 && !(t->sigmask & BIT(sig));
	if (ok && deliver(frame, t, sig, code, addr))
		ok = 0;
	spin_unlock_irqrestore(&sig_lock, f);
	(void)fault_sig_code;
	(void)fault_sig_addr;
	return ok;
}

int64_t sys_sig_return(void)
{
	struct thread *t = sched_current();
	uint64_t mask;
	if (arch_signal_return(t->uregs, &mask))
		proc_exit(-SIGSEGV);
	t->sigmask = mask & ~UNBLOCKABLE;
	t->frame_replaced = 1;
	return 0;
}

int64_t sys_sig_mask(uint64_t how, uint64_t set)
{
	struct thread *t = sched_current();
	struct process *p = t->proc;
	set &= ~UNBLOCKABLE;
	uint64_t old = t->sigmask;
	switch (how) {
	case SIGM_SET:
		t->sigmask = set;
		return (int64_t)old;
	case SIGM_BLOCK:
		t->sigmask |= set;
		return (int64_t)old;
	case SIGM_UNBLOCK:
		t->sigmask &= ~set;
		return (int64_t)old;
	case SIGM_PENDING:
		return (int64_t)(t->sigpending | p->sigpending);
	case SIGM_SUSPEND:
		t->saved_mask = old;
		t->restore_mask = 1;
		t->sigmask = set;
		while (!((t->sigpending | p->sigpending) & ~t->sigmask) && !t->kill_pending) {
			sched_prepare_block(0, &sigwait_key);
			if (((t->sigpending | p->sigpending) & ~t->sigmask))
				sched_wake(t);
			sched_block();
		}
		if (!((t->sigpending | p->sigpending) & ~t->sigmask)) {
			t->sigmask = old;
			t->restore_mask = 0;
		}
		return E_INTR;
	case SIGM_WAIT:
		for (;;) {
			uint64_t f = spin_lock_irqsave(&sig_lock);
			uint64_t hit = (t->sigpending | p->sigpending) & set;
			if (hit) {
				int sig = __builtin_ctzll(hit);
				t->sigpending &= ~BIT(sig);
				p->sigpending &= ~BIT(sig);
				t->sigwait_set = 0;
				spin_unlock_irqrestore(&sig_lock, f);
				return sig;
			}
			t->sigwait_set = set;
			sched_prepare_block(0, &sigwait_key);
			spin_unlock_irqrestore(&sig_lock, f);
			if (sched_block() == WAKE_KILLED) {
				t->sigwait_set = 0;
				return E_INTR;
			}
		}
	}
	return E_INVAL;
}

int64_t sys_sig_altstack(uint64_t sp, uint64_t size, uint64_t out)
{
	struct thread *t = sched_current();
	uint64_t o[2] = {t->alt_sp, t->alt_size};
	if (out && copy_to_user(out, o, 16))
		return E_FAULT;
	if (sp != (uint64_t)-1) {
		t->alt_sp = size ? sp : 0;
		t->alt_size = size;
	}
	return 0;
}

/* ---- groupes et sessions */

int64_t sys_setpgid(int64_t pid, int64_t pgid)
{
	struct process *me = sched_current()->proc;
	uint64_t f = sched_lock_take();
	int64_t r = 0;
	if (pid == -1) { /* setsid */
		if (me->pgid == me->pid)
			r = E_PERM;
		else
			me->sid = me->pgid = me->pid;
		sched_lock_give(f);
		return r < 0 ? r : me->sid;
	}
	struct process *p = pid ? proc_get((int)pid) : me;
	if (!p)
		r = E_SRCH;
	else if (pgid < 0)
		r = E_INVAL;
	else if ((p != me && p->parent != me->pid) || p->sid != me->sid || p->sid == p->pid)
		r = E_PERM;
	else
		p->pgid = pgid ? (int)pgid : p->pid;
	sched_lock_give(f);
	return r;
}

#define GETPGID_SID (1ULL << 62)

int64_t sys_getpgid(int64_t pid)
{
	int sid = ((uint64_t)pid & GETPGID_SID) != 0;
	pid &= ~(int64_t)GETPGID_SID;
	uint64_t f = sched_lock_take();
	struct process *p = pid ? proc_get((int)pid) : sched_current()->proc;
	int64_t r = !p ? E_SRCH : sid ? p->sid : p->pgid;
	sched_lock_give(f);
	return r;
}

/* ---- SETITIMER (ITIMER_REAL) : SIGALRM, réarmée si l'intervalle n'est pas nul */

int64_t sys_setitimer(uint64_t which, int64_t value_us, int64_t interval_us, uint64_t old)
{
	if (which != 0 || interval_us < 0)
		return E_INVAL;
	struct process *p = sched_current()->proc;
	uint64_t now = arch_ticks_ms();
	int64_t o[2] = {0, (int64_t)p->alarm_iv_ms * 1000};
	if (p->alarm_ms)
		o[0] = p->alarm_ms > now ? (int64_t)(p->alarm_ms - now) * 1000 : 1;
	if (old && copy_to_user(old, o, 16))
		return E_FAULT;
	if (value_us == -1)
		return 0;
	p->alarm_iv_ms = (uint64_t)(interval_us + 999) / 1000;
	p->alarm_ms = value_us > 0 ? now + (uint64_t)(value_us + 999) / 1000 : 0;
	return 0;
}

void signal_alarm_thread(void *arg)
{
	(void)arg;
	uint64_t last_low = 0;
	for (;;) {
		sched_sleep(5);
		uint64_t now = arch_ticks_ms();
		if (pmm_free_pages() < (64ULL << 20) / PAGE_SIZE && now - last_low >= 1000) {
			last_low = now;
			svc_low_memory();
		}
		struct process *due[32];
		int n = 0;
		uint64_t f = sched_lock_take();
		for (struct process *p = *proc_list_head(); p && n < 32; p = p->next)
			if (p->alarm_ms && now >= p->alarm_ms && p->state == P_ALIVE) {
				p->alarm_ms = p->alarm_iv_ms ? now + p->alarm_iv_ms : 0;
				due[n++] = p;
			}
		sched_lock_give(f);
		for (int i = 0; i < n; i++)
			signal_send_proc(due[i], SIGALRM);
	}
}

void signal_child_exit(struct process *p)
{
	uint64_t f = sched_lock_take();
	struct process *parent = proc_get(p->parent);
	sched_lock_give(f);
	if (parent && parent->act[SIGCHLD - 1].handler > 1)
		signal_send_proc(parent, SIGCHLD);
}
