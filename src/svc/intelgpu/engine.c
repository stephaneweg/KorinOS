/* engine.c : le moteur de rendu (RCS0) par execlists, sans GuC (étape 2,
 * docs/etude/GPU-INTEL.md § 5).
 *
 * La GGTT du service : une fenêtre de SV_GGTT_PAGES entrées hors de tout ce
 * que le micrologiciel y a mis (ggtt_window, comme la sonde), anciennes
 * valeurs remises à la fin. On y met la page d'état du moteur (HWSP : CSB),
 * la page d'état partagée avec les clients (IGS_*, un tampon partagé ouvert
 * à tous), et pour chaque contexte son image (LRC : page par processus,
 * 22 pages d'état des registres) suivie de son anneau (16 Ko).
 *
 * Le contexte doré : le premier contexte (privilégié, au service) est écrit à
 * la main dans sa partie « anneau » avec « restauration interdite », comme la
 * sonde de l'étape 1 (réussie sur le ZBook) ; son anneau charge les
 * contournements du contexte (gt.c). Quand il a fini, le matériel a sauvé
 * l'image COMPLÈTE de l'état du rendu : on la garde, et tout contexte neuf
 * en est une copie (avec son anneau, sa PPGTT, restauration permise) : c'est
 * ainsi que fait i915 (« default state »).
 *
 * Un lot : dans l'anneau de son contexte, une invalidation (PIPE_CONTROL :
 * TLB, caches ; la table auxiliaire de la compression), le saut au lot du
 * client (MI_BATCH_BUFFER_START en PPGTT : non privilégié), un vidage des
 * caches, l'écriture de son numéro dans la page d'état partagée
 * (PIPE_CONTROL, adresse GGTT), MI_USER_INTERRUPT. Les numéros sont uniques
 * pour tout le moteur et croissent : les lots passent dans l'ordre des
 * numéros, un contexte à la fois (les lots qui se suivent d'un même contexte
 * partent ensemble) ; le suivant part quand le CSB dit que le moteur est au
 * repos (le contexte est sauvé). Délai sans progrès : remise à zéro du seul
 * moteur de rendu, les lots en vol sont perdus, leur contexte est banni (iris
 * en refait un), le reste continue. */
#include "igpu.h"

struct ctx ctxs[CTX_MAX];
struct svstate S;

static struct sub subs[SUB_MAX];
static int sub_head, sub_n;
static u8 banned[CTX_MAX];
static u32 done_tail[CTX_MAX];
static int hang_row;
static long idle_wait_since;
static u64 t_submit, t_done; /* (µs) la soumission en cours : partie, fin vue */
int simu_hang_later; /* simu-hang avec « test » : le faux moteur se bloque à l'essai 3 seulement */

#define ST_CTX_CTL 3
#define ST_HEAD 5
#define ST_TAIL 7
#define ST_START 9
#define ST_CTL 11
#define ST_BB_STATE 17
#define ST_TIMESTAMP 35
#define ST_PDP0_UDW 49
#define ST_PDP0_LDW 51
#define RING_CTL_VAL (((CTX_RING_BYTES - 4096) & 0x1FF000) | 1)

int seq_done(u32 seq)
{
	return (int32_t)(S.status[IGS_DONE] - seq) >= 0;
}

/* ---- la fenêtre de la GGTT */

static void ggtt_flush(void)
{
	wr(GFX_FLSH_CNTL, 1);
	(void)rd(GFX_FLSH_CNTL);
}

static long ggtt_alloc(int pages)
{
	for (int i = 0; i + pages <= SV_GGTT_PAGES; i++) {
		int k = 0;
		while (k < pages && !S.ggtt_used[i + k])
			k++;
		if (k == pages) {
			memset(&S.ggtt_used[i], 1, pages);
			return i;
		}
		i += k;
	}
	return -1;
}

/* pages contiguës à phys vues à l'indice i de la fenêtre -> adresse GGTT */
static u32 ggtt_map(long i, u64 phys, int pages)
{
	for (int k = 0; k < pages; k++)
		gtt_wr((u64)S.win + i + k, (phys + (u64)k * 4096) | 1);
	ggtt_flush();
	return (u32)(((u64)S.win + i) * 4096);
}

/* ---- l'image d'un contexte */

static volatile u32 *state_page(struct ctx *c)
{
	return c->va + 1024; /* page 1 de l'image */
}

static volatile u32 *ring_of(struct ctx *c)
{
	return c->va + CTX_IMAGE_PAGES * 1024;
}

static u32 ring_ggtt(struct ctx *c)
{
	return c->ggtt + CTX_IMAGE_PAGES * 4096;
}

/* la partie « anneau » d'une image neuve, restauration interdite (disposition du PRM, Gen12 RCS : celle de la
 * sonde, réussie sur le ZBook) */
