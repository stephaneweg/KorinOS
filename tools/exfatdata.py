"""Le contenu de la partition exFAT de build/sd.img : skel/sd1 et les donnees d'exfattest.

Usage : python -I tools/exfatdata.py skel/sd1 build/exfat_root

Recopie skel/sd1 dans le dossier de sortie, puis ajoute ce qu'exfattest lit : un dossier accentue, un
nom long, gros.bin (contigu) et « chaine fragmentee.bin » (tools/mkexfat.py le fragmente). Le motif
ne se repete pas : deux fichiers qui se recouvrent ne peuvent pas se ressembler. exfattest calcule le
meme, et refuse de tourner sans gros.bin : il ecrit sur SD1:.
"""
import os
import shutil
import sys

PAT_MUL = 0x9E3779B1


def pattern(n, a, b):
    return bytes(((i * a + b) * PAT_MUL & 0xFFFFFFFF) >> 24 for i in range(n))


def main(src, out):
    shutil.rmtree(out, ignore_errors=True)
    shutil.copytree(src, out)
    deep = os.path.join(out, "Dossier été", "SOUS")
    os.makedirs(deep)
    with open(os.path.join(deep, "PROFOND.TXT"), "w", encoding="ascii", newline="\n") as f:
        f.write("Fichier profond, trouve sans tenir compte de la casse.\n")
    with open(os.path.join(out, "Dossier été", "Un nom de fichier très très long pour tester les entrées exFAT.txt"),
              "w", encoding="ascii", newline="\n") as f:
        f.write("Nom long : plusieurs entrees de repertoire.\n")
    with open(os.path.join(out, "gros.bin"), "wb") as f:
        f.write(pattern(300000, 7, 0))
    with open(os.path.join(out, "chaine fragmentee.bin"), "wb") as f:
        f.write(pattern(50000, 13, 5))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
