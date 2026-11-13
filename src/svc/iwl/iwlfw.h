/* iwlfw.h : la partie du pilote iwl qui ne touche pas au matériel (docs/etude/WIFI-PC.md) :
 *   fw.c     le fichier du micrologiciel (en-tête TLV, sections, capacités, versions des commandes) ;
 *   cmds.c   les charges utiles des commandes envoyées au micrologiciel et la lecture de ses réponses
 *            (vivant, NVM, MCC, trames reçues, fin de balayage), à l'octet près ;
 *   ieee.c   les trames 802.11 reçues pendant un balayage (balises, réponses de sonde : SSID, canal,
 *            sécurité RSN/WPA) ;
 *   crypto.c SHA-1, HMAC-SHA1, PBKDF2 (PMK), PRF-SHA1 (PTK), AES-128 et son déballage de clé
 *            (RFC 3394) : le supplicant WPA2 de l'étape suivante.
 * Ces fichiers se compilent aussi sur la machine de construction (IWL_HOST) : tools/pc/iwltest les
 * essaie contre le vrai fichier .ucode, les vecteurs de la norme et des trames d'exemple.
 *
 * Les structures et numéros viennent de la documentation publique et ont été comparés, valeur par
 * valeur, au pilote iwlwifi de Linux (v6.12) ; rien n'en est recopié : tout est réécrit ici, en
 * octets (décalages explicites, petit-boutiste), sans structures « packed ». */
#ifndef IWLFW_H
#define IWLFW_H

#ifdef IWL_HOST
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#else
#include "korinrt.h"
#endif

/* ---- petit-boutiste */
static inline u16 le16(const u8 *p) { return (u16)(p[0] | p[1] << 8); }
static inline u32 le32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static inline u64 le64(const u8 *p) { return (u64)le32(p) | (u64)le32(p + 4) << 32; }
static inline void put16(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline void put32(u8 *p, u32 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24); }
static inline void put64(u8 *p, u64 v) { put32(p, (u32)v); put32(p + 4, (u32)(v >> 32)); }

/* ======================================================================== fw.c */

#define FW_MAGIC 0x0a4c5749u       /* « IWL\n » */
#define FW_HDR_SIZE 88             /* 0, magic, texte (64), ver, build, 8 o ignorés */
#define FW_SEC_CPU_SEPARATOR 0xFFFFCCCCu /* entre les sections du LMAC et de l'UMAC */
#define FW_SEC_PAGING_SEPARATOR 0xAAAABBBBu /* entre l'UMAC et la pagination */
#define FW_MAX_SEC 96
#define FW_MAX_DRAM 64             /* entrées de chaque tableau du « context info » */

/* types de TLV utilisés (les autres sont comptés et laissés) */
#define TLV_FLAGS 18
#define TLV_SEC_RT 19
#define TLV_SEC_INIT 20
#define TLV_DEF_CALIB 22
#define TLV_PHY_SKU 23
#define TLV_SECURE_SEC_RT 24
#define TLV_NUM_OF_CPU 27
#define TLV_API_CHANGES_SET 29
#define TLV_ENABLED_CAPABILITIES 30
#define TLV_N_SCAN_CHANNELS 31
#define TLV_PAGING 32
#define TLV_FW_VERSION 36
#define TLV_CMD_VERSIONS 48
#define TLV_IML 52
#define TLV_FW_NUM_STATIONS 0x100

/* capacités (TLV 30) et changements d'API (TLV 29) : numéros de bit */
#define CAPA_LAR_SUPPORT 1
#define CAPA_UMAC_SCAN 2
#define CAPA_DS_PARAM_SET_IE 9
#define CAPA_DQA_SUPPORT 12
#define CAPA_SOC_LATENCY 37
#define CAPA_ULTRA_HB_CHANNELS 48
#define CAPA_SET_LTR_GEN2 50
#define CAPA_FW_RESET_HANDSHAKE 57
#define CAPA_BT_MPLUT 67
#define CAPA_MCC_UPDATE_11AX 89
#define API_WIFI_MCC_UPDATE 9
#define API_STA_TYPE 30
#define API_ADAPTIVE_DWELL 32
#define API_REGULATORY_NVM_INFO 48
#define API_REDUCED_SCAN_CONFIG 56

