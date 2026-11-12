"""Korin OS pour PC x86-64 sous QEMU q35 + OVMF (UEFI).

  python -I tools/pc/qemu_pc.py run              fenêtre QEMU
  python -I tools/pc/qemu_pc.py test [options]   sans écran : capture PNG et journal

Options de test :
  --wait S        secondes avant la capture (défaut 12)
  --smp N         cœurs de la machine (défaut 4)
  --after S       secondes après les touches (défaut 1)
  --keys "a b"    touches envoyées avant la capture (noms de sendkey de QEMU)
  --shot FICHIER  capture (défaut build/pc/screen.png)
  --type "LIGNE"  une ligne tapée au clavier (AZERTY du noyau), puis Entrée ; plusieurs
                  fois permis, après --keys ; --pause S entre deux lignes (défaut 3)
  --usb           contrôleur qemu-xhci (MSI-X) et clavier usb-kbd (id ukbd)
  --usbdisk IMG   une clé usb-storage sur cette image (avec --usb)
  --usbnet        une carte usb-net (avec --usb ; réseau user de QEMU)
  --usbkeys "a b" touches envoyées à la fin (sendkey), quand le clavier USB les prend
  --usbkbd-late   le clavier USB est branché à chaud après les lignes tapées (--type) : tant
                  qu'il est là, QEMU lui donne toutes les touches, le moniteur n'en reçoit plus
  --usbmouse      une souris usb-mouse (id umouse) ; --usbtablet : une tablette usb-tablet (id utab)
  --late "CMD"    à la fin (après --usbkeys), plusieurs permis, --pause S entre deux :
                  « abs X Y » (pointeur absolu 0..32767, QMP), « btn left 1 » (bouton), sinon une
                  commande du moniteur de QEMU (« mouse_move 10 -5 », « device_del ucle »…) ; la clé
                  de --usbdisk a l'id ucle

Sans --usb, QEMU démarre comme avant (aucun périphérique de plus).

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


# caractères -> touches de sendkey (noms US) pour le clavier AZERTY du noyau
AZERTY = {"a": "q", "q": "a", "z": "w", "w": "z", "m": "semicolon", " ": "spc", "-": "6", '"': "3",
          ",": "m", ";": "comma", ":": "dot", "!": "slash", "_": "8", "'": "4", "(": "5", ")": "minus",
          "=": "equal", "*": "backslash", "/": "shift-dot", ".": "shift-comma", "+": "shift-equal",
          "%": "shift-apostrophe"}
for _d in "0123456789":
    AZERTY[_d] = "shift-" + _d


def azerty_keys(line):
    out = []
    for ch in line:
        if ch in AZERTY:
            out.append(AZERTY[ch])
        elif ch.isupper():
            out.append("shift-" + AZERTY.get(ch.lower(), ch.lower()))
        else:
            out.append(ch)
    return out + ["ret"]


def usb_args(a):
    if not a.usb:
        return []
    r = ["-device", "qemu-xhci,id=xhci"]
    if not a.usbkbd_late:
        r += ["-device", "usb-kbd,bus=xhci.0,id=ukbd"]
    if a.usbdisk:
        r += ["-drive", "if=none,id=cle,format=raw,file=" + a.usbdisk.replace("\\", "/"),
              "-device", "usb-storage,bus=xhci.0,drive=cle,id=ucle"]
    if a.usbmouse:
        r += ["-device", "usb-mouse,bus=xhci.0,id=umouse"]
    if a.usbtablet:
        r += ["-device", "usb-tablet,bus=xhci.0,id=utab"]
    if a.usbnet:
        r += ["-netdev", "user,id=un0", "-device", "usb-net,bus=xhci.0,netdev=un0"]
    return r


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
    ap.add_argument("--type", action="append", default=[])
    ap.add_argument("--pause", type=float, default=3)
    ap.add_argument("--usb", action="store_true")
    ap.add_argument("--usbdisk", default="")
    ap.add_argument("--usbnet", action="store_true")
    ap.add_argument("--usbkeys", default="")
    ap.add_argument("--usbkbd-late", action="store_true")
    ap.add_argument("--usbmouse", action="store_true")
    ap.add_argument("--usbtablet", action="store_true")
    ap.add_argument("--late", action="append", default=[])
    a = ap.parse_args()

    if not os.path.exists(os.path.join(OUT, "esp", "korin", "kernel.elf")):
        raise SystemExit("construire d'abord : wsl sh tools/pc/build.sh")
    log = os.path.join(OUT, "debug.log")
    if os.path.exists(log):
        os.remove(log)

    if a.mode == "run":
        subprocess.call(qemu_args(False, smp=a.smp) + usb_args(a))
        return

    port = 47000 + os.getpid() % 1000
    p = subprocess.Popen(qemu_args(True, port, a.smp) + usb_args(a))
    try:
        q = Qmp(port)
        time.sleep(a.wait)
        for k in a.keys.split():
            q.hmp("sendkey " + k)
            time.sleep(0.15)
        for line in a.type:
            for k in azerty_keys(line):
                q.hmp("sendkey " + k)
                time.sleep(0.08)
            time.sleep(a.pause)
        if a.usb and a.usbkbd_late:
            r = q.hmp("device_add usb-kbd,bus=xhci.0,id=ukbd")
            if r.get("return"):
                print("device_add :", r)
            time.sleep(a.pause)
        for k in a.usbkeys.split():  # (le clavier USB, branché en dernier, reçoit les touches)
            q.hmp("sendkey " + k)
            time.sleep(0.3)
        for cmd in a.late:
            w = cmd.split()
            if w and w[0] == "abs":
                r = q.cmd("input-send-event", events=[
                    {"type": "abs", "data": {"axis": "x", "value": int(w[1])}},
                    {"type": "abs", "data": {"axis": "y", "value": int(w[2])}}])
            elif w and w[0] == "btn":
                r = q.cmd("input-send-event", events=[
                    {"type": "btn", "data": {"down": w[2] == "1", "button": w[1]}}])
            else:
                r = q.hmp(cmd)
            if "error" in r or r.get("return"):
                print(cmd, ":", r)
            time.sleep(a.pause)
        if a.keys or a.type or a.usbkeys or a.late:
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
