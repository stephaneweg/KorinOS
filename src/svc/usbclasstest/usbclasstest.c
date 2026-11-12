/* usbclasstest - essai des pilotes de classe USB en C sur le PC, où il n'y a encore
 * ni hwmanager, ni hidmanager, ni vfs : ce programme joue les trois rôles, par
 * leurs protocoles inchangés (src/inc/usb.inc, hidmanager.asm, blk.inc), et dit
 * sur la console ce que les pilotes lui envoient.
 *
 *	usbclasstest [secondes]       (60 par défaut ; PC : à lancer avant usbd)
 *	usbclasstest watch [secondes] (Pi : abonné de hidmanager, comme Elegant)
 *
 *  * « hwmanager » : chaque interface annoncée par usbd (HW_OP_USB) lance son
 *    pilote comme drivers.txt (usb:03/01/01 usbkeyboard, 03/01/02 usbmouse,
 *    03/00/00 usbgamepad, 03 usbkeyboard, 08/06/50 usbstorage, 02/06 et 02/02/ff
 *    usbnet), par
 *    SYS_MOD_LAUNCH avec « <périphérique> <interface> » ;
 *  * « hid » : l'anneau HID_OP_PILOTE de chaque pilote ; les événements sont dits ;
 *  * « vfs » : VFS_MOUNT et VFS_UNMOUNT sont dits et acceptés ; après un montage,
 *    essai du disque et de la partition par blk.inc : INFO, ATTACHER, lectures
 *    (dont 200 secteurs : deux commandes SCSI), comparaison partition / disque,
 *    écriture de 200 secteurs vers la fin de la partition, relecture, puis remise
 *    des données d'origine (sur une image d'essai seulement), lecture hors bornes ;
 *  * « net » : l'interface d'un pilote réseau (usbnet) ; une requête ARP pour la
 *    passerelle de QEMU (10.0.2.2) est mise à émettre, la réponse est dite.
 * « watch » (Pi, où hidmanager tourne) : s'abonne à hidmanager (HID_OP_ABONNER) et
 * dit chaque événement reçu (type, caractère de la disposition, a, b, c), pour
 * comparer les pilotes en asm et en C par ce qu'en reçoit le bureau ; « @@HID_PRET »
 * quand l'abonnement est pris. */
#include "korinrt.h"

#define HW_OP_USB 1
#define HW_OP_USB_RETRAIT 2
#define HID_OP_PILOTE 3
#define SVC_OP_END 0xFFFF
#define SVF_ENDS 1
#define BLK_INFO 1
#define BLK_ATTACHER 2
#define BLK_LIRE 3
#define BLK_ECRIRE 4
#define RINGS 16

static void say(const char *fmt, ...)
{
	char b[300];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof b - 1)
		n = sizeof b - 1;
	korin_sys2(SYS_PRINT, b, n); /* la console du noyau (pas par « vfs » : c'est nous) */
}

static struct {
	volatile u64 pid;
	volatile u64 *ring;
} rings[RINGS];
static volatile int mount_req;
static char mount_src[64];

static u8 stacks[4][16384] __attribute__((aligned(16)));

static const char *driver_of(u32 cls)
{
	if (cls == 0x030101)
		return "usbkeyboard";
	if (cls == 0x030102)
		return "usbmouse";
	if (cls == 0x030000)
		return "usbgamepad";
	if (cls >> 16 == 3)
		return "usbkeyboard";
	if (cls == 0x080650)
		return "usbstorage";
	if (cls >> 8 == 0x0206 || cls == 0x0202FF)
		return "usbnet";
	return 0;
}

static NORETURN void hw_thread(long ch)
{
	u64 m[64];
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, ch, m, 60000);
		if (req <= 0)
			continue;
		u64 op = m[0], a = m[1], b = m[2];
		u32 cls = (u32)m[3], vp = (u32)m[4];
		m[0] = 0;
		korin_sys2(SYS_SVC_REPLY, req, m); /* (usbd attend : réponse d'abord) */
		if (op == HW_OP_USB) {
			const char *drv = driver_of(cls);
			say("usbclasstest : hwmanager : périphérique %lu interface %lu, %06x, %04x:%04x -> %s\n", a, b,
			    cls, vp >> 16, vp & 0xFFFF, drv ? drv : "(aucun pilote)");
			if (drv) {
				char args[32];
				snprintf(args, sizeof args, "%lu %lu", a, b);
				long pid = korin_sys2(SYS_MOD_LAUNCH, drv, args);
				say("usbclasstest : hwmanager : %s %s -> process %ld\n", drv, args, pid);
			}
		} else if (op == HW_OP_USB_RETRAIT)
			say("usbclasstest : hwmanager : périphérique %lu retiré\n", a);
	}
}

