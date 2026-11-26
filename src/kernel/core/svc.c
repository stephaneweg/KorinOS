/* Services nommés : un programme qui a les droits de /sys s'enregistre
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
	int fwd_pid;           /* le dernier service qui l'a transmise (SVC_FORWARD), 0 : venue directement */
	int64_t result;
	struct request *next;  /* toutes les requêtes */
	uint8_t msg[SVC_MSG];
};

#define MAPC_BASE 0x6E00000000ULL /* + n x 2 Mo dans le service */
#define MAPC_SIZE (1ULL << 20)
/* une vue de MAPC_SIZE octets qui ne commence pas au début d'une page en touche une de plus
 * (257) ; les vues sont espacées de 2 Mo. Avant : 256 places et 1 Mo d'écart, la 257e page
 * débordait sur la vue suivante (sa première traduction écrasée, ses épingles rendues deux
 * fois : « pmm_free : page déjà libre ») et sur le champ suivant du tableau */
#define MAPC_PAGES (MAPC_SIZE / PAGE_SIZE + 1)
#define MAPC_STRIDE (2 * MAPC_SIZE)
#define MAPC_MAX 16
#define MC_WRITE 1

struct mapc {
	int pid;            /* service qui a la vue, 0 : libre */
	uint64_t num;       /* requête */
	uint64_t base;      /* adresse rendue (début de page) */
	uint64_t pages;
	uint64_t phys[MAPC_PAGES];
};

static struct mapc mapcs[MAPC_MAX * 4];

struct service {
	char name[SVC_NAME];
	int pid;               /* 0 : libre */
	uint32_t flags;
	volatile uint64_t signals;
	int uid;               /* -1 : un nom du système ; sinon le compte qui l'a pris (E33, sans droits) */
};

/* E33 étape 2 (docs/etude/E33-CARTOGRAPHIE.md, note A) : un process d'une personne (uid > 0) sans droits prend un nom
 * à son compte : (nom, uid). SVC_OPEN cherche d'abord le nom du système, puis celui du compte de l'appelant (jamais
 * pour l'uid 0 ni un process avec droits) ; un compte ne voit pas les noms des autres. Un nom pris une fois par le
 * système depuis le démarrage (auth, keyvault, notify...) ne peut jamais être pris par un compte : une app ne se
 * fait pas passer pour authd, même s'il redémarre. */
#define SEEN_MAX 96
static char sys_seen[SEEN_MAX][SVC_NAME];
static int n_seen;

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

static int by_name_uid(const char *name, int uid)
{
	for (int i = 0; i < SVC_MAX; i++)
		if (svcs[i].pid && svcs[i].uid == uid && !strcmp(svcs[i].name, name))
			return i;
	return -1;
}

static int by_name(const char *name) /* (un nom du système) */
{
	return by_name_uid(name, -1);
}

static int seen(const char *name) /* (sous le verrou) */
{
	for (int i = 0; i < n_seen; i++)
		if (!strcmp(sys_seen[i], name))
			return 1;
	return 0;
}

/* un nom pris à son compte par un process sans droits -> canal, E_EXIST, E_PERM (un nom du système) */
static int64_t svc_register_account(const char *name, struct process *p)
{
	uint64_t f = spin_lock_irqsave(&svc_lock);
	int64_t r = E_NOMEM;
	if (seen(name) || by_name(name) >= 0)
		r = E_PERM;
	else if (by_name_uid(name, p->uid) >= 0)
		r = E_EXIST;
	else
		for (int i = 0; i < SVC_MAX; i++)
			if (!svcs[i].pid) {
				memcpy(svcs[i].name, name, SVC_NAME);
				svcs[i].pid = p->pid;
				svcs[i].flags = 0; /* (ni fins ni naissances des process : rien sur les autres) */
				svcs[i].signals = 0;
				svcs[i].uid = p->uid;
				r = i;
				break;
			}
	spin_unlock_irqrestore(&svc_lock, f);
	if (r >= 0)
		sched_wake_all(&open_key);
	return r;
}

int64_t sys_svc_register(uint64_t uname, uint64_t flags)
{
	struct process *p = me();
	char name[SVC_NAME];
	if (copy_string_from_user(name, uname, SVC_NAME) <= 0)
		return E_INVAL;
	if (!rights_used(p, RU_SVC_REGISTER, "SVC_REGISTER"))
		return p->uid > 0 ? svc_register_account(name, p) : E_PERM;
	return svc_register_name(name, (uint32_t)flags, p);
}

int svc_owner_has(struct process *p, uint32_t flag)
{
	uint64_t f = spin_lock_irqsave(&svc_lock);
	int r = 0;
	for (int i = 0; i < SVC_MAX && !r; i++)
		r = svcs[i].pid == p->pid && (svcs[i].flags & flag);
	spin_unlock_irqrestore(&svc_lock, f);
	return r;
}

