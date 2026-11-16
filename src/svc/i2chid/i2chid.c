/* i2chid - le touchpad I2C du PC (HP ZBook Fury G8 : contrôleur I2C du LPSS d'Intel, 0000:00:15.0,
 * 8086:43e8 ; touchpad HID over I2C, « precision touchpad » de Windows), lancé à la main.
 * docs/etude/TOUCHPAD-PC.md, docs/etude/PC-PILOTES.md « Touchpad du ZBook ».
 *
 *	i2chid probe       la fonction PCI, sa BAR, le LPSS et le DesignWare (rien sur le bus)
 *	i2chid scan        et les adresses qui répondent sur le bus (une lecture d'un octet à chacune)
 *	i2chid info        le descripteur HID, le descripteur de rapport, ce qu'i2chid y comprend, puis arrêt
 *	i2chid run         le pilote : souris pour hidmanager, jusqu'à « i2chid stop »
 *	i2chid stop        arrête le pilote en marche
 *	  -v               plus de détails (le descripteur de rapport en hexadécimal, les rapports lus avec -vv)
 *	  clé=valeur       un réglage de /etc/i2chid.txt pour cette fois (addr=0x2c hid_desc=0x20…)
 *
 * Réglages (/etc/i2chid.txt, « clé = valeur », # commentaire), à remplir d'après le DSDT / les SSDT :
 *	pci = 0000:00:15.0   la fonction (défaut : le premier I2C du LPSS d'Intel trouvé)
 *	bar = auto | 0x…     adresse à donner à la BAR0 si le micrologiciel l'a laissée à 0 (auto : un trou
 *	                     de 4 Ko entre les BAR des fonctions du bus 0, hors des fenêtres des ponts)
 *	clock_mhz = 100      horloge d'entrée du contrôleur
 *	speed = 400          vitesse du bus (kHz : 100 ou 400 ; le _CRS dit 400000 en général)
 *	hcnt = …, lcnt = …   comptes de SCL haut / bas (FMCN ou SSCN de l'ACPI), à la place du calcul
 *	sda_hold = 300       maintien de SDA (ns)
 *	addr = 0x2c          adresse I2C du touchpad (I2cSerialBusV2 du _CRS ; ZBook : _INI de TPD0, BADR = 0x2C)
 *	hid_desc = 0x20      registre du descripteur HID (le _DSM de HID over I2C ; ZBook : HID2 = 0x20)
 *	irq = 93             interruption directe (Interrupt du _CRS : GSI de l'IOAPIC, niveau, actif bas)
 *	gpio = 0x…           ou l'adresse physique du DW0 du plot GPIO (GpioInt), son bit 1 lu toutes les
 *	                     poll ms ; gpio_low = 1 : actif à 0, 0 : actif à 1, -1 : le niveau au repos, mesuré
 *	                     après le RESET, dit lequel
 *	Valeurs lues dans la mémoire NVS du micrologiciel (sans interprète d'AML : le champ est donné par
 *	« RÉGION+décalage », la région étant cherchée dans le DSDT vivant, OperationRegion SystemMemory) ;
 *	elles remplissent ce qui n'est pas donné plus haut :
 *	nvs_gpdi = GNVS+0x479   le plot du touchpad (GPDI : groupe << 16 | plot) -> gpio (PCH-H)
 *	nvs_tpdm = GNVS+0x40D   0 : GpioInt (plot sondé), sinon Interrupt directe (INTSEL du plot -> irq)
 *	nvs_sbrg = PNVA+0x9C    base du P2SB (SBREG), pour l'adresse du plot
 *	nvs_pchs = PNVA+0x0     sorte du PCH (1 : H)
 *	nvs_fmcn = GNVS+0x226   FMH0, FML0, FMD0 (FMCN du contrôleur : HCNT, LCNT, maintien de SDA en cycles)
 *	nvs_ustp = GNVS+0x43B   FMCN existe-t-il (USTP)
 *	poll = 8             sondage du touchpad (ms) sans irq ni gpio
 *	mode = auto          auto : pavé (« precision touchpad ») s'il le peut, sinon souris ; mouse ; ptp
 *	pointer = 100        vitesse du pointeur (%)
 *	tap = 1              toucher pour cliquer
 *	natural = 1          défilement « naturel » (le contenu suit les doigts) ; 0 : comme une molette
 *	scroll = 300         course des deux doigts pour un cran de molette (centièmes de mm)
 *
 * Lancé de /bin/i2chid (sans droits) : relance /sys/svc/i2chid et recopie ses lignes du
 * journal (src/svc/common/sysfront.c). Pour le PC seulement (svc.conf : arch = x86_64). */
#include "i2chid.h"

#define E_INTR -4
#define HID_OP_PILOTE 3 /* service « hid » (hidmanager) */
#define RING_EV 64
#define RING_N 64
#define OP_STOP 1       /* service « i2chid » */
#define SIGM_BLOCK 1    /* SYS_SIG_MASK (src/inc/syscalls.inc) */
#define SIGM_PENDING 3
#define SIGTERM_BIT (1L << 15)

static struct {
	char pci[24];
	u64 bar;
	int bar_auto;
	u32 clock_mhz, speed, hcnt, lcnt, sda_hold, sda_cycles;
	u32 addr, hid_desc;
	long irq;
	u64 gpio;
	int gpio_low;
	int poll;
	int mode; /* 0 auto, 1 souris, 2 pavé */
	int pointer, tap, natural, scroll;
	int verbose;
	char nvs[6][24]; /* gpdi, tpdm, sbrg, pchs, fmcn, ustp */
} C = {.bar_auto = 1, .clock_mhz = 100, .speed = 400, .addr = 0x2C, .hid_desc = 0x20, .irq = -1, .gpio_low = -1,
       .poll = 8, .pointer = 100, .tap = 1, .natural = 1, .scroll = 300};

static struct pci_info pinfo;
static int claimed;
static struct dw D;
static struct hid_desc HD;
static struct hid_model HM;
static struct gest G;
static volatile u64 *ring;