static void image_inhibit(struct ctx *c, u64 pml4)
{
	volatile u32 *s = state_page(c);
	u32 lri = MI_LRI_POSTED | MI_LRI_CS_MMIO;
	int i = 0;
#define REG(off, val) (s[i++] = RCS + (off), s[i++] = (val))
	s[i++] = MI_NOOP;
	s[i++] = MI_LRI(13) | lri;
	REG(R_CTX_CTL, MASKED_ON(1u << 3) | MASKED_ON(1u << 0));
	REG(R_HEAD, 0);
	REG(R_TAIL, 0);
	REG(R_START, ring_ggtt(c));
	REG(R_CTL, RING_CTL_VAL);
	REG(R_BBADDR_UDW, 0);
	REG(R_BBADDR, 0);
	REG(R_BBSTATE, 1u << 5);
	REG(0x1C0, 0);
	REG(0x1C4, 0);
	REG(0x1C8, 0);
	REG(R_CCID, 0);
	REG(0x2B4, 0);
	for (int k = 0; k < 5; k++)
		s[i++] = MI_NOOP;
	s[i++] = MI_LRI(9) | lri;
	REG(0x3A8, 0);
	REG(0x28C, 0);
	REG(0x288, 0);
	REG(0x284, 0);
	REG(0x280, 0);
	REG(0x27C, 0);
	REG(0x278, 0);
	REG(0x274, (u32)(pml4 >> 32));
	REG(0x270, (u32)pml4);
	s[i++] = MI_LRI(3) | lri;
	REG(0x1B0, 0);
	REG(0x5A8, 0);
	REG(0x5AC, 0);
	for (int k = 0; k < 6; k++)
		s[i++] = MI_NOOP;
	s[i++] = MI_LRI(1) | MI_LRI_CS_MMIO;
	REG(0x0C8, 0);
	for (int k = 0; k < 13; k++)
		s[i++] = MI_NOOP;
	s[i++] = MI_BB_END | 1;
#undef REG
}

/* une copie du contexte doré : son anneau, sa PPGTT, restauration permise */
static void image_from_golden(struct ctx *c, u64 pml4)
{
	volatile u32 *s = state_page(c);
	for (int k = 0; k < 1024; k++)
		c->va[k] = 0; /* page par processus */
	for (int k = 0; k < 22 * 1024; k++)
		s[k] = S.golden[k];
	s[ST_CTX_CTL] = MASKED_ON(1u << 3) | MASKED_OFF(1u << 0);
	s[ST_HEAD] = 0;
	s[ST_TAIL] = 0;
	s[ST_START] = ring_ggtt(c);
	s[ST_CTL] = RING_CTL_VAL;
	s[ST_BB_STATE] = 1u << 5;
	s[ST_TIMESTAMP] = 0;
	s[ST_PDP0_UDW] = (u32)(pml4 >> 32);
	s[ST_PDP0_LDW] = (u32)pml4;
}

int ctx_create(int owner, int vm, int priv)
{
	int c;
	for (c = 0; c < CTX_MAX && ctxs[c].used; c++)
		;
	if (c == CTX_MAX) {
		glog("REFUS : plus de place pour un contexte (%d)\n", CTX_MAX);
		return E_NOMEM;
	}
	struct ctx *x = &ctxs[c];
	if (!x->va) { /* la place d'une image : gardée d'un contexte à l'autre */
		long g = ggtt_alloc(CTX_PAGES);
		if (g < 0) {
			glog("REFUS : contexte %d : plus de %d pages libres de suite dans la GGTT du service\n", c, CTX_PAGES);
			return E_NOMEM;
		}
		u64 phys;
		long va = korin_sys2(SYS_DMA_ALLOC, CTX_PAGES * 4096ul, &phys);
		if (va < 0) {
			glog("REFUS : contexte %d : DMA_ALLOC de %d pages refusé (%ld)\n", c, CTX_PAGES, va);
			memset(&S.ggtt_used[g], 0, CTX_PAGES);
			return E_NOMEM;
		}
		x->va = (volatile u32 *)va;
		x->phys = phys;
		x->ggtt = ggtt_map(g, phys, CTX_PAGES);
		if (G.simu)
			simu_note(phys, (void *)va, CTX_PAGES * 4096ull);
	}
	u64 pml4 = vm > 0 ? vms[vm].pml4 : scratch_phys();
	for (int k = CTX_IMAGE_PAGES * 1024; k < CTX_PAGES * 1024; k++)
		x->va[k] = 0;
	if (S.golden_ok)
		image_from_golden(x, pml4);
	else
		image_inhibit(x, pml4);
	clflush_range(x->va, CTX_PAGES * 4096);
	x->used = 1;
	x->owner = owner;
	x->vm = vm;
	x->priv = priv;
	x->tail = x->hw_tail = 0;
	x->last_seq = S.status[IGS_DONE];
	x->lost = 0;
	x->queued = 0;
	banned[c] = 0;
	done_tail[c] = 0;
	return c;
}

int eng_ctx_idle(int c)
{
	return !ctxs[c].queued && !(S.busy && S.running == c);
}

void ctx_destroy(int c)
{
	if (c < 0 || c >= CTX_MAX || !ctxs[c].used)
		return;
	ctxs[c].used = 0; /* (sa place, son image et son anneau restent pour le suivant) */
}

/* ---- l'anneau */

