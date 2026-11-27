/* pcid - le bus PCIe en service (docs/etude/PC-PILOTES.md), pour le PC.
 *
 * Il joue pour le PCIe le rôle que usbd joue pour l'USB :
 *  * énumération par l'ECAM : la table ACPI MCFG (SYS_ACPI_TABLE) donne,
 *    par segment, l'adresse de l'espace de configuration ; chaque bus est
 *    mappé (SYS_DEV_MAP, 1 Mo) quand on le parcourt, en descendant par les
 *    ponts depuis le premier bus du segment (les numéros de bus sont ceux du
 *    micrologiciel : rien n'est renuméroté) ;
 *  * pour chaque fonction : identité, classe, BAR (taille mesurée, décodage
 *    coupé le temps de la mesure ; pas pour l'affichage, classe 03, dont le
 *    framebuffer GOP sert à la console), capacités MSI, MSI-X, PCIe, énergie ;
 *  * chaque fonction est annoncée à hwmanager (HW_OP_PCI), qui lance le
 *    pilote de drivers.txt (pci:VVVV:DDDD, pci:cc/ss/pp…) avec son adresse ;
 *  * le service « pci » : liste, recherche, prise d'une fonction par son
 *    pilote (rendue à la fin du pilote), accès à la configuration.
 *
 *	pcid        service (ce que lance init2 sur PC)
 *	pcid -l     liste, sans service ni annonce
 *	pcid -n     service sans annonce (essai sans hwmanager ; au démarrage du PC,
 *	            init.txt : init2 demande les annonces, PCI_OP_ANNOUNCE, quand
 *	            hwmanager tourne)
 *
 * Le Pi 4 n'a qu'un périphérique PCIe (le VL805, derrière le pont du
 * BCM2711, sans ECAM ni ACPI) et le Pi 5 un seul aussi (le RP1) : pcid ne
 * leur servirait qu'avec une autre découverte (voir PC-PILOTES.md). */
#include "korinrt.h"
#include "pci.h"
#include "caller.h"

#define FUNC_MAX 256
#define SEG_MAX 4

#define SVC_OP_SIGNAL 0xFFFD
#define SVC_OP_END 0xFFFF
#define SVF_ENDS 1
#define E_BUSY -16

struct seg {
	uint64_t base; /* ECAM du bus 0 du segment */
	uint16_t seg;
	uint8_t start, end;
	volatile uint8_t *bus[256]; /* bus mappés (1 Mo chacun), 0 : pas encore */
};

static struct seg segs[SEG_MAX];
static int nsegs;
static struct pci_info funcs[FUNC_MAX];
static int nfuncs;
static long chan = -1;
static long hw_chan = -1;
static u8 msg[512] __attribute__((aligned(16)));
static u8 hw_msg[512] __attribute__((aligned(16)));

static void say(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof buf - 1)
		n = sizeof buf - 1;
	korin_sys2(SYS_PRINT, buf, n);
}

/* ---- espace de configuration */

static struct seg *seg_of(uint32_t addr)
{
	unsigned s = addr >> 16, bus = (addr >> 8) & 0xFF;
	for (int i = 0; i < nsegs; i++)
		if (segs[i].seg == s && bus >= segs[i].start && bus <= segs[i].end)
			return &segs[i];
	return 0;
}

static volatile uint8_t *cfg_page(uint32_t addr)
{
	struct seg *g = seg_of(addr);
	if (!g)
		return 0;
	unsigned bus = (addr >> 8) & 0xFF;
	if (!g->bus[bus]) {
		long v = korin_sys2(SYS_DEV_MAP, g->base + ((uint64_t)bus << 20), 1 << 20);
		if (v < 0)
			return 0;
		g->bus[bus] = (volatile uint8_t *)v;
	}
	return g->bus[bus] + ((addr & 0xFF) << 12);
}

static uint64_t cfg_phys(uint32_t addr)
{
	struct seg *g = seg_of(addr);
	return g ? g->base + ((uint64_t)((addr >> 8) & 0xFF) << 20) + ((addr & 0xFF) << 12) : 0;
}

static uint32_t rd32(uint32_t addr, unsigned off)
{
	volatile uint8_t *p = cfg_page(addr);
	return p ? *(volatile uint32_t *)(p + (off & 0xFFC)) : 0xFFFFFFFF;
}

