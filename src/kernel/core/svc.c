/* Services nommés : un programme qui a les droits de SD0:/sys s'enregistre
 * sous un nom (vfs, netd…) ; les autres l'ouvrent par ce nom et lui envoient
 * des requêtes de 512 octets (docs/ABI.md, 0x30 ; src/kernel/svc.asm du Pi).
 *
 * Requête : en file, prise par SVC_NEXT, finie par SVC_REPLY (ou passée à un
 * autre service par SVC_FORWARD). L'appelant dort jusqu'à la réponse, 30 s
 * au plus. Fin d'un service : ses requêtes échouent (E_PIPE). Fin d'un
 * appelant : ses requêtes sont abandonnées. Avis du noyau (fins, naissances)
 * : requêtes sans appelant ni réponse.
 *
 * Verrou svc_lock, puis celui de l'ordonnanceur (jamais l'inverse). Portable. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/vm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>
#include <korin/svc.h>

#define SVC_MAX 64
#define SVC_NAME 32
#define SVC_CALL_MS 30000
#define REQ_SIGNAL 0x7FFFFFFF /* SVC_NEXT : op SVC_OP_SIGNAL, pas de requête */
#define MSG_PID 44

enum { R_QUEUED, R_TAKEN, R_DONE, R_ABANDONED };

struct request {
	uint64_t num;
	int svc;
	int state;
	struct thread *caller; /* 0 : avis du noyau */
	int caller_pid;
	int64_t result;
	struct request *next;  /* toutes les requêtes */
	uint8_t msg[SVC_MSG];
};

#define MAPC_BASE 0x6E00000000ULL /* + n x 1 Mo dans le service */
#define MAPC_SIZE (1ULL << 20)
#define MAPC_MAX 16
#define MC_WRITE 1

struct mapc {
	int pid;            /* service qui a la vue, 0 : libre */
	uint64_t num;       /* requête */
	uint64_t base;      /* adresse rendue (début de page) */
	uint64_t pages;
	uint64_t phys[MAPC_SIZE / PAGE_SIZE];
};

static struct mapc mapcs[MAPC_MAX * 4];

struct service {
	char name[SVC_NAME];
	int pid;               /* 0 : libre */
	uint32_t flags;
	volatile uint64_t signals;
};

static struct service svcs[SVC_MAX];
static struct request *reqs; /* en file et prises, dans l'ordre d'arrivée */
static uint64_t next_num = 1;
static spinlock_t svc_lock = SPINLOCK_INIT;
static int open_key; /* SVC_OPEN attend un service sur cette adresse */

static struct process *me(void)
{
	return sched_current()->proc;
}

static void set_pid(uint8_t *msg, int pid)
{
	uint32_t v = (uint32_t)pid;
	memcpy(msg + MSG_PID, &v, 4);
}

/* ajoute en fin de liste (sous le verrou) */
static void enqueue(struct request *r)
{
	r->next = 0;
	struct request **pr = &reqs;
	while (*pr)
		pr = &(*pr)->next;
	*pr = r;
}

static void unlink_req(struct request *r)
{
	for (struct request **pr = &reqs; *pr; pr = &(*pr)->next)
		if (*pr == r) {
			*pr = r->next;
			return;
		}
}

static int by_name(const char *name)
{
	for (int i = 0; i < SVC_MAX; i++)
		if (svcs[i].pid && !strcmp(svcs[i].name, name))
			return i;
	return -1;
}

int64_t sys_svc_register(uint64_t uname, uint64_t flags)
{
	struct process *p = me();
	if (!p->rights)
		return E_PERM;
	char name[SVC_NAME];
	if (copy_string_from_user(name, uname, SVC_NAME) <= 0)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	int64_t r = E_NOMEM;
	if (by_name(name) >= 0)
		r = E_EXIST;
	else
		for (int i = 0; i < SVC_MAX; i++)
			if (!svcs[i].pid) {
				memcpy(svcs[i].name, name, SVC_NAME);
				svcs[i].pid = p->pid;
				svcs[i].flags = (uint32_t)flags;
				svcs[i].signals = 0;
				r = i;
				break;
			}
	spin_unlock_irqrestore(&svc_lock, f);
	if (r >= 0)
		sched_wake_all(&open_key);
	return r;
}

