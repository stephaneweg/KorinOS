"""Refait FASMARM depuis ses sources, sous Windows, dans build/fasmarm (docs/CONSTRUIRE.md) :

    python tools/fasmarm/rebuild.py        (sans -I : PYTHONHASHSEED fixe l'ordre des tables de x86toa64.py)

  windows/fasmarm.exe   fasm 1.73.30 (ref/fasm17330) + l'ajout ARM (tools/fasmarm/source), assemble par FASM.EXE
  armpi/fasmarm.asm     le meme code traduit en AArch64 par tools/x86toa64.py
  armpi/fasmarm.elf     assemble par le fasmarm.exe qu'on vient de faire : SD0:/bin/fasmarm

Compare le tout a tools/fasmarm/fasmarm.exe, a src/bin/fasmarm/fasmarm.asm et au programme de la carte.
"""
import filecmp, glob, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
F = os.path.join(ROOT, "ref", "fasm17330", "w")
A = os.path.join(ROOT, "tools", "fasmarm", "source")
OUT = os.path.join(ROOT, "build", "fasmarm")


def run(args, cwd):
    r = subprocess.run(args, cwd=cwd, capture_output=True, text=True, errors="replace")
    print("\n".join(r.stdout.strip().splitlines()[-2:]))
    if r.returncode:
        sys.exit(f"echec : {args[0]}\n{r.stderr}")


def copie(motif, dst):
    os.makedirs(dst, exist_ok=True)
    for f in glob.glob(motif):
        if os.path.isfile(f):
            shutil.copy(f, dst)


def compare(nom, a, b):
    if not os.path.exists(b):
        print(f"{nom} : pas de reference ({b})")
        return True
    same = filecmp.cmp(a, b, shallow=False)
    print(f"{nom} : {'identique' if same else 'DIFFERENT'} ({os.path.getsize(a)} / {os.path.getsize(b)} octets)")
    return same


shutil.rmtree(OUT, ignore_errors=True)
# Windows : l'arborescence de fasm, avec les fichiers ARM par-dessus
win = os.path.join(OUT, "windows")
copie(os.path.join(F, "SOURCE", "*.INC"), win)
copie(os.path.join(A, "*.inc"), win)
copie(os.path.join(F, "SOURCE", "WIN32", "*"), os.path.join(win, "win32"))
copie(os.path.join(A, "win32", "*"), os.path.join(win, "win32"))
env_inc = os.path.join(F, "INCLUDE")
os.environ["INCLUDE"] = env_inc
run([os.path.join(F, "FASM.EXE"), "fasmarm.asm", os.path.join(win, "fasmarm.exe")], os.path.join(win, "win32"))
ok = compare("fasmarm.exe", os.path.join(win, "fasmarm.exe"), os.path.join(ROOT, "tools", "fasmarm", "fasmarm.exe"))

# Korin OS : le meme code traduit
arm = os.path.join(OUT, "armpi")
s = os.path.join(OUT, "src")
copie(os.path.join(F, "SOURCE", "*.INC"), s)
copie(os.path.join(A, "*.inc"), s)
copie(os.path.join(F, "SOURCE", "LINUX", "SYSTEM.INC"), s)
os.makedirs(arm)
x = os.path.join(ROOT, "src", "bin", "fasmarm", "x86")
run([sys.executable, os.path.join(ROOT, "tools", "x86toa64.py"), "armpi_main.asm", os.path.join(arm, "fasmarm.asm"), s], x)
run([os.path.join(win, "fasmarm.exe"), "fasmarm.asm", "fasmarm.elf"], arm)
ok &= compare("fasmarm.asm (generation)", os.path.join(arm, "fasmarm.asm"), os.path.join(ROOT, "src", "bin", "fasmarm", "fasmarm.asm"))
ok &= compare("fasmarm.elf", os.path.join(arm, "fasmarm.elf"), os.path.join(ROOT, "build", "apps", "fasmarm.elf"))
sys.exit(0 if ok else 1)
