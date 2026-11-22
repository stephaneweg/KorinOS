/* nvme - un disque NVMe en service, en C (docs/etude/PC-PILOTES.md, « NVMe »).
 * D'après les spécifications publiques « NVM Express Base Specification » 1.4
 * et « NVM Command Set » : registres de BAR0, file d'administration, IDENTIFY
 * (contrôleur, liste des espaces de noms actifs, espace de noms), une paire de
 * files d'E/S, lecture et écriture par PRP (listes PRP pour plus de deux pages),
 * FLUSH, arrêt (CC.SHN). Rien de propre au PC ici : la fonction PCIe, BAR0 et
 * les interruptions viennent de plat_<arch>.c.
 *
 *	nvme [-r] [-v] [-d] [-p] [-n NOM] [-N NSID] [-c FICHIER] [-w RÈGLE]... [SSSS:BB:DD.F]
 *	  -r  tout en lecture seule (les règles sont lues et dites, sans effet)
 *	  -d  diagnostic : contrôleur et espaces de noms décrits, puis fin (rien de servi)
 *	  -v  chaque commande au journal       -p  sondage (pas d'interruption)
 *	  -n  nom du disque (défaut : le premier nvmeN libre)
 *	  -N  l'espace de noms (défaut : le premier actif)
 *	  -G  essai de la garde : le service blk ouvre toutes les partitions, la garde du
 *	      plus bas niveau doit seule refuser ; accepté seulement sur le disque émulé
 *	      de QEMU (modèle « QEMU NVMe Ctrl », série KORINTEST), ignoré ailleurs
 *	  -c  fichier de règles (défaut : le module « nvme.txt » de l'initrd)
 *	  -w  une règle de plus, comme une ligne du fichier (« + » vaut une espace) :
 *	      -w write=type=basic,label=KORIN+OS   -w name=korin,label=KORIN+OS
 *	nvme stop [NOM]    vide le cache du disque, arrêt propre (CC.SHN), fin du pilote
 *	nvme flush [NOM]   vide le cache du disque (FLUSH)
 *
 * Écritures : refusées partout sauf sur les partitions permises par les règles
 * (policy.c) ; le disque entier est toujours en lecture seule ; chaque écriture
 * est revérifiée au plus bas (policy_write_ok) avant d'aller au contrôleur. */
#include "nvme.h"

/* ---- registres de BAR0 */
#define R_CAP 0x00
#define R_VS 0x08
#define R_CC 0x14
#define R_CSTS 0x1C
#define R_AQA 0x24
#define R_ASQ 0x28
#define R_ACQ 0x30
#define R_DB 0x1000

#define CC_EN 1u
#define CC_SHN_NORMAL (1u << 14)
#define CC_SHN_MASK (3u << 14)
#define CC_IOSQES (6u << 16) /* 64 octets */
#define CC_IOCQES (4u << 20) /* 16 octets */
#define CSTS_RDY 1u
#define CSTS_CFS 2u
#define CSTS_SHST_MASK (3u << 2)
#define CSTS_SHST_DONE (2u << 2)

/* ---- commandes */
#define A_DELETE_SQ 0x00
#define A_CREATE_SQ 0x01
#define A_DELETE_CQ 0x04
#define A_CREATE_CQ 0x05
#define A_IDENTIFY 0x06
#define A_SET_FEATURES 0x09
#define N_FLUSH 0x00
#define N_WRITE 0x01
#define N_READ 0x02
#define FEAT_NUM_QUEUES 0x07

#define AQ_SIZE 32
#define IOQ_MAX 64
#define QD_MAX 16
#define XFER_MAX (1u << 20) /* octets par commande au plus (liste PRP d'une page) */
#define BOUNCE (1u << 20)
#define ADMIN_MS 5000
#define IO_MS 30000
#define PAGE 4096u

#define SIG_STOP BLK_SIG_STOP /* (blkserv.h : signaux du canal du disque) */
#define SIG_FLUSH BLK_SIG_FLUSH

struct sqe {
	u32 cdw0, nsid, rsv2, rsv3;
	u64 mptr, prp1, prp2;
	u32 cdw10, cdw11, cdw12, cdw13, cdw14, cdw15;
};
_Static_assert(sizeof(struct sqe) == 64, "entrée de soumission de 64 octets");
struct cqe {
	u32 dw0, dw1;
	u16 sqhd, sqid, cid, status;
};
_Static_assert(sizeof(struct cqe) == 16, "entrée d'achèvement de 16 octets");

struct queue {
	volatile struct sqe *sq;
	volatile struct cqe *cq;
	u64 sq_pa, cq_pa;
	u16 size, tail, head;
	u16 phase;
	volatile u32 *sq_db, *cq_db;
};

/* une commande en cours */
struct slot {
	u16 cid;
	int busy, done;
	u16 status;
	u32 result;
};

int nv_verbose;
u64 nv_sectors;

