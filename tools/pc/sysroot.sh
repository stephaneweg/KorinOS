#!/bin/sh
# Arbre système du PC (docs/etude/PC-PILOTES.md, « Démarrage jusqu'au shell »), dans WSL :
#   build/pc/sysroot/   ce qui va à la racine de la partition KORIN OS (SD0: du PC), à côté de
#                       EFI/ et korin/ (le chargeur, le noyau, l'initrd : tools/pc/build.sh)
#     bin/              les commandes en C (build/bin/x86_64, tools/bin/build_c.sh)
#     sys/svc/          les services en C (build/svc/x86_64, tools/svc/build_c.sh)
#     sys/              Elegant et la session du bureau en C (build/sys/x86_64, tools/sys/build_c.sh :
#                       elegant, menubar, panel, voronoy, notifyd, clipd, clockd)
#     lib/              les kits en C (build/kits/x86_64, tools/kits/build_c.sh), comme SD0:/lib du Pi
#     etc/              de skel/sd0/etc ce qui vaut pour le PC, puis skel/pc/etc (init.txt,
#                       mounts.txt, services.txt, system.ini « shell=console », options.txt)
#     res/keyb/, res/   les dispositions de clavier (hidmanager), les fuseaux ; pour le bureau : les
#                       dictionnaires (res/lang, tools/lang/merge.py), les icônes sans application
#                       (res/icons, de tools/icons/rendered), les polices (fonts/, de ref/fonts)
#     home/             vide : SD1: (« SD1 = blk.korin, fatfs, /home » de mounts.txt)
# Pas encore d'apps graphiques (SD0:/apps) ni de GCC. Installé par tools/pc/korin.ps1 install,
# essayé sous QEMU par tools/pc/mkkorindisk.py --sysroot build/pc/sysroot.
#
#   wsl sh tools/pc/sysroot.sh          (après wsl sh tools/pc/build.sh, qui construit tout)
set -e
cd "$(dirname "$0")/../.."

OUT=build/pc/sysroot
for d in build/bin/x86_64 build/svc/x86_64 build/kits/x86_64; do
	[ -d "$d" ] || { echo "absent : $d (construire d'abord : wsl sh tools/pc/build.sh)"; exit 1; }
done
# Elegant et la session du bureau (programmes de src/sys en C ; PC_QUICK=1 : déjà construits)
[ -n "$PC_QUICK" ] || sh tools/sys/build_c.sh >/dev/null
rm -rf "$OUT"
mkdir -p "$OUT/bin" "$OUT/sys/svc" "$OUT/lib" "$OUT/etc" "$OUT/res/keyb" "$OUT/res/icons" "$OUT/res/lang" 	"$OUT/fonts" "$OUT/home"

copy_files() { # copy_files <dossier source> <dossier cible> [motif]
	for f in "$1"/${3:-*}; do
		[ -f "$f" ] || continue
		cp "$f" "$2/"
	done
}
copy_files build/bin/x86_64 "$OUT/bin"
copy_files build/svc/x86_64 "$OUT/sys/svc"
[ -d build/sys/x86_64 ] && copy_files build/sys/x86_64 "$OUT/sys" # (Elegant, la session)
copy_files build/kits/x86_64 "$OUT/lib" '*.so'

# etc : les fichiers communs qui valent pour le PC (pas ceux du Pi : initramfs.txt, preload.txt,
# startup.txt, session : celle du PC est dans skel/pc/etc, pkg dont le dépôt est celui du Pi) ; ceux du
# bureau (thème, panel, accès rapides, associations des fichiers, dock pour ses espaces de travail)
for n in paths.txt drivers.txt nvme.txt exec.txt termcap theme.txt panel.ini places.ini fileassoc.ini 	dock.ini; do
	[ -f "skel/sd0/etc/$n" ] && cp "skel/sd0/etc/$n" "$OUT/etc/"
done
cp skel/pc/etc/* "$OUT/etc/" # (init.txt : pour mémoire, init lit celui de l'initrd)
copy_files skel/sd0/res/keyb "$OUT/res/keyb"
[ -f skel/sd0/res/tz.txt ] && cp skel/sd0/res/tz.txt "$OUT/res/"
# le bureau : polices, icônes sans application (comme build.ps1 : celles d'une app partent avec elle),
# dictionnaires
copy_files ref/fonts "$OUT/fonts" '*.ttf'
for f in tools/icons/rendered/*.rgba; do
	[ -d "src/apps/$(basename "$f" .rgba)" ] || cp "$f" "$OUT/res/icons/"
done
python3 -I tools/lang/merge.py "$OUT/res/lang" >/dev/null

n=$(find "$OUT" -type f | wc -l)
k=$(du -sk "$OUT" | cut -f1)
echo "$OUT : $n fichiers, $k Ko"
