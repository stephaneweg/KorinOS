/* plat_aarch64.c : le xHCI du Pi, repris de xh_init et xh_pcie_init de
 * src/svc/usbd/xhci.inc (JAMAIS ESSAYÉ : QEMU raspi4b n'émule pas le PCIe ;
 * l'asm reste sur la carte, voir docs/etude/PC-PILOTES.md) :
 *  * Pi 5 : premier xHCI du RP1, déjà relié par le micrologiciel ; le RP1
 *    voit la RAM à 0x10_0000_0000 ;
 *  * Pi 4 : pont PCIe du BCM2711 monté ici (remise à zéro, fenêtres, lien),
 *    microcode du VL805 par le VideoCore (NOTIFY_XHCI_RESET), BAR0 posée à
 *    0xF8000000 du bus = 0x6_0000_0000 du processeur ; DMA sous 1 Go ;
 *  * sous QEMU (révision du micrologiciel < 0x50000000) : absent.
 * Pas d'interruption : sondage, comme l'asm (la mémoire DMA n'est pas cachée,
 * les barrières de usbd.h suffisent). */
#include "usbd.h"

#define PCIE_PHYS 0xFD500000ull
#define PCIE_SIZE 0x10000
#define CLASS_REV 0x0008
#define CMD 0x0004
#define HDR_TYPE 0x000E
#define CACHE_LINE 0x000C
#define SEC_BUS 0x0019
#define SUB_BUS 0x001A
#define MEM_BASE 0x0020
#define MEM_LIMIT 0x0022
#define BRIDGE_CTL 0x003E
#define PCAP 0x00AC
#define LNKCAP (PCAP + 0x0C)
#define RTCTL (PCAP + 0x1C)
#define LNKCTL2 (PCAP + 0x30)
#define VENDOR_REG1 0x0188
#define PRIV1_ID_VAL3 0x043C
#define MISC_CTRL 0x4008
#define WIN0_LO 0x400C
#define WIN0_HI 0x4010
#define BAR1_LO 0x402C
#define BAR2_LO 0x4034
#define BAR2_HI 0x4038
#define BAR3_LO 0x403C
#define STATUS 0x4068
#define WIN0_BASE_LIMIT 0x4070
#define WIN0_BASE_HI 0x4080
#define WIN0_LIMIT_HI 0x4084
#define HARD_DEBUG 0x4204
#define INTR2_CLR 0x4308
#define INTR2_MASK_SET 0x4310
#define EXT_CFG_DATA 0x8000
#define EXT_CFG_INDEX 0x9000
#define SW_INIT_1 0x9210
#define D_BAR0 0x10
#define D_BAR1 0x14
#define D_INT_PIN 0x3D
#define CMD_VALUE 0x0146 /* MEMORY | MASTER | PARITY | SERR */
#define OUT_CPU 0x600000000ull
#define OUT_BUS 0xF8000000u
#define OUT_SIZE 0x4000000ull
#define IN_LOG2 30
#define VL805_BDF 0x00100000u
#define XHCI_CLASS 0x0C0330u
#define BAR_SIZE 0x10000
#define RP1_PHYS 0x1F00200000ull
#define RP1_DMA 0x1000000000ull
#define DMA_MAX 0x40000000ull

static volatile uint8_t *pcie;
static uint32_t fw_msg[8] __attribute__((aligned(16)));

#define R32(o) (*(volatile uint32_t *)(pcie + (o)))
#define R16(o) (*(volatile uint16_t *)(pcie + (o)))
#define R8(o) (*(volatile uint8_t *)(pcie + (o)))

void plat_udelay(unsigned us)
{
	uint64_t f, t;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
	__asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
	uint64_t end = t + us * f / 1000000;
	do
		__asm__ volatile("mrs %0, cntpct_el0" : "=r"(t));
	while (t < end);
}

