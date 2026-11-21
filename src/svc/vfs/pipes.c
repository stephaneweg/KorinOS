/* pipes.c : tubes et entrées / sorties standard des programmes, traduction de
 * pipes.inc (docs/FICHIERS.md, étape 4).
 *  * Un tube : un anneau de PIPE_CAP octets, compteurs de lecteurs et
 *    d'écrivains. Un bout de tube : une poignée (1..END_MAX) qui appartient à un
 *    ou plusieurs pid (table owners) ; seuls eux s'en servent (E_BADF sinon).
 *    Après un fork, le fils tient tous les bouts du père, aux mêmes numéros.
 *    Le bout se ferme avec son dernier propriétaire ; la fin (0) vient quand
 *    plus aucun pid ne tient d'écrivain.
 *  * Lecture : ce qui est là (<= 464 o) ; vide avec un écrivain : E_AGAIN
 *    (PIPE_NOWAIT) ou réponse différée (WAIT_MS au plus, WAIT_COURT avec
 *    PIPE_SHORT : puis E_AGAIN). Écriture : ce qui tient (0 : plein) ; E_PIPE
 *    sans lecteur.
 *  * Entrées / sorties standard : trois poignées par pid (pids.c), données par
 *    l'avis de naissance ; un bout de vterm (VT_TAG) est gardé tel quel.
 *  * Redirection vers un fichier (V_FILE_END) : un tube sans anneau
 *    (PIPE_FICHIER) qui garde un chemin. */
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

struct pipe {
	u64 used; /* 0 libre, 1 tube, PIPE_FICHIER */
	u64 head, tail;
	u32 readers, writers;
};
struct end {
	u32 pipe; /* tube + 1 (0 : libre) */
	u32 nown; /* propriétaires */
	u64 mode; /* 1 lecture, 2 écriture */
};
struct wait {
	long req; /* numéro de la requête (0 : libre) */
	u64 end;  /* poignée */
	u64 bytes;
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

static u64 pipe_avail(struct pipe *p)
{
	return (p->head - p->tail) & (PIPE_CAP - 1);
}

static u64 pipe_take(struct pipe *p, u64 num, u8 *dst, u64 max)
{
	u8 *d = pipe_data[num];
	u64 h = p->head, t = p->tail, n = 0;
	while (n < max && t != h) {
		dst[n++] = d[t];
		t = (t + 1) & (PIPE_CAP - 1);
	}
	p->tail = t;
	return n;
}

static u64 pipe_put(struct pipe *p, u64 num, const u8 *src, u64 len)
{
	u8 *d = pipe_data[num];
	u64 h = p->head, t = p->tail, n = 0;
	while (n < len) {
		u64 nx = (h + 1) & (PIPE_CAP - 1);
		if (nx == t) /* plein */
			break;
		d[h] = src[n++];
		h = nx;
	}
	p->head = h;
	return n;
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
			u64 k = pipe_take(p, num, rmsg.data, w->bytes);
			wait_reply(w, k, k);
		} else if (!p->writers) { /* plus d'écrivain : fin */
			wait_reply(w, 0, 0);
		}
	}
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
	if (!(p->readers | p->writers))
		p->used = 0;
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
	for (int i = 0; i < WAIT_MAX; i++)
		if (waits[i].req && waits[i].limit <= now)
			wait_reply(&waits[i], E_AGAIN, 0);
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
	if (op == V_PIPE_READ) {
		if (e->mode != 1)
			return E_BADF;
		m->len = 0;
		u64 n = (u64)m->b > FS_DATA_MAX ? FS_DATA_MAX : (u64)m->b;
		if (!n)
			return 0;
		if (pipe_avail(p)) {
			u64 k = pipe_take(p, num, m->data, n);
			m->len = (u32)k;
			return k;
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
			w->pid = pid;
			w->limit = korin_sys0(SYS_TIME_MS) + ((m->c & PIPE_SHORT) ? WAIT_COURT : WAIT_MS);
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
	u64 len = m->len > FS_DATA_MAX ? FS_DATA_MAX : m->len;
	u64 k = pipe_put(p, num, m->data, len);
	m->len = 0;
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
