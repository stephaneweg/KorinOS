/* submit.c : un lot minimal soumis au moteur de rendu par execlists (sans
 * GuC), option « submit » de la sonde (docs/etude/GPU-INTEL.md, § 6).
 *
 * Une zone DMA contiguë à nous (64 pages), vue du GPU par 64 entrées de la
 * GGTT choisies hors de tout ce que le micrologiciel y a mis (ggtt_window) :
 *
 *	page 0      page d'état du moteur (HWSP) ; l'anneau y écrit MAGIC_RING en +0x100
 *	pages 1-4   l'anneau (16 Ko) : MI_BATCH_BUFFER_START (GGTT) puis MI_STORE_DATA_IMM
 *	page 5      le lot : MI_STORE_DATA_IMM (MAGIC_BATCH vers la page 6), MI_BATCH_BUFFER_END
 *	page 6      la cible
 *	page 7      une PML4 vide (le contexte est en mode PPGTT à 4 niveaux ; rien n'y est lu)
 *	pages 8-63  le contexte : page par processus (PPHWSP), puis l'état des registres
 *
 * Le contexte est neuf : « restauration du moteur interdite » (comme le premier
 * contexte d'un pilote), seule la partie « anneau » de son image est écrite
 * (en-têtes MI_LOAD_REGISTER_IMM et adresses des registres d'après le PRM,
 * « Execlist Context »), close par MI_BATCH_BUFFER_END. Puis : attente du
 * résultat (500 ms au plus), arrêt de l'anneau, registres du moteur remis comme
 * au départ, entrées de la GGTT rendues. Moteur bloqué : remise à zéro du
 * domaine de rendu seul (GDRST bit 1 : jamais l'affichage). */
#include "igpu.h"

#define NPAGES 64
#define P_HWSP 0
#define P_RING 1
#define RING_BYTES (4 * 4096)
#define P_BATCH 5
#define P_TARGET 6
#define P_PML4 7
#define P_CTX 8
#define HWSP_SEQ (0x100 / 4)
#define HWSP_CSB 0x10      /* Gen11+ : 12 entrées de 2 mots */
#define HWSP_CSB_WP 0x2F   /* pointeur d'écriture */
#define MAGIC_BATCH 0x4B4F5249u /* « IROK » */
#define MAGIC_RING 0x31555047u  /* « GPU1 » */

static volatile u32 *dma;
static u64 dma_phys;
static long win = -1;
static u64 old_pte[NPAGES];
static int ptes_set;

static u32 ga(int page) /* adresse d'une page vue du GPU (GGTT) */
{
	return (u32)((u64)(win + page) * 4096);
}

static volatile u32 *pg(int page)
{
	return dma + page * 1024;
}

/* simu.c lit nos pages par leur adresse physique */
void *submit_phys_to_va(u64 phys)
{
	if (phys < dma_phys || phys >= dma_phys + NPAGES * 4096)
		return 0;
	return (u8 *)dma + (phys - dma_phys);
}

static int ggtt_install(void)
{
	win = ggtt_window(NPAGES);
	if (win < 0) {
		glog("ANOMALIE : pas de fenêtre libre de %d entrées dans la GGTT\n", NPAGES);
		return -1;
	}
	for (int i = 0; i < NPAGES; i++)
		old_pte[i] = gtt_rd((u64)win + i);
	glog("fenêtre de la GGTT : entrées %lx..%lx (adresses GPU %x..%x), anciennes valeurs %lx..%lx\n",
	     (unsigned long)win, (unsigned long)win + NPAGES - 1, ga(0), ga(NPAGES) - 1, (unsigned long)old_pte[0],
	     (unsigned long)old_pte[NPAGES - 1]);
	ptes_set = 1;
	for (int i = 0; i < NPAGES; i++)
		gtt_wr((u64)win + i, (dma_phys + (u64)i * 4096) | 1); /* présente ; PAT 0 du GPU */
	for (int i = 0; i < NPAGES; i++) {
		u64 v = gtt_rd((u64)win + i);
		if (v != ((dma_phys + (u64)i * 4096) | 1)) {
			glog("ANOMALIE : GGTT[%lx] relue %lx\n", (unsigned long)win + i, (unsigned long)v);
			return -1;
		}
	}
	wr(GFX_FLSH_CNTL, 1); /* TLB de la GGTT */
	(void)rd(GFX_FLSH_CNTL);
	return 0;
}

static void ggtt_restore(void)
{
	if (!ptes_set)
		return;
	for (int i = 0; i < NPAGES; i++)
		gtt_wr((u64)win + i, old_pte[i]);
	int bad = 0;
	for (int i = 0; i < NPAGES; i++)
		bad += gtt_rd((u64)win + i) != old_pte[i];
	wr(GFX_FLSH_CNTL, 1);
	(void)rd(GFX_FLSH_CNTL);
	ptes_set = 0;
	glog("GGTT : %d entrées remises comme avant%s\n", NPAGES, bad ? " (ANOMALIE : relues autrement)" : "");
}