static long fw_call(uint32_t tag, uint32_t value)
{
	fw_msg[0] = 32;
	fw_msg[1] = 0;
	fw_msg[2] = tag;
	fw_msg[3] = 4;
	fw_msg[4] = 0;
	fw_msg[5] = value;
	fw_msg[6] = 0;
	fw_msg[7] = 0;
	return korin_sys1(SYS_FW_CALL, fw_msg);
}

/* -> 1 sous émulateur (révision du micrologiciel < 0x50000000) */
static int emulated(void)
{
	if (fw_call(1, 0) < 0 || !fw_msg[5]) /* GET_FIRMWARE_REVISION */
		return 0;
	return fw_msg[5] < 0x50000000u;
}

/* -> 1 si le lien est monté et le xHCI du VL805 activé */
static int pcie_init(void)
{
	R32(SW_INIT_1) |= 2; /* INIT_GENERIC : pont en reset */
	R32(SW_INIT_1) |= 1; /* PERST# */
	(void)R32(SW_INIT_1);
	plat_udelay(200);
	R32(SW_INIT_1) &= ~2u; /* pont hors reset (PERST# reste actif) */
	(void)R32(SW_INIT_1);
	R32(HARD_DEBUG) &= ~(1u << 27); /* SERDES_IDDQ = 0 : PHY alimenté */
	(void)R32(HARD_DEBUG);
	plat_udelay(200);

	uint32_t v = R32(MISC_CTRL);
	v |= 0x3000;      /* SCB_ACCESS_EN | CFG_READ_UR_MODE */
	v &= ~0x300000u;  /* MAX_BURST_SIZE = 128 octets */
	v &= 0x07FFFFFFu; /* SCB0_SIZE = log2(1 Go) - 15 */
	v |= (uint32_t)(IN_LOG2 - 15) << 27;
	R32(MISC_CTRL) = v;
	R32(BAR2_LO) = IN_LOG2 - 15; /* fenêtre entrante : bus 0, 1 Go */
	R32(BAR2_HI) = 0;
	R32(BAR1_LO) &= ~0x1Fu;
	R32(BAR3_LO) &= ~0x1Fu;
	R32(INTR2_CLR) = 0xFFFFFFFF; /* IRQ du pont : acquittées et masquées */
	(void)R32(INTR2_CLR);
	R32(INTR2_MASK_SET) = 0xFFFFFFFF;
	(void)R32(INTR2_MASK_SET);
	R32(LNKCAP) = (R32(LNKCAP) & ~0xFu) | 2; /* Gen2 au plus */
	R16(LNKCTL2) = (uint16_t)((R16(LNKCTL2) & ~0xFu) | 2);

	R32(SW_INIT_1) &= ~1u; /* PERST# relâché */
	korin_sys1(SYS_SLEEP, 100);
	int up = 0;
	for (int i = 0; i < 20 && !up; i++) {
		up = (R32(STATUS) & 0x30) == 0x30; /* PHYLINKUP | DL_ACTIVE */
		if (!up)
			korin_sys1(SYS_SLEEP, 5);
	}
	if (!up || !(R32(STATUS) & 0x80)) /* complexe racine */
		return 0;

	R32(WIN0_LO) = OUT_BUS; /* fenêtre sortante 0x6_0000_0000 -> 0xF8000000 */
	R32(WIN0_HI) = 0;
	v = R32(WIN0_BASE_LIMIT) & ~0xFFF0u & ~0xFFF00000u;
	v |= (uint32_t)(((OUT_CPU >> 20) & 0xFFF) << 4) | (uint32_t)((((OUT_CPU + OUT_SIZE - 1) >> 20) & 0xFFF) << 20);
	R32(WIN0_BASE_LIMIT) = v;
	R32(WIN0_BASE_HI) = (R32(WIN0_BASE_HI) & ~0xFFu) | (uint32_t)((OUT_CPU >> 32) & 0xFF);
	R32(WIN0_LIMIT_HI) = (R32(WIN0_LIMIT_HI) & ~0xFFu) | (uint32_t)(((OUT_CPU + OUT_SIZE - 1) >> 32) & 0xFF);
	R32(PRIV1_ID_VAL3) = (R32(PRIV1_ID_VAL3) & 0xFF000000u) | 0x060400; /* pont PCI-PCI */
	(void)R32(PRIV1_ID_VAL3);
	R32(VENDOR_REG1) &= ~0xCu; /* ENDIAN_MODE_BAR2 = données */
	(void)R32(VENDOR_REG1);
	R32(HARD_DEBUG) |= 2; /* CLKREQ_DEBUG_ENABLE */
	(void)R32(HARD_DEBUG);

	/* pont (bus 0, dev 0) */
	if ((R32(CLASS_REV) >> 8) != 0x060400 || (R8(HDR_TYPE) & 0x7F) != 1)
		return 0;
	R8(CACHE_LINE) = 16;
	R8(SEC_BUS) = 1;
	R8(SUB_BUS) = 1;
	R16(MEM_BASE) = OUT_BUS >> 16;
	R16(MEM_LIMIT) = OUT_BUS >> 16;
	R8(BRIDGE_CTL) = 1;
	R8(RTCTL) = 0x10; /* CRS Software Visibility */
	R16(CMD) = CMD_VALUE;

	fw_call(0x00030058, VL805_BDF); /* NOTIFY_XHCI_RESET (réponse ignorée : vieux firmwares) */
	korin_sys1(SYS_SLEEP, 10);

	/* 01:00.0 = xHCI */
	R32(EXT_CFG_INDEX) = VL805_BDF;
	(void)R32(EXT_CFG_INDEX);
	volatile uint8_t *d = pcie + EXT_CFG_DATA;
	if ((*(volatile uint32_t *)(d + CLASS_REV) >> 8) != XHCI_CLASS || (d[HDR_TYPE] & 0x7F) != 0)
		return 0;
	d[CACHE_LINE] = 16;
	*(volatile uint32_t *)(d + D_BAR0) = OUT_BUS | 4; /* mémoire 64 bits */
	*(volatile uint32_t *)(d + D_BAR1) = 0;
	if (d[D_INT_PIN] != 1)
		d[D_INT_PIN] = 1;
	*(volatile uint16_t *)(d + CMD) = CMD_VALUE;
	(void)*(volatile uint16_t *)(d + CMD);
	return 1;
}

