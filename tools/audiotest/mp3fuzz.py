"""mp3fuzz.py - trames MP3 couche III ALEATOIRES mais valides, pour exercer
tous les chemins du decodeur d'AudioKit (blocs courts, mixtes, de debut et
de fin, scfsi, pretab, M/S, intensite MPEG-1 et MPEG-2, LSF, linbits,
quadruplets A et B, reservoir de bits). Le son n'a aucun sens : seule la
comparaison avec la reference (minimp3) compte.

    python -I tools/audiotest/mp3fuzz.py sortie.mp3 [mpeg1|lsf|lsf25] [graine] [trames]
"""
import os
import random
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_mp3tab as G  # noqa: E402


class BW:
    def __init__(self):
        self.bits = []

    def put(self, v, n):
        for i in range(n - 1, -1, -1):
            self.bits.append((v >> i) & 1)

    def tobytes(self):
        b = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(int(''.join(map(str, b[i:i + 8])), 2) for i in range(0, len(b), 8))


def load_tables():
    common = open(os.path.join(G.FF, 'mpegaudiodec_common.c'), encoding='utf-8').read()
    lens = G.carray(common, 'mpa_hufflens')
    syms = G.carray(common, 'mpa_huffsymbols')
    sizes = [v + 1 for v in G.carray(common, 'mpa_huff_sizes_minus_one')]
    huff_data = G.carray(common, 'ff_mpa_huff_data')
    quad_codes = G.carray(common, 'mpa_quad_codes')
    quad_bits = G.carray(common, 'mpa_quad_bits')
    band_long = G.carray(common, 'ff_band_size_long')
    band_short = G.carray(common, 'ff_band_size_short')
    pos = 0
    vlc = []
    for t in range(15):
        n = sizes[t]
        codes = G.huff_codes(lens[pos:pos + n], syms[pos:pos + n])
        pos += n
        vlc.append({s: (c, l) for c, l, s in codes})
    tables = {}
    for i in range(32):
        v, lb = huff_data[2 * i], huff_data[2 * i + 1]
        if v:
            tables[i] = (vlc[v - 1], lb)
    return tables, quad_codes, quad_bits, band_long, band_short


TABLES, QC, QB, BL, BS = load_tables()


def tmax(t):
    """plus grande valeur codable par la table t (15 + linbits ou le maximum)"""
    code, lb = TABLES[t]
    m = max(max(s >> 4, s & 15) for s in code)
    return m + ((1 << lb) - 1 if lb else 0)


def enc_pair(bw, t, x, y):
    code, lb = TABLES[t]
    ax, ay = abs(x), abs(y)
    sx, sy = min(ax, 15) if lb else ax, min(ay, 15) if lb else ay
    c, l = code[sx << 4 | sy]
    bw.put(c, l)
    if lb and sx == 15:
        bw.put(ax - 15, lb)
    if ax:
        bw.put(1 if x < 0 else 0, 1)
    if lb and sy == 15:
        bw.put(ay - 15, lb)
    if ay:
        bw.put(1 if y < 0 else 0, 1)


