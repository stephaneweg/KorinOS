/* rtsx - la carte SD par un lecteur Realtek PCIe (jeu de registres « rtsx »),
 * en service, en C (docs/etude/PC-PILOTES.md, « Carte SD »). C'est le lecteur
 * 10EC:5261 (RTS5261) du ZBook Fury 17 G8. JAMAIS ESSAYÉ : QEMU ne l'émule pas.
 *
 * Références (aucune documentation de Realtek n'est publique) :
 *  * le pilote rtsx(4) d'OpenBSD (sys/dev/ic/rtsx.c, rtsxreg.h, pci/rtsx_pci.c ;
 *    licence ISC, © 2006 Uwe Stuehler, © 2012 Stefan Sperling) : lu, et repris
 *    pour l'accès aux registres internes (HAIMR), le tampon de commandes
 *    (HCBAR/HCBCTLR), l'ADMA (HDBAR/HDBCTLR), les interruptions (BIPR/BIER), les
 *    noms des registres ; il gère RTS5209, 5227, 5229, 522A, 5249, 525A, RTL8402,
 *    8411, 8411B, PAS le 5261 ;
 *  * le pilote rtsx_pci de Linux (drivers/misc/cardreader/rtsx_pcr.c, rts5261.c,
 *    drivers/mmc/host/rtsx_pci_sdmmc.c ; GPL) : seulement comme CARTE de ce qui
 *    est propre au 5261, sans en copier le code : alimentation par les LDO
 *    (0xFF70-0xFF73) au lieu de PWR_GATE_CTRL, horloge SSC (n = f - 4, divisé
 *    par 2 dans SSC_DIV_N_0, profondeurs propres), protection contre les
 *    surintensités (OCP par LDO1_CFG0), descripteurs ADMA (longueur aux bits
 *    16-31), réglages du fabricant dans la configuration PCI (0x724, 0x814),
 *    PRSNT# relâché, MCU sans horloge coupée, lectures par AUTO_READ_2 et
 *    écritures par AUTO_WRITE_3 après la commande, CMD12 envoyé à la main.
 *
 * Fonctionnement : chaque opération est une liste de commandes de 32 bits dans
 * un tampon en mémoire (écrire, lire, vérifier un registre interne) que la
 * puce exécute seule (HCBCTLR) ; les registres lus reviennent, un octet par
 * lecture, au début du même tampon. Les données passent par l'ADMA (table de
 * descripteurs de 64 bits) vers un tampon de 128 Ko sous 4 Go (la puce n'a que
 * 32 bits d'adresse), ou par le tampon « ping-pong » interne (CMD6, ACMD51).
 * Fin d'opération : TRANS_OK / TRANS_FAIL dans BIPR (effacés par écriture),
 * par MSI si la machine en donne, sinon en sondant.
 *
 *	rtsx [-v] [-t] [-d] [-r] [-n NOM] [SSSS:BB:DD.F]
 *	  -d  diagnostic : registres et configuration lus SANS RIEN ÉCRIRE, puis fin
 *	  -t  traces de chaque étape et registres après chacune, puis le service
 *	  -v  chaque commande au journal       -r  lecture seule (rien n'est écrit)
 *	  -n  nom du disque (défaut : sd0, sinon le premier sdN libre)
 *	  l'adresse PCIe (donnée par hwmanager d'après drivers.txt, pci:10ec:5261) */
#include "../sdmmc/sdmmc.h"
#include "pci.h"

/* ---- registres de la BAR (OpenBSD rtsxreg.h) */
#define HCBAR 0x00
#define HCBCTLR 0x04
#define START_CMD (1u << 31)
#define HW_AUTO_RSP (1u << 30)
#define STOP_CMD (1u << 28)
#define HDBAR 0x08
#define HDBCTLR 0x0C
#define TRIG_DMA (1u << 31)
#define DMA_READ (1u << 29)
#define STOP_DMA (1u << 28)
#define ADMA_MODE (2u << 26)
#define HAIMR 0x10
#define HAIMR_BUSY (1u << 31)
#define HAIMR_WRITE (1u << 30)
#define BIPR 0x14
#define BIER 0x18
#define DUM_REG 0x1C
#define CMD_DONE_INT (1u << 31)
#define DATA_DONE_INT (1u << 30)
#define TRANS_OK_INT (1u << 29)
#define TRANS_FAIL_INT (1u << 28)
#define SD_INT (1u << 25)
#define DELINK_INT (1u << 24)
#define SD_OC_INT (1u << 22)
#define SD_WRITE_PROTECT (1u << 19)
#define SD_EXIST (1u << 16)

/* ---- registres internes (OpenBSD rtsxreg.h ; ceux du 5261 d'après la carte de Linux) */
#define FPDCTL 0xFC00
#define CLK_CTL 0xFC02
#define CLK_LOW_FREQ 0x01
#define CLK_DIV 0xFC03
#define CLK_SEL 0xFC04
#define SSC_DIV_N_0 0xFC0F
#define SSC_CTL1 0xFC11
#define SSC_RSTB 0x80
#define SSC_8X_EN 0x40
#define SSC_SEL_4M 0x10
#define SSC_CTL2 0xFC12
#define RCCTL 0xFC14
#define OCPCTL 0xFD6A
#define OCPGLITCH 0xFD6C
#define OCPSTAT 0xFD6E
#define OLT_LED_CTL 0xFC1E
#define GPIO_CTL 0xFC1F
#define CARD_CLK_SOURCE 0xFC2E
#define CARD_PWR_CTL 0xFD50
#define CARD_SHARE_MODE 0xFD52
#define CARD_DRIVE_SEL 0xFD53
#define CARD_STOP 0xFD54
#define CARD_OE 0xFD55
#define CARD_GPIO_DIR 0xFD57
#define CARD_GPIO 0xFD58
#define SD30_CLK_DRIVE 0xFD5A
#define CARD_DATA_SOURCE 0xFD5B
#define CARD_SELECT 0xFD5C
#define SD30_CMD_DRIVE 0xFD5E
#define SD30_DAT_DRIVE 0xFD5F
#define CARD_PULL_CTL1 0xFD60
#define CARD_PULL_CTL2 0xFD61
#define CARD_PULL_CTL3 0xFD62
#define CARD_CLK_EN 0xFD69
#define CRC_DUMMY_0 0xFD71
#define SD_CFG1 0xFDA0
#define SD_CFG2 0xFDA1
#define SD_CFG3 0xFDA2
#define SD_STAT1 0xFDA3
#define SD_STAT2 0xFDA4
#define SD_BUS_STAT 0xFDA5
#define SD_PAD_CTL 0xFDA6
#define SD_SAMPLE_POINT 0xFDA7
#define SD_PUSH_POINT 0xFDA8
#define SD_CMD0 0xFDA9
#define SD_BYTE_CNT_L 0xFDAF
#define SD_BYTE_CNT_H 0xFDB0
#define SD_BLOCK_CNT_L 0xFDB1
#define SD_BLOCK_CNT_H 0xFDB2
#define SD_TRANSFER 0xFDB3
#define SD_CMD_STATE 0xFDB5
#define SD_DATA_STATE 0xFDB6
#define SD_STOP_SDCLK_CFG 0xFDB8
#define PPBUF_BASE2 0xFA00
#define IRQSTAT0 0xFE21
#define DMATC0 0xFE28
#define DMACTL 0xFE2C
#define RBCTL 0xFE34
#define PHY_RWCTL 0xFE3C
#define PHY_DATA0 0xFE3D
#define PHY_DATA1 0xFE3E
#define PHY_ADDR 0xFE3F
#define PETXCFG_OLD 0xFE49
#define PCLK_CTL 0xFE55
#define ASPM_FORCE_CTL 0xFE57
#define PM_CLK_FORCE_CTL 0xFE58
#define FUNC_FORCE_CTL 0xFE59
#define CHANGE_LINK_STATE 0xFE5B
#define PERST_GLITCH_WIDTH 0xFE5C
#define SD30_DRIVE_SEL_OLD 0xFE5E
#define HOST_SLEEP_STATE 0xFE60
#define PM_EVENT_DEBUG 0xFE71
#define NFTS_TX_CTRL 0xFE72
#define PWR_GATE_CTRL 0xFE75
#define PWD_SUSPEND_EN 0xFE76
#define LDO_PWR_SEL 0xFE78
#define L1SUB_CONFIG1 0xFE8D
#define L1SUB_CONFIG3 0xFE8F
#define DUMMY_REG 0xFE90
#define DMA_RST_CTL_0 0xFEBF
#define PETXCFG 0xFF03
#define LDO_VCC_CFG1 0xFF73 /* (525A) */
/* propres au 5261 */
#define R61_FW_CFG_INFO0 0xFF50
#define R61_FW_CFG1 0xFF55
#define R61_FW_STATUS 0xFF56
#define R61_FW_CTL 0xFF5F
#define R61_FPDCTL 0xFF60
#define R61_LDO_POW_CTL 0xFF70 /* LDO1233318_POW_CTL */
#define R61_DV3318_CFG 0xFF71
#define R61_LDO1_CFG0 0xFF72
#define R61_LDO1_CFG1 0xFF73
#define R61_PME_FORCE_CTL 0xFF78
#define R61_AUTOLOAD_CFG1 0xFF7C
#define R61_AUTOLOAD_CFG3 0xFF7E
#define R61_AUTOLOAD_CFG4 0xFF7F

