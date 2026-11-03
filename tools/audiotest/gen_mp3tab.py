"""gen_mp3tab.py - tables du decodeur MP3 d'AudioKit (src/kits/audiokit/ak_mp3tab.inc)

    python -I tools/audiotest/gen_mp3tab.py [chemin de ffmpeg]

Les DONNEES de la norme ISO/IEC 11172-3 (annexe B : codes de Huffman de la
couche III, fenetre de synthese D[i] ; bandes de facteurs d'echelle,
pretab ; 13818-3 : nombre de bandes LSF) sont lues dans les tableaux de
ffmpeg (ou elles sont recopiees de la norme) ; aucun code n'en est repris.
Les tables calculees (cosinus de la synthese et de l'IMDCT, fenetres,
papillons d'anticrenelage, stereo d'intensite) viennent des formules de la
norme. Sortie : FASMARM, flottants en hexadecimal (dw = 32 bits).
En fin de fichier, les tables de codage de l'encodeur (mp3e_* : codes et
longueurs des memes tables de Huffman, quadruplets, choix des linbits).
"""
import math
import os
import re
import struct
import sys

FF = os.path.join(os.environ.get('ONYX', 'build/onyx'), 'third_party/ffmpeg-7.1.2/libavcodec')
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'src', 'kits', 'audiokit', 'ak_mp3tab.inc')


def carray(text, name):
    m = re.search(re.escape(name) + r'\s*(\[[^\]]*\])+\s*=\s*\{(.*?)\};', text, re.S)
    body = re.sub(r'/\*.*?\*/', '', m.group(2), flags=re.S)
    return [int(v, 0) for v in re.findall(r'-?0x[0-9A-Fa-f]+|-?\d+', body)]


def f32(v):
    return '0x%08X' % struct.unpack('<I', struct.pack('<f', v))[0]


def emit_words(out, label, vals, per=8, kind='dw'):
    out.append(label)
    for i in range(0, len(vals), per):
        out.append('\t\t%s\t%s' % (kind, ', '.join(str(v) for v in vals[i:i + per])))


def huff_codes(lens, syms):
    """codes d'apres les longueurs dans l'ordre lexicographique"""
    codes = []
    code = 0
    prev = None
    for i, l in enumerate(lens):
        if prev is None:
            code = 0
        else:
            code += 1
            if l > prev:
                code <<= (l - prev)
            elif l < prev:
                assert code & ((1 << (prev - l)) - 1) == 0
                code >>= (prev - l)
        prev = l
        codes.append((code, l, syms[i]))
    return codes


def build_lookup(codes, cap_first=7, cap=6):
    """tables a plusieurs niveaux, entrees de 16 bits :
       feuille : 0 | longueur << 8 | x << 4 | y ; noeud : 0x8000 | bits << 11 | decalage"""
    entries = []

    def make(cs, depth_bits, cap_now):
        maxlen = max(l - depth_bits for c, l, s in cs)
        nb = min(maxlen, cap_now)
        base = len(entries)
        entries.extend([None] * (1 << nb))
        groups = {}
        for c, l, s in cs:
            rel = l - depth_bits
            if rel <= nb:
                top = (c << (nb - rel)) & ((1 << nb) - 1) if rel <= nb else 0
                for k in range(1 << (nb - rel)):
                    assert entries[base + top + k] is None
                    entries[base + top + k] = (rel << 8) | s
            else:
                top = (c >> (rel - nb)) & ((1 << nb) - 1)
                groups.setdefault(top, []).append((c & ((1 << (rel - nb)) - 1), l - depth_bits - nb + 0, s))
        for top, sub in groups.items():
            subcodes = [(c, l, s) for c, l, s in sub]
            off = make_rel(subcodes, cap)
            sb = sub_bits[off]
            entries[base + top] = 0x8000 | (sb << 11) | off
        for i in range(base, base + (1 << nb)):
            if entries[i] is None:
                entries[i] = 0          # (code invalide : longueur 0)
        return base

    sub_bits = {}

    def make_rel(cs, cap_now):
        maxlen = max(l for c, l, s in cs)
        nb = min(maxlen, cap_now)
        base = len(entries)
        sub_bits[base] = nb
        entries.extend([None] * (1 << nb))
        groups = {}
        for c, l, s in cs:
            if l <= nb:
                top = (c << (nb - l)) & ((1 << nb) - 1)
                for k in range(1 << (nb - l)):
                    entries[base + top + k] = (l << 8) | s
            else:
                top = (c >> (l - nb)) & ((1 << nb) - 1)
                groups.setdefault(top, []).append((c & ((1 << (l - nb)) - 1), l - nb, s))
        for top, sub in groups.items():
            off = make_rel(sub, cap_now)
            entries[base + top] = 0x8000 | (sub_bits[off] << 11) | off
        for i in range(base, base + (1 << nb)):
            if entries[i] is None:
                entries[i] = 0
        return base

    first = make_rel(codes, cap_first)
    assert first == 0
    assert len(entries) < 2048
    return entries, sub_bits[0]


