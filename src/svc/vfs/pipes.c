/* pipes.c : tubes et entrées / sorties standard des programmes, traduction de
 * pipes.inc (docs/FICHIERS.md, étape 4).
 *  * Un tube : un anneau de PIPE_CAP octets, compteurs de lecteurs et
 *    d'écrivains. Un bout de tube : une poignée (1..END_MAX) qui appartient à un
 *    ou plusieurs pid (table owners) ; seuls eux s'en servent (E_BADF sinon).
 *    Après un fork, le fils tient tous les bouts du père, aux mêmes numéros.
 *    Le bout se ferme avec son dernier propriétaire ; la fin (0) vient quand
 *    plus aucun pid ne tient d'écrivain.
 *  * Lecture : ce qui est là (<= 464 o ; V_PIPE_READ_BULK : <= 64 Ko, écrits directement dans la mémoire de
 *    l'appelant par SVC_MAP_CALLER, et V_PIPE_WRITE_BULK de même : 464 octets par aller et retour limitaient git,
 *    21/11/2026) ; vide avec un écrivain : E_AGAIN
 *    (PIPE_NOWAIT) ou réponse différée (WAIT_MS au plus, WAIT_COURT avec
 *    PIPE_SHORT : puis E_AGAIN). Écriture : ce qui tient (0 : plein) ; E_PIPE
 *    sans lecteur.
 *  * Entrées / sorties standard : trois poignées par pid (pids.c), données par
 *    l'avis de naissance ; un bout de vterm (VT_TAG) est gardé tel quel.
 *  * Redirection vers un fichier (V_FILE_END) : un tube sans anneau
 *    (PIPE_FICHIER) qui garde un chemin.
 *  * Anneau partagé (PC, 22/11/2026, docs/etude/ANNEAUX.md § 4) : à la demande d'un client (V_PIPE_RING, libposix
 *    après 64 Ko passés par messages), les données du tube passent dans un tampon partagé (SYS_BUF_*) donné aux
 *    process qui tiennent un de ses bouts : ils lisent et écrivent sans message, et dorment sur les futex de son
 *    en-tête (struct pipe_ring, fssvc.h). vfs garde les compteurs de bouts, la fin, et sert toujours les clients
 *    par messages sur le même anneau (sous les mêmes verrous). Au plus PIPE_RINGS à la fois (les tampons du noyau
 *    sont comptés : 128 pour tout le système). */
#include "vfs.h"

#define PIPE_MAX 128
#define PIPE_CAP 65536 /* (puissance de 2 ; 64 Ko comme Linux : bash y écrit un here-document entier avant de lancer la commande) */
#define PIPE_FICHIER 2
#define PIPE_CHEMIN 256
#define END_MAX 512
#define OWN_MAX 4096
#define WAIT_MAX 64
#define WAIT_MS 10000
#define WAIT_COURT 1000
#define PIPE_E_MFILE -24
#define PIPE_ATOMIC 4096 /* PIPE_BUF de libposix : une écriture jusque-là ne se coupe pas */

struct pipe {
	u64 used; /* 0 libre, 1 tube, PIPE_FICHIER */
	u64 head, tail; /* (sans anneau partagé ; avec : ceux de son en-tête) */
	u32 readers, writers;
	u8 *ring;  /* anneau partagé : son en-tête (0 : aucun, les données dans pipe_data) */
	long buf;  /* son tampon du noyau */
};

#if defined(__x86_64__)
#define PIPE_RINGS 24 /* (PC : futex partagés entre process) */
#else
#define PIPE_RINGS 0 /* (Pi : le noyau n'a pas de futex partagés) */
#endif
#define RING_VFS 0xFFFFFFFEu /* verrou pris par vfs */
static int rings_used;
struct end {
	u32 pipe; /* tube + 1 (0 : libre) */
	u32 nown; /* propriétaires */
	u64 mode; /* 1 lecture, 2 écriture */
};
struct wait {
	long req; /* numéro de la requête (0 : libre) */
	u64 end;  /* poignée */
	u64 bytes;
	u64 bulk; /* V_PIPE_READ_BULK : l'adresse du tampon de l'appelant (0 : dans le message) */
	u64 limit; /* échéance (ms) */
	u64 pid;
};