/* commandes du tampon */
#define CMD_READ 0
#define CMD_WRITE 1
#define CMD_CHECK 2
#define CMD_MAX 256 /* 1 Ko */

/* SD_CFG2 : types de réponse */
#define RSP_R0 0x04
#define RSP_R1 0x01
#define RSP_R1B 0x09
#define RSP_R2 0x02
#define RSP_R3 0x05
#define NO_CHECK_CRC7 0x04
#define CRC7_ERR 0x80

/* SD_TRANSFER */
#define TR_START 0x80
#define TR_END 0x40
#define TR_IDLE 0x20
#define TR_ERR 0x10
#define TM_CMD_RSP 0x08
#define TM_NORMAL_READ 0x0C
#define TM_AUTO_READ2 0x0E
#define TM_AUTO_WRITE3 0x01

#define MAX_BLOCKS 256
#define BOUNCE (MAX_BLOCKS * 512)
#define DESC_MAX 0xFE00u /* octets par descripteur ADMA (multiple de 512) */

#define CHIP_5261 1 /* profil d'après la carte de Linux */
#define CHIP_OBSD 2 /* profil d'OpenBSD (5227, 5229, 522A, 5249, 525A…) */

struct rtsx {
	volatile uint8_t *regs;
	struct pci_info pi;
	char addr[16];
	int chip, f5209, f525a, type_c;
	uint8_t ic;
	uint32_t *cmd;
	uint64_t cmd_pa;
	uint64_t *sg;
	uint64_t sg_pa;
	uint8_t *bounce;
	uint64_t bounce_pa;
	int n;
	long irq;
	unsigned cur_clk;   /* horloge interne SSC (MHz), 0 : pas encore réglée */
	unsigned card_hz;
	int initial, hs;
	int trace;          /* -t */
	uint32_t setting1, setting2;
	int reverse_socket, drive_3v3;
	uint64_t n_cmds, n_fail, n_timeouts;
	char desc[96];
};

static struct rtsx rx;
static struct sd_host host;
static struct sd_card card;

static inline uint32_t r32(unsigned r)
{
	return *(volatile uint32_t *)(rx.regs + r);
}

static inline void w32(unsigned r, uint32_t v)
{
	*(volatile uint32_t *)(rx.regs + r) = v;
}

/* ---- registres internes par HAIMR (OpenBSD rtsx_read / rtsx_write) */

static int rreg(uint16_t a)
{
	w32(HAIMR, HAIMR_BUSY | (uint32_t)(a & 0x3FFF) << 16);
	for (int i = 0; i < 1024; i++) {
		uint32_t v = r32(HAIMR);
		if (!(v & HAIMR_BUSY))
			return (int)(v & 0xFF);
	}
	sd_log("registre %04x : lecture sans fin (HAIMR %08x)\n", a, r32(HAIMR));
	return E_TIMEDOUT;
}

static int wreg(uint16_t a, uint8_t mask, uint8_t val)
{
	w32(HAIMR, HAIMR_BUSY | HAIMR_WRITE | (uint32_t)(a & 0x3FFF) << 16 | (uint32_t)mask << 8 | val);
	for (int i = 0; i < 1024; i++) {
		uint32_t v = r32(HAIMR);
		if (!(v & HAIMR_BUSY)) {
			if (rx.trace > 1 && (uint8_t)v != val)
				sd_log("registre %04x : %02x/%02x écrit, %02x relu\n", a, val, mask, v & 0xFF);
			return 0;
		}
	}
	sd_log("registre %04x : écriture sans fin (HAIMR %08x)\n", a, r32(HAIMR));
	return E_TIMEDOUT;
}

/* registres PHY (rtsx_write_phy d'OpenBSD) */
static int wphy(uint8_t a, uint16_t v)
{
	wreg(PHY_DATA0, 0xFF, (uint8_t)v);
	wreg(PHY_DATA1, 0xFF, (uint8_t)(v >> 8));
	wreg(PHY_ADDR, 0xFF, a);
	wreg(PHY_RWCTL, 0xFF, 0x81);
	for (int i = 0; i < 100000; i++) {
		int s = rreg(PHY_RWCTL);
		if (s < 0)
			return s;
		if (!(s & 0x80))
			return 0;
	}
	sd_log("PHY %02x : écriture sans fin\n", a);
	return E_TIMEDOUT;
}

static int rphy(uint8_t a, uint16_t *v)
{
	wreg(PHY_ADDR, 0xFF, a);
	wreg(PHY_RWCTL, 0xFF, 0x80);
	for (int i = 0; i < 100000; i++) {
		int s = rreg(PHY_RWCTL);
		if (s < 0)
			return s;
		if (!(s & 0x80)) {
			*v = (uint16_t)(rreg(PHY_DATA0) | rreg(PHY_DATA1) << 8);
			return 0;
		}
	}
	sd_log("PHY %02x : lecture sans fin\n", a);
	return E_TIMEDOUT;
}

/* ---- le tampon de commandes */

static void cinit(void)
{
	rx.n = 0;
}

static void cadd(unsigned type, uint16_t reg, uint8_t mask, uint8_t val)
{
	if (rx.n >= CMD_MAX) {
		sd_log("tampon de commandes plein\n");
		return;
	}
	rx.cmd[rx.n++] = (uint32_t)(type & 3) << 30 | (uint32_t)(reg & 0x3FFF) << 16 | (uint32_t)mask << 8 | val;
}

static void cstart(void)
{
	sd_wmb();
	w32(HCBAR, (uint32_t)rx.cmd_pa);
	w32(HCBCTLR, START_CMD | HW_AUTO_RSP | ((rx.n * 4) & 0x00FFFFFF));
}

static void stop_cmd(void);

/* attend TRANS_OK / TRANS_FAIL -> 0, E_IO (échec), E_TIMEDOUT, E_NOMEDIUM */
static int wait_trans(long ms, const char *what)
{
	long end = sd_now_ms() + ms;
	for (int spin = 0;; spin++) {
		uint32_t s = r32(BIPR);
		if (s == 0xFFFFFFFF) {
			sd_log("%s : BIPR = ffffffff (lecteur disparu du bus ?)\n", what);
			return E_NOMEDIUM;
		}
		if (s & (TRANS_OK_INT | TRANS_FAIL_INT | DELINK_INT)) {
			w32(BIPR, s & (TRANS_OK_INT | TRANS_FAIL_INT | DELINK_INT | CMD_DONE_INT | DATA_DONE_INT));
			sd_rmb();
			if (s & (TRANS_FAIL_INT | DELINK_INT)) {
				rx.n_fail++;
				sd_dbg("%s : échec (BIPR %08x)\n", what, s);
				return E_IO;
			}
			return 0;
		}
		if (!(s & SD_EXIST))
			return E_NOMEDIUM;
		long left = end - sd_now_ms();
		if (left < 0) {
			rx.n_timeouts++;
			sd_log("%s : pas de fin après %ld ms (BIPR %08x)\n", what, ms, s);
			stop_cmd();
			return E_TIMEDOUT;
		}
		if (rx.irq >= 0 && spin > 16)
			korin_sys2(SYS_IRQ_WAIT, rx.irq, left + 1);
		else if (spin > 256)
			sd_sleep_ms(1);
		else if (spin > 16)
			korin_sys0(SYS_YIELD);
	}
}

