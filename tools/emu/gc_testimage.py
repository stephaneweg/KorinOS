"""gc_testimage : une carte d'essai jetable pour gcemu sous QEMU, build/emu/gc_sd.img (4 Go, fichier creux) : build/sdroot,
gcemu.app, un disque en SD1:/roms et ses arguments au demarrage. Jamais la carte du Pi (docs/EMULATEURS.md).
    python -I tools/emu/gc_testimage.py <disque.iso> ["arguments de gcemu"]
Le disque (jeu de l'utilisateur) ne vit que dans cette image, qui reste dans build/.
"""
import os
import shutil
import subprocess
import sys

root = os.getcwd()
b = os.path.join(root, "build", "emu")
t = os.path.join(b, "gc_root")
x = os.path.join(b, "gc_exfat")
iso = sys.argv[1]
args = sys.argv[2] if len(sys.argv) > 2 else ""
shutil.rmtree(t, ignore_errors=True)
shutil.copytree(os.path.join(root, "build", "sdroot"), t)
shutil.copytree(os.path.join(b, "sdroot", "apps", "gcemu.app"), os.path.join(t, "apps", "gcemu.app"), dirs_exist_ok=True)
os.makedirs(os.path.join(x, "roms"), exist_ok=True)
name = os.path.basename(iso)
d = os.path.join(x, "roms", name)
for f in os.listdir(os.path.join(x, "roms")):
    if f != name:
        os.remove(os.path.join(x, "roms", f))
if not os.path.exists(d):
    try:
        os.link(iso, d)
    except OSError:
        shutil.copy(iso, d)
open(os.path.join(t, "etc", "preload.txt"), "w", encoding="ascii").write("# vide : image d'essai\n")
open(os.path.join(t, "etc", "startup.txt"), "w", encoding="ascii").write("SD0:/apps/gcemu.app/main %s SD1:/roms/%s\n" % (args, name))
os.makedirs(os.path.join(t, "gcgl"), exist_ok=True)
for f in os.listdir(b):
    if f.endswith(".gxs") or f == "gcgl":
        shutil.copy(os.path.join(b, f), os.path.join(t, "gcgl", f))
# (GC_LIB=dossier : les kits a essayer, ceux d'une autre carte par exemple)
if os.environ.get("GC_LIB"):
    for f in os.listdir(os.environ["GC_LIB"]):
        shutil.copy(os.path.join(os.environ["GC_LIB"], f), os.path.join(t, "lib", f))
src = open(os.path.join(root, "tools", "mkdisk.py"), encoding="utf-8").read()
src = src.replace("DISK_SIZE = 1024 * 1024 * 1024", "DISK_SIZE = 4 * 1024 * 1024 * 1024")
w = os.path.join(root, "tools", "mkdisk4.tmp.py")
open(w, "w", encoding="utf-8").write(src)
subprocess.check_call([sys.executable, "-I", w, os.path.join(b, "gc_sd.img"), "--fat32", t, "--exfat", x], stdout=subprocess.DEVNULL)
os.remove(w)
print("build/emu/gc_sd.img", os.path.getsize(os.path.join(b, "gc_sd.img")))
