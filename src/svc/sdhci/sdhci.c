/* sdhci - la carte SD par un contrôleur SDHCI, en service, en C
 * (docs/etude/PC-PILOTES.md, « Carte SD »). D'après la « SD Host Controller
 * Simplified Specification » publique (version 4.20) et l'asm du Pi
 * (src/svc/sd/sdhci.asm), qu'il reprend :
 *  * registres lus et écrits par mots de 32 bits seulement (l'EMMC2 du Pi
 *    l'exige ; le PC l'accepte) ;
 *  * horloge : diviseur de 10 bits (version 3 et plus) ou puissance de 2
 *    (versions 1 et 2 : sdhci-pci de QEMU) ; base des capacités, sinon de la
 *    machine (le micrologiciel du Pi) ;
 *  * commandes avec données en PIO (registre DATA) ou en ADMA2 (descripteurs
 *    de 32 bits, ou de 64 bits si le contrôleur version 3 l'annonce), CMD12
 *    automatique pour les blocs multiples ; le DMA va droit dans le tampon du
 *    client quand son adresse physique est connue et atteignable, sinon par un
 *    tampon de 128 Ko ; il n'est pris qu'après un essai (début de la carte lu
 *    en PIO puis en DMA, identique), et quitté sur une erreur ;
 *  * fin d'une commande attendue par interruption (MSI, IRQ_WAIT) si la
 *    machine en donne, sinon en sondant ;
 *  * retrait et remise de la carte (registre d'état), puis le service blk
 *    (blkserv.c) : « blk.sd0 », « blk.sd0p1 »…
 *
 *	sdhci [-v] [-p] [-r] [-d] [-n NOM] [SSSS:BB:DD.F]
 *	  -v  chaque commande au journal      -p  PIO seulement (pas de DMA)
 *	  -r  lecture seule                   -d  diagnostic : registres, carte, fin
 *	  -n  nom du disque (défaut : sd0, sinon le premier sdN libre)
 *	  l'adresse PCIe (PC : donnée par hwmanager d'après drivers.txt, pci:08/05) */
#include "sdhci.h"

/* ---- registres (mots de 32 bits) */
#define R_SDMA 0x00
#define R_BLOCK 0x04 /* taille (12 bits, 14:12 frontière SDMA) | nombre << 16 */
#define R_ARG 0x08
#define R_CMD 0x0C /* mode de transfert (16) | commande << 16 */
#define R_RESP0 0x10
#define R_DATA 0x20
#define R_PRESENT 0x24
#define R_HOST 0x28  /* contrôle 1 (8) | alimentation << 8 | intervalle << 16 | réveil << 24 */
#define R_CLOCK 0x2C /* horloge (16) | délai << 16 | remise à zéro << 24 */
#define R_INT 0x30   /* état normal (16) | erreurs << 16 */
#define R_INT_EN 0x34
#define R_SIG_EN 0x38
#define R_HOST2 0x3C /* erreur de l'Auto CMD (16) | contrôle 2 << 16 */
#define R_CAPS 0x40
#define R_CAPS1 0x44
#define R_ADMA_ERR 0x54
#define R_ADMA 0x58
#define R_ADMA_HI 0x5C
#define R_VERSION 0xFC /* état des emplacements (16) | version << 16 */

#define TM_DMA (1u << 0)
#define TM_BCNT (1u << 1)
#define TM_ACMD12 (1u << 2)
#define TM_READ (1u << 4)
#define TM_MULTI (1u << 5)
#define CF_136 1
#define CF_48 2
#define CF_48B 3
#define CF_CRC (1u << 3)
#define CF_IDX (1u << 4)
#define CF_DATA (1u << 5)

#define PS_CMD_INHIBIT (1u << 0)
#define PS_DAT_INHIBIT (1u << 1)
#define PS_INSERTED (1u << 16)
#define PS_STABLE (1u << 17)
#define PS_WP (1u << 19) /* 1 : écriture permise */

