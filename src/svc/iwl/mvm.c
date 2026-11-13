/* mvm.c : le micrologiciel en mode MVM (docs/etude/WIFI-PC.md 5) : vivant, initialisation « unifiée »
 * (INIT_EXTENDED_CFG, NVM_ACCESS_COMPLETE, INIT_COMPLETE), lecture du NVM, antennes, coexistence
 * Bluetooth, latence du SoC, régulation (LAR : MCC_UPDATE « ZZ »), configuration du balayage, puis un
 * balayage UMAC et la liste des réseaux vus. Les versions des commandes sont vérifiées contre le TLV 48 du
 * micrologiciel avant tout envoi : une version inconnue arrête proprement plutôt que d'envoyer une
 * structure fausse. */
#include "iwl.h"

static u8 scan_buf[2048];

static void bss_add(const struct bss *b)
{
	for (int i = 0; i < W.nbss; i++)
		if (!memcmp(W.bss[i].bssid, b->bssid, 6)) {
			struct bss *o = &W.bss[i];
			o->seen++;
			if (b->signal > o->signal)
				o->signal = b->signal;
			if (!o->ssid_len && b->ssid_len) { /* (réseau caché : la réponse de sonde le nomme) */
				memcpy(o->ssid, b->ssid, sizeof o->ssid);
				o->ssid_len = b->ssid_len;
			}
			return;
		}
	if (W.nbss < MAX_BSS) {
		W.bss[W.nbss] = *b;
		W.bss[W.nbss].seen = 1;
		W.nbss++;
	}
}

static void rx_mpdu(const u8 *p, u32 len)
{
	struct rx_frame f;
	if (parse_rx_mpdu(p, len, &f)) {
		if (!W.rx_desc_warned++)
			ilog("ATTENTION : trame reçue illisible (%u octets, longueur MPDU %u)\n", len, len >= 2 ? le16(p) : 0);
		return;
	}
	if ((f.status & (RX_CRC_OK | RX_OVERRUN_OK)) != (RX_CRC_OK | RX_OVERRUN_OK)) {
		W.n_mpdu_bad++;
		return;
	}
	if (f.len < 24 || f.hdrlen > f.len)
		return;
	u16 fc = le16(f.frame);
	if (((fc >> 2) & 3) != 0) /* seulement la gestion */
		return;
	struct bss b;
	if (parse_beacon(f.frame, f.frame + f.hdrlen + (f.pad ? 2 : 0), f.len - f.hdrlen, f.channel, f.signal, &b)) {
		W.n_beacons++;
		bss_add(&b);
		if (W.verbose >= 2)
			idbg("%s %02x:%02x:%02x:%02x:%02x:%02x canal %u %d dBm « %.*s »\n", b.probe_resp ? "sonde" : "balise",
			     b.bssid[0], b.bssid[1], b.bssid[2], b.bssid[3], b.bssid[4], b.bssid[5], b.channel, b.signal,
			     b.ssid_len, b.ssid);
	}
}

void mvm_rx(u8 grp, u8 cmd, u16 seq, const u8 *p, u32 len)
{
	int legacy = grp == GRP_LEGACY || grp == GRP_LONG;
	if (legacy && cmd == C_ALIVE) {
		if (parse_alive(p, len, &W.ai)) {
			ilog("ANOMALIE : notification « vivant » de %u octets (inconnue)\n", len);
			return;
		}
		W.got_alive = 1;
	} else if (legacy && cmd == C_INIT_COMPLETE) {
		W.init_complete = 1;
	} else if (grp == GRP_REGULATORY && cmd == C_REG_PNVM_INIT_COMPLETE) {
		W.pnvm_complete = 1;
		idbg("PNVM : fin de l'initialisation (statut %08x)\n", len >= 4 ? le32(p) : 0);
	} else if (legacy && cmd == C_RX_MPDU) {
		rx_mpdu(p, len);
	} else if (legacy && cmd == C_SCAN_COMPLETE_UMAC) {
		parse_scan_complete(p, len, &W.sd);
		W.scan_complete = 1;
	} else if (legacy && cmd == C_SCAN_ITER_COMPLETE_UMAC) {
		idbg("balayage : fin d'un tour (%u canaux)\n", len >= 5 ? p[4] : 0);
	} else if (legacy && cmd == C_REPLY_ERROR) {
		ilog("ERREUR rendue par le micrologiciel : type %08x, commande %02x, n° %04x, service %08x\n",
		     len >= 4 ? le32(p) : 0, len >= 5 ? p[4] : 0, len >= 8 ? le16(p + 6) : 0, len >= 12 ? le32(p + 8) : 0);
	} else if (legacy && (cmd == C_MCC_CHUB_UPDATE || cmd == C_MCC_UPDATE)) {
		ilog("régulation : le micrologiciel annonce un changement de pays (%c%c)\n", len >= 2 ? p[1] : '?',
		     len >= 1 ? p[0] : '?');
	} else if (grp == GRP_LEGACY && cmd == C_DEBUG_LOG_MSG) {
		if (W.verbose && len > 4)
			idbg("micrologiciel : %.*s\n", (int)(len - 4 > 120 ? 120 : len - 4), (const char *)p + 4);
	} else
		idbg("notification %x.%02x (%u octets, n° %04x)\n", grp, cmd, len, seq);
}