static struct nvme_plat plat;
static struct queue aq, ioq;
static struct slot slots[QD_MAX];
static u8 *dma;          /* la zone DMA : files, IDENTIFY, listes PRP, tampon */
static u64 dma_pa;
static u8 *ident;        /* 4 Ko : réponses d'IDENTIFY */
static u64 ident_pa;
static u64 *prp_lists[QD_MAX];
static u64 prp_lists_pa[QD_MAX];
static u8 *bounce;
static u64 bounce_pa;
static u32 dstrd, timeout_ms, nsid, qd;
static u32 max_sect;     /* secteurs de 512 par commande */
static u16 next_cid = 1;
static int vwc, dead, force_poll;
static long irq = -1;
static u32 nv_lock;
static volatile int dirty;
static volatile long last_write_ms;
static u64 n_cmds, n_sleeps, n_errors, n_sectors_rd, n_sectors_wr;
static char ctrl_model[44], ctrl_serial[24], ctrl_fw[12];

/* ---- journal */

static void vlog(const char *fmt, va_list ap)
{
	char b[320];
	int n = snprintf(b, sizeof b, "nvme : ");
	n += vsnprintf(b + n, sizeof b - n, fmt, ap);
	if (n > (int)sizeof b - 1)
		n = sizeof b - 1;
	korin_sys2(SYS_PRINT, b, n);
}

void nv_log(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}

void nv_dbg(const char *fmt, ...)
{
	if (!nv_verbose)
		return;
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}

static long now_ms(void)
{
	return korin_sys0(SYS_TIME_MS);
}

/* une courte attente sur place, sans appel au noyau */
static inline void cpu_relax(void)
{
#if defined(__aarch64__)
	__asm__ volatile("yield" ::: "memory");
#else
	__asm__ volatile("pause" ::: "memory");
#endif
}

static void lock(void)
{
	while (__atomic_exchange_n(&nv_lock, 1, __ATOMIC_ACQUIRE))
		korin_sys0(SYS_YIELD);
}

static void unlock(void)
{
	__atomic_store_n(&nv_lock, 0, __ATOMIC_RELEASE);
}

/* ---- registres (32 bits ; les 64 bits en deux moitiés, la basse d'abord) */

static inline u32 rd32(unsigned r)
{
	return *(volatile u32 *)(plat.regs + r);
}

static inline void wr32(unsigned r, u32 v)
{
	*(volatile u32 *)(plat.regs + r) = v;
}

static u64 rd64(unsigned r)
{
	u32 lo = rd32(r), hi = rd32(r + 4);
	return lo | (u64)hi << 32;
}

static void wr64(unsigned r, u64 v)
{
	wr32(r, (u32)v);
	wr32(r + 4, (u32)(v >> 32));
}

/* attend (CSTS & bits) == want -> 1, 0 au bout de ms */
static int wait_csts(u32 bits, u32 want, long ms)
{
	long end = now_ms() + ms;
	for (int spin = 0;; spin++) {
		u32 s = rd32(R_CSTS);
		if (s != 0xFFFFFFFF && (s & bits) == want)
			return 1;
		if (now_ms() > end)
			return 0;
		if (spin > 100)
			korin_sys1(SYS_SLEEP, 1);
	}
}

/* ---- files */

static void queue_init(struct queue *q, int qid, u16 size, u8 *sq, u64 sq_pa, u8 *cq, u64 cq_pa)
{
	q->sq = (volatile struct sqe *)sq;
	q->cq = (volatile struct cqe *)cq;
	q->sq_pa = sq_pa;
	q->cq_pa = cq_pa;
	q->size = size;
	q->tail = q->head = 0;
	q->phase = 1;
	q->sq_db = (volatile u32 *)(plat.regs + R_DB + (2 * qid) * (4u << dstrd));
	q->cq_db = (volatile u32 *)(plat.regs + R_DB + (2 * qid + 1) * (4u << dstrd));
	memset(sq, 0, size * sizeof(struct sqe));
	memset(cq, 0, size * sizeof(struct cqe));
}

static u16 new_cid(void)
{
	u16 c = next_cid++;
	if (next_cid == 0xFFFF)
		next_cid = 1;
	return c;
}

/* une commande dans la file (sans attendre) ; le cid est mis dans cdw0 */
static void submit(struct queue *q, struct sqe *e, u16 cid)
{
	e->cdw0 = (e->cdw0 & 0xFFFF) | (u32)cid << 16;
	volatile struct sqe *d = &q->sq[q->tail];
	const u32 *s = (const u32 *)e;
	volatile u32 *o = (volatile u32 *)d;
	for (int i = 0; i < 16; i++)
		o[i] = s[i];
	q->tail = (u16)((q->tail + 1) % q->size);
	nv_wmb();
	*q->sq_db = q->tail;
	n_cmds++;
}

