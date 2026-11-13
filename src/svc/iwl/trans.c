/* trans.c : le transport PCIe « gen2 » des Wi-Fi Intel 22000 (docs/etude/WIFI-PC.md 4).
 *
 * Mise en route (comme le pilote de Linux, séquences décrites et réécrites) :
 *   start_hw : la puce prête (NIC_READY), bit de persistance effacé, remise à zéro logicielle, porte
 *              d'alimentation forcée (puces intégrées CNVi), « APM » (horloges, L0s coupé, INIT_DONE), mode
 *              MSI des causes (UREG_CHICK) ;
 *   start_fw : de nouveau la puce prête, interruptions coupées, poignées de main rfkill effacées, APM gen2,
 *              configuration de la carte (étapes du MAC et de la radio), anneau de réception (vide : il est
 *              rempli à l'interruption ALIVE, quand le micrologiciel a réglé le RFH), file de commandes,
 *              registres fantômes, puis le « context info » : son adresse dans CSR_CTXT_INFO_BA, la latence
 *              (LTR) de démarrage, et UREG_CPU_INIT_RUN = 1. La ROM de la puce copie elle-même les sections
 *              du micrologiciel depuis la mémoire de l'hôte, en vérifie la signature et le lance ; il répond
 *              par l'interruption ALIVE puis la notification « vivant » sur l'anneau de réception.
 * Arrêt : poignée de main de remise à zéro (micrologiciel en marche), interruptions coupées, DMA de réception
 * arrêté, maître du bus coupé, remise à zéro logicielle, INIT_DONE effacé, la puce reprise (NIC_READY). */
#include "iwl.h"

static int nic_held;

u32 rd32(u32 off)
{
	return *(volatile u32 *)(W.mmio + off);
}

void wr32(u32 off, u32 v)
{
	*(volatile u32 *)(W.mmio + off) = v;
}

void set_bits(u32 off, u32 m)
{
	wr32(off, rd32(off) | m);
}

void clear_bits(u32 off, u32 m)
{
	wr32(off, rd32(off) & ~m);
}

/* une attente courte : une lecture d'un registre de la puce dure de l'ordre d'une microseconde (au moins
 * 0,5 µs sur le bus) : deux lectures par microseconde demandée, c'est toujours assez */
/* attentes courtes : le compteur de temps du processeur (rdtsc), étalonné sur 20 ms de SYS_TIME_MS ; jamais
 * nulles. (Le 13/11 sur le ZBook, l'étalonnage par lectures de registre donnait 0 : une lecture du CNVi y dure plus
 * d'une microseconde, et les attentes retombaient à 1 lecture.) */
static u64 tsc_per_us; /* 0 : pas encore étalonné (repli : 4 lectures de registre par µs, toujours assez) */

static inline u64 rdtsc(void)
{
	u32 lo, hi;
	__asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
	return (u64)hi << 32 | lo;
}

void udelay(u32 us)
{
	if (!us)
		us = 1;
	if (tsc_per_us) {
		u64 end = rdtsc() + tsc_per_us * us;
		while (rdtsc() < end)
			__asm__ volatile("pause");
		return;
	}
	for (u32 i = 0; i < 4 * us; i++)
		(void)rd32(CSR_HW_REV);
}

void udelay_calibrate(void)
{
	long t0 = now_ms();
	while (now_ms() == t0)
		;
	u64 a = rdtsc();
	long t1 = now_ms() + 20;
	u32 n = 0;
	while (now_ms() < t1) {
		(void)rd32(CSR_HW_REV);
		n++;
	}
	u64 b = rdtsc();
	u64 per = (b - a) / 20000;
	if (per < 100 || per > 20000) { /* (moins de 100 MHz ou plus de 20 GHz : mesure fausse) */
		ilog("attentes courtes : compteur du processeur inutilisable (%lu par µs) : repli sur les lectures de "
		     "registre (4 par µs)\n",
		     (unsigned long)per);
		tsc_per_us = 0;
	} else
		tsc_per_us = per;
	ilog("attentes courtes : compteur à %lu MHz ; une lecture de registre de la puce dure %u ns\n",
	     (unsigned long)tsc_per_us, n ? 20000000u / n : 0);
}

int hw_err(u32 v)
{
	return (v & ~0xFu) == 0xA5A5A5A0u || (v & ~0xFu) == 0x5A5A5A50u || v == 0xFFFFFFFFu;
}

/* (reg & mask) == want, au plus us microsecondes (arrondies à la milliseconde, et au moins 2 ms : l'horloge
 * de SYS_TIME_MS est à la milliseconde) -> 0 ou -1 */
