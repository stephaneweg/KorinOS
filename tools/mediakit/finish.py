"""mediakit : verifie le kit lie (deux PT_LOAD, aucune relocation hors des
variables, seulement R_AARCH64_RELATIVE) et le met en ET_EXEC -> sortie."""
import struct
import sys

src, dst = sys.argv[1], sys.argv[2]
b = bytearray(open(src, "rb").read())
assert b[:4] == b"\x7fELF" and b[4] == 2
e_type, e_machine = struct.unpack_from("<HH", b, 16)
e_entry, e_phoff, e_shoff = struct.unpack_from("<QQQ", b, 24)
e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHHHH", b, 54)
loads = []
for i in range(e_phnum):
    p_type, p_flags, p_off, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
        struct.unpack_from("<IIQQQQQQ", b, e_phoff + i * e_phentsize)
    if p_type == 1:
        loads.append((p_flags, p_vaddr, p_filesz, p_memsz))
print("segments :", [(hex(f), hex(v), fs, ms) for f, v, fs, ms in loads])
assert len(loads) == 2, "il faut deux PT_LOAD"
vars_lo = loads[1][1]
vars_hi = vars_lo + loads[1][3]
# sections : .rela.dyn
shstr_off = struct.unpack_from("<Q", b, e_shoff + e_shstrndx * e_shentsize + 24)[0]
n_rel = 0
for i in range(e_shnum):
    sh_name, sh_type, sh_flags, sh_addr, sh_off, sh_size = struct.unpack_from("<IIQQQQ", b, e_shoff + i * e_shentsize)
    name = b[shstr_off + sh_name:b.index(0, shstr_off + sh_name)].decode()
    if sh_type == 4:  # SHT_RELA
        for j in range(sh_size // 24):
            off, info, add = struct.unpack_from("<QQq", b, sh_off + j * 24)
            assert (info & 0xFFFFFFFF) == 1027, "relocation %d dans %s" % (info & 0xFFFFFFFF, name)
            assert vars_lo <= off < vars_hi, "relocation hors des variables : %#x" % off
            n_rel += 1
print("relocations R_AARCH64_RELATIVE :", n_rel, "(toutes dans les variables)")
struct.pack_into("<H", b, 16, 2)  # ET_EXEC
open(dst, "wb").write(b)
print("ecrit", dst, len(b), "octets")