static uint32_t rd(uint32_t addr, unsigned off, unsigned size)
{
	uint32_t v = rd32(addr, off) >> ((off & 3) * 8);
	return size == 4 ? v : v & ((1u << (size * 8)) - 1);
}

static void wr(uint32_t addr, unsigned off, unsigned size, uint32_t v)
{
	volatile uint8_t *p = cfg_page(addr);
	if (!p)
		return;
	if (size == 4)
		*(volatile uint32_t *)(p + (off & 0xFFC)) = v;
	else if (size == 2)
		*(volatile uint16_t *)(p + (off & 0xFFE)) = (uint16_t)v;
	else
		p[off & 0xFFF] = (uint8_t)v;
}

/* ---- énumération */

static void read_caps(struct pci_info *f)
{
	if (!(rd(f->addr, 6, 2) & 0x10)) /* pas de liste de capacités */
		return;
	unsigned p = rd(f->addr, 0x34, 1) & 0xFC;
	for (int n = 0; p >= 0x40 && n < 48; n++) {
		unsigned id = rd(f->addr, p, 1);
		unsigned ctl = rd(f->addr, p + 2, 2);
		switch (id) {
		case 0x01:
			f->cap_pm = (uint8_t)p;
			break;
		case 0x05:
			f->cap_msi = (uint8_t)p;
			f->msi_max = (uint8_t)(1u << ((ctl >> 1) & 7));
			f->msi_64 = (ctl >> 7) & 1;
			break;
		case 0x10:
			f->cap_pcie = (uint8_t)p;
			break;
		case 0x11: {
			uint32_t t = rd(f->addr, p + 4, 4), b = rd(f->addr, p + 8, 4);
			f->cap_msix = (uint8_t)p;
			f->msix_count = (uint16_t)((ctl & 0x7FF) + 1);
			f->msix_table_bar = t & 7;
			f->msix_table_off = t & ~7u;
			f->msix_pba_bar = b & 7;
			f->msix_pba_off = b & ~7u;
			break;
		}
		}
		p = rd(f->addr, p + 1, 1) & 0xFC;
	}
}

/* adresses et tailles des BAR ; la mesure écrit des 1 dans chaque BAR, le
 * décodage coupé (sauf pour l'affichage : on ne mesure pas) */
static void read_bars(struct pci_info *f)
{
	int n = (f->hdr & 0x7F) == 0 ? 6 : (f->hdr & 0x7F) == 1 ? 2 : 0;
	int measure = f->cls != 0x03;
	uint32_t cmd = rd(f->addr, 4, 2);
	if (measure)
		wr(f->addr, 4, 2, cmd & ~3u);
	for (int i = 0; i < n; i++) {
		unsigned off = 0x10 + 4 * i;
		uint32_t lo = rd(f->addr, off, 4);
		if (lo & 1) { /* ports d'E/S */
			f->bar_flags[i] = PCI_BAR_IO;
			f->bar[i] = lo & ~3u;
			if (measure) {
				wr(f->addr, off, 4, 0xFFFFFFFF);
				uint32_t s = rd(f->addr, off, 4) & ~3u;
				wr(f->addr, off, 4, lo);
				f->bar_size[i] = s ? (uint32_t)(~s + 1) & 0xFFFF : 0;
			}
			continue;
		}
		int is64 = ((lo >> 1) & 3) == 2;
		uint64_t a = lo & ~0xFull, size = 0;
		uint32_t hi = 0;
		if (is64 && i + 1 < n) {
			hi = rd(f->addr, off + 4, 4);
			a |= (uint64_t)hi << 32;
		}
		if (measure) {
			wr(f->addr, off, 4, 0xFFFFFFFF);
			uint64_t s = rd(f->addr, off, 4) & ~0xFull;
			wr(f->addr, off, 4, lo);
			if (is64 && i + 1 < n) {
				wr(f->addr, off + 4, 4, 0xFFFFFFFF);
				s |= (uint64_t)rd(f->addr, off + 4, 4) << 32;
				wr(f->addr, off + 4, 4, hi);
			} else if (s)
				s |= 0xFFFFFFFF00000000ull;
			size = s ? ~s + 1 : 0;
		}
		f->bar[i] = a;
		f->bar_size[i] = size;
		f->bar_flags[i] = (uint8_t)((is64 ? PCI_BAR_64 : 0) | ((lo & 8) ? PCI_BAR_PREFETCH : 0));
		if (is64)
			i++; /* la moitié haute */
	}
	if (measure)
		wr(f->addr, 4, 2, cmd);
}

