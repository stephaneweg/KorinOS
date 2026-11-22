/* igpu.h : le GPU Intel du PC (Tiger Lake, Gen12 LP / Xe-LP), étape 1 : la
 * sonde (docs/etude/GPU-INTEL.md, docs/etude/PC-PILOTES.md « GPU Intel,
 * étape 1 »). Les registres viennent des volumes publics du PRM d'Intel
 * (Tiger Lake, « Command Reference: Registers ») ; les séquences ont été
 * comparées au pilote i915 de Linux, sans en reprendre le code. */
#ifndef IGPU_H
#define IGPU_H

#include "korinrt.h"
#include "pci.h"

/* ---- configuration PCI de 00:02.0 */
#define CFG_CMD 0x04
#define CFG_GMCH_CTRL 0x50 /* 16 bits : GGMS (7:6), GMS (15:8) */
#define CFG_BDSM 0x5C      /* base de la mémoire volée (32 bits, bits 31:20) */
#define CFG_BDSM_LO 0xC0   /* Gen11+ : la même sur 64 bits */
#define CFG_BDSM_HI 0xC4
#define CFG_ASLS 0xFC      /* OpRegion (ACPI) */

/* ---- BAR0 (GTTMMADR, 16 Mo) : registres en bas, la GGTT à 8 Mo */
#define BAR0_SIZE (16ul << 20)
#define GGTT_OFF (8ul << 20)

/* ---- forcewake (Gen11+ : un domaine GT, un domaine rendu ; écritures masquées) */
#define FW_GT 0xA188
#define FW_GT_ACK 0x130044
#define FW_RENDER 0xA278
#define FW_RENDER_ACK 0x0D84
#define FW_KERNEL 1u /* le bit du pilote (bit 0) */

/* écriture masquée : les 16 bits du haut disent quels bits du bas changer */
#define MASKED_ON(b) (((b) << 16) | (b))
#define MASKED_OFF(b) ((b) << 16)

/* ---- identité, fusibles (lecture) */
#define EU_DISABLE 0x9134      /* bits 7:0 : paires d'EU coupées (communes aux DSS) */
#define SLICE_ENABLE 0x9138    /* bits 7:0 */
#define DSS_ENABLE 0x913C      /* Gen12 : sous-tranches doubles allumées */
#define VDBOX_VEBOX_DISABLE 0x9140
#define RPM_CONFIG0 0x0D00     /* horloge du cristal (5:3), décalage CTC (2:1) */
#define CTC_MODE 0xA26C        /* bit 0 : source de l'horodatage */
#define GT_CORE_STATUS 0x138060 /* état RC (2:0) */
#define PAT_INDEX(i) (0x4800 + 4 * (i)) /* Gen12 : table PAT du GPU */
#define GUC_STATUS 0xC000
#define GFX_FLSH_CNTL 0x101008 /* écrire 1 : vide le TLB de la GGTT */
#define GDRST 0x941C           /* remise à zéro : bit 0 tout, bit 1 rendu */
#define GDRST_RENDER (1u << 1)

/* ---- affichage : LECTURE seulement (la sonde n'y écrit jamais) */
#define PLANE_CTL_1_A 0x70180
#define PLANE_SURF_1_A 0x7019C

/* ---- moteur de rendu (RCS0, base 0x2000) */
#define RCS 0x2000
#define R_TAIL 0x030
#define R_HEAD 0x034
#define R_START 0x038
#define R_CTL 0x03C
#define R_IPEHR 0x068
#define R_INSTDONE 0x06C
#define R_ACTHD 0x074
#define R_HWS_PGA 0x080
#define R_HWSTAM 0x098
#define R_MI_MODE 0x09C        /* masqué : bit 8 STOP_RING, bit 9 MODE_IDLE (lecture) */
#define R_IMR 0x0A8
#define R_EIR 0x0B0
#define R_EMR 0x0B4
#define R_ESR 0x0B8
#define R_RESET_CTL 0x0D0      /* masqué : bit 0 demande, bit 1 prêt */
#define R_BBSTATE 0x110
#define R_BBADDR 0x140
#define R_BBADDR_UDW 0x168
#define R_CCID 0x180
#define R_EL_STATUS_LO 0x234
#define R_EL_STATUS_HI 0x238
#define R_CTX_CTL 0x244
#define R_MODE 0x29C           /* masqué : bit 3 = plus de mode « anneau » (execlists, Gen11+) */
#define R_TIMESTAMP 0x358
#define R_CSB_PTR 0x3A0
#define R_EL_SQ 0x510          /* file de soumission : port 0 (bas, haut), port 1 */
#define R_EL_CTRL 0x550        /* bit 0 : charger la file */

#define MI_MODE_STOP (1u << 8)
#define MI_MODE_IDLE (1u << 9)
#define MODE_NO_LEGACY (1u << 3)

