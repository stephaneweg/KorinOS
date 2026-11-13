/* fw.c : le fichier du micrologiciel d'Intel (iwlwifi-*.ucode, format TLV), docs/etude/WIFI-PC.md 3.
 *
 * En-tête de 88 octets (un mot nul, « IWL\n », 64 octets de texte, version, numéro de construction,
 * 8 octets), puis des TLV (type 32 bits, longueur 32 bits, données arrondies à 4 octets). Les
 * sections de l'image d'exécution (TLV 19) portent chacune leur adresse dans la puce sur leurs 4
 * premiers octets ; deux sections « séparateurs » (adresses 0xFFFFCCCC puis 0xAAAABBBB) découpent la
 * liste en trois : processeur LMAC, processeur UMAC, pages du micrologiciel. Le transport « gen2 »
 * des 22000 donne ces trois listes à la puce dans le « context info » (trans.c) ; la puce les copie
 * elle-même et vérifie la signature (la première section de chaque part est l'en-tête CSS signé). */
#include "iwlfw.h"

const char *fw_err(int e)
{
	switch (e) {
	case FWE_SHORT: return "fichier trop court";
	case FWE_MAGIC: return "pas un micrologiciel TLV d'iwlwifi (mot magique)";
	case FWE_TLV: return "un TLV dépasse la fin du fichier";
	case FWE_MANY: return "trop de sections";
	case FWE_LAYOUT: return "séparateurs LMAC/UMAC absents (pas une image gen2)";
	}
	return "erreur inconnue";
}

static void bits_set(u32 *tab, u32 index, u32 bits)
{
	if (index < 4)
		tab[index] |= bits;
}

int fw_parse(struct fw_image *fw, const u8 *d, u32 len)
{
	memset(fw, 0, sizeof *fw);
	if (len < FW_HDR_SIZE)
		return FWE_SHORT;
	if (le32(d) != 0 || le32(d + 4) != FW_MAGIC)
		return FWE_MAGIC;
	memcpy(fw->human, d + 8, 64);
	fw->human[64] = 0;
	fw->ucode_ver = le32(d + 72);
	fw->build = le32(d + 76);
	u32 o = FW_HDR_SIZE;
	while (o + 8 <= len) {
		u32 type = le32(d + o), tl = le32(d + o + 4);
		const u8 *p = d + o + 8;
		if (tl > len - o - 8)
			return FWE_TLV;
		fw->n_tlv++;
		if (type >= 0x1000000)
			fw->n_dbg_tlv++; /* (TLV de débogage « ini » : sans objet ici) */
		switch (type) {
		case TLV_SEC_RT:
		case TLV_SECURE_SEC_RT:
			if (tl < 4)
				return FWE_TLV;
			if (fw->nsec >= FW_MAX_SEC)
				return FWE_MANY;
			fw->sec[fw->nsec].offset = le32(p);
			fw->sec[fw->nsec].data = p + 4;
			fw->sec[fw->nsec].len = tl - 4;
			fw->nsec++;
			break;
		case TLV_SEC_INIT:
			fw->n_init++;
			break;
		case TLV_DEF_CALIB:
			if (tl >= 12 && le32(p) == 0) { /* image REGULAR */
				fw->calib_flow = le32(p + 4);
				fw->calib_event = le32(p + 8);
			}
			break;
		case TLV_PHY_SKU:
			if (tl >= 4)
				fw->phy_config = le32(p);
			break;
		case TLV_FLAGS:
			if (tl >= 4)
				fw->flags = le32(p);
			break;
		case TLV_NUM_OF_CPU:
			if (tl >= 4)
				fw->num_cpu = le32(p);
			break;
		case TLV_API_CHANGES_SET:
			if (tl >= 8)
				bits_set(fw->api, le32(p), le32(p + 4));
			break;
		case TLV_ENABLED_CAPABILITIES:
			if (tl >= 8)
				bits_set(fw->capa, le32(p), le32(p + 4));
			break;
		case TLV_N_SCAN_CHANNELS:
			if (tl >= 4)
				fw->n_scan_channels = le32(p);
			break;
		case TLV_PAGING:
			fw->paging_tlv = 1;
			break;
		case TLV_FW_VERSION:
			if (tl >= 12) {
				fw->ver[0] = le32(p);
				fw->ver[1] = le32(p + 4);
				fw->ver[2] = le32(p + 8);
				fw->has_ver = 1;
			}
			break;
		case TLV_CMD_VERSIONS:
			fw->cmd_versions = p;
			fw->n_cmd_versions = tl / 4;
			break;
		case TLV_IML:
			fw->iml = 1;
			break;
		case TLV_FW_NUM_STATIONS:
			if (tl >= 4)
				fw->num_stations = le32(p);
			break;
		}
		o += 8 + ((tl + 3) & ~3u);
	}
	/* trois parts : LMAC | séparateur | UMAC | séparateur | pagination */
	int i = 0;
	while (i < fw->nsec && fw->sec[i].offset != FW_SEC_CPU_SEPARATOR &&
	       fw->sec[i].offset != FW_SEC_PAGING_SEPARATOR)
		i++;
	fw->n_lmac = i;
	if (i >= fw->nsec || fw->sec[i].offset != FW_SEC_CPU_SEPARATOR)
		return FWE_LAYOUT;
	fw->umac_first = ++i;
	while (i < fw->nsec && fw->sec[i].offset != FW_SEC_CPU_SEPARATOR &&
	       fw->sec[i].offset != FW_SEC_PAGING_SEPARATOR)
		i++;
	fw->n_umac = i - fw->umac_first;
	if (i < fw->nsec) {
		fw->paging_first = ++i;
		while (i < fw->nsec && fw->sec[i].offset != FW_SEC_CPU_SEPARATOR &&
		       fw->sec[i].offset != FW_SEC_PAGING_SEPARATOR)
			i++;
		fw->n_paging = i - fw->paging_first;
	} else
		fw->paging_first = fw->nsec;
	if (!fw->n_lmac || !fw->n_umac || fw->n_lmac > FW_MAX_DRAM || fw->n_umac > FW_MAX_DRAM ||
	    fw->n_paging > FW_MAX_DRAM)
		return FWE_LAYOUT;
	return 0;
}

