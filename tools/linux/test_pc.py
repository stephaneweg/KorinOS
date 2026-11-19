"""Essai de liblinux sous QEMU (docs/etude/WIDEVINE.md, jalon 1) : linuxhost --self-test au moniteur
du noyau du PC, sans disque.

  python -I tools/linux/test_pc.py [--wait S]
  (sans QEMU sous Windows : dans WSL, après « . ~/korin-cloud/env.sh », avec python3)

Fait une copie de build/pc/esp dans build/pc/linux/esp (le noyau et l'initrd de tools/pc/build.sh),
ajoute linuxhost (tools/linux/build.sh) à l'initrd de la copie, démarre QEMU sans écran sur la
copie, tape « run linuxhost --self-test --stats » et lit le journal du port 0xE9 : « linuxhost :
RÉUSSI » attendu. Capture : build/pc/linux/screen.png ; journal : build/pc/linux/debug.log.
Rien n'est écrit dans build/pc/esp ; la copie est effacée à la fin (--keep pour la garder).
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
    a = ap.parse_args()
    host = os.path.join(OUT, "linuxhost")
    if not os.path.exists(host):
        sys.exit("d'abord : wsl sh tools/linux/build.sh")
    esp = os.path.join(OUT, "esp")
    if os.path.exists(esp):
        shutil.rmtree(esp)
    shutil.copytree(os.path.join(PC, "esp"), esp)
    initrd = os.path.join(esp, "korin", "initrd.tar")
    with tarfile.open(initrd, "a", format=tarfile.USTAR_FORMAT) as t:
        t.add(host, arcname="linuxhost")
    log = os.path.join(OUT, "debug.log")
    env = dict(os.environ, KORIN_PC_DEBUGLOG=log)
    cmd = [sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "qemu_pc.py"), "test", "--esp", esp,
           "--wait", str(a.wait), "--type", "run linuxhost --self-test --stats", "--after", str(a.after),
           "--shot", os.path.join(OUT, "screen.png")]
    subprocess.run(cmd, env=env)
    if not a.keep:
        shutil.rmtree(esp, ignore_errors=True)
    text = open(log, encoding="utf-8", errors="replace").read() if os.path.exists(log) else ""
    start = text.find("linuxhost : module")
    print(text[start:] if start >= 0 else text[-3000:])
    ok = "linuxhost : RÉUSSI" in text
    print("test_pc : " + ("RÉUSSI" if ok else "ÉCHEC (voir build/pc/linux/screen.png et debug.log)"))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