static NORETURN void hid_thread(long ch)
{
	u64 m[64];
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, ch, m, 60000);
		if (req <= 0)
			continue;
		u32 pid = *(u32 *)((u8 *)m + MSG_PID);
		if (m[0] == SVC_OP_END) {
			for (int i = 0; i < RINGS; i++)
				if (rings[i].pid == m[1]) {
					rings[i].pid = 0;
					say("usbclasstest : hid : fin du pilote %lu\n", m[1]);
				}
			continue;
		}
		long r = E_INVAL;
		if (m[0] == HID_OP_PILOTE) {
			int slot = -1;
			for (int i = 0; i < RINGS && slot < 0; i++)
				if (!rings[i].pid)
					slot = i;
			r = E_NOMEM;
			if (slot >= 0) {
				long num = korin_sys1(SYS_BUF_CREATE, 4096);
				long g = num < 0 ? num : korin_sys2(SYS_BUF_GRANT, num, pid);
				long va = g < 0 ? g : korin_sys1(SYS_BUF_MAP, num);
				if (va >= 0) {
					volatile u64 *ring = (volatile u64 *)va;
					ring[0] = ring[1] = 0;
					rings[slot].ring = ring;
					__atomic_store_n(&rings[slot].pid, pid, __ATOMIC_RELEASE);
					r = num;
				} else
					r = va;
			}
			say("usbclasstest : hid : anneau du pilote %u -> %ld\n", pid, r);
		}
		m[0] = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, m);
	}
}

static void blk_test(void);

static NORETURN void vfs_thread(long ch)
{
	u64 m[64];
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, ch, m, 60000);
		if (req <= 0)
			continue;
		char *data = (char *)m + MSG_DATA;
		data[463] = 0;
		long r = E_INVAL;
		if (m[0] == VFS_MOUNT) {
			say("usbclasstest : vfs : montage « %s »\n", data);
			const char *s = data;
			while (*s && *s != ' ')
				s++;
			while (*s == ' ')
				s++;
			int n = 0;
			while (s[n] && s[n] != ' ' && n < 63)
				n++;
			memcpy(mount_src, s, (size_t)n);
			mount_src[n] = 0;
			mount_req = 1;
			r = 0;
		} else if (m[0] == VFS_UNMOUNT) {
			say("usbclasstest : vfs : démontage « %s »\n", data);
			r = 0;
		} else
			say("usbclasstest : vfs : requête %lu refusée\n", m[0]);
		m[0] = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, m);
		if (mount_req) { /* (ici : le thread principal continue de dire les événements) */
			mount_req = 0;
			blk_test();
		}
	}
}

static void show_event(u64 pid, const volatile u64 *e)
{
	u64 t = e[0], a = e[1], b = e[2], c = e[3];
	switch (t) {
	case 1:
		say("usbclasstest : [%lu] touche %02lx %s (mods %02lx)\n", pid, a, b ? "enfoncée" : "relâchée", c);
		break;
	case 2:
		say("usbclasstest : [%lu] souris relative %ld %ld, boutons %lx\n", pid, (long)a, (long)b, c);
		break;
	case 3:
		say("usbclasstest : [%lu] souris absolue %lu %lu, boutons %lx\n", pid, a, b, c);
		break;
	case 4:
		say("usbclasstest : [%lu] molette %ld\n", pid, (long)a);
		break;
	case 16:
		say("usbclasstest : [%lu] manette boutons %lx axes %016lx modèle %08lx\n", pid, a, b, c);
		break;
	default:
		say("usbclasstest : [%lu] événement %lu : %lx %lx %lx\n", pid, t, a, b, c);
	}
}

/* ---- essai des périphériques blocs */

struct blk {
	long ch;
	u64 ro, sectors, pid;
	u8 *buf;
	long num;
};

