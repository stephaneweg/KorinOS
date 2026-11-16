/* iwl.h : le pilote du Wi-Fi Intel AX201 du PC (CNVi de Tiger Lake, famille « 22000 », MAC QuZ, RF Hr),
 * étape 1 : mise en route du micrologiciel et balayage, lancé à la main (docs/etude/WIFI-PC.md,
 * docs/etude/PC-PILOTES.md « Wi-Fi AX201, étape 1 »).
 *   iwl.c    commande et journal, fonction PCIe (pcid), MSI, fichier du micrologiciel ;
 *   trans.c  transport PCIe « gen2 » : registres CSR, périphériques (prph), mise en route et arrêt, context
 *            info, file de commandes (TFD), anneau de réception (RBD), interruptions ou sondage ;
 *   mvm.c    le micrologiciel en mode MVM : vivant, initialisation unifiée, NVM, régulation, balayage ;
 *   fw.c, cmds.c, ieee.c, crypto.c : sans matériel (iwlfw.h).
 * Les registres et séquences sont ceux de la documentation publique, comparés au pilote iwlwifi de Linux
 * (v6.12) ; le code est écrit ici. Pour le PC seulement (svc.conf : arch = x86_64). */
#ifndef IWL_H
#define IWL_H

#include "iwlfw.h"
#include "pci.h"
#include "../netif/netif.h"

#ifndef SVC_OP_SIGNAL
#define SVC_OP_SIGNAL 0xFFFD
#endif

#define E_NODEV -19
#define E_BUSY -16
#define E_TIMEDOUT -110
#define E_NOENT_ -2
#define E_FWERR -1000 /* le micrologiciel a signalé une erreur (SW_ERR / HW_ERR) */

/* ---- registres CSR (BAR0, 16 Ko) */
#define CSR_HW_IF_CONFIG 0x000
#define CSR_INT_COALESCING 0x004
#define CSR_INT 0x008
#define CSR_INT_MASK 0x00c
#define CSR_FH_INT_STATUS 0x010
#define CSR_RESET 0x020
#define CSR_GP_CNTRL 0x024
#define CSR_HW_REV 0x028
#define CSR_GIO 0x03c
#define CSR_CTXT_INFO_BA 0x040 /* 64 bits */
#define CSR_UCODE_DRV_GP1 0x054
#define CSR_UCODE_DRV_GP1_CLR 0x05c
#define CSR_UCODE_DRV_GP2 0x060
#define CSR_MBOX_SET 0x088
#define CSR_HW_RF_ID 0x09c
#define CSR_MAC_SHADOW_REG_CTRL 0x0a8
#define CSR_LTR_LAST_MSG 0x0dc
#define CSR_GIO_CHICKEN 0x100
#define CSR_DBG_HPET_MEM 0x240
#define CSR_DBG_LINK_PWR_MGMT 0x250
#define CSR_MAC_ADDR0_OTP 0x380 /* (22000 : base des adresses MAC à 0x380) */
#define CSR_MAC_ADDR1_OTP 0x384
#define CSR_MAC_ADDR0_STRAP 0x388
#define CSR_MAC_ADDR1_STRAP 0x38c
#define HBUS_TARG_MEM_RADDR 0x40c
#define HBUS_TARG_MEM_RDAT 0x41c
#define HBUS_TARG_PRPH_WADDR 0x444
#define HBUS_TARG_PRPH_RADDR 0x448
#define HBUS_TARG_PRPH_WDAT 0x44c
#define HBUS_TARG_PRPH_RDAT 0x450
#define HBUS_TARG_WRPTR 0x460
#define RFH_Q0_FRBDCB_WIDX_TRG 0x1c80
#define CSR_MSIX_HW_INT_CAUSES 0x2808

