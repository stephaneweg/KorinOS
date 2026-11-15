#!/bin/python3
"""vibe-account : les comptes de fournisseurs de modèles de Vibe sous Korin (docs/PYTHON-PORT.md, « Comptes et
choix du modèle »). Installé en /bin/vibe-account ; l'app Vibe (son assistant « Ajouter un compte ») et le client
terminal agent s'en servent, on peut aussi le lancer au Terminal.

    vibe-account presets                       les fournisseurs connus (JSON)
    vibe-account list [--json]                 les comptes : fournisseur, adresse, clé posée ou non, modèles
    vibe-account add PROVIDER [--api-base URL] [--env VAR] [--label TEXTE]
                                               déclare un fournisseur compatible OpenAI dans config.toml
    vibe-account set-key PROVIDER              lit la clé sur l'entrée standard (une ligne) et la range dans
                                               $VIBE_HOME/.env (mode 600) ; elle n'est jamais affichée
    vibe-account models PROVIDER [--json]      les modèles que le fournisseur annonce (GET <api_base>/models,
                                               avec la clé) ; sans réponse : code 2
    vibe-account add-model PROVIDER MODEL [--alias A] [--label TEXTE]
    vibe-account use ALIAS                     le modèle des prochaines séances (active_model de config.toml)
    vibe-account remove PROVIDER               retire le fournisseur, ses modèles et sa clé
    vibe-account open-keys PROVIDER            ouvre la page des clés du fournisseur dans Jet
    vibe-account wizard                        l'assistant au terminal (les mêmes étapes que l'app)

Où : $VIBE_HOME (par défaut ~/.vibe, /.vibe sous Korin), config.toml et .env, ceux que lit vibe-acp.
config.toml est lu par tomllib et récrit par tomli_w (celui de Vibe), comme Vibe le fait lui-même quand on change de
modèle : les autres réglages sont gardés, pas les commentaires. Un fournisseur compatible OpenAI est un [[providers]]
(api_style « openai », backend « generic ») et ses modèles des [[models]] ; Mistral est déjà connu de Vibe (connexion
par le navigateur, ou une clé MISTRAL_API_KEY).

Ni clé ni identifiant dans ce fichier : chaque clé vient de l'utilisateur, qui la copie depuis la page de son
fournisseur.
"""
import json
import os
import re
import subprocess
import sys

import tomllib

# les fournisseurs connus : api_base, variable de la clé, page des clés (à ouvrir dans Jet)
PRESETS = [
    {"name": "mistral", "label": "Mistral AI", "api_base": "https://api.mistral.ai/v1", "env": "MISTRAL_API_KEY",
     "keys_url": "https://console.mistral.ai/api-keys", "builtin": True,
     "note": "Connexion par le navigateur (Vibe), ou une clé d'API de la console de Mistral."},
    {"name": "moonshot", "label": "Kimi (Moonshot AI)", "api_base": "https://api.moonshot.ai/v1",
     "env": "MOONSHOT_API_KEY", "keys_url": "https://platform.moonshot.ai/console/api-keys", "builtin": False,
     "note": "API compatible OpenAI ; les modèles Kimi (K2, K3...) se listent avec la clé."},
    {"name": "custom", "label": "Autre (compatible OpenAI)", "api_base": "", "env": "", "keys_url": "",
     "builtin": False, "note": "Une adresse d'API compatible OpenAI (…/v1), un nom et une clé."},
]
JET = "/apps/jet.app/main"
NAME_RE = re.compile(r"^[a-z][a-z0-9_-]{0,31}$")


def home():
    h = os.environ.get("VIBE_HOME")
    if h:
        return h
    return os.path.join(os.path.expanduser("~"), ".vibe")


def config_path():
    return os.path.join(home(), "config.toml")


def env_path():
    return os.path.join(home(), ".env")


def preset(name):
    for p in PRESETS:
        if p["name"] == name:
            return p
    return None


def die(msg, code=1):
    print("vibe-account : " + msg, file=sys.stderr)
    sys.exit(code)


# ---- .env : KEY=valeur, une par ligne (python-dotenv, que lit Vibe) ----

