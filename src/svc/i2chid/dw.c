/* dw.c - contrôleur I2C DesignWare (Synopsys DW_apb_i2c) du LPSS d'Intel, maître seulement, en sondage
 * (docs/etude/TOUCHPAD-PC.md). Registres d'après la documentation de Synopsys reprise par Intel
 * (« Intel 500 Series Chipset Family PCH Datasheet, Volume 2 », chapitre I2C) et le pilote
 * i2c-designware de Linux (calcul de HCNT / LCNT, ordre des commandes, STOP sur le dernier octet). */
#include "i2chid.h"

#define IC_CON 0x00
#define IC_TAR 0x04
#define IC_DATA_CMD 0x10
#define IC_SS_SCL_HCNT 0x14
#define IC_SS_SCL_LCNT 0x18
#define IC_FS_SCL_HCNT 0x1C
#define IC_FS_SCL_LCNT 0x20
#define IC_INTR_MASK 0x30
#define IC_RAW_INTR_STAT 0x34
#define IC_RX_TL 0x38
#define IC_TX_TL 0x3C
#define IC_CLR_INTR 0x40
#define IC_CLR_TX_ABRT 0x54
#define IC_CLR_STOP_DET 0x60
#define IC_ENABLE 0x6C
#define IC_STATUS 0x70
#define IC_TXFLR 0x74
#define IC_RXFLR 0x78
#define IC_SDA_HOLD 0x7C
#define IC_TX_ABRT_SOURCE 0x80
#define IC_ENABLE_STATUS 0x9C
#define IC_COMP_PARAM_1 0xF4
#define IC_COMP_VERSION 0xF8
#define IC_COMP_TYPE 0xFC
#define DW_COMP_TYPE 0x44570140

#define CON_MASTER 1
#define CON_SPEED_STD (1 << 1)
#define CON_SPEED_FAST (2 << 1)
#define CON_RESTART_EN (1 << 5)
#define CON_SLAVE_DISABLE (1 << 6)

#define CMD_READ (1 << 8)
#define CMD_STOP (1 << 9)
#define CMD_RESTART (1 << 10)

#define INTR_TX_ABRT (1 << 6)
#define INTR_STOP_DET (1 << 9)

#define ST_ACTIVITY 1
#define ST_RFNE (1 << 3)
#define ST_MST_ACTIVITY (1 << 5)

/* registres privés du LPSS (BAR0 + 0x200), d'après intel-lpss.c de Linux */
#define LPSS_PRIV 0x200
#define PRIV_CLOCK_PARAMS 0x00 /* bit 0 : horloge en marche */
#define PRIV_RESETS 0x04       /* bits 0-1 : fonction hors reset, bit 2 : iDMA hors reset */
#define PRIV_REMAP_ADDR 0x40   /* adresse de la BAR (pour l'iDMA) */
#define PRIV_CAPS 0xFC         /* bits 7-4 : sorte (0 I2C, 1 UART, 2 SPI), bit 8 : pas d'iDMA */

static u32 rd(struct dw *d, u32 off)
{
	return *(volatile u32 *)(d->regs + off);
}

static void wr(struct dw *d, u32 off, u32 v)
{
	*(volatile u32 *)(d->regs + off) = v;
}

static long ms(void)
{
	return korin_sys0(SYS_TIME_MS);
}

static void relax(void)
{
	__asm__ volatile("pause");
}

