"""keyroot.py : le système de la clé d'installation, fait des paquets publiés (docs/etude/INSTALLATEUR.md §2.1, §4).

    python -I tools/pc/keyroot.py [--repo DIR] [--ini tools/pc/installkey.ini] [--out build/pc/keyroot]

Extrait du dépôt PC (packages/pc de KorinOS-pub : index.txt et pkgs/*.opk) les paquets obligatoires de l'index et
ceux de [key] system, dans OUT/root (l'arbre du système « live », avec var/pkg/db/<nom>.ini comme les écrit
mkrepo.py : l'installation garde ainsi une base de paquets juste et se met à jour) ; les fichiers de démarrage du
paquet pc (EFI/, korin/) vont dans OUT/esp. Puis OUT/repo : le dépôt hors ligne (index.txt, index.sig, icons/, et
les archives de [key] packages), et OUT/repo/installer.ini (les catégories de l'étape Logiciels).
Ensuite : python -I tools/pc/installkey.py build --sysroot OUT/root --esp OUT/esp --repo OUT/repo
"""
import argparse
import configparser
import hashlib
import os
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def ini_read(path):
    c = configparser.ConfigParser(interpolation=None, strict=False)
    c.optionxform = str
    c.read(path, encoding="utf-8")
    return c


def liste(v):
    return [x.strip() for x in v.replace("\n", ",").split(",") if x.strip()]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo", default=os.path.join(os.path.dirname(ROOT), "KorinOS-pub", "packages", "pc"))
    ap.add_argument("--ini", default=os.path.join(HERE, "installkey.ini"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "pc", "keyroot"))
    ap.add_argument("--release-dir", default=os.path.join(ROOT, "build", "pkg-release-pc"),
                    help="les archives de la Release GitHub (trop grosses pour le dépôt : jet...)")
    a = ap.parse_args()
    idx = ini_read(os.path.join(a.repo, "index.txt"))
    conf = ini_read(a.ini)
    out_root, out_esp, out_repo = (os.path.join(a.out, d) for d in ("root", "esp", "repo"))
    shutil.rmtree(a.out, ignore_errors=True)
    for d in (out_root, out_esp, os.path.join(out_repo, "pkgs")):
        os.makedirs(d)
    pkgs = {s: idx[s] for s in idx.sections() if idx[s].get("file")}
    system = [s for s, p in pkgs.items() if p.get("required", "0") == "1"]
    for n in liste(conf.get("key", "system", fallback="")):
        if n in pkgs and n not in system:
            system.append(n)
        elif n not in pkgs:
            print("keyroot : %s absent de l'index" % n, file=sys.stderr)
    db = os.path.join(out_root, "var", "pkg", "db")
    os.makedirs(db)
    total = 0
    for n in system:
        p = pkgs[n]
        f = os.path.join(a.repo, p["file"])
        if not os.path.isfile(f):  # (une archive de la Release, pas dans le dépôt : à télécharger d'abord)
            print("keyroot : archive absente pour %s (%s)" % (n, p["file"]), file=sys.stderr)
            continue
        z = zipfile.ZipFile(f)
        man = z.read("PKG/manifest.ini").decode("utf-8") if "PKG/manifest.ini" in z.namelist() else ""
        boot = []
        for line in man.splitlines():
            if line.startswith("boot ="):
                boot = line.split("=", 1)[1].split()
        hashes = []
        for name in z.namelist():
            if name.endswith("/") or name.startswith("PKG/"):
                continue
            data = z.read(name)
            dst = os.path.join(out_esp if name.startswith(("EFI/", "korin/")) else out_root, name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as o:
                o.write(data)
            hashes.append("%s = %s" % (name, hashlib.sha256(data).hexdigest()))
            total += len(data)
        keep = [l for l in man.splitlines() if l and not l.startswith("#")]
        with open(os.path.join(db, n + ".ini"), "w", encoding="utf-8", newline="\n") as o:
            o.write("# installe par tools/pc/keyroot.py (cle d'installation)\n[package]\n" + "\n".join(keep) +
                    "\nmode = manual\n\n[files]\n" + "\n".join(hashes) + "\n")
    # le chargeur de la clé : \EFI\BOOT\BOOTX64.EFI, comme celui du paquet pc
    efi = os.path.join(out_esp, "EFI", "Korin", "korin.efi")
    if os.path.isfile(efi):
        os.makedirs(os.path.join(out_esp, "EFI", "BOOT"), exist_ok=True)
        shutil.copyfile(efi, os.path.join(out_esp, "EFI", "BOOT", "BOOTX64.EFI"))
    # le dépôt hors ligne
    for f in ("index.txt", "index.sig"):
        shutil.copyfile(os.path.join(a.repo, f), os.path.join(out_repo, f))
    if os.path.isdir(os.path.join(a.repo, "icons")):
        shutil.copytree(os.path.join(a.repo, "icons"), os.path.join(out_repo, "icons"))
    taille = 0
    for n in liste(conf.get("key", "packages", fallback="")):
        p = pkgs.get(n)
        src = os.path.join(a.repo, p["file"]) if p else ""
        if p and not os.path.isfile(src):  # (une archive de la Release : son nom de fichier dans --release-dir)
            src = os.path.join(a.release_dir, os.path.basename(p["file"]))
        if not p or not os.path.isfile(src):
            print("keyroot : %s introuvable (ni dans le dépôt, ni dans %s)" % (n, a.release_dir), file=sys.stderr)
            continue
        dst = os.path.join(out_repo, "pkgs", os.path.basename(p["file"])) if "://" in p["file"] else os.path.join(out_repo, p["file"])
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(src, dst)
        taille += os.path.getsize(dst)
    with open(os.path.join(out_repo, "installer.ini"), "w", encoding="utf-8", newline="\n") as o:
        o.write("# les catégories de l'étape Logiciels (tools/pc/installkey.ini)\n[categories]\norder = %s\ndefault = %s\n" %
                (conf.get("categories", "order", fallback=""), conf.get("categories", "default", fallback="")))
    print("keyroot : %d paquets du système (%d Mo), dépôt hors ligne de %d Mo -> %s" %
          (len(system), total >> 20, taille >> 20, a.out))


if __name__ == "__main__":
    main()