/* ---- commandes (MI_*) */
#define MI_INSTR(op, len) (((uint32_t)(op) << 23) | (len))
#define MI_NOOP 0u
#define MI_BB_END MI_INSTR(0x0A, 0)
#define MI_SDI MI_INSTR(0x20, 2)    /* + adresse (2 mots) + valeur */
#define MI_USE_GGTT (1u << 22)
#define MI_LRI(n) MI_INSTR(0x22, 2 * (n) - 1)
#define MI_LRI_POSTED (1u << 12)
#define MI_LRI_CS_MMIO (1u << 19)
#define MI_BB_START MI_INSTR(0x31, 1) /* + adresse (2 mots) ; bit 8 : PPGTT (0 : GGTT) */

/* ---- descripteur de contexte (execlists) */
#define CTX_VALID (1u << 0)
#define CTX_ADDR_64B (3u << 3) /* PPGTT à 4 niveaux (aucune adresse PPGTT n'est utilisée ici) */
#define CTX_PRIVILEGE (1u << 8)
#define CTX_SW_ID_SHIFT 37     /* Gen11+ : numéro du contexte (11 bits) */

/* ---- l'état commun */
struct gpu {
	struct pci_info pci;
	char where[16];
	uint32_t cmd_before;    /* registre de commande avant la prise */
	volatile u8 *mmio;      /* BAR0 */
	volatile u64 *ggtt;     /* BAR0 + 8 Mo */
	u64 ggtt_entries;       /* d'après GGMS */
	u64 stolen_base, stolen_size;
	int verbose, simu, fw_held;
};
extern struct gpu G;

/* intelgpu.c */
void glog(const char *fmt, ...);
void gdbg(const char *fmt, ...);
long now_ms(void);
u32 rd(u32 off);
void wr(u32 off, u32 v);
u64 gtt_rd(u64 i);
void gtt_wr(u64 i, u64 v);
void clflush_range(volatile void *p, u64 n);
int wait_bits(u32 off, u32 mask, u32 want, long ms, u32 *last);

struct engine_regs {
	u32 mode, mi_mode, head, tail, start, ctl, acthd, hws, hwstam, imr, el_lo, el_hi, csb, instdone, eir, esr,
		ipehr, reset, ctxctl, ccid, bbaddr;
};
void engine_read(struct engine_regs *e);
void engine_dump(const char *when);
void ggtt_scan(void);
long ggtt_window(u64 n); /* n entrées libres -> premier indice, -1 */

/* submit.c : soumission d'un lot minimal (option submit) -> 0, < 0 : anomalie */
int submit_test(void);
void *submit_phys_to_va(u64 phys); /* une adresse physique de la zone DMA -> la nôtre */

/* rps.c : la fréquence du GT (requête fixe au PCU, « max » par défaut) */
void rps_init(const char *want);
void rps_fini(void);
int rps_status(char *b, int max);
u32 rps_cagf(void);

/* simu.c : un faux GPU en mémoire pour essayer les chemins sous QEMU */
volatile u8 *simu_bar0(void);
void simu_write(u32 off, u32 v);
extern int simu_hang;

/* ======================================================================
 * Étape 2 : le service (serve.c, gt.c, engine.c, ppgtt.c). Registres de
 * Gen12 LP d'après le PRM de Tiger Lake ; valeurs des contournements
 * d'après le volume « Workarounds » recoupé avec ce que fait i915 (décrit,
 * réécrit : aucun code repris). */
#include "igproto.h"

/* ---- interruptions (Gen11+) : maître, banques du GT, identité par source */
#define GFX_MSTR_IRQ 0x190010     /* bit 31 : maître ; 0, 1 : banques GT ; 16 : affichage (jamais servi) */
#define MSTR_IRQ_ENABLE (1u << 31)
#define MSTR_GT_DW0 (1u << 0)
#define MSTR_GT_DW1 (1u << 1)
#define GT_INTR_DW0 0x190018      /* bit 0 : RCS0 */
#define INTR_IDENTITY0 0x190060   /* bit 31 : valide ; 15:0 : les bits du moteur */
#define IIR_SELECTOR0 0x190070
#define RENDER_COPY_INTR_ENABLE 0x190030 /* 31:16 rendu, 15:0 copieur */
#define RCS0_RSVD_INTR_MASK 0x190090     /* 31:16 : RCS0 (1 : masquée) */
#define DISPLAY_INT_CTL 0x44200   /* (lecture seulement : journal) */
#define GT_USER_INTERRUPT (1u << 0)
#define GT_CS_MASTER_ERROR (1u << 3)
#define GT_CONTEXT_SWITCH (1u << 8)

