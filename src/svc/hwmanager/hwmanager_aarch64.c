/* hwmanager_aarch64.c : la couche du Pi de hwmanager (hwmanager.h), reprise
 * de hwmanager.asm et de src/svc/inc/i2c.inc :
 *  * le bus I2C 1 en logiciel sur GPIO 2 (SDA) / 3 (SCL), ~100 kHz (le
 *    contrôleur BSC du SoC gère mal l'étirement d'horloge du GT911) : ligne à 1
 *    = broche en entrée (tirage haut), à 0 = sortie basse ;
 *  * les périphériques du SoC d'après la révision de la carte (boîte aux
 *    lettres du firmware) : soc:genet et soc:wifi sur un Pi 4 B (type 0x11),
 *    Pi 400 (0x13), CM4 (0x14) ; rien sous QEMU (firmware < 0x50000000) ;
 *    options no_wifi, no_ethernet ;
 *  * le son HDMI (docs/SON.md 5) : l'EDID (firmware) dit si l'écran prend le
 *    son ; toutes les 2 s, l'état « hot plug » de HDMI0 ; option no_hdmiaudio.
 * Le compteur du processeur (cntpct_el0) est lisible en EL0. */
#include "hwmanager.h"

/* ---- spawn, drivers.txt */

long plat_spawn(const char *path, const char *args)
{
	return ksys(SYS_SPAWN, (long)path, (long)args, 0, 0, 0, 0);
}

int plat_drivers_missing(ini_fn fn)
{
	(void)fn;
	return 0; /* (comme l'asm : table vide) */
}

/* ---- GPIO du BCM2711 (registres mappés) et bus I2C 1 en logiciel */

#define GPIO_PHYS 0xFE200000
#define BB_SDA 2
#define BB_SCL 3
#define BB_STRETCH_MS 10 /* SCL retenu plus longtemps : bus bloqué */
#define I2C_E_ADDR 1
#define I2C_E_DATA 2
#define I2C_E_RADDR 3
#define I2C_E_SCL 4
#define GP_FSEL0 0x00
#define GP_SET0 0x1C
#define GP_CLR0 0x28
#define GP_LEV0 0x34
#define GP_PUPPDN0 0xE4 /* propre au BCM2711 : 2 bits par broche */

static volatile u32 *gpio;
static u64 bb_half;
static u32 i2c_last_s; /* code du dernier échec (journal) */

static inline u64 cntpct(void)
{
	u64 v;
	__asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r"(v));
	return v;
}

static inline u64 cntfrq(void)
{
	u64 v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
	return v;
}

static void gpio_fsel(unsigned pin, unsigned fn)
{
	volatile u32 *r = gpio + GP_FSEL0 / 4 + pin / 10;
	unsigned sh = (pin % 10) * 3;
	*r = (*r & ~(7u << sh)) | ((fn & 7) << sh);
	__asm__ volatile("dsb sy" ::: "memory");
}

static unsigned gpio_get(unsigned pin)
{
	return (gpio[GP_LEV0 / 4 + (pin >> 5)] >> (pin & 31)) & 1;
}

static void gpio_set(unsigned pin, unsigned level)
{
	gpio[(level ? GP_SET0 : GP_CLR0) / 4 + (pin >> 5)] = 1u << (pin & 31);
}

static void gpio_pull(unsigned pin, unsigned pull)
{
	volatile u32 *r = gpio + GP_PUPPDN0 / 4 + (pin >> 4);
	unsigned sh = (pin & 15) * 2;
	*r = (*r & ~(3u << sh)) | ((pull & 3) << sh);
}

static void bb_dir(unsigned pin, unsigned low) { gpio_fsel(pin, low); } /* 1 sortie basse, 0 relâchée */

static void bb_delay(void)
{
	u64 end = cntpct() + bb_half;
	while (cntpct() < end)
		;
}

static void bb_sda_low(void) { bb_dir(BB_SDA, 1); }
static void bb_sda_rel(void) { bb_dir(BB_SDA, 0); }
static void bb_scl_low(void) { bb_dir(BB_SCL, 1); }

/* relâche SCL et attend qu'il remonte -> 0, -1 si retenu trop longtemps */
static long bb_scl_rel(void)
{
	bb_dir(BB_SCL, 0);
	u64 end = cntpct() + cntfrq() / (1000 / BB_STRETCH_MS);
	while (!gpio_get(BB_SCL))
		if (cntpct() >= end)
			return -1;
	return 0;
}

static void bb_start(void) /* START (ou START répété : SCL peut être bas) */
{
	bb_sda_rel();
	bb_delay();
	bb_scl_rel();
	bb_delay();
	bb_sda_low();
	bb_delay();
	bb_scl_low();
	bb_delay();
}

static void bb_stop(void)
{
	bb_sda_low();
	bb_delay();
	bb_scl_rel();
	bb_delay();
	bb_sda_rel();
	bb_delay();
}

