/* assoc.c : la station 802.11 sur le micrologiciel MVM (docs/etude/WIFI-PC.md 6 et 7 ; docs/etude/PC-PILOTES.md
 * « Wi-Fi AX201, étapes 2 à 4 » et « débit »).
 *
 * Joindre un point d'accès, comme le fait iwlwifi avec mac80211 (séquences décrites et réécrites) :
 *   contexte MAC (station, BSSID, QoS/EDCA, protection, balises reçues tant qu'on n'est pas associé),
 *   contexte PHY (canal primaire, largeur 20/40/80 MHz et position du primaire), RLC (chaînes de réception),
 *   liaison MAC↔PHY, station du point d'accès (ADD_STA, n° 0), sa file de gestion (SCD_QUEUE_CONFIG, TID
 *   15), protection de session, authentification « système ouvert », demande d'association (HT, VHT et WMM
 *   annoncés si le point d'accès les offre : ht.c), contexte MAC « associé », station mise à jour (largeur,
 *   MIMO, agrégation, QoS), file des données QoS (TID 0), débit choisi par le micrologiciel (TLC) ; en WPA2 :
 *   EAPOL à 4 voies (mlme.c), puis la PTK et la GTK dans la puce (ADD_STA_KEY) : CCMP en matériel.
 * Réception agrégée : le point d'accès demande une session block ack (ADDBA) ; la puce la tient (accusés,
 * BAR) une fois le BAID alloué, et l'hôte remet les trames dans l'ordre (ht.c, comme iwl_mvm_reorder).
 * /etc/iwl.txt (facultatif, lu à chaque connexion, jamais écrit) : norme=ag|n|ac, largeur=20|40|80, ba=0|1,
 * tlc=0|1 : pour revenir en arrière sans reconstruire. */
#include "iwl.h"

#define STA_ID 0
#define MAC_ID 0
#define PHY_ID 0
#define MGMT_TID 15
#define DATA_TID 0
#define C_PHY_CONTEXT 0x08
#define C_ADD_STA_KEY 0x17
#define C_ADD_STA 0x18
#define C_REMOVE_STA 0x19
#define C_TX 0x1c
#define C_MAC_CONTEXT 0x28
#define C_BINDING 0x2b
#define C_MISSED_BEACONS 0xa2
#define C_BAR_FRAME_RELEASE 0xc2
#define C_FRAME_RELEASE 0xc3
#define C_DP_RLC_CONFIG 0x08
#define C_DP_TLC_CONFIG 0x0f
#define C_DP_RX_BAID_CFG 0x16
#define C_DP_SCD_QUEUE_CONFIG 0x17
#define C_DP_TLC_NOTIF 0xf7
#define C_MAC_SESSION_PROT 0x05
#define C_MAC_SESSION_PROT_NOTIF 0xfb
#define ACT_ADD 1
#define ACT_MODIFY 2
#define ACT_REMOVE 3
#define TXF_CMD_RATE 0x1
#define TXF_ENCRYPT_DIS 0x2
#define TXF_HIGH_PRI 0x4
#define RATE_OFDM (1u << 8)
#define RATE_ANT_B (2u << 14)
#define STA_MOD_UAPSD_ACS (1u << 2)
#define STA_MOD_ADD_BA (1u << 3)
#define STA_MOD_REMOVE_BA (1u << 4)
#define BA_STATUS_DECLINED 37

#define S (W.sta)

/* ---- réglages (/etc/iwl.txt) */
static struct {
	int mode, width, ba, tlc;
} cfg;

static void read_cfg(void)
{
	cfg.mode = LM_VHT;
	cfg.width = LW_80;
	cfg.ba = 1;
	cfg.tlc = 1;
	static char buf[512];
	long fd = sys_open("/etc/iwl.txt", O_READ);
	if (fd < 0)
		return;
	long n = sys_read(fd, buf, sizeof buf - 1);
	sys_close(fd);
	if (n <= 0)
		return;
	buf[n] = 0;
	for (char *l = buf; *l;) {
		char *e = l;
		while (*e && *e != '\n')
			e++;
		char save = *e;
		*e = 0;
		while (*l == ' ' || *l == '\t')
			l++;
		if (*l != '#') {
			if (!strncmp(l, "norme=", 6)) {
				const char *v = l + 6;
				/* (« ax » : le plus haut que le pilote sait faire, VHT) */
				cfg.mode = !strncmp(v, "ac", 2) || !strncmp(v, "ax", 2) ? LM_VHT : (v[0] == 'n') ? LM_HT : LM_AG;
			} else if (!strncmp(l, "largeur=", 8)) {
				int w = atoi(l + 8);
				cfg.width = w >= 80 ? LW_80 : w >= 40 ? LW_40 : LW_20;
			} else if (!strncmp(l, "ba=", 3))
				cfg.ba = l[3] != '0';
			else if (!strncmp(l, "tlc=", 4))
				cfg.tlc = l[4] != '0';
		}
		*e = save;
		l = *e ? e + 1 : e;
	}
	ilog("réglages de /etc/iwl.txt : norme %s, largeur %d MHz au plus, block ack %s, débit %s\n",
	     cfg.mode == LM_VHT ? "ac" : cfg.mode == LM_HT ? "n" : "a/g", cfg.width == LW_80 ? 80 : cfg.width == LW_40 ? 40 : 20,
	     cfg.ba ? "oui" : "non", cfg.tlc ? "choisi par la puce" : "fixé par l'hôte");
}

/* ---- hasard : rdtsc et l'horloge, passés par SHA-1 (nonces du supplicant) */
static u64 tsc(void)
{
	u32 lo, hi;
	__asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
	return (u64)hi << 32 | lo;
}

void rand_bytes(u8 *out, u32 n)
{
	static u8 pool[20];
	static u64 ctr;
	while (n) {
		u8 b[96];
		memcpy(b, pool, 20);
		/* (E65, P13) l'aléa du noyau (SYS_GETRANDOM : RDRAND et son réservoir) ; le reste n'est qu'un appoint */
		if (korin_sys2(SYS_GETRANDOM, b + 62, 32) != 32)
			memset(b + 62, 0, 32);
		put64(b + 20, tsc());
		put64(b + 28, (u64)now_ms());
		put64(b + 36, ++ctr);
		memcpy(b + 44, W.mac, 6);
		put32(b + 50, rd32(CSR_INT));
		put64(b + 54, tsc());
		sha1(b, 94, pool);
		u32 k = n < 16 ? n : 16;
		memcpy(out, pool, k);
		out += k;
		n -= k;
	}
}

static long cmd_status(const char *what, u8 grp, u8 c, u8 ver, const void *d, u32 len)
{
	long r = send_cmd_v(grp, c, ver, d, len, 2000);
	if (r) {
		ilog("ÉCHEC : %s (commande %x.%02x) : %s\n", what, grp, c, r == E_FWERR ? "erreur du micrologiciel" : "délai");
		return r;
	}
	idbg("%s : réponse de %u octets, statut %08x\n", what, W.resp_len, W.resp_len >= 4 ? le32(W.resp) : 0);
	return 0;
}

