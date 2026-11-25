/* usbd - le bus USB en service, en C (docs/USB.md, docs/etude/PC-PILOTES.md),
 * repris de usbd.asm ; le protocole avec les pilotes de classe ne change pas
 * (src/inc/usb.inc) :
 *  * le contrôleur xHCI (xhci.c, commun au Pi et au PC ; la machine dans
 *    plat_<arch>.c) énumère les ports et les hubs ;
 *  * chaque interface d'un périphérique est annoncée à hwmanager (HW_OP_USB),
 *    qui lance le pilote de drivers.txt (usbkeyboard, usbstorage…) ;
 *  * le service « usb » fait les transferts des pilotes : descripteurs,
 *    contrôle, interruption (lecture : réponse différée, quand un rapport
 *    arrive), bulk (par le tampon partagé du pilote), bulk IN sans attente ;
 *  * chaque périphérique est publié sur le bus (docs/BUS.md) :
 *    « korin.usb.device.<numéro> », retiré avec lui.
 * Sur PC, le contrôleur lève une MSI-X : le noyau lève le bit 1 du service
 * (SVC_OP_SIGNAL), usbd vide les événements ; sans interruption (Pi), la
 * boucle sonde toutes les 2 ms comme l'asm.
 *  * isochrone (iso.c, cartes son et casques USB : usbaudio) : un anneau
 *    partagé par tuyau, ISO_XQ TRB Isoch en file, servis à chaque tour.
 * Pas encore en C : le DWC2 (port USB-C du Pi, et son isochrone d'iso.inc) ;
 * sur le Pi, la carte garde donc usbd.asm.
 *
 * Plusieurs xHCI (PC : le ZBook en a deux) : un seul usbd les sert tous ;
 * hwmanager lance un usbd par contrôleur, le second trouve « usb » pris et
 * passe sa fonction au premier (USB_OP_ADD_CONTROLLER), puis s'arrête. Les
 * pilotes de classe ne voient que les numéros de périphérique d'usbd.
 *
 *	usbd [SSSS:BB:DD.F]   la fonction PCIe du xHCI (PC : donnée par hwmanager) */
#include "usbd.h"
#include "caller.h"

#define DEV_MAX 16
#define PIPE_MAX 16
#define BUF_MAX 8

struct dev {
	int used;
	long handle; /* contrôleur << 8 | slot (xhci.c) */
	uint32_t vidpid;
	int speed;
	unsigned cfg_len;
	uint8_t cfg[CFG_MAX];
};

struct upipe {
	int pid; /* pilote propriétaire (0 : libre) */
	long handle; /* tuyau du contrôleur */
	long req;    /* lecture en attente : numéro de requête (0 : aucune) */
	long dev;
};

struct sbuf {
	int pid;
	uint8_t *va;
	long num;
	uint64_t size;
};

static struct dev devs[DEV_MAX];
static struct upipe upipes[PIPE_MAX];
static struct sbuf bufs[BUF_MAX];
static long dev_next;
static long chan = -1;
static long hw_chan = -1; /* -2 : absent */
static long bus_chan = -1; /* -2 : absent */
static int nb_pid;
static long nb_dev, nb_handle;
static uint8_t msg[512] __attribute__((aligned(16)));
static uint8_t rep[512] __attribute__((aligned(16)));
static uint8_t hw_msg[512] __attribute__((aligned(16)));

void say(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof buf - 1)
		n = sizeof buf - 1;
	korin_sys2(SYS_PRINT, buf, n);
}

#define M(i) (((uint64_t *)msg)[i])

static struct dev *dev_of(long n)
{
	if (n < 1 || n > DEV_MAX || !devs[n - 1].used)
		return 0;
	return &devs[n - 1];
}

static struct sbuf *buf_of(int pid)
{
	for (int i = 0; i < BUF_MAX; i++)
		if (bufs[i].pid == pid)
			return &bufs[i];
	return 0;
}

