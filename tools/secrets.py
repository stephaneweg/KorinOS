"""secrets.py -- remplit les marques @NOM@ d'un fichier construit avec des valeurs gardées hors du dépôt
(docs/OAUTH.md, « Les fournisseurs livrés avec Korin »).

  python3 -I tools/secrets.py FICHIER...

Les valeurs : un fichier « NOM = valeur » par ligne (# : commentaire), hors du dépôt, jamais commité :
  KORIN_SECRETS (variable d'environnement), sinon ../korin-secrets.ini (à côté du dépôt, comme la clé des paquets).
Le fichier est réécrit sur place (une copie construite, jamais une source de skel/). Une marque sans valeur reste
telle quelle (oauthd la prend pour une valeur absente) et est signalée ; sans fichier de valeurs, rien ne change.
Les valeurs ne sont jamais affichées.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MARK = re.compile(r"@([A-Z][A-Z0-9_]*)@")


def values():
    path = os.environ.get("KORIN_SECRETS") or os.path.join(os.path.dirname(ROOT), "korin-secrets.ini")
    v = {}
    if not os.path.isfile(path):
        return v, path
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        k, val = line.split("=", 1)
        v[k.strip()] = val.strip()
    return v, path


def main():
    files = sys.argv[1:]
    if not files:
        sys.exit(__doc__)
    v, path = values()
    for f in files:
        if not os.path.isfile(f):
            continue
        if os.path.abspath(f).startswith(os.path.join(ROOT, "skel") + os.sep):
            sys.exit("secrets.py : %s est une source (skel/) : seulement une copie construite" % f)
        text = open(f, encoding="utf-8", newline="").read()
        missing = sorted(set(m for m in MARK.findall(text) if m not in v))
        out = MARK.sub(lambda m: v.get(m.group(1), m.group(0)), text)
        if out != text:
            open(f, "w", encoding="utf-8", newline="").write(out)
        if missing:
            print("secrets.py : %s : sans valeur (%s) : %s" % (f, path, ", ".join(missing)), file=sys.stderr)


if __name__ == "__main__":
    main()