/* ---- cache : PAT du GPU (PAT_INDEX), MOCS globaux, L3 */
#define PAT_WB 3u
#define PAT_WC 1u
#define PAT_WT 2u
#define PAT_UC 0u
#define GLOBAL_MOCS(i) (0x4000 + 4 * (i)) /* 64 entrées, Gen12 */
#define LNCF_MOCS(i) (0xB020 + 4 * (i))   /* 32 registres : deux entrées L3 de 16 bits */
#define CCS_AUX_INV 0x4208                /* écrire 1 : invalide la table auxiliaire (compression) */

/* ---- registres des contournements (rendu, GT) */
#define CS_DEBUG_MODE1 0x20EC
#define FF_THREAD_MODE 0x20A0
#define R_PSMI_CTL 0x050          /* (moteur) */
#define CS_CHICKEN1 0x2580
#define ROW_CHICKEN2 0xE4F4
#define ROW_CHICKEN4 0xE48C
#define SAMPLER_MODE 0xE18C
#define COMMON_SLICE_CHICKEN1 0x7010
#define HIZ_CHICKEN 0x7018
#define COMMON_SLICE_CHICKEN4 0x7300
#define COMMON_SLICE_CHICKEN3 0x7304
#define FF_MODE2 0x6604
#define DFR_RATIO_EN_AND_CHICKEN 0x9550
#define MISCCPCTL 0x9424

#define R_TIMESTAMP_HI 0x35C
#define R_EL_SQ_STATUS 0x234

/* ---- commandes de plus */
#define MI_USER_INTERRUPT MI_INSTR(0x02, 0)
#define MI_ARB_ON_OFF MI_INSTR(0x08, 0)
#define MI_ARB_ENABLE 1u
#define MI_BB_PPGTT (1u << 8)
#define PIPE_CONTROL (0x7A000000u | 4)
#define PC0_HDC_FLUSH (1u << 9)
#define PC_DEPTH_FLUSH (1u << 0)
#define PC_STATE_INV (1u << 2)
#define PC_CONST_INV (1u << 3)
#define PC_VF_INV (1u << 4)
#define PC_DC_FLUSH (1u << 5)
#define PC_PC_FLUSH (1u << 7)
#define PC_TEX_INV (1u << 10)
#define PC_INSTR_INV (1u << 11)
#define PC_RT_FLUSH (1u << 12)
#define PC_WRITE_IMM (1u << 14)
#define PC_TLB_INV (1u << 18)
#define PC_CS_STALL (1u << 20)
#define PC_GGTT (1u << 24)
#define PC_TILE_FLUSH (1u << 28)
#define PC_CMD_INV (1u << 29)

/* ---- CSB de Gen12 : le numéro du contexte (11 bits) dans les bits 25:15 des deux mots */
#define CSB_ENTRIES 12
#define CSB_CTX(dw) (((dw) >> 15) & 0x7FF)
#define CSB_IDLE 0x7FF

/* ---- l'espace de la GGTT du service : une fenêtre de 4096 entrées (16 Mo) */
#define SV_GGTT_PAGES 16384 /* 64 Mo : contextes (28 pages chacun), pages d'état ; 16 Mo ne laissaient que ~140 contextes */
#define BUF_VA_BASE_SV 0x6800000000ull /* tampon partagé n : BUF_VA_BASE_SV + n * BUF_SLOT_SV */
#define BUF_SLOT_SV 0x4000000ull

/* ---- PPGTT (ppgtt.c) */
#define PTE_PRESENT 1ull
#define PTE_RW 2ull
#define PTE_ADDR 0x000FFFFFFFFFF000ull
#define GPU_VA_MASK ((1ull << 48) - 1)

struct vm {
	int used, owner;      /* pid */
	u64 pml4;             /* adresse physique */
	long tables;          /* pages de tables à lui */
};
#define VM_MAX 128
extern struct vm vms[VM_MAX];

int pt_init(void);                     /* pages de garde, réserve -> 0 */
int vm_create(int pid);                /* -> numéro, < 0 */
void vm_destroy(int v);
int vm_bind(int v, u64 gpu, u64 phys, u64 bytes); /* contigu -> 0 */
void vm_unbind(int v, u64 gpu, u64 bytes);
u64 vm_translate(u64 pml4, u64 gpu); /* -> adresse physique, ~0 : hors de toute page à nous (simu, contrôle) */
void *pt_va(u64 phys);                 /* une page de table -> notre vue */
u64 pt_pages_used(void);
u64 scratch_phys(void);

/* ---- les contextes et le moteur (engine.c) */
#define CTX_MAX 256
#define CTX_IMAGE_PAGES 24   /* PPHWSP + 22 pages d'état de Gen12 RCS + 1 de marge */
#define CTX_RING_BYTES (16 * 1024)
#define CTX_PAGES (CTX_IMAGE_PAGES + CTX_RING_BYTES / 4096)

