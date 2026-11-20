#!/usr/bin/env python3
"""mktiles.py -- les tuiles, decors et fonds de Relics, un jeu par monde, dans le style du tileset « forest
platformer » achete par l'utilisateur (trait sombre epais, aplats, reflets) : la jungle prend ce tileset tel quel ;
le desert, la pyramide, la glace, le volcan, la foret hantee et la vallee de la mort ont leurs tuiles et leurs decors
dessines ici (paves bruns et sable lisse, figuiers de Barbarie et panneaux ; blocs de gres ; neige et glace ; basalte
veine de lave ; pierres violettes, mousse et citrouilles ; roche brune fendue de lave, mesas...). Le tileset reste hors du depot ; le jeu charge world<n>.til a la place de ses
tuiles dessinees (main.c, load_til) :

    python3 tools/relics/mktiles.py /chemin/forest-tileset/png src/apps/relics/data/tiles [0,5,6] [--sheet planche.png]

Format world<n>.til (petit-boutiste) : 'RTIL', u16 version 2, u16 nombre d'images ; puis chaque image : u16 l, u16 h,
u32 nombre de plages, les plages (u16 longueur, u32 valeur 0xAARRGGBB premultipliee). L'ordre : NAMES (l'enum ET_* de
main.c). Les decors deco1..deco8 sont semes sur les sols ; « big » est la plante du premier plan (F dans les niveaux).
"""
import math
import os
import random
import struct
import sys

from PIL import Image, ImageDraw, ImageFilter

NAMES = ("grass", "cobble", "back_a", "back_b", "plank", "logs", "ladder", "spikes", "wsurf", "wmid",
         "deco1", "deco2", "deco3", "deco4", "deco5", "deco6", "deco7", "deco8", "big", "door", "coin", "pot", "key", "bg")
TP = 32
D = 128                     # le dessin : 128 points par case (comme le tileset), reduit a 32
SS = 2                      # et dessine deux fois plus grand (lissage)
INK = (36, 28, 24, 255)
OW = 7                      # le trait


def load(src, name):
    return Image.open(os.path.join(src, name)).convert("RGBA")