/* ---- files d'émission : 0 la gestion (TID 15, et tout sans QoS), 1 les données QoS (TID 0) */
struct txq {
	struct dma_blk tfd, bc, buf;
	u32 w, r;
	int qid, on;
	u8 tid;
};
static struct txq Q[2];
#define QM (&Q[0])
#define QD (&Q[1])

static int alloc_queue_mem(struct txq *q)
{
	if (q->tfd.va)
		return 0;
	if (dma_alloc(DQ_SLOTS * TFD_SIZE, &q->tfd) || dma_alloc(4096, &q->bc) || dma_alloc(DQ_SLOTS * DQ_BUF, &q->buf))
		return -1;
	return 0;
}

static int queue_cfg(struct txq *q, int add, u8 tid)
{
	u8 c[SCD_QUEUE_LEN];
	memset(c, 0, sizeof c);
	put32(c + 0, add ? 0 : 1);
	put32(c + 4, 1u << STA_ID);
	if (add) {
		c[8] = tid;
		put32(c + 12, 0);
		put32(c + 16, 3); /* log2(64) - 3 */
		put64(c + 20, q->bc.pa);
		put64(c + 28, q->tfd.pa);
	} else
		put32(c + 8, q->tid);
	const char *what = tid == MGMT_TID ? "file d'émission (gestion)" : "file d'émission (données QoS)";
	if (cmd_status(add ? what : "file d'émission retirée", GRP_DATA_PATH, C_DP_SCD_QUEUE_CONFIG, 0, c, sizeof c))
		return -1;
	if (!add) {
		q->on = 0;
		return 0;
	}
	if (W.resp_len != 8) {
		ilog("ÉCHEC : réponse de la file de %u octets\n", W.resp_len);
		return -1;
	}
	q->qid = le16(W.resp);
	q->w = q->r = le16(W.resp + 4) & (TFD_QUEUE_MAX - 1);
	q->tid = tid;
	q->on = 1;
	idbg("file %d (TID %u), pointeur %u\n", q->qid, tid, q->w);
	return 0;
}

/* ---- contextes */

/* EDCA d'une catégorie (ordre du micrologiciel : BK, BE, VI, VO) : celui de l'élément WMM du point d'accès,
 * sinon les valeurs par défaut de 802.11 (mac80211) ; sans QoS, celles d'avant (AIFS 2, CW 15-1023) */
static void edca(u8 *a, int ucode_ac)
{
	static const u8 aci_of[4] = {1, 0, 2, 3}; /* BK, BE, VI, VO -> ACI de WMM */
	static const u16 def[4][4] = {{15, 1023, 7, 0}, {15, 1023, 3, 0}, {7, 15, 2, 94}, {3, 7, 2, 47}};
	u16 cmin = 15, cmax = 1023, txop = 0;
	u8 aifs = 2;
	if (S.lc.wmm && S.bss.wmm_params) {
		const u8 *p = S.bss.wmm_ac[aci_of[ucode_ac]];
		aifs = p[0] & 15;
		cmin = (u16)((1u << (p[1] & 15)) - 1);
		cmax = (u16)((1u << (p[1] >> 4)) - 1);
		txop = le16(p + 2);
	} else if (S.lc.wmm) {
		cmin = def[ucode_ac][0];
		cmax = def[ucode_ac][1];
		aifs = (u8)def[ucode_ac][2];
		txop = def[ucode_ac][3];
	}
	put16(a, cmin);
	put16(a + 2, cmax);
	a[4] = aifs;
	a[5] = (u8)(1u << (ucode_ac + 1)); /* BK 1, BE 2, VI 3, VO 4 (FIFO EDCA gen2) */
	put16(a + 6, (u16)(txop * 32));
}

static int mac_ctx(u32 action, int assoc)
{
	u8 c[MAC_CTX_LEN];
	memset(c, 0, sizeof c);
	put32(c + 0, MAC_ID);
	put32(c + 4, action);
	put32(c + 8, 5); /* FW_MAC_TYPE_BSS_STA */
	put32(c + 12, 0); /* tsf_id */
	memcpy(c + 16, W.mac, 6);
	memcpy(c + 24, S.bss.bssid, 6);
	/* débits des accusés : les débits de base du point d'accès, et ceux qu'impose 802.11 en dessous */
	u32 cck = 0x1, ofdm = 0x1; /* 1 Mb/s, 6 Mb/s toujours */
	static const u8 cckr[4] = {2, 4, 11, 22};
	static const u8 ofdmr[8] = {12, 18, 24, 36, 48, 72, 96, 108};
	for (int i = 0; i < S.bss.n_rates; i++) {
		if (!(S.bss.rates[i] & 0x80))
			continue;
		u8 r = S.bss.rates[i] & 0x7f;
		for (int k = 0; k < 4; k++)
			if (cckr[k] == r)
				cck |= 0xfu >> (3 - k); /* (et les plus bas) */
		for (int k = 0; k < 8; k++)
			if (ofdmr[k] == r)
				ofdm |= 1u << k;
	}
	if (ofdm & ~0x7u)
		ofdm |= 0x14; /* 12 et 24 Mb/s sous des débits de base plus hauts */
	put32(c + 32, cck);
	put32(c + 36, ofdm);
	/* protection : ERP (2,4 GHz) ; HT selon le mode de protection du point d'accès (comme mvm) */
	u32 prot = (S.lc.prot & 1) ? 1u << 3 : 0;
	if (S.lc.mode >= LM_HT) {
		u32 pm = S.bss.ht_op_mode & 3;
		if (pm == 1 || pm == 2 || (pm == 3 && S.lc.width > LW_20))
			prot |= (1u << 23) | (1u << 24); /* HT_PROT, FAT_PROT */
	}
	put32(c + 40, prot);
	put32(c + 44, (S.bss.capab & 0x0020) ? 1u << 5 : 0); /* préambule court */
	put32(c + 48, (S.bss.capab & 0x0400) || S.bss.channel > 14 ? 1u << 4 : 0); /* intervalle court */
	put32(c + 52, (1u << 2) | (assoc ? 0 : 1u << 6));    /* groupe ; balises tant que non associé */
	u32 qf = 0;
	if (S.lc.wmm)
		qf |= 1; /* UPDATE_EDCA */
	if (S.lc.mode >= LM_HT)
		qf |= 2; /* TGN : HT */
	put32(c + 56, qf);
	for (int ac = 0; ac < 4; ac++)
		edca(c + 60 + 8 * ac, ac);
	u8 *st = c + 100;
	if (assoc) {
		u32 bi = S.bss.beacon_int ? S.bss.beacon_int : 100;
		u32 dtim = S.dtim_period ? S.dtim_period : S.bss.dtim_period ? S.bss.dtim_period : 1;
		u64 offs = (u64)S.dtim_count * bi * 1024;
		put32(st + 0, 1);
		put32(st + 4, (u32)(S.beacon_gp2 + offs));
		put64(st + 8, S.beacon_tsf + offs);
		put32(st + 16, bi);
		put32(st + 24, bi * dtim);
		put32(st + 32, 10);
		put32(st + 36, S.aid);
		put32(st + 40, S.beacon_gp2);
	} else {
		put32(st + 16, S.bss.beacon_int ? S.bss.beacon_int : 100);
		put32(st + 32, 10);
	}
	return (int)cmd_status(assoc ? "contexte MAC (associé)" : "contexte MAC", GRP_LONG, C_MAC_CONTEXT, 0, c, sizeof c);
}