static u32 ring_space(int c)
{
	struct ctx *x = &ctxs[c];
	return (done_tail[c] - x->tail - 8) & (CTX_RING_BYTES - 1);
}

static void emit(struct ctx *x, u32 v)
{
	ring_of(x)[x->tail / 4] = v;
	x->tail = (x->tail + 4) & (CTX_RING_BYTES - 1);
}

static void emit_pc(struct ctx *x, u32 dw0, u32 flags, u32 addr, u32 data)
{
	emit(x, PIPE_CONTROL | dw0);
	emit(x, flags);
	emit(x, addr);
	emit(x, 0);
	emit(x, data);
	emit(x, 0);
}

#define RING_CMD_DW 32

/* la place pour n mots d'un seul tenant (fin de l'anneau bourrée de MI_NOOP) -> 0, E_AGAIN */
static int ring_begin(int c, u32 n)
{
	struct ctx *x = &ctxs[c];
	u32 to_end = CTX_RING_BYTES - x->tail;
	u32 need = n * 4 + (to_end < n * 4 ? to_end : 0);
	if (ring_space(c) < need)
		return E_AGAIN;
	if (to_end < n * 4)
		while (x->tail)
			emit(x, MI_NOOP);
	return 0;
}

static void emit_pre(struct ctx *x)
{
	u32 scratch = S.status_ggtt + IGS_SCRATCH * 4;
	emit_pc(x, 0,
		PC_CS_STALL | PC_TLB_INV | PC_INSTR_INV | PC_TEX_INV | PC_VF_INV | PC_CONST_INV | PC_STATE_INV |
			PC_CMD_INV | PC_WRITE_IMM | PC_GGTT,
		scratch, 0);
	emit(x, MI_LRI(1));
	emit(x, CCS_AUX_INV);
	emit(x, 1);
	emit(x, MI_NOOP);
}

static void emit_post(struct ctx *x, u32 seq)
{
	emit_pc(x, PC0_HDC_FLUSH,
		PC_CS_STALL | PC_TILE_FLUSH | PC_RT_FLUSH | PC_DEPTH_FLUSH | PC_DC_FLUSH | PC_PC_FLUSH, 0, 0);
	emit_pc(x, 0, PC_CS_STALL | PC_WRITE_IMM | PC_GGTT, S.status_ggtt + IGS_DONE * 4, seq);
	emit(x, MI_USER_INTERRUPT);
	emit(x, MI_ARB_ON_OFF | MI_ARB_ENABLE);
}

static long queue_sub(int c)
{
	if (sub_n == SUB_MAX)
		return E_AGAIN;
	u32 seq = ++S.next_seq;
	if (!seq)
		seq = ++S.next_seq;
	struct sub *s = &subs[(sub_head + sub_n++) % SUB_MAX];
	s->ctx = c;
	s->seq = seq;
	s->tail = ctxs[c].tail;
	s->t_queued = eng_us();
	ctxs[c].last_seq = seq;
	ctxs[c].queued++;
	S.status[IGS_SUBMITTED] = seq;
	return seq;
}

/* un lot (adresse GPU : PPGTT du contexte ; priv : GGTT, lot privilégié du service) -> numéro */
long eng_exec(int c, u64 batch, u32 len, int priv)
{
	(void)len;
	if (S.dead)
		return E_IO;
	if (c < 0 || c >= CTX_MAX || !ctxs[c].used)
		return E_INVAL;
	if (banned[c])
		return E_IO;
	struct ctx *x = &ctxs[c];
	long seq = (long)S.next_seq + 1;
	if (ring_begin(c, RING_CMD_DW))
		return E_AGAIN;
	emit_pre(x);
	batch &= GPU_VA_MASK;
	emit(x, MI_BB_START | (priv ? 0 : MI_BB_PPGTT));
	emit(x, (u32)batch);
	emit(x, (u32)(batch >> 32));
	emit(x, MI_NOOP);
	emit_post(x, (u32)seq);
	return queue_sub(c);
}

/* des commandes privilégiées dans l'anneau même (contexte du service) -> numéro */
static long exec_inline(int c, const u32 *cmds, u32 n)
{
	struct ctx *x = &ctxs[c];
	if (ring_begin(c, n + RING_CMD_DW))
		return E_AGAIN;
	emit_pre(x);
	for (u32 i = 0; i < n; i++)
		emit(x, cmds[i]);
	if (n & 1)
		emit(x, MI_NOOP);
	emit_post(x, S.next_seq + 1);
	return queue_sub(c);
}

/* ---- soumission, CSB, fins, délais */

