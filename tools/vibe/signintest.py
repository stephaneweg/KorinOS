#!/bin/python3
"""signintest : essai de la connexion au compte par le navigateur (browser-auth-delegated) de Vibe, par agent dans un
pty, contre le faux fournisseur tools/vibe/fakellm.py (jamais le vrai Mistral) ; docs/PYTHON-PORT.md « Diagnostic ».

    PC :    python -I tools/vibe/fakellm.py --port 8121 --signin-auto 6 --skew -3600    (A : accordé, horloge décalée)
            python -I tools/vibe/fakellm.py --port 8122 --signin-ttl 8                   (B : jamais accordé)
    Korin : cd /sd0/home/pytest && python3 signintest.py [--a http://10.0.2.2:8121] [--b http://10.0.2.2:8122]

Chaque scénario a son $VIBE_HOME (sous /sd0/home/pytest, pas /tmp : RamFS) avec un [[providers]] « mistral » qui vise
le faux serveur, sans télémétrie ni GrowthBook, et sans clé : session/new dit « Missing API key », agent lance la
connexion.
  A. le serveur a une heure de retard sur Korin (--skew -3600 : l'horloge CMOS à l'heure locale de Windows) et
     accorde l'essai après 6 s : « Connecté », la clé rangée (.env : MISTRAL_API_KEY), la séance s'ouvre ; vibe.log
     dit le délai recalculé (remaining ~600 s) ;
  B. l'essai expire (TTL 8 s) : « Connexion non faite », sans boucle sur « Unknown browser sign-in attempt » ; /login
     redonne un lien neuf.
"""
import argparse
import os
import pty
import re
import select
import shutil
import sys
import time

results = []


def ok(name, cond, info=""):
    results.append(cond)
    print("%-6s %s%s" % ("OK" if cond else "ECHEC", name, (" : " + info) if info else ""), flush=True)


CONFIG = """enable_telemetry = false

[experiments]
enable = false

[[providers]]
name = "mistral"
api_base = "{u}/v1"
api_key_env_var = "MISTRAL_API_KEY"
backend = "mistral"
browser_auth_base_url = "{u}"
browser_auth_api_base_url = "{u}/v1"
"""


class Agent:
    def __init__(self, home):
        cwd = os.path.join(home, "cwd")
        os.makedirs(cwd, exist_ok=True)
        env = dict(os.environ, VIBE_HOME=home, TERM="xterm")
        env.pop("MISTRAL_API_KEY", None)
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(cwd)
            os.execve("/bin/agent", ["agent"], env)
            os._exit(127)
        self.out = b""

    def until(self, pattern, timeout):
        end = time.monotonic() + timeout
        rx = re.compile(pattern.encode() if isinstance(pattern, str) else pattern)
        while not rx.search(self.out) and time.monotonic() < end:
            r, _, _ = select.select([self.fd], [], [], 1)
            if r:
                try:
                    d = os.read(self.fd, 4096)
                except OSError:
                    break
                if not d:
                    break
                self.out += d
        return rx.search(self.out) is not None

    def send(self, b):
        os.write(self.fd, b)

    def close(self):
        try:
            self.send(b"\x04")
            self.until(b"\x00", 5)
            os.kill(self.pid, 15)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except OSError:
            pass


def home_for(name, url):
    h = "/sd0/home/pytest/signin-" + name
    shutil.rmtree(h, ignore_errors=True)
    os.makedirs(h)
    open(os.path.join(h, "config.toml"), "w").write(CONFIG.format(u=url.rstrip("/")))
    return h


def env_names(h):
    try:
        return [l.split("=", 1)[0].strip() for l in open(os.path.join(h, ".env")) if "=" in l]
    except OSError:
        return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--a", default="http://10.0.2.2:8121")
    ap.add_argument("--b", default="http://10.0.2.2:8122")
    a = ap.parse_args()

    h = home_for("a", a.a)
    t0 = time.monotonic()
    ag = Agent(h)
    try:
        ok("A : le lien de connexion (Missing API key -> authenticate start)", ag.until(a.a + "/signin/", 900),
           "%.0f s" % (time.monotonic() - t0))
        ok("A : « Connecté » malgré une heure d'écart entre les horloges", ag.until("Connecté".encode(), 180),
           "%.0f s" % (time.monotonic() - t0))
        ok("A : la séance s'ouvre", ag.until("Séance [0-9a-f-]+ ouverte".encode(), 600), "%.0f s" % (time.monotonic() - t0))
        ok("A : la clé rangée par Vibe (.env : MISTRAL_API_KEY)", "MISTRAL_API_KEY" in env_names(h), str(env_names(h)))
    finally:
        ag.close()
    log = open(os.path.join(h, "logs", "vibe.log"), errors="replace").read() if os.path.exists(os.path.join(h, "logs", "vibe.log")) else ""
    m = re.search(r"Browser sign-in started:[^\n]*remaining=(-?\d+)s", log)
    ok("A : vibe.log : délai recalculé sur l'horloge de Korin (~600 s)", bool(m) and 500 < int(m.group(1)) <= 600,
       m.group(0)[:300] if m else "(pas de ligne Browser sign-in started)")
    ok("A : vibe.log : les polls", "Browser sign-in poll: status=pending" in log and "status=completed" in log)

    h = home_for("b", a.b)
    t0 = time.monotonic()
    ag = Agent(h)
    try:
        ok("B : le lien de connexion", ag.until(a.b + "/signin/", 900), "%.0f s" % (time.monotonic() - t0))
        failed = ag.until("Connexion non faite".encode(), 180)
        ok("B : l'essai expiré est dit (« Connexion non faite »)", failed, "%.0f s" % (time.monotonic() - t0))
        n1 = ag.out.count("Connexion non faite".encode())
        ag.until(b"\x00", 25)
        ok("B : pas de boucle (une seule erreur en 25 s)", ag.out.count("Connexion non faite".encode()) == n1 == 1,
           "%d" % ag.out.count("Connexion non faite".encode()))
        links = ag.out.count((a.b + "/signin/").encode())
        ag.send(b"/login\r")
        got = ag.until(("(?s)" + re.escape(a.b + "/signin/") + ".*" + re.escape(a.b + "/signin/")).encode(), 120)
        ok("B : /login redonne un lien neuf", got and ag.out.count((a.b + "/signin/").encode()) > links)
    finally:
        ag.close()
    slog = open(os.path.join(h, "logs", "server.log"), errors="replace").read()
    ok("B : server.log : les lignes de libacp (essai, expiresAt, heure du système, erreur)",
       "connexion par le navigateur : essai" in slog and "authenticate complete : erreur" in slog)
    print("%d OK, %d ECHEC" % (sum(results), len(results) - sum(results)))
    sys.exit(len(results) - sum(results))


if __name__ == "__main__":
    main()