static int csend(long ms, const char *what)
{
	cstart();
	return wait_trans(ms, what);
}

static const uint8_t *cresult(void)
{
	return (const uint8_t *)rx.cmd;
}

/* tout arrêté (rts5261_stop_cmd, rtsx_pci_stop_cmd) */
static void stop_cmd(void)
{
	w32(HCBCTLR, STOP_CMD);
	w32(HDBCTLR, STOP_DMA);
	if (rx.chip == CHIP_5261)
		wreg(DMA_RST_CTL_0, 0xC0, 0xC0);
	else
		wreg(DMACTL, 0x80, 0x80);
	wreg(RBCTL, 0x80, 0x80);
}

static void clear_error(void)
{
	wreg(CARD_STOP, 0x44, 0x44); /* SD_STOP | SD_CLR_ERR */
}

/* ---- diagnostic */

static void dump(struct sd_host *h, const char *why)
{
	(void)h;
	static const uint16_t regs[] = {
		FPDCTL, CLK_CTL, CLK_DIV, SSC_DIV_N_0, SSC_CTL1, SSC_CTL2, CARD_CLK_SOURCE, CARD_PWR_CTL, CARD_SHARE_MODE,
		CARD_STOP, CARD_OE, CARD_SELECT, CARD_PULL_CTL2, CARD_PULL_CTL3, CARD_CLK_EN, OCPCTL, OCPSTAT, CRC_DUMMY_0,
		SD_CFG1, SD_CFG2, SD_CFG3, SD_STAT1, SD_STAT2, SD_BUS_STAT, SD_PAD_CTL, SD_SAMPLE_POINT, SD_PUSH_POINT,
		SD_TRANSFER, SD_CMD_STATE, SD_DATA_STATE, IRQSTAT0, DMACTL, CHANGE_LINK_STATE, HOST_SLEEP_STATE,
		ASPM_FORCE_CTL, PETXCFG, DUMMY_REG, R61_FW_CFG_INFO0, R61_FW_CFG1, R61_FW_STATUS, R61_FW_CTL, R61_FPDCTL,
		R61_LDO_POW_CTL, R61_DV3318_CFG, R61_LDO1_CFG0, R61_LDO1_CFG1, R61_PME_FORCE_CTL, R61_AUTOLOAD_CFG1,
		R61_AUTOLOAD_CFG3, R61_AUTOLOAD_CFG4};
	sd_log("registres (%s) : HCBAR %08x HCBCTLR %08x HDBAR %08x HDBCTLR %08x HAIMR %08x BIPR %08x BIER %08x\n", why,
	       r32(HCBAR), r32(HCBCTLR), r32(HDBAR), r32(HDBCTLR), r32(HAIMR), r32(BIPR), r32(BIER));
	char line[200];
	int o = 0;
	for (unsigned i = 0; i < sizeof regs / sizeof *regs; i++) {
		if (rx.chip != CHIP_5261 && regs[i] >= 0xFF50)
			continue;
		int v = rreg(regs[i]);
		o += snprintf(line + o, sizeof line - o, " %04x=%02x", regs[i], v < 0 ? 0xEE : v);
		if (o > 150) {
			sd_log("internes :%s\n", line);
			o = 0;
		}
	}
	if (o)
		sd_log("internes :%s\n", line);
}

static void step(const char *what)
{
	if (rx.trace)
		dump(&host, what);
}

/* ---- alimentation et horloge */

static void pull_ctl(int on)
{
	if (rx.chip == CHIP_5261) { /* (tables du 5261 : CTL2, CTL3) */
		wreg(CARD_PULL_CTL2, 0xFF, on ? 0xAA : 0x55);
		wreg(CARD_PULL_CTL3, 0xFF, on ? 0xE9 : 0xD5);
	} else { /* OpenBSD */
		wreg(CARD_PULL_CTL1, 0xFF, on ? 0xAA : 0x55);
		wreg(CARD_PULL_CTL2, 0xFF, on ? 0xAA : 0x55);
		wreg(CARD_PULL_CTL3, 0xFF, on ? (rx.type_c ? 0xD9 : 0xE9) : (rx.type_c ? 0xE5 : 0xD5));
	}
}

/* signaux en 3,3 V (5261 : PHY, régulateur DV3318, pad, sortance) */
static void output_3v3(void)
{
	if (rx.chip != CHIP_5261) {
		wreg(SD30_DRIVE_SEL_OLD, 0x07, 0x01);
		return;
	}
	static const uint8_t drive[4] = {0x96, 0x96, 0x7F, 0x13};
	wreg(CARD_PWR_CTL, 0x20, 0x20); /* PUPDC */
	uint16_t v = 0;
	if (rphy(0x08, &v) == 0) /* PHY_TUNE */
		wphy(0x08, v | 0x0200); /* SDBUS 3,3 V */
	wreg(R61_DV3318_CFG, 0x70, 0x70);
	wreg(SD_PAD_CTL, 0x80, 0);
	uint8_t d = drive[rx.drive_3v3 & 3];
	wreg(SD30_CLK_DRIVE, 0xFF, d);
	wreg(SD30_CMD_DRIVE, 0xFF, d);
	wreg(SD30_DAT_DRIVE, 0xFF, d);
}

static void ocp(int on)
{
	if (rx.chip != CHIP_5261)
		return;
	wreg(R61_LDO1_CFG0, 0x12, on ? 0x12 : 0); /* LDO1_OCP_EN | LDO1_OCP_LMT_EN */
	wreg(OCPCTL, 0x0C, on ? 0x0C : 0);       /* SD_OCP_INT_EN | SD_DETECT_EN */
}

static void power_off(struct sd_host *h)
{
	(void)h;
	wreg(CARD_CLK_EN, 0x04, 0);
	wreg(CARD_OE, 0x04, 0);
	if (rx.chip == CHIP_5261) {
		stop_cmd();
		output_3v3();
		wreg(R61_LDO_POW_CTL, 0x0F, 0);
		wreg(CRC_DUMMY_0, 0x01, 0);
		ocp(0);
	} else { /* OpenBSD rtsx_bus_power_off */
		wreg(CARD_CLK_EN, 0x1E, 0);
		wreg(SD_BUS_STAT, 0x40, 0x40);
		if (rx.f5209)
			wreg(PWR_GATE_CTRL, 0x06, 0x06);
		else
			wreg(PWR_GATE_CTRL, 0x06, 0);
		wreg(CARD_PWR_CTL, 0x03, 0x03);
		wreg(CARD_PWR_CTL, 0x10, 0);
	}
	pull_ctl(0);
}

static unsigned ssc_depth_for(unsigned div)
{
	/* 500 kHz (vitesse normale et haute vitesse : sdmmc_set_ios de Linux),
	 * profondeur 5261 « 512K » = 5, horloge doublée : - 1, puis selon le diviseur */
	unsigned d = 5 - 1;
	if (div == 2)
		d = d > 1 ? d - 1 : 1;
	else if (div == 3)
		d = d > 2 ? d - 2 : 1;
	else if (div == 4)
		d = d > 3 ? d - 3 : 1;
	return d;
}