/* attend un drapeau posé par mvm_rx -> 0, E_TIMEDOUT, E_FWERR */
static int wait_flag(int *flag, long ms)
{
	long end = now_ms() + ms;
	while (!*flag) {
		long left = end - now_ms();
		if (left <= 0)
			return E_TIMEDOUT;
		if (service(left < 20 ? left : 20) == E_FWERR)
			return E_FWERR;
	}
	return 0;
}

static u32 resp_status(void)
{
	return W.resp_len >= 4 ? le32(W.resp) : 0;
}

/* une commande attendue sans réponse particulière : journal de l'échec */
static int cmd(const char *what, u8 grp, u8 c, const void *data, u32 len)
{
	long r = send_cmd(grp, c, data, len, 2000);
	if (r) {
		ilog("ÉCHEC : %s (commande %x.%02x) : %s\n", what, grp, c, r == E_FWERR ? "erreur du micrologiciel" : "délai");
		return (int)r;
	}
	idbg("%s : réponse de %u octets, statut %08x\n", what, W.resp_len, resp_status());
	return 0;
}

static int check_versions(void)
{
	const struct fw_image *fw = &W.fw;
	int sv = fw_cmd_ver(fw, GRP_LONG, C_SCAN_REQ_UMAC, 0);
	int cv = fw_cmd_ver(fw, GRP_LONG, C_SCAN_CFG, 0);
	int nv = fw_notif_ver(fw, GRP_REGULATORY, C_REG_NVM_GET_INFO, 0);
	int av = fw_notif_ver(fw, GRP_LEGACY, C_ALIVE, 0);
	ilog("versions des commandes : balayage %d, config. du balayage %d, NVM %d, vivant %d, MCC %d/%d, SoC %d, BT %d\n",
	     sv, cv, nv, av, fw_cmd_ver(fw, GRP_LONG, C_MCC_UPDATE, 0), fw_notif_ver(fw, GRP_LONG, C_MCC_UPDATE, 0),
	     fw_cmd_ver(fw, GRP_SYSTEM, C_SYS_SOC_CONFIG, 0), fw_cmd_ver(fw, GRP_LONG, C_BT_CONFIG, 0));
	if (sv < 14 || sv > 17 || cv != 5 || (nv != 4 && !fw_api(fw, API_REGULATORY_NVM_INFO)) ||
	    !fw_capa(fw, CAPA_UMAC_SCAN)) {
		ilog("ANOMALIE : versions non prises en charge par ce pilote (écrit pour le micrologiciel 77 : balayage "
		     "14 à 17, config. 5, NVM 4)\n");
		return -1;
	}
	return 0;
}

