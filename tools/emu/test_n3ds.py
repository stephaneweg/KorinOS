"""test_n3ds : n3dsemu sous QEMU, sur une carte d'essai jetable (build/n3ds/sd.img) : build/sdroot, build/emu/sdroot par-dessus, et
la ROM donnee en copie dans SD0:/home/roms. La carte du Pi n'est jamais touchee ; rien de tout cela n'entre au depot.
    python -I tools/emu/test_n3ds.py <rom ou .elf> <secondes> <fichier.png> [--soft] [--dry] [--image-only]
(depuis la racine du worktree, apres build.ps1 et tools/emu/build.sh n3dsemu). Ports propres : 4482, telnet 2382, VNC 82.
"""
import os
import shutil
import subprocess
import sys

root = os.getcwd()
args = [a for a in sys.argv[1:] if not a.startswith("--")]
opts = [a for a in sys.argv[1:] if a.startswith("--") and a != "--image-only"]
rom, seconds, shot = args[0], int(args[1]), args[2]
t = os.path.join(root, "build", "n3ds", "testroot")
shutil.rmtree(t, ignore_errors=True)
shutil.copytree(os.path.join(root, "build", "sdroot"), t)
shutil.copytree(os.path.join(root, "build", "emu", "sdroot"), t, dirs_exist_ok=True)
os.makedirs(os.path.join(t, "home", "roms"), exist_ok=True)
name = os.path.basename(rom)
shutil.copy(rom, os.path.join(t, "home", "roms", name))
shutil.copy(os.path.join(root, "build", "emu", "run_n3ds"), os.path.join(t, "bin", "run_n3ds"))
open(os.path.join(t, "etc", "preload.txt"), "w", encoding="ascii").write("# vide : image d'essai\n")
cmd = "SD0:/apps/n3dsemu.app/main %s SD0:/home/roms/%s" % (" ".join(opts), name)
open(os.path.join(t, "etc", "startup.txt"), "w", encoding="ascii").write(cmd + "\n")
subprocess.check_call([sys.executable, "-I", "tools/mkdisk.py", "build/n3ds/sd.img", "--fat32", t, "--exfat", "build/exfat_root"],
                      stdout=subprocess.DEVNULL)
print("build/n3ds/sd.img", os.path.getsize("build/n3ds/sd.img"))
if "--image-only" in sys.argv:
    sys.exit(0)
s = []
if "--bench" in sys.argv:				# (le banc sans fenetre : sa sortie par telnet, les erreurs de shaders avec)
    open(os.path.join(t, "etc", "startup.txt"), "w", encoding="ascii").write("# vide\n")
    subprocess.check_call([sys.executable, "-I", "tools/mkdisk.py", "build/n3ds/sd.img", "--fat32", t, "--exfat", "build/exfat_root"], stdout=subprocess.DEVNULL)
    s = ["wait 30", 'run python -I tools/armpi_remote.py telnet 127.0.0.1:2382 "@%d SD0:/bin/run_n3ds --dry SD0:/home/roms/%s %s"' % (seconds, name, shot)]
else:
    for k in range(1, 1 + seconds // 30):
        s += ["wait 30", "shot %s" % shot.replace(".png", "_%d.png" % (k * 30))]
open("build/n3ds/run.txt", "w", encoding="utf-8").write("\n".join(s) + "\n")
print("essai :", cmd)
r = subprocess.Popen([sys.executable, "-I", "tools/qemu_drive.py", "build/n3ds/run.txt", "--port", "4482", "--vnc", "82",
                      "--sd", "build/n3ds/sd.img", "--uart", "build/n3ds/uart.log", "--fwd", "2382:23,2122:21"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
out = r.communicate()[0]
open("build/n3ds/run.out", "w", encoding="utf-8").write(out)
print(out[-3000:])
