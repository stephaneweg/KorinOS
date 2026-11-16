"""Les outils de l'agent : lire, écrire, modifier des fichiers, bash, glob, grep, les compétences, le plan.

Chaque outil a sa définition pour l'API (TOOLS) et son exécution (run). Les entrées viennent du modèle en flux
(eager_input_streaming) : l'API ne les valide plus, on les vérifie ici avant d'agir (validate).
"""
import glob as globmod
import os
import re
import signal
import subprocess
import time

MAX_OUT = 30000          # caractères de sortie rendus au modèle (le reste est coupé au milieu)
READ_LIMIT = 2000        # lignes lues par défaut
LINE_MAX = 2000          # caractères par ligne lue


def _schema(props, required):
    return {"type": "object", "properties": props, "required": required, "additionalProperties": False}


TOOLS = [
    {
        "name": "read_file",
        "description": (
            "Lit un fichier texte et le rend avec ses numéros de ligne (format « numéro<TAB>ligne »). Par défaut les "
            "2000 premières lignes ; offset (1 = première ligne) et limit pour un gros fichier. Chemin absolu "
            "(/..., /...) ou relatif au dossier de travail. Lire un fichier avant de le modifier."),
        "input_schema": _schema({
            "path": {"type": "string"},
            "offset": {"type": "integer", "description": "première ligne lue (1 = début)"},
            "limit": {"type": "integer", "description": "nombre de lignes"},
        }, ["path"]),
    },
    {
        "name": "write_file",
        "description": (
            "Écrit un fichier en entier (le crée, avec ses dossiers, ou le remplace). Pour changer une partie d'un "
            "fichier existant, préférer edit_file. Un fichier existant doit avoir été lu par read_file avant."),
        "input_schema": _schema({
            "path": {"type": "string"},
            "content": {"type": "string"},
        }, ["path", "content"]),
    },
    {
        "name": "edit_file",
        "description": (
            "Remplace old_string par new_string dans un fichier déjà lu par read_file. old_string doit être exact "
            "(espaces, tabulations et indentation compris, sans les numéros de ligne de read_file) et unique dans le "
            "fichier, sauf avec replace_all (toutes les occurrences)."),
        "input_schema": _schema({
            "path": {"type": "string"},
            "old_string": {"type": "string"},
            "new_string": {"type": "string"},
            "replace_all": {"type": "boolean"},
        }, ["path", "old_string", "new_string"]),
    },
    {
        "name": "bash",
        "description": (
            "Lance une commande par bash dans le dossier de travail (chaque appel repart de ce dossier : `cd x && ...`). "
            "Rend la sortie standard et d'erreur mêlées, et le code de retour. timeout en secondes (120 par défaut, "
            "600 au plus). Sous Korin : pas d'awk, de xargs, de sort -k ; regex sans {n,m} ; ne pas lancer une app "
            "graphique au premier plan (elle ne rend pas la main)."),
        "input_schema": _schema({
            "command": {"type": "string"},
            "timeout": {"type": "integer"},
            "description": {"type": "string", "description": "ce que fait la commande, en quelques mots"},
        }, ["command"]),
    },
    {
        "name": "glob",
        "description": (
            "Cherche des fichiers par motif (« **/*.c », « src/kits/*/*.abi ») sous path (le dossier de travail par "
            "défaut). Rend les chemins, les plus récents d'abord (200 au plus)."),
        "input_schema": _schema({
            "pattern": {"type": "string"},
            "path": {"type": "string"},
        }, ["pattern"]),
    },
    {
        "name": "grep",
        "description": (
            "Cherche une expression régulière dans les fichiers, par ripgrep (rg). output_mode : « files » (les "
            "fichiers qui la contiennent, par défaut) ou « content » (les lignes, avec leur numéro ; context : lignes "
            "autour). glob filtre les fichiers (« *.c »)."),
        "input_schema": _schema({
            "pattern": {"type": "string"},
            "path": {"type": "string"},
            "glob": {"type": "string"},
            "ignore_case": {"type": "boolean"},
            "output_mode": {"type": "string", "enum": ["files", "content"]},
            "context": {"type": "integer"},
        }, ["pattern"]),
    },
    {
        "name": "skill",
        "description": (
            "Charge une compétence de Korin (son SKILL.md, et la liste de ses fichiers) : ce qu'il faut savoir avant "
            "d'écrire du code ou d'agir sur le système dans son domaine. La liste des compétences est dans les "
            "consignes."),
        "input_schema": _schema({"name": {"type": "string"}}, ["name"]),
    },
    {
        "name": "update_plan",
        "description": (
            "Montre à l'utilisateur la liste des étapes d'une tâche longue et leur état (pending, in_progress, "
            "completed). Rendre toute la liste à chaque appel ; une seule étape in_progress."),
        "input_schema": _schema({
            "items": {
                "type": "array",
                "items": _schema({
                    "content": {"type": "string"},
                    "status": {"type": "string", "enum": ["pending", "in_progress", "completed"]},
                }, ["content", "status"]),
            },
        }, ["items"]),
    },
]