int64_t sys_svc_open(uint64_t uname, uint64_t ms)
{
	char name[SVC_NAME];
	if (copy_string_from_user(name, uname, SVC_NAME) <= 0)
		return E_INVAL;
	uint64_t deadline = arch_ticks_ms() + ms;
	for (;;) {
		uint64_t f = spin_lock_irqsave(&svc_lock);
		int i = by_name(name);
		uint64_t now = arch_ticks_ms();
		if (i >= 0 || now >= deadline) {
			spin_unlock_irqrestore(&svc_lock, f);
			return i >= 0 ? i : E_NOENT;
		}
		sched_prepare_block(deadline - now, &open_key);
		spin_unlock_irqrestore(&svc_lock, f);
		if (sched_block() == WAKE_KILLED)
			return E_INTR;
	}
}

int64_t sys_svc_call(uint64_t chan, uint64_t umsg)
{
	if (chan >= SVC_MAX)
		return E_INVAL;
	struct request *r = kmalloc(sizeof *r);
	if (!r)
		return E_NOMEM;
	if (copy_from_user(r->msg, umsg, SVC_MSG)) {
		kfree(r);
		return E_FAULT;
	}
	struct thread *t = sched_current();
	r->caller = t;
	r->caller_pid = t->proc->pid;
	set_pid(r->msg, r->caller_pid);
	r->svc = (int)chan;
	r->state = R_QUEUED;

	uint64_t f = spin_lock_irqsave(&svc_lock);
	if (!svcs[chan].pid) {
		spin_unlock_irqrestore(&svc_lock, f);
		kfree(r);
		return E_PIPE;
	}
	r->num = next_num++;
	if (next_num >= REQ_SIGNAL)
		next_num = 1;
	enqueue(r);
	sched_wake_all(&svcs[chan]); /* (svc_lock puis l'ordonnanceur : permis) */
	sched_prepare_block(SVC_CALL_MS, r);
	spin_unlock_irqrestore(&svc_lock, f);

	int why = sched_block();
	f = spin_lock_irqsave(&svc_lock);
	while (r->state != R_DONE && (why == WAKE_OK || why == WAKE_SIGNAL)) { /* pas interrompu par un signal */
		sched_prepare_block(SVC_CALL_MS, r);
		spin_unlock_irqrestore(&svc_lock, f);
		why = sched_block();
		f = spin_lock_irqsave(&svc_lock);
	}
	int64_t res;
	if (r->state == R_DONE) {
		unlink_req(r);
		res = r->result;
		spin_unlock_irqrestore(&svc_lock, f);
		if (copy_to_user(umsg, r->msg, SVC_MSG))
			res = E_FAULT;
		kfree(r);
		return res;
	}
	/* délai ou thread tué : la requête est abandonnée */
	if (r->state == R_QUEUED) {
		unlink_req(r);
		kfree(r);
	} else {
		r->state = R_ABANDONED;
		r->caller = 0;
	}
	spin_unlock_irqrestore(&svc_lock, f);
	return why == WAKE_KILLED ? E_INTR : E_TIMEDOUT;
}

/* finit une requête (sous le verrou) */
static void finish(struct request *r, int64_t result)
{
	if (r->state == R_ABANDONED || !r->caller) {
		unlink_req(r);
		kfree(r);
		return;
	}
	r->result = result;
	r->state = R_DONE;
	sched_wake(r->caller);
}

