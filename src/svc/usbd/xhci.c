/* xhci.c : contrôleur xHCI, commun au Pi (VL805 du Pi 4, RP1 du Pi 5) et au
 * PC (xHCI Intel du ZBook, qemu-xhci), réécrit d'après src/svc/usbd/xhci.inc.
 *
 *  * registres par la couche de la machine (plat_<arch>.c : BAR mappée,
 *    décalage DMA) ; une zone DMA de 2 Mo (anneaux, contextes, tampons) ;
 *  * anneaux : commandes, événements (interrupteur 0), un anneau de transfert
 *    par (slot, DCI) ; un TRB de lien en fin d'anneau ;
 *  * énumération des ports racines et des hubs USB 2 (route, TT), arbre des
 *    slots ; branchements et retraits suivis par PORTSC et GET_STATUS ;
 *  * transferts de contrôle, interruption (deux tampons par tuyau, lecture
 *    non bloquante), bulk (par un tampon de 64 Ko de la zone), bulk IN sans
 *    attente (usbnet) ;
 *  * interruptions : MSI-X sur PC (l'interrupteur 0 lève la ligne liée au
 *    canal du service) ; sondage sinon (Pi), comme l'asm.
 * Pas encore : isochrone (iso.inc), hubs SuperSpeed. */
#include "usbd.h"

#define XH_MAX_SLOTS 32
#define XH_RING_TRBS 64 /* dernier TRB = lien */
#define XH_CMD_TRBS 256
#define XH_EVT_TRBS 256
#define XH_DELAY_CMD 1000
#define XH_DELAY_CTRL 500
#define XH_DELAY_BULK 5000
#define XH_PIPES 16
#define XH_PIPE_MPS 512
#define XH_SCAN_MS 200
#define XH_DEPTH_MAX 4

/* zone DMA (2 Mo) */
#define Z_SIZE 0x200000
#define Z_DCBAA 0x0000  /* (MaxSlotsEn + 1) x 8 */
#define Z_ERST 0x0800
#define Z_CMD 0x1000    /* 256 TRB */
#define Z_EVT 0x2000    /* 256 TRB */
#define Z_INCTX 0x3000  /* contexte d'entrée (33 x 64 o au plus) */
#define Z_CBUF 0x4000   /* transferts de contrôle (4 Ko) */
#define Z_REP 0x5000    /* rapports : XH_PIPES x 2 x 512 o */
#define Z_NB 0xD000     /* bulk sans attente (12 Ko) */
#define Z_NB_SIZE 0x3000
#define Z_BULK 0x10000  /* bulk (64 Ko, aligné : un TRB ne franchit pas 64 Ko) */
#define Z_DEV 0x20000   /* par slot : contexte (2 Ko) puis l'anneau du DCI d à 0x800 + (d - 1) Ko */
#define Z_DEV_SIZE 0x9000

/* registres */
#define OP_USBCMD 0x00
#define OP_USBSTS 0x04
#define OP_PAGESIZE 0x08
#define OP_CRCR 0x18
#define OP_DCBAAP 0x30
#define OP_CONFIG 0x38
#define OP_PORTSC 0x400
#define IR_IMAN 0x00
#define IR_IMOD 0x04
#define IR_ERSTSZ 0x08
#define IR_ERSTBA 0x10
#define IR_ERDP 0x18
#define PORTSC_KEEP 0x4E00FFE9u /* RO + RWS : réécrits tels quels (jamais PED ni PR) */
#define PORTSC_CHG 0x00FE0000u  /* changements (RW1C) */

/* TRB */
#define TRB_NORMAL 1
#define TRB_SETUP 2
#define TRB_DATA 3
#define TRB_STATUS 4
#define TRB_LINK 6
#define TRB_ENABLE_SLOT 9
#define TRB_DISABLE_SLOT 10
#define TRB_ADDR_DEV 11
#define TRB_CONFIG_EP 12
#define TRB_EVAL_CTX 13
#define TRB_RESET_EP 14
#define TRB_STOP_EP 15
#define TRB_SET_DEQ 16
#define TRB_NOOP 23
#define TRB_EV_XFER 32
#define TRB_EV_CMD 33
#define TRB_EV_PORT 34
#define TRB_IOC (1u << 5)
#define TRB_ISP (1u << 2)
#define TRB_IDT (1u << 6)
#define CC_SUCCESS 1
#define CC_STALL 6
#define CC_SHORT 13
#define CC_STOPPED 26
#define CC_STOPPED2 27

/* vitesses (identifiants PSI par défaut ; usbd : vitesse - 1) */
#define S_FULL 1
#define S_LOW 2
#define S_HIGH 3
#define S_SUPER 4

struct ring {
	uint8_t *base; /* 0 : pas d'anneau */
	unsigned enq, cyc, n;
	int ready; /* extrémité configurée */
};

struct xdev {
	unsigned route, speed, rport, tt_slot, tt_port, mps0, depth, maxdci;
	int active;  /* adressé, dans l'arbre */
	int parent;  /* slot du hub parent (0 : port racine) */
	int port;    /* port racine (profondeur 0) ou du hub */
	long usbd;   /* numéro usbd (0 : hub, ou pas confié) */
	int hub;     /* hub déclaré : ports scrutés */
	unsigned nports;
};

struct pipe {
	int slot; /* 0 : jamais servi */
	unsigned dci, mps;
	int open, armed;
	int ready; /* 0 rien, 1 données, 2 erreur */
	unsigned len, code, errs;
	int dead, side;
};

static struct xh_plat plat;
static volatile uint8_t *cap, *op, *rt_ir0;
static volatile uint32_t *db;
static uint8_t *zone;
static uint64_t zone_phys, zone_dev; /* adresse vue du contrôleur */
static unsigned nports, nslots, ctx;
static struct ring cmd_ring;
static unsigned evt_deq, evt_cyc;
static struct xdev devs[XH_MAX_SLOTS + 1];
static struct ring rings[XH_MAX_SLOTS + 1][32];
static struct pipe pipes[XH_PIPES];
static long t_scan;
static int scan_now;
static long irq_line = -1;
static uint8_t desc_copy[XH_DEPTH_MAX][32];
static uint8_t cfg_copy[XH_DEPTH_MAX][CFG_MAX];