int plat_xhci_init(const char *arg, struct xh_plat *p)
{
	(void)arg;
	static int done;
	if (done++) /* un seul contrôleur sur le Pi (VL805 ou RP1) */
		return E_EXIST;
	if (emulated()) {
		say("usbd : xhci : absent (émulateur, pas de PCIe)\n");
		return E_NODEV;
	}
	p->regs_size = BAR_SIZE;
	p->irq_setup = 0;
	long r = korin_sys2(SYS_DEV_MAP, RP1_PHYS, BAR_SIZE); /* Pi 5 (refusé sur un Pi 4) */
	if (r >= 0) {
		say("usbd : xhci : RP1 (Pi 5)\n");
		p->regs = (volatile uint8_t *)r;
		p->dma_offset = RP1_DMA;
		p->dma_limit = 0;
		p->name = "pi5";
		return 0;
	}
	r = korin_sys2(SYS_DEV_MAP, PCIE_PHYS, PCIE_SIZE);
	if (r < 0)
		return E_NODEV;
	pcie = (volatile uint8_t *)r;
	if (!pcie_init()) {
		say("usbd : xhci : pas de lien PCIe\n");
		return E_NODEV;
	}
	say("usbd : xhci : lien PCIe, VL805 activé\n");
	r = korin_sys2(SYS_DEV_MAP, OUT_CPU, BAR_SIZE);
	if (r < 0)
		return (int)r;
	p->regs = (volatile uint8_t *)r;
	p->dma_offset = 0;
	p->dma_limit = DMA_MAX; /* fenêtre entrante : 1 Go */
	p->name = "pi4";
	return 0;
}
