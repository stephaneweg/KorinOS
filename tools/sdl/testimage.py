"""testimage : une carte d'essai pour QEMU, build/sdl/sd.img : sdroot, build/sdl/sdroot par-dessus, rien au
demarrage. Ni sdroot ni build/sd.img ne changent. Pris dans le depot principal si ce worktree n'a pas de build :
le noyau et ce que build.ps1 met dans sdroot/.
    python -I tools/sdl/testimage.py [--small] (depuis la racine du worktree ; --small : sans les donnees des jeux)
"""
import os
import shutil
import subprocess
import sys

root = os.getcwd()
main = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
src = os.path.join(root, "build", "sdroot")
if not os.path.exists(os.path.join(src, "sys", "svc")):
    src = os.path.join(main, "build", "sdroot")
t = os.path.join(root, "build", "sdl", "testroot")
shutil.rmtree(t, ignore_errors=True)
shutil.copytree(src, t, ignore=lambda d, n: [x for x in n if d == src and x in ("dotnet", "webkit", "gcc", "src", "t", "test.img", "test_exfat.img", "docs")])
shutil.copytree(os.path.join(root, "build", "sdl", "sdroot"), t, dirs_exist_ok=True,
                ignore=lambda d, n: ["data"] if "--small" in sys.argv and d.endswith(".app") else [])
for f, text in (("preload.txt", "# vide : image d'essai\n"), ("startup.txt", "# vide : image d'essai\n")):
    open(os.path.join(t, "etc", f), "w", encoding="ascii").write(text)
ex = os.path.join(root, "build", "exfat_root")
if not os.path.exists(ex):
    ex = os.path.join(main, "build", "exfat_root")
subprocess.check_call([sys.executable, "-I", "tools/mkdisk.py", "build/sdl/sd.img", "--fat32", t, "--exfat", ex],
                      stdout=subprocess.DEVNULL)
print("build/sdl/sd.img", os.path.getsize("build/sdl/sd.img"))
