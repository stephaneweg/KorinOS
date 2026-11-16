"""gdrivesrv.py -- un faux serveur de l'API Google Drive v3 pour essayer gdrivefs (docs/etude/GDRIVE.md), en clair
(gdrivefs accepte http:// pour une adresse locale ou privée seulement : api_url dans HOME:/gdrive/drives.ini).

  python -I tools/nettest/gdrivesrv.py [PORT]        (8091 par défaut ; QEMU : http://10.0.2.2:8091)

  GET /drive/v3/files?q='<id>' in parents and trashed=false&fields=...&pageSize=N[&pageToken=...]
                       pages de 3 au plus (pour essayer la pagination) ; les pages suivantes en morceaux (chunked)
  GET /drive/v3/files/<id>?alt=media   avec Range : 206 et Content-Range ; sans Range : 200, le fichier entier ;
                       les blocs d'1 Mo de rang impair répondus en morceaux (chunked)
  GET /drive/v3/about?fields=storageQuota,user
  POST /drive/v3/files                 créer (métadonnées JSON : name, mimeType, parents) -> {"id"}
  PATCH /drive/v3/files/<id>[?addParents=..&removeParents=..]   name, trashed, modifiedTime
  PATCH /upload/drive/v3/files/<id>?uploadType=resumable    une session d'envoi (Location)
  PUT <Location>       Content-Range « bytes A-B/T » (un morceau ; 308 et Range tant qu'il en manque) ou
                       « bytes */T » (où en est l'envoi)
  GET /file?id=ID      le contenu d'un fichier (vérifier un envoi) ; GET /find?name=NOM : ses métadonnées
  /ctl?cut=N           les N prochains morceaux d'un envoi gardés à moitié, la connexion coupée sans répondre
  /ctl?cutbody=N       les N prochaines réponses alt=media coupées au milieu du corps (Content-Length complet)
  /ctl?short=N         les N prochaines réponses 206 plus courtes que la plage demandée
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
         "rate_limited": 0, "chunked": 0, "dropped": 0, "bytes_by_id": {}, "creates": 0, "patches": 0,
         "sessions": 0, "puts": 0, "upload_bytes": 0, "cut": 0}
FAIL = {"401": 0, "429": 0, "403": 0, "drop": 0, "close": 0, "cut": 0, "cutbody": 0, "short": 0}
SESS = {}  # upload_id -> {"id", "total", "data"}
SEQ = [0]


def now_iso():
    SEQ[0] += 1  # (une date différente à chaque changement : la version change)
    return "2026-01-01T%02d:%02d:%02d.000Z" % (SEQ[0] // 3600 % 24, SEQ[0] // 60 % 60, SEQ[0] % 60)


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
                for k in ("401", "429", "403", "drop", "close", "cut", "cutbody", "short"):
                    p = k if k in ("drop", "close", "cut", "cutbody", "short") else "fail" + k
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
        if u.path == "/file":
            f = by_id(q.get("id", ""))
            if f is None or f["content"] is None:
                return self.error(404, "notFound", "absent")
            return self.send_body(200, f["content"], "application/octet-stream")
        if u.path == "/find":
            with LOCK:
                fs = [dict(meta(f), parents=f["parents"], trashed=f["trashed"]) for f in FILES
                      if f["name"] == q.get("name")]
            return self.send_json(200, {"files": fs})
        if self.gate():
            return
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

    def gate(self):
        """compteurs, jeton et pannes demandées -> True : déjà répondu (ou coupé)"""
        with LOCK:
            STATS["requests"] += 1
            if not self.counted:
                self.counted = True
                STATS["connections"] += 1
        auth = self.headers.get("Authorization", "")
        if not auth.startswith("Bearer at-"):
            with LOCK:
                STATS["unauthorized"] += 1
            self.error(401, "authError", "Invalid Credentials")
            return True
        with LOCK:
            if FAIL["close"] > 0:  # (après cette réponse, la connexion est fermée sans le dire)
                FAIL["close"] -= 1
                self.close_connection = True
            if FAIL["drop"] > 0:  # (fermée sans répondre : le client doit refaire la connexion)
                FAIL["drop"] -= 1
                STATS["dropped"] += 1
                self.close_connection = True
                return True
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
            self.error(401, "authError", "Invalid Credentials")
        elif fail == 429:
            self.error(429, "rateLimitExceeded", "Rate Limit Exceeded")
        elif fail == 403:
            self.error(403, "userRateLimitExceeded", "User Rate Limit Exceeded")
        return fail != 0

    def body(self):
        n = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(n) if n else b""

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        data = self.body()
        if self.gate():
            return
        if u.path != "/drive/v3/files":
            return self.error(404, "notFound", "not found: " + u.path)
        j = json.loads(data or b"{}")
        parent = (j.get("parents") or ["root"])[0]
        parent = ROOT if parent == "root" else parent
        if parent != ROOT and (by_id(parent) is None or by_id(parent)["mimeType"] != FOLDER):
            return self.error(404, "notFound", "File not found: " + parent)
        with LOCK:
            fid = "fC%d" % len(FILES)
            mime = j.get("mimeType", "application/octet-stream")
            add(fid, j["name"], parent, mime, content=None if mime == FOLDER else b"", created=now_iso(),
                modified=now_iso())
            STATS["creates"] += 1
        self.send_json(200, {"id": fid})

    def do_PATCH(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        data = self.body()
        if self.gate():
            return
        m = re.fullmatch(r"/(upload/)?drive/v3/files/([^/]+)", u.path)
        f = by_id(urllib.parse.unquote(m.group(2))) if m else None
        if f is None or f["trashed"]:
            return self.error(404, "notFound", "File not found")
        if m.group(1):  # (une session d'envoi)
            if q.get("uploadType") != "resumable":
                return self.error(400, "badRequest", "uploadType")
            with LOCK:
                sid = "up%d" % len(SESS)
                SESS[sid] = {"id": f["id"], "total": int(self.headers.get("X-Upload-Content-Length", "-1")),
                             "data": bytearray()}
                STATS["sessions"] += 1
            loc = "http://%s/upload/drive/v3/files/%s?uploadType=resumable&upload_id=%s" % (
                self.headers.get("Host"), f["id"], sid)
            return self.send_body(200, b"", headers=[("Location", loc)])
        j = json.loads(data or b"{}")
        with LOCK:
            STATS["patches"] += 1
            if "name" in j:
                f["name"] = j["name"]
            if j.get("trashed"):
                f["trashed"] = True
            if "modifiedTime" in j:
                f["modifiedTime"] = j["modifiedTime"]
            if q.get("addParents"):  # (« root » : l'alias de la racine, comme chez Google)
                alias = lambda p: ROOT if p == "root" else p
                f["parents"] = [p for p in f["parents"] if p != alias(q.get("removeParents"))] + [alias(q["addParents"])]
        self.send_json(200, {"id": f["id"]})

    def do_PUT(self):
        u = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(u.query))
        data = self.body()
        if self.gate():
            return
        s = SESS.get(q.get("upload_id", ""))
        if s is None:
            return self.error(404, "notFound", "session inconnue")
        cr = self.headers.get("Content-Range", "")
        m = re.fullmatch(r"bytes (\d+)-(\d+)/(\d+)", cr) or re.fullmatch(r"bytes \*/(\d+)", cr)
        if not m:
            return self.error(400, "badRange", "Content-Range : " + cr)
        with LOCK:
            STATS["puts"] += 1
            if len(m.groups()) == 3:
                a, z, total = (int(x) for x in m.groups())
                if a > len(s["data"]) or z - a + 1 != len(data):
                    return self.error(400, "badRange", "morceau hors de place")
                del s["data"][a:]
                if FAIL["cut"] > 0:  # (à moitié reçu, puis la connexion coupée sans répondre)
                    FAIL["cut"] -= 1
                    STATS["cut"] += 1
                    s["data"] += data[:len(data) // 2]
                    self.close_connection = True
                    return
                s["data"] += data
                STATS["upload_bytes"] += len(data)
            else:
                total = int(m.group(1))
            if len(s["data"]) >= total:
                f = by_id(s["id"])
                f["content"] = bytes(s["data"])
                f["modifiedTime"] = now_iso()
                done = True
            else:
                done = False
                hdr = [("Range", "bytes=0-%d" % (len(s["data"]) - 1))] if s["data"] else []
        if done:
            return self.send_json(200, {"id": s["id"]})
        self.send_body(308, b"", headers=hdr)

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
            cutbody = FAIL["cutbody"] > 0 and len(body) > 1
            short = not cutbody and FAIL["short"] > 0 and code == 206 and len(body) > 1
            FAIL["cutbody"] -= cutbody
            FAIL["short"] -= short
            STATS["cutbody"] = STATS.get("cutbody", 0) + cutbody
            STATS["short"] = STATS.get("short", 0) + short
            STATS["media_bytes"] += len(body)
            STATS["bytes_by_id"][fid] = STATS["bytes_by_id"].get(fid, 0) + len(body)
        if cutbody:  # (en-têtes complets, la moitié du corps, puis la connexion coupée)
            self.send_response(code)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(body)))
            for k, v in hdr:
                self.send_header(k, v)
            self.end_headers()
            self.wfile.write(body[:len(body) // 2])
            self.wfile.flush()
            self.close_connection = True
            return
        if short:  # (un 206 juste, mais plus court que la plage demandée)
            body = body[:len(body) // 2]
            hdr = [("Content-Range", "bytes %d-%d/%d" % (a, a + len(body) - 1, len(data)))]
        start = int(re.match(r"bytes=(\d+)", rng).group(1)) if rng else 0
        self.send_body(code, body, "application/octet-stream", chunked=(start >> 20) % 2 == 1, headers=hdr)


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8091
    srv = ThreadingHTTPServer(("0.0.0.0", port), H)
    srv.daemon_threads = True
    sys.stderr.write("gdrivesrv: prêt sur le port %d\n" % port)
    sys.stderr.flush()
    srv.serve_forever()
