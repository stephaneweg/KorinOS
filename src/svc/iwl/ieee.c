/* ieee.c : les balises et réponses de sonde reçues pendant un balayage (802.11-2020, 9.3.3.3 et
 * 9.3.3.11 ; éléments 9.4.2) : SSID, canal, capacités, sécurité (RSN 9.4.2.24, ancien WPA de Wi-Fi
 * Alliance dans un élément propriétaire 00:50:F2 type 1). docs/etude/WIFI-PC.md 6. */
#include "iwlfw.h"

#define E_SSID 0
#define E_DS 3
#define E_HT_CAP 45
#define E_RSN 48
#define E_HT_OP 61
#define E_VHT_CAP 191
#define E_VENDOR 221
#define E_EXT 255
#define EXT_HE_CAP 35

static const u8 oui_rsn[3] = {0x00, 0x0F, 0xAC};
static const u8 oui_wpa[3] = {0x00, 0x50, 0xF2};

static u8 cipher_bit(const u8 *s, const u8 *oui)
{
	if (memcmp(s, oui, 3))
		return CIPH_OTHER;
	switch (s[3]) {
	case 1:
	case 5:
		return CIPH_WEP;
	case 2:
		return CIPH_TKIP;
	case 4:
		return CIPH_CCMP;
	case 8:
	case 9:
		return CIPH_GCMP;
	}
	return CIPH_OTHER;
}

static u8 akm_bit(const u8 *s, const u8 *oui, int wpa1)
{
	if (memcmp(s, oui, 3))
		return AKM_OTHER;
	switch (s[3]) {
	case 1:
		return AKM_8021X;
	case 2:
		return AKM_PSK;
	}
	if (wpa1)
		return AKM_OTHER;
	switch (s[3]) {
	case 3:  /* FT-802.1X */
	case 5:  /* 802.1X-SHA256 */
	case 11: /* suite B */
	case 12:
	case 13:
		return AKM_8021X;
	case 4: /* FT-PSK */
		return AKM_PSK;
	case 6:
		return AKM_PSK256;
	case 8: /* SAE */
	case 9: /* FT-SAE */
	case 24: /* SAE-EXT-KEY */
		return AKM_SAE;
	case 18:
		return AKM_OWE;
	}
	return AKM_OTHER;
}

int parse_rsn(const u8 *ie, u32 len, int wpa1, struct bss *b)
{
	const u8 *oui = wpa1 ? oui_wpa : oui_rsn;
	u32 o = 0;
	if (len < 2 || le16(ie) != 1)
		return -1; /* version 1 seulement */
	o = 2;
	if (wpa1)
		b->wpa1 = 1;
	else
		b->rsn = 1;
	/* tout ce qui suit est facultatif (valeurs par défaut : CCMP, CCMP, 802.1X ; WPA : TKIP) */
	if (o + 4 > len) {
		b->group = b->pairwise = wpa1 ? CIPH_TKIP : CIPH_CCMP;
		b->akm |= AKM_8021X;
		return 0;
	}
	b->group = cipher_bit(ie + o, oui);
	o += 4;
	if (o + 2 > len)
		return 0;
	u32 n = le16(ie + o);
	o += 2;
	if (o + 4 * n > len)
		return -2;
	for (u32 i = 0; i < n; i++)
		b->pairwise |= cipher_bit(ie + o + 4 * i, oui);
	o += 4 * n;
	if (o + 2 > len)
		return 0;
	n = le16(ie + o);
	o += 2;
	if (o + 4 * n > len)
		return -3;
	for (u32 i = 0; i < n; i++)
		b->akm |= akm_bit(ie + o + 4 * i, oui, wpa1);
	o += 4 * n;
	if (!wpa1 && o + 2 <= len) {
		u16 caps = le16(ie + o);
		b->mfpr = (caps >> 6) & 1;
		b->mfpc = (caps >> 7) & 1;
	}
	return 0;
}

int parse_beacon(const u8 *hdr, const u8 *body, u32 blen, int rx_channel, int signal, struct bss *b)
{
	u16 fc = le16(hdr);
	u32 type = (fc >> 2) & 3, sub = (fc >> 4) & 15;
	if (type != 0 || (sub != 8 && sub != 5) || blen < 12)
		return 0;
	memset(b, 0, sizeof *b);
	b->probe_resp = sub == 5;
	memcpy(b->bssid, hdr + 16, 6);
	b->beacon_int = le16(body + 8);
	b->capab = le16(body + 10);
	b->channel = (u8)rx_channel;
	b->signal = signal;
	int have_ds = 0;
	u32 o = 12;
	while (o + 2 <= blen) {
		u8 id = body[o], l = body[o + 1];
		const u8 *v = body + o + 2;
		if (o + 2 + l > blen)
			break; /* élément coupé : on garde ce qui précède */
		switch (id) {
		case E_SSID:
			if (l <= 32) {
				memcpy(b->ssid, v, l);
				b->ssid[l] = 0;
				b->ssid_len = l;
			}
			break;
		case E_DS:
			if (l >= 1) {
				b->channel = v[0];
				have_ds = 1;
			}
			break;
		case E_HT_OP:
			if (l >= 1 && !have_ds)
				b->channel = v[0];
			break;
		case E_HT_CAP:
			b->ht = 1;
			break;
		case E_VHT_CAP:
			b->vht = 1;
			break;
		case E_EXT:
			if (l >= 1 && v[0] == EXT_HE_CAP)
				b->he = 1;
			break;
		case E_RSN:
			parse_rsn(v, l, 0, b);
			break;
		case E_VENDOR:
			if (l >= 4 && !memcmp(v, oui_wpa, 3) && v[3] == 1)
				parse_rsn(v + 4, l - 4, 1, b);
			break;
		}
		o += 2u + l;
	}
	if (b->rsn) {
		if (b->akm & AKM_8021X)
			b->sec = SEC_ENT;
		else if ((b->akm & (AKM_PSK | AKM_PSK256)))
			b->sec = SEC_WPA2; /* (WPA2, ou transition WPA2/WPA3 si SAE aussi) */
		else if (b->akm & AKM_SAE)
			b->sec = SEC_WPA3;
		else if (b->akm & AKM_OWE)
			b->sec = SEC_OPEN; /* (« Enhanced Open » : chiffré sans clé) */
		else
			b->sec = SEC_WPA2;
	} else if (b->wpa1)
		b->sec = (b->akm & AKM_8021X) ? SEC_ENT : SEC_WPA;
	else if (b->capab & 0x0010)
		b->sec = SEC_WEP; /* « Privacy » sans RSN ni WPA */
	else
		b->sec = SEC_OPEN;
	return 1;
}

const char *sec_name(const struct bss *b)
{
	switch (b->sec) {
	case SEC_OPEN:
		return (b->akm & AKM_OWE) ? "OWE" : "ouvert";
	case SEC_WEP:
		return "WEP";
	case SEC_WPA:
		return "WPA";
	case SEC_WPA2:
		if (b->akm & AKM_SAE)
			return "WPA2/WPA3";
		return b->wpa1 ? "WPA/WPA2" : "WPA2";
	case SEC_WPA3:
		return "WPA3";
	case SEC_ENT:
		return "entreprise";
	}
	return "?";
}
