# art.py -- les dessins des icones d'ARMPI. Repere 0..100 sur la tuile ; le symbole tient dans 14..86.
#
# @icon(nom, haut, bas) : couleurs de la tuile. Palette par famille, voisines differentes dans la liste des
# applications triee : systeme en ardoises, internet en bleus et sarcelles, bureautique franche, creation en
# magentas et violets, jeux vifs chacun le sien. Aucun logo de marque, aucun texte hors du « 2 » de 2048 et du « # »
# d'IRC, qui sont le sens meme de ces programmes.
import math
import numpy as np
from PIL import Image
from iconlib import Ic, Grad, Rad, tone, mix, light, dark, sub, add, inter, rgb, M

ICONS = {}
GLYPHS = {}

WHITE = Grad(0xFFFFFF, 0xDDE4EE, 100)
PAPER = Grad(0xFFFFFF, 0xE9EDF3, 100)
INK = 0x2A3342


def icon(name, top, bottom):
    def deco(f):
        ICONS[name] = (top, bottom, f)
        return f
    return deco


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
@icon('control', 0x7E8A9C, 0x434C5C)
def control(ic):
    g = sub(ic.gear(50, 50, 35, 26, 9), ic.circ(50, 50, 11.5))
    ic.raised(g, Grad(0xFFFFFF, 0xC9D1DC, 110), rim=.5, dk=.2)
    ic.fill(ic.ring(50, 50, 12.2, 1.6), 0x000000, .16)


@icon('files', 0x3CC1F2, 0x0C79C8)
def files(ic):
    back = add(ic.rr(15, 25, 32, 16, 5), ic.rr(15, 31, 70, 44, 5))
    ic.raised(back, Grad(0xF6B73C, 0xE08F1B, 100), rim=.3)
    if not ic.small:
        ic.raised(ic.rr(22, 36, 56, 26, 2), PAPER, rim=0, dk=0)
    front = ic.soften(ic.poly([(14, 44), (86, 44), (84, 77), (16, 77)]), 1.6)
    ic.raised(front, Grad(0xFFD768, 0xF5A72C, 100), rim=.6)


@icon('terminal', 0x3A404B, 0x14171D)
def terminal(ic):
    c = Grad(0x9DF7B8, 0x4CD988, 100)
    ic.shadow(ic.line([(24, 34), (43, 50), (24, 66)], 8), op=.4)
    ic.fill(ic.line([(24, 34), (43, 50), (24, 66)], 8), c)
    ic.fill(ic.rr(50, 62, 27, 8, 2.4), c)


@icon('taskman', 0x25B089, 0x0B6657)
def taskman(ic):
    f = screen(ic, 14, 21, 72, 58, tone(0x123B3A, .08, .1), Grad(0x0D2B2C, 0x0A2022, 90), 7, 2)
    if not ic.small:
        for i in range(1, 4):
            ic.fill(ic.rr(16, 23 + i * 13.5, 68, .9), 0x7FE6C0, .16)
        for i in range(1, 5):
            ic.fill(ic.rr(16 + i * 13.6, 23, .9, 54), 0x7FE6C0, .16)
    p = [(16, 54), (34, 54), (41, 34), (50, 69), (58, 44), (63, 54), (84, 54)]
    ic.fill(ic.line(p, 9), 0x53F0B0, .16)
    ic.fill(ic.line(p, 4.6), Grad(0xC9FFE7, 0x66F2B8, 100))


@icon('disks', 0x66748A, 0x323A49)
def disks(ic):
    ic.raised(ic.rr(16, 16, 68, 68, 8), tone(0xC8D0DB, .3, .1))
    d = ic.circ(47, 47, 26)
    ic.fill(d, Rad(0xF4F7FB, 0xA6B3C4, .36, .3, .95))
    if not ic.small:
        for r in (11, 15, 19, 23):
            ic.fill(ic.ring(47, 47, r, .5), 0x6B7A8E, .35)
    ic.fill(ic.circ(47, 47, 6.5), Grad(0x6A7789, 0x3A4556, 100))
    ic.raised(ic.line([(76, 72), (70, 74), (52, 56)], 4.2), Grad(0xF5B342, 0xD98A1B, 100), rim=.3)
    ic.fill(ic.circ(75, 72, 5), Grad(0xFFD066, 0xD98A1B, 100))


