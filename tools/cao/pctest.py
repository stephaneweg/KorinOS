"""pctest : caotest (OCCT + PlaneGCS) sur Korin PC x86-64 sous QEMU q35 (docs/DESIGN-CAO.md, « Construction et
résultats »). Reprend tools/python/pctest.py (disque NVMe d'essai, telnet), dans build/pc/caotest.

  python3 -I tools/cao/pctest.py prep [--mb 400]
      l'arbre d'essai build/pc/caotest/root : une COPIE de build/pc/sysroot (jamais modifié), caotest dans
      /bin (depuis ~/korin-cao/x86_64/test, ou --bin), telnetd et le mot de passe d'essai « essai » ; puis
      le disque build/pc/caotest/disk.img
  python3 -I tools/cao/pctest.py run [--smp 4] "caotest /tmp" ...
      démarre QEMU (l'image en snapshot), lance les commandes par telnet, affiche leur sortie et leur code

À lancer là où QEMU tourne : dans WSL (qemu-system-x86_64 et OVMF de la distribution), ou sous Windows avec
C:\\Program Files\\qemu (--bin alors obligatoire).
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = os.path.join(ROOT, "build", "pc")
W = os.path.join(PC, "caotest")
TROOT = os.path.join(W, "root")
IMG = os.path.join(W, "disk.img")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def prep(a):
    if not os.path.isdir(os.path.join(PC, "sysroot")):
        raise SystemExit("absent : build/pc/sysroot (sh tools/pc/build.sh puis sh tools/pc/sysroot.sh)")
    b = a.bin or os.path.join(os.path.expanduser("~"), "korin-cao", "x86_64", "test", "caotest")
    if not os.path.isfile(b):
        raise SystemExit("absent : %s (sh tools/cao/build-test.sh)" % b)
    shutil.rmtree(TROOT, ignore_errors=True)
    os.makedirs(W, exist_ok=True)
    shutil.copytree(os.path.join(PC, "sysroot"), TROOT)
    shutil.copy(b, os.path.join(TROOT, "bin", "caotest"))
    etc = os.path.join(TROOT, "etc")
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "/sys/svc/telnetd" not in s:
        s += "# essais de tools/cao/pctest.py : telnet\n/sys/svc/telnetd\n"
        open(sv, "w", encoding="utf-8", newline="\n").write(s)
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "mkkorindisk.py"), IMG,
                           "--layout", "zbook", "--korin-mb", str(a.mb), "--sysroot", TROOT,
                           "--cfg", "default = korin\ntimeout = 0"])
    print(IMG, os.path.getsize(IMG))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["prep", "run"])
    ap.add_argument("cmd", nargs="*")
    ap.add_argument("--mb", type=int, default=400)
    ap.add_argument("--bin", default="")
    ap.add_argument("--port", type=int, default=2395)
    ap.add_argument("--hostfwd", action="append", default=[])
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="2G")
    ap.add_argument("--boot", type=float, default=90)
    ap.add_argument("--shot", default="")
    ap.add_argument("--qemu", default="")
    a = ap.parse_args()
    if a.mode == "prep":
        prep(a)
        return
    # le lancement de tools/python/pctest.py, sur le disque et le journal d'ici
    pt = load("pctest", os.path.join(ROOT, "tools", "python", "pctest.py"))
    pt.W, pt.TROOT, pt.IMG = W, TROOT, IMG
    sys.exit(pt.run(a))


if __name__ == "__main__":
    main()