static int set_clock(struct sd_host *h, unsigned hz, int hs)
{
	(void)h;
	int initial = hz <= 1000000;
	rx.initial = initial;
	rx.card_hz = hz;
	if (rx.chip != CHIP_5261) { /* OpenBSD rtsx_bus_clock : 400 kHz, 25, 50 MHz */
		unsigned n, div;
		if (hz >= 50000000)
			n = 100, div = 2;
		else if (hz >= 25000000)
			n = 100, div = 3;
		else
			n = 80, div = 4;
		wreg(SD_CFG1, 0xC0, initial ? 0x80 : 0);
		wreg(SD_CFG1, 0x0C, 0);
		wreg(CLK_CTL, 0x01, 0x01);
		wreg(CARD_CLK_SOURCE, 0xFF, 0x04 | 0x20);
		wreg(SD_SAMPLE_POINT, 0x08, 0);
		wreg(SD_PUSH_POINT, 0xFF, 0);
		wreg(CLK_DIV, 0xFF, (uint8_t)(div << 4 | 7));
		wreg(SSC_CTL1, SSC_RSTB, 0);
		wreg(SSC_CTL2, 0x07, 0);
		wreg(SSC_DIV_N_0, 0xFF, (uint8_t)n);
		wreg(SSC_CTL1, SSC_RSTB, SSC_RSTB);
		sd_sleep_ms(1);
		wreg(CLK_CTL, 0x01, 0);
		rx.hs = hs;
		return 0;
	}
	/* 5261 : SSC = 2 x la carte (ou 30 MHz / 128 au départ, 60 / 256 après la version C) */
	unsigned mhz;
	uint8_t divider;
	if (initial) {
		divider = rx.ic > 2 ? 0xC0 : 0x80;
		mhz = rx.ic > 2 ? 60 : 30;
	} else {
		divider = 0;
		mhz = hz / 1000000 * 2;
	}
	wreg(SD_CFG1, 0xC0, divider);
	/* rythme des données (sd_set_timing) : SD 2.0, échantillonnage et poussée */
	cinit();
	cadd(CMD_WRITE, SD_CFG1, 0x0C, 0x00);
	cadd(CMD_WRITE, CLK_CTL, CLK_LOW_FREQ, CLK_LOW_FREQ);
	cadd(CMD_WRITE, CARD_CLK_SOURCE, 0xFF, 0x24); /* CRC_FIX | SD30_VAR_CLK0 | SAMPLE_VAR_CLK1 */
	cadd(CMD_WRITE, CLK_CTL, CLK_LOW_FREQ, 0);
	if (hs) {
		cadd(CMD_WRITE, SD_PUSH_POINT, 0x10, 0x10);  /* SD20_TX_14_AHEAD */
		cadd(CMD_WRITE, SD_SAMPLE_POINT, 0x08, 0x08); /* SD20_RX_14_DELAY */
	} else {
		cadd(CMD_WRITE, SD_PUSH_POINT, 0xFF, 0);
		cadd(CMD_WRITE, SD_SAMPLE_POINT, 0x08, 0);
	}
	int r = csend(100, "rythme");
	if (r < 0)
		return r;
	rx.hs = hs;
	if (mhz == rx.cur_clk)
		return 0;
	if (mhz <= 4 || mhz - 4 > 396) {
		sd_log("horloge de %u MHz impossible\n", mhz);
		return E_INVAL;
	}
	unsigned n = mhz - 4, div = 1;
	while (n < 80 - 4 && div < 4) {
		n = (n + 4) * 2 - 4;
		div++;
	}
	n = n / 2 - 1;
	unsigned mcu = 125 / mhz + 3;
	if (mcu > 15)
		mcu = 15;
	unsigned depth = ssc_depth_for(div);
	sd_dbg("horloge : carte %u Hz, SSC %u MHz, n %u, diviseur %u, mcu %u, profondeur %u\n", hz, mhz, n, div, mcu,
	       depth);
	cinit();
	cadd(CMD_WRITE, CLK_CTL, CLK_LOW_FREQ, CLK_LOW_FREQ);
	cadd(CMD_WRITE, CLK_DIV, 0xFF, (uint8_t)(div << 4 | mcu));
	cadd(CMD_WRITE, SSC_CTL1, SSC_RSTB, 0);
	cadd(CMD_WRITE, SSC_CTL2, 0x07, (uint8_t)depth);
	cadd(CMD_WRITE, SSC_DIV_N_0, 0xFF, (uint8_t)n);
	cadd(CMD_WRITE, SSC_CTL1, SSC_RSTB, SSC_RSTB);
	r = csend(2000, "horloge");
	if (r < 0)
		return r;
	sd_sleep_ms(1); /* (SSC stable : 130 us) */
	wreg(CLK_CTL, CLK_LOW_FREQ, 0);
	rx.cur_clk = mhz;
	return 0;
}

static int set_width(struct sd_host *h, int bits)
{
	(void)h;
	return wreg(SD_CFG1, 0x03, bits == 4 ? 0x01 : 0x00);
}

static int present(struct sd_host *h)
{
	(void)h;
	uint32_t s = r32(BIPR);
	if (s & SD_INT)
		w32(BIPR, SD_INT); /* (changement de carte vu) */
	return s != 0xFFFFFFFF && (s & SD_EXIST);
}

static int write_protected(struct sd_host *h)
{
	(void)h;
	return (r32(BIPR) & SD_WRITE_PROTECT) != 0;
}

static void recover(struct sd_host *h)
{
	(void)h;
	stop_cmd();
	clear_error();
	w32(BIPR, TRANS_OK_INT | TRANS_FAIL_INT | CMD_DONE_INT | DATA_DONE_INT);
}

/* sd_power_on de Linux, rts5261_card_power_on, ou rtsx_bus_power_on d'OpenBSD */
static int power_up(struct sd_host *h)
{
	if (!present(h))
		return E_NOMEDIUM;
	sd_log("carte présente : coupée, puis alimentée en 3,3 V\n");
	power_off(h);
	sd_sleep_ms(250); /* (la carte repart à zéro, même si l'UEFI l'avait prise) */
	step("alimentation coupée");
	rx.cur_clk = 0;
	output_3v3();
	int r = set_clock(h, 400000, 0);
	if (r < 0) {
		sd_log("horloge de départ : %d\n", r);
		return r;
	}
	set_width(h, 1);
	sd_sleep_ms(100);
	cinit();
	cadd(CMD_WRITE, CARD_SELECT, 0x07, 0x02);     /* SD_MOD_SEL */
	cadd(CMD_WRITE, CARD_SHARE_MODE, 0x0F, 0x04); /* CARD_SHARE_48_SD */
	cadd(CMD_WRITE, CARD_CLK_EN, 0x04, 0x04);     /* SD_CLK_EN */
	if ((r = csend(100, "sélection")) < 0)
		return r;
	pull_ctl(1);
	if (rx.chip == CHIP_5261) {
		ocp(1);
		wreg(CRC_DUMMY_0, 0x01, 0x01);         /* CFG_SD_POW_AUTO_PD */
		wreg(R61_LDO1_CFG1, 0x0E, 0x0E);       /* LDO1 : 3,3 V */
		wreg(R61_LDO_POW_CTL, 0x01, 0x01);     /* LDO1 allumé */
		wreg(R61_LDO_POW_CTL, 0x08, 0x08);     /* LDO3318 allumé */
		sd_sleep_ms(20);
		wreg(CARD_OE, 0x04, 0x04);
		wreg(SD_CFG1, 0xFF, rx.ic > 2 ? 0xC0 : 0x80); /* diviseur de départ, SD 2.0, 1 bit */
		wreg(SD_SAMPLE_POINT, 0xFF, 0);
		wreg(SD_PUSH_POINT, 0xFF, 0);
		clear_error();
		wreg(SD_CFG3, 0x10, 0);
		wreg(SD_STOP_SDCLK_CFG, 0x07, 0);
	} else {
		if (rx.f525a)
			wreg(LDO_VCC_CFG1, 0x07, 0x07);
		wreg(CARD_PWR_CTL, 0x01, 0x01); /* demi-alimentation */
		wreg(PWR_GATE_CTRL, rx.f5209 ? 0x04 : 0x02, rx.f5209 ? 0x04 : 0x02);
		sd_sleep_ms(1);
		wreg(CARD_PWR_CTL, 0x03, 0); /* pleine */
		if (rx.f5209)
			wreg(PWR_GATE_CTRL, 0x06, 0);
		else
			wreg(PWR_GATE_CTRL, 0x04, 0x04);
	}
	sd_sleep_ms(1);
	wreg(CARD_OE, 0x04, 0x04);
	step("carte alimentée");
	if (rx.chip == CHIP_5261) {
		int oc = rreg(OCPSTAT);
		if (oc >= 0 && (oc & 0x06)) {
			sd_log("SURINTENSITÉ sur la carte (OCPSTAT %02x) : alimentation coupée\n", oc);
			power_off(h);
			return E_IO;
		}
	}
	/* 74 cycles au moins avant CMD0 (horloge forcée, puis rendue) */
	wreg(SD_BUS_STAT, 0x80, 0x80);
	sd_sleep_ms(5);
	wreg(SD_BUS_STAT, 0xC0, 0);
	if (!present(h))
		return E_NOMEDIUM;
	return 0;
}

