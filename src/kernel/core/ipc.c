/* IPC entre process, comme le Pi (docs/ABI.md 0x70-0x7D, src/kernel/ipc.asm) :
 *   poignées 1..1023 par process, chacune tient une référence sur un objet :
 *   un bout de canal (STREAM, DGRAM, SEQPACKET) ou un objet de mémoire partagée ;
 *   anneau de réception de 64 Ko par bout ; poignées portées par les messages
 *   (SCM_RIGHTS) ; mémoire partagée projetée par SHM_MAP (pages communes).
 * Un seul verrou pour tout l'IPC (le Pi en a un par canal) ; tout changement
 * d'un canal réveille ceux qui attendent (ils regardent de nouveau). Portable. */
#include <korin/arch.h>
#include <korin/errno.h>
#include <korin/ipc.h>
#include <korin/kheap.h>
#include <korin/lib.h>
#include <korin/mm.h>
#include <korin/pmm.h>
#include <korin/sched.h>
#include <korin/spinlock.h>

#define MAX_HANDLES 1024
#define RING 65536
#define MAX_IOV 64
#define MAX_MSG_HANDLES 256
#define WAIT_MAX 64
#define INHERIT_MAX 255
#define SHM_NAMES 256
#define SHM_NAME 56
#define SHM_MAX (1008ULL << 20)

#define E_MSGSIZE -90
#define E_PROTONOSUPPORT -93
#define E_NOBUFS -105
#define E_MFILE -24
#define E_NFILE -23
#define E_NAMETOOLONG -36

enum { K_CHAN = 1, K_SHM = 2 };
enum { ST_STREAM = 1, ST_DGRAM = 2, ST_SEQPACKET = 5 };
#define S_CLOSED 1
#define S_SHUT_RD 2
#define S_SHUT_WR 4

struct ipc_obj {
	int kind;
	int refs;
};

struct msg {
	struct msg *next;
	uint32_t len, off, nh;
	struct ipc_obj **objs;
	uint8_t data[];
};

struct chan;
struct end {
	struct ipc_obj h; /* kind = K_CHAN */
	struct chan *c;
	int side;
	int state;
	struct msg *head, *tail;
	uint32_t bytes; /* non lus */
};

struct chan {
	int type;
	int creator;
	struct end e[2];
};

struct shm {
	struct ipc_obj h; /* kind = K_SHM */
	uint64_t size;
	uint64_t *pages; /* adresse physique de chaque page, 0 : pas encore créée */
	uint64_t npages;
	int seals, sealable;
	int mapped;
	int weak, pinned;
	int id;
	char name[SHM_NAME]; /* vide : sans nom */
};

struct handles {
	struct ipc_obj *obj[MAX_HANDLES];
	uint32_t mark[MAX_HANDLES];
};

struct inherit {
	int n;
	struct ipc_obj *obj[INHERIT_MAX];
	uint32_t mark[INHERIT_MAX];
};

static spinlock_t ipc_lock = SPINLOCK_INIT;
static int wait_key;
static struct shm *names[SHM_NAMES];
static int next_shm_id = 1;

static struct process *me(void)
{
	return sched_current()->proc;
}

static void changed(void)
{
	sched_wake_all(&wait_key);
}

/* ---- références (sous le verrou) */

static void put_locked(struct ipc_obj *o);

static void free_msg(struct msg *m)
{
	for (uint32_t i = 0; i < m->nh; i++)
		if (m->objs[i])
			put_locked(m->objs[i]);
	kfree(m->objs);
	kfree(m);
}

static void put_locked(struct ipc_obj *o)
{
	if (--o->refs > 0)
		return;
	if (o->kind == K_CHAN) {
		struct end *e = (struct end *)o;
		e->state |= S_CLOSED;
		while (e->head) { /* messages jamais lus : rendus avec ce qu'ils portaient */
			struct msg *m = e->head;
			e->head = m->next;
			free_msg(m);
		}
		e->tail = 0;
		e->bytes = 0;
		struct chan *c = e->c;
		if ((c->e[0].state & S_CLOSED) && (c->e[1].state & S_CLOSED))
			kfree(c);
		return;
	}
	struct shm *s = (struct shm *)o;
	if (s->name[0]) /* nom faible : il part avec l'objet */
		for (int i = 0; i < SHM_NAMES; i++)
			if (names[i] == s)
				names[i] = 0;
	for (uint64_t i = 0; i < s->npages; i++)
		if (s->pages[i])
			pmm_free(s->pages[i]);
	kfree(s->pages);
	kfree(s);
}