static void submit_head(void)
{
	struct sub *h = &subs[sub_head];
	int c = h->ctx;
	struct ctx *x = &ctxs[c];
	u32 last = h->seq, tail = h->tail;
	u64 t = eng_us();
	eng_stat(ST_QUEUE, t - h->t_queued);
	for (int k = 1; k < sub_n; k++) {
		struct sub *s = &subs[(sub_head + k) % SUB_MAX];
		if (s->ctx != c)
			break;
		last = s->seq;
		tail = s->tail;
		eng_merged++;
		eng_stat(ST_QUEUE, t - s->t_queued);
	}
	t_submit = t;
	volatile u32 *st = state_page(x);
	if (S.golden_ok)
		st[ST_CTX_CTL] = MASKED_ON(1u << 3) | MASKED_OFF(1u << 0);
	st[ST_TAIL] = tail;
	clflush_range(st, 64 * 4);
	clflush_range(ring_of(x), CTX_RING_BYTES);
	u64 desc = (u64)x->ggtt | CTX_VALID | CTX_ADDR_64B | CTX_PRIVILEGE | ((u64)(c + 1) << CTX_SW_ID_SHIFT);
	gdbg("soumission : contexte %d, lots ..%u, queue %u, descripteur %08x:%08x\n", c, last, tail,
	     (u32)(desc >> 32), (u32)desc);
	wr(RCS + R_EL_SQ + 8, 0);
	wr(RCS + R_EL_SQ + 12, 0);
	wr(RCS + R_EL_SQ + 0, (u32)desc);
	wr(RCS + R_EL_SQ + 4, (u32)(desc >> 32));
	wr(RCS + R_EL_CTRL, 1);
	x->hw_tail = tail;
	S.busy = 1;
	S.running = c;
	S.run_last = last;
	S.run_since = S.progress_ms = now_ms();
	S.run_done_seen = 0;
	S.submits++;
	idle_wait_since = 0;
	t_done = 0;
}

/* les lots finis sortent de la file */
static void retire(void)
{
	while (sub_n) {
		struct sub *s = &subs[sub_head];
		if (!seq_done(s->seq))
			break;
		done_tail[s->ctx] = s->tail;
		ctxs[s->ctx].queued--;
		sub_head = (sub_head + 1) % SUB_MAX;
		sub_n--;
		S.progress_ms = now_ms();
		hang_row = 0;
	}
}

/* le CSB lu depuis notre dernière lecture -> 1 si le moteur est passé au repos */
static int csb_read(void)
{
	int idle = 0;
	clflush_range(S.hwsp + 0x10, 0x20 * 4);
	u32 wp = S.hwsp[0x2F] & 0xF;
	if (wp >= CSB_ENTRIES)
		return 0;
	while (S.csb_head != (int)wp) {
		S.csb_head = (S.csb_head + 1) % CSB_ENTRIES;
		u32 lo = S.hwsp[0x10 + 2 * S.csb_head], hi = S.hwsp[0x11 + 2 * S.csb_head];
		S.ctx_switches++;
		gdbg("CSB[%d] %08x:%08x (vers %x, depuis %x)\n", S.csb_head, lo, hi, CSB_CTX(lo), CSB_CTX(hi));
		if (CSB_CTX(lo) == CSB_IDLE)
			idle = 1;
		else
			idle = 0;
	}
	return idle;
}

static void engine_start(void)
{
	S.hwsp[0x2F] = CSB_ENTRIES - 1;
	clflush_range(S.hwsp, 4096);
	S.csb_head = CSB_ENTRIES - 1;
	wr(RCS + R_HWSTAM, 0xFFFFFFFF);
	wr(RCS + R_MODE, MASKED_ON(MODE_NO_LEGACY));
	wr(RCS + R_MI_MODE, MASKED_OFF(MI_MODE_STOP));
	wr(RCS + R_HWS_PGA, S.hwsp_ggtt);
	(void)rd(RCS + R_HWS_PGA);
	wr(RCS + R_CSB_PTR, (0xF0Fu << 16) | ((CSB_ENTRIES - 1) << 8) | (CSB_ENTRIES - 1));
}

static int engine_stop(int ms)
{
	u32 v;
	wr(RCS + R_MI_MODE, MASKED_ON(MI_MODE_STOP));
	if (wait_bits(RCS + R_MI_MODE, MI_MODE_IDLE, MI_MODE_IDLE, ms, &v)) {
		glog("le moteur ne s'arrête pas en %d ms (MI_MODE %08x)\n", ms, v);
		return -1;
	}
	return 0;
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
	return 0;
}

void gt_engine_rewa(void);

/* le moteur ne progresse plus : remise à zéro, les lots de la soumission en cours sont perdus */
static void hang(const char *why)
{
	S.hangs++;
	hang_row++;
	glog("BLOCAGE : %s (contexte %d, lots jusqu'à %u, fini %u, depuis %ld ms)\n", why, S.running, S.run_last,
	     S.status[IGS_DONE], now_ms() - S.run_since);
	engine_dump("bloqué");
	if (engine_reset()) {
		S.dead = 2;
		glog("DANGER : le moteur de rendu n'est pas remis à zéro : plus aucun lot\n");
		return;
	}
	gt_engine_rewa();
	engine_start();
	int c = S.running;
	banned[c] = 1;
	ctxs[c].lost++;
	S.status[IGS_LOST] = S.run_last;
	S.status[IGS_DONE] = S.run_last; /* le moteur est arrêté : le processeur écrit à sa place */
	S.status[IGS_RESETS]++;
	S.busy = 0;
	retire();
	if (hang_row >= 4) {
		S.dead = 1;
		glog("GPU perdu : %d blocages de suite ; le service refuse les lots (le redémarrer)\n", hang_row);
	} else
		glog("remise à zéro finie ; contexte %d banni (son client en refera un), le reste continue\n", c);
}

