/* httest.c (inclus par iwltest.c) : au-delà de 802.11a/g (src/svc/iwl/ht.c) : éléments HT, VHT et WMM d'une
 * balise, choix du lien (mode, largeur, canal central, position du primaire comme iwl_mvm_get_ctrl_pos),
 * éléments de la demande d'association, commande TLC v4, débits, trames ADDBA/DELBA, réordonnancement
 * (cas de iwl_mvm_reorder : trou, libération par NSSN, FRAME_RELEASE, trame ancienne, fin de session). */

static u8 ro_seen[64];
static int ro_nseen;

static void ro_collect(const u8 *eth, u32 len)
{
	if (ro_nseen < 64 && len)
		ro_seen[ro_nseen] = eth[0];
	ro_nseen++;
}

static u32 rdata(u16 sn, u16 nssn, int old)
{
	return (u32)nssn | ((u32)sn << 12) | (5u << 24) | (old ? 0x80000000u : 0);
}

static void test_ht(void)
{
	/* une balise de point d'accès 802.11ac, canal 36, 80 MHz (centre 42), WMM avec paramètres */
	u8 h[24], body[200];
	memset(h, 0, sizeof h);
	put16(h, 0x0080);
	unhex("aabbccddeeff", h + 16);
	u32 o = 12;
	memset(body, 0, sizeof body);
	put16(body + 8, 100);
	put16(body + 10, 0x0011);
	/* (identifiant, contenu ; la longueur est comptée ici) */
	static const struct {
		u8 id, n;
		const char *hex;
	} ies[] = {
		{0, 0, "6d61697a"},                         /* SSID « maiz » */
		{1, 0, "8c129824b048606c"},                 /* débits */
		{45, 21, "ef0917ffff"},                     /* HT : cap 09ef, A-MPDU 17, MCS 0-15 (+ 21 octets nuls) */
		{61, 18, "24050400"},                       /* HT op : 36, secondaire au-dessus, 40 MHz (+ 18 nuls) */
		{191, 0, "b259820ffaff0000faff0020"},       /* VHT : cap 0f8259b2, MCS 0-9 sur 2 flux */
		{192, 0, "012a000000"},                     /* VHT op : 80 MHz, centre 42 */
		{221, 0, "0050f2020101800003a4000027a4000042435e0062322f00"}, /* WMM : paramètres */
	};
	for (u32 i = 0; i < sizeof ies / sizeof ies[0]; i++) {
		u32 l = (u32)strlen(ies[i].hex) / 2;
		body[o] = ies[i].id;
		body[o + 1] = (u8)(l + ies[i].n);
		unhex(ies[i].hex, body + o + 2);
		memset(body + o + 2 + l, 0, ies[i].n);
		o += 2 + l + ies[i].n;
	}
	struct bss b;
	CHECK(parse_beacon(h, body, o, 36, -60, &b) == 1, "balise ac");
	CHECK(b.ht && b.ht_cap == 0x09ef && b.ht_ampdu == 0x17 && b.ht_mcs[0] == 0xff && b.ht_mcs[1] == 0xff, "HT cap");
	CHECK(b.ht_op && b.ht_op_sec == 5 && b.channel == 36, "HT op (secondaire au-dessus, 40 MHz)");
	CHECK(b.vht && b.vht_cap == 0x0f8259b2 && b.vht_rx_mcs == 0xfffa, "VHT cap");
	CHECK(b.vht_op && b.vht_op_width == 1 && b.vht_seg0 == 42, "VHT op");
	CHECK(b.wmm && b.wmm_params && b.wmm_ac[0][0] == 0x03 && b.wmm_ac[0][1] == 0xa4 && b.wmm_ac[1][0] == 0x27 &&
		      b.wmm_ac[2][0] == 0x42 && le16(b.wmm_ac[2] + 2) == 94 && b.wmm_ac[3][0] == 0x62 && le16(b.wmm_ac[3] + 2) == 47,
	      "WMM : paramètres par ACI");

	struct link_conf lc;
	u32 all = 7;
	link_choose(&b, LM_VHT, LW_80, all, &lc);
	CHECK(lc.mode == LM_VHT && lc.wmm && lc.width == LW_80 && lc.center == 42 && lc.ctrl_pos == 1 && lc.nss == 2,
	      "lien VHT 80 MHz (centre %u, position %x, %d flux)", lc.center, lc.ctrl_pos, lc.nss);
	CHECK(lc.agg_size == 7 && lc.dens == 5 && lc.sgi == 7 && lc.ldpc && lc.stbc, "agrégation, SGI, LDPC, STBC");
	CHECK(lc.ht_rates[0] == 0x3ff && lc.ht_rates[1] == 0x3ff && lc.non_ht == 0xff0, "MCS 0-9 sur 2 flux, a/g");
	CHECK(link_max_mbps(&lc) == 866, "866 Mb/s au plus (%u)", link_max_mbps(&lc));
	CHECK(!strcmp(link_name(&lc), "VHT 80 MHz"), "nom du lien");
	struct bss b2 = b;
	b2.channel = 44;
	link_choose(&b2, LM_VHT, LW_80, all, &lc);
	CHECK(lc.center == 42 && lc.ctrl_pos == 4, "primaire 44 : position 4 (%x)", lc.ctrl_pos);
	b2.channel = 48;
	link_choose(&b2, LM_VHT, LW_80, all, &lc);
	CHECK(lc.ctrl_pos == 5, "primaire 48 : position 5 (%x)", lc.ctrl_pos);
	link_choose(&b, LM_VHT, LW_80, 3, &lc);
	CHECK(lc.width == LW_40 && lc.center == 38 && lc.ctrl_pos == 0, "80 MHz interdit : 40 MHz (centre %u)", lc.center);
	link_choose(&b, LM_HT, LW_80, all, &lc);
	CHECK(lc.mode == LM_HT && lc.width == LW_40 && lc.ht_rates[0] == 0xff && lc.ht_rates[1] == 0xff, "norme=n : HT 40 MHz");
	link_choose(&b, LM_VHT, LW_20, all, &lc);
	CHECK(lc.width == LW_20 && lc.ht_rates[0] == 0x1ff && lc.sgi == 1, "largeur=20 : pas de MCS 9, SGI 20 seul");
	link_choose(&b, LM_AG, LW_80, all, &lc);
	CHECK(lc.mode == LM_AG && !lc.wmm, "norme=ag");
	b2 = b;
	b2.wmm = 0;
	link_choose(&b2, LM_VHT, LW_80, all, &lc);
	CHECK(lc.mode == LM_AG, "sans WMM : ni HT ni VHT (comme mac80211)");
	b2 = b;
	b2.channel = 6;
	link_choose(&b2, LM_VHT, LW_80, all, &lc);
	CHECK(lc.mode == LM_HT && lc.width == LW_20, "2,4 GHz : HT 20 MHz");
	b2 = b;
	b2.vht_rx_mcs = 0xfffd; /* un flux, MCS 0-8 */
	link_choose(&b2, LM_VHT, LW_80, all, &lc);
	CHECK(lc.nss == 1 && lc.ht_rates[0] == 0x1ff && lc.ht_rates[1] == 0, "un flux, MCS 0-8");

	/* demande d'association : HT (28), VHT (14), WMM (9) */
	link_choose(&b, LM_VHT, LW_80, all, &lc);
	u8 ie[80];
	u32 n = ht_assoc_ies(ie, &b, &lc);
	CHECK(n == 51, "éléments HT/VHT/WMM (%u octets)", n);
	CHECK(hexeq(ie, 28, "2d1a" "ed11" "17" "ffff" "0000000000000000" "2c01" "01" "000000" "0000" "00000000" "00"),
	      "élément HT de la station");
	CHECK(hexeq(ie + 28, 14, "bf0cb0718003faff0000faff0020"), "élément VHT de la station");
	CHECK(hexeq(ie + 42, 9, "dd070050f202000100"), "élément WMM de la station");
	link_choose(&b, LM_VHT, LW_20, all, &lc);
	n = ht_assoc_ies(ie, &b, &lc);
	CHECK(le16(ie + 2) == 0x11ad && le16(ie + 15) == 144, "20 MHz : ni 20/40 ni SGI 40 annoncés");
	u8 sta[6], f[512];
	unhex("021122334455", sta);
	link_choose(&b, LM_VHT, LW_80, all, &lc);
	n = ht_assoc_ies(ie, &b, &lc);
	u32 fl = build_assoc_req_ex(f, &b, sta, 3, 0, 0, ie, n);
	CHECK(fl == 24 + 4 + 6 + 10 + 51 && f[44] == 45 && f[72] == 191 && f[86] == 221, "demande d'association ac (%u)", fl);

	/* TLC v4 */
	u8 c[TLC_CFG_LEN];
	build_tlc_cmd(c, &lc, 0, 3, LW_20);
	CHECK(c[4] == 0 && c[5] == 2 && c[6] == 3 && c[7] == 7 && le16(c + 8) == 3 && le16(c + 10) == 0xff0 &&
		      le16(c + 12) == 0x3ff && le16(c + 18) == 0x3ff && le16(c + 24) == 0,
	      "TLC : VHT, 20 MHz avant l'autorisation, A+B, SGI, STBC+LDPC, MCS 0-9 x2");
	build_tlc_cmd(c, &lc, 0, 3, LW_80);
	CHECK(c[4] == 2, "TLC : 80 MHz une fois autorisé");
	build_tlc_cmd(c, &lc, 0, 2, LW_80);
	CHECK(le16(c + 8) == 2 && c[6] == 2, "TLC : une chaîne, pas de STBC");

	/* débits */
	CHECK(rate_mbps10(9 | 1u << 4 | 3u << 8 | 2u << 11 | 1u << 20) == 8666, "VHT MCS 9 2x2 80 MHz SGI : 866,6");
	CHECK(rate_mbps10(7 | 2u << 8) == 650, "HT MCS 7 20 MHz : 65");
	CHECK(rate_mbps10(7 | 1u << 8) == 540, "OFDM 54");
	CHECK(rate_mbps10(5 | 1u << 4 | 3u << 8 | 2u << 11) == 4680, "VHT MCS 5 2x2 80 MHz : 468");
	char d[80];
	rate_name(9 | 1u << 4 | 3u << 8 | 2u << 11 | 1u << 20, d, sizeof d);
	CHECK(!strcmp(d, "866.6 Mb/s (VHT MCS 9, 2x2, 80 MHz, SGI)"), "nom du débit : %s", d);

	/* block ack */
	u8 req[9];
	unhex("030005031000004006", req);
	struct addba a;
	u16 dt = 0;
	CHECK(parse_ba_action(req, 9, &a, &dt) == BA_ADDBA_REQ && a.dialog == 5 && a.amsdu == 1 && a.policy == 1 && a.tid == 0 &&
		      a.buf_size == 64 && a.timeout == 0 && a.ssn == 100,
	      "ADDBA : dialogue 5, TID 0, fenêtre 64, SSN %u", a.ssn);
	unhex("030005061400004006", req); /* TID 1, fenêtre 80 (0x50) */
	CHECK(parse_ba_action(req, 9, &a, &dt) == BA_ADDBA_REQ && a.tid == 1 && a.buf_size == 80 && !a.amsdu, "ADDBA TID 1");
	u8 bss_id[6];
	unhex("aabbccddeeff", bss_id);
	n = build_addba_resp(f, bss_id, sta, 9, &a, 64, 0);
	CHECK(n == 33 && le16(f) == 0x00d0 && hexeq(f + 24, 9, "030105000006100000"), "réponse ADDBA (fenêtre 64, sans A-MSDU)");
	u8 del[6];
	unhex("030200582500", del);
	CHECK(parse_ba_action(del, 6, &a, &dt) == BA_DELBA && dt == (5 | 0x100), "DELBA de l'initiateur, TID 5");
	n = build_delba(f, bss_id, sta, 10, 3, 37);
	CHECK(n == 30 && hexeq(f + 24, 6, "030200302500"), "DELBA de la station (TID 3)");
	u8 cat[3] = {4, 0, 0};
	CHECK(parse_ba_action(cat, 3, &a, &dt) == -1, "autre catégorie ignorée");

	/* QoS data */
	u8 eth[64];
	unhex("ffffffffffff0211223344550800", eth);
	for (int i = 14; i < 64; i++)
		eth[i] = (u8)i;
	n = eth_to_80211_qos(f, eth, 64, bss_id, sta, 7, 1, 0);
	CHECK(n == 84 && le16(f) == 0x4188 && le16(f + 24) == 0 && !memcmp(f + 26, "\xaa\xaa\x03\x00\x00\x00\x08\x00", 8) &&
		      f[34] == 14,
	      "Ethernet vers QoS data (en-tête 26)");

	/* QoS data reçue : descripteur avec bourrage (2 o après l'en-tête de 26), comme la puce la donne */
	{
		static u8 pk[RX_DESC_V1 + 200];
		memset(pk, 0, sizeof pk);
		u8 *fr = pk + RX_DESC_V1;
		put16(fr, 0x0288); /* QoS data, FromDS */
		memcpy(fr + 4, sta, 6);
		memcpy(fr + 10, bss_id, 6);
		memcpy(fr + 16, bss_id, 6);
		put16(fr + 24, 0);
		/* 2 octets de bourrage en 26-27, puis LLC/SNAP, EAPOL (88 8e), 10 octets */
		memcpy(fr + 28, "\xaa\xaa\x03\x00\x00\x00\x88\x8e", 8);
		for (int i = 0; i < 10; i++)
			fr[36 + i] = (u8)(0xa0 + i);
		put16(pk, 46);  /* longueur MPDU avec le bourrage */
		pk[3] = 0x20;   /* MFLG2_PAD */
		put32(pk + 12, RX_CRC_OK | RX_OVERRUN_OK);
		struct rx_frame rf;
		u8 out[1600];
		CHECK(parse_rx_mpdu(pk, RX_DESC_V1 + 46, &rf) == 0 && rf.pad && rf.hdrlen == 26 && rf.len == 44, "QoS reçue : bourrage");
		n = rx_to_eth(out, &rf, 0);
		CHECK(n == 14 + 10 && out[12] == 0x88 && out[13] == 0x8e && out[14] == 0xa0 && out[23] == 0xa9,
		      "QoS reçue vers Ethernet : rien de perdu à la fin (%u octets)", n);
	}

	/* réordonnancement */
	struct ro_buf r;
	u8 e[1];
	ro_init();
	ro_start(&r, 0, 5, 10, 8);
	ro_nseen = 0;
	e[0] = 10;
	CHECK(ro_rx(&r, rdata(10, 11, 0), 0, 0, 0, e, 1, ro_collect) == 0 && r.head == 11, "dans l'ordre : remise tout de suite");
	e[0] = 12;
	CHECK(ro_rx(&r, rdata(12, 11, 0), 0, 0, 0, e, 1, ro_collect) == 1 && r.stored == 1, "trou (11) : 12 retenue");
	e[0] = 13;
	CHECK(ro_rx(&r, rdata(13, 11, 0), 0, 0, 0, e, 1, ro_collect) == 1 && r.stored == 2, "13 retenue");
	e[0] = 11;
	CHECK(ro_rx(&r, rdata(11, 14, 0), 0, 0, 0, e, 1, ro_collect) == 1 && r.stored == 0 && r.head == 14, "11 arrive : NSSN 14");
	CHECK(ro_nseen == 3 && ro_seen[0] == 11 && ro_seen[1] == 12 && ro_seen[2] == 13, "remises dans l'ordre 11, 12, 13");
	e[0] = 9;
	CHECK(ro_rx(&r, rdata(9, 14, 1), 0, 0, 0, e, 1, ro_collect) == 1 && r.n_old == 1 && ro_nseen == 3, "ancienne : jetée");
	e[0] = 16;
	CHECK(ro_rx(&r, rdata(16, 14, 0), 0, 0, 0, e, 1, ro_collect) == 1 && r.stored == 1, "16 retenue (14, 15 manquent)");
	ro_release(&r, 17, ro_collect);
	CHECK(ro_nseen == 4 && ro_seen[3] == 16 && r.head == 17 && !r.stored, "FRAME_RELEASE jusqu'à 17 : 16 remise");
	e[0] = 17;
	CHECK(ro_rx(&r, rdata(17, 17, 0), 1, 0, 0, e, 1, ro_collect) == 1 && r.n_drop == 1, "doublon vu par la puce : jeté");
	/* A-MSDU : le NSSN d'une sous-trame qui n'est pas la dernière n'avance pas la tête */
	e[0] = 18;
	CHECK(ro_rx(&r, rdata(18, 17, 0), 0, 0, 0, e, 1, ro_collect) == 1, "18 retenue (17 manque)");
	e[0] = 0x71;
	CHECK(ro_rx(&r, rdata(17, 19, 0), 0, 1, 0, e, 1, ro_collect) == 1 && r.stored == 2 && ro_nseen == 4,
	      "A-MSDU : première sous-trame retenue, rien de libéré");
	e[0] = 0x72;
	CHECK(ro_rx(&r, rdata(17, 19, 0), 0, 1, 1, e, 1, ro_collect) == 1 && !r.stored && ro_nseen == 7 && ro_seen[4] == 0x71 &&
		      ro_seen[5] == 0x72 && ro_seen[6] == 18,
	      "A-MSDU : dernière sous-trame, 17 (deux sous-trames) puis 18");
	/* hors de la fenêtre (8) : la tête avance */
	e[0] = 21;
	ro_rx(&r, rdata(21, 19, 0), 0, 0, 0, e, 1, ro_collect);
	e[0] = 40;
	ro_rx(&r, rdata(40, 19, 0), 0, 0, 0, e, 1, ro_collect);
	CHECK(ro_nseen == 8 && ro_seen[7] == 21 && r.stored == 1, "trame hors de la fenêtre : la fenêtre avance (21 remise)");
	ro_stop(&r, ro_collect);
	CHECK(ro_nseen == 9 && ro_seen[8] == 40 && !r.on, "fin de session : 40 remise");
	/* passage de 4095 à 0 */
	ro_start(&r, 0, 5, 4094, 64);
	ro_nseen = 0;
	e[0] = 1;
	ro_rx(&r, rdata(0, 4094, 0), 0, 0, 0, e, 1, ro_collect);
	e[0] = 2;
	ro_rx(&r, rdata(4095, 4094, 0), 0, 0, 0, e, 1, ro_collect);
	e[0] = 3;
	ro_rx(&r, rdata(4094, 1, 0), 0, 0, 0, e, 1, ro_collect);
	CHECK(ro_nseen == 3 && ro_seen[0] == 3 && ro_seen[1] == 2 && ro_seen[2] == 1 && r.head == 1, "4094, 4095, 0 dans l'ordre");
	/* réserve pleine (4 sessions de 63 trames retenues, puis une cinquième) : celle-ci remet tout */
	static struct ro_buf rs[5];
	ro_init();
	ro_nseen = 0;
	int ok = 1;
	for (int k = 0; k < 4; k++) {
		ro_start(&rs[k], (u8)k, (u8)k, 0, 64);
		for (u16 sn = 1; sn < 64; sn++)
			ok &= ro_rx(&rs[k], rdata(sn, 0, 0), 0, 0, 0, e, 1, ro_collect) == 1;
	}
	ro_start(&rs[4], 4, 4, 0, 64);
	for (u16 sn = 1; sn <= 4; sn++)
		ok &= ro_rx(&rs[4], rdata(sn, 0, 0), 0, 0, 0, e, 1, ro_collect) == 1;
	CHECK(ok && rs[4].stored == 4, "256 trames retenues");
	CHECK(ro_rx(&rs[4], rdata(5, 0, 0), 0, 0, 0, e, 1, ro_collect) == 0 && rs[4].stored == 0 && ro_nseen == 4,
	      "réserve pleine : la session remet ses 4 trames, puis la nouvelle");
	for (int k = 0; k < 5; k++)
		ro_stop(&rs[k], ro_collect);
	CHECK(ro_nseen == 4 + 4 * 63, "fin des sessions : tout est remis (%d)", ro_nseen);
}
