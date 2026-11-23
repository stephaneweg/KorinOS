# art.py -- les dessins des icones d'ARMPI. Repere 0..100 sur la tuile ; le symbole tient dans 14..86.
#
# @icon(nom, haut, bas) : couleurs de la tuile. Palette par famille, voisines differentes dans la liste des
# applications triee : systeme en ardoises, internet en bleus et sarcelles, bureautique franche, creation en
# magentas et violets, jeux vifs chacun le sien. Aucun logo de marque, aucun texte hors du « 2 » de 2048 et du « # »
# d'IRC, qui sont le sens meme de ces programmes.
import math
import numpy as np
from PIL import Image, ImageChops
from iconlib import Ic, Grad, Rad, tone, mix, light, dark, sub, add, inter, rgb, M

ICONS = {}
GLYPHS = {}
FREE = set()    # les icônes dessinées en objet libre, à la manière d'Adwaita (sans plaque)

WHITE = Grad(0xFFFFFF, 0xDDE4EE, 100)
PAPER = Grad(0xFFFFFF, 0xE9EDF3, 100)
INK = 0x2A3342


def icon(name, top, bottom, free=False):
    def deco(f):
        ICONS[name] = (top, bottom, f)
        if free:
            FREE.add(name)
        return f
    return deco


def obj(ic, m, face, side, depth=5.0, sh=.22):
    """Un objet à la manière d'Adwaita : sa face, et dessous son épaisseur (la même forme, plus sombre, descendue de
    depth), posé sur une ombre douce."""
    if ic.small:
        depth *= .7
    n = max(1, int(ic.u(depth)))
    step = max(1, n // 8)
    body = add(*[ImageChops.offset(m, 0, k) for k in range(0, n + 1, step)] + [ImageChops.offset(m, 0, n)])
    if not ic.small and sh:
        ic.shadow(body, dy=1.2, blur=1.6, op=sh, dx=0)
    ic.fill(body, side)
    ic.fill(m, face)
    return body


# --- objets à la manière d'Adwaita (palette de GNOME) -------------------------------------------------------------
def adw_doc(ic, x, y, w, h, fold=14, r=4, face=None, side=0xC0BFBC, depth=4):
    """Une feuille en objet : son coin replié, son épaisseur."""
    m = sub(ic.rr(x, y, w, h, r), ic.poly([(x + w - fold, y - 3), (x + w + 3, y - 3), (x + w + 3, y + fold)]))
    obj(ic, m, face if face else Grad(0xFFFFFF, 0xF6F5F4, 90), side, depth)
    ic.fill(ic.poly([(x + w - fold, y), (x + w, y + fold), (x + w - fold + 2, y + fold - 1)]), 0xDEDDDA)
    return m


def adw_screen(ic, x, y, w, h, face, body=0x3D3846, side=0x241F31, r=6, b=3.4):
    """Un écran en objet : le cadre sombre et son épaisseur ; rend le masque de la surface."""
    obj(ic, ic.rr(x, y, w, h, r), body, side, 4)
    f = ic.rr(x + b, y + b, w - 2 * b, h - 2 * b, max(1, r - b * .7))
    ic.fill(f, face)
    return f


def adw_stand(ic, y, w=30):
    """Le pied d'un écran : le col, le socle et son épaisseur."""
    ic.fill(ic.rr(44, y - 1, 12, 9, 0), Grad(0x9A9996, 0x77767B, 90))
    obj(ic, ic.rr(50 - w / 2, y + 7, w, 5, 2.5), 0xDEDDDA, 0x9A9996, 2.5, sh=.15)


# --- morceaux communs ---------------------------------------------------------------------------------------------
def doc(ic, x, y, w, h, fold=13, r=2.6, paint=PAPER):
    """Une feuille au coin replie."""
    p = ic.rr_pts(x, y, w, h, r)
    m = ic.rr(x, y, w, h, r)
    cut = ic.poly([(x + w - fold, y - 2), (x + w + 2, y - 2), (x + w + 2, y + fold)])
    m = sub(m, cut)
    ic.raised(m, paint)
    tri = ic.poly([(x + w - fold, y), (x + w, y + fold), (x + w - fold, y + fold)])
    ic.fill(tri, Grad(0xD3DBE6, 0xB5C0CF, 45))
    return m


def lines(ic, x, y, w, n, step, h=3.2, c=0xB9C4D2, last=.6):
    for i in range(n):
        ic.fill(ic.rr(x, y + i * step, w * (last if i == n - 1 else 1), h, h / 2), c)


def screen(ic, x, y, w, h, body=None, face=None, r=4, b=3):
    """Un cadre et sa surface ; rend le masque de la surface."""
    m = ic.rr(x, y, w, h, r)
    ic.raised(m, body if body else tone(0xE6EBF2, .3, .1))
    f = ic.rr(x + b, y + b, w - 2 * b, h - 2 * b, max(1, r - b * .6))
    ic.fill(f, face if face else 0x1E2632)
    return f


def stand(ic, y, h=6, w=22):
    ic.fill(ic.rr(50 - 4.5, y, 9, h + 1, 0), Grad(0xC7D0DC, 0x95A2B3, 90))
    ic.raised(ic.rr(50 - w / 2, y + h, w, 4, 2), WHITE)


def bolt(ic, cx, cy, s=1.0):
    p = [(4, -22), (-12, 4), (-1, 4), (-5, 22), (13, -6), (2, -6), (8, -22)]
    return ic.poly([(cx + a * s, cy + b * s) for a, b in p])


def play_tri(ic, cx, cy, r):
    p = [(cx - r * .62, cy - r), (cx + r * 1.02, cy), (cx - r * .62, cy + r)]
    return ic.soften(ic.poly(p), r * .05 + .4)


def tilted(ic, x, y, rx, ry, rot):
    """Une ellipse penchee."""
    ic.push(0, 0, 1, rot, (x, y))
    m = ic.ell(x, y, rx, ry)
    ic.pop()
    return m


def block(ic, x, y, s, c, r=None):
    """Un bloc biseaute (Tetris, briques)."""
    r = s * .14 if r is None else r
    ic.raised(ic.rr(x, y, s, s, r), Grad(light(c, .22), dark(c, .1), 110), sh=False, rim=.5, dk=.3, e=1.0)
    if not ic.small:
        ic.fill(ic.rr(x + s * .24, y + s * .24, s * .52, s * .52, r * .6), light(c, .18), .55)


def gamepad_body(ic, paint):
    """Manette arrondie generique : deux poignees."""
    m = add(ic.rr(17, 33, 66, 30, 15),
            ic.poly([(19, 50), (14, 68), (17, 75), (24, 76), (32, 66), (36, 58)]),
            ic.poly([(81, 50), (86, 68), (83, 75), (76, 76), (68, 66), (64, 58)]))
    m = ic.soften(m, 1.6)
    ic.raised(m, paint)
    return m


def cross_pad(ic, cx, cy, s, c):
    w = s * .36
    ic.fill(add(ic.rr(cx - s / 2, cy - w / 2, s, w, w * .25), ic.rr(cx - w / 2, cy - s / 2, w, s, w * .25)), c)


def card(ic, x, y, w, h, rot, suit, col, pipc=None):
    ic.push(0, 0, 1, rot, (x + w / 2, y + h / 2))
    m = ic.rr(x, y, w, h, 4)
    ic.raised(m, Grad(0xFFFFFF, 0xEEF1F5, 100))
    suit(ic, x + w / 2, y + h / 2, w * .3, col)
    ic.pop()


def heart(ic, cx, cy, r, c):
    p = []
    for i in range(121):
        t = i / 120 * 2 * math.pi
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        p.append((cx + x / 16 * r, cy - y / 16 * r - r * .02))
    ic.fill(ic.poly(p), Grad(light(c, .1), dark(c, .1), 100))


def spade(ic, cx, cy, r, c):
    p = []
    for i in range(121):
        t = i / 120 * 2 * math.pi
        x = 16 * math.sin(t) ** 3
        y = 13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t)
        p.append((cx + x / 16 * r, cy + y / 16 * r - r * .12))
    m = add(ic.poly(p), ic.poly([(cx, cy + r * .1), (cx - r * .36, cy + r * 1.02), (cx + r * .36, cy + r * 1.02)]))
    ic.fill(m, Grad(light(c, .15), c, 100))


def diamond(ic, cx, cy, r, c):
    ic.fill(ic.poly([(cx, cy - r * 1.15), (cx + r * .82, cy), (cx, cy + r * 1.15), (cx - r * .82, cy)]), Grad(light(c, .1), dark(c, .1), 100))


# --- systeme ------------------------------------------------------------------------------------------------------
@icon('control', 0x7E8A9C, 0x434C5C, free=True)
def control(ic):
    """Un engrenage en objet (Adwaita) : gris clair, son épaisseur, le moyeu creusé."""
    g = sub(ic.gear(50, 47, 38, 28, 9), ic.circ(50, 47, 12))
    obj(ic, g, Grad(0xF6F5F4, 0xD8D6D3, 90), 0x8D8A86, 6)
    ic.fill(ic.circ(50, 47, 12), 0x5E5C64)
    ic.fill(ic.circ(50, 49.5, 9.5), 0x77767B)


@icon('files', 0x3CC1F2, 0x0C79C8, free=True)
def files(ic):
    """Un classeur à tiroirs fermés, en objet (Adwaita) : le corps clair, les tiroirs ambre, poignées et étiquettes."""
    obj(ic, ic.rr(14, 10, 72, 74, 8), Grad(0xFAFBFC, 0xDEE2E8, 90), 0xA9B1BD, 6)
    drawer = Grad(0xFFC94A, 0xF6A623, 90)
    for y in (16, 39, 62):
        obj(ic, ic.rr(20, y, 60, 18.5, 4), drawer, 0xC77712, 2.4, sh=0)
        if not ic.small:
            ic.fill(ic.rr(40, y + 3.4, 20, 5.2, 1.2), 0xFFFFFF, .9)
        ic.fill(ic.rr(37, y + 10.8, 26, 4.6, 2.3), 0x7A4A0E)


@icon('terminal', 0x3A404B, 0x14171D)
def terminal(ic):
    c = Grad(0x9DF7B8, 0x4CD988, 100)
    ic.shadow(ic.line([(24, 34), (43, 50), (24, 66)], 8), op=.4)
    ic.fill(ic.line([(24, 34), (43, 50), (24, 66)], 8), c)
    ic.fill(ic.rr(50, 62, 27, 8, 2.4), c)


@icon('claude', 0x5B4E8C, 0x2A2246)
def claude(ic):
    # l'agent de code : une bulle de conversation, une invite de terminal dedans, une etincelle
    p = ic.rr(14, 20, 66, 48, 12)
    tail = ic.poly([(26, 64), (24, 82), (42, 66)])
    ic.raised(add(p, tail), WHITE)
    c = Grad(0x7A5CE0, 0x4B33A8, 100)
    ic.fill(ic.line([(28, 34), (40, 44), (28, 54)], 6.5), c)
    ic.fill(ic.rr(45, 50, 20, 6, 2), c)
    s = ic.star(76, 22, 12, 4.2, 4, -90)
    ic.shadow(s, op=.35)
    ic.fill(s, Grad(0xFFE9A8, 0xF6B73C, 100))


@icon('mai', 0x2E86D6, 0x103E78)
def mai(ic):
    # l'agent de code MAI : la bulle de conversation de Claude, des chevrons de code dedans, une etincelle cyan
    p = ic.rr(14, 20, 66, 48, 12)
    tail = ic.poly([(26, 64), (24, 82), (42, 66)])
    ic.raised(add(p, tail), WHITE)
    c = Grad(0x2F8FE8, 0x1655A8, 100)
    ic.fill(ic.line([(36, 32), (26, 44), (36, 56)], 6), c)
    ic.fill(ic.line([(56, 32), (66, 44), (56, 56)], 6), c)
    ic.fill(ic.line([(49, 30), (43, 58)], 5), c)
    s = ic.star(76, 22, 12, 4.2, 4, -90)
    ic.shadow(s, op=.35)
    ic.fill(s, Grad(0xC8F6FF, 0x3CC8F0, 100))


@icon('taskman', 0x25B089, 0x0B6657, free=True)
def taskman(ic):
    """Le gestionnaire de tâches : un écran en objet, une courbe d'activité."""
    f = adw_screen(ic, 8, 14, 84, 66, Grad(0x1F3B3A, 0x14292A, 90))
    if not ic.small:
        for i in range(1, 4):
            ic.fill(ic.rr(11, 17 + i * 15, 78, .9), 0x8FF0A4, .16)
        for i in range(1, 5):
            ic.fill(ic.rr(11 + i * 15.6, 17, .9, 60), 0x8FF0A4, .16)
    p = [(11, 50), (32, 50), (40, 30), (50, 68), (59, 40), (65, 50), (89, 50)]
    ic.fill(ic.line(p, 9), 0x57E389, .16)
    ic.fill(ic.line(p, 4.8), Grad(0xC8FFD8, 0x57E389, 90))


@icon('disks', 0x66748A, 0x323A49, free=True)
def disks(ic):
    """Un disque dur ouvert, en objet : le boîtier, le plateau, le bras."""
    obj(ic, ic.rr(14, 10, 72, 76, 8), Grad(0xDEDDDA, 0xC0BFBC, 90), 0x77767B, 6)
    d = ic.circ(47, 44, 27)
    ic.fill(d, Rad(0xFFFFFF, 0xB4B2AF, .38, .32, .95))
    if not ic.small:
        for rr in (11, 15.5, 20, 24):
            ic.fill(ic.ring(47, 44, rr, .5), 0x77767B, .3)
    ic.fill(ic.circ(47, 44, 6.5), 0x5E5C64)
    ic.fill(ic.line([(76, 72), (70, 74), (52, 54)], 4.4), 0xE5A50A)
    ic.fill(ic.circ(75, 72, 5.2), 0xF5C211)
    if not ic.small:
        for x in (22, 78):
            ic.fill(ic.circ(x, 16.5, 2), 0x9A9996)


@icon('pkgman', 0x7A69F0, 0x3E2DB5, free=True)
def pkgman(ic):
    """Le colis, vu de trois quarts : déjà un objet, plus grand, la bande d'adhésif."""
    top = [(50, 10), (88, 28), (50, 46), (12, 28)]
    left = [(12, 28), (50, 46), (50, 90), (12, 72)]
    right = [(88, 28), (50, 46), (50, 90), (88, 72)]
    if not ic.small:
        ic.shadow(ic.poly([(12, 28), (50, 10), (88, 28), (88, 72), (50, 90), (12, 72)]), dy=1.2, blur=1.6, op=.22, dx=0)
    ic.fill(ic.poly(left), Grad(0xCDAB8F, 0xB5835A, 90))
    ic.fill(ic.poly(right), Grad(0xB5835A, 0x986A44, 90))
    ic.fill(ic.poly(top), Grad(0xE9D2BC, 0xDCBFA2, 90))
    ic.fill(ic.poly([(28, 20.5), (66, 38.5), (66, 46), (75, 41.5), (75, 34), (37, 16)]), 0xF6F5F4, .95)
    if not ic.small:
        ic.fill(ic.line([(50, 46), (50, 90)], .9), 0x63452C, .35)