#define HC_4BIT (1u << 1)
#define HC_HS (1u << 2)
#define HC_DMA_MASK (3u << 3)
#define HC_ADMA32 (2u << 3)
#define HC_ADMA64 (3u << 3)

#define CK_INT_EN (1u << 0)
#define CK_STABLE (1u << 1)
#define CK_SD_EN (1u << 2)
#define RST_ALL (1u << 24)
#define RST_CMD (1u << 25)
#define RST_DAT (1u << 26)

#define I_CMD (1u << 0)
#define I_XFER (1u << 1)
#define I_DMA (1u << 3)
#define I_WRITE (1u << 4)
#define I_READ (1u << 5)
#define I_INSERT (1u << 6)
#define I_REMOVE (1u << 7)
#define I_ERR (1u << 15)
#define I_ERRORS 0xFFFF0000u
#define E_CMD_TIMEOUT (1u << 16)
#define E_DATA_TIMEOUT (1u << 20)
#define E_ADMA (1u << 25)

#define CAP_ADMA2 (1u << 19)
#define CAP_HS (1u << 21)
#define CAP_64BIT (1u << 28)

#define ADMA_VALID 1u
#define ADMA_END 2u
#define ADMA_TRAN 0x20u
#define ADMA_MAX 0xFE00u /* octets par descripteur (multiple de 512) */

#define MAX_BLOCKS 256 /* 128 Ko par commande (comme l'asm) */
#define BOUNCE (MAX_BLOCKS * 512)
#define TABLE 4096
#define CMD_MS 500
#define DATA_MS 3000
#define TEST_BLOCKS 64

struct sdhci {
	struct sdhci_plat p;
	unsigned version; /* 0 : 1.00, 1 : 2.00, 2 : 3.00… */
	uint32_t caps, caps1;
	uint64_t base_hz;
	unsigned clock_hz;
	int dma_ok, dma64, force_pio;
	uint8_t *bounce, *table;
	uint64_t bounce_pa, table_pa;
	long irq;
	uint64_t n_dma, n_pio, n_irq_waits;
	char desc[96];
};

static struct sdhci hc;
static struct sd_host host;
static struct sd_card card;

static inline uint32_t rd(unsigned r)
{
	return *(volatile uint32_t *)(hc.p.regs + r);
}

static inline void wr(unsigned r, uint32_t v)
{
	*(volatile uint32_t *)(hc.p.regs + r) = v;
}

/* attend que (registre & bits) vaille want -> 1, 0 au bout de ms */
static int wait_reg(unsigned r, uint32_t bits, uint32_t want, long ms)
{
	long end = sd_now_ms() + ms;
	for (int spin = 0;; spin++) {
		if ((rd(r) & bits) == want)
			return 1;
		if (sd_now_ms() > end)
			return (rd(r) & bits) == want;
		if (spin > 64)
			korin_sys0(SYS_YIELD);
	}
}

static void reset(uint32_t what)
{
	wr(R_CLOCK, (rd(R_CLOCK) & 0x00FFFFFF) | what);
	if (!wait_reg(R_CLOCK, what, 0, 200))
		sd_log("remise à zéro %x : jamais finie\n", what >> 24);
}

/* attend un bit de R_INT (ou une erreur) -> état lu (bits attendus acquittés),
 * ou 0 au bout de ms ; par l'interruption si la machine en donne */
static uint32_t wait_int(uint32_t bits, long ms)
{
	long end = sd_now_ms() + ms;
	for (int spin = 0;; spin++) {
		uint32_t s = rd(R_INT);
		if (s & (bits | I_ERR)) {
			wr(R_INT, s & (bits | I_ERRORS | I_ERR));
			return s;
		}
		long left = end - sd_now_ms();
		if (left < 0) {
			s = rd(R_INT);
			if (s & (bits | I_ERR)) {
				wr(R_INT, s & (bits | I_ERRORS | I_ERR));
				return s;
			}
			return 0;
		}
		if (hc.irq >= 0 && spin > 32) {
			hc.n_irq_waits++;
			korin_sys2(SYS_IRQ_WAIT, hc.irq, left + 1);
		} else if (spin > 256)
			sd_sleep_ms(1);
		else if (spin > 32)
			korin_sys0(SYS_YIELD);
	}
}

