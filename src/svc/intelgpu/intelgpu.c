/* intelgpu - le GPU Intel du PC (Tiger Lake UHD, Gen12 LP), étape 1 : une
 * SONDE lancée à la main seulement (ni hwmanager, ni drivers.txt) :
 *
 *	SD0:/sys/svc/intelgpu probe            identité, fusibles, moteur de rendu (lecture)
 *	SD0:/sys/svc/intelgpu probe submit     et un lot minimal soumis par execlists
 *	  -v      chaque écriture de registre au journal
 *	  simu    un faux GPU en mémoire (essai des chemins sous QEMU, rien n'est touché)
 *	  simu-hang  idem, le faux GPU ne répond pas (chemin du délai et de la remise à zéro)
 *
 * Ce qu'elle écrit : le registre de commande PCI (prise par pcid, rendu tel
 * qu'il était), le forcewake (pris puis rendu) ; avec submit : quelques
 * entrées de la GGTT hors de celles du micrologiciel (rendues ensuite), les
 * registres du moteur de rendu (rendus ensuite). JAMAIS les registres de
 * l'affichage (pipes, plans, DDI, PLL, panneau, rétroéclairage) ni la mémoire
 * du framebuffer GOP. Une anomalie : arrêt et journal. Voir
 * docs/etude/GPU-INTEL.md. Pour le PC seulement (svc.conf : arch = x86_64). */
#include "igpu.h"

struct gpu G;

/* Tiger Lake (Gen12 LP) : les seuls que cette sonde touche au-delà de la configuration PCI */
static const u16 tgl_ids[] = {0x9A40, 0x9A49, 0x9A59, 0x9A60, 0x9A68, 0x9A70, 0x9A78,
			      0x9AC0, 0x9AC9, 0x9AD9, 0x9AF8};

/* ---- journal : SYS_PRINT du noyau (console du noyau et journal : « journal » l'écrit dans
 * SD0:/journal.txt). Au bureau, la console du noyau ne se voit pas : la même ligne va aussi à la sortie
 * standard (le Terminal) quand elle n'est pas la console. En console texte, la console du noyau est
 * l'écran : une seule fois. */

static int to_stdout = -1;

static void stdout_setup(void)
{
	long fl = korin_sys0(SYS_PROC_FLAGS);
	int redirected = fl >= 0 && (fl & 2);
	/* un serveur graphique ? (SYS_WS_SIGNAL sans bit : rien d'autre ; E_NODEV sans serveur) */
	int desktop = korin_sys1(SYS_WS_SIGNAL, 0) != -19;
	to_stdout = redirected && desktop;
}

static u32 log_lock;

static void vlog(const char *fmt, va_list ap)
{
	char b[400];
	int n = snprintf(b, sizeof b, G.simu ? "intelgpu (simu) : " : "intelgpu : ");
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

/* du texte tel quel, où va le journal (status) */
static void out_text(const char *t)
{
	if (to_stdout < 0)
		stdout_setup();
	long fl = korin_sys0(SYS_PROC_FLAGS);
	if (fl >= 0 && (fl & 2))
		sys_write(1, t, strlen(t));
	else
		korin_sys2(SYS_PRINT, t, strlen(t));
}

void glog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vlog(fmt, ap);
	va_end(ap);
}

