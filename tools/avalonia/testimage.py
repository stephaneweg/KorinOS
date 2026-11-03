"""testimage : une carte d'essai pour QEMU, build/avalonia/sd.img : build/sdroot, build/avalonia/sdroot par-dessus,
preload.txt vide, rien au demarrage. Ni build/sdroot ni build/sd.img ne changent.
    python -I tools/avalonia/testimage.py          (depuis la racine du worktree, apres build.ps1)
"""
import os
import shutil
import subprocess
import sys

root = os.getcwd()
t = os.path.join(root, "build", "avalonia", "testroot")
shutil.rmtree(t, ignore_errors=True)
shutil.copytree(os.path.join(root, "build", "sdroot"), t)
shutil.copytree(os.path.join(root, "build", "avalonia", "sdroot"), t, dirs_exist_ok=True)
for f, text in (("preload.txt", "# vide : image d'essai\n"), ("startup.txt", "# vide : image d'essai\n")):
    open(os.path.join(t, "etc", f), "w", encoding="ascii").write(text)
for f in sys.argv[1:]:                  # chemin=texte : un fichier de plus
    p, text = f.split("=", 1)
    open(os.path.join(t, p), "w", encoding="utf-8").write(text + "\n")
subprocess.check_call([sys.executable, "-I", "tools/mkdisk.py", "build/avalonia/sd.img", "--fat32", t, "--exfat", "build/exfat_root"],
                      stdout=subprocess.DEVNULL)
print("build/avalonia/sd.img", os.path.getsize("build/avalonia/sd.img"))
