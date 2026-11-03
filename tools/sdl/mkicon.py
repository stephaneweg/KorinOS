"""mkicon : une icone d'app au format ARIC (tools/icons/build.py) depuis une image du jeu.
    python -I tools/sdl/mkicon.py image.png icon.rgba
"""
import struct
import sys

from PIL import Image

SIZES = (16, 20, 24, 26, 30, 32, 36, 38, 40, 44, 48, 64)

src = Image.open(sys.argv[1]).convert("RGBA")
head = b"ARIC" + struct.pack("<HH", 1, len(SIZES))
off = len(head) + 8 * len(SIZES)
body = []
for z in SIZES:
    head += struct.pack("<HHI", z, 0, off)
    px = bytearray()
    for r, g, b, a in src.resize((z, z), Image.LANCZOS).getdata():
        px += bytes((b * a // 255, g * a // 255, r * a // 255, a))      # 0xAARRGGBB, alpha premultiplie
    body.append(bytes(px))
    off += z * z * 4
with open(sys.argv[2], "wb") as f:
    f.write(head)
    for b in body:
        f.write(b)