# l'entrée des outils en flux (eager_input_streaming) seulement avec le vrai jiter (Rust) : celui de Korin, en Python
# pur (tools/claude/jiter), relirait toute l'entrée à chaque morceau ; sans, elle arrive d'un bloc, validée par l'API
try:
    import jiter as _jiter
    EAGER = not getattr(_jiter, "__korin_shim__", False)
except ImportError:
    EAGER = False
if EAGER:
    for _t in TOOLS:
        _t["eager_input_streaming"] = True

# ce que chaque outil est pour ACP (kind) et s'il demande l'accord de l'utilisateur
KIND = {"read_file": "read", "write_file": "edit", "edit_file": "edit", "bash": "execute", "glob": "search",
        "grep": "search", "skill": "read", "update_plan": "think"}
EDITS = {"write_file", "edit_file"}


class ToolError(Exception):
    pass


def validate(name, args):
    """L'entrée en flux n'est pas validée par l'API : types et champs requis vérifiés ici."""
    spec = next((t for t in TOOLS if t["name"] == name), None)
    if spec is None:
        raise ToolError("outil inconnu : %s" % name)
    if not isinstance(args, dict):
        raise ToolError("entrée invalide (pas un objet JSON)")
    schema = spec["input_schema"]
    for k in schema["required"]:
        if k not in args:
            raise ToolError("champ requis manquant : %s" % k)
    types = {"string": str, "integer": int, "boolean": bool, "array": list}
    for k, v in args.items():
        p = schema["properties"].get(k)
        if p is None:
            raise ToolError("champ inconnu : %s" % k)
        t = types[p["type"]]
        if not isinstance(v, t) or (t is int and isinstance(v, bool)):
            raise ToolError("%s : %s attendu" % (k, p["type"]))
        if "enum" in p and v not in p["enum"]:
            raise ToolError("%s : une valeur parmi %s" % (k, ", ".join(p["enum"])))
    if name == "update_plan":
        for it in args["items"]:
            if not (isinstance(it, dict) and isinstance(it.get("content"), str)
                    and it.get("status") in ("pending", "in_progress", "completed")):
                raise ToolError("items : des objets {content, status}")


_VOLUME = re.compile(r"^[A-Za-z]+[0-9]*:/")


def resolve(cwd, path):
    """Un chemin du modèle : absolu (/..., /...), ~, ou relatif au dossier de travail."""
    if not path:
        raise ToolError("chemin vide")
    if path.startswith("~"):
        path = os.environ.get("HOME", "/") + path[1:]
    if not (path.startswith("/") or _VOLUME.match(path)):
        path = cwd.rstrip("/") + "/" + path
    m = _VOLUME.match(path)
    if m:  # normpath ne connaît pas « / » : seulement la suite
        return m.group(0)[:-1] + os.path.normpath("/" + path[m.end():])
    return os.path.normpath(path)


def clip(text, limit=MAX_OUT):
    if len(text) <= limit:
        return text
    half = limit // 2
    return text[:half] + "\n[... %d caractères coupés ...]\n" % (len(text) - 2 * half) + text[-half:]


def title(name, args):
    """Le titre de l'appel pour la façade (« bash: git status », « Lecture de main.c »)."""
    a = args if isinstance(args, dict) else {}
    if name == "bash":
        return "bash: " + (a.get("command") or "").strip().splitlines()[0][:200] if a.get("command") else "bash"
    if name in ("read_file", "write_file", "edit_file"):
        verb = {"read_file": "Lecture", "write_file": "Écriture", "edit_file": "Modification"}[name]
        return "%s de %s" % (verb, a.get("path", "?"))
    if name == "glob":
        return "glob: %s" % a.get("pattern", "")
    if name == "grep":
        return "grep: %s%s" % (a.get("pattern", ""), (" dans " + a["path"]) if a.get("path") else "")
    if name == "skill":
        return "Compétence %s" % a.get("name", "")
    if name == "update_plan":
        return "Plan"
    return name


