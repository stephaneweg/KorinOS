"""oai-acp : le serveur ACP (stdio) de claude_acp, sur une API compatible OpenAI ; et ses commandes.

    mai-acp                       l'agent (profil mai), lancé par une façade (agent --mai, l'app MAI)
    mai-acp --set-key             range le point d'accès et la clé (saisie cachée) dans $MAI_ACP_HOME/.env
    mai-acp --version
    python3 -m oai_acp --profile openai [...]     un autre profil (agent.PROFILES)
"""
import os
import socket
import sys
import urllib.error

from claude_acp import main as CM
from claude_acp.rpc import Connection, RpcError

from . import __version__
from . import agent as A

NO_KEY = CM.NO_KEY


class Server(CM.Server):
    SESSION = A.Session

    def __init__(self):
        self.conn = Connection()
        self.cfg = A.load_config()
        self.sessions = {}
        self.INFO = {"name": A.P["name"], "title": A.P["title"], "version": __version__}

    def need_key(self):
        if not A.have_key(self.cfg):
            what = "le point d'accès" if not self.cfg.get("base_url") else "la clé d'API"
            raise RpcError(NO_KEY, "Il manque %s de %s : /account, ou « %s --set-key » au Terminal (rangés dans %s/.env)"
                           % (what, A.P["title"], A.P["name"], A.home()))

    def on_request(self, method, p, id_):
        if method == "authenticate":
            raise RpcError(-32601, "pas de connexion par navigateur : la clé d'API est dans %s/.env" % A.home())
        return super().on_request(method, p, id_)

    def run_turn(self, s, blocks, id_):
        try:
            reason = s.prompt(blocks)
            self.conn.respond(id_, {"stopReason": reason})
        except A.KeyRefused as e:
            self.conn.error(id_, NO_KEY, "Clé d'API refusée (%s) : /account pour en donner une autre" % e)
        except A.ApiError as e:
            hint = ""
            if e.status == 404:
                hint = " (le point d'accès %s ou le nom du déploiement %s est-il juste ?)" % (s.cfg["base_url"], s.model)
            self.conn.error(id_, -32603, "API de %s : %s%s" % (A.P["title"], e, hint))
        except (urllib.error.URLError, socket.timeout, ConnectionError) as e:
            A.log("réseau :", repr(e))
            self.conn.error(id_, -32603, "Pas de réponse de l'API de %s (réseau) : %s" % (
                A.P["title"], getattr(e, "reason", e)))
        except Exception as e:
            import traceback
            traceback.print_exc()
            self.conn.error(id_, -32603, "%s: %s" % (type(e).__name__, e))


def _write_env(updates):
    h = A.home()
    os.makedirs(h, exist_ok=True)
    p = os.path.join(h, ".env")
    lines = []
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            lines = [l for l in f.read().splitlines() if l.split("=", 1)[0].strip() not in updates]
    lines += ["%s=%s" % kv for kv in updates.items()]
    fd = os.open(p, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return p


def set_key():
    import getpass
    A.load_env()
    cur = os.environ.get(A.P["url_env"]) or A.load_config().get("base_url") or ""
    print("Le point d'accès de l'API de %s%s." % (A.P["title"], (" (%s)" % A.P["console"]) if A.P["console"] else ""))
    if A.P["name"] == "mai-acp":
        print("Microsoft Foundry : Modèles + points de terminaison > le déploiement > « Cible » ou « Endpoint », par "
              "exemple https://<ressource>.services.ai.azure.com (le chemin /mai/v1 est ajouté).")
    try:
        url = input("Point d'accès%s : " % (" [%s]" % cur if cur else "")).strip() or cur
        key = getpass.getpass("Clé d'API (rien ne s'affiche) : ").strip()
    except (EOFError, KeyboardInterrupt):
        print()
        return 1
    if not url or not key:
        print("Rien n'a été changé.")
        return 1
    url = A.norm_url(url)
    p = _write_env({A.P["url_env"]: url, A.P["key_env"]: key})
    print("Point d'accès %s et clé rangés dans %s." % (url, p))
    return 0


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    profile = "mai"
    if argv[:1] == ["--profile"] and len(argv) > 1:
        profile = argv[1]
        argv = argv[2:]
    A.use_profile(profile)
    if argv[:1] == ["--version"]:
        print(A.P["name"], __version__)
        return 0
    if argv[:1] == ["--set-key"]:
        return set_key()
    if argv:
        print(__doc__, file=sys.stderr)
        return 2
    A.load_env()
    A.log("%s %s, Python %s, home %s" % (A.P["name"], __version__, sys.version.split()[0], A.home()))
    srv = Server()
    A.log("point d'accès %s, modèle %s" % (srv.cfg["base_url"] or "(aucun)", srv.cfg["model"]))
    srv.conn.serve(srv.on_request, srv.on_notify)
    return 0