struct ctx {
	int used, owner, vm, priv;
	u64 phys;        /* image puis anneau (DMA contigu, gardé d'un contexte à l'autre) */
	volatile u32 *va;
	u32 ggtt;        /* adresse GGTT de l'image ; l'anneau suit */
	u32 tail;        /* écriture du logiciel dans l'anneau */
	u32 hw_tail;     /* ce que le matériel a reçu */
	u32 last_seq;    /* dernier lot donné */
	u32 lost;        /* lots perdus (remises à zéro) */
	int queued;      /* lots en file ou en vol */
};
extern struct ctx ctxs[CTX_MAX];

struct svstate {
	u32 lock;            /* mtx_lock d'AppKit : tout l'état ci-dessous */
	long win;            /* première entrée de la GGTT du service */
	u64 old_pte[SV_GGTT_PAGES];
	u8 ggtt_used[SV_GGTT_PAGES];
	volatile u32 *hwsp;  /* page d'état du moteur (CSB) */
	u64 hwsp_phys;
	u32 hwsp_ggtt;
	volatile u32 *status; /* page d'état partagée (IGS_*) */
	long status_buf;
	u64 status_phys;
	u32 status_ggtt;
	u32 next_seq;
	u32 golden[22 * 1024]; /* l'état sauvé par le matériel après le premier lot */
	int golden_ok;
	int csb_head;
	int busy;            /* un contexte sur le moteur */
	int running;         /* son numéro */
	u32 run_last;        /* le dernier lot de cette soumission */
	long run_since, progress_ms;
	u32 run_done_seen;
	long irq;            /* ligne MSI, -1 : sondage */
	u64 irqs, ctx_switches, submits, hangs, spurious;
	int dead;            /* GPU perdu : plus rien n'est soumis */
	u32 engine_mode0, mi_mode0, hws0, hwstam0, start0, imr0; /* registres au départ */
	int stop;
	long hang_ms;
};
extern struct svstate S;

/* une soumission en file (dans l'ordre des numéros) */
struct sub {
	int ctx;
	u32 seq, tail;
	u64 t_queued;        /* (µs) mise en file : l'attente avant le moteur (eng_status) */
};
#define SUB_MAX 1024

/* gt.c */
void gt_init(void);           /* PAT, MOCS, L3, contournements du GT et du moteur */
void gt_irq_setup(void);      /* MSI et interruptions du rendu ; S.irq = -1 : sondage */
u32 gt_irq_ack(void);         /* les bits du moteur levés (0 : rien pour nous) */
void gt_irq_off(void);
u32 ctx_wa_emit(volatile u32 *cs); /* les contournements du contexte (LRI) -> mots écrits */

/* engine.c */
int eng_init(void);           /* GGTT, pages d'état, moteur en execlists, contexte doré -> 0 */
void eng_fini(void);          /* moteur arrêté, registres et GGTT comme au départ */
int ctx_create(int owner, int vm, int priv); /* -> numéro, < 0 */
void ctx_destroy(int c);      /* (lots finis) */
long eng_exec(int c, u64 batch, u32 len, int priv); /* -> numéro de lot, < 0 */
void eng_poll(void);          /* CSB, fins, file, délais (sous le verrou) */
int seq_done(u32 seq);
int eng_ctx_idle(int c);
u64 eng_timestamp(void);
int eng_selftest(void);       /* lots faits à la main : SDI en PPGTT, PIPE_CONTROL -> 0 */
void eng_status(char *buf, int max);
void eng_times(char *buf, int max); /* le temps des lots depuis le dernier appel (puis remis à zéro) */
/* le temps des lots, pour `intelgpu status` (22/11/2026 : Jet attendait 15 à 45 ms par image) */
u64 eng_us(void);             /* µs depuis le démarrage (compteur du processeur) */
enum { ST_QUEUE, ST_RUN, ST_IDLE, ST_WAIT, ST_N };
void eng_stat(int kind, u64 us); /* une mesure : file, moteur, fin -> repos, attente d'un client */
extern u64 eng_waits_now, eng_waits_later, eng_idle_late, eng_merged;

/* serve.c */
int serve(void);

/* simu.c : le faux GPU de l'étape 2 (anneau, PPGTT, CSB) */
void *simu_phys(u64 phys);    /* une adresse physique connue du service -> notre vue, 0 sinon */
void simu_note(u64 phys, void *va, u64 bytes); /* une zone que le faux GPU peut lire et écrire */
void simu_forget(void *va);

/* intelgpu.c */
int gpu_open(void);           /* trouver, prendre, mapper, forcewake -> 0 */
void gpu_close(void);
u32 cfg_read(unsigned off, unsigned size);

#endif
