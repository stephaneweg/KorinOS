/* serve.c : le service « intelgpu » (étape 2) : un GPU partagé entre
 * plusieurs clients (igproto.h), lancé à la main (intelgpu start), jamais par
 * hwmanager ni drivers.txt tant que gltest n'est pas validé sur le ZBook.
 *
 * Deux fils : le principal prend les requêtes (SVC_NEXT) ; celui du moteur
 * attend l'interruption (MSI, ou sondage), lit le CSB, fait avancer la file,
 * répond aux attentes (IG_OP_WAIT, destructions de contextes en vol) et
 * surveille les délais. Un seul verrou (S.lock) pour tout l'état.
 *
 * Un client : un process (pid du message). Ce qu'il a — morceaux de mémoire,
 * espaces d'adresses, contextes — est à lui seul ; à sa fin (SVC_OP_END),
 * dès que ses contextes ne sont plus sur le moteur, tout est rendu. */
#include "igpu.h"
#include "../svclib/svclib.h"
#include "caller.h"

#define CLIENT_MAX 64
#define CHUNK_MAX 64
#define DEFER_MAX 128
#define E_BUSY -16
#define E_TIMEDOUT -110

struct chunk {
	long buf;
	u64 phys, bytes;
	u8 *va;
};
struct client {
	int pid, dying;
	struct chunk ch[CHUNK_MAX];
	int nch;
};
struct defer {
	int used, kind; /* 1 attente d'un lot, 2 destruction d'un contexte */
	long req;
	u32 seq;
	int ctx;
	long deadline;
	u64 t0;         /* (µs) la demande (eng_status : l'attente d'un client) */
};

static struct client clients[CLIENT_MAX];
static struct defer defers[DEFER_MAX];
static long chan;
static u8 eng_stack[64 * 1024] __attribute__((aligned(16)));

static struct client *client_of(int pid, int create)
{
	struct client *fr = 0;
	for (int i = 0; i < CLIENT_MAX; i++) {
		if (clients[i].pid == pid)
			return &clients[i];
		if (!clients[i].pid && !fr)
			fr = &clients[i];
	}
	if (!create || !fr)
		return 0;
	memset(fr, 0, sizeof *fr);
	fr->pid = pid;
	return fr;
}

static struct chunk *chunk_of(struct client *cl, long buf)
{
	for (int i = 0; i < cl->nch; i++)
		if (cl->ch[i].buf == buf)
			return &cl->ch[i];
	return 0;
}

/* un refus d'une requête qui crée : au journal, avec ce qui est pris (15/11 : Godot sur le ZBook, « execbuf keeps
 * returning ENOMEM » sans dire pourquoi) */
static void refusal(const char *what, int pid, long r)
{
	int ncl = 0, nvm = 0, nctx = 0, ng = 0;
	for (int i = 0; i < CLIENT_MAX; i++)
		ncl += clients[i].pid != 0;
	for (int v = 1; v < VM_MAX; v++)
		nvm += vms[v].used;
	for (int c = 0; c < CTX_MAX; c++)
		nctx += ctxs[c].used;
	for (int g = 0; g < SV_GGTT_PAGES; g++)
		ng += S.ggtt_used[g] != 0;
	glog("REFUS : %s du client %d (%ld) : clients %d/%d, espaces %d/%d, contextes %d/%d, GGTT du service %d/%d pages\n",
	     what, pid, r, ncl, CLIENT_MAX, nvm, VM_MAX - 1, nctx, CTX_MAX, ng, SV_GGTT_PAGES);
}

static int own_ctx(int c, int pid)
{
	return c >= 0 && c < CTX_MAX && ctxs[c].used && ctxs[c].owner == pid;
}

static int own_vm(int v, int pid)
{
	return v > 0 && v < VM_MAX && vms[v].used && vms[v].owner == pid;
}