/* bulk sans attente : une seule lecture en cours (comme le DWC2 de l'asm) */
static struct {
	int armed, done, slot;
	unsigned dci, len, code, left;
} nb;

static uint32_t evt[4]; /* dernier événement pris */

/* ---- accès */

static uint32_t r32(volatile uint8_t *b, unsigned off)
{
	return *(volatile uint32_t *)(b + off);
}

static void w32(volatile uint8_t *b, unsigned off, uint32_t v)
{
	*(volatile uint32_t *)(b + off) = v;
}

static void w64(volatile uint8_t *b, unsigned off, uint64_t v)
{
	w32(b, off, (uint32_t)v);
	w32(b, off + 4, (uint32_t)(v >> 32));
}

static uint64_t dev_addr(const void *p)
{
	return zone_dev + (uint64_t)((const uint8_t *)p - zone);
}

static long now_ms(void)
{
	return korin_sys0(SYS_TIME_MS);
}

static void sleep_ms(long ms)
{
	korin_sys1(SYS_SLEEP, ms);
}

/* registre opérationnel : attendre que (valeur & masque) != 0 vaille want */
static int wait_op(unsigned reg, uint32_t mask, int want, long ms)
{
	for (long i = 0; i <= ms; i++) {
		if (((r32(op, reg) & mask) != 0) == want)
			return 1;
		sleep_ms(1);
	}
	return 0;
}

/* ---- anneaux */

static void ring_init(struct ring *r, uint8_t *mem, unsigned n)
{
	memset(mem, 0, n * 16);
	uint32_t *link = (uint32_t *)(mem + (n - 1) * 16);
	uint64_t a = dev_addr(mem);
	link[0] = (uint32_t)a;
	link[1] = (uint32_t)(a >> 32);
	link[2] = 0;
	link[3] = TRB_LINK << 10 | 2; /* TC : le cycle bascule */
	r->base = mem;
	r->enq = 0;
	r->cyc = 1;
	r->n = n;
	r->ready = 0;
	dma_mb();
}

/* -> le TRB écrit */
static uint32_t *ring_push(struct ring *r, uint64_t param, uint32_t status, uint32_t control)
{
	uint32_t *t = (uint32_t *)(r->base + r->enq * 16);
	t[0] = (uint32_t)param;
	t[1] = (uint32_t)(param >> 32);
	t[2] = status;
	dma_wmb(); /* paramètre et statut visibles avant le cycle */
	t[3] = control | r->cyc;
	if (++r->enq == r->n - 1) {
		uint32_t *l = (uint32_t *)(r->base + (r->n - 1) * 16);
		dma_wmb();
		l[3] = (l[3] & ~1u) | r->cyc; /* le lien prend le cycle courant */
		r->cyc ^= 1;
		r->enq = 0;
	}
	return t;
}

static void ring_bell(unsigned slot, unsigned target)
{
	dma_mb();
	db[slot] = target;
}

static struct ring *ring_of(int slot, unsigned dci)
{
	return &rings[slot][dci & 31];
}

/* ---- événements */

/* -> 1 si un événement a été pris (dans evt), l'anneau avance */
static int evt_take(void)
{
	volatile uint32_t *t = (volatile uint32_t *)(zone + Z_EVT + evt_deq * 16);
	if ((t[3] & 1) != evt_cyc)
		return 0;
	dma_rmb(); /* le reste du TRB après le cycle */
	evt[0] = t[0];
	evt[1] = t[1];
	evt[2] = t[2];
	evt[3] = t[3];
	if (++evt_deq == XH_EVT_TRBS) {
		evt_deq = 0;
		evt_cyc ^= 1;
	}
	w64(rt_ir0, IR_ERDP, dev_addr(zone + Z_EVT + evt_deq * 16) | 8); /* EHB acquitté */
	return 1;
}

static void pipe_event(int slot, unsigned dci, unsigned code, unsigned left)
{
	if (code == CC_STOPPED || code == CC_STOPPED2)
		return;
	if (nb.armed && !nb.done && nb.slot == slot && nb.dci == dci) {
		nb.done = 1;
		nb.code = code;
		nb.left = left;
		return;
	}
	for (int i = 0; i < XH_PIPES; i++) {
		struct pipe *p = &pipes[i];
		if (!p->armed || p->slot != slot || p->dci != dci)
			continue;
		p->armed = 0;
		p->code = code;
		if (code == CC_SUCCESS || code == CC_SHORT) {
			p->len = left > p->mps ? 0 : p->mps - left;
			p->ready = 1;
		} else
			p->ready = 2;
		return;
	}
}

/* un événement pris qui n'était pas attendu : transferts des tuyaux ;
 * changements de port (relus par la scrutation) */
static void dispatch(void)
{
	unsigned type = (evt[3] >> 10) & 63;
	if (type == TRB_EV_PORT) {
		scan_now = 1;
		return;
	}
	if (type != TRB_EV_XFER)
		return;
	int slot = (int)(evt[3] >> 24);
	if (slot < 1 || slot > XH_MAX_SLOTS || !devs[slot].active)
		return;
	pipe_event(slot, (evt[3] >> 16) & 31, evt[2] >> 24, evt[2] & 0xFFFFFF);
}

/* kind 0 : commande ; 1 : transfert de (slot, dci). -> code (-1 : délai),
 * *res = slot (commande) ou octets non transférés, *trb = TRB concerné */
static int xh_wait(int kind, int slot, unsigned dci, long ms, unsigned *res, uint64_t *trb)
{
	long end = now_ms() + ms;
	int spins = 0;
	for (;;) {
		if (evt_take()) {
			unsigned type = (evt[3] >> 10) & 63;
			if (kind == 0 && type == TRB_EV_CMD) {
				*res = evt[3] >> 24;
				if (trb)
					*trb = evt[0] | (uint64_t)evt[1] << 32;
				return (int)(evt[2] >> 24);
			}
			if (kind == 1 && type == TRB_EV_XFER && (int)(evt[3] >> 24) == slot &&
			    ((evt[3] >> 16) & 31) == dci) {
				*res = evt[2] & 0xFFFFFF;
				if (trb)
					*trb = evt[0] | (uint64_t)evt[1] << 32;
				return (int)(evt[2] >> 24);
			}
			dispatch();
			continue;
		}
		if (now_ms() >= end)
			return -1;
		if (spins++ < 20)
			korin_sys0(SYS_YIELD); /* (la plupart des commandes finissent vite) */
		else
			sleep_ms(1);
	}
}