struct fw_sec {
	const u8 *data; /* charge utile (après le décalage de 4 o) */
	u32 len;
	u32 offset;     /* adresse dans la puce, ou un séparateur */
};

struct fw_image {
	char human[65];          /* « release/core74::3cda2a2c » */
	u32 ucode_ver, build;    /* de l'en-tête */
	u32 ver[3];              /* TLV 36 : 77, 0x3cda2a2c, 0 → « 77.3cda2a2c.0 » */
	int has_ver;
	int nsec;
	struct fw_sec sec[FW_MAX_SEC];
	int n_lmac, n_umac, n_paging; /* sections de chaque part (séparateurs exclus) */
	int umac_first, paging_first; /* rang de la première section de l'UMAC, de la pagination */
	int n_init;              /* sections INIT (TLV 20) : 0 pour un micrologiciel « unifié » */
	u32 capa[4], api[4];     /* 128 bits chacun */
	u32 phy_config;          /* TLV 23 */
	u32 calib_flow, calib_event; /* TLV 22 de l'image REGULAR (0) */
	u32 n_scan_channels, num_stations, num_cpu, flags;
	const u8 *cmd_versions;  /* TLV 48 : (cmd, groupe, version, version de la notification) */
	u32 n_cmd_versions;
	int paging_tlv, iml;
	u32 n_tlv, n_dbg_tlv;
};

#define FWE_SHORT -1   /* fichier trop court */
#define FWE_MAGIC -2   /* pas un fichier TLV d'iwlwifi */
#define FWE_TLV -3     /* un TLV dépasse la fin */
#define FWE_MANY -4    /* trop de sections */
#define FWE_LAYOUT -5  /* séparateurs absents (pas le format « gen2 ») */

int fw_parse(struct fw_image *fw, const u8 *d, u32 len);
const char *fw_err(int e);
int fw_capa(const struct fw_image *fw, unsigned bit);
int fw_api(const struct fw_image *fw, unsigned bit);
/* version de la commande (groupe, cmd) ; dflt si absente ou 99 (« inconnue ») */
int fw_cmd_ver(const struct fw_image *fw, u8 grp, u8 cmd, int dflt);
int fw_notif_ver(const struct fw_image *fw, u8 grp, u8 cmd, int dflt);
u32 fw_total_bytes(const struct fw_image *fw);

/* ---- le nom du fichier, d'après CSR_HW_REV et CSR_HW_RF_ID (comme iwlwifi : type du MAC
 * (bits 15:4) et étape (bits 3:0) ; type de RF (bits 23:12)) → « iwlwifi-QuZ-a0-hr-b0 » ou 0 */
const char *fw_name_for(u32 hw_rev, u32 rf_id);

/* ======================================================================== cmds.c */

/* groupes et commandes (identifiants publics de l'API des commandes hôtes) */
#define GRP_LEGACY 0x0
#define GRP_LONG 0x1
#define GRP_SYSTEM 0x2
#define GRP_MAC_CONF 0x3
#define GRP_PHY_OPS 0x4
#define GRP_DATA_PATH 0x5
#define GRP_REGULATORY 0xc
#define GRP_DEBUG 0xf

#define C_ALIVE 0x01
#define C_REPLY_ERROR 0x02
#define C_INIT_COMPLETE 0x04
#define C_SCAN_CFG 0x0c
#define C_SCAN_REQ_UMAC 0x0d
#define C_SCAN_ABORT_UMAC 0x0e
#define C_SCAN_COMPLETE_UMAC 0x0f
#define C_TX_ANT_CFG 0x98
#define C_BT_CONFIG 0x9b
#define C_SCAN_ITER_COMPLETE_UMAC 0xb5
#define C_RX_MPDU 0xc1
#define C_MCC_UPDATE 0xc8
#define C_MCC_CHUB_UPDATE 0xc9
#define C_DEBUG_LOG_MSG 0xf7
#define C_SYS_SOC_CONFIG 0x01
#define C_SYS_INIT_EXT_CFG 0x03
#define C_DP_DQA_ENABLE 0x00
#define C_REG_NVM_ACCESS_COMPLETE 0x00
#define C_REG_NVM_GET_INFO 0x02
#define C_REG_PNVM_INIT_COMPLETE 0xfe

#define ALIVE_OK 0xCAFE