static long blk_call(struct blk *b, u64 op, u64 a, u64 bb, u64 c)
{
	u64 m[64];
	memset(m, 0, 48);
	m[0] = op;
	m[1] = a;
	m[2] = bb;
	m[3] = c;
	long r = korin_sys2(SYS_SVC_CALL, b->ch, m);
	if (op == BLK_INFO && r == 0) {
		b->ro = m[1];
		b->sectors = m[2];
		b->pid = m[4];
	}
	return r;
}

#define BLKBUF (256 * 1024)

static int blk_open(struct blk *b, const char *name)
{
	b->ch = korin_sys2(SYS_SVC_OPEN, name, 5000);
	if (b->ch < 0) {
		say("usbclasstest : %s absent (%ld)\n", name, b->ch);
		return 0;
	}
	long r = blk_call(b, BLK_INFO, 0, 0, 0);
	say("usbclasstest : %s : INFO %ld, lecture seule %lu, %lu secteurs, service %lu\n", name, r, b->ro,
	    b->sectors, b->pid);
	b->num = korin_sys1(SYS_BUF_CREATE, BLKBUF);
	if (b->num < 0 || korin_sys2(SYS_BUF_GRANT, b->num, b->pid) < 0)
		return 0;
	b->buf = (u8 *)korin_sys1(SYS_BUF_MAP, b->num);
	r = blk_call(b, BLK_ATTACHER, (u64)b->num, BLKBUF, 0);
	say("usbclasstest : %s : ATTACHER %ld\n", name, r);
	return r == 0;
}

static long rd(struct blk *b, u64 sec, u64 n, u64 off)
{
	return blk_call(b, BLK_LIRE, sec, n, (u64)(b->buf + off));
}

static long wr(struct blk *b, u64 sec, u64 n, u64 off)
{
	return blk_call(b, BLK_ECRIRE, sec, n, (u64)(b->buf + off));
}

static void verdict(const char *what, int ok)
{
	say("usbclasstest : %s : %s\n", what, ok ? "OK" : "ÉCHEC");
}

static void blk_test(void)
{
	struct blk d = {0}, p = {0};
	char dname[32];
	/* « blk.usb0p1 » -> « blk.usb0 » */
	strcpy(dname, mount_src);
	char *q = strrchr(dname, 'p');
	if (q)
		*q = 0;
	if (!blk_open(&d, dname) || !blk_open(&p, mount_src))
		return;
	long r = rd(&d, 0, 1, 0);
	u8 *s0 = d.buf;
	u64 start = (u64)s0[446 + 8] | (u64)s0[446 + 9] << 8 | (u64)s0[446 + 10] << 16 | (u64)s0[446 + 11] << 24;
	say("usbclasstest : disque, secteur 0 : %ld, fin %02x %02x, partition 1 de type %02x à %lu\n", r, s0[510],
	    s0[511], s0[446 + 4], start);
	r = rd(&p, 0, 1, 0);
	char oem[9];
	memcpy(oem, p.buf + 3, 8);
	oem[8] = 0;
	say("usbclasstest : partition, secteur 0 : %ld, « %s », fin %02x %02x\n", r, oem, p.buf[510], p.buf[511]);
	/* 200 secteurs (deux commandes de 128 au plus) : partition = disque décalé */
	long r1 = rd(&p, 0, 200, 0), r2 = rd(&d, start, 200, 0);
	verdict("200 secteurs, partition = disque", !r1 && !r2 && !memcmp(p.buf, d.buf, 200 * 512));
	/* écriture vers la fin de la partition, relecture, remise */
	u64 w = p.sectors - 300;
	const u64 n = 200, ORIG = 0, PAT = 120 * 1024;
	r1 = rd(&p, w, n, ORIG);
	for (u64 i = 0; i < n * 512; i++)
		p.buf[PAT + i] = (u8)(i * 7 + i / 512 + 0x5A);
	r2 = wr(&p, w, n, PAT);
	say("usbclasstest : écriture de %lu secteurs au secteur %lu : lecture %ld, écriture %ld\n", n, w, r1, r2);
	memset(d.buf, 0, n * 512);
	r = rd(&d, start + w, n, 0);
	verdict("relecture par le disque", !r && !memcmp(d.buf, p.buf + PAT, n * 512));
	r = wr(&p, w, n, ORIG);
	memset(d.buf, 0, n * 512);
	long r3 = rd(&d, start + w, n, 0);
	verdict("données d'origine remises", !r && !r3 && !memcmp(d.buf, p.buf + ORIG, n * 512));
	r = rd(&p, p.sectors - 1, 2, 0);
	say("usbclasstest : lecture hors de la partition : %ld (attendu %d)\n", r, E_INVAL);
	r = blk_call(&p, BLK_LIRE, 0, 1, 0x1000);
	say("usbclasstest : adresse hors du tampon : %ld (attendu %d)\n", r, E_INVAL);
}