void eng_poll(void)
{
	if (S.dead == 2)
		return;
	int idle = csb_read();
	clflush_range(S.status, 64);
	retire();
	if (S.busy) {
		int fin = seq_done(S.run_last);
		long now = now_ms();
		if (fin && !t_done) {
			t_done = eng_us();
			eng_stat(ST_RUN, t_done - t_submit);
		}
		/* fini, pas encore « repos » : le changement de contexte suit de quelques µs ; l'attendre sur place
		 * (200 µs au plus) plutôt qu'au réveil suivant du fil (une interruption de plus, un passage de
		 * l'ordonnanceur), le lot suivant part aussitôt */
		if (fin && !idle && !G.simu) {
			while (!idle && eng_us() - t_done < 200)
				idle = csb_read();
			if (!idle)
				eng_idle_late++;
		}
		if (fin && idle && t_done)
			eng_stat(ST_IDLE, eng_us() - t_done);
		if (idle && fin) {
			S.busy = 0;
		} else if (idle && !fin) {
			clflush_range(S.status, 64);
			if (!seq_done(S.run_last)) {
				glog("ANOMALIE : le contexte %d est sorti du moteur sans finir (fait %u, attendu %u)\n",
				     S.running, S.status[IGS_DONE], S.run_last);
				hang("sortie sans fin de lot");
			} else
				S.busy = 0;
		} else if (fin) {
			/* fini, le CSB n'a pas (encore) dit « repos » */
			if (!idle_wait_since)
				idle_wait_since = now;
			else if (now - idle_wait_since > 50) {
				if (S.ctx_switches == 0 || G.simu)
					gdbg("CSB muet : repos supposé\n");
				else
					glog("CSB sans « repos » 50 ms après la fin (état %08x:%08x) : repos supposé\n",
					     rd(RCS + R_EL_STATUS_HI), rd(RCS + R_EL_STATUS_LO));
				S.busy = 0;
			}
		} else if (now - S.progress_ms > S.hang_ms) {
			hang("pas de progrès");
		}
	}
	while (!S.busy && sub_n && !S.dead) {
		struct sub *h = &subs[sub_head];
		if (banned[h->ctx]) { /* les lots qui restent d'un contexte banni : perdus sans partir */
			S.status[IGS_LOST] = h->seq;
			S.status[IGS_DONE] = h->seq;
			retire();
			continue;
		}
		submit_head();
	}
	if (S.dead && sub_n) { /* plus de GPU : tout ce qui attend est perdu */
		S.status[IGS_LOST] = S.status[IGS_SUBMITTED];
		S.status[IGS_DONE] = S.status[IGS_SUBMITTED];
		retire();
	}
}

/* attend le lot seq en faisant tourner le moteur soi-même (essai, contexte doré, arrêt) -> 0, -1 délai */
static int wait_self(u32 seq, long ms)
{
	long end = now_ms() + ms;
	for (;;) {
		eng_poll();
		if (seq_done(seq) && !S.busy)
			return 0;
		if (now_ms() > end || S.dead)
			return -1;
		korin_sys1(SYS_SLEEP, 1);
	}
}

/* ---- le temps des lots (µs, compteur du processeur : SYS_CLOCK_GET 3) */
static u64 tsc_per_ms;
static inline u64 rdtsc(void)
{
	u32 lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return (u64)hi << 32 | lo;
}

u64 eng_us(void)
{
	if (!tsc_per_ms) {
		long hz = korin_sys1(SYS_CLOCK_GET, 3);
		tsc_per_ms = hz > 1000 ? (u64)hz / 1000 : 0;
		if (!tsc_per_ms)
			tsc_per_ms = ~0ull; /* pas de compteur : le temps en ms */
	}
	if (tsc_per_ms == ~0ull)
		return (u64)now_ms() * 1000;
	return rdtsc() * 1000 / tsc_per_ms;
}

static struct { u64 n, sum, max; } stats[ST_N];
u64 eng_waits_now, eng_waits_later, eng_idle_late, eng_merged;

void eng_stat(int kind, u64 us)
{
	if (kind < 0 || kind >= ST_N || us > 60000000)
		return;
	stats[kind].n++;
	stats[kind].sum += us;
	if (us > stats[kind].max)
		stats[kind].max = us;
}

u64 eng_timestamp(void)
{
	u32 hi, lo, hi2;
	do {
		hi = rd(RCS + R_TIMESTAMP_HI);
		lo = rd(RCS + R_TIMESTAMP);
		hi2 = rd(RCS + R_TIMESTAMP_HI);
	} while (hi != hi2 && !G.simu);
	return (u64)hi << 32 | lo;
}

/* ---- mise en route */

static int kctx = -1;