static void fill(void)
{
	volatile u32 *r = pg(P_RING), *b = pg(P_BATCH);
	int n = 0;
	r[n++] = MI_BB_START; /* bit 8 à 0 : GGTT, lot privilégié */
	r[n++] = ga(P_BATCH);
	r[n++] = 0;
	r[n++] = MI_SDI | MI_USE_GGTT;
	r[n++] = ga(P_HWSP) + HWSP_SEQ * 4;
	r[n++] = 0;
	r[n++] = MAGIC_RING;
	r[n++] = MI_NOOP; /* la queue sur 8 octets */
	u32 tail = (u32)n * 4;

	b[0] = MI_SDI | MI_USE_GGTT;
	b[1] = ga(P_TARGET);
	b[2] = 0;
	b[3] = MAGIC_BATCH;
	b[4] = MI_BB_END;
	b[5] = MI_NOOP;

	/* l'image du contexte : page 1 du contexte = l'état des registres */
	volatile u32 *s = pg(P_CTX + 1);
	u32 lri = MI_LRI_POSTED | MI_LRI_CS_MMIO;
	int i = 0;
#define REG(off, val) (s[i++] = RCS + (off), s[i++] = (val))
	s[i++] = MI_NOOP;
	s[i++] = MI_LRI(13) | lri;
	REG(R_CTX_CTL, MASKED_ON(1u << 3) | MASKED_ON(1u << 0)); /* changement synchrone interdit, restauration
								  * du moteur interdite : contexte neuf */
	REG(R_HEAD, 0);
	REG(R_TAIL, tail);
	REG(R_START, ga(P_RING));
	REG(R_CTL, ((RING_BYTES - 4096) & 0x1FF000) | 1);
	REG(R_BBADDR_UDW, 0);
	REG(R_BBADDR, 0);
	REG(R_BBSTATE, 1u << 5);
	REG(0x1C0, 0); /* lot par contexte */
	REG(0x1C4, 0); /* contexte indirect */
	REG(0x1C8, 0); /* son décalage */
	REG(R_CCID, 0);
	REG(0x2B4, 0);
	for (int k = 0; k < 5; k++)
		s[i++] = MI_NOOP;
	s[i++] = MI_LRI(9) | lri;
	u64 pml4 = dma_phys + P_PML4 * 4096;
	REG(0x3A8, 0); /* horodatage du contexte */
	REG(0x28C, 0); /* PDP3 */
	REG(0x288, 0);
	REG(0x284, 0); /* PDP2 */
	REG(0x280, 0);
	REG(0x27C, 0); /* PDP1 */
	REG(0x278, 0);
	REG(0x274, (u32)(pml4 >> 32)); /* PDP0 = la PML4 (vide) */
	REG(0x270, (u32)pml4);
	s[i++] = MI_LRI(3) | lri;
	REG(0x1B0, 0);
	REG(0x5A8, 0);
	REG(0x5AC, 0);
	for (int k = 0; k < 6; k++)
		s[i++] = MI_NOOP;
	s[i++] = MI_LRI(1) | MI_LRI_CS_MMIO;
	REG(0x0C8, 0); /* R_PWR_CLK_STATE : 0, la configuration des EU par défaut */
	for (int k = 0; k < 13; k++)
		s[i++] = MI_NOOP;
	s[i++] = MI_BB_END | 1; /* fin de l'image restaurée */
#undef REG
	gdbg("image du contexte : %d mots, queue de l'anneau %u\n", i, tail);
	clflush_range(dma, NPAGES * 4096);
}

static int engine_reset(void)
{
	u32 v;
	glog("remise à zéro du moteur de rendu (domaine du rendu seul)\n");
	wr(RCS + R_RESET_CTL, MASKED_ON(1u));
	if (wait_bits(RCS + R_RESET_CTL, 2u, 2u, 50, &v))
		glog("le moteur ne se dit pas prêt (RESET_CTL %08x) : remise à zéro quand même\n", v);
	wr(GDRST, GDRST_RENDER);
	int r = wait_bits(GDRST, GDRST_RENDER, 0, 100, &v);
	wr(RCS + R_RESET_CTL, MASKED_OFF(1u));
	if (r) {
		glog("ANOMALIE : GDRST %08x : la remise à zéro ne finit pas\n", v);
		return -1;
	}
	glog("remise à zéro finie\n");
	return 0;
}

/* l'anneau arrêté, le moteur au repos -> 0 */
static int engine_stop(void)
{
	u32 v;
	wr(RCS + R_MI_MODE, MASKED_ON(MI_MODE_STOP));
	if (wait_bits(RCS + R_MI_MODE, MI_MODE_IDLE, MI_MODE_IDLE, 100, &v)) {
		glog("le moteur ne s'arrête pas en 100 ms (MI_MODE %08x)\n", v);
		return -1;
	}
	return 0;
}

