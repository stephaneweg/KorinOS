#!/bin/python3
"""vibe-diag : un rapport pour comprendre pourquoi Vibe ne démarre pas, ou n'ouvre pas de séance (docs/PYTHON-PORT.md,
« Diagnostic »). Installé en /bin/vibe-diag ; à lancer au Terminal de Korin :

    vibe-diag [--net] [--prompt TEXTE] [--cwd DOSSIER] [--out FICHIER] [--smp-test N]

Écrit au fur et à mesure dans FICHIER (par défaut /vibe-debug.txt : D:\\vibe-debug.txt sur le ZBook) :
  1. le système : machine, cœurs, Python, HOME, VIBE_HOME, le dossier courant ; l'heure : UTC de Korin, décalage
     local, /etc/lastclock ; avec --net, l'heure NTP (pool.ntp.org) et l'en-tête Date du serveur HTTPS du
     fournisseur, et l'écart de Korin à chacune (une connexion par le navigateur expire quand l'écart dépasse son
     délai : horloge CMOS à l'heure locale de Windows) ;
  2. $VIBE_HOME : les fichiers, config.toml, les NOMS des variables de .env (jamais leurs valeurs), vibe-account list ;
  3. les réveils entre fils et process, ce dont asyncio dépend (et que vibe-acp attend à chaque requête) : un fil qui
     écrit dans un tube pendant que l'autre attend dans poll(), asyncio run_in_executor (le réveil de la boucle par sa
     socketpair), asyncio.create_subprocess_exec ; chaque essai a son délai : un réveil perdu est compté, jamais bloquant ;
  4. --net : le réseau vers l'adresse du fournisseur actif (DNS, TCP 443, poignée de main TLS ; rien n'est envoyé au
     fournisseur, aucune clé) ;
  5. vibe-acp avec tout son journal (LOG_LEVEL=DEBUG, messages ACP, PYTHONFAULTHANDLER) : initialize, session/new,
     et --prompt : un tour ; chaque étape chronométrée, avec un délai ; une étape qui n'aboutit pas : SIGABRT à
     vibe-acp, qui écrit alors la pile de tous ses fils (faulthandler) dans son journal ;
  6. la fin des journaux : $VIBE_HOME/logs/server.log (sortie d'erreur de vibe-acp), logs/vibe.log, logs/acp.
Toute valeur de .env et tout « Bearer ... » sont masqués dans le rapport.
"""
import argparse
import json
import os
import platform
import queue
import re
import select
import signal
import subprocess
import sys
import threading
import time

OUT = None
SECRETS = []
T0 = time.monotonic()


def redact(s):
    for v in SECRETS:
        if v and len(v) >= 6:
            s = s.replace(v, "<clé masquée>")
    return re.sub(r"(Bearer\s+)[A-Za-z0-9._\-]{8,}", r"\1<masqué>", s)


def say(*a):
    line = redact(" ".join(str(x) for x in a))
    print(line, flush=True)
    if OUT:
        OUT.write(line + "\n")
        OUT.flush()


def section(t):
    say("\n==== %s (%.1f s) ====" % (t, time.monotonic() - T0))


def ntp_time(host="pool.ntp.org", timeout=5.0):
    """l'heure d'un serveur NTP (SNTP, une requête UDP) -> secondes UTC"""
    import socket
    import struct
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    try:
        s.sendto(b"\x1b" + 47 * b"\0", (host, 123))
        d, _ = s.recvfrom(512)
    finally:
        s.close()
    sec, frac = struct.unpack("!II", d[40:48])
    return sec - 2208988800 + frac / 2 ** 32


def http_date(url, timeout=20.0):
    """l'en-tête Date d'un serveur HTTP(S) (HEAD, sans clé ni corps) -> secondes UTC"""
    import email.utils
    import urllib.request
    req = urllib.request.Request(url, method="HEAD")
    try:
        r = urllib.request.urlopen(req, timeout=timeout)
        d = r.headers.get("Date")
    except urllib.error.HTTPError as e:  # (un 404 a aussi sa date)
        d = e.headers.get("Date")
    return email.utils.parsedate_to_datetime(d).timestamp() if d else None


