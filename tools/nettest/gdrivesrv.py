"""gdrivesrv.py -- un faux serveur de l'API Google Drive v3 pour essayer gdrivefs (docs/etude/GDRIVE.md), en clair
(gdrivefs accepte http:// pour une adresse locale ou privée seulement : api_url dans HOME:/gdrive/drives.ini).

  python -I tools/nettest/gdrivesrv.py [PORT]        (8091 par défaut ; QEMU : http://10.0.2.2:8091)

  GET /drive/v3/files?q='<id>' in parents and trashed=false&fields=...&pageSize=N[&pageToken=...]
                       pages de 3 au plus (pour essayer la pagination) ; les pages suivantes en morceaux (chunked)
  GET /drive/v3/files/<id>?alt=media   avec Range : 206 et Content-Range ; sans Range : 200, le fichier entier ;
                       les blocs d'1 Mo de rang impair répondus en morceaux (chunked)
  GET /drive/v3/about?fields=storageQuota,user
  GET /ctl?fail401=N&fail429=N&fail403=N&add=DOSSIER/NOM&reset=1    pannes et changements à la demande ;
           close=N : connexion fermée après la réponse (sans le dire) ; drop=N : fermée sans répondre
  GET /stats           compteurs (JSON) : requêtes, listes, octets envoyés par fichier, connexions...

Jeton : « Authorization: Bearer at-... » (ceux de tools/nettest/oauthsrv.py, et at-test pour l'essai sous Linux).
Contenu de big.bin (5 Mo et un peu) : l'octet i vaut ((i * 2654435761) mod 2^32) >> 24 XOR ((i >> 11) & 255).
"""
import hashlib
import json
import re
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

BIG_SIZE = 5 * 1024 * 1024 + 12345
PAGE = 3
FOLDER = "application/vnd.google-apps.folder"
LOCK = threading.Lock()


def big_content(n):
    return bytes((((i * 2654435761) & 0xFFFFFFFF) >> 24) ^ ((i >> 11) & 255) for i in range(n))


FILES = []  # (dans l'ordre d'insertion : l'ordre des listes)


def add(fid, name, parent, mime="application/octet-stream", content=None, created="2025-01-01T10:00:00.000Z",
        modified="2025-06-01T12:00:00.000Z", trashed=False):
    FILES.append({"id": fid, "name": name, "parents": [parent], "mimeType": mime, "content": content,
                  "createdTime": created, "modifiedTime": modified, "trashed": trashed})


ROOT = "0AROOTFOLDER"
add("fDocs", "Documents", ROOT, FOLDER)
add("fPhotos", "Photos", ROOT, FOLDER, created="2025-01-01T10:00:00.000Z")
add("fPhotos2", "Photos", ROOT, FOLDER, created="2025-02-01T10:00:00.000Z")
add("fBig", "big.bin", ROOT, content=big_content(BIG_SIZE), modified="2025-07-01T08:30:00.000Z")
add("fEmpty", "empty.txt", ROOT, "text/plain", content=b"")
add("fSlash", "a/b.txt", ROOT, "text/plain", content=b"nom avec une barre\n")
add("gDoc", "Mon document", ROOT, "application/vnd.google-apps.document")
add("gShort", "Raccourci", ROOT, "application/vnd.google-apps.shortcut")
add("gForm", "Formulaire", ROOT, "application/vnd.google-apps.form")
add("fTrash", "jete.txt", ROOT, "text/plain", content=b"a la corbeille\n", trashed=True)
add("fNotes", "notes.txt", "fDocs", "text/plain", content=b"Bonjour depuis le faux Drive.\nDeuxieme ligne.\n")
# trois « rapport.pdf » : listés dans le désordre, nommés par date de création
add("fRap3", "rapport.pdf", "fDocs", "application/pdf", content=b"rapport v3\n", created="2025-05-06T00:00:00.000Z")
add("fRap1", "rapport.pdf", "fDocs", "application/pdf", content=b"rapport v1\n", created="2025-01-02T00:00:00.000Z")
add("fRap2", "rapport.pdf", "fDocs", "application/pdf", content=b"rapport v2\n", created="2025-03-04T00:00:00.000Z")
add("gSheet", "Budget", "fDocs", "application/vnd.google-apps.spreadsheet")
add("fSub", "Sous-dossier", "fDocs", FOLDER)
add("fDeep", "profond.txt", "fSub", "text/plain", content=b"tout au fond\n")
for k in range(1, 8):
    add("fImg%d" % k, "img%02d.jpg" % k, "fPhotos", "image/jpeg", content=bytes([k]) * (1000 * k))
