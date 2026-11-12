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

--keep-all : préfixes (« * » : toutes) des fonctions qui gardent x1-x18 et les drapeaux NZCV
(AppKit : file_sys remplace un svc 0 dans la macro sys de tout programme en assembleur, et
les appelants en assembleur comptent sur des registres que l'asm ne touchait pas). Relais
AArch64 qui sauve x1-x18 et NZCV (mrs / msr nzcv) ; la fonction peut revenir deux fois (fork :
le fils a une copie de la pile) ou ne pas revenir (exec).
--keep-x0 : noms de fonctions (déjà dans --keep-all) qui gardent aussi x0 (rien n'est rendu).
--x8-arg : noms de fonctions (déjà dans --keep-all) qui reçoivent x8 en 7e argument (x6) : le
numéro d'appel de file_sys. Sur x86-64, c'est le 7e argument de la convention System V.
--ret : « nom:registres » (déjà dans --keep-all), fonctions qui rendent plusieurs valeurs, comme
l'asm (UIKit : ui_w_event:012, x0 widget, x1 code, x2 pris ; ui_clip_put:03). La fonction C rend
une structure de quatre long (r0..r3, rendue en mémoire : x8 sur AArch64, premier argument caché
sur x86-64) ; le relais AArch64 charge les registres nommés depuis r0..r3 et garde les autres.
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


KEEP_ALL_AARCH64 = """\t.text
\t.p2align 2
__keepall_{n}:
\tstp x29, x30, [sp, -16]!
\tmov x29, sp
\tsub sp, sp, 160
\tstp x1, x2, [sp]
\tstp x3, x4, [sp, 16]
\tstp x5, x6, [sp, 32]
\tstp x7, x8, [sp, 48]
\tstp x9, x10, [sp, 64]
\tstp x11, x12, [sp, 80]
\tstp x13, x14, [sp, 96]
\tstp x15, x16, [sp, 112]
\tstp x17, x18, [sp, 128]
\tmrs x16, nzcv
\tstp x16, x0, [sp, 144]
{pre}\tbl {n}
\tldp x16, x1, [sp, 144]
\tmsr nzcv, x16
{post}\tldp x17, x18, [sp, 128]
\tldp x15, x16, [sp, 112]
\tldp x13, x14, [sp, 96]
\tldp x11, x12, [sp, 80]
\tldp x9, x10, [sp, 64]
\tldp x7, x8, [sp, 48]
\tldp x5, x6, [sp, 32]
\tldp x3, x4, [sp, 16]
\tldp x1, x2, [sp]
\tadd sp, sp, 160
\tldp x29, x30, [sp], 16
\tret
"""


def keep_all_ret(name, regs):
    """relais --keep-all d'une fonction qui rend les registres regs (chiffres) depuis r0..r3"""
    out = ["\t.text", "\t.p2align 2", "__keepall_%s:" % name,
           "\tstp x29, x30, [sp, -16]!", "\tmov x29, sp", "\tsub sp, sp, 192"]
    saved = list(range(1, 19))  # x1..x18 aux places du relais --keep-all
    for i in range(0, 18, 2):
        out.append("\tstp x%d, x%d, [sp, %d]" % (saved[i], saved[i + 1], i * 8))
    out += ["\tmrs x16, nzcv", "\tstp x16, x0, [sp, 144]", "\tadd x8, sp, 160", "\tbl %s" % name,
            "\tldr x16, [sp, 144]", "\tmsr nzcv, x16"]
    for r in range(1, 19):
        if str(r) not in regs:
            out.append("\tldr x%d, [sp, %d]" % (r, (r - 1) * 8))
    for r in range(0, 4):
        if str(r) in regs:
            out.append("\tldr x%d, [sp, %d]" % (r, 160 + 8 * r))
    out += ["\tadd sp, sp, 192", "\tldp x29, x30, [sp], 16", "\tret"]
    return "\n".join(out)


def main():
    args = [a for a in sys.argv[1:]]

    def opt(name):
        if name not in args:
            return []
        i = args.index(name)
        v = [p for p in args[i + 1].split(",") if p]
        del args[i:i + 2]
        return v

    keep = opt("--keep")
    keep_all = opt("--keep-all")
    keep_x0 = opt("--keep-x0")
    x8_arg = opt("--x8-arg")
    ret = dict(r.split(":", 1) for r in opt("--ret"))
    if len(args) != 2:
        raise SystemExit(__doc__)
    kit, fns = parse_abi(args[0])
    kept_all = [n for n, _ in fns if any(p == "*" or n.startswith(p) for p in keep_all)]
    kept = [n for n, _ in fns if n not in kept_all and any(n.startswith(p) for p in keep)]
    for n in keep_x0 + x8_arg + list(ret):
        if n not in kept_all:
            raise SystemExit("abi2table : %s doit être dans --keep-all" % n)
    for n, regs in ret.items():
        if not regs or "0" not in regs or any(c not in "0123" for c in regs) or n in keep_x0 + x8_arg:
            raise SystemExit("abi2table : --ret %s:%s (registres x0..x3, x0 compris)" % (n, regs))
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
        if name in kept_all:
            lines.append("#if defined(__aarch64__)")
            lines.append("\t.4byte __keepall_%s - kit_table" % name)
            lines.append("#else")
            lines.append("\t.4byte %s - kit_table" % name)
            lines.append("#endif")
        elif name in kept:
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
    if kept_all:
        lines.append("#if defined(__aarch64__)")
        lines.append("/* relais qui gardent x1-x18 et NZCV (--keep-all ; x0 aussi : --keep-x0) */")
        for name in kept_all:
            if name in ret:
                lines.append(keep_all_ret(name, ret[name]))
                continue
            pre ="\tmov x6, x8\n" if name in x8_arg else ""
            post = "\tmov x0, x1\n" if name in keep_x0 else ""
            lines.append(KEEP_ALL_AARCH64.format(n=name, pre=pre, post=post).rstrip("\n"))
        lines.append("#endif")
    lines.append("")
    with open(args[1], "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
