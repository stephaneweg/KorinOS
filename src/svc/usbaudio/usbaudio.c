/* usbaudio - pilote des cartes son et casques USB Audio Class 1 (UAC1), docs/SON.md 5,
 * repris pas à pas de usbaudio.asm (à côté), en C pour le Pi et le PC. Lancé par
 * hwmanager pour l'interface AudioControl (drivers.txt : « usb:01/01 »,
 * arguments « <périphérique> <interface> »). Il s'arrête de lui-même quand la carte
 * est retirée (usbd ferme ses tuyaux : UI_STATE = 1, ou E_NODEV).
 *  * descripteurs (usbd, op 1) : interfaces AudioStreaming (classe 1, sous-classe
 *    2), format Type I PCM 16 bits, extrémité isochrone ; lecture = OUT stéréo,
 *    capture = IN mono ou stéréo ; fréquence : 44,1 kHz si la carte la propose,
 *    sinon 48 kHz, sinon la première ;
 *  * lecture : SET_INTERFACE, SET_CUR (fréquence), tuyau isochrone OUT d'usbd
 *    (op 8) ; inscrit auprès de sound comme sortie « USB : <produit> » à la
 *    fréquence de la carte (sound rééchantillonne) ; toutes les 2 ms, l'anneau
 *    d'usbd est gardé à ~24 ms d'avance depuis celui de sound ;
 *  * capture : inscrite comme entrée ; l'interface et le tuyau IN ne sont ouverts
 *    qu'à la première capture qui écoute (SR_WANTED), puis gardés ; mono ->
 *    stéréo, vers l'anneau d'entrée de sound.
 * Pas de retour de synchronisation, volume de la carte ignoré (le volume est dans
 * sound). Il faut l'isochrone d'usbd : usbd.asm l'a (Pi), usbd en C aussi depuis le
 * 13/11 (iso.c, xHCI : PC). */
#include "korinrt.h"
#include "../sndif/sndif.h"

#define USB_OP_DESCRIPTORS 1
#define USB_OP_CONTROL 2
#define USB_OP_ISO_OPEN 8
#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_SET_INTERFACE 11
#define UI_HEAD 0 /* anneau d'un tuyau isochrone (src/inc/usb.inc) */
#define UI_TAIL 8
#define UI_STATE 32
#define UI_DATA 4096
#define UI_BYTES 32768
#define LEAD_MS 24 /* avance de l'anneau isochrone */
#define THREAD_PRIO_HIGH 1

static struct {
	u32 out_found, out_if, out_alt, out_ep, out_mps, out_rate;
	u32 cap_found, cap_if, cap_alt, cap_ep, cap_mps, cap_rate, cap_ch;
	u32 fmt_ok, fmt_ch, fmt_rate; /* interface en cours d'analyse : format valide, canaux, fréquence */
	u32 cur_if, cur_alt, cur_as;  /* 1 : interface AudioStreaming */
} ua;

static long chan;
static u64 dev, vidpid, lead;
static u8 *out_ring, *cap_ring; /* anneaux isochrones d'usbd (0 : aucun) */
static char name[64];
static u64 msg[64] __attribute__((aligned(16)));
static u8 cfg[512];
static u32 pcm[1024];
#define MB ((u8 *)msg)
#define R64(r, off) (*(volatile u64 *)((r) + (off)))