int64_t svc_register_name(const char *name, uint32_t flags, struct process *p)
{
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
				svcs[i].uid = -1;
				r = i;
				break;
			}
	if (r >= 0 && !seen(name) && n_seen < SEEN_MAX)
		memcpy(sys_seen[n_seen++], name, SVC_NAME);
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
	struct process *p = me();
	int acct = p->uid > 0 && !p->rights ? p->uid : -1; /* (les noms de son compte, après ceux du système) */
	for (;;) {
		uint64_t f = spin_lock_irqsave(&svc_lock);
		int i = by_name(name);
		if (i < 0 && acct > 0)
			i = by_name_uid(name, acct);
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

/* op d'un avis du noyau (SVC_OP_* et WS_OP_EXIT, 0xFFF0 à 0xFFFF) : jamais d'un process. Lue en
 * 64 bits non signés : tout ce qui est au-dessus est refusé aussi, donc un service qui ne lit que
 * les 32 ou 16 bits bas ne voit pas d'avis forgé (aucun client n'envoie d'op >= 0xFF00) */
static int op_reserved(const uint8_t *msg)
{
	uint64_t op;
	memcpy(&op, msg, 8);
	return op >= 0xFFF0;
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
	if (op_reserved(r->msg)) { /* les avis du noyau (fin, naissance, exec, setuid…) ne se forgent pas */
		kfree(r);
		return E_INVAL;
	}
	struct thread *t = sched_current();
	r->caller = t;
	r->caller_pid = t->proc->pid;
	r->fwd_pid = 0;
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

/* une vue retirée : traductions effacées, TLB de tous les cœurs vidés, PUIS les épingles
 * rendues. Avant, les pages étaient rendues tout de suite et les TLB vidés plus tard (après
 * svc_lock) : un autre thread du service, sur un autre cœur, pouvait encore écrire par
 * l'ancienne traduction dans une page déjà rendue, reprise entre-temps par le tas du noyau
 * (en-tête d'un gros bloc abîmé : « pmm_free : page déjà libre » au démarrage sous 8 cœurs).
 * L'invalidation sous svc_lock est permise : un cœur qui attend un verrou y répond
 * (arch_cpu_relax) */
static void unmap_one(struct mapc *m, struct process *svc)
{
	for (uint64_t i = 0; i < m->pages; i++)
		vm_unmap_page(svc->vm, m->base + i * PAGE_SIZE, 0);
	if (m->pages && svc->vm)
		vm_tlb_shootdown(svc->vm);
	for (uint64_t i = 0; i < m->pages; i++)
		pmm_free(m->phys[i]); /* l'épingle */
	m->pid = 0;
}

/* les vues d'une requête (ou toutes celles du process si num = 0), sous le verrou ; les TLB sont
 * vidés par unmap_one -> 0 (plus rien à faire après svc_lock) */
static int unmap_request(struct process *svc, uint64_t num)
{
	for (int i = 0; i < MAPC_MAX * 4; i++)
		if (mapcs[i].pid == svc->pid && (!num || mapcs[i].num == num))
			unmap_one(&mapcs[i], svc);
	return 0;
}

int64_t sys_svc_map_caller(uint64_t num, uint64_t addr, uint64_t size, uint64_t flags)
{
	struct process *svc = me();
	if (!rights_used(svc, RU_SVC_MAP_CALLER, "SVC_MAP_CALLER"))
		return E_PERM;
	if (size == 0 || size > MAPC_SIZE || addr + size < addr)
		return E_INVAL;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	struct request *r = taken(svc, num);
	int caller_pid = r && r->state == R_TAKEN ? r->caller_pid : 0;
	int slot = -1, mine = 0;
	for (int i = 0; i < MAPC_MAX * 4; i++) {
		if (mapcs[i].pid == svc->pid)
			mine |= 1 << ((mapcs[i].base - MAPC_BASE) / MAPC_STRIDE);
		else if (!mapcs[i].pid && slot < 0)
			slot = i;
	}
	int n = -1;
	for (int k = 0; k < MAPC_MAX && n < 0; k++)
		if (!(mine & (1 << k)))
			n = k;
	/* l'appelant tenu (pins) sous svc_lock, avec la requête encore prise : ses pages sont épinglées hors
	 * des verrous, et avant, rien ne l'empêchait de mourir pendant ce temps (SIGKILL d'un rustc que pip
	 * lance pour son en-tête, pendant qu'ext4fs lui sert une lecture) : sched_reap rendait son mm, son
	 * espace et le process lui-même, et mm_pin_page les lisait encore (le service tombait, et son volume
	 * avec). Le faucheur et exec attendent maintenant que pins revienne à 0 (comme mc_quiesce du Pi) */
	struct process *caller = 0;
	if (caller_pid && slot >= 0 && n >= 0) {
		uint64_t g = sched_lock_take();
		caller = proc_get(caller_pid);
		if (caller && (caller->reaping || !caller->vm || !caller->mm))
			caller = 0;
		if (caller)
			__atomic_add_fetch(&caller->pins, 1, __ATOMIC_ACQ_REL);
		sched_lock_give(g);
	}
	if (caller) {
		mapcs[slot].pid = svc->pid; /* réservée */
		mapcs[slot].num = num;
		mapcs[slot].base = MAPC_BASE + (uint64_t)n * MAPC_STRIDE;
		mapcs[slot].pages = 0;
	}
	spin_unlock_irqrestore(&svc_lock, f);
	if (!caller_pid)
		return E_PIPE;
	if (slot < 0 || n < 0)
		return E_BUSY;
	if (!caller)
		return E_PIPE;
	struct mapc *m = &mapcs[slot];
	int write = (flags & MC_WRITE) != 0;
	uint64_t first = ALIGN_DOWN(addr, PAGE_SIZE), end = ALIGN_UP(addr + size, PAGE_SIZE);
	int64_t ret = 0;
	for (uint64_t va = first; ret == 0 && va < end; va += PAGE_SIZE) {
		/* épinglée : elle reste tant que la vue existe (l'appelant dort dans SVC_CALL) */
		uint64_t phys = mm_pin_page(caller, va, write);
		if (!phys) {
			ret = E_FAULT;
			break;
		}
		m->phys[m->pages] = phys;
		if (vm_map_page(svc->vm, m->base + m->pages * PAGE_SIZE, phys, write ? VM_W : 0)) {
			pmm_free(phys);
			ret = E_NOMEM;
			break;
		}
		m->pages++;
	}
	__atomic_sub_fetch(&caller->pins, 1, __ATOMIC_ACQ_REL); /* (ses pages ont leurs épingles) */
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
		if (mapcs[i].pid == svc->pid && addr >= mapcs[i].base && addr < mapcs[i].base + MAPC_PAGES * PAGE_SIZE) {
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
	uint64_t ph[MAPC_PAGES];
	uint64_t n = 0;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	for (int i = 0; i < MAPC_MAX * 4; i++)
		if (mapcs[i].pid == svc->pid && addr >= mapcs[i].base && addr < mapcs[i].base + MAPC_PAGES * PAGE_SIZE) {
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
	int shoot = unmap_request(me(), num);
	if (!r)
		ret = E_INVAL;
	else {
		memcpy(r->msg, in, SVC_MSG);
		int64_t res;
		memcpy(&res, in, 8);
		finish(r, res);
	}
	spin_unlock_irqrestore(&svc_lock, f);
	if (shoot)
		vm_tlb_shootdown(me()->vm); /* (hors du verrou) */
	kfree(in);
	return ret;
}

int64_t sys_svc_signal(uint64_t chan, uint64_t bits)
{
	if (chan >= SVC_MAX)
		return E_INVAL;
	if (!rights_used(me(), RU_SVC_SIGNAL, "SVC_SIGNAL")) /* interruptions des pilotes, SVC_SIG_LOWMEM : pas pour une app */
		return E_PERM;
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
	if (op_reserved(in)) {
		kfree(in);
		return E_INVAL;
	}
	int64_t ret = 0;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	struct request *r = taken(me(), num);
	int shoot = unmap_request(me(), num);
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
		r->fwd_pid = me()->pid;
		r->svc = (int)chan;
		r->state = R_QUEUED;
		sched_wake_all(&svcs[chan]);
	}
	spin_unlock_irqrestore(&svc_lock, f);
	if (shoot)
		vm_tlb_shootdown(me()->vm); /* (hors du verrou) */
	kfree(in);
	return ret;
}

/* SVC_REQ_INFO (num, sortie 32 o) : pour une requête prise par le service qui appelle, {pid de
 * l'appelant, pid du service qui l'a transmise (0 : venue directement), uid de l'appelant, pid de son
 * père} ; lu sous svc_lock pendant que l'appelant attend sa réponse : pas de course sur la
 * réutilisation des pids (PROC_UID(pid) après coup pouvait viser un autre process). E_INVAL : pas
 * une requête prise par lui ; E_SRCH : l'appelant a abandonné (tué, délai) */
int64_t sys_svc_req_info(uint64_t num, uint64_t out)
{
	int64_t o[4] = {0};
	uint64_t f = spin_lock_irqsave(&svc_lock);
	struct request *r = taken(me(), num);
	int64_t ret = 0;
	if (!r)
		ret = E_INVAL;
	else if (r->state == R_ABANDONED || !r->caller)
		ret = E_SRCH;
	else {
		struct process *c = r->caller->proc; /* (l'appelant dort dans SVC_CALL : il est là) */
		o[0] = r->caller_pid;
		o[1] = r->fwd_pid;
		o[2] = c->uid;
		o[3] = c->parent;
	}
	spin_unlock_irqrestore(&svc_lock, f);
	if (ret)
		return ret;
	return copy_to_user(out, o, sizeof o) ? E_FAULT : 0;
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

/* comme svc_naissance du Pi (src/kernel/svc.asm) : les entrées / sorties données au
 * lancement vont à vfs et à vterm, qui en font celles du fils (V_STDIO) */
void svc_notify_birth(int child, int parent, const int64_t *stdio)
{
	int64_t io[3] = {0, 0, 0};
	if (stdio)
		memcpy(io, stdio, sizeof io);
	uint64_t op = SVC_OP_BIRTH;
	int64_t a = child, b = parent;
	uint32_t len = 8;
	uint64_t f = spin_lock_irqsave(&svc_lock);
	for (int i = 0; i < SVC_MAX; i++) {
		if (!svcs[i].pid || !(svcs[i].flags & SVF_BIRTHS))
			continue;
		struct request *r = kzalloc(sizeof *r);
		if (!r)
			continue; /* avis perdu, comme le Pi quand il n'y a plus de place */
		memcpy(r->msg, &op, 8);
		memcpy(r->msg + 8, &a, 8);
		memcpy(r->msg + 16, &b, 8);
		memcpy(r->msg + 24, &io[0], 8); /* c : entrée */
		memcpy(r->msg + 32, &io[1], 8); /* d : sortie */
		memcpy(r->msg + 40, &len, 4);
		memcpy(r->msg + 48, &io[2], 8); /* données : erreur */
		r->svc = i;
		r->state = R_QUEUED;
		r->num = next_num++;
		enqueue(r);
		sched_wake_all(&svcs[i]);
	}
	spin_unlock_irqrestore(&svc_lock, f);
}

/* SYS_SETUID : vfs garde l'uid de chaque pid (avis sans réponse, comme SVC_OP_EXEC) */
void svc_notify_setuid(int pid, int uid)
{
	uint64_t op = SVC_OP_SETUID;
	int64_t a = pid, b = uid;
	struct request *r = kzalloc(sizeof *r);
	if (!r)
		return;
	memcpy(r->msg, &op, 8);
	memcpy(r->msg + 8, &a, 8);
	memcpy(r->msg + 16, &b, 8);
	uint64_t f = spin_lock_irqsave(&svc_lock);
	int i = by_name("vfs");
	if (i >= 0) {
		r->svc = i;
		r->state = R_QUEUED;
		r->num = next_num++;
		enqueue(r);
		sched_wake_all(&svcs[i]);
	} else
		kfree(r);
	spin_unlock_irqrestore(&svc_lock, f);
}

void svc_notify_exec(int pid, const int64_t *stdio)
{
	uint64_t op = SVC_OP_EXEC;
	int64_t a = pid;
	uint32_t len = 8;
	struct request *r = kzalloc(sizeof *r);
	if (!r)
		return; /* avis perdu, comme le Pi quand il n'y a plus de place */
	memcpy(r->msg, &op, 8);
	memcpy(r->msg + 8, &a, 8);
	memcpy(r->msg + 16, &a, 8); /* b = pid */
	memcpy(r->msg + 24, &stdio[0], 8); /* c : entrée */
	memcpy(r->msg + 32, &stdio[1], 8); /* d : sortie */
	memcpy(r->msg + 40, &len, 4);
	memcpy(r->msg + 48, &stdio[2], 8); /* données : erreur */
	uint64_t f = spin_lock_irqsave(&svc_lock);
	int i = by_name("vfs");
	if (i >= 0) {
		r->svc = i;
		r->state = R_QUEUED;
		r->num = next_num++;
		enqueue(r);
		sched_wake_all(&svcs[i]);
	}
	spin_unlock_irqrestore(&svc_lock, f);
	if (i < 0)
		kfree(r);
}

void svc_notify_fin(int pid, int code)
{
	svc_notify(SVF_ENDS, SVC_OP_END, pid, code);
}

void svc_proc_exit(struct process *p)
{
	uint64_t f = spin_lock_irqsave(&svc_lock);
	int shoot = unmap_request(p, 0); /* ses vues : épingles rendues */
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
	if (shoot)
		vm_tlb_shootdown(p->vm); /* (hors du verrou) */
}