/* le descripteur HID (HID over I2C 1.0, 5.1.1) */
static struct {
	u16 len, bcd, rd_len, rd_reg, in_reg, in_max, out_reg, out_max, cmd_reg, data_reg, vid, pid, ver;
} H;

/* ---- réglages */
static int set_hcnt, set_gpio, set_irq; /* (donnés par l'utilisateur : la NVS ne les remplace pas) */

static int set_key(const char *k, const char *v)
{
	char *e;
	u64 n = strtoul(v, &e, 0);
	int num = e != v;
	static const char *const nvs_keys[6] = {"nvs_gpdi", "nvs_tpdm", "nvs_sbrg", "nvs_pchs", "nvs_fmcn", "nvs_ustp"};
	for (int i = 0; i < 6; i++)
		if (!strcmp(k, nvs_keys[i])) {
			snprintf(C.nvs[i], sizeof C.nvs[i], "%s", v);
			return 0;
		}
	if (!strcmp(k, "pci"))
		snprintf(C.pci, sizeof C.pci, "%s", v);
	else if (!strcmp(k, "bar")) {
		C.bar_auto = !strcmp(v, "auto");
		C.bar = C.bar_auto ? 0 : n;
	} else if (!num)
		return -1;
	else if (!strcmp(k, "clock_mhz"))
		C.clock_mhz = (u32)n;
	else if (!strcmp(k, "speed"))
		C.speed = (u32)n;
	else if (!strcmp(k, "hcnt"))
		C.hcnt = (u32)n, set_hcnt = 1;
	else if (!strcmp(k, "lcnt"))
		C.lcnt = (u32)n;
	else if (!strcmp(k, "sda_hold"))
		C.sda_hold = (u32)n;
	else if (!strcmp(k, "addr"))
		C.addr = (u32)n & 0x7F;
	else if (!strcmp(k, "hid_desc"))
		C.hid_desc = (u32)n & 0xFFFF;
	else if (!strcmp(k, "irq"))
		C.irq = (long)n, set_irq = 1;
	else if (!strcmp(k, "gpio"))
		C.gpio = n, set_gpio = 1;
	else if (!strcmp(k, "gpio_low"))
		C.gpio_low = (int)(long)n;
	else if (!strcmp(k, "poll"))
		C.poll = n < 2 ? 2 : (int)n;
	else if (!strcmp(k, "pointer"))
		C.pointer = (int)n;
	else if (!strcmp(k, "tap"))
		C.tap = (int)n;
	else if (!strcmp(k, "natural"))
		C.natural = (int)n;
	else if (!strcmp(k, "scroll"))
		C.scroll = (int)n;
	else
		return -1;
	return 0;
}

static int set_line(char *l)
{
	while (*l == ' ' || *l == '\t')
		l++;
	if (!*l || *l == '#')
		return 0;
	char *eq = strchr(l, '=');
	if (!eq)
		return -1;
	char *ke = eq;
	while (ke > l && (ke[-1] == ' ' || ke[-1] == '\t'))
		ke--;
	*ke = 0;
	char *v = eq + 1;
	while (*v == ' ' || *v == '\t')
		v++;
	char *ve = v + strlen(v);
	while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r'))
		*--ve = 0;
	if (!strcmp(l, "mode")) {
		C.mode = !strcmp(v, "mouse") ? 1 : !strcmp(v, "ptp") ? 2 : 0;
		return 0;
	}
	return set_key(l, v);
}

static void read_config(void)
{
	long fd = sys_open("/etc/i2chid.txt", O_READ);
	if (fd < 0)
		return;
	static char buf[4096];
	long n = sys_read(fd, buf, sizeof buf - 1);
	sys_close(fd);
	if (n <= 0)
		return;
	buf[n] = 0;
	char *l = buf;
	while (*l) {
		char *e = strchr(l, '\n');
		if (e)
			*e = 0;
		if (set_line(l))
			slog("/etc/i2chid.txt : ligne ignorée : %s\n", l);
		if (!e)
			break;
		l = e + 1;
	}
}

/* ---- valeurs du micrologiciel (docs/etude/TOUCHPAD-PC.md, « Ce que disent les tables du ZBook ») */

static u8 *dsdt;
static long dsdt_len;

/* une constante d'AML (BytePrefix, WordPrefix, DWordPrefix, QWordPrefix) -> valeur, 0 sinon */
static u64 aml_const(const u8 *p, const u8 *end)
{
	if (p >= end)
		return 0;
	int n = p[0] == 0x0A ? 1 : p[0] == 0x0B ? 2 : p[0] == 0x0C ? 4 : p[0] == 0x0E ? 8 : 0;
	if (!n || p + 1 + n > end)
		return 0;
	u64 v = 0;
	for (int i = 0; i < n; i++)
		v |= (u64)p[1 + i] << (8 * i);
	return v;
}

/* l'adresse d'une OperationRegion SystemMemory du DSDT vivant : une constante, ou un Name constant
 * (PNVA : PNVB) -> 0 si introuvable */
static u64 aml_region(const char *name)
{
	if (!dsdt) {
		dsdt = sys_vm_map(1u << 20);
		if (!dsdt)
			return 0;
		dsdt_len = korin_sys4(SYS_ACPI_TABLE, "DSDT", 0, dsdt, 1u << 20);
		if (dsdt_len > (1 << 20))
			dsdt_len = 1 << 20;
	}
	const u8 *end = dsdt + (dsdt_len > 0 ? dsdt_len : 0);
	for (const u8 *p = dsdt + 36; p + 8 < end; p++) {
		if (p[0] != 0x5B || p[1] != 0x80 || memcmp(p + 2, name, 4) || p[6] != 0x00) /* OpRegionOp, SystemMemory */
			continue;
		const u8 *a = p + 7;
		if (a[0] == 0x0A || a[0] == 0x0B || a[0] == 0x0C || a[0] == 0x0E)
			return aml_const(a, end);
		for (const u8 *q = dsdt + 36; q + 6 < end; q++) /* un nom : Name (XXXX, constante) */
			if (q[0] == 0x08 && !memcmp(q + 1, a, 4))
				return aml_const(q + 5, end);
		return 0;
	}
	return 0;
}

