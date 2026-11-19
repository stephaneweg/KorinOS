"""Essai de liblinux sous QEMU (docs/etude/WIDEVINE.md) : un programme au moniteur du noyau du PC,
sans disque.

  python -I tools/linux/test_pc.py [--wait S] [--after S]          linuxhost --self-test
  python -I tools/linux/test_pc.py --probe [--after S]             la sonde du module Widevine
                                                                   (build/pc/linux/cdmprobe, construit par
                                                                   WIDEVINE_SO=… sh tools/linux/build.sh)
  (sans QEMU sous Windows : dans WSL, après « . ~/korin-cloud/env.sh », avec python3)

Fait une copie du volume de démarrage (--esp-src, défaut build/pc/esp) dans build/pc/linux/esp, ajoute le
programme à l'initrd de la copie, démarre QEMU sans écran sur la copie, tape la commande et lit le journal
du port 0xE9 : « linuxhost : RÉUSSI » ou « sonde : RÉUSSI » attendu. Capture : build/pc/linux/screen.png ;
journal : build/pc/linux/debug.log. Rien n'est écrit dans build/pc/esp ; la copie est effacée à la fin
(--keep pour la garder). Le noyau doit avoir la capture de syscall : construit à part,
PC_OUT=build/pc/linux/kbuild PC_QUICK=1 PC_SVC=pcid sh tools/pc/build.sh, puis --esp-src build/pc/linux/kbuild/esp.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tarfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = os.path.join(ROOT, "build", "pc")
OUT = os.path.join(PC, "linux")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wait", type=float, default=10)
    ap.add_argument("--after", type=float, default=12)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--probe", action="store_true", help="la sonde du module Widevine (cdmprobe)")
    ap.add_argument("--esp-src", default=os.path.join(PC, "esp"), help="le volume de démarrage à copier")
    a = ap.parse_args()
    name = "cdmprobe" if a.probe else "linuxhost"
    line = "run cdmprobe" if a.probe else "run linuxhost --self-test --stats"
    marker = "sonde : " if a.probe else "linuxhost : module"
    prog = os.path.join(OUT, name)
    if not os.path.exists(prog):
        sys.exit("d'abord : wsl sh tools/linux/build.sh" + (" (avec WIDEVINE_SO=…)" if a.probe else ""))
    esp = os.path.join(OUT, "esp")
    if os.path.exists(esp):
        shutil.rmtree(esp)
    shutil.copytree(a.esp_src, esp)
    initrd = os.path.join(esp, "korin", "initrd.tar")
    with tarfile.open(initrd, "a", format=tarfile.USTAR_FORMAT) as t:
        t.add(prog, arcname=name)
    log = os.path.join(OUT, "debug.log")
    env = dict(os.environ, KORIN_PC_DEBUGLOG=log)
    cmd = [sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "qemu_pc.py"), "test", "--esp", esp,
           "--wait", str(a.wait), "--type", line, "--after", str(a.after),
           "--shot", os.path.join(OUT, "screen.png")]
    subprocess.run(cmd, env=env)
    if not a.keep:
        shutil.rmtree(esp, ignore_errors=True)
    text = open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else ""
    start = text.find(marker)
    print(text[start:] if start >= 0 else text[-3000:])
    ok = ("sonde : RÉUSSI" if a.probe else "linuxhost : RÉUSSI") in text
    print("test_pc : " + ("RÉUSSI" if ok else "ÉCHEC (voir build/pc/linux/screen.png et debug.log)"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