void gdbg(const char *fmt, ...)
{
	if (!G.verbose)
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

/* ---- registres */

u32 rd(u32 off)
{
	if (G.simu && off == RCS + R_TIMESTAMP)
		return (u32)(now_ms() * 19200); /* 19,2 MHz */
	return *(volatile u32 *)(G.mmio + off);
}

void wr(u32 off, u32 v)
{
	gdbg("écrit %06x <- %08x\n", off, v);
	if (G.simu)
		simu_write(off, v);
	else
		*(volatile u32 *)(G.mmio + off) = v;
}

u64 gtt_rd(u64 i)
{
	return G.ggtt[i];
}

void gtt_wr(u64 i, u64 v)
{
	gdbg("écrit GGTT[%lx] <- %016lx\n", (unsigned long)i, (unsigned long)v);
	G.ggtt[i] = v;
}

void clflush_range(volatile void *p, u64 n)
{
	uintptr_t a = (uintptr_t)p & ~(uintptr_t)63, e = (uintptr_t)p + n;
	__asm__ volatile("mfence" ::: "memory");
	for (; a < e; a += 64)
		__asm__ volatile("clflush (%0)" ::"r"(a) : "memory");
	__asm__ volatile("mfence" ::: "memory");
}

/* attend (reg & mask) == want, ms au plus -> 0, -1 au délai ; last : la dernière valeur lue */
int wait_bits(u32 off, u32 mask, u32 want, long ms, u32 *last)
{
	long end = now_ms() + ms;
	u32 v;
	for (;;) {
		v = rd(off);
		if ((v & mask) == want)
			break;
		if (now_ms() > end) {
			if (last)
				*last = v;
			return -1;
		}
		__asm__ volatile("pause");
	}
	if (last)
		*last = v;
	return 0;
}

/* ---- configuration PCI (le faux GPU en a une aussi) */

u32 cfg_read(unsigned off, unsigned size)
{
	if (G.simu) {
		switch (off) {
		case CFG_CMD: return 0x0007;
		case CFG_GMCH_CTRL: return 0x02C0; /* GGTT 8 Mo, 64 Mo volés */
		case CFG_BDSM: return 0x7C000001;
		case CFG_BDSM_LO: return 0x7C000001;
		case CFG_BDSM_HI: return 0;
		case CFG_ASLS: return 0x7A000000;
		}
		return 0;
	}
	long v = pci_cfg_read(G.pci.addr, off, size);
	return v < 0 ? 0xFFFFFFFFu : (u32)v;
}

/* ---- forcewake */

/* Gen11+ : la demande (FW_GT 0xA188, FW_RENDER 0xA278, écritures masquées) et son accusé (FW_GT_ACK
 * 0x130044, FW_RENDER_ACK 0x0D84) : les adresses du PRM de Gen12, celles qu'emploie i915 sur Tiger Lake.
 * L'accusé suit la demande du pilote : 0 avant la prise (personne ne tient le GT), bit 0 tant qu'on le tient,
 * 0 après qu'on l'a rendu. Le journal de l'étape 1 ne montrait que l'avant et l'après (deux 0 : attendus ; la
 * prise elle-même avait bien été accusée, sinon « ANOMALIE ») : on montre aussi le pendant. Comme i915, on
 * attend que l'accusé soit retombé avant de demander (une remise juste avant peut ne pas être finie). */
static int fw_one(u32 req, u32 ack, const char *what)
{
	u32 v;
	if (wait_bits(ack, FW_KERNEL, 0, 50, &v))
		glog("forcewake %s : l'accusé ne retombe pas avant la prise (%08x), demande quand même\n", what, v);
	wr(req, MASKED_ON(FW_KERNEL));
	if (wait_bits(ack, FW_KERNEL, FW_KERNEL, 50, &v)) {
		glog("ANOMALIE : forcewake %s sans accusé en 50 ms (%08x)\n", what, v);
		wr(req, MASKED_OFF(FW_KERNEL));
		return -1;
	}
	return 0;
}

static int fw_get(void)
{
	glog("forcewake : accusés avant GT %08x, rendu %08x (0 attendu : personne ne le tient)\n", rd(FW_GT_ACK),
	     rd(FW_RENDER_ACK));
	if (fw_one(FW_GT, FW_GT_ACK, "GT"))
		return -1;
	G.fw_held = 1;
	if (fw_one(FW_RENDER, FW_RENDER_ACK, "du rendu"))
		return -1;
	G.fw_held = 2;
	glog("forcewake pris (GT, rendu) : accusés GT %08x, rendu %08x (bit 0 attendu)\n", rd(FW_GT_ACK),
	     rd(FW_RENDER_ACK));
	return 0;
}

static void fw_put(void)
{
	if (G.fw_held >= 2)
		wr(FW_RENDER, MASKED_OFF(FW_KERNEL));
	if (G.fw_held >= 1)
		wr(FW_GT, MASKED_OFF(FW_KERNEL));
	if (G.fw_held) {
		u32 a = 0, b = 0;
		wait_bits(FW_GT_ACK, FW_KERNEL, 0, 50, &a);
		wait_bits(FW_RENDER_ACK, FW_KERNEL, 0, 50, &b);
		glog("forcewake rendu (accusés GT %08x, rendu %08x : 0 attendu)\n", a, b);
	}
	G.fw_held = 0;
}

/* ---- identité */

static int popcount8(u32 v)
{
	int n = 0;
	for (v &= 0xFF; v; v &= v - 1)
		n++;
	return n;
}

static void identity_cfg(void)
{
	struct pci_info *p = &G.pci;
	glog("%s %04x:%04x rév. %02x, sous-système %04x:%04x\n", G.where, p->vendor, p->device, p->rev,
	     p->svid, p->ssid);
	glog("BAR0 %lx (registres et GGTT, 16 Mo), BAR2 %lx (ouverture), BAR4 %s %lx ; %s%s\n",
	     (unsigned long)p->bar[0], (unsigned long)p->bar[2], (p->bar_flags[4] & PCI_BAR_IO) ? "E/S" : "",
	     (unsigned long)p->bar[4], p->cap_msi ? "MSI" : "pas de MSI", p->msi_64 ? " 64 bits" : "");
	u32 gmch = cfg_read(CFG_GMCH_CTRL, 2) & 0xFFFF;
	u32 ggms = (gmch >> 6) & 3, gms = (gmch >> 8) & 0xFF;
	u64 ggtt_mb = ggms ? 1ull << ggms : 0;
	G.ggtt_entries = ggtt_mb * (1ull << 20) / 8;
	if (G.ggtt_entries > (BAR0_SIZE - GGTT_OFF) / 8)
		G.ggtt_entries = (BAR0_SIZE - GGTT_OFF) / 8;
	G.stolen_size = gms < 0xF0 ? (u64)gms * (32ull << 20) : (u64)(gms - 0xF0 + 1) * (4ull << 20);
	u32 bdsm32 = cfg_read(CFG_BDSM, 4);
	u64 bdsm64 = (cfg_read(CFG_BDSM_LO, 4) & ~0xFFFFFu) | (u64)cfg_read(CFG_BDSM_HI, 4) << 32;
	G.stolen_base = bdsm64 ? bdsm64 : (bdsm32 & ~0xFFFFFu);
	glog("GMCH_CTRL %04x : GGTT de %lu Mo (%lu entrées, %lu Mo d'adresses), mémoire volée %lu Mo\n", gmch,
	     (unsigned long)ggtt_mb, (unsigned long)G.ggtt_entries, (unsigned long)(G.ggtt_entries * 4096 >> 20),
	     (unsigned long)(G.stolen_size >> 20));
	glog("BDSM %08x (64 bits : %lx), base de la mémoire volée %lx ; OpRegion (ASLS) %08x\n", bdsm32,
	     (unsigned long)bdsm64, (unsigned long)G.stolen_base, cfg_read(CFG_ASLS, 4));
	glog("commande PCI %04x avant la prise%s%s\n", G.cmd_before, (G.cmd_before & 2) ? ", mémoire" : "",
	     (G.cmd_before & 4) ? ", maître du bus" : "");
}

static void identity_mmio(void)
{
	u32 s = rd(SLICE_ENABLE), dss = rd(DSS_ENABLE), eud = rd(EU_DISABLE), vd = rd(VDBOX_VEBOX_DISABLE);
	int ndss = 0;
	for (u32 v = dss; v; v &= v - 1)
		ndss++;
	int eu = ndss * (8 - popcount8(eud)) * 2; /* Gen12 : un bit = une paire d'EU */
	glog("fusibles : tranches %02x, DSS %08x (%d), EU coupées %02x : %d EU ; VDBOX/VEBOX coupés %08x\n",
	     s & 0xFF, dss, ndss, eud & 0xFF, eu, vd);
	u32 c0 = rd(RPM_CONFIG0), ctc = rd(CTC_MODE);
	static const u32 xtal_khz[4] = {24000, 19200, 38400, 25000};
	u32 x = xtal_khz[(c0 >> 3) & 3], shift = (c0 >> 1) & 3;
	glog("RPM_CONFIG0 %08x (cristal %u kHz, décalage %u), CTC_MODE %08x : horodatage %u kHz%s\n", c0, x, shift,
	     ctc, x >> (3 - shift), (ctc & 1) ? " (source « divide logic » : à lire autrement)" : "");
	u32 t0 = rd(RCS + R_TIMESTAMP);
	korin_sys1(SYS_SLEEP, 20);
	u32 t1 = rd(RCS + R_TIMESTAMP);
	glog("horodatage du rendu : %u tops en ~20 ms (%u kHz mesurés)\n", t1 - t0, (t1 - t0) / 20);
	glog("état RC (GT_CORE_STATUS) %08x, GuC %08x\n", rd(GT_CORE_STATUS), rd(GUC_STATUS));
	char b[160];
	int n = 0;
	for (int i = 0; i < 8; i++)
		n += snprintf(b + n, sizeof b - n, " %x", rd(PAT_INDEX(i)));
	glog("PAT du GPU :%s\n", b);
	/* affichage : lecture seulement */
	glog("plan 1 du pipe A (lecture) : PLANE_CTL %08x, PLANE_SURF %08x (adresse dans la GGTT)\n", rd(PLANE_CTL_1_A),
	     rd(PLANE_SURF_1_A));
}

void engine_read(struct engine_regs *e)
{
	e->mode = rd(RCS + R_MODE);
	e->mi_mode = rd(RCS + R_MI_MODE);
	e->head = rd(RCS + R_HEAD);
	e->tail = rd(RCS + R_TAIL);
	e->start = rd(RCS + R_START);
	e->ctl = rd(RCS + R_CTL);
	e->acthd = rd(RCS + R_ACTHD);
	e->hws = rd(RCS + R_HWS_PGA);
	e->hwstam = rd(RCS + R_HWSTAM);
	e->imr = rd(RCS + R_IMR);
	e->el_lo = rd(RCS + R_EL_STATUS_LO);
	e->el_hi = rd(RCS + R_EL_STATUS_HI);
	e->csb = rd(RCS + R_CSB_PTR);
	e->instdone = rd(RCS + R_INSTDONE);
	e->eir = rd(RCS + R_EIR);
	e->esr = rd(RCS + R_ESR);
	e->ipehr = rd(RCS + R_IPEHR);
	e->reset = rd(RCS + R_RESET_CTL);
	e->ctxctl = rd(RCS + R_CTX_CTL);
	e->ccid = rd(RCS + R_CCID);
	e->bbaddr = rd(RCS + R_BBADDR);
}

void engine_dump(const char *when)
{
	struct engine_regs e;
	engine_read(&e);
	glog("rendu %s : GFX_MODE %08x, MI_MODE %08x, HWS_PGA %08x, HWSTAM %08x, IMR %08x\n", when, e.mode, e.mi_mode,
	     e.hws, e.hwstam, e.imr);
	glog("  anneau : HEAD %08x TAIL %08x START %08x CTL %08x ACTHD %08x BBADDR %08x\n", e.head, e.tail, e.start,
	     e.ctl, e.acthd, e.bbaddr);
	glog("  execlists : état %08x:%08x, CSB %08x, CCID %08x, CTX_CTL %08x, RESET_CTL %08x\n", e.el_hi, e.el_lo, e.csb,
	     e.ccid, e.ctxctl, e.reset);
	glog("  INSTDONE %08x, EIR %08x, ESR %08x, IPEHR %08x\n", e.instdone, e.eir, e.esr, e.ipehr);
}

/* ---- la GGTT : ce que le micrologiciel (GOP) y a mis ; une fenêtre libre pour la soumission */

static u64 scratch_pte; /* une entrée répétée partout (page de garde du GOP), 0 sinon */
static u64 used_end;    /* fin de la dernière entrée valide qui n'est pas la page de garde */

static int pte_free(u64 v)
{
	return !(v & 1) || (scratch_pte && v == scratch_pte);
}

void ggtt_scan(void)
{
	u64 n = G.ggtt_entries, runs = 0, valid = 0;
	scratch_pte = 0;
	used_end = 0;
	if (!n) {
		glog("ANOMALIE : GGMS = 0, pas de GGTT\n");
		return;
	}
	u64 last = gtt_rd(n - 1);
	if ((last & 1) && last == gtt_rd(n - 2) && last == gtt_rd(n / 2))
		scratch_pte = last;
	u64 start = 0;
	int in = 0;
	for (u64 i = 0; i <= n; i++) {
		u64 v = i < n ? gtt_rd(i) : 0;
		int used = i < n && !pte_free(v);
		valid += i < n && (v & 1);
		if (used && !in) {
			start = i;
			in = 1;
		} else if (!used && in) {
			in = 0;
			used_end = i;
			if (runs < 8)
				glog("GGTT : entrées %lx..%lx valides (%lu Ko à %lx), pages %lx..\n", (unsigned long)start,
				     (unsigned long)i - 1, (unsigned long)((i - start) * 4), (unsigned long)(start * 4096),
				     (unsigned long)(gtt_rd(start) & ~0xFFFull));
			runs++;
		}
	}
	glog("GGTT : %lu entrées valides, %lu plage(s) utilisée(s), fin de la dernière %lx%s%lx\n",
	     (unsigned long)valid, (unsigned long)runs, (unsigned long)(used_end * 4096),
	     scratch_pte ? ", page de garde " : ", pas de page de garde", (unsigned long)(scratch_pte & ~0xFFFull));
}

/* n entrées libres de suite, après l'ouverture (256 Mo) et ce qu'utilise le micrologiciel, loin du haut
 * (le GuC y a sa place) -> premier indice, ou -1 */
long ggtt_window(u64 n)
{
	u64 aper = (256ull << 20) / 4096;
	u64 i = used_end > aper ? used_end : aper;
	i = (i + 4095) & ~4095ull; /* 16 Mo */
	u64 top = G.ggtt_entries > 8192 ? G.ggtt_entries - 8192 : 0;
	u32 surf = rd(PLANE_SURF_1_A) & ~0xFFFu;
	for (; i + n <= top; i += 4096) {
		u64 j = 0;
		while (j < n && pte_free(gtt_rd(i + j)))
			j++;
		u64 lo = i * 4096, hi = (i + n) * 4096;
		if (j == n && !(surf >= lo && surf < hi))
			return (long)i;
	}
	return -1;
}

/* ---- trouver la fonction */

static int is_tgl(u16 dev)
{
	for (unsigned i = 0; i < sizeof tgl_ids / sizeof tgl_ids[0]; i++)
		if (tgl_ids[i] == dev)
			return 1;
	return 0;
}

static int find_gpu(void)
{
	if (G.simu) {
		memset(&G.pci, 0, sizeof G.pci);
		G.pci.addr = PCI_ADDR(0, 0, 2, 0);
		G.pci.vendor = 0x8086;
		G.pci.device = 0x9A60;
		G.pci.rev = 1;
		G.pci.cls = 3;
		G.pci.svid = 0x103C;
		G.pci.cap_msi = 0xAC;
		G.pci.msi_64 = 1;
		G.pci.bar[0] = 0x6052000000ull;
		G.pci.bar[2] = 0x4000000000ull;
		G.pci.bar[4] = 0x4000;
		G.pci.bar_flags[4] = PCI_BAR_IO;
		pci_format_addr(G.pci.addr, G.where);
		G.cmd_before = cfg_read(CFG_CMD, 2);
		return 1;
	}
	long r = pci_open(3000);
	if (r < 0) {
		glog("service pci absent (%ld) : pcid ne tourne pas ?\n", r);
		return -1;
	}
	long from = 0;
	struct pci_info f;
	int found = 0;
	while (pci_find_class(0x030000, 0xFF0000, &from, &f) == 0) {
		char a[16];
		pci_format_addr(f.addr, a);
		if (f.vendor != 0x8086) {
			glog("affichage %04x:%04x à %s : pas Intel, laissé\n", f.vendor, f.device, a);
			continue;
		}
		if (found)
			continue;
		G.pci = f;
		memcpy(G.where, a, sizeof a);
		found = 1;
	}
	if (!found) {
		glog("pas de GPU Intel (aucune fonction d'affichage 8086) : rien à faire\n");
		return 0;
	}
	G.cmd_before = cfg_read(CFG_CMD, 2) & 0xFFFF;
	return 1;
}

static int claim(void)
{
	if (G.simu)
		return 0;
	struct pci_info f;
	long r = pci_claim(G.pci.addr, &f);
	if (r < 0) {
		glog("%s refusé par pcid (%ld) : un autre pilote le tient ?\n", G.where, r);
		return -1;
	}
	G.pci = f;
	glog("%s pris ; commande PCI %04x\n", G.where, cfg_read(CFG_CMD, 2) & 0xFFFF);
	return 0;
}

/* le registre de commande tel qu'avant (le maître du bus du GOP reste), puis la fonction rendue */
static void unclaim(void)
{
	if (G.simu)
		return;
	u32 now = cfg_read(CFG_CMD, 2) & 0xFFFF;
	if (now != G.cmd_before) {
		gdbg("écrit config %02x <- %04x\n", CFG_CMD, G.cmd_before);
		pci_cfg_write(G.pci.addr, CFG_CMD, 2, G.cmd_before);
	}
	pci_release(G.pci.addr);
	glog("%s rendu ; commande PCI %04x\n", G.where, cfg_read(CFG_CMD, 2) & 0xFFFF);
}

static int map_bar0(void)
{
	if (G.simu) {
		G.mmio = simu_bar0();
	} else {
		if (!G.pci.bar[0] || (G.pci.bar_flags[0] & PCI_BAR_IO)) {
			glog("ANOMALIE : pas de BAR0 en mémoire\n");
			return -1;
		}
		long va = korin_sys2(SYS_DEV_MAP, G.pci.bar[0], BAR0_SIZE);
		if (va < 0) {
			glog("ANOMALIE : BAR0 (%lx, 16 Mo) non projetée (%ld)\n", (unsigned long)G.pci.bar[0], va);
			return -1;
		}
		G.mmio = (volatile u8 *)va;
	}
	G.ggtt = (volatile u64 *)(G.mmio + GGTT_OFF);
	u32 a = rd(FW_GT_ACK), b = rd(FW_RENDER_ACK), c = rd(RCS + R_MODE);
	if (a == 0xFFFFFFFF && b == 0xFFFFFFFF && c == 0xFFFFFFFF) {
		glog("ANOMALIE : BAR0 ne répond pas (que des 1)\n");
		return -1;
	}
	gdbg("BAR0 projetée à %lx\n", (unsigned long)G.mmio);
	return 0;
}

/* ---- pour l'essai et le service : la fonction trouvée, prise, sa BAR0 mappée, le forcewake tenu -> 0 */
int gpu_open(void)
{
	int f = find_gpu();
	if (f <= 0)
		return -1;
	identity_cfg();
	if (!is_tgl(G.pci.device)) {
		glog("%04x n'est pas un Tiger Lake (Gen12 LP) connu : pas de service\n", G.pci.device);
		return -1;
	}
	if (G.ggtt_entries == 0 || G.ggtt_entries > (BAR0_SIZE - GGTT_OFF) / 8) {
		glog("ANOMALIE : taille de GGTT illisible\n");
		return -1;
	}
	if (claim())
		return -1;
	if (map_bar0() || fw_get()) {
		fw_put();
		unclaim();
		return -1;
	}
	identity_mmio();
	engine_dump("au départ");
	ggtt_scan();
	return 0;
}

void gpu_close(void)
{
	fw_put();
	unclaim();
}

/* ---- les commandes qui parlent au service en marche (stop, status) */
static long svc_ask(long op, char *text, int max)
{
	long ch = korin_sys2(SYS_SVC_OPEN, IG_SVC_NAME, 0);
	if (ch < 0) {
		glog("le service ne tourne pas (SD0:/sys/svc/intelgpu start)\n");
		return ch;
	}
	static u64 m[64];
	memset(m, 0, sizeof m);
	m[0] = (u64)op;
	long r = korin_sys2(SYS_SVC_CALL, ch, m);
	if (text) {
		int n = r >= 0 ? (int)((u32 *)m)[10] : 0;
		if (n > 464)
			n = 464;
		if (n > max - 1)
			n = max - 1;
		memcpy(text, (u8 *)m + 48, n);
		text[n] = 0;
	}
	return r;
}

static void usage(void)
{
	glog("SD0:/sys/svc/intelgpu probe [submit] [-v] [simu | simu-hang]   la sonde (étape 1)\n");
	glog("SD0:/sys/svc/intelgpu test [-v] [-p] [simu | simu-hang]   le moteur mis en route, lots d'essai faits à "
	     "la main, tout remis comme avant\n");
	glog("SD0:/sys/svc/intelgpu start [-v] [-p] [simu]   lance le service et attend qu'il soit prêt\n");
	glog("SD0:/sys/svc/intelgpu status | stop   l'état du service ; son arrêt propre\n");
}

int main(int argc, char **argv)
{
	int probe = 0, submit = 0, serve_mode = 0, test = 0, start = 0, stop = 0, status = 0, poll = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "probe"))
			probe = 1;
		else if (!strcmp(argv[i], "submit"))
			submit = 1;
		else if (!strcmp(argv[i], "serve"))
			serve_mode = 1;
		else if (!strcmp(argv[i], "test"))
			test = 1;
		else if (!strcmp(argv[i], "start"))
			start = 1;
		else if (!strcmp(argv[i], "stop"))
			stop = 1;
		else if (!strcmp(argv[i], "status"))
			status = 1;
		else if (!strcmp(argv[i], "-v"))
			G.verbose = 1;
		else if (!strcmp(argv[i], "-p"))
			poll = 1;
		else if (!strcmp(argv[i], "simu"))
			G.simu = 1;
		else if (!strcmp(argv[i], "simu-hang"))
			G.simu = simu_hang = 1;
	}
	S.irq = poll ? -2 : -1; /* -2 : sondage demandé */
	if (stop || status) {
		char t[480];
		if (svc_ask(stop ? IG_OP_STOP : IG_OP_STATUS, t, sizeof t) < 0)
			return 1;
		if (status)
			out_text(t);
		else
			glog("arrêt demandé : le service rend le GPU comme il l'a trouvé (journal)\n");
		return 0;
	}
	if (start) {
		if (korin_sys2(SYS_SVC_OPEN, IG_SVC_NAME, 0) >= 0) {
			glog("le service tourne déjà (SD0:/sys/svc/intelgpu status)\n");
			return 0;
		}
		char args[64];
		snprintf(args, sizeof args, "serve%s%s%s", G.verbose ? " -v" : "", poll ? " -p" : "",
			 G.simu ? " simu" : "");
		long pid = f_spawn("SD0:/sys/svc/intelgpu", args);
		if (pid < 0) {
			glog("lancement refusé (%ld)\n", pid);
			return 1;
		}
		if (korin_sys2(SYS_SVC_OPEN, IG_SVC_NAME, 20000) < 0) {
			glog("le service (pid %ld) ne s'est pas annoncé en 20 s : voir le journal (lignes intelgpu)\n", pid);
			return 1;
		}
		char t[480];
		if (svc_ask(IG_OP_STATUS, t, sizeof t) >= 0)
			out_text(t);
		glog("service prêt (pid %ld) ; pour l'arrêter : SD0:/sys/svc/intelgpu stop\n", pid);
		return 0;
	}
	if (test || serve_mode) {
		extern int simu_hang_later;
		simu_hang_later = simu_hang;
		simu_hang = 0;
		if (gpu_open())
			return 1;
		gt_init();
		int rc = 1;
		if (eng_init() == 0)
			rc = test ? (eng_selftest() ? 1 : 0) : serve();
		eng_fini();
		if (S.dead == 2) {
			glog("DANGER : le moteur de rendu n'a pas pu être arrêté ; ce process reste en vie pour garder ses "
			     "pages (ne pas le tuer) : éteindre le PC (bouton tenu) et rapporter le journal\n");
			for (;;)
				korin_sys1(SYS_SLEEP, 60000);
		}
		gpu_close();
		glog("%s fini (%s)\n", test ? "essai" : "service", rc ? "anomalie, voir plus haut" : "rien d'anormal");
		return rc;
	}
	if (!probe) {
		usage();
		return 1;
	}
	int f = find_gpu();
	if (f <= 0)
		return f < 0 ? 1 : 0;
	identity_cfg();
	if (!is_tgl(G.pci.device)) {
		glog("%04x n'est pas un Tiger Lake (Gen12 LP) connu : arrêt après la configuration PCI\n", G.pci.device);
		return 0;
	}
	if (G.ggtt_entries == 0 || G.ggtt_entries > (BAR0_SIZE - GGTT_OFF) / 8) {
		glog("ANOMALIE : taille de GGTT illisible\n");
		return 1;
	}
	if (claim())
		return 1;
	int rc = 1;
	if (map_bar0())
		goto out;
	if (fw_get())
		goto out;
	identity_mmio();
	engine_dump("au départ");
	ggtt_scan();
	rc = 0;
	if (submit) {
		int s = submit_test();
		if (s == -2) {
			/* le moteur n'a pu être arrêté : ses pages restent à nous tant que le process vit */
			glog("DANGER : le moteur de rendu n'est pas arrêté ; ce process reste en vie pour garder ses "
			     "pages (ne pas le tuer) : éteindre le PC (bouton tenu) et rapporter le journal\n");
			for (;;)
				korin_sys1(SYS_SLEEP, 60000);
		}
		rc = s ? 1 : 0;
	}
out:
	fw_put();
	unclaim();
	glog("sonde finie (%s)\n", rc ? "anomalie, voir plus haut" : "rien d'anormal");
	return rc;
}
