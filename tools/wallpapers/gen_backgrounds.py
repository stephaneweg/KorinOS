#!/usr/bin/env python3
# Fonds d'écran de Korin OS, calculés (aucune image externe).
#
#   python -I tools/wallpapers/gen_backgrounds.py [nom...]
#
# Sans nom : les cinq fonds, puis la planche (build/wallpapers/planche.png).
# Les fonds vont dans build/wallpapers/sdroot/home/public/wallpapers : l'arbre du paquet
# « wallpapers » (tools/pkg/packages.ini, root = build/wallpapers/sdroot), installé à part dans le
# dossier public /home/public/wallpapers, que Réglages ouvre pour « Choisir... » (volet Thème).
# Les graines sont fixes : deux lancements donnent les mêmes images.
# Style « Givre calme » : peu de détail, pas de texte, l'intérêt loin des bords
# (les images sont recadrées en « cover » sur 16:9, 16:10…).

import os
import sys
import time

import numpy as np
from PIL import Image

W, H = 2560, 1440
ASPECT = W / H
OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        "..", "..", "build", "wallpapers"))
PKG_DIR = os.path.join(OUT_DIR, "sdroot", "home", "public", "wallpapers")


# ---------------------------------------------------------------- outils

def hexc(v):
    """Couleur 0xRRGGBB en triplet flottant 0..1."""
    return np.array([(v >> 16) & 255, (v >> 8) & 255, v & 255], np.float32) / 255.0


def grid():
    """Coordonnées des centres de pixels : X dans [0, ASPECT], Y dans [0, 1] (vers le bas)."""
    x = ((np.arange(W, dtype=np.float32) + 0.5) / H)[None, :]
    y = ((np.arange(H, dtype=np.float32) + 0.5) / H)[:, None]
    return x, y


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def edge(d, px=1.5):
    """Bord anticrénelé : d est une distance signée en unités de hauteur d'image."""
    return np.clip(d * (H / px) + 0.5, 0.0, 1.0)


def mixc(a, b, t):
    """Mélange de deux couleurs ; t scalaire ou carte."""
    if np.ndim(t) == 0:
        return a + (b - a) * t
    return a + (b - a) * t[..., None]


def over(img, col, alpha):
    """Pose col sur img avec l'opacité alpha (carte H×W ou diffusable)."""
    a = np.broadcast_to(alpha, img.shape[:2])[..., None]
    img += (col - img) * a


def add(img, col, amount):
    """Lumière ajoutée (fonds sombres)."""
    img += col * np.broadcast_to(amount, img.shape[:2])[..., None]