static int record_golden(void)
{
	kctx = ctx_create(0, 0, 1);
	if (kctx < 0) {
		glog("ANOMALIE : pas de place pour le contexte du service\n");
		return -1;
	}
	u32 cmds[64];
	u32 n = ctx_wa_emit(cmds);
	long seq = exec_inline(kctx, cmds, n);
	eng_poll();
	if (seq < 0 || wait_self((u32)seq, 500)) {
		glog("ÉCHEC : le premier contexte n'a pas fini en 500 ms (fait %u)\n", S.status[IGS_DONE]);
		engine_dump("contexte doré");
		return -1;
	}
	struct ctx *x = &ctxs[kctx];
	volatile u32 *st = state_page(x);
	clflush_range(st, 22 * 4096);
	for (int k = 0; k < 22 * 1024; k++)
		S.golden[k] = st[k];
	int nz = 0;
	for (int k = 0; k < 22 * 1024; k++)
		nz += S.golden[k] != 0;
	glog("contexte doré : %d mots non nuls sauvés par le matériel ; en-têtes %08x %08x, CTX_CTL %08x, queue %x\n",
	     nz, S.golden[0], S.golden[1], S.golden[ST_CTX_CTL], S.golden[ST_TAIL]);
	S.golden_ok = 1;
	/* le contexte du service repart lui-même d'une copie (restauration permise) */
	ctx_destroy(kctx);
	kctx = ctx_create(0, 0, 1);
	return kctx < 0 ? -1 : 0;
}

int eng_init(void)
{
	S.win = -1;
	struct engine_regs e0;
	engine_read(&e0);
	S.engine_mode0 = e0.mode;
	S.mi_mode0 = e0.mi_mode;
	S.hws0 = e0.hws;
	S.hwstam0 = e0.hwstam;
	S.start0 = e0.start;
	S.imr0 = e0.imr;
	if (!S.hang_ms)
		S.hang_ms = 4000;
	if (e0.ctl & 1) {
		glog("ANOMALIE : l'anneau du rendu est allumé (CTL %08x) : quelqu'un s'en sert, pas de service\n", e0.ctl);
		return -1;
	}
	if (pt_init()) {
		glog("ANOMALIE : pas de mémoire pour les tables de pages\n");
		return -1;
	}
	long w = ggtt_window(SV_GGTT_PAGES);
	if (w < 0) {
		glog("ANOMALIE : pas de fenêtre libre de %d entrées dans la GGTT\n", SV_GGTT_PAGES);
		return -1;
	}
	for (int i = 0; i < SV_GGTT_PAGES; i++)
		S.old_pte[i] = gtt_rd((u64)w + i);
	S.win = w; /* (eng_fini ne remet la fenêtre que si elle a été prise) */
	glog("GGTT du service : entrées %lx..%lx (adresses GPU %lx..), anciennes valeurs %lx\n", (unsigned long)S.win,
	     (unsigned long)S.win + SV_GGTT_PAGES - 1, (unsigned long)S.win * 4096, (unsigned long)S.old_pte[0]);
	/* page d'état du moteur */
	long i = ggtt_alloc(1);
	long va = korin_sys2(SYS_DMA_ALLOC, 4096, &S.hwsp_phys);
	if (va < 0)
		return -1;
	S.hwsp = (volatile u32 *)va;
	S.hwsp_ggtt = ggtt_map(i, S.hwsp_phys, 1);
	if (G.simu)
		simu_note(S.hwsp_phys, (void *)va, 4096);
	/* page d'état partagée, lue par les clients : accordée à chacun à IG_OP_INFO (E47 : plus ouverte à tous) */
	S.status_buf = korin_sys1(SYS_BUF_CREATE, 4096);
	if (S.status_buf < 0) {
		glog("ANOMALIE : BUF_CREATE de la page d'état refusé (%ld)\n", S.status_buf);
		return -1;
	}
	u64 bi[4];
	korin_sys2(SYS_BUF_INFO, S.status_buf, bi);
	S.status = (volatile u32 *)(BUF_VA_BASE_SV + (u64)S.status_buf * BUF_SLOT_SV);
	S.status_phys = bi[0];
	i = ggtt_alloc(1);
	S.status_ggtt = ggtt_map(i, S.status_phys, 1);
	if (G.simu)
		simu_note(S.status_phys, (void *)S.status, 4096);
	S.next_seq = 0;
	engine_start();
	engine_dump("prêt");
	gt_irq_setup();
	if (record_golden())
		return -1;
	glog("moteur de rendu prêt : execlists, contexte doré, %s\n", S.irq >= 0 ? "MSI" : "sondage");
	return 0;
}

