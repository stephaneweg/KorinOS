"""pctest.py -- l'essai de korin-mcp sous QEMU PC (docs/MCP-KORIN.md) : un client MCP qui appelle chaque outil.

  python3 -I tools/mcp/pctest.py [--claude /chemin/claude] [--no-claude] [--keep]

Avant : sh tools/pc/build.sh, ARCH=x86_64 sh tools/posix/build.sh, sh tools/linux/build.sh, sh tools/mcp/build.sh,
ARCH=x86_64 sh tools/bash/build.sh, sh tools/pc/sysroot.sh (build/pc/sysroot avec /bin/korin-mcp, /bin/claude et
/etc/claude-code/mcp.json).

1. Les requêtes (build/pc/mcp/test/req.jsonl) : initialize (2025-11-25), notifications/initialized, tools/list,
   ping, server/discover (méthode inconnue : le client de Claude Code se replie), puis tools/call de chaque outil,
   des erreurs voulues (fichier secret, signal à un service, outil inconnu, JSON illisible).
2. Sous QEMU (tools/linux/exectest/run.sh : le disque de build/pc/sysroot, telnet) : korin-mcp lit les requêtes sur
   son entrée et écrit les réponses (une par ligne), recopiées au Terminal ; --call pour launch, process et
   send_signal (un pid connu seulement après le lancement) ; puis, si le binaire de Claude Code est là
   (/opt/claude-code/bin/claude, --claude), Claude Code lui-même sous linuxhost lance korin-mcp comme serveur MCP
   (claude mcp add -s user, claude mcp list : « ✔ Connected ») et /bin/claude avec /etc/claude-code/mcp.json.
3. Ici : chaque réponse vérifiée (id, résultat ou erreur attendue, JSON du texte, champs) ; la capture d'écran
   relue (PNG, zlib) dans build/pc/mcp/test/screen.png. Sortie : « mcp pctest : RÉUSSI » ou les échecs.
Aucune clé ni aucun compte : Claude Code n'appelle pas le modèle (mcp list ne fait que le contrôle des serveurs).
"""
import argparse
import base64
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "build", "pc", "mcp", "test")
RUN = os.path.join(ROOT, "build", "pc", "linux", "exectest", "run", "run.txt")

# les appels : (id, méthode, paramètres, ce qu'on attend : "ok", "tool_error", ou un code d'erreur JSON-RPC)
CALLS = [
    ("init", "initialize", {"protocolVersion": "2025-11-25", "capabilities": {},
                            "clientInfo": {"name": "pctest", "version": "1"}}, "ok"),
    (None, "notifications/initialized", None, None),
    ("list", "tools/list", {}, "ok"),
    ("ping", "ping", {}, "ok"),
    ("discover", "server/discover", {}, -32601),
]
TOOLS = [
    ("processes", {"sample_ms": 500}, "ok"),
    ("processes", {"sample_ms": 0, "name": "netd", "sort": "cpu"}, "ok"),
    ("process", {"name": "vfs", "sample_ms": 300}, "ok"),
    ("process", {"pid": 999999}, "tool_error"),
    ("cpu", {"sample_ms": 500, "top": 5}, "ok"),
    ("kernel_log", {"lines": 30}, "ok"),
    ("kernel_log", {"source": "vfs", "lines": 5}, "ok"),
    ("kernel_log", {"pattern": "xhci|usb|nvme", "lines": 5}, "ok"),
    ("kernel_log", {"pattern": "("}, "tool_error"),
    ("list_dir", {"path": "/"}, "ok"),
    ("list_dir", {"path": "HOME:/"}, "ok"),
    ("list_dir", {"path": "/nexiste/pas"}, "tool_error"),
    ("read_file", {"path": "/etc/services.txt"}, "ok"),
    ("read_file", {"path": "/etc/services.txt", "start_line": 2, "max_lines": 3}, "ok"),
    ("read_file", {"path": "/bin/ls", "length": 64}, "ok"),
    ("read_file", {"path": "/etc/remote.txt"}, "tool_error"),
    ("find_files", {"root": "/etc", "name": "*.txt"}, "ok"),
    ("find_files", {"root": "/", "contains": "korin-mcp", "type": "file"}, "ok"),
    ("volumes", {}, "ok"),
    ("packages", {"action": "installed"}, "ok"),
    ("packages", {"action": "updates"}, "ok"),
    ("packages", {"action": "available", "name": "bash"}, "ok"),
    ("packages", {"action": "bogus"}, "tool_error"),
    ("services", {}, "ok"),
    ("gpu", {}, "ok"),
    ("network", {}, "ok"),
    ("time", {}, "ok"),
    ("windows", {}, "ok_or_error"),
    ("screenshot", {"max_width": 400}, "ok_or_error"),
    ("screenshot", {"region": {"x": 0, "y": 0, "width": 200, "height": 120}}, "ok_or_error"),
    ("send_signal", {"pid": 1}, "tool_error"),
    ("launch", {"program": "/sys/svc/netd"}, "tool_error"),
    ("launch", {"program": "echo", "args": ["bonjour", "de", "korin-mcp"], "wait_ms": 1500}, "ok"),
    ("nexiste", {}, -32602),
]