struct alive_info {
	u16 status, flags;
	u32 lmac_major, lmac_minor, umac_major, umac_minor;
	u32 lmac_err, umac_err, scd_base, lmac_log;
	u32 sku_id[3];
	int version_by_len; /* 3, 4, 5/6 : d'après la longueur */
};
int parse_alive(const u8 *p, u32 len, struct alive_info *a);

#define NVM_CHANNELS 51 /* 1-14 puis 36…181 (les canaux « ext » des 22000) */
extern const u8 nvm_channel_list[NVM_CHANNELS];
struct nvm_info {
	u32 flags, mac_sku, tx_chains, rx_chains, lar, n_channels;
	u16 nvm_version;
	u8 board_type, n_hw_addrs;
	u32 ch_flags[NVM_CHANNELS];
};
#define NVM_INFO_V4_LEN 468
int parse_nvm_info(const u8 *p, u32 len, struct nvm_info *n);
#define NVM_CH_VALID 0x01
#define NVM_CH_ACTIVE 0x08
#define NVM_CH_RADAR 0x10
#define NVM_SKU_24 0x01
#define NVM_SKU_52 0x02
#define NVM_SKU_11N 0x04
#define NVM_SKU_11AC 0x08
#define NVM_SKU_11AX 0x10

struct mcc_info {
	u32 status, n_channels;
	char alpha2[3];
	u8 source;
};
int parse_mcc_resp(const u8 *p, u32 len, int v4, struct mcc_info *m);

/* MAC : CSR « strap » puis OTP (deux mots lus à 0x388/0x38C puis 0x380/0x384) */
void mac_from_csr(u32 w0, u32 w1, u8 mac[6]);
int mac_valid(const u8 mac[6]);

/* ---- le balayage (SCAN_REQ_UMAC versions 14 à 17 : même disposition, 1940 octets) */
#define SCAN_REQ_V14_LEN 1940
#define SCAN_MAX_CH 67
struct scan_params {
	int version;       /* 14..17 */
	int passive;       /* aucune sonde (balises seulement) */
	const u8 *mac;     /* adresse source des sondes */
	int n_ch;
	u8 ch[SCAN_MAX_CH];
	const u8 *ssid;    /* sonde dirigée (0 : générique) */
	int ssid_len;
	u32 uid;
};
/* -> longueur écrite (SCAN_REQ_V14_LEN) ; buf de 2048 octets au moins */
u32 build_scan_req(u8 *buf, const struct scan_params *sp);
/* décalages publics, pour l'essai */
#define SR_GENERAL 8
#define SR_CHANNEL (SR_GENERAL + 36)
#define SR_PERIODIC (SR_CHANNEL + 4 + 67 * 8)
#define SR_PROBE (SR_PERIODIC + 12)
#define SR_PREQ_BUF (SR_PROBE + 20)
#define SR_DIRECT (SR_PROBE + 532 + 4)

struct scan_done {
	u32 uid;
	u8 last_schedule, last_iter, status, ebs_status;
	u32 time_from_last_iter;
};
int parse_scan_complete(const u8 *p, u32 len, struct scan_done *d);

/* ---- une trame reçue (REPLY_RX_MPDU_CMD, descripteur de 48 o des 22000) */
#define RX_DESC_V1 48
struct rx_frame {
	const u8 *frame;  /* l'en-tête 802.11 */
	u32 len;          /* sans le bourrage */
	u32 status;
	int signal;       /* dBm (le plus fort des deux chaînes), -128 inconnu */
	u8 channel;
	int pad;          /* 2 octets de bourrage après l'en-tête (enlevés par rx_frame_copy) */
	u32 hdrlen;
};
int parse_rx_mpdu(const u8 *p, u32 len, struct rx_frame *f);
#define RX_CRC_OK 0x1
#define RX_OVERRUN_OK 0x2

/* ---- context info (1792 o) : version (mac_id, 0, taille en mots), contrôle (TFD longs, taille de l'anneau
 * de réception en log2, taille des RB), RBD libres, utilisés, statut, file des commandes (adresse, log2 - 3),
 * puis les adresses des sections : UMAC à +192, LMAC à +704, pages à +1216 (64 entrées de 8 o chacune).
 * rings_pa : trois pages (RBD libres, utilisés, statut) ; sec_pa[i] : adresse de la section i de fw */