/* les registres touchés, comme au départ (le moteur arrêté) */
static void engine_restore(const struct engine_regs *e)
{
	if (!(e->mode & MODE_NO_LEGACY))
		wr(RCS + R_MODE, MASKED_OFF(MODE_NO_LEGACY));
	/* l'anneau éteint : ses registres viennent maintenant de notre contexte */
	wr(RCS + R_CTL, 0);
	wr(RCS + R_HEAD, 0);
	wr(RCS + R_TAIL, 0);
	wr(RCS + R_START, e->start);
	wr(RCS + R_HWS_PGA, e->hws);
	wr(RCS + R_HWSTAM, e->hwstam);
	if (!(e->mi_mode & MI_MODE_STOP))
		wr(RCS + R_MI_MODE, MASKED_OFF(MI_MODE_STOP));
	(void)rd(RCS + R_HWS_PGA);
}

static void report(void)
{
	volatile u32 *h = pg(P_HWSP);
	clflush_range(h, 4096);
	char b[300];
	int n = 0;
	for (int i = 0; i < 12; i++)
		n += snprintf(b + n, sizeof b - n, " %x:%x", h[HWSP_CSB + 2 * i], h[HWSP_CSB + 2 * i + 1]);
	glog("CSB (page d'état, écriture en %u) :%s\n", h[HWSP_CSB_WP], b);
	engine_dump("après le lot");
}

int submit_test(void)
{
	struct engine_regs e0;
	engine_read(&e0);
	if (e0.ctl & 1) {
		glog("ANOMALIE : l'anneau du rendu est allumé (CTL %08x) : quelqu'un s'en sert, pas de soumission\n",
		     e0.ctl);
		return -1;
	}
	long va = korin_sys2(SYS_DMA_ALLOC, NPAGES * 4096, &dma_phys);
	if (va < 0) {
		glog("ANOMALIE : DMA_ALLOC de %d Ko refusé (%ld)\n", NPAGES * 4, va);
		return -1;
	}
	dma = (volatile u32 *)va;
	glog("zone DMA : %d Ko contigus à %lx (mémoire du système, pas le framebuffer)\n", NPAGES * 4,
	     (unsigned long)dma_phys);
	if (ggtt_install()) {
		ggtt_restore();
		return -1;
	}
	fill();

	/* execlists : page d'état, plus de mode « anneau », anneau autorisé */
	wr(RCS + R_HWSTAM, 0xFFFFFFFF);
	wr(RCS + R_MODE, MASKED_ON(MODE_NO_LEGACY));
	wr(RCS + R_MI_MODE, MASKED_OFF(MI_MODE_STOP));
	wr(RCS + R_HWS_PGA, ga(P_HWSP));
	(void)rd(RCS + R_HWS_PGA);
	engine_dump("prêt");

	u64 desc = (u64)ga(P_CTX) | CTX_VALID | CTX_ADDR_64B | CTX_PRIVILEGE | (1ull << CTX_SW_ID_SHIFT);
	glog("soumission : descripteur %08x:%08x\n", (u32)(desc >> 32), (u32)desc);
	long t0 = now_ms();
	wr(RCS + R_EL_SQ + 8, 0); /* port 1 vide */
	wr(RCS + R_EL_SQ + 12, 0);
	wr(RCS + R_EL_SQ + 0, (u32)desc);
	wr(RCS + R_EL_SQ + 4, (u32)(desc >> 32));
	wr(RCS + R_EL_CTRL, 1);

	volatile u32 *t = pg(P_TARGET), *h = pg(P_HWSP);
	int got_b = 0, got_r = 0;
	long dt = 0;
	while ((dt = now_ms() - t0) <= 500) {
		clflush_range(t, 64);
		clflush_range(h + HWSP_SEQ, 64);
		got_b = t[0] == MAGIC_BATCH;
		got_r = h[HWSP_SEQ] == MAGIC_RING;
		if (got_b && got_r)
			break;
		korin_sys1(SYS_SLEEP, 1);
	}
	int ok = got_b && got_r;
	if (ok)
		glog("RÉUSSI : le lot a écrit %08x, l'anneau %08x, en %ld ms\n", t[0], h[HWSP_SEQ], dt);
	else
		glog("ÉCHEC : rien en 500 ms (cible %08x, page d'état %08x)\n", t[0], h[HWSP_SEQ]);
	report();

	int stuck = engine_stop();
	if (stuck || !ok) {
		if (engine_reset() || engine_stop()) {
			/* les pages et la GGTT restent : le GPU pourrait encore y écrire */
			return -2;
		}
	}
	engine_restore(&e0);
	engine_dump("à la fin");
	ggtt_restore();
	return ok ? 0 : -1;
}
