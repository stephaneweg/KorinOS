# check.py : contre-épreuve indépendante de iwltest.c (tools/pc/iwltest/run.sh).
# 1. La commande SCAN_REQ_UMAC v15 est reconstruite champ par champ, dans l'ordre des structures publiques
#    (iwl_scan_req_umac_v17 et ses sous-structures, « packed »), avec struct.pack, puis comparée octet pour
#    octet à celle de build_scan_req (scan_v15.bin).
# 2. La PTK et le MIC EAPOL sont recalculés avec hashlib / hmac de Python et comparés à ptk.txt.
import hashlib, hmac, struct, sys

out = sys.argv[1]
fails = 0

def le(fmt, *v):
    return struct.pack('<' + fmt, *v)

# ---- 1. balayage (32 canaux, actif générique, MAC 02:11:22:33:44:55)
mac = bytes([0x02, 0x11, 0x22, 0x33, 0x44, 0x55])
chans = list(range(1, 14)) + [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140]
general = (le('H', 0x0002 | 0x0080) + le('B', 0) + le('B', 0)          # flags, reserved, scan_start_mac_or_link_id
           + le('2B', 10, 10) + le('B', 2) + le('B', 8) + le('B', 10) + le('B', 0)  # active_dwell[2], adwell 2g/5g/social, flags2
           + le('H', 300) + le('2I', 0, 0) + le('2I', 0, 0) + le('I', 6)  # budget, max_out_of_time[2], suspend[2], priority
           + le('2B', 110, 110) + le('2B', 0, 0))                          # passive_dwell[2], num_of_fragments[2]
assert len(general) == 36
cfgs = b''
for c in chans:
    band = 1 if c <= 14 else 0
    cfgs += le('I', 1) + le('BBBB', c, band, 1, 0)                     # flags, channel_num, v2 {band, iter_count, iter_interval}
cfgs += bytes(8 * (67 - len(chans)))
channel = le('BB', 0x20, len(chans)) + le('2B', 10, 2) + cfgs
assert len(channel) == 540
periodic = le('HBB', 0, 1, 0) + le('HBB', 0, 0, 0) + le('HH', 0, 0)
frame = bytes([0x40, 0, 0, 0]) + b'\xff' * 6 + mac + b'\xff' * 6 + b'\0\0' + b'\0\0'
r24 = bytes([1, 8, 0x02, 0x04, 0x0b, 0x16, 0x0c, 0x12, 0x18, 0x24, 50, 4, 0x30, 0x48, 0x60, 0x6c])
r5 = bytes([1, 8, 0x0c, 0x12, 0x18, 0x24, 0x30, 0x48, 0x60, 0x6c])
buf = frame + r24 + r5
segs = le('HH', 0, 26) + le('HH', 26, len(r24)) + le('HH', 26 + len(r24), len(r5)) + le('HH', len(buf), 0) + le('HH', len(buf), 0)
preq = segs + buf + bytes(512 - len(buf))
direct = bytes([0, 0]) + bytes(32) + bytes(34 * 19)
probe = preq + le('BBH', 0, 0, 0) + direct + bytes(4 * 8) + bytes(16 * 6)
assert len(probe) == 1344
cmd = le('II', 0, 6) + general + channel + periodic + probe
got = open(out + '/scan_v15.bin', 'rb').read()
if len(cmd) != 1940 or got != cmd:
    fails += 1
    print('ÉCHEC : la commande de balayage diffère (%d / %d octets)' % (len(got), len(cmd)))
    for i in range(min(len(got), len(cmd))):
        if got[i] != cmd[i]:
            print('  premier écart à %d : %02x au lieu de %02x' % (i, got[i], cmd[i]))
            break
else:
    print('balayage v15 : 1940 octets identiques à la construction indépendante')

# ---- 2. PTK et MIC
pmk = hashlib.pbkdf2_hmac('sha1', b'ThisIsAPassword', b'ThisIsASSID', 4096, 32)
aa, spa = bytes.fromhex('a0a1a2a3a4a5'), bytes.fromhex('b0b1b2b3b4b5')
an = bytes(range(0xe0, 0x100)); sn = bytes(range(0xc0, 0xe0))
data = min(aa, spa) + max(aa, spa) + min(an, sn) + max(an, sn)
ptk = b''
for i in range(3):
    ptk += hmac.new(pmk, b'Pairwise key expansion' + b'\0' + data + bytes([i]), hashlib.sha1).digest()
ptk = ptk[:48]
eapol = bytearray((i * 7 + 3) & 0xff for i in range(121))
eapol[81:97] = bytes(16)
mic = hmac.new(ptk[:16], bytes(eapol), hashlib.sha1).digest()[:16]
want = ptk.hex() + '\n' + mic.hex() + '\n'
got = open(out + '/ptk.txt').read()
if got != want:
    fails += 1
    print('ÉCHEC : PTK/MIC\n  C      %s  Python %s' % (got, want))
else:
    print('PTK (KCK, KEK, TK) et MIC EAPOL : identiques à hashlib/hmac de Python')
