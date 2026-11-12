#!/bin/sh
# Essai par comparaison des services du système écrits en C (docs/etude/PC-PILOTES.md, « Services du
# système en C »), dans Git Bash, depuis la racine du dépôt, comme tools/bin/compare.sh :
#   sh tools/svc/compare.sh cards          deux cartes depuis build/sdroot : build/card_sys_a (les
#                                          services en asm) et build/card_sys_c (ceux de
#                                          build/svc/aarch64 à la place) ; telnet « essai » ; modules
#                                          de démarrage build/modules_sys_a (ceux de build/modules) et
#                                          build/modules_sys_c (init.mod avec init en C : INIT_C)
#   sh tools/svc/compare.sh [commande...]  chaque carte démarre dans QEMU, les lignes de
#                                          src/bin/<commande>/test.txt (toutes sans nom) tournent au
#                                          démarrage (tools/bin/mkfixtures.py) ; journaux de l'UART
#                                          build/svccmp/uart_a.log et uart_c.log, sorties
#                                          build/svccmp/cmp_a.txt et cmp_c.txt, différences
#                                          build/svccmp/cmp_diff.txt (vide : identiques)
#   sh tools/svc/compare.sh clean          cartes, images et modules effacés
# SVC_C (facultatif) : les services mis en C sur la carte C (défaut : echo mapsvc busd hwmanager
# hidmanager init2 init). QEMU : emplacement RUN_PI_SLOT (1 par défaut ; ports de
# tools/kits/run_pi.sh). Les services C viennent de tools/svc/build_c.sh (dans WSL, d'abord).
set -e
cd "$(dirname "$0")/../.."
A=build/card_sys_a
C=build/card_sys_c
MA=build/modules_sys_a
MC=build/modules_sys_c
O=build/svccmp
export RUN_PI_SLOT=${RUN_PI_SLOT:-1}
SVC_C=${SVC_C:-"echo mapsvc busd hwmanager hidmanager init2 init"}

if [ "$1" = clean ]; then
	rm -rf $A $C $MA $MC $O build/sd_sys_a.img build/sd_sys_c.img
	exit 0
fi
if [ "$1" = cards ]; then
	rm -rf $A $C $MA $MC
	cp -a build/sdroot $A
	printf 'password = essai\n' > $A/etc/remote.txt
	cp -a $A $C
	mkdir -p $MA $MC
	cp build/modules/*.mod $MA/
	cp build/modules/*.mod $MC/
	echo "cartes : $A, $C ; modules : $MA, $MC"
	exit 0
fi
[ -d $A ] && [ -d $C ] || { echo "pas de cartes : sh tools/svc/compare.sh cards"; exit 1; }
mkdir -p $O

# les services en C sur la carte C (l'asm reste sur la carte A) ; init : aussi dans init.mod
for n in $SVC_C; do
	cp build/svc/aarch64/$n $C/sys/svc/$n
	if [ $n = init ]; then
		(cd src/svc/modules && ../../../tools/fasmarm/fasmarm.exe -d INIT_C=1 init.asm \
			../../../$MC/init.mod >/dev/null)
	fi
done
for d in $A $C; do
	rm -rf $d/bt
	python -I tools/bin/mkfixtures.py $d "$@" >/dev/null
	{ cat build/sdroot/etc/startup.txt; echo "SD0:/bin/bash SD0:/bt/boot.sh"; } > $d/etc/startup.txt
done
LIST="init.mod,list.mod,appkit.mod,sd.mod,vfs.mod,fatfs.mod" # (boot/config.txt)

run_card() { # run_card IMAGE MODULES SORTIE
	N=$RUN_PI_SLOT
	TP=$((2325 + N))
	U=build/run_pi_uart_$N.log
	: > $U
	echo "wait 1800" > build/run_pi_wait_$N.txt
	M=$(echo $LIST | sed "s|\([a-z0-9]*\.mod\)|$2/\1|g")
	python -I tools/qemu_drive.py build/run_pi_wait_$N.txt --sd "$1" --fwd $TP:23 --port $((4467 + N)) \
		--vnc $((67 + N)) --uart $U --modules "$M" >/dev/null 2>&1 &
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
		grep -av '^\[pid [0-9]* [^]]*\] \|^image SD0:\|^@@BT_' > "$3" || true
}

for x in a c; do
	d=build/card_sys_$x
	python -I tools/mkdisk.py build/sd_sys_$x.img --fat32 $d --exfat build/exfat_root >/dev/null
	run_card build/sd_sys_$x.img build/modules_sys_$x $O/raw_$x.txt
	tr -d '\r' < build/run_pi_uart_$RUN_PI_SLOT.log > $O/uart_$x.log
	sed -n '/^=== /,/^=== fin/p' $O/raw_$x.txt > $O/cmp_$x.txt
done
diff $O/cmp_a.txt $O/cmp_c.txt > $O/cmp_diff.txt || true
echo "essais : $(grep -c '^\$ ' $O/cmp_a.txt) (asm), $(grep -c '^\$ ' $O/cmp_c.txt) (C) ;" \
	"différences : $(grep -c '^[<>]' $O/cmp_diff.txt) lignes ($O/cmp_diff.txt) ; journaux $O/uart_a.log, uart_c.log"
