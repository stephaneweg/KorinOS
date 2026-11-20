"""craftlibs_patch : la table [patch."https://github.com/storytold/craft-libs"] d'une app Craft (tools/craft/build.sh,
docs/CRAFT.md).

Une app prend craft-libs à plusieurs révisions (une par caisse, souvent). Seules les révisions où l'app prend une
caisse que Korin change (les fichiers notés dans ~/korin-craft/craft-libs-<rev>/.korin-changed par korin_cfg.py,
korin_libs.py : platform, font-store…) sont redirigées vers leur copie patchée, et de
ces révisions toutes les caisses que l'app y prend : une caisse redirigée tire ses voisines par chemin, depuis sa
copie, et une voisine restée sur git serait une autre copie de mêmes types. Deux révisions redirigées dont les
caisses tirent la même voisine donneraient deux paquets « chemin » de même nom et version, que le Cargo.lock refuse :
build.sh le dit (« package collision »).

Pas les caisses que l'app a déjà en [patch.crates-io] (pas de patch en chaîne), ni celles qu'elle prend à plusieurs
révisions (un [patch] vaut pour toutes : il les ramènerait à une seule).

    python3 -I tools/craft/craftlibs_patch.py <dossier de l'app> <dossier de travail>
"""
import collections
import os
import sys
import tomllib

app, work = sys.argv[1], sys.argv[2]
lock = tomllib.load(open(os.path.join(app, "Cargo.lock"), "rb"))
ws = tomllib.load(open(os.path.join(app, "Cargo.toml"), "rb"))
skip = set(ws.get("patch", {}).get("crates-io", {}))
revs = collections.defaultdict(set)
for p in lock["package"]:
    if "craft-libs" in p.get("source", ""):
        revs[p["name"]].add(p["source"].split("#")[-1][:7])

# les dossiers des caisses dans chaque copie, et les fichiers que Korin y a changés
paths, changed = {}, {}
for rev in {r for v in revs.values() for r in v}:
    top = os.path.join(work, "craft-libs-" + rev)
    try:
        changed[rev] = [l.strip() for l in open(os.path.join(top, ".korin-changed"), encoding="utf-8") if l.strip()]
    except FileNotFoundError:
        changed[rev] = []
    for d, dirs, files in os.walk(top):
        dirs[:] = [x for x in dirs if x not in ("target", ".git")]
        if "Cargo.toml" in files:
            try:
                n = tomllib.load(open(os.path.join(d, "Cargo.toml"), "rb")).get("package", {}).get("name")
            except Exception:
                continue
            if n:
                paths[(n, rev)] = os.path.relpath(d, top)


def touched(name, rev):
    d = paths.get((name, rev))
    return d is not None and any(c == d or c.startswith(d + os.sep) or c.startswith(d + "/") for c in changed[rev])


needed = sorted({r for n, rs in revs.items() for r in rs if touched(n, r)})
print('[patch."https://github.com/storytold/craft-libs"]')
for n in sorted(set(revs) - skip):
    if len(revs[n]) != 1:
        continue
    rev = next(iter(revs[n]))
    if rev not in needed:
        continue
    if (n, rev) not in paths:
        sys.exit("craft : %s absent de craft-libs-%s" % (n, rev))
    print('%s = { path = "%s" }' % (n, os.path.join(work, "craft-libs-" + rev, paths[(n, rev)])))
print("# révisions redirigées : %s" % (", ".join(needed) or "aucune"), file=sys.stderr)
