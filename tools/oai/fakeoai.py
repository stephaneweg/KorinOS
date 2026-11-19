"""Une fausse API chat completions (compatible OpenAI, en flux SSE) pour essayer oai-acp sans clé ni crédit.

    python3 tools/oai/fakeoai.py --port 8790 --log requetes.jsonl

Répond sur /chat/completions et /mai/v1/chat/completions (la clé attendue, en-tête api-key ou Bearer : test-key).
La réponse suit le dernier message : après un résultat d'outil, « fait : <le résultat> » ; sinon selon un mot de
la demande : bonjour, écris, deux, commande, web, json, lent, retry. GET /page rend une page HTML.
"""
import argparse
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG = None
LOCK = threading.Lock()
RETRIED = set()


def chunk(delta=None, finish=None, usage=None, model="m"):
    c = {"id": "chatcmpl-1", "object": "chat.completion.chunk", "model": model, "choices": []}
    if delta is not None or finish:
        c["choices"] = [{"index": 0, "delta": delta or {}, "finish_reason": finish}]
    if usage:
        c["usage"] = usage
    return c


def call(i, cid, name, args):
    """Un appel d'outil, ses arguments coupés en trois morceaux comme le font les vraies API."""
    a = json.dumps(args, ensure_ascii=False)
    n = max(1, len(a) // 3)
    out = [{"tool_calls": [{"index": i, "id": cid, "type": "function", "function": {"name": name, "arguments": ""}}]}]
    for k in range(0, len(a), n):
        out.append({"tool_calls": [{"index": i, "function": {"arguments": a[k:k + n]}}]})
    return out


def text_of(m):
    c = m.get("content")
    if isinstance(c, str):
        return c
    return "".join(p.get("text", "") for p in c or [] if isinstance(p, dict))


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_GET(self):
        body = b"<!doctype html><html><head><title>t</title><script>var x=1;</script></head><body><h1>Titre de la page" \
               b"</h1><p>Le texte de la page.</p></body></html>"
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def fail(self, code, msg):
        b = json.dumps({"error": {"message": msg, "code": str(code)}}).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_POST(self):
        req = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))))
        key = self.headers.get("api-key") or (self.headers.get("Authorization") or "")[7:]
        with LOCK:
            with open(LOG, "a", encoding="utf-8") as f:
                f.write(json.dumps({"path": self.path, "key": key, "body": req}, ensure_ascii=False) + "\n")
        if not self.path.endswith("/chat/completions"):
            return self.fail(404, "Resource not found")
        if key != "test-key":
            return self.fail(401, "Access denied due to invalid subscription key")
        msgs = req["messages"]
        last = msgs[-1]
        users = [m for m in msgs if m["role"] == "user"]
        ask = text_of(users[-1]).lower() if users else ""
        model = req.get("model")
        parts, finish, slow = [], "stop", 0
        if last["role"] == "tool" or (last["role"] == "user" and "images rendues" in text_of(last).lower()):
            tools = [m for m in msgs if m["role"] == "tool"]
            parts = [{"content": "fait : " + text_of(tools[-1])[:200]}]
        elif "retry" in ask and ask not in RETRIED:
            RETRIED.add(ask)
            return self.fail(429, "Too many requests")
        elif "json" in ask:
            b = json.dumps({"id": "x", "object": "chat.completion", "model": model, "choices": [
                {"index": 0, "message": {"role": "assistant", "content": "réponse sans flux"}, "finish_reason": "stop"}],
                "usage": {"prompt_tokens": 10, "completion_tokens": 3}}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(b)))
            self.end_headers()
            self.wfile.write(b)
            return
        elif "écris" in ask:
            parts = [{"content": "J'écris le fichier."}] + call(0, "call_w1", "write_file",
                                                                 {"path": "hello.txt", "content": "salut é\n"})
            finish = "tool_calls"
        elif "deux" in ask:
            parts = call(0, "call_r1", "read_file", {"path": "AGENTS.md"}) + call(1, "call_g1", "glob", {"pattern": "*.md"})
            finish = "tool_calls"
        elif "commande" in ask:
            parts = call(0, "call_b1", "bash", {"command": "echo korin-ok"})
            finish = "tool_calls"
        elif "web" in ask:
            port = self.server.server_address[1]
            parts = call(0, "call_f1", "web_fetch", {"url": "http://127.0.0.1:%d/page" % port})
            finish = "tool_calls"
        elif "lent" in ask:
            parts = [{"content": "mot%d " % i} for i in range(60)]
            slow = 0.1
        else:
            parts = [{"reasoning_content": "Je réfléchis."}, {"content": "Bonjour "}, {"content": "de MAI !"}]
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            for d in [{"role": "assistant"}] + parts:
                self.wfile.write(b"data: " + json.dumps(chunk(d, model=model), ensure_ascii=False).encode() + b"\n\n")
                self.wfile.flush()
                if slow:
                    time.sleep(slow)
            self.wfile.write(b"data: " + json.dumps(chunk(finish=finish, model=model)).encode() + b"\n\n")
            u = {"prompt_tokens": 1000, "completion_tokens": 50, "total_tokens": 1050,
                 "prompt_tokens_details": {"cached_tokens": 400}}
            self.wfile.write(b"data: " + json.dumps(chunk(usage=u, model=model)).encode() + b"\n\n")
            self.wfile.write(b"data: [DONE]\n\n")
            self.wfile.flush()
        except (BrokenPipeError, ConnectionResetError):
            pass
        self.close_connection = True


def main():
    global LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8790)
    ap.add_argument("--log", default="fakeoai.jsonl")
    a = ap.parse_args()
    LOG = a.log
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    print("fausse API sur %d" % a.port, file=sys.stderr, flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
