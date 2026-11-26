"""Essai de la connexion par compte de rdpd (E70, salut "ONYXRDP2", reponse "ONYXRDP3").

  python -I tools/remote/rdp_account_test.py HOTE[:PORT] COMPTE MOT_DE_PASSE

Quatre connexions : le bon compte et un mauvais mot de passe (verdict 1), un compte inconnu (verdict 1, avec un sel
stable : le meme deux fois), l'ancien client au secret commun en mode comptes (verdict 1), puis le bon mot de passe
(verdict 0, puis CAPS et CURSOR). Code de sortie : nombre d'echecs.
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
    s = socket.create_connection(addr, timeout=30)
    h = recvall(s, 14 + 16)
    return s, h[:8], h[14:]


def account(addr, who, pw):
    """-> (verdict, socket, sel)"""
    s, magic, defi = hello(addr)
    n = who.encode("utf-8")
    s.sendall(b"ONYXRDP3" + bytes([4, len(n)]) + n)
    it, sl = struct.unpack("<IB", recvall(s, 5))
    salt = recvall(s, sl)
    key = hashlib.pbkdf2_hmac("sha256", pw.encode("utf-8"), salt, it, 32)
    s.sendall(hmac.new(key, defi, hashlib.sha256).digest())
    return recvall(s, 1)[0], s, (it, salt)


def main():
    host, _, port = sys.argv[1].partition(":")
    addr = (host, int(port or 3390))
    who, pw = sys.argv[2], sys.argv[3]

    v, s, _ = account(addr, who, pw + "x")
    check(v == 1, "compte %s, mauvais mot de passe : verdict %d (1 attendu)" % (who, v))
    s.close()

    v, s, salt1 = account(addr, "inconnu", "abc")
    s.close()
    v2, s, salt2 = account(addr, "inconnu", "abc")
    s.close()
    check(v == 1 and v2 == 1, "compte inconnu : verdicts %d %d (1 attendu)" % (v, v2))
    check(salt1 == salt2, "compte inconnu : le meme sel deux fois (rien ne dit qu'il n'existe pas)")

    s, magic, defi = hello(addr)
    s.sendall(b"ONYXRDP2" + bytes([4]) + hmac.new(b"essai", defi, hashlib.sha256).digest())
    v = recvall(s, 1)[0]
    check(v == 1, "secret commun en mode comptes : verdict %d (1 attendu)" % v)
    s.close()

    v, s, _ = account(addr, who, pw)
    check(v == 0, "compte %s, bon mot de passe : verdict %d (0 attendu)" % (who, v))
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