def encoder_tables(out, lens, syms, sizes, huff_data, quad_codes, quad_bits):
    """tables de codage de l'encodeur (ak_mp3enc.inc) : memes codes que le decodeur"""
    dist = []
    pos = 0
    for t in range(15):
        n = sizes[t]
        cs = huff_codes(lens[pos:pos + n], syms[pos:pos + n])
        pos += n
        dim = max(s >> 4 for c, l, s in cs) + 1
        d = {(s >> 4, s & 15): (c, l) for c, l, s in cs}
        assert len(d) == dim * dim
        dist.append((dim, d))
    starts = []
    hlen = []
    hcode = []
    for dim, d in dist:
        starts.append(len(hlen))
        for x in range(dim):
            for y in range(dim):
                c, l = d[(x, y)]
                assert l <= 24
                hlen.append(l)
                hcode.append(c)
    # repertoire des 32 tables : debut, dimension (0 : table 0 ou inutilisee), linbits
    hdir = []
    first = {}
    for i in range(32):
        v, lb = huff_data[2 * i], huff_data[2 * i + 1]
        if v == 0:
            hdir.append((0, 0, 0))
        else:
            hdir.append((starts[v - 1], dist[v - 1][0], lb))
            first.setdefault(v, i)
    order = sorted(first.items())
    assert [t for v, t in order] == [1, 2, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 15, 16, 24]
    out.append('; --- encodeur (ak_mp3enc.inc) ---')
    out.append('; 15 tables de codes distinctes (1 2 3 5 6 7 8 9 10 11 12 13 15 16 24) :')
    out.append('; debut (indice x * dimension + y), dimension, numero')
    out.append('mp3e_hlist:')
    for v, t in order:
        out.append('\t\tdh\t%d' % starts[v - 1])
        out.append('\t\tdb\t%d, %d' % (dist[v - 1][0], t))
    out.append('; tables 0..31 : debut, dimension (0 : table 0, inutilisee), linbits')
    out.append('mp3e_hdir:')
    for s, dm, lb in hdir:
        out.append('\t\tdh\t%d' % s)
        out.append('\t\tdb\t%d, %d' % (dm, lb))
    emit_words(out, '; longueurs des codes\nmp3e_hlen:', hlen, 16, 'db')
    out.append('\t\talign\t4')
    emit_words(out, '; codes\nmp3e_hcode:', ['0x%X' % c for c in hcode], 12, 'dw')
    q = []
    for i in range(16):
        q += [quad_codes[i], quad_bits[i]]
    emit_words(out, '; quadruplets, table A : code, longueur (indice v w x y)\nmp3e_quad:', q, 16, 'db')
    # linbits necessaires n (valeur max <= 15 + 2^n - 1) -> table et linbits des familles 16 et 24
    sel = []
    for need in range(14):
        row = []
        for base in (16, 24):
            for t in range(base, base + 8):
                if huff_data[2 * t + 1] >= need:
                    row += [t, huff_data[2 * t + 1]]
                    break
        sel += row
    emit_words(out, '; linbits necessaires 0..13 -> table 16..23, linbits, table 24..31, linbits\nmp3e_lbsel:',
               sel, 16, 'db')
    out.append('\t\talign\t4')


