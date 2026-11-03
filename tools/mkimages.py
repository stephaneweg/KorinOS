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
                           "--taille", str(TAILLE)])
    with open(img, "rb") as s, lzma.open(xz, "wb", preset=6, check=lzma.CHECK_CRC32) as d:
        shutil.copyfileobj(s, d, 1 << 20)
    r = {"extract_size": os.path.getsize(img), "extract_sha256": somme(img),
         "image_download_size": os.path.getsize(xz), "image_download_sha256": somme(xz)}
    os.remove(img)
    print(f"{nom} : {r['extract_size'] / 1e9:.1f} Go, archive {r['image_download_size'] / 1e6:.1f} Mo")
    return r


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
