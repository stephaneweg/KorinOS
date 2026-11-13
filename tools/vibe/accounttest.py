#!/bin/python3
"""accounttest : essai des comptes de fournisseurs et du choix du modèle (docs/PYTHON-PORT.md, « Comptes et choix du
modèle »), sur la carte (ou le PC), contre le faux fournisseur tools/vibe/fakellm.py, jamais un vrai :

    PC :     python -I tools/vibe/fakellm.py --port 8099 --models fake-model-1,fake-model-2
    Korin :  cd /sd0/home/pytest && python3 accounttest.py [--api http://10.0.2.2:8099/v1] [--key CLE_D_ESSAI]

Dans un $VIBE_HOME à part (/sd0/home/pytest/vibe-accounttest : la vraie configuration n'est pas touchée) :
  1. vibe-account add fake --api-base ..., set-key (sur l'entrée standard), models (GET /models du faux), add-model des
     deux modèles, use du premier ; config.toml et .env relus (mode 600, la clé jamais affichée) ;
  2. vibe-acp : initialize, session/new : la séance s'ouvre sur le faux fournisseur (sans compte Mistral), les modèles
     offerts (option « model ») sont ceux de config.toml ;
  3. session/set_config_option model = le second ; session/prompt : la réponse vient du faux modèle, et c'est le second ;
  4. avec --agent : agent (le client terminal) dans un pty, sans --model : la liste des modèles à la nouvelle séance,
     la réponse « 2 », un tour : la réponse vient de fake-model-2 ;
  5. une clé fausse : models répond 2 (401), sans afficher la clé.
OK / ECHEC par essai ; le code de sortie est le nombre d'échecs.
"""
import argparse
import asyncio
import json
import os
import shutil
import stat
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from acp_client import AcpClient, PROTOCOL_VERSION  # noqa: E402

results = []


def ok(name, cond, info=""):
    results.append(("OK" if cond else "ECHEC", name, info))
    print("%-6s %s%s" % ("OK" if cond else "ECHEC", name, (" : " + info) if info else ""), flush=True)
    return cond


def tool(*args, inp=None):
    exe = shutil.which("vibe-account") or "/bin/vibe-account"
    r = subprocess.run([sys.executable, exe, *args], input=inp, capture_output=True, text=True)
    return r.returncode, r.stdout, r.stderr


async def session(api, key, home):
    env = dict(os.environ, VIBE_HOME=home)
    t0 = time.monotonic()
    proc = await asyncio.create_subprocess_exec(
        "vibe-acp", stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE,
        stderr=open("/tmp/accounttest-vibe.log", "w"), env=env, limit=16 * 1024 * 1024)
    c = AcpClient(proc, False)
    try:
        init = await c.request("initialize", {"protocolVersion": PROTOCOL_VERSION, "clientCapabilities": {},
                                              "clientInfo": {"name": "accounttest", "version": "1"}}, timeout=900)
        ok("vibe-acp : initialize", bool(init), "%.0f s" % (time.monotonic() - t0))
        cwd = os.path.join(home, "cwd")
        os.makedirs(cwd, exist_ok=True)
        t1 = time.monotonic()
        new = await c.request("session/new", {"cwd": cwd, "mcpServers": []}, timeout=600)
        sid = new.get("sessionId")
        ok("session/new sur le faux fournisseur (sans compte Mistral)", bool(sid), "%.0f s" % (time.monotonic() - t1))
        opt = next((o for o in new.get("configOptions") or [] if o.get("id") == "model"), {})
        vals = [o.get("value") for o in opt.get("options") or []]
        ok("les modèles de la séance viennent de config.toml", "fake-model-1" in vals and "fake-model-2" in vals,
           "%s, actuel %s" % (vals, opt.get("currentValue")))
        ok("le modèle actuel est celui de vibe-account use", opt.get("currentValue") == "fake-model-1",
           str(opt.get("currentValue")))
        r = await c.request("session/set_config_option", {"sessionId": sid, "configId": "model", "value": "fake-model-2"})
        cur = next((o.get("currentValue") for o in r.get("configOptions") or [] if o.get("id") == "model"), None)
        ok("session/set_config_option model -> fake-model-2", cur == "fake-model-2", str(cur))
        c.turn_text.clear()
        t2 = time.monotonic()
        p = await c.request("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "bonjour Korin"}]},
                            timeout=600)
        text = "".join(c.turn_text)
        ok("session/prompt : la réponse du faux fournisseur", "faux modèle" in text and p.get("stopReason") == "end_turn",
           "%r, %s, %.0f s" % (text[:120], p.get("stopReason"), time.monotonic() - t2))
        ok("la réponse vient du modèle choisi (fake-model-2)", "fake-model-2" in text)
    except Exception as e:  # (le journal de Vibe : /tmp/accounttest-vibe.log)
        ok("séance ACP", False, "%s: %s" % (type(e).__name__, e))
    finally:
        try:
            proc.stdin.close()
            await asyncio.wait_for(proc.wait(), 10)
        except Exception:
            proc.kill()


