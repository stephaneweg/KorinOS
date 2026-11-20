#!/usr/bin/env python3
"""expand.py -- décompresse les fichiers de COMPRESS.EXE de Microsoft (format SZDD, « FICHIER.EX_ ») des
disquettes de l'époque, comme EXPAND.EXE et LZEXPAND.DLL de Windows 3.x.

    python3 -I tools/win16/expand.py FICHIER.EX_ [...]     -> FICHIER.EXE à côté (dernière lettre tirée de l'en-tête)

LZSS : fenêtre de 4096 octets remplie d'espaces, écriture à 4096 - 16 ; un octet de drapeaux pour 8 éléments
(bit à 1 : un octet tel quel ; à 0 : deux octets, position sur 12 bits, longueur sur 4 bits + 3)."""
import os
import struct
import sys

MAGIC = b'SZDD\x88\xf0\x27\x33'


def expand(data):
    if data[:8] != MAGIC:
        raise ValueError('pas un fichier SZDD')
    missing = chr(data[9]) if data[9] else ''
    size = struct.unpack_from('<I', data, 10)[0]
    win = bytearray(b' ' * 4096)
    pos = 4096 - 16
    out = bytearray()
    i = 14
    while i < len(data) and len(out) < size:
        flags = data[i]
        i += 1
        for bit in range(8):
            if i >= len(data) or len(out) >= size:
                break
            if flags & (1 << bit):
                c = data[i]
                i += 1
                out.append(c)
                win[pos] = c
                pos = (pos + 1) & 4095
            else:
                if i + 1 >= len(data):
                    break
                b0, b1 = data[i], data[i + 1]
                i += 2
                off = b0 | (b1 & 0xf0) << 4
                n = (b1 & 0x0f) + 3
                for k in range(n):
                    c = win[(off + k) & 4095]
                    out.append(c)
                    win[pos] = c
                    pos = (pos + 1) & 4095
    return bytes(out[:size]), missing


def main():
    for p in sys.argv[1:]:
        data = open(p, 'rb').read()
        try:
            out, missing = expand(data)
        except ValueError as e:
            print(f'{p} : {e}')
            continue
        dst = p[:-1] + (missing.upper() if p[-2:-1].isupper() else missing) if p.endswith('_') else p + '.out'
        open(dst, 'wb').write(out)
        print(f'{os.path.basename(p)} -> {os.path.basename(dst)} ({len(out)} octets)')


if __name__ == '__main__':
    main()
