"""mkstubs.py -- des bouchons faibles (rendent 0) pour chaque fonction d'un <kit>_abi.h, pour essayer Relics sur l'hote
(tools/relics/run.sh) : host.c remplace par les siennes celles qui comptent.

    python3 -I tools/relics/mkstubs.py OUT.c uikit_abi.h audiokit_abi.h
"""
import re
import sys

out = sys.argv[1]
lines = ["/* genere par tools/relics/mkstubs.py */"]
for h in sys.argv[2:]:
    for l in open(h, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*long\s+(\w+)\s*\(([^)]*)\)\s*;", l)
        if m:
            lines.append("__attribute__ ((weak)) long %s (%s) { return 0; }" % (m.group(1), m.group(2)))
lines.append("__thread long uikit__ret[4];")
open(out, "w").write("\n".join(lines) + "\n")
