"""abi2import.py -- un kit écrit en C qui appelle un autre kit (UIKit importe AppKit) :

  python -I tools/kits/abi2import.py src/kits/appkit/appkit.abi OUTDIR [--min N]

Écrit dans OUTDIR :
  <kit>_import.h   un prototype par fonction (long, arguments d'après le commentaire du .abi, comme
                   abi2c.py), et KIT_<KIT>_FUNCTIONS
  <kit>_import.S   les relais, pour AArch64 et x86-64 (passé au préprocesseur) : chaque fonction saute
                   par la table <kit>__imports (.bss du kit, donc une par process) ; la première fois
                   (table vide), le relais garde les arguments, appelle <kit>__import_open, puis saute.
                   <kit>__import_open fait LIB_OPEN (nom, version minimale) et remplit la table avec
                   base + décalage de chaque fonction ; un kit absent ou trop ancien : la fonction
                   rend E_NOSYS (-38) sans rien appeler.

Aucune relocation : la table est remplie à l'exécution et les relais y accèdent relativement au compteur
ordinal. Un kit importé doit déjà être en mémoire (LIB_OPEN ne lit pas de fichier) : AppKit l'est toujours
(module de démarrage), UIKit l'est dès qu'une app l'a ouvert.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from abi2c import arity, parse_abi  # noqa: E402


def main():
    args = sys.argv[1:]
    vmin = 1
    if "--min" in args:
        i = args.index("--min")
        vmin = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        raise SystemExit(__doc__)
    abi, outdir = args
    kit, fns = parse_abi(abi)
    n = len(fns)
    os.makedirs(outdir, exist_ok=True)
    g = "KORIN_%s_IMPORT_H" % kit.upper()
    h = ["/* %s_import.h -- généré par tools/kits/abi2import.py depuis %s : ne pas modifier. */" % (kit, os.path.basename(abi)),
         "#ifndef " + g, "#define " + g, "",
         "#define KIT_%s_FUNCTIONS %d" % (kit.upper(), n), ""]
    for rank, (name, comment) in enumerate(fns):
        params = ", ".join("long a%d" % i for i in range(arity(comment))) or "void"
        if comment:
            h.append("/* %d : %s */" % (rank, comment.replace("*/", "* /")))
        h.append("long %s(%s);" % (name, params))
    h += ["", "#endif", ""]
    with open(os.path.join(outdir, kit + "_import.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(h))

    tab = "%s__imports" % kit
    opener = "%s__import_open" % kit
    s = ["/* %s_import.S -- généré par tools/kits/abi2import.py depuis %s : ne pas modifier. */" % (kit, os.path.basename(abi)),
         "#include \"korin_sys_nr.h\"",
         "\t.bss", "\t.p2align 3", "%s:" % tab, "\t.zero %d" % (8 * max(1, n)),
         "\t.section .rodata", "%s__name:" % kit, '\t.asciz "%s"' % kit, "\t.text", ""]
    s += ["#if defined(__aarch64__)",
          # opener : LIB_OPEN, puis la table ; garde tout sauf x16, x17
          "\t.p2align 2", "%s:" % opener,
          "\tstp x29, x30, [sp, -16]!", "\tmov x29, sp",
          "\tadrp x0, %s__name" % kit, "\tadd x0, x0, :lo12:%s__name" % kit,
          "\tmov x1, #%d" % vmin, "\tmov x8, #SYS_LIB_OPEN", "\tsvc 0",
          "\tcmp x0, #0", "\tb.lt 2f",
          "\tadrp x1, %s" % tab, "\tadd x1, x1, :lo12:%s" % tab,
          "\tmov x2, #0", "1:\tadd x3, x0, #8", "\tldr w4, [x3, x2, lsl #2]", "\tadd x4, x0, x4",
          "\tstr x4, [x1, x2, lsl #3]", "\tadd x2, x2, #1", "\tcmp x2, #%d" % n, "\tb.lo 1b",
          "2:\tldp x29, x30, [sp], 16", "\tret", ""]
    for rank, (name, _) in enumerate(fns):
        s += ["\t.globl %s" % name, "\t.hidden %s" % name, "\t.p2align 2", "%s:" % name,
              "\tadrp x16, %s" % tab, "\tadd x16, x16, :lo12:%s" % tab,
              "\tldr x17, [x16, #%d]" % (rank * 8), "\tcbz x17, 1f", "\tbr x17",
              "1:\tstp x29, x30, [sp, -96]!", "\tstp x0, x1, [sp, 16]", "\tstp x2, x3, [sp, 32]",
              "\tstp x4, x5, [sp, 48]", "\tstp x6, x7, [sp, 64]", "\tstp x8, x16, [sp, 80]",
              "\tbl %s" % opener,
              "\tldp x0, x1, [sp, 16]", "\tldp x2, x3, [sp, 32]", "\tldp x4, x5, [sp, 48]",
              "\tldp x6, x7, [sp, 64]", "\tldp x8, x16, [sp, 80]", "\tldp x29, x30, [sp], 96",
              "\tldr x17, [x16, #%d]" % (rank * 8), "\tcbz x17, 2f", "\tbr x17",
              "2:\tmov x0, #-38", "\tret", ""]
    s += ["#else",
          "\t.p2align 4", "%s:" % opener,
          "\tpush %rbx", "\tleaq %s__name(%%rip), %%rdi" % kit, "\tmovl $%d, %%esi" % vmin,
          "\tmovl $SYS_LIB_OPEN, %eax", "\tsyscall",
          "\ttestq %rax, %rax", "\tjs 2f",
          "\tleaq %s(%%rip), %%rbx" % tab, "\txorl %ecx, %ecx",
          "1:\tmovl 8(%rax,%rcx,4), %edx", "\taddq %rax, %rdx", "\tmovq %rdx, (%rbx,%rcx,8)",
          "\tincl %ecx", "\tcmpl $%d, %%ecx" % n, "\tjb 1b",
          "2:\tpop %rbx", "\tret", ""]
    for rank, (name, _) in enumerate(fns):
        s += ["\t.globl %s" % name, "\t.hidden %s" % name, "\t.p2align 4", "%s:" % name,
              "\tmovq %s+%d(%%rip), %%r11" % (tab, rank * 8), "\ttestq %r11, %r11", "\tjz 1f", "\tjmp *%r11",
              # garde les arguments (rdi rsi rdx rcx r8 r9) ; à l'entrée rsp = 8 mod 16 : 7 push l'alignent
              "1:\tpush %rdi", "\tpush %rsi", "\tpush %rdx", "\tpush %rcx", "\tpush %r8", "\tpush %r9", "\tpush %rax",
              "\tcall %s" % opener,
              "\tpop %rax", "\tpop %r9", "\tpop %r8", "\tpop %rcx", "\tpop %rdx", "\tpop %rsi", "\tpop %rdi",
              "\tmovq %s+%d(%%rip), %%r11" % (tab, rank * 8), "\ttestq %r11, %r11", "\tjz 2f", "\tjmp *%r11",
              "2:\tmovq $-38, %rax", "\tret", ""]
    s += ["#endif", ""]
    with open(os.path.join(outdir, kit + "_import.S"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(s))
    print("abi2import : %s, %d fonctions -> %s" % (kit, n, outdir))


if __name__ == "__main__":
    main()
