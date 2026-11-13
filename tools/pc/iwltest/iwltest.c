/* iwltest.c : essai, sur la machine de construction (WSL), de la partie du pilote iwl qui ne touche
 * pas au matériel (src/svc/iwl/iwlfw.h) : lecture du vrai micrologiciel, commande de balayage,
 * trames reçues, balises et éléments RSN d'exemple, SHA-1 / HMAC / PBKDF2 / PRF / PTK / MIC / AES
 * contre les vecteurs des normes. Lancé par tools/pc/iwltest/run.sh, qui compare aussi la commande de
 * balayage et la PTK à une construction indépendante en Python (check.py).
 *
 *	iwltest FICHIER.ucode [dossier de sortie]
 */
#include "../../../src/svc/iwl/iwlfw.h"
#include <stdio.h>
#include <stdlib.h>

static int fails, checks;

#define CHECK(c, ...)                                                                                  \
	do {                                                                                           \
		checks++;                                                                              \
		if (!(c)) {                                                                            \
			fails++;                                                                       \
			printf("ÉCHEC %s:%d : ", __FILE__, __LINE__);                                  \
			printf(__VA_ARGS__);                                                           \
			printf("\n");                                                                  \
		}                                                                                      \
	} while (0)

static void hex(const u8 *p, int n, char *out)
{
	for (int i = 0; i < n; i++)
		sprintf(out + 2 * i, "%02x", p[i]);
	out[2 * n] = 0;
}

static int hexeq(const u8 *p, int n, const char *want)
{
	char b[512];
	hex(p, n, b);
	if (strcmp(b, want)) {
		printf("   obtenu  %s\n   attendu %s\n", b, want);
		return 0;
	}
	return 1;
}

static void unhex(const char *s, u8 *out)
{
	for (int i = 0; s[2 * i]; i++) {
		unsigned v;
		sscanf(s + 2 * i, "%2x", &v);
		out[i] = (u8)v;
	}
}

static u8 *slurp(const char *path, u32 *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	u8 *d = malloc(n);
	if (fread(d, 1, n, f) != (size_t)n) {
		fclose(f);
		free(d);
		return 0;
	}
	fclose(f);
	*len = (u32)n;
	return d;
}

/* ---------------------------------------------------------------- micrologiciel */
static const char *ctxt_out;

