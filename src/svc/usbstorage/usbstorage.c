/* usbstorage - clés USB et disques (classe 08h, SCSI transparent 06h, Bulk-Only
 * 50h), pilote lancé par hwmanager (drivers.txt) avec « <périphérique>
 * <interface> » (docs/USB.md). Repris pas à pas de usbstorage.asm (à côté), en C
 * pour le Pi et le PC :
 *  * transport Bulk-Only par usbd (op BULK, tampon partagé de 68 Ko : la commande
 *    en +0, le CSW en +512, les données en +4096) ;
 *  * SCSI : INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY(10),
 *    READ(10) / WRITE(10) par paquets de 128 secteurs ; LUN 0, 512 o ;
 *  * périphérique bloc « blk.usb0 » (src/svc/common/blkserv.c : partitions MBR,
 *    « blk.usb0p1 »...), puis un thread demande à vfs de monter la première
 *    partition en USB0: (FatFS : FAT32 ou exFAT). */
#include "korinrt.h"
#include "buskit_import.h"
#include "blkserv.h" /* src/svc/common : le serveur de blocs commun */

#define USB_OP_DESCRIPTORS 1
#define USB_OP_CONTROL 2
#define USB_OP_BULK_BUFFER 5
#define USB_OP_BULK 6
#define USB_DT_INTERFACE 4
#define USB_DT_ENDPOINT 5
#define USB_REQ_CLEAR_FEATURE 1

#define BUFFER (65536 + 4096)
#define DATA 4096 /* dans le tampon partagé */
#define CSW_OFF 512
#define MAX_BLOCKS 128 /* secteurs par commande */
#define CBW_SIG 0x43425355 /* « USBC » */
#define CSW_SIG 0x53425355 /* « USBS » */
#define E_NODEV -19

#define WEV_BUS 13 /* src/kits/buskit/buskit.inc */
#define BEV_LEN 12
#define BUS_DELETED 0xFFFFFFFFu

static long chan = -1;
static u64 dev, iface;
static u64 ep_in, mps_in, ep_out, mps_out;
static u8 *buf;
static u64 blocks;
static u32 tag;
static u64 msg[64] __attribute__((aligned(16)));
static u64 msg_vfs[64] __attribute__((aligned(16)));
static char disk[8] = "usb0";          /* « usb0 » */
static char blk_p1[16] = "blk.usb0p1";
static char vol[32] = "USB0 blk.usb0p1 fatfs"; /* montage : volume, source, service */
static char topic[64];                 /* korin.usb.device.<n> */
static u8 bus_ev[32];
static char text[96];

static NORETURN void removed(void);

/* descripteurs : les extrémités bulk IN et OUT de notre interface -> 1 si trouvées */
static int describe(void)
{
	msg[0] = USB_OP_DESCRIPTORS;
	msg[1] = dev;
	if (korin_sys2(SYS_SVC_CALL, chan, msg) < 0)
		return 0;
	const u8 *p = (const u8 *)msg + 48, *end = p + msg[2];
	int inside = 0; /* 1 : dans notre interface */
	while (p + 2 <= end && p[0] >= 2) {
		if (p[1] == USB_DT_INTERFACE)
			inside = p[2] == iface && p[3] == 0;
		else if (inside && p[1] == USB_DT_ENDPOINT && (p[3] & 3) == 2) { /* bulk seulement */
			u64 mps = (u64)(p[4] | p[5] << 8) & 0x7FF;
			if (p[2] & 0x80) {
				ep_in = p[2];
				mps_in = mps;
			} else {
				ep_out = p[2];
				mps_out = mps;
			}
		}
		p += p[0];
	}
	return ep_in && ep_out;
}

/* 1 IN / 0 OUT, décalage dans le tampon, longueur -> octets transférés, < 0 si
 * erreur ; clé retirée : removed() (ne revient pas) */
static long bulk(int in, u64 off, u64 len)
{
	msg[0] = USB_OP_BULK;
	msg[1] = dev;
	msg[2] = in ? ep_in : ep_out;
	msg[3] = in ? mps_in : mps_out;
	msg[4] = off;
	msg[6] = len;
	long r = korin_sys2(SYS_SVC_CALL, chan, msg);
	if (r == E_NODEV)
		removed();
	return r;
}

/* suite du texte : notification « Clé USB » : « USB0: <suite> » */
static void note(const char *rest)
{
	memcpy(text, vol, 4);
	text[4] = ':';
	text[5] = ' ';
	strcpy(text + 6, rest);
	notify_send("Cl\xC3\xA9 USB", text, 0);
}

/* la clé a été retirée : vfs démonte le volume (son service FatFS s'arrête),
 * puis le pilote s'arrête */
static NORETURN void removed(void)
{
	long v = korin_sys2(SYS_SVC_OPEN, "vfs", 1000);
	if (v >= 0) {
		msg_vfs[0] = VFS_UNMOUNT;
		memcpy((u8 *)msg_vfs + 48, disk, 4); /* « usb0 » */
		((u8 *)msg_vfs)[52] = 0;
		korin_sys2(SYS_SVC_CALL, v, msg_vfs);
		ak_puts("usbstorage : cle retiree, volume demonte\n");
		note("retir\xC3\xA9"
		     "e, volume d\xC3\xA9mont\xC3\xA9");
	}
	_exit(0);
}