int mvm_up(void)
{
	if (check_versions())
		return -1;
	if (trans_start_fw())
		return -1;
	int r = wait_flag(&W.got_alive, 3000);
	if (r) {
		ilog("ÉCHEC : pas de « vivant » en 3 s (%s)\n", r == E_FWERR ? "erreur du micrologiciel" : "délai");
		dump_regs("pas de vivant");
		if (W.got_alive || W.dead)
			dump_fw_error();
		return -1;
	}
	struct alive_info *a = &W.ai;
	ilog("VIVANT : statut %04x, LMAC %u.%u, UMAC %u.%x, notification v%d, journaux d'erreur LMAC %08x UMAC %08x, "
	     "SKU %08x %08x %08x\n",
	     a->status, a->lmac_major, a->lmac_minor, a->umac_major, a->umac_minor, a->version_by_len, a->lmac_err,
	     a->umac_err, a->sku_id[0], a->sku_id[1], a->sku_id[2]);
	if (a->status != ALIVE_OK) {
		ilog("ÉCHEC : le micrologiciel se dit invalide (statut %04x, 0xCAFE attendu)\n", a->status);
		dump_regs("vivant invalide");
		return -1;
	}
	trans_alive();
	/* PNVM : rien à charger pour cette famille, mais le micrologiciel l'attend quand le SKU est connu */
	if (a->sku_id[0] | a->sku_id[1] | a->sku_id[2]) {
		prph_write(UREG_DOORBELL_TO_ISR6, UREG_DOORBELL_PNVM);
		r = wait_flag(&W.pnvm_complete, 1000);
		ilog("PNVM (aucun fichier pour les 22000) : %s\n", r ? "pas de réponse en 1 s (on continue)" : "accusé");
		if (r == E_FWERR)
			goto fwerr;
	}
	u8 b[32];
	memset(b, 0, sizeof b);
	put32(b, 1u << 1); /* IWL_INIT_NVM : l'hôte va dire « NVM fini » */
	if (cmd("configuration initiale", GRP_SYSTEM, C_SYS_INIT_EXT_CFG, b, INIT_EXT_CFG_LEN))
		goto fwerr;
	memset(b, 0, 4);
	if (cmd("accès au NVM fini", GRP_REGULATORY, C_REG_NVM_ACCESS_COMPLETE, b, NVM_ACCESS_COMPLETE_LEN))
		goto fwerr;
	r = wait_flag(&W.init_complete, 2000);
	if (r) {
		ilog("ÉCHEC : pas de fin d'initialisation (INIT_COMPLETE) en 2 s\n");
		goto fwerr;
	}
	ilog("initialisation du micrologiciel finie\n");
	/* NVM */
	memset(b, 0, 4);
	if (cmd("lecture du NVM", GRP_REGULATORY, C_REG_NVM_GET_INFO, b, NVM_GET_INFO_LEN))
		goto fwerr;
	if (parse_nvm_info(W.resp, W.resp_len, &W.nvm)) {
		ilog("ANOMALIE : réponse du NVM de %u octets (%u attendus)\n", W.resp_len, NVM_INFO_V4_LEN);
		goto fwerr;
	}
	W.have_nvm = 1;
	u8 m[6];
	mac_from_csr(rd32(CSR_MAC_ADDR0_STRAP), rd32(CSR_MAC_ADDR1_STRAP), m);
	if (!mac_valid(m))
		mac_from_csr(rd32(CSR_MAC_ADDR0_OTP), rd32(CSR_MAC_ADDR1_OTP), m);
	memcpy(W.mac, m, 6);
	if (!mac_valid(m))
		ilog("ATTENTION : pas d'adresse MAC valide (CSR 0x380-0x38C : %08x %08x %08x %08x)\n", rd32(CSR_MAC_ADDR0_OTP),
		     rd32(CSR_MAC_ADDR1_OTP), rd32(CSR_MAC_ADDR0_STRAP), rd32(CSR_MAC_ADDR1_STRAP));
	u32 fw_tx = (W.fw.phy_config >> 16) & 0xF, fw_rx = (W.fw.phy_config >> 20) & 0xF;
	W.valid_tx = (W.nvm.tx_chains & fw_tx) ? (W.nvm.tx_chains & fw_tx) : fw_tx;
	W.valid_rx = (W.nvm.rx_chains & fw_rx) ? (W.nvm.rx_chains & fw_rx) : fw_rx;
	mvm_print_info();
	/* antennes, coexistence Bluetooth, latence du SoC */
	put32(b, W.valid_tx);
	if (cmd("antennes d'émission", GRP_LONG, C_TX_ANT_CFG, b, TX_ANT_CFG_LEN))
		goto fwerr;
	put32(b, 1);                                                          /* BT_COEX_NW */
	put32(b + 4, 0x10 | (fw_capa(&W.fw, CAPA_BT_MPLUT) ? 0x1 : 0));       /* HIGH_BAND_RET, MPLUT */
	if (cmd("coexistence Bluetooth", GRP_LONG, C_BT_CONFIG, b, BT_CONFIG_LEN))
		goto fwerr;
	if (fw_capa(&W.fw, CAPA_SOC_LATENCY)) {
		/* CNVi de Tiger Lake-H (8086:43f0) : quartz lent (12 ms), LTR appliquée après 2,5 ms */
		put32(b, (2u << 2) | 0x2);
		put32(b + 4, 12000);
		if (cmd("latence du SoC", GRP_SYSTEM, C_SYS_SOC_CONFIG, b, SOC_CONFIG_LEN))
			goto fwerr;
	}
	/* régulation (LAR) : le pays courant, avant tout balayage */
	if (fw_capa(&W.fw, CAPA_LAR_SUPPORT) && W.nvm.lar) {
		memset(b, 0, MCC_CMD_LEN);
		put16(b, 'Z' << 8 | 'Z');
		b[2] = fw_api(&W.fw, API_WIFI_MCC_UPDATE) ? 0x10 : 0; /* GET_CURRENT, sinon OLD_FW */
		if (cmd("régulation (MCC)", GRP_LONG, C_MCC_UPDATE, b, MCC_CMD_LEN))
			goto fwerr;
		struct mcc_info mi;
		int v4 = fw_capa(&W.fw, CAPA_MCC_UPDATE_11AX);
		if (parse_mcc_resp(W.resp, W.resp_len, v4, &mi))
			ilog("régulation : réponse de %u octets illisible\n", W.resp_len);
		else {
			int nv = 0;
			for (u32 i = 0; i < mi.n_channels && i < NVM_CHANNELS; i++) {
				W.mcc_flags[i] = le32(W.resp + (v4 ? 20 : 16) + 4 * i);
				nv += (W.mcc_flags[i] & NVM_CH_VALID) != 0;
			}
			W.have_mcc = mi.n_channels > 0;
			ilog("régulation : pays « %c%c » (source %u, statut %u), %u canaux dont %d permis\n",
			     mi.alpha2[0] >= 32 ? mi.alpha2[0] : '?', mi.alpha2[1] >= 32 ? mi.alpha2[1] : '?', mi.source,
			     mi.status, mi.n_channels, nv);
		}
	} else
		ilog("régulation : LAR %s (pas de MCC)\n", W.nvm.lar ? "non pris en charge" : "coupé dans le NVM");
	/* configuration du balayage (forme réduite, version 5) */
	memset(b, 0, 12);
	put32(b + 4, W.valid_tx);
	put32(b + 8, W.valid_rx);
	if (cmd("configuration du balayage", GRP_LONG, C_SCAN_CFG, b, SCAN_CFG_LEN))
		goto fwerr;
	ilog("PRÊT : micrologiciel en marche, MAC %02x:%02x:%02x:%02x:%02x:%02x\n", W.mac[0], W.mac[1], W.mac[2], W.mac[3],
	     W.mac[4], W.mac[5]);
	return 0;
fwerr:
	dump_regs("initialisation");
	if (W.dead)
		dump_fw_error();
	return -1;
}