def clock_report(net, api):
    t = time.time()
    lt = time.localtime(t)
    say("Korin : %s UTC ; heure locale %s (décalage %+d s) ; monotone %.1f s" % (
        time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(t)), time.strftime("%H:%M:%S", lt),
        getattr(lt, "tm_gmtoff", 0) or 0, time.monotonic()))
    try:
        say("/etc/lastclock : %s" % open("/etc/lastclock").read().strip())
    except OSError as e:
        say("/etc/lastclock : %s" % e)
    if not net:
        return
    try:
        n = ntp_time()
        say("NTP pool.ntp.org : %s UTC ; Korin %+.1f s" % (time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(n)), time.time() - n))
    except Exception as e:
        say("NTP : ÉCHEC : %s: %s" % (type(e).__name__, e))
    from urllib.parse import urlsplit
    u = urlsplit(api)
    base = "%s://%s/" % (u.scheme or "https", u.netloc)
    try:
        d = http_date(base)
        say("Date de %s : %s ; Korin %+.1f s" % (base, time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime(d)) if d else "absente",
                                              time.time() - d if d else 0))
    except Exception as e:
        say("Date de %s : ÉCHEC : %s: %s" % (base, type(e).__name__, e))


def active_api(cfg):
    """l'adresse d'API du fournisseur du modèle actif (Mistral par défaut)"""
    api = "https://api.mistral.ai/v1"
    try:
        import tomllib
        d = tomllib.load(open(cfg, "rb")) if os.path.exists(cfg) else {}
        act = d.get("active_model")
        ms = d.get("models") or []
        ms = list(ms.values()) if isinstance(ms, dict) else ms
        prov = next((m.get("provider") for m in ms if (m.get("alias") or m.get("name")) == act), None) or "mistral"
        api = next((p.get("api_base") for p in d.get("providers") or [] if p.get("name") == prov), None) or api
    except Exception as e:
        say("  (config.toml : %s)" % e)
    return api


def vibe_home():
    return os.environ.get("VIBE_HOME") or os.path.join(os.path.expanduser("~"), ".vibe")


def read_env_values(path):
    names = []
    try:
        for line in open(path, encoding="utf-8", errors="replace"):
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            v = v.strip().strip("'\"")
            names.append(k.strip())
            SECRETS.append(v)
    except OSError:
        pass
    return names


def tail(path, n):
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError as e:
        say("  (%s : %s)" % (path, e))
        return
    say("  %s : %d lignes, les %d dernières :" % (path, len(lines), min(n, len(lines))))
    for l in lines[-n:]:
        say("  | " + l.rstrip("\n")[:400])


# ---- 3. les réveils ----

def wake_pipe(n):
    """un fil écrit dans un tube, le fil principal attend dans poll() : n réveils"""
    r, w = os.pipe()
    lost, worst = 0, 0.0
    go = threading.Event()

    def writer():
        for _ in range(n):
            go.wait(5)
            go.clear()
            os.write(w, b"x")
    th = threading.Thread(target=writer, daemon=True)
    th.start()
    p = select.poll()
    p.register(r, select.POLLIN)
    for _ in range(n):
        t = time.monotonic()
        go.set()
        ev = p.poll(3000)
        dt = time.monotonic() - t
        if not ev:
            lost += 1
            continue
        os.read(r, 1)
        worst = max(worst, dt)
    os.close(r)
    os.close(w)
    return lost, worst


def wake_asyncio(n, procs):
    import asyncio

    async def main():
        loop = asyncio.get_running_loop()
        lost, worst = 0, 0.0
        for _ in range(n):
            t = time.monotonic()
            try:
                await asyncio.wait_for(loop.run_in_executor(None, time.sleep, 0), 3)
            except asyncio.TimeoutError:
                lost += 1
            worst = max(worst, time.monotonic() - t)
        plost, pworst = 0, 0.0
        for _ in range(procs):
            t = time.monotonic()
            try:
                pr = await asyncio.create_subprocess_exec(sys.executable, "-c", "print(42)", stdout=asyncio.subprocess.PIPE)
                out, _ = await asyncio.wait_for(pr.communicate(), 30)
                if out.strip() != b"42":
                    plost += 1
            except asyncio.TimeoutError:
                plost += 1
            pworst = max(pworst, time.monotonic() - t)
        return lost, worst, plost, pworst
    return asyncio.run(main())


def with_timeout(fn, seconds, *a):
    box = {}

    def run():
        try:
            box["r"] = fn(*a)
        except Exception as e:  # (dit dans le rapport)
            box["e"] = e
    th = threading.Thread(target=run, daemon=True)
    th.start()
    th.join(seconds)
    if th.is_alive():
        return None, "bloqué plus de %d s" % seconds
    return box.get("r"), box.get("e")


# ---- 5. vibe-acp ----

