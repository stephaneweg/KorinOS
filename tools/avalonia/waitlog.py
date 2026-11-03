"""waitlog : attend qu'une ligne paraisse dans /tmp/av.log sous QEMU, par telnet (etape until: de desk.py).
    python -I tools/avalonia/waitlog.py mot [secondes]
"""
import subprocess
import sys
import time

word = sys.argv[1]
end = time.time() + (float(sys.argv[2]) if len(sys.argv) > 2 else 300)
while time.time() < end:
    r = subprocess.run([sys.executable, "-I", "tools/armpi_remote.py", "telnet", "127.0.0.1:2371", "cat RAM0:/tmp/av.log"],
                       capture_output=True, text=True, errors="replace")
    if word in r.stdout:
        print("vu :", word)
        sys.exit(0)
    time.sleep(4)
print("pas vu :", word)
