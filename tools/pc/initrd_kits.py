"""initrd_kits : les kits de korin/initrd.tar doivent être ceux de /lib (docs/PAQUETS.md, « Le paquet pc »).

Le noyau prend un kit dans l'initrd avant /lib (src/kernel/core/kits.c, sys_lib_open : « <nom>.so » de l'initrd si le
kit n'est pas en mémoire ; /lib n'est lu que si l'initrd ne l'a pas). Un initrd qui garde un kit plus ancien que /lib
fait échouer les programmes construits pour le nouveau (menubar : « kit absent ou trop ancien », code 127 ; les apps
egui : saut à 0 sur une fonction que l'ancien kit n'a pas ; 21/11/2026, pc 2026.11.70 publié par --only sans l'initrd).

    sync(initrd, libs) -> nouvel initrd (octets) ou None : chaque membre « <kit>.so » remplacé par libs[<kit>.so] quand
                          la version du kit de lib (le mot qui suit « AKIT », celle que vérifie sys_lib_open) est plus
                          haute ; à version égale, le kit de l'initrd est gardé (c'est lui qui tourne) ; l'ordre, les
                          noms et les dates des membres sont gardés (archive reproductible)
    python3 -I tools/pc/initrd_kits.py <initrd.tar> <dossier lib> [<sortie>]   (sans sortie : vérifie seulement)
"""
import io
import os
import struct
import sys
import tarfile


def version(image):
    """La version d'un kit : le mot de 32 bits après « AKIT » (src/kernel/core/kits.c) ; -1 sans table."""
    i = image.find(b"AKIT")
    return struct.unpack_from("<I", image, i + 4)[0] if i >= 0 and i + 8 <= len(image) else -1


def kits(initrd):
    """Les kits (<nom>.so) de l'initrd : {nom: octets}."""
    out = {}
    with tarfile.open(fileobj=io.BytesIO(initrd), mode="r:") as t:
        for m in t.getmembers():
            if m.isfile() and m.name.endswith(".so") and "/" not in m.name:
                out[m.name] = t.extractfile(m).read()
    return out


def sync(initrd, libs):
    with tarfile.open(fileobj=io.BytesIO(initrd), mode="r:") as t:
        members = [(m, t.extractfile(m).read() if m.isfile() else None) for m in t.getmembers()]
    changed = False
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:", format=tarfile.USTAR_FORMAT) as out:
        for m, data in members:
            if data is not None and m.name in libs and version(libs[m.name]) > version(data):
                data = libs[m.name]
                m.size = len(data)
                changed = True
            out.addfile(m, io.BytesIO(data) if data is not None else None)
    return buf.getvalue() if changed else None


def main():
    initrd = open(sys.argv[1], "rb").read()
    libdir = sys.argv[2]
    libs = {f: open(os.path.join(libdir, f), "rb").read() for f in os.listdir(libdir) if f.endswith(".so")}
    have = kits(initrd)
    diff = [n for n in sorted(have) if n in libs and version(libs[n]) > version(have[n])]
    for n in sorted(have):
        if n in libs and libs[n] != have[n]:
            print("initrd_kits : %s : initrd v%d (%d octets), lib v%d (%d)%s" % (n, version(have[n]), len(have[n]), version(libs[n]),
                  len(libs[n]), " -> à remplacer" if n in diff else ""))
    if len(sys.argv) > 3:
        new = sync(initrd, libs)
        open(sys.argv[3], "wb").write(new if new is not None else initrd)
        print("initrd_kits : %s (%d kit(s) remplacé(s))" % (sys.argv[3], len(diff)))
    elif diff:
        sys.exit(1)


if __name__ == "__main__":
    main()
