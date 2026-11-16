"""Les serveurs MCP (Model Context Protocol) de claude-acp : leurs outils, donnés au modèle comme les siens.

Transport « Streamable HTTP » (MCP 2025-06-18) : chaque message JSON-RPC part en POST ; la réponse revient en JSON
ou en flux SSE (les lignes « data: »), les deux sont lus. Les serveurs viennent de config.toml (comme Vibe) :

    [[mcp_servers]]
    name = "forge"
    transport = "streamable-http"          (ou "http")
    url = "http://127.0.0.1:7311/mcp"
    headers = { Authorization = "Bearer ..." }
    tool_timeout_sec = 300

ou de la façade ACP (session/new, mcpServers de type http). Les outils s'appellent « <serveur>_<outil> »
(forge_add_box), comme chez Vibe : une compétence vaut pour les deux. Un serveur qui ne répond pas est réessayé au
tour suivant (Forge peut être lancée en cours de séance).
"""
import json
import re
import time
import urllib.error
import urllib.request

PROTOCOL = "2025-06-18"
_NAME = re.compile(r"[^a-zA-Z0-9_-]")


class McpError(Exception):
    pass


class Server:
    def __init__(self, name, url, headers=None, timeout=300.0):
        self.name = _NAME.sub("_", name)[:20] or "mcp"
        self.url = url
        self.headers = dict(headers or {})
        self.timeout = float(timeout)
        self.session = None       # Mcp-Session-Id rendu par le serveur
        self.tools = None         # la liste du serveur (None : pas encore joint)
        self.next_try = 0.0
        self.error = ""
        self.rid = 0

    # -- le transport

    def _post(self, msg, timeout):
        h = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream",
             "MCP-Protocol-Version": PROTOCOL}
        h.update(self.headers)
        if self.session:
            h["Mcp-Session-Id"] = self.session
        req = urllib.request.Request(self.url, data=json.dumps(msg).encode(), headers=h, method="POST")
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                sid = r.headers.get("Mcp-Session-Id")
                if sid:
                    self.session = sid
                body = r.read().decode("utf-8", "replace")
                ctype = r.headers.get("Content-Type", "")
        except urllib.error.HTTPError as e:
            if e.code == 401:
                raise McpError("%s : accès refusé (401) : le jeton de config.toml ne correspond pas" % self.name)
            if e.code == 404 and self.session:
                self.session = None   # (la séance du serveur a expiré : le prochain message la refait)
            raise McpError("%s : HTTP %d" % (self.name, e.code))
        except (urllib.error.URLError, OSError) as e:
            raise McpError("%s ne répond pas (%s) : est-il lancé ? (Forge : forge --mcp, ou Fichier > Piloter par un "
                           "agent)" % (self.name, getattr(e, "reason", e)))
        if not body.strip():
            return None
        if "text/event-stream" in ctype:
            last = None
            for line in body.splitlines():
                if line.startswith("data:"):
                    try:
                        m = json.loads(line[5:].strip())
                    except ValueError:
                        continue
                    if isinstance(m, dict) and m.get("id") == msg.get("id"):
                        last = m
            return last
        return json.loads(body)

    def _rpc(self, method, params=None, timeout=None):
        self.rid += 1
        r = self._post({"jsonrpc": "2.0", "id": self.rid, "method": method, "params": params or {}},
                       timeout or self.timeout)
        if r is None:
            raise McpError("%s : pas de réponse à %s" % (self.name, method))
        if "error" in r:
            raise McpError("%s : %s" % (self.name, r["error"].get("message", r["error"])))
        return r.get("result") or {}

    # -- la séance

    def connect(self):
        """Joint le serveur (une fois ; réessayé au plus toutes les 5 s) -> vrai si ses outils sont connus."""
        if self.tools is not None:
            return True
        if time.time() < self.next_try:
            return False
        try:
            self._rpc("initialize", {"protocolVersion": PROTOCOL, "capabilities": {},
                                     "clientInfo": {"name": "claude-acp", "version": "1"}}, timeout=5)
            try:
                self._post({"jsonrpc": "2.0", "method": "notifications/initialized"}, 5)
            except McpError:
                pass
            tools, cursor = [], None
            while True:
                r = self._rpc("tools/list", {"cursor": cursor} if cursor else {}, timeout=15)
                tools += r.get("tools", [])
                cursor = r.get("nextCursor")
                if not cursor:
                    break
            self.tools = tools
            self.error = ""
            return True
        except (McpError, ValueError) as e:
            self.error = str(e)
            self.next_try = time.time() + 5
            return False

    def call(self, tool, args):
        """Un appel -> (blocs de contenu pour l'API Anthropic, en erreur ?)."""
        try:
            r = self._rpc("tools/call", {"name": tool, "arguments": args})
        except McpError as e:
            if "ne répond pas" in str(e):
                self.tools = None     # (à rejoindre : Forge a peut-être été fermée)
            return [{"type": "text", "text": str(e)}], True
        blocks = []
        for c in r.get("content", []):
            t = c.get("type")
            if t == "text":
                blocks.append({"type": "text", "text": c.get("text", "") or "(vide)"})
            elif t == "image" and c.get("mimeType") in ("image/png", "image/jpeg", "image/gif", "image/webp"):
                blocks.append({"type": "image", "source": {"type": "base64", "media_type": c["mimeType"],
                                                           "data": c.get("data", "")}})
            elif t == "resource" and isinstance(c.get("resource"), dict) and "text" in c["resource"]:
                blocks.append({"type": "text", "text": c["resource"]["text"]})
        if r.get("structuredContent") is not None and not blocks:
            blocks.append({"type": "text", "text": json.dumps(r["structuredContent"], ensure_ascii=False)})
        return blocks or [{"type": "text", "text": "(vide)"}], bool(r.get("isError"))


