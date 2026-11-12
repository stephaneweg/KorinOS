"""Korin OS pour PC sous QEMU q35 avec une carte SD sur un contrôleur SDHCI PCI
(docs/etude/PC-PILOTES.md, « Carte SD ») : essai de src/svc/sdhci et blktest.

  python -I tools/pc/qemu_sd.py IMAGE [options] [--step ...]...

IMAGE : image de carte (taille en puissance de 2 : QEMU l'exige), par exemple
  python -I tools/mkdisk.py build/pc/sdtest.img --fat32 skel/sd0/etc --taille 64

Options :
  --spec N       version SDHCI du contrôleur (2 par défaut, comme QEMU ; 3 : diviseur de
                 10 bits, ADMA2 64 bits si --caps le dit)
  --caps HEX     registre des capacités (défaut de QEMU : 057834b4 ; 64 bits : 157834b4)
  --smp N        coeurs (défaut 4)
  --wait S       secondes avant la première étape (défaut 12)
  --pause S      secondes après chaque ligne tapée (défaut 3)
  --shot FICHIER capture à la fin (défaut build/pc/screen_sd.png)
  --esp DIR      le volume de démarrage (défaut build/pc/esp)
Étapes, dans l'ordre (plusieurs fois permis) :
  --step "type:LIGNE"   une ligne tapée au moniteur (AZERTY), puis --pause
  --step "hmp:COMMANDE" une commande du moniteur de QEMU (« eject -f sdcard »,
                        « change sdcard IMAGE raw » : la carte retirée, remise)
  --step "sleep:S"      attendre
La carte est le lecteur « sdcard ». Le journal (port 0xE9) est affiché à la fin
(build/pc/debug.log). Les fonctions de QEMU viennent de tools/pc/qemu_pc.py.
"""
import argparse
import importlib.util
import os
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
    ap.add_argument("--spec", type=int, default=2)
    ap.add_argument("--caps", default="")
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--wait", type=float, default=12)
    ap.add_argument("--pause", type=float, default=3)
    ap.add_argument("--shot", default=os.path.join(qp.OUT, "screen_sd.png"))
    ap.add_argument("--step", action="append", default=[])
    ap.add_argument("--esp", default="")
    a = ap.parse_args()
    esp = os.path.abspath(a.esp) if a.esp else None
    if not os.path.exists(os.path.join(esp or os.path.join(qp.OUT, "esp"), "korin", "kernel.elf")):
        raise SystemExit("construire d'abord : wsl sh tools/pc/build.sh")
    log = os.path.join(qp.OUT, "debug.log")
    if os.path.exists(log):
        os.remove(log)
    img = os.path.abspath(a.image).replace("\\", "/")
    dev = "sdhci-pci,id=sdhc,sd-spec-version=%d" % a.spec
    if a.caps:
        dev += ",capareg=0x" + a.caps
    sd = ["-device", dev, "-drive", "if=none,id=sdcard,format=raw,file=" + img,
          "-device", "sd-card,drive=sdcard"]
    port = 47000 + os.getpid() % 1000
    p = subprocess.Popen(qp.qemu_args(True, port, a.smp, esp=esp) + sd)
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
            elif kind == "hmp":
                r = q.hmp(arg)
                print("hmp %s : %s" % (arg, r.get("return", r)))
            elif kind == "sleep":
                time.sleep(float(arg))
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
        with open(log, encoding="utf-8", errors="replace") as f:
            sys.stdout.write(f.read())


if __name__ == "__main__":
    main()
