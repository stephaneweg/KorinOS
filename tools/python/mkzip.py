"""La bibliotheque standard de CPython dans un zip pour Korin (docs/PYTHON-PORT.md).

Usage : python3.14 -I tools/python/mkzip.py <lib/python3.14 installe> <python314.zip> [--deflate]

Chaque module y est en source (.py, pour les traces et inspect) et precompile (.pyc en
« unchecked-hash » : zipimport ne compare pas de date, la carte n'ecrit jamais de __pycache__).
Laisses de cote : les essais, l'IDLE, tkinter, ensurepip, site-packages, lib-dynload (les .so
restent des fichiers) et le _sysconfigdata de la machine de construction.
Le Python qui lance ce script doit etre un 3.14 (meme nombre magique que l'interpreteur).
"""
import importlib.util
import os
import sys
import zipfile

SKIP_DIRS = {"test", "idlelib", "tkinter", "turtledemo", "ensurepip", "site-packages",
             "lib-dynload", "__pycache__", "lib2to3"}
SKIP_FILES = {"turtle.py"}


def main():
    src, dst = sys.argv[1], sys.argv[2]
    method = zipfile.ZIP_DEFLATED if "--deflate" in sys.argv else zipfile.ZIP_STORED
    assert sys.version_info[:2] == (3, 14), "il faut un Python 3.14"
    n = 0
    tmp = dst + ".tmp"
    with zipfile.ZipFile(tmp, "w", method) as z:
        for d, dirs, files in os.walk(src):
            dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS and not x.startswith("config-"))
            rel = os.path.relpath(d, src).replace(os.sep, "/")
            rel = "" if rel == "." else rel + "/"
            for f in sorted(files):
                if not f.endswith(".py") or f in SKIP_FILES:
                    continue
                path = os.path.join(d, f)
                name = rel + f
                data = open(path, "rb").read()
                code = compile(data, name, "exec", dont_inherit=True, optimize=0)
                source_hash = importlib.util.source_hash(data)
                pyc = importlib._bootstrap_external._code_to_hash_pyc(code, source_hash, False)
                z.writestr(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data, method)
                z.writestr(zipfile.ZipInfo(name + "c", (2026, 1, 1, 0, 0, 0)), bytes(pyc), method)
                n += 1
    os.replace(tmp, dst)
    print("%s : %d modules, %d octets" % (dst, n, os.path.getsize(dst)))


if __name__ == "__main__":
    main()