/* les achèvements arrivés : rangés dans les places (n) -> nombre lus */
static int reap(struct queue *q, struct slot *sl, int n)
{
	int got = 0;
	for (;;) {
		volatile struct cqe *c = &q->cq[q->head];
		u16 st = c->status;
		if ((st & 1) != q->phase)
			break;
		nv_rmb();
		u16 cid = c->cid;
		u32 res = c->dw0;
		int found = 0;
		for (int i = 0; i < n; i++)
			if (sl[i].busy && !sl[i].done && sl[i].cid == cid) {
				sl[i].done = 1;
				sl[i].status = st >> 1;
				sl[i].result = res;
				found = 1;
				break;
			}
		if (!found)
			nv_dbg("achèvement inattendu (cid %u, état %04x) : ignoré\n", cid, st >> 1);
		q->head = (u16)((q->head + 1) % q->size);
		if (!q->head)
			q->phase ^= 1;
		got++;
	}
	if (got)
		*q->cq_db = q->head;
	return got;
}

/* attend que les n places soient finies -> 0, E_TIMEDOUT. D'abord sur place, sans appel au noyau (la plupart des
 * commandes finissent en quelques dizaines de µs) ; l'heure n'est lue que tous les 16 tours ensuite (22/11/2026 :
 * nvme à 1 500 000 appels/s sur le ZBook pendant un git clone, SYS_TIME_MS à chaque tour) */
#define SPIN_FREE 256 /* tours sur place, 16 pauses chacun */
static int wait_slots(struct queue *q, struct slot *sl, int n, long ms)
{
	long end = 0;
	for (int spin = 0;; spin++) {
		reap(q, sl, n);
		int left = 0;
		for (int i = 0; i < n; i++)
			left += sl[i].busy && !sl[i].done;
		if (!left)
			return 0;
		if (spin < SPIN_FREE) {
			for (int k = 0; k < 16; k++)
				cpu_relax();
			continue;
		}
		if (spin == SPIN_FREE)
			end = now_ms() + ms;
		else if ((spin & 15) == 0 && now_ms() > end) {
			u32 csts = rd32(R_CSTS);
			nv_log("commande sans achèvement après %ld ms (CSTS %08x%s)\n", ms, csts,
			       csts & CSTS_CFS ? " : ERREUR FATALE du contrôleur" : "");
			if (csts == 0xFFFFFFFF || (csts & CSTS_CFS))
				dead = 1;
			return E_TIMEDOUT;
		}
		if (irq >= 0) {
			n_sleeps++;
			korin_sys2(SYS_IRQ_WAIT, irq, 5); /* (court : une MSI arrivée avant l'attente est rendue au délai) */
		} else if (spin < SPIN_FREE + 4000)
			korin_sys0(SYS_YIELD);
		else {
			n_sleeps++;
			korin_sys1(SYS_SLEEP, 1);
		}
	}
}

static const char *status_text(u16 st)
{
	unsigned sct = (st >> 8) & 7, sc = st & 0xFF;
	if (sct == 0)
		switch (sc) {
		case 0x01: return "opcode invalide";
		case 0x02: return "champ invalide";
		case 0x04: return "erreur de transfert";
		case 0x06: return "erreur interne";
		case 0x0B: return "espace de noms invalide";
		case 0x80: return "LBA hors de portée";
		case 0x81: return "capacité dépassée";
		}
	if (sct == 2)
		switch (sc) {
		case 0x80: return "erreur d'écriture du support";
		case 0x81: return "erreur de lecture (illisible)";
		case 0x86: return "accès refusé";
		}
	if (sct == 1 && sc == 0x20)
		return "espace de noms en écriture protégée";
	return "";
}

/* une commande d'administration -> 0 (*res = dword 0), < 0 */
static int admin(struct sqe *e, u32 *res)
{
	struct slot s = {0};
	s.cid = new_cid();
	s.busy = 1;
	submit(&aq, e, s.cid);
	int r = wait_slots(&aq, &s, 1, ADMIN_MS);
	if (r < 0)
		return r;
	if (s.status & 0x7FF) {
		nv_log("administration %02x : état %03x %s\n", e->cdw0 & 0xFF, s.status & 0x7FF, status_text(s.status));
		return E_IO;
	}
	if (res)
		*res = s.result;
	return 0;
}

static int identify(u32 cns, u32 ns)
{
	struct sqe e = {0};
	e.cdw0 = A_IDENTIFY;
	e.nsid = ns;
	e.prp1 = ident_pa;
	e.cdw10 = cns;
	memset(ident, 0, PAGE);
	return admin(&e, 0);
}

/* ---- transferts : d'où viennent les adresses physiques */

struct xsrc {
	u64 base;          /* adresse physique contiguë (pages == 0) */
	const u64 *pages;  /* sinon : pages (adresses physiques), à partir du décalage off0 */
	u64 off0;
};

static u64 xs_phys(const struct xsrc *x, u64 off)
{
	if (x->pages) {
		u64 o = x->off0 + off;
		return x->pages[o >> 12] + (o & (PAGE - 1));
	}
	return x->base + off;
}

