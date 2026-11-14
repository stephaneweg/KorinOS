#!/usr/bin/env python3
"""fakellm : un faux fournisseur de modèles compatible OpenAI, pour essayer les comptes de Vibe (vibe-account,
l'assistant de vibe.app, le choix du modèle) sans joindre aucun vrai fournisseur ni aucune vraie clé.

    python -I tools/vibe/fakellm.py [--port 8099] [--key CLE_D_ESSAI] [--models a,b] [--log FICHIER]
                                    [--tls CERT.pem KEY.pem] [--host 127.0.0.1]

--tls : en HTTPS, avec ce certificat (d'essai : sa clé publique dans un ca-bundle de test, SSL_CERT_FILE sur la carte).

La connexion par le navigateur de Vibe (celle du compte Mistral), imitée pour un [[providers]] « mistral » dont
api_base, browser_auth_api_base_url = http://10.0.2.2:PORT/v1 et browser_auth_base_url = http://10.0.2.2:PORT :
  POST /v1/vibe/sign-in                  un essai : process_id, sign_in_url, poll_url, expires_at = maintenant + TTL
  GET  /signin/<id>                      « le navigateur » : l'utilisateur se connecte, l'essai est accordé
  GET  /v1/vibe/sign-in/<id>             pending, puis completed (exchange_token) ; expired passé expires_at
  POST /v1/vibe/sign-in/<id>/exchange    {"api_key": --key}
--signin-ttl S (600) ; --signin-auto S : l'essai est accordé de lui-même S secondes après sa création (-1 : seulement
par /signin) ; --skew S : l'horloge du faux serveur décalée de S secondes (expires_at et l'en-tête Date) : -3600
imite un Korin qui a une heure d'avance (horloge CMOS à l'heure locale de Windows).

Sous QEMU, la machine invitée joint le PC en 10.0.2.2 : api_base = http://10.0.2.2:8099/v1.
Bibliothèque standard seule ; écoute sur 127.0.0.1 seulement.

  GET  /v1/models              {"data": [{"id": ...}, ...]} (les noms de --models)
  POST /v1/chat/completions    une réponse fixe qui cite le modèle et le dernier message de l'utilisateur ;
                               « stream »: true -> SSE (chat.completion.chunk, puis l'usage, puis [DONE])
Toute requête sans « Authorization: Bearer <--key> » reçoit 401. Le journal (requêtes, modèle, taille) n'écrit
jamais l'en-tête Authorization ; il dit seulement si la clé était la bonne.
"""
import argparse
import email.utils
import http.server
import secrets
import threading
import json
import sys
import time

A = None
ATTEMPTS = {}  # process_id -> {"created": t, "approved": bool}
LOCK = threading.Lock()


def server_now():
    return time.time() + A.skew


def iso(t):
    return time.strftime("%Y-%m-%dT%H:%M:%S", time.gmtime(t)) + "Z"


def log(*args):
    line = time.strftime("%H:%M:%S ") + " ".join(str(x) for x in args)
    print(line, flush=True)
    if A.log:
        with open(A.log, "a", encoding="utf-8") as f:
            f.write(line + "\n")