static int set_clock(struct sd_host *h, unsigned hz, int hs)
{
	(void)h;
	uint32_t ck = rd(R_CLOCK);
	wr(R_CLOCK, ck & ~(CK_SD_EN | 0xFFFF0000u) & ~(RST_ALL | RST_CMD | RST_DAT));
	uint32_t host1 = rd(R_HOST);
	if (hs && (hc.caps & CAP_HS))
		host1 |= HC_HS;
	else
		host1 &= ~HC_HS;
	wr(R_HOST, host1);
	unsigned div;
	uint64_t f;
	if (hc.version >= 2) { /* 10 bits : f = base / (2 N), N = 0 : base */
		div = hz >= hc.base_hz ? 0 : (unsigned)((hc.base_hz + 2ull * hz - 1) / (2ull * hz));
		if (div > 0x3FF)
			div = 0x3FF;
		f = div ? hc.base_hz / (2 * div) : hc.base_hz;
	} else { /* 8 bits, puissances de 2 */
		unsigned n = 1;
		while (n < 256 && hc.base_hz / n > hz)
			n <<= 1;
		div = n / 2; /* f = base / (2 N) ; N = 0 : base */
		f = hc.base_hz / n;
	}
	uint32_t v = (div & 0xFF) << 8 | ((div >> 8) & 3) << 6 | CK_INT_EN | 0xE0000; /* délai des données au plus long */
	wr(R_CLOCK, v);
	if (!wait_reg(R_CLOCK, CK_STABLE, CK_STABLE, 200)) {
		sd_log("horloge interne jamais stable (diviseur %u)\n", div);
		return E_IO;
	}
	wr(R_CLOCK, (rd(R_CLOCK) & 0x00FFFFFF) | CK_SD_EN);
	sd_sleep_ms(1);
	hc.clock_hz = (unsigned)f;
	sd_dbg("horloge %u Hz demandée : %lu Hz (base %lu, diviseur %u)%s\n", hz, (unsigned long)f,
	       (unsigned long)hc.base_hz, div, hs ? ", haute vitesse" : "");
	return 0;
}

static int set_width(struct sd_host *h, int bits)
{
	(void)h;
	uint32_t v = rd(R_HOST);
	if (bits == 4)
		v |= HC_4BIT;
	else
		v &= ~HC_4BIT;
	wr(R_HOST, v);
	return 0;
}

static int present(struct sd_host *h)
{
	(void)h;
	if (hc.p.assume_present)
		return 1;
	uint32_t s = rd(R_INT);
	if (s & (I_INSERT | I_REMOVE)) /* acquittés (QEMU attend que « retirée » le soit pour remettre la carte) */
		wr(R_INT, s & (I_INSERT | I_REMOVE));
	return (rd(R_PRESENT) & PS_INSERTED) != 0;
}

static int write_protected(struct sd_host *h)
{
	(void)h;
	if (hc.p.ignore_wp)
		return 0;
	return !(rd(R_PRESENT) & PS_WP);
}

static void recover(struct sd_host *h)
{
	(void)h;
	reset(RST_CMD | RST_DAT);
	wr(R_INT, 0xFFFFFFFF);
}