static int phy_ctx(u32 action)
{
	u8 c[PHY_CTX_LEN];
	memset(c, 0, sizeof c);
	put32(c + 0, PHY_ID);
	put32(c + 4, action);
	put32(c + 8, S.lc.primary ? S.lc.primary : S.bss.channel);
	c[12] = S.bss.channel <= 14 ? 1 : 0; /* PHY_BAND_24 / PHY_BAND_5 */
	c[13] = (u8)S.lc.width;              /* IWL_PHY_CHANNEL_MODE20/40/80 */
	c[14] = S.lc.ctrl_pos;               /* position du primaire */
	put32(c + 16, 0); /* LMAC 0 (pas de CDB) */
	if (cmd_status("contexte PHY", GRP_LONG, C_PHY_CONTEXT, 0, c, sizeof c))
		return -1;
	if (action == ACT_REMOVE)
		return 0;
	u8 r[RLC_LEN]; /* RLC v2 : chaînes valides, deux au repos et en MIMO (diversité) */
	memset(r, 0, sizeof r);
	put32(r + 0, PHY_ID);
	put32(r + 4, (W.valid_rx << 1) | (2u << 10) | (2u << 12));
	return (int)cmd_status("chaînes de réception (RLC)", GRP_DATA_PATH, C_DP_RLC_CONFIG, 2, r, sizeof r);
}

static int binding(u32 action)
{
	u8 c[BINDING_LEN];
	memset(c, 0xff, sizeof c);
	put32(c + 0, PHY_ID);
	put32(c + 4, action);
	put32(c + 8, MAC_ID);
	put32(c + 20, PHY_ID);
	put32(c + 24, 0); /* LMAC 0 */
	if (cmd_status("liaison MAC-PHY", GRP_LONG, C_BINDING, 0, c, sizeof c))
		return -1;
	if (W.resp_len >= 4 && le32(W.resp)) {
		ilog("ÉCHEC : liaison refusée (statut %u)\n", le32(W.resp));
		return -1;
	}
	return 0;
}

static int session_prot(u32 action, u32 ms)
{
	u8 c[SESSION_PROT_LEN];
	memset(c, 0, sizeof c);
	put32(c + 0, MAC_ID);
	put32(c + 4, action);
	put32(c + 8, 0); /* SESSION_PROTECT_CONF_ASSOC */
	put32(c + 12, ms * 1000 / 1024);
	S.prot_on = 0;
	return (int)cmd_status("protection de session", GRP_MAC_CONF, C_MAC_SESSION_PROT, 0, c, sizeof c);
}

/* la station du point d'accès ; associée : ses capacités HT/VHT (largeur, flux, agrégation) et QoS, comme
 * iwl_mvm_sta_send_to_fw */
static int add_sta(int modify)
{
	u8 c[ADD_STA_LEN];
	memset(c, 0, sizeof c);
	c[0] = (u8)modify;
	put16(c + 2, 0xffff); /* pas d'agrégation à l'émission */
	put32(c + 4, MAC_ID);
	memcpy(c + 8, S.bss.bssid, 6);
	c[16] = STA_ID;
	u32 fl = 0, msk = (3u << 26) | (3u << 28) | (1u << 17); /* largeur, MIMO, protection MIMO */
	if (modify && S.lc.mode >= LM_HT) {
		if (S.lc.width == LW_80)
			fl |= 3u << 26; /* (80 puis 40, comme mvm) */
		else if (S.lc.width == LW_40)
			fl |= 1u << 26;
		if (S.lc.nss >= 2)
			fl |= 1u << 28; /* MIMO2 */
		if (S.lc.smps == 1)
			fl |= 1u << 17; /* économie MIMO dynamique : RTS avant le MIMO */
		msk |= (0xfu << 19) | (7u << 23);
		u32 agg = S.lc.agg_size > 9 ? 9 : S.lc.agg_size;
		fl |= agg << 19 | (S.lc.dens & 7) << 23;
	}
	put32(c + 20, fl);
	put32(c + 24, msk);
	c[35] = 0; /* IWL_STA_LINK */
	put16(c + 36, S.aid);
	if (modify && S.lc.wmm) {
		c[17] |= STA_MOD_UAPSD_ACS;
		c[46] = 128; /* sp_length (pas d'U-APSD) */
		c[47] = 0;
	}
	if (cmd_status(modify ? "station (associée)" : "station du point d'accès", GRP_LONG, C_ADD_STA, 0, c, sizeof c))
		return -1;
	if (W.resp_len >= 4 && (le32(W.resp) & 0xff) != 1) {
		ilog("ÉCHEC : station refusée (statut %u)\n", le32(W.resp));
		return -1;
	}
	return 0;
}

/* le débit choisi par le micrologiciel (TLC_MNG_CONFIG_CMD v4) ; width : 20 MHz tant que la station n'est
 * pas autorisée (comme rs-fw.c) */
static int tlc_config(int width)
{
	if (!cfg.tlc || !fw_capa(&W.fw, CAPA_TLC_OFFLOAD) || fw_cmd_ver(&W.fw, GRP_DATA_PATH, C_DP_TLC_CONFIG, 0) != 4) {
		if (cfg.tlc)
			ilog("débit : pas de TLC version 4 dans ce micrologiciel : débit fixé par l'hôte\n");
		return -1;
	}
	u8 c[TLC_CFG_LEN];
	build_tlc_cmd(c, &S.lc, STA_ID, (u8)W.valid_tx, width);
	if (cmd_status("débit choisi par la puce (TLC)", GRP_DATA_PATH, C_DP_TLC_CONFIG, 0, c, sizeof c))
		return -1;
	S.tlc_on = 1;
	idbg("TLC : mode %u, largeur %u, chaînes %x, SGI %x, drapeaux %x, a/g %03x, MCS %03x/%03x\n", c[5], c[4], c[6], c[7],
	     le16(c + 8), le16(c + 10), le16(c + 12), le16(c + 18));
	return 0;
}