def fit(im, w, h, bottom=True):
    b = im.getbbox()
    c = im.crop(b)
    k = min(w / c.width, h / c.height)
    c = c.resize((max(1, round(c.width * k)), max(1, round(c.height * k))), Image.LANCZOS)
    o = Image.new("RGBA", (w, h))
    o.alpha_composite(c, ((w - c.width) // 2, h - c.height if bottom else 0))
    return o


def rgb(h):
    return ((h >> 16) & 255, (h >> 8) & 255, h & 255, 255)


def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c[:3]) + (255,)


class Pen:
    """un dessin de w x h points (unites de dessin), fait a SS fois sa taille"""

    def __init__(s, w, h):
        s.w, s.h = w, h
        s.im = Image.new("RGBA", (w * SS, h * SS))
        s.d = ImageDraw.Draw(s.im)

    def P(s, pts):
        return [(x * SS, y * SS) for x, y in pts]

    def poly(s, pts, fill, ow=OW, ink=INK):
        if ow:
            s.d.polygon(s.P(pts), fill=ink, outline=ink, width=int(ow * SS * 2))
        s.d.polygon(s.P(pts), fill=rgb(fill) if isinstance(fill, int) else fill)

    def ell(s, box, fill, ow=OW, ink=INK):
        x0, y0, x1, y1 = box
        if ow:
            s.d.ellipse([(x0 - ow) * SS, (y0 - ow) * SS, (x1 + ow) * SS, (y1 + ow) * SS], fill=ink)
        s.d.ellipse([x0 * SS, y0 * SS, x1 * SS, y1 * SS], fill=rgb(fill) if isinstance(fill, int) else fill)

    def rect(s, box, fill, r=6, ow=OW, ink=INK):
        x0, y0, x1, y1 = box
        if x1 <= x0 or y1 <= y0:
            return
        r = max(0, min(r, (x1 - x0) / 2, (y1 - y0) / 2))
        if ow:
            s.d.rounded_rectangle([(x0 - ow) * SS, (y0 - ow) * SS, (x1 + ow) * SS, (y1 + ow) * SS], radius=(r + ow) * SS, fill=ink)
        s.d.rounded_rectangle([x0 * SS, y0 * SS, x1 * SS, y1 * SS], radius=r * SS, fill=rgb(fill) if isinstance(fill, int) else fill)

    def line(s, pts, col, w):
        s.d.line(s.P(pts), fill=rgb(col) if isinstance(col, int) else col, width=int(w * SS), joint="curve")
        for x, y in pts:
            s.d.ellipse([(x - w / 2) * SS, (y - w / 2) * SS, (x + w / 2) * SS, (y + w / 2) * SS], fill=rgb(col) if isinstance(col, int) else col)

    def out(s, w=None, h=None):
        return s.im.resize((w or s.w * TP // D, h or s.h * TP // D), Image.LANCZOS)


def stone(p, box, base, r=14, seed=0):
    """un pave du tileset : trait, face, ombre en bas a droite, reflet en haut a gauche"""
    x0, y0, x1, y1 = box
    p.rect(box, shade(base, 0.78), r)
    p.rect((x0, y0, x1 - 5, y1 - 6), base, r, ow=0)
    p.line([(x0 + r * 0.7, y0 + 7), (x1 - r - 6, y0 + 7)], shade(base, 1.22), 5)
    rnd = random.Random(seed)
    if rnd.random() < 0.5:
        cx, cy = rnd.uniform(x0 + 12, x1 - 18), rnd.uniform(y0 + 14, y1 - 16)
        p.line([(cx, cy), (cx + 8, cy + 6)], shade(base, 0.6), 3)


def stones(p, layout, base, seed):
    rnd = random.Random(seed)
    for i, b in enumerate(layout):
        stone(p, b, shade(base, rnd.uniform(0.9, 1.08)), 14, seed * 31 + i)


L1 = [(4, 4, 70, 50), (78, 4, 124, 44), (4, 58, 50, 124), (58, 58, 124, 92), (58, 100, 124, 124)]
L2 = [(4, 4, 46, 62), (54, 4, 124, 52), (4, 70, 78, 124), (86, 60, 124, 124)]
BR1 = [(4, 4, 62, 58), (70, 4, 124, 58), (4, 66, 36, 124), (44, 66, 98, 124), (106, 66, 124, 124)]
BR2 = [(4, 4, 34, 58), (42, 4, 96, 58), (104, 4, 124, 58), (4, 66, 62, 124), (70, 66, 124, 124)]


# ===== la pyramide (l'interieur du temple) ================================================================================================
def temple(src):
    SAND, SAND2, STONE, DARK = 0xE2AE62, 0xF4D493, 0xD49A52, 0x6A4A2A
    R = {}

    def blocks(layout, base, seed, chips=True):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x7A4E22))
        rnd = random.Random(seed)
        for i, b in enumerate(layout):
            x0, y0, x1, y1 = b
            c = shade(rgb(base), rnd.uniform(0.92, 1.06))
            p.rect(b, shade(c, 0.8), 5)
            p.rect((x0, y0, x1 - 5, y1 - 6), c, 5, ow=0)
            p.line([(x0 + 6, y0 + 6), (x1 - 12, y0 + 6)], shade(c, 1.18), 4)
            if chips and rnd.random() < 0.6:		# un eclat
                cx = rnd.uniform(x0 + 8, x1 - 16)
                p.poly([(cx, y1 - 3), (cx + 10, y1 - 3), (cx + 4, y1 - 12)], shade(c, 0.62), ow=0)
            if rnd.random() < 0.35:			# des trous de sable
                cx, cy = rnd.uniform(x0 + 10, x1 - 14), rnd.uniform(y0 + 14, y1 - 14)
                p.ell((cx, cy, cx + 5, cy + 4), shade(c, 0.65), ow=0)
        return p

    R["cobble"] = blocks(BR1, STONE, 3).out()
    p = blocks(BR2, STONE, 4)
    # la congere : du sable en vagues sur le dessus, des grains
    pts = [(0, 0), (D, 0), (D, 26)] + [(D - x, 26 + 7 * math.sin(x / 12.0) + (5 if x % 50 < 20 else 0)) for x in range(0, D + 1, 8)] + [(0, 30)]
    p.poly(pts, SAND2)
    p.line([(10, 10), (60, 8)], 0xFFF0C8, 5)
    for x, y in ((24, 18), (84, 14), (104, 22)):
        p.ell((x, y, x + 5, y + 4), 0xC9944E, ow=0)
    R["grass"] = p.out()
    for k, (lay, seed) in enumerate(((BR1, 7), (BR2, 8))):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x3E2A16))
        for i, b in enumerate(lay):
            x0, y0, x1, y1 = b
            c = shade(rgb(DARK), random.Random(seed + i).uniform(0.88, 1.05))
            p.rect(b, c, 5, ow=4, ink=(60, 40, 22, 255))
            p.line([(x0 + 6, y0 + 6), (x1 - 12, y0 + 6)], shade(c, 1.08), 3)
        if k == 0:					# un oeil grave, discret
            p.ell((70, 18, 112, 42), shade(rgb(DARK), 0.8), ow=0)
            p.ell((84, 23, 98, 37), shade(rgb(DARK), 0.65), ow=0)
        R["back_a" if k == 0 else "back_b"] = p.out()
    # les sables mouvants
    for k in (0, 1):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0xC69A58))
        for i in range(5):
            y = 20 + i * 22
            p.line([(10 + (i * 37) % 60, y), (40 + (i * 37) % 60, y + 6), (70 + (i * 37) % 60, y)], 0xA87C40, 5)
        if k == 1:
            p.d.rectangle([0, 0, D * SS, 14 * SS], fill=(0, 0, 0, 0))
            pts = [(0, 14)] + [(x, 14 + 4 * math.sin(x / 10.0)) for x in range(0, D + 1, 8)] + [(D, 30), (0, 30)]
            p.poly(pts, 0xE0BC7A, ow=5)
            p.ell((30, 40, 40, 48), 0xE0BC7A, ow=3)
            p.ell((92, 60, 100, 66), 0xE0BC7A, ow=3)
        R["wsurf" if k else "wmid"] = p.out()

    # les decors
    def cactus(big=False):
        s = 2.2 if big else 1.0
        p = Pen(int(80 * s), int(120 * s))
        G, G2 = 0x5FA845, 0x3F7F33

        def arm(x0, y0, x1, y1):
            p.rect((x0 * s, y0 * s, x1 * s, y1 * s), G, 9 * s, ow=OW)
        arm(30, 14, 50, 116)
        arm(6, 40, 22, 80)
        arm(6, 70, 34, 84)
        arm(58, 30, 74, 66)
        arm(46, 58, 70, 72)
        for x0, y0, x1, y1 in ((30, 14, 50, 116), (6, 40, 22, 80), (58, 30, 74, 66)):
            p.rect((x0 * s, y0 * s, x1 * s, y1 * s), G, 9 * s, ow=0)
            p.line([((x0 + 6) * s, (y0 + 8) * s), ((x0 + 6) * s, (y1 - 8) * s)], 0x8FD068, 4 * s)
            p.line([((x1 - 5) * s, (y0 + 10) * s), ((x1 - 5) * s, (y1 - 6) * s)], G2, 3 * s)
        if big:
            p.ell((36 * s, 4 * s, 46 * s, 14 * s), 0xF05A78, ow=4)
        return p.out(int(80 * s) * TP // D * (2 if big else 1) // (2 if big else 1), int(120 * s) * TP // D)

    def skull():
        p = Pen(110, 70)
        B = 0xF1E8D4
        p.line([(10, 60), (100, 46)], B, 12)
        p.line([(18, 44), (90, 64)], B, 12)
        for x, y in ((10, 60), (100, 46), (18, 44), (90, 64)):
            p.ell((x - 8, y - 8, x + 8, y + 8), B)
        p.ell((34, 6, 82, 52), B)
        p.rect((44, 40, 74, 62), B, 5)
        p.ell((44, 22, 56, 36), 0x3A2A20, ow=0)
        p.ell((62, 22, 74, 36), 0x3A2A20, ow=0)
        p.line([(52, 50), (52, 60)], 0x3A2A20, 3)
        p.line([(60, 50), (60, 60)], 0x3A2A20, 3)
        p.line([(68, 50), (68, 60)], 0x3A2A20, 3)
        return p.out()

    def column():
        p = Pen(70, 120)
        p.rect((10, 100, 60, 118), 0xC79257, 4)
        p.poly([(16, 102), (16, 40), (30, 30), (40, 44), (54, 26), (54, 102)], 0xDDAA6A)
        for x in (24, 36, 46):
            p.line([(x, 46), (x, 98)], 0xB8854C, 4)
        p.line([(20, 50), (20, 96)], 0xF2CA90, 3)
        return p.out()

    def drybush():
        p = Pen(90, 60)
        for a in (-60, -35, -10, 15, 40, 65):
            r = math.radians(a - 90)
            x1, y1 = 45 + 40 * math.cos(r), 56 + 46 * math.sin(r)
            p.line([(45, 56), (x1, y1)], 0x2C1E14, 8)
            p.line([(45, 56), (x1, y1)], 0x9A7444, 4)
            p.line([((45 + x1) / 2, (56 + y1) / 2), ((45 + x1) / 2 + 10, (56 + y1) / 2 - 8)], 0x9A7444, 3)
        return p.out()

    def sandrock():
        p = Pen(100, 64)
        p.poly([(6, 60), (14, 30), (40, 14), (66, 20), (90, 36), (96, 60)], 0xC89154)
        p.poly([(40, 14), (66, 20), (60, 40), (36, 34)], 0xDDAB6E, ow=0)
        p.line([(18, 54), (88, 54)], 0xA06E38, 4)
        return p.out()

    pot = fit(load(src, "item/pot-2.png"), 22, 26)
    R.update(deco1=column(), deco2=skull(), deco3=column(), deco4=pot, deco5=sandrock(), deco6=sandrock(), deco7=pot,
             deco8=skull(), big=cactus(True))
    return R


# ===== le desert (dehors, avant le temple) : paves bruns, sable lisse, figuiers de Barbarie, panneaux =================
def sign(kind):
    """un panneau de bois sur son piquet : fleche (droite, haut) ou tete de mort"""
    p = Pen(110, 110)
    W1, W2 = 0x9A6232, 0x7A4A22
    p.rect((48, 50, 62, 108), W2, 3)
    p.rect((8, 10, 102, 62), W1, 6)
    p.line([(16, 24), (94, 24)], 0x7A4A22, 3)
    p.line([(16, 44), (94, 44)], 0x7A4A22, 3)
    for x, y in ((14, 16), (96, 16), (14, 56), (96, 56)):
        p.ell((x - 2, y - 2, x + 2, y + 2), 0x4A2A12, ow=0)
    W = 0xF4ECDC
    if kind == "right":
        p.poly([(26, 30), (64, 30), (64, 20), (88, 36), (64, 52), (64, 42), (26, 42)], W, ow=4)
    elif kind == "up":
        p.poly([(49, 52), (49, 32), (38, 32), (55, 14), (72, 32), (61, 32), (61, 52)], W, ow=4)
    else:
        p.ell((40, 18, 70, 44), W, ow=4)
        p.ell((46, 26, 53, 33), 0x3A2A20, ow=0)
        p.ell((57, 26, 64, 33), 0x3A2A20, ow=0)
        p.line([(30, 46), (80, 56)], W, 6)
        p.line([(30, 56), (80, 46)], W, 6)
    return p.out()


def pear(big=False):
    """un figuier de Barbarie : des raquettes rondes, des epines, une fleur"""
    s = 2.0 if big else 1.0
    p = Pen(int(110 * s), int(110 * s))
    G, G2, G3 = 0x74B048, 0x4E8A34, 0xA6D670
    pads = [(55, 80, 26, 28), (30, 52, 18, 20), (78, 48, 20, 22), (52, 30, 15, 17), (92, 22, 12, 14)]
    if not big:
        pads = pads[:4]
    for cx, cy, rx, ry in pads:
        p.ell(((cx - rx) * s, (cy - ry) * s, (cx + rx) * s, (cy + ry) * s), G)
    for cx, cy, rx, ry in pads:
        p.ell(((cx - rx * 0.6) * s, (cy - ry * 0.75) * s, (cx + rx * 0.2) * s, (cy - ry * 0.1) * s), G3, ow=0)
        for a in range(0, 360, 70):
            r = math.radians(a + cx)
            x, y = cx + math.cos(r) * rx * 0.6, cy + math.sin(r) * ry * 0.6
            p.ell(((x - 1.6) * s, (y - 1.6) * s, (x + 1.6) * s, (y + 1.6) * s), G2, ow=0)
    p.ell((48 * s, 9 * s, 58 * s, 19 * s), 0xF0607A, ow=4)
    p.ell((51 * s, 12 * s, 55 * s, 16 * s), 0xFFD0D8, ow=0)
    return p.out()


def desert(src):
    MORTAR, STONE, SAND, SAND2, BACK = 0xE6C489, 0xA8683C, 0xF2D9A0, 0xE3C07C, 0x6E4426
    R = {}

    def cob(lay, seed):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(MORTAR))
        rnd = random.Random(seed)
        for i, b in enumerate(lay):			# des paves ronds, bruns, dans le mortier clair
            x0, y0, x1, y1 = b
            c = shade(rgb(STONE), rnd.uniform(0.9, 1.1))
            p.rect((x0 + 3, y0 + 3, x1 - 3, y1 - 3), shade(c, 0.8), 16)
            p.rect((x0 + 3, y0 + 3, x1 - 8, y1 - 9), c, 16, ow=0)
            p.line([(x0 + 14, y0 + 10), (x1 - 22, y0 + 10)], shade(c, 1.25), 4)
        return p

    R["cobble"] = cob(L1, 51).out()
    p = cob(L2, 52)
    # le sable lisse du dessus, epais, son trait et sa lumiere
    pts = [(0, 0), (D, 0), (D, 40)] + [(x, 40 + 3 * math.sin(x / 16.0)) for x in range(D, -1, -4)]
    p.poly(pts, SAND)
    p.poly([(0, 30), (D, 30), (D, 40)] + [(x, 40 + 3 * math.sin(x / 16.0)) for x in range(D, -1, -4)], SAND2, ow=0)
    p.line([(6, 8), (D - 6, 8)], 0xFFF2D0, 4)
    R["grass"] = p.out()
    for k, lay in enumerate((L1, L2)):		# le canyon : des pierres sombres
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x4A2C18))
        rnd = random.Random(60 + k)
        for b in lay:
            x0, y0, x1, y1 = b
            c = shade(rgb(BACK), rnd.uniform(0.9, 1.1))
            p.rect((x0 + 3, y0 + 3, x1 - 3, y1 - 3), c, 16, ow=4, ink=(52, 30, 16, 255))
            p.line([(x0 + 14, y0 + 10), (x1 - 22, y0 + 10)], shade(c, 1.2), 4)
        R["back_a" if k == 0 else "back_b"] = p.out()
    for k in (0, 1):				# l'eau de l'oasis
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x2FA4C4))
        for i in range(4):
            p.line([(14 + i * 28, 36 + i * 22), (44 + i * 28, 36 + i * 22)], 0x6AD0E8, 5)
        if k:
            p.d.rectangle([0, 0, D * SS, 12 * SS], fill=(0, 0, 0, 0))
            p.d.rectangle([0, 12 * SS, D * SS, 26 * SS], fill=rgb(0x9CE8F4))
        R["wsurf" if k else "wmid"] = p.out()

    def rocks():
        p = Pen(100, 60)
        p.ell((8, 22, 58, 58), 0xA8683C)
        p.ell((44, 30, 92, 58), 0x94592F)
        p.ell((18, 28, 36, 38), 0xC88A58, ow=0)
        return p.out()

    def bullskull():
        p = Pen(120, 70)
        B = 0xF2EADA
        p.poly([(8, 20), (30, 30), (40, 22), (18, 8)], B, ow=5)
        p.poly([(112, 20), (90, 30), (80, 22), (102, 8)], B, ow=5)
        p.poly([(36, 22), (84, 22), (78, 52), (60, 66), (42, 52)], B)
        p.ell((44, 30, 54, 40), 0x3A2A20, ow=0)
        p.ell((66, 30, 76, 40), 0x3A2A20, ow=0)
        p.ell((54, 50, 58, 56), 0x3A2A20, ow=0)
        p.ell((62, 50, 66, 56), 0x3A2A20, ow=0)
        return p.out()

    chest = fit(load(src, "item/chest-gold-close.png"), 30, 26)
    R.update(deco1=pear(), deco2=sign("right"), deco3=rocks(), deco4=sign("skull"), deco5=pear(), deco6=bullskull(),
             deco7=sign("up"), deco8=chest, big=pear(True))
    return R