@icon('welcome', 0x6371F2, 0x3323A8, free=True)
def welcome(ic):
    """Une pierre taillée (le losange de Korin), plus grande, posée sur son ombre."""
    tl, tr, l, r = (30, 18), (70, 18), (8, 38), (92, 38)
    b = (50, 90)
    if not ic.small:
        ic.shadow(ic.poly([tl, tr, r, b, l]), dy=1.2, blur=1.6, op=.22, dx=0)
    a, c, t = (34, 38), (66, 38), (50, 18)
    ic.fill(ic.poly([tl, t, a]), 0xDCE8FB)
    ic.fill(ic.poly([t, tr, c]), 0xB9D2F6)
    ic.fill(ic.poly([t, a, c]), 0xFFFFFF)
    ic.fill(ic.poly([l, tl, a]), 0x99C1F1)
    ic.fill(ic.poly([r, tr, c]), 0x62A0EA)
    ic.fill(ic.poly([l, a, b]), Grad(0x99C1F1, 0x62A0EA, 90))
    ic.fill(ic.poly([a, c, b]), Grad(0xDCE8FB, 0x99C1F1, 90))
    ic.fill(ic.poly([c, r, b]), Grad(0x62A0EA, 0x1C71D8, 90))


@icon('uidemo', 0xF06AA8, 0x9B2C8E)
def uidemo(ic):
    ic.raised(ic.rr(15, 19, 70, 62, 7), PAPER)
    ic.fill(ic.rr(23, 29, 30, 13, 6.5), Grad(0x47C98D, 0x20A26A, 100))
    ic.raised(ic.circ(46.5, 35.5, 5.2), 0xFFFFFF, sh=False)
    ic.fill(ic.rr(60, 29, 17, 13, 3.5), 0x4F7CF0)
    ic.fill(ic.rr(23, 52, 54, 4.5, 2.2), 0xC9D2DE)
    ic.fill(ic.rr(23, 52, 34, 4.5, 2.2), 0xE65C9C)
    ic.raised(ic.circ(57, 54, 6), 0xFFFFFF)
    if not ic.small:
        ic.fill(ic.rr(23, 66, 22, 5, 2.5), 0xB9C4D2)
        ic.fill(ic.rr(51, 66, 26, 5, 2.5), 0xB9C4D2)


@icon('builder', 0xF59A3E, 0xC4501A)
def builder(ic):
    ic.push(0, 0, 1, 45)
    # cle plate
    w = sub(add(ic.rr(44.5, 26, 11, 60, 4), ic.circ(50, 24, 14)), ic.poly([(44.5, 4), (55.5, 4), (55.5, 24), (44.5, 24)]))
    ic.raised(w, Grad(0xF4F7FB, 0xB9C3CF, 100))
    ic.pop()
    ic.push(0, 0, 1, -45)
    ic.raised(ic.rr(45.5, 34, 9, 52, 3), Grad(0x9E6A3C, 0x6E4422, 100))
    ic.raised(ic.rr(30, 16, 40, 18, 3.5), Grad(0xE7ECF2, 0x98A4B3, 100))
    ic.pop()


@icon('mixer', 0x56657C, 0x2C3444)
def mixer(ic):
    for x, y, c in ((27, 58, 0x4FD08C), (50, 33, 0xF5B83D), (73, 66, 0xF0596A)):
        ic.fill(ic.rr(x - 2.2, 18, 4.4, 64, 2.2), 0x10141B, .7)
        ic.fill(ic.rr(x - 2.2, y, 4.4, 82 - y, 2.2), c, .9)
        ic.raised(ic.rr(x - 9, y - 6, 18, 12, 3), Grad(0xFFFFFF, 0xCBD3DE, 100))
        ic.fill(ic.rr(x - 6, y - .9, 12, 1.8, 0), c)


@icon('corbeille', 0x8B97A6, 0x4C5766, free=True)
def corbeille(ic):
    """La corbeille en objet (Adwaita) : un seau gris clair cannelé, son couvercle, leurs épaisseurs."""
    body = ic.soften(ic.poly([(24, 32), (76, 32), (70, 86), (30, 86)]), 1.4)
    obj(ic, body, Grad(0xDEDDDA, 0xC0BFBC, 0), 0x77767B, 4)
    for x in (37, 50, 63):
        ic.fill(ic.line([(x, 40), (50 + (x - 50) * .8, 78)], 3.6), 0x9A9996)
    obj(ic, ic.rr(18, 20, 64, 10, 4), Grad(0xF6F5F4, 0xDEDDDA, 90), 0x77767B, 3, sh=.15)
    obj(ic, sub(ic.rr(38, 11, 24, 12, 4), ic.rr(42.5, 15.5, 15, 10, 2)), 0xF6F5F4, 0x77767B, 2, sh=0)


@icon('theme', 0xA176F0, 0x5A36C4)
def theme(ic):
    ic.shadow(add(ic.circ(36, 40, 21), ic.circ(64, 40, 21), ic.circ(50, 63, 21)))
    a, b, c = ic.circ(36, 40, 21), ic.circ(64, 40, 21), ic.circ(50, 63, 21)
    ic.fill(a, Grad(0xFF7A85, 0xE8384F, 100))
    ic.fill(b, Grad(0x6AB3FF, 0x2C6FE8, 100))
    ic.fill(c, Grad(0xFFDC6E, 0xF5A623, 100))
    ic.fill(inter(a, b), Grad(0xC77BEA, 0x9A4FD6, 100))
    ic.fill(inter(a, c), Grad(0xFF9E5A, 0xF2782F, 100))
    ic.fill(inter(b, c), Grad(0x8FE0A4, 0x45C07A, 100))
    ic.fill(inter(inter(a, b), c), 0xFFFFFF)


@icon('clavier', 0x4C5A73, 0x262E3F, free=True)
def clavier(ic):
    """Le clavier en objet : un socle clair, les touches avec leur épaisseur."""
    obj(ic, ic.rr(8, 26, 84, 46, 8), Grad(0xF6F5F4, 0xDEDDDA, 90), 0x9A9996, 6)
    k, kd = 0x5E5C64, 0x3D3846
    if ic.small:
        for r in range(3):
            ic.fill(ic.rr(15, 33 + r * 12, 70, 7, 1.6), k)
        return
    for i in range(7):
        obj(ic, ic.rr(14 + i * 10.4, 32, 8.6, 8, 1.8), k, kd, 1.5, sh=0)
    for i in range(6):
        obj(ic, ic.rr(19 + i * 10.4, 43.5, 8.6, 8, 1.8), k, kd, 1.5, sh=0)
    obj(ic, ic.rr(27, 55, 46, 8, 1.8), k, kd, 1.5, sh=0)


@icon('ecran', 0x35A0F4, 0x1458C4, free=True)
def ecran(ic):
    """L'écran en objet : le cadre, une image, le pied."""
    f = adw_screen(ic, 8, 12, 84, 58, Grad(0x9141AC, 0xF66151, 120))
    ic.fill(inter(f, ic.poly([(8, 64), (32, 44), (46, 55), (64, 38), (92, 64), (92, 70), (8, 70)])), Grad(0x613583, 0x813D9C, 90), .8)
    if not ic.small:
        ic.fill(inter(f, ic.circ(68, 29, 6.5)), 0xF9F06B)
    adw_stand(ic, 74)


def _panel_ic(ic):
    f = screen(ic, 13, 20, 74, 58, None, Grad(0xC8E9FF, 0x8CC7F0, 100), 5.5, 2.6)
    ic.fill(inter(f, ic.rr(14, 60, 72, 20, 0)), 0x1C2533, .92)
    for i, c in enumerate((0x4FB7F5, 0xF5B83D, 0xF0596A, 0x4FD08C)):
        ic.fill(ic.rr(22 + i * 15, 63.5, 11, 9, 2.6), c)
    if not ic.small:
        ic.raised(ic.rr(24, 29, 34, 22, 2.6), PAPER, rim=0)
        ic.fill(ic.rr(24, 29, 34, 5, 1.4), 0x4F7CF0)


@icon('dockconf', 0x31BD8E, 0x0F7E78)
def dockconf(ic):
    _panel_ic(ic)


@icon('panelconf', 0x31BD8E, 0x0F7E78)
def panelconf(ic):
    _panel_ic(ic)


@icon('wifi', 0x3F9CF6, 0x135CC8)
def wifi(ic):
    w = 9
    m = add(ic.arc(50, 72, 47, -135, -45, w), ic.arc(50, 72, 30.5, -135, -45, w), ic.arc(50, 72, 14, -135, -45, w), ic.circ(50, 72, 6.6))
    ic.raised(m, WHITE)


@icon('sound', 0xFF7768, 0xD2263F)
def sound(ic):
    m = ic.soften(ic.poly([(16, 39), (31, 39), (50, 21), (50, 79), (31, 61), (16, 61)]), 1.5)
    m = add(m, ic.arc(50, 50, 16, -48, 48, 6.5), ic.arc(50, 50, 30, -48, 48, 6.5))
    ic.raised(m, WHITE)


@icon('padconf', 0x6A78F4, 0x3A2DBD, free=True)
def padconf(ic):
    """La manette en objet : le corps clair, la croix, deux boutons."""
    m = ic.soften(add(ic.rr(10, 28, 80, 36, 18),
                      ic.poly([(13, 48), (7, 72), (11, 80), (20, 81), (30, 69), (36, 60)]),
                      ic.poly([(87, 48), (93, 72), (89, 80), (80, 81), (70, 69), (64, 60)])), 1.6)
    obj(ic, m, Grad(0xF6F5F4, 0xDEDDDA, 90), 0x9A9996, 5)
    cross_pad(ic, 30, 45, 17, 0x5E5C64)
    obj(ic, ic.circ(63, 50, 5.4), 0x33D17A, 0x26A269, 1.5, sh=0)
    obj(ic, ic.circ(74, 41, 5.4), 0xE01B24, 0xA51D2D, 1.5, sh=0)


@icon('preload', 0xFFB938, 0xE5721A)
def preload(ic):
    # une barrette de memoire et l'eclair : ce qui demarre plus vite
    ic.raised(ic.rr(13, 38, 74, 36, 4), Grad(0x2F8C63, 0x1C6346, 100))
    for i in range(4):
        ic.fill(ic.rr(19 + i * 16, 45, 13, 18, 1.6), Grad(0x2A3342, 0x161C26, 100))
    if not ic.small:
        for i in range(12):
            ic.fill(ic.rr(18 + i * 5.45, 68, 3.4, 6, .6), 0xF7D774)
    ic.raised(bolt(ic, 52, 40, 1.14), Grad(0xFFFFFF, 0xFFE9A8, 100))


@icon('dotnet', 0x8A52E8, 0x4A1FA8)
def dotnet(ic):
    p = ic.ell_pts(50, 50, 37, 37, -90, 270, 6)
    h = ic.soften(ic.poly(p), 1.6)
    ic.raised(sub(h, ic.soften(ic.poly(ic.ell_pts(50, 50, 28, 28, -90, 270, 6)), 1.2)), WHITE)
    ic.raised(add(ic.line([(44, 39), (33, 50), (44, 61)], 6), ic.line([(56, 39), (67, 50), (56, 61)], 6)), Grad(0xFFE08A, 0xF6B73C, 100))


@icon('printconf', 0x3DB0B4, 0x17707E, free=True)
def printconf(ic):
    """L'imprimante en objet : la feuille qui entre, le corps, la feuille imprimée qui sort."""
    obj(ic, ic.rr(28, 10, 44, 30, 3), Grad(0xFFFFFF, 0xF6F5F4, 90), 0xC0BFBC, 2, sh=0)
    obj(ic, ic.rr(10, 32, 80, 38, 9), Grad(0xDEDDDA, 0xC0BFBC, 90), 0x77767B, 6)
    ic.fill(ic.rr(10, 32, 80, 6, 4), 0xF6F5F4, .7)
    ic.fill(ic.rr(19, 43, 10, 5, 2.5), 0x33D17A)
    ic.fill(ic.rr(22, 58, 56, 6, 3), 0x3D3846)
    obj(ic, ic.rr(28, 61, 44, 27, 2.4), Grad(0xFFFFFF, 0xF6F5F4, 90), 0xC0BFBC, 2, sh=.12)
    lines(ic, 35, 68, 30, 2, 7, 3, 0x9A9996)


@icon('webstudio', 0x8A6DF2, 0x4B32C8)
def webstudio(ic):
    # WebStudio (docs/DESIGN-WEBSTUDIO.md) : une page de code, « </> » dessus
    ic.raised(ic.rr(16, 18, 68, 64, 8), Grad(0xFFFFFF, 0xE6EAF2, 100))
    ic.fill(ic.rr(16, 18, 68, 12, 8), 0xD9DEEA)
    if not ic.small:
        for k, c in enumerate((0xFF6B5E, 0xF5B342, 0x3FC07F)):
            ic.fill(ic.circ(24 + 7 * k, 24, 2.2), c)
    w = 5.2 if not ic.small else 6.4
    ink = 0x5B3FD6
    ic.fill(ic.line([(40, 44), (29, 55), (40, 66)], w), ink)
    ic.fill(ic.line([(60, 44), (71, 55), (60, 66)], w), ink)
    ic.fill(ic.line([(54, 40), (46, 70)], w), 0x8A6DF2)


@icon('webd', 0x34C3A1, 0x0E7A6E)
def webd(ic):
    # le serveur web (docs/WEB.md) : deux serveurs empiles et leurs voyants, un globe devant, en bas a droite
    for y in (16, 42):
        ic.raised(ic.rr(14, y, 62, 22, 5), Grad(0xFFFFFF, 0xDDE4EE, 100))
        ic.fill(ic.circ(24, y + 11, 3.4), 0x26B47A)
        if not ic.small:
            for k in range(3):
                ic.fill(ic.rr(36 + 10 * k, y + 8, 6, 6, 1.5), 0xB3BFCE)
    g = ic.circ(67, 69, 19)
    ic.shadow(g, 1.2, 1.2, .25)
    ic.fill(g, Grad(0x5DBBFF, 0x1A62D8, 100))
    w = 1.6 if not ic.small else 2.4
    ic.fill(inter(g, ic.line([(46, 69), (88, 69)], w)), 0xFFFFFF, .9)
    ic.fill(inter(g, ic.line([(67, 48), (67, 90)], w)), 0xFFFFFF, .9)
    ic.fill(inter(g, sub(ic.ell(67, 69, 9, 19), ic.ell(67, 69, 9 - w, 19 - w))), 0xFFFFFF, .9)
    if not ic.small:
        ic.fill(inter(g, ic.line([(49, 60), (85, 60)], w * .8)), 0xFFFFFF, .7)
        ic.fill(inter(g, ic.line([(49, 78), (85, 78)], w * .8)), 0xFFFFFF, .7)


