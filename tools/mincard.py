"""La carte minimale (docs/CONSTRUIRE.md) : de quoi demarrer jusqu'au bureau, le reste vient par les paquets.

    .\\build.ps1 min sdcard                 (fabrique les noyaux 800x600, puis appelle ce script)
    python -I tools/mincard.py --sd build/sdroot --out DOSSIER --kernels K8,K2712 [--overlay DOSSIER]

Prend de la carte construite (build/sdroot) ce que le paquet armpi porte, moins ce qui n'est pas indispensable : pas de
sources, de documents, d'images d'essai, ni de SoundFont, et seulement les apps de base et les programmes de src/bin
qui ne sont pas des essais. Les lignes de /etc qui nomment un programme absent sont retirees. La base des paquets est
ensuite generee pour ce qui est vraiment sur la carte (mkrepo.py --db-only). Jamais de etc/wifi.txt.
"""
import argparse, fnmatch, glob, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APPS = "control files terminal welcome viewer editor taskman mixer pkgman".split()
SANS_RES = ("soundfonts", "audiotest")
ESSAIS = re.compile(r"(test|bench|bug)")
# les fichiers de /etc dont chaque ligne lance un programme
LANCEURS = "preload.txt startup.txt services.txt session drivers.txt".split()


def copie(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst)


def arbre(src, dst, garde=lambda rel: True):
    for d, _, fs in os.walk(src):
        for f in fs:
            p = os.path.join(d, f)
            rel = os.path.relpath(p, src).replace("\\", "/")
            if garde(rel):
                copie(p, os.path.join(dst, rel))


def texte(chemin):
    with open(chemin, "rb") as f:
        return f.read()


def filtre_lignes(chemin, garde):
    d = texte(chemin)
    nl = b"\r\n" if b"\r\n" in d else b"\n"
    r = [l for l in d.split(nl) if garde(l)]
    with open(chemin, "wb") as f:
        f.write(nl.join(r))


def premier_demarrage(out):
    """etc/session : le fond et Setup seuls, le reste attend en commentaire (src/sys/setup)"""
    p = os.path.join(out, "etc", "session")
    d = texte(p)
    nl = b"\r\n" if b"\r\n" in d else b"\n"
    r = []
    for l in d.split(nl):
        if not l.strip() or l.startswith(b"#") or b"voronoy" in l or b"sys/setup" in l:
            r.append(l)
        else:
            r.append(b"#setup: " + l)
    i = next(k for k, l in enumerate(r) if b"voronoy" in l)
    r.insert(i + 1, b"SD0:/sys/setup")
    with open(p, "wb") as f:
        f.write(nl.join(r))
    # le prechargement attend aussi (Jet : pres de 2 minutes sur une carte complete)
    p = os.path.join(out, "etc", "preload.txt")
    if os.path.exists(p):
        d = texte(p)
        nl = b"\r\n" if b"\r\n" in d else b"\n"
        r = [l if not l.strip() or l.startswith(b"#") else b"#setup: " + l for l in d.split(nl)]
        with open(p, "wb") as f:
            f.write(nl.join(r))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sd", default=os.path.join(ROOT, "build", "sdroot"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--kernels", required=True, help="kernel8.img,kernel_2712.img (les noyaux de la carte)")
    ap.add_argument("--overlay", help="ou prendre pkg, pkgd et pkgman s'ils ne sont pas sur la carte (build/office/sdroot)")
    a = ap.parse_args()
    sd, out = a.sd, a.out
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)

    # ce que lit le firmware
    k8, k5 = a.kernels.split(",")
    copie(k8, os.path.join(out, "kernel8.img"))
    copie(k5, os.path.join(out, "kernel_2712.img"))
    for f in ("start4.elf", "fixup4.dat", "bcm2711-rpi-4-b.dtb"):
        copie(os.path.join(ROOT, "firmware", f), os.path.join(out, f))
    copie(os.path.join(ROOT, "boot", "config.txt"), os.path.join(out, "config.txt"))
    for m in glob.glob(os.path.join(ROOT, "build", "modules", "*.mod")):
        copie(m, os.path.join(out, os.path.basename(m)))
    copie(os.path.join(sd, "README.TXT"), os.path.join(out, "README.TXT"))

    # le systeme
    for d in ("sys", "lib", "fonts", "firmware"):
        arbre(os.path.join(sd, d), os.path.join(out, d))
    arbre(os.path.join(sd, "res"), os.path.join(out, "res"), lambda r: r.split("/")[0] not in SANS_RES)
    arbre(os.path.join(sd, "etc"), os.path.join(out, "etc"),
          lambda r: not fnmatch.fnmatch(r, "wifi*.txt") or r == "wifi.example.txt")
    programmes = {n for n in os.listdir(os.path.join(ROOT, "src", "bin")) if not ESSAIS.search(n)}
    arbre(os.path.join(sd, "bin"), os.path.join(out, "bin"), lambda r: r in programmes or r == "pkg")
    for n in APPS:
        s = os.path.join(sd, "apps", n + ".app")
        if os.path.isdir(s):
            arbre(s, os.path.join(out, "apps", n + ".app"))

    # le gestionnaire de paquets vient de WSL (tools/pkg/build.sh)
    for rel in ("bin/pkg", "sys/pkgd", "apps/pkgman.app/main", "apps/pkgman.app/app.txt",
                "sys/setup"):
        d = os.path.join(out, rel)
        if os.path.exists(d):
            continue
        s = os.path.join(a.overlay or "", rel)
        if not a.overlay or not os.path.exists(s):
            sys.exit(f"mincard : {rel} manque (sh tools/pkg/build.sh, ou --overlay)")
        copie(s, d)
    ic = os.path.join(ROOT, "tools", "icons", "rendered", "pkgman.rgba")
    if os.path.exists(ic):
        copie(ic, os.path.join(out, "apps", "pkgman.app", "icon.rgba"))

    # /etc : plus de lignes vers ce qui n'est pas sur la carte
    def present(l):
        if l.lstrip().startswith(b"#"):
            return True
        return all(os.path.exists(os.path.join(out, m.decode())) for m in re.findall(rb"SD0:/(\S+)", l))
    for f in LANCEURS:
        p = os.path.join(out, "etc", f)
        if os.path.exists(p):
            filtre_lignes(p, present)
    def app_la(l):
        m = re.match(rb"\s*pin\s*=\s*(\S+)", l)
        return not m or os.path.isdir(os.path.join(out, "apps", m.group(1).decode() + ".app"))
    filtre_lignes(os.path.join(out, "etc", "panel.ini"), app_la)
    filtre_lignes(os.path.join(out, "etc", "mounts.txt"), lambda l: b"blk.img" not in l)   # (images d'essai)

    premier_demarrage(out)

    for d in ("documents", "music", "pictures", "video"):
        os.makedirs(os.path.join(out, "home", d), exist_ok=True)

    # la base : ce qui est sur la carte, rien de plus
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pkg", "mkrepo.py"), "--db-only", "--sd", out])
    if os.path.exists(os.path.join(out, "etc", "wifi.txt")):
        sys.exit("mincard : etc/wifi.txt sur la carte")
    print(f"mincard : {out}")


if __name__ == "__main__":
    main()
