#!/usr/bin/env python3
# tools/relics/mksprites.py -- un personnage en images -> un fichier .spr que Relics charge a la place de ses sprites
# dessines (src/apps/relics/main.c, load_spr) : les images, recadrees ensemble, reduites a la hauteur du jeu, en alpha
# premultiplie, par groupes de poses. Depuis des sequences PNG (une image par fichier, fond transparent, regard vers
# la droite) :
#
#   python3 tools/relics/mksprites.py SORTIE.spr --height 58 \
#           --walk "Dossier/Walking" 8  --shoot "Dossier/Throwing" 3  [--die "Dossier/Dying" 6] [--stand ... --jump ...
#           --fall ... --climb ... --crouch ...]
#
# Ou depuis Python : write_spr (sortie, {"walk": [images PIL], ...}, height) (tools/relics/explorer.py).
# --GROUPE DOSSIER N : N images prises a intervalles reguliers. La hauteur (points a l'ecran) et les pieds : ceux de
# la marche. Les images d'origine (achetees par l'utilisateur) restent hors du depot : seuls les .spr, a la taille
# du jeu, vont dans src/apps/relics/data/sprites (tools/relics/sprites.sh les refait).
#
# Format .spr, version 3 (petit-boutiste) : 'RSPR', u16 version 3, u16 n images, u16 l, u16 h, u16 ancre x, u16
# ancre y (les pieds), 8 x u8 : le nombre d'images de chaque groupe, dans l'ordre GROUPS ; puis, groupe apres groupe,
# chaque image : u32 nombre de plages, puis les plages (u16 longueur, u32 valeur 0xAARRGGBB premultipliee), ligne
# apres ligne (une plage peut passer a la ligne suivante).
import glob
import os
import struct
import sys

from PIL import Image

GROUPS = ("walk", "shoot", "die", "stand", "jump", "fall", "climb", "crouch")


def frames(folder, n):
    fs = sorted(glob.glob(os.path.join(folder, "*.png")))
    if not fs:
        sys.exit("pas d'images dans " + folder)
    n = min(n, len(fs))
    return [Image.open(fs[i * len(fs) // n]).convert("RGBA") for i in range(n)]


def union(ims):
    box = None
    for im in ims:
        b = im.getbbox()
        if b:
            box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    return box


def write_spr(out, groups, height=58):
    ims = [im for g in GROUPS for im in groups.get(g, [])]
    if not groups.get("walk"):
        sys.exit("il faut au moins la marche")
    box = union(ims)
    wb = union(groups["walk"])
    feet = wb[3]
    k = height / (feet - wb[1])
    cx = (wb[0] + wb[2]) / 2
    w = int((box[2] - box[0]) * k + 2.5)
    h = int((box[3] - box[1]) * k + 2.5)
    if w > 256 or h > 256 or len(ims) > 64:
        sys.exit("trop grand : %dx%d, %d images" % (w, h, len(ims)))
    ax, ay = int((cx - box[0]) * k + 1), int((feet - box[1]) * k + 1)
    data = bytearray()
    for im in ims:
        c = im.crop(box).resize((w - 2, h - 2), Image.LANCZOS)
        f = Image.new("RGBA", (w, h))
        f.paste(c, (1, 1))
        px = f.tobytes()
        vals = []
        for i in range(0, len(px), 4):
            r, g, b, al = px[i], px[i + 1], px[i + 2], px[i + 3]
            vals.append((al << 24) | ((r * al // 255) << 16) | ((g * al // 255) << 8) | (b * al // 255) if al else 0)
        runs = bytearray()
        nr, i = 0, 0
        while i < len(vals):
            j = i
            while j < len(vals) and vals[j] == vals[i] and j - i < 65535:
                j += 1
            runs += struct.pack("<HI", j - i, vals[i])
            nr += 1
            i = j
        data += struct.pack("<I", nr) + runs
    counts = [len(groups.get(g, [])) for g in GROUPS]
    with open(out, "wb") as o:
        o.write(b"RSPR" + struct.pack("<HHHHHH", 3, len(ims), w, h, ax, ay) + bytes(counts) + data)
    print("%s : %d images %dx%d, pieds en %d,%d, %s, %d octets" % (out, len(ims), w, h, ax, ay,
          " ".join("%s %d" % (g, c) for g, c in zip(GROUPS, counts) if c), 24 + len(data)))


def main():
    a = sys.argv[1:]
    out = a[0]
    height = int(a[a.index("--height") + 1]) if "--height" in a else 58
    groups = {}
    for g in GROUPS:
        key = "--" + g
        if key in a:
            i = a.index(key)
            groups[g] = frames(a[i + 1], int(a[i + 2]))
    write_spr(out, groups, height)


if __name__ == "__main__":
    main()