@icon('clipboard', 0x7D8EA8, 0x445269, free=True)
def clipboard(ic):
    """Le presse-papiers en objet : la planchette, la feuille, la pince."""
    obj(ic, ic.rr(18, 16, 64, 72, 7), Grad(0xB5835A, 0x986A44, 90), 0x63452C, 5)
    ic.fill(ic.rr(25, 25, 50, 56, 3), Grad(0xFFFFFF, 0xF6F5F4, 90))
    lines(ic, 33, 40, 34, 4, 9, 3.6, 0xC0BFBC)
    obj(ic, ic.rr(35, 10, 30, 15, 4), Grad(0xDEDDDA, 0xC0BFBC, 90), 0x77767B, 3, sh=.15)
    ic.fill(ic.rr(44, 13.5, 12, 4, 2), 0x77767B)


# --- accessoires --------------------------------------------------------------------------------------------------
@icon('calculator', 0x566072, 0x282E3A, free=True)
def calculator(ic):
    """La calculatrice en objet (Adwaita) : un corps gris foncé, l'écran clair, les touches avec leur épaisseur."""
    obj(ic, ic.rr(20, 8, 60, 78, 9), Grad(0x5E5C64, 0x4A4850, 90), 0x2E2C33, 6)
    ic.fill(ic.rr(26, 15, 48, 17, 3), Grad(0xDFF5E6, 0xC4E8CF, 90))
    if not ic.small:
        ic.fill(ic.rr(52, 20.5, 17, 6, 1.4), 0x2E7D4A)
    for r in range(3):
        for c in range(3):
            k, kd = (0xFF8A3D, 0xC25B12) if c == 2 else (0xDEDDDA, 0x9A9996)
            obj(ic, ic.rr(26 + c * 16.4, 38 + r * 14.4, 13.4, 10.2, 2.4), k, kd, 1.8, sh=0)


@icon('clock', 0xFFA94E, 0xE0561E, free=True)
def clock(ic):
    """L'horloge en objet (Adwaita) : un boîtier rond orangé, son épaisseur, le cadran blanc."""
    obj(ic, ic.circ(50, 47, 40), Grad(0xFFA348, 0xF57C20, 90), 0xB4520C, 6)
    ic.fill(ic.circ(50, 47, 32), Grad(0xFFFFFF, 0xEEEDEC, 90))
    if not ic.small:
        for i in range(12):
            a = math.radians(i * 30)
            q = 5 if i % 3 == 0 else 2.6
            ic.fill(ic.line([(50 + 27 * math.sin(a), 47 - 27 * math.cos(a)), (50 + (27 - q) * math.sin(a), 47 - (27 - q) * math.cos(a))], 2.2), 0x3D3846)
    ic.fill(ic.line([(50, 47), (37, 39)], 5.2), 0x3D3846)
    ic.fill(ic.line([(50, 47), (61, 25)], 3.8), 0x3D3846)
    ic.fill(ic.circ(50, 47, 3.8), 0xE01B24)


@icon('jamendo', 0x2BD4B0, 0x157A8E)
def jamendo(ic):
    # deux croches sur une onde : la musique libre
    if not ic.small:
        ic.fill(ic.arc(50, 88, 46, 210, 330, 3.2), 0xFFFFFF, .35)
        ic.fill(ic.arc(50, 88, 36, 220, 320, 3.2), 0xFFFFFF, .25)
    beam = ic.poly([(38, 28), (72, 20), (72, 29), (38, 37)])
    stems = ic.many([ic.rr_pts(35, 30, 5, 38, 1.5), ic.rr_pts(69, 22, 5, 38, 1.5)])
    heads = ic.many([ic.ell_pts(30, 69, 10, 8), ic.ell_pts(64, 61, 10, 8)])
    for m in (beam, stems, heads):
        ic.shadow(m, 1.2, 1.4, .25)
    ic.fill(beam, 0xFFFFFF)
    ic.fill(stems, 0xFFFFFF)
    ic.fill(heads, Grad(0xFFFFFF, 0xE4F6F2, 100))


@icon('spotify', 0x2B3236, 0x121618)
def spotify(ic):
    # des barres d'égaliseur vertes : la musique en flux (pas le logo de la marque)
    for i, h in enumerate((26, 44, 58, 38, 50, 30)):
        x = 22 + i * 10
        bar = ic.rr(x, 74 - h, 7, h, 3.5)
        ic.shadow(bar, 1, 1.4, .3)
        ic.fill(bar, Grad(0x5BEA92, 0x1DA557, 100))


@icon('netflix', 0xE5414A, 0x8E1219)
def netflix(ic):
    # un écran et le bouton lecture : les films et les séries en flux (pas le logo de la marque)
    video_screen(ic)
    ic.fill(ic.poly([(44, 35), (44, 55), (60, 45)]), 0xF0444C)


def video_screen(ic):
    # l'écran des apps de vidéo (Netflix, Disney+, TF1+, Prime Video) : un téléviseur sombre, son pied
    scr = ic.rr(16, 22, 68, 46, 7)
    ic.shadow(scr, 1.2, 1.6, .3)
    ic.fill(scr, Grad(0xFFFFFF, 0xE6E9EE, 100))
    ic.fill(ic.rr(21, 27, 58, 36, 4), Grad(0x2A2F36, 0x14171B, 100))
    if not ic.small:
        ic.fill(ic.rr(38, 72, 24, 5, 2.5), 0xFFFFFF, .85)


@icon('disneyplus', 0x3C7BE0, 0x0E2A6B)
def disneyplus(ic):
    # un écran, une étoile et un arc : la magie des dessins animés (pas le logo de la marque)
    video_screen(ic)
    if not ic.small:
        ic.fill(ic.arc(50, 62, 22, 220, 320, 2.2), 0x8FD0FF)
    pts = []
    for i in range(10):
        r = 10 if i % 2 == 0 else 4.2
        a = math.radians(i * 36)
        pts.append((50 + r * math.sin(a), 44 - r * math.cos(a)))
    ic.fill(ic.poly(pts), 0xFFFFFF)


@icon('tf1plus', 0x3A8DFF, 0x123C9C)
def tf1plus(ic):
    # un écran, son antenne et un plus : la télévision en direct et en replay (pas le logo de la marque)
    if not ic.small:
        ic.fill(ic.line([(50, 22), (38, 10)], 2.4), 0xFFFFFF)
        ic.fill(ic.line([(50, 22), (62, 10)], 2.4), 0xFFFFFF)
    video_screen(ic)
    ic.fill(ic.rr(46, 33, 8, 24, 2), 0xFFD23F)
    ic.fill(ic.rr(38, 41, 24, 8, 2), 0xFFD23F)


@icon('primevideo', 0x2FB4F0, 0x0B5C8E)
def primevideo(ic):
    # un écran, un disque et le bouton lecture : la vidéo à la demande (pas le logo de la marque)
    video_screen(ic)
    ic.fill(ic.circ(50, 45, 12), 0x29A8E8)
    ic.fill(ic.poly([(46, 38), (46, 52), (57, 45)]), 0xFFFFFF)


@icon('weather', 0x63C0F7, 0x2A7BD8, free=True)
def weather(ic):
    """Le soleil derrière un nuage, en objets (pas de plaque)."""
    if not ic.small:
        for i in range(8):
            a = math.radians(i * 45 + 22.5)
            ic.fill(ic.line([(62 + 24 * math.sin(a), 34 - 24 * math.cos(a)), (62 + 31 * math.sin(a), 34 - 31 * math.cos(a))], 3.8), 0xF6D32D)
    obj(ic, ic.circ(62, 34, 19), Grad(0xF8E45C, 0xF6D32D, 90), 0xE5A50A, 3, sh=.12)
    cl = ic.many([ic.ell_pts(31, 66, 16, 16), ic.ell_pts(52, 56, 21, 21), ic.ell_pts(72, 67, 15, 15),
                  ic.rr_pts(15, 64, 72, 18, 9)])
    obj(ic, cl, Grad(0xFFFFFF, 0xF6F5F4, 90), 0x9A9996, 5)


@icon('editor', 0x8A6DF2, 0x4C2FC0, free=True)
def editor(ic):
    """L'éditeur de texte : une feuille en objet, un crayon posé dessus."""
    adw_doc(ic, 16, 8, 60, 80)
    lines(ic, 26, 30, 38, 5, 10, 3.6, 0xC0BFBC)
    ic.push(0, 0, 1, 40, (66, 62))
    pen = ic.rr(60, 24, 13, 50, 2)
    ic.shadow(pen, dy=1.0, blur=.8, op=.25, dx=0)
    ic.fill(ic.rr(60, 34, 13, 40, 0), Grad(0xF6D32D, 0xF5C211, 0))
    ic.fill(ic.rr(66.5, 34, 6.5, 40, 0), 0xE5A50A)
    ic.fill(ic.rr(60, 24, 13, 8, 3), 0xF66151)
    ic.fill(ic.rr(60, 30, 13, 4.5, 0), 0xC0BFBC)
    ic.fill(ic.poly([(60, 74), (73, 74), (66.5, 88)]), 0xF9DDB8)
    ic.fill(ic.poly([(64.2, 82.5), (68.8, 82.5), (66.5, 88)]), 0x3D3846)
    ic.pop()


@icon('viewer', 0x2EC5C2, 0x0D7F9C)
def viewer(ic):
    # un oeil : voir
    eye = ic.poly([(13 + i * 74 / 60, 50 - 27 * math.sin(i / 60 * math.pi) ** 1.05) for i in range(61)] +
                  [(87 - i * 74 / 60, 50 + 27 * math.sin(i / 60 * math.pi) ** 1.05) for i in range(61)])
    ic.raised(eye, WHITE)
    ic.fill(ic.circ(50, 50, 19), Rad(0x4FD7F0, 0x0D6FA0, .5, .6, .62))
    ic.fill(ic.circ(50, 50, 9.5), 0x10202E)
    ic.fill(ic.circ(45, 44, 4.5), 0xFFFFFF)


@icon('imageview', 0x86E0B4, 0x1F9C7A, free=True)
def imageview(ic):
    """Un tableau en objet (un paysage dans son cadre) et une loupe devant."""
    obj(ic, ic.rr(8, 12, 76, 62, 5), Grad(0xFFFFFF, 0xF6F5F4, 90), 0xC0BFBC, 4)
    f = ic.rr(14, 18, 64, 50, 2)
    ic.fill(f, Grad(0x62A0EA, 0x99C1F1, 90))
    ic.fill(ic.circ(62, 30, 6), 0xF8E45C)
    ic.fill(inter(f, ic.poly([(14, 60), (30, 40), (42, 53), (51, 45), (78, 64), (78, 68), (14, 68)])), Grad(0x33D17A, 0x26A269, 90))
    ic.fill(ic.line([(76, 76), (88, 88)], 8.5), 0x3D3846)
    ic.fill(ic.circ(67, 67, 13), 0xFFFFFF, .45)
    obj(ic, ic.ring(67, 67, 13, 4.6), 0x5E5C64, 0x241F31, 1.5, sh=.15)


@icon('screenshot', 0x5BC8F5, 0x1E6FD0)
def screenshot(ic):
    # un ecran, ses fenetres, une zone choisie et ses coins : un outil de capture
    f = screen(ic, 13, 17, 74, 52)
    ic.fill(inter(f, ic.rr(20, 24, 32, 22, 2)), 0x3A475C)
    ic.fill(inter(f, ic.rr(42, 36, 38, 26, 2)), 0x4B5D79)
    stand(ic, 69)
    x0, y0, x1, y1 = 29, 29, 71, 57
    ic.fill(ic.rr(x0, y0, x1 - x0, y1 - y0, 0), 0x9CC8FF, .38)
    if not ic.small:
        for i in range(6):
            a = x0 + 6 + i * 6
            ic.fill(ic.rr(a, y0 - .6, 3, 1.2), 0xFFFFFF, .9)
            ic.fill(ic.rr(a, y1 - .6, 3, 1.2), 0xFFFFFF, .9)
        for i in range(3):
            b = y0 + 6 + i * 6.5
            ic.fill(ic.rr(x0 - .6, b, 1.2, 3), 0xFFFFFF, .9)
            ic.fill(ic.rr(x1 - .6, b, 1.2, 3), 0xFFFFFF, .9)
    k = 8.5
    for cx, cy, dx, dy in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
        ic.fill(ic.line([(cx + dx * k, cy), (cx, cy), (cx, cy + dy * k)], 3.6), 0xFFFFFF)


@icon('archiver', 0xD9A441, 0x94601C, free=True)
def archiver(ic):
    """Un carton fermé par une fermeture éclair, en objet."""
    obj(ic, ic.rr(12, 14, 76, 70, 7), Grad(0xDEB48A, 0xCDAB8F, 90), 0x865E3C, 6)
    ic.fill(ic.rr(12, 14, 76, 6, 3), 0xE9C9A6)
    ic.fill(ic.rr(44, 14, 12, 70, 0), 0x63452C, .95)
    n = 7 if not ic.small else 4
    for i in range(n):
        y = 18 + i * 46 / (n - 1 or 1)
        ic.fill(ic.rr(46.6 if i % 2 == 0 else 50, y, 3.4 * (1.0 if not ic.small else 1.3), 4, .6), 0xF8E45C)
    obj(ic, ic.rr(44.4, 62, 11.2, 17, 3), Grad(0xF6F5F4, 0xDEDDDA, 90), 0x9A9996, 2, sh=0)
    ic.fill(ic.rr(48, 67, 4, 8, 2), 0x77767B)


@icon('calendar', 0xFF6B5E, 0xCC2437, free=True)
def calendar(ic):
    """Le calendrier en objet (Adwaita) : une page blanche, sa bande rouge, ses anneaux, son épaisseur."""
    m = ic.rr(14, 14, 72, 70, 9)
    obj(ic, m, Grad(0xFFFFFF, 0xF1F0EF, 90), 0xB5B3AF, 6)
    ic.fill(inter(m, ic.rr(10, 10, 80, 22, 0)), Grad(0xF0453A, 0xD92B2B, 90))
    for x in (32, 68):
        ic.fill(ic.rr(x - 3.5, 8, 7, 15, 3.5), 0x77767B)
    for r in range(3):
        for c in range(4):
            if r == 2 and c == 3:
                continue
            k = 0xE01B24 if (r, c) == (1, 2) else 0x9A9996
            ic.fill(ic.rr(22.5 + c * 14.6, 41 + r * 13, 10, 9, 2.2), k)


# --- bureautique --------------------------------------------------------------------------------------------------
@icon('letters', 0x4C93F5, 0x1E50C6, free=True)
def letters(ic):
    """Un document de texte en objet, ses lignes bleues."""
    adw_doc(ic, 18, 8, 64, 80)
    lines(ic, 28, 28, 44, 6, 9.4, 3.6, 0x3584E4, .55)


