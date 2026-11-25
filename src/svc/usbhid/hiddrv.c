/* usbhid/hiddrv.c - corps commun des pilotes HID USB (usbkeyboard, usbmouse,
 * usbgamepad ; docs/USB.md), repris pas à pas de src/svc/inc/hiddrv.inc.
 *
 * Lancé par hwmanager avec « <périphérique> <interface> » : lit les descripteurs
 * (usbd), le descripteur de rapport (hidparse.c), règle l'interface
 * (SET_PROTOCOL boot à défaut, SET_IDLE), ouvre son extrémité d'interruption et
 * traduit chaque rapport en événements, déposés dans l'anneau que hidmanager lui
 * donne (HID_OP_PILOTE). Une interface composite (clavier + souris + multimédia,
 * rapports numérotés) est traduite en entier, quel que soit le pilote.
 * usbgamepad (pad = 1) essaie d'abord la lecture des manettes (hidpad.c). */
#include "hid.h"

/* service « usb » (src/inc/usb.inc) */
#define USB_OP_DESCRIPTORS 1
#define USB_OP_CONTROL 2
#define USB_OP_INTR_OPEN 3
#define USB_OP_INTR_READ 4
#define USB_HS 2
#define USB_DT_DEVICE 1
#define USB_DT_INTERFACE 4
#define USB_DT_ENDPOINT 5
#define USB_DT_HID 0x21
#define USB_DT_REPORT 0x22
#define USB_REQ_GET_DESCRIPTOR 6
#define HID_REQ_SET_IDLE 10
#define HID_REQ_SET_PROTOCOL 11

#define HID_OP_PILOTE 3 /* service « hid » (hidmanager) */
#define RING_EV 64      /* anneau : +0 tête (ici), +8 queue (hidmanager), +64 les événements */
#define RING_N 64
#define E_NODEV -19

/* paquet SETUP dans un mot (usb_setup de usb.inc) */
#define SETUP(type, req, value, index, len)                                                     \
	((u64)(type) | (u64)(req) << 8 | (u64)(value) << 16 | (u64)(index) << 32 | (u64)(len) << 48)

static const char *drv_name;
static int drv_pad;
static long chan = -1;
static u64 dev, iface, speed, protocol, interval = 8;
static volatile u64 *ring;
static struct hid_fn hf;
/* message ; 16 octets de plus : hid_parse lit jusqu'à 4 octets après le descripteur */
static u8 msg[512 + 16] __attribute__((aligned(16)));
#define M(i) (((u64 *)msg)[i])

void hid_push(u64 type, u64 a, u64 b, u64 c)
{
	volatile u64 *r = ring;
	u64 head = r[0], tail = r[1];
	if (head - tail >= RING_N)
		return; /* plein : perdu */
	volatile u64 *e = r + (RING_EV + (head & (RING_N - 1)) * 32) / 8;
	e[0] = type;
	e[1] = a;
	e[2] = b;
	e[3] = c;
	__atomic_store_n(&r[0], head + 1, __ATOMIC_RELEASE); /* l'événement avant la tête */
}

static void puts_dec(u64 v)
{
	print_dec((long)v);
}

/* descripteurs de usbd -> hf (interface, extrémité IN d'interruption, longueur du
 * descripteur de rapport), intervalle -> 1 si l'interface a son extrémité */
static int describe(void)
{
	memset(&hf, 0, sizeof hf);
	M(0) = USB_OP_DESCRIPTORS;
	M(1) = dev;
	if (korin_sys2(SYS_SVC_CALL, chan, msg) < 0)
		return 0;
	speed = M(4);
	const u8 *p = msg + 48, *end = p + M(2);
	int inside = 0; /* 1 : dans notre interface */
	while (p + 2 <= end && p[0] >= 2) {
		if (p[1] == USB_DT_INTERFACE) {
			inside = 0;
			if (p[2] == iface && p[3] == 0) { /* bInterfaceNumber, réglage 0 */
				inside = 1;
				hf.iface = (i32)iface;
				hf.subclass = p[6];
				protocol = p[7];
			}
		} else if (inside && p[1] == USB_DT_HID) {
			hf.rdlen = (u32)(p[7] | p[8] << 8); /* longueur du descripteur de rapport */
		} else if (inside && p[1] == USB_DT_ENDPOINT && (p[2] & 0x80) && (p[3] & 3) == 3) {
			hf.ep = p[2] & 0xF; /* IN, interruption */
			u32 mps = (u32)(p[4] | p[5] << 8) & 0x7FF;
			hf.mps = (i32)(mps > 64 ? 64 : mps);
			u32 iv = p[6]; /* bInterval ; haute vitesse : 2^(n-1) micro-trames */
			if (speed >= USB_HS)
				iv = (1u << ((iv - 1) & 31)) >> 3;
			interval = iv < 1 ? 1 : iv;
			return 1;
		}
		p += p[0];
	}
	return 0;
}