/* PRP1, PRP2 d'un morceau (len octets à off) ; liste dans prp_lists[k] si besoin */
static void build_prp(struct sqe *e, const struct xsrc *x, u64 off, u32 len, int k)
{
	u64 p1 = xs_phys(x, off);
	e->prp1 = p1;
	e->prp2 = 0;
	u32 first = PAGE - (u32)(p1 & (PAGE - 1));
	if (len <= first)
		return;
	u32 rest = len - first;
	if (rest <= PAGE) {
		e->prp2 = xs_phys(x, off + first);
		return;
	}
	u64 *l = prp_lists[k];
	u32 n = (rest + PAGE - 1) / PAGE;
	for (u32 i = 0; i < n; i++)
		l[i] = xs_phys(x, off + first + (u64)i * PAGE);
	nv_wmb();
	e->prp2 = prp_lists_pa[k];
}

/* lecture ou écriture de n secteurs à lba, données décrites par x -> 0, < 0
 * (morceaux de max_sect secteurs, qd en vol à la fois) */
static int io(int write, u64 lba, u64 n, const struct xsrc *x)
{
	u64 done = 0;
	while (done < n) {
		int k = 0;
		u64 start = done;
		for (; k < (int)qd && done < n; k++) {
			u32 c = n - done < max_sect ? (u32)(n - done) : max_sect;
			struct sqe e = {0};
			e.cdw0 = write ? N_WRITE : N_READ;
			e.nsid = nsid;
			build_prp(&e, x, done * 512, c * 512, k);
			e.cdw10 = (u32)(lba + done);
			e.cdw11 = (u32)((lba + done) >> 32);
			e.cdw12 = c - 1;
			slots[k].cid = new_cid();
			slots[k].busy = 1;
			slots[k].done = 0;
			slots[k].status = 0;
			nv_dbg("%s %lu +%u (PRP %lx %lx)\n", write ? "écriture" : "lecture", (unsigned long)(lba + done), c,
			       (unsigned long)e.prp1, (unsigned long)e.prp2);
			submit(&ioq, &e, slots[k].cid);
			done += c;
		}
		int r = wait_slots(&ioq, slots, k, IO_MS);
		for (int i = 0; i < k; i++)
			slots[i].busy = 0;
		if (r < 0) {
			n_errors++;
			return E_IO;
		}
		for (int i = 0; i < k; i++)
			if (slots[i].status & 0x7FF) {
				n_errors++;
				nv_log("%s à %lu : état %03x %s\n", write ? "écriture" : "lecture",
				       (unsigned long)(lba + start), slots[i].status & 0x7FF, status_text(slots[i].status));
				return E_IO;
			}
	}
	if (write)
		n_sectors_wr += n;
	else
		n_sectors_rd += n;
	return 0;
}

static int flush_locked(void)
{
	struct slot s = {0};
	struct sqe e = {0};
	e.cdw0 = N_FLUSH;
	e.nsid = nsid;
	s.cid = new_cid();
	s.busy = 1;
	submit(&ioq, &e, s.cid);
	int r = wait_slots(&ioq, &s, 1, IO_MS);
	if (r == 0 && (s.status & 0x7FF)) {
		nv_log("FLUSH : état %03x %s\n", s.status & 0x7FF, status_text(s.status));
		r = E_IO;
	}
	if (r == 0)
		dirty = 0;
	return r;
}

/* lecture ou écriture par le tampon (adresse physique inconnue, mal alignée, ou
 * machine sans DMA cohérent) */
static int io_bounce(int write, u64 lba, u64 n, u8 *buf)
{
	struct xsrc x = {bounce_pa, 0, 0};
	for (u64 i = 0; i < n;) {
		u64 k = n - i < BOUNCE / 512 ? n - i : BOUNCE / 512;
		if (write)
			memcpy(bounce, buf + i * 512, k * 512);
		int r = io(write, lba + i, k, &x);
		if (r < 0)
			return r;
		if (!write)
			memcpy(buf + i * 512, bounce, k * 512);
		i += k;
	}
	return 0;
}

/* le point de passage de toutes les écritures : la garde d'abord */
static long do_rw(int write, u64 lba, u64 n, void *addr, u64 phys)
{
	if (!n)
		return 0;
	if (lba + n > nv_sectors || lba + n < lba)
		return E_INVAL;
	if (write && !policy_write_ok(lba, n)) {
		nv_log("GARDE : écriture des secteurs %lu à %lu refusée (hors des partitions permises)\n",
		       (unsigned long)lba, (unsigned long)(lba + n - 1));
		return E_PERM;
	}
	lock();
	long r;
	if (dead)
		r = E_IO;
	else if (phys && !(phys & 3) && !plat.bounce_only) {
		struct xsrc x = {phys, 0, 0};
		r = io(write, lba, n, &x);
	} else
		r = io_bounce(write, lba, n, addr);
	if (write && r == 0) {
		dirty = 1;
		last_write_ms = now_ms();
	}
	unlock();
	return r;
}