# ---- 3. context info : struct iwl_context_info (version, contrôle, réservé, rbd_cfg, hcmd_cfg, réservé[4],
#         dump_cfg, edbg_cfg, pnvm_cfg, réservé[16], dram {umac_img[64], lmac_img[64], virtual_img[64]}, réservé[16])
sec = lambda i: 0x30000000 + 0x10000 * i
lmac = [sec(i) for i in range(0, 14)]
umac = [sec(i) for i in range(15, 30)]
pag = [sec(i) for i in range(31, 51)]
pad64 = lambda l: b''.join(le('Q', x) for x in l) + bytes(8 * (64 - len(l)))
ci = (le('HHHH', 0x354, 0, 1792 // 4, 0) + le('II', 0x100 | 9 << 4 | 4 << 9, 0) + le('Q', 0)
      + le('QQQ', 0x10000000, 0x10001000, 0x10002000) + le('Q', 0x20000000) + le('B', 2) + bytes(7)
      + bytes(16) + bytes(16) + bytes(16) + bytes(16) + bytes(64)
      + pad64(umac) + pad64(lmac) + pad64(pag) + bytes(64))
got = open(out + '/ctxt.bin', 'rb').read()
if len(ci) != 1792 or got != ci:
    fails += 1
    print('ÉCHEC : context info différent (%d / %d)' % (len(got), len(ci)))
else:
    print('context info : 1792 octets identiques à la disposition publique (UMAC à +192, LMAC à +704, pages à +1216)')
# ---- 4. tailles des commandes : les structures publiques (champs dans l'ordre, « packed »)
want = {
    'init_ext_cfg': struct.calcsize('<I'),            # iwl_init_extended_cfg_cmd : init_flags
    'nvm_access_complete': struct.calcsize('<I'),     # iwl_nvm_access_complete_cmd : reserved
    'nvm_get_info': struct.calcsize('<I'),            # iwl_nvm_get_info : reserved
    'tx_ant_cfg': struct.calcsize('<I'),              # iwl_tx_ant_cfg_cmd : valid
    'bt_config': struct.calcsize('<II'),              # iwl_bt_coex_cmd : mode, enabled_modules
    'soc_config': struct.calcsize('<II'),             # iwl_soc_configuration_cmd : flags, latency
    'mcc_update': struct.calcsize('<HBBI20s'),        # iwl_mcc_update_cmd : mcc, source_id, reserved, key, reserved2[20]
    'scan_cfg': struct.calcsize('<BBBBII'),           # iwl_scan_config : cam, promisc, bcast_sta_id, reserved, tx, rx
    'scan_req': 1940,
    # iwl_mac_ctx_cmd : id_and_color, action, mac_type, tsf_id, node_addr[6]+2, bssid[6]+2, cck, ofdm, protection,
    # cck_short_preamble, short_slot, filter, qos_flags, ac[5] (cw_min, cw_max, aifsn, fifos_mask, edca_txop),
    # union (le plus grand : iwl_mac_data_p2p_sta = iwl_mac_data_sta + ctwin)
    'mac_ctx': struct.calcsize('<IIII6sH6sHIIIIIII') + 5 * struct.calcsize('<HHBBH')
               + struct.calcsize('<IIQIIIIIII') + 4,
    'phy_ctx': struct.calcsize('<II' + 'IBBBB' + 'IIII'),     # iwl_phy_context_cmd (ci v2)
    'rlc': struct.calcsize('<I' + 'II' + 'IIII' + 'B3s'),     # iwl_rlc_config_cmd v2
    'binding': struct.calcsize('<II3III'),                    # iwl_binding_cmd v2
    'session_prot': struct.calcsize('<IIIIII'),               # iwl_mvm_session_prot_cmd
    # iwl_mvm_add_sta_cmd : add_modify, awake_acs, tid_disable_tx, mac_id_n_color, addr[6], reserved2, sta_id,
    # modify_mask, reserved3, station_flags, station_flags_msk, add_ba_tid, remove_ba_tid, add_ba_ssn,
    # sleep_tx_count, sleep_state_flags, station_type, assoc_id, beamform_flags, tfd_queue_msk, rx_ba_window,
    # sp_length, uapsd_acs
    'add_sta': struct.calcsize('<BBHI6sHBBHIIBBHHBBHHIHBB'),
    'add_sta_key': struct.calcsize('<BBH32s16s') + 24,       # common + rx_mic, tx_mic, transmit_seq_cnt
    'scd_queue': struct.calcsize('<I' + 'IB3sIIQQ'),          # iwl_scd_queue_cfg_cmd (add)
    'tx_cmd': struct.calcsize('<HHI' + 'IHH' + 'I'),          # iwl_tx_cmd_gen2 (dram_info : pn_low, pn_high, aux)
}
got = dict(l.split() for l in open(out + '/sizes.txt'))
bad = [k for k in want if int(got.get(k, -1)) != want[k]]
if bad:
    fails += 1
    print('ÉCHEC : tailles des commandes : ' + ', '.join('%s %s au lieu de %d' % (k, got.get(k), want[k]) for k in bad))
else:
    print('tailles des commandes : ' + ', '.join('%s %d' % (k, want[k]) for k in want) + ' : justes')
# ---- 5. le message 2 du supplicant (mlmetest.c) : PTK et MIC refaits avec hashlib/hmac
pmk = hashlib.pbkdf2_hmac('sha1', b'motdepasse-essai', b'Essai', 4096, 32)
aa, spa = bytes.fromhex('02aabbccddee'), bytes.fromhex('021122334455')
an = bytes(range(0x40, 0x60)); sn = bytes(range(0x90, 0xb0))
data = min(aa, spa) + max(aa, spa) + min(an, sn) + max(an, sn)
ptk = b''.join(hmac.new(pmk, b'Pairwise key expansion\0' + data + bytes([i]), hashlib.sha1).digest() for i in range(3))
m2 = bytearray(open(out + '/eapol_m2.bin', 'rb').read())
mic = bytes(m2[81:97])
m2[81:97] = bytes(16)
want = hmac.new(ptk[:16], bytes(m2), hashlib.sha1).digest()[:16]
if mic != want or m2[0:2] != b'\x02\x03' or int.from_bytes(m2[2:4], 'big') != len(m2) - 4:
    fails += 1
    print('ÉCHEC : message 2 EAPOL')
else:
    print('message 2 EAPOL : en-tête et MIC identiques à hashlib/hmac de Python')
sys.exit(1 if fails else 0)
