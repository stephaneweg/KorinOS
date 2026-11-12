#!/bin/sh
# Essai par comparaison des commandes de /bin écrites en C (docs/BIN-C.md), dans Git Bash, depuis la
# racine du dépôt :
#   sh tools/bin/compare.sh cards          deux cartes depuis build/sdroot : build/card_bin_a (les
#                                          commandes en asm) et build/card_bin_c (celles de
#                                          build/bin/aarch64 à la place) ; telnet « essai »
#   sh tools/bin/compare.sh [commande...]  les lignes de src/bin/<commande>/test.txt (toutes sans
#                                          nom) lancées par bash sur chaque carte dans QEMU, sorties
#                                          dans build/bin/cmp_a.txt et cmp_c.txt, différences dans
#                                          build/bin/cmp_diff.txt (vide : identiques)
#   sh tools/bin/compare.sh clean          cartes et images effacées
# QEMU : emplacement RUN_PI_SLOT (3 par défaut ; ports comme tools/kits/run_pi.sh). Les binaires C
# sont recopiés et les images refaites à chaque essai (tools/bin/build_c.sh d'abord, dans WSL).
set -e
cd "$(dirname "$0")/../.."
A=build/card_bin_a
C=build/card_bin_c
export RUN_PI_SLOT=${RUN_PI_SLOT:-3}

if [ "$1" = clean ]; then
	rm -rf $A $C build/sd_bin_a.img build/sd_bin_c.img
	exit 0
fi
if [ "$1" = cards ]; then
	rm -rf $A $C
	cp -a build/sdroot $A
	printf 'password = essai\n' > $A/etc/remote.txt
	cp -a $A $C
	echo "cartes : $A, $C"
	exit 0
fi
[ -d $A ] && [ -d $C ] || { echo "pas de cartes : sh tools/bin/compare.sh cards"; exit 1; }

# les commandes en C sur la carte C (l'asm reste sur la carte A)
for f in build/bin/aarch64/*; do
	[ -f "$f" ] && cp "$f" $C/bin/
done
for d in $A $C; do
	rm -rf $d/bt
	python -I tools/bin/mkfixtures.py $d "$@" >/dev/null
	# les essais au démarrage (init2 : startup.txt), sans telnet
	{ cat build/sdroot/etc/startup.txt; echo "SD0:/bin/bash SD0:/bt/boot.sh"; } > $d/etc/startup.txt
done

# une carte dans QEMU (ports de tools/kits/run_pi.sh) : les essais tournent au démarrage
# (SD0:/bt/boot.sh, lancé par init2 depuis startup.txt) et leur sortie revient par la console du
# noyau, entre @@BT_DEBUT et @@BT_FIN, dans le journal de l'UART. Pas de telnet : sous QEMU, la
# première session tarde parfois et les suivantes restent muettes ; pas de FTP : le mode passif ne
# traverse pas le réseau de QEMU. Les lignes du noyau (« [pid N nom] terminé », « image … ») sont
# retirées.
run_card() { # run_card IMAGE SORTIE
	N=$RUN_PI_SLOT
	TP=$((2325 + N))
	U=build/run_pi_uart_$N.log
	: > $U
	echo "wait 1800" > build/run_pi_wait_$N.txt
	python -I tools/qemu_drive.py build/run_pi_wait_$N.txt --sd "$1" --fwd $TP:23 --port $((4467 + N)) \
		--vnc $((67 + N)) --uart $U >/dev/null 2>&1 &
	QPID=$!
	for i in $(seq 1 180); do
		sleep 10
		grep -aq '^@@BT_FIN' $U && break
	done
	sleep 2
	kill $QPID 2>/dev/null || true
	powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='qemu-system-aarch64.exe'\" |
		Where-Object { \$_.CommandLine -like '*127.0.0.1:$TP-:23*' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force }" || true
	tr -d '\r' < $U | sed -n '/^@@BT_DEBUT/,/^@@BT_FIN/p' |
		grep -av '^\[pid [0-9]* [^]]*\] \|^image SD0:\|^@@BT_' > "$2" || true
}

for x in a c; do
	d=build/card_bin_$x
	python -I tools/mkdisk.py build/sd_bin_$x.img --fat32 $d --exfat build/exfat_root >/dev/null
	run_card build/sd_bin_$x.img build/bin/cmp_raw_$x.txt
	cp build/run_pi_uart_$RUN_PI_SLOT.log build/bin/cmp_uart_$x.log
	# (seule la sortie de cat : depuis « === » jusqu'à « === fin »)
	sed -n '/^=== /,/^=== fin/p' build/bin/cmp_raw_$x.txt | tr -d '\r' > build/bin/cmp_$x.txt
done
diff build/bin/cmp_a.txt build/bin/cmp_c.txt > build/bin/cmp_diff.txt || true
echo "essais : $(grep -c '^\$ ' build/bin/cmp_a.txt) (asm), $(grep -c '^\$ ' build/bin/cmp_c.txt) (C) ;" \
	"différences : $(grep -c '^[<>]' build/bin/cmp_diff.txt) lignes (build/bin/cmp_diff.txt)"