static void dump(struct sd_host *h, const char *why)
{
	(void)h;
	sd_log("registres (%s) : état %08x, hôte %08x, horloge %08x, int %08x/%08x/%08x, ACMD %08x, ADMA %08x\n", why,
	       rd(R_PRESENT), rd(R_HOST), rd(R_CLOCK), rd(R_INT), rd(R_INT_EN), rd(R_SIG_EN), rd(R_HOST2),
	       rd(R_ADMA_ERR));
	sd_log("registres : bloc %08x, arg %08x, cmd %08x, réponse %08x %08x %08x %08x\n", rd(R_BLOCK), rd(R_ARG),
	       rd(R_CMD), rd(R_RESP0), rd(R_RESP0 + 4), rd(R_RESP0 + 8), rd(R_RESP0 + 12));
}

static const char *describe(struct sd_host *h)
{
	(void)h;
	snprintf(hc.desc, sizeof hc.desc, "%s%s, %s", hc.dma_ok ? "ADMA2" : "PIO",
		 hc.dma_ok ? (hc.dma64 ? " 64 bits" : " 32 bits") : "", hc.irq >= 0 ? "interruptions MSI" : "sondage");
	return hc.desc;
}

static int power_up(struct sd_host *h)
{
	(void)h;
	reset(RST_ALL);
	if (rd(R_CLOCK) & RST_ALL) {
		sd_log("contrôleur : remise à zéro impossible\n");
		return E_IO;
	}
	wr(R_INT_EN, 0x03FF01FF & ~(1u << 8)); /* tout sauf l'interruption de la carte (SDIO) */
	wr(R_SIG_EN, 0);
	wr(R_INT, 0xFFFFFFFF);
	if (!wait_reg(R_PRESENT, PS_STABLE, PS_STABLE, 100))
		sd_log("détection de la carte pas stable\n");
	if (!present(h))
		return E_NOMEDIUM;
	uint32_t v = rd(R_HOST) & ~0xFF00u;
	if (!hc.p.no_power_cycle) { /* coupée puis rendue : la carte repart d'idle */
		wr(R_HOST, v);
		sd_sleep_ms(50);
	}
	wr(R_HOST, v | 0x0E00); /* 3,3 V */
	wr(R_HOST, v | 0x0F00); /* alimentée */
	sd_sleep_ms(20);
	if (!(rd(R_HOST) & 0x0100))
		sd_log("contrôleur : alimentation refusée (hôte %08x)\n", rd(R_HOST));
	wr(R_HOST, rd(R_HOST) & ~(HC_4BIT | HC_HS | HC_DMA_MASK | (1u << 5)));
	int r = set_clock(h, 400000, 0);
	if (r < 0)
		return r;
	if (hc.irq >= 0) /* fin de commande, de transfert, erreurs */
		wr(R_SIG_EN, I_CMD | I_XFER | I_READ | I_WRITE | I_INSERT | I_REMOVE | 0x03FF0000);
	sd_sleep_ms(2); /* (74 cycles au moins avant CMD0) */
	return 0;
}

/* table ADMA2 pour (adresse vue du contrôleur, octets) */
static void adma_build(uint64_t pa, uint64_t len)
{
	uint8_t *t = hc.table;
	unsigned step = hc.dma64 ? 12 : 8;
	while (len) {
		uint32_t n = len > ADMA_MAX ? ADMA_MAX : (uint32_t)len;
		len -= n;
		uint32_t attr = ADMA_VALID | ADMA_TRAN | (len ? 0 : ADMA_END);
		*(volatile uint32_t *)t = attr | n << 16;
		*(volatile uint32_t *)(t + 4) = (uint32_t)pa;
		if (hc.dma64)
			*(volatile uint32_t *)(t + 8) = (uint32_t)(pa >> 32);
		t += step;
		pa += n;
	}
	sd_wmb();
	wr(R_ADMA, (uint32_t)hc.table_pa);
	wr(R_ADMA_HI, (uint32_t)(hc.table_pa >> 32));
}

static int reachable(uint64_t pa, uint64_t len)
{
	if (!pa || (hc.p.bounce_only && pa != hc.bounce_pa && pa != hc.table_pa))
		return 0;
	if (hc.p.dma_limit)
		return pa + len <= hc.p.dma_limit;
	return hc.dma64 || pa + len <= 0x100000000ull;
}