static struct pipe pipes[PIPE_MAX];
static struct end ends[END_MAX];
static u32 owners[OWN_MAX][2]; /* (bout, pid) */
static struct wait waits[WAIT_MAX];
static vmsg rmsg __attribute__((aligned(16)));
static u8 pipe_data[PIPE_MAX][PIPE_CAP];
static char pipe_chemins[PIPE_MAX][PIPE_CHEMIN];

static u64 *stdio_entry(long pid)
{
	struct pr *r = pid_get(pid);
	return r ? r->stdio : 0;
}

static u32 *own_find(u64 h, u64 pid)
{
	for (int i = 0; i < OWN_MAX; i++)
		if (owners[i][0] == (u32)h && owners[i][1] == (u32)pid)
			return owners[i];
	return 0;
}

/* un propriétaire de plus (rien s'il l'est déjà) -> 1, 0 si la table est pleine */
static long own_add(u64 h, u64 pid)
{
	if (own_find(h, pid))
		return 1;
	for (int i = 0; i < OWN_MAX; i++)
		if (!owners[i][0]) {
			owners[i][0] = (u32)h;
			owners[i][1] = (u32)pid;
			ends[h - 1].nown++;
			return 1;
		}
	return 0;
}

/* poignée, pid -> le bout (0 : libre, invalide ou pas à lui) */
static struct end *end_get(u64 h, u64 pid)
{
	if (h - 1 >= END_MAX)
		return 0;
	struct end *e = &ends[h - 1];
	if (!e->pipe || !own_find(h, pid))
		return 0;
	return e;
}

static struct pipe *end_pipe(struct end *e, u64 *num)
{
	*num = (u64)e->pipe - 1;
	return &pipes[*num];
}

/* numéro du tube, pid, mode -> poignée (0 : table pleine) */
static u64 end_alloc(u64 num, u64 pid, u64 mode)
{
	for (int i = 0; i < END_MAX; i++) {
		struct end *e = &ends[i];
		if (e->pipe)
			continue;
		e->pipe = (u32)num + 1;
		e->nown = 0;
		e->mode = mode;
		if (!own_add(i + 1, pid)) {
			e->pipe = 0; /* plus de couple : bout rendu */
			return 0;
		}
		return i + 1;
	}
	return 0;
}

static inline struct pipe_ring *RH(struct pipe *p)
{
	return (struct pipe_ring *)p->ring;
}

static inline void cpu_relax(void)
{
#if defined(__aarch64__)
	__asm__ volatile("yield" ::: "memory");
#else
	__asm__ volatile("pause" ::: "memory");
#endif
}

static u64 pipe_head(struct pipe *p)
{
	return p->ring ? __atomic_load_n(&RH(p)->head, __ATOMIC_ACQUIRE) : p->head;
}

static u64 pipe_tail(struct pipe *p)
{
	return p->ring ? __atomic_load_n(&RH(p)->tail, __ATOMIC_ACQUIRE) : p->tail;
}

static u64 pipe_avail(struct pipe *p)
{
	return (pipe_head(p) - pipe_tail(p)) & (PIPE_CAP - 1);
}