static int xh_cmd(uint64_t param, uint32_t status, uint32_t control, unsigned *slot_out)
{
	unsigned s = 0;
	ring_push(&cmd_ring, param, status, control);
	ring_bell(0, 0);
	int cc = xh_wait(0, 0, 0, XH_DELAY_CMD, &s, 0);
	if (slot_out)
		*slot_out = s;
	return cc;
}

static long cc_errno(int cc)
{
	return cc == -1 ? E_TIMEDOUT : cc == CC_STALL ? -32 /* E_PIPE */ : E_IO;
}

/* remet l'extrémité en état après un échec (Reset Endpoint si arrêtée sur
 * erreur, sinon Stop Endpoint), puis Set TR Dequeue sur le producteur */
static void xh_repair(int slot, unsigned dci, int cc)
{
	uint32_t sd = (uint32_t)slot << 24 | dci << 16;
	int ok = 0;
	if (cc != -1)
		ok = xh_cmd(0, 0, TRB_RESET_EP << 10 | sd, 0) == CC_SUCCESS;
	if (!ok)
		xh_cmd(0, 0, TRB_STOP_EP << 10 | sd, 0);
	struct ring *r = ring_of(slot, dci);
	xh_cmd(dev_addr(r->base + r->enq * 16) | r->cyc, 0, TRB_SET_DEQ << 10 | sd, 0);
}

/* ---- périphériques et contextes */

static uint8_t *slot_area(int slot)
{
	return zone + Z_DEV + (unsigned)(slot - 1) * Z_DEV_SIZE;
}

static int slot_ok(int slot)
{
	return slot >= 1 && slot <= XH_MAX_SLOTS && devs[slot].active;
}

static uint32_t *inctx_clear(void)
{
	memset(zone + Z_INCTX, 0, 0x1000);
	return (uint32_t *)(zone + Z_INCTX);
}

static uint32_t *ictx(uint32_t *in, unsigned i) /* contexte d'entrée n° i (0 : contrôle) */
{
	return (uint32_t *)((uint8_t *)in + i * ctx);
}

static void copy_slot_ctx(int slot, uint32_t *in)
{
	memcpy(ictx(in, 1), slot_area(slot), 16);
}

/* -> slot adressé, 0 si échec */
static int new_device(unsigned rport, unsigned route, unsigned speed, unsigned tt_slot, unsigned tt_port,
		      unsigned depth)
{
	unsigned slot;
	if (xh_cmd(0, 0, TRB_ENABLE_SLOT << 10, &slot) != CC_SUCCESS || !slot)
		return 0;
	if (slot > XH_MAX_SLOTS) {
		xh_cmd(0, 0, TRB_DISABLE_SLOT << 10 | slot << 24, 0);
		return 0;
	}
	struct xdev *d = &devs[slot];
	memset(d, 0, sizeof *d);
	d->route = route;
	d->speed = speed;
	d->rport = rport;
	d->tt_slot = tt_slot;
	d->tt_port = tt_port;
	d->depth = depth;
	d->maxdci = 1;
	d->mps0 = speed == S_HIGH ? 64 : speed >= S_SUPER ? 512 : 8;
	memset(rings[slot], 0, sizeof rings[slot]);
	uint8_t *area = slot_area((int)slot);
	memset(area, 0, Z_DEV_SIZE);
	((uint64_t *)(zone + Z_DCBAA))[slot] = dev_addr(area);
	struct ring *r0 = ring_of((int)slot, 1);
	ring_init(r0, area + 0x800, XH_RING_TRBS);
	uint32_t *in = inctx_clear();
	in[1] = 3; /* A0 | A1 */
	uint32_t *sc = ictx(in, 1);
	sc[0] = route | speed << 20 | 1u << 27; /* Context Entries = 1 */
	sc[1] = rport << 16;
	sc[2] = tt_slot | tt_port << 8;
	uint32_t *ep0 = ictx(in, 2);
	ep0[1] = d->mps0 << 16 | 4 << 3 | 3 << 1; /* contrôle, CErr = 3 */
	uint64_t deq = dev_addr(r0->base) | 1;
	ep0[2] = (uint32_t)deq;
	ep0[3] = (uint32_t)(deq >> 32);
	ep0[4] = 8; /* longueur moyenne de TRB */
	dma_mb();
	int cc = xh_cmd(dev_addr(in), 0, TRB_ADDR_DEV << 10 | slot << 24, 0);
	if (cc != CC_SUCCESS) {
		xh_cmd(0, 0, TRB_DISABLE_SLOT << 10 | slot << 24, 0);
		((uint64_t *)(zone + Z_DCBAA))[slot] = 0;
		say("usbd : xhci : Address Device refusé, code %d\n", cc);
		return 0;
	}
	d->active = 1;
	sleep_ms(20);
	return (int)slot;
}

/* ---- transferts de contrôle (EP0) : données dans Z_CBUF */

static long ctrl(int slot, uint64_t setup)
{
	struct ring *r = ring_of(slot, 1);
	unsigned len = (unsigned)(setup >> 48);
	int in = (setup & 0x80) != 0;
	uint32_t c = TRB_SETUP << 10 | TRB_IDT;
	if (len)
		c |= (in ? 3u : 2u) << 16; /* TRT */
	ring_push(r, setup, 8, c);
	uint64_t data_trb = 0, status_trb;
	if (len) {
		uint32_t *t = ring_push(r, dev_addr(zone + Z_CBUF), len,
					TRB_DATA << 10 | (in ? (1u << 16) | TRB_ISP : 0));
		data_trb = dev_addr(t);
	}
	uint32_t sc = TRB_STATUS << 10 | TRB_IOC;
	if (!len || !in)
		sc |= 1u << 16; /* statut IN (pas de données, ou données OUT) */
	status_trb = dev_addr(ring_push(r, 0, 0, sc));
	ring_bell((unsigned)slot, 1);
	unsigned left = 0;
	for (;;) {
		unsigned res;
		uint64_t trb;
		int cc = xh_wait(1, slot, 1, XH_DELAY_CTRL, &res, &trb);
		if (cc != CC_SUCCESS && cc != CC_SHORT) {
			xh_repair(slot, 1, cc);
			return cc_errno(cc);
		}
		if (trb == status_trb)
			break;
		if (trb == data_trb)
			left = res; /* données courtes : on note, le statut suit */
	}
	return left > len ? 0 : (long)(len - left);
}

