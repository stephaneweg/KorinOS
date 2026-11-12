/* usbnet - carte réseau USB CDC-ECM (classe 02h, sous-classe 06h), pilote lancé par
 * hwmanager (drivers.txt : usb:02/06) avec « <périphérique> <interface> »
 * (docs/RESEAU.md étape 1, docs/USB.md). Repris pas à pas de usbnet.asm (à côté),
 * en C pour le Pi et le PC, d'après CDC 1.2 / ECM 1.2 (pas de NCM ni de RNDIS :
 * décision du 12/11) :
 *  * configuration : interface de données (classe 0Ah) dans son réglage à deux
 *    extrémités bulk (SET_INTERFACE), filtre de paquets, MAC par la chaîne
 *    iMACAddress (12 chiffres hexadécimaux) ;
 *  * réception : lecture bulk IN sans attente de usbd (op BULK_NB : le canal reste
 *    armé, un NAK rend 0) ; émission : op BULK (et paquet vide quand la trame est
 *    un multiple de la taille de paquet) ;
 *  * interface « usb0 » de netd (priorité 10), anneau de trames (src/svc/netif).
 * Essayé avec QEMU -device usb-net (configuration 1 = ECM). */
#include "korinrt.h"
#include "../netif/netif.h"

#define USB_OP_DESCRIPTORS 1
#define USB_OP_CONTROL 2
#define USB_OP_BULK_BUFFER 5
#define USB_OP_BULK 6
#define USB_OP_BULK_NB 7

#define BUFFER 8192 /* tampon partagé avec usbd : RX +0, TX +4096 */
#define RX_MAX 2048 /* > 1536 : la trame arrive d'un bloc */
#define TX_OFF 4096
#define E_NODEV -19
#define E_BUSY -16

static long chan;
static u64 dev;
static u8 *buf;
static u64 mac;
static u32 ctrl_if, data_if, data_alt, mac_str;
static u32 in_ep, in_mps, out_ep, out_mps;
static u64 msg[64] __attribute__((aligned(16)));
#define MB ((u8 *)msg)

