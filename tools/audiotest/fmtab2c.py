"""fmtab2c.py -- les tables de la synthese FM en C (src/kits/audiokit/ak_fmtab.c),
recopiees de src/kits/audiokit/ak_fmtab.inc (produit par gen_fmtab.py) : memes valeurs.

  python -I tools/audiotest/fmtab2c.py src/kits/audiokit/ak_fmtab.inc src/kits/audiokit/ak_fmtab.c
"""
import re
import sys

TYPES = {  # etiquette -> (type C, mot FASMARM, bits, signe)
    "ak_sin1024": ("s16", "dh", 16, True),
    "ak_exp256": ("u16", "dh", 16, False),
    "ak_cents1200": ("u32", "dw", 32, False),
}

tabs = {}
cur = None
with open(sys.argv[1], encoding="utf-8") as f:
    for line in f:
        line = line.split(";")[0].strip()
        if not line:
            continue
        m = re.match(r"^(\w+):$", line)
        if m:
            cur = m.group(1)
            tabs[cur] = []
            continue
        if line.startswith("align"):
            continue
        kw, vals = line.split(None, 1)
        assert kw == TYPES[cur][1], (cur, kw)
        tabs[cur] += [int(v) for v in vals.split(",")]

out = ["/* AudioKit en C : tables de la synthèse FM (générées par tools/audiotest/fmtab2c.py",
       " * depuis ak_fmtab.inc : ne pas modifier). */",
       '#include "ak_fm.h"', ""]
for name, (ct, kw, bits, signed) in TYPES.items():
    vals = tabs[name]
    m = (1 << bits) - 1
    vals = [v & m for v in vals]
    if signed:
        vals = [v - (1 << bits) if v >> (bits - 1) else v for v in vals]
    out.append("const %s %s[%d] = {" % (ct, name, len(vals)))
    per = 16 if bits == 16 else 8
    for i in range(0, len(vals), per):
        out.append("\t" + ", ".join(str(v) + ("u" if ct == "u32" else "") for v in vals[i:i + per]) + ",")
    out.append("};")
    out.append("")
with open(sys.argv[2], "w", encoding="utf-8", newline="") as f:
    f.write("\n".join(out))
