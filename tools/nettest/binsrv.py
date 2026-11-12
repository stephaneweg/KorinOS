"""Serveurs d'essai du lot net (sur 127.0.0.1, vus de QEMU en 10.0.2.2) :
  8000 : HTTP (big.bin pour dltest, chemins d'essai de wget)
  9000 : puits (ultest)
  2121 : FTP minimal, passif, état neuf à chaque connexion
  4343 : WHOIS (texte fixe)
Journal : netsrv.log à côté du script.
"""
import os
import socket
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = open(os.path.join(HERE, "netsrv.log"), "a", buffering=1, encoding="utf-8")


def log(*a):
    LOG.write(time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a) + "\n")


BIG = bytes((i * 13 + (i >> 8)) & 255 for i in range(300000))


def serve(port, handler):
    s = socket.socket()
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("127.0.0.1", port))
    s.listen(8)

    def loop():
        while True:
            c, a = s.accept()
            threading.Thread(target=wrap, args=(handler, c, port), daemon=True).start()

    threading.Thread(target=loop, daemon=True).start()


def wrap(h, c, port):
    try:
        h(c)
    except Exception as e:
        log(port, "erreur", repr(e))
    finally:
        try:
            c.close()
        except Exception:
            pass


def read_head(c):
    d = b""
    while b"\r\n\r\n" not in d:
        x = c.recv(4096)
        if not x:
            break
        d += x
    return d


def http(c):
    head = read_head(c).decode("latin-1")
    line = head.split("\r\n")[0]
    log("http", line)
    parts = line.split(" ")
    path = parts[1] if len(parts) > 1 else "/"

    def send(status, body=b"", headers=(), length=True):
        h = "HTTP/1.0 %s\r\n" % status
        for k in headers:
            h += k + "\r\n"
        if length:
            h += "Content-Length: %d\r\n" % len(body)
        h += "Connection: close\r\n\r\n"
        c.sendall(h.encode() + body)

    if path == "/big.bin":
        send("200 OK", BIG, ["Content-Type: application/octet-stream"])
    elif path == "/small":
        send("200 OK", b"bonjour du serveur\r\nligne 2\n", ["Content-Type: text/plain"])
    elif path == "/nolen":
        send("200 OK", b"sans longueur\n" * 3, ["Content-Type: text/plain"], length=False)
    elif path == "/redir":
        send("302 Found", b"", ["Location: /small"])
    elif path == "/redir2":
        send("301 Moved", b"", ["location:   http://10.0.2.2:8000/nolen"])
    elif path == "/loop":
        send("307 Again", b"", ["Location: /loop"])
    elif path == "/tohttps":
        send("302 Found", b"", ["Location: https://10.0.2.2/x"])
    elif path == "/nolocation":
        send("302 Found", b"corps de 302\n", [])
    elif path == "/short":
        c.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: 100\r\n\r\ncourt\n")
    elif path == "/empty":
        pass
    elif path == "/nocode":
        c.sendall(b"HTTP/1.0\r\n\r\nrien\n")
    elif path.startswith("/proxy"):
        # vers le httpd de la carte (QEMU de l'emplacement 6 : hostfwd 127.0.0.1:8109 -> :80)
        p = socket.create_connection(("127.0.0.1", 8109), timeout=10)
        p.sendall(("GET %s HTTP/1.0\r\n\r\n" % (path[6:] or "/")).encode())
        r = b""
        while True:
            x = p.recv(65536)
            if not x:
                break
            r += x
        p.close()
        send("200 OK", r, ["Content-Type: text/plain"])
    elif path.startswith("/echo"):
        send("200 OK", head.encode("latin-1"), ["Content-Type: text/plain"])
    else:
        send("404 Not Found", b"introuvable : " + path.encode("latin-1") + b"\n")


def sink(c):
    n = 0
    while True:
        x = c.recv(65536)
        if not x:
            break
        n += len(x)
    log("puits", n)


def whois(c):
    q = b""
    while b"\n" not in q:
        x = c.recv(1024)
        if not x:
            break
        q += x
    log("whois", q)
    c.sendall(b"% serveur d'essai\r\n\r\ndomain:       " + q.strip() + b"\r\nrefer: whois.exemple.test\r\n\r\nfin\r\n")


