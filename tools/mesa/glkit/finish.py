"""finish.py -- GlKit : verifie l'objet partage lie par build-glkit.sh, le met en kit d'ARMPI
(ET_EXEC, que le noyau accepte) et ecrit les sources du cote programme (libglkit.a).

    python3 finish.py glkit.elf glkit.so dossier-des-sources-generees

Verifie : deux PT_LOAD (code a 0, puis variables), aucune relocation dans le code (il est partage
par tous les process), seulement les relocations que glkit.c sait faire.
Ecrit :
  glkit_imports.c  ce que le kit prend au programme (libc, libstdc++, libgcc), par nom :
                   glkit_imports_fill() remplit la table que glkit_init lit (adresses, et
                   decalages TLS depuis TPIDR_EL0 pour les variables par thread de la libc)
  glkit_stubs.S    un relais par fonction exportee (gl*, armgl_*...) : saute par glkit_slots,
                   rempli par glkit_load au premier appel (glkit_lazy)
"""
import struct
import sys

R_OK = {257: "ABS64", 1025: "GLOB_DAT", 1026: "JUMP_SLOT", 1027: "RELATIVE", 1030: "TLS_TPREL64",
        1031: "TLSDESC"}
# Korin PC (x86-64, tools/mesa/build-glkit-pc.sh) : les relocations que glkit.c sait faire sur x86-64
R_OK_X64 = {1: "ABS64", 6: "GLOB_DAT", 7: "JUMP_SLOT", 8: "RELATIVE", 16: "DTPMOD64", 17: "DTPOFF64",
            18: "TLS_TPREL64", 36: "TLSDESC", 37: "IRELATIVE"}

src, dst, gen = sys.argv[1], sys.argv[2], sys.argv[3]
b = bytearray(open(src, "rb").read())
assert b[:4] == b"\x7fELF" and b[4] == 2, "pas un ELF64"
X64 = struct.unpack_from("<H", b, 18)[0] == 62     # EM_X86_64
if X64:
    R_OK = R_OK_X64
e_phoff, e_shoff = struct.unpack_from("<QQ", b, 32)
e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHHHH", b, 54)

loads, tls = [], None
for i in range(e_phnum):
    p_type, p_flags, p_off, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
        struct.unpack_from("<IIQQQQQQ", b, e_phoff + i * e_phentsize)
    if p_type == 1:
        loads.append((p_flags, p_vaddr, p_filesz, p_memsz))
    elif p_type == 7:
        tls = (p_memsz, p_align)
assert len(loads) == 2, "il faut deux PT_LOAD : %r" % loads
assert loads[0][1] == 0 and loads[0][0] & 2 == 0, "le code d'abord, a 0"
vars_lo, vars_hi = loads[1][1], loads[1][1] + loads[1][3]
print("glkit : code %d Kio, variables %d Kio, TLS %r" % (loads[0][3] >> 10, (vars_hi - vars_lo) >> 10, tls))

secs = {}
shstr_off = struct.unpack_from("<Q", b, e_shoff + e_shstrndx * e_shentsize + 24)[0]
for i in range(e_shnum):
    sh_name, sh_type, sh_flags, sh_addr, sh_off, sh_size = struct.unpack_from("<IIQQQQ", b, e_shoff + i * e_shentsize)
    name = b[shstr_off + sh_name:b.index(0, shstr_off + sh_name)].decode()
    secs[name] = (sh_type, sh_off, sh_size)