/* ---- bus (busd), sans BusKit comme l'asm (buspub.inc) */

static void bus_publish(long dev, const struct dev *d)
{
	if (bus_chan == -2)
		return;
	if (bus_chan < 0) {
		bus_chan = korin_sys2(SYS_SVC_OPEN, "bus", 1000);
		if (bus_chan < 0) {
			bus_chan = -2;
			say("usbd : busd absent, pas de publication\n");
			return;
		}
	}
	uint8_t m[512] __attribute__((aligned(16)));
	memset(m, 0, 48);
	char *name = (char *)m + 48;
	int n = snprintf(name, 64, "korin.usb.device.%ld", dev) + 1;
	char *val = name + n, *v = val;
	if (d) {
		v += snprintf(v, 16, "%04x:%04x", d->vidpid >> 16, d->vidpid & 0xFFFF);
		for (unsigned o = 0; o + 9 <= d->cfg_len && v < val + 240;) {
			const uint8_t *x = d->cfg + o;
			if (x[0] < 2)
				break;
			if (x[1] == USB_DT_INTERFACE && x[3] == 0)
				v += snprintf(v, 16, " %02x/%02x/%02x", x[5], x[6], x[7]);
			o += x[0];
		}
	}
	*v = 0;
	((uint64_t *)m)[0] = 1;              /* BUS_OP_PUBLISH */
	((uint64_t *)m)[1] = d ? 1 : 2;      /* BUS_RETAIN, BUS_DELETE */
	((uint64_t *)m)[2] = (uint64_t)(v - val);
	if (korin_sys2(SYS_SVC_CALL, bus_chan, m) == -32)
		bus_chan = -1; /* busd relancé : à rouvrir */
}

/* ---- hwmanager */

static void hw_call(uint64_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	if (hw_chan == -2)
		return;
	if (hw_chan < 0) {
		hw_chan = korin_sys2(SYS_SVC_OPEN, "hwmanager", 2000);
		if (hw_chan < 0) {
			hw_chan = -2;
			say("usbd : hwmanager absent, pas d'annonce\n");
			return;
		}
	}
	memset(hw_msg, 0, 48);
	uint64_t *m = (uint64_t *)hw_msg;
	m[0] = op;
	m[1] = a;
	m[2] = b;
	m[3] = c;
	m[4] = d;
	korin_sys2(SYS_SVC_CALL, hw_chan, hw_msg);
}

long usbd_attach(long handle, const uint8_t *dd, const uint8_t *cfg, unsigned cfg_len, int speed)
{
	long n = -1;
	for (int k = 0; k < DEV_MAX; k++) { /* en tournant : un numéro libéré ne resert que tard */
		long i = (dev_next + k) % DEV_MAX;
		if (!devs[i].used) {
			n = i;
			break;
		}
	}
	if (n < 0)
		return E_NOMEM;
	dev_next = n + 1;
	struct dev *d = &devs[n];
	d->used = 1;
	d->handle = handle;
	d->speed = speed;
	d->vidpid = (uint32_t)(dd[8] | dd[9] << 8) << 16 | (dd[10] | dd[11] << 8);
	if (cfg_len > CFG_MAX)
		cfg_len = CFG_MAX;
	d->cfg_len = cfg_len;
	memcpy(d->cfg, cfg, cfg_len);
	n++;
	say("usbd : périphérique %ld (%04X:%04X)\n", n, d->vidpid >> 16, d->vidpid & 0xFFFF);
	for (unsigned o = 0; o + 9 <= cfg_len;) { /* les interfaces (réglage 0) */
		const uint8_t *x = cfg + o;
		if (x[0] < 2)
			break;
		if (x[1] == USB_DT_INTERFACE && x[3] == 0) {
			say("usbd :   interface %u : %02X/%02X/%02X\n", x[2], x[5], x[6], x[7]);
			hw_call(HW_OP_USB, (uint64_t)n, x[2], (uint32_t)x[5] << 16 | (uint32_t)x[6] << 8 | x[7],
				d->vidpid);
		}
		o += x[0];
	}
	bus_publish(n, d);
	return n;
}

