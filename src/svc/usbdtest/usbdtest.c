/* usbdtest - essai du service « usb » (usbd) par son protocole (src/inc/usb.inc),
 * sans pilote de classe : marche avec usbd.asm (Pi) comme avec usbd en C (PC).
 *
 *	usbdtest            les périphériques, leurs interfaces et extrémités
 *	usbdtest kbd [N]    clavier (HID boot) : 8 rapports au plus, 30 s au plus ;
 *	                    attend qu'un clavier apparaisse (branchement à chaud)
 *	usbdtest disk [N]   clé (Bulk-Only, SCSI) : INQUIRY, READ CAPACITY, blocs 0 et 1
 *	usbdtest net [N]    carte réseau : descripteurs, une lecture bulk sans attente
 * Sortie standard (la console du noyau sans vfs). Code : 0, ou 1 sur un échec. */
#include "korinrt.h"

#define USB_OP_DESCRIPTORS 1
#define USB_OP_CONTROL 2
#define USB_OP_INTR_OPEN 3
#define USB_OP_INTR_READ 4
#define USB_OP_BULK_BUFFER 5
#define USB_OP_BULK 6
#define USB_OP_BULK_NB 7

static long chan;
static uint8_t msg[512] __attribute__((aligned(16)));
static uint8_t cfg[464];
static unsigned cfg_len;
static uint32_t vidpid;
static long speed;

static void say(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof buf - 1)
		n = sizeof buf - 1;
	sys_write(1, buf, (u64)n); /* sortie standard (la console sans vfs) */
}