int64_t sys_svc_next(uint64_t chan, uint64_t umsg, uint64_t ms)
{
	struct process *p = me();
	if (chan >= SVC_MAX)
		return E_INVAL;
	if (user_check(umsg, SVC_MSG, 1))
		return E_FAULT;
	uint64_t deadline = arch_ticks_ms() + ms;
	uint8_t *out = kmalloc(SVC_MSG);
	if (!out)
		return E_NOMEM;
	int64_t ret;
	for (;;) {
		uint64_t f = spin_lock_irqsave(&svc_lock);
		if (svcs[chan].pid != p->pid) {
			spin_unlock_irqrestore(&svc_lock, f);
			ret = E_PERM;
			break;
		}
		uint64_t bits = __atomic_exchange_n(&svcs[chan].signals, 0, __ATOMIC_ACQ_REL);
		if (bits) {
			spin_unlock_irqrestore(&svc_lock, f);
			memset(out, 0, 48);
			uint64_t op = SVC_OP_SIGNAL;
			memcpy(out, &op, 8);
			memcpy(out + 8, &bits, 8);
			ret = REQ_SIGNAL;
			if (copy_to_user(umsg, out, 48))
				ret = E_FAULT;
			break;
		}
		struct request *r = reqs;
		while (r && !(r->svc == (int)chan && r->state == R_QUEUED))
			r = r->next;
		if (r) {
			memcpy(out, r->msg, SVC_MSG);
			if (r->caller) {
				r->state = R_TAKEN;
				ret = (int64_t)r->num;
			} else { /* avis du noyau : pas de réponse */
				unlink_req(r);
				kfree(r);
				ret = REQ_SIGNAL - 1;
			}
			spin_unlock_irqrestore(&svc_lock, f);
			if (copy_to_user(umsg, out, SVC_MSG))
				ret = E_FAULT;
			break;
		}
		uint64_t now = arch_ticks_ms();
		if (ms == 0 || now >= deadline) {
			spin_unlock_irqrestore(&svc_lock, f);
			ret = 0;
			break;
		}
		sched_prepare_block(deadline - now, &svcs[chan]);
		spin_unlock_irqrestore(&svc_lock, f);
		if (sched_block() == WAKE_KILLED) {
			ret = E_INTR;
			break;
		}
	}
	kfree(out);
	return ret;
}

/* la requête prise numéro num, d'un service de p (sous le verrou) */
static struct request *taken(struct process *p, uint64_t num)
{
	for (struct request *r = reqs; r; r = r->next)
		if (r->num == num && (r->state == R_TAKEN || r->state == R_ABANDONED) && svcs[r->svc].pid == p->pid)
			return r;
	return 0;
}

static void unmap_one(struct mapc *m, struct process *svc)
{
	for (uint64_t i = 0; i < m->pages; i++) {
		vm_unmap_page(svc->vm, m->base + i * PAGE_SIZE, 0);
		pmm_free(m->phys[i]); /* l'épingle */
	}
	m->pid = 0;
}

/* les vues d'une requête (ou toutes celles du process si num = 0) */
static void unmap_request(struct process *svc, uint64_t num)
{
	int any = 0;
	for (int i = 0; i < MAPC_MAX * 4; i++)
		if (mapcs[i].pid == svc->pid && (!num || mapcs[i].num == num)) {
			unmap_one(&mapcs[i], svc);
			any = 1;
		}
	if (any && svc->vm && svc == me())
		vm_tlb_shootdown(svc->vm);
}