/* « GNVS+0x479 » (ou une adresse physique), plus off -> la valeur de size octets (adresse dans *where),
 * < 0 si illisible */
static long nvs_read(const char *spec, u32 off, int size, u64 *where)
{
	if (!spec[0])
		return -1;
	u64 a;
	char *e;
	if (strlen(spec) > 5 && spec[4] == '+') {
		char name[5];
		memcpy(name, spec, 4);
		name[4] = 0;
		a = aml_region(name);
		if (!a) {
			slog("%s : région %s introuvable dans le DSDT\n", spec, name);
			return -1;
		}
		a += strtoul(spec + 5, &e, 0);
	} else
		a = strtoul(spec, &e, 0);
	a += off;
	if (where)
		*where = a;
	u8 b[8] = {0};
	long r = korin_sys4(SYS_ACPI_TABLE, "FWMM", a, b, (u64)size);
	if (r < size) {
		slog("%s : %lx, mémoire du micrologiciel illisible (%ld)\n", spec, (unsigned long)a, r);
		return -1;
	}
	long v = 0;
	for (int i = 0; i < size; i++)
		v |= (long)b[i] << (8 * i);
	return v;
}

/* les groupes de plots GPIO du PCH-H de Tiger Lake (GPCH du DSDT du ZBook) : décalage de la communauté
 * dans le P2SB, décalage du premier registre PADCFG (0xFFFF : pas de plots) */
static const u32 tglh_groups[19][2] = {
	{0x6E0000, 0x600}, {0x6E0000, 0x790}, {0x6E0000, 0x8D0}, {0x6E0000, 0xA70}, {0x6D0000, 0x600},
	{0x6D0000, 0x7A0}, {0x6D0000, 0x920}, {0x6D0000, 0x9A0}, {0x6D0000, 0xAB0}, {0x6C0000, 0x600},
	{0x6B0000, 0x600}, {0x6B0000, 0x6D0}, {0x6B0000, 0xFFFF}, {0x6A0000, 0x600}, {0x6A0000, 0x780},
	{0x6A0000, 0x820}, {0x690000, 0x600}, {0x690000, 0x6F0}, {0x690000, 0x780},
};

/* les valeurs de la NVS : FMCN, le plot du touchpad (GpioInt : sondé ; Interrupt : son INTSEL) */
static void firmware_values(void)
{
	int any = 0;
	for (int i = 0; i < 6; i++)
		any |= C.nvs[i][0] != 0;
	if (!any)
		return;
	if (!set_hcnt && C.nvs[4][0]) {
		long ustp = C.nvs[5][0] ? nvs_read(C.nvs[5], 0, 1, 0) : 1;
		long h = nvs_read(C.nvs[4], 0, 2, 0), l = nvs_read(C.nvs[4], 2, 2, 0), d = nvs_read(C.nvs[4], 4, 2, 0);
		if (ustp > 0 && h > 0 && l > 0) {
			C.hcnt = (u32)h;
			C.lcnt = (u32)l;
			if (d > 0)
				C.sda_cycles = (u32)d;
			slog("FMCN du micrologiciel : HCNT %ld, LCNT %ld, maintien de SDA %ld cycles\n", h, l, d);
		} else
			slog("pas de FMCN (USTP %ld, %ld/%ld) : HCNT et LCNT calculés\n", ustp, h, l);
	}
	long gpdi = nvs_read(C.nvs[0], 0, 4, 0), tpdm = nvs_read(C.nvs[1], 0, 1, 0);
	long sbrg = nvs_read(C.nvs[2], 0, 4, 0), pchs = nvs_read(C.nvs[3], 0, 2, 0);
	if (gpdi < 0)
		return;
	u32 grp = (u32)(gpdi >> 16) & 0xFF, pad = (u32)gpdi & 0xFFFF;
	slog("touchpad : GPDI %08lx (groupe %u, plot %u), TPDM %ld (%s), SBREG %lx, PCH %ld\n", gpdi, grp, pad, tpdm,
	     tpdm == 0 ? "GpioInt" : tpdm > 0 ? "Interrupt directe" : "inconnu", sbrg, pchs);
	if (sbrg <= 0 || pchs != 1 || grp >= 19 || tglh_groups[grp][1] == 0xFFFF) {
		slog("adresse du plot inconnue (PCH-H de Tiger Lake seulement) : gpio=0x… à la main\n");
		return;
	}
	u64 dw0 = (u64)sbrg + tglh_groups[grp][0] + tglh_groups[grp][1] + pad * 16u;
	long v = korin_sys2(SYS_DEV_MAP, dw0 & ~0xFFFull, 4096);
	if (v < 0) {
		slog("plot %lx : DEV_MAP refusé (%ld)\n", (unsigned long)dw0, v);
		return;
	}
	volatile u32 *r = (volatile u32 *)(v + (long)(dw0 & 0xFFF));
	u32 d0 = r[0], d1 = r[1];
	slog("plot %lx : DW0 %08x (mode %u, RX %s, état RX %u, RXINV %u, routage%s%s%s%s), DW1 %08x (INTSEL %u)\n",
	     (unsigned long)dw0, d0, (d0 >> 10) & 7, (d0 >> 9) & 1 ? "coupé" : "en marche", (d0 >> 1) & 1, (d0 >> 23) & 1,
	     (d0 >> 20) & 1 ? " APIC" : "", (d0 >> 19) & 1 ? " SCI" : "", (d0 >> 18) & 1 ? " SMI" : "",
	     (d0 >> 17) & 1 ? " NMI" : "", d1, d1 & 0x7F);
	if (d0 == 0xFFFFFFFF) {
		slog("plot illisible (SBREG faux ?)\n");
		return;
	}
	if (tpdm == 0 && !set_gpio)
		C.gpio = dw0;
	else if (tpdm > 0 && !set_irq && ((d0 >> 20) & 1))
		C.irq = d1 & 0x7F;
}