def requests():
    lines, expect = [], {}
    for cid, m, p, want in CALLS:
        msg = {"jsonrpc": "2.0", "method": m}
        if cid is not None:
            msg["id"] = cid
            expect[cid] = (m, None, want)
        if p is not None:
            msg["params"] = p
        lines.append(json.dumps(msg, ensure_ascii=False))
    for i, (name, args, want) in enumerate(TOOLS):
        cid = 100 + i
        lines.append(json.dumps({"jsonrpc": "2.0", "id": cid, "method": "tools/call",
                                 "params": {"name": name, "arguments": args}}, ensure_ascii=False))
        expect[cid] = ("tools/call", name, want)
    lines.append("{ceci n'est pas du JSON")
    expect[None] = ("bad json", None, -32700)
    return lines, expect


# ce que chaque outil doit rendre (clés du JSON)
KEYS = {
    "processes": ["processes", "count"], "process": ["pid", "name", "threads"], "cpu": ["cores", "memory", "uptime_ms"],
    "kernel_log": ["lines", "matched"], "list_dir": ["path", "entries"], "read_file": ["content", "encoding"],
    "find_files": ["results", "found"], "volumes": ["volumes"], "packages": ["packages"],
    "services": ["services_txt", "system_processes"], "gpu": ["intelgpu"], "network": ["config", "interfaces"],
    "time": ["utc", "uptime_ms"], "windows": ["windows"], "screenshot": ["image_width", "png_bytes"],
    "launch": ["pid", "state"],
}


