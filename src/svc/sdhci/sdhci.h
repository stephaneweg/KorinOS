/* sdhci.h : l'hôte SDHCI en C (sdhci.c, commun) et sa machine
 * (plat_x86_64.c : fonction PCIe de classe 08/05 par pcid, MSI ;
 * plat_aarch64.c : EMMC2 / Arasan du Pi 4, horloge du micrologiciel, jamais
 * essayé sur le Pi : la carte garde src/svc/sd/sd.asm). */
#ifndef KORIN_SDHCI_H
#define KORIN_SDHCI_H

#include "../sdmmc/sdmmc.h"

struct sdhci_plat {
	volatile uint8_t *regs; /* registres de l'emplacement (256 o), mappés */
	uint64_t base_clock;    /* Hz ; 0 : celle des capacités */
	long irq_line;          /* ligne liée par le pilote (IRQ_WAIT), -1 : sondage */
	long irq_flags;         /* IRQ_BIND (IRQ_SHARED sur le Pi) */
	uint64_t dma_limit;     /* fin de la mémoire que le DMA atteint (0 : 4 Go ou 64 bits) */
	int ignore_wp;          /* le Pi n'a pas de curseur de protection */
	int no_power_cycle;     /* le Pi : l'alimentation de la carte n'est pas au contrôleur */
	int assume_present;     /* le Pi : pas de détection de la carte (EMMC2) */
	int bounce_only;        /* le Pi : tampons des clients cachés : DMA par le tampon (non caché) */
	char where[64];         /* pour le journal */
};

/* le contrôleur n° idx (0, 1… : le Pi en essaie plusieurs, comme sd.asm) -> 0,
 * E_NOENT : plus aucun à essayer, < 0 : celui-ci manque */
int sdhci_plat_init(const char *arg, struct sdhci_plat *p, int idx);
/* IRQ_MSI, IRQ_BIND (fait après l'essai du contrôleur) -> ligne, ou -1 */
long sdhci_plat_irq(struct sdhci_plat *p);

#endif