/* SETUP -> octets, < 0 si erreur (sans données) */
static long control(u64 setup)
{
	msg[0] = USB_OP_CONTROL;
	msg[1] = dev;
	msg[2] = setup;
	msg[3] = 0;
	return korin_sys2(SYS_SVC_CALL, chan, msg);
}

/* CLEAR_FEATURE(ENDPOINT_HALT) sur l'extrémité IN (1) ou OUT (0) */
static void unstall(int in)
{
	control(0x02 | USB_REQ_CLEAR_FEATURE << 8 | (in ? ep_in : ep_out) << 32);
}

/* remise à zéro du transport : Bulk-Only Mass Storage Reset + déblocage */
static void reset_transport(void)
{
	control(0x21 | 0xFF << 8 | iface << 32);
	unstall(1);
	unstall(0);
}

/* bloc de commande SCSI (16 o), sa longueur, octets de données (tampon + DATA),
 * 1 données entrantes -> statut CSW (0 bon, 1 échec de commande), -1 erreur de
 * transport */
static long command(const u8 *cdb, u32 cdb_len, u64 len, int in)
{
	u8 *c = buf;
	memset(c, 0, 32); /* CBW (31 octets) */
	*(u32 *)c = CBW_SIG;
	tag++;
	*(u32 *)(c + 4) = tag;
	*(u32 *)(c + 8) = (u32)len;
	c[12] = in ? 0x80 : 0;
	c[13] = 0; /* LUN 0 */
	c[14] = (u8)cdb_len;
	memcpy(c + 15, cdb, 16);
	if (bulk(0, 0, 31) != 31)
		goto reset;
	if (len && bulk(in, DATA, len) < 0) /* échec (point d'accès bloqué ?) : */
		unstall(in);                /* débloquer, puis lire le CSW */
	for (int tries = 2;;) {
		long r = bulk(1, CSW_OFF, 13);
		/* (comparaison non signée, comme l'asm : une erreur passe, et le CSW
		   précédent resté dans le tampon est refusé plus bas) */
		if ((u64)r >= 13)
			break;
		unstall(1);
		if (!--tries)
			goto reset;
	}
	const u8 *s = buf + CSW_OFF;
	if (*(const u32 *)s != CSW_SIG || *(const u32 *)(s + 4) != tag || s[12] >= 2) /* erreur de phase */
		goto reset;
	return s[12];
reset:
	reset_transport();
	return -1;
}

/* REQUEST SENSE (efface la condition en attente) -> clé, -1 si erreur */
static long sense(void)
{
	u8 cdb[16] = {0x03, 0, 0, 0, 18};
	if (command(cdb, 6, 18, 1))
		return -1;
	return buf[DATA + 2] & 0xF;
}

/* INQUIRY, TEST UNIT READY, READ CAPACITY(10) -> 1 si le support est prêt
 * (blocks = nombre de secteurs) */
static int prepare(void)
{
	u8 *d = buf + DATA;
	u8 cdb[16] = {0x12, 0, 0, 0, 36}; /* INQUIRY : fabricant et produit */
	if (command(cdb, 6, 36, 1))
		return 0;
	ak_puts("usbstorage : ");
	d[32] = 0; /* 8..15 fabricant, 16..31 produit */
	ak_puts((const char *)d + 8);
	ak_puts("\n");
	for (int tries = 20;;) { /* TEST UNIT READY (le support peut demander un peu de temps) */
		memset(cdb, 0, sizeof cdb);
		long r = command(cdb, 6, 0, 0);
		if (!r)
			break;
		if (r < 0)
			return 0;
		sense();
		korin_sys1(SYS_SLEEP, 100);
		if (!--tries)
			return 0;
	}
	memset(cdb, 0, sizeof cdb); /* READ CAPACITY(10) */
	cdb[0] = 0x25;
	if (command(cdb, 10, 8, 1))
		return 0;
	u32 last = (u32)d[0] << 24 | (u32)d[1] << 16 | (u32)d[2] << 8 | d[3]; /* gros-boutiste */
	u32 size = (u32)d[4] << 24 | (u32)d[5] << 16 | (u32)d[6] << 8 | d[7];
	if (size != 512) {
		ak_puts("usbstorage : secteurs de taille non geree (512 seulement)\n");
		return 0;
	}
	blocks = (u64)last + 1;
	ak_puts("usbstorage : support de ");
	print_dec((long)(blocks >> 11)); /* Mo */
	ak_puts(" Mo\n");
	return 1;
}

