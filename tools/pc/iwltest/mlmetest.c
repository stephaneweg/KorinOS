/* mlmetest.c (inclus par iwltest.c) : trames d'authentification, d'association et de données, et le
 * supplicant WPA2 contre un point d'accès simulé ici (le rôle de l'authentificateur : messages 1 et 3,
 * renouvellement de la GTK), avec les calculs de crypto.c ; check.py refait le MIC du message 2 avec Python. */

static void ap_eapol(u8 *o, u32 *len, u16 ki, const u8 *replay, const u8 *nonce, const u8 *rsc, const u8 *data,
		     u32 dlen, const u8 *kck)
{
	memset(o, 0, 99 + dlen);
	o[0] = 2;
	o[1] = 3;
	o[2] = (u8)((95 + dlen) >> 8);
	o[3] = (u8)(95 + dlen);
	o[4] = 2;
	o[5] = (u8)(ki >> 8);
	o[6] = (u8)ki;
	o[8] = 16;
	memcpy(o + 9, replay, 8);
	if (nonce)
		memcpy(o + 17, nonce, 32);
	if (rsc)
		memcpy(o + 65, rsc, 8);
	o[97] = (u8)(dlen >> 8);
	o[98] = (u8)dlen;
	memcpy(o + 99, data, dlen);
	*len = 99 + dlen;
	if (kck) {
		u8 mic[16];
		eapol_mic(kck, o, *len, mic);
		memcpy(o + 81, mic, 16);
	}
}

static int ek_mic_ok(const u8 *kck, const u8 *e, u32 len)
{
	u8 t[600], mic[16];
	memcpy(t, e, len);
	memset(t + 81, 0, 16);
	eapol_mic(kck, t, len, mic);
	return !memcmp(mic, e + 81, 16);
}