class Acp:
    def __init__(self, cmd, env, log):
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, env=env,
                                  cwd=env.get("_DIAG_CWD") or None)
        self.q = queue.Queue()
        self.next = 1
        self.text = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.p.stdout:
            try:
                m = json.loads(line)
            except ValueError:
                say("  ! pas du JSON sur la sortie de vibe-acp : %r" % line[:200])
                continue
            if "method" in m and "id" in m:  # une requête de l'agent : refusée (rien n'est exécuté par le diagnostic)
                if m["method"] == "session/request_permission":
                    self._send({"jsonrpc": "2.0", "id": m["id"], "result": {"outcome": {"outcome": "cancelled"}}})
                else:
                    self._send({"jsonrpc": "2.0", "id": m["id"], "error": {"code": -32601, "message": "non"}})
            elif "method" in m:
                u = (m.get("params") or {}).get("update") or {}
                if u.get("sessionUpdate") == "agent_message_chunk":
                    self.text.append((u.get("content") or {}).get("text", ""))
            else:
                self.q.put(m)
        self.q.put(None)

    def _send(self, m):
        self.p.stdin.write((json.dumps(m) + "\n").encode())
        self.p.stdin.flush()

    def request(self, method, params, timeout):
        rid = self.next
        self.next += 1
        self._send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                raise TimeoutError("%s : pas de réponse en %d s" % (method, timeout))
            try:
                m = self.q.get(timeout=left)
            except queue.Empty:
                continue
            if m is None:
                raise EOFError("vibe-acp s'est arrêté (code %s)" % self.p.poll())
            if m.get("id") == rid:
                if "error" in m:
                    raise RuntimeError("%s : erreur %s" % (method, json.dumps(m["error"])[:600]))
                return m.get("result") or {}


def run_acp(a, home):
    logs = os.path.join(home, "logs")
    os.makedirs(logs, exist_ok=True)
    slog = os.path.join(logs, "server.log")
    log = open(slog, "ab")
    log.write(("\n=== vibe-diag %s ===\n" % time.strftime("%Y-%m-%d %H:%M:%S")).encode())
    log.flush()
    env = dict(os.environ, LOG_LEVEL="DEBUG", VIBE_ACP_LOGGING_ENABLED="1", PYTHONFAULTHANDLER="1",
               _DIAG_CWD=a.cwd)
    t = time.monotonic()
    acp = Acp(["vibe-acp"], env, log)
    say("vibe-acp lancé, pid %d ; sa sortie d'erreur : %s" % (acp.p.pid, slog))
    step = "initialize"
    try:
        r = acp.request("initialize", {"protocolVersion": 1, "clientCapabilities": {},
                                       "clientInfo": {"name": "vibe-diag", "version": "1"}}, a.init_timeout)
        say("initialize : %.1f s ; agent %s ; méthodes de connexion %s" % (
            time.monotonic() - t, (r.get("agentInfo") or {}).get("version"), [m.get("id") for m in r.get("authMethods") or []]))
        step = "session/new"
        t = time.monotonic()
        r = acp.request("session/new", {"cwd": a.cwd, "mcpServers": []}, a.new_timeout)
        opt = next((o for o in r.get("configOptions") or [] if o.get("id") == "model"), {})
        say("session/new : %.1f s ; séance %s ; modèle %s ; modèles %s" % (
            time.monotonic() - t, r.get("sessionId"), opt.get("currentValue"), [o.get("value") for o in opt.get("options") or []]))
        if a.prompt:
            step = "session/prompt"
            t = time.monotonic()
            pr = acp.request("session/prompt", {"sessionId": r.get("sessionId"),
                                                "prompt": [{"type": "text", "text": a.prompt}]}, a.prompt_timeout)
            say("session/prompt : %.1f s ; fin %s ; réponse %r" % (time.monotonic() - t, pr.get("stopReason"),
                                                                  "".join(acp.text)[:300]))
        say("RÉSULTAT : vibe-acp répond (dernière étape : %s)" % step)
    except Exception as e:
        say("RÉSULTAT : ÉCHEC à %s après %.1f s : %s" % (step, time.monotonic() - t, e))
        if acp.p.poll() is None:
            say("  SIGABRT à vibe-acp : faulthandler écrit la pile de ses fils dans %s" % slog)
            try:
                os.kill(acp.p.pid, signal.SIGABRT)
            except OSError as k:
                say("  (kill : %s)" % k)
            time.sleep(5)
    finally:
        try:
            acp.p.stdin.close()
        except OSError:
            pass
        try:
            acp.p.wait(5)
        except subprocess.TimeoutExpired:
            acp.p.kill()
        log.close()
    return slog


