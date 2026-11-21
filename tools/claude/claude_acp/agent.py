"""Les séances : la conversation avec Claude, la boucle des outils, les accords, la mémoire sur disque.

Une séance = un dossier de travail, un historique (messages de l'API, en dicts JSON, rejoués tels quels : la pensée
préservée des modèles récents exige un historique qu'on ne fait que prolonger), un mode d'accord, un modèle.
Un tour (session/prompt) tourne dans son fil ; Échap (session/cancel) le coupe entre deux événements du flux ou
pendant une commande.
"""
import datetime
import json
import os
import sys
import threading
import time
import tomllib
import uuid

from . import __version__
from . import mcp as M
from . import tools as T

# les modèles offerts (option « model » de la séance) : (id, nom, prix $/Mtok entrée, sortie, lecture du cache,
# fenêtre de contexte, fallbacks côté serveur)
MODELS = [
    ("claude-opus-5-5", "Claude Opus 5.5", 4.0, 20.0, 0.20, 1_000_000, True),
    ("claude-sonnet-5-5", "Claude Sonnet 5.5", 2.0, 10.0, 0.10, 1_000_000, True),
    ("claude-haiku-5-5", "Claude Haiku 5.5", 0.10, 0.50, 0.01, 1_000_000, False),
    ("claude-fable-5-2", "Claude Fable 5.2", 8.0, 40.0, 0.20, 1_000_000, True),
]
MODEL_IDS = [m[0] for m in MODELS]

MODES = [
    ("ask", "Demander", "Demande votre accord avant chaque modification de fichier et chaque commande"),
    ("accept-edits", "Accepter les modifications", "Modifie les fichiers sans demander ; demande avant chaque commande"),
    ("auto-approve", "Tout accepter", "Ne demande rien"),
]

FALLBACK_BETA = "server-side-fallback-2026-07-01"
COMPACT_BETA = "compact-2026-01-12"


# le dossier de l'agent : sa variable d'environnement et son nom sous $HOME/.local, comme les données des autres apps
# (docs/etude/MULTI-UTILISATEUR.md, question 4 ; libacp, acp_home, donne le même) ; oai_acp, l'agent compatible
# OpenAI, reprend ce module avec les siens : MAI_ACP_HOME, mai-acp
HOME_ENV = "CLAUDE_ACP_HOME"
HOME_DIR = "claude-acp"


def home():
    h = os.environ.get(HOME_ENV)
    if not h:
        h = os.path.join(os.environ.get("HOME") or "/", ".local", HOME_DIR)
    return h


def log(*a):
    t = datetime.datetime.now(datetime.timezone.utc).strftime("%H:%M:%S")
    print("[%s]" % t, *a, file=sys.stderr, flush=True)


# ---- la configuration ($CLAUDE_ACP_HOME/config.toml et .env) ----

DEFAULTS = {
    "model": "claude-opus-5-5",
    "effort": "high",          # low, medium, high, xhigh, max
    "max_tokens": 64000,
    "web": True,               # recherche et lecture de pages web (outils côté serveur)
    "compaction": True,        # résumé côté serveur des longues séances
    "fallbacks": True,         # refus d'un modèle : un autre reprend (côté serveur)
}


def load_config():
    cfg = dict(DEFAULTS)
    p = os.path.join(home(), "config.toml")
    if os.path.exists(p):
        try:
            with open(p, "rb") as f:
                cfg.update(tomllib.load(f))
        except (OSError, ValueError) as e:
            log("config.toml illisible :", e)
    if cfg["model"] not in MODEL_IDS:
        log("modèle inconnu dans config.toml :", cfg["model"])
        cfg["model"] = DEFAULTS["model"]
    return cfg


