/* plat_x86_64.c : l'hôte SDHCI sur PC. La fonction PCIe vient de pcid :
 * l'adresse donnée par hwmanager (« SSSS:BB:DD.F », drivers.txt : pci:08/05),
 * sinon le premier contrôleur de classe 08/05 (interface 00 ou 01 : SDHCI
 * standard ; 02 : propre au fabricant, refusé). Emplacement 0 seulement (BAR
 * donnée par le registre 0x40 de la configuration). DMA cohérent, adresse vue
 * du contrôleur = adresse physique ; MSI-X ou MSI par pcilib (sdhci-pci de
 * QEMU n'en a pas : sondage). */
#include "sdhci.h"
#include "pci.h"

static struct pci_info info;

int sdhci_plat_init(const char *arg, struct sdhci_plat *p, int idx)
{
	if (idx > 0)
		return E_NOENT;
	long r = pci_open(5000);
	if (r < 0) {
		sd_log("service pci absent (%ld)\n", r);
		return E_NODEV;
	}
	long addr = arg && *arg ? pci_parse_addr(arg) : -1;
	if (addr < 0) {
		long from = 0;
		struct pci_info f;
		for (;;) {
			if (pci_find_class(0x080500, 0xFFFF00, &from, &f) < 0) {
				sd_log("aucun contrôleur SDHCI (classe 08/05)\n");
				return E_NODEV;
			}
			if (f.prog <= 1)
				break;
		}
		addr = f.addr;
	}
	r = pci_claim((uint32_t)addr, &info);
	if (r < 0) {
		sd_log("fonction refusée par pcid (%ld)\n", r);
		return (int)r;
	}
	char a[16];
	pci_format_addr(info.addr, a);
	if (info.cls != 0x08 || info.sub != 0x05 || info.prog > 1)
		sd_log("%s : classe %02x/%02x/%02x : pas un SDHCI standard, essai quand même\n", a, info.cls, info.sub,
		       info.prog);
	long slots = pci_cfg_read(info.addr, 0x40, 1);
	int bar = slots >= 0 ? (int)(slots & 7) : 0;
	if (bar > 5 || !info.bar[bar])
		bar = 0;
	long regs = pci_map_bar(&info, bar);
	if (regs < 0) {
		sd_log("%s : BAR%d non mappée (%ld)\n", a, bar, regs);
		return (int)regs;
	}
	p->regs = (volatile uint8_t *)regs;
	p->base_clock = 0;
	p->irq_line = -1;
	p->irq_flags = 0;
	p->dma_limit = 0;
	p->ignore_wp = 0;
	p->no_power_cycle = 0;
	p->assume_present = 0;
	p->bounce_only = 0;
	snprintf(p->where, sizeof p->where, "%s (%04x:%04x)", a, info.vendor, info.device);
	sd_log("%s : BAR%d %lx, %ld emplacement(s)%s\n", p->where, bar, (unsigned long)info.bar[bar],
	       slots >= 0 ? ((slots >> 4) & 7) + 1 : 1, info.cap_msix ? ", MSI-X" : info.cap_msi ? ", MSI" : ", sans MSI");
	return 0;
}

long sdhci_plat_irq(struct sdhci_plat *p)
{
	long line;
	long n = pci_irq_setup(&info, 1, &line);
	if (n < 1) {
		sd_log("pas de MSI (%ld) : sondage\n", n);
		return -1;
	}
	long r = korin_sys3(SYS_IRQ_BIND, line, -1, 0);
	if (r < 0) {
		sd_log("IRQ_BIND refusé (%ld) : sondage\n", r);
		return -1;
	}
	p->irq_line = line;
	sd_log("interruption MSI%s sur la ligne %ld\n", info.cap_msix ? "-X" : "", line);
	return line;
}
