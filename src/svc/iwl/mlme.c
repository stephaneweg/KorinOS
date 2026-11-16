/* mlme.c : ce que le SoftMAC laisse à l'hôte (docs/etude/WIFI-PC.md 6) : les trames d'authentification
 * et d'association (802.11-2020 9.3.3.6, 9.3.3.12), les trames de données et leur passage à Ethernet
 * (LLC/SNAP, RFC 1042), et le supplicant WPA2-personnel : EAPOL-Key à 4 voies et renouvellement de la
 * clé de groupe (12.7.6, 12.7.7), descripteur de version 2 (HMAC-SHA1-128, AES Key Wrap). Écrit
 * d'après la norme ; essayé contre un point d'accès simulé (tools/pc/iwltest). */
#include "iwlfw.h"

/* ---- en-têtes */
static void hdr80211(u8 *f, u16 fc, const u8 *a1, const u8 *a2, const u8 *a3, u16 seq)
{
	put16(f, fc);
	put16(f + 2, 0); /* durée : la puce la met */
	memcpy(f + 4, a1, 6);
	memcpy(f + 10, a2, 6);
	memcpy(f + 16, a3, 6);
	put16(f + 22, (u16)(seq << 4));
}

u32 build_auth(u8 *f, const u8 bssid[6], const u8 sa[6], u16 seq)
{
	hdr80211(f, FC_AUTH, bssid, sa, bssid, seq);
	put16(f + 24, 0); /* système ouvert */
	put16(f + 26, 1); /* n° de transaction */
	put16(f + 28, 0); /* statut */
	return 30;
}

u32 build_rsn_ie(u8 *o, const struct bss *b)
{
	if (!b->rsn || !(b->pairwise & CIPH_CCMP) || !(b->akm & AKM_PSK) || b->mfpr)
		return 0;
	u8 g = b->group == CIPH_TKIP ? 2 : b->group == CIPH_CCMP ? 4 : 0;
	if (!g)
		return 0;
	static const u8 t[] = {48, 20, 1, 0, 0x00, 0x0f, 0xac, 0, 1, 0, 0x00, 0x0f, 0xac, 4,
			       1, 0, 0x00, 0x0f, 0xac, 2, 0, 0};
	memcpy(o, t, sizeof t);
	o[7] = g;
	return sizeof t;
}

u32 build_assoc_req(u8 *f, const struct bss *b, const u8 sa[6], u16 seq, const u8 *rsn, u32 rsn_len)
{
	return build_assoc_req_ex(f, b, sa, seq, rsn, rsn_len, 0, 0);
}

u32 build_assoc_req_ex(u8 *f, const struct bss *b, const u8 sa[6], u16 seq, const u8 *rsn, u32 rsn_len,
		       const u8 *extra, u32 extra_len)
{
	hdr80211(f, FC_ASSOC_REQ, b->bssid, sa, b->bssid, seq);
	u16 cap = 0x0001; /* ESS */
	if (rsn_len || (b->capab & 0x0010))
		cap |= 0x0010; /* (Privacy, comme le point d'accès) */
	if (b->channel <= 14)
		cap |= (b->capab & 0x0020) | (b->capab & 0x0400); /* préambule court, intervalle court */
	put16(f + 24, cap);
	put16(f + 26, 10); /* intervalle d'écoute (balises) */
	u32 o = 28;
	f[o++] = 0;
	f[o++] = b->ssid_len;
	memcpy(f + o, b->ssid, b->ssid_len);
	o += b->ssid_len;
	/* débits : ceux du point d'accès (bits « de base » gardés), 8 dans l'élément 1, le reste dans 50 */
	static const u8 def24[] = {0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c};
	static const u8 def5[] = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c};
	const u8 *r = b->rates;
	u32 n = b->n_rates;
	if (!n) {
		r = b->channel <= 14 ? def24 : def5;
		n = b->channel <= 14 ? sizeof def24 : sizeof def5;
	}
	u32 n1 = n > 8 ? 8 : n;
	f[o++] = 1;
	f[o++] = (u8)n1;
	memcpy(f + o, r, n1);
	o += n1;
	if (n > n1) {
		f[o++] = 50;
		f[o++] = (u8)(n - n1);
		memcpy(f + o, r + n1, n - n1);
		o += n - n1;
	}
	if (rsn && rsn_len) {
		memcpy(f + o, rsn, rsn_len);
		o += rsn_len;
	}
	if (extra && extra_len) { /* HT, VHT, WMM (ht.c), dans l'ordre de 9.3.3.6 */
		memcpy(f + o, extra, extra_len);
		o += extra_len;
	}
	return o;
}

int parse_auth_resp(const u8 *f, u32 len, u16 *status)
{
	if (len < 30 || (le16(f) & 0x00fc) != FC_AUTH)
		return -1;
	if (le16(f + 24) != 0 || le16(f + 26) != 2)
		return -2;
	*status = le16(f + 28);
	return 0;
}

int parse_assoc_resp(const u8 *f, u32 len, u16 *status, u16 *aid)
{
	if (len < 30 || (le16(f) & 0x00fc) != FC_ASSOC_RESP)
		return -1;
	*status = le16(f + 26);
	*aid = le16(f + 28) & 0x3fff;
	return 0;
}

