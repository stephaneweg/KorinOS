"""mkmp3.py - fichiers MP3 du banc différentiel (cmp_mp3.c), écrits par
tools/audiotest/mp3fuzz.py : trames aléatoires mais valides (MPEG-1 44,1 kHz,
MPEG-2 22,05 kHz, MPEG-2.5 8 kHz ; blocs longs, de début et de fin, courts,
mixtes ; scfsi, pretab, M/S, intensité MPEG-1 et MPEG-2, linbits,
quadruplets A et B, réservoir de bits).

    python -I src/kits/audiokit/test/cmp/mkmp3.py      (depuis la racine du dépôt)
    -> build/audiokit_cmp/mp3/fz_*.mp3
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
FUZZ = os.path.join(ROOT, 'tools', 'audiotest', 'mp3fuzz.py')
TAB = os.path.join(ROOT, 'src', 'kits', 'audiokit', 'ak_mp3tab.inc')
OUT = os.path.join(ROOT, 'build', 'audiokit_cmp', 'mp3')

# (nom, genre, graine, trames, options FUZZ)
CASES = [
    ('m1', 'mpeg1', 1, 60, ''),
    ('m1long', 'mpeg1', 2, 40, 'long'),
    ('m1short', 'mpeg1', 3, 40, 'short'),
    ('m1mixed', 'mpeg1', 4, 40, 'mixed'),
    ('m1ms', 'mpeg1', 5, 40, 'ms'),
    ('m1is', 'mpeg1', 6, 40, 'is'),
    ('m1isshort', 'mpeg1', 7, 40, 'is,short'),
    ('m1ismixed', 'mpeg1', 8, 30, 'is,mixed'),
    ('m1nost', 'mpeg1', 9, 30, 'nostereo,startstop'),
    ('m1noresv', 'mpeg1', 10, 30, 'noresv,legal'),
    ('lsf', 'lsf', 11, 80, ''),
    ('lsfis', 'lsf', 12, 60, 'is'),
    ('lsfisshort', 'lsf', 13, 60, 'is,short'),
    ('lsfismixed', 'lsf', 14, 60, 'is,mixed'),
    ('lsfms', 'lsf', 15, 60, 'ms'),
    ('l25', 'lsf25', 16, 80, ''),
    ('l25is', 'lsf25', 17, 60, 'is'),
    ('l25mixed', 'lsf25', 18, 60, 'mixed'),
    ('l25short', 'lsf25', 19, 60, 'is,short'),
]


def inc_tables():
    """étiquette -> valeurs (ak_mp3tab.inc ; une seule taille par étiquette)"""
    tabs = {}
    cur = None
    for line in open(TAB, encoding='latin-1'):
        code = line.split(';', 1)[0].rstrip()
        m = re.match(r'^([A-Za-z_]\w*):\s*$', code)
        if m:
            cur = tabs.setdefault(m.group(1), [])
            continue
        m = re.match(r'^\s+(db|dh|dw|dd)\s+(.*)$', code)
        if m:
            cur += [int(v, 0) for v in m.group(2).split(',')]
    return tabs


def fuzz_tables():
    """les tables que mp3fuzz.load_tables tire des sources de FFmpeg (absentes
    ici), refaites d'après l'arbre de décodage d'ak_mp3tab.inc"""
    t = inc_tables()
    huff, hdir = t['mp3_huff'], t['mp3_huffdir']
    tables = {}
    for i in range(32):
        start, bits, lb = hdir[4 * i:4 * i + 3]
        if not bits:
            continue
        codes = {}

        def walk(base, nb, prefix, plen):
            for idx in range(1 << nb):
                e = huff[base + idx]
                if e & 0x8000:
                    walk(start + (e & 0x7FF), (e >> 11) & 15, (prefix << nb) | idx, plen + nb)
                else:
                    ln = (e >> 8) & 15
                    codes[e & 0xFF] = ((prefix << ln) | (idx >> (nb - ln)), plen + ln)
        walk(start, bits, 0, 0)
        tables[i] = (codes, lb)
    q = t['mp3e_quad']
    qc, qb = q[0::2], q[1::2]
    sl, ss = t['mp3_sfb_long'], t['mp3_sfb_short']
    bl, bs = [], []
    for k in range(9):
        a = sl[24 * k:24 * k + 23]
        bl += [a[j + 1] - a[j] for j in range(22)]
        a = ss[16 * k:16 * k + 14]
        bs += [a[j + 1] - a[j] for j in range(13)]
    return tables, qc, qb, bl, bs


def main():
    os.makedirs(OUT, exist_ok=True)
    tabs = fuzz_tables()
    src = open(FUZZ, encoding='utf-8').read()
    src = src.replace('TABLES, QC, QB, BL, BS = load_tables()', 'TABLES, QC, QB, BL, BS = _TABS')
    src = src.replace("G.carray(open(os.path.join(G.FF, 'mpegaudiodec_common.c'), encoding='utf-8').read(), "
                      "'ff_lsf_nsf_table')", '_NSF')
    nsf = inc_tables()['mp3_nsf']
    sys.path.insert(0, os.path.dirname(FUZZ))
    code = compile(src, FUZZ, 'exec')
    for name, kind, seed, n, opts in CASES:
        os.environ['FUZZ'] = opts
        out = os.path.join(OUT, 'fz_' + name + '.mp3')
        sys.argv = [FUZZ, out, kind, str(seed), str(n)]
        exec(code, {'__name__': '__main__', '__file__': FUZZ, '_TABS': tabs, '_NSF': nsf})
        print(name, os.path.getsize(out))


if __name__ == '__main__':
    main()