static void reply(long req, long res, u64 a, u64 b, u64 c)
{
	u64 m[64];
	memset(m, 0, sizeof m);
	m[0] = (u64)res;
	m[1] = a;
	m[2] = b;
	m[3] = c;
	korin_sys2(SYS_SVC_REPLY, req, m);
}

static int defer_add(int kind, long req, u32 seq, int ctx, long ms)
{
	for (int i = 0; i < DEFER_MAX; i++)
		if (!defers[i].used) {
			defers[i] = (struct defer){1, kind, req, seq, ctx, now_ms() + ms, eng_us()};
			return 0;
		}
	return -1;
}

/* les attentes finies ou expirées : réponses */
static void defers_run(void)
{
	long now = now_ms();
	for (int i = 0; i < DEFER_MAX; i++) {
		struct defer *d = &defers[i];
		if (!d->used)
			continue;
		if (d->kind == 1) {
			if (seq_done(d->seq) || S.dead) {
				int lost = (int32_t)(S.status[IGS_LOST] - d->seq) >= 0 && S.status[IGS_LOST];
				eng_stat(ST_WAIT, eng_us() - d->t0);
				reply(d->req, 0, 0, 0, lost);
				d->used = 0;
			} else if (now > d->deadline) {
				reply(d->req, E_TIMEDOUT, 0, 0, 0);
				d->used = 0;
			}
		} else if (d->kind == 2 && eng_ctx_idle(d->ctx)) {
			ctx_destroy(d->ctx);
			reply(d->req, 0, 0, 0, 0);
			d->used = 0;
		}
	}
}

/* un client fini : tout ce qui est à lui, dès que ses contextes sont au repos */
static void clients_reap(void)
{
	for (int i = 0; i < CLIENT_MAX; i++) {
		struct client *cl = &clients[i];
		if (!cl->pid || !cl->dying)
			continue;
		int busy = 0;
		for (int c = 0; c < CTX_MAX; c++)
			if (ctxs[c].used && ctxs[c].owner == cl->pid)
				busy += !eng_ctx_idle(c);
		if (busy)
			continue;
		int nc = 0, nv = 0;
		for (int c = 0; c < CTX_MAX; c++)
			if (ctxs[c].used && ctxs[c].owner == cl->pid) {
				ctx_destroy(c);
				nc++;
			}
		for (int v = 1; v < VM_MAX; v++)
			if (vms[v].used && vms[v].owner == cl->pid) {
				vm_destroy(v);
				nv++;
			}
		for (int k = 0; k < cl->nch; k++) {
			if (G.simu)
				simu_forget(cl->ch[k].va);
			korin_sys1(SYS_BUF_FREE, cl->ch[k].buf);
		}
		gdbg("client %d fini : %d contextes, %d espaces, %d morceaux rendus\n", cl->pid, nc, nv, cl->nch);
		memset(cl, 0, sizeof *cl);
	}
}

/* ---- le fil du moteur */

