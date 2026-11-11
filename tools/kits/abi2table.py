"""abi2table.py -- la table d'exportation d'un kit écrit en C, générée depuis son .abi :

  python -I tools/kits/abi2table.py src/kits/buskit/buskit.abi OUT.S

Écrit un fichier assembleur GNU, le même pour AArch64 et x86-64 (section .text.kit_table,
placée au début du code par src/kits/common/kit.ld) :

  kit_table:  .4byte 0x54494B41   « AKIT »
              .4byte N            nombre de fonctions (la version du kit)
              .4byte f - kit_table   pour chaque kit_fn, dans l'ordre du .abi

C'est la table que kit_begin / kit_fn d'src/inc/kit.inc produisent pour un kit en assembleur
(dw = 32 bits dans FASMARM). Le .abi reste la seule source : on ajoute à la fin, on ne retire,
ne déplace ni ne renomme jamais une ligne.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from abi2c import parse_abi  # noqa: E402


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    kit, fns = parse_abi(sys.argv[1])
    lines = [
        "/* Généré par tools/kits/abi2table.py depuis %s : ne pas modifier. */" % os.path.basename(sys.argv[1]),
        '\t.section .text.kit_table,"ax"',
        "\t.p2align 2",
        "\t.globl kit_table",
        "kit_table:",
        "\t.4byte 0x54494B41",
        "\t.4byte %d" % len(fns),
    ]
    for name, _ in fns:
        lines.append("\t.4byte %s - kit_table" % name)
    lines.append("")
    with open(sys.argv[2], "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