counts = {}
for name, (t, off, size) in secs.items():
    if t != 4:  # SHT_RELA
        continue
    for j in range(size // 24):
        r_off, info, add = struct.unpack_from("<QQq", b, off + j * 24)
        rt = info & 0xFFFFFFFF
        assert rt in R_OK, "relocation %d dans %s" % (rt, name)
        assert vars_lo <= r_off < vars_hi, "relocation %s dans le code : %#x" % (R_OK[rt], r_off)
        counts[R_OK[rt]] = counts.get(R_OK[rt], 0) + 1
print("glkit : relocations", counts)

# symboles dynamiques
_, sym_off, sym_size = secs[".dynsym"]
_, str_off, _ = secs[".dynstr"]
imports, exports = [], []
for j in range(1, sym_size // 24):
    st_name, st_info, st_other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", b, sym_off + j * 24)
    name = b[str_off + st_name:b.index(0, str_off + st_name)].decode()
    bind, typ = st_info >> 4, st_info & 15
    if st_shndx == 0:
        if name:
            imports.append((name, bind == 2, typ == 6))
    elif typ == 2 and not name.startswith("glkit_"):
        exports.append(name)
imports.sort()
exports.sort()
print("glkit : %d symboles pris au programme, %d fonctions exportees" % (len(imports), len(exports)))

with open(gen + "/glkit_imports.c", "w", newline="\n") as f:
    f.write("/* glkit_imports.c -- genere par tools/mesa/glkit/finish.py : ce que GlKit prend au programme */\n")
    f.write("#include <stdint.h>\n\nstruct glkit_import { const char *name; uintptr_t value; long kind; };\n\n")
    for name, weak, is_tls in imports:
        w = " __attribute__ ((weak))" if weak else ""
        f.write("extern %schar %s[]%s;\n" % ("__thread " if is_tls else "", name, w))
    f.write("\nconst long glkit_import_count = %d;\n\n" % len(imports))
    f.write("long glkit_imports_fill (struct glkit_import *t)\n{\n\tchar *tp = __builtin_thread_pointer ();\n")
    for i, (name, weak, is_tls) in enumerate(imports):
        if is_tls:
            f.write("\tt[%d] = (struct glkit_import) { \"%s\", (uintptr_t) (%s - tp), 1 };\n" % (i, name, name))
        else:
            f.write("\tt[%d] = (struct glkit_import) { \"%s\", (uintptr_t) %s, 0 };\n" % (i, name, name))
    f.write("\treturn %d;\n}\n" % len(imports))

with open(gen + "/glkit_stubs.S", "w", newline="\n") as f:
    f.write("/* glkit_stubs.S -- genere par tools/mesa/glkit/finish.py : un relais par fonction de GlKit */\n")
    f.write("\t.text\n")
    for i, name in enumerate(exports):
        f.write("\t.global %s\n\t.type %s, %%function\n\t.balign 4\n%s:\n" % (name, name, name))
        f.write("\tmov\tw17, %d\n\tadrp\tx16, glkit_slots + %d\n\tldr\tx16, [x16, :lo12:glkit_slots + %d]\n\tbr\tx16\n"
                % (i, 8 * i, 8 * i))
    f.write("\n\t.data\n\t.balign 8\n\t.global glkit_slots\nglkit_slots:\n")
    for name in exports:
        f.write("\t.quad\tglkit_lazy\n")
    f.write("\n\t.section .rodata\n\t.balign 8\n\t.global glkit_names\nglkit_names:\n")
    for i in range(len(exports)):
        f.write("\t.quad\t.Ln%d\n" % i)
    f.write("\t.global glkit_count\nglkit_count:\n\t.quad\t%d\n" % len(exports))
    for i, name in enumerate(exports):
        f.write(".Ln%d:\t.asciz\t\"%s\"\n" % (i, name))

if X64:
    # (le PC, a la place du precedent : r11 = rang, saut par glkit_slots ; host/glkit_lazy_x86_64.S)
    with open(gen + "/glkit_stubs.S", "w", newline="\n") as f:
        f.write("/* glkit_stubs.S -- genere par tools/mesa/glkit/finish.py : un relais par fonction de GlKit (x86-64) */\n")
        f.write("\t.text\n")
        for i, name in enumerate(exports):
            f.write("\t.globl %s\n\t.type %s, @function\n\t.p2align 4\n%s:\n" % (name, name, name))
            f.write("\tmovl\t$%d, %%r11d\n\tjmpq\t*glkit_slots+%d(%%rip)\n" % (i, 8 * i))
        f.write("\n\t.data\n\t.p2align 3\n\t.globl glkit_slots\nglkit_slots:\n")
        for name in exports:
            f.write("\t.quad\tglkit_lazy\n")
        f.write("\n\t.section .rodata\n\t.p2align 3\n\t.globl glkit_names\nglkit_names:\n")
        for i in range(len(exports)):
            f.write("\t.quad\t.Ln%d\n" % i)
        f.write("\t.globl glkit_count\nglkit_count:\n\t.quad\t%d\n" % len(exports))
        for i, name in enumerate(exports):
            f.write(".Ln%d:\t.asciz\t\"%s\"\n" % (i, name))
        f.write("\t.section .note.GNU-stack,\"\",@progbits\n")

with open(gen + "/tls.txt", "w") as f:
    f.write("%d %d\n" % (tls if tls else (0, 1)))

struct.pack_into("<H", b, 16, 2)  # ET_EXEC : ce que le noyau accepte
open(dst, "wb").write(b)
print("glkit : ecrit", dst, len(b), "octets")