def main():
    out = sys.argv[1]
    kind = sys.argv[2] if len(sys.argv) > 2 else 'mpeg1'
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    nframes = int(sys.argv[4]) if len(sys.argv) > 4 else 200
    rng = random.Random(seed)
    opts = set(os.environ.get('FUZZ', '').split(','))   # long, short, mixed, ms, is, nostereo, noresv, small
    lsf = kind != 'mpeg1'
    if kind == 'mpeg1':
        ver, sri, rate, br_tab, brk = 3, 0, 44100, [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320], 144000
    elif kind == 'lsf':
        ver, sri, rate, br_tab, brk = 2, 3, 22050, [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160], 72000
    else:
        ver, sri, rate, br_tab, brk = 0, 8, 8000, [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160], 72000
    srbits = {0: 0, 3: 0, 8: 2}[sri]
    ngr = 1 if lsf else 2
    nch = 2
    bandl = [0]
    for b in BL[sri * 22:(sri + 1) * 22]:
        bandl.append(bandl[-1] + b)
    bands = [0]
    for b in BS[sri * 13:(sri + 1) * 13]:
        bands.append(bands[-1] + b)
    data = bytearray()
    resv = bytearray()          # octets de donnees principales en reserve
    legal_state = [0]
    prev_bt = [0, 0]
    for fi in range(nframes):
        br_idx = rng.choice([9, 10, 11, 12, 13, 14]) if not lsf else rng.choice([8, 9, 10, 11, 12])
        br = br_tab[br_idx]
        flen = brk * br // rate
        pad = 0
        flen += pad
        mode_ext = rng.choice([0, 1, 2, 3])
        if 'nostereo' in opts:
            mode_ext = 0
        if 'ms' in opts:
            mode_ext = 2
        if 'is' in opts:
            mode_ext = 1
        side_len = (32 if nch == 2 else 17) if not lsf else (17 if nch == 2 else 9)
        main_room = flen - 4 - side_len
        # granules
        gis = []
        main = BW()
        # types de bloc (les memes pour les deux canaux : M/S)
        btl = []
        saved_state = legal_state[0]
        for gr in range(ngr):
            if 'legal' in opts:           # suite permise : long -> debut -> court -> fin
                p = legal_state[0]
                nxt = {0: [0, 0, 1], 1: [2], 2: [2, 2, 3], 3: [0, 1]}[p]
                b = rng.choice(nxt)
                legal_state[0] = b
                btl.append((b, 1 if b == 2 and 'mixed' in opts and rng.random() < 0.5 else 0))
                continue
            choice = rng.random()
            if choice < 0.4:
                btl.append((0, 0))
            elif choice < 0.55:
                btl.append((1, 0))
            elif choice < 0.75:
                btl.append((2, 0))
            elif choice < 0.85:
                btl.append((2, 1))
            else:
                btl.append((3, 0))
            if 'long' in opts:
                btl[-1] = (0, 0)
            if 'legal' in opts:
                pass
            if 'short' in opts:
                btl[-1] = (2, 0)
            if 'mixed' in opts:
                btl[-1] = (2, 1)
            if 'startstop' in opts:
                btl[-1] = (rng.choice([1, 3]), 0)
        if os.environ.get('FUZZBT'):
            print(fi, btl)
        scfsi = [[0] * 4 for _ in range(nch)]
        if not lsf and all(b != 2 for b, m in btl) and 'noscfsi' not in opts:
            for ch in range(nch):
                scfsi[ch] = [rng.randint(0, 1) for _ in range(4)]
        for gr in range(ngr):
            bt, mixed = btl[gr]
            for ch in range(nch):
                gi = {}
                gi['wsf'] = 1 if bt else 0
                gi['bt'] = bt
                gi['mixed'] = mixed
                gi['gain'] = rng.randint(120, 150)
                gi['scale'] = rng.randint(0, 1)
                gi['pre'] = rng.randint(0, 1) if not lsf and bt != 2 and 'nopre' not in opts else 0
                gi['c1t'] = rng.randint(0, 1)
                gi['sbg'] = [rng.randint(0, 7) for _ in range(3)] if bt == 2 else [0, 0, 0]
                start = len(main.bits)
                # facteurs d'echelle
                is_right = lsf and ch == 1 and (mode_ext & 1)
                if not lsf:
                    sfc = rng.randint(0, 15)
                    gi['sfc'] = sfc
                    slen1 = [0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4][sfc]
                    slen2 = [0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3][sfc]
                    if bt == 2:
                        if mixed:
                            for b in range(8):
                                main.put(rng.randint(0, (1 << slen1) - 1), slen1)
                            rng_s = range(3, 12)
                        else:
                            rng_s = range(12)
                        for b in rng_s:
                            sl = slen1 if b < 6 else slen2
                            for w in range(3):
                                main.put(rng.randint(0, (1 << sl) - 1), sl)
                    else:
                        groups = [(0, 6), (6, 11), (11, 16), (16, 21)]
                        for g, (a, z) in enumerate(groups):
                            if gr == 1 and scfsi[ch][g]:
                                continue
                            sl = slen1 if g < 2 else slen2
                            for b in range(a, z):
                                main.put(rng.randint(0, (1 << sl) - 1), sl)
                else:
                    # LSF : scalefac_compress au hasard, nombres de bandes de la norme
                    bti = 0 if bt != 2 else (2 if mixed else 1)
                    if not is_right:
                        sfc = rng.randint(0, 511)
                        if sfc < 400:
                            sl = [(sfc >> 4) // 5, (sfc >> 4) % 5, (sfc & 15) >> 2, sfc & 3]
                            tb = 0
                        elif sfc < 500:
                            s = sfc - 400
                            sl = [(s >> 2) // 5, (s >> 2) % 5, s & 3, 0]
                            tb = 1
                        else:
                            s = sfc - 500
                            sl = [s // 3, s % 3, 0, 0]
                            tb = 2
                    else:
                        isc = rng.randint(0, 255)
                        sfc = isc * 2 + rng.randint(0, 1)
                        if sfc > 511:
                            sfc = 511
                        s = sfc >> 1
                        if s < 180:
                            sl = [s // 36, (s % 36) // 6, (s % 36) % 6, 0]
                            tb = 3
                        elif s < 244:
                            s -= 180
                            sl = [(s % 64) >> 4, (s % 16) >> 2, s % 4, 0]
                            tb = 4
                        else:
                            s -= 244
                            sl = [s // 3, s % 3, 0, 0]
                            tb = 5
                    gi['sfc'] = sfc
                    nr = G.carray(open(os.path.join(G.FF, 'mpegaudiodec_common.c'), encoding='utf-8').read(), 'ff_lsf_nsf_table')[tb * 12 + bti * 4: tb * 12 + bti * 4 + 4]
                    for g in range(4):
                        for _ in range(nr[g]):
                            if sl[g]:
                                main.put(rng.randint(0, (1 << sl[g]) - 1), sl[g])
                # Huffman : grandes valeurs, quadruplets
                sfbits = len(main.bits) - start
                maxbig = 288
                if bt == 2 and not mixed:
                    r1 = bands[3] * 3
                    r2 = 576
                    gi['r0'], gi['r1'] = 8, 20
                elif bt:
                    r1 = bandl[8]
                    r2 = 576
                    gi['r0'], gi['r1'] = 7, 20
                else:
                    gi['r0'] = rng.randint(0, 10)
                    gi['r1'] = rng.randint(0, 6)
                    r1 = bandl[min(gi['r0'] + 1, 22)]
                    r2 = bandl[min(gi['r0'] + gi['r1'] + 2, 22)]
                # droite en intensite : se tait au-dessus d'une limite
                limit = 576
                if ch == 1 and (mode_ext & 1):
                    limit = rng.choice([bandl[6], bandl[10], bandl[14], 3 * bands[5], 3 * bands[8]])
                bigv = rng.randint(0, min(maxbig, limit // 2))
                ts = []
                for r in range(3):
                    ts.append(rng.choice([0, 1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 17, 20, 23, 24, 26, 31]))
                gi['ts'] = ts
                gi['bigv'] = bigv
                vals = []
                for i in range(0, bigv * 2, 2):
                    reg = 0 if i < r1 else 1 if i < r2 else 2
                    t = ts[reg] if bt == 0 else ts[min(reg, 1)]
                    if t == 0:
                        vals += [0, 0]
                        enc = None
                        continue
                    m = min(tmax(t), 40 if rng.random() < 0.97 or 'small' in opts else 600)
                    x = rng.randint(-m, m) if rng.random() < 0.6 else 0
                    y = rng.randint(-m, m) if rng.random() < 0.6 else 0
                    enc_pair(main, t, x, y)
                    vals += [x, y]
                # quadruplets
                bigbits = len(main.bits) - start
                pos = bigv * 2
                nq = rng.randint(0, max(0, (min(limit, 576) - pos) // 4))
                for q in range(nq):
                    v = [rng.choice([0, 0, 1]) * rng.choice([1, -1]) for _ in range(4)]
                    s = (abs(v[0]) << 3) | (abs(v[1]) << 2) | (abs(v[2]) << 1) | abs(v[3])
                    if gi['c1t'] == 0:
                        main.put(QC[s], QB[s])
                    else:
                        main.put(15 - s, 4)
                    for k in range(4):
                        if v[k]:
                            main.put(1 if v[k] < 0 else 0, 1)
                gi['p23'] = len(main.bits) - start
                if gi['p23'] > 4095:         # (12 bits) : granule vide a la place
                    del main.bits[start:]
                    gi['bigv'] = 0
                    gi['p23'] = 0
                    gi['sfc'] = 0
                    gi['scale'] = 0
                    gi['pre'] = 0
                    if not lsf:
                        scfsi[ch] = [0] * 4
                if os.environ.get('FUZZDBG') == '%d' % (fi * ngr + gr) and ch == 1:
                    print('gr', fi * ngr + gr, 'start', start, 'sf', sfbits, 'big', bigbits, 'p23', gi['p23'], 'ts', ts, 'r0r1', gi['r0'], gi['r1'], 'bigv', bigv, 'limit', limit, 'vals', vals, 'nq', nq, 'c1t', gi['c1t'])
                gis.append(gi)
            prev_bt = [bt, bt]
        # bourrage : la trame doit contenir ses donnees (reservoir eventuel)
        mbytes = main.tobytes()
        mdb = 0
        if 'noresv' not in opts and fi > 0 and len(resv) > 0:
            mdb = min(len(resv), rng.randint(0, 255 if lsf else 511), len(mbytes))
        need = len(mbytes) - mdb
        if need > main_room:
            # trop gros : on recommence avec un debit maximal
            br_idx = 14 if not lsf else 14
            br = br_tab[br_idx]
            flen = brk * br // rate
            main_room = flen - 4 - side_len
            if need > main_room:
                print('trame trop grosse, ignoree', fi)
                legal_state[0] = saved_state
                continue
        # en-tete
        hdr = (0x7FF << 21) | (ver << 19) | (1 << 17) | (1 << 16) | (br_idx << 12) | (srbits << 10) | (0 << 9) | (1 << 6) | (mode_ext << 4)
        side = BW()
        side.put(mdb, 8 if lsf else 9)
        side.put(0, (2 if nch == 2 else 1) if lsf else (3 if nch == 2 else 5))
        if not lsf:
            for ch in range(nch):
                for g in range(4):
                    side.put(scfsi[ch][g], 1)
        for gr in range(ngr):
            for ch in range(nch):
                gi = gis[gr * nch + ch]
                side.put(gi['p23'], 12)
                side.put(gi['bigv'], 9)
                side.put(gi['gain'], 8)
                side.put(gi['sfc'], 9 if lsf else 4)
                side.put(gi['wsf'], 1)
                if gi['wsf']:
                    side.put(gi['bt'], 2)
                    side.put(gi['mixed'], 1)
                    side.put(gi['ts'][0], 5)
                    side.put(gi['ts'][1], 5)
                    for w in range(3):
                        side.put(gi['sbg'][w], 3)
                else:
                    for r in range(3):
                        side.put(gi['ts'][r], 5)
                    side.put(gi['r0'], 4)
                    side.put(gi['r1'], 3)
                if not lsf:
                    side.put(gi['pre'], 1)
                side.put(gi['scale'], 1)
                side.put(gi['c1t'], 1)
        sb = side.tobytes()
        assert len(sb) == side_len, (len(sb), side_len)
        # les mdb premiers octets des donnees vont dans le reservoir (fin
        # de la trame precedente) : on reecrit la fin des octets deja emis
        if mdb:
            data[len(data) - mdb:] = mbytes[:mdb]
        body = mbytes[mdb:]
        if os.environ.get('FUZZDBG'):
            print('trame', fi, 'mdb', mdb, 'main', int.from_bytes(mbytes[:8], 'big'), int.from_bytes(mbytes[136:144], 'big') >> 1, int.from_bytes(mbytes[144:152], 'big') >> 1, len(mbytes))
        frame_main = body + bytes(main_room - len(body))
        frame = hdr.to_bytes(4, 'big') + sb + frame_main
        assert len(frame) == flen
        data += frame
        # reserve : les octets de bourrage de cette trame (les derniers)
        resv = bytes(main_room - len(body))
    with open(out, 'wb') as f:
        f.write(data)
    print(out, len(data), 'octets')


if __name__ == '__main__':
    main()