void mvm_print_info(void)
{
	const struct nvm_info *n = &W.nvm;
	int v24 = 0, v5 = 0, act = 0;
	for (int i = 0; i < NVM_CHANNELS; i++)
		if (n->ch_flags[i] & NVM_CH_VALID) {
			if (nvm_channel_list[i] <= 14)
				v24++;
			else
				v5++;
			if (n->ch_flags[i] & NVM_CH_ACTIVE)
				act++;
		}
	ilog("NVM : version %04x, carte %u, %u adresse(s) MAC réservée(s), drapeaux %08x\n", n->nvm_version, n->board_type,
	     n->n_hw_addrs, n->flags);
	ilog("NVM : bandes%s%s, normes%s%s%s, antennes émission %x réception %x (MIMO %s), LAR %s\n",
	     n->mac_sku & NVM_SKU_24 ? " 2,4 GHz" : "", n->mac_sku & NVM_SKU_52 ? " 5 GHz" : "",
	     n->mac_sku & NVM_SKU_11N ? " n" : "", n->mac_sku & NVM_SKU_11AC ? " ac" : "",
	     n->mac_sku & NVM_SKU_11AX ? " ax" : "", W.valid_tx, W.valid_rx, (n->mac_sku & 0x20) ? "coupé" : "oui",
	     n->lar ? "oui" : "non");
	ilog("NVM : canaux valides %d en 2,4 GHz, %d en 5 GHz (%d actifs) sur %u ; adresse MAC "
	     "%02x:%02x:%02x:%02x:%02x:%02x\n",
	     v24, v5, act, n->n_channels, W.mac[0], W.mac[1], W.mac[2], W.mac[3], W.mac[4], W.mac[5]);
}

