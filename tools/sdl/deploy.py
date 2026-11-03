"""deploy : envoie un dossier sur le Pi par FTP, chaque fichier verifie par XCRC (docs/SDL.md). Le Wi-Fi coupe : on
se reconnecte et on reprend. Ce qui est verifie va dans un journal, le relancer ne renvoie que le reste. Une erreur
d'ecriture du Pi (carte pleine) arrete tout.
    python -I tools/sdl/deploy.py 192.168.1.20 build/sdl/sdroot/apps/supertux.app SD0:/apps/supertux.app
"""
import ftplib
import os
import socket
import sys
import time
import zlib

host, local, remote = sys.argv[1:4]
done_path = "build/sdl/deploy-%s.done" % os.path.basename(local)
done = set()
if os.path.exists(done_path):
    done = set(open(done_path, encoding="utf-8").read().split("\n"))
journal = open(done_path, "a", encoding="utf-8")
ftp = None
NET = (OSError, EOFError, socket.timeout, ftplib.error_temp, ftplib.error_reply, ftplib.error_proto)


def connect():
    global ftp
    for i in range(40):
        try:
            ftp = ftplib.FTP()
            ftp.connect(host, 21, timeout=120)
            ftp.login()
            return
        except NET as e:
            print("reconnexion %d : %s" % (i, e), flush=True)
            time.sleep(15)
    sys.exit("le Pi ne repond plus")


def mkd(path):
    for _ in range(5):
        try:
            ftp.mkd(path)
            return
        except ftplib.error_perm:
            return                      # il existe deja
        except NET:
            connect()


def send(path, dest):
    crc = "%08X" % (zlib.crc32(open(path, "rb").read()) & 0xFFFFFFFF)
    key = dest + " " + crc
    if key in done:
        return 0
    for _ in range(8):
        try:
            with open(path, "rb") as f:
                ftp.storbinary("STOR " + dest, f, 65536)
            if crc in ftp.sendcmd("XCRC " + dest):
                journal.write(key + "\n")
                journal.flush()
                return os.path.getsize(path)
            print("CRC faux : " + dest, flush=True)
        except ftplib.error_perm as e:
            sys.exit("refus du Pi sur %s : %s" % (dest, e))
        except NET as e:
            print("coupure sur %s : %s" % (dest, e), flush=True)
            connect()
    sys.exit("abandon : " + dest)


connect()
parts = remote.split("/")
for i in range(2, len(parts) + 1):
    mkd("/".join(parts[:i]))
files = sum(len(f) for _, _, f in os.walk(local))
n = sent = 0
t0 = time.time()
for d, dirs, names in os.walk(local):
    rel = os.path.relpath(d, local).replace("\\", "/")
    rd = remote if rel == "." else remote + "/" + rel
    if rel != ".":
        mkd(rd)
    for name in names:
        sent += send(os.path.join(d, name), rd + "/" + name)
        n += 1
        if n % 200 == 0 or n == files:
            print("%d / %d fichiers, %d Mo envoyes, %.0f s" % (n, files, sent >> 20, time.time() - t0), flush=True)
ftp.quit()
print("FINI : %d fichiers verifies, %d Mo envoyes" % (n, sent >> 20), flush=True)