static long get_desc(int slot, unsigned type_index, unsigned len)
{
	return ctrl(slot, 0x0680 | (uint64_t)type_index << 16 | (uint64_t)len << 48);
}

long xh_control(int slot, uint64_t setup, void *data, unsigned len)
{
	if (!slot_ok(slot))
		return E_NODEV;
	unsigned wlen = (unsigned)(setup >> 48);
	if (wlen > 0x1000)
		return E_INVAL;
	if (!(setup & 0x80) && wlen)
		memcpy(zone + Z_CBUF, data, len < wlen ? len : wlen);
	long n = ctrl(slot, setup);
	if (n < 0 || !(setup & 0x80))
		return n;
	if ((unsigned long)n > len)
		n = len;
	memcpy(data, zone + Z_CBUF, (size_t)n);
	return n;
}

static int set_mps0(int slot, unsigned mps)
{
	devs[slot].mps0 = mps;
	uint32_t *in = inctx_clear();
	in[1] = 2; /* A1 */
	ictx(in, 2)[1] = mps << 16 | 4 << 3 | 3 << 1;
	dma_mb();
	return xh_cmd(dev_addr(in), 0, TRB_EVAL_CTX << 10 | (uint32_t)slot << 24, 0);
}

/* ---- extrémités (interruption, bulk) */

/* Configure Endpoint : seule l'extrémité nouvelle (A0 | A(DCI)) ;
 * type : 2 Bulk OUT, 6 Bulk IN, 7 Interrupt IN */
static int config_ep(int slot, unsigned dci, unsigned mps, unsigned interval, unsigned type, struct ring *r)
{
	uint32_t *in = inctx_clear();
	in[1] = 1u << dci | 1;
	copy_slot_ctx(slot, in);
	struct xdev *d = &devs[slot];
	unsigned maxdci = dci > d->maxdci ? dci : d->maxdci;
	uint32_t *sc = ictx(in, 1);
	sc[0] = (sc[0] & 0x07FFFFFF) | maxdci << 27;
	uint32_t *ep = ictx(in, dci + 1);
	ep[0] = interval << 16;
	ep[1] = mps << 16 | type << 3 | ((type & 3) == 1 ? 0 : 3u << 1); /* (isochrone : CErr 0) */
	uint64_t deq = dev_addr(r->base) | 1;
	ep[2] = (uint32_t)deq;
	ep[3] = (uint32_t)(deq >> 32);
	ep[4] = ((type & 3) == 3 || (type & 3) == 1) ? (mps << 16 | 16) : 3072;
	dma_mb();
	int cc = xh_cmd(dev_addr(in), 0, TRB_CONFIG_EP << 10 | (uint32_t)slot << 24, 0);
	if (cc == CC_SUCCESS)
		d->maxdci = maxdci;
	return cc;
}

/* l'extrémité configurée au premier usage -> 0, < 0 */
static long ep_prepare(int slot, unsigned dci, unsigned mps, unsigned interval, unsigned type)
{
	struct ring *r = ring_of(slot, dci);
	if (r->ready)
		return 0;
	ring_init(r, slot_area(slot) + 0x800 + (dci - 1) * 0x400, XH_RING_TRBS);
	int cc = config_ep(slot, dci, mps, interval, type, r);
	if (cc != CC_SUCCESS) {
		say("usbd : xhci : Configure Endpoint refusé, code %d\n", cc);
		return E_IO;
	}
	r->ready = 1;
	return 0;
}

/* intervalle en ms -> code xHCI : log2(ms x 8), 3..10 en LS/FS, 15 au plus */
static unsigned interval_code(unsigned ms, unsigned speed)
{
	uint64_t v = (uint64_t)ms * 8;
	if (v < 8)
		v = 8;
	unsigned c = 63 - (unsigned)__builtin_clzll(v);
	unsigned max = speed >= S_HIGH ? 15 : 10;
	return c > max ? max : c;
}

static uint8_t *pipe_buf(int i, int side)
{
	return zone + Z_REP + (unsigned)i * 2 * XH_PIPE_MPS + (unsigned)side * XH_PIPE_MPS;
}

static void pipe_arm(int i)
{
	struct pipe *p = &pipes[i];
	ring_push(ring_of(p->slot, p->dci), dev_addr(pipe_buf(i, p->side)), p->mps,
		  TRB_NORMAL << 10 | TRB_IOC | TRB_ISP);
	p->armed = 1;
	ring_bell((unsigned)p->slot, p->dci);
}

long xh_intr_open(int slot, unsigned ep, unsigned mps, unsigned interval_ms)
{
	if (!slot_ok(slot))
		return E_NODEV;
	if (!(ep & 0x80) || !(ep & 15))
		return E_INVAL; /* Interrupt IN seulement */
	mps &= 0x7FF;
	if (!mps)
		return E_INVAL;
	if (mps > XH_PIPE_MPS)
		mps = XH_PIPE_MPS;
	unsigned dci = (ep & 15) * 2 + 1;
	int i, found = -1;
	for (i = 0; i < XH_PIPES; i++) /* le même (slot, DCI) d'abord */
		if (pipes[i].slot == slot && pipes[i].dci == dci) {
			if (pipes[i].open)
				return E_BUSY;
			found = i;
			break;
		}
	for (i = 0; found < 0 && i < XH_PIPES; i++) /* sinon un tuyau fermé sans transfert en cours */
		if (!pipes[i].open && !pipes[i].armed)
			found = i;
	if (found < 0)
		return E_NOMEM;
	long r = ep_prepare(slot, dci, mps, interval_code(interval_ms, devs[slot].speed), 7);
	if (r < 0)
		return r;
	struct pipe *p = &pipes[found];
	int armed = p->slot == slot && p->dci == dci && p->armed; /* un transfert peut rester d'avant */
	p->slot = slot;
	p->dci = dci;
	p->mps = mps;
	p->ready = 0;
	p->errs = 0;
	p->dead = 0;
	p->open = 1;
	if (!armed)
		pipe_arm(found);
	return found;
}

