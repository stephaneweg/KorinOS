"""pctest : Qt sur Korin PC x86-64 sous QEMU q35 (docs/etude/QT.md). Reprend tools/blender/pctest.py (disque NVMe
d'essai, telnet, le lancement de tools/python/pctest.py), dans ~/korin-qt/qttest.

  python3 -I tools/qt/pctest.py prep [--mb 600]
      l'arbre d'essai ~/korin-qt/qttest/root : une COPIE de build/pc/sysroot (jamais modifié), les programmes
      d'essai de tools/qt/test (tools/qt/test.sh) dans /home/qt, telnetd et le mot de passe d'essai « essai » ;
      puis le disque ~/korin-qt/qttest/disk.img
  python3 -I tools/qt/pctest.py run [--smp 4] [--mem 4G] "/home/qt/qtcore_test" "spawn:/home/qt/qtwidgets_test"
      "sleep:10" "shot:widgets" ...
      démarre QEMU (l'image en snapshot), lance les commandes par telnet (et les captures, clics, touches de
      tools/python/pctest.py), affiche leur sortie et leur code

Après `. ~/korin-cloud/env.sh` (docs/agents/CLOUD.md) ; `--qemu "-accel kvm"` sur une machine qui a KVM.
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = os.path.join(ROOT, "build", "pc")
K = os.environ.get("KORIN_QT", os.path.expanduser("~/korin-qt"))
W = os.path.join(K, "qttest")
TROOT = os.path.join(W, "root")
IMG = os.path.join(W, "disk.img")
TESTBIN = os.path.join(K, "x86_64", "test")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def prep(a):
    if not os.path.isdir(os.path.join(PC, "sysroot")):
        raise SystemExit("absent : build/pc/sysroot (sh tools/pc/build.sh puis sh tools/pc/sysroot.sh)")
    shutil.rmtree(TROOT, ignore_errors=True)
    os.makedirs(W, exist_ok=True)
    shutil.copytree(os.path.join(PC, "sysroot"), TROOT,
                    ignore=lambda d, names: ["devtools"] if os.path.samefile(d, os.path.join(PC, "sysroot")) else [])
    # (le service d'impression, pour PrintSupport : tools/pc/apps.sh printd ; PrinterKit le lance s'il manque)
    pd = os.path.join(PC, "apps", "sdroot", "sys", "printd")
    if os.path.isfile(pd) and not os.path.isfile(os.path.join(TROOT, "sys", "printd")):
        shutil.copy(pd, os.path.join(TROOT, "sys", "printd"))
    t = os.path.join(TROOT, "home", "qt")
    os.makedirs(t, exist_ok=True)
    n = 0
    for f in sorted(os.listdir(TESTBIN)) if os.path.isdir(TESTBIN) else []:
        p = os.path.join(TESTBIN, f)
        if f.startswith("qt") and f.endswith("_test") and os.path.isfile(p):
            shutil.copy(p, os.path.join(t, f))
            n += 1
    if not n:
        raise SystemExit("aucun programme d'essai dans %s (sh tools/qt/test.sh)" % TESTBIN)
    etc = os.path.join(TROOT, "etc")
    if a.theme:	# (--theme "Onyx Noir" : le thème du bureau, pour essayer la palette et le style sombres)
        th = os.path.join(etc, "theme.txt")
        s = open(th, encoding="utf-8").read()
        s = "\n".join("theme    = " + a.theme if l.startswith("theme ") else l for l in s.split("\n"))
        open(th, "w", encoding="utf-8", newline="\n").write(s)
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "/sys/svc/telnetd" not in s:
        s += "# essais de tools/qt/pctest.py : telnet\n/sys/svc/telnetd\n"
        open(sv, "w", encoding="utf-8", newline="\n").write(s)
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "mkkorindisk.py"), IMG,
                           "--layout", "zbook", "--korin-mb", str(a.mb), "--sysroot", TROOT,
                           "--cfg", "default = korin\ntimeout = 0"])
    print(IMG, os.path.getsize(IMG))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["prep", "run"])
    ap.add_argument("cmd", nargs="*")
    ap.add_argument("--mb", type=int, default=600)
    ap.add_argument("--theme", default="")
    ap.add_argument("--bin", default="")
    ap.add_argument("--port", type=int, default=2398)
    ap.add_argument("--hostfwd", action="append", default=[])
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot", type=float, default=120)
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
