"""Serveur d'essai de Courier, sur le PC (QEMU le voit en 10.0.2.2) : http sur 18765, https sur 18766.

  python -I tools/dotnet/courier/test/server.py [dossier du certificat]

  /json            du JSON
  /gzip /deflate   du JSON compresse
  /chunked         du texte en morceaux
  /slow?s=3        l'en-tete, puis le corps s secondes plus tard
  /hang            lit la requete et ne repond jamais
  /redirect        302 vers /json
  /cookie          pose le cookie session=abc123
  /echo            renvoie en JSON ce qu'il a recu (methode, chemin, en-tetes, corps)
  /big?n=1000000   n octets
Chaque requete est journalisee sur la sortie.
"""
import gzip
import http.server
import json
import os
import socketserver
import ssl
import subprocess
import sys
import threading
import time
import urllib.parse
import zlib

BODY = json.dumps({"name": "armpi", "ok": True, "items": [{"id": 1}, {"id": 2}], "token": "t0k3n"}).encode()


class H(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *a):
        print("%s %s" % (self.server.label, fmt % a), flush=True)

    def send(self, code, body, ctype="application/json", extra=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        for k, v in extra:
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def handle_any(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        n = int(self.headers.get("Content-Length") or 0)
        data = self.rfile.read(n) if n else b""
        p = u.path
        if p == "/json":
            self.send(200, BODY)
        elif p == "/gzip":
            self.send(200, gzip.compress(BODY), extra=[("Content-Encoding", "gzip")])
        elif p == "/deflate":
            self.send(200, zlib.compress(BODY), extra=[("Content-Encoding", "deflate")])
        elif p == "/chunked":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for s in (b"bonjour ", b"le ", b"monde"):
                self.wfile.write(b"%x\r\n%s\r\n" % (len(s), s))
            self.wfile.write(b"0\r\n\r\n")
        elif p == "/slow":
            s = float(q.get("s", ["3"])[0])
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "5")
            self.end_headers()
            self.wfile.flush()
            time.sleep(s)
            self.wfile.write(b"lent\n")
        elif p == "/hang":
            time.sleep(600)
        elif p == "/redirect":
            self.send(302, b"", extra=[("Location", "/json")])
        elif p == "/cookie":
            self.send(200, b"{}", extra=[("Set-Cookie", "session=abc123; Path=/; Max-Age=3600")])
        elif p == "/echo":
            self.send(200, json.dumps({"method": self.command, "path": self.path, "headers": dict(self.headers.items()),
                                       "body": data.decode("latin-1")}).encode())
        elif p == "/big":
            self.send(200, b"x" * int(q.get("n", ["1000000"])[0]), "application/octet-stream")
        else:
            self.send(404, b'{"error":"not found"}')

    do_GET = do_POST = do_PUT = do_PATCH = do_DELETE = do_HEAD = do_OPTIONS = handle_any


class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True

    def handle_error(self, request, client_address):
        print("%s coupure de %s : %s" % (self.label, client_address, sys.exc_info()[1]), flush=True)


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "build"
    crt, key = os.path.join(d, "courier-test.crt"), os.path.join(d, "courier-test.key")
    if not os.path.exists(crt):
        subprocess.check_call(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "3650",
                               "-keyout", key, "-out", crt, "-subj", "/CN=10.0.2.2",
                               "-addext", "subjectAltName=IP:10.0.2.2,DNS:localhost"])
    a = S(("127.0.0.1", 18765), H)
    a.label = "http "
    b = S(("127.0.0.1", 18766), H)
    b.label = "https"
    c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    c.load_cert_chain(crt, key)
    b.socket = c.wrap_socket(b.socket, server_side=True, do_handshake_on_connect=False)
    threading.Thread(target=b.serve_forever, daemon=True).start()
    for port, ciphers in ((18767, "ECDHE-RSA-AES256-GCM-SHA384"), (18768, "ECDHE-RSA-CHACHA20-POLY1305")):
        o = S(("127.0.0.1", port), H)
        o.label = "tls12"
        c = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        c.maximum_version = ssl.TLSVersion.TLSv1_2
        c.set_ciphers(ciphers)
        c.load_cert_chain(crt, key)
        o.socket = c.wrap_socket(o.socket, server_side=True, do_handshake_on_connect=False)
        threading.Thread(target=o.serve_forever, daemon=True).start()
    print("pret", flush=True)
    a.serve_forever()


main()
