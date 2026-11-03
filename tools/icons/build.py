#!/usr/bin/env python3
# build.py -- rend les icones d'ARMPI : un fichier par icone dans tools/icons/rendered/, commites : build.ps1 les copie
# sur la carte sans avoir besoin de Python.
#
#   python tools/icons/build.py [noms...]              rend (tous par defaut)
#   python tools/icons/build.py --sheet planche.png    et dessine la planche (toutes les tailles, fond clair et sombre)
#
# Format du fichier <nom>.rgba (lu par src/kits/uikit/uikit_icons.inc) :
#   +0  'ARIC'
#   +4  u16 version (1), u16 n (nombre de tailles)
#   +8  n entrees de 8 octets : u16 cote, u16 0, u32 decalage des pixels depuis le debut du fichier
#   pixels : cote x cote mots 0xAARRGGBB petit-boutistes, ALPHA PREMULTIPLIE, ligne du haut d'abord
import multiprocessing
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np
from PIL import Image

from iconlib import Ic, reduce_to
from art import ICONS, GLYPHS

OUT = os.path.join(HERE, 'rendered')
# les tailles dessinees a l'ecran : menus 16, Fichiers 20 et 48, Reglages 24, panel 26 a 38, dock 36 et 40, Logiciels 32,
# lanceur 40 a 48 ; 64 ne sert que de source aux tailles non prevues
SIZES = (16, 20, 24, 26, 30, 32, 36, 38, 40, 44, 48, 64)
SMALL_MAX = 26
# les glyphes des boutons systeme (fichier glyph-<nom>.rgba, meme format, blanc : le kit n'en prend que l'alpha) : dock 16
# et 18, panel 20 a 24, pastille du lanceur 40 a 44 ; les autres servent de source
GLYPH_SIZES = (14, 16, 18, 20, 22, 24, 26, 28, 32, 40, 42, 44, 48)
GLYPH_SMALL_MAX = 20
TOOL_SIZES = (16, 18, 20)       # glyphes d'outils (tb-*) : trois tailles, ils sont lus par dizaines


def master(name, small):
    top, bottom, art = ICONS[name]
    ic = Ic(small)
    ic.tile(top, bottom)
    art(ic)
    ic.rim()
    return ic.pix


def render_glyph(name):
    ms = {}
    for small in (True, False):
        ic = Ic(small, glyph=True)
        GLYPHS[name](ic)
        ms[small] = ic.pix
    sizes = TOOL_SIZES if name.startswith('tb-') else GLYPH_SIZES
    return {z: reduce_to(ms[z <= GLYPH_SMALL_MAX], z) for z in sizes}


def render(name):
    big = master(name, False)
    small = master(name, True)
    return {z: reduce_to(small if z <= SMALL_MAX else big, z) for z in SIZES}


def write(name, imgs):
    sizes = sorted(imgs)
    n = len(sizes)
    off = 8 + 8 * n
    head = b'ARIC' + struct.pack('<HH', 1, n)
    body = []
    for z in sizes:
        head += struct.pack('<HHI', z, 0, off)
        a = imgs[z]
        px = np.dstack((a[:, :, 2], a[:, :, 1], a[:, :, 0], a[:, :, 3]))      # B, G, R, A : 0xAARRGGBB petit-boutiste
        body.append(px.tobytes())
        off += z * z * 4
    with open(os.path.join(OUT, name + '.rgba'), 'wb') as f:
        f.write(head)
        for b in body:
            f.write(b)


def work(name):
    if name.startswith('glyph-'):
        write(name, render_glyph(name[6:]))
    else:
        write(name, render(name))
    return name


def load(name):
    with open(os.path.join(OUT, name + '.rgba'), 'rb') as f:
        d = f.read()
    n = struct.unpack_from('<H', d, 6)[0]
    r = {}
    for i in range(n):
        z, _, off = struct.unpack_from('<HHI', d, 8 + 8 * i)
        a = np.frombuffer(d, dtype=np.uint8, count=z * z * 4, offset=off).reshape(z, z, 4)
        r[z] = a
    return r


def over(canvas, px, x, y):
    """Compose comme le fera le kit : premultiplie sur fond opaque."""
    h, w = px.shape[:2]
    a = px[:, :, 3:4].astype(np.float32) / 255
    src = px[:, :, [2, 1, 0]].astype(np.float32)
    canvas[y:y + h, x:x + w] = src + canvas[y:y + h, x:x + w] * (1 - a)


