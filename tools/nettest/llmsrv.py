"""llmsrv.py -- un faux service de langage compatible OpenAI (POST .../chat/completions) pour essayer PocketQI
(src/apps/pocketqi) sans joindre Mistral ni Groq ni aucune cle.

  python -I tools/nettest/llmsrv.py [--port 8088] [--log build/try/llmsrv.log]

Sous QEMU, l'invite joint le PC en 10.0.2.2 : config.ini de PocketQI, « provider = url » et
« url = http://10.0.2.2:8088/v1/chat/completions ». Bibliotheque standard seule ; tout reste sur 127.0.0.1.

Ce qu'il fait, sans aucun modele : il lit le prompt systeme envoye par PocketQI, en tire la liste des applications
(« - id : nom, categorie ; description ... ») et choisit celle dont les mots ressemblent le plus a la question. Si ses
conseils sont dans le prompt, il repond avec le premier conseil qui partage un mot avec la question ; sinon il demande
« aide [id] » et repond au second tour avec les conseils recus. « ouvre » ou « lance » dans la question : il ajoute
l'action open de l'app. Les outils de PocketQI (weather, memo, planner, shopping : « Tes outils » dans le prompt) : une
phrase qui en parle (« meteo a Lyon demain », « ajoute du lait a la liste de courses », « qu'est-ce que j'ai cette
semaine ? ») donne l'appel, avec les dates AAAA-MM-JJ calculees depuis celle du prompt, comme le ferait un modele.

Il sert aussi de faux Open-Meteo pour l'outil weather (« weather_url = http://10.0.2.2:8088 » dans config.ini) :
GET /v1/search?name=... (une ville inventee, sauf « Nullepart ») et GET /v1/forecast (des valeurs fixes, sept jours).
Et de fausse voix de Mistral (« tts_url = http://10.0.2.2:8088/v1/audio/speech ») : POST /v1/audio/speech rend, en
SSE comme Mistral, un son court (une note qui glisse, 24 kHz float32 en base64), d'autant plus long que le texte l'est.

Chaque requete est journalisee (taille du prompt, applications vues, question, reponse) : la
preuve que la reponse s'appuie sur les manifestes lus par l'OS. L'en-tete Authorization n'est jamais ecrit.
"""
import argparse
import base64
import datetime
import math
import struct
import http.server
import urllib.parse
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


DAYS = ["lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi", "dimanche"]
DAYS_EN = ["monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday"]