static void scan_bus(uint16_t seg, unsigned bus, int depth);

static void scan_func(uint16_t seg, unsigned bus, unsigned dev, unsigned fn, int depth)
{
	uint32_t addr = PCI_ADDR(seg, bus, dev, fn);
	uint32_t id = rd(addr, 0, 4);
	if ((id & 0xFFFF) == 0xFFFF || nfuncs >= FUNC_MAX)
		return;
	struct pci_info *f = &funcs[nfuncs++];
	memset(f, 0, sizeof *f);
	f->addr = addr;
	f->vendor = (uint16_t)id;
	f->device = (uint16_t)(id >> 16);
	uint32_t cr = rd(addr, 8, 4);
	f->rev = (uint8_t)cr;
	f->prog = (uint8_t)(cr >> 8);
	f->sub = (uint8_t)(cr >> 16);
	f->cls = (uint8_t)(cr >> 24);
	f->hdr = (uint8_t)rd(addr, 0x0E, 1);
	f->ecam = cfg_phys(addr);
	if ((f->hdr & 0x7F) == 0) {
		f->irq_pin = (uint8_t)rd(addr, 0x3D, 1);
		f->svid = (uint16_t)rd(addr, 0x2C, 2);
		f->ssid = (uint16_t)rd(addr, 0x2E, 2);
	}
	read_bars(f);
	read_caps(f);
	if ((f->hdr & 0x7F) == 1) { /* pont : le bus d'en dessous */
		f->secondary = (uint8_t)rd(addr, 0x19, 1);
		unsigned sub = rd(addr, 0x1A, 1);
		if (f->secondary > bus && f->secondary <= sub && depth < 16)
			scan_bus(seg, f->secondary, depth + 1);
	}
}

static void scan_bus(uint16_t seg, unsigned bus, int depth)
{
	for (unsigned dev = 0; dev < 32; dev++) {
		uint32_t a = PCI_ADDR(seg, bus, dev, 0);
		if ((rd(a, 0, 4) & 0xFFFF) == 0xFFFF)
			continue;
		int multi = rd(a, 0x0E, 1) & 0x80;
		for (unsigned fn = 0; fn < (multi ? 8u : 1u); fn++)
			scan_func(seg, bus, dev, fn, depth);
	}
}

static int read_mcfg(void)
{
	static u8 t[4096];
	long n = korin_sys4(SYS_ACPI_TABLE, "MCFG", 0, t, sizeof t);
	if (n < 0) {
		say("pcid : pas de table MCFG (%ld)\n", n);
		return -1;
	}
	if (n > (long)sizeof t)
		n = sizeof t;
	for (long off = 44; off + 16 <= n && nsegs < SEG_MAX; off += 16) {
		struct seg *g = &segs[nsegs++];
		memcpy(&g->base, t + off, 8);
		memcpy(&g->seg, t + off + 8, 2);
		g->start = t[off + 10];
		g->end = t[off + 11];
		say("pcid : ECAM du segment %u à %lx, bus %u à %u\n", g->seg, (unsigned long)g->base,
		    g->start, g->end);
	}
	return nsegs ? 0 : -1;
}

static void enumerate(void)
{
	nfuncs = 0;
	for (int i = 0; i < nsegs; i++)
		scan_bus(segs[i].seg, segs[i].start, 0);
}

/* ---- affichage */

static const char *class_name(const struct pci_info *f)
{
	switch (f->cls) {
	case 0x01: return f->sub == 0x08 ? "NVMe" : f->sub == 0x06 ? "SATA" : "stockage";
	case 0x02: return "réseau";
	case 0x03: return "affichage";
	case 0x04: return f->sub == 0x03 ? "son HDA" : "multimédia";
	case 0x06: return f->sub == 0x04 ? "pont PCI" : f->sub == 0x00 ? "pont hôte" : f->sub == 0x01 ? "pont ISA" : "pont";
	case 0x08: return "système";
	case 0x0C:
		if (f->sub == 0x03)
			return f->prog == 0x30 ? "USB xHCI" : f->prog == 0x20 ? "USB EHCI" : "USB";
		return f->sub == 0x05 ? "SMBus" : "bus série";
	case 0x0D: return "sans fil";
	}
	return "?";
}