@icon('pkgman', 0x7A69F0, 0x3E2DB5)
def pkgman(ic):
    top = [(50, 16), (83, 32), (50, 48), (17, 32)]
    left = [(17, 32), (50, 48), (50, 86), (17, 69)]
    right = [(83, 32), (50, 48), (50, 86), (83, 69)]
    ic.shadow(ic.poly([(17, 32), (50, 16), (83, 32), (83, 69), (50, 86), (17, 69)]))
    ic.fill(ic.poly(left), Grad(0xF1C78B, 0xCF9150, 80))
    ic.fill(ic.poly(right), Grad(0xD9A266, 0xA96C35, 100))
    ic.fill(ic.poly(top), Grad(0xFFE3B3, 0xF2C98F, 90))
    ic.fill(ic.poly([(31, 25), (64, 41), (64, 47), (72, 43.5), (72, 37), (39, 21)]), 0xFFF3DD, .95)
    if not ic.small:
        ic.fill(ic.line([(50, 48), (50, 86)], .9), 0x7A4B20, .35)


@icon('welcome', 0x6371F2, 0x3323A8)
def welcome(ic):
    # une pierre taillee, facettes en trois tons : le losange d'ARMPI
    tl, tr, l, r = (34, 28), (66, 28), (17, 44), (83, 44)
    b = (50, 82)
    ic.shadow(ic.poly([tl, tr, r, b, l]), op=.4)
    a, c = (37, 44), (63, 44)
    t = (50, 28)
    ic.fill(ic.poly([tl, t, a]), 0xE9F1FF)
    ic.fill(ic.poly([t, tr, c]), 0xC9DBFF)
    ic.fill(ic.poly([t, a, c]), 0xFFFFFF)
    ic.fill(ic.poly([l, tl, a]), 0xB8CFFF)
    ic.fill(ic.poly([r, tr, c]), 0x9EB9F9)
    ic.fill(ic.poly([l, a, b]), Grad(0xCADBFF, 0x8FB0F7, 90))
    ic.fill(ic.poly([a, c, b]), Grad(0xFFFFFF, 0xB6CCFD, 90))
    ic.fill(ic.poly([c, r, b]), Grad(0xA9C2FB, 0x6E90EA, 90))
    if not ic.small:
        ic.fill(ic.star(79, 22, 7, 1.3, 4), 0xFFFFFF, .95)


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


@icon('corbeille', 0x8B97A6, 0x4C5766)
def corbeille(ic):
    body = ic.soften(ic.poly([(26, 34), (74, 34), (69, 84), (31, 84)]), 1.4)
    ic.raised(body, Grad(0xFFFFFF, 0xCAD2DD, 100))
    for x in (38.5, 50, 61.5):
        ic.fill(ic.line([(x, 43), (50 + (x - 50) * .82, 75)], 3.6), 0x8592A3)
    ic.raised(ic.rr(20, 24, 60, 8.5, 3.5), Grad(0xFFFFFF, 0xDBE1EA, 100))
    ic.fill(sub(ic.rr(39, 15, 22, 13, 4), ic.rr(43.5, 19.5, 13, 12, 2)), 0xF2F5F9)


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


@icon('clavier', 0x4C5A73, 0x262E3F)
def clavier(ic):
    ic.raised(ic.rr(12, 27, 76, 47, 7), Grad(0xFFFFFF, 0xD6DDE7, 100))
    w = 9.4
    k = 0x4C5A73
    if ic.small:
        for r in range(3):
            ic.fill(ic.rr(19, 34 + r * 12, 62, 6.5, 1.6), k)
    else:
        for i in range(6):
            ic.fill(ic.rr(17.5 + i * 11.2, 33, w, 8, 1.8), k)
        for i in range(5):
            ic.fill(ic.rr(23 + i * 11.2, 44, w, 8, 1.8), k)
        ic.fill(ic.rr(28.5, 55, 43, 8, 1.8), k)


@icon('ecran', 0x35A0F4, 0x1458C4)
def ecran(ic):
    f = screen(ic, 13, 18, 74, 50, None, Grad(0x7E6BF0, 0xF0719A, 120), 5, 3.4)
    ic.fill(inter(f, ic.poly([(13, 62), (34, 44), (47, 54), (64, 38), (90, 62), (90, 70), (13, 70)])), Grad(0x352C8E, 0x5A2C8E, 100), .75)
    if not ic.small:
        ic.fill(inter(f, ic.circ(66, 31, 6)), 0xFFE3A1)
    stand(ic, 68, 7, 28)


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