/* un verrou de l'anneau, pris par vfs (il ne dort jamais dessus : un client copie quelques µs) -> 1, 0 occupé */
static int ring_lock(volatile u32 *l)
{
	for (int k = 0; k < 4096; k++) {
		u32 z = 0;
		if (__atomic_compare_exchange_n(l, &z, RING_VFS, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
			return 1;
		cpu_relax();
	}
	return 0;
}

/* un changement : le compteur du futex, puis ses dormeurs réveillés s'il y en a */
static void ring_wake(volatile u32 *seq, volatile u32 *waiters)
{
	__atomic_add_fetch(seq, 1, __ATOMIC_RELEASE);
	if (__atomic_load_n(waiters, __ATOMIC_ACQUIRE))
		korin_sys2(SYS_FUTEX_WAKE, (long)seq, 0x7fffffff);
}

/* les bouts ont changé : l'en-tête de l'anneau, et tous ses dormeurs réveillés (fin, plus de lecteur) */
static void pipe_sync(struct pipe *p)
{
	if (!p->ring)
		return;
	struct pipe_ring *h = RH(p);
	__atomic_store_n(&h->readers, p->readers, __ATOMIC_RELEASE);
	__atomic_store_n(&h->writers, p->writers, __ATOMIC_RELEASE);
	ring_wake(&h->wseq, &h->rwait);
	ring_wake(&h->rseq, &h->wwait);
}

/* (par memcpy, en deux morceaux au tour de l'anneau) ; 0 aussi quand le verrou de l'anneau est pris */
static u64 pipe_take(struct pipe *p, u64 num, u8 *dst, u64 max)
{
	struct pipe_ring *r = RH(p);
	if (r && !ring_lock(&r->rlock))
		return 0;
	u8 *d = r ? p->ring + PIPE_RING_HDR : pipe_data[num];
	u64 t = pipe_tail(p), n = pipe_avail(p);
	if (n > max)
		n = max;
	u64 a = PIPE_CAP - t < n ? PIPE_CAP - t : n;
	memcpy(dst, d + t, a);
	memcpy(dst + a, d, n - a);
	if (r) {
		__atomic_store_n(&r->tail, (u32)((t + n) & (PIPE_CAP - 1)), __ATOMIC_RELEASE);
		__atomic_store_n(&r->rlock, 0, __ATOMIC_RELEASE);
		if (n)
			ring_wake(&r->rseq, &r->wwait);
	} else
		p->tail = (t + n) & (PIPE_CAP - 1);
	return n;
}

static u64 pipe_put(struct pipe *p, u64 num, const u8 *src, u64 len)
{
	struct pipe_ring *r = RH(p);
	if (r && !ring_lock(&r->wlock))
		return 0;
	u8 *d = r ? p->ring + PIPE_RING_HDR : pipe_data[num];
	u64 h = pipe_head(p), room = (PIPE_CAP - 1) - pipe_avail(p); /* (l'anneau garde un octet vide) */
	u64 n = len < room ? len : room;
	u64 a = PIPE_CAP - h < n ? PIPE_CAP - h : n;
	memcpy(d + h, src, a);
	memcpy(d, src + a, n - a);
	if (r) {
		__atomic_store_n(&r->head, (u32)((h + n) & (PIPE_CAP - 1)), __ATOMIC_RELEASE);
		__atomic_store_n(&r->wlock, 0, __ATOMIC_RELEASE);
		if (n)
			ring_wake(&r->wseq, &r->rwait);
	} else
		p->head = (h + n) & (PIPE_CAP - 1);
	return n;
}

/* vfs_wait de l'anneau du tube num : des lectures attendent-elles ici ? */
static void pipe_waits_flag(u64 num)
{
	struct pipe *p = &pipes[num];
	if (!p->ring)
		return;
	u32 any = 0;
	for (int i = 0; i < WAIT_MAX && !any; i++)
		if (waits[i].req && ends[waits[i].end - 1].pipe == (u32)num + 1)
			any = 1;
	__atomic_store_n(&RH(p)->vfs_wait, any, __ATOMIC_RELEASE);
}

/* l'anneau partagé du tube num (créé s'il n'en a pas encore : ce qui y est déjà passe dedans) -> 0, E_AGAIN */
static long ring_make(struct pipe *p, u64 num)
{
	if (p->ring)
		return 0;
	if (rings_used >= PIPE_RINGS)
		return E_AGAIN;
	long b = korin_sys1(SYS_BUF_CREATE, PIPE_RING_HDR + PIPE_RING_CAP);
	if (b < 0)
		return E_AGAIN;
	long va = korin_sys1(SYS_BUF_MAP, b);
	if (va < 0) {
		korin_sys1(SYS_BUF_FREE, b);
		return E_AGAIN;
	}
	u8 *r = (u8 *)va;
	memset(r, 0, PIPE_RING_HDR);
	memcpy(r + PIPE_RING_HDR, pipe_data[num], PIPE_CAP);
	struct pipe_ring *h = (struct pipe_ring *)r;
	h->head = (u32)p->head;
	h->tail = (u32)p->tail;
	h->readers = p->readers;
	h->writers = p->writers;
	h->magic = PIPE_RING_MAGIC;
	p->buf = b;
	__atomic_store_n(&p->ring, r, __ATOMIC_RELEASE);
	rings_used++;
	pipe_waits_flag(num);
	return 0;
}

static void ring_drop(struct pipe *p)
{
	if (!p->ring)
		return;
	p->ring = 0;
	korin_sys1(SYS_BUF_FREE, p->buf); /* (le tampon part avec le dernier process qui l'a encore) */
	rings_used--;
}

/* la mémoire de l'appelant d'une requête (SVC_MAP_CALLER) -> adresse ici, < 0 si refusée */
static long caller_map(long req, u64 addr, u64 len, int write)
{
	if (!addr || !len)
		return E_INVAL;
	return korin_sys4(SYS_SVC_MAP_CALLER, req, addr, len, write ? MC_WRITE : 0);
}

/* lecture en attente, résultat, octets déjà dans rmsg : réponse (différée) */
static void wait_reply(struct wait *w, long r, u64 bytes)
{
	rmsg.op = r;
	rmsg.len = (u32)bytes;
	long req = w->req;
	w->req = 0;
	korin_sys2(SYS_SVC_REPLY, req, &rmsg);
}

/* numéro d'un tube : sert les lectures qui l'attendent (données, ou fin) */
static void pipe_serve(u64 num)
{
	for (int i = 0; i < WAIT_MAX; i++) {
		struct wait *w = &waits[i];
		if (!w->req)
			continue;
		u64 n;
		struct pipe *p = end_pipe(&ends[w->end - 1], &n);
		if (n != num)
			continue;
		if (pipe_avail(p)) {
			if (w->bulk) { /* (V_PIPE_READ_BULK : dans la mémoire de l'appelant) */
				u64 k = pipe_avail(p) < w->bytes ? pipe_avail(p) : w->bytes;
				long va = caller_map(w->req, w->bulk, k, 1);
				if (va < 0) {
					wait_reply(w, va, 0);
					continue;
				}
				u64 got = pipe_take(p, num, (u8 *)va, k);
				if (got) /* (0 : l'anneau est pris par un client, la lecture attend encore) */
					wait_reply(w, (long)got, 0);
				continue;
			}
			u64 k = pipe_take(p, num, rmsg.data, w->bytes);
			if (k)
				wait_reply(w, k, k);
		} else if (!p->writers) { /* plus d'écrivain : fin */
			wait_reply(w, 0, 0);
		}
	}
	pipe_waits_flag(num);
}

/* poignée (valide) : le bout est fermé ; un lecteur ou un écrivain de moins ;
 * plus d'écrivain : fin pour les lectures en attente ; tube libéré quand il n'a
 * plus personne */
static void end_close(u64 h)
{
	struct end *e = &ends[h - 1];
	if (!e->pipe)
		return;
	for (int i = 0; i < WAIT_MAX; i++) /* ses lectures en attente : E_BADF */
		if (waits[i].req && waits[i].end == h)
			wait_reply(&waits[i], E_BADF, 0);
	pid_stdio_drop(h); /* plus une entrée standard */
	for (int i = 0; i < OWN_MAX; i++) /* plus de propriétaire */
		if (owners[i][0] == (u32)h)
			owners[i][0] = owners[i][1] = 0;
	e->nown = 0;
	u64 num;
	struct pipe *p = end_pipe(e, &num);
	u64 mode = e->mode;
	e->pipe = 0;
	if (mode == 1) {
		p->readers--;
	} else if (!--p->writers) {
		pipe_serve(num); /* dernier écrivain : fin pour les lecteurs */
	}
	pipe_sync(p);
	pipe_waits_flag(num);
	if (!(p->readers | p->writers)) {
		ring_drop(p);
		p->used = 0;
	}
}

/* poignée, pid (un de ses propriétaires) : le pid quitte le bout ; le dernier
 * parti, le bout est fermé */
static void end_drop(u64 h, u64 pid)
{
	u32 *o = own_find(h, pid);
	if (!o)
		return;
	o[0] = o[1] = 0;
	for (int i = 0; i < WAIT_MAX; i++) /* ses lectures en attente */
		if (waits[i].req && waits[i].end == h && waits[i].pid == pid)
			wait_reply(&waits[i], E_BADF, 0);
	struct pr *r = pid_find(pid); /* son entrée standard */
	if (r)
		for (int k = 0; k < 3; k++)
			if (r->stdio[k] == h)
				r->stdio[k] = 0;
	struct end *e = &ends[h - 1]; /* un propriétaire de moins */
	e->nown = e->nown ? e->nown - 1 : 0;
	if (!e->nown) /* le dernier : fermé */
		end_close(h);
}

void ends_pid_gone(long pid)
{
	for (int i = 0; i < PIPE_MAX; i++) /* (ses verrous d'anneaux, s'il est mort en copiant) */
		if (pipes[i].used && pipes[i].ring) {
			struct pipe_ring *h = RH(&pipes[i]);
			u32 me = (u32)pid + 1;
			__atomic_compare_exchange_n(&h->wlock, &me, 0, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
			me = (u32)pid + 1;
			__atomic_compare_exchange_n(&h->rlock, &me, 0, 0, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
		}
	for (int i = 0; i < OWN_MAX; i++)
		if (owners[i][0] && owners[i][1] == (u32)pid)
			end_drop(owners[i][0], pid);
	struct pr *r = pid_find(pid);
	if (r)
		r->stdio[0] = r->stdio[1] = r->stdio[2] = 0;
}

/* avis de fork : le fils tient les bouts du père (mêmes poignées) et ses
 * entrées / sorties -> bouts que la table pleine n'a pas pu lui donner */
long ends_fork(long child, long parent)
{
	long refused = 0;
	for (int i = 0; i < OWN_MAX; i++)
		if (owners[i][0] && owners[i][1] == (u32)parent && !own_add(owners[i][0], child))
			refused++;
	struct pr *p = pid_find(parent);
	if (p) {
		u64 *s = stdio_entry(child);
		if (s)
			for (int k = 0; k < 3; k++)
				s[k] = p->stdio[k];
	}
	return refused;
}

/* échéances des lectures en attente : E_AGAIN (le client redemande) */
void waits_tick(void)
{
	u64 now = korin_sys0(SYS_TIME_MS);
	int any = 0;
	for (int i = 0; i < WAIT_MAX; i++)
		if (waits[i].req && waits[i].limit <= now) {
			wait_reply(&waits[i], E_AGAIN, 0);
			any = 1;
		}
	if (any)
		for (int i = 0; i < PIPE_MAX; i++)
			if (pipes[i].ring)
				pipe_waits_flag(i);
}

long waits_any(void)
{
	for (int i = 0; i < WAIT_MAX; i++)
		if (waits[i].req)
			return 1;
	return 0;
}

/* avis de naissance : fils, père, entrée, sortie, erreur (0 : celle du père,
 * -1 : aucune, sinon un bout du père) ; le fils reçoit ses copies */
void stdio_birth(long child, long parent, long in, long out, long err)
{
	long give[3] = {in, out, err};
	u64 *s = stdio_entry(child);
	if (!s)
		return;
	s[0] = s[1] = s[2] = 0;
	for (int k = 0; k < 3; k++) {
		u64 h = give[k];
		if (h == (u64)-1) /* aucune */
			continue;
		if (!h) { /* 0 : celle du père */
			u64 *ps = stdio_entry(parent);
			if (!ps || !(h = ps[k]))
				continue;
		}
		if (h & VT_TAG) { /* un bout de vterm : gardé tel quel */
			s[k] = h;
			continue;
		}
		struct end *e = end_get(h, parent); /* un bout du père ? */
		if (!e)
			continue;
		u64 num;
		struct pipe *p = end_pipe(e, &num);
		u64 nh = end_alloc(num, child, e->mode);
		if (!nh)
			continue;
		s[k] = nh;
		if (e->mode == 1)
			p->readers++;
		else
			p->writers++;
		pipe_sync(p);
	}
}

/* avis d'exec : pid, entrée, sortie, erreur (0 : inchangée ; -1 : aucune ;
 * sinon un bout de ce pid ou de vterm) */
void stdio_exec(long pid, long in, long out, long err)
{
	long give[3] = {in, out, err};
	u64 *s = stdio_entry(pid);
	if (!s)
		return;
	for (int k = 0; k < 3; k++) {
		u64 h = give[k];
		if (!h)
			continue;
		if (h == (u64)-1) {
			s[k] = 0; /* aucune (console) */
			continue;
		}
		if (!(h & VT_TAG) && !end_get(h, pid))
			continue;
		s[k] = h;
	}
}

static long find_free_pipe(void)
{
	for (int i = 0; i < PIPE_MAX; i++)
		if (!pipes[i].used)
			return i;
	return -1;
}

/* (msg) ops 37-41 et 48 au nom de MSG_PID -> résultat, *deferred = 1 : réponse différée */
long ops_pipes(int *deferred)
{
	vmsg *m = &msg;
	u64 op = m->op, pid = m->pid;
	*deferred = 0;
	if (op == V_PIPE) {
		m->len = 0;
		long num = find_free_pipe();
		if (num < 0)
			return PIPE_E_MFILE;
		u64 r = end_alloc(num, pid, 1);
		if (!r)
			return PIPE_E_MFILE;
		u64 w = end_alloc(num, pid, 2);
		if (!w) { /* rendre le premier bout */
			u32 *o = own_find(r, pid);
			if (o)
				o[0] = o[1] = 0;
			ends[r - 1].pipe = 0;
			return PIPE_E_MFILE;
		}
		m->a = r;
		m->b = w;
		struct pipe *p = &pipes[num];
		p->used = 1;
		p->head = p->tail = 0;
		p->readers = p->writers = 1;
		p->ring = 0;
		return 0;
	}
	if (op == V_STDIO) {
		m->len = 0;
		m->a = m->b = m->c = m->d = 0;
		u64 *s = stdio_entry(pid);
		if (!s)
			return 0;
		i64 *out = &m->a;
		for (int k = 0; k < 3; k++) {
			u64 h = s[k];
			if (!h)
				continue;
			if (h & VT_TAG) { /* un bout de vterm : rendu tel quel */
				out[k] = h;
				continue;
			}
			struct end *e = end_get(h, pid); /* (encore à lui ?) */
			if (!e)
				continue;
			u64 num;
			struct pipe *p = end_pipe(e, &num);
			out[k] = h;
			if (p->used == PIPE_FICHIER)
				m->d |= 1 << k;
		}
		return 0;
	}
	struct end *e = end_get(m->a, pid);
	if (!e)
		return E_BADF;
	if (op == V_PIPE_CLOSE) { /* ce pid quitte le bout */
		end_drop(m->a, pid);
		m->len = 0;
		return 0;
	}
	u64 num;
	struct pipe *p = end_pipe(e, &num);
	if (op == V_PIPE_RING) { /* l'anneau partagé, donné à ce process */
		m->len = 0;
		if (!PIPE_RINGS || p->used != 1)
			return E_INVAL;
		long r = ring_make(p, num);
		if (r < 0)
			return r;
		if (korin_sys2(SYS_BUF_GRANT, p->buf, pid) < 0)
			return E_AGAIN;
		return p->buf;
	}
	if (op == V_PIPE_KICK) { /* un écrivain de l'anneau : les lectures en attente ici */
		m->len = 0;
		pipe_serve(num);
		return 0;
	}
	if (op == V_PIPE_READ || op == V_PIPE_READ_BULK) {
		if (e->mode != 1)
			return E_BADF;
		m->len = 0;
		int bulk = op == V_PIPE_READ_BULK;
		u64 cap = bulk ? PIPE_BULK_MAX : FS_DATA_MAX;
		u64 n = (u64)m->b > cap ? cap : (u64)m->b;
		if (!n)
			return 0;
		if (bulk && !m->d)
			return E_INVAL;
		if (pipe_avail(p)) {
			if (bulk) {
				u64 k = pipe_avail(p) < n ? pipe_avail(p) : n;
				long va = caller_map(current_req, (u64)m->d, k, 1);
				if (va < 0)
					return E_INVAL; /* (pas de SVC_MAP_CALLER : le client revient aux messages) */
				u64 got = pipe_take(p, num, (u8 *)va, k);
				return got ? (long)got : E_AGAIN; /* (0 : l'anneau pris par un client : redemander) */
			}
			u64 k = pipe_take(p, num, m->data, n);
			m->len = (u32)k;
			return k ? (long)k : E_AGAIN;
		}
		if (!p->writers) /* plus d'écrivain : fin */
			return 0;
		if (m->c & PIPE_NOWAIT)
			return E_AGAIN;
		for (int i = 0; i < WAIT_MAX; i++) { /* réponse différée */
			struct wait *w = &waits[i];
			if (w->req)
				continue;
			w->req = current_req;
			w->end = m->a;
			w->bytes = n;
			w->bulk = bulk ? (u64)m->d : 0;
			w->pid = pid;
			w->limit = korin_sys0(SYS_TIME_MS) + ((m->c & PIPE_SHORT) ? WAIT_COURT : WAIT_MS);
			if (p->ring) /* (les écrivains de l'anneau préviendront vfs) */
				__atomic_store_n(&RH(p)->vfs_wait, 1, __ATOMIC_RELEASE);
			*deferred = 1;
			return 0;
		}
		return E_AGAIN; /* (table pleine : le client redemande) */
	}
	if (e->mode != 2) /* écrire */
		return E_BADF;
	if (!p->readers)
		return E_PIPE;
	if (op == V_PIPE_ROOM) { /* place libre (l'anneau garde un octet vide) */
		m->len = 0;
		return (PIPE_CAP - 1) - pipe_avail(p);
	}
	u64 k;
	if (op == V_PIPE_WRITE_BULK) { /* depuis la mémoire de l'appelant : ce qui tient */
		u64 room = (PIPE_CAP - 1) - pipe_avail(p);
		u64 len = (u64)m->b > PIPE_BULK_MAX ? PIPE_BULK_MAX : (u64)m->b;
		if (len <= PIPE_ATOMIC && len > room) /* (PIPE_BUF octets au plus : d'un seul morceau, POSIX) */
			len = 0;
		if (len > room)
			len = room;
		m->len = 0;
		if (!len)
			return 0;
		long va = caller_map(current_req, (u64)m->d, len, 0);
		if (va < 0)
			return E_INVAL;
		k = pipe_put(p, num, (const u8 *)va, len);
	} else {
		u64 len = m->len > FS_DATA_MAX ? FS_DATA_MAX : m->len;
		k = len > (PIPE_CAP - 1) - pipe_avail(p) ? 0 : pipe_put(p, num, m->data, len); /* (d'un seul morceau) */
		m->len = 0;
	}
	if (k) /* des lecteurs attendaient ? */
		pipe_serve(num);
	return k;
}

/* (msg) V_FILE_END, V_END_FILE -> résultat */
long ops_bouts(void)
{
	vmsg *m = &msg;
	u64 pid = m->pid;
	if (m->op == V_END_FILE) {
		struct end *e = end_get(m->a, pid);
		if (!e)
			return E_BADF;
		u64 num;
		struct pipe *p = end_pipe(e, &num);
		m->len = 0;
		if (p->used != PIPE_FICHIER)
			return 0;
		m->b = e->mode;
		u64 n = 0;
		do
			m->data[n] = pipe_chemins[num][n];
		while (pipe_chemins[num][n++]);
		m->len = (u32)n;
		return 1;
	}
	u64 mode = m->a;
	if (mode - 1 > 1)
		return E_INVAL;
	((u8 *)m)[MSG_SIZE - 1] = 0;
	long r = resoudre(pid, (char *)m->data, resolu);
	if (r)
		return r;
	long num = find_free_pipe();
	if (num < 0)
		return PIPE_E_MFILE;
	u64 h = end_alloc(num, pid, mode);
	if (!h)
		return PIPE_E_MFILE;
	m->a = h;
	struct pipe *p = &pipes[num];
	p->used = PIPE_FICHIER;
	p->head = p->tail = 0;
	p->ring = 0;
	p->readers = mode == 1;
	p->writers = mode != 1;
	chemin_externe(resolu, pipe_chemins[num]);
	m->len = 0;
	return 0;
}

/* V_PIPE_DUMP : les tubes au journal, chacun avec ses bouts, leur sens et les pid qui les
 * tiennent, puis les lectures en attente (qui tient encore l'écrivain d'un tube dont on attend
 * la fin ?) */
static void dput(const char *s) { ak_puts(s); }
static void dnum(u64 v)
{
	char b[24];
	int i = 23;
	b[i] = 0;
	do {
		b[--i] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	ak_puts(b + i);
}
void pipes_dump(void)
{
	dput("vfs : tubes\n");
	for (int i = 0; i < PIPE_MAX; i++) {
		struct pipe *p = &pipes[i];
		if (!p->used)
			continue;
		dput("vfs : tube ");
		dnum(i);
		dput(" r=");
		dnum(p->readers);
		dput(" w=");
		dnum(p->writers);
		dput(" avail=");
		dnum(pipe_avail(p));
		dput(" bouts :");
		for (int h = 0; h < END_MAX; h++) {
			if (ends[h].pipe != (u32)i + 1)
				continue;
			dput(" [");
			dnum(h + 1);
			dput(ends[h].mode == 1 ? " R" : " W");
			dput(" pid");
			for (int o = 0; o < OWN_MAX; o++)
				if (owners[o][0] == (u32)h + 1) {
					dput(" ");
					dnum(owners[o][1]);
				}
			dput("]");
		}
		dput("\n");
	}
	for (int i = 0; i < WAIT_MAX; i++)
		if (waits[i].req) {
			dput("vfs : lecture en attente, bout ");
			dnum(waits[i].end);
			dput(" pid ");
			dnum(waits[i].pid);
			dput("\n");
		}
	dput("vfs : tubes, fin\n");
}
