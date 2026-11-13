#!/bin/sh
# Arbre système du PC (docs/etude/PC-PILOTES.md, « Démarrage jusqu'au shell »), dans WSL :
#   build/pc/sysroot/   ce qui va à la racine de la partition KORIN OS (SD0: du PC), à côté de
#                       EFI/ et korin/ (le chargeur, le noyau, l'initrd : tools/pc/build.sh)
#     bin/              les commandes en C (build/bin/x86_64, tools/bin/build_c.sh) ; bash
#                       (build/pc/bash/bash : ARCH=x86_64 sh tools/bash/build.sh), s'il est construit
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
# intelgpu aussi dans bin : lancé de là (sans droits), il parle au Terminal (start, status, stop) et y recopie les
# lignes de test et probe ; un programme de SD0:/sys lancé par bash passe par init2 et n'écrit qu'au journal
[ -f build/svc/x86_64/intelgpu ] && cp build/svc/x86_64/intelgpu "$OUT/bin/intelgpu"
# iwl (Wi-Fi AX201, lancé à la main : docs/etude/PC-PILOTES.md « Wi-Fi AX201 ») de même, et son micrologiciel
# (firmware/pc/iwlwifi : Intel, redistribuable, LICENCE.iwlwifi_firmware à côté) dans SD0:/lib/firmware
[ -f build/svc/x86_64/iwl ] && cp build/svc/x86_64/iwl "$OUT/bin/iwl"
# acpidump (les tables ACPI dans SD0:/acpi) et i2chid (le touchpad I2C, lancé à la main) de même :
# docs/etude/TOUCHPAD-PC.md
for n in acpidump i2chid; do
	if [ -f build/svc/x86_64/$n ]; then cp build/svc/x86_64/$n "$OUT/bin/$n"; fi
done
mkdir -p "$OUT/lib/firmware"
copy_files firmware/pc/iwlwifi "$OUT/lib/firmware"
[ -d build/sys/x86_64 ] && copy_files build/sys/x86_64 "$OUT/sys" # (Elegant, la session)
copy_files build/kits/x86_64 "$OUT/lib" '*.so'
# bash (GNU bash 5.2.37 : ARCH=x86_64 sh tools/bash/build.sh, docs/BASH.md), s'il est construit : la
# console texte et le Terminal le prennent à la place de sh, comme sur le Pi
[ -f build/pc/bash/bash ] && cp build/pc/bash/bash "$OUT/bin/bash"
# gltest (Mesa iris et le service intelgpu : tools/mesa/build-iris.sh), s'il est construit
[ -f build/pc/mesa/sdroot/bin/gltest ] && cp build/pc/mesa/sdroot/bin/gltest "$OUT/bin/"
# GlKit du PC (Mesa iris et softpipe en kit partagé : tools/mesa/build-glkit-pc.sh), s'il est construit ; pas dans
# l'initrd (27 Mo) : AppKit le lit dans SD0:/lib au premier programme qui s'en sert
[ -f build/pc/mesa/sdroot/lib/glkit.so ] && cp build/pc/mesa/sdroot/lib/glkit.so "$OUT/lib/"

# etc : les fichiers communs qui valent pour le PC (pas ceux du Pi : initramfs.txt, preload.txt,
# startup.txt, session : celle du PC est dans skel/pc/etc, pkg dont le dépôt est celui du Pi) ; ceux du
# bureau (thème, panel, accès rapides, associations des fichiers, dock pour ses espaces de travail)
for n in paths.txt drivers.txt nvme.txt exec.txt termcap bash.bashrc theme.txt panel.ini places.ini fileassoc.ini 	dock.ini; do
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
# la SoundFont General MIDI (GeneralUser GS, 32 Mo, hors de git), comme build.ps1 pour le Pi : sans elle, le synthé
# MIDI d'AudioKit (Arabesque, Cosmo Zoom, play d'un .mid) joue sur sa banque FM, d'un autre timbre (« des ressorts »)
# et parfois une octave plus bas. Prise dans $ONYX_DIR (chemin WSL ou Windows), le clone d'Onyx de
# tools/extra/fetch.sh (build/onyx), un clone voisin (../Onyx), ou la carte du Pi déjà faite.
sfdir=
for d in "${ONYX_DIR:+$(wslpath -u "$ONYX_DIR" 2>/dev/null || echo "$ONYX_DIR")/sdcard/res/soundfonts}" \
	build/onyx/sdcard/res/soundfonts ../Onyx/sdcard/res/soundfonts build/sdroot/res/soundfonts \
	build/sdcard-full/res/soundfonts; do
	[ -n "$d" ] && ls "$d"/*.sf2 >/dev/null 2>&1 && { sfdir=$d; break; }
done
if [ -n "$sfdir" ]; then
	mkdir -p "$OUT/res/soundfonts"
	copy_files "$sfdir" "$OUT/res/soundfonts"
else
	echo "attention : pas de SoundFont (ONYX_DIR, build/onyx, ../Onyx) : la musique des apps sera celle de la banque FM"
fi

n=$(find "$OUT" -type f | wc -l)
k=$(du -sk "$OUT" | cut -f1)
echo "$OUT : $n fichiers, $k Ko"

# les programmes C et C++ sur libposix (essais, OpenSSL, apps : tools/pc/sysroot_apps.sh), avec PC_APPS=1 :
# l arbre passe alors 65 Mo, la partition KORIN OS des disques de mkkorindisk.py
[ -z "${PC_APPS:-}" ] || sh tools/pc/sysroot_apps.sh "$OUT"
