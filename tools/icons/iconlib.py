# iconlib.py -- le pinceau des icones d'ARMPI : dessin vectoriel surechantillonne (numpy, Pillow).
#
# Une icone se dessine une fois sur un maitre de M x M pixels, dans un repere de 0 a 100 qui est celui de la tuile
# (0,0 = coin haut-gauche de la tuile). Tout passe par des polygones en coordonnees de ce repere : une forme est un
# masque (image L) du maitre, un calque est un masque et une peinture, compose en alpha premultiplie (flottants).
# Deux maitres : `small` (tailles <= 26 : tuile plus grande, sans ombre, moins de details), l'autre.
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageChops

M = 768                 # cote du maitre : 8 x la plus grande taille utile (96), 16 x 48

SP = 5.0                # exposant de la superellipse de la tuile
INSET = 5.4             # marge de la tuile dans la case, en % de la case (ombre comprise)
INSET_SMALL = 1.6


def rgb(c):
    return np.array([(c >> 16) & 255, (c >> 8) & 255, c & 255], dtype=np.float32) / 255


def mix(a, b, t):
    ca, cb = rgb(a), rgb(b)
    c = ca + (cb - ca) * t
    r, g, bl = [int(round(float(v) * 255)) for v in c]
    return (r << 16) | (g << 8) | bl


def light(c, t=.2):
    return mix(c, 0xFFFFFF, t)


def dark(c, t=.2):
    return mix(c, 0x000000, t)


class Grad:
    """Degrade lineaire : a = angle en degres (90 : du haut vers le bas). Sur la boite du masque, sauf box donnee."""
    def __init__(s, c0, c1, a=90, box=None):
        s.c0, s.c1, s.a, s.box = c0, c1, a, box


class Rad:
    """Degrade radial : centre et rayon relatifs a la boite du masque (0..1)."""
    def __init__(s, c0, c1, cx=.5, cy=.5, r=.7):
        s.c0, s.c1, s.cx, s.cy, s.r = c0, c1, cx, cy, r


def tone(c, up=.16, down=.14):
    """Le volume d'une couleur : plus claire en haut, plus sombre en bas (lumiere commune, haut-gauche)."""
    return Grad(light(c, up), dark(c, down), 100)


