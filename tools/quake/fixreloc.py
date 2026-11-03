"""FASMARM ecrit ses relocations de 64 bits sous le numero de x86-64 (1) : ici, R_AARCH64_ABS64 (257).

    python3 -I tools/quake/fixreloc.py objet.o
"""
import struct
import sys

SHT_RELA = 4
R_AARCH64_ABS64 = 257

path = sys.argv[1]
b = bytearray(open(path, 'rb').read())
shoff = struct.unpack_from('<Q', b, 0x28)[0]
shentsize, shnum = struct.unpack_from('<HH', b, 0x3A)
n = 0
for i in range(shnum):
    sh = shoff + i * shentsize
    kind = struct.unpack_from('<I', b, sh + 4)[0]
    if kind != SHT_RELA:
        continue
    off, size = struct.unpack_from('<QQ', b, sh + 0x18)
    for r in range(off, off + size, 24):
        info = struct.unpack_from('<Q', b, r + 8)[0]
        if info & 0xFFFFFFFF == 1:
            struct.pack_into('<Q', b, r + 8, (info >> 32) << 32 | R_AARCH64_ABS64)
            n += 1
open(path, 'wb').write(b)
print('fixreloc :', n, 'relocations')