void ipc_obj_get(struct ipc_obj *o)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	o->refs++;
	spin_unlock_irqrestore(&ipc_lock, f);
}

void ipc_obj_put(struct ipc_obj *o)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	put_locked(o);
	spin_unlock_irqrestore(&ipc_lock, f);
	changed();
}

/* ---- table des poignées (sous le verrou) */

static struct handles *table(struct process *p, int create)
{
	if (!p->ipc_handles && create)
		p->ipc_handles = kzalloc(sizeof(struct handles));
	return p->ipc_handles;
}

static struct ipc_obj *get_handle(struct process *p, uint64_t h)
{
	struct handles *t = p->ipc_handles;
	if (!t || h == 0 || h >= MAX_HANDLES)
		return 0;
	return t->obj[h];
}

/* installe obj (sa référence passe à la table) ; poignée ou E_MFILE */
static int64_t add_handle(struct process *p, struct ipc_obj *o, uint32_t mark)
{
	struct handles *t = table(p, 1);
	if (!t)
		return E_NOMEM;
	for (int h = 1; h < MAX_HANDLES; h++)
		if (!t->obj[h]) {
			t->obj[h] = o;
			t->mark[h] = mark;
			return h;
		}
	return E_MFILE;
}

int64_t sys_handle_close(uint64_t h)
{
	struct process *p = me();
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct ipc_obj *o = get_handle(p, h);
	if (o) {
		((struct handles *)p->ipc_handles)->obj[h] = 0;
		put_locked(o);
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	if (!o)
		return E_BADF;
	changed();
	return 0;
}

static uint32_t peer_state(struct end *e)
{
	return e->c->e[!e->side].state;
}

int64_t sys_handle_info(uint64_t h, uint64_t out)
{
	uint64_t o8[8] = {0};
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct ipc_obj *o = get_handle(me(), h);
	if (o && o->kind == K_CHAN) {
		struct end *e = (struct end *)o;
		uint32_t st = 0, ps = peer_state(e);
		if (ps & S_CLOSED)
			st |= 1;
		if (e->state & S_SHUT_RD)
			st |= 2;
		if (e->state & S_SHUT_WR)
			st |= 4;
		if (ps & S_SHUT_WR)
			st |= 8;
		o8[0] = K_CHAN | ((uint64_t)e->c->type << 32);
		o8[1] = e->bytes;
		o8[2] = st;
		o8[3] = (uint64_t)e->c->creator;
		o8[4] = RING;
	} else if (o) {
		struct shm *s = (struct shm *)o;
		o8[0] = K_SHM;
		o8[1] = s->size;
		o8[2] = (uint64_t)s->seals | ((uint64_t)(s->sealable | (s->pinned ? 2 : 0)) << 32);
		o8[3] = (uint64_t)s->id;
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	if (!o)
		return E_BADF;
	return copy_to_user(out, o8, 64) ? E_FAULT : 0;
}

/* ---- attente */

#define POLLIN 1
#define POLLOUT 4
#define POLLERR 8
#define POLLHUP 16
#define POLLNVAL 32

static int ready(struct ipc_obj *o)
{
	if (!o)
		return POLLNVAL;
	if (o->kind == K_SHM)
		return POLLIN | POLLOUT;
	struct end *e = (struct end *)o;
	struct end *peer = &e->c->e[!e->side];
	int r = 0;
	if (e->bytes || e->head || (peer->state & (S_CLOSED | S_SHUT_WR)) || (e->state & S_SHUT_RD))
		r |= POLLIN;
	if (peer->state & S_CLOSED)
		r |= POLLHUP;
	else if (!(peer->state & S_SHUT_RD) && !(e->state & S_SHUT_WR) && peer->bytes < RING)
		r |= POLLOUT;
	return r;
}

/* sous le verrou : dort jusqu'au prochain changement (verrou rendu et repris) */
static int sleep_locked(uint64_t *f, uint64_t deadline)
{
	uint64_t now = arch_ticks_ms();
	if (deadline && now >= deadline)
		return WAKE_TIMEOUT;
	sched_prepare_block(deadline ? deadline - now : 0, &wait_key);
	spin_unlock_irqrestore(&ipc_lock, *f);
	int why = sched_block();
	*f = spin_lock_irqsave(&ipc_lock);
	return why;
}

int64_t sys_handle_wait(uint64_t entries, uint64_t n, int64_t ms)
{
	if (n > WAIT_MAX)
		return E_INVAL;
	uint32_t e[WAIT_MAX * 2];
	if (copy_from_user(e, entries, n * 8))
		return E_FAULT;
	uint64_t deadline = ms > 0 ? arch_ticks_ms() + (uint64_t)ms : 0;
	struct process *p = me();
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	int64_t count;
	for (;;) {
		count = 0;
		for (uint64_t i = 0; i < n; i++) {
			uint16_t want = (uint16_t)e[i * 2 + 1];
			int r = ready(get_handle(p, e[i * 2])) & (want | POLLHUP | POLLERR | POLLNVAL);
			e[i * 2 + 1] = want | ((uint32_t)r << 16);
			count += r != 0;
		}
		if (count || ms == 0)
			break;
		int why = sleep_locked(&f, deadline);
		if (why == WAKE_TIMEOUT)
			break;
		if (why == WAKE_KILLED) {
			count = E_INTR;
			break;
		}
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	if (count >= 0 && copy_to_user(entries, e, n * 8))
		return E_FAULT;
	return count;
}

int64_t sys_handle_list(uint64_t out, uint64_t max, uint64_t clear)
{
	if (max > MAX_HANDLES)
		max = MAX_HANDLES;
	uint32_t *buf = kmalloc(max * 16 + 16);
	if (!buf)
		return E_NOMEM;
	uint64_t n = 0;
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct handles *t = me()->ipc_handles;
	for (int h = 1; t && h < MAX_HANDLES && n < max; h++) {
		struct ipc_obj *o = t->obj[h];
		if (!o)
			continue;
		uint32_t type = o->kind == K_CHAN ? (uint32_t)((struct end *)o)->c->type : 0;
		buf[n * 4] = (uint32_t)h;
		buf[n * 4 + 1] = t->mark[h];
		buf[n * 4 + 2] = (uint32_t)o->kind | (type << 8);
		buf[n * 4 + 3] = 0;
		n++;
		if (clear)
			t->mark[h] = 0;
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	int64_t r = copy_to_user(out, buf, n * 16) ? E_FAULT : (int64_t)n;
	kfree(buf);
	return r;
}

static void drop_inherit_locked(struct process *p)
{
	struct inherit *in = p->ipc_inherit;
	if (!in)
		return;
	for (int i = 0; i < in->n; i++)
		put_locked(in->obj[i]);
	kfree(in);
	p->ipc_inherit = 0;
}

int64_t sys_handle_inherit(uint64_t entries, uint64_t n)
{
	if (n > INHERIT_MAX)
		return E_INVAL;
	uint32_t e[INHERIT_MAX * 2];
	if (n && copy_from_user(e, entries, n * 8))
		return E_FAULT;
	struct inherit *in = n ? kzalloc(sizeof *in) : 0;
	if (n && !in)
		return E_NOMEM;
	struct process *p = me();
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	for (uint64_t i = 0; i < n; i++)
		if (!get_handle(p, e[i * 2])) {
			spin_unlock_irqrestore(&ipc_lock, f);
			kfree(in);
			return E_BADF;
		}
	drop_inherit_locked(p);
	for (uint64_t i = 0; i < n; i++) {
		in->obj[i] = get_handle(p, e[i * 2]);
		in->obj[i]->refs++;
		in->mark[i] = e[i * 2 + 1];
	}
	if (in)
		in->n = (int)n;
	p->ipc_inherit = in;
	spin_unlock_irqrestore(&ipc_lock, f);
	return 0;
}

void ipc_inherit(struct process *parent, struct process *child)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct inherit *in = parent->ipc_inherit;
	parent->ipc_inherit = 0;
	for (int i = 0; in && i < in->n; i++)
		if (add_handle(child, in->obj[i], in->mark[i]) < 0)
			put_locked(in->obj[i]);
	spin_unlock_irqrestore(&ipc_lock, f);
	kfree(in);
}

int ipc_fork(struct process *parent, struct process *child)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct handles *pt = parent->ipc_handles;
	int r = 0;
	if (pt) {
		struct handles *ct = table(child, 1);
		if (!ct)
			r = E_NOMEM;
		else
			for (int h = 1; h < MAX_HANDLES; h++)
				if (pt->obj[h]) {
					ct->obj[h] = pt->obj[h];
					ct->mark[h] = pt->mark[h];
					pt->obj[h]->refs++;
				}
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	return r;
}

void ipc_proc_exit(struct process *p)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct handles *t = p->ipc_handles;
	for (int h = 1; t && h < MAX_HANDLES; h++)
		if (t->obj[h])
			put_locked(t->obj[h]);
	drop_inherit_locked(p);
	p->ipc_handles = 0;
	spin_unlock_irqrestore(&ipc_lock, f);
	kfree(t);
	changed();
}

/* ---- canaux */

int64_t sys_chan_create(uint64_t type, uint64_t out)
{
	if (type != ST_STREAM && type != ST_DGRAM && type != ST_SEQPACKET)
		return E_PROTONOSUPPORT;
	if (user_check(out, 8, 1))
		return E_FAULT;
	struct chan *c = kzalloc(sizeof *c);
	if (!c)
		return E_NOMEM;
	c->type = (int)type;
	c->creator = me()->pid;
	for (int i = 0; i < 2; i++) {
		c->e[i].h.kind = K_CHAN;
		c->e[i].h.refs = 1;
		c->e[i].c = c;
		c->e[i].side = i;
	}
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	int64_t a = add_handle(me(), &c->e[0].h, 0);
	int64_t b = a > 0 ? add_handle(me(), &c->e[1].h, 0) : a;
	if (b < 0) {
		if (a > 0)
			((struct handles *)me()->ipc_handles)->obj[a] = 0;
		spin_unlock_irqrestore(&ipc_lock, f);
		kfree(c);
		return b;
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	uint32_t o[2] = {(uint32_t)a, (uint32_t)b};
	return copy_to_user(out, o, 8) ? E_FAULT : 0;
}

/* message (32 o) : iovecs, leur nombre, poignées (place / nombre), poignées, drapeaux */
struct umsg {
	uint64_t iov;
	uint32_t iovcnt, nh;
	uint64_t handles;
	uint32_t flags, pad;
};

#define IPC_NOWAIT 1
#define IPC_PEEK 2
#define IPC_TRUNC 1
#define IPC_CTRUNC 2

int64_t sys_chan_send(uint64_t h, uint64_t umsg, uint64_t flags, uint64_t ms)
{
	struct umsg um;
	if (copy_from_user(&um, umsg, 32))
		return E_FAULT;
	if (um.iovcnt > MAX_IOV || um.nh > MAX_MSG_HANDLES)
		return E_INVAL;
	uint64_t iov[MAX_IOV * 2];
	if (copy_from_user(iov, um.iov, um.iovcnt * 16ULL))
		return E_FAULT;
	uint64_t total = 0;
	for (uint32_t i = 0; i < um.iovcnt; i++)
		total += iov[i * 2 + 1];
	uint32_t hs[MAX_MSG_HANDLES * 2];
	if (um.nh && copy_from_user(hs, um.handles, um.nh * 8ULL))
		return E_FAULT;
	struct process *p = me();
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct ipc_obj *o = get_handle(p, h);
	int type = o && o->kind == K_CHAN ? ((struct end *)o)->c->type : 0;
	spin_unlock_irqrestore(&ipc_lock, f);
	if (!type)
		return E_BADF;
	if (type != ST_STREAM && total > RING)
		return E_MSGSIZE;
	uint64_t deadline = ms ? arch_ticks_ms() + ms : 0;
	uint64_t sent = 0;
	int handles_sent = 0;
	for (;;) {
		uint64_t want = total - sent;
		/* données rassemblées hors du verrou (fautes de page possibles) */
		uint64_t chunk = want > RING ? RING : want;
		struct msg *m = kmalloc(sizeof *m + chunk);
		if (!m)
			return sent ? (int64_t)sent : E_NOMEM;
		uint64_t pos = 0, skip = sent;
		for (uint32_t i = 0; i < um.iovcnt && pos < chunk; i++) {
			uint64_t len = iov[i * 2 + 1];
			if (skip >= len) {
				skip -= len;
				continue;
			}
			uint64_t n = len - skip < chunk - pos ? len - skip : chunk - pos;
			if (copy_from_user(m->data + pos, iov[i * 2] + skip, n)) {
				kfree(m);
				return E_FAULT;
			}
			pos += n;
			skip = 0;
		}
		f = spin_lock_irqsave(&ipc_lock);
		struct end *e = (struct end *)get_handle(p, h);
		if (!e || e->h.kind != K_CHAN) {
			spin_unlock_irqrestore(&ipc_lock, f);
			kfree(m);
			return E_BADF;
		}
		struct end *peer = &e->c->e[!e->side];
		if ((peer->state & (S_CLOSED | S_SHUT_RD)) || (e->state & S_SHUT_WR)) {
			spin_unlock_irqrestore(&ipc_lock, f);
			kfree(m);
			return sent ? (int64_t)sent : E_PIPE;
		}
		uint64_t room = RING - peer->bytes;
		uint64_t take = type == ST_STREAM ? (chunk < room ? chunk : room) : (chunk <= room ? chunk : 0);
		if (take == 0 && !(chunk == 0 && type != ST_STREAM)) {
			kfree(m);
			if ((flags & IPC_NOWAIT) || sent) {
				spin_unlock_irqrestore(&ipc_lock, f);
				return sent ? (int64_t)sent : E_AGAIN;
			}
			int why = sleep_locked(&f, deadline);
			spin_unlock_irqrestore(&ipc_lock, f);
			if (why == WAKE_TIMEOUT)
				return E_AGAIN;
			if (why == WAKE_KILLED)
				return E_INTR;
			continue;
		}
		/* poignées portées, avec le premier morceau */
		m->nh = 0;
		m->objs = 0;
		if (um.nh && !handles_sent) {
			m->objs = kmalloc(um.nh * sizeof(struct ipc_obj *));
			for (uint32_t i = 0; i < um.nh; i++) {
				struct ipc_obj *x = m->objs ? get_handle(p, hs[i * 2]) : 0;
				if (!x) {
					int64_t err = m->objs ? E_BADF : E_NOMEM;
					spin_unlock_irqrestore(&ipc_lock, f);
					kfree(m->objs);
					kfree(m);
					return err;
				}
				if (x == &peer->h) { /* le bout receveur par son propre canal */
					spin_unlock_irqrestore(&ipc_lock, f);
					kfree(m->objs);
					kfree(m);
					return E_INVAL;
				}
				m->objs[i] = x;
			}
			for (uint32_t i = 0; i < um.nh; i++)
				m->objs[i]->refs++;
			m->nh = um.nh;
			handles_sent = 1;
		}
		m->len = (uint32_t)take;
		m->off = 0;
		m->next = 0;
		if (peer->tail)
			peer->tail->next = m;
		else
			peer->head = m;
		peer->tail = m;
		peer->bytes += (uint32_t)take;
		sent += take;
		spin_unlock_irqrestore(&ipc_lock, f);
		changed();
		if (sent >= total || type != ST_STREAM || (flags & IPC_NOWAIT))
			return (int64_t)sent;
	}
}

int64_t sys_chan_recv(uint64_t h, uint64_t umsg, uint64_t flags, uint64_t ms)
{
	struct umsg um;
	if (copy_from_user(&um, umsg, 32))
		return E_FAULT;
	if (um.iovcnt > MAX_IOV || um.nh > MAX_MSG_HANDLES)
		return E_INVAL;
	uint64_t iov[MAX_IOV * 2];
	if (copy_from_user(iov, um.iov, um.iovcnt * 16ULL))
		return E_FAULT;
	uint64_t space = 0;
	for (uint32_t i = 0; i < um.iovcnt; i++)
		space += iov[i * 2 + 1];
	uint8_t *buf = kmalloc(space < RING ? space + 1 : RING + 1);
	if (!buf)
		return E_NOMEM;
	struct process *p = me();
	uint64_t deadline = ms ? arch_ticks_ms() + ms : 0;
	uint32_t hout[MAX_MSG_HANDLES * 2];
	uint32_t nh_out = 0, oflags = 0;
	int64_t got = 0;
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	for (;;) {
		struct end *e = (struct end *)get_handle(p, h);
		if (!e || e->h.kind != K_CHAN) {
			got = E_BADF;
			break;
		}
		struct end *peer = &e->c->e[!e->side];
		if (!e->head) {
			if ((peer->state & (S_CLOSED | S_SHUT_WR)) || (e->state & S_SHUT_RD)) {
				got = 0; /* la fin */
				break;
			}
			if (flags & IPC_NOWAIT) {
				got = E_AGAIN;
				break;
			}
			int why = sleep_locked(&f, deadline);
			if (why == WAKE_TIMEOUT) {
				got = E_AGAIN;
				break;
			}
			if (why == WAKE_KILLED || why == WAKE_SIGNAL) {
				got = E_INTR;
				break;
			}
			continue;
		}
		uint64_t cap = space < RING ? space : RING;
		struct msg *m = e->head;
		int stream = e->c->type == ST_STREAM;
		uint64_t pos = 0;
		while (m) {
			if (stream && pos && m->nh)
				break; /* ses poignées viennent avec son premier octet */
			uint32_t avail = m->len - m->off;
			uint64_t n = avail < cap - pos ? avail : cap - pos;
			memcpy(buf + pos, m->data + m->off, n);
			pos += n;
			if (!(flags & IPC_PEEK) && m->nh && m->objs) { /* poignées : à la table */
				for (uint32_t i = 0; i < m->nh; i++) {
					struct ipc_obj *x = m->objs[i];
					int64_t nhh = nh_out < um.nh ? add_handle(p, x, 0) : E_MFILE;
					if (nhh > 0) {
						uint32_t type = x->kind == K_CHAN ? (uint32_t)((struct end *)x)->c->type : 0;
						hout[nh_out * 2] = (uint32_t)nhh;
						hout[nh_out * 2 + 1] = (uint32_t)x->kind | (type << 8);
						nh_out++;
					} else {
						put_locked(x);
						oflags |= IPC_CTRUNC;
					}
				}
				kfree(m->objs);
				m->objs = 0;
				m->nh = 0;
			}
			if (!stream) {
				if (n < avail)
					oflags |= IPC_TRUNC;
				if (!(flags & IPC_PEEK)) {
					e->bytes -= avail;
					e->head = m->next;
					if (!e->head)
						e->tail = 0;
					kfree(m);
				}
				break;
			}
			if (flags & IPC_PEEK) {
				m = m->next;
				if (pos >= cap)
					break;
				continue;
			}
			m->off += (uint32_t)n;
			e->bytes -= (uint32_t)n;
			if (m->off < m->len)
				break;
			e->head = m->next;
			if (!e->head)
				e->tail = 0;
			kfree(m);
			m = e->head;
			if (pos >= cap)
				break;
		}
		got = (int64_t)pos;
		break;
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	if (got > 0 || (got == 0 && nh_out)) {
		changed();
		uint64_t pos = 0;
		for (uint32_t i = 0; i < um.iovcnt && pos < (uint64_t)got; i++) {
			uint64_t n = iov[i * 2 + 1] < (uint64_t)got - pos ? iov[i * 2 + 1] : (uint64_t)got - pos;
			if (copy_to_user(iov[i * 2], buf + pos, n)) {
				got = E_FAULT;
				break;
			}
			pos += n;
		}
	}
	kfree(buf);
	if (got >= 0) {
		if (nh_out && copy_to_user(um.handles, hout, nh_out * 8ULL))
			return E_FAULT;
		uint32_t tail[2] = {um.iovcnt, nh_out};
		if (copy_to_user(umsg + 8, tail, 8) || copy_to_user(umsg + 24, &oflags, 4))
			return E_FAULT;
	}
	return got;
}

int64_t sys_chan_shutdown(uint64_t h, uint64_t how)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct end *e = (struct end *)get_handle(me(), h);
	int ok = e && e->h.kind == K_CHAN;
	if (ok) {
		if (how & 1)
			e->state |= S_SHUT_RD;
		if (how & 2)
			e->state |= S_SHUT_WR;
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	if (!ok)
		return E_BADF;
	changed();
	return 0;
}

/* ---- mémoire partagée */

static struct shm *new_shm(void)
{
	struct shm *s = kzalloc(sizeof *s);
	if (!s)
		return 0;
	s->h.kind = K_SHM;
	s->h.refs = 1;
	s->id = next_shm_id++;
	return s;
}

/* taille (sous le verrou) : rétrécir rend les pages au-delà, met à zéro la fin */
static int resize(struct shm *s, uint64_t size)
{
	if (size > SHM_MAX)
		return E_INVAL;
	uint64_t np = ALIGN_UP(size, PAGE_SIZE) / PAGE_SIZE;
	if (np > s->npages) {
		uint64_t *pg = kzalloc(np * 8);
		if (!pg)
			return E_NOMEM;
		if (s->pages)
			memcpy(pg, s->pages, s->npages * 8);
		kfree(s->pages);
		s->pages = pg;
		s->npages = np;
	} else {
		for (uint64_t i = np; i < s->npages; i++)
			if (s->pages[i]) {
				pmm_free(s->pages[i]);
				s->pages[i] = 0;
			}
		if ((size & (PAGE_SIZE - 1)) && np && s->pages[np - 1])
			memset((uint8_t *)arch_phys_to_virt(s->pages[np - 1]) + (size & (PAGE_SIZE - 1)), 0,
			       PAGE_SIZE - (size & (PAGE_SIZE - 1)));
	}
	s->size = size;
	return 0;
}

int64_t sys_shm_create(uint64_t size, uint64_t flags)
{
	struct shm *s = new_shm();
	if (!s)
		return E_NOMEM;
	s->sealable = (flags & 1) != 0;
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	int64_t r = resize(s, size);
	if (r == 0)
		r = add_handle(me(), &s->h, 0);
	if (r < 0)
		put_locked(&s->h);
	spin_unlock_irqrestore(&ipc_lock, f);
	return r;
}

static int64_t get_name(uint64_t uname, char *name)
{
	int64_t n = copy_string_from_user(name, uname, SHM_NAME);
	if (n == E_INVAL)
		return E_NAMETOOLONG;
	return n <= 0 ? (n ? n : E_INVAL) : 0;
}

#define SHM_CREAT 1
#define SHM_EXCL 2
#define SHM_TRUNC 4
#define SHM_WEAK 8

int64_t sys_shm_open(uint64_t uname, uint64_t flags)
{
	char name[SHM_NAME];
	int64_t r = get_name(uname, name);
	if (r)
		return r;
	struct shm *fresh = (flags & SHM_CREAT) ? new_shm() : 0;
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct shm *s = 0;
	int slot = -1;
	for (int i = 0; i < SHM_NAMES; i++) {
		if (names[i] && !strcmp(names[i]->name, name))
			s = names[i];
		else if (!names[i] && slot < 0)
			slot = i;
	}
	if (s && (flags & SHM_CREAT) && (flags & SHM_EXCL))
		r = E_EXIST;
	else if (!s && !(flags & SHM_CREAT))
		r = E_NOENT;
	else if (!s && slot < 0)
		r = E_NFILE;
	else if (!s && !fresh)
		r = E_NOMEM;
	if (r == 0 && !s) {
		s = fresh;
		fresh = 0;
		memcpy(s->name, name, SHM_NAME);
		s->weak = (flags & SHM_WEAK) != 0;
		names[slot] = s;
		/* la référence de création devient celle du nom ; un nom faible n'en tient pas */
		if (s->weak)
			s->h.refs--;
	}
	if (r == 0) {
		if ((flags & SHM_TRUNC))
			resize(s, 0);
		s->h.refs++;
		r = add_handle(me(), &s->h, 0);
		if (r < 0)
			s->h.refs--;
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	if (fresh)
		kfree(fresh);
	return r;
}

int64_t sys_shm_unlink(uint64_t uname)
{
	char name[SHM_NAME];
	int64_t r = get_name(uname, name);
	if (r)
		return r;
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	r = E_NOENT;
	for (int i = 0; i < SHM_NAMES; i++)
		if (names[i] && !strcmp(names[i]->name, name)) {
			struct shm *s = names[i];
			names[i] = 0;
			s->name[0] = 0;
			int strong = !s->weak;
			if (s->pinned)
				s->pinned = 0, put_locked(&s->h);
			if (strong)
				put_locked(&s->h); /* la référence du nom */
			r = 0;
			break;
		}
	spin_unlock_irqrestore(&ipc_lock, f);
	return r;
}

#define SHM_SET_SIZE 1
#define SHM_ADD_SEALS 2
#define SHM_PIN 3
#define SHM_UNPIN 4
#define SEAL_SEAL 1
#define SEAL_SHRINK 2
#define SEAL_GROW 4
#define SEAL_WRITE 8

int64_t sys_shm_ctl(uint64_t h, uint64_t op, uint64_t val)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct shm *s = (struct shm *)get_handle(me(), h);
	int64_t r = 0;
	if (!s || s->h.kind != K_SHM)
		r = E_BADF;
	else if (op == SHM_SET_SIZE) {
		if ((val > s->size && (s->seals & SEAL_GROW)) || (val < s->size && (s->seals & SEAL_SHRINK)))
			r = E_PERM;
		else if (val < s->size && s->mapped)
			r = E_BUSY;
		else
			r = resize(s, val);
	} else if (op == SHM_ADD_SEALS) {
		if (!s->sealable || (s->seals & SEAL_SEAL))
			r = E_PERM;
		else
			s->seals |= (int)val;
	} else if (op == SHM_PIN || op == SHM_UNPIN) {
		if (!s->weak || !s->name[0])
			r = E_INVAL;
		else if (op == SHM_PIN && !s->pinned)
			s->pinned = 1, s->h.refs++;
		else if (op == SHM_UNPIN && s->pinned)
			s->pinned = 0, put_locked(&s->h);
	} else
		r = E_INVAL;
	spin_unlock_irqrestore(&ipc_lock, f);
	return r;
}

uint64_t ipc_shm_page(struct ipc_obj *o, uint64_t off)
{
	struct shm *s = (struct shm *)o;
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	uint64_t phys = 0;
	uint64_t i = off / PAGE_SIZE;
	if (off < ALIGN_UP(s->size, PAGE_SIZE) && i < s->npages) {
		if (!s->pages[i]) {
			uint64_t pg = pmm_alloc();
			if (pg) {
				memset(arch_phys_to_virt(pg), 0, PAGE_SIZE);
				s->pages[i] = pg;
			}
		}
		phys = s->pages[i];
	}
	spin_unlock_irqrestore(&ipc_lock, f);
	return phys;
}

void ipc_shm_mapped(struct ipc_obj *o, int delta)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	((struct shm *)o)->mapped += delta;
	spin_unlock_irqrestore(&ipc_lock, f);
}

int64_t sys_shm_map(uint64_t h, uint64_t addr, uint64_t size, uint64_t prot, uint64_t flags, uint64_t off)
{
	uint64_t f = spin_lock_irqsave(&ipc_lock);
	struct shm *s = (struct shm *)get_handle(me(), h);
	int64_t r = 0;
	if (!s || s->h.kind != K_SHM)
		r = E_BADF;
	else if ((s->seals & SEAL_WRITE) && (prot & PROT_WRITE) && !(flags & 0x200000))
		r = E_PERM;
	else
		s->h.refs++; /* la région en tiendra une */
	spin_unlock_irqrestore(&ipc_lock, f);
	if (r)
		return r;
	r = mm_map_shared(&s->h, addr, size, prot, flags, off);
	if (r < 0)
		ipc_obj_put(&s->h);
	return r;
}
