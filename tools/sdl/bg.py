"""bg : lance une commande dans ARMPI par telnet sans attendre sa fin, pour qu'un script de qemu_drive.py agisse
pendant qu'elle tourne. La sortie va dans un fichier.
    python -I tools/sdl/bg.py sortie.txt 127.0.0.1:2391 "sdltest --events 30"
"""
import subprocess
import sys

out, host, cmd = sys.argv[1:4]
subprocess.Popen([sys.executable, "-I", "-u", "tools/armpi_remote.py", "telnet", host, "@560 " + cmd], stdin=subprocess.DEVNULL,
                 stdout=open(out, "w"), stderr=subprocess.STDOUT, creationflags=0x00000008)
