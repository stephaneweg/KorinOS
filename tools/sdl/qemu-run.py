"""qemu-run : l'essai de SDL sous QEMU, par telnet, sur la carte de testimage.py, avec des captures.
    python -I tools/sdl/qemu-run.py [--script fichier] "sdltest --sw" "sdl2test --gl"
--script : un script de qemu_drive.py a jouer tel quel. Ports propres a cet essai (4491, VNC 91, telnet 2391).
Le noyau vient de build/sdl/kernel8.img, ou du depot principal.
"""
import os
import shutil
import subprocess
import sys

if not os.path.isdir("build/modules"):
    shutil.copytree(os.path.join(os.path.dirname(__file__), "..", "..", "build", "modules"), "build/modules")
kernel = "build/sdl/kernel8.img"
if not os.path.exists(kernel):
    shutil.copy(os.path.join(os.path.dirname(__file__), "..", "..", "build", "kernel8.img"), kernel)
args = sys.argv[1:]
if args and args[0] == "--script":
    run = args[1]
else:
    run = "build/sdl/run.txt"
    s = ["wait 25"]
    for c in args:
        s.append('run python -I tools/armpi_remote.py telnet 127.0.0.1:2391 "@280 %s"' % c)
    open(run, "w", encoding="utf-8").write("\n".join(s) + "\n")
r = subprocess.run([sys.executable, "-I", "tools/qemu_drive.py", run, "--port", "4491", "--vnc", "91",
                    "--kernel", kernel, "--sd", "build/sdl/sd.img", "--uart", "build/sdl/uart.log", "--fwd", "2391:23,2191:21"],
                   capture_output=True, text=True, errors="replace")
open("build/sdl/run.out", "w", encoding="utf-8").write(r.stdout)
print(r.stdout[-30000:])
print(r.stderr[-1500:])