int poll_bits(u32 off, u32 want, u32 mask, u32 us)
{
	long end = now_ms() + (long)(us / 1000) + 2;
	for (int spin = 0;; spin++) {
		if ((rd32(off) & mask) == want)
			return 0;
		if (now_ms() > end)
			return (rd32(off) & mask) == want ? 0 : -1;
		if (spin > 200)
			korin_sys0(SYS_YIELD);
		else
			udelay(5);
	}
}

/* ---- accès aux registres internes : MAC_ACCESS_REQ, puis horloge prête et pas en sommeil */
int nic_grab(void)
{
	if (nic_held++)
		return 0;
	set_bits(CSR_GP_CNTRL, GP_MAC_ACCESS_REQ);
	udelay(2);
	if (poll_bits(CSR_GP_CNTRL, GP_MAC_CLOCK_READY, GP_MAC_CLOCK_READY | GP_GOING_TO_SLEEP, 15000)) {
		ilog("ANOMALIE : accès MAC refusé (CSR_GP_CNTRL %08x)\n", rd32(CSR_GP_CNTRL));
		clear_bits(CSR_GP_CNTRL, GP_MAC_ACCESS_REQ);
		nic_held = 0;
		return -1;
	}
	/* l'horloge était souvent déjà prête (INIT_DONE) : le sondage rend aussitôt ; la marge laisse le temps au
	 * réveil de l'accès lui-même (13/11 sur le ZBook : lectures A5A5A5A2 avec une attente trop courte) */
	udelay(10);
	return 0;
}

void nic_release(void)
{
	if (nic_held && !--nic_held)
		clear_bits(CSR_GP_CNTRL, GP_MAC_ACCESS_REQ);
}

u32 prph_rd(u32 a)
{
	wr32(HBUS_TARG_PRPH_RADDR, (a & PRPH_MASK) | (3u << 24));
	return rd32(HBUS_TARG_PRPH_RDAT);
}

void prph_wr(u32 a, u32 v)
{
	wr32(HBUS_TARG_PRPH_WADDR, (a & PRPH_MASK) | (3u << 24));
	wr32(HBUS_TARG_PRPH_WDAT, v);
}

u32 prph_read(u32 a)
{
	if (nic_grab())
		return 0xA5A5A5A5;
	u32 v = prph_rd(a);
	nic_release();
	return v;
}

void prph_write(u32 a, u32 v)
{
	if (nic_grab())
		return;
	prph_wr(a, v);
	nic_release();
}

/* lecture-modification-écriture : jamais à partir d'une valeur d'erreur du bus (0xA5A5A5Ax) -> 0, -1 */
static int prph_rmw(u32 a, u32 clr, u32 set)
{
	if (nic_grab())
		return -1;
	u32 v = prph_rd(a);
	if (hw_err(v)) {
		nic_release();
		ilog("ANOMALIE : registre interne %06x illisible (%08x) : pas d'écriture\n", a, v);
		return -1;
	}
	prph_wr(a, (v & ~clr) | set);
	nic_release();
	return 0;
}

/* l'essai d'accès aux registres internes : quelques lectures sûres, OK ou ÉCHEC au journal -> 0, -1 */
/* Le bus des registres internes de la famille 22000 : après une lecture qui échoue (A5A5A5Ax), toutes les suivantes
 * échouent jusqu'à la prochaine remise à zéro (vu le 13/11 sur le ZBook). Donc : WFPM_CTRL d'abord (il se lit dès
 * INIT_DONE), son bit 31 (« ENABLE_WFPM ») posé comme le fait iwlwifi à la sonde de toutes les puces 8000 et plus,
 * puis seulement les autres registres, du plus sûr au moins sûr ; on s'arrête à la première erreur. */
int wfpm_enable(const char *step)
{
	if (nic_grab())
		return -1;
	u32 w = prph_rd(WFPM_CTRL_REG);
	if (hw_err(w)) {
		nic_release();
		ilog("WFPM %s : WFPM_CTRL illisible (%08x) : rien n'est écrit\n", step, w);
		return -1;
	}
	u32 w2 = w;
	if (!(w & WFPM_AUX_IF_MAC_OWNER)) {
		prph_wr(WFPM_CTRL_REG, w | WFPM_AUX_IF_MAC_OWNER);
		w2 = prph_rd(WFPM_CTRL_REG);
	}
	nic_release();
	idbg("WFPM %s : WFPM_CTRL %08x -> %08x\n", step, w, w2);
	if (hw_err(w2) || !(w2 & WFPM_AUX_IF_MAC_OWNER)) {
		ilog("WFPM %s : le bit 31 ne tient pas (WFPM_CTRL %08x -> %08x)\n", step, w, w2);
		return -1;
	}
	return 0;
}

