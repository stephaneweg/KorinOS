"""pctest : les apps Craft et la démo d'egui sur Korin PC sous QEMU q35 (docs/CRAFT.md, docs/EGUI.md). Même
montage que tools/godot/pctest.py ; la boucle d'essai est celle de tools/python/pctest.py.

  python3 -I tools/craft/pctest.py prep [--mb 900] [--quick]
      build/pc/crafttest/root : une COPIE de build/pc/sysroot (sans /devtools), puis les apps de
      build/craft/x86_64/apps (<nom>.app : main, app.txt), build/craft/x86_64/bin dans HOME:/craft, telnetd ;
      puis le disque build/pc/crafttest/disk.img
  python3 -I tools/craft/pctest.py run [--port 2399] ... "commande" | shot:NOM | sleep:S | click:X,Y | key:K | type:T
      comme tools/python/pctest.py run, dans build/pc/crafttest (captures : build/pc/crafttest/NOM.png ; journal
      du noyau : build/pc/crafttest/debug.log). Clics : --qemu "-device usb-tablet,bus=xhci.0" (écran 1280×800).

Dans WSL (QEMU de Linux ; l'OVMF avec le shell UEFI : . ~/korin-cloud/env.sh). Exemple :
  python3 -I tools/craft/pctest.py run --qemu "-device usb-tablet,bus=xhci.0" \\
      "/apps/eguidemo.app/main &" sleep:20 shot:demo
Effacer build/pc/crafttest après l'essai (l'image fait près d'un Go).
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = os.path.join(ROOT, "build", "pc")
W = os.path.join(PC, "crafttest")
TROOT = os.path.join(W, "root")
IMG = os.path.join(W, "disk.img")
LOCK = os.path.join(ROOT, "build", "pc.lock")
CRAFT = os.path.join(ROOT, "build", "craft", "x86_64")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def prep(a):
    if not os.path.isdir(os.path.join(PC, "sysroot")):
        raise SystemExit("absent : build/pc/sysroot (wsl sh tools/pc/build.sh puis sh tools/pc/sysroot.sh)")
    if not (a.quick and os.path.isdir(TROOT)):
        if os.path.isdir(TROOT):
            shutil.rmtree(TROOT)		# (notre copie, dans build/pc/crafttest)
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
            src = os.path.join(PC, "sysroot")
            shutil.copytree(src, TROOT, ignore=lambda d, n: [x for x in n if os.path.samefile(d, src)
                                                             and x in ("devtools", "python", "dotnet", "gcc", ".vibe")])
        finally:
            os.rmdir(LOCK)
    overlay(a)


def overlay(a):
    apps = os.path.join(CRAFT, "apps")
    for n in sorted(os.listdir(apps)) if os.path.isdir(apps) else []:
        d = os.path.join(TROOT, "apps", n)
        if os.path.isdir(d):
            shutil.rmtree(d)
        shutil.copytree(os.path.join(apps, n), d)
        print("app :", n)
    home = os.path.join(TROOT, "home", "craft")
    if os.path.isdir(home):
        shutil.rmtree(home)
    os.makedirs(home)
    for d in (os.path.join(CRAFT, "bin"), os.path.join(ROOT, "tools", "craft", "test")):
        for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if os.path.isdir(os.path.join(apps, f + ".app")):
                continue			# (un programme qui est déjà une app : pas deux fois)
            shutil.copy(os.path.join(d, f), os.path.join(home, f))
    # le lanceur et le menu de Korin qui cachent la catégorie « Craft » (src/sys/common/applist.c, cat_hidden) :
    # ceux de build/sys/x86_64 (sh tools/sys/build_c.sh panel menubar), s'ils y sont
    for n in ("panel", "menubar"):
        f = os.path.join(ROOT, "build", "sys", "x86_64", n)
        if os.path.isfile(f):
            shutil.copy(f, os.path.join(TROOT, "sys", n))
            print("sys :", n)
    # l'OVMF de Linux ne lance pas le shell UEFI (startup.nsh) : le chargeur au chemin de secours des supports
    # amovibles, sur la partition KORIN OS (il y trouve korin/kernel.elf)
    boot = os.path.join(TROOT, "EFI", "BOOT")
    os.makedirs(boot, exist_ok=True)
    shutil.copy(os.path.join(PC, "esp", "EFI", "BOOT", "BOOTX64.EFI"), os.path.join(boot, "BOOTX64.EFI"))
    etc = os.path.join(TROOT, "etc")
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "/sys/svc/telnetd" not in s:
        s += "# essais de tools/craft/pctest.py : telnet\n/sys/svc/telnetd\n"
        open(sv, "w", encoding="utf-8", newline="\n").write(s)
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "mkkorindisk.py"), IMG,
                           "--layout", "zbook", "--korin-mb", str(a.mb), "--sysroot", TROOT, "--no-windows",
                           "--cfg", "default = korin\ntimeout = 0"])
    print(IMG, os.path.getsize(IMG))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["prep", "run"])
    ap.add_argument("cmd", nargs="*")
    ap.add_argument("--mb", type=int, default=900)
    ap.add_argument("--quick", action="store_true", help="prep : garder la copie de l'arbre, refaire le reste")
    ap.add_argument("--port", type=int, default=2399)
    ap.add_argument("--hostfwd", action="append", default=[])
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot", type=float, default=180)
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