static void engine_thread(long arg)
{
	(void)arg;
	long sec = now_ms();
	long active = 0; /* (ms) la dernière fois qu'un lot était en vol ou qu'un client attendait */
	u64 spur0 = 0;
	for (;;) {
		int hurry;
		mtx_lock(&S.lock);
		hurry = S.busy;
		for (int i = 0; i < DEFER_MAX && !hurry; i++)
			hurry = defers[i].used;
		mtx_unlock(&S.lock);
		/* Le délai se décide avant le sommeil : un lot donné pendant un sommeil de 50 ms (le moteur était
		 * libre) n'était vu fini qu'à son interruption, et certaines ne réveillent pas ce fil (ZBook, 22/11 :
		 * attentes des clients de 26 ms en moyenne, 50 au pire, pour des lots de quelques ms). Une seconde
		 * après la dernière activité, on regarde chaque milliseconde ; au repos, 50 ms comme avant. */
		long now = now_ms();
		if (hurry)
			active = now;
		int quick = hurry || now - active < 1000;
		long w = 0;
		if (S.irq >= 0)
			w = korin_sys2(SYS_IRQ_WAIT, S.irq, quick ? 1 : 50);
		else
			korin_sys1(SYS_SLEEP, quick ? 1 : 20);
		mtx_lock(&S.lock);
		if (S.irq >= 0) {
			u32 bits = gt_irq_ack();
			if (!bits && w != 1)
				S.spurious--; /* (un délai écoulé, pas une interruption sans objet) */
			if (bits) {
				S.irqs++;
				if (bits & GT_CS_MASTER_ERROR)
					glog("interruption : erreur du CS (EIR %08x, ESR %08x)\n", rd(RCS + R_EIR),
					     rd(RCS + R_ESR));
			}
			korin_sys1(SYS_IRQ_ACK, S.irq);
			if (now_ms() - sec >= 1000) {
				if (S.spurious - spur0 > 5000) {
					glog("tempête d'interruptions sans objet (%lu en 1 s) : sondage\n",
					     (unsigned long)(S.spurious - spur0));
					gt_irq_off();
				}
				sec = now_ms();
				spur0 = S.spurious;
			}
		}
		eng_poll();
		defers_run();
		clients_reap();
		int stop = S.stop;
		mtx_unlock(&S.lock);
		if (stop)
			break;
	}
	korin_sys1(SYS_THREAD_EXIT, 0);
}

/* ---- les requêtes */

