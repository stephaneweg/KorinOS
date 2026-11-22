# relocate-sdk.py ANCIEN NOUVEAU [DOSSIER] -- après la copie du SDK de Jet ailleurs que /devtools/jet-sdk (le volume
# système de Korin est en FAT32, servi par un seul process fatfs : 10 compilations parallèles le saturent, ≈ 20 objets
# par quart d'heure ; sur /home, en ext4, le même travail va bien plus vite) : le chemin ANCIEN remplacé par NOUVEAU
# dans les fichiers texte du SDK qui le contiennent (commands.txt, les en-têtes de renvoi de PrivateHeaders, les
# cmake_pch.hxx, les lots unifiés, jetbuild, jetlink...). Les objets, les archives (minces, aux chemins relatifs) et
# les autres binaires ne sont pas touchés. Usage : python3 -I relocate-sdk.py /devtools/jet-sdk /home/moi/jet-sdk
import os, sys, time
old, new = sys.argv[1].encode(), sys.argv[2].encode()
top = sys.argv[3] if len(sys.argv) > 3 else sys.argv[2]
SKIP = (".o", ".a", ".gch", ".so", ".png", ".jpg", ".gz", ".zip", ".bin", ".dat", ".ttf", ".otf", ".woff", ".woff2")
t0 = time.time(); seen = changed = 0
for root, dirs, files in os.walk(top):
    for f in files:
        if f.endswith(SKIP): continue
        p = os.path.join(root, f); seen += 1
        try:
            with open(p, "rb") as h: d = h.read()
        except OSError:
            continue
        if old not in d or b"\0" in d[:4096]: continue
        with open(p, "wb") as h: h.write(d.replace(old, new))
        changed += 1
print("relocate-sdk : %d fichiers lus, %d réécrits (%s -> %s), %.0f s" % (seen, changed, old.decode(), new.decode(), time.time() - t0))
