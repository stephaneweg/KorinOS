"""JSON-RPC 2.0 sur stdio, un message par ligne (le côté agent de l'ACP, en face de libacp).

Le fil principal lit stdin et répartit ; les tours tournent dans leurs propres fils et écrivent par `send` (verrou).
Une requête de l'agent vers la façade (session/request_permission) attend sa réponse dans `request`.
"""
import json
import os
import sys
import threading


class RpcError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code
        self.message = message


class Connection:
    def __init__(self):
        # stdout ne porte que du JSON-RPC : tout print égaré part dans le journal (stderr)
        self._out = os.fdopen(os.dup(sys.stdout.fileno()), "w", encoding="utf-8", newline="\n")
        sys.stdout = sys.stderr
        self._lock = threading.Lock()
        self._next = 1
        self._waiting = {}  # id -> [Event, résultat, erreur]
        self.closed = False

    def send(self, msg):
        line = json.dumps(msg, ensure_ascii=False, separators=(",", ":"))
        with self._lock:
            if self.closed:
                return
            try:
                self._out.write(line + "\n")
                self._out.flush()
            except (BrokenPipeError, OSError):
                self.closed = True

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def respond(self, id_, result):
        self.send({"jsonrpc": "2.0", "id": id_, "result": result})

    def error(self, id_, code, message):
        self.send({"jsonrpc": "2.0", "id": id_, "error": {"code": code, "message": message}})

    def request(self, method, params, cancel=None):
        """Une requête vers la façade ; attend la réponse (ou cancel, un threading.Event) ; rend le résultat."""
        with self._lock:
            id_ = self._next
            self._next += 1
            slot = [threading.Event(), None, None]
            self._waiting[id_] = slot
        self.send({"jsonrpc": "2.0", "id": id_, "method": method, "params": params})
        while not slot[0].wait(0.2):
            if self.closed or (cancel is not None and cancel.is_set()):
                with self._lock:
                    self._waiting.pop(id_, None)
                return None
        if slot[2] is not None:
            raise RpcError(slot[2].get("code", -32603), slot[2].get("message", ""))
        return slot[1]

    def resolve(self, msg):
        with self._lock:
            slot = self._waiting.pop(msg.get("id"), None)
        if slot is None:
            return
        slot[1] = msg.get("result")
        slot[2] = msg.get("error")
        slot[0].set()

    def close(self):
        with self._lock:
            self.closed = True
            for slot in self._waiting.values():
                slot[0].set()
            self._waiting.clear()

    def serve(self, on_request, on_notify):
        """Lit stdin jusqu'à la fin : on_request(method, params, id) rend un résultat ou lève RpcError ; une
        méthode qui répond plus tard (session/prompt) rend l'objet DEFER."""
        stdin = sys.stdin.buffer
        while True:
            raw = stdin.readline()
            if not raw:
                break
            raw = raw.strip()
            if not raw:
                continue
            try:
                msg = json.loads(raw)
            except ValueError:
                print("rpc : ligne qui n'est pas du JSON : %r" % raw[:200], file=sys.stderr)
                continue
            if "method" in msg and "id" in msg:
                id_ = msg["id"]
                try:
                    result = on_request(msg["method"], msg.get("params") or {}, id_)
                except RpcError as e:
                    self.error(id_, e.code, e.message)
                except Exception as e:  # une erreur de l'agent : la dire, ne pas mourir
                    import traceback
                    traceback.print_exc()
                    self.error(id_, -32603, "%s: %s" % (type(e).__name__, e))
                else:
                    if result is not DEFER:
                        self.respond(id_, result)
            elif "method" in msg:
                try:
                    on_notify(msg["method"], msg.get("params") or {})
                except Exception:
                    import traceback
                    traceback.print_exc()
            elif "id" in msg:
                self.resolve(msg)
        self.close()


DEFER = object()