/* ---- la fonction PCI */

/* identifiants des I2C du LPSS d'Intel (Tiger Lake-H 43xx, Tiger Lake-LP a0xx, Alder Lake 51xx / 7axx,
 * Comet Lake 02xx / 06xx, Cannon Lake 9dxx / a3xx) */
static const u16 lpss_i2c[] = {
	0x43E8, 0x43E9, 0x43EA, 0x43EB, 0x43AD, 0x43AE, 0xA0E8, 0xA0E9, 0xA0EA, 0xA0EB, 0xA0C5, 0xA0C6, 0x51E8,
	0x51E9, 0x51EA, 0x51EB, 0x51C5, 0x51C6, 0x7AE8, 0x7AE9, 0x7AEA, 0x7AEB, 0x7AFC, 0x7AFD, 0x02E8, 0x02E9,
	0x02EA, 0x02EB, 0x02C5, 0x02C6, 0x06E8, 0x06E9, 0x06EA, 0x06EB, 0x9DE8, 0x9DE9, 0x9DEA, 0x9DEB, 0xA368,
	0xA369, 0xA36A, 0xA36B,
};

static int find_function(void)
{
	long r = pci_open(5000);
	if (r < 0) {
		slog("service pci absent (%ld) : pcid n'est pas lancé\n", r);
		return -1;
	}
	if (C.pci[0]) {
		long a = pci_parse_addr(C.pci);
		if (a < 0) {
			slog("adresse PCI illisible : %s\n", C.pci);
			return -1;
		}
		pinfo.addr = (u32)a;
		return 1;
	}
	long from = 0;
	struct pci_info f;
	while (pci_find_class(0x0C8000, 0xFFFF00, &from, &f) == 0) { /* 0c/80 : autre bus série */
		for (unsigned i = 0; i < sizeof lpss_i2c / sizeof lpss_i2c[0]; i++)
			if (f.vendor == 0x8086 && f.device == lpss_i2c[i]) {
				pinfo = f;
				return 1;
			}
	}
	slog("pas de contrôleur I2C du LPSS d'Intel (classe 0c/80, 8086:43e8…) : rien à faire\n");
	return 0;
}

/* les fenêtres mémoire d'un pont (mémoire, préchargeable) -> nombre (0..2) */
static int bridge_windows(u32 addr, u64 *lo, u64 *hi)
{
	int n = 0;
	long m = pci_cfg_read(addr, 0x20, 4);
	if (m >= 0) {
		u64 b = (u64)(m & 0xFFF0) << 16, l = ((u64)((m >> 16) & 0xFFF0) << 16) | 0xFFFFF;
		if (l > b) {
			lo[n] = b;
			hi[n++] = l + 1;
		}
	}
	long p = pci_cfg_read(addr, 0x24, 4), pu = pci_cfg_read(addr, 0x28, 4), lu = pci_cfg_read(addr, 0x2C, 4);
	if (p >= 0 && pu >= 0 && lu >= 0) {
		u64 b = (u64)(p & 0xFFF0) << 16, l = ((u64)((p >> 16) & 0xFFF0) << 16) | 0xFFFFF;
		if ((p & 0xF) == 1) {
			b |= (u64)(u32)pu << 32;
			l |= (u64)(u32)lu << 32;
		}
		if (l > b) {
			lo[n] = b;
			hi[n++] = l + 1;
		}
	}
	return n;
}

/* une adresse pour la BAR0 laissée à 0 : un trou de 4 Ko entre les petites BAR 64 bits du bus 0 (là où
 * le micrologiciel range celles du PCH), hors de toute BAR et de toute fenêtre de pont -> 0 si aucun */
static u64 pick_bar(void)
{
	static u64 lo[96], hi[96];
	int n = 0;
	u64 cmin = ~0ull, cmax = 0;
	long from = 0;
	struct pci_info f;
	while (n < 90 && pci_find_class(0, 0, &from, &f) == 0) {
		if ((f.hdr & 0x7F) == 1) {
			n += bridge_windows(f.addr, lo + n, hi + n);
			continue;
		}
		for (int i = 0; i < 6 && n < 94; i++) {
			if (!f.bar[i] || !f.bar_size[i] || (f.bar_flags[i] & PCI_BAR_IO))
				continue;
			lo[n] = f.bar[i];
			hi[n] = f.bar[i] + f.bar_size[i];
			n++;
			if (((f.addr >> 8) & 0xFF) == 0 && f.bar[i] >= (1ull << 32) && f.bar_size[i] <= (1u << 20)) {
				if (f.bar[i] < cmin)
					cmin = f.bar[i];
				if (f.bar[i] + f.bar_size[i] > cmax)
					cmax = f.bar[i] + f.bar_size[i];
			}
		}
	}
	if (cmin >= cmax)
		return 0;
	for (u64 a = cmin; a + 4096 <= cmax; a += 4096) {
		int used = 0;
		for (int i = 0; i < n && !used; i++)
			used = a < hi[i] && a + 4096 > lo[i];
		if (!used)
			return a;
	}
	return 0;
}