@icon('sheet', 0x3CC87F, 0x0E7E54, free=True)
def sheet(ic):
    """Le tableur : une feuille en objet, sa grille, l'en-tête vert."""
    m = ic.rr(10, 14, 80, 72, 7)
    obj(ic, m, Grad(0xFFFFFF, 0xF6F5F4, 90), 0xC0BFBC, 5)
    ic.fill(inter(m, ic.rr(8, 12, 84, 15, 0)), Grad(0x33D17A, 0x2EC27E, 90))
    ic.fill(inter(m, ic.rr(8, 12, 16, 76, 0)), 0x2EC27E, .85)
    ic.fill(ic.rr(25, 42, 21, 14, 0), 0xC4F2D3)
    ic.fill(ic.rr(47, 56, 21, 14, 0), 0xC4F2D3)
    for x in (24, 46, 68):
        ic.fill(ic.rr(x, 14, 1.2, 72), 0x9A9996, .7)
    for y in (27, 41, 55, 69):
        ic.fill(ic.rr(10, y, 80, 1.2), 0x9A9996, .7)


@icon('slides', 0xFF9A45, 0xDC4A17, free=True)
def slides(ic):
    """La présentation : un tableau en objet sur son chevalet, un camembert orange."""
    ic.fill(ic.line([(50, 68), (36, 90)], 4.4), 0x77767B)
    ic.fill(ic.line([(50, 68), (64, 90)], 4.4), 0x77767B)
    obj(ic, ic.rr(10, 14, 80, 56, 6), Grad(0xFFFFFF, 0xF6F5F4, 90), 0xC0BFBC, 4)
    obj(ic, ic.rr(7, 9, 86, 8, 4), 0x5E5C64, 0x3D3846, 2.5, sh=0)
    ic.fill(ic.circ(34, 43, 15), 0xDEDDDA)
    ic.fill(ic.poly(ic.ell_pts(34, 43, 15, 15, -90, 30) + [(34, 43)]), Grad(0xFFA348, 0xFF7800, 90))
    if not ic.small:
        lines(ic, 56, 33, 26, 3, 9, 4, 0xC0BFBC)


@icon('cardfile', 0xE8B23A, 0xB0701A)
def cardfile(ic):
    for i, c in enumerate((0xF6A06A, 0x7CC2F0, 0xFFFFFF)):
        y = 15 + i * 12
        m = add(ic.rr(15, y + 8, 70, 84 - y - 8, 5), ic.rr(22 + i * 19, y, 20, 14, 4))
        ic.raised(ic.soften(m, .6), Grad(c, dark(c, .07), 100), rim=.35)
    lines(ic, 25, 58, 50, 3, 7.4, 3.2, 0x9FB0C2)


@icon('budget', 0x4FD08F, 0x0F8A5C)
def budget(ic):
    # le portefeuille : deux billets qui depassent, le rabat, une piece doree
    for i, c in enumerate((0xC9F2D9, 0xE6FAEE)):
        ic.push(0, 0, 1, -8 + i * 10, (50, 40))
        ic.raised(ic.rr(24 + i * 6, 18 + i * 3, 44, 30, 3), Grad(c, dark(c, .08), 100), rim=.3)
        ic.pop()
    ic.raised(ic.rr(16, 34, 68, 48, 8), Grad(0xFFFFFF, 0xE9EEF3, 100))
    ic.fill(ic.rr(54, 46, 30, 22, 6), Grad(0xDCE4EC, 0xC5D0DB, 100))
    ic.raised(ic.circ(66, 57, 7.5 if not ic.small else 9), Grad(0xFFE07A, 0xF2A922, 100), rim=.3)
    if not ic.small:
        ic.fill(ic.ring(66, 57, 4.4, 1.4), 0xD98A10, .8)


@icon('notes', 0xFFD84D, 0xEFA41B, free=True)
def notes(ic):
    """Le bloc-notes en objet : les feuilles jaunes, la reliure, les spirales."""
    obj(ic, ic.rr(18, 16, 64, 72, 6), Grad(0xFFFBE0, 0xFFF4B8, 90), 0xE5A50A, 5)
    ic.fill(ic.rr(18, 16, 64, 13, 5), Grad(0xF6D32D, 0xF5C211, 90))
    for i in range(5 if not ic.small else 3):
        x = 28 + i * (11 if not ic.small else 22)
        obj(ic, ic.rr(x - 1.8, 9, 3.6, 15, 1.8), 0xDEDDDA, 0x77767B, 1.2, sh=0)
    lines(ic, 28, 42, 44, 4, 10, 3, 0xD7B85A)


@icon('arabesque', 0x8A5BE8, 0x2A3FA8)
def arabesque(ic):
    # la spirale du chemin, des points de couleur dessus, et la note qui en sort
    pts = [(46 + (4 + t * 2.1) * math.cos(t * .62), 46 + (4 + t * 2.1) * math.sin(t * .62)) for t in range(0, 15)]
    ic.fill(ic.line(pts, 3.4 if not ic.small else 5), 0xE8E2FF, .9)
    for i, c in enumerate((0xFFC73D, 0x5CE0C6, 0xFF7FA8)):
        x, y = pts[4 + i * 4]
        ic.fill(ic.circ(x, y, 5.2 if not ic.small else 7), c)
    ic.raised(ic.ell(64, 74, 9.5, 7.5), 0xFFFFFF, rim=.3)
    ic.fill(ic.rr(70.5, 40, 3.8, 34, 1.4), 0xFFFFFF)
    ic.fill(ic.poly([(74, 40), (86, 46), (86, 53), (74, 48)]), 0xFFFFFF)


@icon('pdf', 0xF2595E, 0xB51E36, free=True)
def pdf(ic):
    """Un document PDF en objet : la feuille, une image, deux lignes, la bande rouge."""
    adw_doc(ic, 18, 8, 64, 80)
    ic.fill(ic.rr(28, 28, 44, 26, 3), Grad(0xF66151, 0xC01C28, 110))
    ic.fill(ic.poly([(28, 54), (41, 39), (49, 47), (58, 36), (72, 54)]), 0xA51D2D, .8)
    lines(ic, 28, 62, 44, 2, 9, 3.6, 0xC0BFBC)


@icon('photos', 0xFBC6E0, 0xA585EA, free=True)
def photos(ic):
    """Deux tirages en objets, l'un penché derrière l'autre."""
    ic.push(0, 0, 1, -10, (46, 50))
    obj(ic, ic.rr(14, 14, 58, 66, 3), Grad(0xDC8ADD, 0xC061CB, 90), 0x813D9C, 3)
    ic.pop()
    ic.push(0, 0, 1, 7, (54, 52))
    obj(ic, ic.rr(24, 14, 62, 74, 3), Grad(0xFFFFFF, 0xF6F5F4, 90), 0xC0BFBC, 4)
    f = ic.rr(30, 20, 50, 50, 1)
    ic.fill(f, Grad(0x62A0EA, 0x99C1F1, 90))
    ic.fill(ic.circ(68, 33, 6.5), 0xF8E45C)
    ic.fill(inter(f, ic.poly([(30, 62), (46, 43), (56, 55), (64, 47), (80, 62), (80, 70), (30, 70)])), Grad(0x33D17A, 0x26A269, 90))
    ic.pop()


# --- internet -----------------------------------------------------------------------------------------------------
@icon('jet', 0x39B8FF, 0x0E58D2)
def jet(ic):
    # un avion a reaction vu de dessus, cap haut-droite, sa trainee en arc
    tr = ic.arc(70, 88, 62, 188, 232, 8, 'butt')
    ic.fill(tr, 0xFFFFFF, .35)
    ic.push(4, -2, .96, 45)
    b = [(50, 10), (54.5, 20), (55.6, 38), (86, 60), (86, 67), (55.6, 57), (54.6, 74), (65, 83), (65, 88), (50, 84),
         (35, 88), (35, 83), (45.4, 74), (44.4, 57), (14, 67), (14, 60), (44.4, 38), (45.5, 20)]
    ic.raised(ic.soften(ic.poly(b), .7), Grad(0xFFFFFF, 0xD5DEEA, 90))
    ic.fill(ic.ell(50, 22, 3.2, 6.5), 0x2F6FD6)
    ic.pop()


@icon('mail', 0x4FB5F7, 0x1B64DC, free=True)
def mail(ic):
    """L'enveloppe en objet : le dos, le rabat, les plis."""
    m = ic.rr(8, 20, 84, 60, 8)
    obj(ic, m, Grad(0xFFFFFF, 0xF6F5F4, 90), 0x9A9996, 5)
    ic.fill(inter(m, ic.poly([(8, 80), (50, 46), (92, 80)])), Grad(0xF6F5F4, 0xDEDDDA, 90))
    fl = inter(m, ic.soften(ic.poly([(5, 18), (95, 18), (55, 56), (45, 56)]), 1))
    ic.shadow(fl, 1.0, 1.0, .18, dx=0)
    ic.fill(fl, Grad(0x99C1F1, 0x62A0EA, 90))


def bubble(ic, x, y, w, h, tailx, right=False):
    m = ic.rr(x, y, w, h, h * .34)
    if right:
        t = ic.poly([(tailx, y + h - 7), (tailx + 15, y + h - 7), (tailx + 16, y + h + 13)])
    else:
        t = ic.poly([(tailx, y + h - 7), (tailx + 16, y + h - 7), (tailx - 1, y + h + 13)])
    return ic.soften(add(m, t), .9)


@icon('irc', 0x34CFA6, 0x0B7E88)
def irc(ic):
    ic.raised(bubble(ic, 14, 18, 72, 54, 26), WHITE)
    c = Grad(0x1FA98F, 0x0F7E7A, 100)
    h = add(ic.line([(43, 28), (38, 62)], 5.6, 'butt'), ic.line([(62, 28), (57, 62)], 5.6, 'butt'),
            ic.line([(31, 38.5), (70, 38.5)], 5.6, 'butt'), ic.line([(29, 51.5), (68, 51.5)], 5.6, 'butt'))
    ic.fill(h, c)


@icon('telegram', 0x6C7DF8, 0x2A9BE0)
def telegram(ic):
    ic.raised(bubble(ic, 14, 18, 72, 54, 26), Grad(0x28306A, 0x1B2150, 100), rim=.25)
    ic.push(-1, -5, .78)
    p = [(84, 22), (18, 50), (40, 58), (46, 80), (58, 64), (74, 76)]
    ic.fill(ic.poly(p), Grad(0xFFFFFF, 0xD5E5FF, 110))
    ic.fill(ic.poly([(84, 22), (40, 58), (46, 80), (47, 62)]), 0xA9C6F7)
    ic.pop()


@icon('pocketqi', 0x5AA8F0, 0x2348B8)
def pocketqi(ic):
    # la bulle de l'assistante, et les deux yeux de Pocket (src/apps/pocketqi/core/pocket_visage.c) sur son ecran noir
    ic.raised(bubble(ic, 14, 20, 72, 52, 26), Grad(0x101722, 0x05080C, 100), rim=.25)
    for x in (37, 63):
        ic.fill(ic.rr(x - 7, 33, 14, 26, 6.5), Grad(0x7FE6FF, 0x38D6F5, 100))


@icon('courier', 0xFF9245, 0xD63C20)
def courier(ic):
    # requete et reponse : deux fleches qui se croisent, entre accolades
    def arrow(y, d, c):
        x0, x1 = (24, 72) if d > 0 else (76, 28)
        ic.raised(add(ic.line([(x0, y), (x1, y)], 8), ic.line([(x1 - d * 13, y - 12), (x1 + d * 3, y), (x1 - d * 13, y + 12)], 8)), c)
    arrow(35, 1, WHITE)
    arrow(65, -1, Grad(0xFFE9B0, 0xFFD06A, 100))


# --- creation -----------------------------------------------------------------------------------------------------
@icon('paint', 0xFF8FA8, 0xD9416E, free=True)
def paint(ic):
    """La palette de peintre en objet : le bois, le trou du pouce, les couleurs en relief."""
    pal = sub(ic.ell(48, 50, 41, 35), ic.ell(72, 66, 9, 8))
    obj(ic, ic.soften(pal, .7), Grad(0xDEB48A, 0xCDAB8F, 90), 0x865E3C, 5)
    for x, y, c, cd in ((26, 42, 0xE01B24, 0xA51D2D), (40, 29, 0xF6D32D, 0xE5A50A), (58, 28, 0x33D17A, 0x26A269),
                        (74, 39, 0x3584E4, 0x1A5FB4), (28, 61, 0x9141AC, 0x613583)):
        obj(ic, ic.circ(x, y, 7), c, cd, 2, sh=0)


@icon('iconedit', 0xEA5FBC, 0x8C2AB8)
def iconedit(ic):
    ic.raised(ic.rr(16, 16, 68, 68, 6), PAPER)
    pat = ['..XXXX..', '.XOOOOX.', 'XOOOOOOX', 'XOKOOKOX', 'XOOOOOOX', 'XOKKKKOX', '.XOOOOX.', '..XXXX..']
    col = {'X': 0x2A3342, 'O': 0xFFC73D, 'K': 0x2A3342}
    s = 7
    for y, row in enumerate(pat):
        for x, v in enumerate(row):
            if v != '.':
                ic.fill(ic.rr(22 + x * s, 22 + y * s, s, s, 0), col[v])
    if not ic.small:
        for i in range(9):
            ic.fill(ic.rr(22 + i * s - .2, 22, .4, 56), 0x000000, .1)
            ic.fill(ic.rr(22, 22 + i * s - .2, 56, .4), 0x000000, .1)


def _mandel(n=480):
    """L'ensemble de Mandelbrot lui-meme, en bandes de fuite lissees."""
    x = np.linspace(-2.05, .62, n)
    y = np.linspace(-1.335, 1.335, n)
    c = x[None, :] + 1j * y[:, None]
    z = np.zeros_like(c)
    v = np.zeros(c.shape, dtype=np.float32)
    alive = np.ones(c.shape, dtype=bool)
    for i in range(160):
        z[alive] = z[alive] ** 2 + c[alive]
        out = alive & (np.abs(z) > 8)
        v[out] = i + 1 - np.log2(np.log2(np.abs(z[out])) + 1e-9)
        alive &= ~out
    return v, alive


