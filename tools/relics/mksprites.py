#!/usr/bin/env python3
# tools/relics/mksprites.py -- un personnage en sequences PNG (une image par fichier, fond transparent, regard vers la
# droite) -> un fichier .spr que Relics charge a la place de ses sprites dessines (src/apps/relics/main.c, load_spr) :
# les images choisies, recadrees ensemble, reduites a la hauteur du jeu, en alpha premultiplie.
#
#   python3 tools/relics/mksprites.py SORTIE.spr --height 58 \
#           --walk "Dossier/Walking" 8  --shoot "Dossier/Throwing" 3  [--die "Dossier/Dying" 6]
#
# --walk DOSSIER N : N images de la marche, prises a intervalles reguliers ; --shoot : la pose de tir (N images) ;
# --die : la mort (jouee quand il est abattu). Les images d'origine (achetees par l'utilisateur) restent hors du depot :
# seul le .spr, a la taille du jeu, va dans src/apps/relics/data/sprites (tools/relics/sprites.sh le refait).
# --height : la hauteur du personnage a l'ecran (points ; un gardien fait 42 points de boite, ~58 dessine).
# Format .spr (petit-boutiste) : 'RSPR', u16 version 1, u16 n images, u16 l, u16 h, u16 ancre x, u16 ancre y (les
# pieds), u8 nombre d'images de marche, u8 de tir, u8 de mort, u8 0 ; puis n x l x h mots 0xAARRGGBB premultiplies.
import glob
import os
import struct
import sys

from PIL import Image


def frames(folder, n):
    fs = sorted(glob.glob(os.path.join(folder, "*.png")))
    if not fs:
        sys.exit("pas d'images dans " + folder)
    n = min(n, len(fs))
    return [fs[i * len(fs) // n] for i in range(n)]


def main():
    a = sys.argv[1:]
    out = a[0]
    height = int(a[a.index("--height") + 1]) if "--height" in a else 58
    groups = []
    for key in ("--walk", "--shoot", "--die"):
        if key in a:
            i = a.index(key)
            groups.append(frames(a[i + 1], int(a[i + 2])))
        else:
            groups.append([])
    files = groups[0] + groups[1] + groups[2]
    ims = [Image.open(f).convert("RGBA") for f in files]
    # la boite commune, et les pieds : le bas de la marche
    box = None
    for im in ims:
        b = im.getbbox()
        box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    feet = max(im.getbbox()[3] for im in ims[:len(groups[0])] or ims)
    walkbox = None
    for im in ims[:len(groups[0])] or ims:
        b = im.getbbox()
        walkbox = b if walkbox is None else (min(walkbox[0], b[0]), min(walkbox[1], b[1]), max(walkbox[2], b[2]), max(walkbox[3], b[3]))
    k = height / (feet - walkbox[1])                    # la hauteur : celle de la marche
    cx = (walkbox[0] + walkbox[2]) / 2                  # le milieu : celui de la marche
    w = int((box[2] - box[0]) * k + 2.5)
    h = int((box[3] - box[1]) * k + 2.5)
    ax, ay = int((cx - box[0]) * k + 1), int((feet - box[1]) * k + 1)
    data = bytearray()
    for im in ims:
        c = im.crop(box).resize((w - 2, h - 2), Image.LANCZOS)
        f = Image.new("RGBA", (w, h))
        f.paste(c, (1, 1))
        px = f.tobytes()
        for i in range(0, len(px), 4):
            r, g, b, al = px[i], px[i + 1], px[i + 2], px[i + 3]
            data += struct.pack("<I", (al << 24) | ((r * al // 255) << 16) | ((g * al // 255) << 8) | (b * al // 255))
    with open(out, "wb") as o:
        o.write(b"RSPR" + struct.pack("<HHHHHHBBBB", 1, len(ims), w, h, ax, ay, len(groups[0]), len(groups[1]), len(groups[2]), 0) + data)
    print("%s : %d images %dx%d, pieds en %d,%d, %d octets" % (out, len(ims), w, h, ax, ay, 20 + len(data)))


main()
