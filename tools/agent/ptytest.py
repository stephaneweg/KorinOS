#!/usr/bin/env python3
"""Essai de l'étape 8 (docs/PYTHON-PORT.md) : agent, le client terminal de Vibe, piloté dans un pseudo-terminal.

    ptytest.py [--agent-bin agent] [--cwd DIR] [--vibe vibe-acp] [--load-only]

Bibliothèque standard seulement : il tourne sur le PC (Linux) et sur la carte (python3 de Korin, pty de vterm).
La séance de l'étape 7, mais au clavier : lire notes.txt, le modifier en mode « ask » (la question d'approbation,
réponse « o »), git status et rg par le shell (approbation par « 1 »), Échap pendant une longue réponse, Ctrl+C
pendant une autre, un redimensionnement (TIOCSWINSZ, donc SIGWINCH) pendant un tour, puis Ctrl+D ; enfin agent
--continue (session/load de la séance la plus récente du dossier) qui doit se souvenir du premier tour.

Toute la sortie est vérifiée : chaque séquence d'échappement doit être de celles qu'agent écrit exprès (SGR,
effacements, déplacements du curseur) ; aucune ne doit venir de l'agent ni d'une touche, et aucun caractère de
commande ne doit sortir seul (« aucune séquence affichée en clair »). Code de sortie 0 si tout passe.
"""
from __future__ import annotations

import argparse
import fcntl
import os
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios
import time

SECRET = "LUNE-2604"
NOTES = ("Korin est un système pour le Raspberry Pi 4.\n"
         f"Le mot secret de ce dossier est {SECRET}.\n")
ADDED = "Modifié par Vibe sous Korin."
# ce qu'agent a le droit d'écrire : SGR, effacement de ligne et d'écran, curseur haut / droite, maison
ALLOWED = re.compile(rb"\x1b\[(?:[0-9;]*m|K|J|2J|H|[0-9]+[AC])")
ANSI = re.compile(rb"\x1b\[[0-?]*[ -/]*[@-~]")


def log(*a: object) -> None:
    print(*a, file=sys.stderr, flush=True)


class Pty:
    def __init__(self, argv: list[str], cwd: str, cols: int = 100, rows: int = 30) -> None:
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            try:
                os.chdir(cwd)
                os.execvp(argv[0], argv)
            finally:
                os._exit(127)
        self.resize(cols, rows)
        self.raw = b""
        self.mark = 0
        self.log = open(os.path.join(cwd, "ptytest.live"), "ab")    # la sortie au fil de l'eau

    def resize(self, cols: int, rows: int) -> None:
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))

    def send(self, data: bytes | str) -> None:
        if isinstance(data, str):
            data = data.encode()
        os.write(self.fd, data)

    def type(self, text: str) -> None:
        for ch in text:
            self.send(ch)
            time.sleep(0.01)

    def pump(self, t: float) -> None:
        end = time.time() + t
        while True:
            left = end - time.time()
            if left <= 0:
                return
            r, _, _ = select.select([self.fd], [], [], left)
            if not r:
                return
            try:
                d = os.read(self.fd, 65536)
            except OSError:
                return
            if not d:
                return
            self.raw += d
            if self.log:
                self.log.write(d)
                self.log.flush()

    def text(self, start: int = 0) -> str:
        return ANSI.sub(b"", self.raw[start:]).replace(b"\r", b"").decode("utf-8", "replace")

    def wait(self, pattern: str, timeout: float, since: int | None = None) -> bool:
        start = self.mark if since is None else since
        end = time.time() + timeout
        rx = re.compile(pattern)
        while time.time() < end:
            if rx.search(self.text(start)):
                return True
            self.pump(min(1.0, end - time.time()))
        return bool(rx.search(self.text(start)))

    def new(self) -> None:
        """la suite des attentes ne regarde que ce qui vient après"""
        self.mark = len(self.raw)

    def alive(self) -> bool:
        try:
            pid, _ = os.waitpid(self.pid, os.WNOHANG)
        except ChildProcessError:
            return False
        return pid == 0

    def finish(self, timeout: float) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            if not self.alive():
                return True
            self.pump(0.5)
        return False