static long call(uint64_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	uint64_t *m = (uint64_t *)msg;
	m[0] = op;
	m[1] = a;
	m[2] = b;
	m[3] = c;
	m[4] = d;
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

static long descriptors(long dev)
{
	long r = call(USB_OP_DESCRIPTORS, (uint64_t)dev, 0, 0, 0);
	if (r < 0)
		return r;
	uint64_t *m = (uint64_t *)msg;
	cfg_len = m[2] > sizeof cfg ? sizeof cfg : (unsigned)m[2];
	memcpy(cfg, msg + 48, cfg_len);
	vidpid = (uint32_t)m[3];
	speed = (long)m[4];
	return 0;
}

/* l'interface de cette classe (masque sur classe << 16 | sous-classe << 8 |
 * protocole) -> son numéro, et ses extrémités IN et OUT (bulk ou interruption) */
static int find_itf(uint32_t cls, uint32_t mask, unsigned *in_ep, unsigned *in_mps, unsigned *out_ep,
		    unsigned *out_mps, unsigned *interval)
{
	int itf = -1;
	for (unsigned o = 0; o + 2 <= cfg_len && cfg[o] >= 2; o += cfg[o]) {
		const uint8_t *x = cfg + o;
		if (x[1] == 4) {
			if (itf >= 0)
				break; /* l'interface suivante */
			uint32_t c = (uint32_t)x[5] << 16 | (uint32_t)x[6] << 8 | x[7];
			if (x[3] == 0 && (c & mask) == (cls & mask))
				itf = x[2];
		} else if (x[1] == 5 && itf >= 0) {
			unsigned mps = x[4] | x[5] << 8;
			if (x[2] & 0x80) {
				*in_ep = x[2];
				*in_mps = mps;
				if (interval)
					*interval = x[6];
			} else if (out_ep) {
				*out_ep = x[2];
				*out_mps = mps;
			}
		}
	}
	return itf;
}

static void list(void)
{
	int n = 0;
	for (long d = 1; d <= 16; d++) {
		if (descriptors(d) < 0)
			continue;
		n++;
		say("usbdtest : périphérique %ld : %04x:%04x, vitesse %ld, configuration de %u o\n", d,
		    vidpid >> 16, vidpid & 0xFFFF, speed, cfg_len);
		for (unsigned o = 0; o + 2 <= cfg_len && cfg[o] >= 2; o += cfg[o]) {
			const uint8_t *x = cfg + o;
			if (x[1] == 4)
				say("usbdtest :   interface %u (réglage %u) : %02x/%02x/%02x, %u extrémité(s)\n",
				    x[2], x[3], x[5], x[6], x[7], x[4]);
			else if (x[1] == 5)
				say("usbdtest :     extrémité %02x : %s, %u o, intervalle %u\n", x[2],
				    (const char *[]){"contrôle", "isochrone", "bulk", "interruption"}[x[3] & 3],
				    x[4] | x[5] << 8, x[6]);
		}
	}
	say("usbdtest : %d périphérique(s)\n", n);
}

/* le premier périphérique qui a cette interface (ou dev) ; attente en ms */
static long find_dev(long dev, uint32_t cls, uint32_t mask, long wait_ms)
{
	long end = korin_sys0(SYS_TIME_MS) + wait_ms;
	unsigned a, b;
	do {
		for (long d = dev ? dev : 1; d <= (dev ? dev : 16); d++)
			if (descriptors(d) == 0 && find_itf(cls, mask, &a, &b, 0, 0, 0) >= 0)
				return d;
		korin_sys1(SYS_SLEEP, 200);
	} while (korin_sys0(SYS_TIME_MS) < end);
	return -1;
}

static int kbd(long dev)
{
	dev = find_dev(dev, 0x030101, 0xFFFFFF, 30000);
	if (dev < 0) {
		say("usbdtest : pas de clavier\n");
		return 1;
	}
	unsigned ep = 0, mps = 0, iv = 10;
	int itf = find_itf(0x030101, 0xFFFFFF, &ep, &mps, 0, 0, &iv);
	say("usbdtest : clavier : périphérique %ld, interface %d, extrémité %02x (%u o)\n", dev, itf, ep, mps);
	/* SET_PROTOCOL(boot), SET_IDLE(0) */
	long r = call(USB_OP_CONTROL, (uint64_t)dev, 0x0B21 | (uint64_t)itf << 32, 0, 0);
	say("usbdtest : SET_PROTOCOL : %ld\n", r);
	r = call(USB_OP_CONTROL, (uint64_t)dev, 0x0A21 | (uint64_t)itf << 32, 0, 0);
	say("usbdtest : SET_IDLE : %ld\n", r);
	long pipe = call(USB_OP_INTR_OPEN, (uint64_t)dev, ep, mps, iv);
	if (pipe < 0) {
		say("usbdtest : tuyau refusé (%ld)\n", pipe);
		return 1;
	}
	say("usbdtest : tuyau %ld, j'attends les touches\n", pipe);
	for (int i = 0; i < 8; i++) {
		r = call(USB_OP_INTR_READ, (uint64_t)pipe, 0, 0, 0);
		if (r < 0) {
			say("usbdtest : lecture : %ld\n", r);
			return 1;
		}
		char hex[64], *h = hex;
		for (long k = 0; k < r && k < 16; k++)
			h += snprintf(h, 4, " %02x", msg[48 + k]);
		*h = 0;
		say("usbdtest : rapport de %ld o :%s\n", r, hex);
	}
	return 0;
}

static uint8_t *bbuf;
static unsigned in_ep, in_mps, out_ep, out_mps;
static long ddev;
static uint32_t tag = 1;

static long bulk(unsigned ep, unsigned mps, unsigned off, unsigned len)
{
	*(uint64_t *)(msg + 48) = len;
	return call(USB_OP_BULK, (uint64_t)ddev, ep, mps, off);
}

/* une commande SCSI par Bulk-Only : CBW (31 o), données IN (à +512), CSW */
static long scsi(const uint8_t *cdb, unsigned cdb_len, unsigned len)
{
	uint8_t *c = bbuf;
	memset(c, 0, 31);
	c[0] = 'U';
	c[1] = 'S';
	c[2] = 'B';
	c[3] = 'C';
	memcpy(c + 4, &tag, 4);
	memcpy(c + 8, &len, 4);
	c[12] = 0x80;
	c[14] = (uint8_t)cdb_len;
	memcpy(c + 15, cdb, cdb_len);
	long r = bulk(out_ep, out_mps, 0, 31);
	if (r != 31)
		return r < 0 ? r : -5;
	long got = 0;
	if (len) {
		got = bulk(in_ep, in_mps, 512, len);
		if (got < 0)
			return got;
	}
	r = bulk(in_ep, in_mps, 256, 13);
	if (r != 13 || memcmp(bbuf + 256, "USBS", 4))
		return r < 0 ? r : -5;
	uint32_t t;
	memcpy(&t, bbuf + 256 + 4, 4);
	uint8_t status = bbuf[256 + 12];
	tag++;
	if (t != tag - 1 || status)
		return -100 - status;
	return got;
}

static void hexdump(const uint8_t *p, unsigned n)
{
	for (unsigned o = 0; o < n; o += 16) {
		char line[80], *h = line;
		h += snprintf(h, 8, "%04x ", o);
		for (unsigned k = 0; k < 16 && o + k < n; k++)
			h += snprintf(h, 4, " %02x", p[o + k]);
		*h = 0;
		say("usbdtest :   %s\n", line);
	}
}

static int disk(long dev)
{
	ddev = find_dev(dev, 0x080650, 0xFFFFFF, 10000);
	if (ddev < 0) {
		say("usbdtest : pas de clé\n");
		return 1;
	}
	int itf = find_itf(0x080650, 0xFFFFFF, &in_ep, &in_mps, &out_ep, &out_mps, 0);
	say("usbdtest : clé : périphérique %ld, interface %d, IN %02x (%u o), OUT %02x (%u o)\n", ddev, itf,
	    in_ep, in_mps, out_ep, out_mps);
	long num = call(USB_OP_BULK_BUFFER, 8192, 0, 0, 0);
	if (num < 0) {
		say("usbdtest : tampon refusé (%ld)\n", num);
		return 1;
	}
	long va = korin_sys1(SYS_BUF_MAP, num);
	if (va < 0) {
		say("usbdtest : BUF_MAP : %ld\n", va);
		return 1;
	}
	bbuf = (uint8_t *)va;
	uint8_t inq[6] = {0x12, 0, 0, 0, 36, 0};
	long r = scsi(inq, 6, 36);
	if (r < 0) {
		say("usbdtest : INQUIRY : %ld\n", r);
		return 1;
	}
	char vendor[9], product[17];
	memcpy(vendor, bbuf + 512 + 8, 8);
	vendor[8] = 0;
	memcpy(product, bbuf + 512 + 16, 16);
	product[16] = 0;
	say("usbdtest : INQUIRY : type %u, « %s » « %s »\n", bbuf[512] & 31, vendor, product);
	uint8_t tur[6] = {0};
	for (int i = 0; i < 5 && (r = scsi(tur, 6, 0)) < 0; i++)
		korin_sys1(SYS_SLEEP, 100);
	say("usbdtest : TEST UNIT READY : %ld\n", r);
	uint8_t cap[10] = {0x25};
	r = scsi(cap, 10, 8);
	if (r < 8) {
		say("usbdtest : READ CAPACITY : %ld\n", r);
		return 1;
	}
	uint8_t *p = bbuf + 512;
	uint32_t last = (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
	uint32_t bs = (uint32_t)p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7];
	say("usbdtest : READ CAPACITY : %u blocs de %u o (%u Ko)\n", last + 1, bs,
	    (unsigned)(((uint64_t)(last + 1) * bs) >> 10));
	for (uint32_t lba = 0; lba < 2; lba++) {
		uint8_t rd[10] = {0x28, 0, (uint8_t)(lba >> 24), (uint8_t)(lba >> 16), (uint8_t)(lba >> 8),
				  (uint8_t)lba, 0, 0, 1, 0};
		r = scsi(rd, 10, bs);
		if (r < 0) {
			say("usbdtest : READ(10) bloc %u : %ld\n", lba, r);
			return 1;
		}
		say("usbdtest : bloc %u (%ld o) :\n", lba, r);
		hexdump(bbuf + 512, 64);
		if (lba == 0)
			say("usbdtest :   fin du bloc : %02x %02x\n", bbuf[512 + 510], bbuf[512 + 511]);
	}
	return 0;
}

static int net(long dev)
{
	long d = find_dev(dev, 0x020000, 0xFF0000, 10000);
	if (d < 0) {
		say("usbdtest : pas de carte réseau\n");
		return 1;
	}
	unsigned ie = 0, im = 0, oe = 0, om = 0;
	/* l'interface de données (0a/00/00) porte les extrémités bulk */
	int itf = find_itf(0x0A0000, 0xFF0000, &ie, &im, &oe, &om, 0);
	say("usbdtest : réseau : périphérique %ld, données interface %d, IN %02x, OUT %02x\n", d, itf, ie, oe);
	return 0;
}

int main(int argc, char **argv)
{
	chan = korin_sys2(SYS_SVC_OPEN, "usb", 10000);
	if (chan < 0) {
		say("usbdtest : service « usb » absent (%ld)\n", chan);
		return 1;
	}
	long dev = argc > 2 ? (long)parse_dec(argv[2]) : 0;
	if (argc > 1 && !strcmp(argv[1], "kbd"))
		return kbd(dev);
	if (argc > 1 && !strcmp(argv[1], "disk"))
		return disk(dev);
	if (argc > 1 && !strcmp(argv[1], "net"))
		return net(dev);
	list();
	return 0;
}
