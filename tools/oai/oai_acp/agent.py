"""Les séances sur une API compatible OpenAI : la requête en flux, l'historique au format chat completions, le coût.

Tout le reste vient de claude_acp.agent (Session : le tour, les accords, les outils, MCP, la mémoire sur disque) :
cette séance n'en change que ce qui touche à l'API. L'historique est gardé au format de l'API (messages user,
assistant avec tool_calls, tool), rejoué tel quel ; la pensée éventuelle (reasoning_content) est montrée mais pas
gardée.
"""
import html.parser
import json
import os
import socket
import time
import tomllib
import urllib.error
import urllib.parse
import urllib.request
import uuid
from types import SimpleNamespace

from claude_acp import agent as A
from claude_acp import tools as T

from . import __version__

# ---- les profils : un fournisseur et ses modèles ----
#
# Un modèle : id (le nom donné à l'API ; sous Foundry, le nom du déploiement), nom montré, prix $/Mtok (entrée,
# sortie, entrée lue du cache ; 0 : inconnu), fenêtre de contexte.

PROFILES = {
    "mai": {
        "name": "mai-acp", "title": "MAI", "who": "MAI, un modèle de Microsoft",
        "home_env": "MAI_ACP_HOME", "home_dir": "mai-acp",          # ~/.local/mai-acp
        "key_env": "MAI_API_KEY", "url_env": "MAI_BASE_URL",
        "base_url": "",            # https://<ressource>.services.ai.azure.com/mai/v1 : celui du déploiement Foundry
        "model": "MAI-Code-1.1-Flash",
        "models": [
            {"id": "MAI-Code-1.1-Flash", "name": "MAI-Code-1.1-Flash", "input": 0.20, "output": 1.20,
             "cached": 0.02, "context": 256000},
            {"id": "MAI-Code-2-Flash", "name": "MAI Code 2 Flash (préversion)", "input": 0, "output": 0,
             "cached": 0, "context": 256000},
        ],
        "max_tokens": 64000,
        "token_param": "max_completion_tokens",
        "console": "https://ai.azure.com",
    },
    "openai": {
        "name": "oai-acp", "title": "Agent", "who": "un agent de code",
        "home_env": "OAI_ACP_HOME", "home_dir": "oai-acp",
        "key_env": "OAI_API_KEY", "url_env": "OAI_BASE_URL",
        "base_url": "https://api.openai.com/v1",
        "model": "",
        "models": [],
        "max_tokens": 32000,
        "token_param": "max_completion_tokens",
        "console": "",
    },
}

P = PROFILES["mai"]


def use_profile(name):
    """Le profil de ce process : son dossier ($MAI_ACP_HOME, ~/.local/mai-acp) est pris par tout claude_acp."""
    global P
    if name not in PROFILES:
        raise SystemExit("profil inconnu : %s (connus : %s)" % (name, ", ".join(PROFILES)))
    P = PROFILES[name]
    A.HOME_ENV = P["home_env"]
    A.HOME_DIR = P["home_dir"]


log = A.log
home = A.home
load_env = A.load_env


def _models(raw):
    out = []
    for m in raw or []:
        if isinstance(m, str):
            m = {"id": m}
        if isinstance(m, dict) and m.get("id"):
            out.append({"id": str(m["id"]), "name": str(m.get("name") or m["id"]),
                        "input": float(m.get("input", 0) or 0), "output": float(m.get("output", 0) or 0),
                        "cached": float(m.get("cached", 0) or 0), "context": int(m.get("context", 0) or 128000)})
    return out


def load_config():
    """$HOME_DIR/config.toml par-dessus le profil ; MAI_BASE_URL (environnement ou .env) l'emporte sur base_url."""
    cfg = {k: v for k, v in P.items()}
    cfg.update({"auth": "", "temperature": None, "reasoning_effort": "", "extra": {}, "web": True,
                "fallbacks": False, "compaction": False})
    p = os.path.join(home(), "config.toml")
    if os.path.exists(p):
        try:
            with open(p, "rb") as f:
                cfg.update(tomllib.load(f))
        except (OSError, ValueError) as e:
            log("config.toml illisible :", e)
    if os.environ.get(P["url_env"]):
        cfg["base_url"] = os.environ[P["url_env"]]
    cfg["base_url"] = norm_url(cfg.get("base_url") or "")
    models = _models(cfg.get("models"))
    if cfg.get("model") and not any(m["id"] == cfg["model"] for m in models):
        models.insert(0, _models([cfg["model"]])[0])     # (un déploiement nommé autrement : offert aussi)
    if not cfg.get("model") and models:
        cfg["model"] = models[0]["id"]
    cfg["models"] = models
    cfg["fallbacks"] = cfg["compaction"] = False
    return cfg