def bad_sequences(raw: bytes) -> list[bytes]:
    """les séquences et caractères de commande qu'agent n'aurait pas dû écrire"""
    bad = []
    i = 0
    while i < len(raw):
        c = raw[i]
        if c == 0x1B:
            m = ALLOWED.match(raw, i)
            if m:
                i = m.end()
                continue
            bad.append(raw[i:i + 12])
            i += 1
            continue
        if c < 0x20 and c not in (0x0A, 0x0D, 0x09, 0x08):
            bad.append(raw[i:i + 1])
        i += 1
    return bad


def git(cwd: str, *argv: str) -> str:
    return subprocess.run(["git", *argv], cwd=cwd, capture_output=True, text=True).stdout


def prepare(cwd: str) -> None:
    os.makedirs(cwd, exist_ok=True)
    with open(os.path.join(cwd, "notes.txt"), "w", encoding="utf-8") as f:
        f.write(NOTES)
    if not os.path.isdir(os.path.join(cwd, ".git")):
        ident = ["-c", "user.name=essai", "-c", "user.email=essai@korin"]
        git(cwd, "init", "-q")
        git(cwd, *ident, "add", "notes.txt")
        git(cwd, *ident, "commit", "-q", "-m", "notes")
    else:
        git(cwd, "checkout", "-q", "--", "notes.txt")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--agent-bin", default="agent")
    ap.add_argument("--cwd", default="work8")
    ap.add_argument("--vibe", default=None, help="la commande de l'agent ACP (agent --agent)")
    ap.add_argument("--slow", type=float, default=1.0, help="facteur des délais (QEMU : 4)")
    args = ap.parse_args()
    cwd = os.path.abspath(args.cwd)
    prepare(cwd)
    k = args.slow
    binary = os.path.abspath(args.agent_bin) if "/" in args.agent_bin else args.agent_bin
    argv = [binary, "--cwd", cwd]
    if args.vibe:
        argv += ["--agent", args.vibe]
    checks: dict[str, bool] = {}
    times: dict[str, float] = {}

    t0 = time.time()
    p = Pty(argv, cwd)
    checks["séance ouverte en ask"] = (p.wait(r"Séance \S+ ouverte", 600 * k)
                                      and p.wait(r"Mode : ask|ouverte, mode ask", 60 * k))
    times["démarrage"] = time.time() - t0
    log(f"séance ouverte : {times['démarrage']:.1f} s")
    if not checks["séance ouverte en ask"]:
        log(p.text()[-2000:])
        return 1

    def turn(text: str, until: str = r"\n\d+\.\d s\n", timeout: float = 600) -> str:
        p.new()
        t = time.time()
        p.type(text)
        p.send("\r")
        ok = p.wait(until, timeout * k)
        times[text[:28]] = time.time() - t
        out = p.text(p.mark)
        log(f"--- {text[:50]!r} ({times[text[:28]]:.1f} s, {'fin' if ok else 'PAS DE FIN'})")
        return out

    # 1. lire
    out = turn("Lis le fichier notes.txt et dis-moi quel est le mot secret qu'il contient.")
    checks["lecture : outil affiché"] = "» Reading notes.txt" in out
    checks["lecture : mot secret"] = SECRET in out

    # 2. modifier : la question d'approbation, réponse « o »
    p.new()
    p.type(f"Ajoute à la fin de notes.txt une ligne qui dit exactement : {ADDED}")
    p.send("\r")
    t = time.time()
    asked = p.wait(r"Vibe demande votre accord", 600 * k)
    checks["modification : approbation demandée"] = asked and "[1/o]" in p.text(p.mark)
    p.send("o")
    p.wait(r"\n\d+\.\d s\n", 600 * k)
    times["modification"] = time.time() - t
    out = p.text(p.mark)
    checks["modification : diff affiché"] = "+ " + ADDED.rstrip(".") in out
    checks["modification : fichier changé"] = ADDED.rstrip(".") in open(os.path.join(cwd, "notes.txt"), encoding="utf-8").read()

    # 3. le shell, avec un redimensionnement pendant le tour ; chaque approbation par « 1 »
    p.new()
    p.type("Lance dans le shell `git status --short`, puis `rg -n Korin`, et recopie-moi leurs sorties telles quelles.")
    p.send("\r")
    t = time.time()
    p.pump(2)
    p.resize(72, 24)
    os.kill(p.pid, signal.SIGWINCH)      # (le noyau l'envoie déjà au groupe de premier plan ; une fois de plus)
    end = time.time() + 600 * k
    approvals = 0
    while time.time() < end:
        if re.search(r"\n\d+\.\d s\n", p.text(p.mark)):
            break
        n = p.text(p.mark).count("Vibe demande votre accord")
        if n > approvals:
            approvals = n
            p.send("1")
        p.pump(1)
    times["shell"] = time.time() - t
    out = p.text(p.mark)
    checks["shell : git status"] = "» bash:" in out and "git status" in out and "notes.txt" in out
    checks["shell : rg"] = "rg -n Korin" in out and "1:Korin" in out
    checks["redimensionnement : l'agent continue"] = p.alive()

    # 4. Échap pendant une longue réponse
    p.new()
    p.type("Écris un très long poème de cent strophes sur l'hiver à Korin, sans utiliser d'outil.")
    p.send("\r")
    t = time.time()
    p.wait(r"Vibe travaille", 120 * k)
    p.wait(r"[A-Za-zéè]{4}", 600 * k, since=len(p.raw))     # le premier texte du modèle
    p.pump(2 * k)
    p.send("\x1b")
    checks["Échap : interrompu"] = p.wait(r"\(interrompu\)", 300 * k)
    times["Échap"] = time.time() - t

    # 5. Ctrl+C pendant un tour, puis la séance continue
    p.new()
    p.type("Compte lentement de 1 à 300 en toutes lettres, un nombre par ligne.")
    p.send("\r")
    p.wait(r"Vibe travaille", 120 * k)
    p.wait(r"[a-z]{4}", 600 * k, since=len(p.raw))
    p.send("\x03")
    checks["Ctrl+C : interrompu"] = p.wait(r"\(interrompu\)", 300 * k)
    checks["Ctrl+C : agent toujours là"] = p.alive()

    # 6. une flèche et Suppr à l'invite : rien d'affiché en clair ; puis Ctrl+D
    p.new()
    p.send("abc\x1b[D\x1b[3~\x1b[1;5C\x1bOP")      # flèche gauche, Suppr, Ctrl+droite, F1
    p.pump(1)
    p.send("\x15")                                   # Ctrl+U
    p.send("\x04")                                   # Ctrl+D
    checks["Ctrl+D : quitte"] = p.finish(30 * k)

    bad = bad_sequences(p.raw)
    checks["aucune séquence en clair"] = not bad and "[D" not in p.text() and "[3~" not in p.text()
    if bad:
        log(f"séquences inattendues : {bad[:10]}")

    # 7. --continue : la séance la plus récente du dossier, reprise
    t0 = time.time()
    p2 = Pty(argv + ["--continue"], cwd)
    checks["--continue : séance reprise"] = p2.wait(r"Séance \S+ reprise", 600 * k)
    times["reprise"] = time.time() - t0
    p2.new()
    p2.type("Quel était le mot secret de notes.txt que tu as lu tout à l'heure ? Réponds sans relire le fichier.")
    p2.send("\r")
    p2.wait(r"\n\d+\.\d s\n", 600 * k)
    checks["--continue : se souvient"] = SECRET in p2.text(p2.mark)
    p2.send("\x04")
    p2.finish(30 * k)
    bad = bad_sequences(p2.raw)
    checks["aucune séquence en clair (reprise)"] = not bad

    with open(os.path.join(cwd, "ptytest.raw"), "wb") as f:
        f.write(p.raw + b"\n===\n" + p2.raw)
    log("temps : " + ", ".join(f"{k_} {v:.1f} s" for k_, v in times.items()))
    log("résultat : " + ", ".join(f"{k_} {'oui' if v else 'NON'}" for k_, v in checks.items()))
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