/* SETUP, longueur -> octets (données en msg + 48), < 0 si erreur */
static long control(u64 setup, u64 len)
{
	M(0) = USB_OP_CONTROL;
	M(1) = dev;
	M(2) = setup;
	M(3) = len;
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

/* le descripteur de l'appareil -> pd_vidpid (le mapping du modèle, chez hidmanager) */
static void pad_vidpid(void)
{
	if (control(SETUP(0x80, USB_REQ_GET_DESCRIPTOR, USB_DT_DEVICE << 8, 0, 18), 18) < 12)
		return;
	u32 vid = (u32)(msg[48 + 8] | msg[48 + 9] << 8), pid = (u32)(msg[48 + 10] | msg[48 + 11] << 8);
	pd_vidpid = pid | vid << 16;
}

/* descripteur de rapport (hid_parse), sinon protocole « boot » ; SET_IDLE(0) ;
 * hf.kind : 0 si inutilisable */
static void setup_iface(void)
{
	u64 itf = (u64)(u32)hf.iface;
	if (hf.rdlen) {
		if (hf.rdlen > 464)
			hf.rdlen = 464;
		long r = control(SETUP(0x81, USB_REQ_GET_DESCRIPTOR, USB_DT_REPORT << 8, 0, 0) | itf << 32 |
					 (u64)hf.rdlen << 48,
				 hf.rdlen);
		if (r >= 2) {
			if (drv_pad && pad_parse(msg + 48, (u32)r)) {
				hf.kind = HID_PAD;
				pad_vidpid();
				goto idle;
			}
			hid_parse(&hf, msg + 48, (u32)r);
			if (hf.has) {
				hf.kind = HID_REPORT; /* décodé d'après le descripteur */
				goto idle;
			}
		}
	}
	/* protocole boot : 1 clavier, 2 souris, sinon pointeur absolu (tablette QEMU) */
	hf.kind = protocol == 1 ? HID_KBD : protocol == 2 ? HID_MOUSE : HID_TABLET;
	if (hf.subclass == 1)
		control(SETUP(0x21, HID_REQ_SET_PROTOCOL, 0, 0, 0) | itf << 32, 0); /* SET_PROTOCOL(boot) */
idle:
	control(SETUP(0x21, HID_REQ_SET_IDLE, 0, 0, 0) | itf << 32, 0); /* rapport sur changement */
}

/* « usbkeyboard : peripherique 1 interface 0 : clavier » */
static void announce(void)
{
	static const char *const kinds[] = {"inutilisable", "clavier (boot)", "souris (boot)", "tablette"};
	static const char *const has[] = {"?", "clavier", "souris", "clavier, souris"};
	ak_puts(drv_name);
	ak_puts(" : peripherique ");
	puts_dec(dev);
	ak_puts(" interface ");
	puts_dec(iface);
	ak_puts(" : ");
	if (hf.kind == HID_PAD)
		ak_puts("manette");
	else if (hf.kind == HID_REPORT)
		ak_puts(has[hf.has & 3]);
	else
		ak_puts(kinds[hf.kind & 3]);
	ak_puts("\n");
}

int hid_driver_run(const char *name, int pad)
{
	drv_name = name;
	drv_pad = pad;
	const char *s = krt_line; /* « <périphérique> <interface> » (x0 de l'asm) */
	if (!s)
		return 1;
	dev = parse_dec(s);
	for (;;) { /* l'interface, après l'espace */
		char c = *s++;
		if (!c)
			return 1;
		if (c == ' ')
			break;
	}
	iface = parse_dec(s);
	chan = korin_sys2(SYS_SVC_OPEN, "usb", 10000); /* usbd */
	if (chan < 0)
		return 1;
	if (!describe())
		goto useless;
	setup_iface();
	if (!hf.kind)
		goto useless;
	long hid = korin_sys2(SYS_SVC_OPEN, "hid", 10000); /* l'anneau vers hidmanager */
	if (hid < 0)
		return 1;
	M(0) = HID_OP_PILOTE;
	long r = korin_sys2(SYS_SVC_CALL, hid, msg);
	if (r < 0)
		return 1;
	r = korin_sys1(SYS_BUF_MAP, r);
	if (r < 0)
		return 1;
	ring = (volatile u64 *)r;
	M(0) = USB_OP_INTR_OPEN; /* l'extrémité d'interruption */
	M(1) = dev;
	M(2) = (u64)(u32)hf.ep | 0x80;
	M(3) = (u64)(u32)hf.mps;
	M(4) = interval;
	long pipe = korin_sys2(SYS_SVC_CALL, chan, msg);
	if (pipe < 0)
		return 1;
	announce();
	for (;;) {
		M(0) = USB_OP_INTR_READ; /* un rapport (réponse différée) */
		M(1) = (u64)pipe;
		r = korin_sys2(SYS_SVC_CALL, chan, msg);
		if (r == E_NODEV || r == E_INVAL) /* périphérique retiré, ou tuyau perdu */
			return 0;                 /* (usbd relancé) : fini, sans relance */
		if (r <= 0)
			continue; /* (attente trop longue : on redemande) */
		const u8 *d = msg + 48;
		hid_rep_len = (u64)r < sizeof msg - 48 ? (u32)r : (u32)(sizeof msg - 48);
		switch (hf.kind) {
		case HID_REPORT:
			usb_report_parsed(&hf, d);
			break;
		case HID_PAD:
			pad_report(d);
			break;
		case HID_KBD:
			usb_report_kbd(&hf, d);
			break;
		case HID_MOUSE:
			usb_report_mouse(d);
			break;
		default:
			usb_report_tablet(d);
		}
	}
useless:
	ak_puts(drv_name);
	ak_puts(" : interface sans rapport utilisable\n");
	return 0;
}
