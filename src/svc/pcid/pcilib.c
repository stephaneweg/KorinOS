/* pcilib.c : ce qu'un pilote PCIe fait avec pcid (pci.h) ; compilé avec le
 * pilote (src/svc/<pilote>/svc.conf : « extra = ../pcid/pcilib.c »). */
#include "korinrt.h"
#include "pci.h"

static long pci_chan = -1;
static u8 pmsg[512] __attribute__((aligned(16)));

static int hexval(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

/* un nombre hexadécimal jusqu'à stop -> valeur, -1 si vide ou illisible */
static long hexpart(const char **ps, char stop)
{
	const char *s = *ps;
	long v = 0;
	int n = 0;
	for (; *s && *s != stop; s++, n++) {
		int h = hexval(*s);
		if (h < 0 || n > 4)
			return -1;
		v = v * 16 + h;
	}
	if (!n || *s != stop)
		return -1;
	*ps = *s ? s + 1 : s;
	return v;
}

long pci_parse_addr(const char *s)
{
	long seg = 0, bus, dev, fn;
	int colons = 0;
	for (const char *p = s; *p && *p != ' '; p++)
		colons += *p == ':';
	if (colons == 2 && (seg = hexpart(&s, ':')) < 0)
		return -1;
	if ((bus = hexpart(&s, ':')) < 0 || bus > 255 || (dev = hexpart(&s, '.')) < 0 || dev > 31)
		return -1;
	fn = hexval(*s);
	if (fn < 0 || fn > 7 || (s[1] && s[1] != ' '))
		return -1;
	return (long)PCI_ADDR(seg, bus, dev, fn);
}

void pci_format_addr(uint32_t addr, char *buf)
{
	snprintf(buf, 16, "%04x:%02x:%02x.%x", addr >> 16, (addr >> 8) & 0xFF, (addr >> 3) & 31, addr & 7);
}

long pci_open(long wait_ms)
{
	if (pci_chan >= 0)
		return 0;
	long c = korin_sys2(SYS_SVC_OPEN, "pci", wait_ms);
	if (c < 0)
		return c;
	pci_chan = c;
	return 0;
}

static long call(uint64_t op, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
	if (pci_chan < 0 && pci_open(5000) < 0)
		return E_NOENT;
	uint64_t *m = (uint64_t *)pmsg;
	memset(pmsg, 0, 48);
	m[0] = op;
	m[1] = a;
	m[2] = b;
	m[3] = c;
	m[4] = d;
	return korin_sys2(SYS_SVC_CALL, pci_chan, pmsg);
}

long pci_claim(uint32_t addr, struct pci_info *info)
{
	long r = call(PCI_OP_CLAIM, addr, 0, 0, 0);
	if (r == 0 && info)
		memcpy(info, pmsg + 48, sizeof *info);
	return r;
}

long pci_find_class(uint32_t cls, uint32_t mask, long *from, struct pci_info *info)
{
	long r = call(PCI_OP_FIND, 0xFFFFFFFF, cls, mask, (uint64_t)*from);
	if (r < 0)
		return r;
	*from = r + 1;
	long i = call(PCI_OP_INFO, (uint64_t)r, 0, 0, 0);
	if (i == 0 && info)
		memcpy(info, pmsg + 48, sizeof *info);
	return i;
}

long pci_cfg_read(uint32_t addr, unsigned off, unsigned size)
{
	return call(PCI_OP_CFG_READ, addr, off, size, 0);
}

long pci_cfg_write(uint32_t addr, unsigned off, unsigned size, uint32_t v)
{
	return call(PCI_OP_CFG_WRITE, addr, off, size, v);
}

long pci_map_bar(const struct pci_info *info, int i)
{
	if (i < 0 || i > 5 || !info->bar[i] || (info->bar_flags[i] & PCI_BAR_IO))
		return E_INVAL;
	uint64_t size = info->bar_size[i] ? info->bar_size[i] : 0x10000;
	return korin_sys2(SYS_DEV_MAP, info->bar[i], size);
}

long pci_irq_setup(const struct pci_info *info, unsigned n, long *lines)
{
	uint64_t ad[64];
	uint32_t a = info->addr;
	if (n == 0)
		n = 1;
	if (info->cap_msix) {
		if (n > info->msix_count)
			n = info->msix_count;
		if (n > 32)
			n = 32;
		int b = info->msix_table_bar;
		if (!info->bar[b])
			return E_NOSYS;
		long tab = korin_sys2(SYS_DEV_MAP, info->bar[b] + info->msix_table_off, info->msix_count * 16);
		if (tab < 0)
			return tab;
		long first = korin_sys2(SYS_IRQ_MSI, n, ad);
		if (first < 0)
			return first;
		unsigned p = info->cap_msix;
		uint32_t ctl = (uint32_t)pci_cfg_read(a, p + 2, 2);
		pci_cfg_write(a, p + 2, 2, ctl | 0xC000); /* allumé, tout masqué le temps d'écrire */
		volatile uint32_t *t = (volatile uint32_t *)tab;
		for (unsigned i = 0; i < n; i++) {
			t[4 * i + 0] = (uint32_t)ad[2 * i];
			t[4 * i + 1] = (uint32_t)(ad[2 * i] >> 32);
			t[4 * i + 2] = (uint32_t)ad[2 * i + 1];
			t[4 * i + 3] = 0; /* démasquée */
			lines[i] = first + i;
		}
		pci_cfg_write(a, p + 2, 2, (ctl | 0x8000) & ~0x4000u);
		return n;
	}
	if (info->cap_msi) {
		long first = korin_sys2(SYS_IRQ_MSI, 1, ad);
		if (first < 0)
			return first;
		unsigned p = info->cap_msi;
		pci_cfg_write(a, p + 4, 4, (uint32_t)ad[0]);
		if (info->msi_64) {
			pci_cfg_write(a, p + 8, 4, (uint32_t)(ad[0] >> 32));
			pci_cfg_write(a, p + 12, 2, (uint32_t)ad[1]);
		} else
			pci_cfg_write(a, p + 8, 2, (uint32_t)ad[1]);
		uint32_t ctl = (uint32_t)pci_cfg_read(a, p + 2, 2);
		pci_cfg_write(a, p + 2, 2, (ctl & ~0x70u) | 1); /* un vecteur, allumé */
		lines[0] = first;
		return 1;
	}
	return E_NOSYS;
}
