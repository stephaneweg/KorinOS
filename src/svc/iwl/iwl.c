/* iwl - le Wi-Fi Intel AX201 du PC (ZBook : 0000:00:14.3, 8086:43f0), étape 1 : lancé à la main seulement
 * (ni hwmanager, ni drivers.txt). docs/etude/WIFI-PC.md, docs/etude/PC-PILOTES.md « Wi-Fi AX201, étape 1 ».
 *
 *	iwl fwinfo [FICHIER]      lit et décrit le micrologiciel (aucun matériel touché)
 *	iwl probe                 la fonction PCIe, ses registres d'identité, le fichier qu'il lui faut
 *	iwl info                  micrologiciel lancé, NVM, adresse MAC, bandes, régulation, puis arrêt
 *	iwl scan [passif] [ssid=NOM]   et un balayage : la liste des réseaux vus, puis arrêt
 *	  -v    plus de détails (-vv : chaque balise)
 *	  -p    sondage seulement (pas de MSI)
 *	  SSSS:BB:DD.F   une autre fonction que la première AX201 trouvée
 *
 * Tout va au journal du noyau (« iwl : … ») et au Terminal : lancé de SD0:/bin/iwl (sans droits), le
 * programme lance SD0:/sys/svc/iwl (pilote, avec droits) et recopie ses lignes au Terminal.
 * À la fin, et en cas d'erreur : la puce est arrêtée (DMA coupé, remise à zéro) avant que sa mémoire ne
 * soit rendue, la fonction rendue à pcid. Pour le PC seulement (svc.conf : arch = x86_64). */
#include "iwl.h"
#include <stdarg.h>

struct iwl W;

/* les CNVi de la famille 22000 (Qu, QuZ) avec un module Hr ou Jf : le ZBook a 43f0 (Tiger Lake-H) */
static const u16 qu_ids[] = {0x43F0, 0xA0F0, 0x02F0, 0x06F0, 0x34F0, 0x3DF0, 0x4DF0};

/* ---- journal : SYS_PRINT (console du noyau, journal), et la sortie standard au bureau ou dans un tube */
static int to_stdout = -1;
static u32 log_lock;

static void stdout_setup(void)
{
	long fl = korin_sys0(SYS_PROC_FLAGS);
	int redirected = fl >= 0 && (fl & 2);
	int desktop = korin_sys1(SYS_WS_SIGNAL, 0) != -19;
	to_stdout = redirected || desktop;
}

static void vlog(const char *fmt, va_list ap)
{
	char b[480];
	int n = snprintf(b, sizeof b, "iwl : ");
	n += vsnprintf(b + n, sizeof b - n, fmt, ap);
	if (n > (int)sizeof b - 1)
		n = sizeof b - 1;
	if (to_stdout < 0)
		stdout_setup();
	mtx_lock(&log_lock);
	korin_sys2(SYS_PRINT, b, n);
	if (to_stdout)
		sys_write(1, b, n);
	mtx_unlock(&log_lock);
}

void ilog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}

void idbg(const char *fmt, ...)
{
	if (!W.verbose)
		return;
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}

long now_ms(void)
{
	return korin_sys0(SYS_TIME_MS);
}

void msleep(long ms)
{
	korin_sys1(SYS_SLEEP, ms);
}

static u64 dma_total;

long dma_alloc(u32 len, struct dma_blk *b)
{
	u64 pa = 0;
	long va = korin_sys2(SYS_DMA_ALLOC, (len + 4095) & ~4095u, &pa);
	if (va < 0) {
		ilog("ÉCHEC : mémoire DMA refusée (%u octets : %ld), %lu Ko déjà pris\n", len, va,
		     (unsigned long)(dma_total >> 10));
		return va;
	}
	b->va = (u8 *)va;
	b->pa = pa;
	b->len = len;
	dma_total += (len + 4095) & ~4095u;
	return 0;
}

/* ---- la commande sans droits (SD0:/bin/iwl) : lance SD0:/sys/svc/iwl et recopie ses lignes du journal */
#define KLOG_MAX 65536
static char klog[KLOG_MAX];

static long klog_len(void)
{
	long n = korin_sys2(SYS_KLOG, klog, KLOG_MAX);
	return n < 0 ? 0 : n;
}