static void test_fw(const char *path)
{
	u32 len;
	u8 *d = slurp(path, &len);
	CHECK(d, "lecture de %s", path);
	if (!d)
		return;
	struct fw_image fw;
	int r = fw_parse(&fw, d, len);
	CHECK(r == 0, "fw_parse : %s", fw_err(r));
	printf("micrologiciel : %s, %u.%08x.%u, %d sections (LMAC %d, UMAC %d, pages %d), %u octets, %u TLV\n",
	       fw.human, fw.ver[0], fw.ver[1], fw.ver[2], fw.nsec, fw.n_lmac, fw.n_umac, fw.n_paging,
	       fw_total_bytes(&fw), fw.n_tlv);
	CHECK(fw.has_ver && fw.ver[0] == 77, "version 77");
	CHECK(fw.n_lmac == 14 && fw.n_umac == 15 && fw.n_paging == 20, "découpe LMAC/UMAC/pages");
	CHECK(fw.n_init == 0, "image unifiée (pas de sections INIT)");
	CHECK(fw.sec[0].offset == 0x00433000 && fw.sec[0].len == 1656, "première section du LMAC (CSS)");
	CHECK(fw.sec[fw.umac_first].offset == 0x80433000, "première section de l'UMAC");
	CHECK(fw.sec[fw.paging_first].offset == 0 && fw.sec[fw.paging_first].len == 1656, "en-tête des pages");
	CHECK(fw.phy_config == 0x330018, "PHY_SKU %08x", fw.phy_config);
	CHECK(((fw.phy_config >> 16) & 15) == 3 && ((fw.phy_config >> 20) & 15) == 3, "antennes A+B");
	CHECK(fw.calib_flow == 0x5f1601d3 && fw.calib_event == 0x5b06958b, "étalonnage par défaut");
	CHECK(fw_capa(&fw, CAPA_LAR_SUPPORT) && fw_capa(&fw, CAPA_UMAC_SCAN), "LAR, balayage UMAC");
	CHECK(fw_capa(&fw, CAPA_SOC_LATENCY), "latence du SoC");
	CHECK(fw_capa(&fw, CAPA_FW_RESET_HANDSHAKE), "poignée de main de l'arrêt");
	CHECK(fw_capa(&fw, CAPA_MCC_UPDATE_11AX), "MCC 11ax (réponse v4)");
	CHECK(!fw_capa(&fw, CAPA_DQA_SUPPORT), "pas de DQA_ENABLE");
	CHECK(fw_api(&fw, API_REGULATORY_NVM_INFO) && fw_api(&fw, API_REDUCED_SCAN_CONFIG) &&
		      fw_api(&fw, API_STA_TYPE) && fw_api(&fw, API_WIFI_MCC_UPDATE),
	      "API : NVM v4, config. de balayage réduite, type de station, MCC Wi-Fi");
	int sv = fw_cmd_ver(&fw, GRP_LONG, C_SCAN_REQ_UMAC, 0);
	CHECK(sv >= 14 && sv <= 17, "SCAN_REQ_UMAC v%d", sv);
	CHECK(sv == 15, "SCAN_REQ_UMAC v15 (77)");
	CHECK(fw_cmd_ver(&fw, GRP_LONG, C_SCAN_CFG, 0) == 5, "SCAN_CFG v5");
	CHECK(fw_notif_ver(&fw, GRP_LEGACY, C_ALIVE, 0) == 6, "vivant v6");
	CHECK(fw_notif_ver(&fw, GRP_REGULATORY, C_REG_NVM_GET_INFO, 0) == 4, "NVM_GET_INFO v4");
	CHECK(fw_cmd_ver(&fw, GRP_SYSTEM, C_SYS_SOC_CONFIG, 0) == 2, "SOC_CONFIGURATION v2");
	CHECK(fw_cmd_ver(&fw, GRP_LONG, C_BT_CONFIG, 0) == 6, "BT_CONFIG v6");
	CHECK(fw_cmd_ver(&fw, GRP_LONG, C_TX_ANT_CFG, 0) == 1, "TX_ANT v1");
	CHECK(fw_cmd_ver(&fw, GRP_LONG, C_MCC_UPDATE, 0) == 1, "MCC_UPDATE v1");
	CHECK(fw_notif_ver(&fw, GRP_REGULATORY, C_REG_PNVM_INIT_COMPLETE, 0) == 1, "PNVM_INIT_COMPLETE");
	printf("versions : balayage %d, config. %d, vivant %d, NVM %d, MCC %d/%d, SoC %d, BT %d\n", sv,
	       fw_cmd_ver(&fw, GRP_LONG, C_SCAN_CFG, 0), fw_notif_ver(&fw, GRP_LEGACY, C_ALIVE, 0),
	       fw_notif_ver(&fw, GRP_REGULATORY, C_REG_NVM_GET_INFO, 0), fw_cmd_ver(&fw, GRP_LONG, C_MCC_UPDATE, 0),
	       fw_notif_ver(&fw, GRP_LONG, C_MCC_UPDATE, 0), fw_cmd_ver(&fw, GRP_SYSTEM, C_SYS_SOC_CONFIG, 0),
	       fw_cmd_ver(&fw, GRP_LONG, C_BT_CONFIG, 0));
	/* sections : bornes, pas de chevauchement dans le fichier */
	for (int i = 0; i < fw.nsec; i++)
		CHECK(fw.sec[i].data >= d && fw.sec[i].data + fw.sec[i].len <= d + len, "section %d dans le fichier", i);
	/* fichier tronqué, corrompu */
	CHECK(fw_parse(&fw, d, 40) == FWE_SHORT, "trop court");
	d[4] ^= 1;
	CHECK(fw_parse(&fw, d, len) == FWE_MAGIC, "magique");
	d[4] ^= 1;
	CHECK(fw_parse(&fw, d, len - 1000) == FWE_TLV, "TLV coupé");
	/* context info : adresses factices, comparées par check.py à la disposition publique */
	fw_parse(&fw, d, len);
	static u64 pa[FW_MAX_SEC];
	for (int i = 0; i < fw.nsec; i++)
		pa[i] = 0x30000000ull + 0x10000ull * (u64)i;
	static u8 ci[CTXT_INFO_SIZE];
	build_ctxt_info(ci, 0x354, 0x10000000, 0x20000000, &fw, pa);
	CHECK(le64(ci + 704) == 0x30000000ull && le64(ci + 192) == 0x30000000ull + 0x10000ull * 15, "UMAC puis LMAC");
	if (ctxt_out) {
		FILE *f = fopen(ctxt_out, "wb");
		fwrite(ci, 1, sizeof ci, f);
		fclose(f);
	}
	free(d);
	/* nom du fichier d'après les registres */
	CHECK(!strcmp(fw_name_for(0x354, 0x10a100), "iwlwifi-QuZ-a0-hr-b0"), "QuZ (Tiger Lake)");
	CHECK(!strcmp(fw_name_for(0x351, 0x10a100), "iwlwifi-QuZ-a0-hr-b0"), "QuZ 0x351");
	CHECK(!strcmp(fw_name_for(0x332, 0x10a100), "iwlwifi-Qu-c0-hr-b0"), "Qu C0");
	CHECK(!strcmp(fw_name_for(0x331, 0x10c100), "iwlwifi-Qu-b0-hr-b0"), "Qu B0 Hr1");
	CHECK(fw_name_for(0x354, 0x10d000) == 0, "Gf : pas un 22000");
}