static void print_func(const struct pci_info *f)
{
	char a[16];
	pci_format_addr(f->addr, a);
	say("pcid : %s %04x:%04x %02x/%02x/%02x %s", a, f->vendor, f->device, f->cls, f->sub, f->prog,
	    class_name(f));
	if (f->cap_msix)
		say(", MSI-X %u", f->msix_count);
	if (f->cap_msi)
		say(", MSI %u%s", f->msi_max, f->msi_64 ? " (64)" : "");
	if (f->cap_pcie)
		say(", PCIe");
	say("\n");
	for (int i = 0; i < 6; i++) {
		if (!f->bar[i] && !f->bar_size[i])
			continue;
		say("pcid :     BAR%d %s %lx", i, f->bar_flags[i] & PCI_BAR_IO ? "E/S" : "mém.",
		    (unsigned long)f->bar[i]);
		if (f->bar_size[i])
			say(" (%lu Ko)", (unsigned long)((f->bar_size[i] + 1023) >> 10));
		say("%s%s\n", f->bar_flags[i] & PCI_BAR_64 ? ", 64 bits" : "",
		    f->bar_flags[i] & PCI_BAR_PREFETCH ? ", préchargeable" : "");
	}
}

/* ---- annonces à hwmanager */

static void announce(const struct pci_info *f)
{
	if (hw_chan == -2)
		return; /* absent : rien à annoncer */
	if (hw_chan < 0) {
		hw_chan = korin_sys2(SYS_SVC_OPEN, "hwmanager", 2000);
		if (hw_chan < 0) {
			say("pcid : hwmanager absent, pas d'annonce\n");
			hw_chan = -2;
			return;
		}
	}
	memset(hw_msg, 0, sizeof hw_msg);
	uint64_t *m = (uint64_t *)hw_msg;
	m[0] = HW_OP_PCI;
	m[1] = f->addr;
	m[2] = (uint32_t)f->cls << 16 | (uint32_t)f->sub << 8 | f->prog;
	m[3] = (uint32_t)f->vendor << 16 | f->device;
	m[4] = (uint32_t)f->svid << 16 | f->ssid;
	korin_sys2(SYS_SVC_CALL, hw_chan, hw_msg);
}

static void announce_all(void)
{
	for (int i = 0; i < nfuncs; i++)
		if ((funcs[i].hdr & 0x7F) == 0) /* les ponts ne prennent pas de pilote */
			announce(&funcs[i]);
}

/* ---- service */

static struct pci_info *func_of(uint32_t addr)
{
	for (int i = 0; i < nfuncs; i++)
		if (funcs[i].addr == addr)
			return &funcs[i];
	return 0;
}

static long op_find(uint64_t id, uint64_t cls, uint64_t mask, uint64_t from)
{
	unsigned v = (id >> 16) & 0xFFFF, d = id & 0xFFFF;
	for (uint64_t i = from; i < (uint64_t)nfuncs; i++) {
		struct pci_info *f = &funcs[i];
		uint32_t c = (uint32_t)f->cls << 16 | (uint32_t)f->sub << 8 | f->prog;
		if (v != 0xFFFF && f->vendor != v)
			continue;
		if (d != 0xFFFF && f->device != d)
			continue;
		if ((c & mask) != (cls & mask))
			continue;
		return (long)i;
	}
	return E_NOENT;
}

static long op_claim(uint32_t addr, int pid, u8 *data)
{
	struct pci_info *f = func_of(addr);
	if (!f)
		return E_NOENT;
	if (f->owner && f->owner != pid)
		return E_BUSY;
	f->owner = pid;
	uint32_t cmd = rd(addr, 4, 2) | 2 | 4 | (1 << 10); /* mémoire, maître, INTx coupé */
	for (int i = 0; i < 6; i++)
		if ((f->bar_flags[i] & PCI_BAR_IO) && f->bar_size[i])
			cmd |= 1;
	wr(addr, 4, 2, cmd);
	memcpy(data, f, sizeof *f);
	char a[16];
	pci_format_addr(addr, a);
	say("pcid : %s prise par le process %d\n", a, pid);
	return 0;
}

