"""Les dictionnaires du systeme (docs/LANGUES.md) : assemble SD0:/res/lang/<xx>.txt depuis skel et les lang/<xx>.txt de
chaque programme de src/. build.ps1 l'appelle.

    python -I tools/lang/merge.py <dossier de sortie : .../res/lang>
"""
import glob, os, re, sys

CLE = re.compile(r"((?:[^=\\]|\\.)*)=")
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
for xx in ("en", "fr"):
    vus, lignes = {}, []
    for f in [os.path.join(ROOT, "skel", "sd0", "res", "lang", xx + ".txt")] + sorted(
            glob.glob(os.path.join(ROOT, "src", "*", "*", "lang", xx + ".txt"))):
        if os.sep + "fmtracker" + os.sep in f or not os.path.exists(f):
            continue                                    # (FM Tracker charge le sien)
        with open(f, encoding="utf-8") as h:
            for l in h.read().splitlines():
                m = CLE.match(l)
                if not l.strip() or l.startswith("#") or not m:
                    continue
                k = m.group(1).rstrip(" ")              # (comme lang_ajouter : une espace en tete compte)
                if k in vus and vus[k] != l:
                    print(f"lang : {xx} : deux traductions pour « {k} » ({f})")
                if k not in vus:
                    vus[k] = l
                    lignes.append(l)
    with open(os.path.join(out, xx + ".txt"), "w", encoding="utf-8", newline="\n") as h:
        h.write("# genere par tools/lang/merge.py\n" + "\n".join(lignes) + "\n")
    print(f"lang : {xx} : {len(lignes)} textes")
