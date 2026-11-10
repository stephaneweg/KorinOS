"""Serveur HTTP de depots git pour les essais de git sous QEMU (docs/GIT.md), sur le PC.

  python3 -I tools/git/serve.py <dossier des depots> <port>

Chaque depot nu de <dossier> est servi :
  * en HTTP « intelligent » par `git http-backend` (clone, fetch, push : http.receivepack est
    accorde a tous) ;
  * en HTTP « simple » (fichiers) sous /dumb/<depot>.git/... (clone, fetch ; lancer
    `git update-server-info` dans le depot).
Vu de QEMU (reseau « user ») : http://10.0.2.2:<port>/<depot>.git
"""
import http.server
import os
import subprocess
import sys

RACINE = os.path.abspath(sys.argv[1])
PORT = int(sys.argv[2])


class Gestion(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, fmt, *args):
        sys.stderr.write("serve.py: " + (fmt % args) + "\n")

    def fichier(self, chemin):
        p = os.path.normpath(os.path.join(RACINE, chemin.lstrip("/")))
        if not p.startswith(RACINE) or not os.path.isfile(p):
            self.send_error(404)
            return
        with open(p, "rb") as f:
            data = f.read()
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def backend(self):
        chemin, _, requete = self.path.partition("?")
        if chemin.startswith("/dumb/"):
            if self.command != "GET":
                self.send_error(405)
                return
            self.fichier(chemin[5:])
            return
        n = int(self.headers.get("Content-Length") or 0)
        corps = self.rfile.read(n) if n else b""
        env = dict(os.environ, GIT_PROJECT_ROOT=RACINE, GIT_HTTP_EXPORT_ALL="1",
                   REQUEST_METHOD=self.command, PATH_INFO=chemin, QUERY_STRING=requete,
                   CONTENT_TYPE=self.headers.get("Content-Type", ""),
                   CONTENT_LENGTH=str(len(corps)), REMOTE_USER="essai", REMOTE_ADDR="10.0.2.15",
                   GATEWAY_INTERFACE="CGI/1.1", SERVER_PROTOCOL="HTTP/1.0")
        if self.headers.get("Git-Protocol"):
            env["GIT_PROTOCOL"] = self.headers["Git-Protocol"]
        if self.headers.get("Content-Encoding"):
            env["HTTP_CONTENT_ENCODING"] = self.headers["Content-Encoding"]
        r = subprocess.run(["git", "-c", "http.receivepack=true", "http-backend"], input=corps,
                           env=env, capture_output=True)
        tete, _, data = r.stdout.partition(b"\r\n\r\n")
        statut = 200
        entetes = []
        for ligne in tete.split(b"\r\n"):
            if b":" not in ligne:
                continue
            k, v = ligne.decode("latin-1").split(":", 1)
            if k.lower() == "status":
                statut = int(v.split()[0])
            else:
                entetes.append((k, v.strip()))
        self.send_response(statut)
        for k, v in entetes:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        if r.stderr:
            sys.stderr.write(r.stderr.decode("utf-8", "replace"))

    do_GET = backend
    do_POST = backend


http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Gestion).serve_forever()