int fw_capa(const struct fw_image *fw, unsigned bit)
{
	return bit < 128 && (fw->capa[bit / 32] >> (bit % 32) & 1);
}

int fw_api(const struct fw_image *fw, unsigned bit)
{
	return bit < 128 && (fw->api[bit / 32] >> (bit % 32) & 1);
}

static int lookup(const struct fw_image *fw, u8 grp, u8 cmd, int notif, int dflt)
{
	for (u32 i = 0; i < fw->n_cmd_versions; i++) {
		const u8 *e = fw->cmd_versions + 4 * i;
		if (e[0] == cmd && e[1] == grp) {
			int v = e[notif ? 3 : 2];
			return v == 99 ? dflt : v; /* 99 : « inconnue » pour le micrologiciel */
		}
	}
	return dflt;
}

int fw_cmd_ver(const struct fw_image *fw, u8 grp, u8 cmd, int dflt)
{
	return lookup(fw, grp, cmd, 0, dflt);
}

int fw_notif_ver(const struct fw_image *fw, u8 grp, u8 cmd, int dflt)
{
	return lookup(fw, grp, cmd, 1, dflt);
}

u32 fw_total_bytes(const struct fw_image *fw)
{
	u32 t = 0;
	for (int i = 0; i < fw->nsec; i++)
		if (fw->sec[i].offset != FW_SEC_CPU_SEPARATOR && fw->sec[i].offset != FW_SEC_PAGING_SEPARATOR)
			t += fw->sec[i].len;
	return t;
}

const char *fw_name_for(u32 hw_rev, u32 rf_id)
{
	u32 mac = (hw_rev >> 4) & 0xFFF, step = hw_rev & 0xF, rf = (rf_id >> 12) & 0xFFF;
	int hr = rf == 0x10A || rf == 0x10C; /* Hr2 (AX201), Hr1 (AX101) */
	int jf = rf == 0x105 || rf == 0x108; /* Jf (9560/9461/9462) */
	if (mac == 0x35)
		return hr ? "iwlwifi-QuZ-a0-hr-b0" : jf ? "iwlwifi-QuZ-a0-jf-b0" : 0;
	if (mac == 0x33) {
		if (step >= 2)
			return hr ? "iwlwifi-Qu-c0-hr-b0" : jf ? "iwlwifi-Qu-c0-jf-b0" : 0;
		return hr ? "iwlwifi-Qu-b0-hr-b0" : jf ? "iwlwifi-Qu-b0-jf-b0" : 0;
	}
	return 0;
}