def _read_text(path):
    with open(path, "rb") as f:
        data = f.read()
    if b"\0" in data[:8192]:
        raise ToolError("%s : fichier binaire" % path)
    return data.decode("utf-8", errors="replace")


def preview(name, args, cwd, state):
    """Ce que montre la demande d'accord : (content ACP, texte de l'ancien fichier pour un diff ou None)."""
    if name == "write_file":
        p = resolve(cwd, args["path"])
        old = ""
        if os.path.exists(p):
            try:
                old = _read_text(p)
            except (OSError, ToolError):
                old = ""
        return [{"type": "diff", "path": p, "oldText": old, "newText": args["content"]}]
    if name == "edit_file":
        p = resolve(cwd, args["path"])
        try:
            old = _read_text(p)
        except (OSError, ToolError):
            return []
        new = _edit(old, args, p)
        return [{"type": "diff", "path": p, "oldText": old, "newText": new}]
    if name == "bash":
        return [{"type": "content", "content": {"type": "text", "text": args["command"]}}]
    return []


def _edit(old, args, path):
    a, b = args["old_string"], args["new_string"]
    if a == b:
        raise ToolError("old_string et new_string sont identiques")
    if a == "":
        raise ToolError("old_string vide : pour créer un fichier, write_file")
    n = old.count(a)
    if n == 0:
        raise ToolError("%s : old_string introuvable (vérifier espaces et indentation, relire le fichier)" % path)
    if n > 1 and not args.get("replace_all"):
        raise ToolError("%s : old_string apparaît %d fois ; l'allonger pour qu'il soit unique, ou replace_all" % (path, n))
    return old.replace(a, b) if args.get("replace_all") else old.replace(a, b, 1)


class State:
    """Ce que les outils gardent d'un appel à l'autre dans une séance."""

    def __init__(self, cwd, skills):
        self.cwd = cwd
        self.skills = skills  # nom -> dossier
        self.read = {}        # chemin -> mtime à la lecture
        self.proc = None      # la commande bash en cours (pour l'interrompre)