class Registry:
    """Les serveurs d'une séance et leurs outils, sous les noms donnés au modèle."""

    def __init__(self, servers):
        self.servers = servers
        self.by_name = {}         # nom donné au modèle -> (serveur, outil MCP, lecture seule ?)

    def api_tools(self):
        out = []
        self.by_name = {}
        for s in self.servers:
            if not s.connect():
                continue
            for t in s.tools:
                name = (s.name + "_" + _NAME.sub("_", t.get("name", "")))[:64]
                schema = t.get("inputSchema") or {"type": "object", "properties": {}}
                if schema.get("type") != "object":
                    schema = {"type": "object", "properties": {}}
                ann = t.get("annotations") or {}
                self.by_name[name] = (s, t["name"], bool(ann.get("readOnlyHint")))
                out.append({"name": name, "description": ("[%s] " % s.name) + (t.get("description") or t["name"]),
                            "input_schema": schema})
        return out

    def owns(self, name):
        return name in self.by_name

    def read_only(self, name):
        return self.by_name[name][2]

    def server_of(self, name):
        return self.by_name[name][0].name

    def call(self, name, args):
        s, tool, _ = self.by_name[name]
        return s.call(tool, args if isinstance(args, dict) else {})

    def status(self):
        return [(s.name, s.tools is not None, len(s.tools or []), s.error) for s in self.servers]


def from_config(cfg, acp_servers=None):
    """Les serveurs de config.toml ([[mcp_servers]]) et ceux de la façade (ACP, type http)."""
    out = []
    for e in cfg.get("mcp_servers") or []:
        if not isinstance(e, dict) or e.get("disabled"):
            continue
        tr = e.get("transport", "streamable-http")
        if tr not in ("http", "streamable-http") or not e.get("url"):
            continue      # (stdio : pas encore)
        headers = dict(e.get("headers") or {})
        auth = e.get("auth")
        if isinstance(auth, dict) and isinstance(auth.get("headers"), dict):
            headers.update(auth["headers"])
        out.append(Server(e.get("name", "mcp"), e["url"], headers, e.get("tool_timeout_sec", 300)))
    for e in acp_servers or []:
        if isinstance(e, dict) and e.get("type") in ("http", "sse") and e.get("url"):
            hs = {h.get("name"): h.get("value") for h in e.get("headers") or [] if isinstance(h, dict)}
            if not any(s.name == e.get("name") for s in out):
                out.append(Server(e.get("name", "mcp"), e["url"], hs))
    return out