/* ---- commandes */

static uint8_t rsp_type(unsigned rsp)
{
	switch (rsp) {
	case SD_RSP_NONE:
		return RSP_R0;
	case SD_RSP_R1B:
		return RSP_R1B;
	case SD_RSP_R2:
		return RSP_R2;
	case SD_RSP_R3:
		return RSP_R3;
	default:
		return RSP_R1;
	}
}

static void add_cmd(const struct sd_cmd *c)
{
	cadd(CMD_WRITE, SD_CMD0, 0xFF, (uint8_t)(0x40 | c->idx));
	cadd(CMD_WRITE, SD_CMD0 + 1, 0xFF, (uint8_t)(c->arg >> 24));
	cadd(CMD_WRITE, SD_CMD0 + 2, 0xFF, (uint8_t)(c->arg >> 16));
	cadd(CMD_WRITE, SD_CMD0 + 3, 0xFF, (uint8_t)(c->arg >> 8));
	cadd(CMD_WRITE, SD_CMD0 + 4, 0xFF, (uint8_t)c->arg);
}

static void add_len(unsigned blocks, unsigned blksz)
{
	cadd(CMD_WRITE, SD_BLOCK_CNT_L, 0xFF, (uint8_t)blocks);
	cadd(CMD_WRITE, SD_BLOCK_CNT_H, 0xFF, (uint8_t)(blocks >> 8));
	cadd(CMD_WRITE, SD_BYTE_CNT_L, 0xFF, (uint8_t)blksz);
	cadd(CMD_WRITE, SD_BYTE_CNT_H, 0xFF, (uint8_t)(blksz >> 8));
}

static void on_error(struct sd_cmd *c, int r)
{
	int st1 = rreg(SD_STAT1), st2 = rreg(SD_STAT2), tr = rreg(SD_TRANSFER);
	c->err_state = (uint32_t)(st1 & 0xFF) << 16 | (uint32_t)(st2 & 0xFF) << 8 | (uint32_t)(tr & 0xFF);
	sd_dbg("CMD%u : %d, SD_STAT1 %02x, SD_STAT2 %02x, SD_TRANSFER %02x\n", c->idx, r, st1, st2, tr);
	if (rx.trace && c->idx != 8 && c->idx != 55)
		dump(&host, "erreur de commande");
	recover(&host);
}

/* commande sans données (sd_send_cmd_get_rsp de la carte de Linux) */
static int cmd_rsp(struct sd_cmd *c)
{
	uint8_t t = rsp_type(c->rsp);
	cinit();
	add_cmd(c);
	cadd(CMD_WRITE, SD_CFG2, 0xFF, t);
	cadd(CMD_WRITE, CARD_DATA_SOURCE, 0x01, 0x01); /* ping-pong */
	cadd(CMD_WRITE, SD_TRANSFER, 0xFF, TM_CMD_RSP | TR_START);
	cadd(CMD_CHECK, SD_TRANSFER, TR_END | TR_IDLE, TR_END | TR_IDLE);
	if (t == RSP_R2)
		for (int i = 0; i < 16; i++)
			cadd(CMD_READ, PPBUF_BASE2 + i, 0, 0);
	else if (t != RSP_R0)
		for (int i = 0; i < 5; i++)
			cadd(CMD_READ, SD_CMD0 + i, 0, 0);
	cadd(CMD_READ, SD_STAT1, 0, 0);
	rx.n_cmds++;
	int r = csend(t == RSP_R1B ? 3000 : 100, "commande");
	if (r < 0) {
		on_error(c, r);
		return r == E_IO ? E_TIMEDOUT : r; /* (échec du tampon : le plus souvent pas de réponse) */
	}
	if (t == RSP_R0)
		return 0;
	const uint8_t *p = cresult() + 1; /* (l'octet 0 : la vérification) */
	if (p[0] & 0xC0) {
		sd_dbg("CMD%u : bits de départ faux (%02x)\n", c->idx, p[0]);
		return E_IO;
	}
	unsigned stat = t == RSP_R2 ? 16 : 5;
	if (!(t & NO_CHECK_CRC7) && (p[stat] & CRC7_ERR)) {
		sd_dbg("CMD%u : CRC7 faux\n", c->idx);
		return E_IO;
	}
	if (t == RSP_R2) {
		/* p[1..15] : bits 127-8 du registre ; convention SDHCI : resp[w],
		 * octet k = bits 8 + 32 w + 8 k */
		for (int w = 0; w < 4; w++) {
			uint32_t v = 0;
			for (int k = 3; k >= 0; k--) {
				int i = 15 - 4 * w - k; /* p[15] : bits 15-8 */
				v = v << 8 | (i >= 1 ? p[i] : 0);
			}
			c->resp[w] = v;
		}
	} else
		c->resp[0] = (uint32_t)p[1] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 8 | p[4];
	return 0;
}

/* petite lecture dans le tampon ping-pong (CMD6 : 64 o, ACMD51 : 8 o) */
static int cmd_small_read(struct sd_cmd *c)
{
	unsigned n = c->blksz;
	cinit();
	add_cmd(c);
	add_len(1, n);
	cadd(CMD_WRITE, SD_CFG2, 0xFF, 0x01); /* CRC7, CRC16, réponse de 6 octets */
	cadd(CMD_WRITE, CARD_DATA_SOURCE, 0x01, 0x01);
	cadd(CMD_WRITE, SD_TRANSFER, 0xFF, TM_NORMAL_READ | TR_START);
	cadd(CMD_CHECK, SD_TRANSFER, TR_END, TR_END);
	rx.n_cmds++;
	int r = csend(500, "petite lecture");
	if (r < 0) {
		on_error(c, r);
		return r;
	}
	cinit();
	for (unsigned i = 0; i < n; i++)
		cadd(CMD_READ, PPBUF_BASE2 + i, 0, 0);
	if ((r = csend(250, "ping-pong")) < 0)
		return r;
	memcpy(c->buf, cresult(), n);
	return 0;
}

static void sg_build(uint64_t pa, uint64_t len)
{
	unsigned i = 0;
	while (len) {
		uint64_t n = len > DESC_MAX ? DESC_MAX : len;
		len -= n;
		uint64_t opt = 0x01 | 0x20 | (len ? 0 : 0x02); /* VALID, TRANS_DATA, END */
		uint64_t d = rx.chip == CHIP_5261 ? (pa << 32 | n << 16 | opt) : (pa << 32 | n << 12 | opt);
		rx.sg[i++] = d;
		pa += n;
	}
	sd_wmb();
}

/* données de 512 o par blocs : lecture AUTO_READ_2 (la commande et les
 * données), écriture : la commande puis AUTO_WRITE_3 ; ADMA ; CMD12 si
 * plusieurs blocs */