long nv_read_raw(u64 lba, u64 n, void *buf)
{
	if (lba + n > nv_sectors)
		return E_INVAL;
	lock();
	long r = dead ? E_IO : io_bounce(0, lba, n, buf);
	unlock();
	return r;
}

/* ---- le service (blkserv) */

static long b_read(void *ctx, u64 sector, u64 count, void *addr, u64 phys)
{
	(void)ctx;
	return do_rw(0, sector, count, addr, phys);
}

static long b_write(void *ctx, u64 sector, u64 count, const void *addr, u64 phys)
{
	(void)ctx;
	return do_rw(1, sector, count, (void *)addr, phys);
}

/* BLK_READ_D : les pages de l'appelant, droit dans sa mémoire */
static long b_read_sg(void *ctx, u64 sector, u64 count, void *addr, const u64 *phys)
{
	(void)ctx;
	if (sector + count > nv_sectors)
		return E_INVAL;
	lock();
	long r;
	if (dead)
		r = E_IO;
	else if (((u64)addr & 3) || plat.bounce_only)
		r = io_bounce(0, sector, count, addr);
	else {
		struct xsrc x = {0, phys, (u64)addr & (PAGE - 1)};
		r = io(0, sector, count, &x);
	}
	unlock();
	return r;
}

/* stats : sommeils ; a commandes, b secteurs lus, c secteurs écrits, d erreurs ; remis à zéro */
static long b_stats(void *ctx, long busy_us, struct msg *m)
{
	(void)ctx;
	(void)busy_us;
	long sleeps = (long)n_sleeps;
	m->a = (i64)n_cmds;
	m->b = (i64)n_sectors_rd;
	m->c = (i64)n_sectors_wr;
	m->d = (i64)n_errors;
	n_cmds = n_sleeps = n_sectors_rd = n_sectors_wr = n_errors = 0;
	return sleeps;
}

/* arrêt propre : cache vidé, files d'E/S rendues, CC.SHN, attente de SHST */
static void shutdown_locked(void)
{
	if (dead)
		return;
	int f = flush_locked();
	struct sqe e = {0};
	e.cdw0 = A_DELETE_SQ;
	e.cdw10 = 1;
	admin(&e, 0);
	memset(&e, 0, sizeof e);
	e.cdw0 = A_DELETE_CQ;
	e.cdw10 = 1;
	admin(&e, 0);
	wr32(R_CC, (rd32(R_CC) & ~CC_SHN_MASK) | CC_SHN_NORMAL);
	long t = now_ms();
	int ok = wait_csts(CSTS_SHST_MASK, CSTS_SHST_DONE, 10000);
	nv_log("arrêt %s en %ld ms (cache vidé : %s, CSTS %08x)\n", ok ? "propre" : "PAS confirmé", now_ms() - t,
	       f == 0 ? "oui" : "non", rd32(R_CSTS));
	dead = 1;
}

/* BLK_SIG_STOP (« nvme stop ») : la couche finit ensuite le pilote */
static long b_stop(void *ctx)
{
	(void)ctx;
	nv_log("arrêt demandé\n");
	lock();
	shutdown_locked();
	unlock();
	return 0;
}

/* BLK_SIG_FLUSH (« nvme flush ») */
static long b_flush(void *ctx)
{
	(void)ctx;
	lock();
	int r = dead ? E_IO : flush_locked();
	unlock();
	nv_log("cache vidé à la demande (%d)\n", r);
	return r;
}

static void b_table(void *ctx, struct blk_part *t, int n, const struct blk_gpt *g)
{
	(void)ctx;
	policy_table(t, n, g);
}

static long b_plan(void *ctx, struct msg *m)
{
	(void)ctx;
	return policy_plan(m);
}

static void b_denied(void *ctx, const char *dev, u64 sector, u64 count, u32 pid)
{
	(void)ctx;
	policy_denied(dev, sector, count, pid);
}

/* (thread) le cache d'écriture du disque vidé une seconde après la dernière écriture :
 * le PC n'a pas encore d'extinction qui préviendrait le pilote */
static THREAD_ENTRY void flusher(long arg)
{
	(void)arg;
	for (;;) {
		korin_sys1(SYS_SLEEP, 250);
		if (!dirty || now_ms() - last_write_ms < 1000)
			continue;
		lock();
		if (dirty && !dead) {
			int r = flush_locked();
			if (r < 0)
				nv_log("vidage du cache : %d\n", r);
		}
		unlock();
	}
}

/* ---- démarrage du contrôleur */

static void copy_str(char *d, const u8 *s, int n)
{
	memcpy(d, s, n);
	d[n] = 0;
	for (int i = n - 1; i >= 0 && (d[i] == ' ' || !d[i]); i--)
		d[i] = 0;
}