@icon('padconf', 0x6A78F4, 0x3A2DBD)
def padconf(ic):
    gamepad_body(ic, Grad(0xFFFFFF, 0xD3DBE7, 100))
    cross_pad(ic, 33, 47, 15, 0x4D5A73)
    ic.fill(ic.circ(62, 51, 4.8), 0x2CB67D)
    ic.fill(ic.circ(72, 43, 4.8), 0xF0475C)


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


@icon('printconf', 0x3DB0B4, 0x17707E)
def printconf(ic):
    ic.raised(ic.rr(29, 15, 42, 30, 2.4), PAPER)
    ic.raised(ic.rr(14, 36, 72, 34, 7), Grad(0xFFFFFF, 0xC6CFDB, 100))
    ic.fill(ic.rr(22, 45, 9, 4.6, 2.3), 0x35C088)
    ic.fill(ic.rr(24, 58, 52, 5, 2.4), 0x4A5668, .85)
    ic.raised(ic.rr(29, 60, 42, 26, 2.4), PAPER, rim=0)
    lines(ic, 36, 68, 28, 2, 7, 3, 0x9FB0C2)


@icon('clipboard', 0x7D8EA8, 0x445269)
def clipboard(ic):
    ic.raised(ic.rr(22, 19, 56, 67, 6.5), Grad(0xC59562, 0x93673B, 100))
    ic.raised(ic.rr(29, 27, 42, 53, 2.6), PAPER, rim=0)
    lines(ic, 36, 41, 28, 4, 8.5, 3.6)
    ic.raised(ic.rr(36, 13, 28, 15, 4), Grad(0xEDF1F6, 0xA7B3C2, 100))


# --- accessoires --------------------------------------------------------------------------------------------------
@icon('calculator', 0x566072, 0x282E3A)
def calculator(ic):
    ic.raised(ic.rr(21, 13, 58, 74, 8), Grad(0xF7F9FC, 0xCCD4DF, 100))
    ic.fill(ic.rr(28, 20, 44, 16, 3), Grad(0x263340, 0x34495A, 100))
    if not ic.small:
        ic.fill(ic.rr(51, 25.5, 16, 5.5, 1.4), 0x8FF0B8)
    for r in range(3):
        for c in range(3):
            k = 0xF28C28 if c == 2 and r == 2 else (0xF28C28 if c == 2 else 0x5B667A)
            ic.fill(ic.rr(28 + c * 15.4, 42 + r * 13.6, 13, 10.4, 2.4), Grad(light(k, .12), dark(k, .08), 100))


@icon('clock', 0xFFA94E, 0xE0561E)
def clock(ic):
    m = ic.circ(50, 50, 37)
    ic.raised(m, Grad(0xFFFFFF, 0xE6EAF0, 110))
    ic.fill(ic.ring(50, 50, 35.6, 2.8), 0xC9D2DE, .9)
    if not ic.small:
        for i in range(12):
            a = math.radians(i * 30)
            q = 5.4 if i % 3 == 0 else 3
            ic.fill(ic.line([(50 + 30 * math.sin(a), 50 - 30 * math.cos(a)), (50 + (30 - q) * math.sin(a), 50 - (30 - q) * math.cos(a))], 2.0), INK)
    ic.fill(ic.line([(50, 50), (36, 41)], 5.2), INK)
    ic.fill(ic.line([(50, 50), (62, 26)], 3.8), INK)
    if not ic.small:
        ic.fill(ic.line([(44, 58), (61, 40)], 1.5), 0xE8452C)
    ic.fill(ic.circ(50, 50, 3.8), 0xE8452C)


@icon('editor', 0x8A6DF2, 0x4C2FC0)
def editor(ic):
    doc(ic, 22, 14, 50, 70)
    lines(ic, 31, 34, 32, 4, 9.4, 3.4, 0xB3BFD0)
    ic.push(0, 0, 1, 45, (62, 62))
    ic.shadow(ic.rr(56, 28, 12, 44, 2))
    ic.fill(ic.rr(56, 38, 12, 34, 0), Grad(0xFFC64D, 0xE8922A, 0))
    ic.fill(ic.rr(56, 28, 12, 8, 3), 0xF17C8B)
    ic.fill(ic.rr(56, 34, 12, 4.5, 0), 0xC9D2DE)
    ic.fill(ic.poly([(56, 72), (68, 72), (62, 85)]), 0xF5DCC0)
    ic.fill(ic.poly([(59.8, 79.5), (64.2, 79.5), (62, 85)]), INK)
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