/* ---- données */
static const u8 llc_snap[6] = {0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00};

u32 eth_to_80211(u8 *f, const u8 *eth, u32 len, const u8 bssid[6], const u8 sa[6], u16 seq, int prot)
{
	if (len < 14 || len - 14 + 32 > 2400)
		return 0;
	hdr80211(f, (u16)(FC_DATA | FC_TODS | (prot ? FC_PROTECTED : 0)), bssid, sa, eth, seq);
	memcpy(f + 24, llc_snap, 6);
	memcpy(f + 30, eth + 12, 2); /* type */
	memcpy(f + 32, eth + 14, len - 14);
	return 32 + len - 14;
}

/* la même en « QoS data » (sous-type 8) : en-tête de 26 octets, contrôle QoS (TID, accusé normal) */
u32 eth_to_80211_qos(u8 *f, const u8 *eth, u32 len, const u8 bssid[6], const u8 sa[6], u16 seq, int prot, u8 tid)
{
	if (len < 14 || len - 14 + 34 > 2400)
		return 0;
	hdr80211(f, (u16)(FC_QOS_DATA | FC_TODS | (prot ? FC_PROTECTED : 0)), bssid, sa, eth, seq);
	put16(f + 24, tid & 15);
	memcpy(f + 26, llc_snap, 6);
	memcpy(f + 32, eth + 12, 2);
	memcpy(f + 34, eth + 14, len - 14);
	return 34 + len - 14;
}

u32 wlan_to_eth(u8 *out, const u8 *f, u32 len, u32 hdrlen, u32 crypt)
{
	u16 fc = le16(f);
	if (((fc >> 2) & 3) != 2 || (fc & 0x0040) || len < hdrlen + crypt + 8)
		return 0; /* (pas des données, ou « null data ») */
	const u8 *da = f + 4, *sa;
	if ((fc & (FC_TODS | FC_FROMDS)) == FC_FROMDS)
		sa = f + 16;
	else if (!(fc & (FC_TODS | FC_FROMDS)))
		sa = f + 10;
	else
		return 0; /* (quatre adresses, ou vers le DS) */
	const u8 *p = f + hdrlen + crypt;
	u32 n = len - hdrlen - crypt;
	if (memcmp(p, llc_snap, 6) || n - 8 + 14 > 1544)
		return 0;
	memcpy(out, da, 6);
	memcpy(out + 6, sa, 6);
	memcpy(out + 12, p + 6, 2);
	memcpy(out + 14, p + 8, n - 8);
	return 14 + n - 8;
}

/* ---- EAPOL-Key (descripteur RSN : type 2) :
 *  0 version, 1 type (3 : Key), 2 longueur (gros-boutiste) ; 4 descripteur, 5 key_info, 7 key_length,
 *  9 replay (8), 17 nonce (32), 49 IV (16), 65 RSC (8), 73 réservé (8), 81 MIC (16), 97 longueur des
 *  données, 99 données */
#define KI_VER_MASK 0x0007
#define KI_PAIRWISE 0x0008
#define KI_INSTALL 0x0040
#define KI_ACK 0x0080
#define KI_MIC 0x0100
#define KI_SECURE 0x0200
#define KI_ERROR 0x0400
#define KI_REQUEST 0x0800
#define KI_ENC 0x1000
#define EK_HDR 99

static u16 be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static void pbe16(u8 *p, u32 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }

void sup_init(struct supplicant *s, const u8 pmk[32], const u8 aa[6], const u8 spa[6], const u8 *rsn_ie,
	      u32 rsn_len, u8 group_cipher, const u8 snonce[32])
{
	memset(s, 0, sizeof *s);
	memcpy(s->pmk, pmk, 32);
	memcpy(s->aa, aa, 6);
	memcpy(s->spa, spa, 6);
	if (rsn_len > sizeof s->rsn_ie)
		rsn_len = sizeof s->rsn_ie;
	memcpy(s->rsn_ie, rsn_ie, rsn_len);
	s->rsn_len = rsn_len;
	s->group_cipher = group_cipher;
	memcpy(s->snonce, snonce, 32);
	s->state = SUP_WAIT_M1;
}

/* une réponse : version de l'EAPOL du point d'accès, key_info, replay, nonce, données -> longueur */
static u32 ek_build(struct supplicant *s, u8 *o, u8 ver, u16 ki, const u8 *nonce, const u8 *data, u32 dlen)
{
	memset(o, 0, EK_HDR + dlen);
	o[0] = ver;
	o[1] = 3;
	pbe16(o + 2, 95 + dlen);
	o[4] = 2;
	pbe16(o + 5, ki);
	memcpy(o + 9, s->replay, 8);
	if (nonce)
		memcpy(o + 17, nonce, 32);
	pbe16(o + 97, dlen);
	if (dlen)
		memcpy(o + EK_HDR, data, dlen);
	u8 mic[16];
	eapol_mic(s->ptk, o, EK_HDR + dlen, mic);
	memcpy(o + 81, mic, 16);
	return EK_HDR + dlen;
}

