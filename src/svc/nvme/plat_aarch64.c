/* plat_aarch64.c : le Pi. Le Pi 4 n'a pas de NVMe (son seul PCIe porte le VL805) ;
 * un CM4 ou un Pi 5 pourraient en avoir un derrière leur pont PCIe, mais pcid ne
 * tourne pas sur le Pi (docs/etude/PC-PILOTES.md). Ce fichier garde le pilote
 * construisible pour les deux machines : nvme.c et policy.c n'ont rien de x86. */
#include "nvme.h"

int nvme_plat_init(const char *arg, struct nvme_plat *p)
{
	(void)arg;
	(void)p;
	nv_log("pas de NVMe sur cette machine (pas de pcid)\n");
	return E_NODEV;
}

long nvme_plat_irq(struct nvme_plat *p)
{
	(void)p;
	return -1;
}