static int install_key(int group)
{
	u8 c[ADD_STA_KEY_LEN];
	memset(c, 0, sizeof c);
	c[0] = STA_ID;
	c[1] = (u8)(group ? 1 : 0); /* place dans la table des clés */
	u16 kf = 1u << 3;           /* WEP_KEY_MAP (clé de la station) */
	if (group) {
		kf |= (u16)((S.sup.gtk_idx & 3) << 8) | (1u << 14); /* n° de clé, multicast */
		if (S.sup.group_cipher == CIPH_TKIP) {
			kf |= 3;
			memcpy(c + 4, S.sup.gtk, 16);
			memcpy(c + 52, S.sup.gtk + 16, 8); /* MIC de réception */
			memcpy(c + 60, S.sup.gtk + 24, 8); /* MIC d'émission */
		} else {
			kf |= 2;
			memcpy(c + 4, S.sup.gtk, 16);
		}
		memcpy(c + 36, S.sup.gtk_rsc, 6); /* compteur de réception */
	} else {
		kf |= 2; /* CCM */
		memcpy(c + 4, S.sup.ptk + 32, 16);
	}
	put16(c + 2, kf);
	if (cmd_status(group ? "clé de groupe" : "clé de la station", GRP_LONG, C_ADD_STA_KEY, 0, c, sizeof c))
		return -1;
	if (W.resp_len >= 4 && le32(W.resp) != 1) {
		ilog("ÉCHEC : clé refusée par la puce (statut %u)\n", le32(W.resp));
		return -1;
	}
	return 0;
}

/* ---- émission : commande TX + en-tête + données dans le tampon de la place, 3 TB. En-tête de 24 octets,
 * ou de 26 en QoS (la puce saute les 2 octets de bourrage : TX_CMD_OFFLD_PAD) */
static int tx_raw(struct txq *q, const u8 *frame, u32 len, u32 hdrlen, u32 flags, u32 rate)
{
	if (!S.have_sta || !q->on || len + 4 + TX_CMD_LEN > DQ_BUF || (hdrlen != 24 && hdrlen != 26))
		return -1;
	long end = now_ms() + 200;
	while (((q->w - q->r) & (TFD_QUEUE_MAX - 1)) >= DQ_SLOTS - 2) { /* file pleine : on attend les réponses */
		if (now_ms() > end || service(5) == E_FWERR) {
			S.tx_drop++;
			return -1;
		}
	}
	u32 slot = q->w & (DQ_SLOTS - 1);
	u8 *b = q->buf.va + slot * DQ_BUF;
	u64 pa = q->buf.pa + (u64)slot * DQ_BUF;
	b[0] = C_TX;
	b[1] = 0;
	put16(b + 2, (u16)((q->qid & 0x1f) << 8 | slot));
	u8 *tc = b + 4;
	memset(tc, 0, TX_CMD_LEN);
	put16(tc + 0, len);
	put16(tc + 2, (u16)((hdrlen / 2) << 8 | ((hdrlen & 3) ? 1u << 13 : 0))); /* taille de l'en-tête (mots), bourrage */
	put32(tc + 4, flags);
	put32(tc + 16, rate);
	memcpy(b + 4 + TX_CMD_LEN, frame, len);
	u8 *t = q->tfd.va + slot * TFD_SIZE;
	memset(t, 0, TFD_SIZE);
	u32 head = 4 + TX_CMD_LEN + hdrlen; /* 48 ou 50 */
	put16(t + 2, FIRST_TB);
	put64(t + 4, pa);
	put16(t + 12, (head - FIRST_TB + 3) & ~3u);
	put64(t + 14, pa + FIRST_TB);
	int ntb = 2;
	if (len > hdrlen) {
		put16(t + 22, len - hdrlen);
		put64(t + 24, pa + head);
		ntb = 3;
	}
	put16(t, ntb);
	u32 filled = 2 + 10 * (u32)ntb, chunks = (filled + 63) / 64 - 1;
	put16(q->bc.va + 2 * slot, ((len + 3) / 4) | (chunks << 12));
	q->w = (q->w + 1) & (TFD_QUEUE_MAX - 1);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	wr32(HBUS_TARG_WRPTR, q->w | ((u32)q->qid << 16));
	return 0;
}

static u32 mgmt_rate(void)
{
	return RATE_OFDM | RATE_ANT_B; /* 6 Mb/s OFDM */
}

static u32 data_rate(void)
{
	return S.data_rate | RATE_OFDM | RATE_ANT_B;
}

static int tx_mgmt(const u8 *f, u32 len)
{
	return tx_raw(QM, f, len, 24, TXF_CMD_RATE | TXF_ENCRYPT_DIS | TXF_HIGH_PRI, mgmt_rate());
}

/* une trame Ethernet vers le point d'accès : QoS data (TID 0) sur la file des données si la station est QoS,
 * sinon données simples sur la file de gestion ; eapol : débit de l'hôte, priorité haute (comme mvm avant
 * l'autorisation) */
static int tx_eth(const u8 *eth, u32 len, int eapol)
{
	static u8 f[1700];
	u32 n, hl;
	struct txq *q;
	if (S.have_dq) {
		n = eth_to_80211_qos(f, eth, len, S.bss.bssid, W.mac, S.tid_seq[DATA_TID], S.keys_on, DATA_TID);
		S.tid_seq[DATA_TID] = (u16)((S.tid_seq[DATA_TID] + 1) & 0xfff);
		hl = 26;
		q = QD;
	} else {
		n = eth_to_80211(f, eth, len, S.bss.bssid, W.mac, S.seq++, S.keys_on);
		hl = 24;
		q = QM;
	}
	if (!n)
		return -1;
	u32 fl = S.keys_on ? 0 : TXF_ENCRYPT_DIS;
	if (eapol)
		return tx_raw(q, f, n, hl, fl | TXF_CMD_RATE | TXF_HIGH_PRI, mgmt_rate());
	if (S.tlc_on)
		return tx_raw(q, f, n, hl, fl, 0); /* (débit du TLC) */
	return tx_raw(q, f, n, hl, fl | TXF_CMD_RATE, data_rate());
}

/* une trame EAPOL (contenu EAPOL) vers le point d'accès */
static int tx_eapol(const u8 *e, u32 len)
{
	static u8 eth[1600];
	if (len + 14 > sizeof eth)
		return -1;
	memcpy(eth, S.bss.bssid, 6);
	memcpy(eth + 6, W.mac, 6);
	eth[12] = 0x88;
	eth[13] = 0x8e;
	memcpy(eth + 14, e, len);
	return tx_eth(eth, len + 14, 1);
}

int sta_tx_eth(const u8 *eth, u32 len)
{
	if (S.state != ST_UP)
		return -1;
	return tx_eth(eth, len, 0);
}

/* ---- sessions block ack de réception */

/* le BAID que la puce donne à la session (RX_BAID_ALLOCATION_CONFIG_CMD si elle le connaît, sinon ADD_STA),
 * comme iwl_mvm_fw_baid_op -> BAID, < 0 refusé */
