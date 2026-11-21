"""registry : un registre de caisses « sparse » pour les essais de cargo sous QEMU (docs/CARGO.md), sur le PC.

  python3 -I tools/cargo/test/registry.py fetch <dossier>         copie de quelques caisses de crates.io
  python3 -I tools/cargo/test/registry.py serve <dossier> <port> [hôte]   le sert en HTTP (hôte des
                                                                   archives : 10.0.2.2, l'hôte vu de QEMU)

« fetch » prend, pour chaque caisse de CRATES, la version donnée : sa ligne de l'index de crates.io et son
archive (somme vérifiée par cargo, celle de l'index). « serve » les sert comme crates.io :
  /index/config.json, /index/<chemin de l'index>, /crates/<caisse>/<caisse>-<version>.crate
Vu de QEMU (réseau « user ») : sparse+http://10.0.2.2:<port>/index/
"""
import http.server
import json
import os
import sys
import urllib.request

# des caisses sans dépendance obligatoire (le résolveur de cargo n'a besoin que d'elles), et heck qui les suit
CRATES = [("cfg-if", "1.0.1"), ("itoa", "1.0.15"), ("either", "1.15.0"), ("heck", "0.5.0")]


def index_path(name):
    n = name.lower()
    if len(n) <= 2:
        return "%d/%s" % (len(n), n)
    if len(n) == 3:
        return "3/%s/%s" % (n[0], n)
    return "%s/%s/%s" % (n[0:2], n[2:4], n)


def get(url):
    req = urllib.request.Request(url, headers={"User-Agent": "korin-cargo-test"})
    with urllib.request.urlopen(req, timeout=60) as r:
        return r.read()


def fetch(top):
    for name, vers in CRATES:
        p = index_path(name)
        lines = [l for l in get("https://index.crates.io/" + p).decode().splitlines()
                 if l and json.loads(l)["vers"] == vers]
        if len(lines) != 1:
            sys.exit("registry : %s %s absente de l'index" % (name, vers))
        os.makedirs(os.path.join(top, "index", os.path.dirname(p)), exist_ok=True)
        open(os.path.join(top, "index", p), "w", newline="\n").write(lines[0] + "\n")
        d = os.path.join(top, "crates", name)
        os.makedirs(d, exist_ok=True)
        f = os.path.join(d, "%s-%s.crate" % (name, vers))
        if not os.path.isfile(f):
            open(f, "wb").write(get("https://static.crates.io/crates/%s/%s-%s.crate" % (name, name, vers)))
        print("registry :", name, vers)


def serve(top, port, host="10.0.2.2"):
    cfg = json.dumps({"dl": "http://%s:%d/crates/{crate}/{crate}-{version}.crate" % (host, port),
                      "api": "http://%s:%d" % (host, port)}).encode()
    top = os.path.abspath(top)

    class H(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def log_message(self, fmt, *args):
            sys.stderr.write("registry: " + (fmt % args) + "\n")

        def do_GET(self):
            path = self.path.split("?")[0]
            if path == "/index/config.json":
                body = cfg
            else:
                p = os.path.normpath(os.path.join(top, path.lstrip("/")))
                if not p.startswith(top + os.sep) or not os.path.isfile(p):
                    self.send_error(404)
                    return
                body = open(p, "rb").read()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    http.server.ThreadingHTTPServer(("127.0.0.1", port), H).serve_forever()


if __name__ == "__main__":
    if sys.argv[1:2] == ["fetch"]:
        fetch(sys.argv[2])
    elif sys.argv[1:2] == ["serve"]:
        serve(sys.argv[2], int(sys.argv[3]), *sys.argv[4:5])
    else:
        sys.exit(__doc__)