/* bits */
#define HWIF_HAP_WAKE_L1A 0x00080000u
#define HWIF_NIC_READY 0x00400000u
#define HWIF_PREPARE 0x08000000u
#define HWIF_ENABLE_PME 0x10000000u
#define INT_ALIVE (1u << 0)
#define INT_WAKEUP (1u << 1)
#define INT_SW_RX (1u << 3)
#define INT_CT_KILL (1u << 6)
#define INT_RF_KILL (1u << 7)
#define INT_SW_ERR (1u << 25)
#define INT_FH_TX (1u << 27)
#define INT_RX_PERIODIC (1u << 28)
#define INT_HW_ERR (1u << 29)
#define INT_FH_RX (1u << 31)
#define INT_ALL_WANTED (INT_FH_RX | INT_HW_ERR | INT_FH_TX | INT_SW_ERR | INT_RF_KILL | INT_SW_RX | INT_WAKEUP | \
			INT_ALIVE | INT_RX_PERIODIC)
#define RESET_SW_RESET 0x00000080u
#define RESET_MASTER_DISABLED 0x00000100u
#define RESET_STOP_MASTER 0x00000200u
#define RESET_LINK_PWR_MGMT_DISABLED 0x80000000u
#define GP_MAC_CLOCK_READY 0x00000001u
#define GP_INIT_DONE 0x00000004u
#define GP_MAC_ACCESS_REQ 0x00000008u
#define GP_GOING_TO_SLEEP 0x00000010u
#define GP_HW_RF_KILL_SW 0x08000000u /* 1 : radio permise (interrupteur « avion » relâché) */
#define MBOX_OS_ALIVE (1u << 5)
#define GIO_L0S_DISABLED 0x00000002u
#define GIO_CHICKEN_L1A_NO_L0S_RX 0x00800000u
#define HPET_MEM_VAL 0xFFFF0000u
#define GP1_SW_BIT_RFKILL 0x2
#define GP1_CMD_BLOCKED 0x4
#define MSIX_HW_RESET_DONE (1u << 2)
#define FH_RX_MASK 0x40030000u /* priorité haute, voies 0 et 1 */
#define INT_COALESCING_DEF 0x40

/* ---- registres des périphériques (par HBUS_TARG_PRPH, accès MAC tenu) */
#define PRPH_MASK 0x000FFFFFu
#define UREG_CHICK 0xA05C00
#define UREG_CHICK_MSI_ENABLE (1u << 24)
#define UREG_DOORBELL_TO_ISR6 0xA05C04
#define UREG_DOORBELL_PNVM (1u << 20)
#define UREG_NIC_SET_NMI_DRIVER 0xA05C10
#define UREG_NMI_RESET_HANDSHAKE ((1u << 24) | (1u << 25))
#define UREG_UMAC_CURRENT_PC 0xA05C18
#define UREG_LMAC1_CURRENT_PC 0xA05C1C
#define UREG_CPU_INIT_RUN 0xA05C44
#define HPM_UMAC_LTR 0xA03480
#define HPM_MAC_LTR_CSR 0xA0348C
#define HPM_MAC_LTR_ENABLE_ALL 0xF
#define HPM_DEBUG 0xA03440
#define HPM_PERSISTENCE_BIT (1u << 12)
#define PREG_PRPH_WPROT_22000 0xA04D00
#define PREG_WFPM_ACCESS (1u << 12)
#define HPM_HIPM_GEN_CFG 0xA03458
#define HIPM_PG_EN (1u << 0)
#define HIPM_SLP_EN (1u << 1)
#define HIPM_FORCE_ACTIVE (1u << 10)
#define UMAG_SB_CPU_1_STATUS 0xA038C0
#define UMAG_SB_CPU_2_STATUS 0xA038C4
#define UMAG_GEN_HW_STATUS 0xA038C8
#define WFPM_ARC1_PD_NOTIFICATION 0xA03044
#define WFPM_LMAC1_PD_NOTIFICATION 0xA0338C
#define CNVI_AUX_MISC_CHIP 0xA200B0
#define WFPM_CTRL_REG 0xA03030
#define WFPM_AUX_IF_MAC_OWNER 0x80000000u
#define WFPM_OTP_CFG1 0xA03098
#define SD_REG_VER 0xA29600
#define RFH_GEN_STATUS 0xA09808
#define RFH_RXF_DMA_CFG 0xA09820
#define RXF_DMA_IDLE (1u << 31)
#define REG_OTP_MINOR 0xA0333C

