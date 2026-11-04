"""Essais a distance d'ARMPI (docs/RESEAU.md 5.5) : telnet, FTP, VNC, rdpd.

  python -I tools/armpi_remote.py telnet HOTE[:PORT] "commande" ["commande"...]
      chaque commande est envoyee a sh, suivie de "echo @FIN $?" ; la sortie
      de chaque commande et son code sont affiches ; "@N commande" : attendre
      sa fin N secondes (60 sinon)
  python -I tools/armpi_remote.py ftp HOTE[:PORT] ls|get|put|crc ...
      ls CHEMIN ; get CHEMIN_DISTANT FICHIER_LOCAL ; put FICHIER_LOCAL
      CHEMIN_DISTANT ; crc CHEMIN_DISTANT (XCRC)
  python -I tools/armpi_remote.py vnc HOTE[:PORT] capture.png [BPP [clic:X,Y] [touche:KEYSYM]...]
      une image de l'ecran (RFB 3.8, codage Raw, 32 ou 8 bits), apres des clics
      ou des touches
Mot de passe de l'acces a distance (SD0:/etc/remote.txt) : variable ARMPI_REMOTE_PW, donnee a telnet
quand il le demande (les cartes d'essai de QEMU : tools/mesa/testimage.py).
"""
import ftplib
import os
import socket
import struct
import sys
import time
import zlib


def hostport(s, default):
    if ":" in s:
        h, p = s.rsplit(":", 1)
        return h, int(p)
    return s, default


class Telnet:
    def __init__(self, host, port=23, timeout=20):
        self.s = socket.create_connection((host, port), timeout=timeout)
        self.buf = b""

    def read_until(self, marker, timeout=30):
        end = time.time() + timeout
        while marker not in self.buf:
            if time.time() > end:
                raise TimeoutError("attendu %r, recu %r" % (marker, self.buf[-200:]))
            self.s.settimeout(max(0.1, end - time.time()))
            try:
                d = self.s.recv(4096)
            except socket.timeout:
                continue
            if not d:
                raise EOFError("connexion fermee : %r" % self.buf[-200:])
            d = d.replace(b"\xff\xf1", b"")  # NOP
            self.buf += d
        i = self.buf.index(marker) + len(marker)
        out, self.buf = self.buf[:i], self.buf[i:]
        return out

    def run(self, cmd, timeout=60):
        """-> (sortie, code)"""
        self.s.sendall(cmd.encode("utf-8") + b"\r\n")
        self.s.sendall(b"echo @FIN $?\r\n")
        out = self.read_until(b"@FIN ", timeout)
        tail = self.read_until(b"\n", timeout)
        code = int(tail.strip() or b"0")
        text = out[:-len(b"@FIN ")].decode("utf-8", "replace")
        return text, code

    def close(self):
        try:
            self.s.sendall(b"exit\r\n")
        except OSError:
            pass
        self.s.close()


def crc32_local(path):
    return "%08X" % (zlib.crc32(open(path, "rb").read()) & 0xFFFFFFFF)


