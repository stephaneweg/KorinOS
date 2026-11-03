"""montage.py -- cas de la suite resvg cote a cote (nous, reference), pour regarder les echecs :
  python -I tools/vector/montage.py SUITE TAG sortie.png cas1 cas2 ...      (cas : chemin sous tests/, sans .svg)
"""
import os
import sys

from PIL import Image, ImageDraw

suite, tag, out = sys.argv[1:4]
names = sys.argv[4:]
cols = 4
cell = 200
W = cols * (cell * 2 + 12)
H = ((len(names) + cols - 1) // cols) * (cell + 16)
im = Image.new("RGB", (W, H), (200, 200, 210))
d = ImageDraw.Draw(im)
for i, n in enumerate(names):
    x = (i % cols) * (cell * 2 + 12)
    y = (i // cols) * (cell + 16)
    d.text((x + 2, y + 1), n[-38:], fill=(0, 0, 0))
    for k, p in enumerate(("build/try/%s-out/%s.out" % (tag, n), os.path.join(suite, "tests", n + ".png"))):
        try:
            a = Image.open(p).convert("RGBA")
        except OSError:
            continue
        a.thumbnail((cell, cell))
        bg = Image.new("RGB", a.size, (255, 255, 255))
        bg.paste(a, (0, 0), a)
        im.paste(bg, (x + k * (cell + 2), y + 14))
im.save(out)
