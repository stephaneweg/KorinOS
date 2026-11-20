#!/usr/bin/env python3
"""explorer.py -- l'explorateur de Relics, fait dans le style des personnages achetes par l'utilisateur (Chibi
Character Bundle) : des morceaux de ces personnages, recolores, un chapeau et un pistolet dessines ici, animes par le
fichier Spriter du paquet (tools/relics/spriter.py), puis ecrits en .spr (mksprites.write_spr). Les paquets restent
hors du depot :

    python3 tools/relics/explorer.py /chemin/Chibi\\ Character\\ Bundle/extrait  src/apps/relics/data/sprites/hero.spr
            [--sheet planche.png]

Le dossier : un sous-dossier par personnage (« Chibi Prisoner Guy », « Medieval Sergeant »...), chacun avec ses
« Vector Parts » (morceaux et Animations.scml).
"""
import colorsys
import glob
import math
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from spriter import Scml          # noqa: E402
import mksprites                  # noqa: E402

INK = (30, 26, 24)


def parts(root, who):
    vp = glob.glob(os.path.join(root, who, "**", "Vector Parts"), recursive=True)
    if not vp:
        sys.exit("pas de « Vector Parts » pour " + who)
    vp = vp[0]
    return vp, {os.path.basename(f): Image.open(f).convert("RGBA") for f in glob.glob(os.path.join(vp, "*.png"))}


def recolor(im, fn):
    """chaque point (r, g, b) -> fn (h, s, v) : (h, s, v) ou None (inchange) ; l'alpha garde"""
    px = im.load()
    out = im.copy()
    po = out.load()
    for y in range(im.height):
        for x in range(im.width):
            r, g, b, a = px[x, y]
            if not a:
                continue
            h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
            n = fn(h, s, v)
            if n is None:
                continue
            rr, gg, bb = colorsys.hsv_to_rgb(*n)
            po[x, y] = (int(rr * 255 + .5), int(gg * 255 + .5), int(bb * 255 + .5), a)
    return out


def khaki(h, s, v):
    """le gris et le noir des habits -> kaki ; le trait (tres sombre) et le cuir (brun sature) restent"""
    if v < 0.16:
        return None
    if s < 0.22:
        return (0.11, 0.38, min(1.0, 0.42 + v * 1.05))
    return None


def trousers(h, s, v):
    if v < 0.13:
        return None
    return (0.07, min(0.55, s + 0.2), min(1.0, 0.18 + v * 1.25))


def body_colors(h, s, v):
    if v < 0.16:
        return None
    if s < 0.25 and v < 0.5:                    # la tunique -> chemise kaki
        return (0.11, 0.38, min(1.0, 0.42 + v * 1.05))
    if s < 0.45 and v >= 0.5 and 0.03 < h < 0.15:   # le col clair -> le foulard rouge
        return (0.0, 0.72, min(1.0, v * 0.95))
    return None


def supersample(draw_fn, size, k=4):
    big = Image.new("RGBA", (size[0] * k, size[1] * k))
    draw_fn(ImageDraw.Draw(big), k)
    return big.resize(size, Image.LANCZOS)


def add_hat(head):
    """un chapeau d'aventurier sur la tete (480 x 480), au trait et en aplats comme les morceaux du paquet"""
    x0, y0, x1, y1 = head.getbbox()
    w = x1 - x0
    top = y0
    brown, dark, light, band = (139, 90, 43), (104, 64, 28), (178, 124, 66), (52, 32, 20)
    T = 11

    def draw(d, k):
        def P(pts):
            return [(px * k, py * k) for px, py in pts]
        cx = x0 + w * 0.47
        # la calotte : un trapeze arrondi, creusee au milieu
        crown = [(cx - w * 0.36, top + w * 0.30), (cx - w * 0.30, top - w * 0.06), (cx - w * 0.12, top - w * 0.13),
                 (cx + w * 0.02, top - w * 0.07), (cx + w * 0.16, top - w * 0.14), (cx + w * 0.33, top - w * 0.05),
                 (cx + w * 0.38, top + w * 0.30)]
        brim = (cx - w * 0.62, top + w * 0.20, cx + w * 0.64, top + w * 0.42)
        # le trait d'abord (plus gros), puis les aplats
        d.polygon(P(crown), fill=INK)
        for i in range(len(crown)):
            a, b = crown[i], crown[(i + 1) % len(crown)]
            d.line(P([a, b]), fill=INK, width=int(T * 2 * k))
        d.ellipse([v * k for v in (brim[0] - T, brim[1] - T, brim[2] + T, brim[3] + T)], fill=INK)
        d.ellipse([v * k for v in brim], fill=dark)
        d.ellipse([v * k for v in (brim[0] + w * 0.06, brim[1], brim[2] - w * 0.04, brim[3] - w * 0.07)], fill=brown)
        d.polygon(P(crown), fill=brown)
        d.polygon(P([(cx + w * 0.10, top + w * 0.30), (cx + w * 0.16, top - w * 0.10), (cx + w * 0.33, top - w * 0.04),
                     (cx + w * 0.38, top + w * 0.30)]), fill=dark)
        d.polygon(P([(cx - w * 0.27, top + w * 0.10), (cx - w * 0.26, top - w * 0.03), (cx - w * 0.12, top - w * 0.09),
                     (cx - w * 0.14, top + w * 0.06)]), fill=light)
        # le ruban
        band_y0, band_y1 = top + w * 0.17, top + w * 0.29
        d.polygon(P([(cx - w * 0.37, band_y0), (cx + w * 0.37, band_y0), (cx + w * 0.38, band_y1), (cx - w * 0.38, band_y1)]), fill=band)
        d.line(P([(cx - w * 0.37, band_y0), (cx + w * 0.37, band_y0)]), fill=INK, width=int(4 * k))
        d.line(P([(cx - w * 0.38, band_y1), (cx + w * 0.38, band_y1)]), fill=INK, width=int(4 * k))
        # le pli de la calotte
        d.line(P([(cx - w * 0.13, top - w * 0.08), (cx - w * 0.04, top + w * 0.10)]), fill=INK, width=int(5 * k))
    hat = supersample(draw, head.size)
    out = head.copy()
    out.alpha_composite(hat)
    return out


