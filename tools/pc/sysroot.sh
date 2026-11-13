#!/bin/sh
# Arbre système du PC (docs/etude/PC-PILOTES.md, « Démarrage jusqu'au shell »), dans WSL :
#   build/pc/sysroot/   ce qui va à la racine de la partition KORIN OS (SD0: du PC), à côté de
#                       EFI/ et korin/ (le chargeur, le noyau, l'initrd : tools/pc/build.sh)
#     bin/              les commandes en C (build/bin/x86_64, tools/bin/build_c.sh)
#     sys/svc/          les services en C (build/svc/x86_64, tools/svc/build_c.sh)
#     lib/              les kits en C (build/kits/x86_64, tools/kits/build_c.sh), comme SD0:/lib du Pi
#     etc/              de skel/sd0/etc ce qui vaut pour le PC, puis skel/pc/etc (init.txt,
#                       mounts.txt, services.txt, system.ini « shell=console », options.txt)
#     res/keyb/, res/   les dispositions de clavier (hidmanager), les fuseaux
#     home/             vide : SD1: (« SD1 = blk.korin, fatfs, /home » de mounts.txt)
# Pas d'apps graphiques ni de GCC pour l'instant. Installé par tools/pc/korin.ps1 install,
# essayé sous QEMU par tools/pc/mkkorindisk.py --sysroot build/pc/sysroot.
#
#   wsl sh tools/pc/sysroot.sh          (après wsl sh tools/pc/build.sh, qui construit tout)
set -e
cd "$(dirname "$0")/../.."

OUT=build/pc/sysroot
for d in build/bin/x86_64 build/svc/x86_64 build/kits/x86_64; do
	[ -d "$d" ] || { echo "absent : $d (construire d'abord : wsl sh tools/pc/build.sh)"; exit 1; }
done
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/sys/svc" "$OUT/lib" "$OUT/etc" "$OUT/res/keyb" "$OUT/home"

copy_files() { # copy_files <dossier source> <dossier cible> [motif]
	for f in "$1"/${3:-*}; do
		[ -f "$f" ] || continue
		cp "$f" "$2/"
	done
}
copy_files build/bin/x86_64 "$OUT/bin"
copy_files build/svc/x86_64 "$OUT/sys/svc"
copy_files build/kits/x86_64 "$OUT/lib" '*.so'

# etc : les fichiers communs qui valent pour le PC (pas ceux des apps, du bureau, du Pi : initramfs.txt,
# preload.txt, startup.txt, session, dock, panel, pkg dont le dépôt est celui du Pi)
for n in paths.txt drivers.txt nvme.txt exec.txt termcap; do
	[ -f "skel/sd0/etc/$n" ] && cp "skel/sd0/etc/$n" "$OUT/etc/"
done
cp skel/pc/etc/* "$OUT/etc/" # (init.txt : pour mémoire, init lit celui de l'initrd)
copy_files skel/sd0/res/keyb "$OUT/res/keyb"
[ -f skel/sd0/res/tz.txt ] && cp skel/sd0/res/tz.txt "$OUT/res/"

n=$(find "$OUT" -type f | wc -l)
k=$(du -sk "$OUT" | cut -f1)
echo "$OUT : $n fichiers, $k Ko"
