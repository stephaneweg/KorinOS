"""Korin OS pour PC sous QEMU q35 avec un disque NVMe (docs/etude/PC-PILOTES.md,
« NVMe ») : essai de src/svc/nvme sur une IMAGE d'essai (jamais un vrai disque).

  python -I tools/pc/qemu_nvme.py IMAGE [options] [--step ...]...

IMAGE : image d'essai, par exemple celle de tools/pc/mkgpt.py (disque GPT qui imite le
ZBook) ; refusée si ce n'est pas un fichier ordinaire (\\\\.\\PhysicalDrive…, un volume).

Options :
  --mdts N       transfert maximal du contrôleur, 2^N pages de 4 Ko (défaut de QEMU : 7)
  --smp N        coeurs (défaut 4)
  --wait S       secondes avant la première étape (défaut 12)
  --pause S      secondes après chaque ligne tapée (défaut 3)
  --esp DIR      le volume de démarrage (défaut build/pc/esp)
  --vars FICHIER variables UEFI d'OVMF (défaut build/pc/ovmf_vars_nvme.fd, à part : OVMF y
                 inscrit le disque NVMe, les autres essais ne doivent pas le voir)
  --log FICHIER  copie du journal (port 0xE9) à la fin
  --boot-disk    démarrer du disque NVMe lui-même (pas de build/pc/esp) : une image de
                 tools/pc/mkkorindisk.py, dont le startup.nsh lance \EFI\Korin\korin.efi de
                 KORIN OS (comme F9 sur le ZBook) ; avec --vars à part
  --snapshot     l'image n'est pas écrite (snapshot=on)
  --shot FICHIER capture à la fin (défaut build/pc/screen_nvme.png)
  --qemu ARGS    arguments de plus pour QEMU (séparés par des espaces), par exemple deux
                 contrôleurs USB : "-device qemu-xhci,id=xa -device usb-kbd,bus=xa.0
                 -device qemu-xhci,id=xb -device usb-mouse,bus=xb.0" ; plusieurs fois permis
Étapes, dans l'ordre (plusieurs fois permis) :
  --step "type:LIGNE"   une ligne tapée au moniteur (AZERTY), puis --pause
  --step "key:TOUCHE"   une touche de sendkey (ctrl-c, ret...), puis --pause
  --step "shot:NOM"     une capture (build/pc/NOM.png)
  --step "hmp:COMMANDE" une commande du moniteur de QEMU
  --step "sleep:S"      attendre
  --step "abs:X Y"      pointeur absolu (0..32767), avec une tablette : --qemu "-device qemu-xhci,id=x
                        -device usb-tablet,bus=x.0"
  --step "btn:left 1"   bouton enfoncé (1) ou relâché (0)
Le disque est le lecteur « nv » (nvme,serial=KORINTEST). Les fonctions de QEMU viennent
de tools/pc/qemu_pc.py.
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location("qemu_pc", os.path.join(HERE, "qemu_pc.py"))
qp = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qp)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--mdts", type=int, default=-1)
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--wait", type=float, default=12)
    ap.add_argument("--pause", type=float, default=3)
    ap.add_argument("--esp", default="")
    ap.add_argument("--vars", default=os.path.join(qp.OUT, "ovmf_vars_nvme.fd"))
    ap.add_argument("--log", default="")
    ap.add_argument("--shot", default=os.path.join(qp.OUT, "screen_nvme.png"))
    ap.add_argument("--step", action="append", default=[])
    ap.add_argument("--boot-disk", action="store_true")
    ap.add_argument("--snapshot", action="store_true")
    ap.add_argument("--qemu", action="append", default=[])
    a = ap.parse_args()
    img = os.path.abspath(a.image)
    if img.startswith("\\\\") or not os.path.isfile(img):
        raise SystemExit("IMAGE doit être un fichier d'essai ordinaire : %s" % a.image)
    esp = os.path.abspath(a.esp) if a.esp else None
    if not os.path.exists(os.path.join(esp or os.path.join(qp.OUT, "esp"), "korin", "kernel.elf")):
        raise SystemExit("construire d'abord : wsl sh tools/pc/build.sh")
    log = qp.DEBUGLOG
    if os.path.exists(log):
        os.remove(log)
    dev = "nvme,serial=KORINTEST,drive=nv"
    if a.mdts >= 0:
        dev += ",mdts=%d" % a.mdts
    snap = ",snapshot=on" if a.snapshot else ""
    nv = ["-drive", "if=none,id=nv,format=raw%s,file=%s" % (snap, img.replace("\\", "/")), "-device", dev]
    port = 47000 + os.getpid() % 1000
    p = subprocess.Popen(qp.qemu_args(True, port, a.smp, esp=esp, vars_fd=os.path.abspath(a.vars),
                                      boot_drive=not a.boot_disk) + nv +
                         [x for q in a.qemu for x in q.split()])
    try:
        q = qp.Qmp(port)
        time.sleep(a.wait)
        for st in a.step:
            kind, _, arg = st.partition(":")
            if kind == "type":
                for k in qp.azerty_keys(arg):
                    q.hmp("sendkey " + k)
                    time.sleep(0.08)
                time.sleep(a.pause)
            elif kind == "key":
                q.hmp("sendkey " + arg)
                time.sleep(a.pause)
            elif kind == "shot":
                f = os.path.join(qp.OUT, arg + ".png").replace("\\", "/")
                q.cmd("screendump", filename=f, format="png")
                print("capture :", f)
            elif kind == "hmp":
                r = q.hmp(arg)
                print("hmp %s : %s" % (arg, r.get("return", r)))
            elif kind == "sleep":
                time.sleep(float(arg))
            elif kind == "abs":  # pointeur absolu 0..32767 (avec une usb-tablet : --qemu)
                x, y = arg.split()
                q.cmd("input-send-event", events=[
                    {"type": "abs", "data": {"axis": "x", "value": int(x)}},
                    {"type": "abs", "data": {"axis": "y", "value": int(y)}}])
                time.sleep(0.3)
            elif kind == "btn":  # « left 1 », « left 0 »
                b, down = arg.split()
                q.cmd("input-send-event", events=[{"type": "btn", "data": {"down": down == "1", "button": b}}])
                time.sleep(0.3)
            else:
                print("étape inconnue :", st)
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
        if a.log:
            shutil.copyfile(log, a.log)
        with open(log, encoding="utf-8", errors="replace") as f:
            sys.stdout.write(f.read())


if __name__ == "__main__":
    main()