static int controller_start(void)
{
	u64 cap = rd64(R_CAP);
	u32 vs = rd32(R_VS);
	if ((u32)cap == 0xFFFFFFFF) {
		nv_log("%s : registres illisibles (ffffffff)\n", plat.where);
		return E_IO;
	}
	u32 mqes = (u32)(cap & 0xFFFF) + 1;
	dstrd = (u32)(cap >> 32) & 0xF;
	timeout_ms = (u32)((cap >> 24) & 0xFF) * 500;
	if (timeout_ms < 500)
		timeout_ms = 500;
	unsigned css = (cap >> 37) & 0xFF, mpsmin = (cap >> 48) & 0xF;
	nv_log("%s : NVMe %u.%u.%u, CAP %08x%08x (files de %u au plus, délai %u ms, pas %u)\n", plat.where, vs >> 16,
	       (vs >> 8) & 0xFF, vs & 0xFF, (u32)(cap >> 32), (u32)cap, mqes, timeout_ms, 4u << dstrd);
	if (!(css & 1)) {
		nv_log("pas de jeu de commandes NVM (CSS %02x)\n", css);
		return E_NODEV;
	}
	if (mpsmin > 0) {
		nv_log("pages de %u Ko au moins : pas géré (pages de 4 Ko)\n", 4u << mpsmin);
		return E_NODEV;
	}
	if (R_DB + 4 * (4u << dstrd) > plat.regs_size) {
		nv_log("BAR0 trop petite pour les sonnettes\n");
		return E_NODEV;
	}
	/* remise à zéro : le micrologiciel (pilote NVMe de l'UEFI) a pu le laisser en marche */
	u32 cc = rd32(R_CC), csts = rd32(R_CSTS);
	if (cc & CC_EN) {
		if (!(csts & CSTS_RDY) && !(csts & CSTS_CFS))
			wait_csts(CSTS_RDY, CSTS_RDY, timeout_ms);
		nv_log("contrôleur laissé en marche (CC %08x, CSTS %08x) : remis à zéro\n", cc, csts);
		wr32(R_CC, cc & ~CC_EN);
	}
	if (!wait_csts(CSTS_RDY, 0, timeout_ms)) {
		nv_log("remise à zéro : RDY reste à 1 (CSTS %08x)\n", rd32(R_CSTS));
		return E_IO;
	}
	u16 iosz = mqes < IOQ_MAX ? (u16)mqes : IOQ_MAX;
	u16 asz = mqes < AQ_SIZE ? (u16)mqes : AQ_SIZE;
	/* zone DMA : 0 file d'admin. (SQ), 1 (CQ), 2 SQ d'E/S, 3 CQ d'E/S, 4 IDENTIFY,
	 * 5.. listes PRP, puis le tampon */
	queue_init(&aq, 0, asz, dma, dma_pa, dma + PAGE, dma_pa + PAGE);
	ident = dma + 4 * PAGE;
	ident_pa = dma_pa + 4 * PAGE;
	for (int i = 0; i < QD_MAX; i++) {
		prp_lists[i] = (u64 *)(dma + (5 + i) * PAGE);
		prp_lists_pa[i] = dma_pa + (5 + i) * PAGE;
	}
	wr32(R_AQA, (u32)(asz - 1) << 16 | (asz - 1));
	wr64(R_ASQ, aq.sq_pa);
	wr64(R_ACQ, aq.cq_pa);
	/* (INTMS laissé à zéro : INTx est coupé par pcid, une MSI n'est pas masquée ici, et la
	 * spécification interdit de toucher INTMS en MSI-X) */
	wr32(R_CC, CC_IOSQES | CC_IOCQES | CC_EN); /* NVM, pages de 4 Ko, tourniquet */
	if (!wait_csts(CSTS_RDY, CSTS_RDY, timeout_ms) || (rd32(R_CSTS) & CSTS_CFS)) {
		nv_log("mise en marche refusée (CSTS %08x)\n", rd32(R_CSTS));
		return E_IO;
	}
	/* le contrôleur */
	if (identify(1, 0) < 0)
		return E_IO;
	copy_str(ctrl_serial, ident + 4, 20);
	copy_str(ctrl_model, ident + 24, 40);
	copy_str(ctrl_fw, ident + 64, 8);
	unsigned mdts = ident[77];
	u32 nn = ident[516] | ident[517] << 8 | ident[518] << 16 | (u32)ident[519] << 24;
	vwc = ident[525] & 1;
	u64 mx = mdts ? (u64)PAGE << mdts : XFER_MAX;
	if (mx > XFER_MAX)
		mx = XFER_MAX;
	max_sect = (u32)(mx / 512);
	nv_log("« %s », série « %s », micrologiciel « %s », %u espace(s) de noms, transfert de %lu Ko au plus%s\n",
	       ctrl_model, ctrl_serial, ctrl_fw, nn, (unsigned long)(mx >> 10), vwc ? ", cache d'écriture" : "");
	/* une file d'E/S (une par cœur : plus tard) */
	struct sqe e = {0};
	u32 res = 0;
	e.cdw0 = A_SET_FEATURES;
	e.cdw10 = FEAT_NUM_QUEUES;
	e.cdw11 = 0; /* 1 SQ, 1 CQ */
	if (admin(&e, &res) < 0)
		return E_IO;
	irq = force_poll ? -1 : nvme_plat_irq(&plat);
	queue_init(&ioq, 1, iosz, dma + 2 * PAGE, dma_pa + 2 * PAGE, dma + 3 * PAGE, dma_pa + 3 * PAGE);
	memset(&e, 0, sizeof e);
	e.cdw0 = A_CREATE_CQ;
	e.prp1 = ioq.cq_pa;
	e.cdw10 = (u32)(iosz - 1) << 16 | 1;
	e.cdw11 = (irq >= 0 ? 2u : 0u) | 1u; /* vecteur 0, interruption si MSI, contiguë */
	if (admin(&e, 0) < 0)
		return E_IO;
	memset(&e, 0, sizeof e);
	e.cdw0 = A_CREATE_SQ;
	e.prp1 = ioq.sq_pa;
	e.cdw10 = (u32)(iosz - 1) << 16 | 1;
	e.cdw11 = 1u << 16 | 1u; /* CQ 1, contiguë */
	if (admin(&e, 0) < 0)
		return E_IO;
	qd = iosz - 2 < QD_MAX ? iosz - 2u : QD_MAX;
	nv_log("file d'E/S : %u entrées, %u commandes en vol, %s\n", iosz, qd,
	       irq >= 0 ? "interruption" : "sondage");
	return (int)nn;
}