def run(name, args, state, cancel):
    """Exécute un outil ; rend le texte du résultat ; lève ToolError (résultat en erreur pour le modèle)."""
    cwd = state.cwd
    if name == "read_file":
        p = resolve(cwd, args["path"])
        if os.path.isdir(p):
            raise ToolError("%s est un dossier (glob ou bash ls pour le lister)" % p)
        try:
            text = _read_text(p)
        except FileNotFoundError:
            raise ToolError("%s : fichier introuvable" % p)
        except OSError as e:
            raise ToolError("%s : %s" % (p, e.strerror or e))
        state.read[p] = _mtime(p)
        lines = text.splitlines()
        start = max(1, args.get("offset") or 1)
        limit = args.get("limit") or READ_LIMIT
        chunk = lines[start - 1:start - 1 + limit]
        out = []
        for i, line in enumerate(chunk, start):
            if len(line) > LINE_MAX:
                line = line[:LINE_MAX] + " [...]"
            out.append("%6d\t%s" % (i, line))
        if not lines:
            return "(fichier vide)"
        rest = len(lines) - (start - 1 + len(chunk))
        tail = "\n(%d lignes de plus : offset=%d)" % (rest, start + len(chunk)) if rest > 0 else ""
        return clip("\n".join(out)) + tail
    if name == "write_file":
        p = resolve(cwd, args["path"])
        if os.path.exists(p) and p not in state.read:
            raise ToolError("%s existe : le lire par read_file avant de le remplacer" % p)
        d = os.path.dirname(p)
        if d and not os.path.isdir(d):
            os.makedirs(d, exist_ok=True)
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(args["content"])
        state.read[p] = _mtime(p)
        return "%s écrit (%d lignes)" % (p, args["content"].count("\n") + (0 if args["content"].endswith("\n") else 1))
    if name == "edit_file":
        p = resolve(cwd, args["path"])
        if p not in state.read:
            raise ToolError("%s : le lire par read_file avant de le modifier" % p)
        try:
            old = _read_text(p)
        except FileNotFoundError:
            raise ToolError("%s : fichier introuvable" % p)
        if _mtime(p) != state.read[p]:
            raise ToolError("%s a changé depuis sa lecture : le relire" % p)
        new = _edit(old, args, p)
        with open(p, "w", encoding="utf-8", newline="") as f:
            f.write(new)
        state.read[p] = _mtime(p)
        return "%s modifié" % p
    if name == "bash":
        return _bash(args, state, cancel)
    if name == "glob":
        base = resolve(cwd, args.get("path") or cwd)
        if not os.path.isdir(base):
            raise ToolError("%s : pas un dossier" % base)
        found = globmod.glob(args["pattern"], root_dir=base, recursive=True)
        paths = [base.rstrip("/") + "/" + f for f in found]
        paths.sort(key=lambda f: -_mtime(f))
        if not paths:
            return "(aucun fichier)"
        more = "\n(%d de plus)" % (len(paths) - 200) if len(paths) > 200 else ""
        return "\n".join(paths[:200]) + more
    if name == "grep":
        base = resolve(cwd, args.get("path") or cwd)
        cmd = ["rg", "--color=never", "--no-heading", "--max-columns=500"]
        if args.get("ignore_case"):
            cmd.append("-i")
        if args.get("glob"):
            cmd += ["--glob", args["glob"]]
        if args.get("output_mode") == "content":
            cmd.append("-n")
            if args.get("context"):
                cmd += ["-C", str(args["context"])]
        else:
            cmd.append("-l")
        cmd += ["-e", args["pattern"], base]
        try:
            r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL,
                               timeout=120)
        except FileNotFoundError:
            raise ToolError("rg introuvable (pkg install rg)")
        except subprocess.TimeoutExpired:
            raise ToolError("grep : plus de 120 s")
        if r.returncode == 1:
            return "(rien trouvé)"
        if r.returncode not in (0, 1):
            raise ToolError("rg : " + r.stderr.decode("utf-8", "replace").strip()[:2000])
        return clip(r.stdout.decode("utf-8", "replace"))
    if name == "skill":
        d = state.skills.get(args["name"])
        if not d:
            raise ToolError("compétence inconnue : %s (connues : %s)" % (args["name"], ", ".join(sorted(state.skills))))
        text = _read_text(os.path.join(d, "SKILL.md"))
        files = []
        for root, _dirs, names in os.walk(d):
            for n in names:
                rel = os.path.relpath(os.path.join(root, n), d)
                if rel != "SKILL.md":
                    files.append(rel)
        extra = ""
        if files:
            files.sort()
            extra = "\n\n---\nDossier de la compétence : %s\nSes autres fichiers (read_file) :\n%s" % (
                d, "\n".join(files[:200]))
        return clip(text + extra, 60000)
    if name == "update_plan":
        return "Plan montré à l'utilisateur."
    raise ToolError("outil inconnu : %s" % name)


def _mtime(p):
    try:
        return os.stat(p).st_mtime
    except OSError:
        return 0


def _bash(args, state, cancel):
    timeout = min(max(args.get("timeout") or 120, 1), 600)
    shell = "/bin/bash" if os.path.exists("/bin/bash") else "/bin/sh"
    env = dict(os.environ)
    env.setdefault("TERM", "dumb")
    env["PAGER"] = "cat"
    env["GIT_PAGER"] = "cat"
    try:
        p = subprocess.Popen([shell, "-c", args["command"]], cwd=state.cwd, stdin=subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, start_new_session=True)
    except OSError as e:
        raise ToolError("bash : %s" % (e.strerror or e))
    state.proc = p
    out = b""
    t0 = time.monotonic()
    why = None
    try:
        while True:
            try:
                out, _ = p.communicate(timeout=0.25)
                break
            except subprocess.TimeoutExpired:
                if cancel.is_set():
                    why = "interrompu par l'utilisateur"
                elif time.monotonic() - t0 > timeout:
                    why = "arrêté après %d s (timeout)" % timeout
                if why:
                    _kill(p)
                    out, _ = p.communicate()
                    break
    finally:
        state.proc = None
    text = clip(out.decode("utf-8", "replace"))
    if why:
        return (text + "\n" if text else "") + "[%s]" % why
    if p.returncode:
        return (text + "\n" if text else "") + "[code de retour %d]" % p.returncode
    return text if text else "(aucune sortie)"


def _kill(p):
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(p.pid, sig)
        except (OSError, AttributeError):
            try:
                p.send_signal(sig)
            except OSError:
                pass
        try:
            p.wait(1)
            return
        except subprocess.TimeoutExpired:
            pass