def png_size(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "pas un PNG"
    i, idat, w, h = 8, b"", 0, 0
    while i < len(data):
        n, = struct.unpack(">I", data[i:i + 4])
        t, c = data[i + 4:i + 8], data[i + 8:i + 8 + n]
        assert zlib.crc32(t + c) == struct.unpack(">I", data[i + 8 + n:i + 12 + n])[0], "CRC"
        if t == b"IHDR":
            w, h = struct.unpack(">II", c[:8])
        elif t == b"IDAT":
            idat += c
        i += 12 + n
    raw = zlib.decompress(idat)
    assert len(raw) == h * (w * 3 + 1), "taille des données"
    return w, h


def check(text, expect):
    fails, seen, notes = [], set(), []
    replies = []
    for line in text.splitlines():
        line = line.strip()
        if line.startswith("{\"jsonrpc\""):
            try:
                replies.append(json.loads(line))
            except ValueError as e:
                fails.append("réponse illisible (%s) : %s" % (e, line[:120]))
    for r in replies:
        cid = r.get("id")
        if cid not in expect:
            fails.append("id inattendu : %r" % cid)
            continue
        seen.add(cid)
        m, tool, want = expect[cid]
        label = "%s %s" % (m, tool or "")
        if isinstance(want, int):
            code = r.get("error", {}).get("code")
            (fails.append if code != want else notes.append)("%s : erreur %s (attendue %s)" % (label, code, want))
            continue
        if "error" in r:
            fails.append("%s : erreur %s" % (label, r["error"]))
            continue
        res = r.get("result", {})
        if m == "initialize":
            if res.get("protocolVersion") != "2025-11-25" or "tools" not in res.get("capabilities", {}):
                fails.append("initialize : %s" % res)
            continue
        if m == "tools/list":
            names = [t["name"] for t in res.get("tools", [])]
            for t in res.get("tools", []):
                if t.get("inputSchema", {}).get("type") != "object":
                    fails.append("tools/list : %s sans inputSchema objet" % t["name"])
            notes.append("tools/list : %d outils : %s" % (len(names), " ".join(names)))
            for t in KEYS:
                if t not in names:
                    fails.append("tools/list : %s manque" % t)
            continue
        if m != "tools/call":
            continue
        content = res.get("content", [])
        texts = [c["text"] for c in content if c.get("type") == "text"]
        imgs = [c for c in content if c.get("type") == "image"]
        err = res.get("isError", False)
        if want == "tool_error":
            (notes.append if err else fails.append)("%s : %s (erreur attendue)" % (label, texts[0][:160] if texts else "?"))
            continue
        if err:
            (notes.append if want == "ok_or_error" else fails.append)("%s : isError : %s" % (label, texts[0][:200] if texts else "?"))
            continue
        try:
            body = json.loads(texts[0])
        except (IndexError, ValueError) as e:
            fails.append("%s : texte qui n'est pas du JSON (%s)" % (label, e))
            continue
        miss = [k for k in KEYS.get(tool, []) if k not in body]
        if miss:
            fails.append("%s : clés absentes %s : %s" % (label, miss, texts[0][:200]))
            continue
        summary = texts[0] if len(texts[0]) < 300 else texts[0][:300] + "…"
        notes.append("%s -> %s" % (label, summary))
        if tool == "screenshot":
            if not imgs:
                fails.append("screenshot : pas d'image")
                continue
            data = base64.b64decode(imgs[0]["data"])
            try:
                w, h = png_size(data)
            except Exception as e:  # noqa: BLE001 (tout échec du PNG est un échec de l'essai)
                fails.append("screenshot : PNG faux (%s)" % e)
                continue
            if (w, h) != (body["image_width"], body["image_height"]):
                fails.append("screenshot : %dx%d au lieu de %sx%s" % (w, h, body["image_width"], body["image_height"]))
            name = "screen.png" if "region" not in json.dumps(body) and not os.path.exists(os.path.join(OUT, "screen.png")) else "screen-%d.png" % cid
            with open(os.path.join(OUT, name), "wb") as f:
                f.write(data)
            notes.append("screenshot : PNG %dx%d relu (%d octets) -> build/pc/mcp/test/%s" % (w, h, len(data), name))
    for cid, (m, tool, want) in expect.items():
        if cid not in seen:
            fails.append("pas de réponse à %s %s (id %r)" % (m, tool or "", cid))
    return fails, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--claude", default="/opt/claude-code/bin/claude", help="le binaire de Claude Code pour Linux x86-64")
    ap.add_argument("--no-claude", action="store_true")
    ap.add_argument("--keep", action="store_true", help="garder l'arbre d'essai (build/pc/mcp/test/tree)")
    ap.add_argument("--only-check", action="store_true", help="vérifier run.txt d'un essai déjà fait")
    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    lines, expect = requests()
    if not a.only_check:
        for need in ("build/pc/sysroot/bin/korin-mcp", "build/pc/sysroot/etc/claude-code/mcp.json", "build/pc/linux/linuxhost"):
            if not os.path.exists(os.path.join(ROOT, need)):
                raise SystemExit("absent : %s (voir l'en-tête de ce script)" % need)
        tree = os.path.join(OUT, "tree")
        shutil.rmtree(tree, ignore_errors=True)
        os.makedirs(os.path.join(tree, "home", "lx"))
        with open(os.path.join(tree, "home", "lx", "req.jsonl"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        with open(os.path.join(OUT, "req.jsonl"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        claude = not a.no_claude and os.path.isfile(a.claude)
        extra_mb = 0
        if claude:  # le binaire officiel, là où le met le paquet claude-code (le lanceur /bin/claude le cherche là)
            os.makedirs(os.path.join(tree, "devtools", "claude-code"))
            shutil.copy(a.claude, os.path.join(tree, "devtools", "claude-code", "claude"))
            extra_mb = os.path.getsize(a.claude) // (1 << 20) + 50
        M = "/bin/korin-mcp"
        sed_pid = "sed 's/.*\"pid\":\\([0-9]*\\).*/\\1/'"
        cmds = [
            "sleep:25",
            "@60 " + M + " --list; echo rc=$?",
            "@120 " + M + " < /home/lx/req.jsonl > /tmp/mcpout.jsonl; echo rc=$?",
            "@60 cat /tmp/mcpout.jsonl",
            "@60 cat /tmp/korin-mcp/*.log; echo rc=$?",
            # launch, process, send_signal : le pid du lancement d'abord
            # (pas de « | » : c'est le séparateur de ONLY ; le pid lu par sed dans un fichier)
            "@60 " + M + " --call launch '{\"program\":\"sleep\",\"args\":[\"60\"],\"wait_ms\":200}' > /tmp/l.json; P=$(" + sed_pid + " /tmp/l.json); echo pid=$P; "
            + M + " --call process \"{\\\"pid\\\":$P,\\\"sample_ms\\\":200}\"; " + M + " --call send_signal \"{\\\"pid\\\":$P}\"; echo rc=$?",
            # un signal à un service (vfs) : refusé sans allow_system
            "@60 " + M + " --call process '{\"name\":\"vfs\",\"sample_ms\":0}' > /tmp/v.json; V=$(" + sed_pid + " /tmp/v.json); " + M + " --call send_signal \"{\\\"pid\\\":$V}\"; echo rc=$?",
            "@60 " + M + " --call screenshot '{\"max_width\":640}'; echo rc=$?",
        ]
        if claude:
            env = "HOME=/home/lx "  # (et < /dev/null : sinon Claude Code lit ce que le banc tape au terminal)
            cmds += [
                "@300 " + env + "/bin/claude --version < /dev/null; echo rc=$?",
                "@300 " + env + "/bin/claude mcp add -s user korin-direct -- " + M + " < /dev/null; echo rc=$?",
                "@300 " + env + "/bin/claude mcp list < /dev/null > /tmp/mcplist.txt 2>&1; echo rc=$?; cat /tmp/mcplist.txt",
                "@300 " + env + "/bin/claude mcp get korin-direct < /dev/null > /tmp/mcpget.txt 2>&1; echo rc=$?; cat /tmp/mcpget.txt",
            ]
        env = dict(os.environ, ONLY="|".join(cmds), TREE=tree, MEM="4G", EXTRA_MB=str(extra_mb),
                   CA_EXTRA="/root/.ccr/ca-bundle.crt" if os.path.isfile("/root/.ccr/ca-bundle.crt") else "")
        if not env["CA_EXTRA"]:
            del env["CA_EXTRA"]
        subprocess.run(["sh", "tools/linux/exectest/run.sh"], cwd=ROOT, env=env, check=False)
        if not a.keep:
            shutil.rmtree(tree, ignore_errors=True)
        shutil.copy(RUN, os.path.join(OUT, "run.txt"))
    text = open(os.path.join(OUT, "run.txt"), encoding="utf-8", errors="replace").read()
    fails, notes = check(text, expect)
    for n in notes:
        print("  ok  " + n)
    # --call : launch, process, send_signal, screenshot
    m = re.search(r"pid=(\d+)", text)
    if not m:
        fails.append("--call launch : pas de pid")
    else:
        if '"sent":true' not in text:
            fails.append("--call send_signal : pas de \"sent\":true")
        if '"name":"sleep","state":"running"' not in text:
            fails.append("--call process : le process lancé (sleep) n'apparaît pas")
    if "programme de /sys (service, pilote) : refusé sans allow_system" not in text:
        fails.append("--call send_signal vfs : pas refusé")
    if "bonjour de korin-mcp" not in text:
        fails.append("launch echo : la sortie n'est pas dans le journal de /tmp/korin-mcp")
    if not a.no_claude and "korin-direct" in text:
        if not re.search(r"korin-direct: /bin/korin-mcp\s+- . Connected", text):
            fails.append("claude mcp list : korin-direct n'est pas « Connected »")
        else:
            print("  ok  Claude Code (sous linuxhost) : korin-mcp ✔ Connected")
    for f in fails:
        print("ÉCHEC " + f)
    print("mcp pctest : %s (%d vérifications, %d échecs ; sortie : build/pc/mcp/test/run.txt)"
          % ("RÉUSSI" if not fails else "ÉCHEC", len(notes), len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