def ftp(c):
    files = {"a.txt": b"premier fichier\nligne 2\n", "sub/b.txt": b"dans sub\n", "big.bin": BIG[:40000]}
    dirs = {"", "sub"}
    st = {"cwd": "", "user": None, "ok": False, "pasv": None, "rnfr": None}
    f = c.makefile("rb")

    def out(s):
        c.sendall(s.encode("utf-8") + b"\r\n")

    def full(name):
        if name.startswith("/"):
            p = name[1:]
        else:
            p = (st["cwd"] + "/" + name) if st["cwd"] else name
        return p.strip("/")

    def data_conn():
        p = st["pasv"]
        st["pasv"] = None
        if not p:
            return None
        p.settimeout(10)
        d, _ = p.accept()
        p.close()
        return d

    out("220 Serveur FTP d'essai")
    while True:
        line = f.readline()
        if not line:
            break
        line = line.decode("utf-8").rstrip("\r\n")
        log("ftp", line)
        cmd, _, arg = line.partition(" ")
        cmd = cmd.upper()
        if cmd == "USER":
            st["user"] = arg
            if arg == "bad":
                out("530 Utilisateur refuse")
            elif arg == "direct":
                st["ok"] = True
                out("230 Entrez")
            else:
                out("331 Mot de passe ?")
        elif cmd == "PASS":
            if st["user"] in ("anonymous", "test", "bob") and arg in ("korin@", "secret", "mdp"):
                st["ok"] = True
                out("230-Bienvenue")
                out(" ligne de suite")
                out("230 Connecte")
            else:
                out("530 Mot de passe refuse")
        elif cmd == "QUIT":
            out("221 Au revoir")
            break
        elif not st["ok"]:
            out("530 Identifiez-vous")
        elif cmd == "TYPE":
            out("200 Type " + arg)
        elif cmd == "PWD":
            out('257 "/%s" dossier courant' % st["cwd"])
        elif cmd == "CWD":
            p = full(arg)
            if p in dirs:
                st["cwd"] = p
                out("250 Dossier change")
            else:
                out("550 Pas de dossier")
        elif cmd == "CDUP":
            st["cwd"] = st["cwd"].rpartition("/")[0]
            out("250 Dossier change")
        elif cmd == "PASV":
            p = socket.socket()
            p.bind(("127.0.0.1", 0))
            p.listen(1)
            st["pasv"] = p
            port = p.getsockname()[1]
            out("227 Entering Passive Mode (192,168,9,9,%d,%d)" % (port >> 8, port & 255))
        elif cmd in ("LIST", "NLST"):
            base = full(arg) if arg else st["cwd"]
            names = sorted(set(k[len(base) + 1:].split("/")[0] if base else k.split("/")[0]
                               for k in list(files) + [x for x in dirs if x]
                               if (k.startswith(base + "/") if base else True) and k != base))
            d = data_conn()
            if not d:
                out("425 Pas de connexion")
                continue
            out("150 Liste")
            body = ""
            for n in names:
                p = (base + "/" + n) if base else n
                if cmd == "LIST":
                    body += "%s %8d %s\r\n" % ("d" if p in dirs else "-", len(files.get(p, b"")), n)
                else:
                    body += n + "\r\n"
            d.sendall(body.encode())
            d.close()
            out("226 Fini")
        elif cmd == "RETR":
            p = full(arg)
            d = data_conn()
            if p not in files:
                if d:
                    d.close()
                out("550 Fichier introuvable")
                continue
            out("150 Envoi")
            d.sendall(files[p])
            d.close()
            out("226 Fini")
        elif cmd == "STOR":
            p = full(arg)
            d = data_conn()
            if arg == "refuse.txt":
                if d:
                    d.close()
                out("553 Nom refuse")
                continue
            out("150 Reception")
            buf = b""
            while True:
                x = d.recv(65536)
                if not x:
                    break
                buf += x
            d.close()
            files[p] = buf
            out("226 Recu %d octets" % len(buf))
        elif cmd == "SIZE":
            p = full(arg)
            out("213 %d" % len(files[p]) if p in files else "550 Introuvable")
        elif cmd == "DELE":
            p = full(arg)
            if p in files:
                del files[p]
                out("250 Efface")
            else:
                out("550 Introuvable")
        elif cmd == "MKD":
            dirs.add(full(arg))
            out('257 "%s" cree' % arg)
        elif cmd == "RMD":
            p = full(arg)
            if p in dirs:
                dirs.discard(p)
                out("250 Retire")
            else:
                out("550 Introuvable")
        elif cmd == "RNFR":
            p = full(arg)
            if p in files:
                st["rnfr"] = p
                out("350 Pret")
            else:
                out("550 Introuvable")
        elif cmd == "RNTO":
            files[full(arg)] = files.pop(st["rnfr"])
            out("250 Renomme")
        elif cmd == "HELP":
            out("214-Commandes :")
            out(" USER PASS LIST RETR STOR")
            out("215 autre code")
            out("214 Fin")
        elif cmd == "NOOP":
            out("200 NOOP recu : " + arg)
        else:
            out("502 Commande inconnue")


serve(8000, http)
serve(9000, sink)
serve(2121, ftp)
serve(4343, whois)
log("prêt")
while True:
    time.sleep(3600)