static int cmd_data(struct sd_cmd *c)
{
	uint64_t len = (uint64_t)c->blocks * 512;
	uint64_t pa = c->phys;
	int bounce = !pa || pa + len > 0x100000000ull;
	if (bounce) {
		pa = rx.bounce_pa;
		if (c->write)
			memcpy(rx.bounce, c->buf, len);
	}
	int r;
	if (c->write) {
		struct sd_cmd x = *c;
		x.blocks = 0;
		if ((r = cmd_rsp(&x)) < 0)
			return r;
		c->resp[0] = x.resp[0];
	}
	cinit();
	if (!c->write)
		add_cmd(c);
	add_len(c->blocks, 512);
	cadd(CMD_WRITE, IRQSTAT0, 0x80, 0x80); /* DMA_DONE_INT acquitté */
	cadd(CMD_WRITE, DMATC0 + 3, 0xFF, (uint8_t)(len >> 24));
	cadd(CMD_WRITE, DMATC0 + 2, 0xFF, (uint8_t)(len >> 16));
	cadd(CMD_WRITE, DMATC0 + 1, 0xFF, (uint8_t)(len >> 8));
	cadd(CMD_WRITE, DMATC0, 0xFF, (uint8_t)len);
	cadd(CMD_WRITE, DMACTL, 0x03 | 0x30, (c->write ? 0x00 : 0x02) | 0x01 | 0x20); /* sens, DMA_EN, 512 */
	cadd(CMD_WRITE, CARD_DATA_SOURCE, 0x01, 0x00); /* tampon circulaire */
	if (c->write) {
		cadd(CMD_WRITE, SD_CFG2, 0xFF, 0x80 | 0x04 | 0x20); /* pas de CRC7 ni de réponse */
		cadd(CMD_WRITE, SD_TRANSFER, 0xFF, TR_START | TM_AUTO_WRITE3);
	} else {
		cadd(CMD_WRITE, SD_CFG2, 0xFF, 0x20 | RSP_R1);
		cadd(CMD_WRITE, SD_TRANSFER, 0xFF, TR_START | TM_AUTO_READ2);
	}
	cadd(CMD_CHECK, SD_TRANSFER, TR_END, TR_END);
	rx.n_cmds++;
	cstart();
	sg_build(pa, len);
	w32(HDBAR, (uint32_t)rx.sg_pa);
	w32(HDBCTLR, (c->write ? 0 : DMA_READ) | TRIG_DMA | ADMA_MODE);
	r = wait_trans(10000, c->write ? "écriture" : "lecture");
	if (r == 0) { /* la fin du DMA (DMA_DONE_INT), par prudence */
		int ok = 0;
		for (int i = 0; i < 100 && !ok; i++) {
			int s = rreg(IRQSTAT0);
			ok = s >= 0 && (s & 0x80);
			if (!ok && i == 0)
				sd_dbg("CMD%u : TRANS_OK avant DMA_DONE (IRQSTAT0 %02x)\n", c->idx, s);
			if (!ok && i > 10)
				sd_sleep_ms(1);
		}
		if (!ok)
			sd_log("CMD%u : DMA_DONE jamais vu après TRANS_OK (suite quand même)\n", c->idx);
	}
	if (r < 0) {
		on_error(c, r);
		if (c->blocks > 1) { /* (la carte peut être restée en transfert) */
			struct sd_cmd s = {.idx = 12, .rsp = SD_RSP_R1B};
			cmd_rsp(&s);
		}
		return r;
	}
	if (c->blocks > 1) {
		struct sd_cmd s = {.idx = 12, .rsp = SD_RSP_R1B};
		if ((r = cmd_rsp(&s)) < 0) {
			sd_log("CMD12 après CMD%u : %d\n", c->idx, r);
			return r;
		}
	}
	sd_rmb();
	if (bounce && !c->write)
		memcpy(c->buf, rx.bounce, len);
	return 0;
}

static int do_cmd(struct sd_host *h, struct sd_cmd *c)
{
	if (!present(h))
		return E_NOMEDIUM;
	if (!c->blocks)
		return cmd_rsp(c);
	if (c->blksz != 512 && !c->write && c->blocks == 1 && c->blksz <= 256)
		return cmd_small_read(c);
	if (c->blksz != 512 || c->blocks > MAX_BLOCKS)
		return E_INVAL;
	return cmd_data(c);
}

static const char *describe(struct sd_host *h)
{
	(void)h;
	snprintf(rx.desc, sizeof rx.desc, "ADMA, %s, SSC %u MHz", rx.irq >= 0 ? "interruptions MSI" : "sondage",
		 rx.cur_clk);
	return rx.desc;
}

/* ---- la puce */

static long cfg(unsigned off)
{
	return pci_cfg_read(rx.pi.addr, off, 4);
}

/* D0, ASPM coupé (Linux : rtsx_disable_aspm pendant le travail) */
static void pcie_prepare(void)
{
	if (rx.pi.cap_pm) {
		long pm = pci_cfg_read(rx.pi.addr, rx.pi.cap_pm + 4, 2);
		if (pm >= 0 && (pm & 3)) {
			sd_log("état d'énergie D%ld : passage en D0\n", pm & 3);
			pci_cfg_write(rx.pi.addr, rx.pi.cap_pm + 4, 2, (uint32_t)pm & ~3u);
			sd_sleep_ms(10);
		}
	}
	if (rx.pi.cap_pcie) {
		long lc = pci_cfg_read(rx.pi.addr, rx.pi.cap_pcie + 0x10, 2);
		if (lc >= 0 && (lc & 3)) {
			sd_log("ASPM coupé (LNKCTL %04lx)\n", lc);
			pci_cfg_write(rx.pi.addr, rx.pi.cap_pcie + 0x10, 2, (uint32_t)lc & ~3u);
		}
	}
}