static const struct {
	u32 a;
	const char *n;
} probe_regs[] = {
	/* seulement ce qu'iwlwifi lit lui-même avant le micrologiciel (sonde, persistance, porte d'alimentation) : une
	 * lecture refusée bloquerait le bus jusqu'à la remise à zéro suivante */
	{WFPM_CTRL_REG, "WFPM_CTRL"}, {HPM_DEBUG, "HPM_DEBUG"}, {HPM_HIPM_GEN_CFG, "HIPM_GEN_CFG"},
	{CNVI_AUX_MISC_CHIP, "CNVI"},
};

/* -> 0 si tous se lisent ; journal : chaque valeur, et où la première erreur est arrivée */
int prph_check(const char *step)
{
	if (nic_grab()) {
		ilog("accès aux registres internes %s : ÉCHEC (accès MAC refusé, GP_CNTRL %08x)\n", step, rd32(CSR_GP_CNTRL));
		return -1;
	}
	u32 gp = rd32(CSR_GP_CNTRL);
	char b[360];
	int k = 0, n = (int)(sizeof probe_regs / sizeof probe_regs[0]), ok = 0;
	for (int i = 0; i < n; i++) {
		u32 v = prph_rd(probe_regs[i].a);
		k += snprintf(b + k, sizeof b - k, "%s%s %08x", i ? ", " : "", probe_regs[i].n, v);
		if (hw_err(v))
			break; /* (les suivantes échoueraient aussi) */
		ok++;
	}
	nic_release();
	ilog("accès aux registres internes %s : %s (%d/%d ; GP_CNTRL %08x pendant l'accès ; %s)\n", step,
	     ok == n ? "OK" : ok ? "PARTIEL" : "ÉCHEC", ok, n, gp, b);
	return ok == n ? 0 : -1;
}

int mem_read(u32 addr, u32 *out, int n)
{
	if (nic_grab())
		return -1;
	wr32(HBUS_TARG_MEM_RADDR, addr);
	for (int i = 0; i < n; i++)
		out[i] = rd32(HBUS_TARG_MEM_RDAT); /* (l'adresse avance d'elle-même) */
	nic_release();
	return 0;
}

/* ---- la puce prête pour l'hôte (et pas tenue par le moteur de gestion, ME/AMT) */
static int set_hw_ready(void)
{
	set_bits(CSR_HW_IF_CONFIG, HWIF_NIC_READY);
	int r = poll_bits(CSR_HW_IF_CONFIG, HWIF_NIC_READY, HWIF_NIC_READY, 50);
	if (!r)
		set_bits(CSR_MBOX_SET, MBOX_OS_ALIVE);
	return r;
}

static int prepare_card_hw(void)
{
	if (!set_hw_ready())
		return 0;
	set_bits(CSR_DBG_LINK_PWR_MGMT, RESET_LINK_PWR_MGMT_DISABLED);
	msleep(2);
	for (int iter = 0; iter < 10; iter++) {
		set_bits(CSR_HW_IF_CONFIG, HWIF_PREPARE);
		for (int t = 0; t < 150; t++) { /* 150 ms */
			if (!set_hw_ready())
				return 0;
			msleep(1);
		}
		msleep(25);
	}
	ilog("ANOMALIE : la puce ne se dit pas prête (NIC_READY) : tenue par le ME/AMT ? CSR_HW_IF_CONFIG %08x\n",
	     rd32(CSR_HW_IF_CONFIG));
	return -1;
}

static int sw_reset(int retake)
{
	set_bits(CSR_RESET, RESET_SW_RESET);
	msleep(6);
	return retake ? prepare_card_hw() : 0;
}

static int finish_nic_init(void)
{
	set_bits(CSR_GP_CNTRL, GP_INIT_DONE);
	if (poll_bits(CSR_GP_CNTRL, GP_MAC_CLOCK_READY, GP_MAC_CLOCK_READY, 25000)) {
		ilog("ANOMALIE : l'horloge du MAC ne se stabilise pas (CSR_GP_CNTRL %08x)\n", rd32(CSR_GP_CNTRL));
		return -1;
	}
	return 0;
}

/* APM : commun à start_hw (ancienne forme, sans APMG sur les 22000) et à la forme gen2 */
static int apm_init(void)
{
	set_bits(CSR_GIO_CHICKEN, GIO_CHICKEN_L1A_NO_L0S_RX); /* L0s coupé, L1 gardé */
	set_bits(CSR_DBG_HPET_MEM, HPET_MEM_VAL);              /* seuil d'attente du FH au maximum */
	set_bits(CSR_HW_IF_CONFIG, HWIF_HAP_WAKE_L1A);
	set_bits(CSR_GIO, GIO_L0S_DISABLED);
	if (finish_nic_init())
		return -1;
	W.device_enabled = 1;
	return 0;
}