def load_env():
    """$CLAUDE_ACP_HOME/.env : ANTHROPIC_API_KEY=... (et d'autres variables), sans écraser l'environnement."""
    p = os.path.join(home(), ".env")
    if not os.path.exists(p):
        return
    with open(p, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            k = k.strip()
            v = v.strip().strip('"').strip("'")
            if k and v and not os.environ.get(k):
                os.environ[k] = v


def have_key():
    return bool(os.environ.get("ANTHROPIC_API_KEY") or os.environ.get("ANTHROPIC_AUTH_TOKEN"))


# ---- les consignes (le prompt système) ----

BASE = """Tu es Claude, un agent de code qui tourne directement sur Korin OS, par la façade de Korin (claude-acp \
%(version)s, sur l'API de Claude). L'utilisateur te demande de créer, modifier, construire et essayer des \
applications, des commandes et le système lui-même (noyau, services, kits) : tu agis par tes outils sur la machine \
où tu tournes.

# Comment travailler
- Lis avant d'agir : explore le code concerné (glob, grep, read_file) et charge la compétence du domaine (outil \
skill) avant d'écrire du code pour Korin ; Korin n'est pas Linux et son API ne se devine pas.
- Fais le travail demandé, en entier, puis vérifie-le : construis, lance les essais, relis la sortie. Ne dis pas \
qu'une chose marche sans l'avoir vu.
- Préfère edit_file à write_file pour changer un fichier existant ; garde le style et les conventions du code autour.
- Une tâche de plusieurs étapes : montre-les par update_plan et tiens-les à jour.
- Les commandes sont lentes sous Korin (une compilation C : quelques secondes à une minute) : ne relance pas une \
commande parce qu'elle tarde ; donne un timeout plus long aux longues constructions.
- Avant une action difficile à défaire (effacer, écraser des données, toucher à la carte système, au démarrage, \
formater, pousser sur un dépôt distant), demande d'abord à l'utilisateur.
- Si l'utilisateur refuse une action, arrête-toi et demande-lui comment continuer.
- Réponds dans la langue de l'utilisateur (le français par défaut), brièvement : ce qui a été fait, ce qui reste, \
ce qui n'a pas été vérifié. Du Markdown simple (le terminal le rend en partie).
- Les chemins que tu montres : absolus, en forme POSIX (/...).
"""


def find_skills():
    """Les compétences (nom -> dossier, description) : $CLAUDE_ACP_HOME/skills/<nom>/SKILL.md, puis celles des apps
    installées (app_skills) ; celle d'une app l'emporte sur une copie du même nom dans skills/ (une ancienne copie
    d'avant que la compétence vienne avec son app)."""
    skills, desc = {}, {}
    d = os.path.join(home(), "skills")
    if os.path.isdir(d):
        for name in sorted(os.listdir(d)):
            f = os.path.join(d, name, "SKILL.md")
            if not os.path.isfile(f):
                continue
            skills[name] = os.path.join(d, name)
            desc[name] = _frontmatter(f).get("description", "")
    for name, (adir, text) in app_skills().items():
        skills[name] = adir
        desc[name] = text
    return dict(sorted(skills.items())), dict(sorted(desc.items()))


# le dossier des apps installées (docs/ABI.md) ; un essai hors de Korin le change par CLAUDE_ACP_APPS
APPS_ENV = "CLAUDE_ACP_APPS"


def app_skills():
    """Les compétences des apps : /apps/<app>.app/app.txt a la clé `skill = SKILL.md` (docs/ABI.md), le fichier à
    côté du manifeste, avec son frontmatter `name` et `description` comme les autres (Vibe le trouve au même endroit
    par VIBE_SKILL_PATHS, que libacp donne). Nom -> (dossier de l'app, description). Une app sans la clé n'en a pas,
    même avec un SKILL.md."""
    out = {}
    base = os.environ.get(APPS_ENV) or "/apps"
    try:
        names = sorted(os.listdir(base))
    except OSError:
        return out
    for app in names:
        adir = os.path.join(base, app)
        if _manifest_key(os.path.join(adir, "app.txt"), "skill") != "SKILL.md":
            continue
        f = os.path.join(adir, "SKILL.md")
        if not os.path.isfile(f):
            continue
        fm = _frontmatter(f)
        name = fm.get("name") or app.split(".")[0]
        out[name] = (adir, fm.get("description", ""))
    return out


def _manifest_key(path, key):
    """La valeur d'une clé d'un app.txt (`clé = valeur`, `#` commence un commentaire), ou None."""
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                line = line.split("#", 1)[0]
                if "=" not in line:
                    continue
                k, v = line.split("=", 1)
                if k.strip() == key:
                    return v.strip()
    except OSError:
        pass
    return None


def _frontmatter(path):
    out = {}
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read(8192)
    except OSError:
        return out
    if not text.startswith("---"):
        return out
    end = text.find("\n---", 3)
    for line in text[3:end].splitlines():
        if ":" in line and not line.startswith(" "):
            k, v = line.split(":", 1)
            out[k.strip()] = v.strip().strip('"')
    return out


def project_notes(cwd):
    """AGENTS.md ou CLAUDE.md du dossier de travail et de ses parents (le plus proche en dernier)."""
    found = []
    d = cwd
    seen = set()
    while d and d not in seen:
        seen.add(d)
        for n in ("AGENTS.md", "CLAUDE.md"):
            p = os.path.join(d, n)
            if os.path.isfile(p):
                try:
                    with open(p, encoding="utf-8", errors="replace") as f:
                        found.append((p, f.read(60000)))
                except OSError:
                    pass
                break
        parent = os.path.dirname(d.rstrip("/"))
        if not parent or parent == d:
            break
        d = parent
    return list(reversed(found))


def system_prompt(cwd, skill_desc, base=None):
    """Deux blocs : le fixe (le même pour toutes les séances, en cache) et celui de la séance."""
    fixed = base if base is not None else BASE % {"version": __version__}
    agents = os.path.join(home(), "AGENTS.md")
    if os.path.isfile(agents):
        with open(agents, encoding="utf-8", errors="replace") as f:
            fixed += "\n# Korin OS\n\n" + f.read()
    if skill_desc:
        fixed += "\n# Compétences (outil skill)\n\n" + "\n".join(
            "- %s : %s" % (n, d) for n, d in skill_desc.items())
    today = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    sess = "# Cette séance\n\n- Dossier de travail : %s\n- Date (UTC) : %s\n" % (cwd, today)
    for p, text in project_notes(cwd):
        sess += "\n## Consignes du projet (%s)\n\n%s\n" % (p, text)
    return [
        {"type": "text", "text": fixed, "cache_control": {"type": "ephemeral"}},
        {"type": "text", "text": sess},
    ]


# ---- une séance ----

def dump(block):
    """Un bloc de la réponse, en dict JSON tel que l'API l'a donné (rejoué à l'identique)."""
    if isinstance(block, dict):
        return block
    return block.model_dump(mode="json", exclude_unset=True)


class Cancelled(Exception):
    pass


class Session:
    def __init__(self, server, sid, cwd, cfg, acp_mcp=None):
        self.server = server
        self.conn = server.conn
        self.id = sid
        self.cwd = cwd
        self.cfg = cfg
        self.model = cfg["model"]
        self.mode = "ask"
        self.messages = []
        self.title = ""
        self.created = _now_iso()
        self.updated = self.created
        self.allowed = set()      # outils permis « toujours » dans cette séance
        self.usage = {"input": 0, "output": 0, "cache_read": 0, "cache_write": 0, "cost": 0.0}
        self.context = 0
        skills, desc = find_skills()
        self.state = T.State(cwd, skills)
        self.system = system_prompt(cwd, desc)
        self.cancel = threading.Event()
        self.turn = None
        self.no_fallbacks = not cfg["fallbacks"]
        self.no_compaction = not cfg["compaction"]
        self.mcp = M.Registry(M.from_config(cfg, acp_mcp))   # (serveurs MCP : Forge...)

    # -- ACP --

    def update(self, upd):
        self.conn.notify("session/update", {"sessionId": self.id, "update": upd})

    def text(self, kind, text):
        if text:
            self.update({"sessionUpdate": kind, "content": {"type": "text", "text": text}})

    def models(self):
        """Les modèles offerts : [(id, nom)]."""
        return [(m[0], m[1]) for m in MODELS]

    def config_options(self):
        return [{
            "id": "model", "name": "Modèle", "type": "select", "currentValue": self.model,
            "options": [{"value": i, "name": n} for i, n in self.models()],
        }]

    def modes(self):
        return {"currentModeId": self.mode,
                "availableModes": [{"id": i, "name": n, "description": d} for i, n, d in MODES]}

    def opened(self):
        return {"sessionId": self.id, "modes": self.modes(), "configOptions": self.config_options()}

    def set_mode(self, mode):
        if mode not in [m[0] for m in MODES]:
            raise ValueError("mode inconnu : %s" % mode)
        self.mode = mode
        self.update({"sessionUpdate": "current_mode_update", "currentModeId": mode})

    def set_model(self, model):
        if model not in [m[0] for m in self.models()]:
            raise ValueError("modèle inconnu : %s" % model)
        self.model = model
        self.save()

    # -- un tour --

    def prompt(self, blocks):
        text = "\n".join(b.get("text", "") for b in blocks if b.get("type") == "text").strip()
        self.cancel.clear()
        if text == "/cost":
            self.text("agent_message_chunk", self.cost_report())
            return "end_turn"
        if not self.title:
            self.title = text.splitlines()[0][:80] if text else "séance"
        content = [{"type": "text", "text": text or "(vide)"}]
        # un tour interrompu ou des résultats d'outils laissent un message « user » en dernier : on le prolonge
        if self.messages and self.messages[-1]["role"] == "user":
            self.messages[-1]["content"] = _as_list(self.messages[-1]["content"]) + content
        else:
            self.messages.append({"role": "user", "content": content})
        try:
            return self.loop()
        except Cancelled:
            return "cancelled"
        finally:
            self.updated = _now_iso()
            self.save()

    def loop(self):
        steps = 0
        while True:
            steps += 1
            if steps > 200:
                self.text("agent_message_chunk", "\n(200 appels de suite : je m'arrête ici.)")
                return "max_turn_requests"
            msg = self.request()
            self.messages.append({"role": "assistant", "content": [dump(b) for b in msg.content]})
            self.account(msg)
            stop = msg.stop_reason
            if stop == "pause_turn":
                continue
            if stop == "refusal":
                det = getattr(msg, "stop_details", None)
                why = getattr(det, "explanation", None) if det else None
                self.text("agent_message_chunk", "\n(Claude a refusé de continuer%s.)" % (" : " + why if why else ""))
                return "refusal"
            uses = [b for b in msg.content if b.type == "tool_use"]
            if not uses:
                return "max_tokens" if stop == "max_tokens" else "end_turn"
            results = []
            for i, b in enumerate(uses):
                if self.cancel.is_set():
                    for rest in uses[i:]:
                        results.append(_result(rest.id, "Interrompu par l'utilisateur.", True))
                    self.messages.append({"role": "user", "content": results})
                    raise Cancelled()
                if stop == "max_tokens":
                    results.append(_result(b.id, "Réponse coupée (max_tokens) : entrée incomplète, rien n'a été fait. "
                                           "Découper le travail (fichiers plus petits, edit_file).", True))
                    self.tool_update(b.id, "failed", "entrée coupée (max_tokens)")
                    continue
                results.append(self.call(b))
            self.messages.append({"role": "user", "content": results})
            if self.cancel.is_set():
                raise Cancelled()

    def request(self):
        """Une requête en flux ; le texte et la pensée partent vers la façade au fil de l'eau."""
        import anthropic
        tries = 0
        while True:
            tries += 1
            try:
                return self._stream()
            except Cancelled:
                raise
            except ValueError as e:
                # une entrée d'outil en JSON illisible (flux) : le tour est repris, deux fois au plus
                log("flux : JSON d'outil illisible :", e)
                if tries > 2:
                    raise
            except anthropic.BadRequestError as e:
                msg = str(e)
                # un réglage que ce modèle ou ce compte n'accepte pas : sans lui, une fois
                if "fallback" in msg and not self.no_fallbacks:
                    log("fallbacks refusés, désactivés :", msg)
                    self.no_fallbacks = True
                elif ("compact" in msg or "context_management" in msg) and not self.no_compaction:
                    log("compaction refusée, désactivée :", msg)
                    self.no_compaction = True
                else:
                    raise

    def _params(self):
        m = next(x for x in MODELS if x[0] == self.model)
        tools = list(T.TOOLS) + self.mcp.api_tools()
        if self.cfg["web"]:
            tools += [{"type": "web_search_20260209", "name": "web_search", "max_uses": 5},
                      {"type": "web_fetch_20260209", "name": "web_fetch", "max_uses": 30}]
        p = dict(
            model=self.model, max_tokens=int(self.cfg["max_tokens"]), system=self.system, tools=tools,
            messages=self.messages, cache_control={"type": "ephemeral"},
            thinking={"type": "adaptive", "display": "summarized"},
            output_config={"effort": self.cfg["effort"]},
        )
        betas = []
        if m[6] and not self.no_fallbacks:
            betas.append(FALLBACK_BETA)
            p["fallbacks"] = "default"
        if not self.no_compaction:
            betas.append(COMPACT_BETA)
            p["context_management"] = {"edits": [{"type": "compact_20260112"}]}
        if betas:
            p["betas"] = betas
        return p

    def _stream(self):
        client = self.server.client()
        shown = {}  # index du bloc -> (id, type) des appels d'outils serveur montrés
        with client.beta.messages.stream(**self._params()) as stream:
            for ev in stream:
                if self.cancel.is_set():
                    stream.close()
                    raise Cancelled()
                t = ev.type
                if t == "text":
                    self.text("agent_message_chunk", ev.text)
                elif t == "thinking":
                    self.text("agent_thought_chunk", ev.thinking)
                elif t == "content_block_stop":
                    b = ev.content_block
                    if b.type == "thinking":
                        self.text("agent_thought_chunk", "\n")
                    elif b.type == "server_tool_use":
                        inp = b.input if isinstance(b.input, dict) else {}
                        what = inp.get("query") or inp.get("url") or ""
                        self.update({"sessionUpdate": "tool_call", "toolCallId": b.id,
                                     "title": "%s: %s" % (b.name, what),
                                     "kind": "search" if b.name == "web_search" else "fetch",
                                     "status": "in_progress"})
                        shown[b.id] = b.name
                    elif b.type in ("web_search_tool_result", "web_fetch_tool_result"):
                        tid = getattr(b, "tool_use_id", None)
                        if tid in shown:
                            c = b.content
                            err = (isinstance(c, dict) and "error_code" in c) or getattr(c, "error_code", None)
                            self.update({"sessionUpdate": "tool_call_update", "toolCallId": tid,
                                         "status": "failed" if err else "completed"})
                    elif b.type == "compaction":
                        self.text("agent_thought_chunk", "(contexte résumé pour continuer la séance)\n")
            if self.cancel.is_set():
                raise Cancelled()
            return stream.get_final_message()

    def tool_update(self, tid, status, text=None, content=None):
        u = {"sessionUpdate": "tool_call_update", "toolCallId": tid, "status": status}
        if content is not None:
            u["content"] = content
        elif text is not None:
            u["content"] = [{"type": "content", "content": {"type": "text", "text": text}}]
        self.update(u)

    def call(self, b):
        """Un appel d'outil : vérification, accord, exécution, résultat."""
        name, args = b.name, b.input
        if self.mcp.owns(name):
            return self.call_mcp(b)
        kind = T.KIND.get(name, "other")
        try:
            T.validate(name, args)
        except T.ToolError as e:
            self.update({"sessionUpdate": "tool_call", "toolCallId": b.id, "title": name, "kind": kind,
                         "status": "failed"})
            return _result(b.id, "Entrée invalide : %s. Refaire l'appel avec une entrée JSON complète." % e, True)
        title = T.title(name, args)
        if name == "update_plan":
            self.update({"sessionUpdate": "plan", "entries": [
                {"content": it["content"], "priority": "medium", "status": it["status"]} for it in args["items"]]})
            return _result(b.id, "Plan montré à l'utilisateur.")
        content = []
        try:
            content = T.preview(name, args, self.cwd, self.state)
        except T.ToolError:
            pass
        except OSError:
            pass
        self.update({"sessionUpdate": "tool_call", "toolCallId": b.id, "title": title, "kind": kind,
                     "status": "pending", "content": content, "rawInput": args})
        if self.needs_permission(name):
            answer = self.ask(b.id, title, kind, content, name)
            if answer == "cancelled":
                # la façade a annulé la question (Échap) : le tour s'arrête ; les autres appels du même message
                # reçoivent leur résultat dans loop
                self.tool_update(b.id, "failed", "annulé")
                self.cancel.set()
                return _result(b.id, "Interrompu par l'utilisateur.", True)
            if answer == "reject":
                self.tool_update(b.id, "failed", "refusé par l'utilisateur")
                return _result(b.id, "L'utilisateur a refusé cette action. Arrête-toi et demande-lui comment "
                               "continuer.", True)
        self.tool_update(b.id, "in_progress")
        try:
            out = T.run(name, args, self.state, self.cancel)
        except T.ToolError as e:
            self.tool_update(b.id, "failed", str(e))
            return _result(b.id, str(e), True)
        except Exception as e:
            log("outil %s :" % name, repr(e))
            self.tool_update(b.id, "failed", "%s: %s" % (type(e).__name__, e))
            return _result(b.id, "%s: %s" % (type(e).__name__, e), True)
        shown = out if name in ("bash", "grep", "glob") else None
        if name in T.EDITS:
            self.tool_update(b.id, "completed", content=content or None)
        else:
            self.tool_update(b.id, "completed", text=T.clip(shown, 4000) if shown else None)
        return _result(b.id, out)

    def call_mcp(self, b):
        """Un outil d'un serveur MCP (forge_add_box...) : l'accord pour ce qui change, puis l'appel."""
        name, args = b.name, b.input if isinstance(b.input, dict) else {}
        ro = self.mcp.read_only(name)
        srv = self.mcp.server_of(name)
        title = "%s : %s" % (srv, name[len(srv) + 1:])
        kind = "read" if ro else "other"
        self.update({"sessionUpdate": "tool_call", "toolCallId": b.id, "title": title, "kind": kind,
                     "status": "pending", "rawInput": args})
        if not ro and self.mode not in ("auto-approve",) and "mcp:" + srv not in self.allowed:
            answer = self.ask(b.id, title, kind, [], "mcp:" + srv)
            if answer == "cancelled":
                self.tool_update(b.id, "failed", "annulé")
                self.cancel.set()
                return _result(b.id, "Interrompu par l'utilisateur.", True)
            if answer == "reject":
                self.tool_update(b.id, "failed", "refusé par l'utilisateur")
                return _result(b.id, "L'utilisateur a refusé cette action. Arrête-toi et demande-lui comment "
                               "continuer.", True)
        self.tool_update(b.id, "in_progress")
        blocks, err = self.mcp.call(name, args)
        txt = "\n".join(x["text"] for x in blocks if x["type"] == "text")
        self.tool_update(b.id, "failed" if err else "completed", T.clip(txt, 2000) if txt else None)
        r = {"type": "tool_result", "tool_use_id": b.id, "content": blocks}
        if err:
            r["is_error"] = True
        return r

    def needs_permission(self, name):
        if name not in T.EDITS and name != "bash":
            return False
        if self.mode == "auto-approve" or name in self.allowed:
            return False
        if self.mode == "accept-edits" and name in T.EDITS:
            return False
        return True

    def ask(self, tid, title, kind, content, name):
        what = ("les commandes" if name == "bash" else "les actions de %s" % name[4:] if name.startswith("mcp:")
                else "les modifications de fichiers")
        opts = [
            {"optionId": "allow", "name": "Oui", "kind": "allow_once"},
            {"optionId": "always", "name": "Oui, et %s sans demander dans cette séance" % what, "kind": "allow_always"},
            {"optionId": "reject", "name": "Non", "kind": "reject_once"},
        ]
        try:
            r = self.conn.request("session/request_permission", {
                "sessionId": self.id,
                "toolCall": {"toolCallId": tid, "title": title, "kind": kind, "status": "pending", "content": content},
                "options": opts,
            }, cancel=self.cancel)
        except Exception as e:
            log("request_permission :", e)
            return "reject"
        if r is None:
            return "cancelled"
        out = r.get("outcome") or {}
        if out.get("outcome") != "selected":
            return "cancelled"
        oid = out.get("optionId")
        if oid == "always":
            if name == "bash" or name.startswith("mcp:"):
                self.allowed.add(name)
            else:
                self.allowed.update(T.EDITS)
            return "allow"
        return "allow" if oid == "allow" else "reject"

    # -- l'usage et le coût --

    def account(self, msg):
        u = msg.usage
        # (après un fallback, msg.model peut être un modèle hors de la table : les prix du modèle choisi)
        mid = getattr(msg, "model", None)
        m = next(x for x in MODELS if x[0] == (mid if mid in MODEL_IDS else self.model))
        inp = u.input_tokens or 0
        out = u.output_tokens or 0
        cr = getattr(u, "cache_read_input_tokens", 0) or 0
        cw = getattr(u, "cache_creation_input_tokens", 0) or 0
        self.usage["input"] += inp
        self.usage["output"] += out
        self.usage["cache_read"] += cr
        self.usage["cache_write"] += cw
        self.usage["cost"] += (inp * m[2] + cw * m[2] * 1.25 + cr * m[4] + out * m[3]) / 1e6
        self.context = inp + cr + cw + out
        self.update({"sessionUpdate": "usage_update", "used": self.context, "size": m[5]})
        log("requête %s : entrée %d, cache lu %d, cache écrit %d, sortie %d, arrêt %s ; séance ~%.4f $" % (
            getattr(msg, "model", self.model), inp, cr, cw, out, msg.stop_reason, self.usage["cost"]))

    def cost_report(self):
        u = self.usage
        return ("Séance : %d jetons d'entrée, %d lus du cache, %d écrits au cache, %d de sortie ; coût estimé "
                "%.4f $ (prix publics de l'API, hors recherche web). Contexte actuel : %d jetons.\n" % (
                    u["input"], u["cache_read"], u["cache_write"], u["output"], u["cost"], self.context))

    # -- sur disque ($CLAUDE_ACP_HOME/sessions/<id>.json) --

    def path(self):
        return os.path.join(home(), "sessions", self.id + ".json")

    def save(self):
        d = os.path.dirname(self.path())
        os.makedirs(d, exist_ok=True)
        data = {"id": self.id, "cwd": self.cwd, "title": self.title, "model": self.model, "mode": self.mode,
                "created": self.created, "updated": self.updated, "usage": self.usage, "context": self.context,
                "messages": self.messages}
        tmp = self.path() + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False)
        os.replace(tmp, self.path())

    @classmethod
    def load(cls, server, sid, cwd, cfg):
        p = os.path.join(home(), "sessions", sid + ".json")
        with open(p, encoding="utf-8") as f:
            data = json.load(f)
        s = cls(server, sid, cwd or data["cwd"], cfg)
        s.messages = data["messages"]
        s.title = data.get("title", "")
        s.model = data["model"] if data.get("model") in [m[0] for m in s.models()] else s.model
        s.mode = data.get("mode", "ask")
        s.created = data.get("created", s.created)
        s.updated = data.get("updated", s.updated)
        s.usage.update(data.get("usage", {}))
        s.context = data.get("context", 0)
        return s

    def replay(self):
        """session/load : l'historique rejoué vers la façade (texte, appels d'outils et leurs résultats)."""
        for m in self.messages:
            for b in _as_list(m["content"]):
                t = b.get("type")
                if t == "text":
                    self.text("user_message_chunk" if m["role"] == "user" else "agent_message_chunk", b["text"])
                    if m["role"] == "assistant":
                        self.text("agent_message_chunk", "\n")
                elif t == "tool_use":
                    self.update({"sessionUpdate": "tool_call", "toolCallId": b["id"],
                                 "title": T.title(b["name"], b.get("input")), "kind": T.KIND.get(b["name"], "other"),
                                 "status": "completed"})
                elif t == "tool_result":
                    c = b.get("content")
                    txt = c if isinstance(c, str) else "\n".join(x.get("text", "") for x in (c or []) if isinstance(x, dict))
                    self.tool_update(b["tool_use_id"], "failed" if b.get("is_error") else "completed",
                                     T.clip(txt, 2000) if txt else None)


def list_sessions(cwd=None):
    d = os.path.join(home(), "sessions")
    out = []
    if not os.path.isdir(d):
        return out
    for n in os.listdir(d):
        if not n.endswith(".json"):
            continue
        try:
            with open(os.path.join(d, n), encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, ValueError):
            continue
        if cwd and data.get("cwd") != cwd:
            continue
        out.append({"sessionId": data["id"], "cwd": data.get("cwd", ""), "title": data.get("title", ""),
                    "updatedAt": data.get("updated", "")})
    out.sort(key=lambda s: s["updatedAt"], reverse=True)
    return out


def new_id():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S-") + uuid.uuid4().hex[:8]


def _now_iso():
    return datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def _as_list(c):
    return [{"type": "text", "text": c}] if isinstance(c, str) else list(c)


def _result(tid, text, error=False):
    r = {"type": "tool_result", "tool_use_id": tid, "content": text if text else "(vide)"}
    if error:
        r["is_error"] = True
    return r