/* ---------------------------------------------------------------- commande de balayage */
static void test_scan(const char *outdir)
{
	static u8 b[2048];
	static const u8 mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
	struct scan_params sp = {0};
	sp.version = 15;
	sp.mac = mac;
	for (int i = 0; i < 13; i++)
		sp.ch[sp.n_ch++] = (u8)(i + 1);
	static const u8 c5[] = {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140};
	for (unsigned i = 0; i < sizeof c5; i++)
		sp.ch[sp.n_ch++] = c5[i];
	sp.uid = 0;
	u32 n = build_scan_req(b, &sp);
	CHECK(n == 1940, "taille %u", n);
	CHECK(SR_PROBE + 1344 == 1940 && SR_CHANNEL == 44 && SR_PERIODIC == 584 && SR_PROBE == 596, "décalages");
	CHECK(le16(b + SR_GENERAL) == 0x0082, "drapeaux généraux %04x", le16(b + SR_GENERAL));
	CHECK(b[SR_CHANNEL + 1] == 32, "32 canaux");
	CHECK(b[SR_CHANNEL + 4 + 8 * 13 + 4] == 36 && b[SR_CHANNEL + 4 + 8 * 13 + 5] == 0, "canal 36, bande 5 GHz");
	CHECK(b[SR_CHANNEL + 4 + 4] == 1 && b[SR_CHANNEL + 4 + 5] == 1 && le32(b + SR_CHANNEL + 4) == 1,
	      "canal 1, bande 2,4 GHz, sonde générique");
	CHECK(b[SR_DIRECT] == 0 && b[SR_DIRECT + 1] == 0, "SSID générique");
	CHECK(!memcmp(b + SR_PREQ_BUF + 10, mac, 6), "adresse source du gabarit");
	if (outdir) {
		char p[512];
		snprintf(p, sizeof p, "%s/sizes.txt", outdir);
		FILE *fs = fopen(p, "w");
		fprintf(fs, "init_ext_cfg %d\nnvm_access_complete %d\nnvm_get_info %d\ntx_ant_cfg %d\nbt_config %d\n"
			    "soc_config %d\nmcc_update %d\nscan_cfg %d\nscan_req %u\n",
			INIT_EXT_CFG_LEN, NVM_ACCESS_COMPLETE_LEN, NVM_GET_INFO_LEN, TX_ANT_CFG_LEN, BT_CONFIG_LEN,
			SOC_CONFIG_LEN, MCC_CMD_LEN, SCAN_CFG_LEN, n);
		fclose(fs);
		snprintf(p, sizeof p, "%s/scan_v15.bin", outdir);
		FILE *f = fopen(p, "wb");
		fwrite(b, 1, n, f);
		fclose(f);
	}
	/* passif, version 17 (bande dans les drapeaux) */
	sp.passive = 1;
	sp.version = 17;
	build_scan_req(b, &sp);
	CHECK(le16(b + SR_GENERAL) == 0x0882, "passif : drapeaux %04x", le16(b + SR_GENERAL));
	CHECK(le32(b + SR_CHANNEL + 4) == (1u << 30), "v17 : bande 2,4 GHz dans les drapeaux");
	CHECK(b[SR_CHANNEL + 4 + 5] == 0, "v17 : octet de bande vide");
	/* dirigé */
	sp.passive = 0;
	sp.version = 15;
	sp.ssid = (const u8 *)"Maison";
	sp.ssid_len = 6;
	build_scan_req(b, &sp);
	CHECK(b[SR_DIRECT + 1] == 6 && !memcmp(b + SR_DIRECT + 2, "Maison", 6), "SSID dirigé");
	CHECK(le16(b + SR_GENERAL) == 0x00c2 && le16(b + SR_GENERAL + 10) == 100, "dirigé : toutes les chaînes, budget 100");
}

