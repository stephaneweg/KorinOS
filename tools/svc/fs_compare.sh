#!/bin/sh
# Essai par comparaison des services de fichiers écrits en C (vfs, fatfs, ramfs, imgblk ;
# docs/etude/PC-PILOTES.md, « Services de fichiers en C »), dans Git Bash, depuis la racine du dépôt :
#   sh tools/svc/fs_compare.sh cards   deux cartes depuis build/sdroot : build/card_fs_a (les services
#                                      en asm) et build/card_fs_c (ceux de build/svc/aarch64 à la place,
#                                      sur la carte et dans les modules vfs.mod et fatfs.mod) ; mêmes
#                                      commandes de /bin sur les deux ; telnet « essai »
#   sh tools/svc/fs_compare.sh [commande...]
#                                      au démarrage de chaque carte (startup.txt) : les programmes
#                                      d'essai des fichiers (FS_TESTS), puis les lignes des test.txt de
#                                      src/bin (toutes, ou celles des commandes nommées) ; sorties dans
#                                      build/fs_cmp_a.txt et build/fs_cmp_c.txt, différences dans
#                                      build/fs_cmp_diff.txt
#   sh tools/svc/fs_compare.sh clean   cartes, images et modules effacés
# Sur les cartes, SD1: est l'image d'essai test_exfat.img (refaite avec build/exfat_root : exfattest et
# fstest2 y écrivent), jamais une partition personnelle. QEMU : emplacement RUN_PI_SLOT (2 par défaut).
# Les services C sont recopiés et les images refaites à chaque essai (tools/svc/build_c.sh d'abord, WSL).
set -e
cd "$(dirname "$0")/../.."
A=build/card_fs_a
C=build/card_fs_c
MA=build/fs_mods_a
MC=build/fs_mods_c
export RUN_PI_SLOT=${RUN_PI_SLOT:-2}
FASM=tools/fasmarm/fasmarm.exe
FS_TESTS=${FS_TESTS:-"fstest|fstest2|vfstest|exfattest|cachetest|directtest|writetest|writebench|readbench|pipetest|exectest|ramtest RAM0|ramtest IMG0|aktest -f|posixtest file io exec fork ipc"}

if [ "$1" = clean ]; then
	rm -rf $A $C $MA $MC build/sd_fs_a.img build/sd_fs_c.img build/fs_exfat.img
	exit 0