static int setup_function(void)
{
	char a[16];
	pci_format_addr(pinfo.addr, a);
	long r = pci_claim(pinfo.addr, &pinfo);
	if (r < 0) {
		if (r == E_NOENT)
			slog("%s : pas de fonction PCI à cette adresse (pci= de i2chid.txt : pas ce PC ?) : rien à faire\n", a);
		else
			slog("%s : refusée par pcid (%ld)%s\n", a, r, r == -16 ? " : un autre pilote la tient" : "");
		return -1;
	}
	claimed = 1;
	slog("%s : %04x:%04x, classe %02x/%02x/%02x, BAR0 %lx (%lu Ko)%s\n", a, pinfo.vendor, pinfo.device, pinfo.cls,
	     pinfo.sub, pinfo.prog, (unsigned long)pinfo.bar[0], (unsigned long)(pinfo.bar_size[0] >> 10),
	     pinfo.bar_flags[0] & PCI_BAR_64 ? ", 64 bits" : "");
	/* D0 (gestion d'énergie : PMCSR, bits 1-0) */
	if (pinfo.cap_pm) {
		long pm = pci_cfg_read(pinfo.addr, pinfo.cap_pm + 4u, 2);
		if (pm >= 0 && (pm & 3)) {
			slog("la fonction est en D%ld : passage en D0\n", pm & 3);
			pci_cfg_write(pinfo.addr, pinfo.cap_pm + 4u, 2, (u32)(pm & ~3L));
			sys_sleep(10);
		}
	}
	u64 bar = pinfo.bar[0];
	if (!bar) {
		u64 want = C.bar_auto ? pick_bar() : C.bar;
		if (!want) {
			slog("BAR0 laissée à 0 par le micrologiciel, et aucun trou trouvé : donner bar=0x… (i2chid.txt)\n");
			return -1;
		}
		slog("BAR0 laissée à 0 par le micrologiciel : %lx lui est donnée (%s)\n", (unsigned long)want,
		     C.bar_auto ? "un trou entre les BAR du bus 0 ; bar=0x… pour une autre" : "bar= de i2chid.txt");
		pci_cfg_write(pinfo.addr, 0x10, 4, (u32)want);
		if (pinfo.bar_flags[0] & PCI_BAR_64)
			pci_cfg_write(pinfo.addr, 0x14, 4, (u32)(want >> 32));
		long lo = pci_cfg_read(pinfo.addr, 0x10, 4), hi = pci_cfg_read(pinfo.addr, 0x14, 4);
		u64 got = (u64)(lo & ~0xFL) | ((pinfo.bar_flags[0] & PCI_BAR_64) ? (u64)(u32)hi << 32 : 0);
		if (got != want) {
			slog("la BAR0 relue vaut %lx, pas %lx\n", (unsigned long)got, (unsigned long)want);
			return -1;
		}
		bar = want;
	}
	/* décodage mémoire (pcid l'allume à la prise ; la BAR vient peut-être de changer) */
	long cmd = pci_cfg_read(pinfo.addr, 4, 2);
	if (cmd >= 0 && !(cmd & 2))
		pci_cfg_write(pinfo.addr, 4, 2, (u32)cmd | 2);
	long v = korin_sys2(SYS_DEV_MAP, bar, 4096);
	if (v < 0) {
		slog("BAR0 %lx : DEV_MAP refusé (%ld)\n", (unsigned long)bar, v);
		return -1;
	}
	D.regs = (volatile u8 *)v;
	D.phys = bar;
	return 0;
}

/* ---- HID over I2C (spécification de Microsoft, version 1.0) */
static int hid_write(const u8 *w, u32 n)
{
	return dw_xfer(&D, C.addr, w, n, 0, 0);
}

static int hid_cmd(u8 opcode, u8 type, u8 id, const u8 *data, u32 dlen)
{
	u8 b[96];
	u32 n = 0;
	b[n++] = (u8)H.cmd_reg;
	b[n++] = (u8)(H.cmd_reg >> 8);
	b[n++] = (u8)(type << 4 | (id < 15 ? id : 15));
	b[n++] = opcode;
	if (id >= 15)
		b[n++] = id;
	if (data) {
		u32 len = 2 + (id ? 1 : 0) + dlen;
		b[n++] = (u8)H.data_reg;
		b[n++] = (u8)(H.data_reg >> 8);
		b[n++] = (u8)len;
		b[n++] = (u8)(len >> 8);
		if (id)
			b[n++] = id;
		for (u32 i = 0; i < dlen && n < sizeof b; i++)
			b[n++] = data[i];
	}
	return hid_write(b, n);
}

static int set_power(int on)
{
	return hid_cmd(8, 0, on ? 0 : 1, 0, 0); /* SET_POWER : ON 0, SLEEP 1 */
}

static u8 inbuf[4096];

/* un rapport d'entrée : lecture du registre d'entrée, sans écriture d'adresse -> longueur des données
 * (après les 2 octets de longueur), 0 : rien, < 0 erreur */
static long read_input(void)
{
	u32 max = H.in_max >= 2 && H.in_max <= sizeof inbuf ? H.in_max : 64;
	int r = dw_xfer(&D, C.addr, 0, 0, inbuf, max);
	if (r)
		return r;
	u32 len = (u32)(inbuf[0] | inbuf[1] << 8);
	if (len == 0 || len == 0xFFFF)
		return 0;
	if (len < 2 || len > max)
		return 0; /* (longueur fausse : ignoré) */
	return (long)len - 2;
}

static int read_hid_desc(u32 reg)
{
	u8 w[2] = {(u8)reg, (u8)(reg >> 8)}, b[30];
	int r = dw_xfer(&D, C.addr, w, 2, b, 30);
	if (r)
		return r;
	u16 *f = (u16 *)&H;
	for (int i = 0; i < 13; i++)
		f[i] = (u16)(b[2 * i] | b[2 * i + 1] << 8);
	if (H.len != 30 || H.bcd != 0x0100) {
		slog("descripteur HID refusé au registre %04x (longueur %u, version %04x : attendu 30, 0100)\n", reg, H.len,
		     H.bcd);
		return -3;
	}
	return 0;
}

static void hexdump(const char *what, const u8 *p, u32 n)
{
	for (u32 i = 0; i < n; i += 24) {
		char l[100];
		int k = 0;
		for (u32 j = i; j < n && j < i + 24; j++)
			k += snprintf(l + k, sizeof l - k, "%02x ", p[j]);
		slog("%s %04x: %s\n", what, i, l);
	}
}

static const char *abort_text(u32 s)
{
	if (s & 1)
		return "adresse sans réponse (NACK)";
	if (s & 8)
		return "donnée refusée (NACK)";
	if (s & (1 << 12))
		return "arbitrage perdu";
	return "abandon";
}

