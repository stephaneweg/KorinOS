/* cmds.c : les charges utiles des commandes hôtes et la lecture des réponses du micrologiciel
 * (mode MVM des 22000), à l'octet près, docs/etude/WIFI-PC.md 4 et 5. Les tailles et les décalages
 * sont ceux des versions annoncées par le TLV 48 du micrologiciel 77 (QuZ-a0-hr-b0) ; mvm.c vérifie
 * ces versions avant de s'en servir. */
#include "iwlfw.h"

/* ---- vivant (UCODE_ALIVE_NTFY) : statut 2, drapeaux 2, LMAC (48 o, un ou deux), UMAC (16 o),
 * puis en version 5 et plus l'identité du SKU (12 o), en 6 la région IMR (16 o) */
int parse_alive(const u8 *p, u32 len, struct alive_info *a)
{
	memset(a, 0, sizeof *a);
	int nlmac;
	if (len == 68)
		nlmac = 1, a->version_by_len = 3;
	else if (len == 116)
		nlmac = 2, a->version_by_len = 4;
	else if (len >= 128)
		nlmac = 2, a->version_by_len = len >= 144 ? 6 : 5;
	else
		return -1;
	a->status = le16(p);
	a->flags = le16(p + 2);
	const u8 *l = p + 4;
	a->lmac_major = le32(l);
	a->lmac_minor = le32(l + 4);
	a->lmac_err = le32(l + 20);
	a->lmac_log = le32(l + 24);
	a->scd_base = le32(l + 40);
	const u8 *u = p + 4 + 48 * nlmac;
	a->umac_major = le32(u);
	a->umac_minor = le32(u + 4);
	a->umac_err = le32(u + 8) & ~0xC0000000u; /* (bits du cache des adresses du micrologiciel) */
	if (a->version_by_len >= 5)
		for (int i = 0; i < 3; i++)
			a->sku_id[i] = le32(p + 116 + 4 * i);
	return 0;
}

/* ---- NVM_GET_INFO (réponse version 4, 468 o) */
const u8 nvm_channel_list[NVM_CHANNELS] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120, 124,
	128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177, 181};

int parse_nvm_info(const u8 *p, u32 len, struct nvm_info *n)
{
	memset(n, 0, sizeof *n);
	if (len != NVM_INFO_V4_LEN)
		return -1;
	n->flags = le32(p);
	n->nvm_version = le16(p + 4);
	n->board_type = p[6];
	n->n_hw_addrs = p[7];
	n->mac_sku = le32(p + 8);
	n->tx_chains = le32(p + 12);
	n->rx_chains = le32(p + 16);
	n->lar = le32(p + 20);
	n->n_channels = le32(p + 24);
	for (int i = 0; i < NVM_CHANNELS; i++)
		n->ch_flags[i] = le32(p + 28 + 4 * i);
	return 0;
}

/* ---- MCC_UPDATE (réponse v3, ou v4 si la capacité MCC_UPDATE_11AX) */
int parse_mcc_resp(const u8 *p, u32 len, int v4, struct mcc_info *m)
{
	memset(m, 0, sizeof *m);
	u32 hdr = v4 ? 20 : 16;
	if (len < hdr)
		return -1;
	m->status = le32(p);
	u16 mcc = le16(p + 4);
	m->alpha2[0] = (char)(mcc >> 8);
	m->alpha2[1] = (char)mcc;
	m->source = v4 ? p[12] : p[7];
	m->n_channels = le32(p + (v4 ? 16 : 12));
	if (len != hdr + 4 * m->n_channels)
		return -2;
	return 0;
}

void mac_from_csr(u32 w0, u32 w1, u8 mac[6])
{
	mac[0] = (u8)(w0 >> 24);
	mac[1] = (u8)(w0 >> 16);
	mac[2] = (u8)(w0 >> 8);
	mac[3] = (u8)w0;
	mac[4] = (u8)(w1 >> 8);
	mac[5] = (u8)w1;
}