def main():
    global FF
    if len(sys.argv) > 1:
        FF = sys.argv[1]
    common = open(os.path.join(FF, 'mpegaudiodec_common.c'), encoding='utf-8').read()
    dsp = open(os.path.join(FF, 'mpegaudiodsp_data.c'), encoding='utf-8').read()
    lens = carray(common, 'mpa_hufflens')
    syms = carray(common, 'mpa_huffsymbols')
    sizes = [v + 1 for v in carray(common, 'mpa_huff_sizes_minus_one')]
    huff_data = carray(common, 'ff_mpa_huff_data')
    quad_codes = carray(common, 'mpa_quad_codes')
    quad_bits = carray(common, 'mpa_quad_bits')
    band_long = carray(common, 'ff_band_size_long')
    band_short = carray(common, 'ff_band_size_short')
    pretab = carray(common, 'ff_mpa_pretab')[22:]
    slen = carray(common, 'ff_slen_table')
    nsf = carray(common, 'ff_lsf_nsf_table')
    enwin = carray(dsp, 'ff_mpa_enwindow')
    assert len(enwin) == 257 and len(band_long) == 9 * 22

    out = ['; ' + '=' * 76,
           ';  kits/audiokit/ak_mp3tab.inc - tables du decodeur MP3 (ISO/IEC 11172-3,',
           ';  13818-3) ; ENGENDRE par tools/audiotest/gen_mp3tab.py : ne pas modifier',
           '; ' + '=' * 76, '']
    # Huffman : 15 tables distinctes (1,2,3,5,6,7,8,9,10,11,12,13,15,16,24)
    allent = []
    starts = []
    firsts = []
    pos = 0
    for t in range(15):
        n = sizes[t]
        ls = lens[pos:pos + n]
        ss = syms[pos:pos + n]
        pos += n
        codes = huff_codes(ls, ss)
        ent, fb = build_lookup(codes)
        starts.append(len(allent))
        firsts.append(fb)
        allent.extend(ent)
    # table des 32 numeros de la norme : decalage (entrees), bits du 1er niveau, linbits
    dirv = []
    for i in range(32):
        v, lb = huff_data[2 * i], huff_data[2 * i + 1]
        if v == 0:
            dirv += [0, 0, 0, 0]
        else:
            dirv += [starts[v - 1], firsts[v - 1], lb, 0]
    out.append('; repertoire : par table 0..31 : debut (entrees de 16 bits), bits du premier')
    out.append('; niveau (0 : table 0, tout a zero), linbits')
    emit_words(out, 'mp3_huffdir:', dirv, 8, 'dh')
    out.append('; entrees : feuille = longueur << 8 | x << 4 | y ; noeud = 0x8000 | bits << 11 | decalage')
    emit_words(out, 'mp3_huff:', ['0x%04X' % e for e in allent], 12, 'dh')
    # quadruplets : table A (Huffman) ; B : 4 bits inverses
    qa = huff_codes_sorted = sorted([(quad_codes[i], quad_bits[i], i) for i in range(16)], key=lambda t: (t[0] << (6 - t[1])))
    ent, fb = build_lookup([(c, l, s) for c, l, s in qa])
    out.append('; table de quadruplets A (count1table_select = 0), %d bits au premier niveau' % fb)
    emit_words(out, 'mp3_quadA:', ['0x%04X' % e for e in ent], 12, 'dh')
    out.append('MP3_QUADA_BITS = %d' % fb)
    # bandes : debuts (long : 23, court : 14) par frequence (9)
    bl = []
    for r in range(9):
        s = 0
        row = [0]
        for b in band_long[r * 22:(r + 1) * 22]:
            s += b
            row.append(s)
        bl += row + [0]
    emit_words(out, '; debuts des bandes longues (23 + 1 par frequence : 44,1 48 32 22,05 24 16 11,025 12 8 kHz)\nmp3_sfb_long:', bl, 12, 'dh')
    bs = []
    for r in range(9):
        s = 0
        row = [0]
        for b in band_short[r * 13:(r + 1) * 13]:
            s += b
            row.append(s)
        bs += row + [0, 0]
    emit_words(out, '; debuts des bandes courtes (14 + 2 par frequence)\nmp3_sfb_short:', bs, 16, 'dh')
    emit_words(out, 'mp3_pretab:', pretab + [0, 0], 12, 'db')
    emit_words(out, 'mp3_slen:', slen, 16, 'db')
    emit_words(out, '; nombre de bandes LSF (13818-3) [6][3][4]\nmp3_nsf:', nsf, 12, 'db')
    out.append('\t\talign\t4')
    # fenetre de synthese D[i], i = 0..511
    D = [0.0] * 512
    for i in range(257):
        v = enwin[i] / 65536.0
        D[i] = v
        if i:
            D[512 - i] = v if (i & 63) == 0 else -v
    emit_words(out, '; fenetre de synthese D[i] (annexe B, table 3-B.3)\nmp3_D:', [f32(v) for v in D], 8)
    # matrice N[i][k] = cos((16 + i)(2k + 1) pi / 64) : par colonne k, 64 valeurs
    N = []
    for k in range(32):
        for i in range(64):
            N.append(f32(math.cos((16 + i) * (2 * k + 1) * math.pi / 64)))
    emit_words(out, '; matrice de synthese N, colonne par colonne (k = 0..31, i = 0..63)\nmp3_N:', N, 8)
    # IMDCT : x[i] = somme X[k] cos(pi / 2n (2i + 1 + n / 2)(2k + 1)), par k puis i
    c36 = []
    for k in range(18):
        for i in range(36):
            c36.append(f32(math.cos(math.pi / 72 * (2 * i + 1 + 18) * (2 * k + 1))))
    emit_words(out, '; IMDCT longue (n = 36), par k (18) puis i (36)\nmp3_imdct36:', c36, 9)
    c12 = []
    for k in range(6):
        for i in range(12):
            c12.append(f32(math.cos(math.pi / 24 * (2 * i + 1 + 6) * (2 * k + 1))))
    emit_words(out, '; IMDCT courte (n = 12), par k (6) puis i (12)\nmp3_imdct12:', c12, 12)
    win = []
    for bt in range(4):
        w = [0.0] * 36
        for i in range(36):
            if bt == 0:
                w[i] = math.sin(math.pi / 36 * (i + 0.5))
            elif bt == 1:
                w[i] = math.sin(math.pi / 36 * (i + 0.5)) if i < 18 else 1.0 if i < 24 else \
                    math.sin(math.pi / 12 * (i - 18 + 0.5)) if i < 30 else 0.0
            elif bt == 3:
                w[i] = 0.0 if i < 6 else math.sin(math.pi / 12 * (i - 6 + 0.5)) if i < 12 else \
                    1.0 if i < 18 else math.sin(math.pi / 36 * (i + 0.5))
            else:
                w[i] = math.sin(math.pi / 12 * (i + 0.5)) if i < 12 else 0.0
        win += [f32(v) for v in w]
    emit_words(out, '; fenetres (types de bloc 0, 1, 2 : les 12 premieres, 3), 36 chacune\nmp3_win:', win, 9)
    ci = [-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037]
    cs = [f32(1 / math.sqrt(1 + c * c)) for c in ci]
    ca = [f32(c / math.sqrt(1 + c * c)) for c in ci]
    emit_words(out, '; anticrenelage : cs[i], ca[i]\nmp3_cs:', cs, 8)
    emit_words(out, 'mp3_ca:', ca, 8)
    # stereo d'intensite MPEG-1 : k = tan(p pi / 12), gauche = k / (1 + k), droite = 1 / (1 + k)
    il, ir = [], []
    for p in range(7):
        if p == 6:
            l, r = 1.0, 0.0
        else:
            k = math.tan(p * math.pi / 12)
            l, r = k / (1 + k), 1 / (1 + k)
        il.append(f32(l))
        ir.append(f32(r))
    emit_words(out, '; intensite MPEG-1 (is_pos 0..6) : gauche, droite\nmp3_is_l:', il, 8)
    emit_words(out, 'mp3_is_r:', ir, 8)
    # intensite MPEG-2 : io^n, io = 2^-1/4 (echelle 0) ou 2^-1/2 (1), n = 0..31
    lsf = []
    for sc in range(2):
        io = 2 ** -0.25 if sc == 0 else 2 ** -0.5
        lsf += [f32(io ** n) for n in range(32)]
    emit_words(out, '; intensite MPEG-2 : io^n (io = 2^-1/4, puis 2^-1/2), n = 0..31\nmp3_is_lsf:', lsf, 8)
    out.append('; 2^(q / 4), q = 0..3')
    emit_words(out, 'mp3_pow2q:', [f32(2 ** (q / 4)) for q in range(4)], 4)
    encoder_tables(out, lens, syms, sizes, huff_data, quad_codes, quad_bits)
    out.append('')
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out))
    print(OUT, len(allent), 'entrees de Huffman', firsts)


if __name__ == '__main__':
    main()