fi
if [ "$1" = cards ]; then
	rm -rf $A $C
	cp -a build/sdroot $A
	printf 'password = essai\n' > $A/etc/remote.txt
	# SD1: = l'image d'essai exFAT (imgblk img1), avec les données d'exfattest ; plus d'IMG1:
	python -I tools/mkdisk.py build/fs_exfat.img --exfat build/exfat_root --taille 16 >/dev/null
	cp build/fs_exfat.img $A/test_exfat.img
	sed -i -e 's/^SD1 = .*/SD1 = blk.img1p1, fatfs/' -e '/^IMG1 = /d' $A/etc/mounts.txt
	[ -f build/kits/test/aarch64/aktest ] && cp build/kits/test/aarch64/aktest $A/bin/
	cp -a $A $C
	rm -rf $MA $MC
	mkdir -p $MA $MC
	cp build/modules/*.mod $MA/
	cp build/modules/*.mod $MC/
	echo "cartes : $A, $C ; modules : $MA, $MC"
	exit 0
fi
[ -d $A ] && [ -d $C ] || { echo "pas de cartes : sh tools/svc/fs_compare.sh cards"; exit 1; }

# les services en C sur la carte C (sur la carte et dans l'initramfs : vfs.mod, fatfs.mod)
for n in vfs fatfs ramfs imgblk; do
	cp build/svc/aarch64/$n $C/sys/svc/$n
done
for n in vfs fatfs; do
	printf "include '../src/svc/inc/module.inc'\nmodule '%s', 'svc/aarch64/%s'\n" $n $n > build/fs_mod_$n.asm
	(cd build && ../$FASM fs_mod_$n.asm fs_mods_c/$n.mod >/dev/null)
	rm -f build/fs_mod_$n.asm
done

for d in $A $C; do
	rm -rf $d/bt $d/ft
	python -I tools/bin/mkfixtures.py $d "$@" >/dev/null
	mkdir -p $d/ft
	{
		echo "# genere par tools/svc/fs_compare.sh"
		echo ": > RAM0:/ft_out.txt"
		echo 't() { echo "### $*" >> RAM0:/ft_out.txt; "$@" < SD0:/bt/empty.txt >> RAM0:/ft_out.txt 2>&1; echo "[code $?]" >> RAM0:/ft_out.txt; }'
		echo "cd SD0:/"
		echo "mkdir SD0:/tmp"
		echo "$FS_TESTS" | tr '|' '\n' | sed 's/^/t /'
		echo "bash SD0:/bt/run.sh < SD0:/bt/empty.txt >> RAM0:/ft_out.txt 2>&1"
		echo "echo @@BT_DEBUT"
		echo "cat RAM0:/ft_out.txt"
		echo "echo @@BT_FIN"
	} > $d/ft/boot.sh
	{ cat build/sdroot/etc/startup.txt; echo "SD0:/bin/bash SD0:/ft/boot.sh"; } > $d/etc/startup.txt
done

# une carte dans QEMU (ports de tools/kits/run_pi.sh), ses modules : les essais tournent au démarrage
# et leur sortie revient par la console du noyau, entre @@BT_DEBUT et @@BT_FIN (comme compare.sh)
run_card() { # run_card IMAGE MODULES SORTIE
	N=$RUN_PI_SLOT
	TP=$((2325 + N))
	U=build/run_pi_uart_$N.log
	: > $U
	echo "wait 2400" > build/run_pi_wait_$N.txt
	mods=$(sed -n 's/^initramfs \([^ ]*\).*/\1/p' boot/config.txt | tr ',' '\n' | sed "s|^|$2/|" |
		tr '\n' ',' | sed 's/,$//') # (dans l'ordre de boot/config.txt)
	python -I tools/qemu_drive.py build/run_pi_wait_$N.txt --sd "$1" --fwd $TP:23 --port $((4467 + N)) \
		--vnc $((67 + N)) --uart $U --modules "$mods" >/dev/null 2>&1 &
	QPID=$!
	for i in $(seq 1 240); do
		sleep 10
		grep -aq '^@@BT_FIN' $U && break
	done
	sleep 2
	kill $QPID 2>/dev/null || true
	powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='qemu-system-aarch64.exe'\" |
		Where-Object { \$_.CommandLine -like '*127.0.0.1:$TP-:23*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" || true
	tr -d '\r' < $U | sed -n '/^@@BT_DEBUT/,/^@@BT_FIN/p' |
		grep -av '^\[pid [0-9]* [^]]*\] \|^image SD0:\|^@@BT_' > "$3" || true
}

for x in a c; do
	d=build/card_fs_$x
	python -I tools/mkdisk.py build/sd_fs_$x.img --fat32 $d --exfat build/exfat_root >/dev/null
	run_card build/sd_fs_$x.img build/fs_mods_$x build/fs_cmp_$x.txt
	cp build/run_pi_uart_$RUN_PI_SLOT.log build/fs_cmp_uart_$x.log
done
diff build/fs_cmp_a.txt build/fs_cmp_c.txt > build/fs_cmp_diff.txt || true
echo "essais : $(grep -c '^### \|^\$ ' build/fs_cmp_a.txt) (asm), $(grep -c '^### \|^\$ ' build/fs_cmp_c.txt) (C) ;" \
	"différences : $(grep -c '^[<>]' build/fs_cmp_diff.txt) lignes (build/fs_cmp_diff.txt)"