/* l'espace de noms : want (0 : le premier actif) -> 0, < 0 */
static int namespace_start(u32 want, u32 nn)
{
	u32 list[64];
	int n = 0;
	if (want)
		list[n++] = want;
	else if (identify(2, 0) == 0) { /* liste des actifs (1.1 et plus) */
		for (int i = 0; i < 64; i++) {
			u32 id = ident[4 * i] | ident[4 * i + 1] << 8 | ident[4 * i + 2] << 16 | (u32)ident[4 * i + 3] << 24;
			if (!id)
				break;
			list[n++] = id;
		}
	} else
		for (u32 i = 1; i <= nn && n < 64; i++)
			list[n++] = i;
	for (int i = 0; i < n; i++) {
		if (identify(0, list[i]) < 0)
			continue;
		u64 nsze = 0;
		for (int k = 7; k >= 0; k--)
			nsze = nsze << 8 | ident[k];
		if (!nsze)
			continue;
		unsigned nlbaf = ident[25] + 1u, fl = ident[26] & 0xF;
		const u8 *f = ident + 128 + 4 * fl;
		unsigned ms = f[0] | f[1] << 8, lbads = f[2];
		char fmts[96];
		int o = 0;
		for (unsigned k = 0; k < nlbaf && k < 16 && o < 80; k++)
			o += snprintf(fmts + o, sizeof fmts - o, "%s%u%s", k ? ", " : "", 1u << ident[128 + 4 * k + 2],
			              k == fl ? " (choisi)" : "");
		nv_log("espace de noms %u : %lu blocs de %u octets (%lu Mo), formats : %s\n", list[i],
		       (unsigned long)nsze, 1u << lbads, (unsigned long)((nsze << lbads) >> 20), fmts);
		if (lbads != 9 || ms) {
			nv_log("espace de noms %u : blocs de %u octets%s : pas géré (seulement 512, sans métadonnées)\n",
			       list[i], 1u << lbads, ms ? " avec métadonnées" : "");
			continue;
		}
		nsid = list[i];
		nv_sectors = nsze;
		return 0;
	}
	nv_log("aucun espace de noms servable\n");
	return E_NODEV;
}

/* nvme stop | flush [NOM] : SVC_SIGNAL sur le canal du disque */
static int send_signal(const char *name, u64 bits)
{
	char n[32];
	snprintf(n, sizeof n, "blk.%s", name ? name : "nvme0");
	long c = korin_sys2(SYS_SVC_OPEN, n, 1000);
	if (c < 0) {
		nv_log("%s absent (%ld)\n", n, c);
		return 1;
	}
	long r = korin_sys2(SYS_SVC_SIGNAL, c, bits);
	nv_log("%s : %s demandé (%ld)\n", n, bits & SIG_STOP ? "arrêt" : "vidage du cache", r);
	return r < 0;
}

static char rules_file[8192];

static void load_rules(const char *path)
{
	long n;
	if (path) {
		long fd = sys_open(path, O_READ);
		if (fd < 0) {
			nv_log("%s illisible (%ld) : aucune règle d'écriture\n", path, fd);
			return;
		}
		n = sys_read(fd, rules_file, sizeof rules_file - 1);
		sys_close(fd);
	} else {
		path = "nvme.txt (local.tar ou initrd)";
		n = korin_sys3(SYS_MOD_READ, "nvme.txt", rules_file, sizeof rules_file - 1);
	}
	if (n < 0) {
		nv_log("pas de %s : aucune règle d'écriture\n", path);
		return;
	}
	rules_file[n] = 0;
	policy_text(rules_file, path);
}