/* rtsx_pci_init_hw et rts5261_extra_init_hw de la carte de Linux */
static int init_5261(void)
{
	rx.setting1 = (uint32_t)cfg(0x724);
	rx.setting2 = (uint32_t)cfg(0x814);
	sd_log("réglages du fabricant : 0x724 = %08x, 0x814 = %08x, 0x818 = %08lx, 0x81c = %08lx\n", rx.setting1,
	       rx.setting2, (unsigned long)cfg(0x818), (unsigned long)cfg(0x81C));
	if (rx.setting2 & 0x010000) {
		rx.reverse_socket = (rx.setting2 & 0x04) != 0;
		rx.drive_3v3 = (rx.setting1 >> 16) & 3;
		sd_log("réglages valides : prise %s, sortance 3,3 V n° %d%s\n", rx.reverse_socket ? "inversée" : "normale",
		       rx.drive_3v3, rx.setting2 & 0x08 ? ", RTD3 (ignoré)" : "");
	} else
		sd_log("réglages du fabricant pas valides : valeurs par défaut\n");
	int fw = rreg(R61_FW_STATUS), info0 = rreg(R61_FW_CFG_INFO0);
	sd_log("micrologiciel : état %02x%s, configuration %02x%s\n", fw, fw >= 0 && !(fw & 0x80) ? " (SD Express possible, pas utilisé)" : "",
	       info0, info0 >= 0 && (info0 & 1) ? " (MODE D'ESSAI D'USINE : SD Express forcé ?)" : "");
	w32(HCBAR, (uint32_t)rx.cmd_pa);
	w32(BIER, TRANS_OK_INT | TRANS_FAIL_INT | SD_INT | SD_OC_INT);
	wreg(R61_FW_CFG1, 0x02, 0); /* MCU_CLOCK_GATING */
	wreg(R61_FPDCTL, 0x01, 0);  /* SSC allumé */
	sd_sleep_ms(1);
	wreg(ASPM_FORCE_CTL, 0x33, 0x30); /* ASPM coupé (forcé) */
	wreg(SD_CFG1, 0x10, 0);
	step("SSC allumé");
	cinit();
	cadd(CMD_WRITE, CLK_DIV, 0x07, 0x07);
	cadd(CMD_WRITE, HOST_SLEEP_STATE, 0x03, 0x00);
	cadd(CMD_WRITE, CARD_CLK_EN, 0x1E, 0);
	cadd(CMD_WRITE, CHANGE_LINK_STATE, 0x0A, 0);
	cadd(CMD_WRITE, CARD_DRIVE_SEL, 0xFF, 0x41); /* MS 8 mA | GPIO 8 mA */
	cadd(CMD_WRITE, SSC_CTL1, 0xFF, SSC_8X_EN | SSC_SEL_4M);
	cadd(CMD_WRITE, SSC_CTL2, 0xFF, 0x03); /* profondeur 2M du 5261 */
	cadd(CMD_WRITE, CHANGE_LINK_STATE, 0x16, 0x10);
	cadd(CMD_WRITE, IRQSTAT0, 0x20, 0x20); /* LINK_RDY_INT acquitté */
	cadd(CMD_WRITE, PERST_GLITCH_WIDTH, 0xFF, 0x80);
	cadd(CMD_WRITE, RCCTL, 0x01, 0x00);       /* oscillateur RC à 400 kHz */
	cadd(CMD_WRITE, NFTS_TX_CTRL, 0x02, 0);   /* interruptions effacées par écriture */
	int r = csend(100, "initialisation");
	if (r < 0) {
		sd_log("premier tampon de commandes : %d (BIPR %08x) : le DMA de la puce ne marche pas ?\n", r, r32(BIPR));
		return r;
	}
	sd_log("premier tampon de commandes exécuté\n");
	wreg(PM_CLK_FORCE_CTL, 0x01, 0x01);
	/* OCP : LDO1, seuil 1040 mA, limite 2000 mA, filtre 800 us */
	wreg(R61_LDO1_CFG0, 0x12, 0x12);
	wreg(R61_LDO1_CFG0, 0xE0, 0xA0);
	wreg(R61_LDO1_CFG0, 0x0C, 0x0C);
	wreg(OCPGLITCH, 0x0F, 0x05);
	ocp(1);
	/* rts5261_extra_init_hw (sans l'efuse, ni le LTR, ni le RTD3) */
	wreg(R61_AUTOLOAD_CFG1, 0xF0, 0xF0); /* CD_RESUME_EN */
	wreg(R61_PME_FORCE_CTL, 0x03, 0x00); /* efuse éteint */
	wreg(L1SUB_CONFIG1, 0x01, 0x00);
	wreg(L1SUB_CONFIG3, 0xFF, 0);
	if (rx.ic > 1)
		w32(DUM_REG, r32(DUM_REG) | 1);
	wreg(R61_AUTOLOAD_CFG4, 0x20, 0); /* AUX_CLK_16M_EN */
	wreg(R61_AUTOLOAD_CFG4, 0x40, 0); /* PRSNT# relâché */
	wreg(FUNC_FORCE_CTL, 0x02, 0x02);
	wreg(PCLK_CTL, 0x20, 0x20);
	wreg(PM_EVENT_DEBUG, 0x08, 0x08);
	wreg(PM_CLK_FORCE_CTL, 0x01, 0x01);
	wreg(OLT_LED_CTL, 0x0F, 0x02);
	output_3v3();
	wreg(PETXCFG, 0x30, rx.reverse_socket ? 0x30 : 0);
	wreg(PETXCFG, 0x80, 0x80); /* CLKREQ# tenu bas : horloge PCIe toujours là */
	wreg(PWD_SUSPEND_EN, 0xFF, 0xFB);
	wreg(R61_AUTOLOAD_CFG3, 0x01, 0);
	wreg(R61_PME_FORCE_CTL, 0x30, 0x20);
	wreg(R61_AUTOLOAD_CFG3, 0x10, 0);
	wreg(R61_FW_CTL, 0x20, 0);
	wreg(ASPM_FORCE_CTL, 0x30, 0x30);
	return 0;
}

/* rtsx_init d'OpenBSD */
static int init_obsd(void)
{
	if (rx.pi.device == 0x5229 || rx.pi.device == 0x5249)
		rx.type_c = (rx.ic & 0x0F) == 2;
	wreg(NFTS_TX_CTRL, 0x02, 0);
	uint32_t s = r32(BIPR);
	w32(BIPR, s);
	w32(BIER, TRANS_OK_INT | TRANS_FAIL_INT | SD_INT);
	wreg(FPDCTL, 0x01, 0);
	sd_sleep_ms(1);
	if (wphy(0x00, rx.f5209 ? 0xB966 : 0xBA42) < 0)
		return E_IO;
	wreg(CLK_DIV, 0x07, 0x07);
	wreg(HOST_SLEEP_STATE, 0x03, 0);
	wreg(CARD_CLK_EN, 0x1E, 0);
	wreg(CHANGE_LINK_STATE, 0x0A | 0x04, 0);
	wreg(SD30_DRIVE_SEL_OLD, 0xFF, 0x01);
	wreg(SSC_CTL1, 0xFF, SSC_8X_EN | SSC_SEL_4M);
	wreg(SSC_CTL2, 0xFF, 0x12);
	wreg(CHANGE_LINK_STATE, 0x10, 0x10);
	wreg(IRQSTAT0, 0x20, 0x20);
	wreg(PERST_GLITCH_WIDTH, 0xFF, 0x80);
	wreg(RCCTL, 0x01, 0);
	wreg(PETXCFG_OLD, 0x08, 0x08);
	if (rx.f5209) {
		wreg(CARD_GPIO, 0xFF, 0x03);
		wreg(CARD_GPIO_DIR, 0xFF, 0x03);
	} else {
		wreg(GPIO_CTL, 0x02, 0x02);
		wreg(LDO_PWR_SEL, 0x03, 0);
		wreg(LDO_PWR_SEL, 0x01, 0x01);
		wreg(OLT_LED_CTL, 0x02, 0x02);
	}
	return 0;
}

static int b_read(void *ctx, uint64_t lba, unsigned n, void *buf, uint64_t phys)
{
	return sd_read(ctx, lba, n, buf, phys);
}

static int b_write(void *ctx, uint64_t lba, unsigned n, const void *buf, uint64_t phys)
{
	return sd_write(ctx, lba, n, buf, phys);
}

static uint64_t b_sectors(void *ctx)
{
	struct sd_card *c = ctx;
	return c->ready ? c->sectors : 0;
}

static int b_check(void *ctx, int *changed)
{
	struct sd_card *c = ctx;
	*changed = 0;
	if (rx.chip == CHIP_5261 && c->ready) {
		int oc = rreg(OCPSTAT);
		if (oc >= 0 && (oc & 0x06)) {
			sd_log("SURINTENSITÉ (OCPSTAT %02x) : carte coupée\n", oc);
			wreg(OCPCTL, 0x03, 0x03);
			sd_sleep_ms(1);
			wreg(OCPCTL, 0x03, 0);
			power_off(&host);
			c->ready = 0;
			return 0;
		}
	}
	if (sd_card_check(c))
		return 1;
	if (!present(&host)) {
		if (c->sectors && c->gen) {
			power_off(&host);
			c->sectors = 0;
		}
		return 0;
	}
	sd_log("carte insérée : identification\n");
	if (sd_card_init(c, &host) < 0)
		return 0;
	*changed = 1;
	return 1;
}

static void b_stats(void *ctx, uint64_t out[4])
{
	struct sd_card *c = ctx;
	out[0] = c->n_cmds;
	out[1] = c->n_errors;
	out[2] = c->n_retries;
	out[3] = c->n_blocks;
}

static const struct {
	uint16_t id;
	int chip, bar;
	const char *name;
} chips[] = {
	{0x5261, CHIP_5261, 0, "RTS5261"}, {0x5209, CHIP_OBSD, 0, "RTS5209"}, {0x5227, CHIP_OBSD, 0, "RTS5227"},
	{0x5229, CHIP_OBSD, 0, "RTS5229"}, {0x522A, CHIP_OBSD, 0, "RTS522A"}, {0x5249, CHIP_OBSD, 0, "RTS5249"},
	{0x525A, CHIP_OBSD, 1, "RTS525A"}, {0x5286, CHIP_OBSD, 0, "RTL8402"}, {0x5289, CHIP_OBSD, 0, "RTL8411"},
	{0x5287, CHIP_OBSD, 0, "RTL8411B"},
};

