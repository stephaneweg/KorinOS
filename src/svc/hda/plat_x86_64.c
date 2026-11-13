/* plat_x86_64.c : le contrôleur HDA sur PC. La fonction PCIe vient de pcid : l'adresse donnée
 * par hwmanager (« SSSS:BB:DD.F », drivers.txt : pci:04/03, pci:8086:43c8), sinon le premier
 * contrôleur de classe 04/03. BAR0 (registres), MSI par pcilib ; config PCI d'Intel (TCSEL,
 * DEVC, CGCTL) et de NVIDIA (cohérence des flux), docs/etude/SON-PC.md 2 et 5. */
#include "hda.h"
#include "pci.h"

static struct pci_info info;

static void cfg_update(unsigned off, unsigned size, u32 mask, u32 val)
{
	long v = pci_cfg_read(info.addr, off, size);
	if (v < 0)
		return;
	u32 nv = ((u32)v & ~mask) | (val & mask);
	if (nv != (u32)v)
		pci_cfg_write(info.addr, off, size, nv);
}

int hda_plat_init(const char *arg, struct hda_plat *p)
{
	long r = pci_open(5000);
	if (r < 0) {
		hd_log("service pci absent (%ld)\n", r);
		return E_NODEV;
	}
	long addr = arg && *arg ? pci_parse_addr(arg) : -1;
	if (addr < 0) {
		long from = 0;
		struct pci_info f;
		if (pci_find_class(0x040300, 0xFFFF00, &from, &f) < 0) {
			hd_log("aucun contrôleur HDA (classe 04/03)\n");
			return E_NODEV;
		}
		addr = f.addr;
	}
	r = pci_claim((uint32_t)addr, &info);
	if (r == E_BUSY)
		return E_BUSY;
	if (r < 0) {
		hd_log("fonction refusée par pcid (%ld)\n", r);
		return (int)r;
	}
	char a[16];
	pci_format_addr(info.addr, a);
	snprintf(p->where, sizeof p->where, "%s", a[0] == '0' && a[1] == '0' && a[2] == '0' && a[3] == '0' ? a + 5 : a);
	p->vendor = info.vendor;
	p->device = info.device;
	if (!info.bar[0] || (info.bar_flags[0] & PCI_BAR_IO)) {
		hd_log("pas de BAR0 en mémoire\n");
		return E_NODEV;
	}
	if (info.cap_pm) { /* D3 -> D0 (pcid ne le fait pas encore) */
		long pm = pci_cfg_read(info.addr, info.cap_pm + 4u, 2);
		if (pm >= 0 && (pm & 3)) {
			hd_log("fonction en D%ld : passage en D0\n", pm & 3);
			pci_cfg_write(info.addr, info.cap_pm + 4u, 2, (u32)pm & ~3u);
			hd_sleep(10);
		}
	}
	long regs = pci_map_bar(&info, 0);
	if (regs < 0) {
		hd_log("BAR0 non mappée (%ld)\n", regs);
		return (int)regs;
	}
	p->regs = (volatile u8 *)regs;
	p->regs_size = info.bar_size[0] ? info.bar_size[0] : 0x4000;
	p->irq_line = -1;
	if (info.vendor == 0x8086) {
		cfg_update(0x44, 1, 0x07, 0);       /* TCSEL : classe de trafic 0 */
		cfg_update(0x78, 2, 1u << 11, 0);   /* DEVC : « no snoop » coupé (DMA cohérent) */
	} else if (info.vendor == 0x10de) {
		cfg_update(0x4e, 1, 0x0f, 0x0f);    /* NVIDIA : TRANSREG */
		cfg_update(0x4d, 1, 0x01, 0x01);    /* cohérence des flux d'entrée */
		cfg_update(0x4c, 1, 0x01, 0x01);    /* et de sortie */
	}
	hd_log("%04x:%04x classe %02x/%02x/%02x, BAR0 %lx (%lu Ko)%s\n", info.vendor, info.device, info.cls, info.sub,
	       info.prog, (unsigned long)info.bar[0], (unsigned long)(p->regs_size >> 10),
	       info.cap_msix ? ", MSI-X" : info.cap_msi ? ", MSI" : ", sans MSI");
	return 0;
}

void hda_plat_before_reset(struct hda_plat *p)
{
	if (p->vendor == 0x8086)
		cfg_update(0x48, 4, 1u << 6, 0); /* CGCTL.MISCBDCGE coupé pendant la remise à zéro */
}

void hda_plat_after_reset(struct hda_plat *p)
{
	if (p->vendor == 0x8086)
		cfg_update(0x48, 4, 1u << 6, 1u << 6);
}

long hda_plat_irq(struct hda_plat *p)
{
	long line;
	long n = pci_irq_setup(&info, 1, &line);
	if (n < 1) {
		hd_log("pas de MSI (%ld) : sondage\n", n);
		return -1;
	}
	long r = korin_sys3(SYS_IRQ_BIND, line, -1, 0);
	if (r < 0) {
		hd_log("IRQ_BIND refusé (%ld) : sondage\n", r);
		return -1;
	}
	p->irq_line = line;
	return line;
}
