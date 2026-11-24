"""pctest : Blender sur Korin PC x86-64 sous QEMU q35 (docs/etude/BLENDER.md, jalon 2). Reprend tools/cao/pctest.py
(disque NVMe d'essai, telnet, le lancement de tools/python/pctest.py), dans build/pc/blendertest.

  python3 -I tools/blender/pctest.py prep [--mb 1500] [--tree ~/korin-blender/x86_64/install-headless]
      l'arbre d'essai ~/korin-blender/blendertest/root : une COPIE de build/pc/sysroot (jamais modifié), l'arbre portable
      de Blender dans /apps/blender (blender et 5.2/), les scripts d'essai de tools/blender/test dans /home/blender,
      telnetd et le mot de passe d'essai « essai » ; puis le disque build/pc/blendertest/disk.img
  python3 -I tools/blender/pctest.py run [--smp 4] [--mem 4G] "/apps/blender/blender -b --version" ...
      démarre QEMU (l'image en snapshot), lance les commandes par telnet, affiche leur sortie et leur code

À lancer dans WSL après `. ~/korin-cloud/env.sh` (fasm pour Linux, et OVMF avec le shell UEFI : les disques de
mkkorindisk.py démarrent par startup.nsh ; docs/agents/CLOUD.md) ; `wsl -u root` et `--qemu "-accel kvm"` pour KVM.
"""
import argparse
import importlib.util
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PC = os.path.join(ROOT, "build", "pc")
# dans WSL même (~/korin-blender/blendertest) : plus rapide que /mnt/c, et sans la limite de 260 caractères des
# chemins de Windows (les site-packages de /devtools/python la dépassent)
W = os.path.join(os.environ.get("KORIN_BLENDER", os.path.expanduser("~/korin-blender")), "blendertest")
TROOT = os.path.join(W, "root")
IMG = os.path.join(W, "disk.img")
HERE = os.path.dirname(os.path.abspath(__file__))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def prep(a):
    if not os.path.isdir(os.path.join(PC, "sysroot")):
        raise SystemExit("absent : build/pc/sysroot (sh tools/pc/build.sh puis sh tools/pc/sysroot.sh)")
    tree = os.path.expanduser(a.tree)
    if not os.path.isfile(os.path.join(tree, "blender")):
        raise SystemExit("absent : %s/blender (sh tools/blender/build.sh)" % tree)
    shutil.rmtree(TROOT, ignore_errors=True)
    os.makedirs(W, exist_ok=True)
    # sans /devtools (GCC, .NET, Python 3.14… : inutiles à blender -b, et plus d'un Go)
    shutil.copytree(os.path.join(PC, "sysroot"), TROOT,
                    ignore=lambda d, names: ["devtools"] if os.path.samefile(d, os.path.join(PC, "sysroot")) else [])
    shutil.copytree(tree, os.path.join(TROOT, "apps", "blender"))
    t = os.path.join(TROOT, "home", "blender")
    shutil.copytree(os.path.join(HERE, "test"), t)
    if a.lvp:
        # Vulkan sur le processeur (QEMU n'a pas de GPU Intel) : le kit de lavapipe (tools/mesa/build-lvp.sh) à la
        # place de celui d'anv, et son vktest ; un programme Vulkan doit alors être lié à build/pc/mesa-lvp
        lv = os.path.join(PC, "mesa-lvp", "sdroot")
        shutil.copy(os.path.join(lv, "lib", "vkkit.so"), os.path.join(TROOT, "lib", "vkkit.so"))
        shutil.copy(os.path.join(lv, "bin", "vktest"), os.path.join(TROOT, "bin", "vktest"))
    etc = os.path.join(TROOT, "etc")
    open(os.path.join(etc, "remote.txt"), "w", encoding="ascii").write("password = essai\n")
    sv = os.path.join(etc, "services.txt")
    s = open(sv, encoding="utf-8").read()
    if "/sys/svc/telnetd" not in s:
        s += "# essais de tools/blender/pctest.py : telnet\n/sys/svc/telnetd\n"
        open(sv, "w", encoding="utf-8", newline="\n").write(s)
    subprocess.check_call([sys.executable, "-I", os.path.join(ROOT, "tools", "pc", "mkkorindisk.py"), IMG,
                           "--layout", "zbook", "--korin-mb", str(a.mb), "--sysroot", TROOT,
                           "--cfg", "default = korin\ntimeout = 0"])
    print(IMG, os.path.getsize(IMG))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["prep", "run"])
    ap.add_argument("cmd", nargs="*")
    ap.add_argument("--mb", type=int, default=1500)
    ap.add_argument("--tree", default="~/korin-blender/x86_64/install-headless")
    ap.add_argument("--lvp", action="store_true", help="le kit de lavapipe (Vulkan sur le processeur) et son vktest")
    ap.add_argument("--bin", default="")
    ap.add_argument("--port", type=int, default=2396)
    ap.add_argument("--hostfwd", action="append", default=[])
    ap.add_argument("--smp", type=int, default=4)
    ap.add_argument("--mem", default="4G")
    ap.add_argument("--boot", type=float, default=90)
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