static void disable_interrupts(void)
{
	wr32(CSR_INT_MASK, 0);
	wr32(CSR_INT, 0xFFFFFFFF);
	wr32(CSR_FH_INT_STATUS, 0xFFFFFFFF);
}

/* comme la sonde d'iwlwifi (5.10 à 6.12, toutes les puces 8000 et plus) : la puce prête, INIT_DONE, l'accès MAC,
 * ENABLE_WFPM, puis CNVI lu. Aucune remise à zéro. */
int trans_probe_access(void)
{
	ilog("au départ : GP_CNTRL %08x RESET %08x HW_IF_CONFIG %08x\n", rd32(CSR_GP_CNTRL), rd32(CSR_RESET),
	     rd32(CSR_HW_IF_CONFIG));
	if (prepare_card_hw())
		return -1;
	if (finish_nic_init())
		return -1;
	if (wfpm_enable("à la sonde"))
		return -1;
	int r = prph_check("à la sonde");
	W.cnv_id = prph_read(CNVI_AUX_MISC_CHIP);
	return r;
}

int trans_start_hw(void)
{
	if (prepare_card_hw())
		return -1;
	/* bit de persistance (une session précédente) : à effacer, sauf si la puce le protège */
	u32 hpm = prph_rd(HPM_DEBUG); /* (sans l'accès MAC, comme iwlwifi : l'horloge n'est pas encore là) */
	if (!hw_err(hpm) && (hpm & HPM_PERSISTENCE_BIT)) {
		u32 wprot = prph_rd(PREG_PRPH_WPROT_22000);
		if (hw_err(wprot) || (wprot & PREG_WFPM_ACCESS)) {
			ilog("ANOMALIE : bit de persistance protégé (HPM_DEBUG %08x, WPROT %08x)\n", hpm, wprot);
			return -1;
		}
		prph_wr(HPM_DEBUG, hpm & ~HPM_PERSISTENCE_BIT);
		ilog("bit de persistance effacé (HPM_DEBUG %08x)\n", hpm);
	}
	if (sw_reset(1))
		return -1;
	/* puce intégrée (CNVi) : porte d'alimentation forcée puis rendue, et nouvelle remise à zéro ; seulement si les
	 * registres internes se lisent (jamais d'écriture tirée d'une valeur d'erreur) */
	if (finish_nic_init())
		return -1;
	wfpm_enable("après la remise à zéro");
	if (prph_check("après la remise à zéro")) {
		ilog("ÉCHEC : registres internes inaccessibles : arrêt avant toute écriture interne\n");
		return -1;
	}
	if (prph_rmw(HPM_HIPM_GEN_CFG, 0, HIPM_FORCE_ACTIVE))
		return -1;
	udelay(20);
	if (prph_rmw(HPM_HIPM_GEN_CFG, 0, HIPM_PG_EN | HIPM_SLP_EN))
		return -1;
	udelay(20);
	if (prph_rmw(HPM_HIPM_GEN_CFG, HIPM_FORCE_ACTIVE, 0))
		return -1;
	if (sw_reset(1))
		return -1;
	if (apm_init())
		return -1;
	wfpm_enable("après l'APM");
	if (prph_check("après l'APM")) {
		ilog("ÉCHEC : registres internes inaccessibles après la porte d'alimentation\n");
		return -1;
	}
	/* causes d'interruption par CSR_INT (MSI ou sondage), pas par les tables MSI-X */
	prph_write(UREG_CHICK, UREG_CHICK_MSI_ENABLE);
	disable_interrupts();
	return 0;
}

/* ---- anneau de réception */
static int rx_alloc(void)
{
	if (dma_alloc(3 * 4096, &W.rings))
		return -1;
	W.rx_free = (u64 *)W.rings.va;              /* 512 × 8 o : adresse | numéro du RB (12 bits) */
	W.rx_used = (u32 *)(W.rings.va + 4096);     /* 512 × 4 o : numéro du RB rempli */
	W.rb_stts = W.rings.va + 8192;              /* 16 o : RB fermés (12 bits) */
	for (int i = 0; i < RX_BUFS; i += RB_CHUNK) {
		struct dma_blk b;
		int n = RX_BUFS - i < RB_CHUNK ? RX_BUFS - i : RB_CHUNK;
		if (dma_alloc((u32)n * RB_SIZE, &b))
			return -1;
		for (int k = 0; k < n; k++) {
			W.rb_va[i + k] = b.va + k * RB_SIZE;
			W.rb_pa[i + k] = b.pa + (u64)k * RB_SIZE;
		}
	}
	return 0;
}

