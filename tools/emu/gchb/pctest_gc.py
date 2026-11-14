"""pctest_gc : gcemu sous QEMU q35 (Korin PC, GlKit en softpipe : pas de GPU Intel sous QEMU) avec les DOL de
tools/emu/gchb, l'equivalent de tools/python/pctest.py sans Python ni .NET.

  python -I tools/emu/gchb/pctest_gc.py prep [--mb 600]
      build/pc/gctest/root : une COPIE de build/pc/sysroot (sans python, dotnet, gcc ; build/pc/sysroot n'est que
      lu, sous le verrou build/pc.lock), puis par-dessus gcemu (build/pc/apps/sdroot/apps/gcemu.app), EmuKit
      (build/kits/x86_64/emukit.so), les DOL (build/emu/gchb/*.dol dans SD0:/home/roms), telnetd ; puis le disque
      build/pc/gctest/disk.img (mkkorindisk.py --layout zbook) ; --quick : la copie gardee, seulement le reste
  python -I tools/emu/gchb/pctest_gc.py run [--port 2395] ... "commande" | shot:NOM | sleep:S | click:X,Y
      comme pctest.py run, dans build/pc/gctest (journal du noyau : build/pc/gctest/debug.log)
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
PC = os.path.join(ROOT, "build", "pc")
W = os.path.join(PC, "gctest")
TROOT = os.path.join(W, "root")
IMG = os.path.join(W, "disk.img")
LOCK = os.path.join(ROOT, "build", "pc.lock")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def prep(a):
    if not os.path.isdir(os.path.join(PC, "sysroot")):
        raise SystemExit("absent : build/pc/sysroot")
    if a.quick and os.path.isdir(TROOT):
        overlay(a)
        return
    if os.path.isdir(TROOT):
        shutil.rmtree(TROOT)		# (notre copie, dans build/pc/gctest)
    os.makedirs(W, exist_ok=True)
    for _ in range(600):
        try:
            os.mkdir(LOCK)
            break
        except FileExistsError:
            time.sleep(1)
    else:
        raise SystemExit("build/pc.lock pris depuis 10 min")
    try:
        shutil.copytree(os.path.join(PC, "sysroot"), TROOT,
                        ignore=lambda d, n: [x for x in n if os.path.samefile(d, os.path.join(PC, "sysroot"))
                                             and x in ("python", "dotnet", "gcc", ".vibe")])
    finally:
        os.rmdir(LOCK)
    overlay(a)


def overlay(a):
    app = os.path.join(TROOT, "apps", "gcemu.app")
    shutil.copy(os.path.join(PC, "apps", "sdroot", "apps", "gcemu.app", "main"), os.path.join(app, "main"))
    shutil.copy(os.path.join(ROOT, "build", "kits", "x86_64", "emukit.so"), os.path.join(TROOT, "lib", "emukit.so"))
    roms = os.path.join(TROOT, "home", "roms")
    os.makedirs(roms, exist_ok=True)
    hb = os.path.join(ROOT, "build", "emu", "gchb")
    for f in os.listdir(hb):
        if f.endswith(".dol"):
            shutil.copy(os.path.join(hb, f), os.path.join(roms, f))
    etc = os.path.join(TROOT, "etc")
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "SD0:/sys/svc/telnetd" not in s:
        s += "# essais de tools/emu/gchb/pctest_gc.py : telnet\nSD0:/sys/svc/telnetd\n"
        open(sv, "w", encoding="utf-8", newline="\n").write(s)
    if a.trace:
        open(os.path.join(etc, "gcemu-trace"), "w").close()
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "mkkorindisk.py"), IMG,
                           "--layout", "zbook", "--korin-mb", str(a.mb), "--sysroot", TROOT,
                           "--cfg", "default = korin\ntimeout = 0"])
    print(IMG, os.path.getsize(IMG))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["prep", "run"])
    ap.add_argument("cmd", nargs="*")
    ap.add_argument("--mb", type=int, default=600)
    ap.add_argument("--trace", action="store_true", help="prep : SD0:/etc/gcemu-trace")
    ap.add_argument("--quick", action="store_true", help="prep : garder la copie de l'arbre, refaire le reste")
    ap.add_argument("--port", type=int, default=2395)
    ap.add_argument("--hostfwd", action="append", default=[])
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot", type=float, default=120)
    ap.add_argument("--shot", default="")
    ap.add_argument("--qemu", default="")
    a = ap.parse_args()
    if a.mode == "prep":
        prep(a)
        return 0
    pt = load("pctest", os.path.join(ROOT, "tools", "python", "pctest.py"))
    pt.W, pt.TROOT, pt.IMG = W, TROOT, IMG
    return pt.run(a)


if __name__ == "__main__":
    sys.exit(main())