int mac_valid(const u8 mac[6])
{
	if (mac[0] & 1)
		return 0; /* (groupe) */
	return (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
}

/* ---- SCAN_REQ_UMAC versions 14 à 17 (même disposition) :
 *   0 uid, 4 priorité hors canal ;
 *   8  paramètres généraux (36 o) : drapeaux 16 bits, réservé, MAC de départ, attente active (2 LMAC),
 *      N par défaut de l'attente adaptative (2,4 GHz, 5 GHz, canaux sociaux), drapeaux2, budget 16
 *      bits, temps hors canal (2 × 32), suspension (2 × 32), priorité 32, attente passive (2),
 *      fragments (2) ;
 *   44 canaux (540 o) : drapeaux, nombre, n_aps (2), 67 × (drapeaux 32, canal, bande, répétitions,
 *      intervalle) — en version 17 la bande va dans les bits 31:30 des drapeaux ;
 *   584 périodes (12 o) : 2 × (intervalle 16, répétitions 8, réservé), délai 16, réservé 16 ;
 *   596 sondes (1344 o) : gabarit (en-tête MAC, 3 bandes, partie commune : 5 × (décalage, longueur),
 *      512 o), nombres de SSID courts et de BSSID, réservé, 20 SSID dirigés (34 o), 8 SSID courts,
 *      16 BSSID. Total 1940 octets. */
#define GF_PASS_ALL 0x0002
#define GF_USE_ALL_RX_CHAINS 0x0040
#define GF_ADAPTIVE_DWELL 0x0080
#define GF_FORCE_PASSIVE 0x0800
#define CHF_ENABLE_CHAN_ORDER 0x20
#define PHY_BAND_5 0
#define PHY_BAND_24 1

u32 build_scan_req(u8 *b, const struct scan_params *sp)
{
	memset(b, 0, SCAN_REQ_V14_LEN);
	put32(b + 0, sp->uid);
	put32(b + 4, 6); /* priorité EXT_6 (balayage ordinaire) */
	u8 *g = b + SR_GENERAL;
	u16 gf = GF_PASS_ALL | GF_ADAPTIVE_DWELL;
	if (sp->passive)
		gf |= GF_FORCE_PASSIVE;
	else if (sp->ssid && sp->ssid_len)
		gf |= GF_USE_ALL_RX_CHAINS;
	put16(g + 0, gf);
	g[3] = 0;                /* MAC de départ (aucune n'est associée) */
	g[4] = g[5] = 10;        /* attente active (ms) */
	g[6] = 2;                /* N d'AP par défaut, 2,4 GHz */
	g[7] = 8;                /* 5 GHz */
	g[8] = 10;               /* canaux sociaux */
	g[9] = 0;                /* drapeaux2 */
	put16(g + 10, sp->ssid && sp->ssid_len ? 100 : 300); /* budget de l'attente adaptative */
	if (sp->assoc) {
		put32(g + 12, 120); /* temps hors canal (2 LMAC) */
		put32(g + 16, 120);
		put32(g + 20, 30);  /* suspension */
		put32(g + 24, 30);
	}
	put32(g + 28, 6);        /* priorité EXT_6 */
	g[32] = g[33] = 110;     /* attente passive (ms) */
	u8 *c = b + SR_CHANNEL;
	c[0] = CHF_ENABLE_CHAN_ORDER;
	int n = sp->n_ch > SCAN_MAX_CH ? SCAN_MAX_CH : sp->n_ch;
	c[1] = (u8)n;
	c[2] = 10; /* n_aps « GO friendly » */
	c[3] = 2;  /* canaux sociaux */
	for (int i = 0; i < n; i++) {
		u8 *e = c + 4 + 8 * i;
		u8 band = sp->ch[i] <= 14 ? PHY_BAND_24 : PHY_BAND_5;
		u32 fl = sp->passive ? 0 : 1; /* bit 0 : sonde avec le SSID dirigé n° 0 */
		if (sp->version >= 17)
			fl |= (u32)band << 30;
		else
			e[5] = band;
		put32(e, fl);
		e[4] = sp->ch[i];
		e[6] = 1; /* une fois */
		e[7] = 0;
	}
	u8 *per = b + SR_PERIODIC;
	put16(per + 0, 0);
	per[2] = 1; /* un seul tour */
	u8 *pr = b + SR_PROBE;
	u8 *f = b + SR_PREQ_BUF;
	/* gabarit de la sonde : en-tête 802.11 (24 o) et l'élément SSID (rempli par le micrologiciel) */
	f[0] = 0x40;                 /* gestion, sous-type 4 : requête de sonde */
	memset(f + 4, 0xFF, 6);      /* destination : diffusion */
	memcpy(f + 10, sp->mac, 6);  /* source */
	memset(f + 16, 0xFF, 6);     /* BSSID : générique */
	f[24] = 0;                   /* SSID */
	f[25] = 0;
	put16(pr + 0, 0);
	put16(pr + 2, 26);
	static const u8 r24[] = {1, 8, 0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24, 50, 4, 0x30, 0x48, 0x60, 0x6c};
	static const u8 r5[] = {1, 8, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c};
	u32 pos = 26;
	memcpy(f + pos, r24, sizeof r24);
	put16(pr + 4, pos);
	put16(pr + 6, sizeof r24);
	pos += sizeof r24;
	memcpy(f + pos, r5, sizeof r5);
	put16(pr + 8, pos);
	put16(pr + 10, sizeof r5);
	pos += sizeof r5;
	put16(pr + 12, pos); /* 6 GHz : rien */
	put16(pr + 14, 0);
	put16(pr + 16, pos); /* partie commune : rien */
	put16(pr + 18, 0);
	if (!sp->passive) {
		u8 *ds = b + SR_DIRECT; /* SSID dirigé n° 0 : celui demandé, ou vide (générique) */
		ds[0] = 0;
		int l = sp->ssid ? sp->ssid_len : 0;
		if (l > 32)
			l = 32;
		ds[1] = (u8)l;
		if (l)
			memcpy(ds + 2, sp->ssid, l);
	}
	return SCAN_REQ_V14_LEN;
}

int parse_scan_complete(const u8 *p, u32 len, struct scan_done *d)
{
	memset(d, 0, sizeof *d);
	if (len < 16)
		return -1;
	d->uid = le32(p);
	d->last_schedule = p[4];
	d->last_iter = p[5];
	d->status = p[6];
	d->ebs_status = p[7];
	d->time_from_last_iter = le32(p + 8);
	return 0;
}

/* ---- REPLY_RX_MPDU_CMD : descripteur de 48 o (longueur MPDU 16 bits, drapeaux MAC 1 et 2, …,
 * statut à +12, puis la partie v1 : … énergie A à +32, B à +33, canal à +34), puis la trame */
static u32 hdrlen_of(const u8 *h, u32 len)
{
	if (len < 2)
		return 0;
	u16 fc = le16(h);
	u32 type = (fc >> 2) & 3, sub = (fc >> 4) & 15;
	if (type == 0)
		return 24; /* gestion */
	if (type == 1)
		return (sub == 12 || sub == 13) ? 10 : 16; /* contrôle (CTS, ACK : 10) */
	u32 l = 24;
	if ((fc & 0x0300) == 0x0300)
		l += 6; /* quatre adresses */
	if (sub & 8)
		l += 2; /* QoS */
	if ((sub & 8) && (fc & 0x8000))
		l += 4; /* HT control */
	return l;
}

int parse_rx_mpdu(const u8 *p, u32 len, struct rx_frame *f)
{
	memset(f, 0, sizeof *f);
	if (len < RX_DESC_V1)
		return -1;
	u32 mlen = le16(p);
	if (mlen + RX_DESC_V1 > len)
		return -2;
	f->status = le32(p + 12);
	u8 ea = p[32], eb = p[33];
	int sa = ea ? -(int)ea : -128, sb = eb ? -(int)eb : -128;
	f->signal = sa > sb ? sa : sb;
	f->channel = p[34];
	f->gp2 = le32(p + 36);
	f->frame = p + RX_DESC_V1;
	f->len = mlen;
	f->pad = (p[3] & 0x20) != 0;
	f->hdrlen = hdrlen_of(f->frame, mlen);
	if (f->pad && f->len >= 2)
		f->len -= 2;
	return 0;
}

void build_ctxt_info(u8 *c, u32 hw_rev, u64 rings_pa, u64 tfd_pa, const struct fw_image *fw, const u64 *sec_pa)
{
	memset(c, 0, CTXT_INFO_SIZE);
	put16(c + 0, hw_rev & 0xFFFF);
	put16(c + 2, 0);
	put16(c + 4, CTXT_INFO_SIZE / 4);
	put32(c + 8, 0x0100 | CI_RXQ_LOG2 << 4 | CI_RB_4K << 9);
	put64(c + 24, rings_pa);
	put64(c + 32, rings_pa + 4096);
	put64(c + 40, rings_pa + 8192);
	put64(c + 48, tfd_pa);
	c[56] = 2; /* 32 TFD : log2(32) - 3 */
	for (int i = 0; i < fw->n_umac; i++)
		put64(c + 192 + 8 * i, sec_pa[fw->umac_first + i]);
	for (int i = 0; i < fw->n_lmac; i++)
		put64(c + 704 + 8 * i, sec_pa[i]);
	for (int i = 0; i < fw->n_paging; i++)
		put64(c + 1216 + 8 * i, sec_pa[fw->paging_first + i]);
}