# ===== la glace ===================================================================================================
def ice(src):
    ROCK, SNOW, SNOW2 = 0x7F9CBC, 0xF6FBFF, 0xC4DCF0
    R = {}

    def cob(lay, seed):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x3C5068))
        stones(p, lay, rgb(ROCK), seed)
        return p

    R["cobble"] = cob(L1, 11).out()
    p = cob(L2, 12)
    # la neige : une couche ronde et epaisse, des coulures, son ombre bleue
    pts = [(0, 0), (D, 0), (D, 28)]
    for x in range(D, -1, -4):
        drip = 10 if 30 < x < 42 else 16 if 86 < x < 96 else 0
        drip = drip * math.sin((x % 12) / 12 * math.pi) if drip else 0
        pts.append((x, 28 + 3 * math.sin(x / 9.0) + drip))
    p.poly(pts, SNOW)
    p.poly([(0, 22), (D, 22), (D, 28)] + [(x, 28 + 3 * math.sin(x / 9.0)) for x in range(D, -1, -4)], SNOW2, ow=0)
    p.line([(8, 7), (54, 6)], 0xFFFFFF, 5)
    R["grass"] = p.out()
    for k, lay in enumerate((BR1, BR2)):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x16243A))
        for i, b in enumerate(lay):
            x0, y0, x1, y1 = b
            c = shade(rgb(0x2E4A70), random.Random(20 + k * 7 + i).uniform(0.88, 1.08))
            p.rect(b, c, 6, ow=4, ink=(18, 28, 46, 255))
            p.line([(x0 + 8, y1 - 10), (x0 + 26, y0 + 10)], shade(c, 1.35), 4)	# le givre
            p.line([(x0 + 18, y1 - 8), (x0 + 30, y1 - 24)], shade(c, 1.25), 3)
        R["back_a" if k == 0 else "back_b"] = p.out()
    for k in (0, 1):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x3D93C8))
        for i in range(4):
            p.line([(10 + i * 30, 30 + i * 22), (40 + i * 30, 30 + i * 22)], 0x78C2E8, 5)
        if k:
            p.d.rectangle([0, 0, D * SS, 10 * SS], fill=(0, 0, 0, 0))
            p.d.rectangle([0, 10 * SS, D * SS, 26 * SS], fill=rgb(0xB8E6FA))
            p.rect((14, 8, 58, 20), 0xEAF8FF, 5, ow=4)		# un glacon qui flotte
        R["wsurf" if k else "wmid"] = p.out()

    def pine(big=False):
        s = 2.4 if big else 1.0
        p = Pen(int(96 * s), int(128 * s))
        p.rect((42 * s, 100 * s, 54 * s, 126 * s), 0x6B4428, 2 * s)
        for i, (w, y) in enumerate(((44, 70), (36, 44), (26, 18))):
            cx = 48
            p.poly([(cx * s, (y - 18) * s), ((cx + w) * s, (y + 34) * s), ((cx - w) * s, (y + 34) * s)], 0x2F7D5A)
            p.poly([(cx * s, (y - 18) * s), ((cx + w * 0.55) * s, (y + 8) * s), ((cx + w * 0.2) * s, (y + 4) * s),
                    ((cx - w * 0.2) * s, (y + 10) * s), ((cx - w * 0.55) * s, (y + 6) * s)], SNOW, ow=0)
            p.line([((cx - w) * s, (y + 32) * s), ((cx + w) * s, (y + 32) * s)], 0xDDEEFF, 4 * s)
        return p.out()

    def snowman():
        p = Pen(80, 120)
        p.ell((8, 56, 72, 118), SNOW)
        p.ell((18, 22, 62, 64), SNOW)
        p.ell((52, 64, 60, 70), 0x405060, ow=0)
        p.ell((46, 82, 54, 88), 0x405060, ow=0)
        p.ell((28, 34, 34, 40), 0x202830, ow=0)
        p.ell((44, 34, 50, 40), 0x202830, ow=0)
        p.poly([(38, 42), (60, 46), (38, 48)], 0xF08A30, ow=3)
        p.rect((22, 2, 58, 24), 0x303848, 3)
        p.rect((14, 20, 66, 26), 0x303848, 2)
        p.rect((20, 58, 60, 66), 0xD84040, 3, ow=4)
        p.rect((50, 60, 58, 84), 0xD84040, 3, ow=4)
        return p.out()

    def crystals():
        p = Pen(96, 96)
        for x, h, w in ((30, 70, 14), (52, 90, 18), (74, 60, 12)):
            p.poly([(x - w, 94), (x - w, 94 - h + 16), (x, 94 - h), (x + w, 94 - h + 16), (x + w, 94)], 0x8EE0FF)
            p.poly([(x - w, 94), (x - w, 94 - h + 16), (x, 94 - h), (x, 94)], 0xC8F4FF, ow=0)
        return p.out()

    def snowrock():
        p = Pen(100, 64)
        p.poly([(6, 60), (14, 30), (40, 14), (66, 20), (90, 36), (96, 60)], 0x8DA4BE)
        p.poly([(14, 30), (40, 12), (66, 18), (90, 34), (70, 34), (50, 26), (30, 32)], SNOW, ow=0)
        return p.out()

    crate = fit(load(src, "item/crate.png"), 30, 30)
    cp = Pen(D, D)
    cap = cp.out()
    c2 = Image.new("RGBA", (30, 34))
    c2.alpha_composite(crate, (0, 4))
    d = ImageDraw.Draw(c2)
    d.rounded_rectangle((0, 1, 29, 8), radius=3, fill=rgb(SNOW), outline=INK, width=1)
    R.update(deco1=pine(), deco2=snowman(), deco3=crystals(), deco4=snowrock(), deco5=pine(), deco6=c2, deco7=crystals(),
             deco8=snowrock(), big=pine(True))
    return R


