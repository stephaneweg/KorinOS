/* assoc.c : la station 802.11 sur le micrologiciel MVM (docs/etude/WIFI-PC.md 6 et 7, étapes 2 et 3).
 *
 * Joindre un point d'accès, comme le fait iwlwifi avec mac80211 (séquences décrites et réécrites) :
 *   contexte MAC (station, BSSID, balises reçues tant qu'on n'est pas associé), contexte PHY (le canal,
 *   20 MHz), RLC (chaînes de réception), liaison MAC↔PHY, station du point d'accès (ADD_STA, n° 0), sa file
 *   d'émission (SCD_QUEUE_CONFIG, TID de gestion 15), protection de session (la puce reste sur le canal),
 *   authentification « système ouvert », demande d'association (station 802.11a/g, sans HT ni WMM : débits
 *   fixés par l'hôte), contexte MAC « associé » (AID, DTIM d'après la dernière balise), station mise à jour ;
 *   en WPA2 : EAPOL à 4 voies (mlme.c), puis la PTK et la GTK installées dans la puce (ADD_STA_KEY) :
 *   CCMP en matériel (la puce ajoute l'en-tête CCMP et le MIC, les retire à la réception).
 * Les trames de données : Ethernet ↔ 802.11 (mlme.c), une file de 64 TFD (commande TX de 20 octets, en-tête,
 * données), table des longueurs, pointeur d'écriture ; les réponses TX rendent les places. */
#include "iwl.h"

#define STA_ID 0
#define MAC_ID 0
#define PHY_ID 0
#define MGMT_TID 15
#define C_PHY_CONTEXT 0x08
#define C_ADD_STA_KEY 0x17
#define C_ADD_STA 0x18
#define C_REMOVE_STA 0x19
#define C_TX 0x1c
#define C_MAC_CONTEXT 0x28
#define C_BINDING 0x2b
#define C_MISSED_BEACONS 0xa2
#define C_DP_RLC_CONFIG 0x08
#define C_DP_SCD_QUEUE_CONFIG 0x17
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

#define S (W.sta)


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
		u8 b[64];
		memcpy(b, pool, 20);
		put64(b + 20, tsc());
		put64(b + 28, (u64)now_ms());
		put64(b + 36, ++ctr);
		memcpy(b + 44, W.mac, 6);
		put32(b + 50, rd32(CSR_INT));
		put64(b + 54, tsc());
		sha1(b, 62, pool);
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

/* ---- contextes */
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
	put32(c + 40, 0);                                    /* protection */
	put32(c + 44, (S.bss.capab & 0x0020) ? 1u << 5 : 0); /* préambule court */
	put32(c + 48, (S.bss.capab & 0x0400) || S.bss.channel > 14 ? 1u << 4 : 0); /* intervalle court */
	put32(c + 52, (1u << 2) | (assoc ? 0 : 1u << 6));    /* groupe ; balises tant que non associé */
	put32(c + 56, 0);                                    /* QoS : non (station a/g) */
	for (int ac = 0; ac < 4; ac++) {                     /* EDCA sans QoS : AIFS 2, CW 15-1023 */
		u8 *a = c + 60 + 8 * ac;
		put16(a, 15);
		put16(a + 2, 1023);
		a[4] = 2;
		a[5] = (u8)(1u << (ac + 1)); /* BK 1, BE 2, VI 3, VO 4 (FIFO EDCA gen2) */
		put16(a + 6, 0);
	}
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
	put32(c + 8, S.bss.channel);
	c[12] = S.bss.channel <= 14 ? 1 : 0; /* PHY_BAND_24 / PHY_BAND_5 */
	c[13] = 0;                            /* 20 MHz */
	c[14] = 0;
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

static int add_sta(int modify)
{
	u8 c[ADD_STA_LEN];
	memset(c, 0, sizeof c);
	c[0] = (u8)modify;
	put16(c + 2, 0xffff); /* pas d'agrégation */
	put32(c + 4, MAC_ID);
	memcpy(c + 8, S.bss.bssid, 6);
	c[16] = STA_ID;
	put32(c + 24, (3u << 26) | (3u << 28) | (1u << 17)); /* masque : largeur, MIMO, protection MIMO */
	c[35] = 0;                                           /* IWL_STA_LINK */
	put16(c + 36, S.aid);
	if (cmd_status(modify ? "station (associée)" : "station du point d'accès", GRP_LONG, C_ADD_STA, 0, c, sizeof c))
		return -1;
	if (W.resp_len >= 4 && (le32(W.resp) & 0xff) != 1) {
		ilog("ÉCHEC : station refusée (statut %u)\n", le32(W.resp));
		return -1;
	}
	return 0;
}