long xh_intr_read(int i, const uint8_t **data)
{
	if (i < 0 || i >= XH_PIPES || !pipes[i].open)
		return E_INVAL;
	struct pipe *p = &pipes[i];
	if (p->dead)
		return E_IO;
	if (p->ready == 1) {
		p->ready = 0;
		p->errs = 0;
		*data = pipe_buf(i, p->side);
		p->side ^= 1; /* le suivant dans l'autre tampon */
		unsigned n = p->len;
		pipe_arm(i);
		return n;
	}
	if (p->ready == 2) {
		p->ready = 0;
		if (++p->errs >= 20) {
			p->dead = 1;
			say("usbd : xhci : tuyau abandonné (erreurs)\n");
			return E_IO;
		}
		xh_repair(p->slot, p->dci, (int)p->code);
		pipe_arm(i);
		return 0;
	}
	if (!p->armed)
		pipe_arm(i);
	return 0;
}

void xh_intr_close(int i)
{
	if (i < 0 || i >= XH_PIPES)
		return;
	pipes[i].open = 0;
	pipes[i].ready = 0;
}

static unsigned bulk_dci(unsigned ep)
{
	return (ep & 15) * 2 + ((ep >> 7) & 1);
}

long xh_bulk(int slot, unsigned ep, unsigned mps, void *data, unsigned len)
{
	if (!slot_ok(slot))
		return E_NODEV;
	if (!len)
		return 0;
	if (len > BULK_MAX)
		return E_INVAL;
	unsigned dci = bulk_dci(ep);
	if (dci < 2)
		return E_INVAL;
	long r = ep_prepare(slot, dci, mps & 0x7FF, 0, (ep & 0x80) ? 6 : 2);
	if (r < 0)
		return r;
	uint8_t *buf = zone + Z_BULK;
	if (!(ep & 0x80))
		memcpy(buf, data, len);
	ring_push(ring_of(slot, dci), dev_addr(buf), len, TRB_NORMAL << 10 | TRB_IOC | TRB_ISP);
	ring_bell((unsigned)slot, dci);
	unsigned left = 0;
	int cc = xh_wait(1, slot, dci, XH_DELAY_BULK, &left, 0);
	if (cc != CC_SUCCESS && cc != CC_SHORT) {
		xh_repair(slot, dci, cc); /* STALL, erreur ou délai */
		return cc_errno(cc);
	}
	unsigned n = left > len ? 0 : len - left;
	if (ep & 0x80)
		memcpy(data, buf, n);
	return n;
}

long xh_bulk_nb(int slot, unsigned ep, unsigned mps, void *data, unsigned len)
{
	if (!slot_ok(slot))
		return E_NODEV;
	if (!(ep & 0x80) || !len)
		return E_INVAL;
	if (len > Z_NB_SIZE)
		len = Z_NB_SIZE;
	unsigned dci = bulk_dci(ep);
	if (nb.armed && (nb.slot != slot || nb.dci != dci))
		return E_BUSY;
	if (!nb.armed) {
		long r = ep_prepare(slot, dci, mps & 0x7FF, 0, 6);
		if (r < 0)
			return r;
		nb.armed = 1;
		nb.done = 0;
		nb.slot = slot;
		nb.dci = dci;
		nb.len = len;
		ring_push(ring_of(slot, dci), dev_addr(zone + Z_NB), len, TRB_NORMAL << 10 | TRB_IOC | TRB_ISP);
		ring_bell((unsigned)slot, dci);
	}
	while (!nb.done && evt_take())
		dispatch();
	if (!nb.done)
		return 0;
	nb.armed = 0;
	if (nb.code != CC_SUCCESS && nb.code != CC_SHORT) {
		xh_repair(slot, dci, (int)nb.code);
		return cc_errno((int)nb.code);
	}
	unsigned n = nb.left > nb.len ? 0 : nb.len - nb.left;
	if (n > len)
		n = len;
	memcpy(data, zone + Z_NB, n);
	return n;
}

void xh_bulk_nb_stop(void)
{
	if (!nb.armed)
		return;
	if (!nb.done && slot_ok(nb.slot))
		xh_repair(nb.slot, nb.dci, -1); /* Stop Endpoint : le TRB en cours est abandonné */
	nb.armed = 0;
}

long xh_bulk_reset(int slot, unsigned ep)
{
	if (!slot_ok(slot))
		return E_NODEV;
	unsigned dci = bulk_dci(ep);
	if (dci < 2)
		return E_INVAL;
	struct ring *r = ring_of(slot, dci);
	if (!r->ready)
		return 0; /* jamais configurée */
	uint32_t state = *(uint32_t *)(slot_area(slot) + dci * ctx) & 7;
	if (state == 2) /* Halted */
		xh_repair(slot, dci, CC_STALL);
	return 0;
}

/* ---- hubs (USB 2) */

static int config_hub(int slot, unsigned ports, unsigned ttt)
{
	uint32_t *in = inctx_clear();
	in[1] = 1; /* A0 */
	copy_slot_ctx(slot, in);
	uint32_t *sc = ictx(in, 1);
	sc[0] |= 1u << 26;                           /* Hub */
	sc[1] = (sc[1] & 0x00FFFFFF) | ports << 24; /* nombre de ports */
	sc[2] = (sc[2] & ~(3u << 16)) | ttt << 16;
	dma_mb();
	return xh_cmd(dev_addr(in), 0, TRB_CONFIG_EP << 10 | (uint32_t)slot << 24, 0);
}

static long hub_feature(int slot, unsigned port, unsigned req, unsigned sel)
{
	return ctrl(slot, req | (uint64_t)sel << 16 | (uint64_t)port << 32);
}

/* -> wPortStatus | wPortChange << 16, < 0 si erreur */
static long hub_status(int slot, unsigned port)
{
	long r = ctrl(slot, 0x00A3 | (uint64_t)port << 32 | 4ull << 48);
	if (r < 0)
		return r;
	return *(uint32_t *)(zone + Z_CBUF);
}

