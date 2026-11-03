"""testimage : une carte d'essai pour QEMU, build/mesa/sd.img : build/sdroot, build/mesa/sdroot par-dessus, rien au
demarrage. Ni build/sdroot ni build/sd.img ne changent.
    python -I tools/mesa/testimage.py          (depuis la racine du worktree, apres build-armgl.sh)
"""
import os
import shutil
import subprocess
import sys

root = os.getcwd()
t = os.path.join(root, "build", "mesa", "testroot")
shutil.rmtree(t, ignore_errors=True)
shutil.copytree(os.path.join(root, "build", "sdroot"), t)
shutil.copytree(os.path.join(root, "build", "mesa", "sdroot"), t, dirs_exist_ok=True)
v3d = os.path.join(root, "build", "mesa", "v3d.elf")
if os.path.exists(v3d):
    shutil.copy(v3d, os.path.join(t, "sys", "svc", "v3d"))
for f, text in (("preload.txt", "# vide : image d'essai\n"), ("startup.txt", "# vide : image d'essai\n")):
    open(os.path.join(t, "etc", f), "w", encoding="ascii").write(text)
subprocess.check_call([sys.executable, "-I", "tools/mkdisk.py", "build/mesa/sd.img", "--fat32", t, "--exfat", "build/exfat_root"],
                      stdout=subprocess.DEVNULL)
print("build/mesa/sd.img", os.path.getsize("build/mesa/sd.img"))