static long usb_call(void)
{
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

/* paquet SETUP (sans données en sortie ; longueur dans le paquet) -> octets ou < 0 ;
 * données reçues en msg + 48 */
static long control(u64 setup)
{
	msg[0] = USB_OP_CONTROL;
	msg[1] = dev;
	msg[2] = setup;
	msg[3] = setup >> 48;
	*(u32 *)(MB + MSG_LEN) = 0;
	return usb_call();
}

/* longueur du descripteur de configuration en msg + 48 -> 1 s'il contient une
 * interface ECM et une interface de données à deux extrémités bulk */
static int parse(u64 len)
{
	in_ep = out_ep = 0;
	mac_str = 0;
	if (len > 464)
		len = 464;
	const u8 *p = MB + 48, *end = p + len;
	int in_ctrl = 0, in_data = 0, ecm = 0; /* dans l'interface de contrôle ECM, de données */
	while (p + 2 <= end && p[0]) {
		if (p[1] == 4) { /* interface */
			in_ctrl = in_data = 0;
			if (p[5] == 2) { /* classe, sous-classe */
				if (p[6] == 6) {
					in_ctrl = ecm = 1;
					ctrl_if = p[2];
				}
			} else if (p[5] == 0x0A && p[4]) { /* données, avec des extrémités */
				in_data = 1;
				data_if = p[2];
				data_alt = p[3];
			}
		} else if (p[1] == 0x24) { /* descripteur fonctionnel CDC */
			if (in_ctrl && p[2] == 0x0F) /* Ethernet Networking */
				mac_str = p[3];
		} else if (p[1] == 5 && in_data && (p[3] & 3) == 2) { /* extrémité bulk */
			u32 mps = (u32)(p[4] | p[5] << 8) & 0x7FF;
			if (p[2] & 0x80) {
				in_ep = p[2];
				in_mps = mps;
			} else {
				out_ep = p[2];
				out_mps = mps;
			}
		}
		p += p[0];
	}
	return ecm && in_ep && out_ep;
}

/* -> 1 si le périphérique a une configuration ECM (choisie au besoin : QEMU usb-net
 * présente d'abord RNDIS, 02/02/FF, puis ECM) */
static int describe(void)
{
	msg[0] = USB_OP_DESCRIPTORS; /* la configuration choisie par usbd */
	msg[1] = dev;
	if (usb_call() < 0)
		return 0;
	if (parse(msg[2]))
		return 1;
	if (control(0x80 | 6 << 8 | 0x0100 << 16 | (u64)18 << 48) < 18) /* descripteur de périphérique */
		return 0;
	u32 n = MB[48 + 17]; /* bNumConfigurations */
	for (u32 i = 0; i < n; i++) {
		long r = control((0x80 | 6 << 8 | 0x0200 << 16 | (u64)464 << 48) | (u64)i << 16);
		if (r < 9 || !parse((u64)r))
			continue;
		/* SET_CONFIGURATION(bConfigurationValue) */
		if (control((u64)MB[48 + 5] << 16 | 9 << 8) < 0)
			return 0;
		ak_puts("usbnet : configuration ECM choisie\n");
		return 1;
	}
	return 0;
}

/* caractère -> 0..15, -1 si pas hexadécimal */
static int hex_digit(u32 c)
{
	if (c - '0' <= 9)
		return (int)(c - '0');
	c |= 0x20;
	if (c - 'a' > 5)
		return -1;
	return (int)(c - 'a' + 10);
}

/* SET_INTERFACE(données, réglage), SET_ETHERNET_PACKET_FILTER, MAC */
static void configure(void)
{
	control(0x01 | 11 << 8 | (u64)data_alt << 16 | (u64)data_if << 32);
	/* filtre : dirigé, diffusion, multidiffusion (échec toléré) */
	control(0x21 | 0x43 << 8 | 0x0E << 16 | (u64)ctrl_if << 32);
	if (mac_str && control((0x80 | 6 << 8 | (u64)0x0300 << 16 | (u64)0x0409 << 32 | (u64)64 << 48) |
			       (u64)mac_str << 16) >= 26) {
		const u8 *s = MB + 48 + 2; /* caractères UTF-16 */
		u8 *m = (u8 *)&mac;
		int i;
		for (i = 0; i < 6; i++, s += 4) {
			int hi = hex_digit((u32)(s[0] | s[1] << 8)), lo = hex_digit((u32)(s[2] | s[3] << 8));
			if (hi < 0 || lo < 0)
				break;
			m[i] = (u8)(lo | hi << 4);
		}
		if (i == 6)
			return;
	}
	mac = 0x00015042; /* adresse locale administrée 42:50:01:00:00:00 (les 8 octets) */
}

/* -> octets d'une trame reçue (au début du tampon), 0 : rien, < 0 */
static long rx(void)
{
	msg[0] = USB_OP_BULK_NB;
	msg[1] = dev;
	msg[2] = in_ep | 0x80;
	msg[3] = in_mps; /* c = taille max., d = décalage 0 */
	msg[4] = 0;
	msg[6] = RX_MAX;
	long r = usb_call();
	return r == E_BUSY ? 0 : r; /* E_BUSY (une autre carte) : rien */
}

/* octets (en TX_OFF) -> résultat */
static long bulk_out(u64 len)
{
	msg[0] = USB_OP_BULK;
	msg[6] = len;
	msg[1] = dev;
	msg[2] = out_ep;
	msg[3] = out_mps;
	msg[4] = TX_OFF;
	return usb_call();
}

/* octets à émettre (en TX_OFF du tampon) -> résultat */
static long tx(u64 len)
{
	long r = bulk_out(len);
	if (r < 0 || !out_mps || len % out_mps)
		return r;
	return bulk_out(0); /* multiple de la taille de paquet : paquet vide (fin de trame) */
}

static void print_mac(void)
{
	const u8 *m = (const u8 *)&mac;
	for (int i = 0; i < 6; i++) {
		char h[3];
		u32 hi = m[i] >> 4, lo = m[i] & 15;
		h[0] = (char)(hi < 10 ? '0' + hi : 'a' + hi - 10);
		h[1] = (char)(lo < 10 ? '0' + lo : 'a' + lo - 10);
		h[2] = 0;
		ak_puts(h);
		if (i < 5)
			ak_puts(":");
	}
	ak_puts("\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	if (!krt_line) /* « <périphérique> <interface> » (x0 de l'asm ; l'interface n'est pas lue) */
		return 1;
	dev = parse_dec(krt_line);
	chan = korin_sys2(SYS_SVC_OPEN, "usb", 10000); /* usbd */
	if (chan < 0)
		return 1;
	if (!describe()) { /* ECM ? extrémités, chaîne de la MAC */
		ak_puts("usbnet : pas une interface ECM\n");
		return 0; /* (pas de relance) */
	}
	configure(); /* SET_INTERFACE, filtre, MAC */
	msg[0] = USB_OP_BULK_BUFFER; /* le tampon partagé */
	msg[1] = BUFFER;
	long r = usb_call();
	if (r < 0)
		return 1;
	r = korin_sys1(SYS_BUF_MAP, r);
	if (r < 0)
		return 1;
	buf = (u8 *)r;
	ak_puts("usbnet : ECM pret, MAC ");
	print_mac();
	if (netif_register(NIF_USB, mac, NIF_PRIO_USB, "usb0") < 0) { /* interface usb0 de netd */
		ak_puts("usbnet : netd ne repond pas\n");
		return 1;
	}
	netif_link(1, 100, 0); /* lien (QEMU : toujours) */
	for (;;) {
		u64 work = 0; /* travail fait dans ce tour */
		for (int n = 8;;) { /* au plus 8 trames reçues par tour */
			r = rx();
			if (r <= 0) {
				if (r == E_NODEV) /* la carte est partie */
					goto gone;
				if (r == E_NOSYS) { /* contrôleur sans lecture sans attente */
					ak_puts("usbnet : port USB-A (xHCI) pas encore pris en charge : brancher sur "
						"l'USB-C\n");
					return 0;
				}
				break;
			}
			netif_rx_put(buf, (u64)r);
			work++;
			if (!--n)
				break;
		}
		for (;;) { /* trames à émettre */
			u64 len = netif_tx_get(buf + TX_OFF);
			if (!len)
				break;
			if (tx(len) == E_NODEV)
				goto gone;
			work++;
		}
		netif_tick();
		if (!work)
			korin_sys1(SYS_SLEEP, 1);
	}
gone:
	ak_puts("usbnet : carte retiree\n");
	return 0;
}