static int do_cmd(struct sd_host *h, struct sd_cmd *c)
{
	(void)h;
	if (!present(h))
		return E_NOMEDIUM;
	uint32_t inhibit = PS_CMD_INHIBIT | (c->blocks || c->rsp == SD_RSP_R1B ? PS_DAT_INHIBIT : 0);
	if (c->idx == 12)
		inhibit = PS_CMD_INHIBIT;
	if (!wait_reg(R_PRESENT, inhibit, 0, CMD_MS)) {
		c->err_state = rd(R_PRESENT);
		sd_dbg("CMD%u : ligne occupée (état %08x)\n", c->idx, c->err_state);
		return E_BUSY;
	}
	uint32_t flags = 0;
	switch (c->rsp) {
	case SD_RSP_R1:
	case SD_RSP_R6:
	case SD_RSP_R7:
		flags = CF_48 | CF_CRC | CF_IDX;
		break;
	case SD_RSP_R1B:
		flags = CF_48B | CF_CRC | CF_IDX;
		break;
	case SD_RSP_R2:
		flags = CF_136 | CF_CRC;
		break;
	case SD_RSP_R3:
		flags = CF_48;
		break;
	}
	uint32_t mode = 0;
	uint64_t bytes = (uint64_t)c->blksz * c->blocks;
	int dma = 0, bounce = 0;
	if (c->blocks) {
		flags |= CF_DATA;
		mode = c->write ? 0 : TM_READ;
		if (c->blocks > 1)
			mode |= TM_MULTI | TM_BCNT | TM_ACMD12;
		if (hc.dma_ok && c->blksz == 512) {
			dma = 1;
			uint64_t pa = c->phys;
			if (!reachable(pa, bytes)) {
				bounce = 1;
				pa = hc.bounce_pa;
				if (c->write)
					memcpy(hc.bounce, c->buf, bytes);
			}
			adma_build(pa, bytes);
			mode |= TM_DMA;
			wr(R_HOST, (rd(R_HOST) & ~HC_DMA_MASK) | (hc.dma64 ? HC_ADMA64 : HC_ADMA32));
		}
		wr(R_BLOCK, (c->blksz & 0xFFF) | 7u << 12 | c->blocks << 16);
	}
	wr(R_INT, 0xFFFFFFFF);
	wr(R_ARG, c->arg);
	wr(R_CMD, mode | (flags | c->idx << 8) << 16);
	uint32_t s = wait_int(I_CMD, CMD_MS);
	if (!(s & I_CMD) || (s & I_ERRORS)) {
		c->err_state = s;
		recover(h);
		return !s || (s & E_CMD_TIMEOUT) ? E_TIMEDOUT : E_IO;
	}
	if (c->rsp == SD_RSP_R2)
		for (int i = 0; i < 4; i++)
			c->resp[i] = rd(R_RESP0 + 4 * i);
	else if (c->rsp != SD_RSP_NONE)
		c->resp[0] = rd(R_RESP0);
	if (c->blocks && !dma) { /* PIO : bloc par bloc, par le registre DATA */
		hc.n_pio++;
		uint32_t *p = c->buf;
		for (unsigned b = 0; b < c->blocks; b++) {
			s = wait_int(c->write ? I_WRITE : I_READ, DATA_MS);
			if (!(s & (c->write ? I_WRITE : I_READ)) || (s & I_ERRORS)) {
				c->err_state = s;
				recover(h);
				return s & E_DATA_TIMEOUT || !s ? E_TIMEDOUT : E_IO;
			}
			if (c->write)
				for (unsigned i = 0; i < c->blksz / 4; i++)
					wr(R_DATA, *p++);
			else
				for (unsigned i = 0; i < c->blksz / 4; i++)
					*p++ = rd(R_DATA);
		}
	}
	if (c->blocks || c->rsp == SD_RSP_R1B) {
		s = wait_int(I_XFER, c->blocks ? DATA_MS : 1000);
		if (!(s & I_XFER) || (s & I_ERRORS)) {
			c->err_state = s;
			if (dma && (s & E_ADMA))
				sd_log("erreur d'ADMA (%08x)\n", rd(R_ADMA_ERR));
			recover(h);
			return !s ? E_TIMEDOUT : E_IO;
		}
	}
	if (dma) {
		hc.n_dma++;
		sd_rmb();
		if (bounce && !c->write)
			memcpy(c->buf, hc.bounce, bytes);
	}
	return 0;
}