static void log_port(const char *what, unsigned port, int hub, unsigned speed)
{
	say("usbd : xhci : %s port %u", what, port);
	if (hub)
		say(" (hub %d)", hub);
	if (speed)
		say(", vitesse %u", speed);
	say("\n");
}

static void enumerate(unsigned rport, unsigned route, unsigned speed, unsigned tt_slot, unsigned tt_port,
		      unsigned depth, int parent, int port);

static void hub_port(int hub, unsigned port)
{
	struct xdev *h = &devs[hub];
	hub_feature(hub, port, 0x0323, 4); /* SET_FEATURE(PORT_RESET) */
	long st = -1;
	for (int i = 0; i < 25; i++) {
		sleep_ms(10);
		st = hub_status(hub, port);
		if (st < 0)
			return;
		if (st & (1 << 20)) /* C_PORT_RESET */
			break;
		st = -1;
	}
	if (st < 0)
		return;
	hub_feature(hub, port, 0x0123, 20); /* CLEAR_FEATURE(C_PORT_RESET) */
	if (!(st & 2))
		return; /* port non activé */
	sleep_ms(20);
	unsigned speed = (st & (1 << 10)) ? S_HIGH : (st & (1 << 9)) ? S_LOW : S_FULL;
	log_port("branchement", port, hub, speed);
	/* TT : hub HS + périphérique LS/FS -> ce hub ; sinon celui du hub */
	unsigned tts = h->tt_slot, ttp = h->tt_port;
	if (h->speed == S_HIGH && speed != S_HIGH) {
		tts = (unsigned)hub;
		ttp = port;
	}
	unsigned route = h->route | (port > 15 ? 15u : port) << (h->depth * 4);
	enumerate(h->rport, route, speed, tts, ttp, h->depth + 1, hub, (int)port);
}

static void hub_init(int slot)
{
	struct xdev *d = &devs[slot];
	if (d->speed >= S_SUPER)
		return; /* hubs SuperSpeed : pas encore */
	if (ctrl(slot, 0x06A0 | (uint64_t)0x2900 << 16 | 8ull << 48) < 0)
		return;
	uint8_t *b = zone + Z_CBUF;
	unsigned ports = b[2], pwr = b[5], chars = b[3] | b[4] << 8;
	say("usbd : xhci : hub, %u ports\n", ports);
	config_hub(slot, ports, (chars >> 5) & 3);
	d->nports = ports;
	d->hub = 1;
	for (unsigned p = 1; p <= ports; p++)
		hub_feature(slot, p, 0x0323, 8); /* SET_FEATURE(PORT_POWER) */
	sleep_ms(pwr * 2 + 500);           /* (le temps que les périphériques se signalent) */
	for (unsigned p = 1; p <= ports; p++) {
		long st = hub_status(slot, p);
		if (st < 0)
			continue;
		if (st & (1 << 16))
			hub_feature(slot, p, 0x0123, 16); /* C_PORT_CONNECTION acquitté */
		if (st & 1)
			hub_port(slot, p);
	}
}

static void slot_free(int slot);

/* adresse, note dans l'arbre, configure, puis hub ou usbd_attach ; un échec
 * rend le slot */
static void enumerate(unsigned rport, unsigned route, unsigned speed, unsigned tt_slot, unsigned tt_port,
		      unsigned depth, int parent, int port)
{
	if (depth >= XH_DEPTH_MAX)
		return;
	int slot = new_device(rport, route, speed, tt_slot, tt_port, depth);
	if (!slot) {
		say("usbd : xhci : périphérique sans slot (Enable Slot / Address Device)\n");
		return;
	}
	struct xdev *d = &devs[slot];
	d->parent = parent;
	d->port = port;
	uint8_t *cb = zone + Z_CBUF;
	if (get_desc(slot, 0x0100, 8) < 0) /* bMaxPacketSize0 */
		goto err;
	unsigned m = cb[7];
	if (d->speed < S_HIGH && m != d->mps0 && m >= 8 && m <= 64)
		set_mps0(slot, m);
	if (get_desc(slot, 0x0100, 18) < 18)
		goto err;
	uint8_t *dd = desc_copy[depth];
	memcpy(dd, cb, 18);
	unsigned dclass = dd[4];
	say("usbd : xhci : slot %d, id %04x:%04x, classe %02x\n", slot, dd[8] | dd[9] << 8,
	    dd[10] | dd[11] << 8, dclass);
	if (get_desc(slot, 0x0200, 9) < 9)
		goto err;
	unsigned total = cb[2] | cb[3] << 8;
	if (total > CFG_MAX)
		total = CFG_MAX;
	long got = get_desc(slot, 0x0200, total);
	if (got < 9)
		goto err;
	if ((unsigned long)got < total)
		total = (unsigned)got;
	uint8_t *cfg = cfg_copy[depth];
	memcpy(cfg, cb, total);
	if (ctrl(slot, 0x0900 | (uint64_t)cfg[5] << 16) < 0) /* SET_CONFIGURATION */
		goto err;
	sleep_ms(20);
	if (dclass == 9 || (total >= 9 + 6 && cfg[9 + 5] == 9)) {
		hub_init(slot);
		return;
	}
	unsigned us = d->speed - 1; /* au cœur : vitesse usbd = PSI - 1 */
	if (us > USB_SS)
		us = USB_SS;
	long n = usbd_attach(slot, dd, cfg, total, (int)us);
	d->usbd = n > 0 ? n : 0;
	return;
err:
	say("usbd : xhci : erreur d'énumération\n");
	slot_free(slot);
}

/* ---- ports racines */

static volatile uint8_t *portsc(unsigned port)
{
	return op + OP_PORTSC + (port - 1) * 16;
}

/* bits conservés + changements présents (acquittés) ; jamais PED ni PR */
static uint32_t portsc_ack(uint32_t v)
{
	return (v & PORTSC_KEEP) | (v & PORTSC_CHG);
}