static int baid_op(int start, u8 tid, u16 ssn, u16 win)
{
	if (fw_capa(&W.fw, CAPA_BAID_ML)) {
		int v = fw_cmd_ver(&W.fw, GRP_DATA_PATH, C_DP_RX_BAID_CFG, 0);
		if (v != 2) {
			ilog("block ack : RX_BAID_ALLOCATION_CONFIG version %d inconnue\n", v);
			return -1;
		}
		u8 c[RX_BAID_CFG_LEN];
		memset(c, 0, sizeof c);
		put32(c, start ? 0 : 2); /* ADD, REMOVE */
		put32(c + 4, 1u << STA_ID);
		if (start) {
			c[8] = tid;
			put16(c + 12, ssn);
			put16(c + 14, win);
		} else
			put32(c + 8, tid);
		if (cmd_status(start ? "session block ack (BAID)" : "fin de session block ack", GRP_DATA_PATH, C_DP_RX_BAID_CFG, 0,
			       c, sizeof c))
			return -1;
		if (!start)
			return 0;
		u32 baid = W.resp_len >= 4 ? le32(W.resp) : 0xffffffffu;
		if (baid >= RX_BAID_NONE) {
			ilog("block ack : BAID %u refusé\n", baid);
			return -1;
		}
		return (int)baid;
	}
	u8 c[ADD_STA_LEN];
	memset(c, 0, sizeof c);
	c[0] = 1; /* modifier */
	put32(c + 4, MAC_ID);
	c[16] = STA_ID;
	if (start) {
		c[17] = STA_MOD_ADD_BA;
		c[28] = tid;
		put16(c + 30, ssn);
		put16(c + 44, win);
	} else {
		c[17] = STA_MOD_REMOVE_BA;
		c[29] = tid;
	}
	if (cmd_status(start ? "session block ack (station)" : "fin de session block ack", GRP_LONG, C_ADD_STA, 0, c, sizeof c))
		return -1;
	u32 st = W.resp_len >= 4 ? le32(W.resp) : 0;
	if ((st & 0xff) != 1) {
		ilog("block ack : refusé par la puce (statut %08x)\n", st);
		return -1;
	}
	if (!start)
		return 0;
	if (!(st & 0x8000)) {
		ilog("block ack : pas de BAID dans la réponse (%08x)\n", st);
		return -1;
	}
	return (int)((st >> 8) & 0x7f);
}

static void deliver_eth(const u8 *eth, u32 len)
{
	S.rx_data++;
	if (!netif_rx_put(eth, len))
		S.rx_drop++;
}

static void ba_stop(u8 tid, int tell_ap)
{
	int k = tid < 8 ? S.baid_of[tid] : -1;
	if (k < 0)
		return;
	struct ro_buf *r = &S.ro[k];
	ro_stop(r, deliver_eth);
	S.baid_of[tid] = -1;
	if (W.dead || !S.have_sta)
		return;
	baid_op(0, tid, 0, 0);
	if (tell_ap) {
		u8 f[32];
		u32 n = build_delba(f, S.bss.bssid, W.mac, S.seq++, tid, 37);
		tx_mgmt(f, n);
	}
	ilog("block ack : session du TID %u finie (%u trames retenues, %u anciennes, %u doublons)\n", tid, r->n_held, r->n_old,
	     r->n_drop);
}

static void ba_start(const struct addba *a)
{
	u16 win = a->buf_size && a->buf_size < RO_WIN ? a->buf_size : RO_WIN, status = 0;
	while (win & (win - 1)) /* (une puissance de 2 : la place d'un SN, sn % fenêtre, reste juste au passage de 4095 à 0) */
		win &= (u16)(win - 1);
	int k = -1, baid = -1;
	S.n_addba++;
	if (!cfg.ba || S.lc.mode == LM_AG || !a->policy || a->tid >= 8)
		status = BA_STATUS_DECLINED;
	else {
		ba_stop(a->tid, 0); /* (nouvelle demande pour ce TID : l'ancienne session s'arrête) */
		for (int i = 0; i < BA_MAX; i++)
			if (!S.ro[i].on) {
				k = i;
				break;
			}
		if (k < 0 || (baid = baid_op(1, a->tid, a->ssn, win)) < 0) {
			status = BA_STATUS_DECLINED;
			S.n_ba_fail++;
		}
	}
	if (!status) {
		ro_start(&S.ro[k], a->tid, (u8)baid, a->ssn, win);
		S.baid_of[a->tid] = k;
	}
	u8 f[40];
	u32 n = build_addba_resp(f, S.bss.bssid, W.mac, S.seq++, a, win, status);
	tx_mgmt(f, n);
	if (status)
		ilog("block ack : demande du point d'accès (TID %u, SSN %u, fenêtre %u) refusée\n", a->tid, a->ssn, a->buf_size);
	else
		ilog("block ack : session de réception acceptée (TID %u, SSN %u, fenêtre %u/%u, BAID %d)\n", a->tid, a->ssn, win,
		     a->buf_size, baid);
}

/* ---- réception (mvm.c : trames de gestion et de données venues du point d'accès) */
static u8 eapol_out[600];
static u32 eapol_len;
static int eapol_ev;
#define PEND_MAX 4
static struct {
	int kind; /* BA_ADDBA_REQ, BA_DELBA */
	struct addba a;
	u16 tid;
} pend[PEND_MAX];
static int npend;

/* (appelé pendant la lecture de l'anneau de réception : rien n'y est envoyé à la puce ; sta_pending le
 * fait ensuite) */
static void handle_eapol(const u8 *e, u32 len, int prot)
{
	if (!S.wpa || eapol_ev)
		return;
	u32 olen = 0;
	S.sup.rx_plain = S.keys_on && !prot; /* (E65 : en clair, les clés posées : seulement un message 3) */
	int ev = sup_rx(&S.sup, e, len, eapol_out, &olen);
	if (ev & SUP_EV_BAD)
		ilog("EAPOL : trame refusée (%s)\n", S.sup.bad_mic ? "MIC faux : clé erronée ?" : "compteur, nonce ou format");
	if (ev == SUP_EV_SEND || ev == SUP_EV_PTK || ev == SUP_EV_GTK) {
		eapol_len = olen;
		eapol_ev = ev;
	}
}

static void handle_action(const u8 *body, u32 len)
{
	struct addba a;
	u16 tid = 0;
	int k = parse_ba_action(body, len, &a, &tid);
	if ((k != BA_ADDBA_REQ && k != BA_DELBA) || npend >= PEND_MAX)
		return;
	pend[npend].kind = k;
	pend[npend].a = a;
	pend[npend].tid = tid;
	npend++;
}

static void authorized(void);

