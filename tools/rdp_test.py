"""Essai minimal de rdpd (docs/RESEAU.md 5.4, etape 12), a la maniere de
tools/tests/rdpd/rdpd_test.py d'Onyx. Ancien protocole, sans mot de passe (ONYXRDP1) : rdpd
parle maintenant ONYXRDP2, voir tools/remote/rdp_auth_test.py pour l'essai avec mot de passe.

  python -I tools/rdp_test.py HOTE[:PORT] [--16] [--capture ecran.png] [--save fenetre.png]
         [--type TITRE texte [--at X Y]]

Poignee de main, une ronde (WIN, ZORDER, PIXELS en LZ4 ou bruts, END), le
contenu de la fenetre la plus en avant (image, comparee a une capture de
l'ecran si --capture), puis --type : un clic dans la fenetre TITRE et des
caracteres (CHAR) + Entree : la ronde suivante doit renvoyer ses pixels.
Code de sortie : nombre d'echecs.
"""
import socket
import struct
import sys
import time

fails = 0


def check(ok, what):
    global fails
    print(("ok   " if ok else "ECHEC ") + what)
    if not ok:
        fails += 1


def recvall(s, n):
    b = b""
    while len(b) < n:
        d = s.recv(n - len(b))
        if not d:
            raise EOFError
        b += d
    return b


def lz4(src, n):
    out = bytearray()
    i = 0
    while i < len(src):
        t = src[i]
        i += 1
        lit = t >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += src[i:i + lit]
        i += lit
        if i >= len(src):
            break
        off = src[i] | src[i + 1] << 8
        i += 2
        ml = t & 15
        if ml == 15:
            while True:
                b = src[i]
                i += 1
                ml += b
                if b != 255:
                    break
        r = len(out) - off
        for k in range(ml + 4):
            out.append(out[r + k])
    assert len(out) == n, (len(out), n)
    return bytes(out)


def round_(s, timeout=240):
    """une ronde : les messages jusqu'a END (PING -> PONG), puis READY"""
    s.settimeout(timeout)
    msgs = []
    while True:
        t, n = struct.unpack("<BI", recvall(s, 5))
        p = recvall(s, n) if n else b""
        if t == 10:
            s.sendall(b"\x08" + p[:4])
            continue
        msgs.append((t, p))
        if t == 5:
            s.sendall(b"\x01")
            return msgs