static u8 rdesc[4096];

/* descripteurs HID et de rapport, modèle -> 1 souris, 2 pavé, 3 les deux ; < 0 */
static int describe(void)
{
	int r = read_hid_desc(C.hid_desc);
	if (r) {
		if (r == -1)
			slog("adresse %02x, registre %04x : %s (adresse ou registre faux ? i2chid scan)\n", C.addr, C.hid_desc,
			     abort_text(D.abort_src));
		return -1;
	}
	slog("touchpad %04x:%04x (version %04x) à l'adresse %02x : descripteur de rapport %u octets (registre %04x), "
	     "entrée %04x (%u octets), commande %04x, données %04x\n",
	     H.vid, H.pid, H.ver, C.addr, H.rd_len, H.rd_reg, H.in_reg, H.in_max, H.cmd_reg, H.data_reg);
	if (!H.rd_len || H.rd_len > sizeof rdesc) {
		slog("longueur du descripteur de rapport refusée\n");
		return -1;
	}
	u8 w[2] = {(u8)H.rd_reg, (u8)(H.rd_reg >> 8)};
	if (dw_xfer(&D, C.addr, w, 2, rdesc, H.rd_len)) {
		slog("lecture du descripteur de rapport : %s\n", abort_text(D.abort_src));
		return -1;
	}
	if (C.verbose)
		hexdump("rapport", rdesc, H.rd_len);
	hid_parse_desc(&HD, rdesc, H.rd_len);
	int k = hid_model_build(&HD, &HM);
	slog("descripteur de rapport : %d éléments%s%s ; souris %s ; pavé %s\n", HD.n, HD.err ? " (coupé)" : "",
	     HD.overflow ? " (trop d'éléments)" : "", k & 1 ? "oui" : "non", k & 2 ? "oui" : "non");
	for (int i = 0; i < HM.nmouse; i++) {
		const struct hid_mouse *ms = &HM.m[i];
		slog("  souris %d : rapport %d, molette %s, boutons %s%s%s\n", i, ms->id, ms->wheel >= 0 ? "oui" : "non",
		     ms->btn[0] >= 0 ? "1" : "", ms->btn[1] >= 0 ? " 2" : "", ms->btn[2] >= 0 ? " 3" : "");
	}
	if (k & 2)
		slog("  pavé : rapport %d, %d doigts par rapport, X %d..%d, Y %d..%d, %d,%02d x %d,%02d mm, boutons %s, "
		     "Input Mode %s\n",
		     HM.t_id, HM.nfing, HM.xmin, HM.xmax, HM.ymin, HM.ymax, HM.xmm100 / 100, HM.xmm100 % 100,
		     HM.ymm100 / 100, HM.ymm100 % 100,
		     HM.t_btn[0] < 0 ? "aucun" : HM.t_btn[1] < 0 ? "1 (le pavé entier)" : "1 à 3",
		     HM.mode_item >= 0 ? "oui" : "non");
	return k;
}

/* un réglage Feature : l'élément à v, le reste du rapport à 0 -> 0 */
static int set_feature(int item, int32_t v, int item2, int32_t v2)
{
	const struct hid_item *it = &HD.it[item];
	u32 n = hid_report_bytes(&HD, HK_FEATURE, it->id);
	u8 data[64];
	if (n > sizeof data)
		return -1;
	memset(data, 0, sizeof data);
	hid_put(it, data, n, v);
	if (item2 >= 0 && HD.it[item2].id == it->id)
		hid_put(&HD.it[item2], data, n, v2);
	return hid_cmd(3, 3, it->id, data, n); /* SET_REPORT, Feature */
}

/* ---- l'anneau de hidmanager */
static void push(void *ctx, int type, long a, long b, long c)
{
	(void)ctx;
	volatile u64 *r = ring;
	if (!r)
		return;
	u64 head = r[0], tail = r[1];
	if (head - tail >= RING_N)
		return;
	volatile u64 *e = r + (RING_EV + (head & (RING_N - 1)) * 32) / 8;
	e[0] = (u64)type;
	e[1] = (u64)a;
	e[2] = (u64)b;
	e[3] = (u64)c;
	__atomic_store_n(&r[0], head + 1, __ATOMIC_RELEASE);
}

static long ioapic_line(long gsi)
{
	static u8 madt[4096];
	long n = korin_sys4(SYS_ACPI_TABLE, "APIC", 0, madt, sizeof madt);
	if (n > (long)sizeof madt)
		n = sizeof madt;
	for (long o = 44; o + 2 <= n && madt[o + 1] >= 2 && o + madt[o + 1] <= n; o += madt[o + 1])
		if (madt[o] == 1 && madt[o + 1] >= 12) {
			long base = (long)(madt[o + 8] | madt[o + 9] << 8 | madt[o + 10] << 16 | (u32)madt[o + 11] << 24);
			return gsi >= base ? gsi - base : gsi;
		}
	return gsi;
}

static int stop_now(long chan)
{
	static u8 msg[512] __attribute__((aligned(16)));
	long req = korin_sys3(SYS_SVC_NEXT, chan, msg, 0);
	if (req <= 0)
		return 0;
	int stop = ((u64 *)msg)[0] == OP_STOP;
	((u64 *)msg)[0] = stop ? 0 : (u64)E_INVAL;
	korin_sys2(SYS_SVC_REPLY, req, msg);
	return stop;
}