add("fAutre", "autre.jpg", "fPhotos2", "image/jpeg", content=b"\xff\xd8autre")

STATS = {"requests": 0, "list": 0, "media": 0, "media_bytes": 0, "about": 0, "connections": 0, "unauthorized": 0,
         "rate_limited": 0, "chunked": 0, "dropped": 0, "bytes_by_id": {}}
FAIL = {"401": 0, "429": 0, "403": 0, "drop": 0, "close": 0}


def by_id(fid):
    for f in FILES:
        if f["id"] == fid:
            return f
    return None


def meta(f):
    m = {"id": f["id"], "name": f["name"], "mimeType": f["mimeType"], "modifiedTime": f["modifiedTime"],
         "createdTime": f["createdTime"]}
    if f["content"] is not None:
        m["size"] = str(len(f["content"]))
        m["md5Checksum"] = hashlib.md5(f["content"]).hexdigest()
    return m


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    counted = False  # (une connexion comptée à sa première requête à l'API : pas celles de /ctl et /stats)

    def log_message(self, fmt, *a):
        sys.stderr.write("gdrivesrv: " + fmt % a + "\n")

    def send_body(self, code, body, ctype="application/json", chunked=False, headers=()):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        for k, v in headers:
            self.send_header(k, v)
        if chunked:  # (pour essayer le décodage des morceaux)
            with LOCK:
                STATS["chunked"] += 1
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            step = max(1, len(body) // 5 + 1)
            for i in range(0, len(body), step):
                part = body[i:i + step]
                self.wfile.write(b"%x\r\n%s\r\n" % (len(part), part))
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    def send_json(self, code, obj, chunked=False):
        self.send_body(code, json.dumps(obj).encode(), chunked=chunked)

    def error(self, code, reason, message):
        self.send_json(code, {"error": {"code": code, "message": message,
                                        "errors": [{"domain": "global", "reason": reason, "message": message}]}})

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        if u.path == "/stats":
            with LOCK:
                return self.send_json(200, STATS)
        if u.path == "/ctl":
            with LOCK:
                for k in ("401", "429", "403", "drop", "close"):
                    p = k if k in ("drop", "close") else "fail" + k
                    if p in q:
                        FAIL[k] = int(q[p])
                if "add" in q:
                    folder, name = q["add"].rsplit("/", 1)
                    parent = {"": ROOT, "Documents": "fDocs"}.get(folder, ROOT)
                    add("fNew%d" % len(FILES), name, parent, "text/plain", content=b"nouveau\n")
                if q.get("reset"):
                    for k in STATS:
                        STATS[k] = {} if k == "bytes_by_id" else 0
            return self.send_json(200, {"ok": True})
        with LOCK:
            STATS["requests"] += 1
            if not self.counted:
                self.counted = True
                STATS["connections"] += 1
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer at-"):
            with LOCK:
                STATS["unauthorized"] += 1
            return self.error(401, "authError", "Invalid Credentials")
        with LOCK:
            if FAIL["close"] > 0:  # (après cette réponse, la connexion est fermée sans le dire)
                FAIL["close"] -= 1
                self.close_connection = True
            if FAIL["drop"] > 0:  # (fermée sans répondre : le client doit refaire la connexion)
                FAIL["drop"] -= 1
                STATS["dropped"] += 1
                self.close_connection = True
                return
            if FAIL["401"] > 0:
                FAIL["401"] -= 1
                STATS["unauthorized"] += 1
                fail = 401
            elif FAIL["429"] > 0:
                FAIL["429"] -= 1
                STATS["rate_limited"] += 1
                fail = 429
            elif FAIL["403"] > 0:
                FAIL["403"] -= 1
                STATS["rate_limited"] += 1
                fail = 403
            else:
                fail = 0
        if fail == 401:
            return self.error(401, "authError", "Invalid Credentials")
        if fail == 429:
            return self.error(429, "rateLimitExceeded", "Rate Limit Exceeded")
        if fail == 403:
            return self.error(403, "userRateLimitExceeded", "User Rate Limit Exceeded")
        if u.path == "/drive/v3/about":
            with LOCK:
                STATS["about"] += 1
            return self.send_json(200, {"storageQuota": {"limit": "16106127360", "usage": "5368709120",
                                                         "usageInDrive": "5000000000"},
                                        "user": {"kind": "drive#user", "displayName": "Essai",
                                                 "emailAddress": "essai@example.com"}})
        if u.path == "/drive/v3/files":
            return self.files_list(q)
        m = re.fullmatch(r"/drive/v3/files/([^/]+)", u.path)
        if m and q.get("alt") == "media":
            return self.media(urllib.parse.unquote(m.group(1)))
        return self.error(404, "notFound", "not found: " + u.path)

    def files_list(self, q):
        m = re.fullmatch(r"'((?:[^'\\]|\\.)+)' in parents and trashed=false", q.get("q", ""))
        if not m or "files(" not in q.get("fields", ""):
            return self.error(400, "invalid", "q ou fields inattendu : %r %r" % (q.get("q"), q.get("fields")))
        parent = m.group(1).replace("\\'", "'").replace("\\\\", "\\")
        if parent == "root":
            parent = ROOT
        if parent != ROOT and (by_id(parent) is None or by_id(parent)["mimeType"] != FOLDER):
            return self.error(404, "notFound", "File not found: " + parent)
        with LOCK:
            STATS["list"] += 1
            items = [f for f in FILES if parent in f["parents"] and not f["trashed"]]
        size = min(int(q.get("pageSize", "100")), PAGE)
        tok = q.get("pageToken")
        start = int(tok[4:]) if tok and tok.startswith("page") else 0
        page = items[start:start + size]
        out = {"kind": "drive#fileList", "incompleteSearch": False, "files": [meta(f) for f in page]}
        if start + size < len(items):
            out["nextPageToken"] = "page%d" % (start + size)
        self.send_json(200, out, chunked=bool(tok))

    def media(self, fid):
        f = by_id(fid)
        if f is None or f["trashed"]:
            return self.error(404, "notFound", "File not found: " + fid)
        if f["content"] is None:
            return self.error(403, "fileNotDownloadable", "Only files with binary content can be downloaded")
        data = f["content"]
        rng = self.headers.get("Range")
        with LOCK:
            STATS["media"] += 1
        if not rng:
            body, code, hdr = data, 200, []
        else:
            m = re.fullmatch(r"bytes=(\d+)-(\d*)", rng.strip())
            if not m:
                return self.error(400, "badRange", "Range illisible")
            a = int(m.group(1))
            z = int(m.group(2)) if m.group(2) else len(data) - 1
            if a >= len(data):
                return self.send_body(416, b"", headers=[("Content-Range", "bytes */%d" % len(data))])
            z = min(z, len(data) - 1)
            body, code = data[a:z + 1], 206
            hdr = [("Content-Range", "bytes %d-%d/%d" % (a, z, len(data)))]
        with LOCK:
            STATS["media_bytes"] += len(body)
            STATS["bytes_by_id"][fid] = STATS["bytes_by_id"].get(fid, 0) + len(body)
        start = int(re.match(r"bytes=(\d+)", rng).group(1)) if rng else 0
        self.send_body(code, body, "application/octet-stream", chunked=(start >> 20) % 2 == 1, headers=hdr)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8091
    srv = ThreadingHTTPServer(("0.0.0.0", port), H)
    srv.daemon_threads = True
    sys.stderr.write("gdrivesrv: prêt sur le port %d\n" % port)
    sys.stderr.flush()
    srv.serve_forever()