#define HID_OP_ABONNER 1

static int watch(long secs)
{
	long ch = korin_sys2(SYS_SVC_OPEN, "hid", 10000);
	u64 m[64] = {HID_OP_ABONNER};
	long num = ch < 0 ? ch : korin_sys2(SYS_SVC_CALL, ch, m);
	long va = num < 0 ? num : korin_sys1(SYS_BUF_MAP, num);
	if (va < 0) {
		say("usbclasstest : abonnement refusé (%ld)\n", va);
		return 1;
	}
	volatile u64 *r = (volatile u64 *)va; /* +0 tête (hidmanager), +8 queue (ici) */
	say("@@HID_PRET\n");
	long end = korin_sys0(SYS_TIME_MS) + secs * 1000;
	while (korin_sys0(SYS_TIME_MS) < end) {
		u64 head = __atomic_load_n(&r[0], __ATOMIC_ACQUIRE);
		while (r[1] != head) {
			const volatile u64 *e = r + (64 + (r[1] & 63) * 32) / 8;
			u32 t = (u32)e[0], ch32 = (u32)(e[0] >> 32);
			say("hid : %u '%c' %lx %lx %lx\n", t, ch32 >= 32 && ch32 < 127 ? (int)ch32 : '.', e[1], e[2], e[3]);
			__atomic_store_n(&r[1], r[1] + 1, __ATOMIC_RELEASE);
		}
		korin_sys1(SYS_SLEEP, 10);
	}
	say("@@HID_FIN\n");
	return 0;
}

/* ---- « net » : netd pour une interface (NET_OP_IF_REGISTER, NET_OP_IF_STATE) ;
 * une requête ARP de 10.0.2.15 pour 10.0.2.2 (la passerelle du réseau user de
 * QEMU) mise à émettre, la réponse attendue dans l'anneau de réception */
#define NET_OP_IF_REGISTER 20
#define NET_OP_IF_STATE 21
#define NR_SIZE (4096 + 2 * 64 * 2048)
static volatile u8 *net_ring;
static u64 net_mac;

static NORETURN void net_thread(long ch)
{
	u64 m[64];
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, ch, m, 60000);
		if (req <= 0)
			continue;
		u32 pid = *(u32 *)((u8 *)m + MSG_PID);
		long r = E_INVAL;
		if (m[0] == NET_OP_IF_REGISTER) {
			char name[9];
			memcpy(name, (u8 *)m + MSG_DATA, 8);
			name[8] = 0;
			say("usbclasstest : net : interface « %s », type %lu, MAC %012lx, priorité %lu, réveil %lu\n",
			    name, m[1], m[2], m[3], m[4]);
			net_mac = m[2];
			long num = korin_sys1(SYS_BUF_CREATE, NR_SIZE);
			long g = num < 0 ? num : korin_sys2(SYS_BUF_GRANT, num, pid);
			long va = g < 0 ? g : korin_sys1(SYS_BUF_MAP, num);
			if (va >= 0) {
				memset((void *)va, 0, 4096);
				m[2] = (u64)num;
				__atomic_store_n(&net_ring, (volatile u8 *)va, __ATOMIC_RELEASE);
				r = 0; /* (numéro de l'interface) */
			} else
				r = va;
		} else if (m[0] == NET_OP_IF_STATE) {
			say("usbclasstest : net : état : lien %lu, %lu Mb/s\n", m[2], m[3]);
			r = 0;
		} else
			say("usbclasstest : net : requête %lu refusée\n", m[0]);
		m[0] = (u64)r;
		korin_sys2(SYS_SVC_REPLY, req, m);
	}
}