# ===== le volcan ==================================================================================================
def volcano(src):
    BAS, CRUST, LAVA, LAVA2 = 0x4B3B3E, 0x7A2E22, 0xFF8A1E, 0xFFD34A
    R = {}

    def cob(lay, seed):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0xE85A12))		# entre les pierres : la lave
        stones(p, lay, rgb(BAS), seed)
        return p

    R["cobble"] = cob(L1, 31).im.filter(ImageFilter.SMOOTH).resize((TP, TP), Image.LANCZOS)
    p = cob(L2, 32)
    pts = [(0, 0), (D, 0), (D, 22)] + [(x, 22 + 5 * math.sin(x / 7.0) + (6 if x % 40 < 10 else 0)) for x in range(D, -1, -4)]
    p.poly(pts, CRUST)
    for x in (16, 52, 96):
        p.ell((x, 6, x + 7, 12), LAVA2, ow=0)
    p.line([(8, 6), (40, 5)], 0xC4583A, 4)
    R["grass"] = p.out()
    for k, lay in enumerate((L1, L2)):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x5A1A10))
        for i, b in enumerate(lay):
            x0, y0, x1, y1 = b
            c = shade(rgb(0x2E2224), random.Random(40 + k * 9 + i).uniform(0.9, 1.1))
            p.rect(b, c, 14, ow=3, ink=(20, 12, 12, 255))
            p.line([(x0 + 10, y0 + 7), (x1 - 16, y0 + 7)], shade(c, 1.3), 4)
        R["back_a" if k == 0 else "back_b"] = p.out()
    for k in (0, 1):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(LAVA))
        for i in range(5):
            x, y = (i * 47) % 100 + 8, 20 + i * 21
            p.ell((x, y, x + 18, y + 10), LAVA2, ow=0)
            p.ell((x + 50, y + 4, x + 64, y + 12), 0xD8501A, ow=0)
        if k:
            p.d.rectangle([0, 0, D * SS, 10 * SS], fill=(0, 0, 0, 0))
            pts = [(0, 10)] + [(x, 10 + 4 * math.sin(x / 8.0)) for x in range(0, D + 1, 8)] + [(D, 24), (0, 24)]
            p.poly(pts, LAVA2, ow=0)
            p.ell((70, 2, 86, 16), LAVA2, ow=4, ink=(150, 50, 10, 255))	# une bulle
        R["wsurf" if k else "wmid"] = p.out()

    def deadtree(big=False):
        s = 2.4 if big else 1.0
        p = Pen(int(100 * s), int(128 * s))
        B, B2 = 0x2A1E1E, 0x4A3434
        branches = [((50, 126), (50, 60)), ((50, 80), (22, 50)), ((22, 50), (12, 30)), ((50, 64), (78, 36)), ((78, 36), (92, 30)),
                    ((50, 60), (46, 22)), ((46, 22), (56, 8)), ((22, 50), (34, 38))]
        for (a, b) in branches:
            w = 12 if a[1] > 90 else 8 if a[1] > 55 else 6
            p.line([(a[0] * s, a[1] * s), (b[0] * s, b[1] * s)], 0x0E0808, (w + 6) * s)
        for (a, b) in branches:
            w = 12 if a[1] > 90 else 8 if a[1] > 55 else 6
            p.line([(a[0] * s, a[1] * s), (b[0] * s, b[1] * s)], B2, w * s)
        if big:
            for x, y in ((12, 30), (92, 30), (56, 8)):
                p.ell(((x - 3) * s, (y - 3) * s, (x + 3) * s, (y + 3) * s), LAVA, ow=0)
        return p.out()

    def redcrystals():
        p = Pen(96, 96)
        for x, h, w in ((30, 60, 12), (52, 88, 16), (74, 70, 12)):
            p.poly([(x - w, 94), (x - w, 94 - h + 14), (x, 94 - h), (x + w, 94 - h + 14), (x + w, 94)], 0xE8304A)
            p.poly([(x - w, 94), (x - w, 94 - h + 14), (x, 94 - h), (x, 94)], 0xFF8090, ow=0)
        return p.out()

    def vent():
        p = Pen(100, 120)
        p.poly([(8, 118), (26, 70), (42, 62), (58, 62), (74, 70), (92, 118)], 0x3E3032)
        p.ell((38, 58, 62, 70), LAVA, ow=4)
        for i, (x, y, r) in enumerate(((50, 40, 12), (42, 20, 10), (56, 6, 8))):
            p.ell((x - r, y - r, x + r, y + r), (150, 140, 140, 200), ow=0)
        p.line([(30, 100), (44, 80)], LAVA, 4)
        return p.out()

    def magmarock():
        p = Pen(100, 64)
        p.poly([(6, 60), (14, 30), (40, 14), (66, 20), (90, 36), (96, 60)], 0x3A2C2E)
        p.line([(20, 50), (36, 34), (52, 40), (70, 26)], LAVA, 5)
        p.line([(52, 40), (60, 56)], LAVA, 4)
        return p.out()

    def skulls():
        p = Pen(110, 60)
        B = 0xD9CCB8
        for x in (20, 54, 88):
            p.ell((x - 18, 14, x + 18, 50), B)
            p.ell((x - 10, 24, x - 2, 34), 0x281818, ow=0)
            p.ell((x + 2, 24, x + 10, 34), 0x281818, ow=0)
        return p.out()

    R.update(deco1=deadtree(), deco2=redcrystals(), deco3=vent(), deco4=magmarock(), deco5=skulls(), deco6=magmarock(),
             deco7=redcrystals(), deco8=deadtree(), big=deadtree(True))
    return R


# ===== la foret hantee ============================================================================================
def spooky(src):
    STONE, MORT, MOSS, MOSS2, SLIME, SLIME2 = 0x6C5C8A, 0x261A38, 0x4A3168, 0x7A55A0, 0x7CD43A, 0xC8F57A
    R = {}

    def cob(lay, seed, glow=True):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(MORT))
        stones(p, lay, rgb(STONE), seed)
        if glow:					# une mousse verte qui luit sur une pierre
            rnd = random.Random(seed)
            b = lay[rnd.randrange(len(lay))]
            x, y = b[0] + 10, b[3] - 18
            p.ell((x, y, x + 16, y + 9), SLIME, ow=0)
            p.ell((x + 3, y + 2, x + 9, y + 5), SLIME2, ow=0)
        return p

    R["cobble"] = cob(L1, 71).out()
    p = cob(L2, 72, False)
    # la mousse violette du dessus, epaisse, et des coulures de bave verte
    pts = [(0, 0), (D, 0), (D, 26)]
    for x in range(D, -1, -4):
        drip = 14 if 22 < x < 32 else 9 if 80 < x < 88 else 0
        drip = drip * math.sin((x % 10) / 10 * math.pi) if drip else 0
        pts.append((x, 26 + 4 * math.sin(x / 7.0) + drip))
    p.poly(pts, MOSS)
    p.poly([(0, 0), (D, 0), (D, 12)] + [(x, 12 + 3 * math.sin(x / 6.0)) for x in range(D, -1, -4)], MOSS2, ow=0)
    for x in (26, 84):
        p.ell((x - 4, 34, x + 6, 44), SLIME, ow=3)
    for x in (12, 58, 104):				# des brins d'herbe morte
        p.line([(x, 4), (x - 4, -6)], MOSS2, 4)
        p.line([(x + 6, 4), (x + 9, -4)], MOSS2, 4)
    R["grass"] = p.out()
    for k, lay in enumerate((BR1, BR2)):		# la crypte : des briques violettes, fendues
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x150E22))
        for i, b in enumerate(lay):
            x0, y0, x1, y1 = b
            c = shade(rgb(0x3A2C52), random.Random(80 + k * 7 + i).uniform(0.85, 1.1))
            p.rect(b, c, 6, ow=4, ink=(16, 10, 26, 255))
            p.line([(x0 + 8, y0 + 7), (x1 - 12, y0 + 7)], shade(c, 1.3), 4)
            if (i + k) % 3 == 0:
                p.line([(x0 + 20, y0 + 12), (x0 + 30, y0 + 26), (x0 + 24, y0 + 38)], shade(c, 0.55), 3)
        R["back_a" if k == 0 else "back_b"] = p.out()
    for k in (0, 1):				# la bave verte qui bouillonne
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0x3E9A2A))
        for i in range(5):
            x, y = (i * 43) % 104 + 8, 22 + i * 21
            p.ell((x, y, x + 14, y + 14), 0x62C23C, ow=0)
            p.ell((x + 3, y + 3, x + 7, y + 7), SLIME2, ow=0)
        if k:
            p.d.rectangle([0, 0, D * SS, 10 * SS], fill=(0, 0, 0, 0))
            pts = [(0, 12)] + [(x, 12 + 4 * math.sin(x / 9.0)) for x in range(0, D + 1, 8)] + [(D, 26), (0, 26)]
            p.poly(pts, SLIME, ow=0)
            p.ell((64, 2, 82, 18), SLIME2, ow=4, ink=(40, 110, 30, 255))	# une bulle
        R["wsurf" if k else "wmid"] = p.out()

    def pumpkin(face=True, s=1.0):
        p = Pen(int(100 * s), int(90 * s))
        O, O2, O3 = 0xF08A1E, 0xC85E10, 0xFFB850
        p.rect((46 * s, 2 * s, 56 * s, 22 * s), 0x4E6A2A, 3 * s)
        for x0, x1 in ((6, 52), (48, 94), (26, 74)):
            p.ell((x0 * s, 16 * s, x1 * s, 86 * s), O if x0 == 26 else O2)
        p.ell((34 * s, 22 * s, 48 * s, 40 * s), O3, ow=0)
        if face:
            G = 0xFFE860
            p.poly([(28 * s, 46 * s), (40 * s, 36 * s), (42 * s, 50 * s)], G, ow=3)
            p.poly([(72 * s, 46 * s), (60 * s, 36 * s), (58 * s, 50 * s)], G, ow=3)
            p.poly([(26 * s, 60 * s), (74 * s, 60 * s), (66 * s, 74 * s), (56 * s, 68 * s), (50 * s, 76 * s), (44 * s, 68 * s), (34 * s, 74 * s)], G, ow=3)
        return p.out()

    def grave(cross=False):
        p = Pen(80, 110)
        G, G2 = 0x7A7890, 0x9C9AB0
        if cross:
            p.rect((32, 6, 48, 104), G, 3)
            p.rect((12, 28, 68, 44), G, 3)
            p.line([(36, 12), (36, 98)], G2, 4)
        else:
            p.poly([(10, 106), (10, 34), (18, 14), (40, 4), (62, 14), (70, 34), (70, 106)], G)
            p.line([(18, 34), (24, 20), (40, 12)], G2, 5)
            p.line([(26, 48), (54, 48)], 0x4E4C62, 5)
            p.line([(26, 62), (54, 62)], 0x4E4C62, 5)
            p.line([(48, 72), (40, 86), (46, 96)], 0x4E4C62, 3)
        p.ell((4, 96, 76, 110), MOSS, ow=4)
        return p.out()

    def shrooms():
        p = Pen(100, 80)
        for x, h, r in ((26, 40, 20), (64, 56, 24), (86, 28, 12)):
            p.rect((x - 5, 78 - h, x + 5, 78), 0xE8DCC8, 3)
            p.d.pieslice([(x - r) * SS, (78 - h - r) * SS, (x + r) * SS, (78 - h + r) * SS], 180, 360, fill=INK)
            p.d.pieslice([(x - r + 4) * SS, (78 - h - r + 4) * SS, (x + r - 4) * SS, (78 - h + r - 4) * SS], 180, 360, fill=rgb(0x9A4AC8))
            p.ell((x - r * 0.5, 78 - h - r * 0.6, x - r * 0.2, 78 - h - r * 0.3), SLIME2, ow=0)
            p.ell((x + r * 0.2, 78 - h - r * 0.45, x + r * 0.5, 78 - h - r * 0.15), SLIME2, ow=0)
        return p.out()

    def fence():
        p = Pen(120, 80)
        p.rect((4, 34, 116, 44), 0x4A3C52, 2)
        p.rect((4, 58, 116, 68), 0x4A3C52, 2)
        for i, x in enumerate((12, 40, 68, 96)):
            h = 10 if i % 2 else 2
            p.poly([(x, 78), (x, 14 + h), (x + 7, 4 + h), (x + 14, 14 + h), (x + 14, 78)], 0x5E4E68)
        return p.out()

    def lantern():
        p = Pen(70, 120)
        p.rect((30, 30, 40, 118), 0x2E2236, 2)
        p.line([(35, 30), (58, 22)], 0x2E2236, 6)
        p.rect((46, 24, 66, 52), 0x2E2236, 4)
        p.rect((50, 30, 62, 48), 0xFFD860, 3, ow=0)
        p.ell((52, 34, 58, 42), 0xFFF4C0, ow=0)
        return p.out()

    def deadtree(big=False):
        s = 2.4 if big else 1.0
        p = Pen(int(110 * s), int(128 * s))
        B, B2 = 0x2A1E34, 0x4A3A58
        branches = [((55, 126), (50, 92)), ((50, 92), (58, 62)), ((58, 62), (48, 40)), ((58, 62), (86, 46)), ((86, 46), (100, 50)),
                    ((48, 40), (24, 30)), ((24, 30), (12, 38)), ((48, 40), (56, 14)), ((56, 14), (70, 8)), ((50, 92), (28, 78))]
        for (a, b) in branches:
            w = 14 if a[1] > 100 else 10 if a[1] > 60 else 6
            p.line([(a[0] * s, a[1] * s), (b[0] * s, b[1] * s)], 0x0E0814, (w + 6) * s)
        for (a, b) in branches:
            w = 14 if a[1] > 100 else 10 if a[1] > 60 else 6
            p.line([(a[0] * s, a[1] * s), (b[0] * s, b[1] * s)], B2, w * s)
        if big:					# du feuillage violet en touffes, comme sur la capture
            for x, y, r in ((24, 28, 16), (56, 12, 18), (86, 44, 16), (40, 40, 12), (100, 50, 10), (70, 26, 12)):
                p.ell(((x - r) * s, (y - r * 0.7) * s, (x + r) * s, (y + r * 0.7) * s), 0x8A3CA8)
                p.ell(((x - r * 0.6) * s, (y - r * 0.6) * s, (x + r * 0.1) * s, (y - r * 0.1) * s), 0xB86AD4, ow=0)
        return p.out()

    R.update(deco1=pumpkin(), deco2=grave(), deco3=shrooms(), deco4=fence(), deco5=pumpkin(False), deco6=lantern(),
             deco7=grave(True), deco8=deadtree(), big=deadtree(True))
    return R


