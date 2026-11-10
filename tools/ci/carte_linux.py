"""Construit sous Linux (ou WSL) ce qu'il faut pour demarrer Korin OS dans QEMU, sans PowerShell :
la partie en assembleur de build.ps1, posee sur la carte deja construite du depot (sdcard/).

  1. assemble src/{apps,bin,sys,kits,svc}/<nom>/<nom>.asm -> build/{apps,lib,svc} (comme build.ps1) ;
  2. les modules de demarrage src/svc/modules/*.asm -> build/modules/*.mod ;
  3. le noyau -> build/kernel8.img (1024x768, comme pour QEMU) ;
  4. build/sdroot : une copie de sdcard/, les programmes neufs par-dessus, etc/init.txt, les
     programmes C de prebuilt/ (posixtest, posixtest-cxx, ttytest), git s'il est construit
     (build/git/sdroot, tools/git/build.sh) ;
  5. build/sd.img par tools/mkdisk.py (FAT32 + exFAT de skel/sd1, comme build.ps1).

Ce qui se construit dans WSL (WebKit, GCC, .NET...) n'est pas refait : la carte du depot l'a deja.

Usage : python -I tools/ci/carte_linux.py [--fasmarm chemin] [--seul kernel,terminal,...]
FASMARM : --fasmarm, sinon la variable FASMARM, sinon `fasmarm` dans le PATH (un FASMARM pour
Linux se fait depuis tools/fasmarm/source/linux/x64 avec le fasm de ref/fasm17330, voir docs/CI.md).
Puis : ARMPI_QEMU=/chemin/qemu-system-aarch64 python -I tools/ci/boot_test.py
"""
import glob
import os
import shutil
import subprocess
import sys


def arg(nom, defaut):
    if nom in sys.argv:
        return sys.argv[sys.argv.index(nom) + 1]
    return defaut


ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
FASM = arg("--fasmarm", os.environ.get("FASMARM", "fasmarm"))
SEUL = set(filter(None, arg("--seul", "").split(",")))
B = os.path.join(ROOT, "build")


def fasm(cwd, src, out, *defs):
    cmd = [FASM, "-m", "262144"]
    for d in defs:
        cmd += ["-d", d]
    r = subprocess.run(cmd + [src, out], cwd=cwd, capture_output=True, text=True, errors="replace")
    if r.returncode:
        sys.exit(f"echec : {os.path.relpath(os.path.join(cwd, src), ROOT)}\n{r.stdout}{r.stderr}")


def assembler(cat, dst, ext):
    os.makedirs(dst, exist_ok=True)
    noms = []
    for d in sorted(glob.glob(os.path.join(ROOT, "src", cat, "*"))):
        n = os.path.basename(d)
        if not os.path.isfile(os.path.join(d, n + ".asm")):
            continue
        if not SEUL or n in SEUL:
            fasm(d, n + ".asm", os.path.join(dst, n + ext))
        noms.append(n)
    return noms


def copie(src, dst):
    if os.path.exists(src):
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(src, dst)


def main():
    apps, libs, svc = os.path.join(B, "apps"), os.path.join(B, "lib"), os.path.join(B, "svc")
    p_apps = assembler("apps", apps, ".elf")
    p_bin = assembler("bin", apps, ".elf")
    p_sys = assembler("sys", apps, ".elf")
    p_kits = assembler("kits", libs, ".so")
    p_svc = assembler("svc", svc, ".elf")

    mods = os.path.join(B, "modules")
    os.makedirs(mods, exist_ok=True)
    md = os.path.join(ROOT, "src", "svc", "modules")
    for f in sorted(glob.glob(os.path.join(md, "*.asm"))):
        fasm(md, os.path.basename(f), os.path.join(mods, os.path.basename(f)[:-4] + ".mod"))

    kd = os.path.join(ROOT, "src", "kernel")
    fasm(kd, "kernel.asm", os.path.join(B, "kernel8.img"))
    fasm(kd, "kernel.asm", os.path.join(B, "kernel_2712.img"), "ARCH=pi5")

    sd = os.path.join(B, "sdroot")
    shutil.rmtree(sd, ignore_errors=True)
    shutil.copytree(os.path.join(ROOT, "sdcard"), sd)
    for n in p_sys:
        copie(os.path.join(apps, n + ".elf"), os.path.join(sd, "sys", n))
    for n in p_bin:
        copie(os.path.join(apps, n + ".elf"), os.path.join(sd, "bin", n))
    for n in p_apps:
        dst = os.path.join(sd, "apps", n + ".app")
        copie(os.path.join(apps, n + ".elf"), os.path.join(dst, "main"))
        for extra in ("app.txt", "help_fr.md", "help_en.md"):
            copie(os.path.join(ROOT, "src", "apps", n, extra), os.path.join(dst, extra))
    for n in p_svc:
        copie(os.path.join(svc, n + ".elf"), os.path.join(sd, "sys", "svc", n))
    for n in p_kits:
        if n != "tlskit":
            copie(os.path.join(libs, n + ".so"), os.path.join(sd, "lib", n + ".so"))
    copie(os.path.join(md, "init.txt"), os.path.join(sd, "etc", "init.txt"))
    for n in ("posixtest", "posixtest-cxx", "ttytest"):     # programmes C precompiles (build.ps1)
        copie(os.path.join(ROOT, "prebuilt", n), os.path.join(sd, "bin", n))
    g = os.path.join(B, "git", "sdroot")                    # git (tools/git/build.sh, docs/GIT.md)
    if os.path.isdir(g):
        shutil.copytree(g, sd, dirs_exist_ok=True)

    py = [sys.executable, "-I"]
    ex = os.path.join(B, "exfat_root")
    subprocess.run(py + [os.path.join(ROOT, "tools", "exfatdata.py"), os.path.join(ROOT, "skel", "sd1"), ex], check=True)
    subprocess.run(py + [os.path.join(ROOT, "tools", "mkdisk.py"), os.path.join(B, "sd.img"),
                         "--fat32", sd, "--exfat", ex], check=True, stdout=subprocess.DEVNULL)
    print("build/kernel8.img, build/modules, build/sd.img : prets")


main()