static void net_poll(void)
{
	static int sent;
	static long t0;
	volatile u8 *ring = __atomic_load_n(&net_ring, __ATOMIC_ACQUIRE);
	if (!ring)
		return;
	volatile u64 *h = (volatile u64 *)ring; /* RX tête, RX queue, TX tête, TX queue */
	if (!sent) {
		if (!t0)
			t0 = korin_sys0(SYS_TIME_MS);
		if (korin_sys0(SYS_TIME_MS) - t0 < 2000)
			return;
		u8 f[42] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
		const u8 *mac = (const u8 *)&net_mac;
		memcpy(f + 6, mac, 6);
		f[12] = 0x08, f[13] = 0x06; /* ARP */
		f[14] = 0, f[15] = 1, f[16] = 8, f[17] = 0, f[18] = 6, f[19] = 4, f[20] = 0, f[21] = 1;
		memcpy(f + 22, mac, 6);
		f[28] = 10, f[29] = 0, f[30] = 2, f[31] = 15; /* 10.0.2.15 */
		f[38] = 10, f[39] = 0, f[40] = 2, f[41] = 2;  /* qui a 10.0.2.2 ? */
		u64 head = h[2];
		volatile u8 *slot = ring + 4096 + 64 * 2048 + (head & 63) * 2048;
		*(volatile u64 *)slot = sizeof f;
		for (unsigned i = 0; i < sizeof f; i++)
			slot[8 + i] = f[i];
		__atomic_store_n(&h[2], head + 1, __ATOMIC_RELEASE);
		sent = 1;
		say("usbclasstest : net : requête ARP pour 10.0.2.2 mise à émettre\n");
	}
	while (h[1] != __atomic_load_n(&h[0], __ATOMIC_ACQUIRE)) {
		volatile u8 *slot = ring + 4096 + (h[1] & 63) * 2048;
		u64 len = *(volatile u64 *)slot;
		const volatile u8 *f = slot + 8;
		u32 type = (u32)f[12] << 8 | f[13];
		if (type == 0x0806 && len >= 42)
			say("usbclasstest : net : trame reçue, %lu o, ARP op %u : %u.%u.%u.%u est "
			    "%02x:%02x:%02x:%02x:%02x:%02x\n",
			    len, f[21], f[28], f[29], f[30], f[31], f[22], f[23], f[24], f[25], f[26], f[27]);
		else
			say("usbclasstest : net : trame reçue, %lu o, type %04x\n", len, type);
		__atomic_store_n(&h[1], h[1] + 1, __ATOMIC_RELEASE);
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && !strcmp(argv[1], "watch"))
		return watch(argc > 2 ? (long)parse_dec(argv[2]) : 60);
	long secs = argc > 1 ? (long)parse_dec(argv[1]) : 60;
	long hw = korin_sys2(SYS_SVC_REGISTER, "hwmanager", 0);
	long hid = korin_sys2(SYS_SVC_REGISTER, "hid", SVF_ENDS);
	long vfs = korin_sys2(SYS_SVC_REGISTER, "vfs", 0);
	long net = korin_sys2(SYS_SVC_REGISTER, "net", 0);
	if (hw < 0 || hid < 0 || vfs < 0 || net < 0) {
		say("usbclasstest : services refusés (%ld %ld %ld)\n", hw, hid, vfs);
		return 1;
	}
	korin_sys3(SYS_THREAD_CREATE, hw_thread, stacks[0] + sizeof stacks[0], hw);
	korin_sys3(SYS_THREAD_CREATE, hid_thread, stacks[1] + sizeof stacks[1], hid);
	korin_sys3(SYS_THREAD_CREATE, vfs_thread, stacks[2] + sizeof stacks[2], vfs);
	korin_sys3(SYS_THREAD_CREATE, net_thread, stacks[3] + sizeof stacks[3], net);
	say("usbclasstest : hwmanager, hid, vfs et net servis pendant %ld s\n", secs);
	long end = korin_sys0(SYS_TIME_MS) + secs * 1000;
	while (korin_sys0(SYS_TIME_MS) < end) {
		for (int i = 0; i < RINGS; i++) {
			u64 pid = __atomic_load_n(&rings[i].pid, __ATOMIC_ACQUIRE);
			if (!pid)
				continue;
			volatile u64 *r = rings[i].ring;
			u64 head = __atomic_load_n(&r[0], __ATOMIC_ACQUIRE);
			while (r[1] != head) {
				show_event(pid, r + (64 + (r[1] & 63) * 32) / 8);
				r[1] = r[1] + 1;
			}
		}
		net_poll();
		korin_sys1(SYS_SLEEP, 10);
	}
	say("usbclasstest : fin\n");
	return 0;
}