#define CTXT_INFO_SIZE 1792
#define CI_RXQ_LOG2 9 /* 512 RBD */
#define CI_RB_4K 4
void build_ctxt_info(u8 *c, u32 hw_rev, u64 rings_pa, u64 tfd_pa, const struct fw_image *fw, const u64 *sec_pa);

/* ======================================================================== ieee.c */

#define SEC_OPEN 0  /* (même codage que l'entrée de 48 o de wifi_scan, docs/RESEAU.md 4.3) */
#define SEC_WEP 1
#define SEC_WPA 2
#define SEC_WPA2 3
#define SEC_WPA3 4  /* proposé : SAE seulement (WPA2/WPA3 de transition : SEC_WPA2, drapeau AKM) */
#define SEC_ENT 5   /* proposé : 802.1X (entreprise), pas pris en charge */

#define AKM_PSK 0x01
#define AKM_SAE 0x02
#define AKM_8021X 0x04
#define AKM_PSK256 0x08
#define AKM_OWE 0x10
#define AKM_OTHER 0x80
#define CIPH_CCMP 0x01
#define CIPH_TKIP 0x02
#define CIPH_GCMP 0x04
#define CIPH_WEP 0x08
#define CIPH_OTHER 0x80

struct bss {
	u8 bssid[6];
	u8 ssid[33];
	u8 ssid_len;
	u8 channel;     /* de l'élément DS (2,4 GHz) ou HT, sinon celui de la réception */
	int signal;     /* dBm */
	u16 capab, beacon_int;
	u8 sec;         /* SEC_* */
	u8 akm, pairwise, group, wpa1, rsn, mfpc, mfpr;
	u8 ht, vht, he;
	int probe_resp;
	int seen;
};
/* une balise (sous-type 8) ou une réponse de sonde (5) : l'en-tête (24 o) et le corps (après le
 * bourrage éventuel) → 1 et *b rempli ; 0 sinon */
int parse_beacon(const u8 *hdr, const u8 *body, u32 blen, int rx_channel, int signal, struct bss *b);
const char *sec_name(const struct bss *b);
/* RSN (élément 48) ou WPA (élément 221 00:50:F2:01) -> 0 ou < 0 si illisible */
int parse_rsn(const u8 *ie, u32 len, int wpa1, struct bss *b);

/* ======================================================================== crypto.c */

void sha1(const u8 *data, u32 len, u8 out[20]);
void hmac_sha1(const u8 *key, u32 klen, const u8 *data, u32 len, u8 out[20]);
void hmac_sha1_v(const u8 *key, u32 klen, int n, const u8 *const *data, const u32 *lens, u8 out[20]);
/* PMK de WPA2-personnel : PBKDF2-HMAC-SHA1(phrase, SSID, 4096, 32) */
void wpa_pmk(const char *pass, const u8 *ssid, u32 ssid_len, u8 pmk[32]);
void pbkdf2_sha1(const u8 *pass, u32 plen, const u8 *salt, u32 slen, u32 iter, u8 *out, u32 olen);
/* PRF-SHA1 de 802.11i (clé, étiquette, données) -> olen octets */
void prf_sha1(const u8 *key, u32 klen, const char *label, const u8 *data, u32 dlen, u8 *out, u32 olen);
/* PTK = PRF-384(PMK, « Pairwise key expansion », min/max(AA,SPA) || min/max(ANonce,SNonce)) :
 * KCK 16, KEK 16, TK 16 (CCMP) */
void wpa_ptk(const u8 pmk[32], const u8 aa[6], const u8 spa[6], const u8 anonce[32], const u8 snonce[32],
	     u8 ptk[48]);
/* MIC d'une trame EAPOL-Key (champ MIC à zéro) : HMAC-SHA1 tronqué à 16 octets (version 2) */
void eapol_mic(const u8 kck[16], const u8 *eapol, u32 len, u8 mic[16]);
void aes128_encrypt(const u8 key[16], const u8 in[16], u8 out[16]);
/* RFC 3394 : n blocs de 8 o chiffrés (n+1 en entrée) -> 0 si l'IV est juste */
int aes_unwrap(const u8 kek[16], const u8 *in, u32 inlen, u8 *out);

#endif