static void rx_write_index(void)
{
	u32 wa = W.rx_write & ~7u;
	if (wa != W.rx_write_actual) {
		W.rx_write_actual = wa;
		wr32(RFH_Q0_FRBDCB_WIDX_TRG, wa);
	}
}

static void rx_put(int vid) /* le RB n° vid (1…511) dans l'anneau libre */
{
	W.rx_free[W.rx_write] = W.rb_pa[vid - 1] | (u64)vid;
	W.rx_write = (W.rx_write + 1) & (RXQ_SIZE - 1);
}

static void rx_restock(void)
{
	if (W.restocked)
		return;
	W.restocked = 1;
	for (int v = 1; v <= RX_BUFS; v++)
		rx_put(v);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	W.rx_write_actual = 0xFFFFFFFF;
	rx_write_index();
	idbg("anneau de réception rempli : %d RB, index d'écriture %u\n", RX_BUFS, W.rx_write & ~7u);
}

/* ---- file de commandes */
static int txq_alloc(void)
{
	if (dma_alloc(CMD_SLOTS * TFD_SIZE, &W.tfd) || dma_alloc(CMD_SLOTS * CMD_BUF, &W.cbufs))
		return -1;
	W.txq_w = 0;
	return 0;
}

/* ---- context info (cmds.c : build_ctxt_info) et les sections, chacune dans sa zone DMA */
static int ctxt_info_init(void)
{
	const struct fw_image *fw = &W.fw;
	if (dma_alloc(4096, &W.ctxt))
		return -1;
	static u64 pa[FW_MAX_SEC];
	u32 total = 0;
	for (int i = 0; i < fw->nsec; i++) {
		const struct fw_sec *s = &fw->sec[i];
		pa[i] = 0;
		if (s->offset == FW_SEC_CPU_SEPARATOR || s->offset == FW_SEC_PAGING_SEPARATOR)
			continue;
		if (dma_alloc(s->len, &W.fwsec[i]))
			return -1;
		memcpy(W.fwsec[i].va, s->data, s->len);
		pa[i] = W.fwsec[i].pa;
		total += s->len;
	}
	build_ctxt_info(W.ctxt.va, W.hw_rev, W.rings.pa, W.tfd.pa, fw, pa);
	idbg("context info à %lx : %d + %d + %d sections, %u octets copiés\n", (unsigned long)W.ctxt.pa, fw->n_lmac,
	     fw->n_umac, fw->n_paging, total);
	return 0;
}

int trans_start_fw(void)
{
	if (prepare_card_hw())
		return -1;
	wr32(CSR_INT, 0xFFFFFFFF);
	disable_interrupts();
	if (!(rd32(CSR_GP_CNTRL) & GP_HW_RF_KILL_SW))
		ilog("ATTENTION : radio coupée par l'interrupteur (rfkill, mode avion) : le micrologiciel démarre, mais "
		     "le balayage ne verra rien\n");
	wr32(CSR_UCODE_DRV_GP1_CLR, GP1_SW_BIT_RFKILL);
	wr32(CSR_UCODE_DRV_GP1_CLR, GP1_CMD_BLOCKED);
	wr32(CSR_INT, 0xFFFFFFFF);
	/* APM gen2 */
	if (apm_init())
		return -1;
	/* configuration de la carte (radio : type, étape, tiret du PHY_SKU ; MAC : étape et tiret de HW_REV) */
	u32 phy = W.fw.phy_config;
	u32 v = (W.hw_rev & 0xF) | (phy & 3) << 10 | ((phy >> 2) & 3) << 14 | ((phy >> 4) & 3) << 12;
	u32 m = 0xF | 0xC00 | 0xC000 | 0x3000 | 0x200 | 0x100;
	wr32(CSR_HW_IF_CONFIG, (rd32(CSR_HW_IF_CONFIG) & ~m) | v);
	/* réception (vide jusqu'à ALIVE), commandes, registres fantômes */
	wr32(CSR_INT_COALESCING, INT_COALESCING_DEF);
	if (!W.rings.va && rx_alloc())
		return -2;
	memset(W.rb_stts, 0, 16);
	memset(W.rx_free, 0, RXQ_SIZE * 8);
	memset(W.rx_used, 0, RXQ_SIZE * 4);
	W.rx_read = W.rx_write = 0;
	W.rx_write_actual = 0;
	W.restocked = 0;
	if (!W.tfd.va && txq_alloc())
		return -2;
	memset(W.tfd.va, 0, CMD_SLOTS * TFD_SIZE);
	W.txq_w = 0;
	set_bits(CSR_MAC_SHADOW_REG_CTRL, 0x800FFFFF);
	if (ctxt_info_init())
		return -2;
	/* interruptions du chargement : ALIVE et réception */
	wr32(CSR_INT_MASK, W.irq >= 0 ? (INT_ALIVE | INT_FH_RX) : 0);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	wr32(CSR_CTXT_INFO_BA, (u32)W.ctxt.pa);
	wr32(CSR_CTXT_INFO_BA + 4, (u32)(W.ctxt.pa >> 32));
	/* LTR de démarrage (~250 µs) : CNVi des 22000 */
	u32 ltr = 0x80000000u | 2u << 26 | 250u << 16 | 0x8000u | 2u << 10 | 250u;
	if (prph_check("avant le lancement")) {
		ilog("ÉCHEC : registres internes inaccessibles : micrologiciel non lancé\n");
		return -1;
	}
	prph_write(HPM_MAC_LTR_CSR, HPM_MAC_LTR_ENABLE_ALL);
	prph_write(HPM_UMAC_LTR, ltr);
	prph_write(UREG_CPU_INIT_RUN, 1);
	ilog("lancement : LTR %08x écrite, UREG_CPU_INIT_RUN = 1 écrit (rien n'est relu : une lecture refusée bloquerait le bus)\n", ltr);
	W.fw_loaded = 1;
	W.fw_load_ms = now_ms();
	ilog("micrologiciel donné à la puce (%u octets, context info à %lx) : attente de « vivant »\n",
	     fw_total_bytes(&W.fw), (unsigned long)W.ctxt.pa);
	return 0;
}

