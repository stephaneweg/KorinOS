"""pctest : Godot sur Korin PC sous QEMU q35 (GlKit en softpipe : pas de GPU Intel sous QEMU), docs/GODOT.md.
Même montage que tools/emu/gchb/pctest_gc.py.

  python3 -I tools/godot/pctest.py prep [--mb 900] [--quick]
      build/pc/godottest/root : une COPIE de build/pc/sysroot (sans python, dotnet, gcc), puis Godot
      (build/pc/godot/sdroot/apps/godot.app), GlKit (build/pc/mesa/sdroot/lib/glkit.so), la démo et cli.gd
      (tools/godot/test -> SD0:/home/godot), telnetd ; puis le disque build/pc/godottest/disk.img
  python3 -I tools/godot/pctest.py run [--port 2397] ... "commande" | shot:NOM | sleep:S | click:X,Y
      comme tools/python/pctest.py run, dans build/pc/godottest (journal du noyau : build/pc/godottest/debug.log)

Dans WSL (QEMU et OVMF de Linux : qemu-system-x86, ovmf). Exemples :
  python3 -I tools/godot/pctest.py run "@300 /sd0/apps/godot.app/main --headless -s /sd0/home/godot/cli.gd"
  python3 -I tools/godot/pctest.py run --qemu "-device usb-tablet,bus=xhci.0" \\
      "@600 /sd0/apps/godot.app/main --path /sd0/home/godot/demo -- --quit-after-demo" shot:demo
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
W = os.path.join(PC, "godottest")
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
        raise SystemExit("absent : build/pc/sysroot (wsl sh tools/pc/build.sh puis sh tools/pc/sysroot.sh)")
    if not (a.quick and os.path.isdir(TROOT)):
        if os.path.isdir(TROOT):
            shutil.rmtree(TROOT)		# (notre copie, dans build/pc/godottest)
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
                                                             and x in ("python", "dotnet", "gcc", ".vibe")])
        finally:
            os.rmdir(LOCK)
    overlay(a)


def overlay(a):
    app = os.path.join(TROOT, "apps", "godot.app")
    os.makedirs(app, exist_ok=True)
    g = os.path.join(PC, "godot", "sdroot", "apps", "godot.app")
    for f in ("main", "app.txt"):
        shutil.copy(os.path.join(g, f), os.path.join(app, f))
    os.makedirs(os.path.join(TROOT, "lib"), exist_ok=True)
    shutil.copy(os.path.join(PC, "mesa", "sdroot", "lib", "glkit.so"), os.path.join(TROOT, "lib", "glkit.so"))
    home = os.path.join(TROOT, "home", "godot")
    if os.path.isdir(home):
        shutil.rmtree(home)
    shutil.copytree(os.path.join(ROOT, "tools", "godot", "test"), home)
    for d in a.extra:				# (prep --extra DOSSIER : un projet de plus dans SD0:/home/godot/<nom>)
        shutil.copytree(d, os.path.join(home, os.path.basename(os.path.normpath(d))),
                        ignore=shutil.ignore_patterns(".git"))
    # le modele d'export (tools/godot/build.sh template_release) : le binaire d'un jeu exporte, demo.x86_64 ;
    # il prend demo.pck a cote de lui
    tpl = os.path.join(PC, "godot", "templates", "template_release.x86_64")
    if os.path.isfile(tpl):
        shutil.copy(tpl, os.path.join(home, "demo.x86_64"))
    # l'OVMF de Linux ne lance pas le shell UEFI (startup.nsh) : le chargeur au chemin de secours des supports
    # amovibles, sur la partition KORIN OS (il y trouve korin/kernel.elf)
    boot = os.path.join(TROOT, "EFI", "BOOT")
    os.makedirs(boot, exist_ok=True)
    shutil.copy(os.path.join(PC, "esp", "EFI", "BOOT", "BOOTX64.EFI"), os.path.join(boot, "BOOTX64.EFI"))
    etc = os.path.join(TROOT, "etc")
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "SD0:/sys/svc/telnetd" not in s:
        s += "# essais de tools/godot/pctest.py : telnet\nSD0:/sys/svc/telnetd\n"
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
    ap.add_argument("--extra", action="append", default=[], help="prep : un projet Godot de plus (dossier)")
    ap.add_argument("--port", type=int, default=2397)
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
    # une session telnet qui tombe (programme qui emporte le shell) n'arrete pas l'essai : les captures (QMP)
    # et les clics continuent, les commandes suivantes sont sautees
    real_load = pt.load

    def load_tolerant(name, path):
        m = real_load(name, path)
        if name == "armpi_remote":
            run0 = m.Telnet.run

            def run(self, c, limit):
                if getattr(self, "dead", False):
                    return "(telnet perdu : commande sautee)\n", 255
                try:
                    return run0(self, c, limit)
                except (EOFError, OSError, TimeoutError) as e:
                    self.dead = True
                    return "(telnet : %s)\n" % str(e)[:160], 255
            m.Telnet.run = run
            m.Telnet.close = lambda self: None if getattr(self, "dead", False) else self.s.close()
        return m
    pt.load = load_tolerant
    return pt.run(a)


if __name__ == "__main__":
    sys.exit(main())
