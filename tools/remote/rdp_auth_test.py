"""Essai du mot de passe de rdpd (src/svc/rdpd/rdpd.asm, salut "ONYXRDP2").

  python -I tools/remote/rdp_auth_test.py HOTE[:PORT] MOT_DE_PASSE

Trois connexions : un mauvais mot de passe (verdict 1, le serveur ferme), un
ancien client qui repond "ONYXRDP1" (le serveur ferme sans rien dire), puis le
bon mot de passe (verdict 0, puis la session : CAPS et CURSOR doivent venir).
Code de sortie : nombre d'echecs.
"""
import hashlib
import hmac
import socket
import struct
import sys

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
            raise EOFError("connexion fermee apres %d octets sur %d" % (len(b), n))
        b += d
    return b


def hello(addr):
    s = socket.create_connection(addr, timeout=20)
    h = recvall(s, 14 + 16)
    w, ht, ver = struct.unpack("<HHH", h[8:14])
    return s, h[:8], w, ht, ver, h[14:]


def closed(s):
    try:
        return s.recv(1) == b""
    except (ConnectionError, socket.timeout):
        return True


def main():
    host, _, port = sys.argv[1].partition(":")
    addr = (host, int(port or 3390))
    pw = sys.argv[2].encode("utf-8")

    s, magic, w, h, ver, defi = hello(addr)
    check(magic == b"ONYXRDP2", "salut %r, ecran %dx%d, version %d" % (magic, w, h, ver))
    mac = hmac.new(pw + b"x", defi, hashlib.sha256).digest()
    s.sendall(b"ONYXRDP2" + bytes([4]) + mac)
    v = recvall(s, 1)[0]
    check(v == 1, "mauvais mot de passe : verdict %d (1 attendu)" % v)
    check(closed(s), "puis le serveur ferme")
    s.close()

    s, magic, w, h, ver, defi = hello(addr)
    s.sendall(b"ONYXRDP1" + bytes([4, 7, 0, 1]))
    check(closed(s), "ancien client (ONYXRDP1) : le serveur ferme sans verdict")
    s.close()

    s, magic, w, h, ver, defi2 = hello(addr)
    check(defi2 != defi, "un nouveau defi a chaque connexion")
    mac = hmac.new(pw, defi2, hashlib.sha256).digest()
    s.sendall(b"ONYXRDP2" + bytes([4]) + mac)
    v = recvall(s, 1)[0]
    check(v == 0, "bon mot de passe : verdict %d (0 attendu)" % v)
    if v == 0:
        s.sendall(bytes([7, 0, 1]))            # DESKTOP 0, READY
        types = []
        for _ in range(2):
            t, n = struct.unpack("<BI", recvall(s, 5))
            recvall(s, n)
            types.append(t)
        check(types == [9, 11], "la session commence : CAPS puis CURSOR (%s)" % types)
    s.close()
    print("echecs :", fails)
    sys.exit(fails)


main()