static void root_port(unsigned port)
{
	volatile uint8_t *ps = portsc(port);
	uint32_t v = r32(ps, 0);
	if (!(v & 1)) { /* rien de connecté : changements acquittés */
		w32(ps, 0, portsc_ack(v));
		return;
	}
	w32(ps, 0, (v & PORTSC_KEEP) | 1u << 4); /* PR */
	int i;
	for (i = 0; i < 200; i++) {
		sleep_ms(1);
		v = r32(ps, 0);
		if (v & (1u << 21)) /* PRC */
			break;
	}
	if (i == 200)
		return;
	w32(ps, 0, portsc_ack(v));
	if (!(v & 2))
		return; /* port non activé */
	sleep_ms(20);
	unsigned speed = (v >> 10) & 15;
	log_port("branchement", port, 0, speed);
	enumerate(port, 0, speed, 0, 0, 0, 0, (int)port);
}

static int occupant(int parent, int port)
{
	for (int s = 1; s <= XH_MAX_SLOTS; s++)
		if (devs[s].active && devs[s].parent == parent && devs[s].port == port)
			return s;
	return 0;
}

/* le slot hors de l'arbre ; ses tuyaux et extrémités oubliés, Disable Slot ;
 * puis usbd_detach s'il avait été confié au cœur */
static void slot_free(int slot)
{
	struct xdev *d = &devs[slot];
	long n = d->usbd;
	d->active = 0;
	d->usbd = 0;
	d->hub = 0;
	d->nports = 0;
	d->parent = 0;
	d->port = 0;
	for (int i = 0; i < XH_PIPES; i++)
		if (pipes[i].slot == slot)
			memset(&pipes[i], 0, sizeof pipes[i]);
	if (nb.armed && nb.slot == slot)
		nb.armed = 0;
	xh_cmd(0, 0, TRB_DISABLE_SLOT << 10 | (uint32_t)slot << 24, 0);
	((uint64_t *)(zone + Z_DCBAA))[slot] = 0;
	memset(rings[slot], 0, sizeof rings[slot]);
	if (n)
		usbd_detach(n);
}

/* descendants d'abord (hubs), puis lui -> slots rendus */
static int detach_slot(int slot)
{
	int n = 1;
	for (int s = 1; s <= XH_MAX_SLOTS; s++)
		if (s != slot && devs[s].active && devs[s].parent == slot)
			n += detach_slot(s);
	slot_free(slot);
	return n;
}

static int detach(int parent, int port)
{
	int n = 0;
	for (int s = 1; s <= XH_MAX_SLOTS; s++)
		if (devs[s].active && devs[s].parent == parent && devs[s].port == port)
			n += detach_slot(s);
	return n;
}

static void scan_roots(void)
{
	for (unsigned p = 1; p <= nports; p++) {
		volatile uint8_t *ps = portsc(p);
		uint32_t v = r32(ps, 0);
		if (!(v & (1u << 17))) { /* CSC */
			if (v & PORTSC_CHG)
				w32(ps, 0, portsc_ack(v)); /* (autres changements : acquittés) */
			continue;
		}
		w32(ps, 0, portsc_ack(v));
		if (!(v & 1)) {
			detach(0, (int)p);
			log_port("retrait", p, 0, 0);
			continue;
		}
		if ((v & 2) && occupant(0, (int)p))
			continue; /* connecté, activé, déjà connu : parasite */
		detach(0, (int)p); /* (remplacé en un éclair : l'ancien part) */
		sleep_ms(100);     /* laisser le contact s'établir */
		root_port(p);
	}
}

/* -> 0, < 0 si le hub ne répond pas */
static long hub_change(int hub, unsigned port)
{
	long st = hub_status(hub, port);
	if (st < 0)
		return st;
	if (!(st & (1 << 16)))
		return 0;
	hub_feature(hub, port, 0x0123, 16); /* CLEAR_FEATURE(C_PORT_CONNECTION) */
	if (!(st & 1)) {
		detach(hub, (int)port);
		log_port("retrait", port, hub, 0);
		return 0;
	}
	if ((st & 2) && occupant(hub, (int)port))
		return 0;
	detach(hub, (int)port);
	sleep_ms(100);
	hub_port(hub, port);
	return 0;
}

static void scan_hubs(void)
{
	for (unsigned depth = 0; depth < XH_DEPTH_MAX; depth++)
		for (int s = 1; s <= XH_MAX_SLOTS; s++) {
			struct xdev *d = &devs[s];
			if (!d->active || d->depth != depth)
				continue;
			for (unsigned p = 1; d->hub && p <= d->nports; p++)
				if (hub_change(s, p) < 0)
					break; /* hub muet : son parent le retirera */
		}
}

void xh_poll(int irq)
{
	if (irq && irq_line >= 0) {
		w32(op, OP_USBSTS, 1u << 3);   /* EINT acquitté */
		w32(rt_ir0, IR_IMAN, 3);       /* IP acquitté, IE gardé */
	}
	while (evt_take())
		dispatch();
	long t = now_ms();
	if (scan_now || t - t_scan >= (irq_line >= 0 ? 1000 : XH_SCAN_MS)) {
		scan_now = 0;
		t_scan = t;
		scan_roots();
		scan_hubs();
		t_scan = now_ms(); /* (une énumération a pu durer) */
	}
	if (irq && irq_line >= 0)
		korin_sys1(SYS_IRQ_ACK, irq_line);
}

long xh_irq_line(void)
{
	return irq_line;
}

/* ---- contrôleur */

/* le micrologiciel (BIOS) rend le contrôleur au système (capacité étendue 1,
 * USB Legacy Support) : sur PC, l'émulation du clavier USB de l'UEFI */
static void bios_handoff(void)
{
	uint32_t hcc = r32(cap, 0x10);
	unsigned off = (hcc >> 16) * 4;
	for (int n = 0; off && off + 8 <= plat.regs_size && n < 64; n++) {
		uint32_t v = r32(cap, off);
		if ((v & 0xFF) == 1) {
			if (v & (1u << 16)) { /* BIOS Owned */
				w32(cap, off, v | 1u << 24); /* OS Owned */
				for (int i = 0; i < 1000 && (r32(cap, off) & (1u << 16)); i++)
					sleep_ms(1);
				if (r32(cap, off) & (1u << 16)) {
					say("usbd : xhci : le BIOS garde le contrôleur, on le prend\n");
					w32(cap, off, (r32(cap, off) & ~(1u << 16)) | 1u << 24);
				}
			}
			/* SMI de l'USB coupés (les bits RW1C acquittés) */
			w32(cap, off + 4, (r32(cap, off + 4) & 0xFFFF1FEEu) | 0xE0000000u);
			return;
		}
		unsigned next = (v >> 8) & 0xFF;
		if (!next)
			break;
		off += next * 4;
	}
}