static int mic_ok(const struct supplicant *s, const u8 *e, u32 len)
{
	static u8 tmp[2048];
	if (len > sizeof tmp)
		return 0;
	memcpy(tmp, e, len);
	memset(tmp + 81, 0, 16);
	u8 mic[16];
	eapol_mic(s->ptk, tmp, len, mic);
	return !memcmp(mic, e + 81, 16);
}

/* les KDE et éléments des données déchiffrées : la GTK (00-0F-AC:1) */
static int find_gtk(struct supplicant *s, const u8 *d, u32 n)
{
	for (u32 o = 0; o + 2 <= n;) {
		u8 id = d[o], l = d[o + 1];
		if (id == 0xdd && l == 0)
			break; /* bourrage */
		if (o + 2 + l > n)
			break;
		if (id == 0xdd && l >= 6 && d[o + 2] == 0x00 && d[o + 3] == 0x0f && d[o + 4] == 0xac && d[o + 5] == 1) {
			u32 gl = l - 6;
			if (gl != 16 && gl != 32)
				return -1;
			s->gtk_idx = d[o + 6] & 3;
			memcpy(s->gtk, d + o + 8, gl);
			s->gtk_len = gl;
			return 0;
		}
		o += 2u + l;
	}
	return -1;
}

static int replay_newer(const struct supplicant *s, const u8 *r)
{
	if (!s->have_replay)
		return 1;
	return memcmp(r, s->replay, 8) > 0;
}

int sup_rx(struct supplicant *s, const u8 *e, u32 len, u8 *out, u32 *out_len)
{
	*out_len = 0;
	if (len < EK_HDR || e[1] != 3 || e[4] != 2)
		return SUP_EV_NONE;
	u32 blen = be16(e + 2) + 4u;
	if (blen > len)
		return SUP_EV_BAD;
	len = blen;
	u16 ki = be16(e + 5);
	u32 dlen = be16(e + 97);
	if (EK_HDR + dlen > len || (ki & KI_VER_MASK) != 2)
		return SUP_EV_BAD;
	const u8 *data = e + EK_HDR;
	if (ki & KI_PAIRWISE) {
		if ((ki & (KI_ACK | KI_MIC)) == KI_ACK) { /* message 1 */
			if (!replay_newer(s, e + 9) && s->state != SUP_WAIT_M1)
				return SUP_EV_BAD;
			memcpy(s->anonce, e + 17, 32);
			memcpy(s->replay, e + 9, 8);
			s->have_replay = 1;
			wpa_ptk(s->pmk, s->aa, s->spa, s->anonce, s->snonce, s->ptk);
			*out_len = ek_build(s, out, e[0], 2 | KI_PAIRWISE | KI_MIC, s->snonce, s->rsn_ie, s->rsn_len);
			s->state = SUP_WAIT_M3;
			return SUP_EV_SEND;
		}
		if ((ki & (KI_ACK | KI_MIC | KI_INSTALL)) == (KI_ACK | KI_MIC | KI_INSTALL)) { /* message 3 */
			if (s->state < SUP_WAIT_M3 || memcmp(e + 17, s->anonce, 32) || !replay_newer(s, e + 9))
				return SUP_EV_BAD;
			if (!mic_ok(s, e, len)) {
				s->bad_mic++;
				return SUP_EV_BAD;
			}
			static u8 plain[512];
			if (!(ki & KI_ENC) || dlen < 24 || dlen % 8 || dlen - 8 > sizeof plain ||
			    aes_unwrap(s->ptk + 16, data, dlen, plain))
				return SUP_EV_BAD;
			if (find_gtk(s, plain, dlen - 8))
				return SUP_EV_BAD;
			memcpy(s->gtk_rsc, e + 65, 8);
			memcpy(s->replay, e + 9, 8);
			*out_len = ek_build(s, out, e[0], 2 | KI_PAIRWISE | KI_MIC | KI_SECURE, 0, 0, 0);
			s->state = SUP_DONE;
			return SUP_EV_PTK;
		}
		return SUP_EV_NONE;
	}
	/* groupe : renouvellement de la GTK (après les 4 voies) */
	if (s->state != SUP_DONE || (ki & (KI_ACK | KI_MIC | KI_ENC)) != (KI_ACK | KI_MIC | KI_ENC))
		return SUP_EV_NONE;
	if (!replay_newer(s, e + 9))
		return SUP_EV_BAD;
	if (!mic_ok(s, e, len)) {
		s->bad_mic++;
		return SUP_EV_BAD;
	}
	static u8 plain[512];
	if (dlen < 24 || dlen % 8 || dlen - 8 > sizeof plain || aes_unwrap(s->ptk + 16, data, dlen, plain) ||
	    find_gtk(s, plain, dlen - 8))
		return SUP_EV_BAD;
	memcpy(s->gtk_rsc, e + 65, 8);
	memcpy(s->replay, e + 9, 8);
	*out_len = ek_build(s, out, e[0], 2 | KI_MIC | KI_SECURE, 0, 0, 0);
	return SUP_EV_GTK;
}