def norm_url(u):
    """Le point d'accès donné par l'utilisateur -> la base des chemins de l'API (sans /chat/completions).

    https://x.services.ai.azure.com          -> https://x.services.ai.azure.com/mai/v1 (les modèles MAI)
    https://x.openai.azure.com               -> https://x.openai.azure.com/openai/v1
    .../chat/completions, un / final          -> retirés ; ?api-version=... gardé
    """
    u = (u or "").strip().strip('"').strip("'")
    if not u:
        return ""
    if "://" not in u:
        u = "https://" + u
    s = urllib.parse.urlsplit(u)
    path = s.path.rstrip("/")
    if path.endswith("/chat/completions"):
        path = path[:-len("/chat/completions")]
    host = (s.hostname or "").lower()
    if not path or path == "/models":
        if host.endswith(".services.ai.azure.com"):
            path = "/mai/v1"
        elif host.endswith(".openai.azure.com") or host.endswith(".cognitiveservices.azure.com"):
            path = "/openai/v1"
    return urllib.parse.urlunsplit((s.scheme, s.netloc, path, s.query, ""))


def have_key(cfg):
    return bool(os.environ.get(P["key_env"])) and bool(cfg.get("base_url"))


# ---- les erreurs de l'API ----

class ApiError(Exception):
    def __init__(self, status, message):
        super().__init__("%s %s" % (status, message))
        self.status = status
        self.message = message


class KeyRefused(ApiError):
    pass


# ---- les outils : ceux de claude_acp, en fonctions ; et web_fetch, ici côté client ----

WEB_FETCH = {
    "name": "web_fetch",
    "description": (
        "Lit une page web (http ou https) et rend son texte (le HTML converti en texte, 40 000 caractères au plus) : "
        "une documentation, un fichier brut de GitHub (raw.githubusercontent.com), une API JSON."),
    "input_schema": T._schema({"url": {"type": "string"}}, ["url"]),
}


def functions(tools):
    return [{"type": "function", "function": {"name": t["name"], "description": t.get("description", ""),
                                              "parameters": t["input_schema"]}} for t in tools]


