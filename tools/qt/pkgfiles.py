"""pkgfiles.py : les motifs « files = » des paquets <module>-dev de Qt (tools/pkg/packages-pc.ini), tirés des
install_manifest.txt de chaque module (tools/qt/build.sh). mkrepo.plan donne un fichier au premier paquet dont un
motif le prend : les modules autres que qtbase ont des motifs à eux (un dossier quand aucun autre module n'y met de
fichier, sinon les fichiers un par un), placés AVANT [qtbase-dev], qui prend le reste (devtools/qt/).

    python3 -I tools/qt/pkgfiles.py ~/korin-qt/x86_64 qtshadertools qtsvg qtdeclarative
"""
import fnmatch
import os
import sys

PREFIX = "devtools/qt/"


def manifest(out, m):
    stage = os.path.join(out, "stage") + "/"
    files = []
    for line in open(os.path.join(out, "obj", m, "install_manifest.txt"), encoding="utf-8"):
        f = line.strip()
        if f.startswith(stage):
            files.append(os.path.normpath(f[len(stage):]))
    return files


def main():
    out = os.path.expanduser(sys.argv[1])
    mods = sys.argv[2:]
    allm = {m: manifest(out, m) for m in ["qtbase"] + mods}
    owner = {}
    for m, fs in allm.items():
        for f in fs:
            owner.setdefault(f, set()).add(m)
    for m in mods:
        pats = []
        for f in allm[m]:
            parts = f[len(PREFIX):].split("/")
            pat = f
            # le dossier le moins profond (qml/, include/QtQuick/, lib/cmake/Qt6Quick/…) qui n'a que des fichiers de ce
            # module ; sinon le fichier lui-même
            for depth in range(1, len(parts)):
                d = PREFIX + "/".join(parts[:depth]) + "/"
                if not any(g.startswith(d) and (owner[g] - {m}) for g in owner):
                    pat = d
                    break
            if pat not in pats:
                pats.append(pat)
        # les fichiers d'un dossier partagé : regroupés en « préfixe* » quand aucun fichier d'un autre module n'y
        # répond (au moins la moitié du nom et 10 caractères : un futur module, qt5compat…, ne doit pas y tomber)
        others = [g for g in owner if owner[g] - {m}]
        out = []
        for pat in pats:
            if pat.endswith("/"):
                out.append(pat)
                continue
            d, name = pat.rsplit("/", 1)
            best = pat
            for k in range(len(name) - 1, max(10, len(name) // 2 + 2) - 1, -1):
                g = d + "/" + name[:k] + "*"
                if any(fnmatch.fnmatchcase(o, g) for o in others):
                    break
                best = g
            if best not in out:
                out.append(best)
        # (les fichiers, et les motifs plus étroits, déjà pris par un motif à préfixe plus large)
        globs = [q for q in out if q.endswith("*")]
        pats = [p for p in out if p.endswith("/")
                or not any(q != p and fnmatch.fnmatchcase(p.rstrip("*"), q) for q in globs)]
        pats.append(PREFIX + "share/doc/%s/" % m)	# (les licences : stage.sh)
        print("[%s-dev] %d motifs" % (m, len(pats)), file=sys.stderr)
        lines, cur = [], ""
        for p in pats:
            if cur and len(cur) + len(p) > 110:
                lines.append(cur)
                cur = ""
            cur = (cur + " " + p).strip()
        lines.append(cur)
        print("[%s-dev]\nfiles    = %s\n" % (m, "\n           ".join(lines)))


if __name__ == "__main__":
    main()
