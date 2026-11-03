"""abi2cs.py -- la liaison C# d'un kit d'ARMPI, generee depuis son .abi (docs/DOTNET.md) :

  python -I tools/kits/abi2cs.py src/kits/uikit/uikit.abi OUT.cs Classe Espace [--const fichier.inc ...]

ecrit une classe statique publique, dans cet espace de noms : une methode par ligne kit_fn, dans
l'ordre de la table, des constantes (les .inc), et l'ouverture du kit dans le constructeur statique
(le programme n'a rien a ouvrir). Chaque argument est un long, comme dans abi2c.py. Une fonction qui
rend plusieurs registres (« -> x0 = ..., x1 = ... ») a des parametres out, par armpi_call : le C#
ne lit que x0 d'un appel direct.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import abi2c


def results(comment):
    """nombre de registres rendus, d'apres le commentaire apres « -> »"""
    if "->" not in comment:
        return 1
    after = comment.split("->", 1)[1]
    regs = [int(m.group(1)) for m in re.finditer(r"\bx([0-3])\s*=", after)]
    return max(regs) + 1 if regs else 1


CS_WORDS = {"lock", "event", "params", "object", "string", "ref", "out", "in", "is", "as", "base", "this"}


def main():
    args = sys.argv[1:]
    if len(args) < 4:
        sys.exit(__doc__)
    abi, out, cls, space = args[0], args[1], args[2], args[3]
    incs = args[args.index("--const") + 1:] if "--const" in args else []
    kit, fns = abi2c.parse_abi(abi)
    w = ["// genere par tools/kits/abi2cs.py depuis %s : ne pas editer." % os.path.basename(abi),
         "#nullable disable", "using System.Kits;", "", "namespace %s;" % space, "",
         "public static unsafe class %s" % cls, "{",
         "\tstatic readonly nint[] s_fn = KitLoader.Open(\"%s\", %d);" % (kit, len(fns)), ""]
    seen = set()
    for name, v, _ in abi2c.consts(incs):
        if name not in seen:
            seen.add(name)
            w.append("\tpublic const long %s = unchecked((long) (%s));" % (name, v))
    w.append("")
    for rank, (name, comment) in enumerate(fns):
        n = abi2c.arity(re.sub(r"\([^)]*\)", "", comment))	# (une fonction de rappel a ses registres entre parentheses)
        r = results(comment)
        if comment:
            w.append("\t/// <summary>%s</summary>" % comment.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))
        params = ["long a%d" % i for i in range(n)]
        call = ["a%d" % i for i in range(n)]
        if r == 1:
            types = ",".join(["long"] * (n + 1))
            w.append("\tpublic static long %s (%s) => ((delegate* unmanaged<%s>) s_fn[%d]) (%s);" %
                     (name, ", ".join(params), types, rank, ", ".join(call)))
        else:
            outs = ["out long r%d" % i for i in range(r)]
            w.append("\tpublic static void %s (%s)" % (name, ", ".join(params + outs)))
            w.append("\t{")
            w.append("\t\tlong* a = stackalloc long[8]; long* o = stackalloc long[4];")
            for i in range(n):
                w.append("\t\ta[%d] = a%d;" % (i, i))
            w.append("\t\tKitLoader.Call (s_fn[%d], a, o);" % rank)
            for i in range(r):
                w.append("\t\tr%d = o[%d];" % (i, i))
            w.append("\t}")
    w += ["}", ""]
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(w))
    print("abi2cs : %s, %d fonctions -> %s" % (kit, len(fns), out))


if __name__ == "__main__":
    main()