int lpss_init(struct dw *d)
{
	u32 caps = rd(d, LPSS_PRIV + PRIV_CAPS);
	if (caps == 0xFFFFFFFF) {
		slog("registres illisibles (0xFFFFFFFF) : la BAR ne décode pas, ou la fonction dort (D3)\n");
		return -1;
	}
	if ((caps >> 4 & 0xF) != 0) {
		slog("le LPSS dit que cette fonction n'est pas un I2C (capacités %08x, sorte %u)\n", caps, caps >> 4 & 0xF);
		return -1;
	}
	if (d->verbose)
		slog("LPSS : capacités %08x, horloge %08x, resets %08x\n", caps, rd(d, LPSS_PRIV + PRIV_CLOCK_PARAMS),
		     rd(d, LPSS_PRIV + PRIV_RESETS));
	/* reset puis sortie de reset (fonction et iDMA), comme intel_lpss_init_dev de Linux */
	wr(d, LPSS_PRIV + PRIV_RESETS, 0);
	wr(d, LPSS_PRIV + PRIV_RESETS, 7);
	/* l'adresse physique de la BAR, pour l'iDMA (inutile ici : pas de DMA ; comme Linux) */
	wr(d, LPSS_PRIV + PRIV_REMAP_ADDR, (u32)d->phys);
	wr(d, LPSS_PRIV + PRIV_REMAP_ADDR + 4, (u32)(d->phys >> 32));
	wr(d, LPSS_PRIV + PRIV_CLOCK_PARAMS, rd(d, LPSS_PRIV + PRIV_CLOCK_PARAMS) | 1);
	return 0;
}

void dw_disable(struct dw *d)
{
	wr(d, IC_ENABLE, 0);
	long t = ms();
	while ((rd(d, IC_ENABLE_STATUS) & 1) && ms() - t < 25)
		relax();
}

static void dw_enable(struct dw *d)
{
	wr(d, IC_ENABLE, 1);
}

/* d'après i2c_dw_scl_hcnt / i2c_dw_scl_lcnt de Linux (cond = 0, décalage 0), ic_clk en kHz */
static u32 scl_hcnt(u32 khz, u32 t_ns, u32 tf_ns)
{
	u64 v = ((u64)khz * (t_ns + tf_ns) + 500000) / 1000000;
	return v > 3 ? (u32)v - 3 : 1;
}

static u32 scl_lcnt(u32 khz, u32 t_ns, u32 tf_ns)
{
	u64 v = ((u64)khz * (t_ns + tf_ns) + 500000) / 1000000;
	return v > 1 ? (u32)v - 1 : 1;
}

int dw_init(struct dw *d)
{
	u32 type = rd(d, IC_COMP_TYPE), ver = rd(d, IC_COMP_VERSION), p1 = rd(d, IC_COMP_PARAM_1);
	if (type != DW_COMP_TYPE) {
		slog("pas un DesignWare (IC_COMP_TYPE %08x au lieu de %08x)\n", type, DW_COMP_TYPE);
		return -1;
	}
	d->tx_depth = ((p1 >> 16) & 0xFF) + 1;
	d->rx_depth = ((p1 >> 8) & 0xFF) + 1;
	if (p1 == 0 || d->tx_depth < 2)
		d->tx_depth = d->rx_depth = 32;
	dw_disable(d);
	u32 hs = scl_hcnt(d->clk_khz, 4000, 300), ls = scl_lcnt(d->clk_khz, 4700, 300);
	u32 hf = scl_hcnt(d->clk_khz, 600, 300), lf = scl_lcnt(d->clk_khz, 1300, 300);
	if (d->hcnt && d->lcnt) { /* (FMCN / SSCN de l'ACPI : i2chid.txt) */
		if (d->khz > 100)
			hf = d->hcnt, lf = d->lcnt;
		else
			hs = d->hcnt, ls = d->lcnt;
	}
	if (d->verbose)
		slog("DesignWare %c.%c%c, FIFO %u/%u, avant : SS %u/%u FS %u/%u, maintien SDA %08x\n", (char)(ver >> 24),
		     (char)(ver >> 16), (char)(ver >> 8), d->tx_depth, d->rx_depth, rd(d, IC_SS_SCL_HCNT),
		     rd(d, IC_SS_SCL_LCNT), rd(d, IC_FS_SCL_HCNT), rd(d, IC_FS_SCL_LCNT), rd(d, IC_SDA_HOLD));
	wr(d, IC_SS_SCL_HCNT, hs);
	wr(d, IC_SS_SCL_LCNT, ls);
	wr(d, IC_FS_SCL_HCNT, hf);
	wr(d, IC_FS_SCL_LCNT, lf);
	/* maintien de SDA après la descente de SCL (version 1.11a et plus) */
	if (ver >= 0x3131312A) {
		u32 hold = d->sda_hold_cycles ? d->sda_hold_cycles
					      : (u32)(((u64)d->clk_khz * (d->sda_hold_ns ? d->sda_hold_ns : 300) + 500000) / 1000000);
		u32 cur = rd(d, IC_SDA_HOLD);
		if ((cur & 0xFFFF) < hold)
			wr(d, IC_SDA_HOLD, (cur & 0xFF0000) | (hold & 0xFFFF));
	}
	wr(d, IC_CON, CON_MASTER | CON_SLAVE_DISABLE | CON_RESTART_EN | (d->khz > 100 ? CON_SPEED_FAST : CON_SPEED_STD));
	wr(d, IC_INTR_MASK, 0); /* (sondage : aucune interruption du contrôleur) */
	wr(d, IC_RX_TL, 0);
	wr(d, IC_TX_TL, d->tx_depth / 2);
	d->tar = 0xFFFF;
	u32 khz = (u32)((u64)d->clk_khz / ((d->khz > 100 ? hf + lf : hs + ls) + 8));
	slog("contrôleur DesignWare : horloge %u MHz, bus %u kHz (HCNT %u, LCNT %u : environ %u kHz)\n",
	     d->clk_khz / 1000, d->khz, d->khz > 100 ? hf : hs, d->khz > 100 ? lf : ls, khz);
	return 0;
}