# ===== la vallee de la mort =======================================================================================
def valley(src):
    ROCK, ROCK2, EARTH, EARTH2, LAVA, LAVA2 = 0x4A3024, 0x5E4030, 0xB07A44, 0xD09A5C, 0xFF8A1E, 0xFFD24A
    R = {}

    def cob(lay, seed):
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0xE8661A))		# entre les pierres : un fil de lave qui luit
        rnd = random.Random(seed)
        for i, b in enumerate(lay):
            x0, y0, x1, y1 = b
            c = shade(rgb(ROCK), rnd.uniform(0.85, 1.05))
            p.rect((x0 + 1, y0 + 1, x1 - 1, y1 - 1), c, 12, ow=4, ink=(40, 22, 14, 255))
            p.rect((x0 + 3, y0 + 3, x1 - 7, y1 - 9), shade(rgb(ROCK2), rnd.uniform(0.9, 1.05)), 12, ow=0)
            p.line([(x0 + 12, y0 + 9), (x1 - 20, y0 + 9)], 0x8E6A4E, 4)
        return p

    R["cobble"] = cob(L1, 91).out()
    p = cob(L2, 92)
    # la croute de terre cuite du dessus, ses couches et ses cailloux
    pts = [(0, 0), (D, 0), (D, 30)] + [(x, 30 + 3 * math.sin(x / 11.0) + (4 if x % 46 < 12 else 0)) for x in range(D, -1, -4)]
    p.poly(pts, EARTH)
    p.poly([(0, 0), (D, 0), (D, 10)] + [(x, 10 + 2 * math.sin(x / 9.0)) for x in range(D, -1, -4)], EARTH2, ow=0)
    p.line([(4, 20), (D - 4, 21)], 0x8E5A30, 3)
    for x in (18, 70, 108):
        p.ell((x, 14, x + 9, 20), 0x7A4E2E, ow=0)
    R["grass"] = p.out()
    for k in (0, 1):				# la paroi du canyon : des strates rouges
        p = Pen(D, D)
        cols = (0x6A3A28, 0x7E4A30, 0x5A3020, 0x744230)
        for i in range(4):
            y0 = i * 32
            p.d.rectangle([0, y0 * SS, D * SS, (y0 + 32) * SS], fill=rgb(cols[(i + k) % 4]))
            p.line([(0, y0 + 1), (D, y0 + 1)], 0x3A1E14, 3)
            p.line([(6 + k * 30, y0 + 9), (60 + k * 30, y0 + 9)], 0x9A5E3E, 3)
        for x, y in ((20 + k * 40, 50), (90 - k * 30, 100)):
            p.line([(x, y), (x + 8, y + 12), (x + 4, y + 22)], 0x3A1E14, 3)
        R["back_a" if k == 0 else "back_b"] = p.out()
    for k in (0, 1):				# la lave
        p = Pen(D, D)
        p.d.rectangle([0, 0, D * SS, D * SS], fill=rgb(0xF06A14))
        for i in range(5):
            x, y = (i * 47) % 100 + 8, 20 + i * 21
            p.ell((x, y, x + 20, y + 10), LAVA2, ow=0)
            p.ell((x + 50, y + 4, x + 64, y + 12), 0xC8400E, ow=0)
        if k:
            p.d.rectangle([0, 0, D * SS, 10 * SS], fill=(0, 0, 0, 0))
            pts = [(0, 10)] + [(x, 10 + 4 * math.sin(x / 8.0)) for x in range(0, D + 1, 8)] + [(D, 24), (0, 24)]
            p.poly(pts, LAVA2, ow=0)
        R["wsurf" if k else "wmid"] = p.out()

    def bullskull():
        p = Pen(120, 70)
        B = 0xF2EADA
        p.poly([(8, 20), (30, 30), (40, 22), (18, 8)], B, ow=5)
        p.poly([(112, 20), (90, 30), (80, 22), (102, 8)], B, ow=5)
        p.poly([(36, 22), (84, 22), (78, 52), (60, 66), (42, 52)], B)
        for b in ((44, 30, 54, 40), (66, 30, 76, 40), (54, 50, 58, 56), (62, 50, 66, 56)):
            p.ell(b, 0x3A2A20, ow=0)
        return p.out()

    def rocks():
        p = Pen(100, 60)
        p.ell((8, 22, 58, 58), 0x7E5236)
        p.ell((44, 30, 92, 58), 0x6A4430)
        p.ell((18, 28, 36, 38), 0xA8784E, ow=0)
        p.line([(56, 40), (66, 52)], LAVA, 4)
        return p.out()

    def tumbleweed():
        p = Pen(80, 70)
        p.ell((6, 6, 74, 66), (0, 0, 0, 0), ow=0)
        for a in range(0, 360, 24):
            r = math.radians(a)
            p.line([(40, 36), (40 + 30 * math.cos(r), 36 + 28 * math.sin(r))], 0x8A6A3A, 4)
        p.d.ellipse([10 * SS, 8 * SS, 70 * SS, 64 * SS], outline=rgb(0x6A4A26), width=5 * SS)
        p.d.ellipse([20 * SS, 16 * SS, 60 * SS, 56 * SS], outline=rgb(0xA88450), width=4 * SS)
        return p.out()

    def bones():
        p = Pen(110, 50)
        B = 0xEDE2CC
        for (x0, y0, x1, y1) in ((12, 34, 70, 20), (40, 42, 100, 38)):
            p.line([(x0, y0), (x1, y1)], INK, 14)
            for x, y in ((x0, y0), (x1, y1)):
                p.ell((x - 8, y - 8, x + 8, y + 8), B, ow=4)
            p.line([(x0, y0), (x1, y1)], B, 8)
        return p.out()

    def deadtree(big=False):
        s = 2.4 if big else 1.0
        p = Pen(int(110 * s), int(128 * s))
        B2 = 0x6E4A30
        branches = [((55, 126), (52, 80)), ((52, 80), (30, 52)), ((30, 52), (16, 46)), ((52, 80), (74, 50)), ((74, 50), (96, 40)),
                    ((74, 50), (70, 24)), ((30, 52), (34, 26)), ((52, 80), (52, 50))]
        for (a, b) in branches:
            w = 14 if a[1] > 100 else 9 if a[1] > 60 else 6
            p.line([(a[0] * s, a[1] * s), (b[0] * s, b[1] * s)], 0x1E120A, (w + 6) * s)
        for (a, b) in branches:
            w = 14 if a[1] > 100 else 9 if a[1] > 60 else 6
            p.line([(a[0] * s, a[1] * s), (b[0] * s, b[1] * s)], B2, w * s)
            p.line([((a[0] - 1.5) * s, a[1] * s), ((b[0] - 1.5) * s, b[1] * s)], 0x9A6E48, max(1, w * 0.3) * s)
        return p.out()

    barrel = fit(load(src, "item/barrel.png"), 26, 30)
    crate = fit(load(src, "item/crate.png"), 30, 30)
    R.update(deco1=barrel, deco2=sign("right"), deco3=bullskull(), deco4=rocks(), deco5=tumbleweed(), deco6=crate,
             deco7=sign("skull"), deco8=bones(), big=deadtree(True))
    return R


