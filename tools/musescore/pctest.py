"""pctest : MuseScore Studio sur Korin PC x86-64 sous QEMU q35 (docs/etude/MUSESCORE.md). Comme tools/qt/pctest.py.

  python3 -I tools/musescore/pctest.py prep [--mb 1200] [--theme "Onyx Noir"]
      ~/korin-mscore/mstest/root : une COPIE de build/pc/sysroot, l'app (build/pc/musescore-pkg : stage.sh) dans
      /apps/musescore.app, des partitions de démonstration de la source dans /home/ms, telnet ; le disque disk.img
  python3 -I tools/musescore/pctest.py run [--mem 6G] "chmod +x /apps/musescore.app/main" \
      "@600 /apps/musescore.app/main -o /home/ms/out.pdf /home/ms/demo.mscz" "spawn:/apps/musescore.app/main" "shot:ms"
"""
import argparse
import glob
import importlib.util
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = os.path.join(ROOT, "build", "pc")
K = os.environ.get("KORIN_MSCORE", os.path.expanduser("~/korin-mscore"))
W = os.path.join(K, "mstest")
TROOT = os.path.join(W, "root")
IMG = os.path.join(W, "disk.img")
PKG = os.path.join(PC, "musescore-pkg")
DEMOS = os.path.join(K, "src", "musescore", "demos")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def prep(a):
    if not os.path.isdir(os.path.join(PC, "sysroot")):
        raise SystemExit("absent : build/pc/sysroot (sh tools/pc/build.sh puis sh tools/pc/sysroot.sh)")
    if not os.path.isfile(os.path.join(PKG, "apps", "musescore.app", "main")):
        raise SystemExit("absent : %s (sh tools/musescore/stage.sh)" % PKG)
    shutil.rmtree(TROOT, ignore_errors=True)
    os.makedirs(W, exist_ok=True)
    shutil.copytree(os.path.join(PC, "sysroot"), TROOT,
                    ignore=lambda d, names: ["devtools"] if os.path.samefile(d, os.path.join(PC, "sysroot")) else [])
    shutil.copytree(os.path.join(PKG, "apps"), os.path.join(TROOT, "apps"), dirs_exist_ok=True)
    # (le service d'impression, pour Fichier > Imprimer : tools/pc/apps.sh printd ; PrinterKit le lance s'il manque)
    pd = os.path.join(PC, "apps", "sdroot", "sys", "printd")
    if os.path.isfile(pd) and not os.path.isfile(os.path.join(TROOT, "sys", "printd")):
        shutil.copy(pd, os.path.join(TROOT, "sys", "printd"))
    t = os.path.join(TROOT, "home", "ms")
    os.makedirs(t, exist_ok=True)
    for f in sorted(glob.glob(os.path.join(DEMOS, "*.mscz")))[:4] + [os.path.join(DEMOS, "Dawn.mscx")]:
        shutil.copy(f, os.path.join(t, os.path.basename(f).replace(" ", "_")))
    # (les essais : le premier lancement passé, pas d'introduction ; HOME=/home sous telnet)
    cfg = os.path.join(TROOT, "home", ".config", "MuseScore")
    os.makedirs(cfg, exist_ok=True)
    open(os.path.join(cfg, "MuseScore4.ini"), "w", encoding="utf-8", newline="\n").write(
        "[application]\nhasCompletedFirstLaunchSetup=true\n")
    etc = os.path.join(TROOT, "etc")
    if a.theme:
        th = os.path.join(etc, "theme.txt")
        s = open(th, encoding="utf-8").read()
        s = "\n".join("theme    = " + a.theme if l.startswith("theme ") else l for l in s.split("\n"))
        open(th, "w", encoding="utf-8", newline="\n").write(s)
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "/sys/svc/telnetd" not in s:
        s += "# essais de tools/musescore/pctest.py : telnet\n/sys/svc/telnetd\n"
        open(sv, "w", encoding="utf-8", newline="\n").write(s)
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "mkkorindisk.py"), IMG,
                           "--layout", "zbook", "--korin-mb", str(a.mb), "--sysroot", TROOT,
                           "--cfg", "default = korin\ntimeout = 0"])
    print(IMG, os.path.getsize(IMG), "; partitions :", os.listdir(t))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["prep", "run"])
    ap.add_argument("cmd", nargs="*")
    ap.add_argument("--mb", type=int, default=1200)
    ap.add_argument("--theme", default="")
    ap.add_argument("--bin", default="")
    ap.add_argument("--port", type=int, default=2402)
    ap.add_argument("--hostfwd", action="append", default=[])
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="6G")
    ap.add_argument("--boot", type=float, default=150)
    ap.add_argument("--shot", default="")
    ap.add_argument("--qemu", default="")
    a = ap.parse_args()
    if a.mode == "prep":
        prep(a)
        return
    pt = load("pctest", os.path.join(ROOT, "tools", "python", "pctest.py"))
    pt.W, pt.TROOT, pt.IMG = W, TROOT, IMG
    sys.exit(pt.run(a))


if __name__ == "__main__":
    main()