class Ic:
    def __init__(s, small=False, glyph=False):
        s.small = small
        s.pix = np.zeros((M, M, 4), dtype=np.float32)
        inset = 0 if glyph else INSET_SMALL if small else INSET
        s.span = (100 - 2 * inset) / 100 * M        # cote de la tuile, pixels du maitre
        s.ox = inset / 100 * M
        s.oy = (inset / 100 * M) - (0 if small or glyph else .55 / 100 * M)   # un peu haute : l'ombre est dessous
        s.stack = [np.eye(3)]
        s.clip_mask = None
        s.yy, s.xx = np.mgrid[0:M, 0:M].astype(np.float32)

    # --- repere -------------------------------------------------------------------------------------------------
    def u(s, v):
        return v * s.span / 100

    def push(s, dx=0, dy=0, scale=1, rot=0, c=(50, 50)):
        """Transformation des formes suivantes (rotation en degres autour de c, puis echelle autour de c, puis dx, dy)."""
        a = math.radians(rot)
        cs, sn = math.cos(a) * scale, math.sin(a) * scale
        t = np.array([[cs, -sn, c[0] - cs * c[0] + sn * c[1] + dx],
                      [sn, cs, c[1] - sn * c[0] - cs * c[1] + dy],
                      [0, 0, 1]])
        s.stack.append(s.stack[-1] @ t)

    def pop(s):
        s.stack.pop()

    def pts(s, p):
        m = s.stack[-1]
        out = []
        for x, y in p:
            xx = m[0, 0] * x + m[0, 1] * y + m[0, 2]
            yy = m[1, 0] * x + m[1, 1] * y + m[1, 2]
            out.append((s.ox + xx * s.span / 100, s.oy + yy * s.span / 100))
        return out

    # --- formes -> masques --------------------------------------------------------------------------------------
    def blank(s):
        return Image.new('L', (M, M), 0)

    def poly(s, p):
        im = s.blank()
        ImageDraw.Draw(im).polygon(s.pts(p), fill=255)
        return im

    def many(s, polys):
        im = s.blank()
        d = ImageDraw.Draw(im)
        for p in polys:
            d.polygon(s.pts(p), fill=255)
        return im

    def rr_pts(s, x, y, w, h, r):
        r = min(r, w / 2, h / 2)
        p = []
        for cx, cy, a0 in ((x + w - r, y + r, -90), (x + w - r, y + h - r, 0), (x + r, y + h - r, 90), (x + r, y + r, 180)):
            for i in range(25):
                a = math.radians(a0 + i * 90 / 24)
                p.append((cx + r * math.cos(a), cy + r * math.sin(a)))
        return p

    def rr(s, x, y, w, h, r=0):
        return s.poly(s.rr_pts(x, y, w, h, r))

    def ell_pts(s, cx, cy, rx, ry, a0=0, a1=360, n=180):
        return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
                 cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]

    def circ(s, cx, cy, r):
        return s.poly(s.ell_pts(cx, cy, r, r))

    def ell(s, cx, cy, rx, ry):
        return s.poly(s.ell_pts(cx, cy, rx, ry))

    def ring(s, cx, cy, r, w):
        return sub(s.circ(cx, cy, r + w / 2), s.circ(cx, cy, r - w / 2))

    def line(s, p, w, cap='round'):
        """Trait de largeur w le long de la ligne brisee p (joints ronds)."""
        polys = []
        h = w / 2
        for (x0, y0), (x1, y1) in zip(p, p[1:]):
            dx, dy = x1 - x0, y1 - y0
            n = math.hypot(dx, dy)
            if n == 0:
                continue
            nx, ny = -dy / n * h, dx / n * h
            polys.append([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)])
        joints = p[1:-1] if cap != 'round' else p
        for x, y in joints:
            polys.append(s.ell_pts(x, y, h, h, n=48))
        return s.many(polys)

    def arc(s, cx, cy, r, a0, a1, w, cap='round', ry=None):
        return s.line(s.ell_pts(cx, cy, r, ry if ry else r, a0, a1, n=96), w, cap)

    def star(s, cx, cy, r, r2, n=5, rot=-90):
        p = []
        for i in range(n * 2):
            a = math.radians(rot + i * 180 / n)
            q = r if i % 2 == 0 else r2
            p.append((cx + q * math.cos(a), cy + q * math.sin(a)))
        return s.poly(p)

    def gear(s, cx, cy, r, rb, teeth=8, top=.42, bot=.62):
        """Roue dentee : r exterieur, rb au fond des dents ; flancs inclines, sommets arrondis par lissage."""
        p = []
        st = 360 / teeth
        for i in range(teeth):
            a = i * st - 90
            for da, q in ((-bot / 2, rb), (-top / 2, r), (top / 2, r), (bot / 2, rb)):
                t = math.radians(a + da * st)
                p.append((cx + q * math.cos(t), cy + q * math.sin(t)))
            # fond de dent : un arc du cercle de base
            for k in range(1, 8):
                t = math.radians(a + bot / 2 * st + (1 - bot) * st * k / 8)
                p.append((cx + rb * math.cos(t), cy + rb * math.sin(t)))
        return s.soften(s.poly(p), .7)

    def soften(s, m, r):
        """Arrondit les angles d'un masque (r en unites) : flou puis seuil adouci."""
        b = m.filter(ImageFilter.GaussianBlur(s.u(r)))
        return b.point(lambda v: max(0, min(255, (v - 128) * 12 + 128)))

    # --- calques ------------------------------------------------------------------------------------------------
    def clip(s, m):
        s.clip_mask = m

    def _color(s, paint, a):
        if isinstance(paint, int):
            c = rgb(paint)
            return c[None, None, :]
        box = None
        ys, xs = np.nonzero(a > 0.004)
        if len(xs):
            box = (float(xs.min()), float(ys.min()), float(xs.max()) + 1, float(ys.max()) + 1)
        else:
            box = (0, 0, M, M)
        if isinstance(paint, Grad):
            if paint.box:
                pp = s.pts([(paint.box[0], paint.box[1]), (paint.box[2], paint.box[3])])
                box = (pp[0][0], pp[0][1], pp[1][0], pp[1][1])
            r = math.radians(paint.a)
            dx, dy = math.cos(r), math.sin(r)
            cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
            half = (abs(dx) * (box[2] - box[0]) + abs(dy) * (box[3] - box[1])) / 2 or 1
            t = ((s.xx - cx) * dx + (s.yy - cy) * dy) / (2 * half) + .5
            c0, c1 = rgb(paint.c0), rgb(paint.c1)
        else:
            w, h = box[2] - box[0], box[3] - box[1]
            cx, cy = box[0] + w * paint.cx, box[1] + h * paint.cy
            t = np.hypot(s.xx - cx, s.yy - cy) / (paint.r * max(w, h) or 1)
            c0, c1 = rgb(paint.c0), rgb(paint.c1)
        t = np.clip(t, 0, 1)[:, :, None]
        return c0[None, None, :] + (c1 - c0)[None, None, :] * t

    def fill(s, m, paint=0xFFFFFF, op=1.0):
        a = np.asarray(m, dtype=np.float32) / 255 * op
        if s.clip_mask is not None:
            a = a * (np.asarray(s.clip_mask, dtype=np.float32) / 255)
        col = s._color(paint, a)
        inv = (1 - a)[:, :, None]
        s.pix[:, :, :3] = col * a[:, :, None] + s.pix[:, :, :3] * inv
        s.pix[:, :, 3] = a + s.pix[:, :, 3] * inv[:, :, 0]

    def shadow(s, m, dy=1.2, blur=1.4, op=.3, dx=None, color=0x000000):
        """Ombre portee d'une forme : la lumiere vient du haut-gauche, elle tombe en bas a droite."""
        if s.small:
            dy, blur, op = dy * .7, blur * .6, op * .7
        dx = dy * .35 if dx is None else dx
        o = ImageChops.offset(m, int(s.u(dx)), int(s.u(dy))).filter(ImageFilter.GaussianBlur(s.u(blur)))
        s.fill(o, color, op)

    def raised(s, m, paint=None, sh=True, rim=.4, dk=.16, e=.7):
        """Un element en leger relief : ombre sur ce qui est dessous, peinture, arete claire en haut, sombre en bas."""
        if paint is None:
            paint = Grad(0xFFFFFF, 0xDCE3EE, 100)
        if sh:
            s.shadow(m)
        s.fill(m, paint)
        if s.small:
            return
        d = int(max(1, s.u(e)))
        b = s.u(e * .4)
        if rim:
            r = ImageChops.subtract(m, ImageChops.offset(m, 0, d)).filter(ImageFilter.GaussianBlur(b))
            s.fill(ImageChops.multiply(r, m), 0xFFFFFF, rim)
        if dk:
            r = ImageChops.subtract(m, ImageChops.offset(m, 0, -d)).filter(ImageFilter.GaussianBlur(b))
            s.fill(ImageChops.multiply(r, m), 0x000000, dk)

    # --- la tuile -----------------------------------------------------------------------------------------------
    def tile_pts(s):
        p = []
        for i in range(720):
            a = math.radians(i / 2)
            c, sn = math.cos(a), math.sin(a)
            p.append((50 + 50 * abs(c) ** (2 / SP) * math.copysign(1, c),
                      50 + 50 * abs(sn) ** (2 / SP) * math.copysign(1, sn)))
        return p

    def tile(s, top, bottom):
        t = s.poly(s.tile_pts())
        s.tile_mask = t
        if not s.small:     # ombre douce, comprise dans l'image : un contact serre et une nappe large
            s.shadow(t, 1.1, .6, .22, dx=0)
            s.shadow(t, 2.0, 2.0, .24, dx=0)
        s.fill(t, Grad(top, bottom, 90, (0, 0, 100, 100)))
        s.clip(t)
        # reflet du haut, discret : une nappe claire qui s'efface a mi-hauteur
        sheen = Image.fromarray(np.uint8(np.clip(1 - s.yy / (M * .55), 0, 1) ** 1.6 * 255))
        s.fill(sheen, 0xFFFFFF, .13)
        # le bas, un peu plus dense
        foot = Image.fromarray(np.uint8(np.clip((s.yy / M - .6) / .4, 0, 1) ** 2 * 255))
        s.fill(foot, 0x000000, .08)

    def rim(s):
        """Lisere de la tuile, a la fin : fonce tout autour, clair sur l'arete haute."""
        t = s.tile_mask
        s.clip(None)
        px = max(1, int(s.u(.9 if not s.small else 2.0)))
        inner = t.filter(ImageFilter.MinFilter(px * 2 + 1 if px < 6 else 11))
        if px >= 6:
            inner = ImageChops.offset(t, 0, 0).filter(ImageFilter.GaussianBlur(px)).point(lambda v: 255 if v > 245 else 0)
        edge = ImageChops.subtract(t, inner)
        s.fill(edge, 0x000000, .16)
        top = ImageChops.subtract(t, ImageChops.offset(t, 0, max(1, int(s.u(.8)))))
        top = top.filter(ImageFilter.GaussianBlur(s.u(.3)))
        s.fill(ImageChops.multiply(top, t), 0xFFFFFF, .42)


