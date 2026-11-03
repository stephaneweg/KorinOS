"""abi2c.py -- l'interface C d'un kit d'ARMPI, generee depuis son .abi (docs/KITS.md, « en-tete C ») :

  python -I tools/kits/abi2c.py src/kits/uikit/uikit.abi OUT [--const fichier.inc ...]

ecrit dans le dossier OUT :
  <kit>_abi.h   un prototype par ligne kit_fn, dans l'ordre de la table (rang = ligne, a partir de 0) :
                les fonctions du kit suivent l'AAPCS64 et ne prennent que des entiers, des pointeurs
                et des poignees : chaque argument est un « long » (ou un pointeur converti en long),
                leur nombre lu dans le commentaire de la ligne (x0..x7, w0..w7, « x0..x3 ») ; le
                resultat (x0) est un long. Le commentaire du .abi accompagne chaque prototype.
                Plus, si --const : les constantes numeriques des .inc (NOM = valeur) en #define.
  <kit>_abi.S   les relais : chaque fonction saute a la table du kit (x16, comme les relais de
                kit_use des programmes en assembleur) ; <kit>_kit_open () la remplit
                (armpi_kit_open de <armpi/kit.h>, libposix) -> 1 si le kit est la.
Le fichier .abi est la seule source : un kit qui grandit (lignes ajoutees a la fin) regenere ces
fichiers sans rien d'autre a changer ; un en-tete ecrit a la main (<kit>.h) les inclut et ajoute
les structures. Sans dependance : python standard.
"""
import os
import re
import sys


def parse_abi(path):
    kit = None
    fns = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"\s*kit_begin\s+(\w+)\s*,\s*'(\w+)'", line)
            if m:
                kit = m.group(2)
                continue
            m = re.match(r"\s*kit_fn\s+(\w+)\s*(?:;(.*))?$", line.rstrip("\n"))
            if m:
                fns.append([m.group(1), (m.group(2) or "").strip()])
            elif fns and re.match(r"\s*;\s+\S", line) and not line.lstrip().startswith("; Table"):
                # (ligne de suite du commentaire de la fonction precedente)
                if re.match(r"^\s+;", line):
                    fns[-1][1] += " " + line.strip().lstrip(";").strip()
    if kit is None:
        sys.exit("abi2c : pas de kit_begin dans " + path)
    return kit, fns


def arity(comment):
    """nombre d'arguments d'apres le commentaire : le plus haut registre cite avant « -> »"""
    args = comment.split("->")[0]
    n = 0
    for m in re.finditer(r"\b[xw]([0-7])(?:\s*\.\.\s*[xw]?([0-7]))?\b", args):
        hi = int(m.group(2) or m.group(1))
        n = max(n, hi + 1)
    return n


def consts(paths):
    out = []
    seen = set()
    for p in paths:
        with open(p, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = re.match(r"^([A-Z][A-Z0-9_]*)\s*=\s*([^;]+?)\s*(?:;.*)?$", line.rstrip("\n"))
                if not m:
                    continue
                name, val = m.group(1), m.group(2).strip()
                v = re.sub(r"\bshl\b", "<<", val)
                v = re.sub(r"\bshr\b", ">>", v)
                v = re.sub(r"\bor\b", "|", v)
                v = re.sub(r"\band\b", "&", v)
                v = re.sub(r"\bnot\b", "~", v)
                # seulement des nombres, des noms deja definis et des operateurs simples
                toks = re.findall(r"[A-Za-z_]\w*", re.sub(r"\b0x[0-9A-Fa-f]+\b|\b\d+\b", " ", v))
                ok = all(t in seen for t in toks)
                if not re.fullmatch(r"[\w\s()<>|&~+\-*/x]+", v) or not ok:
                    continue
                if name in seen:
                    continue
                seen.add(name)
                out.append((name, v, os.path.basename(p)))
    return out


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    abi, outdir = args[0], args[1]
    incs = []
    if "--const" in args:
        incs = args[args.index("--const") + 1:]
    kit, fns = parse_abi(abi)
    os.makedirs(outdir, exist_ok=True)
    guard = "_ARMPI_%s_ABI_H" % kit.upper()
    h = ["/* %s_abi.h -- genere par tools/kits/abi2c.py depuis %s : ne pas editer. */" % (kit, os.path.basename(abi)),
         "#ifndef " + guard, "#define " + guard, "", "#ifdef __cplusplus", 'extern "C" {', "#endif", "",
         "/* la table du kit mappee et les relais remplis -> 1 ; 0 : %s absent (SD0:/lib/%s.so) */" % (kit, kit),
         "int %s_kit_open (void);" % kit,
         "#define %s_KIT_FUNCTIONS %d" % (kit.upper(), len(fns)), ""]
    for rank, (name, comment) in enumerate(fns):
        n = arity(comment)
        params = ", ".join("long a%d" % i for i in range(n)) or "void"
        if comment:
            h.append("/* %d : %s */" % (rank, comment.replace("*/", "* /")))
        h.append("long %s (%s);" % (name, params))
    if incs:
        h += ["", "/* constantes (%s) */" % ", ".join(os.path.basename(p) for p in incs)]
        for name, v, src in consts(incs):
            h.append("#ifndef %s\n#define %s (%s)\n#endif" % (name, name, v))
    h += ["", "#ifdef __cplusplus", "}", "#endif", "", "#endif", ""]
    with open(os.path.join(outdir, kit + "_abi.h"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(h))
    s = ["/* %s_abi.S -- genere par tools/kits/abi2c.py depuis %s : ne pas editer. */" % (kit, os.path.basename(abi)),
         "\t.text", "\t.p2align 2"]
    for rank, (name, _) in enumerate(fns):
        s += ["\t.globl %s" % name, "\t.type %s, %%function" % name, "%s:" % name,
              "\tadrp x16, %s__imports + %d" % (kit, rank * 8), "\tldr x16, [x16, #:lo12:%s__imports + %d]" % (kit, rank * 8),
              "\tbr x16", "\t.size %s, . - %s" % (name, name)]
    s += ["", "\t.bss", "\t.p2align 3", "\t.globl %s__imports" % kit, "%s__imports:" % kit,
          "\t.zero %d" % (8 * max(1, len(fns))), ""]
    with open(os.path.join(outdir, kit + "_abi.S"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(s))
    c = ["/* %s_abi_open.c -- genere par tools/kits/abi2c.py depuis %s : ne pas editer. */" % (kit, os.path.basename(abi)),
         "#include <armpi/kit.h>", "extern void *%s__imports[%d];" % (kit, max(1, len(fns))),
         "int %s_kit_open (void)" % kit, "{",
         '\tvoid *k = armpi_kit_open ("%s", 1);' % kit,
         "\tif (k == 0)", "\t\treturn 0;",
         "\tfor (unsigned i = 0; i < %d; i++)" % len(fns),
         "\t\t%s__imports[i] = armpi_kit_fn (k, i);" % kit,
         "\treturn 1;", "}", ""]
    with open(os.path.join(outdir, kit + "_abi_open.c"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(c))
    print("abi2c : %s, %d fonctions -> %s" % (kit, len(fns), outdir))


if __name__ == "__main__":
    main()
