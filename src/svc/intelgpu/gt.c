/* gt.c : le GT de Tiger Lake mis en état pour le rendu 3D (étape 2) : table
 * PAT du GPU, MOCS (états de cache que les index d'iris supposent), L3,
 * contournements (« workarounds ») du GT, du moteur de rendu et du contexte,
 * interruptions MSI du moteur de rendu.
 *
 * Les valeurs viennent du PRM public de Tiger Lake (« Memory Views », «
 * Workarounds ») ; la liste a été recoupée avec ce que programme i915 sur
 * Tiger Lake (B0 et suivantes) et avec les index que Mesa (isl.c) choisit :
 * décrite ici et réécrite, sans en reprendre le code.
 *
 * Rien dans l'affichage (0x40000-0x7FFFF... : DISPLAY_INT_CTL est seulement
 * lu pour le journal). */
#include "igpu.h"

/* ---- PAT du GPU (Gen12) : 0 WB, 1 WC, 2 WT, 3 UC, 4-7 WB. Les PTE du service et des clients prennent l'index
 * 0 (WB : le LLC est partagé avec les cœurs, docs/etude/GPU-INTEL.md § 8). Le ZBook rend 3 partout au départ :
 * l'index 0 ne change pas, donc rien ne change pour le GOP (la GGTT de Gen12 n'a pas de bits PAT : index 0). */
static const u32 pat[8] = {PAT_WB, PAT_WC, PAT_WT, PAT_UC, PAT_WB, PAT_WB, PAT_WB, PAT_WB};

/* ---- MOCS de Tiger Lake. Une entrée : le registre global (LLC : cacheabilité 1:0 — 0 d'après la PTE, 1 UC,
 * 2 WT, 3 WB —, cible 3:2 — 1 LLC —, LRU 5:4, AOM 6, RSC 7, SCC 10:8, SSE 18:17) et sa moitié de registre L3
 * (cacheabilité 5:4 : 1 UC, 3 WB). Les index que Mesa emploie sur Gen12 LP (isl.c) : 2 interne (LLC WB, L3 WB),
 * 3 non caché, 61 « affichable » (LLC UC, L3 WB), 48 (L1 + L3 + LLC) ; les autres décrits par le PRM. Une
 * entrée non décrite prend la valeur de l'entrée 1 (d'après la PTE, L3 UC). */
#define LE_WB 3u
#define LE_UC 1u
#define LE_TC_LLC (1u << 2)
#define LE_LRUM(x) ((u32)(x) << 4)
#define LE_AOM (1u << 6)
#define LE_RSC (1u << 7)
#define LE_SCC(x) ((u32)(x) << 8)
#define LE_SSE(x) ((u32)(x) << 17)
#define L3_UC (1u << 4)
#define L3_WB (3u << 4)
#define WB_LLC (LE_WB | LE_TC_LLC)

