"""Essai du mot de passe de vncd (RFB 3.8, « VNC Authentication », E70 S4).

  python -I tools/remote/vnc_auth_test.py HOTE[:PORT] MOT_DE_PASSE [ok|refus|aucun]

ok : la securite passe (SecurityResult 0) ; refus : SecurityResult 1 ; aucun : le serveur n'offre aucun type (pas de
mot de passe VNC, mode comptes). Code de sortie : 0 si le resultat est celui attendu.
"""
import socket
import struct
import sys
import warnings

warnings.simplefilter("ignore")
from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES  # noqa: E402
from cryptography.hazmat.primitives.ciphers import Cipher, modes  # noqa: E402


def recvall(s, n):
    b = b""
    while len(b) < n:
        d = s.recv(n - len(b))
        if not d:
            raise EOFError("connexion fermee")
        b += d
    return b


def vnc_key(pw):
    k = bytearray(pw.encode("utf-8")[:8].ljust(8, b"\0"))
    return bytes(int("{:08b}".format(c)[::-1], 2) for c in k)  # bits de chaque octet inverses


def main():
    host, _, port = sys.argv[1].partition(":")
    pw, want = sys.argv[2], (sys.argv[3] if len(sys.argv) > 3 else "ok")
    s = socket.create_connection((host, int(port or 5900)), timeout=20)
    recvall(s, 12)
    s.sendall(b"RFB 003.008\n")
    n = recvall(s, 1)[0]
    if n == 0:
        l = struct.unpack(">I", recvall(s, 4))[0]
        got = "aucun (%s)" % recvall(s, l).decode("utf-8", "replace")
    else:
        types = recvall(s, n)
        s.sendall(b"\x02")
        chal = recvall(s, 16)
        k = vnc_key(pw)
        e = Cipher(TripleDES(k * 3), modes.ECB()).encryptor()
        s.sendall(e.update(chal) + e.finalize())
        r = struct.unpack(">I", recvall(s, 4))[0]
        got = "ok" if r == 0 else "refus"
    print("vnc, mot de passe %r : %s (attendu : %s)" % (pw, got, want))
    sys.exit(0 if got.split()[0] == want else 1)


main()