int mvm_scan(int passive, const char *ssid, long ms)
{
	struct scan_params sp;
	memset(&sp, 0, sizeof sp);
	sp.version = fw_cmd_ver(&W.fw, GRP_LONG, C_SCAN_REQ_UMAC, 15);
	sp.passive = passive;
	sp.mac = W.mac;
	if (ssid && *ssid) {
		sp.ssid = (const u8 *)ssid;
		sp.ssid_len = (int)strlen(ssid);
	}
	/* les canaux permis : ceux de la régulation (LAR : réponse de MCC_UPDATE), sinon ceux du NVM, sinon 1 à 11 ;
	 * le micrologiciel passe de lui-même en passif là où l'émission est interdite */
	for (int i = 0; i < NVM_CHANNELS && sp.n_ch < SCAN_MAX_CH; i++) {
		u8 c = nvm_channel_list[i];
		int ok = W.have_mcc ? (W.mcc_flags[i] & NVM_CH_VALID) != 0
			 : W.have_nvm && !W.nvm.lar ? (W.nvm.ch_flags[i] & NVM_CH_VALID) != 0
						    : c <= 11;
		if (ok)
			sp.ch[sp.n_ch++] = c;
	}
	if (!sp.n_ch) {
		ilog("ANOMALIE : aucun canal permis (régulation)\n");
		return -1;
	}
	u32 len = build_scan_req(scan_buf, &sp);
	W.nbss = 0;
	W.n_beacons = 0;
	W.scan_complete = 0;
	ilog("balayage %s%s%s sur %d canaux (commande v%d, %u octets)…\n", passive ? "passif" : "actif",
	     sp.ssid ? " dirigé vers " : "", sp.ssid ? ssid : "", sp.n_ch, sp.version, len);
	long t0 = now_ms();
	long r = send_cmd(GRP_LONG, C_SCAN_REQ_UMAC, scan_buf, len, 2000);
	if (r) {
		ilog("ÉCHEC : le balayage n'est pas accepté (%s)\n", r == E_FWERR ? "erreur du micrologiciel" : "délai");
		dump_regs("balayage");
		if (W.dead)
			dump_fw_error();
		return -1;
	}
	if (resp_status())
		ilog("balayage : statut de la réponse %08x\n", resp_status());
	int w = wait_flag(&W.scan_complete, ms);
	long dt = now_ms() - t0;
	if (w == E_FWERR) {
		ilog("ÉCHEC : erreur du micrologiciel pendant le balayage\n");
		dump_regs("balayage");
		dump_fw_error();
		return -1;
	}
	if (w)
		ilog("ATTENTION : pas de fin de balayage en %ld ms : liste partielle\n", ms);
	else
		ilog("balayage fini en %ld ms (statut %u, %s), %u trames reçues, %u écartées (CRC)\n", dt, W.sd.status,
		     W.sd.status == 1 ? "complet" : W.sd.status == 2 ? "interrompu" : "?", W.n_beacons, W.n_mpdu_bad);
	return 0;
}

static int by_signal(const void *a, const void *b)
{
	return ((const struct bss *)b)->signal - ((const struct bss *)a)->signal;
}

void mvm_print_scan(void)
{
	qsort(W.bss, (size_t)W.nbss, sizeof W.bss[0], by_signal);
	ilog("%d réseau(x) vu(s) :\n", W.nbss);
	ilog("   signal  canal  sécurité    BSSID              norme  SSID\n");
	for (int i = 0; i < W.nbss; i++) {
		const struct bss *b = &W.bss[i];
		char ssid[34 * 4 + 1];
		int k = 0;
		if (!b->ssid_len)
			k = snprintf(ssid, sizeof ssid, "(caché)");
		for (int j = 0; j < b->ssid_len && k < (int)sizeof ssid - 5; j++) {
			u8 ch = b->ssid[j];
			if (ch >= 32 && ch != 127)
				ssid[k++] = (char)ch; /* (UTF-8 gardé tel quel) */
			else
				k += snprintf(ssid + k, sizeof ssid - k, "\\x%02x", ch);
		}
		ssid[k] = 0;
		ilog("   %4d dBm %4u   %-11s %02x:%02x:%02x:%02x:%02x:%02x  %-5s  %s\n", b->signal, b->channel, sec_name(b),
		     b->bssid[0], b->bssid[1], b->bssid[2], b->bssid[3], b->bssid[4], b->bssid[5],
		     b->he ? "ax" : b->vht ? "ac" : b->ht ? "n" : b->channel > 14 ? "a" : "b/g", ssid);
	}
}