static const struct {
	u8 i;
	u32 le;
	u16 l3;
} mocs[] = {
	{1, 0, L3_UC},                                  /* d'après la PTE */
	{2, WB_LLC | LE_LRUM(3), L3_WB},                /* L3 + LLC (interne de Mesa) */
	{3, LE_UC | LE_TC_LLC, L3_UC},                  /* non caché */
	{4, LE_UC | LE_TC_LLC, L3_WB},                  /* L3 */
	{5, WB_LLC | LE_LRUM(3), L3_UC},                /* LLC */
	{6, WB_LLC | LE_LRUM(1), L3_UC},                /* âge 0, LLC */
	{7, WB_LLC | LE_LRUM(1), L3_WB},
	{8, WB_LLC | LE_LRUM(2), L3_UC},                /* âge inchangé */
	{9, WB_LLC | LE_LRUM(2), L3_WB},
	{10, WB_LLC | LE_LRUM(3) | LE_AOM, L3_UC},      /* sans AOM */
	{11, WB_LLC | LE_LRUM(3) | LE_AOM, L3_WB},
	{12, WB_LLC | LE_LRUM(1) | LE_AOM, L3_UC},
	{13, WB_LLC | LE_LRUM(1) | LE_AOM, L3_WB},
	{14, WB_LLC | LE_LRUM(2) | LE_AOM, L3_UC},
	{15, WB_LLC | LE_LRUM(2) | LE_AOM, L3_WB},
	{18, WB_LLC | LE_LRUM(3) | LE_SSE(3), L3_WB},   /* auto-espionnage */
	{19, WB_LLC | LE_SCC(7), L3_WB},                /* saut du LLC : 12,5 % */
	{20, WB_LLC | LE_SCC(3), L3_WB},                /* 25 % */
	{21, WB_LLC | LE_SCC(1), L3_WB},                /* 50 % */
	{22, WB_LLC | LE_LRUM(3) | LE_RSC | LE_SCC(3), L3_WB}, /* 75 % */
	{23, WB_LLC | LE_LRUM(3) | LE_RSC | LE_SCC(7), L3_WB}, /* 87,5 % */
	{48, WB_LLC | LE_LRUM(3), L3_WB},               /* L1 + L3 + LLC */
	{49, LE_UC | LE_TC_LLC, L3_WB},                 /* L1 + L3 */
	{50, WB_LLC | LE_LRUM(3), L3_UC},               /* L1 + LLC */
	{51, LE_UC | LE_TC_LLC, L3_UC},                 /* L1 */
	{60, WB_LLC | LE_LRUM(3), L3_UC},               /* cas du matériel (CCS) */
	{61, LE_UC | LE_TC_LLC, L3_WB},                 /* affichable (externe de Mesa) */
	{62, WB_LLC | LE_LRUM(3), L3_UC},               /* réservées au matériel : programmées, jamais servies */
	{63, WB_LLC | LE_LRUM(3), L3_UC},
};

static void mocs_entry(int i, u32 *le, u32 *l3)
{
	*le = 0;
	*l3 = L3_UC;
	for (unsigned k = 0; k < sizeof mocs / sizeof mocs[0]; k++)
		if (mocs[k].i == i) {
			*le = mocs[k].le;
			*l3 = mocs[k].l3;
		}
}

static void wr_or(u32 off, u32 bits)
{
	wr(off, rd(off) | bits);
}

static void wr_clr(u32 off, u32 bits)
{
	wr(off, rd(off) & ~bits);
}

/* les contournements du GT et du moteur de rendu : registres hors du contexte, écrits par MMIO une fois (et
 * après une remise à zéro du moteur, qui les efface) */
static void engine_wa(void)
{
	/* Wa_1606700617 : pas de coupure d'horloge du DOP des fonctions fixes */
	wr(CS_DEBUG_MODE1, MASKED_ON(1u << 1));
	/* Wa_1409804808 : ROW_CHICKEN2, pas de « push constant deref hold » */
	wr(ROW_CHICKEN2, MASKED_ON(1u << 8));
	/* Wa_1606931601 : ROW_CHICKEN2, pas de lecture anticipée */
	wr(ROW_CHICKEN2, MASKED_ON(1u << 14));
	/* Wa_14010229206 : ROW_CHICKEN4, pas de « TDL push » */
	wr(ROW_CHICKEN4, MASKED_ON(1u << 9));
	/* Wa_1406941453 : SAMPLER_MODE, « small PL » permis */
	wr(SAMPLER_MODE, MASKED_ON(1u << 15));
	/* Wa_18011464164, Wa_22010931296 : pas de coupure d'horloge du DOP du pavage (FF_THREAD_MODE, non masqué) */
	wr_or(FF_THREAD_MODE, 1u << 19);
	/* Wa_1607297627, Wa_1607030317, Wa_1607186500 : PSMI_CTL du rendu, ni mise en veille sur MI_WAIT_FOR_EVENT
	 * ni message de repos des sémaphores */
	wr(RCS + R_PSMI_CTL, MASKED_ON((1u << 7) | (1u << 12)));
}

