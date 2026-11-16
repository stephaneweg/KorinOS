#!/bin/sh
# mktarget.sh IMAGE [TAILLE] : un disque d'essai pour l'installateur (WSL, sgdisk et mkfs.vfat) : GPT, une partition
# EFI de 100 Mo avec un faux \EFI\Microsoft\Boot\bootmgfw.efi, une « Windows » de 1 Go (données de base, FAT32 ici :
# son contenu doit rester intact), puis l'espace libre. Défaut : 6G.
set -e
IMG=$1
SIZE=${2:-6G}
rm -f "$IMG"
truncate -s "$SIZE" "$IMG"
sgdisk -n 1:2048:+100M -t 1:EF00 -c 1:EFI -n 2:0:+1G -t 2:0700 -c 2:Windows "$IMG" >/dev/null
T=$(mktemp -d)
for p in 1 2; do
	S=$(sgdisk -i $p "$IMG" | sed -n 's/^First sector: \([0-9]*\).*/\1/p')
	E=$(sgdisk -i $p "$IMG" | sed -n 's/^Last sector: \([0-9]*\).*/\1/p')
	truncate -s $(( (E - S + 1) * 512 )) "$T/p$p.img"
	mkfs.vfat -F 32 -n $( [ $p = 1 ] && echo EFI || echo WINDOWS ) "$T/p$p.img" >/dev/null
	if [ $p = 1 ]; then
		mmd -i "$T/p1.img" ::EFI ::EFI/Microsoft ::EFI/Microsoft/Boot
		echo "faux bootmgfw" > "$T/f"
		mcopy -i "$T/p1.img" "$T/f" ::EFI/Microsoft/Boot/bootmgfw.efi
	else
		echo "fichier de Windows" > "$T/f"
		mcopy -i "$T/p2.img" "$T/f" ::temoin.txt
	fi
	dd if="$T/p$p.img" of="$IMG" bs=512 seek=$S conv=notrunc,sparse status=none
done
rm -rf "$T"
sgdisk -p "$IMG" | tail -3