/* les réponses EAPOL, les clés et les sessions block ack, hors de la lecture de l'anneau */
static void sta_pending(void)
{
	while (npend && S.have_sta && !W.dead) {
		int k = pend[0].kind;
		struct addba a = pend[0].a;
		u16 tid = pend[0].tid;
		npend--;
		memmove(&pend[0], &pend[1], sizeof pend[0] * (u32)npend);
		if (k == BA_ADDBA_REQ)
			ba_start(&a);
		else if ((tid & 0x100) && (tid & 15) < 8) { /* DELBA de l'initiateur (le point d'accès) */
			ilog("block ack : le point d'accès arrête la session du TID %u\n", tid & 15);
			ba_stop((u8)(tid & 15), 0);
		}
	}
	int ev = eapol_ev;
	if (!ev)
		return;
	eapol_ev = 0;
	u32 olen = eapol_len;
	if (ev == SUP_EV_SEND) {
		idbg("EAPOL : réponse envoyée (message 2, ou message 4 / de groupe d'un message répété, sans reposer de clé)\n");
		tx_eapol(eapol_out, olen);
	} else if (ev == SUP_EV_PTK) {
		ilog("EAPOL : message 3 reçu (MIC juste, GTK n° %u), message 4 envoyé, clés installées\n", S.sup.gtk_idx);
		tx_eapol(eapol_out, olen);
		service(20); /* (le message 4 part en clair avant les clés) */
		if (!install_key(0) && !install_key(1)) {
			S.keys_on = 1;
			authorized();
		}
	} else if (ev == SUP_EV_GTK) {
		ilog("EAPOL : nouvelle clé de groupe (n° %u)\n", S.sup.gtk_idx);
		tx_eapol(eapol_out, olen);
		install_key(1);
	}
}

void sta_rx(const struct rx_frame *f)
{
	if (S.state == ST_IDLE || f->len < 24)
		return;
	const u8 *h = f->frame;
	if (memcmp(h + 10, S.bss.bssid, 6)) /* (émetteur : le point d'accès) */
		return;
	u16 fc = le16(h);
	u32 type = (fc >> 2) & 3, sub = (fc >> 4) & 15;
	u32 pad = f->pad ? 2 : 0; /* (E65 : le bourrage n'est pas dans la longueur du corps) */
	if (f->len < f->hdrlen + pad)
		return;
	const u8 *body = h + f->hdrlen + pad;
	u32 blen = f->len - f->hdrlen - pad;
	if (type == 0) {
		if (memcmp(h + 4, W.mac, 6))
			return;
		u16 st, aid;
		if (sub == 11 && !parse_auth_resp(h, f->len, &st)) {
			S.auth_status = st;
			S.got_auth = 1;
		} else if (sub == 1 && !parse_assoc_resp(h, f->len, &st, &aid)) {
			S.assoc_status = st;
			S.aid = aid;
			S.got_assoc = 1;
		} else if ((sub == 12 || sub == 10) && blen >= 2) {
			S.deauth_reason = le16(body);
			ilog("le point d'accès %s (raison %u)\n", sub == 12 ? "nous déconnecte" : "nous dissocie",
			     S.deauth_reason);
			S.lost = 1;
		} else if (sub == 13 && S.got_assoc && !(fc & FC_PROTECTED))
			handle_action(body, blen);
		return;
	}
	if (type != 2)
		return;
	u32 crypt = 0;
	if (fc & FC_PROTECTED) {
		if ((f->status & RX_SEC_MASK) == RX_SEC_NONE || !(f->status & RX_DECRYPTED) ||
		    ((f->status & RX_SEC_MASK) == RX_SEC_CCM && !(f->status & RX_MIC_OK))) {
			S.rx_drop++;
			return;
		}
		crypt = 8; /* en-tête CCMP (ou IV/EIV de TKIP) laissé par la puce ; MIC retiré */
	}
	static u8 eth[1600];
	u32 n = rx_to_eth(eth, f, crypt);
	if (!n)
		return;
	if (eth[12] == 0x88 && eth[13] == 0x8e) {
		handle_eapol(eth + 14, n - 14, crypt != 0);
		return;
	}
	if (S.state != ST_UP || (S.wpa && !crypt)) { /* (en WPA2, rien en clair à part l'EAPOL) */
		S.rx_drop++;
		return;
	}
	S.last_signal = f->signal;
	int qos = (sub & 8) != 0;
	u8 tid = qos ? (h[24] & 15) : 8;
	int ucast = !(h[4] & 1);
	/* doublons (répétition d'une trame déjà reçue, comme iwl_mvm_is_dup) ; les sous-trames d'un A-MSDU
	 * partagent le n° de séquence */
	if (ucast && tid <= 8 && !f->amsdu) {
		u16 sq = le16(h + 22);
		if ((fc & FC_RETRY) && S.last_seq[tid] == sq) {
			S.rx_drop++;
			return;
		}
		S.last_seq[tid] = sq;
	}
	/* session block ack : la puce donne le BAID ; on remet dans l'ordre */
	u32 baid = (f->reorder >> 24) & 0x7f;
	if (qos && ucast && tid < 8 && baid != RX_BAID_NONE && S.baid_of[tid] >= 0) {
		struct ro_buf *r = &S.ro[S.baid_of[tid]];
		if (r->on && r->baid == baid &&
		    ro_rx(r, f->reorder, (f->status & RX_DUPLICATE) != 0, f->amsdu, (f->amsdu_info & 0x80) != 0, eth, n,
			  deliver_eth))
			return;
	}
	deliver_eth(eth, n);
}

void sta_beacon(const struct bss *b, const struct rx_frame *f)
{
	if (S.state == ST_IDLE || memcmp(b->bssid, S.bss.bssid, 6) || b->probe_resp)
		return;
	S.beacon_tsf = b->tsf;
	S.beacon_gp2 = f->gp2;
	S.dtim_count = b->dtim_count;
	S.dtim_period = b->dtim_period;
	S.have_beacon = 1;
	S.last_signal = f->signal;
}

static struct ro_buf *ro_of_baid(u32 baid)
{
	for (int i = 0; i < BA_MAX; i++)
		if (S.ro[i].on && S.ro[i].baid == baid)
			return &S.ro[i];
	return 0;
}