/* le DMA, s'il passe l'essai : le début de la carte lu en PIO puis en DMA */
static void dma_start(void)
{
	static uint8_t ref[TEST_BLOCKS * 512] __attribute__((aligned(64)));
	if (hc.force_pio) {
		sd_log("PIO seulement (-p)\n");
		return;
	}
	if (!(hc.caps & CAP_ADMA2)) {
		sd_log("pas d'ADMA2 : PIO\n");
		return;
	}
	hc.dma64 = hc.version >= 2 && (hc.caps & CAP_64BIT);
	if (!reachable(hc.bounce_pa, BOUNCE) || !reachable(hc.table_pa, TABLE)) {
		sd_log("tampon de DMA hors de portée (%lx) : PIO\n", (unsigned long)hc.bounce_pa);
		return;
	}
	if (sd_read(&card, 0, TEST_BLOCKS, ref, 0) < 0) {
		sd_log("essai du DMA : lecture en PIO ratée\n");
		return;
	}
	hc.dma_ok = 1;
	memset(hc.bounce, 0xA5, TEST_BLOCKS * 512);
	if (sd_read(&card, 0, TEST_BLOCKS, hc.bounce, hc.bounce_pa) < 0 || memcmp(ref, hc.bounce, sizeof ref)) {
		hc.dma_ok = 0;
		sd_log("essai du DMA raté : PIO\n");
		return;
	}
	sd_log("DMA (ADMA2, descripteurs de %d bits) en service\n", hc.dma64 ? 64 : 32);
}

/* ---- le pilote de blkserv */

static int b_read(void *ctx, uint64_t lba, unsigned n, void *buf, uint64_t phys)
{
	int r = sd_read(ctx, lba, n, buf, phys);
	if (r < 0 && hc.dma_ok && present(&host)) { /* DMA rendu : la suite en PIO */
		sd_log("erreur en DMA : retour au PIO\n");
		hc.dma_ok = 0;
		r = sd_read(ctx, lba, n, buf, phys);
	}
	return r;
}