def vgradient(y, stops):
    """Dégradé vertical lissé (pas de cassure de pente aux arrêts) ; rend H×1×3."""
    ys = np.array([s[0] for s in stops], np.float32)
    cols = np.stack([hexc(s[1]) for s in stops])
    yy = y[:, 0]
    k = max(3, H // 8) | 1
    ker = np.ones(k, np.float32) / k
    out = np.empty((H, 1, 3), np.float32)
    for c in range(3):
        prof = np.interp(yy, ys, cols[:, c])
        prof = np.convolve(np.pad(prof, k // 2, mode="edge"), ker, mode="valid")
        out[:, 0, c] = prof
    return out


def noise1d(x, table):
    """Bruit de valeur 1D, interpolation quintique."""
    n = len(table)
    i = np.floor(x).astype(np.int64)
    f = (x - i).astype(np.float32)
    f = f * f * f * (f * (f * 6.0 - 15.0) + 10.0)
    a = table[i % n]
    b = table[(i + 1) % n]
    return a + (b - a) * f


def fbm1d(x, rng, octaves=4, gain=0.5, lac=2.0):
    """Somme d'octaves de bruit 1D, ramenée dans 0..1."""
    total = 0.0
    norm = 0.0
    amp = 1.0
    freq = 1.0
    for _ in range(octaves):
        table = rng.random(256).astype(np.float32)
        total = total + amp * noise1d(x * freq + rng.random() * 64.0, table)
        norm += amp
        amp *= gain
        freq *= lac
    return total / norm


def noise2d(x, y, table):
    """Bruit de valeur 2D (table carrée, repliée)."""
    n = table.shape[0]
    xb = np.broadcast_to(x, np.broadcast(x, y).shape)
    yb = np.broadcast_to(y, xb.shape)
    ix = np.floor(xb).astype(np.int64)
    iy = np.floor(yb).astype(np.int64)
    fx = (xb - ix).astype(np.float32)
    fy = (yb - iy).astype(np.float32)
    fx = fx * fx * fx * (fx * (fx * 6.0 - 15.0) + 10.0)
    fy = fy * fy * fy * (fy * (fy * 6.0 - 15.0) + 10.0)
    x0, x1 = ix % n, (ix + 1) % n
    y0, y1 = iy % n, (iy + 1) % n
    top = table[y0, x0] + (table[y0, x1] - table[y0, x0]) * fx
    bot = table[y1, x0] + (table[y1, x1] - table[y1, x0]) * fx
    return top + (bot - top) * fy


def fbm2d(x, y, rng, octaves=3, gain=0.5):
    total = 0.0
    norm = 0.0
    amp = 1.0
    freq = 1.0
    for _ in range(octaves):
        table = rng.random((64, 64)).astype(np.float32)
        total = total + amp * noise2d(x * freq + rng.random() * 32.0,
                                      y * freq + rng.random() * 32.0, table)
        norm += amp
        amp *= gain
        freq *= 2.0
    return total / norm


def glow(x, y, cx, cy, radius, sx=1.0):
    """Tache gaussienne douce centrée en (cx, cy)."""
    return np.exp(-(((x - cx) / (radius * sx)) ** 2 + ((y - cy) / radius) ** 2))


def finish(img, rng, vignette=0.08, grain=0.9):
    """Vignette légère, grain fin et tramage (contre les bandes), puis 8 bits."""
    x, y = grid()
    r2 = ((x / ASPECT - 0.5) ** 2 + (y - 0.5) ** 2) * 2.0
    img *= (1.0 - vignette * r2)[..., None]
    # Grain de luminance (le même sur les trois canaux) + tramage triangulaire par canal.
    g = rng.standard_normal((H, W)).astype(np.float32) * (grain / 255.0)
    img += g[..., None]
    d = (rng.random((H, W, 3), dtype=np.float32) - rng.random((H, W, 3), dtype=np.float32)) / 255.0
    out = np.clip((img + d) * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGB")


# ---------------------------------------------------------------- 1. Givre, voiles de soie

def veil(img, x, y, a, b, col_a, col_b, line_col, lines, fill, line_alpha):
    """Voile tendu entre les courbes a(x) et b(x) : un fond translucide et des fils fins.

    Là où les courbes se croisent, le voile se tord et les fils se resserrent."""
    wdt = b - a
    sgn = np.where(wdt >= 0, 1.0, -1.0).astype(np.float32)
    aw = np.maximum(np.abs(wdt), 1e-5)
    t = (y - a) / (wdt + sgn * 1e-5)
    lo = np.minimum(a, b)
    hi = np.maximum(a, b)
    inside = edge(y - lo, 2.0) * edge(hi - y, 2.0)
    # Le voile s'efface vers les bords gauche et droit de l'image.
    xb = x / ASPECT
    inside = inside * (0.15 + 0.85 * smoothstep(0.0, 0.22, xb) * (1.0 - smoothstep(0.78, 1.0, xb)))
    tc = np.clip(t, 0.0, 1.0)
    # Le fond : plus dense vers un bord (pli), s'efface quand le voile est vu par la tranche.
    body = (0.35 + 0.65 * tc ** 1.6) * smoothstep(0.0, 0.06, aw)
    col = mixc(col_a, col_b, tc)
    over(img, col, inside * body * fill)
    # Les fils : motif périodique en t ; fondu vers une teinte unie quand ils sont trop serrés.
    spacing = aw / lines * H
    ph = t * lines
    dpx = np.abs(ph - np.floor(ph + 0.5)) * spacing
    pat = np.exp(-(dpx / 1.1) ** 2)
    avg = np.clip(1.95 / np.maximum(spacing, 0.01), 0.0, 1.0)
    k = smoothstep(2.5, 6.0, spacing)
    pat = avg + (pat - avg) * k
    dens = np.clip(pat, 0.0, 1.0) * (0.45 + 0.55 * tc)
    over(img, line_col, inside * dens * line_alpha)


def render_silk(rng):
    x, y = grid()
    img = np.broadcast_to(vgradient(y, [(0.0, 0xEEF2F7), (0.5, 0xDFE6EF), (1.0, 0xCDD7E4)]),
                          (H, W, 3)).copy()
    over(img, hexc(0xF7F9FC), 0.75 * glow(x, y, 0.55 * ASPECT, 0.30, 0.55, 1.5))
    over(img, hexc(0xC3D2EA), 0.45 * glow(x, y, 0.85 * ASPECT, 0.95, 0.50, 1.6))
    over(img, hexc(0xE9DEEA), 0.30 * glow(x, y, 0.10 * ASPECT, 0.85, 0.45, 1.5))

    u = x / ASPECT * 2.0 * np.pi
    # Trois voiles, du plus lointain au plus proche.
    a = 0.50 + 0.16 * np.sin(u * 0.55 + 0.4) + 0.05 * np.sin(u * 1.3 + 2.0)
    b = 0.62 + 0.15 * np.sin(u * 0.70 + 2.6) + 0.04 * np.sin(u * 1.6 + 0.3)
    veil(img, x, y, a, b, hexc(0xD5DFF0), hexc(0xAFC3E6), hexc(0x8FA9DA), 46, 0.55, 0.30)

    a = 0.60 + 0.17 * np.sin(u * 0.50 + 3.6) + 0.04 * np.sin(u * 1.2 + 1.0)
    b = 0.50 + 0.14 * np.sin(u * 0.62 + 5.2) + 0.05 * np.sin(u * 1.5 + 4.0)
    veil(img, x, y, a, b, hexc(0xE4E0F2), hexc(0xBDB9E6), hexc(0x9C9ADB), 40, 0.50, 0.28)

    a = 0.47 + 0.15 * np.sin(u * 0.58 + 1.9) + 0.03 * np.sin(u * 1.7 + 5.0)
    b = 0.66 + 0.13 * np.sin(u * 0.45 + 0.2) + 0.05 * np.sin(u * 1.1 + 3.1)
    veil(img, x, y, a, b, hexc(0xF4F7FC), hexc(0x9DBBF2), hexc(0x4F7FDC), 54, 0.50, 0.34)
    return finish(img, rng, vignette=0.06)


# ---------------------------------------------------------------- 2. Givre, aube sur les crêtes

def render_dawn(rng):
    x, y = grid()
    img = np.broadcast_to(vgradient(y, [(0.0, 0xB7C9E3), (0.30, 0xD2DDEC), (0.52, 0xEDE6E6),
                                        (0.64, 0xF6E7DC), (1.0, 0xF6E7DC)]), (H, W, 3)).copy()
    sx, sy = 0.60 * ASPECT, 0.585
    r = np.sqrt((x - sx) ** 2 + (y - sy) ** 2)
    over(img, hexc(0xFFF1E2), 0.70 * np.exp(-(r / 0.30) ** 2))
    over(img, hexc(0xFFF9F1), 0.55 * np.exp(-(r / 0.09) ** 2))
    over(img, hexc(0xFFFCF7), 0.95 * (1.0 - smoothstep(0.030, 0.036, r)))

    far, near, mist = hexc(0xC6CEDD), hexc(0x425B7C), hexc(0xECE7EA)
    n = 6
    for i in range(n):
        d = i / (n - 1)
        base = 0.585 + 0.074 * i
        amp = 0.040 + 0.065 * d
        freq = 2.4 - 1.3 * d
        ridge = base - amp * (fbm1d(x * freq + 10.0 * i, rng, 5, 0.46) - 0.5) * 2.0
        top = mixc(far, near, d ** 1.15)
        # Halo chaud du soleil sur les crêtes lointaines.
        warm = np.exp(-((x - sx) / 0.45) ** 2) * (1.0 - d) * 0.35
        topc = top + (hexc(0xF3DFD3) - top) * warm[..., None]
        low = mixc(top, mist, 0.62 - 0.25 * d)
        m = 1.0 - np.exp(-np.maximum(y - ridge, 0.0) / (0.085 + 0.05 * d))
        col = topc + (low - topc) * m[..., None]
        over(img, col, edge(y - ridge))
    return finish(img, rng, vignette=0.07)


# ---------------------------------------------------------------- 3. Givre, orbes de verre

def render_orbs(rng):
    x, y = grid()
    t = np.clip((x / ASPECT) * 0.45 + y * 0.55, 0.0, 1.0)
    t = np.broadcast_to(t, (H, W))
    img = mixc(hexc(0xEAEFF6), hexc(0xC6D2E4), t).astype(np.float32)
    over(img, hexc(0xF3DDE6), 0.55 * glow(x, y, 0.80 * ASPECT, 0.22, 0.42, 1.4))
    over(img, hexc(0xB5CCF3), 0.60 * glow(x, y, 0.22 * ASPECT, 0.72, 0.48, 1.4))
    over(img, hexc(0xF8FAFD), 0.55 * glow(x, y, 0.38 * ASPECT, 0.18, 0.36, 1.5))

    palette = [0xFFFFFF, 0xFFFFFF, 0xC4D6F6, 0x9DBBF2, 0xF7E4EC, 0xDCE8FF, 0x7FA4EC]
    orbs = []
    # Positions tirées sur une grille secouée : les orbes se répartissent sur toute l'image.
    cols, rows = 6, 4
    for gy in range(rows):
        for gx in range(cols):
            if rng.random() < 0.12:
                continue
            orbs.append(((gx + rng.uniform(0.1, 0.9)) / cols * ASPECT,
                         (gy + rng.uniform(0.1, 0.9)) / rows,
                         rng.uniform(0.07, 0.25), rng.uniform(0.0, 1.0), rng.uniform(0.12, 0.30),
                         palette[rng.integers(len(palette))]))
    # Les plus flous d'abord (loin), les plus nets devant.
    orbs.sort(key=lambda o: -o[3])
    for cx, cy, rad, blur, alpha, colv in orbs:
        soft = rad * (0.06 + 0.9 * blur ** 1.5)
        dist = np.sqrt((x - cx) ** 2 + (y - cy) ** 2)
        disc = 1.0 - smoothstep(rad - soft, rad + soft * 0.4 + 0.002, dist)
        # Verre : un peu plus clair vers le haut gauche, liseré lumineux pour les plus nets.
        shade = 0.78 + 0.22 * smoothstep(-1.6, 1.6, ((cx - x) + (cy - y)) / rad)
        over(img, hexc(colv), disc * shade * alpha)
        rim = np.exp(-((dist - rad + soft * 0.5) / (0.010 + soft * 0.35)) ** 2)
        over(img, hexc(0xFFFFFF), rim * 0.10 * (1.0 - blur))
    return finish(img, rng, vignette=0.07)


# ---------------------------------------------------------------- 4. Onyx, vagues

def render_waves(rng):
    x, y = grid()
    ang = np.deg2rad(-13.0)
    xc, yc = x - ASPECT / 2.0, y - 0.5
    u = xc * np.cos(ang) + yc * np.sin(ang)
    v = -xc * np.sin(ang) + yc * np.cos(ang) + 0.5
    v = np.broadcast_to(v, (H, W))
    u = np.broadcast_to(u, (H, W))

    img = mixc(hexc(0x0D111B), hexc(0x1B2234), np.clip(v, 0.0, 1.0)).astype(np.float32)
    add(img, hexc(0x2B4FA8), 0.30 * glow(x, y, 0.62 * ASPECT, 0.30, 0.50, 1.5))
    add(img, hexc(0x4A2F8F), 0.12 * glow(x, y, 0.20 * ASPECT, 0.20, 0.40, 1.5))

    n = 7
    lit = [0x3A3F8F, 0x3D55C4, 0x2F6BE6, 0x3F86F2, 0x2F6BE6, 0x2B4FB0, 0x24376F]
    for i in range(n):
        d = i / (n - 1)
        c = (0.22 + 0.105 * i
             + 0.060 * np.sin(u * (1.5 + 0.25 * i) + rng.uniform(0, 6.28))
             + 0.025 * np.sin(u * (3.1 + 0.4 * i) + rng.uniform(0, 6.28)))
        dist = v - c
        dp = np.maximum(dist, 0.0)
        deep = mixc(hexc(0x161C2E), hexc(0x0C1019), d)
        mid = mixc(hexc(lit[i]), deep, 0.55)
        k = np.exp(-dp / 0.16)
        col = deep + (mid - deep) * k[..., None]
        # Lumière sur la crête, plus forte vers le centre de l'image.
        focus = 0.45 + 0.55 * np.exp(-((u - 0.15) / 0.75) ** 2)
        hl = (0.55 * np.exp(-dp / 0.012) + 0.35 * np.exp(-dp / 0.05)) * focus
        col = col + (hexc(lit[i]) * 1.15 - col) * np.clip(hl, 0.0, 1.0)[..., None]
        # Ombre portée sur la vague de derrière.
        over(img, hexc(0x080B12), 0.38 * np.exp(-((np.minimum(dist, 0.0)) / 0.035) ** 2) * (dist < 0))
        over(img, col, edge(dist) * 0.96)
    return finish(img, rng, vignette=0.22, grain=1.0)


# ---------------------------------------------------------------- 5. Onyx, aurore

def render_aurora(rng):
    x, y = grid()
    img = np.broadcast_to(vgradient(y, [(0.0, 0x070A12), (0.55, 0x0E1420), (1.0, 0x182033)]),
                          (H, W, 3)).copy()
    add(img, hexc(0x1D3F86), 0.22 * glow(x, y, 0.50 * ASPECT, 0.78, 0.38, 2.2))

    # Étoiles : rares et faibles, loin du bas.
    ns = 260
    sx = rng.integers(2, W - 2, ns)
    sy = (rng.random(ns) ** 1.4 * 0.62 * H).astype(np.int64) + 2
    sb = rng.random(ns) ** 3 * 0.55 + 0.05
    ker = np.array([[0.08, 0.25, 0.08], [0.25, 1.0, 0.25], [0.08, 0.25, 0.08]], np.float32)
    star_col = hexc(0xDCE6FF)
    for px, py, br in zip(sx, sy, sb):
        img[py - 1:py + 2, px - 1:px + 2] += ker[..., None] * star_col * br

    low, midc, high = hexc(0x58C6F0), hexc(0x2563D8), hexc(0x6A48D0)
    curtains = [(0.66, 0.34, 0.85, 0.3), (0.55, 0.28, 0.60, 2.1), (0.47, 0.22, 0.42, 4.4)]
    light = np.zeros((H, W, 3), np.float32)
    for y0, h0, strength, ph in curtains:
        xb = x / ASPECT
        yb = (y0 + 0.055 * np.sin(xb * 5.2 + ph) + 0.03 * np.sin(xb * 11.0 + ph * 2.3)
              + 0.05 * (fbm1d(x * 1.3 + ph, rng, 2) - 0.5))
        hgt = h0 * (0.55 + 0.9 * fbm1d(x * 1.7 + 7.0 * ph, rng, 2))
        d = yb - y
        up = np.maximum(d, 0.0)
        body = np.where(d > 0, np.exp(-up / hgt), np.exp(-(d / 0.022) ** 2))
        # Rayons : légèrement inclinés, peu contrastés.
        rays = fbm1d((x + 0.18 * up) * 16.0 + ph * 5.0, rng, 3, 0.55)
        rays = 0.50 + 0.50 * smoothstep(0.25, 0.75, rays)
        env = smoothstep(0.02, 0.30, xb) * (1.0 - smoothstep(0.72, 0.99, xb))
        env = env * (0.55 + 0.45 * fbm1d(x * 0.9 + ph * 3.0, rng, 2))
        inten = body * rays * env * strength
        tcol = 1.0 - np.exp(-up / (hgt * 0.75))
        col = np.where((tcol < 0.5)[..., None], mixc(low, midc, tcol * 2.0),
                       mixc(midc, high, tcol * 2.0 - 1.0))
        light += col * inten[..., None]
    # Compression douce : pas de zone brûlée.
    img += 1.0 - np.exp(-light * 0.95)

    # Collines au premier plan.
    ridge = 0.90 - 0.045 * (fbm1d(x * 1.1 + 3.0, rng, 4, 0.45) - 0.5) * 2.0
    hill = mixc(hexc(0x090C14), hexc(0x05070C), np.clip((y - ridge) / 0.1, 0.0, 1.0)
                * np.ones((1, W), np.float32))
    over(img, hill, edge(y - ridge))
    ridge = 0.955 - 0.03 * (fbm1d(x * 0.8 + 9.0, rng, 3, 0.45) - 0.5) * 2.0
    over(img, hexc(0x040509), edge(y - ridge))
    return finish(img, rng, vignette=0.20, grain=1.0)


# ---------------------------------------------------------------- lancement

WALLPAPERS = [
    ("givre-silk", render_silk, 1101),
    ("givre-dawn", render_dawn, 2202),
    ("givre-orbs", render_orbs, 3303),
    ("onyx-waves", render_waves, 4404),
    ("onyx-aurora", render_aurora, 5505),
]


def contact_sheet():
    """Planche : les cinq fonds côte à côte, réduits."""
    tw, th, gap = 640, 360, 24
    names = [n for n, _, _ in WALLPAPERS]
    paths = [os.path.join(PKG_DIR, n + ".png") for n in names]
    if not all(os.path.exists(p) for p in paths):
        return None
    sheet = Image.new("RGB", (gap + (tw + gap) * len(paths), th + 2 * gap), (0x2A, 0x2F, 0x3A))
    for i, p in enumerate(paths):
        with Image.open(p) as im:
            sheet.paste(im.convert("RGB").resize((tw, th), Image.LANCZOS), (gap + i * (tw + gap), gap))
    out = os.path.join(OUT_DIR, "planche.png")
    sheet.save(out, optimize=True)
    return out


def main(argv):
    wanted = argv[1:]
    known = {n for n, _, _ in WALLPAPERS}
    for n in wanted:
        if n not in known:
            print("fond inconnu : %s (connus : %s)" % (n, ", ".join(sorted(known))))
            return 2
    os.makedirs(PKG_DIR, exist_ok=True)
    for name, fn, seed in WALLPAPERS:
        if wanted and name not in wanted:
            continue
        t0 = time.time()
        im = fn(np.random.default_rng(seed))
        path = os.path.join(PKG_DIR, name + ".png")
        im.save(path, optimize=False, compress_level=6)
        print("%-12s %s (%.1f s)" % (name, path, time.time() - t0))
    sheet = contact_sheet()
    if sheet:
        print("planche      %s" % sheet)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