void trans_alive(void)
{
	/* toutes les interruptions utiles (MSI) ; en sondage, le masque reste nul */
	wr32(CSR_INT_MASK, W.irq >= 0 ? INT_ALL_WANTED : 0);
	if (!(rd32(CSR_GP_CNTRL) & GP_HW_RF_KILL_SW))
		ilog("ATTENTION : rfkill (interrupteur de la radio) actif\n");
}

/* ---- réception : un RB peut contenir plusieurs paquets (alignés sur 64 o, fin : 0x55550000) */
static void handle_rb(const u8 *rb)
{
	u32 off = 0;
	while (off + 8 < RB_SIZE) {
		u32 lnf = le32(rb + off);
		if (lnf == 0x55550000u)
			break;
		u32 plen = lnf & 0x3FFF; /* en-tête (4 o) + données */
		if (plen < 4 || off + 4 + plen > RB_SIZE) {
			idbg("RB : longueur %u illisible à %u\n", plen, off);
			break;
		}
		u8 cmd = rb[off + 4], grp = rb[off + 5];
		u16 seq = le16(rb + off + 6);
		const u8 *p = rb + off + 8;
		u32 len = plen - 4;
		W.n_rx_pkts++;
		int is_resp = !(seq & 0x8000) && ((seq >> 8) & 0x1F) == 0 && W.waiting_seq == (int)(seq & 0xFF) &&
			      cmd == W.waiting_cmd;
		if (is_resp) {
			W.resp_grp = grp;
			W.resp_cmd = cmd;
			W.resp_len = len > RESP_MAX ? RESP_MAX : len;
			memcpy(W.resp, p, W.resp_len);
			W.resp_ready = 1;
			W.waiting_seq = -1;
			idbg("réponse %x.%02x, %u octets\n", grp, cmd, len);
		} else {
			W.n_notif++;
			mvm_rx(grp, cmd, seq, p, len);
		}
		off += (plen + 4 + 63) & ~63u;
	}
}

static void rx_process(void)
{
	u32 r = le16(W.rb_stts) & 0xFFF & (RXQ_SIZE - 1);
	u32 i = W.rx_read;
	int n = 0;
	while (i != r) {
		u32 vid = W.rx_used[i] & 0xFFF;
		if (!vid || vid > RX_BUFS) {
			ilog("ANOMALIE : RB n° %u invalide à l'entrée %u de l'anneau utilisé\n", vid, i);
		} else {
			handle_rb(W.rb_va[vid - 1]);
			rx_put((int)vid);
		}
		i = (i + 1) & (RXQ_SIZE - 1);
		n++;
	}
	W.rx_read = i;
	if (n) {
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		rx_write_index();
	}
}

