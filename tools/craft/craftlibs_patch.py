"""craftlibs_patch : la table [patch."https://github.com/storytold/craft-libs"] d'une app Craft (tools/craft/build.sh,
docs/CRAFT.md). Chaque caisse de craft-libs que le Cargo.lock de l'app prend à une seule révision va à la copie
patchée de cette révision (~/korin-craft/craft-libs-<rev>) ; celles qu'il prend à plusieurs révisions (des formats en
Rust pur : xml, zip, media…) restent telles quelles : un [patch] ne prend qu'une source par nom et par version.
Pas non plus celles que l'app a déjà en [patch.crates-io] (pas de patch en chaîne).

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
paths = {}
for rev in {r for v in revs.values() for r in v}:
    for d, dirs, files in os.walk(os.path.join(work, "craft-libs-" + rev)):
        dirs[:] = [x for x in dirs if x not in ("target", ".git")]
        if "Cargo.toml" in files:
            try:
                n = tomllib.load(open(os.path.join(d, "Cargo.toml"), "rb")).get("package", {}).get("name")
            except Exception:
                continue
            if n:
                paths[(n, rev)] = d
print('[patch."https://github.com/storytold/craft-libs"]')
for n in sorted(set(revs) - skip):
    if len(revs[n]) != 1:
        continue
    rev = next(iter(revs[n]))
    if (n, rev) not in paths:
        sys.exit("craft : %s absent de craft-libs-%s" % (n, rev))
    print('%s = { path = "%s" }' % (n, paths[(n, rev)]))