def last_user(messages):
    for m in reversed(messages or []):
        if m.get("role") == "user":
            c = m.get("content")
            if isinstance(c, list):
                c = " ".join(p.get("text", "") for p in c if isinstance(p, dict))
            return str(c or "")
    return ""


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def date_time_string(self, timestamp=None):  # l'en-tête Date, avec le décalage d'essai
        return email.utils.formatdate(server_now() if timestamp is None else timestamp + A.skew, usegmt=True)

    def signin(self, body):
        """la connexion par le navigateur imitée ; True si le chemin en était"""
        path = self.path.split("?")[0].rstrip("/")
        host = self.headers.get("Host") or "127.0.0.1:%d" % A.port
        if self.command == "POST" and path.endswith("/vibe/sign-in"):
            pid = "fake-" + secrets.token_hex(6)
            with LOCK:
                ATTEMPTS[pid] = {"created": time.time(), "approved": False}
            exp = server_now() + A.signin_ttl
            log("sign-in : essai", pid, "expires_at", iso(exp), "(décalage %+d s)" % A.skew)
            self.send_json(200, {"process_id": pid, "sign_in_url": "http://%s/signin/%s" % (host, pid),
                                 "poll_url": "http://%s/v1/vibe/sign-in/%s" % (host, pid), "expires_at": iso(exp)})
            return True
        if self.command == "GET" and path.startswith("/signin/"):
            pid = path.rsplit("/", 1)[1]
            with LOCK:
                if pid in ATTEMPTS:
                    ATTEMPTS[pid]["approved"] = True
            log("sign-in : le navigateur accorde", pid)
            b = "<html><body>Connecté (faux fournisseur). Vous pouvez revenir à Vibe.</body></html>".encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
            return True
        if "/vibe/sign-in/" in path:
            rest = path.split("/vibe/sign-in/", 1)[1]
            pid = rest.split("/")[0]
            with LOCK:
                at = ATTEMPTS.get(pid)
            if at is None:
                self.send_json(404, {"error": {"message": "unknown attempt"}})
                return True
            if self.command == "POST" and rest.endswith("/exchange"):
                log("sign-in : échange", pid, "-> clé d'essai")
                self.send_json(200, {"api_key": A.key})
                return True
            age = time.time() - at["created"]
            if A.signin_auto >= 0 and age >= A.signin_auto:
                at["approved"] = True
            if age >= A.signin_ttl:
                st = {"status": "expired"}
            elif at["approved"]:
                st = {"status": "completed", "exchange_token": "xchg-" + pid}
            else:
                st = {"status": "pending"}
            log("sign-in : poll", pid, st["status"], "(%.0f s)" % age)
            self.send_json(200, st)
            return True
        return False

    def send_json(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def authorized(self):
        ok = self.headers.get("Authorization", "") == "Bearer " + A.key
        log(self.command, self.path, "clé :", "bonne" if ok else "absente ou fausse")
        if not ok:
            self.send_json(401, {"error": {"message": "Invalid API key (fakellm)", "type": "invalid_request_error",
                                           "code": "invalid_api_key"}})
        return ok

    def do_GET(self):
        if self.signin(b""):
            return
        if not self.authorized():
            return
        if self.path.rstrip("/").endswith("/models"):
            self.send_json(200, {"object": "list", "data": [{"id": m, "object": "model", "owned_by": "fakellm"}
                                                            for m in A.models.split(",")]})
        else:
            self.send_json(404, {"error": {"message": "not found"}})

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n)
        if self.signin(body):
            return
        if not self.authorized():
            return
        if not self.path.rstrip("/").endswith("/chat/completions"):
            self.send_json(404, {"error": {"message": "not found"}})
            return
        try:
            req = json.loads(body or b"{}")
        except ValueError:
            self.send_json(400, {"error": {"message": "bad json"}})
            return
        model = req.get("model", "?")
        q = last_user(req.get("messages"))
        log("  modèle", model, "| messages", len(req.get("messages") or []), "| outils", len(req.get("tools") or []),
            "| stream", bool(req.get("stream")), "| question", repr(q[:60]))
        text = "Réponse du faux modèle %s : j'ai bien reçu « %s »." % (model, q.strip()[:80])
        usage = {"prompt_tokens": max(1, len(body) // 4), "completion_tokens": len(text) // 4,
                 "total_tokens": max(1, len(body) // 4) + len(text) // 4}
        cid = "chatcmpl-fake%d" % int(time.time() * 1000)
        if not req.get("stream"):
            self.send_json(200, {"id": cid, "object": "chat.completion", "created": int(time.time()), "model": model,
                                 "choices": [{"index": 0, "message": {"role": "assistant", "content": text},
                                              "finish_reason": "stop"}], "usage": usage})
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Connection", "close")
        self.end_headers()

        def chunk(delta, finish=None, extra=None):
            c = {"id": cid, "object": "chat.completion.chunk", "created": int(time.time()), "model": model,
                 "choices": [{"index": 0, "delta": delta, "finish_reason": finish}]}
            if extra:
                c.update(extra)
            self.wfile.write(b"data: " + json.dumps(c).encode() + b"\n\n")
            self.wfile.flush()

        chunk({"role": "assistant", "content": ""})
        for i in range(0, len(text), 24):
            chunk({"content": text[i:i + 24]})
        chunk({}, "stop")
        self.wfile.write(b"data: " + json.dumps({"id": cid, "object": "chat.completion.chunk", "created": int(time.time()),
                                                 "model": model, "choices": [], "usage": usage}).encode() + b"\n\n")
        self.wfile.write(b"data: [DONE]\n\n")
        self.wfile.flush()
        self.close_connection = True


def main():
    global A
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--key", default="korin-test-key")
    ap.add_argument("--models", default="fake-model-1,fake-model-2")
    ap.add_argument("--log", default="")
    ap.add_argument("--tls", nargs=2, metavar=("CERT", "KEY"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--signin-ttl", type=float, default=600)
    ap.add_argument("--signin-auto", type=float, default=-1)
    ap.add_argument("--skew", type=float, default=0)
    A = ap.parse_args()
    s = http.server.ThreadingHTTPServer((A.host, A.port), Handler)
    if A.tls:
        import ssl
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(A.tls[0], A.tls[1])
        s.socket = ctx.wrap_socket(s.socket, server_side=True)
    log("fakellm sur %s:%d%s, modèles %s" % (A.host, A.port, " (HTTPS)" if A.tls else "", A.models))
    try:
        s.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
