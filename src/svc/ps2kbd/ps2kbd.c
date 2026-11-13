/* ps2kbd - le clavier PS/2 du PC (contrôleur i8042 ; sur le ZBook, le clavier
 * interne, derrière l'EC), en service (docs/etude/PC-PILOTES.md, « Démarrage
 * jusqu'au shell »). Lancé par init2 depuis SD0:/etc/services.txt du PC (rien ne
 * l'annonce : ni pcid ni usbd ne voient l'i8042) ; s'inscrit auprès de
 * hidmanager comme usbkeyboard : même anneau (HID_OP_PILOTE), mêmes événements
 * EV_KEY (code HID de la touche, appui 1 / relâche 0, modificateurs comme le
 * rapport « boot » d'un clavier USB). La disposition (FR, BE, US, touches
 * mortes) est le travail de hidmanager ; la répétition aussi (les appuis
 * répétés du clavier lui-même sont ignorés).
 *
 *  * ports 0x60 / 0x64 par l'appel IO_PORT (droits de SD0:/sys ; le noyau ne
 *    permet que ces deux-là ; E_BUSY : le moniteur du noyau tient le
 *    contrôleur, ps2kbd s'arrête sans erreur) ;
 *  * IRQ 1 : la ligne de l'IOAPIC d'après la MADT (SYS_ACPI_TABLE « APIC »,
 *    remplacement de source ISA 1), par IRQ_BIND / IRQ_WAIT ; une attente de
 *    50 ms au plus (une IRQ sur front perdue pendant que la ligne était masquée
 *    ne bloque pas : le contrôleur est relu à chaque réveil) ; IRQ refusée :
 *    sondage toutes les 10 ms ;
 *  * jeu de codes 1 (le contrôleur traduit ; le micrologiciel le laisse ainsi),
 *    préfixes E0 (flèches, AltGr, Ctrl droit, pavé...), E1 (Pause : ignorée),
 *    fausses Maj E0 2A / E0 36 ignorées ; octets de la souris (bit 5 de
 *    l'état) ignorés.
 * Pour le PC seulement (svc.conf : arch = x86_64). */
#include "korinrt.h"

#define DATA 0x60
#define STATUS 0x64
#define E_BUSY -16
#define E_INTR -4

#define HID_OP_PILOTE 3 /* service « hid » (hidmanager) */
#define RING_EV 64      /* anneau : +0 tête (ici), +8 queue (hidmanager), +64 les événements */
#define RING_N 64
#define EV_KEY 1

static volatile u64 *ring;
static u8 msg[512] __attribute__((aligned(16)));
static u8 down[32];   /* codes HID enfoncés (bits) : les appuis répétés du clavier ne passent pas */
static u32 mods;      /* modificateurs, bits comme un rapport USB : Ctrl, Maj, Alt, GUI à gauche puis à droite */
static int prefix;    /* 0, 0xE0, ou 0xE1 (octets de Pause encore à sauter : e1_skip) */
static int e1_skip;

/* jeu 1 (0x00..0x58) -> code HID (page 7) ; 0 : rien */
static const u8 set1[0x59] = {
	[0x01] = 0x29, [0x02] = 0x1E, [0x03] = 0x1F, [0x04] = 0x20, [0x05] = 0x21, [0x06] = 0x22,
	[0x07] = 0x23, [0x08] = 0x24, [0x09] = 0x25, [0x0A] = 0x26, [0x0B] = 0x27, [0x0C] = 0x2D,
	[0x0D] = 0x2E, [0x0E] = 0x2A, [0x0F] = 0x2B, [0x10] = 0x14, [0x11] = 0x1A, [0x12] = 0x08,
	[0x13] = 0x15, [0x14] = 0x17, [0x15] = 0x1C, [0x16] = 0x18, [0x17] = 0x0C, [0x18] = 0x12,
	[0x19] = 0x13, [0x1A] = 0x2F, [0x1B] = 0x30, [0x1C] = 0x28, [0x1D] = 0xE0, [0x1E] = 0x04,
	[0x1F] = 0x16, [0x20] = 0x07, [0x21] = 0x09, [0x22] = 0x0A, [0x23] = 0x0B, [0x24] = 0x0D,
	[0x25] = 0x0E, [0x26] = 0x0F, [0x27] = 0x33, [0x28] = 0x34, [0x29] = 0x35, [0x2A] = 0xE1,
	/* 0x2B : la touche à gauche d'Entrée (ISO) ou « \ » (ANSI) : 0x31, comme un clavier USB ANSI ;
	   les dispositions FR et BE de hidmanager donnent le même caractère pour 0x31 et 0x32 */
	[0x2B] = 0x31, [0x2C] = 0x1D, [0x2D] = 0x1B, [0x2E] = 0x06, [0x2F] = 0x19, [0x30] = 0x05,
	[0x31] = 0x11, [0x32] = 0x10, [0x33] = 0x36, [0x34] = 0x37, [0x35] = 0x38, [0x36] = 0xE5,
	[0x37] = 0x55, [0x38] = 0xE2, [0x39] = 0x2C, [0x3A] = 0x39, [0x3B] = 0x3A, [0x3C] = 0x3B,
	[0x3D] = 0x3C, [0x3E] = 0x3D, [0x3F] = 0x3E, [0x40] = 0x3F, [0x41] = 0x40, [0x42] = 0x41,
	[0x43] = 0x42, [0x44] = 0x43, [0x45] = 0x53, [0x46] = 0x47, [0x47] = 0x5F, [0x48] = 0x60,
	[0x49] = 0x61, [0x4A] = 0x56, [0x4B] = 0x5C, [0x4C] = 0x5D, [0x4D] = 0x5E, [0x4E] = 0x57,
	[0x4F] = 0x59, [0x50] = 0x5A, [0x51] = 0x5B, [0x52] = 0x62, [0x53] = 0x63, [0x56] = 0x64,
	[0x57] = 0x44, [0x58] = 0x45,
};

