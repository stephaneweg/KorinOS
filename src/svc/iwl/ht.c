/* ht.c : au-delà de 802.11a/g (docs/etude/PC-PILOTES.md « Wi-Fi AX201, débit ») : ce que la station annonce
 * et choisit d'après le point d'accès (HT 802.11-2020 9.4.2.55-56, VHT 9.4.2.157-158, WMM), la largeur du
 * canal, la commande TLC (le micrologiciel choisit le débit), les trames d'action « Block Ack » (9.6.4) et
 * le réordonnancement des trames reçues en session block ack. Comme iwlwifi (mvm) avec mac80211 pour les
 * 22000 : rs-fw.c (TLC), sta.c et rxmq.c (BA, réordonnancement), iwl-nvm-utils.c et iwl-nvm-parse.c (ce que
 * la puce annonce) ; tout est réécrit ici, en octets. Sans matériel : tools/pc/iwltest l'essaie. */
#include "iwlfw.h"
#ifdef IWL_HOST
#include <stdio.h>
#endif

#define E_HT_CAP 45
#define E_VHT_CAP 191
#define E_VENDOR 221

/* ---- le lien */

/* un débit a/b/g (500 kb/s) -> indice TLC (0-3 CCK, 4-11 OFDM), -1 inconnu */
static int legacy_index(u8 r)
{
	static const u8 t[12] = {2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108};
	for (int i = 0; i < 12; i++)
		if (t[i] == (r & 0x7f))
			return i;
	return -1;
}

/* le bloc de 80 MHz qui contient le canal primaire (5 GHz) -> canal central, 0 aucun */
static u8 center80(u8 primary)
{
	static const u8 starts[7] = {36, 52, 100, 116, 132, 149, 165};
	for (int i = 0; i < 7; i++)
		if (primary >= starts[i] && primary <= starts[i] + 12 && ((primary - starts[i]) & 3) == 0)
			return (u8)(starts[i] + 6);
	return 0;
}

/* position du primaire par rapport au centre (contexte PHY, comme iwl_mvm_get_ctrl_pos) */
static u8 ctrl_pos(u8 primary, u8 center)
{
	int offs = 5 * ((int)primary - (int)center); /* MHz */
	if (!offs)
		return 0;
	int a = offs < 0 ? -offs : offs;
	u8 r = (u8)((a - 10) / 20);
	r = (u8)((r & 3) | ((r & 4) << 1));
	if (offs > 0)
		r |= 4;
	return r;
}

