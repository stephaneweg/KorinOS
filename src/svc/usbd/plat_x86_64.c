/* plat_x86_64.c : le xHCI sur PC. La fonction PCIe vient de pcid : l'adresse
 * donnée par hwmanager (« SSSS:BB:DD.F », drivers.txt : pci:0c/03/30), sinon
 * le premier xHCI de la liste (essai direct au moniteur). BAR0 mappée, DMA
 * cohérent (adresse vue du contrôleur = adresse physique), MSI-X (sinon MSI)
 * par pcilib. Plusieurs xHCI (le ZBook en a deux) : une fonction par
 * contrôleur ; la même deux fois -> E_EXIST. */
#include "usbd.h"
#include "pci.h"

static struct pci_info infos[XH_HC_MAX];
static int ninfos;

static long irq_setup(struct xh_plat *p)
{
	long line;
	long n = pci_irq_setup((struct pci_info *)p->priv, 1, &line);
	if (n < 1) {
		say("usbd : xhci : pas de MSI (%ld), sondage\n", n);
		return -1;
	}
	return line;
}

int plat_xhci_init(const char *arg, struct xh_plat *p)
{
	long r = pci_open(5000);
	if (r < 0) {
		say("usbd : xhci : service pci absent (%ld)\n", r);
		return E_NODEV;
	}
	long addr = arg && *arg ? pci_parse_addr(arg) : -1;
	if (addr < 0) {
		long from = 0;
		struct pci_info f;
		if (pci_find_class(0x0C0330, 0xFFFFFF, &from, &f) < 0) {
			say("usbd : xhci : aucun contrôleur xHCI\n");
			return E_NODEV;
		}
		addr = f.addr;
	}
	for (int i = 0; i < ninfos; i++)
		if (infos[i].addr == (uint32_t)addr)
			return E_EXIST; /* déjà servi ici */
	if (ninfos >= XH_HC_MAX)
		return E_NOMEM;
	struct pci_info *f = &infos[ninfos];
	r = pci_claim((uint32_t)addr, f);
	if (r < 0) {
		say("usbd : xhci : fonction refusée par pcid (%ld)\n", r);
		return (int)r;
	}
	char a[16];
	pci_format_addr(f->addr, a);
	if ((uint32_t)(f->cls << 16 | f->sub << 8 | f->prog) != 0x0C0330) {
		say("usbd : xhci : %s n'est pas un xHCI\n", a);
		pci_release(f->addr);
		return E_NODEV;
	}
	long regs = pci_map_bar(f, 0);
	if (regs < 0) {
		say("usbd : xhci : BAR0 de %s non mappée (%ld)\n", a, regs);
		pci_release(f->addr);
		return (int)regs;
	}
	say("usbd : xhci : %s (%04x:%04x), BAR0 %lx\n", a, f->vendor, f->device, (unsigned long)f->bar[0]);
	ninfos++;
	p->regs = (volatile uint8_t *)regs;
	p->regs_size = f->bar_size[0] ? f->bar_size[0] : 0x10000;
	p->dma_offset = 0;
	p->dma_limit = 0;
	p->name = "pc";
	p->priv = f;
	p->irq_setup = irq_setup;
	return 0;
}

void plat_udelay(unsigned us)
{
	long end = korin_sys0(SYS_TIME_MS) + (us + 999) / 1000;
	while (korin_sys0(SYS_TIME_MS) < end)
		korin_sys0(SYS_YIELD);
}
