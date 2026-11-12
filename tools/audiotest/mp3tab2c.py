"""mp3tab2c.py - convertit src/kits/audiokit/ak_mp3tab.inc (tables du MP3 en
FASMARM, faites par gen_mp3tab.py) en src/kits/audiokit/ak_mp3tab.c (port
d'AudioKit en C).

    python -I tools/audiotest/mp3tab2c.py

FASMARM : db 8 bits, dh 16, dw 32, dd 64. Une étiquette dont toutes les lignes
ont la même taille donne un tableau de ce type (u8, u16, u32, u64 ; les
flottants restent en u32 : leurs bits) ; une étiquette qui mêle les tailles
(mp3e_hlist, mp3e_hdir : enregistrements dh + db) donne ses octets (u8, petit
boutiste). Les constantes (X = n) deviennent des #define. Écrit aussi les
déclarations (à recopier dans ak_mp3.h) sur la sortie standard.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'src', 'kits', 'audiokit', 'ak_mp3tab.inc')
DST = os.path.join(ROOT, 'src', 'kits', 'audiokit', 'ak_mp3tab.c')
SIZE = {'db': 1, 'dh': 2, 'dw': 4, 'dd': 8}
CTYPE = {1: 'u8', 2: 'u16', 4: 'u32', 8: 'u64'}


def val(s):
    s = s.strip()
    neg = s.startswith('-')
    if neg:
        s = s[1:]
    v = int(s, 16) if s.lower().startswith('0x') else int(s, 10)
    return -v if neg else v


def main():
    tabs = []    # (nom, [(taille, valeur)], commentaire)
    consts = []
    cur = None
    comment = []
    for line in open(SRC, encoding='latin-1'):
        code = line.split(';', 1)[0].rstrip()
        cm = line.split(';', 1)[1].strip() if ';' in line else ''
        if not code.strip():
            if cm and not cm.startswith('==') and 'ENGENDRE' not in cm and 'kits/audiokit' not in cm \
                    and '13818-3) ;' not in cm:
                comment.append(cm)
            continue
        m = re.match(r'^([A-Za-z_]\w*):\s*$', code)
        if m:
            cur = (m.group(1), [], ' '.join(comment))
            tabs.append(cur)
            comment = []
            continue
        m = re.match(r'^([A-Za-z_]\w*)\s*=\s*(\S+)$', code)
        if m:
            consts.append((m.group(1), val(m.group(2))))
            continue
        m = re.match(r'^\s+(db|dh|dw|dd)\s+(.*)$', code)
        if m:
            sz = SIZE[m.group(1)]
            for v in m.group(2).split(','):
                cur[1].append((sz, val(v)))
            continue
        if re.match(r'^\s+align\s+\d+$', code):
            continue
        sys.exit('ligne inconnue : ' + line)
    out = ['/* AudioKit : tables du décodeur et de l\'encodeur MP3 (ISO/IEC 11172-3, 13818-3).',
           ' * ENGENDRÉ par tools/audiotest/mp3tab2c.py d\'après ak_mp3tab.inc (lui-même fait par',
           ' * tools/audiotest/gen_mp3tab.py) : ne pas modifier. Flottants : leurs bits (u32). */',
           '#include "ak_mp3.h"', '']
    decl = []
    for name, vals, cm in tabs:
        sizes = {s for s, _ in vals}
        if len(sizes) == 1:
            sz = sizes.pop()
            items = [v & ((1 << (8 * sz)) - 1) for _, v in vals]
            hexa = sz >= 4 or name in ('mp3_huff', 'mp3_quadA')
        else:
            sz = 1
            items = []
            for s, v in vals:
                v &= (1 << (8 * s)) - 1
                items += [(v >> (8 * k)) & 0xFF for k in range(s)]
            hexa = False
            cm = (cm + ' ' if cm else '') + '(enregistrements mêlés : octets, petit boutiste)'
        t = CTYPE[sz]
        if cm:
            out.append('/* ' + cm + ' */')
        out.append('const %s %s[%d] = {' % (t, name, len(items)))
        per = {1: 16, 2: 12, 4: 8, 8: 4}[sz]
        for i in range(0, len(items), per):
            chunk = items[i:i + per]
            if hexa:
                w = 2 * sz
                s = ', '.join('0x%0*X' % (w, v) for v in chunk)
            else:
                s = ', '.join(str(v) for v in chunk)
            out.append('\t' + s + ',')
        out.append('};')
        out.append('')
        decl.append('extern const %s %s[%d];' % (t, name, len(items)))
    with open(DST, 'w', newline='', encoding='utf-8') as f:
        f.write('\n'.join(out))
    for n, v in consts:
        print('#define %s %d' % (n, v))
    print('\n'.join(decl))


if __name__ == '__main__':
    main()
