#!/usr/bin/env python3
"""fakellm : un faux fournisseur de modèles compatible OpenAI, pour essayer les comptes de Vibe (vibe-account,
l'assistant de vibe.app, le choix du modèle) sans joindre aucun vrai fournisseur ni aucune vraie clé.

    python -I tools/vibe/fakellm.py [--port 8099] [--key CLE_D_ESSAI] [--models a,b] [--log FICHIER]

Sous QEMU, la machine invitée joint le PC en 10.0.2.2 : api_base = http://10.0.2.2:8099/v1.
Bibliothèque standard seule ; écoute sur 127.0.0.1 seulement.

  GET  /v1/models              {"data": [{"id": ...}, ...]} (les noms de --models)
  POST /v1/chat/completions    une réponse fixe qui cite le modèle et le dernier message de l'utilisateur ;
                               « stream »: true -> SSE (chat.completion.chunk, puis l'usage, puis [DONE])
Toute requête sans « Authorization: Bearer <--key> » reçoit 401. Le journal (requêtes, modèle, taille) n'écrit
jamais l'en-tête Authorization ; il dit seulement si la clé était la bonne.
"""
import argparse
import http.server
import json
import sys
import time

A = None


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
    A = ap.parse_args()
    s = http.server.ThreadingHTTPServer(("127.0.0.1", A.port), Handler)
    log("fakellm sur 127.0.0.1:%d, modèles %s" % (A.port, A.models))
    try:
        s.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    sys.exit(main())