# --- operations sur masques ---------------------------------------------------------------------------------------
def sub(a, b):
    return ImageChops.subtract(a, b)


def add(*ms):
    r = ms[0]
    for m in ms[1:]:
        r = ImageChops.lighter(r, m)
    return r


def inter(a, b):
    return ImageChops.multiply(a, b)


# --- reduction ----------------------------------------------------------------------------------------------------
def reduce_to(pix, size):
    """Maitre (flottants premultiplies) -> size x size, par boite, puis un peu d'accentuation : uint8 premultiplie."""
    out = np.zeros((size, size, 4), dtype=np.float32)
    for k in range(4):
        out[:, :, k] = np.asarray(Image.fromarray(pix[:, :, k], 'F').resize((size, size), Image.BOX))
    amount = .55 if size <= 26 else .38 if size <= 48 else .2
    blur = out.copy()
    for ax in (0, 1):
        e = np.concatenate((blur.take([0], ax), blur, blur.take([-1], ax)), ax)     # bords repetes
        lo = e.take(range(0, size), ax)
        mid = e.take(range(1, size + 1), ax)
        hi = e.take(range(2, size + 2), ax)
        blur = (lo + 2 * mid + hi) / 4
    res = out + (out - blur) * amount
    a = np.clip(res[:, :, 3:4], 0, 1)
    res[:, :, :3] = np.clip(res[:, :, :3], 0, a)
    res[:, :, 3:4] = a
    return np.uint8(np.round(res * 255))
