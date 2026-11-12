#!/bin/sh
# Volume d'essai du menu de démarrage (QEMU seulement), dans WSL :
#   sh tools/pc/menu_esp.sh [--no-windows | --bad-windows] [--cfg "texte de boot.cfg"]
# Construit build/pc/esp-menu à partir de build/pc/esp (déjà construit par tools/pc/build.sh) :
#   EFI/BOOT/BOOTX64.EFI, korin/kernel.elf, korin/initrd.tar   copies de build/pc/esp
#   EFI/Microsoft/Boot/bootmgfw.efi   le FAUX Windows (tools/pc/fakewin/bootmgfw.asm)
#   EFI/Microsoft/Boot/BCD            un faux BCD (texte), que le faux Windows cherche
#   korin/boot.cfg                    seulement avec --cfg (« \n » sépare les lignes ; sans : défauts)
# --bad-windows : bootmgfw.efi est un fichier de texte (LoadImage doit échouer) ;
# --no-windows : pas de \EFI\Microsoft.
# Puis : python -I tools/pc/qemu_pc.py test --esp build/pc/esp-menu --vars build/pc/ovmf_vars_menu.fd …
# Ce volume n'est jamais copié ailleurs : sd.ps1 et korin.ps1 ne prennent que build/pc/esp
# (BOOTX64.EFI, kernel.elf, initrd.tar).
set -e
cd "$(dirname "$0")/../.."

SRC=build/pc/esp
DST=build/pc/esp-menu
FASM=ref/fasm/FASM.EXE
WIN=fake
CFG=""
HAVE_CFG=""
while [ $# -gt 0 ]; do
	case "$1" in
	--no-windows) WIN=none ;;
	--bad-windows) WIN=bad ;;
	--cfg) shift; CFG="$1"; HAVE_CFG=1 ;;
	*) echo "option inconnue : $1" >&2; exit 2 ;;
	esac
	shift
done

[ -f "$SRC/EFI/BOOT/BOOTX64.EFI" ] || { echo "construire d'abord : sh tools/pc/build.sh" >&2; exit 1; }
rm -rf "$DST"
mkdir -p "$DST/EFI/BOOT" "$DST/korin"
cp "$SRC/EFI/BOOT/BOOTX64.EFI" "$DST/EFI/BOOT/"
cp "$SRC/korin/kernel.elf" "$DST/korin/"
[ -f "$SRC/korin/initrd.tar" ] && cp "$SRC/korin/initrd.tar" "$DST/korin/"
case "$WIN" in
fake)
	mkdir -p "$DST/EFI/Microsoft/Boot"
	"$FASM" tools/pc/fakewin/bootmgfw.asm "$DST/EFI/Microsoft/Boot/bootmgfw.efi" >build/pc/fakewin.log 2>&1 ||
		{ cat build/pc/fakewin.log; exit 1; }
	echo "faux BCD pour l'essai du menu de Korin" >"$DST/EFI/Microsoft/Boot/BCD"
	;;
bad)
	mkdir -p "$DST/EFI/Microsoft/Boot"
	echo "pas un programme UEFI" >"$DST/EFI/Microsoft/Boot/bootmgfw.efi"
	;;
esac
[ -n "$HAVE_CFG" ] && printf '%b\n' "$CFG" >"$DST/korin/boot.cfg"
find "$DST" -type f | sort