int64_t sys_svc_map_caller(uint64_t num, uint64_t addr, uint64_t size, uint64_t flags)
{
	struct process *svc = me();
	if (!svc->rights)
		return E_PERM;
	if (size == 0 || size > MAPC_SIZE || addr + size < addr)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	struct request *r = taken(svc, num);
	int caller_pid = r && r->state == R_TAKEN ? r->caller_pid : 0;
	int slot = -1, mine = 0;
	for (int i = 0; i < MAPC_MAX * 4; i++) {
		if (mapcs[i].pid == svc->pid)
			mine |= 1 << ((mapcs[i].base - MAPC_BASE) / MAPC_SIZE);
		else if (!mapcs[i].pid && slot < 0)
			slot = i;
	}
	int n = -1;
	for (int k = 0; k < MAPC_MAX && n < 0; k++)
		if (!(mine & (1 << k)))
			n = k;
	if (caller_pid && slot >= 0 && n >= 0) {
		mapcs[slot].pid = svc->pid; /* réservée */
		mapcs[slot].num = num;
		mapcs[slot].base = MAPC_BASE + (uint64_t)n * MAPC_SIZE;
		mapcs[slot].pages = 0;
	}
	spin_unlock_irqrestore(&svc_lock, f);
	if (!caller_pid)
		return E_PIPE;
	if (slot < 0 || n < 0)
		return E_BUSY;
	struct mapc *m = &mapcs[slot];
	f = sched_lock_take();
	struct process *caller = proc_get(caller_pid);
	sched_lock_give(f);
	int write = (flags & MC_WRITE) != 0;
	uint64_t first = ALIGN_DOWN(addr, PAGE_SIZE), end = ALIGN_UP(addr + size, PAGE_SIZE);
	int64_t ret = caller && caller->vm ? 0 : E_PIPE;
	for (uint64_t va = first; ret == 0 && va < end; va += PAGE_SIZE) {
		int fl = vm_page_flags(caller->vm, va);
		if (!(fl & VM_PRESENT) || (write && !(fl & VM_W)))
			if (mm_fault(caller, va, write, 0)) { /* l'appelant dort dans SVC_CALL */
				ret = E_FAULT;
				break;
			}
		uint64_t phys = vm_translate(caller->vm, va) & ~(PAGE_SIZE - 1);
		if (!phys) {
			ret = E_FAULT;
			break;
		}
		pmm_ref(phys); /* épinglée : elle reste tant que la vue existe */
		m->phys[m->pages] = phys;
		if (vm_map_page(svc->vm, m->base + m->pages * PAGE_SIZE, phys, write ? VM_W : 0)) {
			pmm_free(phys);
			ret = E_NOMEM;
			break;
		}
		m->pages++;
	}
	if (ret) {
		f = spin_lock_irqsave(&svc_lock);
		unmap_one(m, svc);
		spin_unlock_irqrestore(&svc_lock, f);
		return ret;
	}
	return (int64_t)(m->base + (addr - first));
}

int64_t sys_svc_unmap_caller(uint64_t addr)
{
	struct process *svc = me();
	uint64_t f = spin_lock_irqsave(&svc_lock);
	int found = 0;
	for (int i = 0; i < MAPC_MAX * 4 && !found; i++)
		if (mapcs[i].pid == svc->pid && addr >= mapcs[i].base && addr < mapcs[i].base + MAPC_SIZE) {
			unmap_one(&mapcs[i], svc);
			found = 1;
		}
	spin_unlock_irqrestore(&svc_lock, f);
	if (!found)
		return E_INVAL;
	vm_tlb_shootdown(svc->vm);
	return 0;
}

int64_t sys_svc_caller_phys(uint64_t addr, uint64_t out, uint64_t places)
{
	struct process *svc = me();
	uint64_t ph[MAPC_SIZE / PAGE_SIZE];
	uint64_t n = 0;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	for (int i = 0; i < MAPC_MAX * 4; i++)
		if (mapcs[i].pid == svc->pid && addr >= mapcs[i].base && addr < mapcs[i].base + MAPC_SIZE) {
			n = mapcs[i].pages < places ? mapcs[i].pages : places;
			memcpy(ph, mapcs[i].phys, n * 8);
			break;
		}
	spin_unlock_irqrestore(&svc_lock, f);
	if (!n)
		return E_INVAL;
	return copy_to_user(out, ph, n * 8) ? E_FAULT : (int64_t)n;
}

int64_t sys_svc_reply(uint64_t num, uint64_t umsg)
{
	uint8_t *in = kmalloc(SVC_MSG);
	if (!in)
		return E_NOMEM;
	if (copy_from_user(in, umsg, SVC_MSG)) {
		kfree(in);
		return E_FAULT;
	}
	int64_t ret = 0;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	struct request *r = taken(me(), num);
	unmap_request(me(), num);
	if (!r)
		ret = E_INVAL;
	else {
		memcpy(r->msg, in, SVC_MSG);
		int64_t res;
		memcpy(&res, in, 8);
		finish(r, res);
	}
	spin_unlock_irqrestore(&svc_lock, f);
	kfree(in);
	return ret;
}