def main():
    global OUT
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="/vibe-debug.txt")
    ap.add_argument("--cwd", default="/home" if os.path.isdir("/home") else os.getcwd())
    ap.add_argument("--net", action="store_true")
    ap.add_argument("--prompt", default="")
    ap.add_argument("--no-acp", action="store_true")
    ap.add_argument("--init-timeout", type=int, default=300)
    ap.add_argument("--new-timeout", type=int, default=240)
    ap.add_argument("--prompt-timeout", type=int, default=300)
    ap.add_argument("--wakes", type=int, default=500)
    a = ap.parse_args()
    home = vibe_home()
    names = read_env_values(os.path.join(home, ".env"))
    OUT = open(a.out, "w", encoding="utf-8")
    say("vibe-diag : rapport dans %s" % a.out)

    section("1. système")
    u = os.uname()
    say("uname : %s %s %s %s" % (u.sysname, u.release, u.version, u.machine))
    say("cœurs : %s ; Python %s ; exécutable %s" % (os.cpu_count(), platform.python_version(), sys.executable))
    for k in ("HOME", "VIBE_HOME", "PATH", "TMPDIR", "LANG", "TERM"):
        say("%s=%s" % (k, os.environ.get(k, "(absent)")))
    say("~ = %s ; VIBE_HOME pris : %s ; dossier courant %s" % (os.path.expanduser("~"), home, os.getcwd()))

    section("1 bis. l'heure")
    clock_report(a.net, active_api(os.path.join(home, "config.toml")))

    section("2. $VIBE_HOME")
    for d, ds, fs in os.walk(home):
        ds[:] = [x for x in ds if x not in ("session", "acp", "skills", "plugins")]
        for f in fs:
            p = os.path.join(d, f)
            try:
                st = os.stat(p)
                say("  %s  %d octets  %s" % (os.path.relpath(p, home), st.st_size, time.strftime("%Y-%m-%d %H:%M", time.gmtime(st.st_mtime))))
            except OSError as e:
                say("  %s : %s" % (p, e))
    say(".env : variables %s (valeurs jamais montrées)" % (names or "aucune / pas de .env"))
    cfg = os.path.join(home, "config.toml")
    if os.path.exists(cfg):
        say("config.toml :")
        for l in open(cfg, encoding="utf-8", errors="replace"):
            say("  | " + re.sub(r"(?i)((key|token|secret)\w*\s*=\s*).*", r"\1<masqué>", l.rstrip("\n")))
    else:
        say("config.toml : absent (le fournisseur par défaut de Vibe : Mistral)")
    r = subprocess.run([sys.executable, "/bin/vibe-account", "list"], capture_output=True, text=True, timeout=120)
    say("vibe-account list :\n" + (r.stdout + r.stderr).rstrip())

    section("3. réveils entre fils et process")
    res, err = with_timeout(wake_pipe, 120, a.wakes)
    say("tube + poll, %d réveils par un autre fil : %s" % (a.wakes, err or "%d perdus (> 3 s), le plus lent %.3f s" % res))
    res, err = with_timeout(wake_asyncio, 300, a.wakes, 10)
    say("asyncio : %d run_in_executor, 10 sous-process : %s" % (
        a.wakes, err or "%d perdus (> 3 s), le plus lent %.3f s ; sous-process : %d ratés, le plus lent %.2f s" % res))

    if a.net:
        section("4. réseau vers le fournisseur")
        import socket
        import ssl
        api = active_api(cfg)
        from urllib.parse import urlsplit
        host = urlsplit(api).hostname
        port = urlsplit(api).port or 443
        t = time.monotonic()
        try:
            ai = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
            say("DNS %s : %s (%.2f s)" % (host, ai[0][4][0], time.monotonic() - t))
            t = time.monotonic()
            s = socket.create_connection((host, port), timeout=20)
            say("TCP %s:%d : %.2f s" % (host, port, time.monotonic() - t))
            t = time.monotonic()
            ctx = ssl.create_default_context()
            ss = ctx.wrap_socket(s, server_hostname=host)
            say("TLS : %s, %s (%.2f s)" % (ss.version(), ss.cipher()[0], time.monotonic() - t))
            ss.close()
        except Exception as e:
            say("réseau : ÉCHEC après %.2f s : %s: %s" % (time.monotonic() - t, type(e).__name__, e))

    slog = os.path.join(home, "logs", "server.log")
    if not a.no_acp:
        section("5. vibe-acp")
        slog = run_acp(a, home)

    section("6. journaux")
    tail(slog, 300)
    tail(os.path.join(home, "logs", "vibe.log"), 300)
    acpl = os.path.join(os.path.expanduser("~"), ".vibe", "logs", "acp", "messages.jsonl")
    tail(acpl, 40)
    say("\nfini en %.1f s ; rapport : %s" % (time.monotonic() - T0, a.out))


if __name__ == "__main__":
    main()