@icon('mandelbrot', 0x1E2A7A, 0x0A0E36)
def mandelbrot(ic):
    v, inside = _mandel()
    t = np.clip(np.sqrt(v / 26), 0, 1)
    stops = np.array([0, .4, .62, .82, 1])
    cols = np.array([[.05, .07, .28], [.12, .28, .78], [.3, .82, .95], [1, .86, .42], [1, 1, 1]])
    r = np.stack([np.interp(t, stops, cols[:, k]) for k in range(3)], -1)
    r[inside] = (.02, .02, .08)
    im = Image.fromarray(np.uint8(r * 255), 'RGB')
    a = int(ic.span)
    im = im.resize((a, a), Image.BICUBIC)
    base = np.asarray(ic.blank(), dtype=np.float32)
    px = np.zeros((M, M, 3), dtype=np.float32)
    x0, y0 = int(ic.ox), int(max(0, ic.oy))
    arr = np.asarray(im, dtype=np.float32) / 255
    h = min(M - y0, a)
    w = min(M - x0, a)
    px[y0:y0 + h, x0:x0 + w] = arr[:h, :w]
    t2 = np.asarray(ic.tile_mask, dtype=np.float32) / 255
    a2 = t2[:, :, None]
    ic.pix[:, :, :3] = px * a2 + ic.pix[:, :, :3] * (1 - a2)


@icon('fmtracker', 0x2C9DB0, 0x115F7A)
def fmtracker(ic):
    f = screen(ic, 15, 15, 70, 70, tone(0x12232E, .1, .1), 0x0C1820, 6, 2.4)
    if ic.small:
        rows = 5
    else:
        rows = 7
    st = 60 / rows
    for r in range(rows):
        y = 20 + r * st
        if r == rows // 2:
            ic.fill(ic.rr(17, y - .8, 66, st, 0), 0x3FD08C, .9)
            c0 = c1 = 0x0C1820
        else:
            c0, c1 = 0xE7EEF5, 0x6FD3E8
        if (r * 5) % 3 != 1:
            ic.fill(ic.rr(22, y + .8, 20, st - 4.6, 1), c0)
        if (r * 3) % 4 != 2:
            ic.fill(ic.rr(50, y + .8, 26, st - 4.6, 1), c1)


@icon('koton', 0xD24BE8, 0x4B23B8)
def koton(ic):
    # le rouleau de notes : des pistes, des notes, la tete de lecture
    f = screen(ic, 14, 18, 72, 64, tone(0x241848, .08, .1), Grad(0x1B1238, 0x140C2A, 100), 7, 2)
    if not ic.small:
        for i in range(1, 5):
            ic.fill(ic.rr(16, 19 + i * 12.4, 68, .7), 0xFFFFFF, .1)
    for x, y, w, c in ((20, 24, 22, 0xFF6FB0), (46, 24, 10, 0xFF6FB0), (28, 36.5, 14, 0x6FC8FF), (52, 36.5, 26, 0x6FC8FF),
                       (20, 49, 16, 0xFFC95C), (44, 49, 22, 0xFFC95C), (32, 61.5, 28, 0x7CF0B0), (66, 61.5, 12, 0x7CF0B0)):
        ic.fill(ic.rr(x, y, w, 9, 2.4), Grad(light(c, .22), c, 100))
    ic.fill(ic.rr(60, 19, 2.2, 62, 0), 0xFFFFFF, .95)


@icon('media', 0x7A5CF0, 0x2C2FB8)
def media(ic):
    # pellicule et lecture
    f = ic.rr(15, 22, 70, 56, 5)
    ic.raised(f, Grad(0x2F3644, 0x171B24, 100), rim=.25)
    n = 6
    for i in range(n):
        x = 21 + i * 11.2
        ic.fill(ic.rr(x, 26, 7, 6, 1.4), 0xFFFFFF, .9)
        ic.fill(ic.rr(x, 68, 7, 6, 1.4), 0xFFFFFF, .9)
    ic.raised(play_tri(ic, 50, 50, 13), Grad(0xFFE08A, 0xF6A623, 100), sh=False)


@icon('mediaplayer', 0x24D0B0, 0x0E70C8)
def mediaplayer(ic):
    ic.raised(ic.circ(50, 50, 36), WHITE)
    ic.fill(play_tri(ic, 53, 50, 20), Grad(0x1FC4B2, 0x1278C8, 110))


@icon('recorder', 0xEF4A6E, 0x9E1646, free=True)
def recorder(ic):
    """La cassette en objet : le boîtier, l'étiquette, la fenêtre et ses bobines."""
    obj(ic, ic.rr(8, 22, 84, 58, 7), Grad(0x5E5C64, 0x3D3846, 90), 0x241F31, 6)
    ic.fill(ic.rr(15, 29, 70, 30, 3), Grad(0xF66151, 0xE01B24, 90))
    ic.fill(ic.rr(15, 29, 70, 8, 3), 0xFFFFFF, .85)
    ic.fill(ic.rr(28, 41, 44, 14, 7), 0x241F31)
    for x in (36, 64):
        ic.fill(ic.circ(x, 48, 6), 0xF6F5F4)
        ic.fill(ic.circ(x, 48, 2.6), 0x241F31)
    ic.fill(ic.poly([(26, 80), (30, 66), (70, 66), (74, 80)]), 0x77767B)


@icon('piano', 0x3B4150, 0x15171E, free=True)
def piano(ic):
    """Le clavier de piano en objet : le corps noir, les touches blanches, les dièses."""
    obj(ic, ic.rr(8, 18, 84, 68, 7), Grad(0x3D3846, 0x241F31, 90), 0x000000, 6)
    x0, w, n = 13, 74 / 5, 5
    for i in range(n):
        obj(ic, ic.rr(x0 + i * w + .7, 26, w - 1.4, 54, 2.4), Grad(0xFFFFFF, 0xF6F5F4, 90), 0xC0BFBC, 2.4, sh=0)
    for i in (1, 2, 4):
        obj(ic, ic.rr(x0 + i * w - 4.8, 26, 9.6, 32, 2), 0x3D3846, 0x000000, 2, sh=0)


@icon('music', 0xFF6F91, 0xB5249E)
def music(ic):
    ic.push(-2, 0, 1.05)
    b = ic.soften(ic.poly([(38, 23), (78, 13), (78, 31), (38, 41)]), .5)
    g = add(tilted(ic, 34, 71, 11.5, 8.8, -20), tilted(ic, 68, 63, 11.5, 8.8, -20), ic.poly([(39.6, 72), (39.6, 28), (45, 28), (45, 72)]), ic.poly([(73.6, 64), (73.6, 22), (79, 22), (79, 64)]), b)
    ic.raised(g, WHITE)
    ic.pop()


# --- developpement ------------------------------------------------------------------------------------------------
@icon('qbasic', 0x2E5BE8, 0x0B1E96)
def qbasic(ic):
    f = screen(ic, 13, 17, 74, 66, tone(0xC9D1DC, .3, .08), Grad(0x1436C8, 0x0A209C, 100), 5, 2.6)
    ic.fill(inter(f, ic.rr(14, 19, 72, 8, 0)), 0xD6DCE5)
    c = 0xFFE36E
    ic.fill(ic.rr(21, 34, 18, 5, 1), c)
    ic.fill(ic.rr(43, 34, 28, 5, 1), 0xFFFFFF)
    ic.fill(ic.rr(28, 45, 22, 5, 1), 0xFFFFFF)
    ic.fill(ic.rr(54, 45, 16, 5, 1), 0x7CF0F0)
    ic.fill(ic.rr(21, 56, 14, 5, 1), c)
    ic.fill(ic.rr(39, 67, 9, 7, 0), 0xFFFFFF)


@icon('qbstudio', 0x1FB8D8, 0x1450B8)
def qbstudio(ic):
    # le dessinateur de fiches : une fenetre, un bouton et ses poignees
    ic.raised(ic.rr(14, 17, 72, 66, 6), PAPER)
    ic.fill(inter(ic.rr(14, 17, 72, 66, 6), ic.rr(14, 17, 72, 12, 0)), Grad(0x2E6FE0, 0x1C4FBE, 100))
    if not ic.small:
        for i in range(10):
            for j in range(7):
                ic.fill(ic.circ(21 + i * 6.6, 36 + j * 6.6, .6), 0x8FA3BC)
    ic.fill(ic.rr(30, 46, 40, 19, 3), Grad(0xFFD46A, 0xF2A72C, 100))
    for x, y in ((30, 46), (70, 46), (30, 65), (70, 65)):
        ic.fill(ic.rr(x - 3, y - 3, 6, 6, .8), 0x1C4FBE)


@icon('studio', 0x7B4DF0, 0x2340C8)
def studio(ic):
    def brace(x, d):
        q = []
        q += [(x + d * (9 - 9 * math.sin(a)), 28 - 9 * math.cos(a)) for a in np.linspace(0, math.pi / 2, 16)]
        q += [(x + d * (0 + 0), 28), (x, 41)]
        q += [(x + d * (-9 + 9 * math.cos(a)), 41 + 9 * math.sin(a)) for a in np.linspace(0, math.pi / 2, 16)]
        q += [(x + d * (-9 + 9 * math.sin(a)), 59 - 9 * math.cos(a)) for a in np.linspace(0, math.pi / 2, 16)]
        q += [(x, 59), (x, 72)]
        q += [(x + d * (9 - 9 * math.cos(a)), 72 + 9 * math.sin(a)) for a in np.linspace(0, math.pi / 2, 16)]
        return q
    ic.raised(add(ic.line(brace(34, 1), 7.2), ic.line(brace(66, -1), 7.2)), WHITE)
    ic.raised(ic.poly([(50, 37), (63, 50), (50, 63), (37, 50)]), Grad(0xFFD46A, 0xF2902C, 100))


@icon('gpiolab', 0x2AA866, 0x0C5E45)
def gpiolab(ic):
    # le connecteur du Pi, deux fils, une diode allumee
    n = 8 if not ic.small else 5
    w = 66 / n
    ic.raised(ic.rr(14, 17, 72, 27, 3.4), Grad(0x2A303C, 0x12161E, 100), rim=.3)
    for r in range(2):
        for c in range(n):
            ic.raised(ic.rr(17 + c * w + w * .14, 20.5 + r * 10.2, w * .72, w * .72 if n == 8 else 9, 1.2), Grad(0xFFEDA8, 0xD9A02C, 110), sh=False, rim=.5, dk=.2, e=.4)
    x1 = 17 + w * (n - 2.5)
    x2 = 17 + w * 1.5
    ic.fill(ic.line([(x1, 38), (x1, 54), (60, 62), (56, 70)], 4), 0xF0475C)
    ic.fill(ic.line([(x2, 38), (x2, 54), (40, 62), (44, 70)], 4), 0x2B3442)
    ic.shadow(ic.circ(50, 70, 13), 0, 4, .7, dx=0, color=0xFF4A5E)
    ic.fill(ic.rr(36, 74, 28, 7, 2.4), Grad(0xE8EDF3, 0xA7B3C2, 100))
    ic.raised(add(ic.circ(50, 66, 11.5), ic.rr(38.5, 66, 23, 10, 0)), Rad(0xFFD0D0, 0xF0283E, .4, .28, .95), sh=False)


@icon('turtle', 0x7ED957, 0x1E9E5E)
def turtle(ic):
    # tortue vue de dessus, cap haut-droite (la tortue LOGO)
    ic.push(0, 2, 1, 35)
    g = 0xB6E388
    ic.shadow(ic.ell(50, 54, 28, 32))
    for x, y, r in ((27, 34, -35), (73, 34, 35), (27, 76, 35), (73, 76, -35)):
        ic.push(0, 0, 1, r, (x, y))
        ic.fill(ic.ell(x, y, 8, 12.5), Grad(0xE9FBC8, g, 100))
        ic.pop()
    ic.fill(ic.ell(50, 18, 10, 12.5), Grad(0xE9FBC8, g, 100))
    if not ic.small:
        ic.fill(ic.circ(45, 12, 1.8), 0x173A27)
        ic.fill(ic.circ(55, 12, 1.8), 0x173A27)
    shell = ic.ell(50, 54, 28, 32)
    ic.raised(shell, Grad(0x3C8F4F, 0x1D5F38, 100), sh=False)
    hexes = [(50, 54, 10.5), (50, 32, 8.5), (50, 76, 8.5), (31, 43, 7.5), (69, 43, 7.5), (31, 65, 7.5), (69, 65, 7.5)]
    for x, y, r in (hexes if not ic.small else hexes[:1]):
        ic.fill(inter(ic.poly(ic.ell_pts(x, y, r, r, -90, 270, 6)), shell), Grad(0xA6E36B, 0x5FB84F, 100))
    ic.pop()


@icon('view3d', 0x344A78, 0x101A36, free=True)
def view3d(ic):
    """Le cube en trois tons, plus grand, posé sur son ombre."""
    t, l, r, b = (50, 8), (12, 29), (88, 29), (50, 50)
    bl, br, bb = (12, 71), (88, 71), (50, 92)
    if not ic.small:
        ic.shadow(ic.poly([t, r, br, bb, bl, l]), dy=1.2, blur=1.6, op=.22, dx=0)
    ic.fill(ic.poly([t, r, b, l]), Grad(0xF9F06B, 0xF6D32D, 90))
    ic.fill(ic.poly([l, b, bb, bl]), Grad(0xFFA348, 0xE66100, 90))
    ic.fill(ic.poly([r, b, bb, br]), Grad(0x62A0EA, 0x1C71D8, 90))


# --- jeux ---------------------------------------------------------------------------------------------------------
@icon('2048', 0xF7C565, 0xE58A2C)
def t2048(ic):
    for (x, y, c) in ((15, 15, 0xFFF1D4), (52, 15, 0xFFD27F), (15, 52, 0xFFB056)):
        ic.raised(ic.rr(x, y, 33, 33, 6), Grad(light(c, .2), c, 100), rim=.5)
    ic.raised(ic.rr(52, 52, 33, 33, 6), Grad(0xF0553A, 0xCC2E20, 100), rim=.5)
    two = [(61, 62), (63, 57.5), (68.6, 56.5), (74, 58), (76, 62.5), (73, 68), (61, 79), (77, 79)]
    ic.fill(ic.line(two, 4.6), 0xFFFFFF)


@icon('arkanoid', 0x2F2A78, 0x0F0E38)
def arkanoid(ic):
    cols = (0xF0596A, 0xF5B83D, 0x4FD08C, 0x4FB7F5)
    for r, c in enumerate(cols):
        for i in range(5):
            if r == 3 and i in (1, 2):
                continue
            ic.raised(ic.rr(15.5 + i * 14 + .5, 18 + r * 9, 13, 8, 1.6), Grad(light(c, .2), dark(c, .08), 100), sh=False, rim=.4, dk=.2, e=.5)
    ic.fill(ic.circ(58, 62, 5.2), 0xFFFFFF)
    ic.raised(ic.rr(30, 75, 40, 7.6, 3.8), Grad(0xE7ECF2, 0x98A4B3, 100), rim=.6, sh=False)
    ic.fill(ic.rr(30, 75, 6.5, 7.6, 3), 0xF0596A)
    ic.fill(ic.rr(63.5, 75, 6.5, 7.6, 3), 0xF0596A)