static void reply(long req, long r, const void *data, unsigned len)
{
	((uint64_t *)rep)[0] = (uint64_t)r;
	if (data && len)
		memcpy(rep + 48, data, len);
	korin_sys2(SYS_SVC_REPLY, req, rep);
}

void usbd_detach(long n)
{
	struct dev *d = dev_of(n);
	if (!d)
		return;
	d->used = 0;
	if (nb_pid && nb_dev == n) {
		xh_bulk_nb_stop(nb_handle);
		nb_pid = 0;
	}
	say("usbd : périphérique %ld retiré\n", n);
	iso_close_dev(n);
	for (int i = 0; i < PIPE_MAX; i++) {
		struct upipe *p = &upipes[i];
		if (!p->pid || p->dev != n)
			continue;
		p->pid = 0;
		xh_intr_close(p->handle);
		if (p->req) { /* le pilote reçoit E_NODEV : il s'arrête */
			reply(p->req - 1, E_NODEV, 0, 0);
			p->req = 0;
		}
	}
	bus_publish(n, 0);
	hw_call(HW_OP_USB_REMOVED, (uint64_t)n, 0, 0, 0);
}

/* un pilote s'est arrêté : ses tuyaux, sa lecture sans attente, son tampon */
static void driver_ended(int pid)
{
	iso_close_pid(pid);
	if (nb_pid == pid) {
		xh_bulk_nb_stop(nb_handle);
		nb_pid = 0;
	}
	for (int i = 0; i < PIPE_MAX; i++) {
		struct upipe *p = &upipes[i];
		if (p->pid != pid)
			continue;
		p->pid = 0;
		xh_intr_close(p->handle);
		if (p->req) {
			reply(p->req - 1, 0, 0, 0);
			p->req = 0;
		}
	}
	struct sbuf *b = buf_of(pid);
	if (b) {
		b->pid = 0;
		korin_sys1(SYS_BUF_FREE, b->num);
	}
}

static long op_bulk_buffer(int pid, uint64_t size)
{
	struct sbuf *b = buf_of(pid);
	if (!b)
		for (int i = 0; i < BUF_MAX && !b; i++)
			if (!bufs[i].pid)
				b = &bufs[i];
	if (!b)
		return E_NOMEM;
	if (size > BULK_MAX + 4096)
		size = BULK_MAX + 4096;
	long num = korin_sys1(SYS_BUF_CREATE, size);
	if (num < 0)
		return num;
	long va = korin_sys1(SYS_BUF_MAP, num);
	if (va < 0)
		return va;
	long r = korin_sys2(SYS_BUF_GRANT, num, pid);
	if (r < 0)
		return r;
	b->pid = pid;
	b->num = num;
	b->va = (uint8_t *)va;
	b->size = size;
	return num;
}

static long add_controller(const char *arg);

