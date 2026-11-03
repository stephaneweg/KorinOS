"""qemu-run : lance des commandes dans ARMPI sous QEMU, par telnet, sur la carte de testimage.py.
    python -I tools/mesa/qemu-run.py "gltest --dry" "gltest"
Ports propres a cet essai (4481, VNC 81, telnet 2381).
"""
import subprocess
import sys

s = ["wait 25"]
for c in sys.argv[1:]:
    s.append('run python -I tools/armpi_remote.py telnet 127.0.0.1:2381 "@280 %s"' % c)
open("build/mesa/run.txt", "w", encoding="utf-8").write("\n".join(s) + "\n")
r = subprocess.run([sys.executable, "-I", "tools/qemu_drive.py", "build/mesa/run.txt", "--port", "4481", "--vnc", "81",
                    "--kernel", "build/mesa/kernel8.img", "--sd", "build/mesa/sd.img", "--uart", "build/mesa/uart.log", "--fwd", "2381:23,2181:21"],
                   capture_output=True, text=True, errors="replace")
open("build/mesa/run.out", "w", encoding="utf-8").write(r.stdout)
print(r.stdout[-30000:])
print(r.stderr[-1500:])