@icon('circuits', 0x3D6CF0, 0x12379E)
def circuits(ic):
    # une porte ET, ses fils allumes
    y1, y2 = 40, 60
    on = Grad(0xFFE27A, 0xF5B02E, 0)
    ic.raised(add(ic.line([(14, y1), (36, y1)], 5), ic.line([(14, y2), (36, y2)], 5), ic.line([(70, 50), (86, 50)], 5)), on, sh=False)
    p = [(32, 26), (50, 26)] + ic.ell_pts(50, 50, 24, 24, -90, 90, 60)[1:] + [(32, 74)]
    ic.raised(ic.soften(ic.poly(p), .9), WHITE)
    ic.fill(ic.circ(14.5, y1, 5), 0xFFE27A)
    ic.fill(ic.circ(14.5, y2, 5), 0xFFE27A)
    ic.fill(ic.circ(85.5, 50, 5), 0xFFE27A)


@icon('cosmozoom', 0x5A2AD8, 0x12083E)
def cosmozoom(ic):
    rng = np.random.RandomState(7)
    for i in range(30 if not ic.small else 7):
        x, y = rng.uniform(6, 94), rng.uniform(6, 94)
        ic.fill(ic.circ(x, y, rng.uniform(.6, 1.4)), 0xFFFFFF, rng.uniform(.5, 1))
    # la trainee, puis le bolide lance vers le haut a droite
    ic.fill(ic.arc(96, 106, 84, 182, 238, 13, 'butt'), Grad(0xFF4FB0, 0x5FF0FF, 45), .5)
    ic.fill(ic.arc(96, 106, 84, 182, 238, 6, 'butt'), Grad(0xFFC06A, 0xFFFFFF, 45), .95)
    ic.push(5, -4, 1.06, 48)
    b = [(50, 8), (56, 26), (60, 44), (79, 66), (79, 72), (60, 68), (58, 74), (42, 74), (40, 68), (21, 72), (21, 66), (40, 44), (44, 26)]
    ic.raised(ic.soften(ic.poly(b), .6), Grad(0xFFFFFF, 0xB9C8E6, 0))
    ic.fill(ic.ell(50, 34, 4.4, 11), Grad(0x6FE8FF, 0x2F6FD6, 100))
    ic.fill(ic.poly([(21, 66), (40, 44), (40, 68), (21, 72)]), Grad(0xFF5FA8, 0xD12F8E, 90))
    ic.fill(ic.poly([(79, 66), (60, 44), (60, 68), (79, 72)]), Grad(0xFF5FA8, 0xB01E7E, 90))
    ic.fill(ic.poly([(43, 74), (57, 74), (50, 88)]), Grad(0xFFE27A, 0xFF7A2C, 90))
    ic.pop()


@icon('terracube', 0x86CDF5, 0x3B86D6)
def terracube(ic):
    # un bloc d'herbe vu de trois quarts, dans le ciel
    if not ic.small:
        ic.fill(ic.ell(24, 22, 11, 4), 0xFFFFFF, .7)
        ic.fill(ic.ell(78, 15, 8, 3), 0xFFFFFF, .55)
    t, l, r, b = (50, 17), (16, 36), (84, 36), (50, 55)
    ic.raised(ic.soften(ic.poly([t, r, (84, 70), (50, 89), (16, 70), l]), .5), 0x5E4128, sh=True)
    ic.fill(ic.poly([l, b, (50, 89), (16, 70)]), Grad(0xA8764A, 0x7A5232, 90))
    ic.fill(ic.poly([b, r, (84, 70), (50, 89)]), Grad(0x8C603A, 0x5E4128, 90))
    # l'herbe qui déborde sur les côtés, en dents
    ic.fill(ic.poly([l, b, (50, 62), (44, 60), (40, 64), (33, 54), (27, 56), (22, 47), (16, 46)]), 0x58A83A)
    ic.fill(ic.poly([b, r, (84, 46), (79, 49), (74, 46), (68, 55), (62, 52), (56, 60), (50, 62)]), 0x4A9030)
    ic.fill(ic.poly([t, r, b, l]), Grad(0x9BE36A, 0x5DB040, 100))
    if not ic.small:
        for x, y, c in ((26, 62, 0x6E4A2C), (36, 72, 0x6E4A2C), (64, 70, 0x4F3520), (73, 60, 0x4F3520), (42, 80, 0x8F6440)):
            ic.fill(ic.rr(x - 2.2, y - 2.2, 4.4, 4.4, .6), c, .9)
        for x, y in ((40, 33), (55, 27), (60, 40), (47, 44)):
            ic.fill(ic.rr(x - 2, y - 1.4, 4, 2.8, .6), 0x7CC855, .9)


@icon('critters', 0x7CCBF5, 0x3C9CE0)
def critters(ic):
    ic.fill(ic.ell(32, 30, 14, 5), 0xFFFFFF, .6)
    ic.fill(ic.rr(-5, 76, 110, 30, 0), Grad(0x9A6A3F, 0x70482A, 100))
    ic.fill(ic.rr(-5, 72, 110, 8, 3), Grad(0x8FD163, 0x5CA63E, 100))
    ic.fill(ic.ell(42, 75, 7, 3.2), 0x3A2412)
    ic.fill(ic.ell(60, 75, 7, 3.2), 0x3A2412)
    ic.raised(ic.ell(51, 53, 24, 23), Grad(0xFFD56B, 0xF29A2C, 110))
    ic.fill(ic.ell(51, 62, 14, 11), 0xFFEFC2, .85)
    for x in (43, 60):
        ic.fill(ic.ell(x, 46, 7.6, 9), 0xFFFFFF)
        ic.fill(ic.circ(x + 2.2, 47, 4), 0x24160A)
    ic.fill(ic.line([(51, 31), (52, 24)], 3), 0x4F9E3C)
    ic.fill(ic.ell(60, 21, 9, 5), Grad(0x9EE36B, 0x4F9E3C, 100))
    ic.fill(ic.ell(44, 22, 6, 3.6), Grad(0x9EE36B, 0x4F9E3C, 100))


@icon('freecell', 0x2FA39C, 0x0F5F6E)
def freecell(ic):
    for i in range(4):
        ic.fill(ic.rr(16 + i * 17.8, 14, 14.4, 19, 2.6), 0x000000, .22)
    card(ic, 19, 30, 38, 53, -6, heart, 0xE03A3E)
    card(ic, 44, 30, 38, 53, 6, spade, 0x1E2632)


@icon('gamelib', 0xFF8A4C, 0xC93A2B)
def gamelib(ic):
    # des cartouches rangees
    for x, c, r in ((16, 0x4F7CF0, 0), (37.5, 0x2FB57E, 0), (59, 0xF5B83D, 0)):
        ic.raised(ic.rr(x, 22, 25, 62, 3.4), Grad(0x5A6476, 0x2F3644, 100), rim=.3)
        ic.fill(ic.rr(x + 4, 28, 17, 24, 2), Grad(light(c, .2), c, 100))
        ic.fill(ic.rr(x + 6, 62, 13, 2.2), 0xFFFFFF, .35)
        ic.fill(ic.rr(x + 6, 68, 13, 2.2), 0xFFFFFF, .35)


def handheld_buttons(ic, cx, cy, s, c):
    ic.fill(ic.circ(cx + s, cy - s * .5, s * .62), c)
    ic.fill(ic.circ(cx - s * .1, cy + s * .35, s * .62), c)


@icon('gbemu', 0xB2C24A, 0x5F7A22)
def gbemu(ic):
    ic.raised(ic.rr(25, 11, 50, 78, 6), Grad(0xEEF0EE, 0xC5CACC, 100))
    ic.fill(ic.rr(31, 17, 38, 34, 3), Grad(0x6A7180, 0x474E5C, 100))
    ic.fill(ic.rr(36, 21, 28, 26, 1.2), Grad(0xB6D36A, 0x8FB34A, 100))
    if not ic.small:
        ic.fill(ic.poly([(40, 44), (47, 33), (52, 39), (55, 35), (61, 44)]), 0x40602A)
    cross_pad(ic, 38, 66, 12, 0x2A3342)
    handheld_buttons(ic, 57, 66, 5.6, 0xC2306A)


@icon('gbaemu', 0x8B7BF0, 0x4F36C0)
def gbaemu(ic):
    m = ic.soften(add(ic.rr(13, 29, 74, 44, 15), ic.rr(9, 34, 82, 30, 12)), 1.2)
    ic.raised(m, Grad(0x7A5CD6, 0x4A2FA6, 100))
    ic.fill(ic.rr(31, 33, 38, 30, 2.5), Grad(0x2A3342, 0x161C26, 100))
    ic.fill(ic.rr(34, 36, 32, 24, 1), Grad(0xA7E6F5, 0x5FB0E8, 120))
    cross_pad(ic, 20.5, 48, 12, 0xE7E9F5)
    handheld_buttons(ic, 77, 49, 4.4, 0xFFC94D)


@icon('nesemu', 0xE04848, 0x961C2C)
def nesemu(ic):
    # manette rectangulaire
    ic.raised(ic.rr(11, 32, 78, 36, 4), Grad(0xE8E8E6, 0xBDBFC2, 100))
    ic.fill(ic.rr(16, 38, 68, 24, 2.4), Grad(0x2A2D35, 0x16181E, 100))
    cross_pad(ic, 29, 50, 16, 0xD8DADE)
    if not ic.small:
        ic.fill(ic.rr(43, 47, 5.4, 2.6, 1), 0x8F949E)
        ic.fill(ic.rr(50, 47, 5.4, 2.6, 1), 0x8F949E)
    ic.fill(ic.circ(66, 54, 5), 0xE03A3E)
    ic.fill(ic.circ(77, 54, 5), 0xE03A3E)


@icon('snesemu', 0xB6AEEC, 0x6D5FC4)
def snesemu(ic):
    # manette en os, quatre boutons
    m = ic.soften(add(ic.circ(29, 50, 19), ic.circ(71, 50, 19), ic.rr(29, 35, 42, 26, 0)), 1)
    ic.raised(m, Grad(0xE4E6EE, 0xB9BDCC, 100))
    cross_pad(ic, 29, 50, 18, 0x2A2D35)
    ic.fill(ic.circ(71, 40, 4.6), 0x3B7DE0)
    ic.fill(ic.circ(61, 50, 4.6), 0x2FB57E)
    ic.fill(ic.circ(81, 50, 4.6), 0xE03A3E)
    ic.fill(ic.circ(71, 60, 4.6), 0xF5C33D)


@icon('n64emu', 0x35B28A, 0x0E6A70)
def n64emu(ic):
    # manette a trois poignees
    m = add(ic.rr(13, 25, 74, 26, 13), ic.poly([(14, 38), (16, 66), (22, 70), (28, 66), (36, 48)]),
            ic.poly([(86, 38), (84, 66), (78, 70), (72, 66), (64, 48)]),
            ic.poly([(40, 44), (44, 80), (50, 84), (56, 80), (60, 44)]))
    ic.raised(ic.soften(m, 1.4), Grad(0x4A5163, 0x262A36, 100), rim=.3)
    cross_pad(ic, 26, 38, 15, 0xD8DADE)
    ic.fill(ic.circ(50, 52, 6), 0x9AA2B2)
    ic.fill(ic.circ(50, 52, 3.4), 0x2A2D35)
    for x, y in ((74, 28), (81, 35), (67, 35), (74, 42)):
        ic.fill(ic.circ(x, y, 3.2), 0xF5C33D)
    ic.fill(ic.circ(62, 44, 3.8), 0x3BA3E0)


@icon('ndsemu', 0xBEC8D8, 0x7486A0)
def ndsemu(ic):
    # deux ecrans l'un sur l'autre
    ic.raised(ic.rr(24, 10, 52, 39, 5), Grad(0x57607A, 0x3A4258, 100), rim=.3)
    ic.fill(ic.rr(31, 15, 38, 29, 1.6), Grad(0x8FD8F5, 0x4FA2E0, 110))
    ic.fill(ic.rr(24, 47, 52, 3, 0), 0x1E2432)
    ic.raised(ic.rr(16, 50, 68, 39, 5), Grad(0x4A5268, 0x2C3346, 100), rim=.3)
    ic.fill(ic.rr(36, 54, 28, 31, 1.6), Grad(0xFFE9B0, 0xF6B85A, 110))
    cross_pad(ic, 26, 67, 10.5, 0xD8DCE8)
    ic.fill(ic.circ(74, 64, 2.6), 0xFFC94D)
    ic.fill(ic.circ(74, 72, 2.6), 0xF0596A)


@icon('asteroids', 0x26304F, 0x0A0E1E)
def asteroids(ic):
    if not ic.small:
        for x, y, r in ((22, 20, 1.4), (78, 16, 1.1), (86, 44, 1.3), (16, 58, 1.0), (70, 84, 1.2), (40, 12, .9)):
            ic.fill(ic.circ(x, y, r), 0xDDE6FF, .8)
    rock = ic.poly([(46, 24), (60, 18), (74, 24), (82, 38), (78, 52), (66, 58), (52, 56), (42, 46), (40, 34)])
    ic.raised(rock, Grad(0xB5A898, 0x6E6255, 100))
    if not ic.small:
        ic.fill(ic.circ(58, 34, 4.5), 0x5E5348, .55)
        ic.fill(ic.circ(70, 46, 3.2), 0x5E5348, .55)
        ic.fill(ic.circ(66, 30, 2.4), 0x5E5348, .5)
    ic.push(0, 0, 1, 40, (34, 70))
    ic.fill(ic.poly([(29.5, 80), (34, 93), (38.5, 80)]), Grad(0xFFE07A, 0xFF7A2E, 100))
    hull = [(34, 50), (46, 84), (34, 77), (22, 84), (34, 50)]
    ic.shadow(ic.line(hull, 4.2), .1, 2.5, .8, dx=0, color=0x6FD3FF)
    ic.fill(ic.line(hull, 4.2), 0xF2F7FF)
    ic.pop()


@icon('relics', 0x3E9A5C, 0x17482E)
def relics(ic):
    # la relique de Relics : une statuette d'or sur son socle, sous l'arche d'un temple
    if not ic.small:
        arch = sub(add(ic.rr(20, 30, 60, 58, 3), ic.circ(50, 32, 30)), add(ic.rr(30, 36, 40, 54, 1), ic.circ(50, 38, 20)))
        ic.fill(arch, Grad(0x8FA67E, 0x56704E, 100), .55)
    ic.raised(ic.rr(30, 72, 40, 12, 3), Grad(0xC9B48A, 0x806A48, 100))
    gold = Grad(0xFFE88E, 0xC9801A, 100)
    body = add(ic.rr(37, 44, 26, 30, 8), ic.circ(50, 35, 12.5), ic.poly([(36, 30), (50, 13), (64, 30)]))
    ic.raised(body, gold)
    ic.fill(ic.line([(38, 50), (30, 60)], 6), gold)
    ic.fill(ic.line([(62, 50), (70, 60)], 6), gold)
    if not ic.small:
        ic.fill(ic.circ(45, 35, 2.6), 0x1FA2DE)
        ic.fill(ic.circ(55, 35, 2.6), 0x1FA2DE)
        ic.fill(ic.circ(50, 57, 4.2), 0xD8283C)
        ic.fill(ic.circ(42, 24, 2.2), 0xFFFFFF, .8)


