"""tryimg.py -- une carte d'essai a soi, pour que plusieurs essais QEMU tournent ensemble : une copie de build/sd.img
dont on remplace quelques fichiers (FAT32 de la partition 1, par mtools dans WSL), sans refaire la carte.

  python -I tools/office/tryimg.py new TAG                    build/try/TAG.img, copie de build/sd.img
  python -I tools/office/tryimg.py put TAG LOCAL /chemin      un fichier ou un dossier vers la carte
  python -I tools/office/tryimg.py get TAG /chemin LOCAL      un fichier de la carte vers le PC
  python -I tools/office/tryimg.py ls TAG /chemin

Puis, depuis la racine du depot, avec un port et un ecran VNC a soi :
  python -I tools/qemu_drive.py script.txt --sd build/try/TAG.img --port 45xx --vnc NN --uart build/try/TAG.log
Les fichiers de carte se donnent sans volume : /apps/sheet.app/main, /etc/startup.txt.
"""
import os
import shutil
import struct
import subprocess
import sys


def wsl(path):
    p = os.path.abspath(path).replace("\\", "/")
    return "/mnt/%s%s" % (p[0].lower(), p[2:])


def part_offset(img):
    with open(img, "rb") as f:
        mbr = f.read(512)
    return struct.unpack("<I", mbr[446 + 8:446 + 12])[0] * 512


def mtools(img, *args):
    a = ["wsl", args[0], "-i", "%s@@%d" % (wsl(img), part_offset(img))] + list(args[1:])
    r = subprocess.run(a, stdin=subprocess.DEVNULL)
    if r.returncode:
        sys.exit("echec : " + " ".join(a))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    op, tag = sys.argv[1], sys.argv[2]
    os.makedirs("build/try", exist_ok=True)
    img = "build/try/%s.img" % tag
    if op == "new":
        shutil.copyfile("build/sd.img", img)
    elif op == "put":
        src, dst = sys.argv[3], sys.argv[4]
        if os.path.isdir(src):                  # (son contenu, le dossier cree s'il manque)
            a = ["wsl", "mmd", "-D", "s", "-i", "%s@@%d" % (wsl(img), part_offset(img)), "::" + dst]
            subprocess.run(a, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            for n in os.listdir(src):
                mtools(img, "mcopy", "-n", "-o", "-Q", "-s", wsl(os.path.join(src, n)), "::" + dst + "/")
        else:
            mtools(img, "mcopy", "-n", "-o", "-Q", wsl(src), "::" + dst)
    elif op == "get":
        mtools(img, "mcopy", "-n", "-o", "::" + sys.argv[3], wsl(sys.argv[4]))
    elif op == "ls":
        mtools(img, "mdir", "::" + sys.argv[3])
    else:
        sys.exit(__doc__)


main()
