/* plat_aarch64.c : l'hôte SDHCI sur le Pi, repris de src/svc/sd/sd.asm.
 * JAMAIS ESSAYÉ SUR LE PI : la carte garde sd.asm (docs/etude/PC-PILOTES.md,
 * « Carte SD ») ; ce fichier ne sert qu'à garder le C construisible pour les
 * deux machines, en attendant une comparaison sous QEMU raspi4b puis sur le Pi.
 *
 * Contrôleurs essayés dans l'ordre (DEV_MAP refuse ceux de l'autre carte) :
 * EMMC2 (vrai Pi 4), Arasan (QEMU raspi4b), SDHCI du BCM2712 (Pi 5). Horloge
 * de base par le micrologiciel (GET_CLOCK_RATE : EMMC2 id 12, les autres
 * id 1 ; Pi 5 : 200 MHz s'il se tait). Ligne 158 du GIC, partagée avec
 * l'Arasan du Wi-Fi (Pi 5 : SPI 273). Comme l'asm : l'alimentation n'est pas
 * coupée (le micrologiciel a alimenté la carte en 3,3 V), pas de détection ni
 * de curseur de protection, DMA seulement par le tampon non caché (les
 * tampons des clients sont cachés), jusqu'à 0x9C000000. */
#include "sdhci.h"

#define BASE_EMMC2 0xFE340000ull
#define BASE_ARASAN 0xFE300000ull
#define BASE_PI5 0x1000FFF000ull
#define IRQ_SD 158
#define IRQ_PI5 (32 + 273)
#define IRQ_SHARED (1ull << 63)

static const uint64_t bases[] = {BASE_EMMC2, BASE_ARASAN, BASE_PI5};
static const char *names[] = {"EMMC2", "Arasan", "BCM2712"};
static uint32_t fw[8] __attribute__((aligned(16)));
static uint64_t cur;

static uint64_t fw_clock(uint32_t id)
{
	fw[0] = 32;
	fw[1] = 0;
	fw[2] = 0x00030002; /* GET_CLOCK_RATE */
	fw[3] = 8;
	fw[4] = 0;
	fw[5] = id;
	fw[6] = 0;
	fw[7] = 0;
	if (korin_sys1(SYS_FW_CALL, fw) < 0)
		return 0;
	return fw[6];
}

int sdhci_plat_init(const char *arg, struct sdhci_plat *p, int idx)
{
	(void)arg;
	if (idx >= 3)
		return E_NOENT;
	long regs = korin_sys2(SYS_DEV_MAP, bases[idx], 0x100);
	if (regs < 0)
		return (int)regs;
	cur = bases[idx];
	memset(p, 0, sizeof *p);
	p->regs = (volatile uint8_t *)regs;
	p->base_clock = fw_clock(idx == 0 ? 12 : 1);
	if (!p->base_clock && idx == 2)
		p->base_clock = 200000000;
	p->irq_line = -1;
	p->irq_flags = (long)IRQ_SHARED;
	p->dma_limit = 0x9C000000;
	p->ignore_wp = 1;
	p->no_power_cycle = 1;
	p->assume_present = 1;
	p->bounce_only = 1;
	snprintf(p->where, sizeof p->where, "%s (%lx)", names[idx], (unsigned long)bases[idx]);
	return 0;
}

long sdhci_plat_irq(struct sdhci_plat *p)
{
	long line = cur == BASE_PI5 ? IRQ_PI5 : IRQ_SD;
	long r = korin_sys3(SYS_IRQ_BIND, line, -1, p->irq_flags);
	if (r < 0) {
		sd_log("IRQ_BIND refusé (%ld) : sondage\n", r);
		return -1;
	}
	p->irq_line = line;
	return line;
}
