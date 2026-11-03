"""abi2bas.py -- ce que le BASIC d'Onyx lit d'un kit (#import, docs/BASIC.md), genere depuis son .abi :

  python -I tools/kits/abi2bas.py src/kits/audiokit/audiokit.abi SORTIE [--head fichier]

ecrit SORTIE/<kit>.bi, le format de tools/kitbi/kitbi.py d'Onyx : une ligne par fonction,
  <nom> <place> <resultat> <arguments ou -> <nom C> <noms des arguments>
Le .abi ne dit que le nombre d'arguments (abi2c.py) : chacun prend le type s de BASIC (une chaine, ou un
nombre : un pointeur, une poignee), le resultat un nombre de 64 bits (l), ou s quand le commentaire
annonce un texte. Le prefixe commun des noms est retire (ak_open : AudioKit.open), le nom C reste accepte.
--head : les fonctions du nom d'Onyx faites par le runtime (src/lib/basic/bi/*.head, src/apps/basic/baskits.h) :
recopiees d'abord ; celles du kit suivent, a partir de la place d'apres, sous leur seul nom C.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from abi2c import arity, parse_abi  # noqa: E402


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    abi, outdir = args[0], args[1]
    head = args[args.index("--head") + 1] if "--head" in args else None
    kit, fns = parse_abi(abi)
    os.makedirs(outdir, exist_ok=True)
    lines = ["# %s.bi -- %s pour le BASIC (#import %s) : genere par tools/kits/abi2bas.py depuis %s ; ne pas editer."
             % (kit, kit, kit, os.path.basename(abi))]
    first = 0
    body = []
    if head:
        with open(head, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\r\n")
                body.append(line)
                w = line.split()
                if len(w) > 3 and not line.startswith(("#", "struct", "field")):
                    first = max(first, int(w[1]) + 1)
    prefix = os.path.commonprefix([n for n, _ in fns]) if len(fns) > 1 else ""
    prefix = prefix[:prefix.rfind("_") + 1] if "_" in prefix else ""
    lines.append("kit %s %d" % (kit, first + len(fns)))
    lines += body
    n = 0
    for rank, (name, comment) in enumerate(fns):
        k = n if re.match(r"\s*idem\b", comment) else arity(comment)
        n = k
        ret = "s" if re.search(r"->\s*(x0 = )?(texte|son nom|ce qu|\xab)", comment) else "l"
        short = name if head or not prefix else name[len(prefix):]
        names = ",".join("a%d" % i for i in range(k))
        lines.append(" ".join(x for x in (short, str(first + rank), ret, "s" * k or "-", name, names) if x))
    with open(os.path.join(outdir, kit + ".bi"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