static long handle(long req, u64 *m)
{
	u32 pid = M32(m, 44);
	u64 op = m[0], a = m[1], b = m[2], c = m[3];
	struct client *cl = client_of((int)pid, 1);
	if (!cl) {
		refusal("nouveau client", (int)pid, E_NOMEM);
		return E_NOMEM;
	}
	switch (op) {
	case IG_OP_INFO: {
		struct ig_info *in = (struct ig_info *)((u8 *)m + 48);
		memset(in, 0, sizeof *in);
		in->version = IG_VERSION;
		in->state = S.dead ? 0 : G.simu ? 2 : 1;
		in->pid = (u32)korin_sys0(SYS_GETPID);
		in->status_buf = (u32)S.status_buf;
		in->device = G.pci.device;
		in->revision = G.pci.rev;
		u32 dss = G.simu ? 3 : rd(DSS_ENABLE), eud = G.simu ? 0 : rd(EU_DISABLE) & 0xFF;
		int ndss = 0, off = 0;
		for (u32 v = dss; v; v &= v - 1)
			ndss++;
		for (u32 v = eud; v; v &= v - 1)
			off++;
		in->dss_mask = dss & 0x3F;
		in->slice_mask = 1;
		in->eu_per_dss = (8 - off) * 2;
		in->eu_total = ndss * in->eu_per_dss;
		in->ts_hz = 19200000;
		in->resets = S.status[IGS_RESETS];
		in->gtt_size = 1ull << 48;
		M32(m, 40) = sizeof *in;
		return 0;
	}
	case IG_OP_CHUNK: {
		u64 bi[4];
		if (cl->nch == CHUNK_MAX) {
			refusal("morceau (64 au plus)", (int)pid, E_NOMEM);
			return E_NOMEM;
		}
		if (korin_sys2(SYS_BUF_INFO, a, bi) < 0 || (int)bi[2] != (int)pid)
			return E_PERM;
		long va = korin_sys1(SYS_BUF_MAP, a);
		if (va < 0)
			return va;
		struct chunk *k = &cl->ch[cl->nch++];
		k->buf = (long)a;
		k->phys = bi[0];
		k->bytes = bi[1];
		k->va = (u8 *)va;
		if (G.simu)
			simu_note(k->phys, k->va, k->bytes);
		gdbg("client %u : morceau %ld, %lu Ko à %lx\n", pid, k->buf, (unsigned long)(k->bytes >> 10),
		     (unsigned long)k->phys);
		return 0;
	}
	case IG_OP_CHUNK_FREE: {
		struct chunk *k = chunk_of(cl, (long)a);
		if (!k)
			return E_INVAL;
		for (int x = 0; x < CTX_MAX; x++)
			if (ctxs[x].used && ctxs[x].owner == (int)pid && !eng_ctx_idle(x))
				return E_BUSY;
		/* délié de tous les espaces du client avant d'être rendu : sinon le GPU gardait l'accès à
		 * des pages rendues, reprises ensuite par n'importe qui (J7) ; les TLB du GPU sont vidés au
		 * début de chaque lot (PC_TLB_INV, engine.c), et aucun de ses contextes ne tourne */
		for (int v = 1; v < VM_MAX; v++)
			if (vms[v].used && vms[v].owner == (int)pid)
				vm_unbind_phys(v, k->phys, k->bytes);
		if (G.simu)
			simu_forget(k->va);
		korin_sys1(SYS_BUF_FREE, k->buf);
		*k = cl->ch[--cl->nch];
		return 0;
	}
	case IG_OP_VM_CREATE: {
		int v = vm_create((int)pid);
		m[1] = (u64)v;
		if (v < 0)
			refusal("espace d'adresses", (int)pid, v);
		return v < 0 ? v : 0;
	}
	case IG_OP_VM_DESTROY:
		if (!own_vm((int)a, (int)pid))
			return E_INVAL;
		for (int x = 0; x < CTX_MAX; x++)
			if (ctxs[x].used && ctxs[x].vm == (int)a)
				return E_BUSY;
		vm_destroy((int)a);
		return 0;
	case IG_OP_CTX_CREATE: {
		if (!own_vm((int)a, (int)pid)) {
			glog("REFUS : contexte du client %d sur l'espace %d, qui n'est pas à lui (propriétaire %d)\n", (int)pid,
			     (int)a, a > 0 && a < VM_MAX && vms[a].used ? vms[a].owner : -1);
			return E_INVAL;
		}
		int x = ctx_create((int)pid, (int)a, 0);
		m[1] = (u64)x;
		if (x < 0)
			refusal("contexte", (int)pid, x);
		return x < 0 ? x : 0;
	}
	case IG_OP_CTX_DESTROY:
		if (!own_ctx((int)a, (int)pid))
			return E_INVAL;
		if (eng_ctx_idle((int)a)) {
			ctx_destroy((int)a);
			return 0;
		}
		if (defer_add(2, req, 0, (int)a, 0))
			return E_NOMEM;
		return 1; /* réponse plus tard */
	case IG_OP_BIND:
	case IG_OP_UNBIND: {
		if (!own_vm((int)a, (int)pid) || b > IG_BIND_MAX)
			return E_INVAL;
		struct ig_bind *e = (struct ig_bind *)((u8 *)m + 48);
		for (u64 i = 0; i < b; i++) {
			if ((e[i].gpu | e[i].bytes) & 0xFFF)
				return E_INVAL;
			if (op == IG_OP_UNBIND) {
				vm_unbind((int)a, e[i].gpu, e[i].bytes);
				continue;
			}
			struct chunk *k = chunk_of(cl, e[i].buf);
			if (!k || (e[i].off & 0xFFF) || e[i].off + e[i].bytes > k->bytes || e[i].off + e[i].bytes < e[i].off)
				return E_INVAL;
			long r = vm_bind((int)a, e[i].gpu, k->phys + e[i].off, e[i].bytes);
			if (r)
				return r;
		}
		return 0;
	}
	case IG_OP_EXEC: {
		if (!own_ctx((int)a, (int)pid))
			return E_INVAL;
		long s = eng_exec((int)a, b, (u32)c, 0);
		if (s == E_AGAIN) { /* l'anneau ou la file pleins : un peu de place */
			eng_poll();
			s = eng_exec((int)a, b, (u32)c, 0);
		}
		if (s < 0)
			return s;
		eng_poll(); /* part tout de suite si le moteur est libre */
		m[1] = (u64)s;
		return 0;
	}
	case IG_OP_WAIT: {
		u32 seq = (u32)a;
		/* le moteur a peut-être fini sans que son fil soit déjà réveillé : la réponse tout de suite (et le
		 * lot suivant part) plutôt qu'au prochain passage du fil */
		if (!seq_done(seq) && !S.dead)
			eng_poll();
		if (seq_done(seq) || S.dead) {
			eng_waits_now++;
			m[3] = (int32_t)(S.status[IGS_LOST] - seq) >= 0 && S.status[IGS_LOST];
			return 0;
		}
		long ms = (long)b;
		if (ms <= 0)
			return E_TIMEDOUT;
		if (ms > 20000)
			ms = 20000;
		if (defer_add(1, req, seq, 0, ms))
			return E_NOMEM;
		eng_waits_later++;
		return 1;
	}
	case IG_OP_TIMESTAMP:
		m[1] = eng_timestamp();
		return 0;
	case IG_OP_RESET_STATS:
		m[1] = S.status[IGS_RESETS];
		m[2] = own_ctx((int)a, (int)pid) ? ctxs[a].lost : 0;
		return 0;
	case IG_OP_STATUS: {
		char *t = (char *)m + 48;
		eng_status(t, 464);
		M32(m, 40) = (u32)strlen(t);
		return 0;
	}
	case IG_OP_TIMES: {
		char *t = (char *)m + 48;
		eng_times(t, 464);
		M32(m, 40) = (u32)strlen(t);
		return 0;
	}
	case IG_OP_STOP: /* (init2 à l'arrêt, ou « intelgpu stop » par sudo : pas une app) */
		if (!caller_admin((long)pid, 0))
			return E_PERM;
		S.stop = 1;
		return 0;
	}
	return E_INVAL;
}