/* une requête ; -> 1 si la réponse est différée */
static int request(long req)
{
	uint64_t op = M(0);
	int pid = (int)*(uint32_t *)(msg + 44);
	long r = E_INVAL;
	struct dev *d;
	struct sbuf *b;
	if (op >= 0xFFF0) { /* les avis : du noyau seulement (pid 0), sans réponse */
		if (op == SVC_OP_END && svc_from_kernel(msg))
			driver_ended((int)M(1));
		return 1;
	}
	/* tout ce qui parle au matériel (CONTROL, BULK, INTR, ISO, ADD_CONTROLLER) : les pilotes (droits
	 * de /sys) ; DESCRIPTORS reste ouvert (lsusb) */
	if (op != USB_OP_DESCRIPTORS && !caller_rights(pid)) {
		M(0) = (uint64_t)E_PERM;
		korin_sys2(SYS_SVC_REPLY, req, msg);
		return 0;
	}
	switch (op) {
	case USB_OP_DESCRIPTORS:
		if (!(d = dev_of((long)M(1)))) {
			r = E_NODEV;
			break;
		}
		memcpy(msg + 48, d->cfg, d->cfg_len);
		M(2) = d->cfg_len;
		M(3) = d->vidpid;
		M(4) = (uint64_t)d->speed;
		r = 0;
		break;
	case USB_OP_CONTROL: {
		if (!(d = dev_of((long)M(1)))) {
			r = E_NODEV;
			break;
		}
		if (M(3) > CFG_MAX)
			break;
		uint64_t setup = M(2);
		r = xh_control(d->handle, setup, msg + 48, (unsigned)M(3));
		/* CLEAR_FEATURE(ENDPOINT_HALT) réussi : le contrôleur remet son
		 * extrémité (bascule, anneau) à zéro */
		if (r >= 0 && (setup & 0xFFFFFFFF) == 0x00000102)
			xh_bulk_reset(d->handle, (unsigned)(setup >> 32) & 0xFF);
		break;
	}
	case USB_OP_INTR_OPEN: {
		if (!(d = dev_of((long)M(1)))) {
			r = E_NODEV;
			break;
		}
		int i;
		for (i = 0; i < PIPE_MAX && upipes[i].pid; i++)
			;
		if (i == PIPE_MAX) {
			r = E_NOMEM;
			break;
		}
		r = xh_intr_open(d->handle, (unsigned)M(2), (unsigned)M(3), (unsigned)M(4));
		if (r < 0)
			break;
		upipes[i].handle = r;
		upipes[i].req = 0;
		upipes[i].dev = (long)M(1);
		upipes[i].pid = pid;
		r = i;
		break;
	}
	case USB_OP_INTR_READ: {
		if (M(1) >= PIPE_MAX)
			break;
		struct upipe *p = &upipes[M(1)];
		if (p->pid != pid) {
			r = E_PERM;
			break;
		}
		if (p->req) /* une lecture déjà en attente : rendue */
			reply(p->req - 1, 0, 0, 0);
		p->req = req + 1; /* réponse quand un rapport arrive */
		return 1;
	}
	case USB_OP_BULK_BUFFER:
		r = op_bulk_buffer(pid, M(1));
		break;
	case USB_OP_BULK:
	case USB_OP_BULK_NB: {
		if (!(b = buf_of(pid)))
			break;
		uint64_t len = *(uint64_t *)(msg + 48), off = M(4);
		if (len > BULK_MAX || len > b->size || off > b->size - len)
			break;
		if (!(d = dev_of((long)M(1)))) {
			r = E_NODEV;
			break;
		}
		if (op == USB_OP_BULK)
			r = xh_bulk(d->handle, (unsigned)M(2), (unsigned)M(3), b->va + off, (unsigned)len);
		else {
			r = xh_bulk_nb(d->handle, (unsigned)M(2), (unsigned)M(3), b->va + off, (unsigned)len);
			if (r >= 0) {
				nb_pid = pid;
				nb_dev = (long)M(1);
				nb_handle = d->handle;
			}
		}
		break;
	}
	case USB_OP_ISO_OPEN: { /* b extrémité, c taille max., d octets par ms | trame << 32 -> tuyau ; b = anneau */
		if (!(d = dev_of((long)M(1)))) {
			r = E_NODEV;
			break;
		}
		long buf = 0;
		r = iso_open(pid, (long)M(1), d->handle, (unsigned)M(2), (unsigned)M(3), M(4), &buf);
		if (r >= 0)
			M(2) = (uint64_t)buf;
		break;
	}
	case USB_OP_ISO_CLOSE:
		r = iso_close(pid, (long)M(1));
		break;
	case USB_OP_ADD_CONTROLLER: /* (un second usbd, lancé pour un autre xHCI) */
		msg[48 + 31] = 0;
		if (!msg[48])
			break; /* (E_INVAL : une adresse, sinon le premier xHCI serait repris) */
		say("usbd : un autre xHCI : %s\n", (char *)msg + 48);
		r = add_controller((char *)msg + 48);
		if (r > 0)
			r = 0;
		break;
	}
	M(0) = (uint64_t)r;
	korin_sys2(SYS_SVC_REPLY, req, msg);
	return 0;
}

