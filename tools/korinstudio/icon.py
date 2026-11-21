"""icon.py : l'icône de KorinStudio (le losange bleu de Korin et des chevrons de code), dessinée par PIL, puis mise au
format des icônes de Korin par tools/craft/icon_rgba.py.

    python3 -E -P tools/korinstudio/icon.py <icon.rgba> [<icon.png>]
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw

S = 256
im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
# un carré arrondi sombre, comme les icônes d'IDE, et le losange de Korin
d.rounded_rectangle((8, 8, S - 8, S - 8), radius=52, fill=(30, 30, 34, 255))
c = S / 2
h = 92
d.polygon([(c, c - h), (c + h, c), (c, c + h), (c - h, c)], fill=(30, 111, 232, 255))
w = 15
for sx in (-1, 1):
    pts = [(c + sx * 16, c - 38), (c + sx * 52, c), (c + sx * 16, c + 38)]
    d.line(pts, fill=(255, 255, 255, 255), width=w, joint="curve")
    for x, y in (pts[0], pts[2]):
        d.ellipse((x - w / 2, y - w / 2, x + w / 2, y + w / 2), fill=(255, 255, 255, 255))
out = sys.argv[1]
png = sys.argv[2] if len(sys.argv) > 2 else os.path.join(tempfile.mkdtemp(), "korinstudio.png")
im.save(png)
here = os.path.dirname(os.path.abspath(__file__))
subprocess.check_call([sys.executable, "-E", "-P", os.path.join(here, "..", "craft", "icon_rgba.py"), png, out])
