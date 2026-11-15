"""Essai de bout en bout de claude-acp contre la fausse API (fakeapi.py), sans clé ni crédit.

    python3 tools/claude/acptest.py                  sur le PC (WSL : le SDK anthropic installé dans ce Python)
    python3 /tools/claude/acptest.py --installed sous Korin : /bin/claude-acp et le SDK de la carte

Lance la fausse API, puis claude-acp (python -m claude_acp, ou /bin/claude-acp avec --installed) avec un
CLAUDE_ACP_HOME neuf, et lui parle ACP comme une façade : séance, tours, outils, accords, interruption, coût,
reprise d'une séance. Chaque vérification est écrite ; le code de retour est le nombre d'échecs.
"""
import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 8765 + os.getpid() % 1000
FAILS = []


def check(cond, what):
    print(("  ok    " if cond else "  ÉCHEC ") + what, flush=True)
    if not cond:
        FAILS.append(what)


class Agent:
    def __init__(self, env, installed):
        cmd = ["/bin/claude-acp"] if installed else [sys.executable, "-m", "claude_acp"]
        self.p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=open(env["LOGF"], "a"),
                                  env=env, cwd=env["WORK"])
        self.q = queue.Queue()
        self.next = 100
        self.updates = []
        threading.Thread(target=self._read, daemon=True).start()

    def _read(self):
        for line in self.p.stdout:
            self.q.put(json.loads(line))
        self.q.put(None)

    def send(self, msg):
        self.p.stdin.write((json.dumps(msg) + "\n").encode())
        self.p.stdin.flush()

    def call(self, method, params, on_request=None, timeout=60):
        """Une requête ; les notifications sont gardées dans updates ; on_request(msg) répond aux requêtes."""
        self.next += 1
        id_ = self.next
        self.send({"jsonrpc": "2.0", "id": id_, "method": method, "params": params})
        t0 = time.time()
        while time.time() - t0 < timeout:
            try:
                m = self.q.get(timeout=0.5)
            except queue.Empty:
                continue
            if m is None:
                raise EOFError("claude-acp s'est arrêté")
            if "method" in m and "id" in m:
                r = on_request(m) if on_request else None
                if r is not None:
                    self.send({"jsonrpc": "2.0", "id": m["id"], "result": r})
            elif "method" in m:
                self.updates.append(m["params"]["update"])
            elif m.get("id") == id_:
                return m
        raise TimeoutError(method)

    def notify(self, method, params):
        self.send({"jsonrpc": "2.0", "method": method, "params": params})

    def texts(self, kind):
        return "".join(u["content"]["text"] for u in self.updates if u.get("sessionUpdate") == kind)

    def close(self):
        self.p.stdin.close()
        self.p.wait(10)


def requests(logf):
    if not os.path.exists(logf):
        return []
    with open(logf, encoding="utf-8") as f:
        return [json.loads(l) for l in f]


