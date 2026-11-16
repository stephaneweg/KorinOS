"""Une fausse API Messages de Claude, pour essayer claude-acp sans clé ni crédit (tools/claude/acptest.py).

    python3 tools/claude/fakeapi.py [--port 8765] [--log requetes.jsonl]

POST /v1/messages en flux (SSE), comme l'API : pensée résumée, texte, appels d'outils. Ce qu'elle répond dépend du
dernier message « user » :
  - des tool_result : un texte « Résultat : <début du premier résultat> » (fin du tour) ;
  - « !tool NOM {json} » : un appel d'outil (tool_use) avec cette entrée ;
  - « !slow » : un texte lent (20 s, un mot toutes les 0,2 s), pour l'interruption ;
  - « !refuse » : un refus (stop_reason refusal) ;
  - sinon : de la pensée, puis « Écho : <texte> ».
Chaque requête reçue est ajoutée en une ligne JSON à --log (en-têtes anthropic-beta compris), pour les vérifier.
"""
import argparse
import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

LOG = None
N = [0]


def sse(w, event, data):
    w.write(("event: %s\ndata: %s\n\n" % (event, json.dumps(data, ensure_ascii=False))).encode("utf-8"))
    w.flush()


def last_user(messages):
    for m in reversed(messages):
        if m["role"] == "user":
            c = m["content"]
            return [{"type": "text", "text": c}] if isinstance(c, str) else c
    return []


class H(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *a):
        pass

    def do_POST(self):
        n = int(self.headers.get("content-length", 0))
        body = json.loads(self.rfile.read(n))
        # la clé : x-api-key, ou « Authorization: Bearer » (ce que le SDK 2.x envoie)
        auth = self.headers.get("authorization") or ""
        key = self.headers.get("x-api-key") or (auth[7:] if auth.startswith("Bearer ") else "")
        if LOG:
            with open(LOG, "a", encoding="utf-8") as f:
                f.write(json.dumps({"path": self.path, "beta": self.headers.get("anthropic-beta"), "key": key[:10],
                                    "body": body}, ensure_ascii=False) + "\n")
        if key == "sk-ant-bad":
            err = json.dumps({"type": "error", "error": {"type": "authentication_error", "message": "invalid x-api-key"}})
            self.send_response(401)
            self.send_header("content-type", "application/json")
            self.send_header("content-length", str(len(err)))
            self.end_headers()
            self.wfile.write(err.encode())
            return
        self.send_response(200)
        self.send_header("content-type", "text/event-stream")
        self.send_header("cache-control", "no-cache")
        self.send_header("connection", "close")
        self.end_headers()
        self.close_connection = True
        N[0] += 1
        w = self.wfile
        model = body.get("model")
        sse(w, "message_start", {"type": "message_start", "message": {
            "id": "msg_%d" % N[0], "type": "message", "role": "assistant", "model": model, "content": [],
            "stop_reason": None, "stop_sequence": None,
            "usage": {"input_tokens": 1200, "output_tokens": 1, "cache_read_input_tokens": 3000,
                      "cache_creation_input_tokens": 500}}})
        blocks = last_user(body["messages"])
        results = [b for b in blocks if b.get("type") == "tool_result"]
        text = "\n".join(b.get("text", "") for b in blocks if b.get("type") == "text").strip()
        idx = 0
        stop = "end_turn"

        def text_block(t, delay=0.0):
            nonlocal idx
            sse(w, "content_block_start", {"type": "content_block_start", "index": idx,
                                           "content_block": {"type": "text", "text": ""}})
            for word in t.split(" "):
                sse(w, "content_block_delta", {"type": "content_block_delta", "index": idx,
                                               "delta": {"type": "text_delta", "text": word + " "}})
                if delay:
                    time.sleep(delay)
            sse(w, "content_block_stop", {"type": "content_block_stop", "index": idx})
            idx += 1

        def thinking_block(t):
            nonlocal idx
            sse(w, "content_block_start", {"type": "content_block_start", "index": idx,
                                           "content_block": {"type": "thinking", "thinking": "", "signature": ""}})
            sse(w, "content_block_delta", {"type": "content_block_delta", "index": idx,
                                           "delta": {"type": "thinking_delta", "thinking": t}})
            sse(w, "content_block_delta", {"type": "content_block_delta", "index": idx,
                                           "delta": {"type": "signature_delta", "signature": "sig-%d" % N[0]}})
            sse(w, "content_block_stop", {"type": "content_block_stop", "index": idx})
            idx += 1

        try:
            if results:
                c = results[-1].get("content")
                c = c if isinstance(c, str) else json.dumps(c, ensure_ascii=False)
                text_block("Résultat%s : %s" % (" (erreur)" if results[-1].get("is_error") else "", c[:300]))
            elif text.startswith("!tool "):
                _, name, rest = text.split(" ", 2)
                thinking_block("Il faut appeler %s." % name)
                sse(w, "content_block_start", {"type": "content_block_start", "index": idx, "content_block": {
                    "type": "tool_use", "id": "toolu_%d" % N[0], "name": name, "input": {}}})
                # l'entrée en morceaux, comme le flux réel
                for i in range(0, len(rest), 7):
                    sse(w, "content_block_delta", {"type": "content_block_delta", "index": idx,
                                                   "delta": {"type": "input_json_delta", "partial_json": rest[i:i + 7]}})
                sse(w, "content_block_stop", {"type": "content_block_stop", "index": idx})
                idx += 1
                stop = "tool_use"
            elif text.startswith("!slow"):
                text_block(" ".join(["mot"] * 100), delay=0.2)
            elif text.startswith("!refuse"):
                stop = "refusal"
            else:
                thinking_block("L'utilisateur dit : %s" % text[:50])
                text_block("Écho : %s" % text)
            sse(w, "message_delta", {"type": "message_delta", "delta": {"stop_reason": stop, "stop_sequence": None},
                                     "usage": {"output_tokens": 42}})
            sse(w, "message_stop", {"type": "message_stop"})
        except (BrokenPipeError, ConnectionResetError):
            pass


def main():
    global LOG
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--log")
    a = ap.parse_args()
    LOG = a.log
    srv = ThreadingHTTPServer(("127.0.0.1", a.port), H)
    print("fakeapi sur http://127.0.0.1:%d" % a.port, file=sys.stderr, flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