static void test_mlme(const char *outdir)
{
	/* RFC 3394 4.1 : l'emballage */
	u8 kek[16], key[16], w[24];
	unhex("000102030405060708090a0b0c0d0e0f", kek);
	unhex("00112233445566778899aabbccddeeff", key);
	aes_wrap(kek, key, 16, w);
	CHECK(hexeq(w, 24, "1fa68b0a8112b447aef34bd8fb5a7b829d3e862371d2cfe5"), "RFC 3394 4.1 (emballage)");

	/* un point d'accès WPA2-PSK CCMP vu au balayage */
	struct bss b;
	memset(&b, 0, sizeof b);
	unhex("02aabbccddee", b.bssid);
	memcpy(b.ssid, "Essai", 5);
	b.ssid_len = 5;
	b.channel = 36;
	b.rsn = 1;
	b.pairwise = CIPH_CCMP;
	b.group = CIPH_CCMP;
	b.akm = AKM_PSK;
	b.capab = 0x0011;
	static const u8 rates[] = {0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c, 0x0c};
	memcpy(b.rates, rates, sizeof rates);
	b.n_rates = sizeof rates;
	u8 sta[6];
	unhex("021122334455", sta);
	u8 rsn[64];
	u32 rl = build_rsn_ie(rsn, &b);
	CHECK(rl == 22 && hexeq(rsn, 22, "30140100000fac040100000fac040100000fac020000"), "RSN de la station");
	struct bss bw = b;
	bw.mfpr = 1;
	CHECK(build_rsn_ie(rsn, &bw) == 0, "MFP obligatoire : refusé");
	bw = b;
	bw.akm = AKM_SAE;
	CHECK(build_rsn_ie(rsn, &bw) == 0, "SAE seul : refusé (étape 6)");
	rl = build_rsn_ie(rsn, &b);

	u8 f[512];
	u32 n = build_auth(f, b.bssid, sta, 1);
	CHECK(n == 30 && hexeq(f, 30, "b000000002aabbccddee02112233445502aabbccddee1000000001000000"), "authentification");
	n = build_assoc_req(f, &b, sta, 2, rsn, rl);
	/* fc 0000 ; cap 0011 ; écoute 10 ; SSID ; 8 débits + 1 étendu ; RSN */
	CHECK(n == 24 + 4 + 7 + 10 + 3 + 22 && le16(f + 24) == 0x0011 && f[28] == 0 && f[29] == 5 && f[35] == 1 &&
		      f[36] == 8 && f[45] == 50 && f[46] == 1 && f[47] == 0x0c && f[48] == 48,
	      "demande d'association (%u octets)", n);
	u8 r[30];
	memset(r, 0, sizeof r);
	put16(r, 0x00b0);
	put16(r + 26, 2);
	u16 st, aid;
	CHECK(parse_auth_resp(r, 30, &st) == 0 && st == 0, "réponse d'authentification");
	put16(r, 0x0010);
	put16(r + 26, 0);
	put16(r + 28, 0xc005);
	CHECK(parse_assoc_resp(r, 30, &st, &aid) == 0 && st == 0 && aid == 5, "réponse d'association (AID 5)");

	/* données : Ethernet -> 802.11 -> Ethernet (du point d'accès) */
	u8 eth[64], back[1600];
	unhex("ffffffffffff0211223344550800", eth);
	for (int i = 14; i < 64; i++)
		eth[i] = (u8)i;
	n = eth_to_80211(f, eth, 64, b.bssid, sta, 7, 1);
	CHECK(n == 82 && le16(f) == 0x4108 && !memcmp(f + 4, b.bssid, 6) && !memcmp(f + 16, eth, 6) && le16(f + 22) == 0x70 &&
		      !memcmp(f + 24, "\xaa\xaa\x03\x00\x00\x00\x08\x00", 8),
	      "Ethernet vers 802.11 (ToDS, protégé, LLC/SNAP)");
	u8 rx[200];
	put16(rx, 0x4208); /* FromDS, protégé */
	memcpy(rx + 4, sta, 6);
	memcpy(rx + 10, b.bssid, 6);
	unhex("021234567890", rx + 16);
	memset(rx + 24, 0xee, 8); /* en-tête CCMP */
	memcpy(rx + 32, "\xaa\xaa\x03\x00\x00\x00\x08\x06", 8);
	for (int i = 0; i < 28; i++)
		rx[40 + i] = (u8)i;
	n = wlan_to_eth(back, rx, 68, 24, 8);
	CHECK(n == 14 + 28 && !memcmp(back, sta, 6) && !memcmp(back + 6, rx + 16, 6) && back[12] == 8 && back[13] == 6 &&
		      back[14 + 27] == 27,
	      "802.11 vers Ethernet (FromDS, CCMP sauté) : %u", n);
	put16(rx, 0x4248); /* null data */
	CHECK(wlan_to_eth(back, rx, 68, 24, 8) == 0, "null data ignorée");

	/* EAPOL à 4 voies contre le point d'accès simulé */
	u8 pmk[32], ptk_ap[48], an[32], sn[32], replay[8] = {0, 0, 0, 0, 0, 0, 0, 1};
	wpa_pmk("motdepasse-essai", b.ssid, b.ssid_len, pmk);
	for (int i = 0; i < 32; i++)
		an[i] = (u8)(0x40 + i), sn[i] = (u8)(0x90 + i);
	struct supplicant s;
	sup_init(&s, pmk, b.bssid, sta, rsn, rl, CIPH_CCMP, sn);
	u8 m[600], out[600];
	u32 ml, ol;
	ap_eapol(m, &ml, 0x008a, replay, an, 0, 0, 0, 0); /* message 1 : ack, paire, v2 */
	int ev = sup_rx(&s, m, ml, out, &ol);
	CHECK(ev == SUP_EV_SEND && ol == 99 + 22, "message 2 rendu");
	wpa_ptk(pmk, b.bssid, sta, an, sn, ptk_ap);
	CHECK(ek_mic_ok(ptk_ap, out, ol) && out[5] == 0x01 && out[6] == 0x0a && !memcmp(out + 17, sn, 32) &&
		      !memcmp(out + 99, rsn, 22) && !memcmp(out + 9, replay, 8),
	      "message 2 : MIC, key_info 010a, SNonce, RSN, compteur");
	if (outdir) {
		char p[512];
		snprintf(p, sizeof p, "%s/eapol_m2.bin", outdir);
		FILE *fo = fopen(p, "wb");
		fwrite(out, 1, ol, fo);
		fclose(fo);
	}
	/* message 3 : données chiffrées (RSN + GTK KDE + bourrage) */
	u8 gtk[16], kd[64], kdw[72];
	for (int i = 0; i < 16; i++)
		gtk[i] = (u8)(0xa0 + i);
	u32 k = 0;
	memcpy(kd, rsn, rl);
	k = rl;
	static const u8 kde[] = {0xdd, 22, 0x00, 0x0f, 0xac, 0x01, 0x01, 0x00};
	memcpy(kd + k, kde, 8);
	k += 8;
	memcpy(kd + k, gtk, 16);
	k += 16;
	if (k % 8) { /* bourrage : 0xdd puis des zéros */
		kd[k++] = 0xdd;
		while (k % 8)
			kd[k++] = 0;
	}
	aes_wrap(ptk_ap + 16, kd, k, kdw);
	replay[7] = 2;
	u8 rsc[8] = {5, 0, 0, 0, 0, 0, 0, 0};
	ap_eapol(m, &ml, 0x13ca, replay, an, rsc, kdw, k + 8, ptk_ap); /* install, ack, mic, secure, enc, paire */
	ev = sup_rx(&s, m, ml, out, &ol);
	CHECK(ev == SUP_EV_PTK && s.state == SUP_DONE, "message 3 accepté (événement %d)", ev);
	CHECK(!memcmp(s.ptk + 32, ptk_ap + 32, 16) && s.gtk_len == 16 && !memcmp(s.gtk, gtk, 16) && s.gtk_idx == 1 &&
		      s.gtk_rsc[0] == 5,
	      "TK, GTK n° 1, RSC");
	CHECK(ol == 99 && ek_mic_ok(ptk_ap, out, ol) && out[5] == 0x03 && out[6] == 0x0a && !memcmp(out + 9, replay, 8),
	      "message 4 : MIC, key_info 030a");
	/* rejoué, MIC faux */
	ev = sup_rx(&s, m, ml, out, &ol);
	CHECK(ev == SUP_EV_NONE || ev == SUP_EV_BAD, "message 3 rejoué refusé");
	/* renouvellement de la GTK */
	for (int i = 0; i < 16; i++)
		gtk[i] = (u8)(0xc0 + i);
	k = 0;
	static const u8 kde2[] = {0xdd, 22, 0x00, 0x0f, 0xac, 0x01, 0x02, 0x00};
	memcpy(kd, kde2, 8);
	memcpy(kd + 8, gtk, 16);
	k = 24;
	aes_wrap(ptk_ap + 16, kd, k, kdw);
	replay[7] = 3;
	ap_eapol(m, &ml, 0x1382, replay, 0, rsc, kdw, k + 8, ptk_ap); /* groupe : ack, mic, secure, enc */
	ev = sup_rx(&s, m, ml, out, &ol);
	CHECK(ev == SUP_EV_GTK && s.gtk_idx == 2 && !memcmp(s.gtk, gtk, 16), "nouvelle GTK n° 2");
	CHECK(ol == 99 && ek_mic_ok(ptk_ap, out, ol) && out[5] == 0x03 && out[6] == 0x02, "réponse de groupe : key_info 0302");
	/* mauvaise clé : le message 3 de l'AP (qui a la bonne PMK) est refusé par MIC */
	struct supplicant s2;
	u8 badpmk[32];
	wpa_pmk("mauvaise-clé", b.ssid, b.ssid_len, badpmk);
	sup_init(&s2, badpmk, b.bssid, sta, rsn, rl, CIPH_CCMP, sn);
	replay[7] = 1;
	ap_eapol(m, &ml, 0x008a, replay, an, 0, 0, 0, 0);
	sup_rx(&s2, m, ml, out, &ol);
	CHECK(!ek_mic_ok(ptk_ap, out, ol), "mauvaise clé : MIC du message 2 faux pour l'AP");
	replay[7] = 2;
	k = 48;
	aes_wrap(ptk_ap + 16, kd, 24, kdw);
	ap_eapol(m, &ml, 0x13ca, replay, an, rsc, kdw, 32, ptk_ap);
	ev = sup_rx(&s2, m, ml, out, &ol);
	CHECK(ev == SUP_EV_BAD && s2.bad_mic == 1, "mauvaise clé : message 3 refusé (MIC)");
}