/* ---------------------------------------------------------------- réponses */
static void test_parse(void)
{
	u8 a[144];
	memset(a, 0, sizeof a);
	put16(a, ALIVE_OK);
	put32(a + 4, 77);
	put32(a + 4 + 20, 0x00801234);
	put32(a + 4 + 40, 0x00800a80);
	put32(a + 100 + 8, 0xC0401000); /* UMAC : bits du cache à enlever */
	put32(a + 116, 0x11);
	struct alive_info ai;
	CHECK(parse_alive(a, 144, &ai) == 0 && ai.version_by_len == 6, "vivant v6");
	CHECK(ai.status == ALIVE_OK && ai.lmac_major == 77 && ai.lmac_err == 0x00801234 && ai.scd_base == 0x00800a80,
	      "vivant : champs du LMAC");
	CHECK(ai.umac_err == 0x00401000 && ai.sku_id[0] == 0x11, "vivant : UMAC, SKU");
	CHECK(parse_alive(a, 68, &ai) == 0 && ai.version_by_len == 3, "vivant v3");
	CHECK(parse_alive(a, 100, &ai) < 0, "vivant de taille inconnue");

	u8 nv[NVM_INFO_V4_LEN];
	memset(nv, 0, sizeof nv);
	put16(nv + 4, 0x0a1d);
	nv[7] = 2;
	put32(nv + 8, 0x1f);
	put32(nv + 12, 3);
	put32(nv + 16, 3);
	put32(nv + 20, 1);
	put32(nv + 24, 51);
	put32(nv + 28, 0xf0b);
	put32(nv + 28 + 4 * 14, 0x19);
	struct nvm_info ni;
	CHECK(parse_nvm_info(nv, sizeof nv, &ni) == 0 && ni.nvm_version == 0x0a1d && ni.n_hw_addrs == 2 &&
		      ni.mac_sku == 0x1f && ni.lar == 1 && ni.n_channels == 51,
	      "NVM v4");
	CHECK(ni.ch_flags[0] == 0xf0b && ni.ch_flags[14] == 0x19 && nvm_channel_list[14] == 36, "NVM : canaux");
	CHECK(parse_nvm_info(nv, 300, &ni) < 0, "NVM v3 refusée");

	u8 m[20 + 4 * 3];
	memset(m, 0, sizeof m);
	put16(m + 4, 'F' << 8 | 'R');
	m[12] = 5;
	put32(m + 16, 3);
	struct mcc_info mi;
	CHECK(parse_mcc_resp(m, sizeof m, 1, &mi) == 0 && !strcmp(mi.alpha2, "FR") && mi.n_channels == 3 && mi.source == 5,
	      "MCC v4");
	CHECK(parse_mcc_resp(m, sizeof m - 4, 1, &mi) == -2, "MCC : longueur fausse");

	u8 mac[6];
	mac_from_csr(0xa0b1c2d3, 0x0000e4f5, mac);
	CHECK(hexeq(mac, 6, "a0b1c2d3e4f5"), "MAC des CSR");
	CHECK(mac_valid(mac), "MAC valide");
	mac[0] = 1;
	CHECK(!mac_valid(mac), "MAC de groupe refusée");

	u8 sc[16] = {1, 0, 0, 0, 0, 0, 1, 0};
	struct scan_done sd;
	CHECK(parse_scan_complete(sc, 16, &sd) == 0 && sd.uid == 1 && sd.status == 1, "fin de balayage");
}