int link_choose(const struct bss *b, int want_mode, int want_width, u32 allowed, struct link_conf *lc)
{
	memset(lc, 0, sizeof *lc);
	int band5 = b->channel > 14;
	lc->primary = lc->center = b->channel;
	lc->nss = 1;
	lc->smps = 3;
	if (b->erp & 2)
		lc->prot |= 1;
	for (int i = 0; i < b->n_rates; i++) {
		int k = legacy_index(b->rates[i]);
		if (k >= 0 && (!band5 || k >= 4))
			lc->non_ht |= (u16)(1u << k);
	}
	if (!lc->non_ht)
		lc->non_ht = band5 ? 0xff0 : 0xfff;
	/* HT et VHT seulement avec WMM (comme mac80211 : une station HT est une station QoS) */
	lc->wmm = want_mode > LM_AG && b->wmm;
	int mode = LM_AG;
	if (lc->wmm && want_mode >= LM_HT && b->ht && b->ht_mcs[0])
		mode = LM_HT;
	if (mode == LM_HT && want_mode >= LM_VHT && band5 && b->vht && (b->vht_rx_mcs & 3) != 3)
		mode = LM_VHT;
	lc->mode = mode;
	if (mode == LM_AG)
		return 0;
	lc->smps = (b->ht_cap >> 2) & 3;
	int nss;
	if (mode == LM_VHT)
		nss = ((b->vht_rx_mcs >> 2) & 3) != 3 ? 2 : 1;
	else
		nss = b->ht_mcs[1] ? 2 : 1;
	if (lc->smps == 0) /* (économie MIMO statique : un seul flux vers le point d'accès) */
		nss = 1;
	lc->nss = nss;
	/* largeur : 40 MHz si le point d'accès le dit (élément HT Operation) et l'accepte (capacité 20/40),
	 * 80 MHz si son élément VHT Operation le dit ; 20 MHz en 2,4 GHz */
	int w = LW_20;
	u8 sec = b->ht_op_sec & 3;
	if (band5 && b->ht_op && (b->ht_op_sec & 4) && (sec == 1 || sec == 3) && (b->ht_cap & 0x0002))
		w = LW_40;
	u8 c80 = center80(b->channel);
	if (w == LW_40 && mode == LM_VHT && b->vht_op && b->vht_op_width >= 1 && c80)
		w = LW_80;
	if (w > want_width)
		w = want_width;
	while (w > LW_20 && !(allowed & (1u << w)))
		w--;
	lc->width = w;
	if (w == LW_40)
		lc->center = (u8)(sec == 1 ? b->channel + 2 : b->channel - 2);
	else if (w == LW_80)
		lc->center = c80;
	lc->ctrl_pos = ctrl_pos(lc->primary, lc->center);
	/* agrégation : taille et densité du point d'accès */
	lc->agg_size = b->ht_ampdu & 3;
	lc->dens = (b->ht_ampdu >> 2) & 7;
	if (mode == LM_VHT)
		lc->agg_size = (b->vht_cap >> 23) & 7;
	if (b->ht_cap & 0x0020)
		lc->sgi |= 1;
	if ((b->ht_cap & 0x0040) && w >= LW_40)
		lc->sgi |= 2;
	if (mode == LM_VHT && (b->vht_cap & 0x0020) && w >= LW_80)
		lc->sgi |= 4;
	lc->ldpc = (b->ht_cap & 0x0001) || (mode == LM_VHT && (b->vht_cap & 0x0010));
	lc->stbc = mode == LM_VHT ? ((b->vht_cap >> 8) & 7) != 0 : ((b->ht_cap >> 8) & 3) != 0;
	if (mode == LM_HT) {
		lc->ht_rates[0] = b->ht_mcs[0];
		lc->ht_rates[1] = nss >= 2 ? b->ht_mcs[1] : 0;
	} else {
		for (int i = 0; i < nss; i++) {
			u32 m = (b->vht_rx_mcs >> (2 * i)) & 3; /* 0 : 0-7, 1 : 0-8, 2 : 0-9 ; les nôtres : 0-9 */
			if (m == 3)
				continue;
			u32 hi = 7 + m;
			u16 supp = (u16)((1u << (hi + 1)) - 1);
			if (w == LW_20)
				supp &= (u16)~(1u << 9); /* (MCS 9 n'existe pas en 20 MHz pour 1 et 2 flux) */
			lc->ht_rates[i] = supp;
		}
	}
	return 0;
}

const char *link_name(const struct link_conf *lc)
{
	static const char *n[3][3] = {{"a/g", "a/g", "a/g"},
				      {"HT 20 MHz", "HT 40 MHz", "HT 40 MHz"},
				      {"VHT 20 MHz", "VHT 40 MHz", "VHT 80 MHz"}};
	int m = lc->mode < 0 || lc->mode > 2 ? 0 : lc->mode, w = lc->width < 0 || lc->width > 2 ? 0 : lc->width;
	return n[m][w];
}