int sta_notif(u8 grp, u8 cmd, const u8 *p, u32 len)
{
	int legacy = grp == GRP_LEGACY || grp == GRP_LONG;
	if (legacy && cmd == C_TX && len >= 44) {
		u8 n = p[0];
		u16 st = le16(p + 40);
		if ((st & 0xff) == 1)
			S.tx_ok++;
		else
			S.tx_fail++;
		int qid = le16(p + 36);
		struct txq *q = QD->on && QD->qid == qid ? QD : QM;
		if (len >= 40u + 4u * n + 4u)
			q->r = le32(p + 40 + 4 * n) & (TFD_QUEUE_MAX - 1); /* SSN : les places rendues */
		return 1;
	}
	if (legacy && cmd == C_FRAME_RELEASE && len >= 4) { /* la puce libère jusqu'au NSSN (trou trop vieux) */
		struct ro_buf *r = ro_of_baid(p[0]);
		if (r)
			ro_release(r, le16(p + 2) & 0xfff, deliver_eth);
		return 1;
	}
	if (legacy && cmd == C_BAR_FRAME_RELEASE && len >= 8) { /* BAR du point d'accès */
		u32 info = le32(p + 4);
		struct ro_buf *r = ro_of_baid((info >> 24) & 0x3f);
		if (r)
			ro_release(r, info & 0xfff, deliver_eth);
		return 1;
	}
	if (grp == GRP_DATA_PATH && cmd == C_DP_TLC_NOTIF && len >= 12) {
		if ((le32(p + 4) & 1) && le32(p + 8) != S.tlc_rate) {
			S.tlc_rate = le32(p + 8);
			char d[80];
			rate_name(S.tlc_rate, d, sizeof d);
			if (S.n_tlc++)
				idbg("débit d'émission choisi par la puce : %s\n", d);
			else
				ilog("débit d'émission choisi par la puce : %s (puis il suit le signal)\n", d);
		}
		return 1;
	}
	if (grp == GRP_MAC_CONF && cmd == C_MAC_SESSION_PROT_NOTIF) {
		S.prot_on = len >= 12 && le32(p + 4) && le32(p + 8);
		idbg("protection de session : %s\n", S.prot_on ? "commencée" : "finie");
		return 1;
	}
	if (legacy && cmd == C_MISSED_BEACONS && len >= 12) {
		S.missed = le32(p + 8);
		if (S.state == ST_UP && S.missed >= 24 && !S.lost) {
			ilog("%u balises manquées de suite : lien perdu\n", S.missed);
			S.lost = 1;
		}
		return 1;
	}
	return 0;
}

/* ---- attentes */
static int wait_until(int *flag, long ms)
{
	long end = now_ms() + ms;
	while (!*flag) {
		if (W.dead || S.lost)
			return -1;
		long left = end - now_ms();
		if (left <= 0)
			return -1;
		if (service(left < 10 ? left : 10) == E_FWERR)
			return -1;
		sta_pending();
	}
	return 0;
}

static void choose_rate(void)
{
	int s = S.bss.signal;
	S.data_rate = s >= -60 ? 7 : s >= -67 ? 5 : s >= -74 ? 4 : 2; /* 54, 36, 24, 12 Mb/s */
}

/* les largeurs que la régulation permet sur le canal primaire (drapeaux 40/80 MHz du NVM ou de MCC) */
static u32 allowed_widths(u8 ch)
{
	u32 a = 1u << LW_20;
	for (int i = 0; i < NVM_CHANNELS; i++) {
		if (nvm_channel_list[i] != ch)
			continue;
		u32 fl = W.have_mcc ? W.mcc_flags[i] : W.have_nvm ? W.nvm.ch_flags[i] : 0;
		if (fl & (1u << 9))
			a |= 1u << LW_40;
		if (fl & (1u << 10))
			a |= 1u << LW_80;
	}
	return a;
}

/* station autorisée (clés installées, ou réseau ouvert) : le TLC passe à la largeur du lien */
static void authorized(void)
{
	if (S.tlc_on && S.lc.width > LW_20)
		tlc_config(S.lc.width);
}

void sta_leave(int deauth)
{
	if (S.state == ST_IDLE && !S.have_mac)
		return;
	int was_up = S.state == ST_UP;
	if (was_up)
		sta_print();
	/* les sessions block ack d'abord : trames retenues remises, BAID rendu à la puce (comme mac80211 avant de
	 * retirer la station ; une station retirée avec un BAID encore pris fait tomber le micrologiciel :
	 * assertion 0x2010303a sur REMOVE_STA au premier essai du 16/11) */
	for (int t = 0; t < 8; t++)
		ba_stop((u8)t, 0);
	npend = 0;
	if (deauth && S.have_sta && !W.dead) {
		u8 f[26];
		build_auth(f, S.bss.bssid, W.mac, S.seq++);
		put16(f, FC_DEAUTH);
		put16(f + 24, 3); /* la station s'en va */
		tx_mgmt(f, 26);
		service(30);
	}
	if (!W.dead) {
		if (S.have_sta) {
			if (QD->on)
				queue_cfg(QD, 0, DATA_TID);
			queue_cfg(QM, 0, MGMT_TID);
			u8 r[4] = {STA_ID, 0, 0, 0};
			cmd_status("station retirée", GRP_LONG, C_REMOVE_STA, 0, r, 4);
		}
		if (S.prot_on)
			session_prot(ACT_REMOVE, 0);
		if (S.have_binding)
			binding(ACT_REMOVE);
		if (S.have_phy)
			phy_ctx(ACT_REMOVE);
		if (S.have_mac)
			mac_ctx(ACT_REMOVE, 0);
	}
	QM->on = QD->on = 0;
	S.have_sta = S.have_binding = S.have_phy = S.have_mac = S.have_dq = 0;
	S.keys_on = S.tlc_on = 0;
	S.state = ST_IDLE;
	if (was_up)
		netif_link(0, 0, 0);
}

static int join_once(const struct bss *b, const char *pass, int mode, int *assoc_failed);

/* -> 0 associé (et clés installées), < 0 : E_TIMEDOUT, E_ACCES, E_IO_ ; si l'association échoue en HT/VHT
 * (pas de réponse, ou refusée), un second essai en 802.11a/g */
int sta_join(const struct bss *b, const char *pass)
{
	read_cfg();
	int af = 0;
	int r = join_once(b, pass, cfg.mode, &af);
	if (r && af && cfg.mode > LM_AG && S.lc.mode > LM_AG && !W.dead) {
		ilog("association en %s pas aboutie : nouvel essai en 802.11a/g\n", link_name(&S.lc));
		r = join_once(b, pass, LM_AG, &af);
	}
	return r;
}

