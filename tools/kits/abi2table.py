"""abi2table.py -- la table d'exportation d'un kit écrit en C, générée depuis son .abi :

  python -I tools/kits/abi2table.py src/kits/buskit/buskit.abi OUT.S [--keep net_,wifi_]

Écrit un fichier assembleur GNU (passé au préprocesseur : .S), le même pour AArch64 et x86-64
(section .text.kit_table, placée au début du code par src/kits/common/kit.ld) :

  kit_table:  .4byte 0x54494B41   « AKIT »
              .4byte N            nombre de fonctions (la version du kit)
              .4byte f - kit_table   pour chaque kit_fn, dans l'ordre du .abi

C'est la table que kit_begin / kit_fn d'src/inc/kit.inc produisent pour un kit en assembleur
(dw = 32 bits dans FASMARM). Le .abi reste la seule source : on ajoute à la fin, on ne retire,
ne déplace ni ne renomme jamais une ligne.

--keep : préfixes des fonctions qui, comme les anciens appels système qu'elles remplacent,
gardent tous les registres sauf x0 (NetKit : net_*, wifi_*). Sur AArch64, la table pointe sur
un relais qui sauve x1-x15 et x18 autour de la fonction C (comme la macro keep_regs de
l'assembleur) ; sur x86-64, rien de plus (la convention System V des programmes C suffit).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from abi2c import parse_abi  # noqa: E402

KEEP_AARCH64 = """\t.text
\t.p2align 2
__keep_{n}:
\tstp x29, x30, [sp, -16]!
\tmov x29, sp
\tstp x1, x2, [sp, -16]!
\tstp x3, x4, [sp, -16]!
\tstp x5, x6, [sp, -16]!
\tstp x7, x8, [sp, -16]!
\tstp x9, x10, [sp, -16]!
\tstp x11, x12, [sp, -16]!
\tstp x13, x14, [sp, -16]!
\tstp x15, x18, [sp, -16]!
\tbl {n}
\tldp x15, x18, [sp], 16
\tldp x13, x14, [sp], 16
\tldp x11, x12, [sp], 16
\tldp x9, x10, [sp], 16
\tldp x7, x8, [sp], 16
\tldp x5, x6, [sp], 16
\tldp x3, x4, [sp], 16
\tldp x1, x2, [sp], 16
\tldp x29, x30, [sp], 16
\tret
"""


def main():
    args = [a for a in sys.argv[1:]]
    keep = []
    if "--keep" in args:
        i = args.index("--keep")
        keep = [p for p in args[i + 1].split(",") if p]
        del args[i:i + 2]
    if len(args) != 2:
        raise SystemExit(__doc__)
    kit, fns = parse_abi(args[0])
    kept = [n for n, _ in fns if any(n.startswith(p) for p in keep)]
    lines = [
        "/* Généré par tools/kits/abi2table.py depuis %s : ne pas modifier. */" % os.path.basename(args[0]),
        '\t.section .text.kit_table,"ax"',
        "\t.p2align 2",
        "\t.globl kit_table",
        "kit_table:",
        "\t.4byte 0x54494B41",
        "\t.4byte %d" % len(fns),
    ]
    for name, _ in fns:
        if name in kept:
            lines.append("#if defined(__aarch64__)")
            lines.append("\t.4byte __keep_%s - kit_table" % name)
            lines.append("#else")
            lines.append("\t.4byte %s - kit_table" % name)
            lines.append("#endif")
        else:
            lines.append("\t.4byte %s - kit_table" % name)
    if kept:
        lines.append("#if defined(__aarch64__)")
        lines.append("/* relais qui gardent x1-x15 et x18 (--keep) */")
        for name in kept:
            lines.append(KEEP_AARCH64.format(n=name).rstrip("\n"))
        lines.append("#endif")
    lines.append("")
    with open(args[1], "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
