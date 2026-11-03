"""n64link : pose l'ELF de FASMARM dans une edition de liens C, sans relocations (tools/emu/build-n64.sh).

Chaque PT_LOAD de n64emu.elf devient une section a son adresse, dans un PT_LOAD a elle ; n64_asm_start est gl_start,
juste avant le point d'entree. Le script de liens est posix.ld avec ces segments en plus.
    n64link.py n64emu.elf sortie.S base.specs sortie.ld sortie.specs
"""
import os
import struct
import sys

elf, out_s, specs_in, out_ld, out_specs = sys.argv[1:6]
d = open(elf, "rb").read()
entry = struct.unpack_from("<Q", d, 24)[0]
phoff = struct.unpack_from("<Q", d, 32)[0]
phentsize, phnum = struct.unpack_from("<HH", d, 54)
segs = []
for i in range(phnum):
    t, fl, off, va, _, fsz, msz, _ = struct.unpack_from("<IIQQQQQQ", d, phoff + i * phentsize)
    if t == 1:
        segs.append((fl, off, va, fsz, msz))

asm = [".globl n64_asm_start", ".set n64_asm_start,0x%x" % (entry - 8)]
phdrs, secs = [], []
for n, (fl, off, va, fsz, msz) in enumerate(segs):
    kind = "ax" if fl & 1 else "aw"
    asm += ['.section .n64s%d,"%s"' % (n, kind), '.incbin "%s",%d,%d' % (elf, off, fsz)]
    if msz > fsz:
        asm += [".zero %d" % (msz - fsz)]          # (la donnee de l'emulateur reste petite : 500 Ko)
    phdrs.append("\tn64p%d PT_LOAD FLAGS (%d);\n" % (n, 5 if fl & 1 else 6))
    secs.append("\t.n64s%d 0x%x : { KEEP(*(.n64s%d)) } :n64p%d\n" % (n, va, n, n))
open(out_s, "w").write("\n".join(asm) + "\n")

specs = open(specs_in).read()
ld = None
for word in specs.split():
    if word.endswith("posix.ld"):
        ld = word
s = open(ld).read()
a = "\teh PT_GNU_EH_FRAME;\n"
assert s.count(a) == 1
s = s.replace(a, a + "".join(phdrs))
a = "\t/DISCARD/ :"
assert s.count(a) == 1
s = s.replace(a, "".join(secs) + a)
open(out_ld, "w").write(s)
open(out_specs, "w").write(specs.replace(ld, out_ld))