/* après E0 : code (0x00..0x5F) -> code HID ; 0 : ignoré (fausses Maj 2A, 36...) */
static u8 set1_e0(u8 c)
{
	switch (c) {
	case 0x1C: return 0x58; /* Entrée du pavé */
	case 0x1D: return 0xE4; /* Ctrl droit */
	case 0x35: return 0x54; /* / du pavé */
	case 0x37: return 0x46; /* Impr. écran */
	case 0x38: return 0xE6; /* Alt droit (AltGr) */
	case 0x47: return 0x4A; /* Début */
	case 0x48: return 0x52; /* haut */
	case 0x49: return 0x4B; /* page haut */
	case 0x4B: return 0x50; /* gauche */
	case 0x4D: return 0x4F; /* droite */
	case 0x4F: return 0x4D; /* Fin */
	case 0x50: return 0x51; /* bas */
	case 0x51: return 0x4E; /* page bas */
	case 0x52: return 0x49; /* Inser */
	case 0x53: return 0x4C; /* Suppr */
	case 0x5B: return 0xE3; /* GUI gauche */
	case 0x5C: return 0xE7; /* GUI droite */
	case 0x5D: return 0x65; /* Menu */
	}
	return 0;
}

static long io_in(unsigned port)
{
	return korin_sys3(SYS_IO_PORT, port, 0, 0);
}

static long io_out(unsigned port, unsigned v)
{
	return korin_sys3(SYS_IO_PORT, port, 1, v);
}

static int wait_write(void)
{
	for (int i = 0; i < 100000; i++)
		if (!(io_in(STATUS) & 2))
			return 0;
	return -1;
}

static int wait_read(void)
{
	for (int i = 0; i < 100000; i++)
		if (io_in(STATUS) & 1)
			return 0;
	return -1;
}

/* comme ps2_init du noyau (src/kernel/arch/pc/ps2.c) : IRQ 1 et horloge du
 * port 1 en marche, port 1 actif ; la traduction (jeu 1) laissée telle quelle */
static int controller_init(void)
{
	for (int i = 0; i < 64 && (io_in(STATUS) & 1); i++)
		(void)io_in(DATA);
	if (wait_write())
		return -1;
	io_out(STATUS, 0x20); /* lire la configuration */
	if (wait_read())
		return -1;
	u32 cfg = (u32)io_in(DATA) & 0xFF;
	cfg |= 1;         /* IRQ 1 */
	cfg &= ~(1u << 4); /* horloge du clavier en marche */
	if (wait_write())
		return -1;
	io_out(STATUS, 0x60);
	if (wait_write())
		return -1;
	io_out(DATA, cfg);
	if (wait_write())
		return -1;
	io_out(STATUS, 0xAE); /* port 1 actif */
	return 0;
}

/* la ligne de l'IOAPIC de l'IRQ ISA 1 : MADT (« APIC ») : remplacement de source
 * (type 2, bus 0, source 1) sinon GSI 1 ; moins la base du premier IOAPIC (type
 * 1), comme les lignes du noyau (src/kernel/arch/pc/apic.c) */
static long isa1_line(void)
{
	static u8 madt[4096];
	long n = korin_sys4(SYS_ACPI_TABLE, "APIC", 0, madt, sizeof madt);
	if (n < 44)
		return 1;
	if (n > (long)sizeof madt)
		n = sizeof madt;
	u32 gsi = 1, base = 0;
	int have_base = 0;
	for (long o = 44; o + 2 <= n && madt[o + 1] >= 2 && o + madt[o + 1] <= n; o += madt[o + 1]) {
		const u8 *e = madt + o;
		if (e[0] == 1 && e[1] >= 12 && !have_base) {
			base = (u32)(e[8] | e[9] << 8 | e[10] << 16 | (u32)e[11] << 24);
			have_base = 1;
		} else if (e[0] == 2 && e[1] >= 10 && e[2] == 0 && e[3] == 1)
			gsi = (u32)(e[4] | e[5] << 8 | e[6] << 16 | (u32)e[7] << 24);
	}
	return gsi >= base ? (long)(gsi - base) : 1;
}

