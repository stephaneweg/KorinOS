"""llmsrv.py -- un faux service de langage compatible OpenAI (POST .../chat/completions) pour essayer PocketQI
(src/apps/pocketqi) sans joindre Mistral ni Groq ni aucune cle.

  python -I tools/nettest/llmsrv.py [--port 8088] [--log build/try/llmsrv.log]

Sous QEMU, l'invite joint le PC en 10.0.2.2 : config.ini de PocketQI, « provider = url » et
« url = http://10.0.2.2:8088/v1/chat/completions ». Bibliotheque standard seule ; tout reste sur 127.0.0.1.

Ce qu'il fait, sans aucun modele : il lit le prompt systeme envoye par PocketQI, en tire la liste des applications
(« - id : nom, categorie ; description ... ») et choisit celle dont les mots ressemblent le plus a la question. Si ses
conseils sont dans le prompt, il repond avec le premier conseil qui partage un mot avec la question ; sinon il demande
« aide [id] » et repond au second tour avec les conseils recus. « ouvre » ou « lance » dans la question : il ajoute
l'action open de l'app. Chaque requete est journalisee (taille du prompt, applications vues, question, reponse) : la
preuve que la reponse s'appuie sur les manifestes lus par l'OS. L'en-tete Authorization n'est jamais ecrit.
"""
import argparse
import http.server
import json
import os
import re
import time
import unicodedata

STOP = set("les des une est que qui pour dans avec sur par comment faire faut peux puis veux mon mes ton tes son ses "
           "the and how can what with this that quoi quel quelle pas plus bien tout tous fait ouvre ouvrir lance lancer".split())


def fold(s):
    s = unicodedata.normalize("NFD", s.lower())
    return "".join(c for c in s if unicodedata.category(c) != "Mn")


def words(s):
    return [w for w in re.findall(r"[a-z0-9]+", fold(s)) if len(w) >= 3 and w not in STOP]


def match(a, b):
    return a == b or (len(a) >= 5 and len(b) >= 5 and a[:5] == b[:5])


def score(q, text):
    t = words(text)
    return sum(max([2 if a == b else 1 for b in t if match(a, b)] or [0]) for a in q)  # (un mot identique compte double)


def apps_of(system):
    """les lignes « - id : ... » du prompt -> [(id, ligne)]"""
    out = []
    for line in system.split("\n"):
        m = re.match(r"- ([a-z0-9_]+) : (.*)$", line)
        if m:
            out.append((m.group(1), m.group(2)))
    return out


def answer(messages, log):
    system = messages[0]["content"] if messages and messages[0]["role"] == "system" else ""
    last = messages[-1]["content"] if messages else ""
    question = next((m["content"] for m in reversed(messages) if m["role"] == "user" and not m["content"].startswith("Conseils de")), "")
    apps = apps_of(system)
    log(f"prompt {len(system)} octets, {len(apps)} applications ; question : {question!r}")
    with open(os.path.join("build", "try", "llmsrv-prompt.txt"), "w", encoding="utf-8") as f:  # (le dernier prompt, pour le relire)
        f.write(system)
    if last.startswith("Conseils de"):				# second tour : les conseils demandes par aide
        tips = last.split(":", 1)[1].split("Reponds maintenant")[0].strip()
        sent = [s.strip() for s in re.split(r"(?<=[.;])\s+", tips) if s.strip()]
        q = words(question)
        best = max(sent, key=lambda s: score(q, s)) if sent else "aucun conseil"
        return [{"app": "humeur", "args": ["joyeux", "1"]}, {"app": "say", "args": ["D'apres son manifeste : " + best]}]
    q = words(question)
    if not apps or not q:
        return [{"app": "humeur", "args": ["perplexe", "1"]}, {"app": "say", "args": ["Je ne vois aucune application pour cela."]}]
    # (comme le ferait un modele : les apps dont PocketQI a mis les conseils, les plus proches de la question, d'abord)
    def rank(a):
        head = a[1].split(" Ouvre :")[0].split(" Actions :")[0]
        name, _, desc = head.partition(" ; ")
        return 4 * score(q, a[0] + " " + name) + 3 * score(q, desc) + (2 if "(conseils : aide)" not in a[1] else 0)
    ident, line = max(apps, key=rank)
    name = line.split(",")[0].split(" ;")[0]
    log(f"application choisie : {ident} ({name})")
    out = [{"app": "humeur", "args": ["curieux", "1"]}]
    if re.search(r"\b(ouvre|ouvrir|lance|lancer)\b", fold(question)):
        out += [{"app": "say", "args": [f"J'ouvre {name}."]}, {"app": ident, "args": ["open"]}]
        return out
    if "(conseils : aide)" in line:
        return out + [{"app": "aide", "args": [ident]}]
    tips = line.split(" Actions :", 1)[1].split(".", 1)[1] if " Actions :" in line else ""
    sent = [s.strip() for s in re.split(r"(?<=\.)\s+", tips) if s.strip()]
    best = max(sent, key=lambda s: score(q, s)) if sent else line
    return out + [{"app": "say", "args": [f"Avec {name} : {best}"]}]


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n)
        try:
            req = json.loads(body.decode("utf-8"))
            outcomes = answer(req.get("messages", []), self.server.log)
            content = json.dumps({"outcomes": outcomes}, ensure_ascii=False)
            self.server.log("reponse : " + content)
            resp = {"id": "llmsrv", "object": "chat.completion", "created": int(time.time()), "model": req.get("model", ""),
                    "choices": [{"index": 0, "finish_reason": "stop", "message": {"role": "assistant", "content": content}}]}
            code = 200
        except Exception as e:  # (une requete illisible : l'erreur au format d'OpenAI)
            resp, code = {"error": {"message": f"requete illisible : {e}"}}, 400
            self.server.log(resp["error"]["message"])
        data = json.dumps(resp, ensure_ascii=False).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8088)
    ap.add_argument("--log", default=os.path.join("build", "try", "llmsrv.log"))
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.log) or ".", exist_ok=True)
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", a.port), Handler)

    def log(line):
        stamp = time.strftime("%H:%M:%S")
        with open(a.log, "a", encoding="utf-8") as f:
            f.write(f"{stamp} {line}\n")
        print(f"{stamp} {line}", flush=True)
    srv.log = log
    log(f"llmsrv sur 127.0.0.1:{a.port}")
    srv.serve_forever()


if __name__ == "__main__":
    main()