static long svc_call(void)
{
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

/* SETUP, octets de données (dans msg + 48) -> résultat */
static long usb_ctrl(u64 setup, u64 len)
{
	msg[0] = USB_OP_CONTROL;
	msg[1] = dev;
	msg[2] = setup;
	msg[3] = len;
	*(u32 *)(MB + MSG_LEN) = 0;
	return svc_call();
}

static long set_interface(u32 alt, u32 itf)
{
	return usb_ctrl(0x01 | USB_REQ_SET_INTERFACE << 8 | (u64)alt << 16 | (u64)itf << 32, 0);
}

/* extrémité (bit 7 : IN), fréquence : SET_CUR(SAMPLING_FREQ), 3 o */
static long set_rate(u32 ep, u32 rate)
{
	MB[48] = (u8)rate;
	MB[49] = (u8)(rate >> 8);
	MB[50] = (u8)(rate >> 16);
	return usb_ctrl((0x22 | 1 << 8 | 0x0100 << 16 | (u64)3 << 48) | (u64)ep << 32, 3);
}

/* extrémité, taille max., octets par ms (16.16) | octets par trame << 32 -> anneau
 * isochrone mappé, < 0 si refusé */
static long iso_open(u32 ep, u32 mps, u64 rate)
{
	msg[0] = USB_OP_ISO_OPEN;
	msg[1] = dev;
	msg[2] = ep;
	msg[3] = mps;
	msg[4] = rate;
	*(u32 *)(MB + MSG_LEN) = 0;
	long r = svc_call();
	if (r < 0)
		return r;
	return korin_sys1(SYS_BUF_MAP, msg[2]);
}

/* lecture : l'anneau isochrone garde « lead » octets d'avance (depuis sound) -> 0,
 * < 0 si la carte est partie */
static long feed_out(void)
{
	u8 *r = out_ring;
	if (!r)
		return 0;
	if (R64(r, UI_STATE))
		return -1;
	u64 head = R64(r, UI_HEAD);
	u64 ahead = head - __atomic_load_n((u64 *)(r + UI_TAIL), __ATOMIC_ACQUIRE); /* déjà en avance */
	i64 want = (i64)(lead - ahead);
	if (want <= 0)
		return 0;
	u64 n = (u64)want >> 2; /* trames à prendre : 256 au moins d'un coup, 1024 au plus */
	if (n <= 256)
		n = 256;
	if (n > 1024)
		n = 1024;
	sndif_read(pcm, n); /* (silence s'il manque des trames) */
	for (u64 i = 0; i < n; i++, head += 4)
		*(volatile u32 *)(r + UI_DATA + (head & (UI_BYTES - 1))) = pcm[i];
	__atomic_store_n((u64 *)(r + UI_HEAD), head, __ATOMIC_RELEASE);
	return 0;
}

/* capture : ouverte à la première écoute ; ce que la carte envoie -> sound ; -> 0,
 * < 0 si la carte est partie */
static long feed_cap(void)
{
	if (!ua.cap_found)
		return 0;
	u8 *r = cap_ring;
	if (!r) {
		if (!sndif_wanted()) /* personne n'écoute : rien */
			return 0;
		if (set_interface(ua.cap_alt, ua.cap_if) < 0)
			return -1;
		set_rate(ua.cap_ep | 0x80, ua.cap_rate);
		/* octets par trame << 32 (octets par ms : 0, comme l'asm) */
		long v = iso_open(ua.cap_ep | 0x80, ua.cap_mps, (u64)ua.cap_ch << 33);
		if (v < 0) {
			ua.cap_found = 0; /* tuyau refusé : plus de capture */
			return 0;
		}
		cap_ring = (u8 *)v;
		ak_puts("usbaudio : capture ouverte\n");
		return 0;
	}
	if (R64(r, UI_STATE))
		return -1;
	u64 head = __atomic_load_n((u64 *)(r + UI_HEAD), __ATOMIC_ACQUIRE), tail = R64(r, UI_TAIL);
	u64 bpf = (u64)ua.cap_ch << 1; /* octets par trame */
	u64 n = (head - tail) / bpf;
	if (n > 1024)
		n = 1024;
	if (!n)
		return 0;
	const volatile u8 *d = r + UI_DATA;
	for (u64 i = 0; i < n; i++) {
		/* s16, octets un à un : l'anneau n'est pas aligné sur 4 */
		u32 left = d[tail & (UI_BYTES - 1)];
		tail++;
		left |= (u32)d[tail & (UI_BYTES - 1)] << 8;
		tail++;
		u32 right = left; /* mono : droite = gauche */
		if (bpf != 2) {
			right = d[tail & (UI_BYTES - 1)];
			tail++;
			right |= (u32)d[tail & (UI_BYTES - 1)] << 8;
			tail++;
		}
		pcm[i] = (left & 0xFFFF) | right << 16;
	}
	__atomic_store_n((u64 *)(r + UI_TAIL), tail, __ATOMIC_RELEASE);
	sndif_write(pcm, n);
	return 0;
}

/* le nom de la carte : « USB : <iProduct> » (descripteur de chaîne, UTF-16 ->
 * ASCII), « USB : carte son » à défaut */
static void product_name(void)
{
	strcpy(name, "USB : ");
	char *o = name + 6; /* fin du préfixe */
	if (usb_ctrl(0x80 | USB_REQ_GET_DESCRIPTOR << 8 | 0x0100 << 16 | (u64)18 << 48, 18) >= 18) {
		u32 ip = MB[48 + 15]; /* iProduct */
		long r;
		if (ip && (r = usb_ctrl((0x80 | USB_REQ_GET_DESCRIPTOR << 8 | 0x0300 << 16 | (u64)0x0409 << 32 |
					 (u64)255 << 48) | (u64)ip << 16,
					255)) >= 4) {
			const u8 *s = MB + 48;
			u64 len = s[0] <= (u64)r ? s[0] : (u64)r; /* bLength */
			u64 k = 0;
			for (u64 i = 2; i + 1 < len && k < 40; i += 2) {
				u32 c = (u32)(s[i] | s[i + 1] << 8);
				if (c > 0x7F) /* hors ASCII : « ? » */
					c = '?';
				if (c < ' ')
					c = '?';
				o[k++] = (char)c;
			}
			o[k] = 0;
			if (k)
				return;
		}
	}
	strcpy(o, "carte son");
}

/* taille de la configuration (cfg) : interfaces de lecture et de capture */
static void uac_parse(u64 len)
{
	const u8 *p = cfg, *end = cfg + len;
	while (p < end && p[0]) {
		if (p[1] == 4) { /* interface */
			ua.fmt_ok = ua.cur_as = 0;
			ua.cur_if = p[2];
			ua.cur_alt = p[3];
			if (p[5] == 1 && p[6] == 2) /* audio, AudioStreaming */
				ua.cur_as = 1;
		} else if (p[1] == 0x24) { /* descripteur d'interface de classe */
			/* FORMAT_TYPE, type I, 2 octets par sous-trame, 16 bits, 1 ou 2 canaux */
			if (ua.cur_as && p[2] == 2 && p[3] == 1 && p[5] == 2 && p[6] == 16 && p[4] >= 1 && p[4] <= 2) {
				ua.fmt_ch = p[4];
				u32 nf = p[7]; /* fréquences (0 : plage) */
				const u8 *q = p + 8;
				u32 rate;
				if (nf) {
					int s441 = 0, s48 = 0;
					u32 first = 0;
					for (; nf; nf--, q += 3) {
						u32 f = (u32)(q[0] | q[1] << 8 | q[2] << 16);
						if (!first)
							first = f;
						s441 |= f == 44100;
						s48 |= f == 48000;
					}
					rate = s441 ? 44100 : s48 ? 48000 : first;
				} else { /* plage continue : 44,1 kHz si incluse */
					u32 f = (u32)(q[0] | q[1] << 8 | q[2] << 16);
					rate = f <= 44100 ? 44100 : f;
				}
				ua.fmt_rate = rate;
				ua.fmt_ok = 1;
			}
		} else if (p[1] == 5 && ua.fmt_ok && (p[3] & 3) == 1) { /* extrémité isochrone */
			u32 addr = p[2], mps = (u32)(p[4] | p[5] << 8) & 0x7FF;
			if (addr & 0x80) {
				if (!ua.cap_found) {
					ua.cap_ep = addr & 0xF;
					ua.cap_mps = mps;
					ua.cap_if = ua.cur_if;
					ua.cap_alt = ua.cur_alt;
					ua.cap_rate = ua.fmt_rate;
					ua.cap_ch = ua.fmt_ch;
					ua.cap_found = 1;
				}
			} else if (!ua.out_found && ua.fmt_ch == 2) { /* lecture : stéréo seulement */
				ua.out_ep = addr & 0xF;
				ua.out_mps = mps;
				ua.out_if = ua.cur_if;
				ua.out_alt = ua.cur_alt;
				ua.out_rate = ua.fmt_rate;
				ua.out_found = 1;
			}
		}
		p += p[0];
	}
}

static void say_rate(const char *what, u32 rate)
{
	ak_puts(what);
	print_dec(rate);
	ak_puts(" Hz\n");
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	korin_sys2(SYS_THREAD_SET_PRIORITY, 0, THREAD_PRIO_HIGH); /* remplissage des trames : priorité haute */
	if (!krt_line) /* « périphérique interface » ; (code 0 : pas de relance) */
		return 0;
	dev = parse_dec(krt_line);
	if (!dev)
		return 0;
	chan = korin_sys2(SYS_SVC_OPEN, "usb", 5000);
	if (chan < 0)
		return 0;
	msg[0] = USB_OP_DESCRIPTORS; /* descripteur de configuration */
	msg[1] = dev;
	*(u32 *)(MB + MSG_LEN) = 0;
	if (svc_call() < 0)
		return 0;
	u64 len = msg[2] > 464 ? 464 : msg[2];
	memcpy(cfg, MB + MSG_DATA, len);
	vidpid = msg[3];
	uac_parse(len);
	if (!ua.out_found && !ua.cap_found) {
		ak_puts("usbaudio : pas de flux PCM 16 bits (UAC1) : rien \xC3\xA0 faire\n");
		return 0;
	}
	product_name(); /* « USB : <produit> » */
	ak_puts("usbaudio : ");
	ak_puts(name);
	ak_puts("\n");
	const char *bye;
	if (ua.out_found) { /* lecture : interface, fréquence */
		if (set_interface(ua.out_alt, ua.out_if) < 0)
			goto gone;
		set_rate(ua.out_ep, ua.out_rate); /* (refus toléré : fréquence unique) */
		/* octets par ms (16.16) : fréquence x 4 | octets par trame << 32 */
		long v = iso_open(ua.out_ep, ua.out_mps, ((u64)ua.out_rate << 18) / 1000 | (u64)4 << 32);
		if (v < 0) {
			bye = "usbaudio : tuyau isochrone refus\xC3\xA9 (usbd)\n";
			goto leave;
		}
		out_ring = (u8 *)v;
		if (sndif_output(SND_OUT_USB, ua.out_rate, 0, name) < 0) { /* sortie de sound */
			bye = "usbaudio : le service sound ne r\xC3\xA9pond pas\n";
			goto leave;
		}
		say_rate("usbaudio : lecture, ", ua.out_rate);
		lead = ((u64)ua.out_rate * (LEAD_MS * 4) / 1000) & ~(u64)3; /* avance en octets */
	}
	if (ua.cap_found && sndif_input(SND_IN_USB, ua.cap_rate, 0, name) >= 0) /* micro de la carte */
		say_rate("usbaudio : micro, ", ua.cap_rate);
	for (;;) {
		if (feed_out() < 0 || feed_cap() < 0)
			goto gone;
		sndif_tick();
		korin_sys1(SYS_SLEEP, 2);
	}
gone:
	bye = "usbaudio : carte retir\xC3\xA9"
	      "e\n";
leave:
	ak_puts(bye);
	return 0;
}
