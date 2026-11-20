"""icon_rgba : une image PNG -> l'icône d'une app de Korin, <app>.app/icon.rgba (docs/CRAFT.md). Le format est celui
de tools/icons/build.py (lu par src/kits/uikit/uikit_icons.inc) : 'ARIC', u16 version 1, u16 n ; n entrées
(u16 côté, u16 0, u32 décalage) ; puis les pixels de chaque taille, 0xAARRGGBB petit-boutistes, alpha prémultiplié.

    python3 -E -P tools/craft/icon_rgba.py <image.png> <icon.rgba>      (PIL, souvent dans le site de l'utilisateur : pas -I)
"""
import struct
import sys

from PIL import Image

SIZES = (16, 20, 24, 26, 30, 32, 36, 38, 40, 44, 48, 64)

src = Image.open(sys.argv[1]).convert("RGBA")
blobs = []
for side in SIZES:
    im = src.resize((side, side), Image.LANCZOS)
    px = bytearray()
    for r, g, b, a in im.getdata():
        px += struct.pack("<I", (a << 24) | ((r * a // 255) << 16) | ((g * a // 255) << 8) | (b * a // 255))
    blobs.append(bytes(px))
off = 8 + 8 * len(SIZES)
out = bytearray(b"ARIC" + struct.pack("<HH", 1, len(SIZES)))
for side, blob in zip(SIZES, blobs):
    out += struct.pack("<HHI", side, 0, off)
    off += len(blob)
for blob in blobs:
    out += blob
open(sys.argv[2], "wb").write(out)
