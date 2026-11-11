"""Korin OS pour PC x86-64 sous QEMU q35 + OVMF (UEFI).

  python -I tools/pc/qemu_pc.py run              fenêtre QEMU
  python -I tools/pc/qemu_pc.py test [options]   sans écran : capture PNG et journal

Options de test :
  --wait S        secondes avant la capture (défaut 12)
  --smp N         cœurs de la machine (défaut 4)
  --after S       secondes après les touches (défaut 1)
  --keys "a b"    touches envoyées avant la capture (noms de sendkey de QEMU)
  --shot FICHIER  capture (défaut build/pc/screen.png)

Le volume de démarrage est le dossier build/pc/esp, vu par QEMU comme un
disque FAT (vvfat, en lecture seule). La sortie de débogage du noyau (port
0xE9) va dans build/pc/debug.log.
"""
import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "build", "pc")
QEMU_DIR = r"C:\Program Files\qemu"
QEMU = os.path.join(QEMU_DIR, "qemu-system-x86_64.exe")
CODE = os.path.join(QEMU_DIR, "share", "edk2-x86_64-code.fd")
VARS_TEMPLATE = os.path.join(QEMU_DIR, "share", "edk2-i386-vars.fd")


def qemu_args(headless, qmp_port=None, smp=4):
    vars_fd = os.path.join(OUT, "ovmf_vars.fd")
    if not os.path.exists(vars_fd):
        shutil.copyfile(VARS_TEMPLATE, vars_fd)
    esp = os.path.join(OUT, "esp").replace("\\", "/")
    args = [
        QEMU, "-M", "q35", "-cpu", "max", "-smp", str(smp), "-m", "2G",
        "-drive", "if=pflash,format=raw,readonly=on,file=" + CODE,
        "-drive", "if=pflash,format=raw,file=" + vars_fd,
        "-drive", "format=raw,file=fat:rw:" + esp,
        "-debugcon", "file:" + os.path.join(OUT, "debug.log").replace("\\", "/"),
        "-global", "isa-debugcon.iobase=0xe9",
        "-net", "none", "-no-reboot",
    ]
    if headless:
        args += ["-display", "none"]
    if qmp_port:
        args += ["-qmp", "tcp:127.0.0.1:%d,server,nowait" % qmp_port]
    return args


class Qmp:
    def __init__(self, port):
        for _ in range(100):
            try:
                self.s = socket.create_connection(("127.0.0.1", port), timeout=5)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise SystemExit("QMP injoignable")
        self.f = self.s.makefile("rw", encoding="utf-8")
        self.f.readline()
        self.cmd("qmp_capabilities")

    def cmd(self, name, **args):
        self.f.write(json.dumps({"execute": name, "arguments": args}) + "\n")
        self.f.flush()
        while True:
            r = json.loads(self.f.readline())
            if "return" in r or "error" in r:
                return r

    def hmp(self, line):
        return self.cmd("human-monitor-command", **{"command-line": line})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["run", "test"])
    ap.add_argument("--wait", type=float, default=12)
    ap.add_argument("--smp", type=int, default=4, help="coeurs de la machine (defaut 4)")
    ap.add_argument("--after", type=float, default=1, help="secondes apres les touches (defaut 1)")
    ap.add_argument("--keys", default="")
    ap.add_argument("--shot", default=os.path.join(OUT, "screen.png"))
    a = ap.parse_args()

    if not os.path.exists(os.path.join(OUT, "esp", "korin", "kernel.elf")):
        raise SystemExit("construire d'abord : wsl sh tools/pc/build.sh")
    log = os.path.join(OUT, "debug.log")
    if os.path.exists(log):
        os.remove(log)

    if a.mode == "run":
        subprocess.call(qemu_args(False, smp=a.smp))
        return

    port = 47000 + os.getpid() % 1000
    p = subprocess.Popen(qemu_args(True, port, a.smp))
    try:
        q = Qmp(port)
        time.sleep(a.wait)
        for k in a.keys.split():
            q.hmp("sendkey " + k)
            time.sleep(0.15)
        if a.keys:
            time.sleep(a.after)
        shot = os.path.abspath(a.shot).replace("\\", "/")
        r = q.cmd("screendump", filename=shot, format="png")
        if "error" in r:
            print("capture :", r["error"])
        q.cmd("quit")
    finally:
        try:
            p.wait(10)
        except subprocess.TimeoutExpired:
            p.kill()
    print("capture :", a.shot)
    if os.path.exists(log):
        with open(log, encoding="utf-8", errors="replace") as f:
            sys.stdout.write(f.read())


if __name__ == "__main__":
    main()
