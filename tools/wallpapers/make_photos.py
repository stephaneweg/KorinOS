#!/usr/bin/env python3
# Fonds d'écran photographiques du paquet « wallpapers » : les images de tools/wallpapers/photos/ (générées
# par Gemini à partir des prompts du 17/11/2026, environ 1024 x 572), recadrées en 16:9 puis agrandies à
# 2560 x 1440 (Lanczos, accentuation légère), en JPEG dans l'arbre du paquet :
#   build/wallpapers/sdroot/home/public/wallpapers/<nom>.jpg   (root du paquet, tools/pkg/packages.ini)
# et une planche : build/wallpapers/planche-photos.png.
#
#   python -I tools/wallpapers/make_photos.py
#
# Le recadrage garde le centre ; CROP_Y décale la fenêtre vers le haut (< 0,5) ou le bas (> 0,5) quand l'image
# est plus haute que le 16:9.

import os
import sys

from PIL import Image, ImageFilter

W, H = 2560, 1440
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "photos")
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "build", "wallpapers"))
PKG = os.path.join(OUT, "sdroot", "home", "public", "wallpapers")
CROP_Y = {}  # nom -> 0..1 (0,5 : centré)


def crop_16_9(im, fy=0.5):
    w, h = im.size
    if w * 9 > h * 16:  # trop large : on coupe les côtés
        nw = h * 16 // 9
        x = (w - nw) // 2
        return im.crop((x, 0, x + nw, h))
    nh = w * 9 // 16  # trop haute : on coupe le haut et le bas
    y = int((h - nh) * fy)
    return im.crop((0, y, w, y + nh))


def main():
    os.makedirs(PKG, exist_ok=True)
    names = []
    for f in sorted(os.listdir(SRC)):
        name, ext = os.path.splitext(f)
        if ext.lower() not in (".webp", ".jpg", ".jpeg", ".png"):
            continue
        with Image.open(os.path.join(SRC, f)) as im:
            im = crop_16_9(im.convert("RGB"), CROP_Y.get(name, 0.5))
            big = im.resize((W, H), Image.LANCZOS)
            big = big.filter(ImageFilter.UnsharpMask(radius=2, percent=55, threshold=2))
            out = os.path.join(PKG, name + ".jpg")
            big.save(out, quality=92, subsampling=0, optimize=True)
        names.append(name)
        print("%-18s %s (de %dx%d)" % (name, out, im.size[0], im.size[1]))
    tw, th, gap = 480, 270, 20
    cols = 3
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new("RGB", (gap + cols * (tw + gap), gap + rows * (th + gap)), (0x2A, 0x2F, 0x3A))
    for i, n in enumerate(names):
        with Image.open(os.path.join(PKG, n + ".jpg")) as im:
            sheet.paste(im.resize((tw, th), Image.LANCZOS), (gap + (i % cols) * (tw + gap), gap + (i // cols) * (th + gap)))
    sheet.save(os.path.join(OUT, "planche-photos.png"), optimize=True)
    print("planche            %s" % os.path.join(OUT, "planche-photos.png"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