static int run(void)
{
	int k = describe();
	if (k <= 0)
		return k < 0 ? 1 : (slog("ni souris ni pavé dans le descripteur de rapport\n"), 1);
	/* allumé, remis à zéro (la réponse : un rapport de longueur 0) */
	set_power(1);
	sys_sleep(2);
	if (hid_cmd(1, 0, 0, 0, 0))
		slog("RESET refusé : %s\n", abort_text(D.abort_src));
	long t = korin_sys0(SYS_TIME_MS);
	int reset_ok = 0;
	while (korin_sys0(SYS_TIME_MS) - t < 1000) {
		sys_sleep(10);
		u8 b[2];
		if (!dw_xfer(&D, C.addr, 0, 0, b, 2) && b[0] == 0 && b[1] == 0) {
			reset_ok = 1;
			break;
		}
	}
	if (!reset_ok)
		slog("pas de réponse au RESET en 1 s (on continue)\n");
	set_power(1);

	int pad = (k & 2) && C.mode != 1 && (HM.mode_item >= 0 || C.mode == 2 || !(k & 1));
	if (pad && HM.mode_item >= 0) {
		if (set_feature(HM.mode_item, 3, -1, 0)) { /* Input Mode : 3, pavé tactile (0 : souris) */
			slog("Input Mode refusé (%s) : mode souris\n", abort_text(D.abort_src));
			pad = 0;
		} else if (HM.surface_item >= 0)
			set_feature(HM.surface_item, 1, HM.button_item, 1); /* surface et bouton signalés */
	}
	if (!pad && !(k & 1)) {
		slog("pavé sans mode souris, et le mode pavé refusé : rien à faire\n");
		return 1;
	}

	long hid = korin_sys2(SYS_SVC_OPEN, "hid", 10000);
	if (hid < 0) {
		slog("hidmanager absent\n");
		return 1;
	}
	static u8 msg[512] __attribute__((aligned(16)));
	((u64 *)msg)[0] = HID_OP_PILOTE;
	long rr = korin_sys2(SYS_SVC_CALL, hid, msg);
	if (rr < 0 || (rr = korin_sys1(SYS_BUF_MAP, rr)) < 0) {
		slog("anneau de hidmanager refusé (%ld)\n", rr);
		return 1;
	}
	ring = (volatile u64 *)rr;
	long chan = korin_sys2(SYS_SVC_REGISTER, "i2chid", 0);
	/* l'extinction (init2 : SIGTERM à chacun) : retenu, lu à chaque passage, pour s'arrêter proprement (le
	   touchpad rendu en mode souris et endormi, la fonction rendue à pcid) */
	korin_sys2(SYS_SIG_MASK, SIGM_BLOCK, SIGTERM_BIT);

	struct gest_cfg gc = {C.pointer, C.tap, C.natural, C.scroll};
	gest_init(&G, &HM, &gc);
	/* les rapports qui suivent le RESET et le passage en mode pavé (rapports de transition, en mode souris ou
	   à moitié remplis) : lus et jetés pendant 300 ms, jamais envoyés (15/11 : un saut en 0,0 avec un clic
	   au lancement sur le ZBook) */
	{
		long t1 = korin_sys0(SYS_TIME_MS);
		int dropped = 0;
		while (korin_sys0(SYS_TIME_MS) - t1 < 300) {
			if (read_input() > 0)
				dropped++;
			else
				sys_sleep(10);
		}
		if (dropped)
			slog("%d rapport(s) de mise en route jeté(s)\n", dropped);
	}
	long started = korin_sys0(SYS_TIME_MS);
	int jumps_said = 0, mouse_dropped = 0;

	/* attente : interruption directe, plot GPIO, ou sondage */
	long line = -1, bound = -1;
	volatile u32 *pad_dw0 = 0;
	if (C.irq >= 0) {
		line = ioapic_line(C.irq);
		bound = korin_sys3(SYS_IRQ_BIND, line, -1, 0);
		if (bound < 0)
			slog("IRQ %ld (ligne %ld) refusée (%ld) : sondage\n", C.irq, line, bound);
	}
	if (bound < 0 && C.gpio) {
		long v = korin_sys2(SYS_DEV_MAP, C.gpio & ~0xFFFull, 4096);
		if (v >= 0)
			pad_dw0 = (volatile u32 *)(v + (long)(C.gpio & 0xFFC));
		else
			slog("plot GPIO %lx : DEV_MAP refusé (%ld) : sondage\n", (unsigned long)C.gpio, v);
	}
	/* le niveau du plot au repos (gpio_low = -1) : les rapports en attente lus, puis le bit RX */
	int idle = C.gpio_low < 0 ? -1 : (C.gpio_low ? 1 : 0);
	if (pad_dw0 && idle < 0) {
		for (int i = 0; i < 8 && read_input() > 0; i++)
			;
		sys_sleep(20);
		idle = (*pad_dw0 >> 1) & 1;
		slog("plot au repos : %d (interruption à %d)\n", idle, !idle);
	}
	char how[64];
	if (bound >= 0)
		snprintf(how, sizeof how, "IRQ %ld", C.irq);
	else if (pad_dw0)
		snprintf(how, sizeof how, "plot GPIO lu toutes les %d ms", C.poll);
	else
		snprintf(how, sizeof how, "sondage toutes les %d ms", C.poll);
	slog("%s en service (%s ; i2chid stop pour l'arrêter)\n", pad ? "pavé tactile" : "souris", how);

	int last_scan = -1, errors = 0;
	long last_read = 0;
	for (;;) {
		if (chan >= 0 && stop_now(chan))
			break;
		if (korin_sys2(SYS_SIG_MASK, SIGM_PENDING, 0) & SIGTERM_BIT) {
			slog("extinction : arrêt\n");
			break;
		}
		if (bound >= 0) {
			if (korin_sys2(SYS_IRQ_WAIT, line, 50) == E_INTR)
				break;
		} else {
			sys_sleep(C.poll);
			if (pad_dw0) {
				int level = (*pad_dw0 >> 1) & 1;
				/* (pas d'interruption en attente ; une lecture toutes les 500 ms quand même, au cas où le
				   niveau au repos aurait été mal mesuré) */
				if (level == idle && korin_sys0(SYS_TIME_MS) - last_read < 500)
					continue;
				last_read = korin_sys0(SYS_TIME_MS);
			}
		}
		for (int burst = 0; burst < 4; burst++) {
			long n = read_input();
			if (n < 0) {
				if (++errors == 10)
					slog("10 lectures en échec (%s) : le touchpad ne répond plus ?\n", abort_text(D.abort_src));
				break;
			}
			errors = 0;
			if (n == 0)
				break;
			if (C.verbose > 1)
				hexdump("entrée", inbuf + 2, (u32)n);
			struct hid_frame fr;
			if (!hid_decode(&HD, &HM, inbuf + 2, (u32)n, &fr))
				break;
			long now = korin_sys0(SYS_TIME_MS);
			if (fr.kind == 2) {
				/* sondage sans interruption : un rapport relu (même temps de balayage) n'est pas neuf */
				if (bound < 0 && !pad_dw0 && fr.scan >= 0 && fr.scan == last_scan && fr.count > 0)
					break;
				last_scan = fr.scan;
				gest_report(&G, &fr, HM.nfing, now, push, 0);
			} else if (pad && now - started < 2000) {
				mouse_dropped++; /* (mode pavé : un rapport de souris juste après le lancement, de la transition) */
			} else
				gest_mouse(&G, &fr, push, 0);
			if (G.jumps != jumps_said && jumps_said < 20) {
				slog("rapport faux ignoré (doigt téléporté ou saut de souris) : %d en tout\n", G.jumps);
				jumps_said = G.jumps;
			}
			if (mouse_dropped == 1) {
				slog("rapport de souris ignoré juste après le lancement (mode pavé)\n");
				mouse_dropped++;
			}
			if (bound < 0 && !pad_dw0)
				break; /* (sondage : un rapport par passage) */
		}
	}
	if (bound >= 0)
		korin_sys1(SYS_IRQ_UNBIND, line);
	if (pad && HM.mode_item >= 0)
		set_feature(HM.mode_item, 0, -1, 0); /* de retour en souris */
	set_power(0);
	slog("arrêté\n");
	return 0;
}