int service(long ms)
{
	long end = now_ms() + ms;
	for (;;) {
		u32 in = rd32(CSR_INT);
		if (in == 0xFFFFFFFF || in == 0xA5A5A5A5) {
			ilog("ANOMALIE : CSR_INT illisible (%08x) : la puce ne répond plus\n", in);
			W.dead = 1;
			return E_FWERR;
		}
		if (in) {
			wr32(CSR_INT, in);
			W.last_int = in;
			if (in & (INT_FH_RX | INT_SW_RX))
				wr32(CSR_FH_INT_STATUS, FH_RX_MASK);
			if ((in & INT_ALIVE) && !W.alive_irq) {
				W.alive_irq = 1;
				idbg("interruption ALIVE (CSR_INT %08x)\n", in);
				rx_restock();
			}
			if (in & INT_RF_KILL)
				ilog("rfkill : la radio est %s\n",
				     (rd32(CSR_GP_CNTRL) & GP_HW_RF_KILL_SW) ? "permise" : "coupée (mode avion)");
			if (in & (INT_SW_ERR | INT_HW_ERR)) {
				ilog("ERREUR du micrologiciel (CSR_INT %08x : %s%s)\n", in, in & INT_SW_ERR ? "SW_ERR " : "",
				     in & INT_HW_ERR ? "HW_ERR" : "");
				W.dead = 1;
			}
		}
		/* ALIVE pas vu (masqué, perdu ?) : au bout de 1,5 s après le lancement, l'anneau est rempli quand même */
		if (W.fw_loaded && !W.restocked) {
			if (now_ms() > W.fw_load_ms + 1500) {
				ilog("ATTENTION : interruption ALIVE non vue en 1,5 s : anneau de réception rempli quand même "
				     "(CSR_INT %08x, MSI-X %08x)\n",
				     rd32(CSR_INT), rd32(CSR_MSIX_HW_INT_CAUSES));
				rx_restock();
			}
		}
		if (W.restocked)
			rx_process();
		if (W.dead)
			return E_FWERR;
		if (now_ms() >= end)
			return 0;
		if (W.irq >= 0) {
			if (korin_sys2(SYS_IRQ_WAIT, W.irq, 5) > 0)
				korin_sys1(SYS_IRQ_ACK, W.irq);
		} else
			msleep(1);
	}
}

/* ---- une commande hôte (en-tête large de 8 o) */
long send_cmd(u8 grp, u8 cmd, const void *data, u32 len, long timeout_ms)
{
	if (W.dead)
		return E_FWERR;
	if (len + 8 > CMD_BUF)
		return E_INVAL;
	u32 w = W.txq_w;
	u32 slot = w & (CMD_SLOTS - 1);
	u8 *b = W.cbufs.va + slot * CMD_BUF;
	u64 pa = W.cbufs.pa + (u64)slot * CMD_BUF;
	b[0] = cmd;
	b[1] = grp;
	put16(b + 2, (0u << 8) | (w & 0xFF)); /* file 0, numéro */
	put16(b + 4, len);
	b[6] = 0;
	b[7] = 0; /* version 0 */
	if (len)
		memcpy(b + 8, data, len);
	u32 total = len + 8;
	u8 *t = W.tfd.va + slot * TFD_SIZE;
	memset(t, 0, TFD_SIZE);
	u32 tb0 = total < FIRST_TB ? total : FIRST_TB;
	put16(t + 2, tb0);
	put64(t + 4, pa);
	int ntb = 1;
	if (total > tb0) {
		put16(t + 12, total - tb0);
		put64(t + 14, pa + tb0);
		ntb = 2;
	}
	put16(t, ntb);
	W.waiting_seq = (int)(w & 0xFF);
	W.waiting_cmd = cmd;
	W.resp_ready = 0;
	W.txq_w = (w + 1) & (TFD_QUEUE_MAX - 1);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	wr32(HBUS_TARG_WRPTR, W.txq_w | (0u << 16));
	idbg("commande %x.%02x (%u octets) n° %u\n", grp, cmd, len, w & 0xFF);
	long end = now_ms() + timeout_ms;
	while (!W.resp_ready) {
		long left = end - now_ms();
		if (left <= 0) {
			W.waiting_seq = -1;
			ilog("ANOMALIE : pas de réponse à la commande %x.%02x en %ld ms\n", grp, cmd, timeout_ms);
			return E_TIMEDOUT;
		}
		if (service(left < 20 ? left : 20) == E_FWERR) {
			W.waiting_seq = -1;
			return E_FWERR;
		}
	}
	return 0;
}

