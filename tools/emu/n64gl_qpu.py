"""n64gl_qpu : le dernier shader de fragment de la sortie de « n64gltest --qpu » -> un en-tete C (tools/emu/n64gl_qpu.cpp).
    python -I tools/emu/n64gl_qpu.py build/mesa/run.out build/emu/n64gl_qpu.h

Les uniforms de Mesa suivent l'ordre de declaration des vec4 actifs : uPrim, uEnv, uFog, uSize.
"""
import re
import sys

UNIF = re.compile(r"ldunif(?:rf\.\S+|a|arf\.\S+)?\s+\(([^)]*)\)|wrtmuc \(([^)]*)\)")
lines = open(sys.argv[1], encoding="utf-8", errors="replace").read().split("\n")
i = [n for n, l in enumerate(lines) if "MESA_SHADER_FRAGMENT prog" in l and l.rstrip().endswith("QPU:")][-1]
words, unifs = [], []
for l in lines[i + 1:]:
    if not l.strip():
        continue
    t = l[12:] if l.startswith("MESA: info: ") else l
    if not t.startswith("0x"):
        if words:
            break
        continue
    words.append(t[:18])
    unifs += [m.group(1) or m.group(2) for m in UNIF.finditer(t)]


def color(c):
    return ["F(%d / 255.0f)" % (c >> s & 255) for s in (24, 16, 8, 0)]


push = color(0x20406080) + color(0x10203040) + color(0xC0A08060) + ["F(0.25f)"] * 4
subst = {"tex[0].p0": "TEX0", "tex[0].p1": "0", "tex[1].p0": "TEX1", "tex[1].p1": "0"}
for n, v in enumerate(push):
    subst["push[%d]" % n] = v


def value(u):
    u = u.strip()
    m = re.match(r"0x([0-9a-f]+)", u)
    if m:
        return "0x" + m.group(1)
    k = re.sub(r"\s*\|.*", "", u)
    low = re.search(r"\|\s*(0x[0-9a-f]+)", u)
    if k not in subst:
        raise SystemExit("uniform inconnu : " + u)
    return subst[k] + (" | " + low.group(1) if low else "")


with open(sys.argv[2], "w", encoding="ascii") as o:
    o.write("// genere par n64gl_qpu.py depuis la sortie de n64gltest --qpu sous ARMPI\n")
    o.write("static const uint64_t fs_code[] = {\n" + "".join("\t%sull,\n" % w for w in words) + "};\n")
    o.write("static const uint32_t fs_unif[] = { %s };\n" % ", ".join(value(u) for u in unifs))
print(len(words), "instructions,", len(unifs), "uniforms")
