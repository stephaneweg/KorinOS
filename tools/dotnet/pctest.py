""".NET sur Korin PC x86-64 sous QEMU q35 (docs/DOTNET.md, « Le PC ») : les mêmes commandes que
tools/python/pctest.py, sur un autre arbre et un autre disque (build/pc/dntest).

  python -I tools/dotnet/pctest.py prep [--mb 900] [--with-python]
      l'arbre d'essai build/pc/dntest/root : une COPIE de build/pc/sysroot (jamais modifié), sans python/, .vibe/
      ni gcc/ (sauf --with-python, --with-gcc), puis tools/pc/sysroot_dotnet.sh ; les essais de libposix
      (build/pc/posixtest) ; telnetd et le mot de passe d'essai « essai » ; les essais de .NET dans HOME:/dntest
      (check.dll et les sources de tools/dotnet) ; puis le disque build/pc/dntest/disk.img
  python -I tools/dotnet/pctest.py run [--port 2395] [--smp 4] [--mem 4G] "commande"...
      comme pctest.py run (« @600 commande », « shot:NOM », « sleep:S ») ; journal : build/pc/dntest/debug.log.
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = os.path.join(ROOT, "build", "pc")
W = os.path.join(PC, "dntest")
TROOT = os.path.join(W, "root")
IMG = os.path.join(W, "disk.img")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def prep(a):
    src = os.path.join(PC, "sysroot")
    if not os.path.isdir(src):
        raise SystemExit("absent : build/pc/sysroot (wsl sh tools/pc/sysroot.sh)")
    shutil.rmtree(TROOT, ignore_errors=True)
    os.makedirs(W, exist_ok=True)
    skip = set()
    if not a.with_python:
        skip |= {"python", ".vibe"}
    if not a.with_gcc:
        skip.add("gcc")
    shutil.copytree(src, TROOT, ignore=lambda d, names: [n for n in names if os.path.samefile(d, src) and n in skip])
    subprocess.check_call(["wsl", "sh", "-c", "TESTS=1 sh tools/pc/sysroot_dotnet.sh build/pc/dntest/root"], cwd=ROOT)
    px = os.path.join(PC, "posixtest")
    if os.path.isdir(px):
        for f in os.listdir(px):
            if os.path.isfile(os.path.join(px, f)):
                shutil.copy(os.path.join(px, f), os.path.join(TROOT, "bin", f))
    sv = os.path.join(ROOT, "build", "svc", "x86_64")
    if os.path.isdir(sv):
        for f in os.listdir(sv):
            if os.path.isfile(os.path.join(sv, f)):
                shutil.copy(os.path.join(sv, f), os.path.join(TROOT, "sys", "svc", f))
    if os.path.isfile(os.path.join(PC, "bash", "bash")):
        shutil.copy(os.path.join(PC, "bash", "bash"), os.path.join(TROOT, "bin", "bash"))
    etc = os.path.join(TROOT, "etc")
    shutil.copy(os.path.join(ROOT, "skel", "sd0", "etc", "paths.txt"), os.path.join(etc, "paths.txt"))
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    s = open(os.path.join(etc, "services.txt"), encoding="utf-8").read()
    if "/sys/svc/telnetd" not in s:
        s += "# essais de tools/dotnet/pctest.py : telnet\n/sys/svc/telnetd\n"
        open(os.path.join(etc, "services.txt"), "w", encoding="utf-8", newline="\n").write(s)
    # les essais : check.dll (framework-dependent), les sources de cshello, csbuild/test/multi... (copiés par stage.sh
    # dans dotnet/test) ; build/pc/dntest/extra/ (s'il existe) va tel quel dans home/dntest
    h = os.path.join(TROOT, "home", "dntest")
    os.makedirs(h, exist_ok=True)
    ex = os.path.join(W, "extra")
    if os.path.isdir(ex):
        shutil.copytree(ex, h, dirs_exist_ok=True)
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "mkkorindisk.py"), IMG,
                           "--layout", "zbook", "--korin-mb", str(a.mb), "--sysroot", TROOT] + (["--esp", a.esp] if a.esp else []) + [
                           "--cfg", "default = korin\ntimeout = 0"])
    print(IMG, os.path.getsize(IMG))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["prep", "run"])
    ap.add_argument("cmd", nargs="*")
    ap.add_argument("--mb", type=int, default=900)
    ap.add_argument("--with-python", action="store_true")
    ap.add_argument("--with-gcc", action="store_true")
    ap.add_argument("--esp", default="", help="noyau et initrd d'un autre dossier que build/pc/esp (tools/pc/build.sh avec PC_OUT)")
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
        return
    pt = load("pctest", os.path.join(ROOT, "tools", "python", "pctest.py"))
    pt.W, pt.IMG = W, IMG
    sys.exit(pt.run(a))


if __name__ == "__main__":
    main()