static int release_pid(int pid) /* -> le nombre de fonctions qu'il tenait */
{
	int n = 0;
	for (int i = 0; i < nfuncs; i++)
		if (funcs[i].owner == pid) {
			n++;
			funcs[i].owner = 0;
			/* maître du bus coupé : plus de DMA d'un pilote disparu ; sauf l'affichage, dont le
			 * framebuffer du GOP sert à l'écran (sonde du GPU Intel, docs/etude/GPU-INTEL.md) */
			if (funcs[i].cls != 0x03)
				wr(funcs[i].addr, 4, 2, rd(funcs[i].addr, 4, 2) & ~4u);
		}
	return n;
}

/* (E41) ce qu'un pilote peut écrire dans la configuration de sa fonction : une adresse MSI seulement
 * dans la fenêtre des interruptions (celles que rend SYS_IRQ_MSI) : sinon le périphérique écrit, par
 * une « interruption », où l'on veut en RAM ; une BAR seulement si elle vaut 0 (i2chid assigne une
 * BAR0 que le micrologiciel a laissée vide) ; rien dans un pont (ses fenêtres) */
static uint32_t merged(uint32_t cur, unsigned off, unsigned size, uint32_t v)
{
	unsigned sh = (off & 3) * 8;
	uint32_t mask = size == 4 ? 0xFFFFFFFFu : size == 2 ? 0xFFFFu : 0xFFu;
	return (cur & ~(mask << sh)) | ((v & mask) << sh);
}

static int cfg_write_ok(struct pci_info *f, unsigned off, unsigned size, uint32_t v)
{
	if ((f->hdr & 0x7F) != 0)
		return 0;
	unsigned d = off & 0xFFC; /* (une écriture alignée ne passe pas un mot de 32 bits, wr) */
	if (d >= 0x10 && d < 0x28) { /* une BAR déjà assignée : refusée. « Vide » = son ADRESSE à 0 : une BAR 64 bits
				       * vide se relit 0x4 (ses bits de type) ; avant, ce 4 la faisait croire assignée, et
				       * i2chid ne pouvait plus donner son adresse au touchpad du ZBook (26/11/2026) */
		uint32_t cur = rd(f->addr, d, 4);
		uint32_t prev = d > 0x10 ? rd(f->addr, d - 4, 4) : 1;
		int upper = d > 0x10 && !(prev & 1) && (prev & 6) == 4; /* (la moitié haute d'une BAR 64 bits) */
		uint32_t addr = upper ? cur : (cur & 1) ? cur & ~3u : cur & ~0xFu;
		if (addr != 0)
			return 0;
	}
	if (d == 0x30)
		return 0; /* la ROM d'extension */
	if (f->cap_msi) {
		unsigned a = f->cap_msi + 4u;
		if (d == a && (merged(rd(f->addr, a, 4), off, size, v) & 0xFFF00000u) != 0xFEE00000u)
			return 0;
		if (f->msi_64 && d == a + 4 && merged(rd(f->addr, a + 4, 4), off, size, v) != 0)
			return 0;
	}
	return 1;
}

