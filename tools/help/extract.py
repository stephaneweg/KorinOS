"""Les exemples complets du guide du developpeur (src/apps/help/data/api/*.md) : chaque bloc ```c ou ```cpp qui a
une fonction main devient un fichier a compiler (tools/help/check.sh les compile et les lie comme pibuild).

    python3 -I tools/help/extract.py <dossier de sortie> <page.md>...

Un bloc dont la premiere ligne est « // fichier : nom.h » (ou nom.c, nom.cpp) est ecrit sous ce nom a cote des
exemples de sa page : un exemple en plusieurs fichiers les inclut. Ecrit un fichier par exemple :
<page>-<n>.c|cpp, et liste.txt (une ligne par exemple : chemin, page, ligne du bloc).
"""
import os, re, sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
FENCE = re.compile(r"^\s*(```|~~~)\s*([A-Za-z+]*)")
liste = []
for page in sys.argv[2:]:
    nom = os.path.splitext(os.path.basename(page))[0]
    lignes = open(page, encoding="utf-8").read().splitlines()
    i, n = 0, 0
    while i < len(lignes):
        m = FENCE.match(lignes[i])
        if not m:
            i += 1
            continue
        lang, debut, ind = m.group(2).lower(), i + 1, len(lignes[i]) - len(lignes[i].lstrip())
        j = i + 1
        while j < len(lignes) and not lignes[j].strip().startswith(m.group(1)):
            j += 1
        code = "\n".join(l[ind:] if l[:ind].strip() == "" else l for l in lignes[i + 1:j]) + "\n"
        i = j + 1
        if lang not in ("c", "cpp", "c++"):
            continue
        dossier = os.path.join(out, nom)
        os.makedirs(dossier, exist_ok=True)
        f = re.match(r"\s*//\s*fichier\s*:\s*(\S+)", code.splitlines()[0] if code.strip() else "")
        if f:
            open(os.path.join(dossier, f.group(1)), "w", encoding="utf-8").write(code)
            if not re.search(r"\bmain\s*\(", code):
                continue
        if not re.search(r"\bint\s+main\s*\(", code):
            continue
        n += 1
        ext = "c" if lang == "c" else "cpp"
        chemin = os.path.join(dossier, "%s-%d.%s" % (nom, n, ext))
        open(chemin, "w", encoding="utf-8").write(code)
        liste.append("%s\t%s\t%d" % (chemin, page, debut))
open(os.path.join(out, "liste.txt"), "w", encoding="utf-8").write("\n".join(liste) + ("\n" if liste else ""))
print("exemples :", len(liste))