def add_pistol(hand):
    """la main droite (128 x 128) tient un revolver, le canon vers l'avant (vers la droite de la main)"""
    big = Image.new("RGBA", (256, 128))

    def draw(d, k):
        def P(pts):
            return [(px * k, py * k) for px, py in pts]
        metal, metal2, wood = (96, 100, 108), (150, 156, 166), (120, 72, 36)
        barrel = [(92, 44), (200, 44), (200, 62), (92, 62)]
        body = [(72, 38), (118, 38), (118, 74), (72, 74)]
        grip = [(74, 66), (98, 66), (92, 104), (66, 104)]
        for poly, col in ((grip, wood), (barrel, metal), (body, metal)):
            d.polygon(P(poly), fill=INK)
            for i in range(len(poly)):
                d.line(P([poly[i], poly[(i + 1) % len(poly)]]), fill=INK, width=int(18 * k))
        for poly, col in ((grip, wood), (barrel, metal), (body, metal)):
            d.polygon(P(poly), fill=col)
        d.polygon(P([(94, 46), (198, 46), (198, 51), (94, 51)]), fill=metal2)
        d.polygon(P([(190, 36), (198, 36), (198, 44), (190, 44)]), fill=INK)
    gun = supersample(draw, big.size, 3)
    out = Image.new("RGBA", (256, 128))
    out.alpha_composite(gun, (0, 0))
    out.alpha_composite(hand, (0, 0))
    return out