void eng_fini(void)
{
	if (S.win < 0 || S.dead == 2)
		return;
	if (S.busy && wait_self(S.run_last, 1000))
		glog("arrêt : le moteur n'a pas fini en 1 s\n");
	int stuck = engine_stop(100);
	if (stuck || S.busy) {
		if (engine_reset() || engine_stop(100)) {
			S.dead = 2; /* les pages et la GGTT restent : le GPU pourrait encore y écrire */
			return;
		}
	}
	gt_irq_off();
	if (!(S.engine_mode0 & MODE_NO_LEGACY))
		wr(RCS + R_MODE, MASKED_OFF(MODE_NO_LEGACY));
	wr(RCS + R_CTL, 0);
	wr(RCS + R_HEAD, 0);
	wr(RCS + R_TAIL, 0);
	wr(RCS + R_START, S.start0);
	wr(RCS + R_HWS_PGA, S.hws0);
	wr(RCS + R_HWSTAM, S.hwstam0);
	wr(RCS + R_IMR, S.imr0);
	if (!(S.mi_mode0 & MI_MODE_STOP))
		wr(RCS + R_MI_MODE, MASKED_OFF(MI_MODE_STOP));
	(void)rd(RCS + R_HWS_PGA);
	engine_dump("à la fin");
	int bad = 0;
	for (int k = 0; k < SV_GGTT_PAGES; k++)
		gtt_wr((u64)S.win + k, S.old_pte[k]);
	for (int k = 0; k < SV_GGTT_PAGES; k++)
		bad += gtt_rd((u64)S.win + k) != S.old_pte[k];
	ggtt_flush();
	glog("GGTT : %d entrées remises comme avant%s\n", SV_GGTT_PAGES, bad ? " (ANOMALIE : relues autrement)" : "");
	S.win = -1;
}

/* ---- l'essai (intelgpu test) : des lots faits à la main, en PPGTT, par le même chemin que ceux d'iris */

#define T_MAGIC1 0x4B4F5249u /* « IROK » */
#define T_MAGIC2 0x32555047u /* « GPU2 » */

int eng_selftest(void)
{
	int rc = 0;
	int v = vm_create(0);
	if (v < 0)
		return -1;
	int c1 = ctx_create(0, v, 0), c2 = ctx_create(0, v, 0);
	if (c1 < 0 || c2 < 0) {
		glog("ANOMALIE : contextes de l'essai refusés\n");
		return -1;
	}
	u64 phys;
	long va = korin_sys2(SYS_DMA_ALLOC, 4 * 4096, &phys);
	if (va < 0)
		return -1;
	if (G.simu)
		simu_note(phys, (void *)va, 4 * 4096);
	volatile u32 *b = (volatile u32 *)va, *t = b + 1024;
	const u64 gpu = 0x0000100000000000ull; /* très haut dans les 48 bits : quatre niveaux servent */
	if (vm_bind(v, gpu, phys, 4 * 4096)) {
		glog("ANOMALIE : vm_bind refusé\n");
		return -1;
	}
	u64 tg = gpu + 4096;
	int n = 0;
	b[n++] = MI_SDI; /* PPGTT : sans MI_USE_GGTT */
	b[n++] = (u32)tg;
	b[n++] = (u32)(tg >> 32);
	b[n++] = T_MAGIC1;
	b[n++] = PIPE_CONTROL;
	b[n++] = PC_CS_STALL | PC_WRITE_IMM; /* adresse PPGTT */
	b[n++] = (u32)(tg + 8);
	b[n++] = (u32)((tg + 8) >> 32);
	b[n++] = T_MAGIC2;
	b[n++] = 0;
	b[n++] = MI_BB_END;
	b[n++] = MI_NOOP;
	clflush_range(b, 8192);
	glog("essai 1 : un lot en PPGTT (MI_STORE_DATA_IMM, PIPE_CONTROL), adresse GPU %lx, PML4 %lx\n",
	     (unsigned long)gpu, (unsigned long)vms[v].pml4);
	long t0 = now_ms();
	long seq = eng_exec(c1, gpu, n * 4, 0);
	if (seq < 0 || wait_self((u32)seq, 1000)) {
		glog("ÉCHEC : le lot n'a pas fini (fait %u, attendu %ld)\n", S.status[IGS_DONE], seq);
		return -1;
	}
	clflush_range(t, 64);
	int ok1 = t[0] == T_MAGIC1, ok2 = t[2] == T_MAGIC2;
	glog("%s : MI_STORE_DATA_IMM %08x, PIPE_CONTROL %08x, en %ld ms\n", ok1 && ok2 ? "RÉUSSI" : "ÉCHEC", t[0], t[2],
	     now_ms() - t0);
	rc |= !(ok1 && ok2);
	/* essai 2 : deux contextes en alternance, 64 lots, chacun écrit son rang */
	for (int k = 0; k < 64; k++)
		t[16 + k] = 0;
	volatile u32 *bb = b + 2048; /* page 2 : 64 petits lots de 8 mots */
	for (int k = 0; k < 64; k++) {
		volatile u32 *p = bb + 16 * k;
		u64 a = tg + 64 + 4 * k;
		p[0] = MI_SDI;
		p[1] = (u32)a;
		p[2] = (u32)(a >> 32);
		p[3] = 0x100 + k;
		p[4] = MI_BB_END;
		p[5] = MI_NOOP;
	}
	clflush_range(bb, 4096);
	clflush_range(t, 4096);
	t0 = now_ms();
	long last = 0;
	for (int k = 0; k < 64; k++) {
		long s;
		while ((s = eng_exec((k & 2) ? c2 : c1, gpu + 8192 + 64 * k, 24, 0)) == E_AGAIN)
			eng_poll();
		if (s < 0) {
			glog("ÉCHEC : lot %d refusé (%ld)\n", k, s);
			return -1;
		}
		last = s;
		eng_poll();
	}
	if (wait_self((u32)last, 2000)) {
		glog("ÉCHEC : les 64 lots n'ont pas fini (fait %u, attendu %ld)\n", S.status[IGS_DONE], last);
		return -1;
	}
	clflush_range(t, 4096);
	int bad = 0;
	for (int k = 0; k < 64; k++)
		bad += t[16 + k] != (u32)(0x100 + k);
	glog("%s : 64 lots sur deux contextes en %ld ms, %d faux ; %lu soumissions, %lu changements de contexte\n",
	     bad ? "ÉCHEC" : "RÉUSSI", now_ms() - t0, bad, (unsigned long)S.submits, (unsigned long)S.ctx_switches);
	rc |= bad != 0;
	u64 ts0 = eng_timestamp();
	korin_sys1(SYS_SLEEP, 10);
	u64 ts1 = eng_timestamp();
	glog("horodatage : %lu tops en ~10 ms\n", (unsigned long)(ts1 - ts0));
	if (simu_hang_later) {
		simu_hang = 1;
		glog("essai 3 (simu-hang) : un lot qui ne finit pas, délai réduit à 300 ms\n");
		S.hang_ms = 300;
		long s = eng_exec(c1, gpu, n * 4, 0);
		wait_self((u32)s, 2000);
		glog("après le blocage : fait %u, perdus jusqu'à %u, remises à zéro %u ; contexte %d %s\n",
		     S.status[IGS_DONE], S.status[IGS_LOST], S.status[IGS_RESETS], c1,
		     eng_exec(c1, gpu, n * 4, 0) == E_IO ? "banni (E_IO)" : "PAS banni (ANOMALIE)");
		simu_hang = 0;
		long s2 = eng_exec(c2, gpu, n * 4, 0);
		int ok = s2 > 0 && !wait_self((u32)s2, 1000);
		glog("%s : l'autre contexte repart après la remise à zéro\n", ok ? "RÉUSSI" : "ÉCHEC");
		rc |= !ok;
	}
	ctx_destroy(c1);
	ctx_destroy(c2);
	vm_destroy(v);
	glog("essai %s (tables de pages : %lu pages)\n", rc ? "ÉCHOUÉ" : "RÉUSSI", (unsigned long)pt_pages_used());
	return rc;
}