/* octet -> 0 acquitté, 1 non acquitté, -1 SCL retenu */
static long bb_write(unsigned v)
{
	for (int bit = 7; bit >= 0; bit--) {
		if ((v >> bit) & 1)
			bb_sda_rel();
		else
			bb_sda_low();
		bb_delay();
		if (bb_scl_rel() < 0)
			return -1;
		bb_delay();
		bb_scl_low();
	}
	bb_sda_rel(); /* bit d'acquittement (lu) */
	bb_delay();
	if (bb_scl_rel() < 0)
		return -1;
	bb_delay();
	long ack = gpio_get(BB_SDA); /* 0 : acquitté */
	bb_scl_low();
	bb_delay();
	return ack;
}

/* ack = 1 : acquitter (d'autres octets suivent) -> octet, -1 si SCL retenu */
static long bb_read(int ack)
{
	unsigned v = 0;
	bb_sda_rel();
	for (int i = 0; i < 8; i++) {
		bb_delay();
		if (bb_scl_rel() < 0)
			return -1;
		bb_delay();
		v = (v << 1) | gpio_get(BB_SDA);
		bb_scl_low();
	}
	if (ack)
		bb_sda_low();
	else
		bb_sda_rel();
	bb_delay();
	if (bb_scl_rel() < 0)
		return -1;
	bb_delay();
	bb_scl_low();
	bb_sda_rel();
	return v;
}

static void bb_recover(void) /* 9 impulsions d'horloge, puis STOP */
{
	bb_sda_rel();
	for (int i = 0; i < 9; i++) {
		bb_scl_low();
		bb_delay();
		bb_scl_rel();
		bb_delay();
	}
	bb_stop();
}

static void i2c1_init(void)
{
	gpio_pull(BB_SDA, 1); /* tirage haut sur les deux lignes */
	gpio_pull(BB_SCL, 1);
	gpio_set(BB_SDA, 0); /* niveau de sortie : 0 */
	gpio_set(BB_SCL, 0);
	bb_dir(BB_SDA, 0);
	bb_dir(BB_SCL, 0);
	bb_half = cntfrq() / 200000; /* demi-période : 5 us */
	bb_recover();
}

/* adresse, octets à écrire, à lire -> 0, -1 (code : i2c_last_s) ; écriture puis
 * lecture : START répété */
static long i2c1_xfer(unsigned adr, const u8 *w, long nw, u8 *r, long nr)
{
	long x;
	u32 code;
	if (nw) {
		bb_start();
		x = bb_write(adr << 1);
		code = I2C_E_ADDR;
		if (x)
			goto err;
		while (nw) {
			x = bb_write(*w++);
			code = I2C_E_DATA;
			if (x)
				goto err;
			nw--;
		}
	}
	if (nr) {
		bb_start();
		x = bb_write(adr << 1 | 1);
		code = I2C_E_RADDR;
		if (x)
			goto err;
		do {
			x = bb_read(nr != 1); /* acquitter sauf le dernier */
			code = I2C_E_SCL;
			if (x < 0)
				goto err;
			*r++ = (u8)x;
		} while (--nr);
	}
	bb_stop();
	return 0;
err:
	if (x < 0)
		code = I2C_E_SCL; /* SCL retenu : code dédié */
	i2c_last_s = code;
	bb_stop();
	return -1;
}

long plat_i2c_open(void)
{
	long v = korin_sys2(SYS_DEV_MAP, GPIO_PHYS, 4096);
	if (v < 0)
		return v;
	gpio = (volatile u32 *)v;
	i2c1_init();
	return 0;
}

/* lecture d'un octet, sans registre : sans effet sur le GT911 */
int plat_i2c_present(u64 adr)
{
	u8 b[16];
	return i2c1_xfer((unsigned)adr, 0, 0, b, 1) == 0;
}

/* ---- son HDMI (docs/SON.md 5) */

#define HDMI_PAGE 0xFEF00000
#define HDMI_HOTPLUG (0x700 + 0x1A8) /* HDMI0, registre HOTPLUG (bit 0 : écran ; d'après Linux
					vc4_hdmi_regs.h, à confirmer) */

static u64 hdmi_on;   /* 1 : HOTPLUG surveillé */
static volatile u8 *hdmi_regs;
static u64 hdmi_hp;   /* dernier état hot plug */
static u64 hdmi_next;
static u64 hdmi_pid;  /* pilote lancé (0 : aucun) */
static u32 fw_edid[40] __attribute__((aligned(16)));

/* bloc d'EDID (0, 1) -> ses 128 octets (lus par le firmware), 0 si illisible */
static const u8 *edid_block(u32 n)
{
	fw_edid[0] = 160;
	fw_edid[1] = 0;
	fw_edid[2] = 0x00030020; /* GET_EDID_BLOCK */
	fw_edid[3] = 136;
	fw_edid[4] = 0;
	fw_edid[5] = n;
	fw_edid[6] = 0;
	fw_edid[39] = 0;
	if (korin_sys1(SYS_FW_CALL, fw_edid) < 0 || fw_edid[6]) /* état : 0 lu */
		return 0;
	return (const u8 *)fw_edid + 28;
}