def sheet(names, path, sizes=(64, 48, 38, 32, 24, 20, 16), cols=4):
    gap = 8
    cw = sum(sizes) + gap * (len(sizes) + 1)
    rows = (len(names) + cols - 1) // cols
    ch = 64 + gap * 2
    W = cw * cols
    H = ch * rows * 2 + 12
    bgs = ((0xF2, 0xF4, 0xF7), (0x1C, 0x1F, 0x26))
    c = np.zeros((H, W, 3), dtype=np.float32)
    half = ch * rows + 6
    c[:half] = bgs[0]
    c[half:] = bgs[1]
    for t in range(2):
        for i, nm in enumerate(names):
            imgs = load(nm)
            ox = (i % cols) * cw
            oy = (i // cols) * ch + (0 if t == 0 else half)
            x = ox + gap
            for z in sizes:
                over(c, imgs[z], x, oy + gap + (64 - z) // 2)
                x += z + gap
    g = [glyph_strip(bgs[0], (0x20, 0x26, 0x30), (0x2F, 0x74, 0xE0), (0xD9, 0x2D, 0x3A)),
         glyph_strip(bgs[1], (0xEC, 0xEF, 0xF4), (0x3B, 0x82, 0xE8), (0xF0, 0x4F, 0x5A))]
    c = np.vstack([g[0], c[:half], g[1], c[half:]])
    Image.fromarray(np.uint8(np.clip(c, 0, 255))).save(path)


def glyph_strip(bg, ink, accent, danger):
    """Les glyphes, coloris comme au dessin : boutons du panel et du dock, a leurs tailles, puis agrandis."""
    names = sorted(GLYPHS)
    sizes = (44, 24, 22, 20, 18, 16)
    W, H = 1224, 60 * len(names) // 2 + 20
    c = np.zeros((H, W, 3), dtype=np.float32)
    c[:] = bg
    cols = {'power': danger}
    y = 8
    x0 = 8
    for i, nm in enumerate(names):
        d = load("glyph-" + nm)
        x = x0
        col = cols.get(nm, accent if nm == 'pill' else ink)
        for z in sizes:
            a = d[z][:, :, 3:4].astype(np.float32) / 255
            px = np.dstack((np.array(col[::-1], dtype=np.float32) * a, a * 255))
            over(c, px, x, y + (44 - z) // 2)
            x += z + 12
        x0 += 400
        if x0 > 900:
            x0 = 8
            y += 58
    return c


def zoom(names, path, cols=10, size=64, scale=2):
    """Une page agrandie, pour regarder les dessins de pres."""
    z = size * scale + 8
    rows = (len(names) + cols - 1) // cols
    c = np.full((z * rows * 2, z * cols, 3), 0xE9, dtype=np.float32)
    c[z * rows:] = 0x22
    for t in range(2):
        for i, nm in enumerate(names):
            p = load(nm)[size]
            p = np.repeat(np.repeat(p, scale, 0), scale, 1)
            over(c, p, (i % cols) * z + 4, (i // cols) * z + 4 + t * z * rows)
    Image.fromarray(np.uint8(np.clip(c, 0, 255))).save(path)


def main():
    args = sys.argv[1:]
    sheetp = zoomp = None
    if '--sheet' in args:
        i = args.index('--sheet')
        sheetp = args[i + 1]
        del args[i:i + 2]
    if '--zoom' in args:
        i = args.index('--zoom')
        zoomp = args[i + 1]
        del args[i:i + 2]
    norender = '--no-render' in args
    if norender:
        args.remove('--no-render')
    os.makedirs(OUT, exist_ok=True)
    names = args or sorted(ICONS) + ['glyph-' + g for g in sorted(GLYPHS)]
    if not norender:
        with multiprocessing.Pool() as p:
            for nm in p.imap_unordered(work, names):
                pass
    print(len(names), 'icones')
    alln = sorted(ICONS)
    names = [n for n in names if not n.startswith('glyph-')]
    if sheetp:
        sheet(alln, sheetp)
    if zoomp:
        zoom(names if args else alln, zoomp)


if __name__ == '__main__':
    main()