static void klog_show(long *from)
{
	long n = klog_len();
	if (n < *from)
		*from = 0;
	long i = *from;
	while (i < n) {
		long e = i;
		while (e < n && klog[e] != '\n')
			e++;
		if (e == n)
			break;
		char *l = klog + i;
		long len = e - i + 1;
		char *p = memchr(l, ']', len < 16 ? len : 16);
		if (p && p[1] == ' ') {
			len -= p + 2 - l;
			l = p + 2;
		}
		static char last[480];
		static long last_len;
		if (len > 6 && !memcmp(l, "iwl : ", 6) && !(len == last_len && !memcmp(l, last, len))) {
			sys_write(1, l, len);
			if (len <= (long)sizeof last) {
				memcpy(last, l, len);
				last_len = len;
			}
		}
		i = e + 1;
	}
	*from = i;
}

static int front(int argc, char **argv)
{
	char args[256];
	int n = 0;
	args[0] = 0;
	for (int i = 1; i < argc; i++)
		n += snprintf(args + n, sizeof args - n, "%s%s", i > 1 ? " " : "", argv[i]);
	long from = klog_len();
	long pid = f_spawn("SD0:/sys/svc/iwl", args);
	if (pid < 0) {
		ilog("lancement de SD0:/sys/svc/iwl refusé (%ld)\n", pid);
		return 1;
	}
	for (;;) {
		u64 e[8];
		int alive = korin_sys2(SYS_PROC_INFO, pid, e) == 0 && e[7] == 0;
		klog_show(&from);
		if (!alive)
			break;
		msleep(200);
	}
	msleep(100);
	klog_show(&from);
	return 0;
}

/* ---- le fichier du micrologiciel */
static int load_file(const char *path)
{
	long st[2];
	if (sys_stat(path, st) < 0)
		return -1;
	long fd = sys_open(path, O_READ);
	if (fd < 0)
		return -1;
	u32 size = (u32)st[0];
	u8 *d = sys_vm_map(size + 4096);
	if (!d) {
		sys_close(fd);
		return -2;
	}
	u32 got = 0;
	while (got < size) {
		long r = sys_read(fd, d + got, size - got);
		if (r <= 0)
			break;
		got += (u32)r;
	}
	sys_close(fd);
	if (got != size)
		return -3;
	W.fw_file = d;
	W.fw_len = size;
	snprintf(W.fw_path, sizeof W.fw_path, "%s", path);
	return 0;
}

static void fw_describe(void)
{
	const struct fw_image *fw = &W.fw;
	ilog("micrologiciel %s : %s, version %u.%08x.%u, %u octets\n", W.fw_path, fw->human, fw->ver[0], fw->ver[1],
	     fw->ver[2], W.fw_len);
	ilog("  sections : LMAC %d, UMAC %d, pages %d (%u octets à donner à la puce), image %s, %u TLV\n", fw->n_lmac,
	     fw->n_umac, fw->n_paging, fw_total_bytes(fw), fw->n_init ? "INIT + RT" : "unifiée", fw->n_tlv);
	ilog("  PHY_SKU %08x (antennes émission %x, réception %x), %u canaux de balayage, %u stations\n", fw->phy_config,
	     (fw->phy_config >> 16) & 15, (fw->phy_config >> 20) & 15, fw->n_scan_channels, fw->num_stations);
	ilog("  capacités : LAR %d, balayage UMAC %d, latence SoC %d, arrêt en poignée de main %d, MCC 11ax %d\n",
	     fw_capa(fw, CAPA_LAR_SUPPORT), fw_capa(fw, CAPA_UMAC_SCAN), fw_capa(fw, CAPA_SOC_LATENCY),
	     fw_capa(fw, CAPA_FW_RESET_HANDSHAKE), fw_capa(fw, CAPA_MCC_UPDATE_11AX));
	ilog("  versions : balayage %d, config. %d, vivant %d, NVM %d, MCC %d/%d, SoC %d, BT %d\n",
	     fw_cmd_ver(fw, GRP_LONG, C_SCAN_REQ_UMAC, 0), fw_cmd_ver(fw, GRP_LONG, C_SCAN_CFG, 0),
	     fw_notif_ver(fw, GRP_LEGACY, C_ALIVE, 0), fw_notif_ver(fw, GRP_REGULATORY, C_REG_NVM_GET_INFO, 0),
	     fw_cmd_ver(fw, GRP_LONG, C_MCC_UPDATE, 0), fw_notif_ver(fw, GRP_LONG, C_MCC_UPDATE, 0),
	     fw_cmd_ver(fw, GRP_SYSTEM, C_SYS_SOC_CONFIG, 0), fw_cmd_ver(fw, GRP_LONG, C_BT_CONFIG, 0));
}