int serve(void)
{
	chan = korin_sys2(SYS_SVC_REGISTER, IG_SVC_NAME, SVF_ENDS);
	if (chan < 0) {
		glog("service « %s » refusé (%ld) : déjà lancé ?\n", IG_SVC_NAME, chan);
		return 1;
	}
	if (korin_sys3(SYS_THREAD_CREATE, engine_thread, eng_stack + sizeof eng_stack, 0) < 0) {
		glog("ANOMALIE : fil du moteur refusé\n");
		return 1;
	}
	glog("service « %s » prêt (%s) : lots soumis par execlists, délai %ld ms\n", IG_SVC_NAME,
	     S.irq >= 0 ? "MSI" : "sondage", S.hang_ms);
	static u64 m[64];
	while (!S.stop) {
		long req = korin_sys3(SYS_SVC_NEXT, chan, m, 1000);
		if (req <= 0)
			continue;
		mtx_lock(&S.lock);
		if (m[0] == SVC_OP_END) {
			struct client *cl = client_of((int)m[1], 0);
			if (cl)
				cl->dying = 1;
			mtx_unlock(&S.lock);
			continue;
		}
		if (m[0] == SVC_OP_SIGNAL) {
			mtx_unlock(&S.lock);
			continue;
		}
		long r = handle(req, m);
		mtx_unlock(&S.lock);
		if (r == 1)
			continue; /* réponse donnée plus tard par le fil du moteur */
		m[0] = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, m);
	}
	/* arrêt : le fil du moteur sort de lui-même (S.stop) ; les attentes en cours reçoivent leur réponse */
	korin_sys1(SYS_SLEEP, 100);
	mtx_lock(&S.lock);
	for (int i = 0; i < DEFER_MAX; i++)
		if (defers[i].used) {
			reply(defers[i].req, E_IO, 0, 0, 1);
			defers[i].used = 0;
		}
	mtx_unlock(&S.lock);
	glog("arrêt demandé : %lu lots, %lu blocages\n", (unsigned long)S.submits, (unsigned long)S.hangs);
	return S.dead ? 1 : 0;
}
