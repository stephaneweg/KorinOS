"""cardplan : compare build/sdroot a la carte E:, sans rien ecrire, ou copie avec --go ; depuis la racine du depot,
apres les noyaux 800 x 600 (build/kernel8_sd.img, build/kernel_2712_sd.img). Jamais etc, home, var : seulement gpudemo
dans home/projects. Aucun effacement. Un fichier de la carte plus recent que le notre (hors date FTP) est garde et signale."""
import glob, hashlib, os, shutil, sys, time

GO = "--go" in sys.argv
E = "E:\\"
DIRS = ("sys", "lib", "res", "fonts", "bin", "apps", "basic", "dotnet", "gcc")
plan = []
for d in DIRS:
    for base, dirs, files in os.walk(os.path.join("build", "sdroot", d)):
        for n in files:
            p = os.path.join(base, n)
            plan.append((p, os.path.join(E, os.path.relpath(p, os.path.join("build", "sdroot")))))
for p in glob.glob("skel/sd0/home/projects/gpudemo/*"):
    plan.append((p, os.path.join(E, "home", "projects", "gpudemo", os.path.basename(p))))
for p in glob.glob("build/modules/*.mod"):
    plan.append((p, os.path.join(E, os.path.basename(p))))
plan.append(("build/kernel_2712_sd.img", E + "kernel_2712.img"))
plan.append(("build/kernel8_sd.img", E + "kernel8.img"))


def same(a, b):
    if os.path.getsize(a) != os.path.getsize(b):
        return False
    with open(a, "rb") as x, open(b, "rb") as y:
        while True:
            p, q = x.read(1 << 20), y.read(1 << 20)
            if p != q:
                return False
            if not p:
                return True


new, diff, keep, equal = [], [], [], 0
for p, r in plan:
    if not os.path.exists(r):
        new.append((p, r))
        continue
    if same(p, r):
        equal += 1
        continue
    t = os.path.getmtime(r)
    ftp = time.localtime(t).tm_year < 2026 or time.strftime("%m-%d", time.localtime(t)) == "01-01"
    if t > os.path.getmtime(p) + 2 and not ftp:
        keep.append((p, r, time.strftime("%d/%m %H:%M", time.localtime(t)), time.strftime("%d/%m %H:%M", time.localtime(os.path.getmtime(p)))))
    else:
        diff.append((p, r))
print("identiques", equal, "nouveaux", len(new), "differents", len(diff), "plus recents sur la carte", len(keep))
print("octets a copier", sum(os.path.getsize(p) for p, r in new + diff))
for k in keep[:60]:
    print("GARDE", k[1], "carte", k[2], "nous", k[3])
if GO:
    # le noyau en dernier : plan garde cet ordre
    order = [x for x in new + diff if "kernel" not in x[1]] + [x for x in new + diff if "kernel" in x[1]]
    for p, r in order:
        os.makedirs(os.path.dirname(r), exist_ok=True)
        shutil.copyfile(p, r)
    bad = [r for p, r in order if not same(p, r)]
    print("copies", len(order), "faux", len(bad))
    for b in bad[:10]:
        print("FAUX", b)