class _Text(html.parser.HTMLParser):
    SKIP = {"script", "style", "noscript", "svg", "head"}
    BLOCK = {"p", "div", "br", "li", "tr", "h1", "h2", "h3", "h4", "h5", "h6", "pre", "section", "article", "table"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.out, self.skip = [], 0

    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP:
            self.skip += 1
        elif tag in self.BLOCK:
            self.out.append("\n")

    def handle_endtag(self, tag):
        if tag in self.SKIP and self.skip:
            self.skip -= 1
        elif tag in self.BLOCK:
            self.out.append("\n")

    def handle_data(self, data):
        if not self.skip:
            self.out.append(data)


def web_fetch(url, cancel=None):
    if not url.startswith(("http://", "https://")):
        raise T.ToolError("adresse http ou https attendue")
    req = urllib.request.Request(url, headers={"User-Agent": "korin-oai-acp/%s" % __version__,
                                               "Accept": "text/html,text/plain,application/json,*/*"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read(4 << 20)
            ctype = r.headers.get("Content-Type", "")
            charset = r.headers.get_content_charset() or "utf-8"
    except urllib.error.HTTPError as e:
        raise T.ToolError("%s : HTTP %d %s" % (url, e.code, e.reason))
    except (urllib.error.URLError, OSError) as e:
        raise T.ToolError("%s : %s" % (url, getattr(e, "reason", e)))
    text = data.decode(charset, errors="replace")
    if "html" in ctype or text.lstrip()[:15].lower().startswith(("<!doctype html", "<html")):
        p = _Text()
        p.feed(text)
        lines = [" ".join(l.split()) for l in "".join(p.out).splitlines()]
        text = "\n".join(l for l in lines if l)
    return T.clip(text, 40000) or "(page vide)"


# ---- le prompt système : celui de claude_acp, avec un autre nom ----

def base_prompt():
    b = A.BASE.replace("Tu es Claude, un agent de code", "Tu es %(who)s, agent de code")
    b = b.replace("(claude-acp %(version)s, sur l'API de Claude)", "(%(name)s %(version)s, par une API compatible OpenAI)")
    return b % {"who": P["who"], "name": P["name"], "version": __version__}


# ---- une séance ----

class Session(A.Session):
    def __init__(self, server, sid, cwd, cfg, acp_mcp=None):
        super().__init__(server, sid, cwd, cfg, acp_mcp)
        _skills, desc = A.find_skills()
        self.system = A.system_prompt(cwd, desc, base_prompt())

    def models(self):
        return [(m["id"], m["name"]) for m in self.cfg["models"]]

    def model_info(self):
        return next((m for m in self.cfg["models"] if m["id"] == self.model),
                    {"id": self.model, "input": 0, "output": 0, "cached": 0, "context": 128000})

    # -- le tour --

    def loop(self):
        steps = 0
        while True:
            steps += 1
            if steps > 200:
                self.text("agent_message_chunk", "\n(200 appels de suite : je m'arrête ici.)")
                return "max_turn_requests"
            msg, finish, usage = self.request()
            self.messages.append(msg)
            self.account(usage, finish)
            if finish == "content_filter":
                self.text("agent_message_chunk", "\n(Réponse arrêtée par le filtre de contenu du fournisseur.)")
                return "refusal"
            calls = msg.get("tool_calls") or []
            if not calls:
                return "max_tokens" if finish == "length" else "end_turn"
            results, images = [], []
            for i, c in enumerate(calls):
                if self.cancel.is_set():
                    for rest in calls[i:]:
                        results.append(_tool_msg(rest["id"], "Interrompu par l'utilisateur."))
                    self.messages += results
                    raise Cancelled()
                name = c["function"]["name"]
                if finish == "length":
                    results.append(_tool_msg(c["id"], "Erreur : réponse coupée (longueur maximale) : entrée incomplète, "
                                             "rien n'a été fait. Découper le travail (fichiers plus petits, edit_file)."))
                    self.update({"sessionUpdate": "tool_call", "toolCallId": c["id"], "title": name,
                                 "kind": T.KIND.get(name, "other"), "status": "failed"})
                    continue
                try:
                    args = json.loads(c["function"].get("arguments") or "{}")
                except ValueError as e:
                    self.update({"sessionUpdate": "tool_call", "toolCallId": c["id"], "title": name,
                                 "kind": T.KIND.get(name, "other"), "status": "failed"})
                    results.append(_tool_msg(c["id"], "Erreur : arguments en JSON illisible (%s). Refaire l'appel avec "
                                             "un objet JSON complet." % e))
                    continue
                r = self.call(SimpleNamespace(id=c["id"], name=name, input=args))
                results.append(_from_result(r, name, images))
            self.messages += results
            if images:
                self.messages.append({"role": "user", "content": [
                    {"type": "text", "text": "(Les images rendues par les outils ci-dessus.)"}] + images})
            if self.cancel.is_set():
                raise Cancelled()

    def call(self, b):
        if b.name != "web_fetch":
            return super().call(b)
        url = b.input.get("url") if isinstance(b.input, dict) else None
        self.update({"sessionUpdate": "tool_call", "toolCallId": b.id, "title": "web_fetch: %s" % (url or "?"),
                     "kind": "fetch", "status": "in_progress", "rawInput": b.input})
        if not isinstance(url, str):
            self.tool_update(b.id, "failed", "url manquante")
            return A._result(b.id, "Entrée invalide : champ requis manquant : url.", True)
        try:
            out = web_fetch(url, self.cancel)
        except T.ToolError as e:
            self.tool_update(b.id, "failed", str(e))
            return A._result(b.id, str(e), True)
        self.tool_update(b.id, "completed")
        return A._result(b.id, out)

    # -- la requête --

    def body(self):
        tools = list(T.TOOLS) + self.mcp.api_tools()
        if self.cfg.get("web"):
            tools.append(WEB_FETCH)
        system = "\n\n".join(b["text"] for b in self.system)
        body = {"model": self.model, "messages": [{"role": "system", "content": system}] + self.messages,
                "tools": functions(tools), "stream": True, "stream_options": {"include_usage": True}}
        body[self.cfg.get("token_param") or "max_tokens"] = int(self.cfg["max_tokens"])
        if self.cfg.get("temperature") is not None:
            body["temperature"] = self.cfg["temperature"]
        if self.cfg.get("reasoning_effort"):
            body["reasoning_effort"] = self.cfg["reasoning_effort"]
        if isinstance(self.cfg.get("extra"), dict):
            body.update(self.cfg["extra"])
        return body

    def request(self):
        """Une requête en flux, réessayée sur 429, 5xx et le réseau ; rend (message assistant, finish, usage)."""
        tries = 0
        while True:
            tries += 1
            try:
                return self._stream()
            except Cancelled:
                raise
            except ApiError as e:
                if isinstance(e, KeyRefused) or e.status not in (408, 409, 429, 500, 502, 503, 504) or tries > 4:
                    raise
                wait = min(2 ** tries, 30)
                log("API : %s ; nouvel essai dans %d s" % (e, wait))
            except (urllib.error.URLError, socket.timeout, ConnectionError) as e:
                if tries > 2:
                    raise
                wait = 3
                log("réseau : %r ; nouvel essai dans %d s" % (e, wait))
            t0 = time.monotonic()
            while time.monotonic() - t0 < wait:
                if self.cancel.is_set():
                    raise Cancelled()
                time.sleep(0.2)

    def _open(self, body):
        cfg = self.cfg
        s = urllib.parse.urlsplit(cfg["base_url"])
        url = urllib.parse.urlunsplit((s.scheme, s.netloc, s.path.rstrip("/") + "/chat/completions", s.query, ""))
        key = os.environ.get(P["key_env"], "")
        headers = {"Content-Type": "application/json", "Accept": "text/event-stream",
                   "User-Agent": "korin-%s/%s" % (P["name"], __version__)}
        auth = cfg.get("auth") or ("api-key" if (s.hostname or "").endswith(".azure.com") else "bearer")
        if auth == "api-key":
            headers["api-key"] = key
        else:
            headers["Authorization"] = "Bearer " + key
        req = urllib.request.Request(url, json.dumps(body, ensure_ascii=False).encode("utf-8"), headers, method="POST")
        try:
            return urllib.request.urlopen(req, timeout=float(cfg.get("timeout", 600)))
        except urllib.error.HTTPError as e:
            try:
                raw = e.read().decode("utf-8", "replace")
            except OSError:
                raw = ""
            msg = raw[:500]
            try:
                j = json.loads(raw)
                err = j.get("error", j) if isinstance(j, dict) else j
                msg = err.get("message", msg) if isinstance(err, dict) else str(err)
            except ValueError:
                pass
            log("API : %d %s" % (e.code, raw[:2000]))
            raise (KeyRefused if e.code in (401, 403) else ApiError)(e.code, msg or e.reason)

    def _stream(self):
        text, calls, finish, usage = [], {}, None, None
        thinking = False
        plain = []           # une réponse qui n'est pas en SSE (un proxy qui ignore stream)
        with self._open(self.body()) as r:
            for raw in r:
                if self.cancel.is_set():
                    raise Cancelled()
                line = raw.decode("utf-8", "replace").rstrip("\r\n")
                if not line.startswith("data:"):
                    if line and not line.startswith((":", "event:", "id:", "retry:")):
                        plain.append(line)
                    continue
                data = line[5:].strip()
                if data == "[DONE]":
                    break
                try:
                    ev = json.loads(data)
                except ValueError:
                    log("flux : ligne illisible :", data[:200])
                    continue
                if ev.get("error"):
                    err = ev["error"] if isinstance(ev["error"], dict) else {"message": str(ev["error"])}
                    code = err.get("code")
                    raise ApiError(code if isinstance(code, int) else 500, err.get("message") or str(err))
                if ev.get("usage"):
                    usage = ev["usage"]
                for ch in ev.get("choices") or []:
                    d = ch.get("delta") or {}
                    th = d.get("reasoning_content") or d.get("reasoning")
                    if isinstance(th, str) and th:
                        thinking = True
                        self.text("agent_thought_chunk", th)
                    c = d.get("content")
                    if isinstance(c, str) and c:
                        if thinking:
                            self.text("agent_thought_chunk", "\n")
                            thinking = False
                        text.append(c)
                        self.text("agent_message_chunk", c)
                    for tc in d.get("tool_calls") or []:
                        slot = calls.setdefault(tc.get("index", len(calls)), {"id": "", "name": "", "arguments": ""})
                        if tc.get("id"):
                            slot["id"] = tc["id"]
                        f = tc.get("function") or {}
                        if f.get("name"):
                            slot["name"] += f["name"]
                        if f.get("arguments"):
                            slot["arguments"] += f["arguments"]
                    if ch.get("finish_reason"):
                        finish = ch["finish_reason"]
        if self.cancel.is_set():
            raise Cancelled()
        if plain and not text and not calls:
            try:
                j = json.loads("\n".join(plain))
                m = (j.get("choices") or [{}])[0]
                finish = m.get("finish_reason")
                m = m.get("message") or {}
                usage = j.get("usage") or usage
                if m.get("content"):
                    text.append(m["content"])
                    self.text("agent_message_chunk", m["content"])
                for i, tc in enumerate(m.get("tool_calls") or []):
                    f = tc.get("function") or {}
                    calls[i] = {"id": tc.get("id", ""), "name": f.get("name", ""), "arguments": f.get("arguments", "")}
            except (ValueError, AttributeError, IndexError):
                raise ApiError(502, "réponse illisible : %s" % "\n".join(plain)[:300])
        msg = {"role": "assistant", "content": "".join(text) or None}
        if calls:
            msg["tool_calls"] = [{"id": c["id"] or "call_" + uuid.uuid4().hex[:24], "type": "function",
                                  "function": {"name": c["name"], "arguments": c["arguments"] or "{}"}}
                                 for _i, c in sorted(calls.items())]
        return msg, finish, usage or {}

    # -- l'usage et le coût --

    def account(self, u, finish=None):
        m = self.model_info()
        inp = int(u.get("prompt_tokens") or 0)
        out = int(u.get("completion_tokens") or 0)
        cr = int((u.get("prompt_tokens_details") or {}).get("cached_tokens") or 0)
        self.usage["input"] += inp - cr
        self.usage["output"] += out
        self.usage["cache_read"] += cr
        self.usage["cost"] += ((inp - cr) * m["input"] + cr * (m["cached"] or m["input"]) + out * m["output"]) / 1e6
        self.context = inp + out
        self.update({"sessionUpdate": "usage_update", "used": self.context, "size": m["context"]})
        log("requête %s : entrée %d (cache %d), sortie %d, fin %s ; séance ~%.4f $" % (
            self.model, inp, cr, out, finish, self.usage["cost"]))

    def cost_report(self):
        u = self.usage
        known = self.model_info()["output"] > 0
        cost = ("coût estimé %.4f $ (prix publics du fournisseur)" % u["cost"]) if known else \
            "coût inconnu (pas de prix pour ce modèle dans config.toml)"
        return ("Séance : %d jetons d'entrée, %d lus du cache, %d de sortie ; %s. Contexte actuel : %d jetons.\n" % (
            u["input"], u["cache_read"], u["output"], cost, self.context))

    # -- la reprise --

    def replay(self):
        for m in self.messages:
            role = m.get("role")
            c = m.get("content")
            if role in ("user", "assistant"):
                parts = [{"type": "text", "text": c}] if isinstance(c, str) else (c or [])
                if any(isinstance(p, dict) and p.get("type") == "image_url" for p in parts):
                    continue      # (les images des outils)
                for p in parts:
                    if isinstance(p, dict) and p.get("type") == "text" and p.get("text"):
                        self.text("user_message_chunk" if role == "user" else "agent_message_chunk", p["text"])
                        if role == "assistant":
                            self.text("agent_message_chunk", "\n")
                for tc in m.get("tool_calls") or []:
                    f = tc.get("function") or {}
                    try:
                        args = json.loads(f.get("arguments") or "{}")
                    except ValueError:
                        args = {}
                    name = f.get("name", "")
                    self.update({"sessionUpdate": "tool_call", "toolCallId": tc.get("id"),
                                 "title": "web_fetch: %s" % args.get("url", "") if name == "web_fetch"
                                 else T.title(name, args), "kind": T.KIND.get(name, "other"), "status": "completed"})
            elif role == "tool":
                txt = c if isinstance(c, str) else ""
                self.tool_update(m.get("tool_call_id"), "failed" if txt.startswith("Erreur : ") else "completed",
                                 T.clip(txt, 2000) if txt else None)


Cancelled = A.Cancelled


def _tool_msg(tid, text):
    return {"role": "tool", "tool_call_id": tid, "content": text or "(vide)"}


def _from_result(r, name, images):
    """Un tool_result de claude_acp (Session.call, call_mcp) -> un message « tool » ; ses images vont dans images."""
    c = r.get("content")
    if isinstance(c, list):
        parts = []
        for b in c:
            if b.get("type") == "text":
                parts.append(b.get("text", ""))
            elif b.get("type") == "image":
                s = b.get("source") or {}
                images.append({"type": "image_url", "image_url": {
                    "url": "data:%s;base64,%s" % (s.get("media_type", "image/png"), s.get("data", ""))}})
                parts.append("(image de %s : jointe au message suivant)" % name)
        c = "\n".join(parts)
    if r.get("is_error"):
        c = "Erreur : " + (c or "")
    return _tool_msg(r["tool_use_id"], c)