static void serve(long req)
{
	uint64_t *m = (uint64_t *)msg;
	int pid = (int)*(uint32_t *)(msg + 44);
	long r = E_INVAL;
	struct pci_info *f;
	if (m[0] >= 0xFFF0) { /* les avis : du noyau seulement (pid 0), sans réponse */
		/* un pilote fini : le maître du bus de ses fonctions coupé, puis ses pages DMA, que le noyau a mises de côté,
		 * rendues (DMA_RELEASE ; sans elle, le noyau les rend au bout de 10 s : étape 3 de
		 * docs/etude/DURCISSEMENT-N3-N5.md) */
		if (m[0] == SVC_OP_END && svc_from_kernel(msg) && release_pid((int)m[1]))
			korin_sys1(SYS_DMA_RELEASE, (long)m[1]);
		return;
	}
	/* prendre, rendre, écrire la configuration, relire le bus, annoncer : les pilotes et init2 (droits
	 * de /sys) ; COUNT, INFO, FIND, CFG_READ restent ouverts (lspci) */
	if ((m[0] == PCI_OP_CLAIM || m[0] == PCI_OP_RELEASE || m[0] == PCI_OP_CFG_WRITE || m[0] == PCI_OP_RESCAN ||
	     m[0] == PCI_OP_ANNOUNCE) && !caller_rights(pid)) {
		m[0] = (uint64_t)E_PERM;
		korin_sys2(SYS_SVC_REPLY, req, msg);
		return;
	}
	switch (m[0]) {
	case SVC_OP_SIGNAL:
		return;
	case PCI_OP_COUNT:
		r = nfuncs;
		break;
	case PCI_OP_INFO:
		if (m[1] >= (uint64_t)nfuncs)
			r = E_NOENT;
		else {
			memcpy(msg + 48, &funcs[m[1]], sizeof funcs[0]);
			r = 0;
		}
		break;
	case PCI_OP_FIND:
		r = op_find(m[1], m[2], m[3], m[4]);
		break;
	case PCI_OP_CLAIM:
		r = op_claim((uint32_t)m[1], pid, msg + 48);
		break;
	case PCI_OP_RELEASE:
		f = func_of((uint32_t)m[1]);
		r = !f ? E_NOENT : f->owner != pid ? E_PERM : 0;
		if (r == 0)
			f->owner = 0;
		break;
	case PCI_OP_CFG_READ:
		if (!func_of((uint32_t)m[1]) || m[2] > 0xFFF || (m[3] != 1 && m[3] != 2 && m[3] != 4))
			break;
		r = rd((uint32_t)m[1], (unsigned)m[2], (unsigned)m[3]);
		break;
	case PCI_OP_CFG_WRITE:
		f = func_of((uint32_t)m[1]);
		if (!f || m[2] > 0xFFF || (m[3] != 1 && m[3] != 2 && m[3] != 4))
			break;
		if (f->owner != pid || !cfg_write_ok(f, (unsigned)m[2], (unsigned)m[3], (uint32_t)m[4])) {
			r = E_PERM;
			break;
		}
		wr((uint32_t)m[1], (unsigned)m[2], (unsigned)m[3], (uint32_t)m[4]);
		r = 0;
		break;
	case PCI_OP_RESCAN: {
		static struct pci_info keep[FUNC_MAX]; /* les prises restent */
		int n = nfuncs;
		memcpy(keep, funcs, sizeof funcs[0] * (size_t)n);
		enumerate();
		for (int i = 0; i < nfuncs; i++)
			for (int j = 0; j < n; j++)
				if (keep[j].addr == funcs[i].addr)
					funcs[i].owner = keep[j].owner;
		announce_all();
		r = nfuncs;
		break;
	}
	case PCI_OP_ANNOUNCE:
		if (hw_chan == -2)
			hw_chan = -1; /* (absent plus tôt : on essaie de nouveau) */
		r = 0;
		for (int i = 0; i < nfuncs; i++)
			if ((funcs[i].hdr & 0x7F) == 0 && !funcs[i].owner) { /* (comme announce_all) */
				announce(&funcs[i]);
				r++;
			}
		break;
	}
	m[0] = (uint64_t)r;
	korin_sys2(SYS_SVC_REPLY, req, msg);
}

int main(int argc, char **argv)
{
	int list = 0, quiet = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-l"))
			list = 1;
		else if (!strcmp(argv[i], "-n"))
			quiet = 1;
	}
	if (!list) {
		chan = korin_sys2(SYS_SVC_REGISTER, "pci", SVF_ENDS);
		if (chan < 0) {
			say("pcid : service « pci » refusé (%ld)\n", chan);
			return 1;
		}
	}
	if (read_mcfg())
		return 1;
	enumerate();
	say("pcid : %d fonction(s)\n", nfuncs);
	for (int i = 0; i < nfuncs; i++)
		print_func(&funcs[i]);
	if (list)
		return 0;
	if (!quiet)
		announce_all();
	say("pcid : en service\n");
	for (;;) {
		long req = korin_sys3(SYS_SVC_NEXT, chan, msg, 60000);
		if (req < 0)
			return 1;
		if (req > 0)
			serve(req);
	}
}