@icon('imageview', 0x86E0B4, 0x1F9C7A)
def imageview(ic):
    # un tableau : un paysage dans son cadre, une loupe devant
    ic.raised(ic.rr(13, 17, 66, 54, 4), WHITE)
    f = ic.rr(18.5, 22.5, 55, 43, 1.5)
    ic.fill(f, Grad(0x4FB2F2, 0xC4E9FA, 90))
    ic.fill(ic.circ(60, 33, 5.5), 0xFFD65C)
    ic.fill(inter(f, ic.poly([(18, 58), (32, 40), (43, 52), (51, 45), (74, 63), (74, 66), (18, 66)])), Grad(0x49B572, 0x1E7A4F, 90))
    ic.raised(ic.line([(74, 74), (85, 85)], 8), Grad(0x4A5568, 0x262E3B, 100))
    ic.fill(ic.circ(66, 66, 12.5), 0xFFFFFF, .45)
    ic.raised(ic.ring(66, 66, 12.5, 4.4), Grad(0x5B6779, 0x2E3746, 100))
    if not ic.small:
        ic.fill(ic.arc(66, 66, 8.2, 200, 250, 2), 0xFFFFFF, .85)


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


@icon('archiver', 0xD9A441, 0x94601C)
def archiver(ic):
    # un carton ferme par une fermeture eclair
    ic.raised(ic.rr(18, 17, 64, 66, 6), Grad(0xFFE9C2, 0xE8C78F, 100))
    ic.fill(ic.rr(44, 17, 12, 66, 0), 0x6E4A1E, .95)
    n = 7 if not ic.small else 4
    for i in range(n):
        y = 20 + i * 45 / (n - 1 or 1)
        ic.fill(ic.rr(46.8 if i % 2 == 0 else 50, y, 3.4 * (1.0 if not ic.small else 1.3), 4, .6), 0xF5D27A)
    ic.raised(ic.rr(44.6, 64, 10.8, 16, 2.8), Grad(0xFFFFFF, 0xC7D0DC, 100))
    ic.fill(ic.rr(48, 69, 4, 7.4, 2), 0x6B7A8E)


@icon('calendar', 0xFF6B5E, 0xCC2437)
def calendar(ic):
    m = ic.rr(16, 19, 68, 65, 8)
    ic.raised(m, PAPER)
    ic.fill(inter(m, ic.rr(14, 17, 72, 21, 0)), Grad(0xF7574D, 0xD92F3C, 100))
    for x in (33, 67):
        ic.raised(ic.rr(x - 3, 13, 6, 14, 3), Grad(0xFFFFFF, 0xD9DFE8, 100), sh=False)
    for r in range(3):
        for c in range(4):
            if r == 2 and c == 3:
                continue
            k = 0xE03A3E if (r, c) == (1, 2) else 0x8C99AC
            ic.fill(ic.rr(24.5 + c * 13.6, 44 + r * 12.6, 9, 8.6, 2.2), k)


# --- bureautique --------------------------------------------------------------------------------------------------
@icon('letters', 0x4C93F5, 0x1E50C6)
def letters(ic):
    doc(ic, 24, 13, 52, 74)
    lines(ic, 32, 32, 36, 5, 9, 3.6, 0x4A82E8, .55)


@icon('sheet', 0x3CC87F, 0x0E7E54)
def sheet(ic):
    m = ic.rr(15, 19, 70, 62, 6)
    ic.raised(m, PAPER)
    ic.fill(inter(m, ic.rr(14, 18, 72, 14, 0)), Grad(0x2FAF70, 0x1F8F5A, 100))
    ic.fill(inter(m, ic.rr(14, 18, 15, 64, 0)), 0x2FAF70, .85)
    ic.fill(ic.rr(30, 45, 19, 12, 0), 0xBFF0D3)
    ic.fill(ic.rr(50, 58, 19, 12, 0), 0xBFF0D3)
    c = 0x8CA79A
    for x in (29, 49, 68):
        ic.fill(ic.rr(x, 19, 1.2, 62), c, .9)
    for y in (32, 44.5, 57, 69.5):
        ic.fill(ic.rr(15, y, 70, 1.2), c, .9)


