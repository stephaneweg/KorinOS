"""claude-acp : le serveur ACP (stdio), et deux commandes pour l'utilisateur.

    claude-acp              l'agent, lancé par une façade (agent --claude) : JSON-RPC sur stdin/stdout
    claude-acp --set-key    range la clé d'API (saisie cachée) dans $CLAUDE_ACP_HOME/.env
    claude-acp --version
"""
import os
import sys
import threading

from . import __version__
from . import agent as A
from .rpc import DEFER, Connection, RpcError

PROTOCOL_VERSION = 1
NO_KEY = -32002


class Server:
    def __init__(self):
        self.conn = Connection()
        self.cfg = A.load_config()
        self.sessions = {}
        self._client = None

    def client(self):
        if self._client is None:
            import anthropic
            # les essais de l'API (429, 5xx, réseau) : le SDK réessaie de lui-même ; ANTHROPIC_BASE_URL est pris
            self._client = anthropic.Anthropic(max_retries=4, timeout=900.0)
        return self._client

    def session(self, params):
        s = self.sessions.get(params.get("sessionId"))
        if s is None:
            raise RpcError(-32602, "séance inconnue : %s" % params.get("sessionId"))
        return s

    def need_key(self):
        if not A.have_key():
            raise RpcError(NO_KEY, "Pas de clé d'API de Claude : /account, ou « claude-acp --set-key » au Terminal "
                           "(elle est rangée dans %s/.env)" % A.home())

    def on_request(self, method, p, id_):
        if method == "initialize":
            return {
                "protocolVersion": PROTOCOL_VERSION,
                "agentCapabilities": {"loadSession": True,
                                      "promptCapabilities": {"image": False, "audio": False, "embeddedContext": False}},
                "authMethods": [],
                "agentInfo": {"name": "claude-acp", "title": "Claude", "version": __version__},
            }
        if method == "authenticate":
            raise RpcError(-32601, "pas de connexion par navigateur : la clé d'API est dans %s/.env" % A.home())
        if method == "session/new":
            self.need_key()
            cwd = p.get("cwd") or os.getcwd()
            s = A.Session(self, A.new_id(), cwd, self.cfg)
            self.sessions[s.id] = s
            A.log("séance %s dans %s, modèle %s" % (s.id, cwd, s.model))
            return s.opened()
        if method == "session/list":
            return {"sessions": A.list_sessions(p.get("cwd"))}
        if method == "session/load":
            self.need_key()
            sid = p.get("sessionId", "")
            if "/" in sid or "\\" in sid or not sid:
                raise RpcError(-32602, "identifiant de séance invalide")
            try:
                s = A.Session.load(self, sid, p.get("cwd"), self.cfg)
            except FileNotFoundError:
                raise RpcError(-32602, "séance introuvable : %s" % sid)
            self.sessions[s.id] = s
            s.replay()
            A.log("séance %s reprise (%d messages)" % (s.id, len(s.messages)))
            r = s.opened()
            del r["sessionId"]
            return r
        if method == "session/set_mode":
            s = self.session(p)
            try:
                s.set_mode(p.get("modeId", ""))
            except ValueError as e:
                raise RpcError(-32602, str(e))
            return {}
        if method == "session/set_config_option":
            s = self.session(p)
            if p.get("configId") != "model":
                raise RpcError(-32602, "option inconnue : %s" % p.get("configId"))
            try:
                s.set_model(p.get("value", ""))
            except ValueError as e:
                raise RpcError(-32602, str(e))
            return {"configOptions": s.config_options()}
        if method == "session/prompt":
            s = self.session(p)
            if s.turn is not None and s.turn.is_alive():
                raise RpcError(-32600, "un tour est déjà en cours")
            s.turn = threading.Thread(target=self.run_turn, args=(s, p.get("prompt") or [], id_), daemon=True)
            s.turn.start()
            return DEFER
        raise RpcError(-32601, "méthode inconnue : %s" % method)

    def run_turn(self, s, blocks, id_):
        import anthropic
        try:
            reason = s.prompt(blocks)
            self.conn.respond(id_, {"stopReason": reason})
        except anthropic.AuthenticationError:
            self.conn.error(id_, NO_KEY, "Clé d'API refusée par Anthropic (/account pour en donner une autre)")
        except anthropic.APIStatusError as e:
            A.log("API : %s %s (requête %s)" % (e.status_code, e.message, getattr(e, "request_id", None)))
            self.conn.error(id_, -32603, "API de Claude : %s %s" % (e.status_code, e.message))
        except anthropic.APIConnectionError as e:
            A.log("réseau :", repr(e))
            self.conn.error(id_, -32603, "Pas de réponse de l'API de Claude (réseau) : %s" % e)
        except Exception as e:
            import traceback
            traceback.print_exc()
            self.conn.error(id_, -32603, "%s: %s" % (type(e).__name__, e))

    def on_notify(self, method, p):
        if method == "session/cancel":
            s = self.sessions.get(p.get("sessionId"))
            if s is not None:
                s.cancel.set()
                proc = s.state.proc
                if proc is not None:
                    from .tools import _kill
                    _kill(proc)


def set_key():
    import getpass
    print("Clé d'API de Claude (console.anthropic.com > API keys ; commence par sk-ant-) : rien ne s'affiche à la "
          "saisie.")
    try:
        key = getpass.getpass("Clé : ").strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return 1
    if not key:
        print("Rien n'a été changé.")
        return 1
    if not key.startswith("sk-ant-"):
        print("Attention : la clé ne commence pas par sk-ant-.")
    h = A.home()
    os.makedirs(h, exist_ok=True)
    p = os.path.join(h, ".env")
    lines = []
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            lines = [l for l in f.read().splitlines() if not l.startswith("ANTHROPIC_API_KEY=")]
    lines.append("ANTHROPIC_API_KEY=" + key)
    fd = os.open(p, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("Clé rangée dans %s." % p)
    return 0


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv[:1] == ["--version"]:
        print("claude-acp", __version__)
        return 0
    if argv[:1] == ["--set-key"]:
        return set_key()
    if argv:
        print(__doc__, file=sys.stderr)
        return 2
    A.load_env()
    A.log("claude-acp %s, Python %s, home %s" % (__version__, sys.version.split()[0], A.home()))
    srv = Server()
    srv.conn.serve(srv.on_request, srv.on_notify)
    return 0