int main(int argc, char **argv)
{
	const char *arg = 0, *name = 0;
	int ro = 0, diag = 0;
	sd_tag = "rtsx";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v"))
			sd_verbose = 1;
		else if (!strcmp(argv[i], "-t"))
			rx.trace = 1, sd_verbose = 1;
		else if (!strcmp(argv[i], "-tt"))
			rx.trace = 2, sd_verbose = 1;
		else if (!strcmp(argv[i], "-r"))
			ro = 1;
		else if (!strcmp(argv[i], "-d"))
			diag = 1;
		else if (!strcmp(argv[i], "-n") && i + 1 < argc)
			name = argv[++i];
		else
			arg = argv[i];
	}
	rx.irq = -1;
	sd_log("lecteur Realtek PCIe : pilote JAMAIS ESSAYÉ (premier essai : rtsx -d, puis rtsx -t -r)\n");
	long r = pci_open(5000);
	if (r < 0) {
		sd_log("service pci absent (%ld)\n", r);
		return 1;
	}
	long addr = arg && *arg ? pci_parse_addr(arg) : -1;
	if (addr < 0) {
		long from = 0;
		struct pci_info f;
		while (addr < 0 && pci_find_class(0, 0, &from, &f) == 0)
			if (f.vendor == 0x10EC)
				for (unsigned k = 0; k < sizeof chips / sizeof *chips; k++)
					if (f.device == chips[k].id)
						addr = f.addr;
		if (addr < 0) {
			sd_log("aucun lecteur Realtek connu (10ec:5261…)\n");
			return 1;
		}
	}
	if ((r = pci_claim((uint32_t)addr, &rx.pi)) < 0) {
		sd_log("fonction refusée par pcid (%ld)\n", r);
		return 1;
	}
	pci_format_addr(rx.pi.addr, rx.addr);
	int bar = 0;
	const char *cname = "inconnu";
	for (unsigned k = 0; k < sizeof chips / sizeof *chips; k++)
		if (rx.pi.vendor == 0x10EC && rx.pi.device == chips[k].id) {
			rx.chip = chips[k].chip;
			bar = chips[k].bar;
			cname = chips[k].name;
		}
	if (!rx.chip) {
		sd_log("%s : %04x:%04x n'est pas un lecteur connu : profil du 5261 essayé\n", rx.addr, rx.pi.vendor,
		       rx.pi.device);
		rx.chip = CHIP_5261;
	}
	rx.f5209 = rx.pi.device == 0x5209;
	rx.f525a = rx.pi.device == 0x525A;
	sd_log("%s : %04x:%04x %s, classe %02x/%02x/%02x, révision %02x, sous-système %04x:%04x\n", rx.addr, rx.pi.vendor,
	       rx.pi.device, cname, rx.pi.cls, rx.pi.sub, rx.pi.prog, rx.pi.rev, rx.pi.svid, rx.pi.ssid);
	sd_log("%s : BAR%d %lx (%lu o), %s, PCIe %02x, énergie %02x\n", rx.addr, bar, (unsigned long)rx.pi.bar[bar],
	       (unsigned long)rx.pi.bar_size[bar],
	       rx.pi.cap_msix ? "MSI-X" : rx.pi.cap_msi ? "MSI" : "sans MSI", rx.pi.cap_pcie, rx.pi.cap_pm);
	long regs = pci_map_bar(&rx.pi, bar);
	if (regs < 0) {
		sd_log("BAR%d non mappée (%ld)\n", bar, regs);
		return 1;
	}
	rx.regs = (volatile uint8_t *)regs;
	if (r32(BIPR) == 0xFFFFFFFF) {
		sd_log("registres illisibles (BIPR = ffffffff) : puce endormie ou absente\n");
		pcie_prepare();
		if (r32(BIPR) == 0xFFFFFFFF)
			return 1;
	}
	int ic = rreg(DUMMY_REG);
	rx.ic = ic < 0 ? 0 : (uint8_t)(ic & 0x0F);
	sd_log("version de la puce %02x (%c), BIPR %08x : carte %s%s\n", ic, 'A' + rx.ic, r32(BIPR),
	       r32(BIPR) & SD_EXIST ? "présente" : "absente", r32(BIPR) & SD_WRITE_PROTECT ? ", protégée en écriture" : "");
	if (diag) { /* rien n'est écrit */
		dump(&host, "diagnostic, rien d'écrit");
		sd_log("configuration : 0x724 = %08lx, 0x814 = %08lx, 0x818 = %08lx, 0x81c = %08lx\n",
		       (unsigned long)cfg(0x724), (unsigned long)cfg(0x814), (unsigned long)cfg(0x818),
		       (unsigned long)cfg(0x81C));
		sd_log("diagnostic fini\n");
		return 0;
	}
	pcie_prepare();
	long ph = 0;
	long va = korin_sys2(SYS_DMA_ALLOC, 4096 + BOUNCE, &ph);
	if (va < 0 || (uint64_t)ph + 4096 + BOUNCE > 0x100000000ull) {
		sd_log("mémoire DMA sous 4 Go introuvable (%ld, %lx) : la puce n'a que 32 bits d'adresse\n", va,
		       (unsigned long)ph);
		return 1;
	}
	rx.cmd = (uint32_t *)va;
	rx.cmd_pa = (uint64_t)ph;
	rx.sg = (uint64_t *)(va + 1024);
	rx.sg_pa = (uint64_t)ph + 1024;
	rx.bounce = (uint8_t *)va + 4096;
	rx.bounce_pa = (uint64_t)ph + 4096;
	sd_log("mémoire DMA : commandes %lx, descripteurs %lx, tampon %lx\n", (unsigned long)rx.cmd_pa,
	       (unsigned long)rx.sg_pa, (unsigned long)rx.bounce_pa);
	long line;
	if (pci_irq_setup(&rx.pi, 1, &line) >= 1 && korin_sys3(SYS_IRQ_BIND, line, -1, 0) >= 0) {
		rx.irq = line;
		sd_log("interruption MSI%s sur la ligne %ld\n", rx.pi.cap_msix ? "-X" : "", line);
	} else
		sd_log("pas de MSI : sondage\n");
	step("départ");
	stop_cmd(); /* (l'UEFI a pu laisser un transfert : il lisait le noyau sur la carte) */
	w32(BIPR, 0xFFFFFFFF & ~(SD_EXIST | SD_WRITE_PROTECT | 0x60000));
	r = rx.chip == CHIP_5261 ? init_5261() : init_obsd();
	if (r < 0) {
		sd_log("initialisation de la puce ratée (%d)\n", r);
		dump(&host, "échec");
		return 1;
	}
	step("puce initialisée");
	host.name = "rtsx";
	host.priv = &rx;
	host.max_blocks = MAX_BLOCKS;
	host.caps = SDH_4BIT | SDH_HS;
	host.power_up = power_up;
	host.power_off = power_off;
	host.present = present;
	host.write_protected = write_protected;
	host.set_clock = set_clock;
	host.set_width = set_width;
	host.cmd = do_cmd;
	host.recover = recover;
	host.dump = dump;
	host.describe = describe;
	r = sd_card_init(&card, &host);
	step(r == 0 ? "carte prête" : "carte pas prête");
	if (r < 0 && r != E_NOMEDIUM)
		sd_log("carte pas prête (%d) : le service attend une autre carte\n", r);
	rx.trace = rx.trace > 1 ? 2 : 0; /* (les traces d'étapes : seulement à l'initialisation) */
	char nm[8];
	if (name)
		snprintf(nm, sizeof nm, "%s", name);
	else
		blk_free_name("sd", nm);
	static struct blk_driver d;
	d.read = b_read;
	d.write = b_write;
	d.sectors = b_sectors;
	d.check = b_check;
	d.stats = b_stats;
	d.ctx = &card;
	d.read_only = ro;
	sd_log("service blk.%s%s\n", nm, ro ? " (lecture seule)" : "");
	blk_serve(nm, &d, 500);
}