void eng_status(char *b, int max)
{
	int n = 0, nc = 0, nv = 0;
	for (int c = 0; c < CTX_MAX; c++)
		nc += ctxs[c].used;
	for (int v = 0; v < VM_MAX; v++)
		nv += vms[v].used;
	n += snprintf(b + n, max - n, "intelgpu : %s, %s, %04x rév. %02x\n",
		      S.dead == 2 ? "MOTEUR BLOQUÉ" : S.dead ? "GPU perdu" : "prêt", G.simu ? "simu" : "vrai GPU",
		      G.pci.device, G.pci.rev);
	n += snprintf(b + n, max - n, "  lots : donnés %u, finis %u, perdus jusqu'à %u ; soumissions %lu, en file %d\n",
		      S.status[IGS_SUBMITTED], S.status[IGS_DONE], S.status[IGS_LOST], (unsigned long)S.submits, sub_n);
	n += snprintf(b + n, max - n, "  contextes %d (dont le service), espaces %d, tables %lu pages ; blocages %lu\n", nc,
		      nv, (unsigned long)pt_pages_used(), (unsigned long)S.hangs);
	n += snprintf(b + n, max - n, "  interruptions %s : %lu (%lu sans objet), changements de contexte %lu\n",
		      S.irq >= 0 ? "MSI" : "par sondage", (unsigned long)S.irqs, (unsigned long)S.spurious,
		      (unsigned long)S.ctx_switches);
	n += rps_status(b + n, max - n);
	(void)n;
}

void eng_times(char *b, int max)
{
	int n = 0;
	static const char *const nm[ST_N] = {"file", "moteur", "fin->repos", "attente"};
	n += snprintf(b + n, max - n, "  temps des lots depuis le dernier status (µs, moyenne/pire, nombre) :\n   ");
	for (int k = 0; k < ST_N && n < max; k++)
		n += snprintf(b + n, max - n, " %s %lu/%lu (%lu)", nm[k],
			      (unsigned long)(stats[k].n ? stats[k].sum / stats[k].n : 0), (unsigned long)stats[k].max,
			      (unsigned long)stats[k].n);
	if (n < max)
		n += snprintf(b + n, max - n, "\n  attentes : %lu tout de suite, %lu plus tard ; repos en retard %lu ; lots joints %lu\n",
			      (unsigned long)eng_waits_now, (unsigned long)eng_waits_later, (unsigned long)eng_idle_late,
			      (unsigned long)eng_merged);
	memset(stats, 0, sizeof stats);
	eng_waits_now = eng_waits_later = eng_idle_late = eng_merged = 0;
	(void)n;
}