static int join_once(const struct bss *b, const char *pass, int mode, int *assoc_failed)
{
	sta_leave(1);
	memset(&S, 0, sizeof S);
	for (int t = 0; t < 8; t++)
		S.baid_of[t] = -1;
	for (int t = 0; t < 9; t++)
		S.last_seq[t] = 0xffff;
	npend = 0;
	eapol_ev = 0;
	ro_init(); /* (la réserve des trames retenues : toutes libres) */
	*assoc_failed = 0;
	S.bss = *b;
	S.state = ST_JOINING;
	S.wpa = pass && *pass;
	choose_rate();
	link_choose(b, mode, cfg.width, allowed_widths(b->channel), &S.lc);
	if (alloc_queue_mem(QM) || alloc_queue_mem(QD))
		return E_IO_;
	u8 rsn[64];
	u32 rsn_len = 0;
	if (S.wpa) {
		rsn_len = build_rsn_ie(rsn, b);
		if (!rsn_len) {
			ilog("ÉCHEC : « %.*s » : sécurité non prise en charge (%s ; seul WPA2-PSK avec CCMP l'est)\n",
			     b->ssid_len, b->ssid, sec_name(b));
			S.state = ST_IDLE;
			return E_IO_;
		}
		u8 pmk[32], sn[32];
		wpa_pmk(pass, b->ssid, b->ssid_len, pmk);
		rand_bytes(sn, 32);
		sup_init(&S.sup, pmk, b->bssid, W.mac, rsn, rsn_len, b->group, sn);
		memset(pmk, 0, sizeof pmk);
	}
	ilog("connexion à « %.*s » (%02x:%02x:%02x:%02x:%02x:%02x, canal %u, %d dBm, %s, %s%s%s%s)\n", b->ssid_len, b->ssid,
	     b->bssid[0], b->bssid[1], b->bssid[2], b->bssid[3], b->bssid[4], b->bssid[5], b->channel, b->signal,
	     S.wpa ? "WPA2" : "ouvert", link_name(&S.lc), S.lc.mode ? (S.lc.nss >= 2 ? ", 2 flux" : ", 1 flux") : "",
	     S.lc.wmm ? ", WMM" : "", b->he ? " ; le point d'accès offre aussi HE (ax), pas encore pris" : "");
	if (S.lc.width > LW_20)
		idbg("canal primaire %u, centre %u, position %x\n", S.lc.primary, S.lc.center, S.lc.ctrl_pos);
	int r = E_IO_;
	if (mac_ctx(ACT_ADD, 0))
		goto fail;
	S.have_mac = 1;
	if (phy_ctx(ACT_ADD))
		goto fail;
	S.have_phy = 1;
	if (binding(ACT_ADD))
		goto fail;
	S.have_binding = 1;
	if (add_sta(0))
		goto fail;
	S.have_sta = 1;
	if (queue_cfg(QM, 1, MGMT_TID))
		goto fail;
	session_prot(ACT_ADD, 1200);
	wait_until(&S.prot_on, 300);
	/* authentification */
	r = E_TIMEDOUT;
	for (int i = 0; i < 3 && !S.got_auth; i++) {
		u8 f[30];
		u32 n = build_auth(f, b->bssid, W.mac, S.seq++);
		tx_mgmt(f, n);
		wait_until(&S.got_auth, 300);
	}
	if (!S.got_auth) {
		ilog("ÉCHEC : pas de réponse à l'authentification\n");
		goto fail;
	}
	if (S.auth_status) {
		ilog("ÉCHEC : authentification refusée (statut %u)\n", S.auth_status);
		r = E_IO_;
		goto fail;
	}
	/* association */
	u8 ies[64];
	u32 ies_len = ht_assoc_ies(ies, b, &S.lc);
	for (int i = 0; i < 3 && !S.got_assoc; i++) {
		static u8 f[512];
		u32 n = build_assoc_req_ex(f, b, W.mac, S.seq++, rsn, rsn_len, ies, ies_len);
		tx_mgmt(f, n);
		wait_until(&S.got_assoc, 500);
	}
	if (!S.got_assoc) {
		ilog("ÉCHEC : pas de réponse à l'association\n");
		*assoc_failed = 1;
		goto fail;
	}
	if (S.assoc_status) {
		ilog("ÉCHEC : association refusée (statut %u%s)\n", S.assoc_status,
		     S.assoc_status == 17 ? " : point d'accès plein" : S.assoc_status == 18 ? " : débits refusés" : "");
		*assoc_failed = S.assoc_status != 17;
		r = E_IO_;
		goto fail;
	}
	ilog("associé (AID %u, %s)\n", S.aid, link_name(&S.lc));
	if (!S.have_beacon) {
		long end = now_ms() + 400;
		while (!S.have_beacon && now_ms() < end && service(10) != E_FWERR)
			;
	}
	if (mac_ctx(ACT_MODIFY, 1) || add_sta(1)) {
		r = E_IO_;
		goto fail;
	}
	if (S.lc.wmm) { /* les données en QoS sur leur file (TID 0) */
		if (!queue_cfg(QD, 1, DATA_TID))
			S.have_dq = 1;
		else
			ilog("file des données QoS refusée : données sur la file de gestion\n");
	}
	tlc_config(S.wpa ? LW_20 : S.lc.width);
	if (S.wpa) {
		long end = now_ms() + 6000;
		while (!S.keys_on && now_ms() < end && !S.lost && !W.dead) {
			if (service(10) == E_FWERR)
				break;
			sta_pending();
		}
		if (!S.keys_on) {
			ilog("ÉCHEC : échange EAPOL à 4 voies pas fini (%s)\n",
			     S.sup.bad_mic ? "MIC faux" : S.sup.state == SUP_WAIT_M1 ? "pas de message 1" : "pas de message 3 : clé refusée ?");
			r = E_ACCES;
			goto fail;
		}
	}
	session_prot(ACT_REMOVE, 0);
	S.state = ST_UP;
	S.lost = 0;
	static const u8 mbps[8] = {6, 9, 12, 18, 24, 36, 48, 54};
	char ssid[33];
	memcpy(ssid, b->ssid, b->ssid_len);
	ssid[b->ssid_len] = 0;
	u32 speed = S.lc.mode > LM_AG ? link_max_mbps(&S.lc) : mbps[S.data_rate & 7];
	netif_link(1, speed, ssid);
	if (S.tlc_on)
		ilog("CONNECTÉ à « %s » (%s, %s, débit choisi par la puce, jusqu'à %u Mb/s)\n", ssid,
		     S.wpa ? "WPA2, CCMP en matériel" : "réseau ouvert", link_name(&S.lc), speed);
	else
		ilog("CONNECTÉ à « %s » (%s, débit fixe %u Mb/s)\n", ssid, S.wpa ? "WPA2, CCMP en matériel" : "réseau ouvert",
		     mbps[S.data_rate & 7]);
	return 0;
fail:
	if (S.lost)
		r = S.wpa && S.got_assoc ? E_ACCES : r;
	sta_leave(S.got_auth);
	return W.dead ? E_IO_ : r;
}

void sta_print(void)
{
	char d[80];
	rate_name(S.tlc_rate, d, sizeof d);
	ilog("lien : %s, émission %s, reçues %lu (jetées %lu), émises %lu (échecs %lu, perdues %lu), ADDBA %u (refus %u)\n",
	     link_name(&S.lc), S.tlc_rate ? d : "?", (unsigned long)S.rx_data, (unsigned long)S.rx_drop,
	     (unsigned long)S.tx_ok, (unsigned long)S.tx_fail, (unsigned long)S.tx_drop, S.n_addba, S.n_ba_fail);
}

void sta_tick(void)
{
	sta_pending();
	if (S.state == ST_UP && S.lost) {
		ilog("lien perdu avec « %.*s »\n", S.bss.ssid_len, S.bss.ssid);
		sta_leave(0);
	}
	static long next;
	long now = now_ms();
	if (S.state == ST_UP && W.verbose && now >= next) {
		next = now + 30000;
		sta_print();
	}
}