/* ---- files : commandes (32 TFD de 256 o), réception (512 RBD, RB de 4 Ko) */
#define CMD_SLOTS 32
#define TFD_SIZE 256
#define TFD_QUEUE_MAX 256 /* les pointeurs d'écriture tournent sur 256 (max_tfd_queue_size des 22000) */
#define CMD_BUF 4096
#define RXQ_SIZE 512
#define RX_BUFS (RXQ_SIZE - 1)
#define RB_SIZE 4096
#define RB_CHUNK 32 /* RB par allocation DMA (128 Ko) */
#define FIRST_TB 20
#define RESP_MAX 2048
#define MAX_BSS 64

struct dma_blk {
	u8 *va;
	u64 pa;
	u32 len;
};

/* ---- la station (assoc.c) */
#define ST_IDLE 0
#define ST_JOINING 1
#define ST_UP 2
#define DQ_SLOTS 64     /* TFD d'une file d'émission */
#define DQ_BUF 2048     /* tampon par place : commande, en-tête 802.11, données */
#define BA_MAX 4        /* sessions block ack de réception à la fois */
struct sta_ctx {
	int state;
	struct bss bss;
	struct link_conf lc; /* HT/VHT, largeur, WMM (ht.c) */
	int have_mac, have_phy, have_binding, have_sta, have_dq;
	int tlc_on;     /* débit choisi par le micrologiciel */
	u32 tlc_rate;   /* dernier débit annoncé par le TLC (rate_n_flags) */
	u32 n_tlc;      /* notifications du TLC reçues */
	u16 seq, aid;
	u16 tid_seq[8];  /* n° de séquence des trames QoS émises, par TID */
	u16 last_seq[9]; /* dernier n° reçu par TID (8 : sans QoS), pour jeter les doublons */
	struct ro_buf ro[BA_MAX];
	int baid_of[8];  /* session (indice de ro) par TID, -1 aucune */
	u32 n_addba, n_ba_fail;
	int got_auth, got_assoc, prot_on, lost, deauth_reason;
	u16 auth_status, assoc_status;
	int wpa, keys_on;
	struct supplicant sup;
	u32 beacon_gp2;
	u64 beacon_tsf;
	int have_beacon;
	u8 dtim_count, dtim_period;
	u8 data_rate; /* indice OFDM (0 : 6 Mb/s … 7 : 54 Mb/s) */
	u32 missed;
	u64 tx_ok, tx_fail, rx_data, rx_drop, tx_drop;
	int last_signal;
};

struct iwl {
	/* fonction PCIe */
	struct pci_info pci;
	int claimed;
	volatile u8 *mmio;
	long irq; /* ligne MSI liée, -1 : sondage */
	char where[16];
	u32 hw_rev, rf_id, cnv_id;
	/* micrologiciel */
	u8 *fw_file;
	u32 fw_len;
	char fw_path[96];
	struct fw_image fw;
	/* DMA */
	struct dma_blk ctxt, rings, tfd, cbufs, fwsec[FW_MAX_SEC];
	u8 *rb_va[RX_BUFS];
	u64 rb_pa[RX_BUFS];
	u64 *rx_free;
	u32 *rx_used;
	u8 *rb_stts;
	u32 rx_read, rx_write, rx_write_actual;
	u32 txq_w;
	/* état */
	int device_enabled, fw_loaded, alive_irq, restocked, dead, verbose, poll;
	u32 last_int, n_rx_pkts, n_notif;
	long fw_load_ms;
	int waiting_seq;   /* numéro (0-255) de la commande attendue, -1 : aucune */
	u8 waiting_cmd;
	int resp_ready;
	u8 resp_grp, resp_cmd;
	u32 resp_len;
	u8 resp[RESP_MAX];
	/* micrologiciel en marche */
	int got_alive, init_complete, pnvm_complete, scan_complete;
	struct alive_info ai;
	struct nvm_info nvm;
	int have_nvm, have_mcc;
	u32 mcc_flags[NVM_CHANNELS]; /* drapeaux des canaux donnés par MCC_UPDATE (même ordre que le NVM) */
	u8 mac[6];
	u32 valid_tx, valid_rx;
	struct scan_done sd;
	struct bss bss[MAX_BSS];
	int nbss;
	u32 n_beacons, n_mpdu_bad;
	int rx_desc_warned;
	struct sta_ctx sta;
	int serving, in_rx;
};
extern struct iwl W;

