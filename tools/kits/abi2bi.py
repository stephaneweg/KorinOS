"""abi2bi.py -- l'interface FreeBASIC d'un kit d'ARMPI, generee depuis son .abi (docs/GCC.md) :

  python -I tools/kits/abi2bi.py src/kits/uikit/uikit.abi OUT [--const fichier.inc ...]

ecrit OUT/<kit>_abi.bi : #inclib "<kit>" (l'edition de liens prend /gcc/lib/lib<kit>.a toute seule), une
declaration par ligne kit_fn, et les constantes des .inc. Comme dans <kit>_abi.h : chaque argument
et le resultat sont des entiers de 64 bits, un pointeur se passe converti (cast (longint, p)).

Quand le commentaire renvoie a une autre fonction (« comme ui_text », « idem »), le nombre
d'arguments ne se lit pas : la declaration en prend 8, tous facultatifs, et l'AAPCS64 fait le reste.

FreeBASIC ne distingue pas les majuscules : une constante du nom d'une fonction du kit (UI_FONT et
ui_font) n'est pas ecrite. Partage parse_abi, arity et consts avec abi2c.py.
"""
import importlib.util
import os
import re
import sys


spec = importlib.util.spec_from_file_location("abi2c", os.path.join(os.path.dirname(os.path.abspath(__file__)), "abi2c.py"))
abi2c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi2c)


def basic_expr(v):
    """une valeur de abi2c.consts (notation C) en notation FreeBASIC"""
    v = re.sub(r"\b0x([0-9A-Fa-f]+)\b", r"&h\1", v)
    return v.replace("<<", " shl ").replace(">>", " shr ").replace("|", " or ").replace("&&", "&").replace("~", " not ")


def basic_and(v):
    # (le « & » de la notation C, sans toucher aux &h)
    return re.sub(r"&(?!h)", " and ", v)


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    abi, outdir = args[0], args[1]
    incs = args[args.index("--const") + 1:] if "--const" in args else []
    kit, fns = abi2c.parse_abi(abi)
    os.makedirs(outdir, exist_ok=True)
    b = ["' %s_abi.bi -- genere par tools/kits/abi2bi.py depuis %s : ne pas editer." % (kit, os.path.basename(abi)),
         "#pragma once", '#inclib "%s"' % kit, "", 'extern "C"', "",
         "' la table du kit mappee et les relais remplis -> 1 ; 0 : %s absent (SD0:/lib/%s.so)" % (kit, kit),
         "declare function %s_kit_open () as long" % kit, ""]
    names = {kit + "_kit_open"}
    for rank, (name, comment) in enumerate(fns):
        names.add(name.lower())
        n = abi2c.arity(comment)
        if comment:
            b.append("' %d : %s" % (rank, comment))
        if re.search(r"\b(comme|idem)\b", comment, re.I):
            params = ", ".join("byval a%d as longint = 0" % i for i in range(8))
        else:
            params = ", ".join("byval a%d as longint" % i for i in range(n))
        b.append("declare function %s (%s) as longint" % (name, params))
    b += ["", "end extern", ""]
    b.append("const %s_KIT_FUNCTIONS = %d" % (kit.upper(), len(fns)))
    skipped = []
    defined = set()
    for name, v, src in abi2c.consts(incs):
        if name.lower() in names:
            skipped.append(name)
            continue
        defined.add(name.lower())
        toks = re.findall(r"[A-Za-z_]\w*", re.sub(r"\b0x[0-9A-Fa-f]+\b", " ", v))
        if any(t.lower() not in defined for t in toks):
            skipped.append(name)		# (elle s'appuie sur une constante ecartee)
            defined.discard(name.lower())
            continue
        b.append("const %s as longint = %s" % (name, basic_and(basic_expr(v))))
    b.append("")
    with open(os.path.join(outdir, kit + "_abi.bi"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(b))
    print("abi2bi : %s, %d fonctions -> %s%s" % (kit, len(fns), outdir, " (ecartees : " + " ".join(skipped) + ")" if skipped else ""))


if __name__ == "__main__":
    main()