static void gt_wa(void)
{
	/* Wa_14011059788 : DFR coupé */
	wr_or(DFR_RATIO_EN_AND_CHICKEN, 1u << 9);
	/* Wa_14015795083 : pas de coupure d'horloge du DOP du rendu (MISCCPCTL bit 1) */
	wr_clr(MISCCPCTL, 1u << 1);
}

/* les contournements du contexte : dans le contexte doré (premier lot, privilégié), donc dans chaque contexte
 * copié de lui. Des MI_LOAD_REGISTER_IMM -> mots écrits */
u32 ctx_wa_emit(volatile u32 *cs)
{
	static const u32 regs[][2] = {
		/* Wa_1409142259, Wa_1409347922, Wa_1409252684, Wa_1409217633, Wa_1409207793, Wa_1409178076,
		 * Wa_1408979724, Wa_14010443199, Wa_14010698770 : COMMON_SLICE_CHICKEN3, « CPS aware color pipe »
		 * coupé */
		{COMMON_SLICE_CHICKEN3, MASKED_ON(1u << 9)},
		/* pas de préemption au milieu d'un fil de calcul : au niveau du groupe (CS_CHICKEN1 2:1 = 1) */
		{CS_CHICKEN1, (6u << 16) | (1u << 1)},
		/* Wa_16011163337 et le minuteur du TDS (Wa_1604555607) : FF_MODE2, GS 224 (31:24), TDS 128 (23:16 = 4).
		 * Lu par le processeur, ce registre ment (Wa_1608008084) : écrit en entier, jamais relu */
		{FF_MODE2, (224u << 24) | (4u << 16)},
		/* Wa_1806527549 : HIZ_CHICKEN, pas d'optimisation du test de profondeur LE/GE */
		{HIZ_CHICKEN, MASKED_ON(1u << 13)},
		/* Wa_1606376872 : COMMON_SLICE_CHICKEN4, pas d'équilibrage de charge du TDC */
		{COMMON_SLICE_CHICKEN4, MASKED_ON(1u << 6)},
		/* Wa_1508744258 : COMMON_SLICE_CHICKEN1, pas d'optimisation RHWO */
		{COMMON_SLICE_CHICKEN1, MASKED_ON(1u << 14)},
	};
	u32 n = 0, k = sizeof regs / sizeof regs[0];
	cs[n++] = MI_LRI(k);
	for (u32 i = 0; i < k; i++) {
		cs[n++] = regs[i][0];
		cs[n++] = regs[i][1];
	}
	cs[n++] = MI_NOOP;
	return n;
}

void gt_init(void)
{
	char b[200];
	int n = 0;
	for (int i = 0; i < 8; i++) {
		u32 o = rd(PAT_INDEX(i));
		if (o != pat[i])
			wr(PAT_INDEX(i), pat[i]);
		n += snprintf(b + n, sizeof b - n, " %x", rd(PAT_INDEX(i)));
	}
	glog("PAT du GPU (0 WB, 1 WC, 2 WT, 3 UC, 4-7 WB) :%s\n", b);
	/* MOCS : 64 entrées globales, 32 registres L3 (deux entrées chacun) */
	int changed = 0;
	for (int i = 0; i < 64; i++) {
		u32 le, l3;
		mocs_entry(i, &le, &l3);
		changed += rd(GLOBAL_MOCS(i)) != le;
		wr(GLOBAL_MOCS(i), le);
	}
	for (int i = 0; i < 32; i++) {
		u32 a, b2, la, lb;
		mocs_entry(2 * i, &a, &la);
		mocs_entry(2 * i + 1, &b2, &lb);
		wr(LNCF_MOCS(i), la | lb << 16);
	}
	glog("MOCS de Tiger Lake chargés (%d entrées globales différentes de celles du micrologiciel) ; L3 chargé\n",
	     changed);
	gt_wa();
	engine_wa();
	glog("contournements : GT 2, moteur de rendu 7 (MMIO), contexte 6 (dans le contexte doré)\n");
}