int64_t sys_svc_signal(uint64_t chan, uint64_t bits)
{
	if (chan >= SVC_MAX)
		return E_INVAL;
	svc_signal_kernel((int)chan, bits);
	return 0;
}

void svc_signal_kernel(int chan, uint64_t bits)
{
	if (chan < 0 || chan >= SVC_MAX || !bits)
		return;
	uint64_t old = __atomic_fetch_or(&svcs[chan].signals, bits, __ATOMIC_ACQ_REL);
	if (!old)
		sched_wake_all(&svcs[chan]);
}

int64_t sys_svc_forward(uint64_t num, uint64_t chan, uint64_t umsg)
{
	if (chan >= SVC_MAX)
		return E_INVAL;
	uint8_t *in = kmalloc(SVC_MSG);
	if (!in)
		return E_NOMEM;
	if (copy_from_user(in, umsg, SVC_MSG)) {
		kfree(in);
		return E_FAULT;
	}
	int64_t ret = 0;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	struct request *r = taken(me(), num);
	unmap_request(me(), num);
	if (!r)
		ret = E_INVAL;
	else if (r->state == R_ABANDONED) {
		unlink_req(r);
		kfree(r);
	} else if (!svcs[chan].pid)
		ret = E_NOENT; /* la requête reste prise : le service répond */
	else {
		memcpy(r->msg, in, SVC_MSG);
		set_pid(r->msg, r->caller_pid);
		r->svc = (int)chan;
		r->state = R_QUEUED;
		sched_wake_all(&svcs[chan]);
	}
	spin_unlock_irqrestore(&svc_lock, f);
	kfree(in);
	return ret;
}

/* ---- avis du noyau et fins de process */

void svc_notify(uint32_t flag, uint64_t op, int64_t a, int64_t b)
{
	uint64_t f = spin_lock_irqsave(&svc_lock);
	for (int i = 0; i < SVC_MAX; i++) {
		if (!svcs[i].pid || !(svcs[i].flags & flag))
			continue;
		struct request *r = kzalloc(sizeof *r);
		if (!r)
			continue; /* avis perdu, comme le Pi quand il n'y a plus de place */
		memcpy(r->msg, &op, 8);
		memcpy(r->msg + 8, &a, 8);
		memcpy(r->msg + 16, &b, 8);
		r->svc = i;
		r->state = R_QUEUED;
		r->num = next_num++;
		enqueue(r);
		sched_wake_all(&svcs[i]);
	}
	spin_unlock_irqrestore(&svc_lock, f);
}

void svc_low_memory(void)
{
	for (int i = 0; i < SVC_MAX; i++)
		if (svcs[i].pid && (svcs[i].flags & SVF_MEMORY))
			svc_signal_kernel(i, 1); /* SVC_SIG_LOWMEM */
}

void svc_notify_birth(int child, int parent)
{
	svc_notify(SVF_NAISSANCES, SVC_OP_NAISSANCE, child, parent);
}

void svc_notify_fin(int pid, int code)
{
	svc_notify(SVF_FINS, SVC_OP_FIN, pid, code);
}

void svc_proc_exit(struct process *p)
{
	uint64_t f = spin_lock_irqsave(&svc_lock);
	unmap_request(p, 0); /* ses vues : épingles rendues */
	/* ses requêtes à d'autres : abandonnées */
	for (struct request *r = reqs, *n; r; r = n) {
		n = r->next;
		if (r->caller && r->caller_pid == p->pid) {
			if (r->state == R_QUEUED) {
				unlink_req(r);
				kfree(r);
			} else {
				r->state = R_ABANDONED;
				r->caller = 0;
			}
		}
	}
	/* ses services : fermés, leurs requêtes échouent */
	for (int i = 0; i < SVC_MAX; i++) {
		if (svcs[i].pid != p->pid)
			continue;
		svcs[i].pid = 0;
		for (struct request *r = reqs, *n; r; r = n) {
			n = r->next;
			if (r->svc == i)
				finish(r, E_PIPE);
		}
	}
	spin_unlock_irqrestore(&svc_lock, f);
}