def back_climb(n=4):
    """l'explorateur de dos sur l'echelle (le paquet n'a que des profils) : dessine ici dans le style des morceaux
    (trait noir epais, aplats, une ombre), sur la toile de Scml.render (1100 x 1100, les pieds en 530, 822). n images :
    la main gauche monte avec le pied droit, puis l'inverse."""
    khaki, khaki_d, khaki_l = (169, 147, 105), (128, 110, 78), (196, 176, 134)
    trou, trou_d, boot, boot_d = (141, 108, 85), (109, 87, 74), (84, 58, 40), (60, 42, 30)
    belt, strap = (97, 79, 66), (110, 70, 38)
    skin, skin_d = (239, 208, 172), (206, 170, 136)
    buzz, hair, hair_l = (117, 105, 95), (70, 66, 62), (96, 91, 86)
    brown, dark, light, band = (139, 90, 43), (104, 64, 28), (178, 124, 66), (52, 32, 20)
    T = 12
    out = []
    for f in range(n):
        ph = 2 * math.pi * f / n
        up = math.cos(ph)              # 1 : la main gauche en haut, -1 : la droite
        bob = -26 - 6 * abs(math.sin(ph))   # (sur l'echelle, le corps plus haut que debout : les jambes se voient ; il monte entre deux prises)

        def draw(d, k, up=up, bob=bob):
            def P(pts):
                return [(x * k, (y + bob) * k) for x, y in pts]

            def shape(pts, fill, w=T):
                d.polygon(P(pts), fill=INK)
                for i in range(len(pts)):
                    d.line(P([pts[i], pts[(i + 1) % len(pts)]]), fill=INK, width=int(2 * w * k))
                    a = pts[i]
                    d.ellipse([(a[0] - w) * k, (a[1] + bob - w) * k, (a[0] + w) * k, (a[1] + bob + w) * k], fill=INK)
                d.polygon(P(pts), fill=fill)

            def ell(b, fill, w=T):
                x0, y0, x1, y1 = b
                d.ellipse([(x0 - w) * k, (y0 + bob - w) * k, (x1 + w) * k, (y1 + bob + w) * k], fill=INK)
                d.ellipse([x0 * k, (y0 + bob) * k, x1 * k, (y1 + bob) * k], fill=fill)

            def limb(a, b, wid, fill):
                d.line(P([a, b]), fill=INK, width=int((wid + 2 * T) * k))
                for q in (a, b):
                    r = wid / 2 + T
                    d.ellipse([(q[0] - r) * k, (q[1] + bob - r) * k, (q[0] + r) * k, (q[1] + bob + r) * k], fill=INK)
                d.line(P([a, b]), fill=fill, width=int(wid * k))
                for q in (a, b):
                    r = wid / 2
                    d.ellipse([(q[0] - r) * k, (q[1] + bob - r) * k, (q[0] + r) * k, (q[1] + bob + r) * k], fill=fill)

            # les jambes : la droite plie quand la main gauche monte (le pied monte d'un barreau)
            for side, x in ((-1, 496), (1, 564)):
                lift = (max(0.0, up) if side > 0 else max(0.0, -up)) * 30
                top, foot = 770, 822 - lift - bob
                shape([(x - 30, top), (x + 30, top), (x + 27, foot - 30), (x - 27, foot - 30)], trou_d if side > 0 else trou)
                shape([(x - 33, foot - 40), (x + 33, foot - 40), (x + 35, foot - 6), (x + 27, foot), (x - 27, foot), (x - 35, foot - 6)], boot)
                d.polygon(P([(x - 29, foot - 18), (x + 29, foot - 18), (x + 29, foot - 8), (x - 29, foot - 8)]), fill=boot_d)
            # les bras, derriere la tete (l'echelle est devant lui) : de l'epaule a la main
            hands = []
            for side, sx in ((-1, 452), (1, 608)):
                hy = 536 - 62 * (up if side < 0 else -up)
                hx = 530 + side * 150
                limb((sx, 676), (hx, hy + 40), 58, khaki if side < 0 else khaki_d)
                hands.append((hx, hy))
            # le buste, de dos : la chemise, la sangle de la sacoche, la ceinture
            body = [(438, 656), (622, 656), (632, 700), (622, 790), (438, 790), (428, 700)]
            shape(body, khaki)
            d.polygon(P([(560, 660), (622, 660), (630, 700), (620, 786), (580, 786)]), fill=khaki_d)
            d.polygon(P([(446, 664), (470, 664), (462, 740), (440, 740)]), fill=khaki_l)
            d.line(P([(530, 680), (530, 750)]), fill=INK, width=int(5 * k))
            shape([(452, 660), (484, 660), (612, 760), (590, 776)], strap, 7)
            shape([(432, 752), (628, 752), (630, 786), (430, 786)], belt, 8)
            shape([(574, 744), (618, 744), (614, 800), (578, 800)], strap, 8)
            # les mains, sur les montants : derriere la tete (l'echelle est devant lui)
            for hx, hy in hands:
                ell((hx - 34, hy - 30, hx + 34, hy + 34), skin)
                for j in range(3):
                    d.line(P([(hx - 22, hy - 12 + j * 14), (hx + 22, hy - 12 + j * 14)]), fill=skin_d, width=int(5 * k))
            # la tete, de dos : les oreilles, la nuque rasee, les cheveux courts au-dessus
            for x0 in (330, 690):
                ell((x0, 500, x0 + 40, 586), skin)
                d.ellipse([(x0 + 12) * k, (516 + bob) * k, (x0 + 28) * k, (570 + bob) * k], fill=skin_d)
            ell((352, 384, 708, 650), skin_d)
            # (les cheveux : tout le crane, la ligne de la nuque en U ; la nuque rasee entre les deux)
            d.ellipse([360 * k, (392 + bob) * k, 700 * k, (636 + bob) * k], fill=buzz)
            d.ellipse([356 * k, (388 + bob) * k, 704 * k, (612 + bob) * k], fill=hair)
            d.arc([392 * k, (420 + bob) * k, 560 * k, (560 + bob) * k], 190, 250, fill=hair_l, width=int(14 * k))
            for x, y in ((450, 470), (520, 450), (600, 470), (480, 540), (580, 545)):
                d.line(P([(x, y), (x + 10, y + 34)]), fill=hair_l, width=int(6 * k))
            # le chapeau, de dos : le bord, la calotte creusee au milieu, le ruban
            cx, w = 530, 300
            top = 262
            crown = [(cx - w * 0.42, top + w * 0.42), (cx - w * 0.36, top + w * 0.02), (cx - w * 0.16, top - w * 0.08),
                     (cx, top + w * 0.02), (cx + w * 0.16, top - w * 0.08), (cx + w * 0.36, top + w * 0.02),
                     (cx + w * 0.42, top + w * 0.42)]
            ell((290, 360, 770, 432), dark)
            d.ellipse([306 * k, (360 + bob) * k, 754 * k, (418 + bob) * k], fill=brown)
            shape(crown, brown)
            d.polygon(P([(cx + w * 0.06, top + w * 0.42), (cx + w * 0.16, top - w * 0.04), (cx + w * 0.34, top + w * 0.04),
                         (cx + w * 0.40, top + w * 0.42)]), fill=dark)
            d.polygon(P([(cx - w * 0.33, top + w * 0.20), (cx - w * 0.32, top + w * 0.05), (cx - w * 0.16, top - w * 0.03),
                         (cx - w * 0.17, top + w * 0.14)]), fill=light)
            by0, by1 = top + w * 0.27, top + w * 0.40
            d.polygon(P([(cx - w * 0.41, by0), (cx + w * 0.41, by0), (cx + w * 0.42, by1), (cx - w * 0.42, by1)]), fill=band)
            d.line(P([(cx - w * 0.41, by0), (cx + w * 0.41, by0)]), fill=INK, width=int(4 * k))
            d.line(P([(cx, top + w * 0.02), (cx, top + w * 0.22)]), fill=INK, width=int(5 * k))
            d.arc([290 * k, (360 + bob) * k, 770 * k, (432 + bob) * k], 20, 160, fill=INK, width=int(6 * k))
        out.append(supersample(draw, (1100, 1100), 2))
    return out