# ===== les fonds ==================================================================================================
def grad(W, H, stops):
    im = Image.new("RGBA", (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / (H - 1) * (len(stops) - 1)
        i = min(int(t), len(stops) - 2)
        u = t - i
        a, b = stops[i], stops[i + 1]
        d.line([(0, y), (W, y)], fill=tuple(int(a[k] + (b[k] - a[k]) * u) for k in range(3)) + (255,))
    return im


def hills(W, y, amp, period, col, seed, jag=0.0):
    """une bande de collines sans couture (periodes qui divisent 1024)"""
    pts = [(0, 480)]
    rnd = random.Random(seed)
    ph = [rnd.uniform(0, 6.28) for _ in range(3)]
    for x in range(0, W + 1, 4):
        v = y - amp * (0.6 * math.sin(x / W * 6.2832 * period + ph[0]) + 0.3 * math.sin(x / W * 6.2832 * period * 2 + ph[1]) +
                       0.1 * math.sin(x / W * 6.2832 * period * 5 + ph[2]))
        if jag:
            v -= jag * abs(((x * period * 2) % 256) - 128) / 128 * amp
        pts.append((x, v))
    pts.append((W, 480))
    im = Image.new("RGBA", (W, 480))
    ImageDraw.Draw(im).polygon(pts, fill=rgb(col))
    return im


def background(src, world):
    W, H = 1024, 480
    if world == 0:				# la jungle : le ciel, les arbres et les buissons du paquet
        bg = Image.new("RGBA", (W, H))
        bg.alpha_composite(load(src, "bg/sky.png").resize((1, H), Image.LANCZOS).resize((W, H)))
        bush, bush2 = load(src, "bg/bush.png"), load(src, "bg/bush-2.png")
        tree, tree2 = load(src, "bg/tree.png"), load(src, "bg/tree-2.png")

        def band(img, y, h, dark):
            im = img.resize((256, h), Image.LANCZOS)
            if dark != 1:
                im = Image.eval(im, lambda v: v)
                px = im.load()
                for yy in range(im.height):
                    for xx in range(im.width):
                        r, g, b, a = px[xx, yy]
                        px[xx, yy] = (int(r * dark), int(g * dark), int(b * dark), a)
            for x in range(0, W, 256):
                bg.alpha_composite(im, (x, y))
        band(bush2, 170, 180, 1.05)
        for i, x in enumerate(range(40, W, 205)):
            t = (tree if i % 2 else tree2).resize((46, 300), Image.LANCZOS)
            bg.alpha_composite(t, (x, 150))
            crown = (bush if i % 2 else bush2).resize((200, 150), Image.LANCZOS)
            m = Image.new("L", crown.size, 0)
            ImageDraw.Draw(m).ellipse((0, 0, 199, 149), fill=255)
            c2 = Image.new("RGBA", crown.size)
            c2.paste(crown, (0, 0), Image.composite(crown.getchannel("A"), Image.new("L", crown.size, 0), m))
            for dx, dy in ((-60, 70), (60, 60), (0, 0)):
                xx = (x + 22 + dx - 100) % W
                bg.alpha_composite(c2, (xx, 70 + dy))
                if xx > W - 200:
                    bg.alpha_composite(c2, (xx - W, 70 + dy))
        band(bush, 330, 150, 0.85)
        band(bush2, 400, 80, 0.65)
        return bg
    if world == 1:				# le desert : ciel ocre, nuages en bandes, dunes en couches, pyramides au loin
        bg = grad(W, H, [(238, 196, 104), (246, 214, 130), (250, 228, 160)])
        d = ImageDraw.Draw(bg)
        for i, (y, a) in enumerate(((60, 16), (110, 12), (150, 10))):	# les nuages : de longues bandes ondulees
            pts = [(x, y + a * math.sin(x / W * 6.2832 * (2 + i) + i)) for x in range(0, W + 1, 8)]
            d.line(pts, fill=(252, 236, 186, 255), width=14 - 3 * i, joint="curve")
        d = ImageDraw.Draw(bg)
        for px_, h in ((190, 130), (330, 210), (760, 165)):	# (jusqu'en bas, derriere les dunes : le pied toujours cache)
            e = h + (H - 330)
            d.polygon([(px_ - e, H), (px_, 330 - h), (px_ + e, H)], fill=(226, 160, 92, 255))
            d.polygon([(px_, 330 - h), (px_ + e, H), (px_, H)], fill=(200, 132, 72, 255))
        bg.alpha_composite(hills(W, 335, 26, 3, 0xE9B46A, 11))
        bg.alpha_composite(hills(W, 375, 32, 2, 0xEFA650, 12))
        dl = ImageDraw.Draw(bg)
        for k in range(3):				# les lignes du vent sur les dunes
            pts = [(x, 384 + k * 18 + 8 * math.sin(x / W * 6.2832 * 3 + k)) for x in range(0, W + 1, 8)]
            dl.line(pts, fill=(214, 128, 52, 255), width=4, joint="curve")
        bg.alpha_composite(hills(W, 445, 20, 3, 0xF4BE66, 13))
        return bg
    if world == 2:				# le temple : couchant, soleil, pyramides, dunes
        bg = grad(W, H, [(92, 52, 110), (232, 120, 96), (255, 196, 120), (255, 226, 160)])
        d = ImageDraw.Draw(bg)
        d.ellipse((690, 120, 790, 220), fill=(255, 236, 170, 255))
        for px_, h in ((180, 170), (320, 240), (840, 160)):	# (jusqu'en bas, derriere les dunes : le pied toujours cache)
            d = ImageDraw.Draw(bg)
            e = h + (H - 340)
            d.polygon([(px_ - e, H), (px_, 340 - h), (px_ + e, H)], fill=(214, 140, 92, 255))
            d.polygon([(px_, 340 - h), (px_ + e, H), (px_, H)], fill=(180, 108, 72, 255))
        bg.alpha_composite(hills(W, 340, 24, 2, 0xD8946A, 1))
        bg.alpha_composite(hills(W, 380, 22, 3, 0xE6A860, 2))
        for x in range(60, W, 170):			# des cactus au loin
            d.rectangle((x, 345, x + 8, 395), fill=(110, 120, 70, 255))
            d.rectangle((x - 10, 361, x, 367), fill=(110, 120, 70, 255))
            d.rectangle((x - 10, 351, x - 4, 367), fill=(110, 120, 70, 255))
        bg.alpha_composite(hills(W, 440, 18, 4, 0xF0BE74, 3))
        return bg
    if world == 3:				# la glace : montagnes enneigees, sapins
        bg = grad(W, H, [(70, 120, 190), (150, 200, 240), (220, 240, 255)])
        d = ImageDraw.Draw(bg)
        d.ellipse((800, 60, 870, 130), fill=(255, 255, 235, 255))
        m = hills(W, 260, 90, 3, 0xA8C0DC, 4, jag=0.6)
        bg.alpha_composite(m)
        snow = hills(W, 262, 90, 3, 0xF4FAFF, 4, jag=0.6)
        cut = hills(W, 300, 70, 3, 0xFFFFFF, 4, jag=0.6)
        sm = Image.composite(snow, Image.new("RGBA", (W, H)), Image.eval(cut.getchannel("A"), lambda v: 255 - v))
        bg.alpha_composite(sm)
        bg.alpha_composite(hills(W, 330, 30, 2, 0xD6E6F6, 5))
        d = ImageDraw.Draw(bg)
        for x in range(0, W, 42):			# une foret de sapins
            h = 60 + (x * 7) % 40
            d.polygon([(x, 380), (x + 21, 380 - h), (x + 42, 380)], fill=(60, 110, 120, 255))
        bg.alpha_composite(hills(W, 400, 16, 4, 0xEEF6FF, 6))
        return bg
    if world == 5:				# la foret hantee : nuit violette, pleine lune, nuages, arbres tordus, tombes
        bg = grad(W, H, [(24, 14, 44), (52, 30, 80), (92, 56, 120)])
        d = ImageDraw.Draw(bg)
        rnd = random.Random(5)
        for _ in range(90):				# les etoiles
            x, y = rnd.randrange(W), rnd.randrange(260)
            r = rnd.choice((1, 1, 2))
            d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 240, 210, 255))
        halo = Image.new("RGBA", (W, H))
        hd = ImageDraw.Draw(halo)
        for r in range(150, 70, -8):
            hd.ellipse((760 - r, 120 - r, 760 + r, 120 + r), fill=(240, 220, 170, int(22 * (150 - r) / 80)))
        bg.alpha_composite(halo)
        d = ImageDraw.Draw(bg)
        d.ellipse((700, 60, 820, 180), fill=(246, 236, 180, 255))
        for x, y, r in ((730, 92, 12), (782, 124, 16), (752, 150, 8), (796, 84, 7)):
            d.ellipse((x - r, y - r, x + r, y + r), fill=(222, 206, 150, 255))
        for i, (y, col) in enumerate(((130, (150, 80, 160)), (200, (120, 62, 140)))):	# des nuages violets, en longues bandes
            for x0 in range(-100 + i * 140, W + 100, 330):
                for dx, dy, rx, ry in ((0, 0, 70, 18), (50, -12, 50, 20), (100, 2, 60, 16), (-40, 6, 40, 12)):
                    for xx in (x0 + dx, x0 + dx - W, x0 + dx + W):
                        d.ellipse((xx - rx, y + dy - ry, xx + rx, y + dy + ry), fill=col + (255,))
        bg.alpha_composite(hills(W, 330, 40, 2, 0x3A2656, 51))
        d = ImageDraw.Draw(bg)

        def tree(x, base, h, col):			# un arbre tordu, sans feuilles
            pts = [(x, base), (x - 4, base - h * 0.4), (x + 6, base - h * 0.7), (x - 2, base - h)]
            d.line(pts, fill=col, width=10, joint="curve")
            d.line([(x - 4, base - h * 0.45), (x - 34, base - h * 0.7), (x - 46, base - h * 0.64)], fill=col, width=6, joint="curve")
            d.line([(x + 5, base - h * 0.7), (x + 36, base - h * 0.86), (x + 44, base - h * 0.8)], fill=col, width=5, joint="curve")
            d.line([(x - 2, base - h), (x + 14, base - h * 1.12)], fill=col, width=4)
        for i, x in enumerate(range(70, W, 240)):
            tree(x, 360, 120 + (i % 3) * 30, (36, 22, 52, 255))
        bg.alpha_composite(hills(W, 385, 22, 3, 0x241834, 52))
        d = ImageDraw.Draw(bg)
        for x in range(30, W, 150):				# des tombes et une grille au loin
            d.rounded_rectangle((x, 380, x + 18, 404), radius=7, fill=(28, 18, 40, 255))
            d.rectangle((x + 60, 388, x + 64, 404), fill=(28, 18, 40, 255))
            d.rectangle((x + 54, 392, x + 70, 396), fill=(28, 18, 40, 255))
        fog = Image.new("RGBA", (W, H))
        fd = ImageDraw.Draw(fog)
        for k in range(14):
            fd.rectangle((0, 400 + k * 3, W, 403 + k * 3), fill=(170, 140, 200, 6 + k * 3))
        bg.alpha_composite(fog)
        bg.alpha_composite(hills(W, 450, 14, 4, 0x1A1028, 53))
        return bg
    if world == 6:				# la vallee de la mort : couchant ocre, mesas, aiguilles, lave au fond
        bg = grad(W, H, [(214, 104, 44), (240, 150, 64), (252, 206, 120), (255, 226, 160)])
        d = ImageDraw.Draw(bg)
        d.ellipse((150, 200, 250, 300), fill=(255, 240, 190, 255))

        def mesa(x, top, w, col, cap):
            for xx in (x, x - W, x + W):
                d.polygon([(xx - w - 40, H), (xx - w, top + 12), (xx - w + 8, top), (xx + w - 8, top), (xx + w, top + 12), (xx + w + 40, H)], fill=col)
                d.rectangle((xx - w + 4, top, xx + w - 4, top + 8), fill=cap)
                for k in range(1, 4):			# les strates
                    y = top + 26 * k
                    hw = w + 40 * (y - top - 12) / (H - top - 12) - 3
                    d.line([(xx - hw, y), (xx + hw, y)], fill=tuple(max(0, v - 18) for v in col[:3]) + (255,), width=3)
        for x, top, w in ((120, 230, 90), (430, 200, 120), (720, 245, 70), (900, 215, 100)):
            mesa(x, top, w, (196, 112, 64, 255), (226, 150, 90, 255))
        for x, h in ((300, 150), (330, 110), (600, 130), (980, 120)):	# des aiguilles de roche
            for xx in (x, x - W, x + W):
                d.polygon([(xx - 16, 380), (xx - 9, 380 - h), (xx + 9, 380 - h + 6), (xx + 16, 380)], fill=(168, 90, 52, 255))
        for x, top, w in ((260, 285, 110), (620, 300, 140), (980, 280, 90)):
            mesa(x, top, w, (150, 78, 44, 255), (186, 108, 64, 255))
        bg.alpha_composite(hills(W, 400, 20, 3, 0x6E3A22, 61))
        d = ImageDraw.Draw(bg)
        for x in range(0, W, 128):				# des fissures de lave dans la plaine
            d.line([(x + 10, 430), (x + 40, 420), (x + 70, 432), (x + 110, 424)], fill=(255, 130, 30, 255), width=3)
        bg.alpha_composite(hills(W, 452, 10, 4, 0x4A2416, 62))
        d = ImageDraw.Draw(bg)
        d.rectangle((0, 466, W, H), fill=(255, 120, 30, 255))
        return bg
    # le volcan : nuit rouge, cendres, deux volcans aux flancs ravines, coulees sinueuses, panache eclaire par-dessous
    return volcano_bg(W, H)


