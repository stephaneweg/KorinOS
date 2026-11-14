#!/usr/bin/env python3
# elf2dol.py -- un ELF PowerPC 32 bits gros-boutien (tools/emu/gchb) en DOL : ses segments chargeables, le code en
# sections de texte, le reste en donnees, la bss d'apres le dernier segment.
#     python3 -I tools/emu/gchb/elf2dol.py entree.elf sortie.dol
import struct
import sys


def main(src, dst):
    e = open(src, "rb").read()
    assert e[:4] == b"\x7fELF" and e[4] == 1 and e[5] == 2, "ELF 32 bits gros-boutien attendu"
    entry, phoff = struct.unpack(">II", e[24:32])
    phentsize, phnum = struct.unpack(">HH", e[42:46])
    text, data, bss_lo, bss_hi = [], [], None, None
    for i in range(phnum):
        p = phoff + i * phentsize
        typ, off, vaddr, _, filesz, memsz, flags, _ = struct.unpack(">8I", e[p:p + 32])
        if typ != 1 or memsz == 0:
            continue
        if filesz:
            (text if flags & 1 else data).append((vaddr, e[off:off + filesz]))
        if memsz > filesz:
            lo, hi = vaddr + filesz, vaddr + memsz
            bss_lo = lo if bss_lo is None else min(bss_lo, lo)
            bss_hi = hi if bss_hi is None else max(bss_hi, hi)
    assert len(text) <= 7 and len(data) <= 11
    hdr = bytearray(0x100)
    out = bytearray()
    pos = 0x100

    def put(slot, vaddr, blob):
        nonlocal pos
        struct.pack_into(">I", hdr, 0x00 + slot * 4, pos)
        struct.pack_into(">I", hdr, 0x48 + slot * 4, vaddr)
        n = (len(blob) + 31) & ~31
        struct.pack_into(">I", hdr, 0x90 + slot * 4, n)
        out.extend(blob + b"\0" * (n - len(blob)))
        pos += n

    for k, (v, b) in enumerate(text):
        put(k, v, b)
    for k, (v, b) in enumerate(data):
        put(7 + k, v, b)
    if bss_lo is not None:
        struct.pack_into(">II", hdr, 0xD8, bss_lo, bss_hi - bss_lo)
    struct.pack_into(">I", hdr, 0xE0, entry)
    open(dst, "wb").write(bytes(hdr) + bytes(out))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