/* ---------------------------------------------------------------- balises */
static u32 mk_beacon(u8 *f, int sub, const char *ssid, int ch, const u8 *extra, u32 elen, u16 capab)
{
	memset(f, 0, 24);
	f[0] = (u8)(sub << 4);
	memset(f + 4, 0xff, 6);
	static const u8 bss[6] = {0x00, 0x24, 0xd4, 0x01, 0x02, 0x03};
	memcpy(f + 10, bss, 6);
	memcpy(f + 16, bss, 6);
	u8 *b = f + 24;
	memset(b, 0, 12);
	put16(b + 8, 100);
	put16(b + 10, capab);
	u32 o = 12;
	b[o++] = 0;
	b[o++] = (u8)strlen(ssid);
	memcpy(b + o, ssid, strlen(ssid));
	o += strlen(ssid);
	b[o++] = 1;
	b[o++] = 2;
	b[o++] = 0x82;
	b[o++] = 0x84;
	if (ch) {
		b[o++] = 3;
		b[o++] = 1;
		b[o++] = (u8)ch;
	}
	memcpy(b + o, extra, elen);
	return o + elen; /* longueur du corps */
}

static void test_beacons(void)
{
	u8 f[512];
	struct bss b;
	/* WPA2-PSK, CCMP */
	static const u8 rsn_psk[] = {48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 2, 0x0c, 0};
	u32 n = mk_beacon(f, 8, "Maison", 6, rsn_psk, sizeof rsn_psk, 0x0411);
	CHECK(parse_beacon(f, f + 24, n, 0, -42, &b) == 1, "balise WPA2");
	CHECK(b.ssid_len == 6 && !strcmp((char *)b.ssid, "Maison") && b.channel == 6 && b.signal == -42, "SSID, canal");
	CHECK(b.sec == SEC_WPA2 && b.akm == AKM_PSK && b.pairwise == CIPH_CCMP && b.group == CIPH_CCMP,
	      "WPA2-PSK CCMP : sec %d akm %x", b.sec, b.akm);
	CHECK(!strcmp(sec_name(&b), "WPA2") && b.beacon_int == 100, "nom");
	/* transition WPA2/WPA3 : PSK + SAE, MFPC */
	static const u8 rsn_t[] = {48, 24, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 2, 0, 0, 0x0f, 0xac, 2, 0, 0x0f, 0xac, 8, 0x80, 0};
	n = mk_beacon(f, 8, "Box", 36, rsn_t, sizeof rsn_t, 0x0011);
	CHECK(parse_beacon(f, f + 24, n, 36, -60, &b) == 1 && b.sec == SEC_WPA2 && (b.akm & AKM_SAE) && b.mfpc && !b.mfpr,
	      "transition WPA2/WPA3");
	CHECK(!strcmp(sec_name(&b), "WPA2/WPA3"), "nom de la transition : %s", sec_name(&b));
	/* WPA3 seul (SAE, MFPR) */
	static const u8 rsn_sae[] = {48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 8, 0xc0, 0};
	n = mk_beacon(f, 5, "Wpa3", 0, rsn_sae, sizeof rsn_sae, 0x0011);
	CHECK(parse_beacon(f, f + 24, n, 44, -70, &b) == 1 && b.sec == SEC_WPA3 && b.mfpr && b.probe_resp && b.channel == 44,
	      "WPA3 (réponse de sonde, canal de réception)");
	/* ancien WPA (TKIP) */
	static const u8 wpa1[] = {221, 22, 0, 0x50, 0xf2, 1, 1, 0, 0, 0x50, 0xf2, 2, 1, 0, 0, 0x50, 0xf2, 2, 1, 0, 0, 0x50, 0xf2, 2};
	n = mk_beacon(f, 8, "Vieux", 11, wpa1, sizeof wpa1, 0x0011);
	CHECK(parse_beacon(f, f + 24, n, 0, -80, &b) == 1 && b.sec == SEC_WPA && b.wpa1 && b.pairwise == CIPH_TKIP,
	      "WPA (TKIP)");
	/* WEP, ouvert, caché, entreprise */
	n = mk_beacon(f, 8, "Wep", 1, 0, 0, 0x0011);
	CHECK(parse_beacon(f, f + 24, n, 0, -50, &b) == 1 && b.sec == SEC_WEP, "WEP");
	n = mk_beacon(f, 8, "Libre", 1, 0, 0, 0x0001);
	CHECK(parse_beacon(f, f + 24, n, 0, -50, &b) == 1 && b.sec == SEC_OPEN && !strcmp(sec_name(&b), "ouvert"), "ouvert");
	n = mk_beacon(f, 8, "", 1, 0, 0, 0x0001);
	CHECK(parse_beacon(f, f + 24, n, 0, -50, &b) == 1 && b.ssid_len == 0, "SSID caché");
	static const u8 rsn_ent[] = {48, 20, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 4, 1, 0, 0, 0x0f, 0xac, 1, 0, 0};
	n = mk_beacon(f, 8, "Eduroam", 1, rsn_ent, sizeof rsn_ent, 0x0011);
	CHECK(parse_beacon(f, f + 24, n, 0, -50, &b) == 1 && b.sec == SEC_ENT, "entreprise");
	/* trame de données, élément coupé, RSN illisible */
	f[0] = 0x08;
	CHECK(parse_beacon(f, f + 24, n, 0, -50, &b) == 0, "données ignorées");
	static const u8 cut[] = {48, 40, 1, 0};
	n = mk_beacon(f, 8, "Coupe", 3, cut, sizeof cut, 0x0011);
	CHECK(parse_beacon(f, f + 24, n, 0, -50, &b) == 1 && b.channel == 3 && !b.rsn && b.sec == SEC_WEP,
	      "élément coupé : le reste gardé");
	struct bss z;
	memset(&z, 0, sizeof z);
	static const u8 badver[] = {2, 0, 0, 0x0f, 0xac, 4};
	CHECK(parse_rsn(badver, sizeof badver, 0, &z) < 0, "RSN version 2 refusée");
}

