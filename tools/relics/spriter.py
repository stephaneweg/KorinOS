"""spriter.py -- rejoue une animation Spriter (.scml : os, morceaux, cles) en images, pour faire les personnages de
Relics a partir de morceaux (tools/relics/explorer.py). Ce qu'il faut des fichiers des paquets de l'utilisateur :
interpolation lineaire (ou « instant »), sens de rotation (spin), pivots, echelles, alpha, ordre z.

    from spriter import Scml
    s = Scml ("Animations.scml", {"Head.png": image PIL, ...})
    im = s.render ("Walking", 300, scale=0.5, poses={"bone_002": 40})   # 300 ms, os bone_002 tourne de 40 degres
"""
import math
import xml.etree.ElementTree as ET

from PIL import Image


def _f(e, k, d=0.0):
    v = e.get(k)
    return float(v) if v is not None else d


class Scml:
    def __init__(self, path, images):
        r = ET.parse(path).getroot()
        self.files = {}
        for fo in r.findall("folder"):
            for fi in fo.findall("file"):
                self.files[(int(fo.get("id")), int(fi.get("id")))] = (
                    fi.get("name"), _f(fi, "pivot_x", 0), _f(fi, "pivot_y", 1), _f(fi, "width"), _f(fi, "height"))
        self.images = images
        self.anims = {a.get("name"): a for a in r.find("entity").findall("animation")}

    def length(self, name):
        return int(self.anims[name].get("length"))

    # -- l'etat d'une ligne de temps a l'instant t
    def _state(self, anim, tl_id, key_id, t, looping):
        tl = anim.findall("timeline")[tl_id]
        keys = tl.findall("key")
        k = keys[key_id]
        nxt = key_id + 1
        length = int(anim.get("length"))
        if nxt >= len(keys):
            if not looping:
                nxt = None
            else:
                nxt = 0
        o = k[0]
        st = {n: _f(o, n, dflt) for n, dflt in (("x", 0), ("y", 0), ("angle", 0), ("scale_x", 1), ("scale_y", 1), ("a", 1))}
        if o.tag == "object":
            fo, fi = int(o.get("folder")), int(o.get("file"))
            name, px, py, w, h = self.files[(fo, fi)]
            st.update(name=name, pivot_x=_f(o, "pivot_x", px), pivot_y=_f(o, "pivot_y", py))
        st["name_tl"] = tl.get("name")
        if nxt is None or k.get("curve_type") == "instant":
            return st
        k2 = keys[nxt]
        t0 = _f(k, "time")
        t1 = _f(k2, "time") + (length if nxt == 0 else 0)
        if t1 <= t0:
            return st
        tt = t if t >= t0 else t + length
        u = max(0.0, min(1.0, (tt - t0) / (t1 - t0)))
        o2 = k2[0]
        for n, dflt in (("x", 0), ("y", 0), ("scale_x", 1), ("scale_y", 1), ("a", 1)):
            st[n] = st[n] + (_f(o2, n, dflt) - st[n]) * u
        spin = int(k.get("spin", "1"))
        a0, a1 = st["angle"], _f(o2, "angle", 0)
        if spin == 1 and a1 < a0:
            a1 += 360
        elif spin == -1 and a1 > a0:
            a1 -= 360
        if spin != 0:
            st["angle"] = a0 + (a1 - a0) * u
        return st

    def render(self, name, t, scale=1.0, poses=None, swap=None, size=1100, origin=(550, 820), looping=True, hide=()):
        """L'image a l'instant t (ms). poses : {nom d'os ou de morceau : degres en plus, ou (degres, dx, dy)} ; swap : {nom de fichier :
        image} (un morceau change) ; hide : des morceaux caches. Le zero de Spriter est en origin (les pieds)."""
        anim = self.anims[name]
        ml = anim.find("mainline").findall("key")
        mk = ml[0]
        for k in ml:
            if _f(k, "time") <= t:
                mk = k
        poses = poses or {}
        swap = swap or {}
        bones = {}

        def world(st, parent):
            if parent is None:
                return dict(st)
            p = bones[parent]
            x, y = st["x"] * p["scale_x"], st["y"] * p["scale_y"]
            a = math.radians(p["angle"])
            w = dict(st)
            w["x"] = p["x"] + x * math.cos(a) - y * math.sin(a)
            w["y"] = p["y"] + x * math.sin(a) + y * math.cos(a)
            w["angle"] = p["angle"] + st["angle"]
            w["scale_x"] = p["scale_x"] * st["scale_x"]
            w["scale_y"] = p["scale_y"] * st["scale_y"]
            w["a"] = p["a"] * st["a"]
            return w

        def pose(st):
            p = poses.get(st["name_tl"], 0)
            if isinstance(p, tuple):			# (degres, dx, dy) : le morceau tourne et se deplace
                st["angle"] += p[0]
                st["x"] += p[1]
                st["y"] += p[2]
            else:
                st["angle"] += p

        for br in mk.findall("bone_ref"):
            st = self._state(anim, int(br.get("timeline")), int(br.get("key")), t, looping)
            pose(st)
            par = br.get("parent")
            bones[br.get("id")] = world(st, par)
        objs = []
        for orf in mk.findall("object_ref"):
            st = self._state(anim, int(orf.get("timeline")), int(orf.get("key")), t, looping)
            pose(st)
            w = world(st, orf.get("parent"))
            objs.append((int(orf.get("z_index", "0")), w))
        objs.sort(key=lambda o: o[0])
        canvas = Image.new("RGBA", (size, size))
        for _, w in objs:
            if w["a"] <= 0.01 or w["name_tl"] in hide:
                continue
            img = swap.get(w["name"]) or self.images.get(w["name"])
            if img is None:
                continue
            self._draw(canvas, img, w, scale, origin)
        return canvas

    @staticmethod
    def _draw(canvas, img, w, scale, origin):
        iw, ih = img.size
        # le pivot dans l'image (Spriter : y vers le haut, depuis le bas)
        px, py = w["pivot_x"] * iw, (1 - w["pivot_y"]) * ih
        a = math.radians(-w["angle"])			# (l'ecran : y vers le bas)
        sx, sy = w["scale_x"] * scale, w["scale_y"] * scale
        ca, sa = math.cos(a), math.sin(a)
        ox, oy = origin[0] + w["x"] * scale, origin[1] - w["y"] * scale
        # ecran = O + R * S * (p - pivot) ; l'inverse pour PIL
        # p = pivot + S^-1 * R^-1 * (ecran - O)
        a11, a12 = ca / sx, sa / sx
        a21, a22 = -sa / sy, ca / sy
        c1 = px - (a11 * ox + a12 * oy)
        c2 = py - (a21 * ox + a22 * oy)
        layer = img.transform(canvas.size, Image.AFFINE, (a11, a12, c1, a21, a22, c2), resample=Image.BICUBIC)
        if w["a"] < 0.99:
            al = layer.getchannel("A").point(lambda v: int(v * w["a"]))
            layer.putalpha(al)
        canvas.alpha_composite(layer)
