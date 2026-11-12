/* plat_x86_64.c : le contrôleur NVMe sur PC. La fonction PCIe vient de pcid :
 * l'adresse donnée par hwmanager (« SSSS:BB:DD.F », drivers.txt : pci:01/08/02),
 * sinon le premier contrôleur de classe 01/08/02. BAR0 (registres et sonnettes),
 * MSI-X ou MSI par pcilib (vecteur 0 pour toutes les files) ; DMA cohérent,
 * adresse vue du contrôleur = adresse physique. */
#include "nvme.h"
#include "pci.h"

static struct pci_info info;

int nvme_plat_init(const char *arg, struct nvme_plat *p)
{
	long r = pci_open(5000);
	if (r < 0) {
		nv_log("service pci absent (%ld)\n", r);
		return E_NODEV;
	}
	long addr = arg && *arg ? pci_parse_addr(arg) : -1;
	if (addr < 0) {
		long from = 0;
		struct pci_info f;
		if (pci_find_class(0x010802, 0xFFFFFF, &from, &f) < 0) {
			nv_log("aucun contrôleur NVMe (classe 01/08/02)\n");
			return E_NODEV;
		}
		addr = f.addr;
	}
	r = pci_claim((uint32_t)addr, &info);
	if (r == E_BUSY)
		return E_BUSY;
	if (r < 0) {
		nv_log("fonction refusée par pcid (%ld)\n", r);
		return (int)r;
	}
	char a[16];
	pci_format_addr(info.addr, a);
	if (info.cls != 0x01 || info.sub != 0x08 || info.prog != 0x02)
		nv_log("%s : classe %02x/%02x/%02x : pas un NVMe, essai quand même\n", a, info.cls, info.sub, info.prog);
	if (!info.bar[0] || (info.bar_flags[0] & PCI_BAR_IO)) {
		nv_log("%s : pas de BAR0 en mémoire\n", a);
		return E_NODEV;
	}
	long regs = pci_map_bar(&info, 0);
	if (regs < 0) {
		nv_log("%s : BAR0 non mappée (%ld)\n", a, regs);
		return (int)regs;
	}
	p->regs = (volatile u8 *)regs;
	p->regs_size = info.bar_size[0] ? info.bar_size[0] : 0x10000;
	p->irq_line = -1;
	p->bounce_only = 0;
	snprintf(p->where, sizeof p->where, "%s (%04x:%04x)", a, info.vendor, info.device);
	nv_log("%s : BAR0 %lx (%lu Ko)%s\n", p->where, (unsigned long)info.bar[0], (unsigned long)(p->regs_size >> 10),
	       info.cap_msix ? ", MSI-X" : info.cap_msi ? ", MSI" : ", sans MSI");
	return 0;
}

long nvme_plat_irq(struct nvme_plat *p)
{
	long line;
	long n = pci_irq_setup(&info, 1, &line);
	if (n < 1) {
		nv_log("pas de MSI (%ld) : sondage\n", n);
		return -1;
	}
	long r = korin_sys3(SYS_IRQ_BIND, line, -1, 0);
	if (r < 0) {
		nv_log("IRQ_BIND refusé (%ld) : sondage\n", r);
		return -1;
	}
	p->irq_line = line;
	nv_log("interruption MSI%s sur la ligne %ld\n", info.cap_msix ? "-X" : "", line);
	return line;
}