u32 ht_assoc_ies(u8 *o, const struct bss *b, const struct link_conf *lc)
{
	u32 n = 0;
	if (lc->mode >= LM_HT) {
		u16 cap = OUR_HT_CAP;
		if (lc->width < LW_40)
			cap &= (u16)~(0x0002 | 0x0040); /* (comme mac80211 : 20/40 et SGI 40 seulement si 40 MHz est permis) */
		o[n++] = E_HT_CAP;
		o[n++] = 26;
		put16(o + n, cap);
		o[n + 2] = OUR_HT_AMPDU;
		memset(o + n + 3, 0, 23);
		o[n + 3] = 0xff; /* MCS 0-7 */
		o[n + 4] = 0xff; /* MCS 8-15 (deux antennes) */
		put16(o + n + 13, lc->width >= LW_40 ? 300 : 144); /* débit reçu le plus haut (Mb/s) */
		o[n + 15] = 0x01; /* jeu MCS d'émission défini, le même */
		n += 26;
	}
	if (lc->mode >= LM_VHT) {
		o[n++] = E_VHT_CAP;
		o[n++] = 12;
		put32(o + n, OUR_VHT_CAP);
		put16(o + n + 4, OUR_VHT_MCS);
		put16(o + n + 6, 0);
		put16(o + n + 8, OUR_VHT_MCS);
		put16(o + n + 10, 0x2000); /* EXT_NSS_BW capable */
		n += 12;
	}
	if (lc->wmm) { /* élément d'information WMM : version 1, qos_info 0 (pas d'U-APSD) */
		static const u8 wmm[9] = {E_VENDOR, 7, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01, 0x00};
		memcpy(o + n, wmm, sizeof wmm);
		n += sizeof wmm;
	}
	(void)b;
	return n;
}

/* débits HT/VHT (Mb/s × 10, un flux, intervalle de garde long), MCS 0 à 9, en 20, 40 et 80 MHz */
static const u16 vht_rate[3][10] = {
	{65, 130, 195, 260, 390, 520, 585, 650, 780, 867},
	{135, 270, 405, 540, 810, 1080, 1215, 1350, 1620, 1800},
	{293, 585, 878, 1170, 1755, 2340, 2633, 2925, 3510, 3900},
};

static u32 ht_mbps10(u32 mcs, u32 nss, u32 bw, int sgi)
{
	if (mcs > 9 || bw > 2 || nss < 1)
		return 0;
	u32 r = vht_rate[bw][mcs] * nss;
	return sgi ? r * 10 / 9 : r;
}

u32 link_max_mbps(const struct link_conf *lc)
{
	if (lc->mode == LM_AG)
		return 54;
	u32 hi = 0;
	for (u32 i = 0; i < 10; i++)
		if (lc->ht_rates[0] & (1u << i))
			hi = i;
	if (lc->mode == LM_HT && hi > 7)
		hi = 7;
	return ht_mbps10(hi, (u32)lc->nss, (u32)lc->width, (lc->sgi >> lc->width) & 1) / 10;
}

u32 build_tlc_cmd(u8 *c, const struct link_conf *lc, u8 sta_id, u8 chains, int width)
{
	memset(c, 0, TLC_CFG_LEN);
	c[0] = sta_id;
	c[4] = (u8)(width < lc->width ? width : lc->width); /* largeur la plus grande (IWL_TLC_MNG_CH_WIDTH_*) */
	c[5] = (u8)(lc->mode == LM_VHT ? 2 : lc->mode == LM_HT ? 1 : 0); /* NON_HT, HT, VHT */
	c[6] = chains & 3;                                               /* chaînes d'émission A, B */
	c[7] = lc->sgi;                                                  /* SGI par largeur */
	u16 fl = 0;
	if (lc->stbc && (chains & 3) == 3)
		fl |= 1; /* STBC */
	if (lc->ldpc)
		fl |= 2; /* LDPC */
	put16(c + 8, fl);
	put16(c + 10, lc->non_ht);
	/* ht_rates[flux][largeur jusqu'à 80, 160, 320] */
	if (lc->mode >= LM_HT) {
		put16(c + 12, lc->ht_rates[0]);
		put16(c + 18, lc->ht_rates[1]);
	}
	put16(c + 24, 0); /* max_mpdu_len : pas d'A-MSDU à l'émission */
	put16(c + 26, 0); /* max_tx_op */
	return TLC_CFG_LEN;
}

