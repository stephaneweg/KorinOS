"""opustab2c.py - convertit les tables de CELT de l'asm en C (port d'AudioKit) :
src/kits/audiokit/ak_opustab.inc (tables ce_* ; les sk_* de SILK sont déjà dans
ak_silk.c) et les petites tables de la fin de ak_celt.inc (ce_halfpi .. ce_bitdi)
-> src/kits/audiokit/ak_opustab.c.

    python -I tools/audiotest/opustab2c.py

FASMARM : db 8 bits, dh 16, dw 32, dd 64. Les flottants (dw de bits, dd de
doubles) deviennent des littéraux hexadécimaux exacts ; aucune adresse.
Écrit aussi les déclarations (pour ak_celt.h) sur la sortie standard.
"""
import os
import re
import struct

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
KIT = os.path.join(ROOT, 'src', 'kits', 'audiokit')
DST = os.path.join(KIT, 'ak_opustab.c')
NL = '\n'

# type C de chaque table (par défaut : d'après la directive)
TYPES = {
    'ce_window': 'float', 'ce_emeans': 'float', 'ce_pred': 'float', 'ce_combg': 'float',
    'ce_halfpi': 'double', 'ce_cindex': 's16', 'ce_tfsel': 's8',
}
DEF = {'db': 'u8', 'dh': 'u16', 'dw': 'u32', 'dd': 'u64'}
NOTE = {
    'ce_window': 'fenêtre de recouvrement (120)',
    'ce_emeans': 'énergies moyennes des bandes (eMeans)',
    'ce_eband': 'bornes des bandes (eband5ms)',
    'ce_logn': 'log2(largeur) des bandes, Q3 (logN400)',
    'ce_cindex': 'cache des impulsions : début de chaque (LM + 1, bande) dans ce_cbits (-1 : aucun)',
    'ce_balloc': 'vecteurs d\'allocation (11 x 21)',
    'ce_cbits': 'cache des impulsions (cache_bits50)',
    'ce_ccaps': 'plafonds des bandes [LM][C][21]',
    'ce_eprob': 'modèle de Laplace des énergies [LM][intra][42]',
    'ce_log2frac': 'log2 fractionnaire (LOG2_FRAC_TABLE)',
    'ce_halfpi': 'pi / 2',
    'ce_pred': 'prédiction des énergies : coef[4], beta[4], beta intra',
    'ce_combg': 'gains du filtre en peigne (3 jeux de 3)',
    'ce_exp2t8': '2^(k/8) en Q14',
    'ce_tfsel': 'tf_select_table [LM][8]',
    'ce_spread_icdf': '', 'ce_trim_icdf': '', 'ce_tapset_icdf': '', 'ce_small_icdf': '',
    'ce_spreadf': 'facteurs d\'étalement',
    'ce_ordery': 'ordre de Hadamard (2, 4, 8, 16)',
    'ce_bitil': 'entrelacement des bits du masque',
    'ce_bitdi': 'désentrelacement des bits du masque',
}


def val(s):
    s = s.strip()
    neg = s.startswith('-')
    if neg:
        s = s[1:]
    v = int(s, 16) if s.lower().startswith('0x') else int(s, 10)
    return -v if neg else v


def parse(path, first, last):
    """étiquettes de first à last (comprise) -> [(nom, directive, [valeurs brutes])]"""
    out = []
    cur = None
    on = False
    for line in open(path, encoding='latin-1'):
        line = line.split(';')[0].rstrip()
        m = re.match(r'^([A-Za-z_]\w*):?\s*(?:(db|dh|dw|dd)\s+(.*))?$', line)
        if m and line and not line[0].isspace():
            name = m.group(1)
            if name == first:
                on = True
            if not on:
                continue
            if cur and cur[0] == last:
                break
            if name.startswith('sk_'):
                break
            cur = [name, m.group(2), []]
            out.append(cur)
            if m.group(3):
                cur[2] += [x for x in m.group(3).split(',')]
            continue
        if not on or not cur:
            continue
        m = re.match(r'^\s+(db|dh|dw|dd)\s+(.*)$', line)
        if m:
            if cur[1] and cur[1] != m.group(1):
                raise SystemExit('directives mêlées : ' + cur[0])
            cur[1] = m.group(1)
            cur[2] += [x for x in m.group(2).split(',')]
    return [c for c in out if c[1]]


def lit(t, d, s):
    if t == 'double':
        v = struct.unpack('<d', struct.pack('<d', float(s)))[0]
        return re.sub(r'\.?0+p', 'p', v.hex())
    if t == 'float':
        b = val(s) & 0xFFFFFFFF
        f = struct.unpack('<f', struct.pack('<I', b))[0]
        if f == 0:
            return '-0.0f' if b >> 31 else '0.0f'
        return re.sub(r'\.?0+p', 'p', f.hex()) + 'f'
    v = val(s)
    if t.startswith('u'):
        bits = {'u8': 8, 'u16': 16, 'u32': 32, 'u64': 64}[t]
        if v < 0:
            v += 1 << bits
        return str(v)
    return str(v)


def main():
    tabs = parse(os.path.join(KIT, 'ak_opustab.inc'), 'ce_window', None)
    tabs += parse(os.path.join(KIT, 'ak_celt.inc'), 'ce_halfpi', 'ce_bitdi')
    src = ['/* AudioKit : tables de CELT (RFC 6716). ENGENDRÉ par tools/audiotest/opustab2c.py',
           ' * d\'après ak_opustab.inc (tables ce_*) et la fin de ak_celt.inc : ne pas modifier. */',
           '#include "ak_celt.h"', '']
    decl = []
    for name, d, vals in tabs:
        t = TYPES.get(name, DEF[d])
        vals = [v for v in vals if v.strip()]
        n = len(vals)
        if NOTE.get(name):
            src.append('/* ' + NOTE[name] + ' */')
        if n == 1 and t == 'double':
            src.append('const double ' + name + ' = ' + lit(t, d, vals[0]) + ';')
            decl.append('extern const double ' + name + ';')
            src.append('')
            continue
        src.append('const ' + t + ' ' + name + '[' + str(n) + '] = {')
        per = 8 if t in ('float',) else 16
        for i in range(0, n, per):
            src.append('\t' + ', '.join(lit(t, d, v) for v in vals[i:i + per]) + ',')
        src.append('};')
        src.append('')
        decl.append('extern const ' + t + ' ' + name + '[' + str(n) + '];')
    with open(DST, 'w', newline='', encoding='utf-8') as f:
        f.write(NL.join(src).rstrip() + NL)
    print(NL.join(decl))


if __name__ == '__main__':
    main()
