"""imports.py -- ce qu'un ELF de Linux x86-64 importe, et ce que la glibc de compatibilité de liblinux
(src/lib/linux : les tables LX_F/LX_W, F/W) sert déjà ; sans rien exécuter, sur la machine de construction
(docs/etude/LINUX-HOST.md). Lit le binaire avec readelf.

  python3 -I tools/linux/imports.py BINAIRE [--missing] [--by-lib]
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def served():
    names = set()
    d = os.path.join(ROOT, "src", "lib", "linux")
    for f in os.listdir(d):
        if not f.endswith(".c"):
            continue
        t = open(os.path.join(d, f), encoding="utf-8").read()
        names.update(re.findall(r'\bLX_W\s*\(\s*"([^"]+)"', t))
        names.update(re.findall(r'\bLX_F\s*\(\s*(\w+)\s*\)', t))
        names.update(re.findall(r'(?<![\w])W\s*\(\s*"([^"]+)"', t))
        names.update(re.findall(r'(?<![\w])F\s*\(\s*(\w+)\s*\)', t))
    names.discard("n")
    return names


def imports(path):
    out = subprocess.run(["readelf", "-W", "--dyn-syms", path], capture_output=True, text=True, check=True).stdout
    res = []
    for line in out.splitlines():
        p = line.split()
        if len(p) >= 8 and p[6] == "UND" and p[7]:
            name, _, ver = p[7].partition("@")
            res.append((name, ver.lstrip("@"), p[4]))
    return res


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    have = served()
    imp = imports(sys.argv[1])
    miss = [(n, v, b) for n, v, b in imp if n not in have and b != "WEAK"]
    if "--missing" in sys.argv:
        by = {}
        for n, v, b in miss:
            by.setdefault(v.split("_")[0] if v else "?", []).append(n)
        for n, v, b in sorted(miss, key=lambda x: (x[1], x[0])):
            print("%-36s %s" % (n, v))
    print("%s : %d imports, %d servis, %d faibles absents, %d absents" % (
        os.path.basename(sys.argv[1]), len(imp), sum(1 for n, v, b in imp if n in have),
        sum(1 for n, v, b in imp if n not in have and b == "WEAK"), len(miss)))


if __name__ == "__main__":
    main()