u32 rate_mbps10(u32 r)
{
	static const u16 cck[4] = {10, 20, 55, 110};
	static const u16 ofdm[8] = {60, 90, 120, 180, 240, 360, 480, 540};
	u32 type = (r >> 8) & 7, mcs = r & 0xf, nss = ((r >> 4) & 1) + 1, bw = (r >> 11) & 7;
	int sgi = (r >> 20) & 1;
	switch (type) {
	case 0:
		return cck[r & 3];
	case 1:
		return ofdm[r & 7];
	case 2:
		return ht_mbps10(mcs & 7, nss, bw, sgi);
	case 3:
		return ht_mbps10(mcs, nss, bw, sgi);
	}
	return 0;
}

void rate_name(u32 r, char *out, u32 max)
{
	static const char *ty[8] = {"CCK", "OFDM", "HT", "VHT", "HE", "HE", "HE", "EHT"};
	u32 type = (r >> 8) & 7, mcs = r & 0xf, nss = ((r >> 4) & 1) + 1, bw = (r >> 11) & 7;
	u32 m = rate_mbps10(r);
	static const u16 bwm[5] = {20, 40, 80, 160, 320};
	if (type <= 1)
		snprintf(out, max, "%u.%u Mb/s (%s)", m / 10, m % 10, ty[type]);
	else
		snprintf(out, max, "%u.%u Mb/s (%s MCS %u, %ux%u, %u MHz%s)", m / 10, m % 10, ty[type],
			 type == 2 ? mcs & 7 : mcs, nss, nss, bw < 5 ? bwm[bw] : 0, ((r >> 20) & 1) ? ", SGI" : "");
}

/* ---- block ack */

static void hdr(u8 *f, u16 fc, const u8 *a1, const u8 *a2, const u8 *a3, u16 seq)
{
	put16(f, fc);
	put16(f + 2, 0);
	memcpy(f + 4, a1, 6);
	memcpy(f + 10, a2, 6);
	memcpy(f + 16, a3, 6);
	put16(f + 22, (u16)(seq << 4));
}

int parse_ba_action(const u8 *body, u32 len, struct addba *a, u16 *delba_tid)
{
	if (len < 2 || body[0] != 3)
		return -1;
	memset(a, 0, sizeof *a);
	if (body[1] == BA_ADDBA_REQ) {
		if (len < 9)
			return -1;
		u16 p = le16(body + 3);
		a->dialog = body[2];
		a->amsdu = p & 1;
		a->policy = (p >> 1) & 1;
		a->tid = (p >> 2) & 15;
		a->buf_size = p >> 6;
		a->timeout = le16(body + 5);
		a->ssn = le16(body + 7) >> 4;
		return BA_ADDBA_REQ;
	}
	if (body[1] == BA_DELBA) {
		if (len < 6)
			return -1;
		u16 p = le16(body + 2);
		*delba_tid = (u16)(((p >> 12) & 15) | (((p >> 11) & 1) << 8)); /* TID, et bit 8 : de l'initiateur */
		return BA_DELBA;
	}
	if (body[1] == BA_ADDBA_RESP)
		return BA_ADDBA_RESP;
	return -1;
}

u32 build_addba_resp(u8 *f, const u8 bssid[6], const u8 sa[6], u16 seq, const struct addba *a, u16 win, u16 status)
{
	hdr(f, FC_ACTION, bssid, sa, bssid, seq);
	f[24] = 3;
	f[25] = BA_ADDBA_RESP;
	f[26] = a->dialog;
	put16(f + 27, status);
	/* pas d'A-MSDU dans les A-MPDU (bit 0 à 0), accusé immédiat, TID, fenêtre */
	put16(f + 29, (u16)((1u << 1) | ((u32)(a->tid & 15) << 2) | ((u32)win << 6)));
	put16(f + 31, a->timeout);
	return 33;
}

u32 build_delba(u8 *f, const u8 bssid[6], const u8 sa[6], u16 seq, u8 tid, u16 reason)
{
	hdr(f, FC_ACTION, bssid, sa, bssid, seq);
	f[24] = 3;
	f[25] = BA_DELBA;
	put16(f + 26, (u16)((u32)(tid & 15) << 12)); /* (nous sommes le destinataire : initiateur 0) */
	put16(f + 28, reason);
	return 30;
}

