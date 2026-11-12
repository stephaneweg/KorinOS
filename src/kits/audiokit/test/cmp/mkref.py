"""mkref.py - référence en asm pour le banc différentiel d'AudioKit (port en C).

Assemble les codecs en asm (src/kits/audiokit/ak_codecs.inc, inchangés) en un binaire
plat (FASMARM, org 0) précédé d'une table des décalages de toutes leurs étiquettes de
premier niveau ; écrit aussi akref_syms.h (REF_<étiquette> = rang dans la table).

    python -I src/kits/audiokit/test/cmp/mkref.py      (depuis la racine du dépôt)
    -> build/audiokit_cmp/akref.bin, akref_syms.h
"""
import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..'))
KIT = os.path.join(ROOT, 'src', 'kits', 'audiokit')
OUT = os.path.join(ROOT, 'build', 'audiokit_cmp')
FASM = os.path.join(ROOT, 'tools', 'fasmarm', 'fasmarm.exe')
NL = '\n'
TAB = '\t'

# hôte : fonctions que les codecs appellent hors de ak_codecs.inc
HOST = ['ak_alloc', 'ak_free', 'ak_open', 'ak_close', 'ak_write', 'ak_pending', 'ak_volume',
        'ak_dec_open', 'ak_fm_live', 'h_dbg']


def labels():
    inc = open(os.path.join(KIT, 'ak_codecs.inc'), encoding='latin-1').read()
    files = re.findall(r"^include\s+'(ak_\w+\.inc)'", inc, re.M)
    names = []
    seen = set()
    # inclusions imbriquées (tables) : à la fin, pour garder les rangs des autres
    nested = []
    for f in files:
        for line in open(os.path.join(KIT, f), encoding='latin-1'):
            m = re.match(r"^include\s+'(ak_\w+\.inc)'", line)
            if m and m.group(1) not in files:
                nested.append(m.group(1))
    files = files + nested
    for f in files:
        for line in open(os.path.join(KIT, f), encoding='latin-1'):
            m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*):', line) or \
                re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s+(db|dh|dw|dd|du|rb|rh|rw|rd|file)\b', line)
            if m and m.group(1) not in seen:
                seen.add(m.group(1))
                names.append(m.group(1))
    return files, names


def rel(p):
    return os.path.relpath(p, OUT).replace(os.sep, '/')


def main():
    os.makedirs(OUT, exist_ok=True)
    files, names = labels()
    src = ['format binary', 'processor cpu64_v8 +cpu64_fp +cpu64_simd', 'code64', 'org 0',
           'AKHOST = 1', "include '" + rel(os.path.join(ROOT, 'src', 'inc')) + "/armpi.inc'",
           'ref_table:', TAB + 'dw ' + str(len(names)) + ', 0']
    for n in names:
        src.append(TAB + 'dw ' + n)
    src += [TAB + 'align 8', 'ref_host:']
    for h in HOST:
        src.append(TAB + 'dd 0' + TAB + '; ' + h)
    for i, h in enumerate(HOST):
        src.append(h + ':')
        src.append(TAB + 'adr x16,ref_host')
        src.append(TAB + 'ldr x16,[x16,' + str(i * 8) + ']')
        src.append(TAB + 'br x16')
    src.append("include '" + rel(KIT) + "/ak_codecs.inc'")
    src += [TAB + 'align 8', 'ref_end:']
    with open(os.path.join(OUT, 'akref.asm'), 'w', newline='') as f:
        f.write(NL.join(src) + NL)
    off = (8 + 4 * len(names) + 7) & ~7
    h = ['/* Généré par mkref.py : rangs des étiquettes de la référence en asm. */',
         '#define REF_COUNT ' + str(len(names)), '#define REF_HOSTS ' + str(len(HOST)),
         '#define REF_HOST_OFF ' + str(off)]
    for i, n in enumerate(names):
        h.append('#define REF_' + n + ' ' + str(i))
    for i, n in enumerate(HOST):
        h.append('#define REFH_' + n + ' ' + str(i))
    with open(os.path.join(OUT, 'akref_syms.h'), 'w', newline='', encoding='utf-8') as f:
        f.write(NL.join(h) + NL)
    r = subprocess.run([FASM, 'akref.asm', 'akref.bin'], cwd=OUT, capture_output=True, text=True)
    sys.stdout.write(r.stdout[-3000:])
    sys.stderr.write(r.stderr[-2000:])
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