static int queue_cfg(int add)
{
	u8 c[SCD_QUEUE_LEN];
	memset(c, 0, sizeof c);
	put32(c + 0, add ? 0 : 1);
	put32(c + 4, 1u << STA_ID);
	if (add) {
		c[8] = MGMT_TID;
		put32(c + 12, 0);
		put32(c + 16, 3); /* log2(64) - 3 */
		put64(c + 20, S.qbc.pa);
		put64(c + 28, S.qtfd.pa);
	} else
		put32(c + 8, MGMT_TID);
	if (cmd_status(add ? "file d'émission" : "file d'émission retirée", GRP_DATA_PATH, C_DP_SCD_QUEUE_CONFIG, 0, c,
		       sizeof c))
		return -1;
	if (!add)
		return 0;
	if (W.resp_len != 8) {
		ilog("ÉCHEC : réponse de la file de %u octets\n", W.resp_len);
		return -1;
	}
	S.qid = le16(W.resp);
	S.qw = S.qr = le16(W.resp + 4) & (TFD_QUEUE_MAX - 1);
	idbg("file %d, pointeur %u\n", S.qid, S.qw);
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

/* ---- émission : commande TX + en-tête + données dans le tampon de la place, 3 TB */
static int tx_raw(const u8 *frame, u32 len, u32 hdrlen, u32 flags, u32 rate)
{
	if (!S.have_sta || len + 4 + TX_CMD_LEN > DQ_BUF || hdrlen != 24)
		return -1;
	long end = now_ms() + 200;
	while (((S.qw - S.qr) & (TFD_QUEUE_MAX - 1)) >= DQ_SLOTS - 2) { /* file pleine : on attend les réponses */
		if (now_ms() > end || service(5) == E_FWERR) {
			S.tx_drop++;
			return -1;
		}
	}
	u32 slot = S.qw & (DQ_SLOTS - 1);
	u8 *b = S.qbuf.va + slot * DQ_BUF;
	u64 pa = S.qbuf.pa + (u64)slot * DQ_BUF;
	b[0] = C_TX;
	b[1] = 0;
	put16(b + 2, (u16)((S.qid & 0x1f) << 8 | slot));
	u8 *tc = b + 4;
	memset(tc, 0, TX_CMD_LEN);
	put16(tc + 0, len);
	put16(tc + 2, (u16)((hdrlen / 2) << 8)); /* taille de l'en-tête MAC (mots) */
	put32(tc + 4, flags);
	put32(tc + 16, rate);
	memcpy(b + 4 + TX_CMD_LEN, frame, len);
	u8 *t = S.qtfd.va + slot * TFD_SIZE;
	memset(t, 0, TFD_SIZE);
	u32 head = 4 + TX_CMD_LEN + hdrlen; /* 48 */
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
	put16(S.qbc.va + 2 * slot, ((len + 3) / 4) | (chunks << 12));
	S.qw = (S.qw + 1) & (TFD_QUEUE_MAX - 1);
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	wr32(HBUS_TARG_WRPTR, S.qw | ((u32)S.qid << 16));
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
	return tx_raw(f, len, 24, TXF_CMD_RATE | TXF_ENCRYPT_DIS | TXF_HIGH_PRI, mgmt_rate());
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
	static u8 f[1700];
	u32 n = eth_to_80211(f, eth, len + 14, S.bss.bssid, W.mac, S.seq++, S.keys_on);
	return tx_raw(f, n, 24, TXF_CMD_RATE | TXF_HIGH_PRI | (S.keys_on ? 0 : TXF_ENCRYPT_DIS), mgmt_rate());
}

int sta_tx_eth(const u8 *eth, u32 len)
{
	if (S.state != ST_UP)
		return -1;
	static u8 f[1700];
	u32 n = eth_to_80211(f, eth, len, S.bss.bssid, W.mac, S.seq++, S.keys_on);
	if (!n)
		return -1;
	return tx_raw(f, n, 24, TXF_CMD_RATE | (S.keys_on ? 0 : TXF_ENCRYPT_DIS), data_rate());
}

/* ---- réception (mvm.c : trames de gestion et de données venues du point d'accès) */
static u8 eapol_out[600];
static u32 eapol_len;
static int eapol_ev;

/* (appelé pendant la lecture de l'anneau de réception : rien n'y est envoyé à la puce ; sta_pending le
 * fait ensuite) */
static void handle_eapol(const u8 *e, u32 len)
{
	if (!S.wpa || eapol_ev)
		return;
	u32 olen = 0;
	int ev = sup_rx(&S.sup, e, len, eapol_out, &olen);
	if (ev & SUP_EV_BAD)
		ilog("EAPOL : trame refusée (%s)\n", S.sup.bad_mic ? "MIC faux : clé erronée ?" : "compteur, nonce ou format");
	if (ev == SUP_EV_SEND || ev == SUP_EV_PTK || ev == SUP_EV_GTK) {
		eapol_len = olen;
		eapol_ev = ev;
	}
}

/* les réponses EAPOL et les clés, hors de la lecture de l'anneau */
static void sta_pending(void)
{
	int ev = eapol_ev;
	if (!ev)
		return;
	eapol_ev = 0;
	u32 olen = eapol_len;
	if (ev == SUP_EV_SEND) {
		idbg("EAPOL : message 1 reçu, message 2 envoyé\n");
		tx_eapol(eapol_out, olen);
	} else if (ev == SUP_EV_PTK) {
		ilog("EAPOL : message 3 reçu (MIC juste, GTK n° %u), message 4 envoyé, clés installées\n", S.sup.gtk_idx);
		tx_eapol(eapol_out, olen);
		service(20); /* (le message 4 part en clair avant les clés) */
		if (!install_key(0) && !install_key(1))
			S.keys_on = 1;
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
	const u8 *body = h + f->hdrlen + (f->pad ? 2 : 0);
	u32 blen = f->len - f->hdrlen;
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
		}
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
	u32 n = wlan_to_eth(eth, h, f->len, f->hdrlen + (f->pad ? 2 : 0), crypt);
	if (!n)
		return;
	if (eth[12] == 0x88 && eth[13] == 0x8e) {
		handle_eapol(eth + 14, n - 14);
		return;
	}
	if (S.state != ST_UP || (S.wpa && !crypt)) { /* (en WPA2, rien en clair à part l'EAPOL) */
		S.rx_drop++;
		return;
	}
	S.rx_data++;
	S.last_signal = f->signal;
	if (!netif_rx_put(eth, n))
		S.rx_drop++;
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
		if (len >= 40u + 4u * n + 4u)
			S.qr = le32(p + 40 + 4 * n) & (TFD_QUEUE_MAX - 1); /* SSN : les places rendues */
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

static int alloc_queue_mem(void)
{
	if (S.qtfd.va)
		return 0;
	if (dma_alloc(DQ_SLOTS * TFD_SIZE, &S.qtfd) || dma_alloc(4096, &S.qbc) || dma_alloc(DQ_SLOTS * DQ_BUF, &S.qbuf))
		return -1;
	return 0;
}

static void choose_rate(void)
{
	int s = S.bss.signal;
	S.data_rate = s >= -60 ? 7 : s >= -67 ? 5 : s >= -74 ? 4 : 2; /* 54, 36, 24, 12 Mb/s */
}

void sta_leave(int deauth)
{
	if (S.state == ST_IDLE && !S.have_mac)
		return;
	int was_up = S.state == ST_UP;
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
			queue_cfg(0);
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
	S.have_sta = S.have_binding = S.have_phy = S.have_mac = 0;
	S.keys_on = 0;
	S.state = ST_IDLE;
	if (was_up)
		netif_link(0, 0, 0);
}

/* -> 0 associé (et clés installées), < 0 : E_TIMEDOUT, E_ACCES, E_IO_ */
int sta_join(const struct bss *b, const char *pass)
{
	sta_leave(1);
	struct dma_blk t = S.qtfd, bc = S.qbc, qb = S.qbuf;
	memset(&S, 0, sizeof S);
	S.qtfd = t, S.qbc = bc, S.qbuf = qb;
	S.bss = *b;
	S.state = ST_JOINING;
	S.wpa = pass && *pass;
	choose_rate();
	if (alloc_queue_mem())
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
	ilog("connexion à « %.*s » (%02x:%02x:%02x:%02x:%02x:%02x, canal %u, %d dBm, %s)\n", b->ssid_len, b->ssid,
	     b->bssid[0], b->bssid[1], b->bssid[2], b->bssid[3], b->bssid[4], b->bssid[5], b->channel, b->signal,
	     S.wpa ? "WPA2" : "ouvert");
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
	if (queue_cfg(1))
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
	for (int i = 0; i < 3 && !S.got_assoc; i++) {
		static u8 f[512];
		u32 n = build_assoc_req(f, b, W.mac, S.seq++, rsn, rsn_len);
		tx_mgmt(f, n);
		wait_until(&S.got_assoc, 500);
	}
	if (!S.got_assoc) {
		ilog("ÉCHEC : pas de réponse à l'association\n");
		goto fail;
	}
	if (S.assoc_status) {
		ilog("ÉCHEC : association refusée (statut %u%s)\n", S.assoc_status,
		     S.assoc_status == 17 ? " : point d'accès plein" : S.assoc_status == 18 ? " : débits refusés" : "");
		r = E_IO_;
		goto fail;
	}
	ilog("associé (AID %u)\n", S.aid);
	if (!S.have_beacon) {
		long end = now_ms() + 400;
		while (!S.have_beacon && now_ms() < end && service(10) != E_FWERR)
			;
	}
	if (mac_ctx(ACT_MODIFY, 1) || add_sta(1)) {
		r = E_IO_;
		goto fail;
	}
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
	netif_link(1, mbps[S.data_rate & 7], ssid);
	ilog("CONNECTÉ à « %s » (%s, débit fixe %u Mb/s)\n", ssid, S.wpa ? "WPA2, CCMP en matériel" : "réseau ouvert",
	     mbps[S.data_rate & 7]);
	return 0;
fail:
	if (S.lost)
		r = S.wpa && S.got_assoc ? E_ACCES : r;
	sta_leave(S.got_auth);
	return W.dead ? E_IO_ : r;
}

void sta_tick(void)
{
	sta_pending();
	if (S.state == ST_UP && S.lost) {
		ilog("lien perdu avec « %.*s »\n", S.bss.ssid_len, S.bss.ssid);
		sta_leave(0);
	}
}
