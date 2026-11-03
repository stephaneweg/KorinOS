"""qpucheck : le code QPU produit par Mesa dans ARMPI, execute sur le PC par qpusim d'Onyx (docs/MESA.md).

Lit la sortie de « gltest --dry --qpu » (build/mesa/run.out), ecrit les mots QPU et les uniforms de chaque
shader dans un en-tete C.
    python -I tools/mesa/qpucheck.py build/mesa/run.out build/mesa/qpu_shaders.h
"""
import re
import sys

UNIF = re.compile(r"ldunif(?:rf\.\S+|a|arf\.\S+)?\s+\(([^)]*)\)|wrtmuc \(([^)]*)\)")


def shader(lines, name, which):
    idx = [i for i, l in enumerate(lines) if name + " prog" in l and l.rstrip().endswith("QPU:")]
    i = idx[which]
    words, unifs = [], []
    for l in lines[i + 1:]:
        if not l.strip():               # (telnet : une ligne vide apres chaque ligne)
            continue
        t = l[12:] if l.startswith("MESA: info: ") else l
        if not t.startswith("0x"):
            if words:
                break
            continue
        words.append(t[:18])
        for m in UNIF.finditer(t):
            unifs.append(m.group(1) or m.group(2))
    return words, unifs


def unif_value(u, subst):
    u = u.strip()
    m = re.match(r"0x([0-9a-f]+)", u)
    if m:
        return "0x" + m.group(1)
    k = re.sub(r"\s*\|.*", "", u)
    low = re.search(r"\|\s*(0x[0-9a-f]+)", u)
    v = subst.get(k)
    if v is None:
        raise SystemExit("uniform inconnu : " + u)
    return v + (" | " + low.group(1) if low else "")


def emit(o, name, words, unifs, subst):
    o.write("static const uint64_t %s_code[] = {\n" % name)
    for w in words:
        o.write("\t%sull,\n" % w)
    o.write("};\nstatic const uint32_t %s_unif[] = { %s };\n" % (name, ", ".join(unif_value(u, subst) for u in unifs) or "0"))


def main():
    lines = open(sys.argv[1], encoding="utf-8").read().split("\n")
    vp = {"vp_x_scale": "F(12800.0f)", "vp_y_scale": "F(-12800.0f)", "vp_z_scale": "F(0.5f)", "vp_z_offset": "F(0.5f)",
          "vp_wclip?": "0"}
    with open(sys.argv[2], "w", encoding="ascii") as o:
        o.write("// genere par qpucheck.py depuis la sortie de gltest sous ARMPI\n")
        w, u = shader(lines, "MESA_SHADER_FRAGMENT", 0)
        emit(o, "fs1", w, u, {})
        w, u = shader(lines, "MESA_SHADER_VERTEX", 0)
        emit(o, "vs1", w, u, dict(vp, **{"push[0]": "F(GAIN)"}))
        w, u = shader(lines, "MESA_SHADER_FRAGMENT", -1)     # (entre les deux : le blit de glGenerateMipmap)
        emit(o, "fs2", w, u, {"push[0]": "F(1.0f)", "push[1]": "F(0.5f)", "push[2]": "F(0.25f)", "push[3]": "F(1.0f)",
                              "tex[0].p0": "TEX0", "tex[0].p1": "0"})


main()