int dw_xfer(struct dw *d, u32 addr, const u8 *w, u32 wn, u8 *r, u32 rn)
{
	if (!wn && !rn)
		return 0;
	if (addr != d->tar) {
		dw_disable(d);
		wr(d, IC_TAR, addr & 0x7F);
		d->tar = addr;
		dw_enable(d);
	} else if (!(rd(d, IC_ENABLE_STATUS) & 1))
		dw_enable(d);
	(void)rd(d, IC_CLR_INTR);
	/* l'ordre des commandes : les écritures, puis les lectures (RESTART sur la première), STOP sur la
	   dernière ; les lectures en vol jamais plus que la FIFO de réception */
	u32 total = wn + rn, sent = 0, got = 0;
	long t0 = ms(), limit = 50 + (long)total / 4;
	while (got < rn || sent < total) {
		u32 raw = rd(d, IC_RAW_INTR_STAT);
		if (raw & INTR_TX_ABRT) {
			d->abort_src = rd(d, IC_TX_ABRT_SOURCE);
			(void)rd(d, IC_CLR_TX_ABRT);
			long t = ms();
			while (!(rd(d, IC_RAW_INTR_STAT) & INTR_STOP_DET) && ms() - t < 5)
				relax();
			(void)rd(d, IC_CLR_INTR);
			return -1;
		}
		while (sent < total && rd(d, IC_TXFLR) < d->tx_depth) {
			u32 cmd;
			if (sent < wn)
				cmd = w[sent];
			else {
				if (sent - wn - got >= d->rx_depth)
					break; /* (la FIFO de réception déborderait) */
				cmd = CMD_READ;
				if (sent == wn && wn)
					cmd |= CMD_RESTART;
			}
			if (sent == total - 1)
				cmd |= CMD_STOP;
			wr(d, IC_DATA_CMD, cmd);
			sent++;
		}
		while (got < rn && (rd(d, IC_STATUS) & ST_RFNE))
			r[got++] = (u8)rd(d, IC_DATA_CMD);
		if (ms() - t0 > limit) {
			slog("délai dépassé (cible %02x : %u/%u commandes, %u/%u octets lus, état %08x)\n", addr, sent, total,
			     got, rn, rd(d, IC_STATUS));
			dw_disable(d);
			d->tar = 0xFFFF;
			return -2;
		}
		relax();
	}
	/* le STOP */
	long t = ms();
	while (!(rd(d, IC_RAW_INTR_STAT) & INTR_STOP_DET)) {
		if (rd(d, IC_RAW_INTR_STAT) & INTR_TX_ABRT) {
			d->abort_src = rd(d, IC_TX_ABRT_SOURCE);
			(void)rd(d, IC_CLR_INTR);
			return -1;
		}
		if (ms() - t > 10)
			break;
		relax();
	}
	(void)rd(d, IC_CLR_STOP_DET);
	return 0;
}
