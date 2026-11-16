"""mktree.py DOSSIER ATTENDU [--no-links] : un arbre d'essai pour ext4r (run_read.sh), et la liste attendue de
readtest. --no-links : sans les liens symboliques (l'essai de ext4fs sous QEMU, qui les suit : installkey.py
--disktest ext4read).

Des tailles autour des blocs, un gros fichier, un fichier creux, un dossier de 3000 entrées (htree), des noms en
UTF-8 et avec des espaces, des liens symboliques courts (dans l'inode), longs (dans un bloc), vers un dossier,
absolus. Le contenu est fixé (graine) : deux appels donnent le même arbre."""
import os
import random
import sys
import zlib

root, expected = sys.argv[1], sys.argv[2]
links = "--no-links" not in sys.argv
rnd = random.Random(1234)


def write(rel, data):
    p = os.path.join(root, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as f:
        f.write(data)


for n in (0, 1, 59, 60, 61, 1023, 1024, 1025, 4095, 4096, 4097, 65536, 65537):
    write("tailles/f%d.bin" % n, bytes(rnd.getrandbits(8) for _ in range(n)))
write("gros/un_mo.bin", rnd.randbytes(1 << 20))
write("gros/sept_mo.bin", rnd.randbytes(7 << 20))
with open(os.path.join(root, "gros", "creux.bin"), "wb") as f:  # creux : des trous, des données au bout
    f.seek(20 << 20)
    f.write(b"fin du fichier creux\n")
for i in range(3000):
    write("beaucoup/fichier_%04d.txt" % i, ("contenu %d\n" % i).encode())
write("a/b/c/d/e/f/profond.txt", b"tout au fond\n")
write("Été à Noël/café crème.txt", "accents : é à ç ü\n".encode())
write("avec des espaces/un fichier.txt", b"espaces\n")
write("Casse/Fichier.TXT", b"casse\n")
if links:
    os.symlink("tailles/f4096.bin", os.path.join(root, "lien_court"))
    os.symlink("a/b/c/d/e/f/" + "../" * 6 + "a/b/c/d/e/f/profond.txt",
               os.path.join(root, "lien_long_qui_depasse_soixante_octets"))
    os.symlink("a/b/c", os.path.join(root, "lien_dossier"))
    os.symlink("/avec des espaces", os.path.join(root, "Casse", "lien_absolu"))

lines = []
for base, dirs, files in os.walk(root):
    rel = os.path.relpath(base, root).replace("\\", "/")
    rel = "" if rel == "." else rel + "/"
    for d in list(dirs):
        p = os.path.join(base, d)
        if os.path.islink(p):
            lines.append("L %s%s %s" % (rel, d, os.readlink(p)))
            dirs.remove(d)
        else:
            lines.append("D %s%s" % (rel, d))
    for n in files:
        p = os.path.join(base, n)
        if os.path.islink(p):
            lines.append("L %s%s %s" % (rel, n, os.readlink(p)))
        else:
            data = open(p, "rb").read()
            lines.append("F %s%s %d %08x" % (rel, n, len(data), zlib.crc32(data)))
with open(expected, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(sorted(lines)) + "\n")
