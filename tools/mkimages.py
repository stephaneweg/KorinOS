"""Les images de carte de Korin OS et leur liste pour Raspberry Pi Imager (docs/CONSTRUIRE.md).

    python -I tools/mkimages.py --min sdcard --full build/sdcard-full [--url-full https://.../releases/download/...] [--version 2026.11.1]

Une image de 4 Go, une partition FAT32, compressee en .img.xz (le vide ne coute rien), dans img/ ; et img/os-list.json,
au format de doc/os-sublist-example.json de rpi-imager : tailles et SHA-256 de l'image et de son archive. GitHub refuse
un fichier de plus de 100 Mo : une telle archive va dans une Release (--url-full) et reste hors de git.
"""
import argparse, datetime, hashlib, json, lzma, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TAILLE = 4096                                       # Mo
SITE = "https://stephaneweg.github.io/KorinOS/"
HORS_FULL = ("supertux", "supertuxkart", "koton-samples")   # paquets laisses hors de l'image complete : les jeux
                                                    # SDL (pres de 1,2 Go, par Logiciels), les chansons de l'utilisateur
                                                    # (publish = 0)


def somme(chemin):
    h = hashlib.sha256()
    with open(chemin, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                return h.hexdigest()
            h.update(b)


def image(dossier, nom, sortie):
    img = os.path.join(sortie, nom + ".img")
    xz = img + ".xz"
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "mkdisk.py"), img, "--fat32", dossier,
                           "--taille", str(TAILLE), "--extensible", "128"])   # (Setup l'etend jusqu'a 128 Go)
    with open(img, "rb") as s, lzma.open(xz, "wb", preset=6, check=lzma.CHECK_CRC32) as d:
        shutil.copyfileobj(s, d, 1 << 20)
    r = {"extract_size": os.path.getsize(img), "extract_sha256": somme(img),
         "image_download_size": os.path.getsize(xz), "image_download_sha256": somme(xz)}
    os.remove(img)
    print(f"{nom} : {r['extract_size'] / 1e9:.1f} Go, archive {r['image_download_size'] / 1e6:.1f} Mo")
    return r


def retire(dossier, paquets):
    """Retire des paquets d'une carte, d'apres sa base (var/pkg/db) : leurs fichiers, ceux de leurs donnees
    (<nom>-data-N, split) et leurs entrees."""
    db = os.path.join(dossier, "var", "pkg", "db")
    for f in sorted(os.listdir(db)) if os.path.isdir(db) else []:
        n = f[:-4]
        if not f.endswith(".ini") or not any(n == p or n.startswith(p + "-data-") for p in paquets):
            continue
        dans = False
        for ligne in open(os.path.join(db, f), encoding="utf-8"):
            ligne = ligne.strip()
            if ligne.startswith("["):
                dans = ligne == "[files]"
            elif dans and " = " in ligne:
                c = os.path.join(dossier, ligne.split(" = ")[0])
                if os.path.isfile(c):
                    os.remove(c)
        os.remove(os.path.join(db, f))
    for racine, ds, fs in os.walk(dossier, topdown=False):    # (les dossiers d'apps vides qu'ils laissent)
        for d in ds:
            c = os.path.join(racine, d)
            if not os.listdir(c) and os.sep + "apps" + os.sep in c:    # (home/ garde ses dossiers vides)
                os.rmdir(c)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--min", required=True)
    ap.add_argument("--full")
    ap.add_argument("--url", default=SITE + "img", help="le dossier en ligne des .img.xz")
    ap.add_argument("--url-full", help="le dossier en ligne de l'image complete")
    ap.add_argument("--version", default=datetime.date.today().strftime("%Y.%m.%d"))
    ap.add_argument("--out", default=os.path.join(ROOT, "img"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    liste = []
    for dossier, nom, titre, texte, base in (
            (a.min, "korin_lite", "Korin OS (lite)",
             "A 64-bit, micro-kernel based, desktop OS for the Raspberry Pi 4, written in assembly.", a.url),
            (a.full, "korin_full", "Korin OS (full)",
             "A 64-bit, micro-kernel based, desktop OS for the Raspberry Pi 4, written in assembly.", a.url_full or a.url)):
        if not dossier:
            continue
        ses = os.path.join(dossier, "etc", "session")
        with open(ses, "rb") as h:
            prem = b"sys/setup" in h.read()
        if not prem:                                # (une carte de build.ps1 sd : Setup a son premier demarrage)
            sys.path.insert(0, os.path.join(ROOT, "tools"))
            import mincard
            mincard.premier_demarrage(dossier)
        if nom == "korin_full":
            retire(dossier, HORS_FULL)
        r = image(dossier, nom, a.out)
        e = {"name": titre, "description": texte, "url": base.rstrip("/") + "/" + nom + ".img.xz",
             "icon": SITE + "assets/korin-os-40.png", "website": SITE,
             "release_date": datetime.date.today().isoformat()}
        e.update(r)
        e.update({"devices": ["pi4-64bit"], "architecture": "armv8"})
        liste.append(e)
    with open(os.path.join(a.out, "os-list.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump({"os_list": liste}, f, indent=2)
        f.write("\n")
    print(os.path.join(a.out, "os-list.json"))


if __name__ == "__main__":
    main()
