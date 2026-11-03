"""pi-package : ce qu'il faut pour essayer sur le Pi, dans build/mesa/pi/, avec les CRC a verifier (docs/MESA.md).
    python -I tools/mesa/pi-package.py          (depuis la racine du worktree)
"""
import os
import shutil
import subprocess
import zlib

o = "build/mesa/pi"
shutil.rmtree(o, ignore_errors=True)
# le noyau d'une carte est assemble pour son ecran (build.ps1 sd), pas celui de build/ : 800 x 600, le PocketTerm
subprocess.check_call([os.path.abspath("tools/fasmarm/fasmarm.exe"), "-d", "ECRAN_W=800", "-d", "ECRAN_H=600", "kernel.asm",
                       os.path.abspath("build/mesa/kernel-carte.img")], cwd="src/kernel", stdout=subprocess.DEVNULL)
for src, dst in (("build/mesa/kernel-carte.img", "kernel8.img"), ("build/mesa/v3d.elf", "sys/svc/v3d"),
                 ("build/mesa/sdroot/bin/gltest", "bin/gltest"), ("build/mesa/sdroot/bin/gltri", "bin/gltri"),
                 ("build/mesa/sdroot/bin/skgltest", "bin/skgltest")):
    p = os.path.join(o, dst)
    os.makedirs(os.path.dirname(p) or o, exist_ok=True)
    shutil.copy(src, p)
    d = open(src, "rb").read()
    print("%08X %9d %s" % (zlib.crc32(d), len(d), dst))