def volcano_bg(W, H):
    K = 2					# (dessine deux fois plus grand, puis reduit : des bords lisses)
    bg = grad(W * K, H * K, [(22, 8, 26), (58, 16, 34), (128, 36, 34), (206, 82, 40)])
    rnd = random.Random(44)

    def soft(layer, r):
        return layer.filter(ImageFilter.GaussianBlur(r * K))

    def wrap(fn):				# (sans couture : dessine a x, x - W, x + W)
        for o in (-W, 0, W):
            fn(o)

    # des nuages de cendre, en longues nappes
    ash = Image.new("RGBA", bg.size)
    da = ImageDraw.Draw(ash)
    for y, col in ((70, (90, 40, 52, 150)), (130, (110, 46, 50, 120)), (190, (150, 64, 50, 90))):
        for i in range(10):
            x = rnd.uniform(0, W)
            rx, ry = rnd.uniform(60, 140), rnd.uniform(10, 22)
            wrap(lambda o: da.ellipse(((x + o - rx) * K, (y - ry) * K, (x + o + rx) * K, (y + ry) * K), fill=col))
    bg.alpha_composite(soft(ash, 8))

    def profile(cx, half, top, base, crater, seed):
        """la silhouette : des flancs concaves (plus raides en haut), bosseles, un cratere aux levres inegales"""
        r = random.Random(seed)
        ph = [r.uniform(0, 6.28) for _ in range(4)]
        pts = []
        for i in range(-60, 61):
            u = i / 60.0				# -1 .. 1
            x = cx + u * half
            a = abs(u)
            if a < crater:				# le cratere : un creux entre deux levres
                lip = top + 6 + (4 if u < 0 else 0)
                y = lip + 5 * (1 - (a / crater) ** 2)
            else:
                t = (a - crater) / (1 - crater)
                y = top + (base - top) * (1 - (1 - t) ** 1.8)
                y += 9 * math.sin(u * 7 + ph[0]) * t + 4 * math.sin(u * 19 + ph[1]) * t + 2.5 * math.sin(u * 41 + ph[2])
                if 0.45 < u < 0.7:			# un epaulement (un vieux cratere) sur le flanc droit
                    y -= 22 * math.sin((u - 0.45) / 0.25 * math.pi) * t
            pts.append((x, y))
        return pts

    def volcano(cx, half, top, base, crater, seed, body, light, dark, flows, glow):
        pts = profile(cx, half, top, base, crater, seed)
        mask = Image.new("L", bg.size, 0)
        ImageDraw.Draw(mask).polygon([(x * K, y * K) for x, y in pts] + [((cx + half) * K, H * K), ((cx - half) * K, H * K)], fill=255)
        # la roche : eclairee a gauche par la lueur, dans l'ombre a droite, en degrade (pas de pans)
        row = Image.new("RGBA", (bg.width, 1))
        rp = row.load()
        for X in range(bg.width):
            u = max(-1.0, min(1.0, (X / K - cx) / half))
            if u < 0:
                c = tuple(int(body[i] + (light[i] - body[i]) * (-u) ** 0.7) for i in range(3))
            else:
                c = tuple(int(body[i] + (dark[i] - body[i]) * u ** 0.7) for i in range(3))
            rp[X, 0] = c + (255,)
        lay = row.resize(bg.size)
        d = ImageDraw.Draw(lay)
        r = random.Random(seed + 1)
        for k in range(14):			# les ravines : des traits courbes qui descendent du sommet
            u0 = r.uniform(-1, 1)
            x0 = cx + u0 * half * crater * 1.6
            y0 = top + 18 + abs(u0) * 10
            spread = u0 * r.uniform(0.5, 0.95)
            path = []
            for j in range(13):
                t = j / 12.0
                x = cx + (u0 * crater * 1.6 + spread * t) * half + 6 * math.sin(t * 6 + k)
                path.append((x * K, (y0 + (base - y0) * t) * K))
            col = tuple(int(v * (0.72 if u0 > 0 else 0.82)) for v in (body if abs(u0) < 0.3 else (light if u0 < 0 else dark))[:3]) + (255,)
            d.line(path, fill=col, width=int(r.uniform(2, 4) * K), joint="curve")
            d.line([(x + 2 * K, y) for x, y in path], fill=tuple(min(255, int(v * 1.25)) for v in col[:3]) + (120,), width=int(1 * K))
        for _ in range(34):			# des ressauts de roche : une arete claire sur une ombre
            x, y = r.uniform(cx - half * 0.75, cx + half * 0.75), r.uniform(top + 40, base - 10)
            w = r.uniform(4, 10)
            u = (x - cx) / half
            d.line([((x - w) * K, (y + 2) * K), ((x + w) * K, (y + 1) * K)], fill=tuple(int(v * 0.78) for v in body[:3]) + (255,), width=int(3 * K))
            d.line([((x - w) * K, y * K), ((x + w * 0.4) * K, (y - 1) * K)], fill=tuple(min(255, int(v * (1.3 if u < 0 else 1.1))) for v in body[:3]) + (200,), width=int(1.2 * K))
        cut = Image.new("RGBA", bg.size)
        cut.paste(lay, (0, 0), mask)
        bg.alpha_composite(cut)
        # la lueur du cratere, puis les coulees (un coeur jaune, un bord orange, une lueur floue)
        gl = Image.new("RGBA", bg.size)
        dg = ImageDraw.Draw(gl)
        for rr, al in ((70, 60), (44, 110), (24, 170)):
            dg.ellipse(((cx - rr * 1.4) * K, (top + 8 - rr * 0.6) * K, (cx + rr * 1.4) * K, (top + 8 + rr * 0.6) * K), fill=glow + (al,))
        lava = Image.new("RGBA", bg.size)		# la bouche du cratere, vue un peu d'en haut : un ovale de lave
        cw = half * crater * 0.95
        dc = ImageDraw.Draw(lava)
        dc.ellipse(((cx - cw) * K, (top + 3) * K, (cx + cw) * K, (top + 13) * K), fill=(150, 40, 20, 255))
        dc.ellipse(((cx - cw * 0.85) * K, (top + 5) * K, (cx + cw * 0.85) * K, (top + 12) * K), fill=(255, 120, 30, 255))
        dc.ellipse(((cx - cw * 0.5) * K, (top + 7) * K, (cx + cw * 0.4) * K, (top + 11) * K), fill=(255, 220, 110, 255))
        dl = ImageDraw.Draw(lava)
        for f, (u0, drift, length) in enumerate(flows):
            x, y = cx + u0 * half * crater, top + 14
            path = [(x, y)]
            for j in range(1, 26):
                t = j / 25.0
                x = cx + (u0 * crater + drift * t ** 0.8) * half + 10 * math.sin(t * 8 + f * 2) * min(1.0, t * 4)
                yy = top + (base - top) * (1 - (1 - min(1.0, abs(x - cx) / half)) ** 1.8)
                y = max(y + 3, yy + 4, top + 14 + t * (base - top) * length)
                path.append((x, y))
                if t > length:
                    break
            for w, col in ((9, (255, 90, 20, 255)), (5, (255, 150, 40, 255)), (2, (255, 230, 120, 255))):
                ww = w
                for j in range(len(path) - 1):
                    ww = max(1.0, w * (1 - j / len(path) * 0.6))
                    dl.line([(path[j][0] * K, path[j][1] * K), (path[j + 1][0] * K, path[j + 1][1] * K)], fill=col, width=int(ww * K))
            dg.line([(px * K, py * K) for px, py in path], fill=glow + (120,), width=int(22 * K), joint="curve")
        bg.alpha_composite(soft(gl, 10))
        bg.alpha_composite(lava)
        return pts

    # le volcan du fond, bleute et lointain
    volcano(820, 230, 225, 430, 0.10, 3, (52, 26, 40, 255), (66, 32, 46, 255), (40, 20, 32, 255),
            [(0.3, 0.25, 0.55)], (255, 110, 40))
    # le grand volcan
    volcano(330, 340, 168, 440, 0.12, 7, (46, 24, 30, 255), (70, 34, 38, 255), (32, 16, 22, 255),
            [(-0.6, -0.35, 0.95), (0.4, 0.2, 0.7), (0.9, 0.55, 1.0)], (255, 120, 40))
    # le panache : des boules de fumee qui montent et derivent, eclairees par-dessous
    sm = Image.new("RGBA", bg.size)
    ds = ImageDraw.Draw(sm)
    for i in range(22):
        t = i / 21.0
        x, y, r = 330 + 110 * t * t + 16 * math.sin(i * 1.7), 160 - 130 * t, 22 + 46 * t
        lit = (int(220 - 90 * t), int(100 - 10 * t), int(70 + 10 * t), int(250 - 60 * t))
        ds.ellipse(((x - r) * K, (y - r * 0.8) * K, (x + r) * K, (y + r * 0.8) * K), fill=lit)
        ds.ellipse(((x - r * 0.8) * K, (y - r * 0.95) * K, (x + r * 0.6) * K, (y + r * 0.15) * K), fill=(104, 78, 90, int(240 - 60 * t)))
        ds.ellipse(((x - r * 0.5) * K, (y - r * 0.85) * K, (x + r * 0.1) * K, (y - r * 0.35) * K), fill=(140, 112, 122, int(200 - 60 * t)))
    bg.alpha_composite(soft(sm, 4))
    # des escarbilles
    de = ImageDraw.Draw(bg)
    for _ in range(70):
        x, y = rnd.uniform(230, 450), rnd.uniform(60, 200)
        de.ellipse(((x - 1.2) * K, (y - 1.2) * K, (x + 1.2) * K, (y + 1.2) * K), fill=(255, rnd.randint(150, 230), 80, 255))
    bg = bg.resize((W, H), Image.LANCZOS)
    # le premier plan : des crêtes noires dentelees, puis le lac de lave et sa lueur
    bg.alpha_composite(hills(W, 410, 26, 4, 0x24141A, 7, jag=0.6))
    glow = Image.new("RGBA", (W, H))
    ImageDraw.Draw(glow).rectangle((0, 430, W, H), fill=(255, 110, 30, 120))
    bg.alpha_composite(glow.filter(ImageFilter.GaussianBlur(10)))
    d = ImageDraw.Draw(bg)
    d.rectangle((0, 446, W, H), fill=(255, 112, 30, 255))
    for x in range(0, W, 64):
        d.line([(x + 6, 452), (x + 30, 450), (x + 52, 454)], fill=(255, 200, 80, 255), width=3)
    return bg


