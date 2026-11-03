"""Essai de la phase 3 de GCC sur le Pi (docs/GCC.md) : shell, outils, make, par telnet.

  python -I tools/gcc/pitest.py [HOTE] [--port N] [--rounds N] [--first N]

--first N : commence au N-ieme essai de TESTS (0 : le shell).

Compte les PASS et les FAIL de chaque essai, chronometre du PC. Une connexion coupee est notee, puis
l'essai reprend sur une nouvelle session. Resultats, sorties comprises : build/pitest.txt.
"""
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pibench import Telnet

OUT = "build/pitest.txt"
TESTS = [
    ("shell", "sh /gcc/test/shtest.sh", 600),
    ("outils", "sh /gcc/test/tooltest.sh", 600),
    ("make sur RAM0", "sh /gcc/test/maketest.sh /tmp/projet", 900),
    ("make sur SD0", "sh /gcc/test/maketest.sh SD0:/tmp/projet", 900),
    ("posix proc", "posixtest proc", 120),
    ("posix ipc", "posixtest ipc", 120),
    ("posix RAM0", "posixtest file RAM0:/posixtest", 120),
    ("posix SD0", "posixtest file SD0:/tmp", 120),
]


def main():
    argv = sys.argv[1:]

    def option(name, default):
        if name not in argv:
            return default
        i = argv.index(name)
        v = argv[i + 1]
        del argv[i:i + 2]
        return int(v)

    port = option("--port", 23)
    rounds = option("--rounds", 30)
    first = option("--first", 0)
    host = argv[0] if argv else os.environ.get("ARMPI_PI", "192.168.1.20")
    log = open(OUT, "w", encoding="utf-8")

    def say(s=""):
        print(s, flush=True)
        log.write(s + "\n")
        log.flush()

    t = [Telnet(host, port)]
    time.sleep(1)
    bad = 0

    def run(name, cmd, timeout):
        nonlocal bad
        t0 = time.time()
        try:
            out, code = t[0].run(cmd, timeout)
        except OSError as e:
            say("%-16s COUPURE apres %.1f s (%s)" % (name, time.time() - t0, e))
            bad += 1
            t0 = time.time()
            while True:
                try:
                    t[0] = Telnet(host, port)
                    break
                except OSError:
                    if time.time() - t0 > 600:
                        raise
                    time.sleep(5)
            say("%-16s reconnexion apres %.1f s" % (name, time.time() - t0))
            time.sleep(1)
            return ""
        lines = [l.split("> ")[-1] for l in out.splitlines()]		# (la premiere suit l'invite)
        ok = sum(1 for l in lines if l.startswith("PASS"))
        ko = [l.strip() for l in lines if l.startswith("FAIL")]
        tail = [l.strip() for l in out.splitlines() if " passed," in l]
        say("%-16s %6.1f s  code %d  %s" % (name, time.time() - t0, code, tail[-1] if tail else "PASS %d, FAIL %d" % (ok, len(ko))))
        for l in ko:
            say("    " + l)
        bad += len(ko) + (1 if code else 0)
        log.write(out + "\n")
        return out

    run("memoire", "mem", 30)
    run("dossier", "mkdir RAM0:/tmp", 30)
    run("dossier", "mkdir -p SD0:/tmp", 30)
    for name, cmd, timeout in TESTS[first:]:
        run(name, cmd, timeout)
    run("preparer -j", "mkdir -p /tmp/par ; cp /gcc/test/projet/par.mk /tmp/par ; cd /tmp/par", 30)
    ko = 0
    t0 = time.time()
    for i in range(rounds):
        out = run("make -j2 %d" % (i + 1), "/gcc/bin/make -j2 -f par.mk | grep -c 'a fini' ; rm b.ok", 60)
        if not any(l.strip() == "1" for l in out.splitlines()):
            ko += 1
    say("make -j2 : %d tours, %d echecs, %.1f s" % (rounds, ko, time.time() - t0))
    bad += ko
    run("nettoyer", "cd /tmp ; rm -r /tmp/par", 30)
    run("memoire", "mem", 30)
    say("RESULTAT : %s" % ("rien a signaler" if bad == 0 else "%d problemes" % bad))
    log.close()


if __name__ == "__main__":
    main()
