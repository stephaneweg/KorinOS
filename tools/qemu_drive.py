"""Pilote ARMPI dans QEMU raspi4b par QMP : souris absolue, clavier, captures.

Usage : python -I tools/qemu_drive.py script.txt [--port 4444] [--vnc 55] [--usbdisk img]
        [--kernel build/kernel8.img] [--sd build/sd.img] [--ecran 1024x768]
        [--modules a.mod,b.mod]  (comme « initramfs » : mis bout a bout a 0x02000000)
Le script contient une commande par ligne :
  wait <secondes>
  screen <LxH>              (les coordonnees suivantes : nouvelle resolution de l'invite)
  move <x> <y>              (coordonnees ecran, 1024x768 sauf --ecran)
  click <x> <y> [bouton]    (left/right)
  dclick <x> <y>            (double-clic)
  mclick <shift|ctrl> <x> <y>  (clic, la touche tenue)
  wheel <x> <y> <crans>     (molette, > 0 : vers le haut)
  drag <x1> <y1> <x2> <y2>
  key <touches QEMU>        (ex. 'a', 'shift-a', 'ret', 'spc', 'backspace')
  type <texte>              (lettres/chiffres/espaces, disposition US cote QEMU)
  shot <fichier.png>
  hmp <commande>            (moniteur QEMU, ex. pmemsave adresse taille fichier)
  run <commande>            (commande du PC, ex. un essai Python des serveurs ; sa
                             sortie est affichee)
Options reseau : --fwd 2323:23,2121:21 (redirections TCP de plus, hote:invite) ;
--uart fichier (journal de la console, build/uart.log par defaut).
Le noyau, la carte SD et les journaux sont ceux de build/.
"""
import json
import socket
import subprocess
import sys
import time

QEMU = r"C:\Program Files\qemu\qemu-system-aarch64.exe"
W, H = 1024, 768


class Qmp:
    def __init__(self, port):
        for _ in range(100):
            try:
                self.s = socket.create_connection(("127.0.0.1", port))
                break
            except OSError:
                time.sleep(0.1)
        self.f = self.s.makefile("rw")
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

    def abs(self, x, y):
        self.cmd("input-send-event", events=[
            {"type": "abs", "data": {"axis": "x", "value": int(x * 32767 / (W - 1))}},
            {"type": "abs", "data": {"axis": "y", "value": int(y * 32767 / (H - 1))}}])

    def btn(self, down, b="left"):
        self.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down, "button": b}}])