@icon('slides', 0xFF9A45, 0xDC4A17)
def slides(ic):
    m = ic.rr(13, 19, 74, 50, 5)
    ic.raised(m, PAPER)
    ic.fill(ic.rr(11, 15, 78, 7, 3), Grad(0x5A6476, 0x3A4252, 100))
    ic.fill(ic.circ(36, 44, 14), 0xB8C3D2)
    ic.fill(ic.poly(ic.ell_pts(36, 44, 14, 14, -90, 30) + [(36, 44)]), Grad(0xFF8238, 0xE64A19, 90))
    if not ic.small:
        lines(ic, 57, 36, 22, 3, 8.5, 4, 0xB3BFD0)
    ic.fill(ic.line([(50, 70), (40, 85)], 4), 0xF1F4F8)
    ic.fill(ic.line([(50, 70), (60, 85)], 4), 0xF1F4F8)


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


@icon('notes', 0xFFD84D, 0xEFA41B)
def notes(ic):
    ic.raised(ic.rr(22, 21, 56, 65, 5), Grad(0xFFFDF2, 0xFFF3C8, 100))
    ic.fill(ic.rr(22, 21, 56, 12, 4), Grad(0xFFC23D, 0xF2A122, 100))
    for i in range(5 if not ic.small else 3):
        x = 31 + i * (9.5 if not ic.small else 19)
        ic.fill(ic.rr(x - 1.6, 13, 3.2, 14, 1.6), Grad(0xE6EAF0, 0x8E99A8, 0))
    lines(ic, 31, 44, 38, 4, 9.2, 2.8, 0xD3B878)


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


@icon('pdf', 0xF2595E, 0xB51E36)
def pdf(ic):
    doc(ic, 24, 13, 52, 74)
    ic.fill(ic.rr(33, 35, 34, 20, 2), Grad(0xFFC2B8, 0xE85A58, 110))
    ic.fill(ic.poly([(33, 55), (44, 43), (51, 49), (58, 40), (67, 55)]), 0xB51E36, .8)
    lines(ic, 33, 62, 34, 2, 8, 3.4, 0xB9C4D2)


@icon('photos', 0xFBC6E0, 0xA585EA)
def photos(ic):
    ic.push(0, 0, 1, -11, (46, 50))
    ic.raised(ic.rr(19, 18, 52, 60, 2.4), Grad(0xF7B9CC, 0xEC7FA6, 100))
    ic.pop()
    ic.push(0, 0, 1, 7, (54, 52))
    ic.raised(ic.rr(24, 18, 58, 68, 2.4), Grad(0xFFFFFF, 0xEEF1F5, 100))
    f = ic.rr(30, 24, 46, 46, .8)
    ic.fill(f, Grad(0x5FC4F5, 0xBFE8F5, 90))
    ic.fill(ic.circ(63, 37, 6.5), 0xFFD65C)
    ic.fill(inter(f, ic.poly([(30, 62), (45, 44), (55, 56), (63, 49), (76, 62), (76, 70), (30, 70)])), Grad(0x3FA86E, 0x1F7A52, 90))
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


@icon('mail', 0x4FB5F7, 0x1B64DC)
def mail(ic):
    m = ic.rr(14, 25, 72, 52, 7)
    ic.raised(m, Grad(0xFFFFFF, 0xE6ECF4, 100))
    ic.fill(inter(m, ic.poly([(14, 77), (50, 46), (86, 77)])), Grad(0xFAFCFE, 0xDCE4EF, 100))
    ic.fill(ic.line([(16, 75), (42, 52)], 1.4), 0x9FB0C8, .6)
    ic.fill(ic.line([(84, 75), (58, 52)], 1.4), 0x9FB0C8, .6)
    fl = inter(m, ic.soften(ic.poly([(11, 23), (89, 23), (54, 56), (46, 56)]), 1))
    ic.shadow(fl, 1.1, 1.1, .22)
    ic.fill(fl, Grad(0xFFFFFF, 0xEDF2F8, 100))


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
@icon('paint', 0xFF8FA8, 0xD9416E)
def paint(ic):
    pal = add(ic.ell(48, 52, 36, 31))
    pal = sub(pal, ic.ell(70, 66, 8.5, 7.5))
    ic.raised(ic.soften(pal, .7), Grad(0xF3D3A6, 0xCF9C63, 110))
    for x, y, c in ((29, 44, 0xE5383B), (42, 33, 0xF5B83D), (58, 32, 0x2F9E6E), (73, 42, 0x2F74E0), (31, 62, 0x8A4FD6)):
        ic.raised(ic.circ(x, y, 6.4), Grad(light(c, .2), c, 100), sh=False, rim=.5, dk=.1)
    if not ic.small:
        ic.fill(ic.ell(50, 62, 8, 7), 0xFFFFFF, .9)


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