def env_read():
    out = {}
    try:
        with open(env_path(), encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#") or "=" not in line:
                    continue
                k, v = line.split("=", 1)
                k = k.strip()
                if k.startswith("export "):
                    k = k[7:].strip()
                v = v.strip()
                if len(v) >= 2 and v[0] == v[-1] and v[0] in "'\"":
                    v = v[1:-1]
                out[k] = v
    except FileNotFoundError:
        pass
    return out


def env_write_key(var, value):
    """remplace (ou ajoute, ou retire si value est None) la ligne de var, les autres lignes gardées"""
    os.makedirs(home(), exist_ok=True)
    lines = []
    try:
        with open(env_path(), encoding="utf-8") as f:
            lines = f.read().splitlines()
    except FileNotFoundError:
        pass
    keep = [l for l in lines if not re.match(r"^\s*(export\s+)?%s\s*=" % re.escape(var), l)]
    if value is not None:
        keep.append("%s='%s'" % (var, value.replace("'", "")))
    tmp = env_path() + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        f.write("\n".join(keep) + ("\n" if keep else ""))
    os.replace(tmp, env_path())
    try:
        os.chmod(env_path(), 0o600)
    except OSError:
        pass


# ---- config.toml : lu par tomllib, écrit par tomli_w (celui de Vibe, qui récrit lui-même ce fichier quand on
# change de modèle : les commentaires n'y survivent pas) ----

def cfg_load():
    try:
        with open(config_path(), "rb") as f:
            return tomllib.load(f)
    except FileNotFoundError:
        return {}
    except tomllib.TOMLDecodeError as e:
        die("config.toml illisible : %s" % e)


def cfg_save(d):
    try:
        import tomli_w
    except ImportError:
        die("tomli_w absent (il vient avec Vibe)")
    os.makedirs(home(), exist_ok=True)
    tmp = config_path() + ".tmp"
    with open(tmp, "wb") as f:
        tomli_w.dump(d, f)
    os.replace(tmp, config_path())


def models_of(d):
    """les modèles déclarés, en liste (Vibe accepte une liste de [[models]] ou une table par alias)"""
    m = d.get("models") or []
    if isinstance(m, dict):
        return [dict(v, alias=v.get("alias", k)) for k, v in m.items() if isinstance(v, dict)]
    return [x for x in m if isinstance(x, dict)]


def set_models(d, models):
    if isinstance(d.get("models"), dict):
        d["models"] = {m.get("alias") or m["name"]: m for m in models}
    elif models:
        d["models"] = models
    else:
        d.pop("models", None)


def provider_of(d, name):
    return next((p for p in d.get("providers") or [] if isinstance(p, dict) and p.get("name") == name), None)


def provider_info(name):
    """(api_base, env) d'un fournisseur : celui de config.toml, sinon le préréglage de Vibe (mistral)"""
    p = provider_of(cfg_load(), name)
    if p:
        return p.get("api_base", ""), p.get("api_key_env_var", "")
    pr = preset(name)
    if pr and pr["builtin"]:
        return pr["api_base"], pr["env"]
    return None, None


# ---- les commandes ----

def cmd_presets(_a):
    print(json.dumps(PRESETS, ensure_ascii=False))


def accounts():
    env = env_read()
    d = cfg_load()
    models = models_of(d)
    names = ["mistral"] + [p["name"] for p in d.get("providers") or [] if isinstance(p, dict) and p.get("name")
                           and p["name"] != "mistral"]
    rows = []
    for n in names:
        api, var = provider_info(n)
        p = preset(n)
        rows.append({"name": n, "label": p["label"] if p else n, "api_base": api or "", "env": var or "",
                     "key": bool(var and (env.get(var) or os.environ.get(var))),
                     "models": [m.get("alias") or m.get("name") for m in models if m.get("provider") == n],
                     "keys_url": p["keys_url"] if p else ""})
    return rows


def cmd_list(a):
    rows = accounts()
    if "--json" in a:
        print(json.dumps(rows, ensure_ascii=False))
        return
    for r in rows:
        print("%-10s %-28s clé : %-3s  %s%s" % (r["name"], r["label"], "oui" if r["key"] else "non", r["api_base"],
                                               ("  modèles : " + ", ".join(r["models"])) if r["models"] else ""))


def opt(a, flag, default=None):
    if flag in a:
        i = a.index(flag)
        if i + 1 < len(a):
            v = a[i + 1]
            del a[i:i + 2]
            return v
    return default


def cmd_add(a):
    api = opt(a, "--api-base")
    var = opt(a, "--env")
    opt(a, "--label")  # (pas un champ de Vibe : la liste reprend le libellé du préréglage)
    if not a:
        die("add PROVIDER [--api-base URL] [--env VAR] [--label TEXTE]")
    name = a[0]
    if not NAME_RE.match(name):
        die("nom de fournisseur invalide : %r (minuscules, chiffres, - et _)" % name)
    p = preset(name)
    if name == "mistral":
        return  # (déjà connu de Vibe)
    api = api or (p["api_base"] if p else "")
    if not re.match(r"^https?://[^\s]+$", api or ""):
        die("adresse d'API invalide : %r (http(s)://…/v1)" % api)
    var = var or (p["env"] if p and p["env"] else re.sub(r"[^A-Z0-9]", "_", name.upper()) + "_API_KEY")
    d = cfg_load()
    provs = [x for x in d.get("providers") or [] if not (isinstance(x, dict) and x.get("name") == name)]
    provs.append({"name": name, "api_base": api.rstrip("/"), "api_key_env_var": var, "api_style": "openai",
                  "backend": "generic"})
    d["providers"] = provs
    cfg_save(d)


def cmd_set_key(a):
    if not a:
        die("set-key PROVIDER (la clé sur l'entrée standard)")
    api, var = provider_info(a[0])
    if not var:
        die("fournisseur inconnu : %s (vibe-account add d'abord)" % a[0])
    key = sys.stdin.readline().strip()
    if not key or any(c.isspace() for c in key) or len(key) > 400:
        die("clé vide ou invalide")
    env_write_key(var, key)
    print("clé rangée dans %s (%s)" % (env_path(), var))


def fetch_models(name):
    api, var = provider_info(name)
    if not api:
        die("fournisseur inconnu : %s" % name)
    key = env_read().get(var) or os.environ.get(var or "", "")
    if not key:
        die("pas de clé pour %s (vibe-account set-key %s)" % (name, name), 3)
    import urllib.request
    req = urllib.request.Request(api.rstrip("/") + "/models", headers={"Authorization": "Bearer " + key,
                                                                       "Accept": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            d = json.load(r)
    except Exception as e:  # (réseau, 401 : dit sans la clé)
        die("le fournisseur ne répond pas : %s" % (getattr(e, "code", None) or e.__class__.__name__), 2)
    ids = [m.get("id") for m in d.get("data", []) if isinstance(m, dict) and m.get("id")]
    return sorted(set(ids))


def cmd_models(a):
    if not a:
        die("models PROVIDER")
    ids = fetch_models(a[0])
    print(json.dumps(ids) if "--json" in a else "\n".join(ids))


def cmd_add_model(a):
    alias = opt(a, "--alias")
    label = opt(a, "--label")
    if len(a) < 2:
        die("add-model PROVIDER MODEL [--alias A]")
    name, model = a[0], a[1]
    if not re.match(r"^[A-Za-z0-9._:/@+-]{1,128}$", model):
        die("nom de modèle invalide : %r" % model)
    api, var = provider_info(name)
    if not api:
        die("fournisseur inconnu : %s (vibe-account add d'abord)" % name)
    alias = alias or model
    d = cfg_load()
    models = [m for m in models_of(d) if (m.get("alias") or m.get("name")) != alias]
    m = {"name": model, "provider": name, "alias": alias}
    if label:
        m["display_name"] = label
    models.append(m)
    set_models(d, models)
    cfg_save(d)


def cmd_use(a):
    """active_model = ALIAS (le modèle des nouvelles séances)"""
    if not a or not re.match(r"^[A-Za-z0-9._:/@+-]{1,128}$", a[0]):
        die("use ALIAS")
    d = cfg_load()
    d["active_model"] = a[0]
    cfg_save(d)


def cmd_remove(a):
    if not a:
        die("remove PROVIDER")
    name = a[0]
    api, var = provider_info(name)
    d = cfg_load()
    models = models_of(d)
    gone = {m.get("alias") or m.get("name") for m in models if m.get("provider") == name}
    d["providers"] = [x for x in d.get("providers") or [] if not (isinstance(x, dict) and x.get("name") == name)]
    if not d["providers"]:
        del d["providers"]
    set_models(d, [m for m in models if m.get("provider") != name])
    # active_model sur un modèle retiré : Vibe refuserait de démarrer ; le modèle par défaut reprend
    if d.get("active_model") in gone:
        del d["active_model"]
    cfg_save(d)
    if var:
        env_write_key(var, None)


def open_jet(url):
    if not url:
        return False
    for exe in (JET,):
        if os.path.exists(exe):
            try:
                subprocess.Popen([exe, url], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, start_new_session=True)
                return True
            except OSError:
                pass
    return False


def cmd_open_keys(a):
    p = preset(a[0]) if a else None
    if not p or not p["keys_url"]:
        die("pas de page de clés connue pour ce fournisseur")
    if not open_jet(p["keys_url"]):
        print("Jet est absent : ouvrez %s dans un navigateur" % p["keys_url"])


def ask(prompt, default=""):
    try:
        s = input(prompt + (" [%s]" % default if default else "") + " : ").strip()
    except EOFError:
        sys.exit(1)
    return s or default


def cmd_wizard(_a):
    print("Ajouter un compte de fournisseur de modèles pour Vibe\n")
    for i, p in enumerate(PRESETS, 1):
        print("  %d. %s — %s" % (i, p["label"], p["note"]))
    c = ask("Fournisseur (1-%d)" % len(PRESETS), "1")
    if not c.isdigit() or not 1 <= int(c) <= len(PRESETS):
        die("choix invalide")
    p = PRESETS[int(c) - 1]
    name = p["name"]
    if name == "custom":
        name = ask("Nom court (minuscules)")
        api = ask("Adresse de l'API (…/v1)")
        cmd_add([name, "--api-base", api])
    else:
        cmd_add([name])
    if p["keys_url"]:
        print("\nLa page des clés : %s" % p["keys_url"])
        if ask("L'ouvrir dans Jet ? (o/n)", "o").lower().startswith("o") and not open_jet(p["keys_url"]):
            print("(Jet est absent : ouvrez-la sur un autre appareil)")
    import getpass
    key = getpass.getpass("Collez la clé d'API (rien ne s'affiche) : ").strip()
    if not key:
        die("pas de clé")
    api, var = provider_info(name)
    env_write_key(var, key)
    print("clé rangée dans %s" % env_path())
    try:
        ids = fetch_models(name)
    except SystemExit:
        ids = []
    if ids:
        print("\nModèles annoncés :")
        for i, m in enumerate(ids, 1):
            print("  %d. %s" % (i, m))
        sel = ask("Lesquels ajouter (numéros séparés par des virgules, ou un nom)", "1")
        chosen = []
        for s in sel.split(","):
            s = s.strip()
            chosen.append(ids[int(s) - 1] if s.isdigit() and 1 <= int(s) <= len(ids) else s)
    else:
        chosen = [ask("Nom du modèle (tel que le fournisseur l'écrit)")]
    chosen = [m for m in chosen if m]
    for m in chosen:
        cmd_add_model([name, m])
        print("modèle ajouté : %s" % m)
    if chosen:
        cmd_use([chosen[0]])
    print("\nFait. Le modèle se choisit au début de chaque séance (vibe.app, agent --model).")


def main():
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help"):
        print(__doc__)
        return
    cmds = {"presets": cmd_presets, "list": cmd_list, "add": cmd_add, "set-key": cmd_set_key, "models": cmd_models,
            "add-model": cmd_add_model, "use": cmd_use, "remove": cmd_remove, "open-keys": cmd_open_keys, "wizard": cmd_wizard}
    f = cmds.get(a[0])
    if not f:
        die("commande inconnue : %s" % a[0])
    f(a[1:])


if __name__ == "__main__":
    main()