# ===== l'assemblage ===============================================================================================
def tile(im):
    return im.resize((TP, TP), Image.LANCZOS)


def build(src, world):
    common = {
        "plank": tile(load(src, "tile/platform-one-way.png")),
        "logs": tile(load(src, "tile/bridge.png")),
        "ladder": tile(load(src, "tile/ladder.png")),
        "spikes": tile(load(src, "tile/spikes.png")),
        "door": fit(load(src, "tile/cave-door-2.png" if world in (1, 2) else "tile/cave-door.png"), 44, 78),
        "coin": fit(load(src, "item/coin.png"), 22, 22),
        "pot": fit(load(src, "item/pot-1.png"), 22, 26),
        "key": fit(load(src, "item/key-gold.png"), 18, 26),
        "bg": background(src, world),
    }
    if world == 0:
        ts = Image.open(os.path.join(src, "tileset.png")).convert("RGBA")

        def cell(r, c):
            return ts.crop((c * 128, r * 128, c * 128 + 128, r * 128 + 128))
        w = {"grass": tile(load(src, "tile/01.png")), "cobble": tile(load(src, "tile/02.png")),
             "back_a": tile(load(src, "tile/03a.png")), "back_b": tile(load(src, "tile/03b.png")),
             "wsurf": tile(load(src, "tile/water-surface.png")), "wmid": tile(load(src, "tile/water-middle.png")),
             "deco1": fit(load(src, "tile/fern.png"), 30, 26), "deco2": fit(cell(1, 3), 28, 26), "deco3": fit(cell(1, 4), 22, 30),
             "deco4": fit(cell(1, 5), 22, 22), "deco5": fit(load(src, "tile/rock.png"), 28, 24), "deco6": fit(load(src, "item/crate.png"), 30, 30),
             "deco7": fit(load(src, "tile/signboard.png"), 30, 30), "deco8": fit(load(src, "item/barrel.png"), 26, 30),
             "big": fit(load(src, "tile/big-fern.png"), 112, 72)}
    else:
        w = {1: desert, 2: temple, 3: ice, 4: volcano, 5: spooky, 6: valley}[world](src)
    common.update(w)
    out = []
    for n in NAMES:
        im = common[n]
        if n.startswith("deco"):			# un decor : au plus 1,2 case de haut (32 x 40 points)
            b = im.getbbox()
            im = fit(im, min(40, max(8, b[2] - b[0])), min(40, max(8, b[3] - b[1])))
        if n == "big":
            im = fit(im, 112, 88)
        out.append(im)
    return out


def runs(im):
    px = im.tobytes()
    vals = []
    for i in range(0, len(px), 4):
        r, g, b, a = px[i], px[i + 1], px[i + 2], px[i + 3]
        vals.append((a << 24) | ((r * a // 255) << 16) | ((g * a // 255) << 8) | (b * a // 255) if a else 0)
    out = bytearray()
    n, i = 0, 0
    while i < len(vals):
        j = i
        while j < len(vals) and vals[j] == vals[i] and j - i < 65535:
            j += 1
        out += struct.pack("<HI", j - i, vals[i])
        n += 1
        i = j
    return struct.pack("<HHI", im.width, im.height, n) + out


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    sheets = []
    worlds = [int(a) for a in sys.argv[3].split(",")] if len(sys.argv) > 3 and not sys.argv[3].startswith("-") else range(7)
    for w in worlds:
        ims = build(src, w)
        data = b"RTIL" + struct.pack("<HH", 2, len(ims)) + b"".join(runs(i) for i in ims)
        p = os.path.join(out, "world%d.til" % w)
        with open(p, "wb") as f:
            f.write(data)
        print("%s : %d images, %d octets" % (p, len(ims), len(data)))
        sheets.append(ims)
    if "--sheet" in sys.argv:
        sh = Image.new("RGBA", (1024 + 560, len(sheets) * 480), (128, 128, 136, 255))
        for w, ims in enumerate(sheets):
            sh.alpha_composite(ims[-1], (0, w * 480))
            x, y = 1040, w * 480 + 8
            for i in ims[:-1]:
                big = i.resize((i.width * 2, i.height * 2), Image.NEAREST) if i.width <= 48 else i
                if x + big.width > sh.width:
                    x, y = 1040, y + 100
                sh.alpha_composite(big, (x, y))
                x += big.width + 6
        sh.save(sys.argv[sys.argv.index("--sheet") + 1])


if __name__ == "__main__":
    main()
