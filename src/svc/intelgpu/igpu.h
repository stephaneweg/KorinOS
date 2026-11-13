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

/* simu.c : un faux GPU en mémoire pour essayer les chemins sous QEMU */
volatile u8 *simu_bar0(void);
void simu_write(u32 off, u32 v);
extern int simu_hang;

#endif