/* secteur, nombre, adresse, 1 écriture -> 0 ou erreur (blkserv) */
static long transfer(u64 sector, u64 count, u8 *addr, int write)
{
	while (count) {
		u64 k = count < MAX_BLOCKS ? count : MAX_BLOCKS; /* paquet */
		if (sector + k > blocks) /* bornes du support */
			return E_IO;
		if (write) /* écriture : vers le tampon partagé */
			memcpy(buf + DATA, addr, k << 9);
		for (int tries = 2;;) {
			u8 cdb[16] = {0};
			cdb[0] = write ? 0x2A : 0x28; /* READ(10) / WRITE(10) */
			cdb[2] = (u8)(sector >> 24); /* LBA (gros-boutiste, 32 bits) */
			cdb[3] = (u8)(sector >> 16);
			cdb[4] = (u8)(sector >> 8);
			cdb[5] = (u8)sector;
			cdb[7] = (u8)(k >> 8); /* nombre (gros-boutiste) */
			cdb[8] = (u8)k;
			long r = command(cdb, 10, k << 9, !write);
			if (!r)
				break;
			if (r > 0)
				sense(); /* échec de commande : effacer, recommencer */
			if (!--tries)
				return E_IO;
		}
		if (!write) /* lecture : vers l'appelant */
			memcpy(addr, buf + DATA, k << 9);
		sector += k;
		count -= k;
		addr += k << 9;
	}
	return 0;
}

static long read_fn(u64 sector, u64 count, void *addr, u64 phys)
{
	(void)phys;
	return transfer(sector, count, addr, 0);
}

static long write_fn(u64 sector, u64 count, void *addr, u64 phys)
{
	(void)phys;
	return transfer(sector, count, addr, 1);
}

/* (thread) dès que « blk.usbNp1 » est servi, vfs le monte en USBN: ; puis il
 * veille : la clé retirée, le volume est démonté */
static NORETURN void mount_thread(void)
{
	if (korin_sys2(SYS_SVC_OPEN, blk_p1, 10000) < 0)
		goto end;
	long v = korin_sys2(SYS_SVC_OPEN, "vfs", 10000);
	if (v < 0)
		goto end;
	msg_vfs[0] = VFS_MOUNT; /* « USB0 blk.usb0p1 fatfs » */
	strcpy((char *)msg_vfs + 48, vol);
	long r = korin_sys2(SYS_SVC_CALL, v, msg_vfs);
	ak_puts("usbstorage : montage de ");
	ak_puts(vol);
	ak_puts(" -> ");
	print_dec(r);
	ak_puts("\n");
	note("est pr\xC3\xAAt"); /* (même sur un refus, comme l'asm) */
	/* la clé retirée : usbd retire son sujet korin.usb.device.<n> (docs/BUS.md) */
	char *t = topic;
	strcpy(t, "korin.usb.device.");
	*fmt_dec(dev, t + strlen(t)) = 0;
	if (bus_subscribe((long)topic, 0) > 0) {
		for (;;) { /* dort jusqu'au retrait */
			if (bus_wait((long)bus_ev, 60000) != WEV_BUS)
				continue;
			if (*(u32 *)(bus_ev + BEV_LEN) == BUS_DELETED)
				removed();
		}
	}
	for (;;) { /* sans busd : la clé est-elle encore là ? (sans transfert en cours,
		      le retrait ne se verrait pas) */
		korin_sys1(SYS_SLEEP, 1000);
		msg_vfs[0] = USB_OP_DESCRIPTORS;
		msg_vfs[1] = dev;
		if (korin_sys2(SYS_SVC_CALL, chan, msg_vfs) == E_NODEV)
			removed();
	}
end:
	for (;;)
		korin_sys1(SYS_THREAD_EXIT, 0);
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const char *s = krt_line; /* « <périphérique> <interface> » (x0 de l'asm) */
	if (!s)
		return 1;
	dev = parse_dec(s);
	for (;;) {
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
	if (!describe()) { /* extrémités bulk */
		ak_puts("usbstorage : pas d'extremites bulk\n");
		return 0;
	}
	msg[0] = USB_OP_BULK_BUFFER; /* le tampon partagé */
	msg[1] = BUFFER;
	long r = korin_sys2(SYS_SVC_CALL, chan, msg);
	if (r < 0)
		return 1;
	r = korin_sys1(SYS_BUF_MAP, r);
	if (r < 0)
		return 1;
	buf = (u8 *)r;
	if (!prepare()) { /* INQUIRY, prêt, capacité */
		ak_puts("usbstorage : support muet\n");
		return 1;
	}
	for (int n = 0;;) { /* un nom libre : usb0, usb1... */
		char name[16];
		disk[3] = (char)('0' + n);
		strcpy(name, "blk.");
		strcat(name, disk);
		if (korin_sys2(SYS_SVC_OPEN, name, 0) < 0)
			break;
		if (++n >= 9)
			return 1;
	}
	vol[3] = disk[3]; /* « USB0 blk.usb0p1 fatfs » */
	vol[12] = disk[3];
	blk_p1[7] = disk[3];
	u8 *stack = malloc(16384); /* le montage, dès que la partition est servie : un thread */
	if (stack)
		korin_sys3(SYS_THREAD_CREATE, mount_thread, stack + 16384, 0);
	blk_read_fn = read_fn; /* le pilote du serveur de blocs */
	blk_write_fn = write_fn;
	blk_start(disk, 0, blocks); /* (ne revient pas) */
}
