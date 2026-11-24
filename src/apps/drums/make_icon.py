# make_icon.py -- dessine l'icône de la Boîte à rythmes (data/icon.rgba, format ARIC de tools/icons/build.py :
# 'ARIC', u16 version 1, u16 n ; n entrées (u16 côté, u16 0, u32 décalage) ; pixels 0xAARRGGBB prémultipliés).
#   python3 -I src/apps/drums/make_icon.py src/apps/drums/data/icon.rgba   (sans PIL : Korin n'a pas Pillow)
import struct
import sys

SIZES = (16, 20, 24, 26, 30, 32, 36, 38, 40, 44, 48, 64)
SS = 4  # sur-échantillonnage


def rbox(x, y, x0, y0, w, h, r):
    """1 si (x, y) est dans la boîte arrondie."""
    cx = min(max(x, x0 + r), x0 + w - r)
    cy = min(max(y, y0 + r), y0 + h - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r and x0 <= x <= x0 + w and y0 <= y <= y0 + h


def lerp(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


TOP, BOTTOM = (0x7A, 0x5C, 0xE6), (0x4B, 0x34, 0xB0)
PAD_OFF = (0xFF, 0xFF, 0xFF, 0x48)
LIT = {(0, 0): (0xFF, 0x5A, 0x5F), (1, 1): (0xFF, 0xB0, 0x20), (2, 0): (0xFF, 0xB0, 0x20),
       (0, 2): (0x34, 0xD3, 0x99), (2, 2): (0x34, 0xD3, 0x99), (3, 1): (0xFF, 0xFF, 0xFF), (3, 3): (0xFF, 0x5A, 0x5F)}


def sample(x, y):
    """couleur (r, g, b, a) d'un point de la grille de 40."""
    if not rbox(x, y, 0, 0, 40, 40, 10):
        return (0, 0, 0, 0)
    col = lerp(TOP, BOTTOM, y / 40)
    if y < 1.6 and 10 < x < 30:
        col = lerp(col, (255, 255, 255), 0.35)
    # 4 x 4 pads
    for i in range(4):
        for j in range(4):
            px, py = 7 + i * 6.8, 7 + j * 6.8
            if rbox(x, y, px, py, 5.4, 5.4, 1.4):
                if (i, j) in LIT:
                    return LIT[(i, j)] + (255,)
                a = PAD_OFF[3] / 255
                return lerp(col, PAD_OFF[:3], a) + (255,)
    return col + (255,)


def render(side):
    px = bytearray()
    for yy in range(side):
        for xx in range(side):
            acc = [0, 0, 0, 0]
            for sy in range(SS):
                for sx in range(SS):
                    r, g, b, a = sample((xx + (sx + 0.5) / SS) * 40 / side, (yy + (sy + 0.5) / SS) * 40 / side)
                    acc[0] += r * a; acc[1] += g * a; acc[2] += b * a; acc[3] += a
            n = SS * SS
            a = acc[3] // n
            r, g, b = (acc[k] // (n * 255) for k in range(3))  # prémultiplié
            px += struct.pack("<I", (a << 24) | (r << 16) | (g << 8) | b)
    return bytes(px)


blobs = [render(s) for s in SIZES]
off = 8 + 8 * len(SIZES)
out = bytearray(b"ARIC" + struct.pack("<HH", 1, len(SIZES)))
for side, blob in zip(SIZES, blobs):
    out += struct.pack("<HHI", side, 0, off)
    off += len(blob)
for blob in blobs:
    out += blob
open(sys.argv[1], "wb").write(out)
print(len(out), "octets")