/* ---- réordonnancement */

static u8 pool[RO_POOL][RO_FRAME];
static u16 plen[RO_POOL];
static short pnext[RO_POOL];
static short pfree = -1;
static int pready;

void ro_init(void)
{
	pfree = -1;
	for (int i = RO_POOL - 1; i >= 0; i--) {
		pnext[i] = pfree;
		pfree = (short)i;
	}
	pready = 1;
}

static int sn_less(u16 a, u16 b)
{
	return ((a - b) & 0xfff) > 0x800;
}

static u16 sn_inc(u16 a)
{
	return (u16)((a + 1) & 0xfff);
}

static u16 sn_add(u16 a, u16 b)
{
	return (u16)((a + b) & 0xfff);
}

void ro_start(struct ro_buf *r, u8 tid, u8 baid, u16 ssn, u16 size)
{
	if (!pready)
		ro_init();
	memset(r, 0, sizeof *r);
	r->on = 1;
	r->tid = tid;
	r->baid = baid;
	r->size = (u16)(size && size < RO_WIN ? size : RO_WIN);
	r->head = ssn & 0xfff;
	for (int i = 0; i < RO_WIN; i++)
		r->first[i] = r->last[i] = -1;
}

void ro_release(struct ro_buf *r, u16 nssn, ro_deliver_fn deliver)
{
	if (!r->on)
		return;
	u16 sn = r->head;
	nssn &= 0xfff;
	while (r->stored && sn_less(sn, nssn)) {
		int i = sn % r->size;
		sn = sn_inc(sn);
		short e = r->first[i];
		while (e >= 0) {
			short nx = pnext[e];
			deliver(pool[e], plen[e]);
			pnext[e] = pfree;
			pfree = e;
			r->stored--;
			e = nx;
		}
		r->first[i] = r->last[i] = -1;
	}
	r->head = nssn;
}

void ro_stop(struct ro_buf *r, ro_deliver_fn deliver)
{
	ro_release(r, sn_add(r->head, r->size), deliver);
	r->on = 0;
}

int ro_rx(struct ro_buf *r, u32 reorder, int dup, int amsdu, int last_sub, const u8 *eth, u32 len,
	  ro_deliver_fn deliver)
{
	u16 nssn = reorder & 0xfff, sn = (reorder >> 12) & 0xfff;
	int old = (reorder >> 31) & 1;
	int ends = !amsdu || last_sub; /* (le NSSN d'une sous-trame d'A-MSDU qui n'est pas la dernière n'est pas sûr) */
	if (!r->valid) {
		if (old)
			return 0;
		r->valid = 1;
	}
	if (dup || old) {
		if (old)
			r->n_old++;
		else
			r->n_drop++;
		return 1;
	}
	if (!r->stored && sn_less(sn, nssn)) { /* rien de retenu et le NSSN la laisse passer */
		if (ends)
			r->head = nssn;
		return 0;
	}
	if (!r->stored && sn == r->head) {
		if (ends)
			r->head = sn_inc(r->head);
		return 0;
	}
	/* hors de la fenêtre (ne devrait pas arriver : la puce garde la fenêtre) : on la fait avancer */
	if (!sn_less(sn, sn_add(r->head, r->size)))
		ro_release(r, sn_add(sn, (u16)(0x1000 - r->size + 1)), deliver);
	if (pfree < 0 || len > RO_FRAME) { /* réserve pleine : tout ce qui est retenu part, puis celle-ci */
		ro_release(r, sn_add(r->head, r->size), deliver);
		r->head = sn_inc(sn);
		return 0;
	}
	short e = pfree;
	pfree = pnext[e];
	memcpy(pool[e], eth, len);
	plen[e] = (u16)len;
	pnext[e] = -1;
	int i = sn % r->size;
	if (r->last[i] >= 0)
		pnext[r->last[i]] = e;
	else
		r->first[i] = e;
	r->last[i] = e;
	r->stored++;
	r->n_held++;
	if (ends)
		ro_release(r, nssn, deliver);
	return 1;
}