def main():
    root, out = sys.argv[1], sys.argv[2]
    rig_dir, rig = parts(root, "Medieval Sergeant")
    _, pris = parts(root, "Chibi Prisoner Guy")
    imgs = dict(rig)
    imgs["Head.png"] = add_hat(pris["Head.png"])
    for n in ("Face 01.png", "Face 02.png", "Face 03.png"):
        imgs[n] = pris[n]
    imgs["Body.png"] = recolor(rig["Body.png"], body_colors)
    for n in ("Left Arm.png", "Right Arm.png"):
        imgs[n] = recolor(rig[n], khaki)
    for n in ("Left Leg.png", "Right Leg.png"):
        imgs[n] = recolor(rig[n], trousers)
    imgs["Sword.png"] = Image.new("RGBA", (4, 4))
    imgs["SlashFX.png"] = Image.new("RGBA", (4, 4))
    s = Scml(os.path.join(rig_dir, "Animations.scml"), imgs)
    gun = {"Right Hand.png": add_pistol(imgs["Right Hand.png"])}

    def frames(anim, n, **kw):
        L = s.length(anim)
        return [s.render(anim, L * i // n, **kw) for i in range(n)]

    groups = {
        "walk": frames("Walking", 8),
        # le tir : le bras de devant tendu en travers du buste, le revolver vers l'avant ; un petit recul
        "shoot": [s.render("Idle", 0, swap=gun, poses={"bone_000": a, "bone_001": -12}) for a in (40, 47, 42)],
        "die": frames("Dying", 5, looping=False),
        "stand": frames("Idle", 4),
        "jump": [s.render("Jump Loop", 0)],
        "fall": [s.render("Falling Down", 0)],
        # l'echelle : de dos, dessine ici (back_climb)
        "climb": back_climb(4),
        # accroupi (pour poser la dynamite) : le buste descend, les jambes plient
        "crouch": [s.render("Idle", 0, poses={"bone_006": (0, 0, -40), "bone_004": (35, 0, 0), "bone_005": (-35, 0, 0)})],
    }
    mksprites.write_spr(out, groups, height=58)
    if "--sheet" in sys.argv:
        sheet = sys.argv[sys.argv.index("--sheet") + 1]
        allf = [(k, im) for k, v in groups.items() for im in v]
        cell = 200
        o = Image.new("RGBA", (cell * 8, cell * ((len(allf) + 7) // 8)), (88, 92, 98, 255))
        d = ImageDraw.Draw(o)
        for i, (k, im) in enumerate(allf):
            b = im.getbbox()
            c = im.crop(b)
            c.thumbnail((cell - 10, cell - 24))
            x, y = (i % 8) * cell, (i // 8) * cell
            o.alpha_composite(c, (x + (cell - c.width) // 2, y + cell - 4 - c.height))
            d.text((x + 4, y + 2), k, fill="white")
        o.save(sheet)


if __name__ == "__main__":
    main()