static int hc_init(void)
{
	uint32_t v = r32(cap, 0);
	if (v == 0xFFFFFFFF)
		return 0;
	unsigned ver = v >> 16, caplen = v & 0xFF;
	if (ver < 0x96 || ver > 0x120)
		return 0;
	say("usbd : xhci : version %x\n", ver);
	op = cap + caplen;
	unsigned dboff = r32(cap, 0x14) & ~3u, rtsoff = r32(cap, 0x18) & ~0x1Fu;
	if (dboff + 0x400 > plat.regs_size || rtsoff + 0x40 > plat.regs_size)
		return 0;
	db = (volatile uint32_t *)(cap + dboff);
	rt_ir0 = cap + rtsoff + 0x20;
	uint32_t hcs1 = r32(cap, 4);
	nports = hcs1 >> 24;
	if (caplen + OP_PORTSC + nports * 16 > plat.regs_size)
		return 0;
	nslots = hcs1 & 0xFF;
	if (nslots > XH_MAX_SLOTS)
		nslots = XH_MAX_SLOTS;
	uint32_t hcc = r32(cap, 0x10);
	ctx = (hcc & 4) ? 64 : 32;
	uint32_t hcs2 = r32(cap, 8);
	unsigned nscratch = ((hcs2 >> 21) & 31) << 5 | ((hcs2 >> 27) & 31);
	bios_handoff();

	/* arrêt, puis remise à zéro */
	w32(op, OP_USBCMD, r32(op, OP_USBCMD) & ~1u);
	if (!wait_op(OP_USBSTS, 1, 1, 100))
		return 0;
	w32(op, OP_USBCMD, 2); /* HCRST */
	sleep_ms(10);
	if (!wait_op(OP_USBCMD, 2, 0, 1000) || !wait_op(OP_USBSTS, 1u << 11, 0, 1000))
		return 0;
	if (!(r32(op, OP_PAGESIZE) & 1))
		return 0; /* pages de 4 Ko obligatoires */

	/* slots, DCBAA, scratchpad */
	w32(op, OP_CONFIG, nslots);
	if (nscratch) {
		uint64_t ph;
		long a = korin_sys2(SYS_DMA_ALLOC, (nscratch + 1) * 4096ull, &ph);
		if (a < 0)
			return 0;
		uint64_t *arr = (uint64_t *)a; /* le tableau, puis les pages */
		uint64_t base = ph + plat.dma_offset;
		for (unsigned i = 0; i < nscratch; i++)
			arr[i] = base + (i + 1) * 4096ull;
		((uint64_t *)(zone + Z_DCBAA))[0] = base;
		say("usbd : xhci : %u page(s) scratchpad\n", nscratch);
	}
	dma_mb();
	w64(op, OP_DCBAAP, dev_addr(zone + Z_DCBAA));

	/* anneau de commandes */
	ring_init(&cmd_ring, zone + Z_CMD, XH_CMD_TRBS);
	w64(op, OP_CRCR, dev_addr(zone + Z_CMD) | 1); /* RCS = 1 */

	/* anneau d'événements (interrupteur 0) */
	uint64_t *erst = (uint64_t *)(zone + Z_ERST);
	erst[0] = dev_addr(zone + Z_EVT);
	erst[1] = XH_EVT_TRBS;
	evt_deq = 0;
	evt_cyc = 1;
	dma_mb();
	w32(rt_ir0, IR_ERSTSZ, 1);
	w64(rt_ir0, IR_ERDP, dev_addr(zone + Z_EVT));
	w64(rt_ir0, IR_ERSTBA, dev_addr(erst)); /* ERSTBA en dernier */
	w32(rt_ir0, IR_IMOD, 4000);             /* au plus une interruption par ms */
	w32(rt_ir0, IR_IMAN, 1);                /* IP acquitté, IE = 0 */

	/* interruptions (MSI-X) si la machine en donne */
	if (plat.irq_setup) {
		irq_line = plat.irq_setup();
		if (irq_line >= 0)
			w32(rt_ir0, IR_IMAN, 3); /* IE */
	}

	/* marche */
	w32(op, OP_USBCMD, r32(op, OP_USBCMD) | 1 | (irq_line >= 0 ? 4u : 0)); /* RS, INTE */
	if (!wait_op(OP_USBSTS, 1, 0, 100))
		return 0;
	if (xh_cmd(0, 0, TRB_NOOP << 10, 0) != CC_SUCCESS)
		return 0;
	/* ports alimentés (contrôleurs à PPC) */
	if (hcc & 8)
		for (unsigned p = 1; p <= nports; p++) {
			uint32_t s = r32(portsc(p), 0);
			if (!(s & (1u << 9)))
				w32(portsc(p), 0, (s & PORTSC_KEEP) | 1u << 9);
		}
	sleep_ms(100); /* laisser les ports s'établir */
	return 1;
}

int xh_init(const char *arg)
{
	int r = plat_xhci_init(arg, &plat);
	if (r < 0)
		return r;
	cap = plat.regs;
	uint64_t ph;
	long a = korin_sys2(SYS_DMA_ALLOC, Z_SIZE, &ph);
	if (a < 0) {
		say("usbd : xhci : pas de zone DMA (%ld)\n", a);
		return (int)a;
	}
	zone = (uint8_t *)a;
	zone_phys = ph;
	zone_dev = ph + plat.dma_offset;
	if (plat.dma_limit && zone_dev + Z_SIZE > plat.dma_limit) {
		say("usbd : xhci : zone DMA hors de la fenêtre du contrôleur\n");
		return E_IO;
	}
	if (!hc_init()) {
		say("usbd : xhci : échec d'initialisation\n");
		return E_IO;
	}
	say("usbd : xhci : %u ports, %u slots, contextes de %u o, %s\n", nports, nslots, ctx,
	    irq_line >= 0 ? "MSI-X" : "sondage");
	for (unsigned p = 1; p <= nports; p++)
		root_port(p);
	t_scan = now_ms();
	say("usbd : xhci : prêt\n");
	return 0;
}