def main():
    installed = "--installed" in sys.argv
    tmp = tempfile.mkdtemp(prefix="claude-acp-test-")
    work = os.path.join(tmp, "work")
    os.makedirs(work)
    reqlog = os.path.join(tmp, "requests.jsonl")
    fake = subprocess.Popen([sys.executable, os.path.join(HERE, "fakeapi.py"), "--port", str(PORT), "--log", reqlog],
                            stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    env = dict(os.environ)
    env.update({"CLAUDE_ACP_HOME": os.path.join(tmp, "home"), "ANTHROPIC_BASE_URL": "http://127.0.0.1:%d" % PORT,
                "WORK": work, "LOGF": os.path.join(tmp, "agent.log")})
    env.pop("ANTHROPIC_API_KEY", None)
    env.pop("ANTHROPIC_AUTH_TOKEN", None)
    if not installed:
        env["PYTHONPATH"] = HERE
    os.makedirs(os.path.join(tmp, "home", "skills", "korin-demo"))
    with open(os.path.join(tmp, "home", "skills", "korin-demo", "SKILL.md"), "w", encoding="utf-8") as f:
        f.write("---\nname: korin-demo\ndescription: une compétence d'essai\n---\n\nContenu de la compétence.\n")
    with open(os.path.join(tmp, "home", "AGENTS.md"), "w", encoding="utf-8") as f:
        f.write("Consignes de Korin pour l'essai.\n")
    with open(os.path.join(work, "AGENTS.md"), "w", encoding="utf-8") as f:
        f.write("Consignes du projet d'essai.\n")
    try:
        run(env, work, reqlog, installed)
    except Exception as e:
        import traceback
        traceback.print_exc()
        FAILS.append("exception : %r" % e)
    finally:
        fake.kill()
    print("\njournal de claude-acp : %s" % env["LOGF"])
    if FAILS:
        print("%d échec(s)" % len(FAILS))
        with open(env["LOGF"], encoding="utf-8", errors="replace") as f:
            print(f.read()[-3000:])
    else:
        print("tout est bon")
        shutil.rmtree(tmp, ignore_errors=True)
    return len(FAILS)


def run(env, work, reqlog, installed):
    print("== sans clé")
    a = Agent(env, installed)
    r = a.call("initialize", {"protocolVersion": 1, "clientCapabilities": {}})
    check(r["result"]["agentInfo"]["name"] == "claude-acp", "initialize : agentInfo")
    r = a.call("session/new", {"cwd": work, "mcpServers": []})
    check(r.get("error", {}).get("code") == -32002, "session/new sans clé : erreur -32002 (%s)" % r.get("error"))
    a.close()

    os.makedirs(env["CLAUDE_ACP_HOME"], exist_ok=True)
    with open(os.path.join(env["CLAUDE_ACP_HOME"], ".env"), "w") as f:
        f.write("ANTHROPIC_API_KEY=sk-ant-test\n")

    print("== une séance")
    a = Agent(env, installed)
    a.call("initialize", {"protocolVersion": 1, "clientCapabilities": {}})
    r = a.call("session/new", {"cwd": work, "mcpServers": []})["result"]
    sid = r["sessionId"]
    check(r["modes"]["currentModeId"] == "ask", "mode ask au départ")
    check(r["configOptions"][0]["currentValue"] == "claude-opus-5-5", "modèle par défaut : claude-opus-5-5")

    r = a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "bonjour"}]})
    check(r["result"]["stopReason"] == "end_turn", "tour simple : end_turn")
    check("Écho : bonjour" in a.texts("agent_message_chunk"), "texte du modèle en flux")
    check("L'utilisateur dit" in a.texts("agent_thought_chunk"), "pensée résumée en flux")
    check(any(u.get("sessionUpdate") == "usage_update" and u["used"] > 0 for u in a.updates), "usage_update")
    req = requests(reqlog)[-1]
    b = req["body"]
    check("server-side-fallback-2026-07-01" in (req["beta"] or "") and b.get("fallbacks") == "default",
          "fallbacks côté serveur (bêta et paramètre)")
    check("compact-2026-01-12" in (req["beta"] or "") and b.get("context_management"), "compaction")
    check(b.get("thinking") == {"type": "adaptive", "display": "summarized"}, "pensée adaptative résumée")
    check(b.get("output_config") == {"effort": "high"}, "effort high")
    check(b.get("cache_control") == {"type": "ephemeral"}, "cache automatique")
    names = [t.get("name") for t in b["tools"]]
    check(names[:8] == ["read_file", "write_file", "edit_file", "bash", "glob", "grep", "skill", "update_plan"]
          and "web_search" in names, "outils : %s" % names)
    # (le jiter de Korin, en Python pur, est devant dans PYTHONPATH comme sur la carte : pas d'entrée en flux)
    check(not any(t.get("eager_input_streaming") for t in b["tools"][:8]), "pas d'eager_input_streaming (jiter de Korin)")
    sysp = b["system"]
    check("korin-demo" in sysp[0]["text"] and "Consignes de Korin" in sysp[0]["text"], "compétences et AGENTS.md")
    check("Consignes du projet d'essai" in sysp[1]["text"] and work in sysp[1]["text"], "consignes du projet")

    print("== outils et accords (mode ask)")
    asked = []

    def allow(m):
        asked.append(m)
        return {"outcome": {"outcome": "selected", "optionId": "allow"}}

    def reject(m):
        asked.append(m)
        return {"outcome": {"outcome": "selected", "optionId": "reject"}}

    w = json.dumps({"path": "hello.txt", "content": "salut\nKorin\n"})
    r = a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool write_file " + w}]},
               on_request=allow)
    check(r["result"]["stopReason"] == "end_turn", "write_file : fin du tour")
    check(len(asked) == 1 and asked[0]["method"] == "session/request_permission", "write_file demande l'accord")
    check(asked and asked[0]["params"]["toolCall"]["content"][0]["type"] == "diff", "l'accord montre un diff")
    check(os.path.exists(os.path.join(work, "hello.txt")), "hello.txt écrit")
    reqs = requests(reqlog)
    last = reqs[-1]["body"]["messages"]
    th = last[-2]["content"][0]
    check(th.get("type") == "thinking" and th.get("signature", "").startswith("sig-"),
          "le bloc de pensée est rejoué avec sa signature")
    check(last[-1]["content"][0]["type"] == "tool_result", "tool_result renvoyé")

    asked.clear()
    e = json.dumps({"path": "hello.txt", "old_string": "Korin", "new_string": "Korin OS"})
    a.updates.clear()
    r = a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool edit_file " + e}]},
               on_request=allow)
    check(open(os.path.join(work, "hello.txt")).read() == "salut\nKorin OS\n", "edit_file après write_file")

    r = a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool bash " + json.dumps(
        {"command": "echo coucou"})}]}, on_request=reject)
    check("refusé" in a.texts("agent_message_chunk") or "refusé" in json.dumps(requests(reqlog)[-1]["body"]["messages"][-1]),
          "bash refusé : le modèle le sait")

    print("== mode auto-approve")
    a.call("session/set_mode", {"sessionId": sid, "modeId": "auto-approve"})
    check(any(u.get("sessionUpdate") == "current_mode_update" for u in a.updates), "current_mode_update")
    asked.clear()
    a.updates.clear()
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool bash " + json.dumps(
        {"command": "echo coucou; pwd; exit 3"})}]}, on_request=allow)
    res = requests(reqlog)[-1]["body"]["messages"][-1]["content"][0]["content"]
    check(not asked and "coucou" in res and work in res and "code de retour 3" in res, "bash sans accord : %r" % res)
    upd = [u for u in a.updates if u.get("sessionUpdate") == "tool_call_update" and u.get("status") == "completed"]
    check(upd and "coucou" in json.dumps(upd[-1]), "la sortie de bash va à la façade")

    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool read_file " + json.dumps(
        {"path": "hello.txt"})}]})
    res = requests(reqlog)[-1]["body"]["messages"][-1]["content"][0]["content"]
    check("     1\tsalut" in res, "read_file numérote les lignes")
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool glob " + json.dumps(
        {"pattern": "*.txt"})}]})
    res = requests(reqlog)[-1]["body"]["messages"][-1]["content"][0]["content"]
    check(res.endswith("hello.txt"), "glob : %r" % res)
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool skill " + json.dumps(
        {"name": "korin-demo"})}]})
    res = requests(reqlog)[-1]["body"]["messages"][-1]["content"][0]["content"]
    check("Contenu de la compétence" in res, "skill charge SKILL.md")
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool edit_file " + json.dumps(
        {"path": "absent.txt", "old_string": "a", "new_string": "b"})}]})
    r0 = requests(reqlog)[-1]["body"]["messages"][-1]["content"][0]
    check(r0.get("is_error") is True, "edit_file sans lecture : erreur pour le modèle")
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool write_file {\"path\": 3}"}]})
    r0 = requests(reqlog)[-1]["body"]["messages"][-1]["content"][0]
    check(r0.get("is_error") is True and "invalide" in r0["content"], "entrée invalide refusée : %r" % r0["content"])
    a.updates.clear()
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!tool update_plan " + json.dumps(
        {"items": [{"content": "Étape un", "status": "in_progress"}, {"content": "Étape deux", "status": "pending"}]})}]})
    check(any(u.get("sessionUpdate") == "plan" and len(u["entries"]) == 2 for u in a.updates), "update_plan : plan")

    print("== /cost, interruption, refus")
    a.updates.clear()
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "/cost"}]})
    check("coût estimé" in a.texts("agent_message_chunk"), "/cost : %r" % a.texts("agent_message_chunk")[:120])
    n = len(requests(reqlog))
    a.next += 1
    pid = a.next
    a.send({"jsonrpc": "2.0", "id": pid, "method": "session/prompt",
            "params": {"sessionId": sid, "prompt": [{"type": "text", "text": "!slow"}]}})
    time.sleep(1.5)
    a.notify("session/cancel", {"sessionId": sid})
    t0 = time.time()
    got = None
    while time.time() - t0 < 10:
        m = a.q.get(timeout=10)
        if m.get("id") == pid:
            got = m
            break
    check(got and got["result"]["stopReason"] == "cancelled" and time.time() - t0 < 3, "Échap pendant le flux")
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "après"}]})
    msgs = requests(reqlog)[-1]["body"]["messages"]
    check(len(requests(reqlog)) == n + 2 and msgs[-1]["content"][-1]["text"] == "après"
          and msgs[-1]["content"][0]["text"] == "!slow", "le tour interrompu est fusionné au suivant")
    r = a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "!refuse"}]})
    check(r["result"]["stopReason"] == "refusal", "refus : stopReason refusal")
    r = a.call("session/set_config_option", {"sessionId": sid, "configId": "model", "value": "claude-sonnet-5-5"})
    check(r["result"]["configOptions"][0]["currentValue"] == "claude-sonnet-5-5", "changer de modèle")
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "sonnet ?"}]})
    check(requests(reqlog)[-1]["body"]["model"] == "claude-sonnet-5-5", "la requête suivante prend le modèle")
    a.close()

    print("== reprise de la séance")
    a = Agent(env, installed)
    a.call("initialize", {"protocolVersion": 1, "clientCapabilities": {}})
    r = a.call("session/list", {"cwd": work})["result"]
    check(len(r["sessions"]) == 1 and r["sessions"][0]["sessionId"] == sid and r["sessions"][0]["title"] == "bonjour",
          "session/list")
    r = a.call("session/load", {"sessionId": sid, "cwd": work, "mcpServers": []})
    check("result" in r and r["result"]["configOptions"][0]["currentValue"] == "claude-sonnet-5-5", "session/load")
    check("bonjour" in a.texts("user_message_chunk") and "Écho : bonjour" in a.texts("agent_message_chunk"),
          "l'historique est rejoué")
    n = len(requests(reqlog)[-1]["body"]["messages"])
    a.call("session/prompt", {"sessionId": sid, "prompt": [{"type": "text", "text": "encore"}]})
    check(len(requests(reqlog)[-1]["body"]["messages"]) == n + 2, "la séance reprise continue l'historique")
    a.close()

    print("== clé refusée")
    with open(os.path.join(env["CLAUDE_ACP_HOME"], ".env"), "w") as f:
        f.write("ANTHROPIC_API_KEY=sk-ant-bad\n")
    a = Agent(env, installed)
    a.call("initialize", {"protocolVersion": 1, "clientCapabilities": {}})
    sid2 = a.call("session/new", {"cwd": work, "mcpServers": []})["result"]["sessionId"]
    r = a.call("session/prompt", {"sessionId": sid2, "prompt": [{"type": "text", "text": "x"}]})
    last = requests(reqlog)[-1]
    check(r.get("error", {}).get("code") == -32002 and "refusée" in r["error"]["message"],
          "clé refusée : %s (clé reçue %r)" % (r.get("error"), last.get("key")))
    a.close()


if __name__ == "__main__":
    sys.exit(main())