static int fw_open(const char *name)
{
	char path[96];
	snprintf(path, sizeof path, "SD0:/lib/firmware/%s-77.ucode", name);
	int r = load_file(path);
	if (r) {
		ilog("micrologiciel absent : %s (%s)\n", path, r == -1 ? "pas de fichier" : "lecture incomplète");
		return -1;
	}
	r = fw_parse(&W.fw, W.fw_file, W.fw_len);
	if (r) {
		ilog("ÉCHEC : %s illisible : %s\n", path, fw_err(r));
		return -1;
	}
	return 0;
}

/* ---- la fonction PCIe */
static int is_qu(u16 dev)
{
	for (unsigned i = 0; i < sizeof qu_ids / sizeof qu_ids[0]; i++)
		if (qu_ids[i] == dev)
			return 1;
	return 0;
}

static int find_device(const char *addr_arg)
{
	long r = pci_open(5000);
	if (r < 0) {
		ilog("service pci absent (%ld) : pcid n'est pas lancé\n", r);
		return -1;
	}
	if (addr_arg) {
		long a = pci_parse_addr(addr_arg);
		if (a < 0) {
			ilog("adresse PCI illisible : %s\n", addr_arg);
			return -1;
		}
		W.pci.addr = (u32)a;
		return 1;
	}
	long from = 0;
	struct pci_info f;
	int others = 0;
	while (pci_find_class(0x020000, 0xFF0000, &from, &f) == 0) { /* classe 02 : réseau */
		char a[16];
		pci_format_addr(f.addr, a);
		if (f.vendor == 0x8086 && is_qu(f.device)) {
			W.pci = f;
			if (f.device != 0x43F0)
				ilog("ATTENTION : %04x:%04x à %s n'est pas l'AX201 du ZBook (8086:43f0) ; même famille, pas essayé\n",
				     f.vendor, f.device, a);
			return 1;
		}
		ilog("fonction réseau %s : %04x:%04x classe %02x/%02x (pas un Wi-Fi Intel 22000)\n", a, f.vendor, f.device,
		     f.cls, f.sub);
		others++;
	}
	ilog("pas d'AX201 : aucune fonction 8086:43f0 (ni autre CNVi Wi-Fi Intel « Qu ») parmi %d fonction(s) réseau "
	     "PCIe ; rien n'est touché\n",
	     others);
	return 0;
}

static void msi_teardown(void);

static int claim(void)
{
	long r = pci_claim(W.pci.addr, &W.pci);
	pci_format_addr(W.pci.addr, W.where);
	if (r == E_BUSY) {
		ilog("%s est déjà tenue par un autre pilote (iwl déjà lancé ?)\n", W.where);
		return -1;
	}
	if (r < 0) {
		ilog("%s refusée par pcid (%ld)\n", W.where, r);
		return -1;
	}
	W.claimed = 1;
	ilog("%s : %04x:%04x rév. %02x, sous-système %04x:%04x, classe %02x/%02x, BAR0 %lx (%lu Ko)%s%s\n", W.where,
	     W.pci.vendor, W.pci.device, W.pci.rev, W.pci.svid, W.pci.ssid, W.pci.cls, W.pci.sub,
	     (unsigned long)W.pci.bar[0], (unsigned long)(W.pci.bar_size[0] >> 10),
	     W.pci.cap_msix ? ", MSI-X" : "", W.pci.cap_msi ? ", MSI" : "");
	if (!W.pci.bar[0] || (W.pci.bar_flags[0] & PCI_BAR_IO)) {
		ilog("ANOMALIE : pas de BAR0 en mémoire\n");
		return -1;
	}
	if (W.pci.cap_pm) {
		long pm = pci_cfg_read(W.pci.addr, W.pci.cap_pm + 4u, 2);
		if (pm >= 0 && (pm & 3)) {
			ilog("fonction en D%ld : passage en D0\n", pm & 3);
			pci_cfg_write(W.pci.addr, W.pci.cap_pm + 4u, 2, (u32)pm & ~3u);
			msleep(10);
		}
	}
	pci_cfg_write(W.pci.addr, 0x41, 1, 0); /* RETRY_TIMEOUT : 0 (comme iwlwifi : pas de réessais PCI en C3) */
	long regs = pci_map_bar(&W.pci, 0);
	if (regs < 0) {
		ilog("BAR0 non mappée (%ld)\n", regs);
		return -1;
	}
	W.mmio = (volatile u8 *)regs;
	W.hw_rev = rd32(CSR_HW_REV);
	if (W.hw_rev == 0xFFFFFFFF) {
		ilog("ANOMALIE : CSR_HW_REV = FFFFFFFF : la puce ne répond pas sur le bus\n");
		W.mmio = 0;
		return -1;
	}
	return 0;
}