def main():
    script = open(sys.argv[1], encoding="utf-8").read().splitlines()
    port = 4444
    vnc = 55
    if "--port" in sys.argv:
        port = int(sys.argv[sys.argv.index("--port") + 1])
    if "--vnc" in sys.argv:
        vnc = int(sys.argv[sys.argv.index("--vnc") + 1])
    global W, H
    kernel, sd = "build/kernel8.img", "build/sd.img"
    if "--kernel" in sys.argv:                  # noyau d'une autre resolution
        kernel = sys.argv[sys.argv.index("--kernel") + 1]
    if "--sd" in sys.argv:
        sd = sys.argv[sys.argv.index("--sd") + 1]
    if "--ecran" in sys.argv:
        W, H = map(int, sys.argv[sys.argv.index("--ecran") + 1].split("x"))
    extra = []
    fwd = "hostfwd=tcp:127.0.0.1:%d-:80" % (8080 + port - 4444)
    if "--fwd" in sys.argv:                     # redirections de plus
        for r in sys.argv[sys.argv.index("--fwd") + 1].split(","):
            h, g = r.split(":")
            fwd += ",hostfwd=tcp:127.0.0.1:%s-:%s" % (h, g)
    uart = "build/uart.log"
    if "--uart" in sys.argv:
        uart = sys.argv[sys.argv.index("--uart") + 1]
    if "--usbdisk" in sys.argv:                 # cle USB (stockage de masse)
        img = sys.argv[sys.argv.index("--usbdisk") + 1]
        extra = ["-drive", "file=%s,if=none,id=cle,format=raw" % img,
                 "-device", "usb-storage,drive=cle"]
    noms = []                                   # modules de demarrage (initramfs) :
    if "--modules" in sys.argv:                 # --modules, sinon la ligne de
        noms = sys.argv[sys.argv.index("--modules") + 1].split(",")
    else:                                       # boot/config.txt (build/modules)
        for l in open("boot/config.txt", encoding="utf-8"):
            if l.startswith("initramfs "):
                noms = ["build/modules/" + n for n in l.split()[1].split(",")]
                break
    if noms:                                    # (par port : plusieurs QEMU peuvent tourner)
        ini = "build/initramfs.bin" if port == 4444 else "build/initramfs_%d.bin" % port
        with open(ini, "wb") as f:
            for n in noms:
                f.write(open(n, "rb").read())
        extra += ["-device", "loader,file=%s,addr=0x02000000,force-raw=on" % ini]
    p = subprocess.Popen([QEMU, "-M", "raspi4b", "-kernel", kernel,
                          "-dtb", "firmware/bcm2711-rpi-4-b.dtb", "-serial", "file:" + uart,
                          "-vnc", "127.0.0.1:%d" % vnc, "-qmp", "tcp:127.0.0.1:%d,server,nowait" % port,
                          "-drive", "file=%s,if=sd,format=raw" % sd,
                          "-device", "usb-kbd", "-device", "usb-tablet",
                          "-device", "usb-net,netdev=n0",
                          "-device", "usb-audio,audiodev=snd0",
                          "-audiodev", "wav,id=snd0,path=%s,out.frequency=48000" % ("build/out.wav" if port == 4444 else "build/out_%d.wav" % port),
                          "-netdev", "user,id=n0," + fwd] + extra,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        q = Qmp(port)
        for line in script:
            parts = line.split()
            if not parts or parts[0].startswith("#"):
                continue
            op = parts[0]
            if op == "wait":
                time.sleep(float(parts[1]))
            elif op == "screen":                # l'invite a change de resolution
                W, H = map(int, parts[1].split("x"))
            elif op == "move":
                q.abs(int(parts[1]), int(parts[2]))
            elif op == "click":
                b = parts[3] if len(parts) > 3 else "left"
                q.abs(int(parts[1]), int(parts[2]))
                time.sleep(0.15)
                q.btn(True, b)
                time.sleep(0.15)
                q.btn(False, b)
                time.sleep(0.2)
            elif op == "mclick":                # mclick <shift|ctrl|alt> <x> <y> : clic, la touche tenue
                k = {"type": "qcode", "data": parts[1]}
                q.cmd("input-send-event", events=[{"type": "key", "data": {"down": True, "key": k}}])
                time.sleep(0.2)
                q.abs(int(parts[2]), int(parts[3]))
                time.sleep(0.15)
                q.btn(True)
                time.sleep(0.15)
                q.btn(False)
                time.sleep(0.2)
                q.cmd("input-send-event", events=[{"type": "key", "data": {"down": False, "key": k}}])
                time.sleep(0.2)
            elif op == "dclick":                # double-clic (deux clics rapides)
                q.abs(int(parts[1]), int(parts[2]))
                time.sleep(0.15)
                for _ in range(2):
                    q.btn(True)
                    time.sleep(0.05)
                    q.btn(False)
                    time.sleep(0.08)
                time.sleep(0.2)
            elif op == "wheel":                 # wheel <x> <y> <crans> (> 0 : vers le haut)
                q.abs(int(parts[1]), int(parts[2]))
                time.sleep(0.15)
                n = int(parts[3])
                b = "wheel-up" if n > 0 else "wheel-down"
                for _ in range(abs(n)):
                    q.btn(True, b)
                    q.btn(False, b)
                    time.sleep(0.1)
                time.sleep(0.2)
            elif op == "drag":
                x1, y1, x2, y2 = map(int, parts[1:5])
                q.abs(x1, y1)
                time.sleep(0.15)
                q.btn(True)
                time.sleep(0.3)             # (la tablette de QEMU fusionne appui et mouvement)
                for i in range(1, 11):
                    q.abs(x1 + (x2 - x1) * i // 10, y1 + (y2 - y1) * i // 10)
                    time.sleep(0.05)
                q.btn(False)
                time.sleep(0.2)
            elif op == "key":
                q.hmp("sendkey " + parts[1])
                time.sleep(0.12)
            elif op == "type":
                text = line.split(" ", 1)[1]
                for ch in text:
                    k = {" ": "spc", ".": "dot", "/": "slash", "-": "minus", ",": "comma",
                         ":": "shift-semicolon", ";": "semicolon", "=": "equal",
                         "_": "shift-minus", '"': "shift-apostrophe", ">": "shift-dot",
                         "<": "shift-comma", "|": "shift-backslash", "@": "shift-2",
                         "#": "shift-3", "*": "shift-8", "+": "shift-equal"}.get(ch, ch)
                    if ch.isupper():
                        k = "shift-" + ch.lower()
                    q.hmp("sendkey " + k)
                    time.sleep(0.08)
            elif op == "run":                   # commande du PC (essai des serveurs)
                r = subprocess.run(line.split(None, 1)[1], shell=True,
                                   capture_output=True, text=True)
                print(r.stdout + r.stderr, end="")
            elif op == "hmp":                   # commande du moniteur (pmemsave...)
                r = q.hmp(line.split(None, 1)[1])
                print(r.get("return", r))
            elif op == "shot":
                ppm = parts[1] + ".ppm"
                q.hmp("screendump " + ppm.replace("\\", "/"))
                time.sleep(0.5)
                from PIL import Image
                Image.open(ppm).save(parts[1])
        q.hmp("quit")
    finally:
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()


if __name__ == "__main__":
    main()