def vnc_capture(host, port, out, bpp=32, actions=()):
    """Une image de l'ecran ; bpp 8 (bgr233) pour aller plus vite sur un
    lien lent ; actions : ("clic", x, y) / ("touche", keysym) avant la capture."""
    s = socket.create_connection((host, port), timeout=120)
    ver = recvall(s, 12)
    assert ver.startswith(b"RFB "), ver
    s.sendall(b"RFB 003.008\n")
    n = recvall(s, 1)[0]
    types = recvall(s, n)
    assert 1 in types, types
    s.sendall(b"\x01")
    res = struct.unpack(">I", recvall(s, 4))[0]
    assert res == 0, "securite refusee"
    s.sendall(b"\x01")  # ClientInit : partage
    w, h = struct.unpack(">HH", recvall(s, 4))
    recvall(s, 16)  # format de pixels du serveur
    nl = struct.unpack(">I", recvall(s, 4))[0]
    name = recvall(s, nl)
    if bpp == 8:
        s.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, 8, 8, 0, 1, 7, 7, 3, 0, 3, 6))
    else:
        s.sendall(struct.pack(">BxxxBBBBHHHBBBxxx", 0, 32, 24, 0, 1, 255, 255, 255, 16, 8, 0))
    s.sendall(struct.pack(">BxH", 2, 1) + struct.pack(">i", 0))  # Raw
    for a in actions:
        if a[0] == "clic":
            x, y = a[1], a[2]
            s.sendall(struct.pack(">BBHH", 5, 0, x, y))
            s.sendall(struct.pack(">BBHH", 5, 1, x, y))
            time.sleep(0.15)
            s.sendall(struct.pack(">BBHH", 5, 0, x, y))
        elif a[0] == "touche":
            s.sendall(struct.pack(">BBxxI", 4, 1, a[1]))
            s.sendall(struct.pack(">BBxxI", 4, 0, a[1]))
        time.sleep(0.3)
    if actions:
        time.sleep(2)
    s.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, w, h))
    pb = bpp // 8
    img = bytearray(w * h * pb)
    got = 0
    t0 = time.time()
    while got < w * h:
        t = recvall(s, 1)[0]
        if t != 0:
            raise RuntimeError("message %d inattendu" % t)
        recvall(s, 1)
        nr = struct.unpack(">H", recvall(s, 2))[0]
        for _ in range(nr):
            x, y, rw, rh, enc = struct.unpack(">HHHHi", recvall(s, 12))
            assert enc == 0, enc
            data = recvall(s, rw * rh * pb)
            for row in range(rh):
                o = ((y + row) * w + x) * pb
                img[o:o + rw * pb] = data[row * rw * pb:(row + 1) * rw * pb]
            got += rw * rh
    dt = time.time() - t0
    s.close()
    from PIL import Image
    if bpp == 8:
        rgb = bytearray()
        for v in img:
            rgb += bytes(((v & 7) * 255 // 7, ((v >> 3) & 7) * 255 // 7, ((v >> 6) & 3) * 255 // 3))
        im = Image.frombytes("RGB", (w, h), bytes(rgb))
    else:
        im = Image.frombytes("RGBX", (w, h), bytes(img), "raw", "BGRX").convert("RGB")
    im.save(out)
    return name.decode("utf-8", "replace"), w, h, "%.1f s" % dt


def recvall(s, n):
    b = b""
    while len(b) < n:
        d = s.recv(n - len(b))
        if not d:
            raise EOFError
        b += d
    return b


def main():
    what, target = sys.argv[1], sys.argv[2]
    if what == "telnet":
        h, p = hostport(target, 23)
        t = Telnet(h, p)
        if b"Mot de passe" in t.read_until(b" : ", 20):
            t.s.sendall(os.environ.get("ARMPI_REMOTE_PW", "").encode("utf-8") + b"\r\n")
        t.read_until(b"> ", 20)
        for c in sys.argv[3:]:
            limit = 60
            if c.startswith("@") and " " in c:      # "@600 commande" : attente de 600 s
                limit, c = int(c[1:c.index(" ")]), c[c.index(" ") + 1:]
            out, code = t.run(c, limit)
            print("$ %s\n%s[code %d]" % (c, out, code))
        t.close()
    elif what == "ftp":
        h, p = hostport(target, 21)
        f = ftplib.FTP()
        f.connect(h, p, timeout=30)
        f.login("armpi", "")
        op = sys.argv[3]
        if op == "ls":
            for l in f.nlst(sys.argv[4] if len(sys.argv) > 4 else ""):
                print(l)
        elif op == "list":
            f.retrlines("LIST " + (sys.argv[4] if len(sys.argv) > 4 else ""))
        elif op == "get":
            with open(sys.argv[5], "wb") as o:
                f.retrbinary("RETR " + sys.argv[4], o.write)
        elif op == "put":
            with open(sys.argv[4], "rb") as i:
                f.storbinary("STOR " + sys.argv[5], i)
        elif op == "crc":
            print(f.sendcmd("XCRC " + sys.argv[4]))
        f.quit()
    elif what == "vnc":
        h, p = hostport(target, 5900)
        bpp = int(sys.argv[4]) if len(sys.argv) > 4 else 32
        acts = []
        for a in sys.argv[5:]:      # clic:X,Y  touche:KEYSYM
            k, v = a.split(":")
            if k == "clic":
                x, y = v.split(",")
                acts.append(("clic", int(x), int(y)))
            else:
                acts.append(("touche", int(v, 0)))
        print(vnc_capture(h, p, sys.argv[3], bpp, acts))


if __name__ == "__main__":
    main()