@icon('recorder', 0xEF4A6E, 0x9E1646)
def recorder(ic):
    # la cassette : deux bobines et sa fenetre
    ic.raised(ic.rr(12, 24, 76, 54, 6), Grad(0x3A4252, 0x1E232E, 100), rim=.25)
    ic.fill(ic.rr(19, 31, 62, 28, 3), Grad(0xFFF2D8, 0xF0D7A8, 100))
    ic.fill(ic.rr(29, 38, 42, 15, 7.5), 0x1E232E)
    for x in (37, 63):
        ic.fill(ic.circ(x, 45.5, 6), 0xF4F6FA)
        ic.fill(ic.circ(x, 45.5, 2.6), 0x1E232E)
    ic.fill(ic.poly([(28, 78), (31, 64), (69, 64), (72, 78)]), 0x565F70)


@icon('piano', 0x3B4150, 0x15171E)
def piano(ic):
    x0, w, n = 15, 70 / 5, 5
    for i in range(n):
        ic.raised(ic.rr(x0 + i * w + .6, 18, w - 1.2, 64, 3), Grad(0xFFFFFF, 0xDDE3EC, 100), sh=False, rim=0, dk=.14)
    for i in (1, 2, 4):
        ic.raised(ic.rr(x0 + i * w - 4.6, 16, 9.2, 40, 2.2), Grad(0x3A3F4C, 0x0E1016, 100), rim=.3, dk=0)


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


@icon('view3d', 0x344A78, 0x101A36)
def view3d(ic):
    t, l, r, b = (50, 13), (17, 31), (83, 31), (50, 49)
    bl, br, bb = (17, 69), (83, 69), (50, 87)
    ic.shadow(ic.poly([t, r, br, bb, bl, l]), op=.45)
    ic.fill(ic.poly([t, r, b, l]), Grad(0xFFD98A, 0xF5B14A, 90))
    ic.fill(ic.poly([l, b, bb, bl]), Grad(0xFF8A5C, 0xD9472E, 90))
    ic.fill(ic.poly([r, b, bb, br]), Grad(0x4FC8E8, 0x1C7CC0, 90))
    if not ic.small:
        for q in ((t, r), (r, b), (b, l), (l, t), (b, bb), (l, bl), (r, br), (bl, bb), (br, bb)):
            ic.fill(ic.line(list(q), .9), 0xFFFFFF, .75)


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


@icon('targets', 0x35C4C0, 0x0E7890)
def targets(ic):
    for r, c in ((36, 0xFFFFFF), (28.5, 0xE8344A), (21, 0xFFFFFF), (13.5, 0xE8344A), (6, 0xFFFFFF)):
        m = ic.circ(50, 50, r)
        if r == 36:
            ic.raised(m, Grad(0xFFFFFF, 0xE3E8EF, 110))
        else:
            ic.fill(m, Grad(light(c, .08), dark(c, .08), 100))
    ic.shadow(ic.line([(50, 50), (84, 16)], 3))
    ic.fill(ic.line([(50, 50), (82, 18)], 3), 0xF5C33D)
    ic.fill(ic.poly([(80, 12), (90, 12), (88, 20), (78, 22)]), 0xFFFFFF)


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


@glyph('power')
def g_power(ic):
    w = 15 if ic.small else 12.5
    ic.fill(add(ic.arc(50, 55, 34, -52, 232, w), ic.line([(50, 11), (50, 47)], w)))


@glyph('gear')
def g_gear(ic):
    ic.fill(sub(ic.gear(50, 50, 48, 35, 8, .46, .66), ic.circ(50, 50, 17 if ic.small else 16)))


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
    w = 12 if ic.small else 10
    board = sub(ic.rr(15, 12, 70, 84, 13), ic.rr(15 + w, 12 + w, 70 - 2 * w, 84 - 2 * w, 5))
    clip = ic.rr(32, 2, 36, 24, 7)
    ic.fill(sub(board, ic.rr(26, 0, 48, 30, 9)))
    ic.fill(clip)
    if not ic.small:
        ic.fill(add(ic.rr(36, 46, 28, 7, 3.5), ic.rr(36, 62, 28, 7, 3.5)))

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


glyph('trash')(lambda ic: strokes(ic, TOOLS['trash']))

for _n, _s in TOOLS.items():
    _tool(_n, _s)
