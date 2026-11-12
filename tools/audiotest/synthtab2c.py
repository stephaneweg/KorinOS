"""synthtab2c.py - tables du synthe MIDI d'AudioKit en C (port de l'asm).

Lit src/kits/audiokit/ak_sftab.inc (ak_concave) et ak_gmbank.inc (ak_gm_prog,
ak_gm_drum, ak_gm_ins) et ecrit src/kits/audiokit/ak_gmbank.c : une seule
structure, dans l'ordre de l'asm (ak_synth.inc inclut ak_sftab.inc puis
ak_gmbank.inc).

    python -I tools/audiotest/synthtab2c.py      (depuis la racine du depot)
"""
import os
import re

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
KIT = os.path.join(ROOT, 'src', 'kits', 'audiokit')


def tables(path):
    """etiquette -> (directive, liste de valeurs)"""
    out = {}
    cur = None
    for line in open(path, encoding='latin-1'):
        line = line.split(';', 1)[0].rstrip()
        m = re.match(r'^([A-Za-z_]\w*):?\s*(.*)$', line)
        if m and not line[:1].isspace():
            cur = m.group(1)
            out[cur] = [None, []]
            line = ' ' + m.group(2)
        m = re.match(r'^\s+(db|dh)\s+(.*)$', line)
        if m and cur:
            out[cur][0] = m.group(1)
            out[cur][1] += [int(v, 0) for v in m.group(2).split(',') if v.strip()]
    return out


def rows(vals, n, ind='\t\t'):
    return ',\n'.join(ind + ', '.join(str(v) for v in vals[i:i + n]) for i in range(0, len(vals), n))


def main():
    sf = tables(os.path.join(KIT, 'ak_sftab.inc'))
    gm = tables(os.path.join(KIT, 'ak_gmbank.inc'))
    conc = sf['ak_concave'][1]
    prog = gm['ak_gm_prog'][1]
    drum = gm['ak_gm_drum'][1]
    ins = gm['ak_gm_ins'][1]
    assert len(conc) == 128 and len(prog) == 128 and len(drum) == 94 and len(ins) % 24 == 0
    nins = len(ins) // 24
    assert nins == 67, nins
    assert (256 + 128 + 94 + len(ins)) % 4 == 2
    c = ['/* Généré par tools/audiotest/synthtab2c.py (d\'après ak_sftab.inc et ak_gmbank.inc) : ne pas',
         ' * modifier. Tables du synthé MIDI (ak_synth.c) : une structure, dans l\'ordre de l\'asm. */',
         '#include "ak_synth.h"',
         '',
         'const struct ak_gmtab ak_gmtab = {',
         '\t/* ak_concave : atténuation en centibels d\'une commande MIDI v (0..127) : -400 log10(v / 127) */',
         '\t{',
         rows(conc, 16),
         '\t},',
         '\t/* ak_gm_prog : programme -> instrument */',
         '\t{',
         rows(prog, 16),
         '\t},',
         '\t/* ak_gm_drum : touche 35..81 -> instrument, touche jouée */',
         '\t{']
    c.append(',\n'.join('\t\t{%d, %d}' % (drum[2 * i], drum[2 * i + 1]) for i in range(47)))
    c += ['\t},', '\t/* ak_gm_ins : instruments FM (AKFI_SIZE octets) */', '\t{']
    c.append(',\n'.join('\t\t{' + ', '.join(str(v) for v in ins[24 * i:24 * i + 24]) + '}' for i in range(nins)))
    c += ['\t},', '\t{0, 0},', '};', '']
    with open(os.path.join(KIT, 'ak_gmbank.c'), 'w', newline='', encoding='utf-8') as f:
        f.write('\n'.join(c))


if __name__ == '__main__':
    main()