/* les adresses qui répondent à une lecture d'un octet */
static void scan_bus(void)
{
	char l[200];
	int k = 0, found = 0;
	for (u32 a = 0x08; a < 0x78; a++) {
		u8 b;
		if (dw_xfer(&D, a, 0, 0, &b, 1) == 0) {
			k += snprintf(l + k, sizeof l - k, " %02x", a);
			found++;
		}
	}
	slog("bus : %d adresse(s) répondent :%s\n", found, found ? l : " aucune");
}

static void usage(void)
{
	slog("i2chid probe | scan | info | run | stop  [-v|-vv] [clé=valeur…]   (réglages : /etc/i2chid.txt)\n");
}

int main(int argc, char **argv)
{
	slog_name("i2chid");
	int cmd = 0; /* 1 probe, 2 scan, 3 info, 4 run, 5 stop */
	const char *cmd_pci = 0; /* (la fonction donnée par hwmanager : plus forte que pci= du fichier) */
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "probe"))
			cmd = 1;
		else if (!strcmp(a, "scan"))
			cmd = 2;
		else if (!strcmp(a, "info"))
			cmd = 3;
		else if (!strcmp(a, "run"))
			cmd = 4;
		else if (!strcmp(a, "stop"))
			cmd = 5;
		else if (!strcmp(a, "-v"))
			C.verbose = 1;
		else if (!strcmp(a, "-vv"))
			C.verbose = 2;
		else if (strchr(a, ':') && strchr(a, '.') && !strchr(a, '=') && !strchr(a, '/') && strlen(a) < 20) {
			/* lancé par hwmanager (drivers.txt : pci:8086:43e8) avec la fonction : le pilote */
			snprintf(C.pci, sizeof C.pci, "%s", a);
			cmd_pci = a;
			if (!cmd)
				cmd = 4;
		} else if (!strchr(a, '=')) {
			usage();
			return 1;
		}
	}
	if (!cmd) {
		usage();
		return 1;
	}
	if (cmd == 5) {
		long s = korin_sys2(SYS_SVC_OPEN, "i2chid", 0);
		if (s < 0) {
			slog("i2chid ne tourne pas\n");
			return 1;
		}
		static u8 msg[512] __attribute__((aligned(16)));
		((u64 *)msg)[0] = OP_STOP;
		korin_sys2(SYS_SVC_CALL, s, msg);
		slog("arrêt demandé\n");
		return 0;
	}
	if (cmd == 4 && korin_sys2(SYS_SVC_OPEN, "i2chid", 0) >= 0) {
		slog("i2chid tourne déjà (i2chid stop d'abord)\n");
		return 1;
	}
	if (!sysfront_rights())
		return sysfront_run("/sys/svc/i2chid", argc, argv, cmd == 4 ? " en service (" : 0);

	/* les réglages : le fichier, puis ceux de la ligne de commande */
	read_config();
	for (int i = 1; i < argc; i++)
		if (strchr(argv[i], '=') && !(cmd_pci && argv[i] == cmd_pci)) {
			char b[96];
			snprintf(b, sizeof b, "%s", argv[i]);
			if (set_line(b)) {
				slog("réglage refusé : %s\n", argv[i]);
				return 1;
			}
		}
	if (cmd_pci)
		snprintf(C.pci, sizeof C.pci, "%s", cmd_pci);
	D.verbose = C.verbose;
	int f = find_function();
	if (f <= 0)
		return f < 0;
	int rc = 1;
	if (setup_function())
		goto out;
	firmware_values();
	if (lpss_init(&D))
		goto out;
	D.clk_khz = C.clock_mhz * 1000;
	D.khz = C.speed;
	D.hcnt = C.hcnt;
	D.lcnt = C.lcnt;
	D.sda_hold_ns = C.sda_hold;
	D.sda_hold_cycles = C.sda_cycles;
	if (dw_init(&D))
		goto out;
	if (cmd == 1) {
		rc = 0;
		goto out;
	}
	if (cmd == 2) {
		scan_bus();
		rc = 0;
		goto out;
	}
	if (cmd == 3) {
		rc = describe() > 0 ? 0 : 1;
		goto out;
	}
	rc = run();
out:
	if (D.regs)
		dw_disable(&D);
	if (claimed)
		pci_release(pinfo.addr);
	return rc;
}
