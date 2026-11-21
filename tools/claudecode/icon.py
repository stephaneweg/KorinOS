"""icon.py -- l'icône de Claude Code pour Korin (/apps/claudecode.app/icon.rgba) : un carré arrondi graphite (le
« Studio » des apps Craft) et une étoile à huit branches couleur terre cuite. Dessinée ici (PIL), puis mise au format
de Korin par tools/craft/icon_rgba.py.
    python3 -E -P tools/claudecode/icon.py <icone.png>
"""
import math
import sys

from PIL import Image, ImageDraw

S = 512
im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rounded_rectangle((16, 16, S - 16, S - 16), radius=110, fill=(0x1b, 0x1c, 0x1f, 255), outline=(0x34, 0x34, 0x36, 255), width=6)
c = S / 2
for k in range(8):
    a = k * math.pi / 4 + math.pi / 8
    r1, r2 = 34, 168
    w = 46 if k % 2 == 0 else 36
    d.line((c + r1 * math.cos(a), c + r1 * math.sin(a), c + r2 * math.cos(a), c + r2 * math.sin(a)), fill=(0xd9, 0x77, 0x57, 255), width=w)
    d.ellipse((c + r2 * math.cos(a) - w / 2, c + r2 * math.sin(a) - w / 2, c + r2 * math.cos(a) + w / 2, c + r2 * math.sin(a) + w / 2), fill=(0xd9, 0x77, 0x57, 255))
d.ellipse((c - 52, c - 52, c + 52, c + 52), fill=(0xd9, 0x77, 0x57, 255))
im.save(sys.argv[1])