def tool_call(question, system):
    """une phrase pour un outil de PocketQI -> les outcomes (ou None), comme un modele qui lit « Tes outils »"""
    if "Tes outils" not in system:
        return None
    f = fold(question)
    m = re.search(r"\((\d{4})-(\d{2})-(\d{2})\)", system)  # (aujourd'hui, ecrit par PocketQI)
    today = datetime.date(int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else datetime.date.today()

    def day():
        if "apres-demain" in f:
            return today + datetime.timedelta(2)
        if "demain" in f or "tomorrow" in f:
            return today + datetime.timedelta(1)
        for names in (DAYS, DAYS_EN):
            for i, d in enumerate(names):
                if re.search(r"\b" + d + r"\b", f):
                    return today + datetime.timedelta((i - today.weekday()) % 7)
        return None

    def hour():
        h = re.search(r"\b(\d{1,2})\s*(?:h|:)\s*(\d{2})?", f)
        return f"{int(h.group(1)):02d}:{h.group(2) or '00'}" if h else None
    town = re.search(r"\b(?:à|a|in)\s+([A-Z][\w\-]+)", question)
    town = [town.group(1)] if town else []

    def say(t):
        return {"app": "say", "args": [t]}
    if re.search(r"meteo|weather|temps|pleuv|pluie|rain", f):
        d = day()
        if "semaine" in f or "week" in f:
            return [{"app": "weather", "args": ["week"] + town}]
        if d and d != today:
            return [{"app": "weather", "args": ["day", d.isoformat()] + town}]
        return [{"app": "weather", "args": ["now"] + town}]
    if "courses" in f or "shopping" in f:
        act = ("print" if re.search(r"imprim|print", f) else "remove" if re.search(r"retire|enleve|remove", f)
               else "check" if re.search(r"coche|achete", f) else "add" if re.search(r"ajoute|mets|add", f) else "list")
        if act in ("print", "list"):
            return [{"app": "shopping", "args": [act]}]
        m = re.search(r"(?:ajoute|mets|retire|enlève|enleve|coche|add|remove)\s+(?:du |de la |des |de l'|le |la |les |l')?(.+?)\s+"
                      r"(?:à|a|de|sur|dans|to|from)\s+(?:la|ma|my|the)\b", question, re.I)
        items = re.split(r",\s*|\s+et\s+|\s+and\s+", m.group(1)) if m else []
        return [{"app": "shopping", "args": [act, i.strip()]} for i in items if i.strip()] or [say("Quel article ?")]
    if re.search(r"rendez-vous|rdv|agenda|calendrier|j'ai|appointment", f):
        d = day()
        if re.search(r"ajoute|note|add", f):
            if not d:
                return [say("Pour quel jour ?")]
            m = re.search(r"(chez|avec|with)\s+(.+?)(?=\s+(?:demain|à|a|le|lundi|mardi|mercredi|jeudi|vendredi|samedi|dimanche)\b|$)",
                          question, re.I)
            title = ("Rendez-vous " + m.group(1) + " " + m.group(2)) if m else "Rendez-vous"
            return [{"app": "planner", "args": ["add", d.isoformat(), title] + ([hour()] if hour() else [])}]
        act = "print" if re.search(r"imprim|print", f) else "list"
        if "semaine" in f or "week" in f:
            monday = today - datetime.timedelta(today.weekday()) + datetime.timedelta(7 if "prochaine" in f or "next" in f else 0)
            return [{"app": "planner", "args": [act, max(monday, today).isoformat(), (monday + datetime.timedelta(6)).isoformat()]}]
        return [{"app": "planner", "args": [act, (d or today).isoformat()]}]
    if re.search(r"\bnotes?\b", f) and not re.search(r"\bcomment\b|\bhow\b", f):
        if re.search(r"imprim|print", f):
            return [{"app": "memo", "args": ["print"]}]
        m = re.search(r"(?:note|noter)\s*:\s*(.+)$", question, re.I)
        if m or re.search(r"ajoute|add", f):
            return [{"app": "memo", "args": ["add", (m.group(1) if m else question).strip()]}]
        return [{"app": "memo", "args": ["read" if re.search(r"\blis\b|read", f) else "list"]}]
    return None


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
    tool = tool_call(question, system) if not last.startswith("Conseils de") else None
    if tool:
        log(f"outil : {json.dumps(tool, ensure_ascii=False)}")
        return [{"app": "humeur", "args": ["joyeux", "1"]}] + tool
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

    def do_GET(self):
        """le faux Open-Meteo : /v1/search (geocodage) et /v1/forecast"""
        u = urllib.parse.urlparse(self.path)
        qs = urllib.parse.parse_qs(u.query)
        if u.path == "/v1/search":
            name = qs.get("name", [""])[0]
            resp = {} if fold(name) == "nullepart" else {"results": [{"name": name[:1].upper() + name[1:], "latitude": 45.76,
                                                                         "longitude": 4.84, "country": "France"}]}
        elif u.path == "/v1/forecast":
            d0 = datetime.date.today()
            resp = {"current": {"temperature_2m": 12.4, "apparent_temperature": 10.1, "weather_code": 3, "wind_speed_10m": 14.2},
                    "daily": {"time": [(d0 + datetime.timedelta(i)).isoformat() for i in range(7)],
                              "weather_code": [3, 61, 63, 0, 1, 80, 71], "temperature_2m_max": [13.1, 11.0, 9.6, 14.2, 15.0, 10.4, 4.9],
                              "temperature_2m_min": [6.2, 5.0, 4.4, 3.1, 6.8, 2.2, -1.5],
                              "precipitation_probability_max": [10, 80, 95, 0, 5, 60, 70],
                              "wind_speed_10m_max": [20.5, 31.0, 40.2, 12.0, 15.3, 25.0, 18.8]}}
        else:
            resp = {"error": True, "reason": "inconnu"}
        self.server.log(f"GET {self.path}")
        data = json.dumps(resp, ensure_ascii=False).encode("utf-8")
        self.send_response(200 if "error" not in resp else 404)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def speech(self, body):
        """la fausse voix : des evenements SSE speech.audio.delta, PCM float32 24 kHz en base64"""
        req = json.loads(body.decode("utf-8"))
        text = req.get("input", "")
        self.server.log(f"voix {req.get('voice_id')!r} ({'avec' if self.headers.get('Authorization') else 'sans'} cle) : {text[:120]!r}")
        secs = min(4.0, 0.4 + 0.03 * len(text))
        n = int(24000 * secs)
        pcm = [0.3 * math.sin(2 * math.pi * (300 + 200 * i / n) * i / 24000) * min(1, i / 600, (n - i) / 600) for i in range(n)]
        out = b""
        for k in range(0, n, 4800):
            chunk = struct.pack("<%df" % len(pcm[k:k + 4800]), *pcm[k:k + 4800])
            ev = {"type": "speech.audio.delta", "audio_data": base64.b64encode(chunk).decode()}
            out += b"data: " + json.dumps(ev).encode() + b"\n\n"
        out += b'data: {"type": "speech.audio.done"}\n\n'
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Content-Length", str(len(out)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(out)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(n)
        if self.path.startswith("/v1/audio/speech"):
            return self.speech(body)
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