@icon('morpho', 0x8E6CF0, 0x4A2FA8)
def morpho(ic):
    # la créature de Morpho : une boule ronde aux grands yeux, deux petites cornes, sortie de sa coquille
    body = Grad(0x9BE36A, 0x3E9E45, 100)
    if not ic.small:
        for x, y, r in ((24, 22, 1.6), (78, 18, 1.3), (84, 40, 1.1), (18, 46, 1.0)):
            ic.fill(ic.circ(x, y, r), 0xFFF2B0, .9)
        ic.fill(ic.line([(36, 18), (42, 30)], 7), Grad(0xFFF1C8, 0xE6B85A, 100))
        ic.fill(ic.line([(64, 18), (58, 30)], 7), Grad(0xFFF1C8, 0xE6B85A, 100))
    ic.raised(add(ic.ell(50, 54, 30, 27), ic.ell(50, 74, 24, 12)), body)
    # la coquille, en bas
    shell = sub(ic.ell(50, 80, 32, 14), ic.poly([(14, 66), (26, 74), (34, 66), (44, 75), (54, 66), (64, 75), (74, 66), (86, 74), (86, 60), (14, 60)]))
    ic.raised(shell, Grad(0xFFFDF6, 0xE4DCCB, 100))
    if not ic.small:
        ic.fill(ic.circ(36, 84, 2.8), 0xF07AA8)
        ic.fill(ic.circ(64, 85, 2.4), 0x5FB6F0)
    for x in (39, 61):
        ic.fill(ic.circ(x, 50, 10.5), 0xFFFFFF)
        ic.fill(ic.circ(x + 1.2, 51.5, 6.4), 0x3A2A6A)
        ic.fill(ic.circ(x - 1.6, 47.8, 2.4), 0xFFFFFF)
    if not ic.small:
        ic.fill(ic.circ(29, 62, 4), 0xFF8FA8, .55)
        ic.fill(ic.circ(71, 62, 4), 0xFF8FA8, .55)
        ic.fill(ic.arc(50, 61, 6, 20, 160, 2.4), 0x2E4A22)


@icon('parkingjam', 0x6F7680, 0x40464F)
def parkingjam(ic):
    # le parking vu de haut : des places, trois voitures, une qui sort
    for i in range(4):
        ic.fill(ic.rr(16 + i * 21, 14, 2.6, 30, 1.2), 0xFFFFFF, .45)
        ic.fill(ic.rr(16 + i * 21, 58, 2.6, 30, 1.2), 0xFFFFFF, .45)

    def car(x, y, w, h, c):
        body = ic.rr(x, y, w, h, 5.5)
        ic.raised(body, Grad(light(c, .15), dark(c, .12), 100))
        if not ic.small:
            ic.fill(ic.rr(x + 3, y + h * .22, w - 6, h * .2, 2.4), 0xBFE3FF, .9)
            ic.fill(ic.rr(x + 3, y + h * .64, w - 6, h * .13, 2.4), 0xBFE3FF, .75)
    car(21, 16, 14, 27, 0xE8452C)
    car(42, 60, 14, 27, 0x2F8FE8)
    ic.push(0, 0, 1, 90, (70, 51))
    car(63, 36, 14, 30, 0xF5B83D)
    ic.pop()


@icon('invaders', 0x1A2150, 0x070A22)
def invaders(ic):
    pat = ['..X.....X..', '...X...X...', '..XXXXXXX..', '.XX.XXX.XX.', 'XXXXXXXXXXX', 'X.XXXXXXX.X', 'X.X.....X.X', '...XX.XX...']
    s = 6.2
    x0 = 50 - 5.5 * s
    y0 = 25
    m = ic.many([ic.rr_pts(x0 + x * s - .05, y0 + y * s - .05, s + .1, s + .1, 0) for y, r in enumerate(pat) for x, v in enumerate(r) if v == 'X'])
    ic.shadow(m, .1, 3, .8, dx=0, color=0x4DFF8F)
    ic.fill(m, Grad(0xA9FFC2, 0x34E07F, 100))
    if not ic.small:
        ic.fill(ic.rr(47.6, 78, 4.8, 8, 2), 0xFFD84D)


@icon('pinball', 0xEC4FD0, 0x6425C4)
def pinball(ic):
    for x, y, r, c in ((35, 32, 10.5, 0xFFD84D), (66, 28, 8.5, 0x6FE0FF), (55, 49, 9, 0xFF7A85)):
        ic.raised(ic.circ(x, y, r), Grad(light(c, .3), dark(c, .1), 100))
        ic.fill(ic.circ(x, y, r * .45), 0xFFFFFF, .8)
    ic.raised(ic.line([(17, 67), (43, 81)], 7), WHITE)
    ic.raised(ic.line([(83, 67), (57, 81)], 7), WHITE)
    b = ic.circ(36, 58, 8)
    ic.shadow(b)
    ic.fill(b, Rad(0xFFFFFF, 0x7B8798, .34, .3, .8))


@icon('pipes', 0x24406E, 0x0E1C3C)
def pipes(ic):
    c = 0xE8924A
    p = [(-4, 34), (36, 34), (36, 66), (64, 66), (64, 34), (104, 34)]
    w = 21
    ic.shadow(ic.line(p, w, 'butt'), 1.6, 1.8, .45)
    for k, col in ((1, dark(c, .42)), (.78, dark(c, .1)), (.52, c), (.26, light(c, .4))):
        ic.push(-w * .12 * (1 - k), -w * .16 * (1 - k))
        ic.fill(ic.line(p, w * k, 'butt'), col)
        ic.pop()
    for x, y, v in ((22, 34, 1), (50, 66, 0), (78, 34, 1)):
        q = ic.rr(x - 3.6, y - w * .62, 7.2, w * 1.24, 1.8) if v else ic.rr(x - 3.6, y - w * .62, 7.2, w * 1.24, 1.8)
        ic.raised(q, Grad(0xFFF1D0, 0xC98A3C, 100), sh=False, rim=.5, dk=.3, e=.5)


@icon('same', 0x7C4FE0, 0x34208E)
def same(ic):
    g = ['RRBY', 'GRBY', 'GGPP', 'YGPB']
    cc = {'R': 0xF0475C, 'B': 0x3B8DF0, 'Y': 0xFFC93D, 'G': 0x3CCB7F, 'P': 0xF06AD0}
    s = 18.2
    for y, r in enumerate(g):
        for x, v in enumerate(r):
            c = cc[v]
            cx, cy = 22.7 + x * s, 22.7 + y * s
            m = ic.circ(cx, cy, 8.4)
            ic.fill(m, Rad(light(c, .55), dark(c, .12), .34, .28, .9))


@icon('bubbles', 0x3FA0F0, 0x1C3A8C)
def bubbles(ic):
    # une grappe accrochee en haut, une bulle qui monte, ses pointilles
    cc = {'R': 0xF04860, 'B': 0x3C8CF0, 'Y': 0xFFC83C, 'G': 0x3CCC7C, 'P': 0xA860F0}
    r, s = 8.6, 18.4
    rows = [(27, 22.4, 'RRBY'), (43, 31.6, 'GBY')]
    for y, x0, cols in rows:
        for i, v in enumerate(cols):
            c = cc[v]
            m = ic.circ(x0 + i * s, y, r)
            ic.shadow(m, 1, 1.2, .25)
            ic.fill(m, Rad(light(c, .55), dark(c, .12), .34, .28, .9))
    if not ic.small:
        for x, y, k in ((54, 91, 2.2), (51, 84, 2.5), (48, 77, 2.8)):
            ic.fill(ic.circ(x, y, k), 0xFFFFFF, .85)
    c = cc['G']
    m = ic.circ(44, 65, 9.4)
    ic.shadow(m)
    ic.fill(m, Rad(light(c, .55), dark(c, .12), .34, .28, .9))
    ic.fill(ic.circ(41, 62, 2.6), 0xFFFFFF, .7)


@icon('snake', 0x53C46B, 0x167A4C)
def snake(ic):
    pts = []
    for i in range(121):
        t = i / 120
        pts.append((24 + 56 * t, 52 + 21 * math.sin(t * math.pi * 2.0 + .25) * (1 - .15 * t)))
    c = Grad(0xFFF3A8, 0xF5B83D, 100)
    ic.shadow(ic.line(pts, 15))
    ic.fill(ic.line(pts, 15), c)
    if not ic.small:
        for k in range(8, 118, 14):
            x, y = pts[k]
            ic.fill(ic.circ(x, y, 3.6), 0xD9822B, .85)
    hx, hy = pts[0]
    ic.raised(ic.ell(hx, hy, 12.5, 11.5), c, sh=False)
    ic.fill(ic.circ(hx + 2, hy - 4.5, 3.6), 0xFFFFFF)
    ic.fill(ic.circ(hx + 2.8, hy - 4.5, 2), 0x1E2632)
    ic.fill(ic.line([(hx - 11, hy + 2), (hx - 17, hy + 3), (hx - 20, hy + 1)], 2.4), 0xE8344A)


@icon('solitaire', 0x3DB86A, 0x0D6E43)
def solitaire(ic):
    card(ic, 19, 22, 40, 56, -16, diamond, 0xE03A3E)
    card(ic, 33, 21, 40, 56, 4, spade, 0x1E2632)


@icon('targets', 0x35C4C0, 0x0E7890, free=True)
def targets(ic):
    """La cible en objet : ses anneaux, son épaisseur, une fléchette plantée."""
    obj(ic, ic.circ(48, 50, 40), 0xFFFFFF, 0x9A9996, 5)
    for r, c in ((32, 0xE01B24), (24, 0xFFFFFF), (16, 0xE01B24), (8, 0xFFFFFF)):
        ic.fill(ic.circ(48, 50, r), c)
    ic.shadow(ic.line([(48, 50), (84, 14)], 3), dy=1.0, blur=.6, op=.25, dx=0)
    ic.fill(ic.line([(48, 50), (82, 16)], 3.2), 0xF5C211)
    ic.fill(ic.poly([(80, 10), (91, 10), (89, 19), (78, 21)]), 0x3584E4)


@icon('tetris', 0x2B2F52, 0x12142E)
def tetris(ic):
    s = 15
    x0, y0 = 20, 19
    t = {(0, 3): 0xF5C33D, (1, 3): 0xF5C33D, (0, 2): 0xF5C33D, (1, 2): 0xF5C33D,
         (2, 3): 0xE8344A, (3, 3): 0xE8344A, (3, 2): 0xE8344A, (3, 1): 0xE8344A,
         (2, 2): 0x4FB7F5, (2, 1): 0x4FB7F5, (2, 0): 0x4FB7F5,
         (1, 1): 0x3CCB7F, (0, 1): 0x3CCB7F, (1, 0): 0x3CCB7F}
    for (x, y), c in t.items():
        block(ic, x0 + x * s, y0 + y * s, s - .6, c)


# --- paquets (la liste de Logiciels : ils n'ont pas de dossier d'application) ---------------------------------------
def _gem(ic):
    welcome(ic)


def _folder_of(ic, sym):
    back = add(ic.rr(15, 25, 32, 16, 5), ic.rr(15, 31, 70, 44, 5))
    ic.raised(back, Grad(0xE2E8F0, 0xB4BFCD, 100), rim=.3)
    front = ic.soften(ic.poly([(14, 40), (86, 40), (84, 77), (16, 77)]), 1.6)
    ic.raised(front, Grad(0xFFFFFF, 0xDDE4EE, 100), rim=.6)
    sym(ic)


def _note_sym(ic):
    ic.fill(add(ic.ell(44, 66, 7, 5.4), ic.ell(62, 62, 7, 5.4), ic.poly([(49, 66), (49, 47), (68, 42), (68, 62), (65, 62), (65, 48), (52, 51.6), (52, 66)])), 0xD24BE8)


def _code_sym(ic):
    ic.fill(add(ic.line([(42, 51), (34, 58), (42, 65)], 3.6), ic.line([(58, 51), (66, 58), (58, 65)], 3.6)), 0x2E6FE0)


def _doc_sym(ic):
    ic.fill(add(ic.rr(34, 50, 32, 3.8, 1.9), ic.rr(34, 58, 32, 3.8, 1.9), ic.rr(34, 66, 20, 3.8, 1.9)), 0x5B6A80)


@icon('armpi', 0x4A5468, 0x1E2330)
def armpi(ic):
    _gem(ic)


@icon('pi-firmware', 0x2FA36B, 0x0E5A48)
def pi_firmware(ic):
    for i in range(5):
        c = 22 + i * 14
        ic.fill(ic.rr(c - 2.2, 13, 4.4, 74, 1), Grad(0xFFE9A0, 0xD9A02C, 90))
        ic.fill(ic.rr(13, c - 2.2, 74, 4.4, 1), Grad(0xFFE9A0, 0xD9A02C, 0))
    ic.raised(ic.rr(21, 21, 58, 58, 6), Grad(0x3A4252, 0x1A1E28, 100), rim=.3)
    ic.fill(ic.rr(33, 33, 34, 34, 3), Grad(0x566076, 0x3A4252, 100))
    ic.fill(ic.circ(29, 29, 2.6), 0xC9D2DE)


@icon('soundfonts', 0x38C7C0, 0x7B4DF0)
def soundfonts(ic):
    p = [(16 + i * 68 / 80, 50 - 26 * math.sin(i / 80 * math.pi * 3) * math.sin(i / 80 * math.pi)) for i in range(81)]
    ic.raised(ic.line(p, 7), WHITE)


@icon('korin-srcs', 0x56657C, 0x263044)
def armpi_src(ic):
    ic.raised(add(ic.line([(36, 32), (17, 50), (36, 68)], 8), ic.line([(64, 32), (83, 50), (64, 68)], 8)), WHITE)
    ic.raised(ic.line([(56, 26), (44, 74)], 7), Grad(0xFFE08A, 0xF6B73C, 100))


@icon('gcc', 0x4C6AD8, 0x1E2F8E)
def gcc(ic):
    g = sub(ic.gear(50, 50, 37, 28, 10), ic.circ(50, 50, 22))
    ic.raised(g, Grad(0xFFE9B0, 0xF0B040, 110), rim=.5, dk=.2)
    ic.raised(ic.arc(50, 50, 11.5, 45, 315, 6.6), WHITE)


@icon('basic-samples', 0x2E5BE8, 0x0B1E96)
def basic_samples(ic):
    _folder_of(ic, _code_sym)


@icon('fmtracker-samples', 0x2C9DB0, 0x115F7A)
def fmtracker_samples(ic):
    _folder_of(ic, _note_sym)


@icon('koton-samples', 0xD24BE8, 0x4B23B8)
def koton_samples(ic):
    _folder_of(ic, _note_sym)