static void push(u64 code, u64 pressed)
{
	volatile u64 *r = ring;
	u64 head = r[0], tail = r[1];
	if (head - tail >= RING_N)
		return; /* plein : perdu */
	volatile u64 *e = r + (RING_EV + (head & (RING_N - 1)) * 32) / 8;
	e[0] = EV_KEY;
	e[1] = code;
	e[2] = pressed;
	e[3] = mods;
	__atomic_store_n(&r[0], head + 1, __ATOMIC_RELEASE); /* l'événement avant la tête */
}

/* un octet du clavier */
static void byte_in(u8 b)
{
	if (e1_skip) { /* Pause : E1 1D 45 / E1 9D C5, ignorée */
		e1_skip--;
		return;
	}
	if (b == 0xE0) {
		prefix = 0xE0;
		return;
	}
	if (b == 0xE1) {
		e1_skip = 2;
		return;
	}
	/* erreurs, accusé, renvoi, écho : pas des touches (0xAA, la réussite de l'essai du clavier, est aussi
	   le relâchement de Maj gauche : jamais filtré, le clavier n'est pas remis à zéro ici) */
	if (b == 0x00 || b == 0xFF || b == 0xFA || b == 0xFE || b == 0xEE)
		return;
	int release = b & 0x80;
	u8 c = b & 0x7F;
	u8 hid = prefix == 0xE0 ? set1_e0(c) : (c < sizeof set1 ? set1[c] : 0);
	prefix = 0;
	if (!hid)
		return;
	int was = down[hid >> 3] >> (hid & 7) & 1;
	if (!release && was)
		return; /* appui répété par le clavier : hidmanager répète lui-même */
	if (release && !was)
		return;
	if (release)
		down[hid >> 3] &= (u8) ~(1u << (hid & 7));
	else
		down[hid >> 3] |= (u8)(1u << (hid & 7));
	if (hid >= 0xE0) { /* modificateur : le bit change, l'événement porte le nouvel état */
		u32 bit = 1u << (hid - 0xE0);
		mods = release ? mods & ~bit : mods | bit;
	}
	push(hid, release ? 0 : 1);
}

/* tout ce qui attend dans le contrôleur */
static void drain(void)
{
	for (int i = 0; i < 64; i++) {
		long st = io_in(STATUS);
		if (st < 0 || !(st & 1))
			return;
		u8 v = (u8)io_in(DATA);
		if (st & 0x20) /* octet de la souris (port 2) */
			continue;
		byte_in(v);
	}
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	long st = io_in(STATUS);
	if (st == E_BUSY) {
		ak_puts("ps2kbd : l'i8042 est au moniteur du noyau (option monitor) : rien à faire\n");
		return 0;
	}
	if (st < 0) {
		ak_puts("ps2kbd : pas d'accès aux ports (");
		print_dec(st);
		ak_puts(")\n");
		return 0; /* (Pi : E_NOSYS) */
	}
	if (st == 0xFF) {
		ak_puts("ps2kbd : pas de contrôleur i8042\n");
		return 0;
	}
	if (controller_init()) {
		ak_puts("ps2kbd : le contrôleur ne répond pas\n");
		return 0;
	}
	long hid = korin_sys2(SYS_SVC_OPEN, "hid", 10000); /* l'anneau vers hidmanager */
	if (hid < 0) {
		ak_puts("ps2kbd : hidmanager absent\n");
		return 1; /* (init2 relance) */
	}
	((u64 *)msg)[0] = HID_OP_PILOTE;
	long r = korin_sys2(SYS_SVC_CALL, hid, msg);
	if (r < 0)
		return 1;
	r = korin_sys1(SYS_BUF_MAP, r);
	if (r < 0)
		return 1;
	ring = (volatile u64 *)r;
	long line = isa1_line();
	long b = korin_sys3(SYS_IRQ_BIND, line, -1, 0);
	ak_puts("ps2kbd : clavier PS/2 inscrit auprès de hidmanager, IRQ 1 ");
	if (b < 0) {
		ak_puts("refusée (");
		print_dec(b);
		ak_puts(") : sondage toutes les 10 ms\n");
	} else {
		ak_puts("sur la ligne ");
		print_dec(line);
		ak_puts("\n");
	}
	for (;;) {
		drain();
		if (b < 0)
			sys_sleep(10);
		else if (korin_sys2(SYS_IRQ_WAIT, line, 50) == E_INTR)
			return 0;
	}
}
