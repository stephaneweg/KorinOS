"""Mesures de GCC sur le Pi (docs/GCC.md) : chaque commande passe par telnet, chronometree du PC.

  python -I tools/gcc/pibench.py [HOTE] [--repeat N] [--no-preload] [--pinned]

--pinned : les outils sont deja epingles (une session precedente), seules les compilations
tournent. Une connexion coupee est notee, puis la mesure reprend sur une nouvelle session.

Le premier passage lit les outils sur la carte ; preload les epingle ensuite en memoire. Les
resultats sont aussi ecrits dans build/pibench.txt. Pas de mot de passe : telnetd n'en demande pas.
"""
import os
import socket
import sys
import time

LIBEXEC = "/gcc/libexec/gcc/aarch64-onyx-elf/14.2.0"
OUT = "build/pibench.txt"


class Telnet:
    def __init__(self, host, port=23):
        self.s = socket.create_connection((host, port), timeout=20)
        self.buf = b""

    def run(self, cmd, timeout):
        self.s.sendall(cmd.encode() + b"\r\n" + b"echo @FIN $?\r\n")
        end = time.time() + timeout
        while b"@FIN " not in self.buf or b"\n" not in self.buf.split(b"@FIN ", 1)[1]:
            if time.time() > end:
                raise TimeoutError(cmd)
            self.s.settimeout(max(0.1, end - time.time()))
            d = self.s.recv(65536)
            if not d:
                raise EOFError(cmd)
            self.buf += d.replace(b"\xff\xf1", b"")
        out, rest = self.buf.split(b"@FIN ", 1)
        line, self.buf = rest.split(b"\n", 1)
        return out.decode("utf-8", "replace"), int(line.strip() or b"0")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    host = args[0] if args else os.environ.get("ARMPI_PI", "192.168.1.20")
    repeat = int(sys.argv[sys.argv.index("--repeat") + 1]) if "--repeat" in sys.argv else 2
    log = open(OUT, "w", encoding="utf-8")

    def say(s=""):
        print(s, flush=True)
        log.write(s + "\n")
        log.flush()

    t = [Telnet(host)]
    time.sleep(1)

    def run(cmd, timeout=1800, show=False):
        t0 = time.time()
        try:
            out, code = t[0].run(cmd, timeout)
        except OSError as e:
            say("%8.2f s  COUPURE (%s) : %s" % (time.time() - t0, e, cmd))
            time.sleep(15)
            t[0] = Telnet(host)
            time.sleep(1)
            return ""
        dt = time.time() - t0
        say("%8.2f s  code %-3d %s" % (dt, code, cmd))
        if show or code:
            say(out[-1500:])
        return out

    say("== systeme")
    run("mem", 30, True)
    run("mkdir RAM0:/tmp", 30)
    if "--pinned" not in sys.argv:
        say("== verification")
        run("cat /etc/paths.txt", 30, True)
        run("posixtest proc", 120, True)
        say("== sans preload : outils lus sur la carte")
    tests = [
        ("gcc --version", "/gcc/bin/gcc --version"),
        ("hello.c -O0", "/gcc/bin/gcc /gcc/test/hello.c -o /tmp/h"),
        ("hello.cpp, en-tetes seuls (-E)", "/gcc/bin/g++ -std=gnu++20 -E /gcc/test/hello.cpp -o /tmp/h.ii"),
        ("hello.cpp -O0", "/gcc/bin/g++ -std=gnu++20 /gcc/test/hello.cpp -o /tmp/hx"),
        ("hello.cpp -O2", "/gcc/bin/g++ -std=gnu++20 -O2 /gcc/test/hello.cpp -o /tmp/hx"),
        ("posixtest-cxx.cpp -O2", "/gcc/bin/g++ -std=gnu++20 -O2 -D_GNU_SOURCE /gcc/test/posixtest-cxx.cpp -o /tmp/pt"),
    ]
    if "--pinned" not in sys.argv:
        for name, cmd in tests[:3] + tests[4:5]:
            run(cmd)
    if "--no-preload" not in sys.argv:
        say("== preload")
        for p in () if "--pinned" in sys.argv else ("/gcc/bin/gcc", "/gcc/bin/g++", LIBEXEC + "/cc1", LIBEXEC + "/cc1plus", LIBEXEC + "/collect2",
                  "/gcc/aarch64-onyx-elf/bin/as", "/gcc/aarch64-onyx-elf/bin/ld"):
            run("preload " + p, 120)
        for i in range(repeat):
            say("== outils en memoire, passage %d" % (i + 1))
            for name, cmd in tests:
                run(cmd)
    say("== les programmes produits")
    run("/tmp/h un deux", 30, True)
    run("/tmp/hx", 30, True)
    run("/tmp/pt", 120, True)
    run("mem", 30, True)
    log.close()


if __name__ == "__main__":
    main()