/* EDID : l'écran prend-il le son ? -> lance le pilote (s'il ne tourne pas déjà) */
static void hdmi_check(void)
{
	if (hdmi_pid)
		return;
	const u8 *b = edid_block(0); /* bloc 0 : nombre d'extensions */
	if (!b) {
		say("EDID illisible\n");
		return;
	}
	int audio = 0;
	if (b[126] && (b = edid_block(1)) && b[0] == 2) { /* bloc 1 : extension CEA ? */
		if (b[3] & 0x40) /* audio de base */
			audio = 1;
		else /* blocs de données : de 4 à d */
			for (u64 i = 4; i < b[2]; i += (b[i] & 31) + 1)
				if ((b[i] >> 5) == 1) { /* Audio Data Block */
					audio = 1;
					break;
				}
	}
	if (!audio) {
		say("non (EDID)\n");
		return;
	}
	say("oui (EDID) -> ");
	long pid = launch(hdmi_line);
	hdmi_pid = pid < 0 ? 0 : (u64)pid;
}

/* au démarrage (Pi 4) : registres, EDID -> pilote s'il y a du son */
static void hdmi_boot(void)
{
	if (!hdmi_line[0]) /* pas de ligne dans drivers.txt : rien */
		return;
	if (option_word("no_hdmiaudio")) {
		say("hwmanager : no_hdmiaudio (options.txt) : son HDMI non surveillé\n");
		return;
	}
	long v = korin_sys2(SYS_DEV_MAP, HDMI_PAGE, 0x1000);
	if (v >= 0) {
		hdmi_regs = (volatile u8 *)v;
		hdmi_hp = *(volatile u32 *)(hdmi_regs + HDMI_HOTPLUG) & 1;
		hdmi_on = 1;
	}
	say("hwmanager : HDMI0, hot plug ");
	say_dec((long)hdmi_hp);
	say(" ; son de l'écran : ");
	hdmi_check();
}

int plat_hdmi_watched(void) { return hdmi_on != 0; }

void plat_hdmi_poll(void)
{
	if (!hdmi_on)
		return;
	u64 now = (u64)sys_time_ms();
	if (now < hdmi_next)
		return;
	hdmi_next = now + 2000;
	u64 hp = *(volatile u32 *)(hdmi_regs + HDMI_HOTPLUG) & 1;
	if (hp == hdmi_hp)
		return;
	hdmi_hp = hp;
	if (hp) {
		say("hwmanager : HDMI0 branché ; son de l'écran : "); /* EDID relu */
		hdmi_check();
		return;
	}
	say("hwmanager : HDMI0 débranché : pilote du son arrêté\n");
	u64 pid = hdmi_pid;
	hdmi_pid = 0;
	if (pid)
		korin_sys1(SYS_KILL, pid);
}

int plat_hdmi_end(long pid)
{
	if (hdmi_pid != (u64)pid)
		return 0;
	hdmi_pid = 0;
	return 1;
}

/* ---- périphériques du SoC (docs/RESEAU.md 2.4) */

static u32 fw_rev[8] __attribute__((aligned(16))) = {28, 0, 0x00000001, 4, 0, 0, 0}; /* GET_FIRMWARE_REVISION */
static u32 fw_board[8] __attribute__((aligned(16))) = {28, 0, 0x00010002, 4, 0, 0, 0}; /* GET_BOARD_REVISION */

void plat_soc_start(void)
{
	if (korin_sys1(SYS_FW_CALL, fw_rev) < 0)
		return;
	if (fw_rev[5] < 0x50000000) {
		say("hwmanager : émulateur : rien du SoC\n");
		return;
	}
	if (korin_sys1(SYS_FW_CALL, fw_board) < 0)
		return;
	u32 rev = fw_board[5];
	say("hwmanager : carte, révision ");
	print_hex(rev);
	say("\n");
	u32 type = (rev >> 4) & 0xFF;
	if (!(rev & (1u << 23)) || (type != 0x11 && type != 0x13 && type != 0x14)) {
		say("hwmanager : pas un Pi 4 : pilotes du SoC non lancés\n"); /* (ancien format : pas un Pi 4) */
		return;
	}
	hdmi_boot(); /* son HDMI (docs/SON.md 5) */
	int no_wifi = option_word("no_wifi");
	int no_eth = option_word("no_ethernet");
	for (long i = 0; i < soc_n; i++) {
		struct soc_entry *e = &soct[i];
		if (no_wifi && str_eq(e->key, "wifi")) {
			say("hwmanager : no_wifi (options.txt) : soc:wifi non lancé\n");
			continue;
		}
		if (no_eth && str_eq(e->key, "genet")) {
			say("hwmanager : no_ethernet (options.txt) : soc:genet non lancé\n");
			continue;
		}
		soc_launch(e);
	}
}