/* ---------------------------------------------------------------- RX : un RB à deux paquets */
static void test_rx(void)
{
	u8 rb[4096];
	memset(rb, 0, sizeof rb);
	/* paquet 1 : vivant (144 o) ; len_n_flags = 4 (en-tête) + 144 */
	put32(rb, 4 + 144);
	rb[4] = C_ALIVE;
	rb[5] = 0;
	put16(rb + 6, 0x8000);
	/* paquet 2 à l'arrondi de 64 : RX_MPDU avec une balise */
	u32 o = (4 + 4 + 144 + 63) & ~63u;
	u8 fr[300];
	u32 bl = mk_beacon(fr, 8, "Maison", 6, 0, 0, 0x0001);
	u32 mlen = 24 + bl;
	put32(rb + o, 4 + RX_DESC_V1 + mlen);
	rb[o + 4] = C_RX_MPDU;
	put16(rb + o + 6, 0x8000);
	u8 *desc = rb + o + 8;
	put16(desc, mlen);
	put32(desc + 12, RX_CRC_OK | RX_OVERRUN_OK);
	desc[32] = 47;
	desc[33] = 52;
	desc[34] = 6;
	memcpy(desc + RX_DESC_V1, fr, mlen);
	u32 o2 = (o + 4 + 4 + RX_DESC_V1 + mlen + 63) & ~63u;
	put32(rb + o2, 0x55550000); /* fin */
	/* la boucle de trans.c, en miniature */
	int seen = 0, alive = 0, beacon = 0;
	for (u32 off = 0; off + 8 < sizeof rb;) {
		u32 lnf = le32(rb + off);
		if (lnf == 0x55550000)
			break;
		u32 plen = lnf & 0x3FFF;
		if (plen < 4)
			break;
		u8 cmd = rb[off + 4];
		const u8 *pl = rb + off + 8;
		u32 pll = plen - 4;
		seen++;
		if (cmd == C_ALIVE)
			alive = pll == 144;
		if (cmd == C_RX_MPDU) {
			struct rx_frame rf;
			struct bss b;
			if (parse_rx_mpdu(pl, pll, &rf) == 0 && (rf.status & 3) == 3 &&
			    parse_beacon(rf.frame, rf.frame + rf.hdrlen + (rf.pad ? 2 : 0), rf.len - rf.hdrlen, rf.channel,
					 rf.signal, &b))
				beacon = b.signal == -47 && b.channel == 6 && b.ssid_len == 6;
		}
		off += (plen + 4 + 63) & ~63u;
	}
	CHECK(seen == 2 && alive && beacon, "RB : %d paquets, vivant %d, balise %d", seen, alive, beacon);
}