static int identity(void)
{
	/* d'abord l'accès aux registres internes, comme la sonde d'iwlwifi (sans remise à zéro), puis la mise en
	 * route complète, essayée étape par étape (« accès aux registres internes … : OK / ÉCHEC ») */
	udelay_calibrate();
	if (trans_probe_access())
		ilog("ÉCHEC : les registres internes ne se lisent pas (valeurs A5A5A5Ax) dès la sonde\n");
	if (trans_start_hw()) {
		dump_regs("mise en route");
		return -1;
	}
	W.rf_id = rd32(CSR_HW_RF_ID);
	ilog("identité : HW_REV %08x (MAC type %03x, étape %x), RF_ID %08x (RF type %03x, étape %x%s), CNVI %08x, "
	     "radio %s\n",
	     W.hw_rev, (W.hw_rev >> 4) & 0xFFF, W.hw_rev & 0xF, W.rf_id, (W.rf_id >> 12) & 0xFFF, (W.rf_id >> 8) & 0xF,
	     (W.rf_id >> 28) & 1 ? ", CDB" : "", W.cnv_id,
	     (rd32(CSR_GP_CNTRL) & GP_HW_RF_KILL_SW) ? "permise" : "COUPÉE (rfkill)");
	return 0;
}

static const char *choose_fw(void)
{
	const char *n = fw_name_for(W.hw_rev, W.rf_id);
	if (!n && W.pci.device == 0x43F0 && ((W.rf_id >> 12) & 0xFFF) == 0) {
		ilog("RF_ID vide : AX201 supposé (Hr2)\n");
		n = fw_name_for(W.hw_rev, 0x10A000);
	}
	if (!n)
		ilog("ANOMALIE : MAC %03x / RF %03x : pas une puce Qu/QuZ avec Hr ou Jf connue\n", (W.hw_rev >> 4) & 0xFFF,
		     (W.rf_id >> 12) & 0xFFF);
	else
		ilog("micrologiciel attendu : %s-77.ucode (%s)\n", n,
		     !strcmp(n, "iwlwifi-QuZ-a0-hr-b0") ? "dans l'arbre du PC" : "à ajouter : tools/pc/iwlfw.sh");
	return n;
}

/* ---- MSI (un vecteur ; les causes se lisent dans CSR_INT, comme sans interruption) */
static long msi_line = -1;

static void msi_setup(void)
{
	W.irq = -1;
	if (W.poll || !W.pci.cap_msi) {
		ilog("interruptions : sondage (%s)\n", W.poll ? "demandé" : "pas de capacité MSI");
		return;
	}
	u64 ad[2];
	long line = korin_sys2(SYS_IRQ_MSI, 1, ad);
	if (line < 0) {
		ilog("interruptions : MSI refusée par le noyau (%ld) : sondage\n", line);
		return;
	}
	u32 a = W.pci.addr, p = W.pci.cap_msi;
	if (W.pci.cap_msix) { /* MSI-X coupé (il ne doit pas être allumé avec MSI) */
		long c = pci_cfg_read(a, W.pci.cap_msix + 2u, 2);
		if (c >= 0 && (c & 0x8000))
			pci_cfg_write(a, W.pci.cap_msix + 2u, 2, (u32)c & ~0x8000u);
	}
	pci_cfg_write(a, p + 4, 4, (u32)ad[0]);
	if (W.pci.msi_64) {
		pci_cfg_write(a, p + 8, 4, (u32)(ad[0] >> 32));
		pci_cfg_write(a, p + 12, 2, (u32)ad[1]);
	} else
		pci_cfg_write(a, p + 8, 2, (u32)ad[1]);
	long ctl = pci_cfg_read(a, p + 2, 2);
	pci_cfg_write(a, p + 2, 2, ((u32)ctl & ~0x70u) | 1);
	long r = korin_sys3(SYS_IRQ_BIND, line, -1, 0);
	if (r < 0) {
		ilog("interruptions : IRQ_BIND refusé (%ld) : sondage\n", r);
		pci_cfg_write(a, p + 2, 2, (u32)ctl & ~1u);
		return;
	}
	msi_line = line;
	W.irq = line;
	ilog("interruptions : MSI, ligne %ld\n", line);
}

