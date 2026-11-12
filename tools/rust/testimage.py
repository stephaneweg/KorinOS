"""testimage : la carte d'essai de Rust pour QEMU, build/rust/sd.img (docs/RUST.md) : build/sdroot
(fait par build.ps1), un mot de passe d'essai pour telnetd et ftpd (« essai », que
tools/armpi_remote.py lit dans ARMPI_REMOTE_PW), et les essais construits par tools/rust/build.sh
(build/rust/tests : korintest, rdlhost, librdl.so, les essais de std) dans SD0:/home/rust.
Ni build/sdroot ni build/sd.img ne changent.
    python -I tools/rust/testimage.py          (depuis la racine du worktree, après build.ps1)
"""
import os
import shutil
import subprocess
import sys

root = os.getcwd()
t = os.path.join(root, "build", "rust", "testroot")
shutil.rmtree(t, ignore_errors=True)
shutil.copytree(os.path.join(root, "build", "sdroot"), t)
open(os.path.join(t, "etc", "remote.txt"), "w", encoding="ascii").write("password = essai\n")
src = os.path.join(root, "build", "rust", "tests")
if not os.path.isdir(src):
    sys.exit("build/rust/tests absent : lancer d'abord sh tools/rust/build.sh dans WSL")
shutil.copytree(src, os.path.join(t, "home", "rust"), dirs_exist_ok=True)
subprocess.check_call([sys.executable, "-I", "tools/mkdisk.py", "build/rust/sd.img", "--fat32", t,
                       "--exfat", "build/exfat_root"], stdout=subprocess.DEVNULL)
print("build/rust/sd.img", os.path.getsize("build/rust/sd.img"))