@icon('home-samples', 0x3CC1F2, 0x0C79C8)
def home_samples(ic):
    _folder_of(ic, _doc_sym)


def _flask(ic, c):
    m = ic.soften(ic.poly([(41, 15), (59, 15), (59, 40), (80, 78), (76, 85), (24, 85), (20, 78), (41, 40)]), 1.2)
    ic.raised(m, Grad(0xFFFFFF, 0xDDE4EE, 100))
    ic.fill(inter(m, ic.poly([(0, 58), (100, 58), (100, 100), (0, 100)])), Grad(light(c, .25), c, 100))
    ic.raised(ic.rr(36, 12, 28, 7, 3), Grad(0xFFFFFF, 0xC9D2DE, 100), sh=False)
    if not ic.small:
        ic.fill(ic.circ(45, 70, 3.2), 0xFFFFFF, .7)
        ic.fill(ic.circ(56, 76, 2.2), 0xFFFFFF, .7)


@icon('webkit-tests', 0x39B8FF, 0x0E58D2)
def webkit_tests(ic):
    _flask(ic, 0x2E8BF0)


@icon('system-tests', 0x7E8A9C, 0x434C5C)
def system_tests(ic):
    _flask(ic, 0x35C088)


# --- glyphes des boutons systeme ------------------------------------------------------------------------------------
# Pas de tuile : une silhouette blanche sur toute la case, le kit n'en garde que l'alpha et la colorie au dessin avec
# un jeton du theme (ui_glyph). ic.small : tailles <= GLYPH_SMALL_MAX, traits plus gras.
def glyph(name):
    def deco(f):
        GLYPHS[name] = f
        return f
    return deco


# Les quatre boutons de droite du panel (et leurs reprises : dock, accueil, connexion) : un jeu au trait, sur la grille
# de 24 des glyphes d'outils, du meme trait (strokes : 1,5 px environ a 18 px), coins et bouts ronds (17/11/2026).
def _w(ic):
    return 8.6 if ic.small else 7


@glyph('power')
def g_power(ic):
    u = 100 / 24
    w = _w(ic)
    ic.fill(add(ic.arc(12 * u, 13 * u, 8 * u, -55, 235, w), ic.line([(12 * u, 3 * u), (12 * u, 11 * u)], w)))


@glyph('gear')
def g_gear(ic):
    # le contour d une roue a six dents larges et arrondies, et son moyeu : au trait
    u = 100 / 24
    w = _w(ic)
    cx = cy = 12 * u
    r, rb, teeth = 10 * u, 7.4 * u, 6
    top, bot = .36, .60
    p = []
    st = 360 / teeth
    for i in range(teeth):
        a = i * st - 90 + st / 2
        for da, q in ((-bot / 2, rb), (-top / 2, r), (top / 2, r), (bot / 2, rb)):
            t = math.radians(a + da * st)
            p.append((cx + q * math.cos(t), cy + q * math.sin(t)))
        for k in range(1, 6):
            t = math.radians(a + bot / 2 * st + (1 - bot) * st * k / 6)
            p.append((cx + rb * math.cos(t), cy + rb * math.sin(t)))
    p.append(p[0])
    ic.fill(add(ic.line(p, w), ic.ring(cx, cy, 3 * u, w)))


@glyph('lock')
def g_lock(ic):
    w = 13 if ic.small else 11
    arc = ic.arc(50, 30, 21, 180, 360, w, 'butt')
    legs = add(ic.rr(29 - w / 2, 30, w, 20, 0), ic.rr(71 - w / 2, 30, w, 20, 0))
    body = ic.rr(14, 42, 72, 53, 12)
    hole = add(ic.circ(50, 63, 8), ic.rr(46, 63, 8, 16, 2))
    ic.fill(add(arc, legs, sub(body, hole)))


@glyph('apps')
def g_apps(ic):
    a = 25
    for y in range(3):
        for x in range(3):
            ic.fill(ic.rr(1.5 + x * 36, 1.5 + y * 36, a, a, 7.5))


@glyph('pill')
def g_pill(ic):
    ic.fill(ic.poly(ic.tile_pts()))


@glyph('clipboard')
def g_clipboard(ic):
    # la planche (son bord du haut s'efface sous la pince), la pince, deux lignes de texte
    u = 100 / 24
    w = _w(ic)

    def outline(x, y, l, h, r):
        return sub(ic.rr((x - w / 2 / u) * u, (y - w / 2 / u) * u, l * u + w, h * u + w, r * u + w / 2),
                   ic.rr((x + w / 2 / u) * u, (y + w / 2 / u) * u, l * u - w, h * u - w, max(0, r * u - w / 2)))
    board = sub(outline(5, 4.5, 14, 17, 2.5), ic.rr(8 * u - w, 2 * u, 8 * u + 2 * w, 5.5 * u))
    clip = outline(8.5, 2.5, 7, 4, 1.5)
    lines = add(ic.line([(9 * u, 12 * u), (15 * u, 12 * u)], w), ic.line([(9 * u, 16 * u), (13 * u, 16 * u)], w))
    ic.fill(add(board, clip, lines))

# --- les glyphes d'outils : barres d'outils de toutes les apps (table uk_tool_map de uikit_tabs.inc), au trait, sur une
# grille de 24 comme le reste : l ligne, L ligne fermee, r contour arrondi x y l h rayon, c cercle, C disque, a arc
TOOLS = {
    'new': [('L', (6, 3), (14, 3), (19, 8), (19, 21), (6, 21)), ('l', (14, 3), (14, 8), (19, 8)), ('l', (12.5, 11), (12.5, 17)), ('l', (9.5, 14), (15.5, 14))],
    'open': [('l', (3, 18.5), (3, 6.5), (9.5, 6.5), (11.5, 9), (19, 9), (19, 11.5)), ('L', (3, 18.5), (6.5, 11.5), (21.5, 11.5), (18, 18.5))],
    'save': [('L', (4, 4), (16, 4), (20, 8), (20, 20), (4, 20)), ('l', (8, 20), (8, 13.5), (16, 13.5), (16, 20)), ('l', (8, 4), (8, 8.5), (14, 8.5))],
    'undo': [('l', (9, 4), (4, 9), (9, 14)), ('l', (4, 9), (15, 9), (20, 14), (15, 19), (9, 19))],
    'redo': [('l', (15, 4), (20, 9), (15, 14)), ('l', (20, 9), (9, 9), (4, 14), (9, 19), (15, 19))],
    'copy': [('r', 8.5, 8.5, 12, 12, 2), ('l', (15.5, 4.5), (5.5, 4.5), (3.5, 6.5), (3.5, 15.5))],
    'cut': [('c', 6.5, 18, 2.8), ('c', 17.5, 18, 2.8), ('l', (8, 15.5), (18, 3.5)), ('l', (16, 15.5), (6, 3.5))],
    'paste': [('r', 5, 4.5, 14, 16.5, 2), ('r', 9, 3, 6, 3.5, 1), ('l', (9, 12), (15, 12)), ('l', (9, 16), (15, 16))],
    'find': [('c', 10.5, 10.5, 6.5), ('l', (15.5, 15.5), (21, 21))],
    'table': [('r', 3.5, 4.5, 17, 15, 1.5), ('l', (3.5, 9.5), (20.5, 9.5)), ('l', (3.5, 14.5), (20.5, 14.5)), ('l', (12, 4.5), (12, 19.5))],
    'image': [('r', 3.5, 4.5, 17, 15, 2), ('C', 8.5, 9.5, 1.4), ('l', (3.5, 16.5), (9, 11.5), (14, 16), (17, 13.5), (20.5, 16.5))],
    'minus': [('l', (5, 12), (19, 12))],
    'plus': [('l', (12, 5), (12, 19)), ('l', (5, 12), (19, 12))],
    'cross': [('l', (6, 6), (18, 18)), ('l', (18, 6), (6, 18))],
    'check': [('l', (5, 12.5), (10, 17.5), (19, 7))],
    'alignl': [('l', (4, 6), (20, 6)), ('l', (4, 10.5), (14, 10.5)), ('l', (4, 15), (20, 15)), ('l', (4, 19.5), (14, 19.5))],
    'alignc': [('l', (4, 6), (20, 6)), ('l', (7, 10.5), (17, 10.5)), ('l', (4, 15), (20, 15)), ('l', (7, 19.5), (17, 19.5))],
    'alignr': [('l', (4, 6), (20, 6)), ('l', (10, 10.5), (20, 10.5)), ('l', (4, 15), (20, 15)), ('l', (10, 19.5), (20, 19.5))],
    'alignj': [('l', (4, 6), (20, 6)), ('l', (4, 10.5), (20, 10.5)), ('l', (4, 15), (20, 15)), ('l', (4, 19.5), (20, 19.5))],
    'bullets': [('l', (9, 6), (21, 6)), ('l', (9, 12), (21, 12)), ('l', (9, 18), (21, 18)), ('C', 4.5, 6, 1.5), ('C', 4.5, 12, 1.5), ('C', 4.5, 18, 1.5)],
    'pagebreak': [('l', (5, 3), (5, 8), (19, 8), (19, 3)), ('l', (5, 21), (5, 16), (19, 16), (19, 21)), ('l', (3, 12), (6, 12)), ('l', (9, 12), (15, 12)), ('l', (18, 12), (21, 12))],
    'up': [('l', (12, 20), (12, 4)), ('l', (6, 10), (12, 4), (18, 10))],
    'down': [('l', (12, 4), (12, 20)), ('l', (6, 14), (12, 20), (18, 14))],
    'left': [('l', (20, 12), (4, 12)), ('l', (10, 6), (4, 12), (10, 18))],
    'right': [('l', (4, 12), (20, 12)), ('l', (14, 6), (20, 12), (14, 18))],
    # l'écran de connexion (docs/DESIGN-CONNEXION.md) : montrer le mot de passe, clavier, accessibilité, réseau, compte
    'eye': [('L', (2, 12), (6, 7.5), (12, 5.5), (18, 7.5), (22, 12), (18, 16.5), (12, 18.5), (6, 16.5)), ('c', 12, 12, 3)],
    'keyboard': [('r', 2.5, 6, 19, 12, 2), ('l', (6, 10), (7, 10)), ('l', (10, 10), (11, 10)), ('l', (14, 10), (15, 10)),
                 ('l', (18, 10), (18.5, 10)), ('l', (8, 14), (16, 14))],
    'access': [('c', 12, 12, 9.5), ('C', 12, 7.5, 1.5), ('l', (7.5, 10.5), (16.5, 10.5)), ('l', (12, 10.5), (12, 14)),
               ('l', (9.5, 18), (12, 14), (14.5, 18))],
    'network': [('r', 9, 3, 6, 5, 1), ('r', 3, 16, 6, 5, 1), ('r', 15, 16, 6, 5, 1), ('l', (12, 8), (12, 12)),
                ('l', (6, 16), (6, 12), (18, 12), (18, 16))],
    'person': [('c', 12, 8, 4), ('l', (4.5, 21), (5.5, 17.5), (9, 15), (15, 15), (18.5, 17.5), (19.5, 21))],
    'arrow': [('l', (5, 19), (19, 5)), ('l', (9, 5), (19, 5), (19, 15))],
    'filter': [('L', (3.5, 5), (20.5, 5), (14, 13), (14, 20), (10, 18), (10, 13))],
    'chart': [('c', 12, 12, 9), ('l', (12, 3), (12, 12), (20, 15))],
    'wrap': [('l', (3, 6), (21, 6)), ('l', (3, 12), (18, 12), (20.5, 14.5), (18, 17), (11, 17)), ('l', (14, 14), (11, 17), (14, 20)), ('l', (3, 18), (7, 18))],
    'merge': [('r', 3, 5, 18, 14, 1.5), ('l', (6.5, 12), (10.5, 12)), ('l', (8.5, 9.5), (11, 12), (8.5, 14.5)), ('l', (17.5, 12), (13.5, 12)), ('l', (15.5, 9.5), (13, 12), (15.5, 14.5))],
    'freeze': [('r', 3.5, 4, 17, 16, 1.5), ('l', (3.5, 9), (20.5, 9)), ('l', (9, 4), (9, 20))],
    'layout': [('r', 3.5, 4.5, 17, 15, 1.5), ('l', (9.5, 4.5), (9.5, 19.5)), ('l', (9.5, 11), (20.5, 11))],
    'shape': [('L', (12, 3.5), (20.5, 12), (12, 20.5), (3.5, 12))],
    'spacing': [('l', (10, 6), (21, 6)), ('l', (10, 12), (21, 12)), ('l', (10, 18), (21, 18)), ('l', (5, 4), (5, 20)), ('l', (2.5, 6.5), (5, 4), (7.5, 6.5)), ('l', (2.5, 17.5), (5, 20), (7.5, 17.5))],
    'front': [('r', 3.5, 3.5, 12, 12, 2), ('l', (20.5, 9), (20.5, 20.5), (9, 20.5))],
    'back': [('r', 8.5, 8.5, 12, 12, 2), ('l', (3.5, 15), (3.5, 3.5), (15, 3.5))],
    'play': [('L', (7, 4.5), (19.5, 12), (7, 19.5))],
    'trash': [('l', (4, 7), (20, 7)), ('l', (9, 7), (9, 4), (15, 4), (15, 7)), ('L', (6, 7), (7, 21), (17, 21), (18, 7)), ('l', (10, 11), (10, 17)), ('l', (14, 11), (14, 17))],
}


def strokes(ic, spec):
    w = 8.6 if ic.small else 7
    u = 100 / 24
    m = None
    for s in spec:
        k = s[0]
        if k in 'lL':
            p = [(x * u, y * u) for x, y in s[1:]]
            if k == 'L':
                p.append(p[0])
            n = ic.line(p, w)
        elif k == 'c':
            n = ic.ring(s[1] * u, s[2] * u, s[3] * u, w)
        elif k == 'C':
            n = ic.circ(s[1] * u, s[2] * u, s[3] * u)
        else:
            x, y, l, h, r = [v * u for v in s[1:]]
            n = sub(ic.rr(x - w / 2, y - w / 2, l + w, h + w, r + w / 2), ic.rr(x + w / 2, y + w / 2, l - w, h - w, max(0, r - w / 2)))
        m = n if m is None else add(m, n)
    ic.fill(m)


def _tool(name, spec):
    GLYPHS['tb-' + name] = lambda ic: strokes(ic, spec)


glyph('trash')(lambda ic: strokes(ic, [('l', (3.5, 6.5), (20.5, 6.5)), ('l', (9, 6.5), (9, 4.5), (10, 3.5), (14, 3.5), (15, 4.5), (15, 6.5)),
                                       ('l', (5.5, 6.5), (6.5, 19), (8, 20.5), (16, 20.5), (17.5, 19), (18.5, 6.5)),
                                       ('l', (10, 10.5), (10, 16.5)), ('l', (14, 10.5), (14, 16.5))]))

for _n, _s in TOOLS.items():
    _tool(_n, _s)