static int b_write(void *ctx, uint64_t lba, unsigned n, const void *buf, uint64_t phys)
{
	int r = sd_write(ctx, lba, n, buf, phys);
	if (r < 0 && r != E_PERM && hc.dma_ok && present(&host)) {
		sd_log("erreur en DMA : retour au PIO\n");
		hc.dma_ok = 0;
		r = sd_write(ctx, lba, n, buf, phys);
	}
	return r;
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
	if (sd_card_check(c))
		return 1;
	if (!present(&host))
		return 0;
	sd_log("carte insérée : identification\n");
	int dma = hc.dma_ok;
	hc.dma_ok = 0;
	if (sd_card_init(c, &host) < 0)
		return 0;
	hc.dma_ok = dma;
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

/* le contrôleur de hc.p : version, capacités, horloge de base -> 0, < 0 */
static int controller_start(void)
{
	uint32_t ver = rd(R_VERSION);
	if (ver == 0xFFFFFFFF) {
		sd_log("%s : registres illisibles (ffffffff)\n", hc.p.where);
		return E_IO;
	}
	hc.version = (ver >> 16) & 0xFF;
	hc.caps = rd(R_CAPS);
	hc.caps1 = rd(R_CAPS1);
	hc.base_hz = hc.p.base_clock;
	if (!hc.base_hz) {
		unsigned mhz = hc.version >= 2 ? (hc.caps >> 8) & 0xFF : (hc.caps >> 8) & 0x3F;
		hc.base_hz = (uint64_t)mhz * 1000000;
	}
	if (!hc.base_hz) {
		sd_log("horloge de base inconnue : 100 MHz supposés\n");
		hc.base_hz = 100000000;
	}
	sd_log("%s : SDHCI version %u.00 (fabricant %02x), capacités %08x %08x, base %lu MHz%s%s%s%s\n", hc.p.where,
	       hc.version + 1, ver >> 24, hc.caps, hc.caps1, (unsigned long)(hc.base_hz / 1000000),
	       hc.caps & CAP_ADMA2 ? ", ADMA2" : "", hc.caps & CAP_64BIT ? ", 64 bits" : "",
	       hc.caps & CAP_HS ? ", haute vitesse" : "", hc.caps & (1u << 24) ? ", 3,3 V" : ", pas de 3,3 V ?");
	host.caps = SDH_4BIT | ((hc.caps & CAP_HS) ? SDH_HS : 0);
	return 0;
}

int main(int argc, char **argv)
{
	const char *addr = 0, *name = 0;
	int ro = 0, diag = 0;
	sd_tag = "sdhci";
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-v"))
			sd_verbose = 1;
		else if (!strcmp(argv[i], "-p"))
			hc.force_pio = 1;
		else if (!strcmp(argv[i], "-r"))
			ro = 1;
		else if (!strcmp(argv[i], "-d"))
			diag = 1;
		else if (!strcmp(argv[i], "-n") && i + 1 < argc)
			name = argv[++i];
		else
			addr = argv[i];
	}
	hc.irq = -1;
	long ph = 0;
	long va = korin_sys2(SYS_DMA_ALLOC, BOUNCE + TABLE, &ph);
	if (va < 0) {
		sd_log("pas de mémoire DMA (%ld) : PIO\n", va);
		hc.force_pio = 1;
	} else {
		hc.table = (uint8_t *)va;
		hc.table_pa = (uint64_t)ph;
		hc.bounce = hc.table + TABLE;
		hc.bounce_pa = hc.table_pa + TABLE;
	}
	host.name = "sdhci";
	host.priv = &hc;
	host.max_blocks = MAX_BLOCKS;
	host.power_up = power_up;
	host.present = present;
	host.write_protected = write_protected;
	host.set_clock = set_clock;
	host.set_width = set_width;
	host.cmd = do_cmd;
	host.recover = recover;
	host.dump = dump;
	host.describe = describe;
	int r = E_NODEV, found = 0;
	for (int idx = 0; r < 0; idx++) { /* (le Pi : EMMC2, puis Arasan, puis le Pi 5) */
		int pr = sdhci_plat_init(addr, &hc.p, idx);
		if (pr == E_NOENT)
			break;
		if (pr < 0)
			continue;
		if (controller_start() < 0)
			continue;
		found = 1;
		if (diag)
			dump(&host, "départ");
		r = sd_card_init(&card, &host);
		if (r == E_NOMEDIUM && !hc.p.assume_present)
			break; /* (contrôleur trouvé, carte absente : servi quand même) */
	}
	if (!found)
		return 1;
	hc.irq = sdhci_plat_irq(&hc.p);
	if (hc.irq >= 0)
		wr(R_SIG_EN, I_CMD | I_XFER | I_READ | I_WRITE | I_INSERT | I_REMOVE | 0x03FF0000);
	if (r == 0)
		dma_start();
	if (diag) {
		dump(&host, "fin");
		if (r == 0)
			sd_card_describe(&card);
		return r < 0;
	}
	if (r == 0 && hc.dma_ok)
		sd_card_describe(&card);
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