/* iwl.c */
void ilog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void idbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#define qlog(...) (W.serving ? idbg(__VA_ARGS__) : ilog(__VA_ARGS__)) /* (en service : seulement avec -v) */
long now_ms(void);
void msleep(long ms);
long dma_alloc(u32 len, struct dma_blk *b);

/* trans.c */
u32 rd32(u32 off);
void wr32(u32 off, u32 v);
void set_bits(u32 off, u32 m);
void clear_bits(u32 off, u32 m);
void udelay(u32 us);
void udelay_calibrate(void);
int hw_err(u32 v);
int prph_check(const char *step);
int wfpm_enable(const char *step);
int trans_probe_access(void);
int poll_bits(u32 off, u32 want, u32 mask, u32 us);
int nic_grab(void);
void nic_release(void);
u32 prph_rd(u32 a);  /* accès tenu par l'appelant */
void prph_wr(u32 a, u32 v);
u32 prph_read(u32 a); /* prend et rend l'accès lui-même ; 0xA5A5A5A5 s'il est refusé */
void prph_write(u32 a, u32 v);
int mem_read(u32 addr, u32 *out, int n);
int trans_start_hw(void);
int trans_start_fw(void);
void trans_alive(void);
void trans_stop(int fw_running);
void dump_regs(const char *why);
void dump_fw_error(void);
/* une commande : (groupe, cmd) ; la réponse est dans W.resp -> 0, E_TIMEDOUT, E_FWERR */
long send_cmd(u8 grp, u8 cmd, const void *data, u32 len, long timeout_ms);
long send_cmd_v(u8 grp, u8 cmd, u8 ver, const void *data, u32 len, long timeout_ms);
/* traite les interruptions et la réception pendant au plus ms -> 0, E_FWERR */
int service(long ms);

/* assoc.c : la station -> 0, E_NOENT, E_ACCES, E_TIMEDOUT, E_IO */
#define E_ACCES -13
#define E_IO_ -5
int sta_join(const struct bss *b, const char *pass);
void sta_leave(int deauth);
int sta_tx_eth(const u8 *eth, u32 len);
void sta_rx(const struct rx_frame *f);
void sta_beacon(const struct bss *b, const struct rx_frame *f);
int sta_notif(u8 grp, u8 cmd, const u8 *p, u32 len); /* -> 1 si traitée */
void sta_print(void); /* état du lien (iwl serve -v, journal) */
void sta_tick(void);
void rand_bytes(u8 *out, u32 n);

/* une entrée du balayage du service « wifi » (48 o, docs/RESEAU.md 4.3, comme chip.inc du Pi) */
#define SCAN_ENT_MAX 64
struct scan_entry {
	u8 ssid[32];
	u8 ssid_len, channel, sec, pad;
	short rssi;
	u8 bssid[6];
	u8 rest[4];
};
_Static_assert(sizeof(struct scan_entry) == 48, "entrée de balayage de 48 octets");
/* iwl.c : la puce pour le service (verrou de la puce pris dedans) */
int be_scan(struct scan_entry *out, int max);
long be_join(const char *ssid, const char *key);
void be_leave(void);
int be_connected(void);
int be_state(void);
int be_rssi(void);
const char *be_ssid(void);
void be_country(const char *cc);
void be_stop(void);
/* wifisvc.c */
int wifisvc_start(void);
void wifisvc_auto(void);
const char *wifi_cc(void);

/* mvm.c (appelé par trans.c pour chaque notification) */
void mvm_rx(u8 grp, u8 cmd, u16 seq, const u8 *p, u32 len);
int mvm_up(void);
int mvm_scan(int passive, const char *ssid, long ms);
void mvm_print_scan(void);
void mvm_print_info(void);

#endif