def main():
    a = sys.argv[1:]
    host, port = (a[0].rsplit(":", 1) + ["3390"])[:2]
    b16 = "--16" in a
    s = socket.create_connection((host, int(port)), timeout=30)
    h = recvall(s, 14)
    w, hh, dpi = struct.unpack("<HHH", h[8:])
    check(h[:8] == b"ONYXRDP1" and dpi >= 56, "salut : ONYXRDP1, %d x %d, %d" % (w, hh, dpi))
    s.sendall(b"ONYXRDP1" + bytes([4 | (1 if b16 else 0), 1]))   # pipeline, READY
    t0 = time.time()
    first = []
    while not any(t == 5 for t, p in first):                      # CAPS, CURSOR, puis la ronde
        s.settimeout(240)
        t, n = struct.unpack("<BI", recvall(s, 5))
        first.append((t, recvall(s, n) if n else b""))
    s.sendall(b"\x01")
    caps = [p for t, p in first if t == 9]
    check(caps and caps[0][0] == 2 and caps[0][1] == 3, "CAPS : protocole 2, 3 rondes en vol")
    wins, z, px = {}, [], {}
    bad = nlz = 0
    for t, p in first:
        if t == 1:
            i, x, y, ww, wh, ow, oh, il, it, fl, al, st, tn = struct.unpack("<IhhHHHHHHIBBB", p[:27])
            wins[i] = dict(x=x, y=y, w=ww, h=wh, ow=ow, oh=oh, flags=fl, state=st,
                           title=p[27:27 + tn].decode("latin-1"))
            px[i] = bytearray(ww * wh * 4)
        elif t == 3:
            n = struct.unpack("<H", p[:2])[0]
            z = list(struct.unpack("<%dI" % n, p[2:2 + 4 * n]))
        elif t == 4:
            i, part, bpp, lz, x, y, rw, rh = struct.unpack("<IBBBHHHH", p[:15])
            d = lz4(p[15:], rw * rh * bpp // 8) if lz else p[15:]
            nlz += lz
            if len(d) != rw * rh * bpp // 8:
                bad += 1
                continue
            if part == 0 and i in wins:
                W = wins[i]["w"]
                for j in range(rh):
                    for k in range(rw):
                        if bpp == 32:
                            c = d[(j * rw + k) * 4:(j * rw + k) * 4 + 3]
                        else:
                            v = d[(j * rw + k) * 2] | d[(j * rw + k) * 2 + 1] << 8
                            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
                            c = bytes((b << 3 | b >> 2, g << 2 | g >> 4, r << 3 | r >> 2))
                        o = ((y + j) * W + x + k) * 4
                        px[i][o:o + 3] = c
    npx = sum(1 for t, p in first if t == 4)
    print("premiere ronde : %.1f s, %d PIXELS (%d en LZ4)" % (time.time() - t0, npx, nlz))
    for i in z:
        m = wins.get(i)
        if m:
            print("  %08X %4d,%4d %4dx%-4d cadre %dx%d drapeaux %d etat %d %s" % (
                i, m["x"], m["y"], m["w"], m["h"], m["ow"], m["oh"], m["flags"], m["state"], m["title"]))
    check(wins and set(z) == set(wins) and npx > 0 and bad == 0, "WIN, ZORDER, PIXELS coherents")
    front = [i for i in z if not wins[i]["flags"] & 7 and not wins[i]["state"] & 4]
    if front:
        from PIL import Image
        m = wins[front[-1]]
        im = Image.frombytes("RGBX", (m["w"], m["h"]), bytes(px[front[-1]]), "raw", "BGRX").convert("RGB")
        if "--save" in a:
            im.save(a[a.index("--save") + 1])
        if "--capture" in a:
            cap = Image.open(a[a.index("--capture") + 1]).convert("RGB")
            A, B = im.load(), cap.load()
            same = tot = 0
            for yy in range(0, m["h"], 2):
                for xx in range(0, m["w"], 2):
                    X, Y = m["x"] + xx, m["y"] + yy
                    if 0 <= X < cap.width and 0 <= Y < cap.height:
                        tot += 1
                        d = max(abs(u - v) for u, v in zip(A[xx, yy], B[X, Y]))
                        same += d <= (8 if b16 else 0)
            check(same >= 0.98 * tot, "%s : %d/%d pixels egaux a la capture" % (m["title"], same, tot))
    if "--type" in a:
        title, text = a[a.index("--type") + 1], a[a.index("--type") + 2]
        ids = [i for i in z if title.lower() in wins[i]["title"].lower()]
        check(bool(ids), "fenetre %r" % title)
        if ids:
            i = ids[-1]
            m = wins[i]
            cx, cy = m["w"] // 2, m["h"] // 2
            if "--at" in a:
                cx, cy = int(a[a.index("--at") + 1]), int(a[a.index("--at") + 2])
            pt = struct.pack("<BIhh", 2, i, cx, cy)
            s.sendall(pt + b"\x01\x00")
            time.sleep(0.2)
            s.sendall(pt + b"\x00\x00")
            time.sleep(0.5)
            for ch in text:
                s.sendall(struct.pack("<BI", 6, ord(ch)))
            s.sendall(struct.pack("<BBI", 3, 1, 0xFF0D) + struct.pack("<BBI", 3, 0, 0xFF0D))
            got = False
            end = time.time() + 60
            while time.time() < end and not got:
                for t, p in round_(s, 60):
                    if t == 4 and struct.unpack("<I", p[:4])[0] == i and p[4] == 0:
                        got = True
            check(got, "clic et texte %r dans %s : ses pixels reviennent" % (text, m["title"]))
    s.close()
    print("%d echec(s)" % fails)
    sys.exit(fails)


if __name__ == "__main__":
    main()