static u8 stack_flusher[16384] __attribute__((aligned(16)));

int main(int argc, char **argv)
{
	const char *addr = 0, *name = 0, *conf = 0;
	int diag = 0, guard_test = 0;
	u32 want_ns = 0;
	if (argc > 1 && (!strcmp(argv[1], "stop") || !strcmp(argv[1], "flush")))
		return send_signal(argc > 2 ? argv[2] : 0, !strcmp(argv[1], "stop") ? SIG_STOP : SIG_FLUSH);
	static char cli_rules[16][128];
	int ncli = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v"))
			nv_verbose = 1;
		else if (!strcmp(argv[i], "-r"))
			policy_all_ro = 1;
		else if (!strcmp(argv[i], "-d"))
			diag = 1;
		else if (!strcmp(argv[i], "-p"))
			force_poll = 1;
		else if (!strcmp(argv[i], "-G"))
			guard_test = 1;
		else if (!strcmp(argv[i], "-n") && i + 1 < argc)
			name = argv[++i];
		else if (!strcmp(argv[i], "-N") && i + 1 < argc)
			want_ns = (u32)parse_dec(argv[++i]);
		else if (!strcmp(argv[i], "-c") && i + 1 < argc)
			conf = argv[++i];
		else if (!strcmp(argv[i], "-w") && i + 1 < argc) {
			if (ncli < 16) {
				char *d = cli_rules[ncli++];
				const char *s = argv[++i];
				int k = 0;
				for (; s[k] && k < 127; k++) /* « write=… » -> « write = … » ; « + » : espace */
					d[k] = s[k] == '+' ? ' ' : s[k];
				d[k] = 0;
				char *eq = strchr(d, '=');
				if (eq) { /* (la clé avant le premier « = ») */
					*eq = 0;
					char tmp[128];
					snprintf(tmp, sizeof tmp, "%s = %s", d, eq + 1);
					strlcpy(d, tmp, 128);
				}
			} else
				i++;
		} else
			addr = argv[i];
	}
	char nm[16];
	int r = nvme_plat_init(addr, &plat);
	if (r == E_BUSY) {
		nv_log("contrôleur déjà servi par un autre pilote : fin\n");
		return 0;
	}
	if (r < 0)
		return 1;
	long ph = 0;
	long va = korin_sys2(SYS_DMA_ALLOC, (5 + QD_MAX) * PAGE + BOUNCE, &ph);
	if (va < 0) {
		nv_log("pas de mémoire DMA (%ld)\n", va);
		return 1;
	}
	dma = (u8 *)va;
	dma_pa = (u64)ph;
	bounce = dma + (5 + QD_MAX) * PAGE;
	bounce_pa = dma_pa + (5 + QD_MAX) * PAGE;
	int nn = controller_start();
	if (nn < 0)
		return 1;
	if (namespace_start(want_ns, (u32)nn) < 0)
		return 1;
	if (guard_test) {
		if (!strcmp(ctrl_model, "QEMU NVMe Ctrl") && !strcmp(ctrl_serial, "KORINTEST"))
			policy_guard_test = 1;
		else
			nv_log("-G ignoré : seulement sur le disque d'essai de QEMU (série KORINTEST)\n");
	}
	if (diag) {
		nv_log("diagnostic fini (rien de servi, rien d'écrit)\n");
		return 0;
	}
	if (name)
		snprintf(nm, sizeof nm, "%s", name);
	else
		blk_free_name("nvme", nm, sizeof nm); /* un nom libre « nvmeN » */
	policy_disk = nm;
	/* les règles : le fichier, puis la ligne de commande */
	load_rules(conf);
	for (int i = 0; i < ncli; i++)
		policy_line(cli_rules[i], "-w");
	static struct blk_driver d; /* le pilote (src/svc/common/blkserv.h) */
	d.version = BLK_DRIVER_V2;
	d.plan = b_plan;
	d.name = nm;
	d.sectors = nv_sectors;
	d.read_only = 1; /* (le disque entier ; les partitions : policy_table) */
	d.read = b_read;
	d.write = b_write;
	d.flush = b_flush;
	d.read_sg = b_read_sg;
	d.stats = b_stats;
	d.stop = b_stop;
	d.table = b_table;
	d.denied = b_denied;
	if (vwc)
		korin_sys3(SYS_THREAD_CREATE, flusher, stack_flusher + sizeof stack_flusher, 0);
	nv_log("service blk.%s (disque entier en lecture seule%s)\n", nm,
	       policy_all_ro ? ", -r : tout en lecture seule" : "");
	blk_serve(&d); /* (ne revient pas) */
}
