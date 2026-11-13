#!/usr/bin/env python3
"""Essai de « Vibe connaît Korin » (docs/PYTHON-PORT.md) : dans agent, demander une app sans rien expliquer.

    skilltest.py [--agent-bin agent] [--cwd DIR] [--slow 4] [--ask TEXTE]

Sur la carte (python3 de Korin, pty de vterm), à côté de ptytest.py dont il reprend le pseudo-terminal. Vibe reçoit
seulement « Crée une app Korin qui affiche un bouton et compte les clics. » ; il doit trouver de lui-même, par
$VIBE_HOME/AGENTS.md et les compétences korin et korin-app (/sd0/.vibe), où écrire les sources, comment compiler
(pibuild, le GCC de /gcc) et où installer. Chaque approbation est accordée pour la séance (« 2 »). Si le tour
finit sans app installée, une relance : « Vas-y : écris les fichiers, compile et installe l'app toi-même. »

Vérifié : une app neuve dans /sd0/apps (main et app.txt, plus récents que le début de l'essai), son main est un
ELF, ses sources dans un dossier de projet, une compilation par pibuild ou gcc dans la sortie ; puis l'app est
lancée détachée et doit être encore en vie 20 s plus tard. La sortie complète va dans DIR/skilltest.raw.
Code de sortie 0 si tout passe ; la ligne « app : /sd0/apps/<nom>.app » dit laquelle (pour une capture par VNC).
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ptytest import Pty, log  # noqa: E402

ASK = "Crée une app Korin qui affiche un bouton et compte les clics."
AGAIN = "Vas-y : écris les fichiers, compile et installe l'app toi-même."
APPS = "/sd0/apps"
END = r"\n\d+\.\d s\n"


def new_apps(since: float) -> list[str]:
    out = []
    for n in os.listdir(APPS):
        main = os.path.join(APPS, n, "main")
        if n.endswith(".app") and os.path.isfile(main) and os.path.getmtime(main) >= since - 2:
            out.append(os.path.join(APPS, n))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--agent-bin", default="agent")
    ap.add_argument("--cwd", default="work10")
    ap.add_argument("--slow", type=float, default=4.0, help="facteur des délais (QEMU : 4)")
    ap.add_argument("--ask", default=ASK)
    args = ap.parse_args()
    cwd = os.path.abspath(args.cwd)
    os.makedirs(cwd, exist_ok=True)
    k = args.slow
    checks: dict[str, bool] = {}
    t_start = time.time()
    p = Pty([args.agent_bin, "--cwd", cwd], cwd)
    if not p.wait(r"Séance \S+ ouverte|https://\S+", 600 * k):
        log(p.text()[-2000:])
        return 1
    m = re.search(r"https://\S+", p.text())
    if m and "ouverte" not in p.text():
        log(f"connexion au compte : ouvrir {m.group(0)}")     # (l'utilisateur l'ouvre lui-même)
        if not p.wait(r"Séance \S+ ouverte", 900):
            return 1
    log(f"séance ouverte : {time.time() - t_start:.1f} s")

    def turn(text: str) -> str:
        p.new()
        t = time.time()
        p.type(text)
        p.send("\r")
        end = t + 1800 * k
        approvals = 0
        while time.time() < end:
            out = p.text(p.mark)
            if re.search(END, out):
                break
            n = out.count("Vibe demande votre accord")
            if n > approvals:
                approvals = n
                p.send("2")                 # oui, pour cette séance
            p.pump(1)
        log(f"--- {text!r} : {time.time() - t:.1f} s, {approvals} approbation(s)")
        return p.text(p.mark)

    t0 = time.time()
    out = turn(args.ask)
    apps = new_apps(t0)
    if not apps:
        out += turn(AGAIN)
        apps = new_apps(t0)
    checks["compétence chargée"] = bool(re.search(r"korin-app|korin", out))
    checks["compilé par pibuild ou gcc"] = bool(re.search(r"pibuild|gcc", out))
    checks["app installée"] = len(apps) == 1
    app = apps[0] if apps else ""
    if app:
        log(f"app : {app}")
        with open(os.path.join(app, "main"), "rb") as f:
            checks["main est un ELF"] = f.read(4) == b"\x7fELF"
        checks["app.txt présent"] = os.path.isfile(os.path.join(app, "app.txt"))
        name = os.path.basename(app)[:-4]
        src = [d for d in ("/sd0/home/projects/" + name, os.path.join(cwd, name), cwd)
               if os.path.isdir(d) and any(f.endswith((".c", ".cpp")) for f in os.listdir(d))]
        checks["sources dans un dossier de projet"] = bool(src)
        if src:
            log(f"sources : {src[0]} ({', '.join(sorted(os.listdir(src[0])))})")
        # l'app lancée détachée (comme le lanceur) : encore en vie 20 s plus tard
        child = subprocess.Popen([os.path.join(app, "main")], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(20)
        checks["l'app tourne"] = child.poll() is None
        log(f"app lancée, pid {child.pid}, {'en vie' if child.poll() is None else 'finie : %s' % child.returncode}")
    p.send("\x04")
    p.finish(30 * k)
    with open(os.path.join(cwd, "skilltest.raw"), "wb") as f:
        f.write(p.raw)
    log("résultat : " + ", ".join(f"{k_} {'oui' if v else 'NON'}" for k_, v in checks.items()))
    return 0 if checks and all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