/* ---- interruptions : une MSI, maître du GT, rendu seulement (contexte changé, interruption utilisateur, erreur
 * du CS). Le bit « affichage » du maître n'est jamais servi : ni lu ni effacé dans l'affichage. */

static int irq_on;

void gt_irq_setup(void)
{
	if (S.irq == -2 || G.simu) {
		glog("interruptions : sondage (%s)\n", G.simu ? "simu" : "-p");
		S.irq = -1;
		return;
	}
	S.irq = -1;
	glog("affichage (lecture) : DISPLAY_INT_CTL %08x, maître %08x\n", rd(DISPLAY_INT_CTL), rd(GFX_MSTR_IRQ));
	long line;
	long n = pci_irq_setup(&G.pci, 1, &line);
	if (n < 1) {
		glog("pas de MSI (%ld) : sondage\n", n);
		return;
	}
	long r = korin_sys3(SYS_IRQ_BIND, line, -1, 0);
	if (r < 0) {
		glog("IRQ_BIND refusé (%ld) : sondage\n", r);
		return;
	}
	u32 en = GT_USER_INTERRUPT | GT_CONTEXT_SWITCH | GT_CS_MASTER_ERROR;
	wr(GFX_MSTR_IRQ, 0);
	wr(RENDER_COPY_INTR_ENABLE, en << 16);          /* rendu seulement (le copieur : 15:0 à 0) */
	wr(RCS0_RSVD_INTR_MASK, ~(en << 16) | 0xFFFFu); /* RCS0 démasqué ; l'autre moitié reste masquée */
	wr(RCS + R_IMR, ~en);
	wr(GT_INTR_DW0, rd(GT_INTR_DW0));               /* ce qui traînait */
	wr(GFX_MSTR_IRQ, MSTR_IRQ_ENABLE);
	S.irq = line;
	irq_on = 1;
	glog("interruption MSI sur la ligne %ld (rendu : fin de lot, contexte changé, erreur)\n", line);
}

void gt_irq_off(void)
{
	if (!irq_on)
		return;
	wr(GFX_MSTR_IRQ, 0);
	wr(RENDER_COPY_INTR_ENABLE, 0);
	wr(RCS0_RSVD_INTR_MASK, 0xFFFFFFFF);
	wr(RCS + R_IMR, 0xFFFFFFFF);
	korin_sys1(SYS_IRQ_UNBIND, S.irq);
	irq_on = 0;
	S.irq = -1;
}

/* le maître coupé, les sources lues et effacées, le maître rallumé -> bits du rendu (0 : rien pour nous) */
u32 gt_irq_ack(void)
{
	if (!irq_on)
		return 0;
	wr(GFX_MSTR_IRQ, 0);
	u32 m = rd(GFX_MSTR_IRQ), bits = 0;
	if (m & MSTR_GT_DW0) {
		u32 dw = rd(GT_INTR_DW0);
		if (dw & 1) { /* RCS0 */
			wr(IIR_SELECTOR0, 1);
			u32 id = 0;
			for (int i = 0; i < 100; i++) {
				id = rd(INTR_IDENTITY0);
				if (id & (1u << 31))
					break;
			}
			if (id & (1u << 31)) {
				bits = id & 0xFFFF;
				wr(INTR_IDENTITY0, id);
			}
		}
		if (dw)
			wr(GT_INTR_DW0, dw);
	}
	if (!bits)
		S.spurious++;
	wr(GFX_MSTR_IRQ, MSTR_IRQ_ENABLE);
	return bits;
}

/* après une remise à zéro du moteur : ses contournements sont effacés */
void gt_engine_rewa(void)
{
	engine_wa();
}