/* les lectures d'interruption en attente */
static void serve_reads(void)
{
	for (int i = 0; i < PIPE_MAX; i++) {
		struct upipe *p = &upipes[i];
		if (!p->pid || !p->req)
			continue;
		const uint8_t *data = 0;
		long n = xh_intr_read(p->handle, &data);
		if (n == 0)
			continue;
		long req = p->req - 1;
		p->req = 0;
		if (n > 0 && n > CFG_MAX)
			n = CFG_MAX;
		reply(req, n, n > 0 ? data : 0, n > 0 ? (unsigned)n : 0);
	}
}

/* un contrôleur de plus (arg : sa fonction PCIe, ou 0 : le premier trouvé) ;
 * son interruption liée au bit 1 du canal, comme le premier -> son numéro,
 * < 0 : erreur */
static long add_controller(const char *arg)
{
	int hc = xh_add(arg);
	if (hc < 0)
		return hc;
	long line = xh_irq_line(hc);
	if (line >= 0) {
		long r = korin_sys3(SYS_IRQ_BIND, line, chan, 1);
		if (r < 0) {
			say("usbd : IRQ_BIND refusé (%ld), sondage\n", r);
			xh_no_irq(hc);
		} else
			say("usbd : interruption sur la ligne %ld\n", line);
	}
	return hc;
}

/* le service « usb » est déjà tenu (un autre xHCI, PC) : notre fonction PCIe est
 * confiée à l'usbd en service, et ce lancement s'arrête -> code de sortie :
 * 0 confié, E_EXIST déjà servi (hwmanager ne relance pas), 1 sinon */
static int hand_over(const char *arg)
{
	long usb = korin_sys2(SYS_SVC_OPEN, "usb", 5000);
	if (usb < 0) {
		say("usbd : service « usb » pris mais injoignable (%ld)\n", usb);
		return 1;
	}
	memset(msg, 0, 48 + 32);
	M(0) = USB_OP_ADD_CONTROLLER;
	strncpy((char *)msg + 48, arg, 31);
	long r = korin_sys2(SYS_SVC_CALL, usb, msg);
	if (r == E_EXIST) {
		say("usbd : %s est déjà servi\n", arg);
		return E_EXIST;
	}
	if (r < 0) {
		say("usbd : %s refusé par l'usbd en service (%ld)\n", arg, r);
		return 1;
	}
	say("usbd : %s confié à l'usbd en service\n", arg);
	return 0;
}

int main(int argc, char **argv)
{
	const char *arg = argc > 1 ? argv[1] : 0;
	chan = korin_sys2(SYS_SVC_REGISTER, "usb", SVF_ENDS);
	if (chan == E_EXIST && arg && *arg)
		return hand_over(arg);
	if (chan < 0) {
		say("usbd : service « usb » refusé (%ld)\n", chan);
		return 1;
	}
	say("usbd : en service\n");
	if (add_controller(arg) < 0)
		return 1; /* (sur une erreur : init2 relance) */
	for (;;) {
		serve_reads();
		/* sondage : 2 ms ; interruptions : les ports au plus tard toutes les
		 * secondes (un changement de port lève aussi l'interruption) */
		int polling = xh_polling();
		long wait = polling ? 2 : 1000;
		long req = korin_sys3(SYS_SVC_NEXT, chan, msg, wait);
		if (req < 0)
			return 1;
		int irq = 0;
		if (req > 0) {
			if (M(0) == SVC_OP_SIGNAL)
				irq = 1;
			else
				request(req);
		}
		xh_poll(irq || polling);
		iso_service();
	}
}