static void msi_teardown(void)
{
	if (msi_line < 0)
		return;
	long ctl = pci_cfg_read(W.pci.addr, W.pci.cap_msi + 2u, 2);
	if (ctl >= 0)
		pci_cfg_write(W.pci.addr, W.pci.cap_msi + 2u, 2, (u32)ctl & ~1u);
	korin_sys1(SYS_IRQ_UNBIND, msi_line);
	msi_line = -1;
	W.irq = -1;
}

static void finish(int fw_running)
{
	if (W.mmio)
		trans_stop(fw_running);
	msi_teardown();
	if (W.claimed)
		pci_release(W.pci.addr);
	W.claimed = 0;
}

static void usage(void)
{
	ilog("iwl fwinfo [FICHIER]   le micrologiciel, sans matériel\n");
	ilog("iwl probe              la fonction PCIe et son identité\n");
	ilog("iwl info [-v] [-p]     micrologiciel lancé, NVM, MAC, régulation, puis arrêt\n");
	ilog("iwl scan [passif] [ssid=NOM] [-v|-vv] [-p]   et un balayage (liste des réseaux), puis arrêt\n");
}

int main(int argc, char **argv)
{
	int mode = 0, passive = 0;
	const char *ssid = 0, *addr = 0, *file = 0;
	W.waiting_seq = -1;
	W.irq = -1;
	for (int i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (!strcmp(a, "fwinfo"))
			mode = 1;
		else if (!strcmp(a, "probe"))
			mode = 2;
		else if (!strcmp(a, "info"))
			mode = 3;
		else if (!strcmp(a, "scan"))
			mode = 4;
		else if (!strcmp(a, "passif") || !strcmp(a, "passive"))
			passive = 1;
		else if (!strncmp(a, "ssid=", 5))
			ssid = a + 5;
		else if (!strcmp(a, "-v"))
			W.verbose = 1;
		else if (!strcmp(a, "-vv"))
			W.verbose = 2;
		else if (!strcmp(a, "-p"))
			W.poll = 1;
		else if (strchr(a, ':') && strchr(a, '.') && !strchr(a, '/'))
			addr = a;
		else if (mode == 1)
			file = a;
		else {
			usage();
			return 1;
		}
	}
	if (!mode) {
		usage();
		return 1;
	}
	long pf = korin_sys0(SYS_PROC_FLAGS);
	if (pf >= 0 && !(pf & 0x100) && mode >= 2) /* sans droits (SD0:/bin/iwl) */
		return front(argc, argv);

	if (mode == 1) {
		int r = file ? load_file(file) : load_file("SD0:/lib/firmware/iwlwifi-QuZ-a0-hr-b0-77.ucode");
		if (r) {
			ilog("micrologiciel illisible : %s\n", file ? file : "SD0:/lib/firmware/iwlwifi-QuZ-a0-hr-b0-77.ucode");
			return 1;
		}
		r = fw_parse(&W.fw, W.fw_file, W.fw_len);
		if (r) {
			ilog("ÉCHEC : %s\n", fw_err(r));
			return 1;
		}
		fw_describe();
		return 0;
	}

	int f = find_device(addr);
	if (f <= 0)
		return f < 0 ? 1 : 0;
	int rc = 1;
	if (claim())
		goto out;
	if (identity())
		goto out;
	const char *name = choose_fw();
	if (mode == 2) {
		rc = 0;
		goto out;
	}
	if (!name || fw_open(name))
		goto out;
	fw_describe();
	msi_setup();
	if (mvm_up())
		goto out;
	if (mode == 4) {
		if (mvm_scan(passive, ssid, 15000) == 0) {
			mvm_print_scan();
			rc = 0;
		}
	} else
		rc = 0;
out:
	finish(W.got_alive && !W.dead);
	ilog("%s (%s), %lu Ko de mémoire DMA rendus\n", mode == 4 ? "balayage fini" : mode == 3 ? "info finie" : "sonde finie",
	     rc ? "anomalie, voir plus haut" : "rien d'anormal", (unsigned long)(dma_total >> 10));
	return rc;
}
