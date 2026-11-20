#!/usr/bin/env python3
"""nedump.py -- décrit un exécutable NE (Windows 3.x) : en-tête, segments, modules importés, relocations.

    python3 -I tools/win16/nedump.py prog.exe [--relocs]

Sert à voir ce qu'un programme Win16 demande à win16 (docs/etude/WIN16.md)."""
import struct
import sys


def main():
    path = sys.argv[1]
    show_relocs = '--relocs' in sys.argv
    d = open(path, 'rb').read()
    if d[:2] != b'MZ':
        sys.exit('pas un MZ')
    ne = struct.unpack_from('<I', d, 0x3c)[0]
    if d[ne:ne + 2] != b'NE':
        sys.exit('pas un NE')
    h = struct.unpack_from('<2sBBHHIHHHHHHHHHHHHHHHHHBB', d, ne)
    (_, linkver, linkrev, enttab, entlen, crc, flags, autodata, heap, stack, ip, cs, sp, ss, nseg, nmod, nresnam,
     segtab, rsrctab, resnam, modref, impnam, nrestab_lo, nrestab_hi, nmovent, align, nrsrc, exetyp) = (
        h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7], h[8], h[9], h[10], h[11], h[12], h[13], h[14], h[15], h[16],
        h[17], h[18], h[19], h[20], h[21], h[22] & 0xffff, 0, 0, 0, 0, 0)
    # (relu champ par champ : la mise en page de l'en-tête NE)
    o = ne
    enttab, entlen = struct.unpack_from('<HH', d, o + 4)
    flags, autodata, heap, stack = struct.unpack_from('<HHHH', d, o + 0x0c)
    ip, cs, sp, ss = struct.unpack_from('<HHHH', d, o + 0x14)
    nseg, nmod, nrestab = struct.unpack_from('<HHH', d, o + 0x1c)
    segtab, rsrctab, resnam, modref, impnam = struct.unpack_from('<HHHHH', d, o + 0x22)
    nrestab_off, nmovent, align, nres = struct.unpack_from('<IHHH', d, o + 0x2c)
    exetyp = d[o + 0x36]
    expver = struct.unpack_from('<H', d, o + 0x3e)[0]
    print(f'NE : drapeaux {flags:04x}, données auto {autodata}, tas {heap}, pile {stack}, CS:IP {cs}:{ip:04x}, '
          f'SS:SP {ss}:{sp:04x}, système {exetyp}, version attendue {expver >> 8}.{expver & 0xff:02d}, '
          f'alignement {1 << align}')
    mods = []
    for i in range(nmod):
        off = struct.unpack_from('<H', d, o + modref + 2 * i)[0]
        n = d[o + impnam + off]
        mods.append(d[o + impnam + off + 1:o + impnam + off + 1 + n].decode())
    print('modules importés :', ', '.join(mods))
    imports = {}
    for s in range(nseg):
        sector, length, sflags, minalloc = struct.unpack_from('<HHHH', d, o + segtab + 8 * s)
        pos = sector << align
        length = length or 0x10000 if sector else length
        kind = 'DATA' if sflags & 1 else 'CODE'
        print(f'segment {s + 1} : {kind}, fichier {pos:#x}, {length} octets, mémoire {minalloc or 65536}, '
              f'drapeaux {sflags:04x}')
        if sflags & 0x100 and sector:
            r = pos + (length if length else 0x10000)
            count = struct.unpack_from('<H', d, r)[0]
            for k in range(count):
                atype, rtype, off, a, b = struct.unpack_from('<BBHHH', d, r + 2 + 8 * k)
                t = rtype & 3
                if t == 1:
                    what = f'{mods[a - 1]}.{b}'
                    imports.setdefault(mods[a - 1], set()).add(b)
                elif t == 2:
                    n = d[o + impnam + b]
                    nm = d[o + impnam + b + 1:o + impnam + b + 1 + n].decode()
                    what = f'{mods[a - 1]}.{nm}'
                    imports.setdefault(mods[a - 1], set()).add(nm)
                elif t == 0:
                    what = f'interne seg {a} : {b:#x}' if a != 0xff else f'interne entrée {b}'
                else:
                    what = f'correction OS {a}'
                if show_relocs:
                    print(f'   reloc {k} : source {atype} à {off:#06x}, {what}{" (additif)" if rtype & 4 else ""}')
    for m, s in imports.items():
        print(f'{m} : {", ".join(str(x) for x in sorted(s, key=str))}')


if __name__ == '__main__':
    main()
