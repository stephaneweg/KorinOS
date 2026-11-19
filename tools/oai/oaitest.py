"""Essai de bout en bout de mai-acp (oai_acp, profil mai) contre la fausse API (fakeoai.py), sans clé ni crédit.

    python3 tools/oai/oaitest.py                   sur le PC (WSL : n'importe quel Python 3.11 et plus, rien à installer)
    python3 /home/pytest/oaitest.py --installed    sous Korin : /bin/mai-acp et les modules de la carte

Lance la fausse API, puis mai-acp avec un MAI_ACP_HOME neuf, et lui parle ACP comme une façade (le client de
tools/claude/acptest.py) : clé absente, séance, flux et pensée, appels d'outils coupés en morceaux, accords, refus,
deux appels dans un message, bash, web_fetch, réponse sans flux, 429 réessayé, interruption, coût, changement de
modèle, reprise d'une séance, clé refusée. Le code de retour est le nombre d'échecs.
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
for d in (os.path.join(HERE, "..", "claude"), HERE):
    if os.path.isfile(os.path.join(d, "acptest.py")):
        sys.path.insert(0, os.path.abspath(d))
        break
import acptest as CT  # noqa: E402  (le client ACP et check de l'essai de claude-acp)

check = CT.check
FAILS = CT.FAILS
PORT = 8790 + os.getpid() % 1000


class Agent(CT.Agent):
    def __init__(self, env, installed):
        cmd = ["/bin/mai-acp"] if installed else [sys.executable, "-m", "oai_acp"]
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=open(env["LOGF"], "a"),
                                  env=env, cwd=env["WORK"])
        import queue
        self.q = queue.Queue()
        self.next = 100
        self.updates = []
        threading.Thread(target=self._read, daemon=True).start()


def reqs(log):
    if not os.path.exists(log):
        return []
    with open(log, encoding="utf-8") as f:
        return [json.loads(l) for l in f]


def allow(option):
    def on(m):
        if m["method"] == "session/request_permission":
            on.asked.append(m["params"])
            return {"outcome": {"outcome": "selected", "optionId": option}}
        return None
    on.asked = []
    return on


def main():
    installed = "--installed" in sys.argv
    tmp = tempfile.mkdtemp(prefix="mai-acp-test-")
    work = os.path.join(tmp, "work")
    os.makedirs(work)
    log = os.path.join(tmp, "requests.jsonl")
    fake = subprocess.Popen([sys.executable, os.path.join(HERE, "fakeoai.py"), "--port", str(PORT), "--log", log],
                            stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    env = dict(os.environ)
    home = os.path.join(tmp, "home")
    env.update({"MAI_ACP_HOME": home, "WORK": work, "LOGF": os.path.join(tmp, "agent.log")})
    for k in ("MAI_API_KEY", "MAI_BASE_URL"):
        env.pop(k, None)
    if not installed:
        env["PYTHONPATH"] = os.pathsep.join([HERE, os.path.join(HERE, "..", "claude")])
    os.makedirs(os.path.join(home, "skills", "korin-demo"))
    with open(os.path.join(home, "skills", "korin-demo", "SKILL.md"), "w", encoding="utf-8") as f:
        f.write("---\nname: korin-demo\ndescription: une compétence d'essai\n---\n\nContenu de la compétence.\n")
    with open(os.path.join(home, "AGENTS.md"), "w", encoding="utf-8") as f:
        f.write("Consignes de Korin pour l'essai.\n")
    with open(os.path.join(work, "AGENTS.md"), "w", encoding="utf-8") as f:
        f.write("Consignes du projet d'essai.\n")
    try:
        run(env, work, home, log, installed)
    except Exception as e:
        import traceback
        traceback.print_exc()
        FAILS.append("exception : %r" % e)
    finally:
        fake.kill()
    print("\njournal de mai-acp : %s" % env["LOGF"])
    if FAILS:
        print("%d échec(s)" % len(FAILS))
        with open(env["LOGF"], encoding="utf-8", errors="replace") as f:
            print(f.read()[-3000:])
    else:
        print("tout est bon")
        shutil.rmtree(tmp, ignore_errors=True)
    return len(FAILS)


def set_env(home, key):
    with open(os.path.join(home, ".env"), "w") as f:
        f.write("MAI_BASE_URL=http://127.0.0.1:%d/mai/v1/chat/completions\nMAI_API_KEY=%s\n" % (PORT, key))


def turn(a, sid, text, on=None, timeout=60):
    a.updates = []
    return a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": text}]}, on, timeout)


def run(env, work, home, log, installed):
    print("== sans clé")
    a = Agent(env, installed)
    r = a.call("initialize", {"protocolVersion": 1, "clientCapabilities": {}})
    check(r["result"]["agentInfo"]["name"] == "mai-acp", "initialize : agentInfo mai-acp")
    r = a.call("session/new", {"cwd": work, "mcpServers": []})
    check(r.get("error", {}).get("code") == -32002, "session/new sans clé : erreur -32002 (%s)" % r.get("error"))
    a.close()

    set_env(home, "test-key")
    print("== une séance")
    a = Agent(env, installed)
    a.call("initialize", {"protocolVersion": 1, "clientCapabilities": {}})
    r = a.call("session/new", {"cwd": work, "mcpServers": []})["result"]
    sid = r["sessionId"]
    opt = r["configOptions"][0]
    check(opt["currentValue"] == "MAI-Code-1.1-Flash", "modèle par défaut MAI-Code-1.1-Flash")
    check(len(opt["options"]) == 2, "deux modèles offerts")
    check(r["modes"]["currentModeId"] == "ask", "mode ask")

    r = turn(a, sid, "bonjour")
    check(r.get("result", {}).get("stopReason") == "end_turn", "bonjour : end_turn (%s)" % r)
    check("Bonjour de MAI !" in a.texts("agent_message_chunk"), "bonjour : le texte en flux")
    check("Je réfléchis." in a.texts("agent_thought_chunk"), "bonjour : la pensée (reasoning_content)")
    q = reqs(log)[-1]
    b = q["body"]
    check(q["path"] == "/mai/v1/chat/completions", "chemin /mai/v1/chat/completions (%s)" % q["path"])
    check(q["key"] == "test-key", "clé envoyée")
    check(b["messages"][0]["role"] == "system" and "Consignes de Korin pour l'essai" in b["messages"][0]["content"]
          and "Consignes du projet d'essai" in b["messages"][0]["content"], "prompt système : AGENTS.md des deux")
    check("MAI, un modèle de Microsoft" in b["messages"][0]["content"] and "Tu es Claude" not in
          b["messages"][0]["content"], "prompt système : le nom de MAI")
    check("korin-demo" in b["messages"][0]["content"], "prompt système : les compétences")
    names = [t["function"]["name"] for t in b["tools"]]
    check({"read_file", "write_file", "edit_file", "bash", "glob", "grep", "skill", "update_plan", "web_fetch"} <= set(names),
          "les outils en fonctions (%s)" % names)
    check(b.get("max_completion_tokens") == 64000 and b.get("stream") is True, "max_completion_tokens, stream")
    us = [u for u in a.updates if u.get("sessionUpdate") == "usage_update"]
    check(us and us[-1]["used"] == 1050 and us[-1]["size"] == 256000, "usage_update (%s)" % us[-1:])

    print("== un outil, avec accord")
    on = allow("allow")
    r = turn(a, sid, "écris le fichier", on)
    check(r.get("result", {}).get("stopReason") == "end_turn", "écris : end_turn (%s)" % r)
    check(len(on.asked) == 1, "écris : un accord demandé")
    p = os.path.join(work, "hello.txt")
    check(os.path.exists(p) and open(p, encoding="utf-8").read() == "salut é\n", "écris : hello.txt écrit")
    b = reqs(log)[-1]["body"]
    am = [m for m in b["messages"] if m["role"] == "assistant" and m.get("tool_calls")]
    tm = [m for m in b["messages"] if m["role"] == "tool"]
    check(am and am[-1]["tool_calls"][0]["id"] == "call_w1" and json.loads(am[-1]["tool_calls"][0]["function"]["arguments"])
          ["path"] == "hello.txt", "historique : l'appel recollé de ses morceaux")
    check(tm and tm[-1]["tool_call_id"] == "call_w1" and "écrit" in tm[-1]["content"], "historique : le résultat")
    check("fait :" in a.texts("agent_message_chunk"), "écris : la réponse après l'outil")

    print("== refus")
    os.remove(p)
    on = allow("reject")
    r = turn(a, sid, "écris encore", on)
    check(not os.path.exists(p), "refus : rien d'écrit")
    tm = [m for m in reqs(log)[-1]["body"]["messages"] if m["role"] == "tool"]
    check(tm[-1]["content"].startswith("Erreur : ") and "refusé" in tm[-1]["content"], "refus : dit au modèle")

    print("== deux appels, sans accord (lecture)")
    on = allow("allow")
    r = turn(a, sid, "deux lectures", on)
    tm = [m for m in reqs(log)[-1]["body"]["messages"] if m["role"] == "tool"]
    check(not on.asked, "deux : pas d'accord pour lire")
    check([m["tool_call_id"] for m in tm[-2:]] == ["call_r1", "call_g1"] and "Consignes du projet" in tm[-2]["content"],
          "deux : les deux résultats, dans l'ordre")

    print("== bash, toujours permis")
    on = allow("always")
    turn(a, sid, "une commande", on)
    turn(a, sid, "une autre commande", on)
    check(len(on.asked) == 1, "bash : un seul accord pour la séance")
    tm = [m for m in reqs(log)[-1]["body"]["messages"] if m["role"] == "tool"]
    check("korin-ok" in tm[-1]["content"], "bash : la sortie")

    print("== web_fetch")
    turn(a, sid, "lis le web")
    tm = [m for m in reqs(log)[-1]["body"]["messages"] if m["role"] == "tool"]
    check("Titre de la page" in tm[-1]["content"] and "var x" not in tm[-1]["content"], "web_fetch : le texte de la page")

    print("== réponse sans flux, 429")
    turn(a, sid, "json")
    check("réponse sans flux" in a.texts("agent_message_chunk"), "json : réponse sans SSE lue")
    r = turn(a, sid, "retry", timeout=90)
    check(r.get("result", {}).get("stopReason") == "end_turn" and "Bonjour" in a.texts("agent_message_chunk"),
          "429 : réessayé (%s)" % r)

    print("== interruption")
    a.updates = []
    a.next += 1
    tid = a.next
    a.send({"jsonrpc": "2.0", "id": tid, "method": "session/prompt",
            "params": {"sessionId": sid, "prompt": [{"type": "text", "text": "réponds lent"}]}})
    time.sleep(1.2)
    a.notify("session/cancel", {"sessionId": sid})
    t0 = time.time()
    res = None
    while time.time() - t0 < 20 and res is None:
        m = a.q.get(timeout=20)
        if m.get("id") == tid:
            res = m
    check(res and res.get("result", {}).get("stopReason") == "cancelled", "interruption : cancelled (%s)" % res)
    r = turn(a, sid, "bonjour encore")
    check(r.get("result", {}).get("stopReason") == "end_turn", "après l'interruption : la séance continue")
    um = [m for m in reqs(log)[-1]["body"]["messages"] if m["role"] == "user"]
    check("réponds lent" in json.dumps(um[-1], ensure_ascii=False), "la demande interrompue fusionnée avec la suivante")

    print("== coût, modèle")
    turn(a, sid, "/cost")
    check("coût estimé" in a.texts("agent_message_chunk"), "/cost (%s)" % a.texts("agent_message_chunk")[:120])
    r = a.call("session/set_config_option", {"sessionId": sid, "configId": "model", "value": "MAI-Code-2-Flash"})
    check(r["result"]["configOptions"][0]["currentValue"] == "MAI-Code-2-Flash", "modèle changé")
    turn(a, sid, "bonjour")
    check(reqs(log)[-1]["body"]["model"] == "MAI-Code-2-Flash", "la requête prend le nouveau modèle")
    turn(a, sid, "/cost")
    check("coût inconnu" in a.texts("agent_message_chunk"), "/cost : prix inconnu de MAI Code 2 Flash")
    a.close()

    print("== reprise")
    a = Agent(env, installed)
    a.call("initialize", {"protocolVersion": 1, "clientCapabilities": {}})
    r = a.call("session/list", {"cwd": work})
    check(r["result"]["sessions"] and r["result"]["sessions"][0]["sessionId"] == sid, "session/list")
    a.updates = []
    r = a.call("session/load", {"sessionId": sid, "cwd": work, "mcpServers": []})
    check("result" in r and r["result"]["configOptions"][0]["currentValue"] == "MAI-Code-2-Flash", "session/load : modèle")
    check("bonjour" in a.texts("user_message_chunk") and "Bonjour de MAI" in a.texts("agent_message_chunk"),
          "session/load : la conversation rejouée")
    check(any(u.get("toolCallId") == "call_w1" for u in a.updates), "session/load : les appels rejoués")
    r = turn(a, sid, "bonjour après la reprise")
    check(r.get("result", {}).get("stopReason") == "end_turn", "après la reprise : un tour")
    a.close()

    print("== clé refusée")
    set_env(home, "mauvaise")
    a = Agent(env, installed)
    a.call("initialize", {"protocolVersion": 1, "clientCapabilities": {}})
    sid = a.call("session/new", {"cwd": work, "mcpServers": []})["result"]["sessionId"]
    r = turn(a, sid, "bonjour")
    check(r.get("error", {}).get("code") == -32002, "clé refusée : -32002 (%s)" % r.get("error"))
    a.close()


if __name__ == "__main__":
    sys.exit(main())