/* ---------------------------------------------------------------- crypto */
static void test_crypto(const char *outdir)
{
	u8 h[20], k[64];
	sha1((const u8 *)"abc", 3, h);
	CHECK(hexeq(h, 20, "a9993e364706816aba3e25717850c26c9cd0d89d"), "SHA-1 « abc »");
	sha1((const u8 *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56, h);
	CHECK(hexeq(h, 20, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"), "SHA-1 à deux blocs");
	/* RFC 2202, cas 1 et 6 */
	memset(k, 0x0b, 20);
	hmac_sha1(k, 20, (const u8 *)"Hi There", 8, h);
	CHECK(hexeq(h, 20, "b617318655057264e28bc0b6fb378c8ef146be00"), "HMAC-SHA1 RFC 2202 n° 1");
	memset(k, 0xaa, 64);
	u8 big[80];
	memset(big, 0xaa, 80);
	hmac_sha1(big, 80, (const u8 *)"Test Using Larger Than Block-Size Key - Hash Key First", 54, h);
	CHECK(hexeq(h, 20, "aa4ae5e15272d00e95705637ce8a3b55ed402112"), "HMAC-SHA1 RFC 2202 n° 6");
	/* 802.11-2020 J.4 : PSK par PBKDF2 */
	u8 pmk[32];
	wpa_pmk("password", (const u8 *)"IEEE", 4, pmk);
	CHECK(hexeq(pmk, 32, "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e"), "PMK J.4 n° 1");
	wpa_pmk("ThisIsAPassword", (const u8 *)"ThisIsASSID", 11, pmk);
	CHECK(hexeq(pmk, 32, "0dc0d6eb90555ed6419756b9a15ec3e3209b63df707dd508d14581f8982721af"), "PMK J.4 n° 2");
	/* RFC 6070 (PBKDF2-HMAC-SHA1) */
	u8 o[25];
	pbkdf2_sha1((const u8 *)"password", 8, (const u8 *)"salt", 4, 2, o, 20);
	CHECK(hexeq(o, 20, "ea6c014dc72d6f8ccd1ed92ace1d41f0d8de8957"), "RFC 6070 c=2");
	pbkdf2_sha1((const u8 *)"passwordPASSWORDpassword", 24, (const u8 *)"saltSALTsaltSALTsaltSALTsaltSALTsalt", 36, 4096,
		    o, 25);
	CHECK(hexeq(o, 25, "3d2eec4fe41c849b80c8d83662c0e44a8b291a964cf2f07038"), "RFC 6070 25 octets");
	/* PRF de 802.11i (J.3, n° 1 : clé 0x0b × 20, « prefix », « Hi There ») */
	u8 prf[64];
	memset(k, 0x0b, 20);
	prf_sha1(k, 20, "prefix", (const u8 *)"Hi There", 8, prf, 64);
	CHECK(hexeq(prf, 64,
		    "bcd4c650b30b9684951829e0d75f9d54b862175ed9f00606e17d8da35402ffee75df78c3d31e0f889f012120c0862beb67753e7439ae242edb8373698356cf5a"),
	      "PRF-512 J.3 n° 1");
	/* PTK et MIC : données d'exemple, comparées à check.py (hmac de Python) */
	u8 aa[6], spa[6], an[32], sn[32], ptk[48];
	unhex("a0a1a2a3a4a5", aa);
	unhex("b0b1b2b3b4b5", spa);
	for (int i = 0; i < 32; i++)
		an[i] = (u8)(0xe0 + i), sn[i] = (u8)(0xc0 + i);
	wpa_pmk("ThisIsAPassword", (const u8 *)"ThisIsASSID", 11, pmk);
	wpa_ptk(pmk, aa, spa, an, sn, ptk);
	u8 eapol[121], mic[16];
	for (int i = 0; i < 121; i++)
		eapol[i] = (u8)(i * 7 + 3);
	memset(eapol + 81, 0, 16); /* champ MIC à zéro */
	eapol_mic(ptk, eapol, sizeof eapol, mic);
	if (outdir) {
		char p[512], b1[200], b2[40];
		snprintf(p, sizeof p, "%s/ptk.txt", outdir);
		FILE *f = fopen(p, "w");
		hex(ptk, 48, b1);
		hex(mic, 16, b2);
		fprintf(f, "%s\n%s\n", b1, b2);
		fclose(f);
	}
	/* AES-128 (FIPS 197 C.1) et déballage (RFC 3394 4.1) */
	u8 key[16], pt[16], ct[16];
	unhex("000102030405060708090a0b0c0d0e0f", key);
	unhex("00112233445566778899aabbccddeeff", pt);
	aes128_encrypt(key, pt, ct);
	CHECK(hexeq(ct, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"), "AES-128 FIPS 197");
	u8 wr[24], un[16];
	unhex("1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5", wr);
	CHECK(aes_unwrap(key, wr, 24, un) == 0 && hexeq(un, 16, "00112233445566778899aabbccddeeff"), "RFC 3394 4.1");
	wr[3] ^= 1;
	CHECK(aes_unwrap(key, wr, 24, un) == -2, "RFC 3394 : intégrité");
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("iwltest FICHIER.ucode [dossier de sortie]\n");
		return 2;
	}
	const char *out = argc > 2 ? argv[2] : 0;
	static char cp[512];
	if (out) {
		snprintf(cp, sizeof cp, "%s/ctxt.bin", out);
		ctxt_out = cp;
	}
	test_fw(argv[1]);
	test_scan(out);
	test_parse();
	test_beacons();
	test_rx();
	test_crypto(out);
	printf("%d vérifications, %d échec(s)\n", checks, fails);
	return fails ? 1 : 0;
}