def agent_pty(home):
    """agent dans un pty : la question du modèle à la nouvelle séance, « 2 », puis un tour"""
    import pty
    import select
    cwd = os.path.join(home, "cwd")
    os.makedirs(cwd, exist_ok=True)
    env = dict(os.environ, VIBE_HOME=home, TERM="xterm")
    pid, fd = pty.fork()
    if pid == 0:
        os.chdir(cwd)
        os.execve("/bin/agent", ["agent"], env)
        os._exit(127)
    out = b""

    def until(marker, timeout):
        nonlocal out
        end = time.monotonic() + timeout
        while marker not in out and time.monotonic() < end:
            r, _, _ = select.select([fd], [], [], 1)
            if r:
                try:
                    d = os.read(fd, 4096)
                except OSError:
                    break
                if not d:
                    break
                out += d
        return marker in out
    t0 = time.monotonic()
    try:
        asked = until("Modèle de cette séance".encode(), 900)
        ok("agent : la liste des modèles à la nouvelle séance", asked and b"fake-model-2" in out,
           "%.0f s" % (time.monotonic() - t0))
        if asked:
            time.sleep(1)
            os.write(fd, b"2\r")
            ok("agent : « 2 » choisit fake-model-2", until("Modèle : fake-model-2".encode(), 120))
            os.write(fd, b"bonjour\r")
            ok("agent : un tour, réponse de fake-model-2", until("faux modèle fake-model-2".encode(), 300))
        os.write(fd, b"\x04")
        until(b"\x00", 10)
    finally:
        try:
            os.kill(pid, 15)
        except OSError:
            pass
        os.waitpid(pid, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--api", default="http://10.0.2.2:8099/v1")
    ap.add_argument("--key", default="korin-test-key")
    ap.add_argument("--no-acp", action="store_true")
    ap.add_argument("--agent", action="store_true", help="aussi agent dans un pty")
    a = ap.parse_args()
    # (sur la carte, pas dans /tmp : RamFS refuse un rename d'un dossier à un autre, que Vibe fait dans logs/)
    home = "/sd0/home/pytest/vibe-accounttest" if os.path.isdir("/sd0/home") else "/tmp/vibe-accounttest"
    shutil.rmtree(home, ignore_errors=True)
    os.makedirs(home)
    os.environ["VIBE_HOME"] = home
    r = tool("add", "fake", "--api-base", a.api)
    ok("vibe-account add fake", r[0] == 0, r[2].strip())
    r = tool("set-key", "fake", inp=a.key + "\n")
    ok("vibe-account set-key (entrée standard)", r[0] == 0 and a.key not in r[1] + r[2], r[1].strip())
    st = os.stat(os.path.join(home, ".env"))
    # (FAT ne garde pas les droits : sous Korin, stat rend 644 quoi qu'on ait demandé ; vibe-account demande 600)
    fat = os.uname().sysname == "ARMPI"
    ok(".env en mode 600" + (" (sans objet sur FAT)" if fat else ""), fat or stat.S_IMODE(st.st_mode) & 0o077 == 0,
       oct(stat.S_IMODE(st.st_mode)))
    r = tool("models", "fake", "--json")
    ids = json.loads(r[1]) if r[0] == 0 else []
    ok("vibe-account models (GET /models du faux)", ids == ["fake-model-1", "fake-model-2"], str(ids or r[2].strip()))
    for m in ("fake-model-1", "fake-model-2"):
        tool("add-model", "fake", m)
    r = tool("use", "fake-model-1")
    import tomllib
    cfg = tomllib.load(open(os.path.join(home, "config.toml"), "rb"))
    ok("config.toml : fournisseur, modèles, active_model",
       cfg.get("active_model") == "fake-model-1" and cfg["providers"][0]["api_base"] == a.api.rstrip("/")
       and len(cfg["models"]) == 2, json.dumps(cfg)[:200])
    r = tool("list", "--json")
    rows = json.loads(r[1]) if r[0] == 0 else []
    ok("vibe-account list : la clé est posée, sans la montrer",
       any(x["name"] == "fake" and x["key"] for x in rows) and a.key not in r[1])
    if not a.no_acp:
        asyncio.run(session(a.api, a.key, home))
    if a.agent:
        tool("use", "fake-model-1")
        agent_pty(home)
    r = tool("set-key", "fake", inp="cle-fausse\n")
    r = tool("models", "fake")
    ok("une clé refusée : code 2, sans la clé", r[0] == 2 and "cle-fausse" not in r[1] + r[2], r[2].strip())
    fails = sum(1 for x in results if x[0] != "OK")
    print("%d OK, %d ECHEC" % (len(results) - fails, fails))
    sys.exit(fails)


if __name__ == "__main__":
    main()