/* ---- journal des registres et de l'erreur du micrologiciel */
void dump_regs(const char *why)
{
	ilog("registres (%s) : HW_REV %08x RF_ID %08x GP_CNTRL %08x RESET %08x HW_IF_CONFIG %08x INT %08x INT_MASK %08x "
	     "FH_INT %08x\n",
	     why, rd32(CSR_HW_REV), rd32(CSR_HW_RF_ID), rd32(CSR_GP_CNTRL), rd32(CSR_RESET), rd32(CSR_HW_IF_CONFIG),
	     rd32(CSR_INT), rd32(CSR_INT_MASK), rd32(CSR_FH_INT_STATUS));
	ilog("registres : GP1 %08x GP2 %08x GIO %08x MSIX_HW %08x LTR_LAST %08x CTXT_INFO_BA %08x%08x\n",
	     rd32(CSR_UCODE_DRV_GP1), rd32(CSR_UCODE_DRV_GP2), rd32(CSR_GIO), rd32(CSR_MSIX_HW_INT_CAUSES),
	     rd32(CSR_LTR_LAST_MSG), rd32(CSR_CTXT_INFO_BA + 4), rd32(CSR_CTXT_INFO_BA));
	if (!nic_grab()) {
		ilog("démarrage sécurisé : CPU1 %08x CPU2 %08x GEN_HW %08x ; PC UMAC %08x LMAC %08x ; PD LMAC %08x ARC %08x ; "
		     "RFH %08x\n",
		     prph_rd(UMAG_SB_CPU_1_STATUS), prph_rd(UMAG_SB_CPU_2_STATUS), prph_rd(UMAG_GEN_HW_STATUS),
		     prph_rd(UREG_UMAC_CURRENT_PC), prph_rd(UREG_LMAC1_CURRENT_PC), prph_rd(WFPM_LMAC1_PD_NOTIFICATION),
		     prph_rd(WFPM_ARC1_PD_NOTIFICATION), prph_rd(RFH_GEN_STATUS));
		nic_release();
	}
	ilog("réception : RB fermés %u, lus %u, écrits %u ; commandes : écriture %u, paquets %u, notifications %u\n",
	     W.rb_stts ? (u32)(le16(W.rb_stts) & 0xFFF) : 0, W.rx_read, W.rx_write, W.txq_w, W.n_rx_pkts, W.n_notif);
}

void dump_fw_error(void)
{
	u32 t[24];
	if (W.ai.umac_err && !mem_read(W.ai.umac_err, t, 15))
		ilog("erreur UMAC : valid %08x id %08x blink1 %08x blink2 %08x ilink1 %08x ilink2 %08x data %08x %08x %08x "
		     "ver %u.%u cmd %08x isr %08x\n",
		     t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], t[10], t[13], t[14]);
	if (W.ai.lmac_err && !mem_read(W.ai.lmac_err, t, 24))
		ilog("erreur LMAC : valid %08x id %08x trm %08x %08x blink2 %08x ilink %08x %08x data %08x %08x %08x "
		     "gp2 %08x ver %u.%u hw %08x pc %08x hcmd %08x\n",
		     t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[7], t[8], t[9], t[14], t[16], t[17], t[18], t[20], t[23]);
	if (!W.ai.umac_err && !W.ai.lmac_err)
		ilog("pas d'adresse des journaux d'erreur (vivant pas reçu)\n");
}

/* ---- arrêt : la puce rendue comme au départ, DMA coupé avant que la mémoire ne soit rendue */
void trans_stop(int fw_running)
{
	if (!W.mmio)
		return;
	if (fw_running && !W.dead && fw_capa(&W.fw, CAPA_FW_RESET_HANDSHAKE)) {
		wr32(CSR_MSIX_HW_INT_CAUSES, MSIX_HW_RESET_DONE);
		prph_write(UREG_NIC_SET_NMI_DRIVER, UREG_NMI_RESET_HANDSHAKE);
		int r = poll_bits(CSR_MSIX_HW_INT_CAUSES, MSIX_HW_RESET_DONE, MSIX_HW_RESET_DONE, 200000);
		ilog("arrêt : poignée de main de remise à zéro %s\n", r ? "sans réponse en 200 ms" : "faite");
	}
	disable_interrupts();
	if (W.device_enabled) {
		if (!nic_grab()) {
			prph_wr(RFH_RXF_DMA_CFG, 0);
			long end = now_ms() + 3;
			while (!(prph_rd(RFH_GEN_STATUS) & RXF_DMA_IDLE) && now_ms() < end)
				udelay(10);
			nic_release();
		}
	}
	W.device_enabled = 0;
	set_bits(CSR_RESET, RESET_STOP_MASTER);
	int master = poll_bits(CSR_RESET, RESET_MASTER_DISABLED, RESET_MASTER_DISABLED, 100);
	if (master)
		ilog("arrêt : le maître du bus ne s'arrête pas (CSR_RESET %08x)\n", rd32(CSR_RESET));
	sw_reset(0);
	clear_bits(CSR_GP_CNTRL, GP_INIT_DONE);
	sw_reset(1);
	disable_interrupts();
	clear_bits(CSR_GP_CNTRL, GP_MAC_ACCESS_REQ);
	W.fw_loaded = 0;
	ilog("puce arrêtée (%s, remise à zéro faite) : CSR_RESET %08x GP_CNTRL %08x\n",
	     master ? "DMA : arrêt non confirmé" : "DMA coupé", rd32(CSR_RESET), rd32(CSR_GP_CNTRL));
}
