#!/bin/sh
# Essai par comparaison d'Elegant et de la session du bureau en C (docs/etude/PC-PILOTES.md, « Bureau
# Elegant en C ») contre l'asm, sur le Pi sous QEMU raspi4b, dans Git Bash depuis la racine du dépôt :
#   sh tools/sys/compare.sh cards          deux cartes depuis build/sdroot : build/card_ws_a (l'asm) et
#                                          build/card_ws_c (build/sys/aarch64/<nom> de SYS_C à la place) ;
#                                          movebench (build/bin/aarch64) sur les deux ; telnet « essai »
#   sh tools/sys/compare.sh run NOM SCRIPT [a|c]
#                                          chaque carte (ou une seule) démarre dans QEMU avec le script de
#                                          tools/qemu_drive.py SCRIPT (« shot X.png » : capture
#                                          build/wscmp/NOM_<a|c>_X.png) ; au démarrage, boot.sh mesure
#                                          (bench, movebench) et copie au journal les lignes datées du
#                                          démarrage (SYS_KLOG) ; journaux build/wscmp/NOM_<a|c>.log,
#                                          mesures build/wscmp/NOM_<a|c>.txt
#   sh tools/sys/compare.sh clean          cartes, images, captures effacées
# Variantes (variables) : THEME="Onyx Noir" (etc/theme.txt), SHELL_MODE=poche (etc/system.ini), BENCH=0
# (sans mesures). SYS_C : les programmes mis en C (défaut : elegant menubar panel voronoy notifyd
# clipd clockd). QEMU : emplacement RUN_PI_SLOT (2 par défaut : telnet 2327, QMP 4469, VNC 69).
set -e
cd "$(dirname "$0")/../.."
A=build/card_ws_a
C=build/card_ws_c
O=build/wscmp
N=${RUN_PI_SLOT:-2}
SYS_C=${SYS_C:-"elegant menubar panel voronoy notifyd clipd clockd"}

if [ "$1" = clean ]; then
	rm -rf $A $C $O build/sd_ws_a.img build/sd_ws_c.img
	exit 0
fi
if [ "$1" = cards ]; then
	rm -rf $A $C
	cp -a build/sdroot $A
	printf 'password = essai\n' > $A/etc/remote.txt
	cp build/bin/aarch64/movebench build/bin/aarch64/bench $A/bin/
	: > $A/etc/preload.txt # (sans préchargement : le démarrage mesuré ne se dispute pas le processeur)
	cp -a $A $C
	for n in $SYS_C; do cp build/sys/aarch64/$n $C/sys/$n; done
	echo "cartes : $A (asm), $C (C : $SYS_C)"
	exit 0
fi
[ "$1" = run ] || { echo "usage : sh tools/sys/compare.sh cards | run NOM SCRIPT [a|c] | clean"; exit 1; }
NAME=$2
SCRIPT=$3
WHICH=${4:-"a c"}
[ -d $A ] && [ -d $C ] || { echo "pas de cartes : sh tools/sys/compare.sh cards"; exit 1; }
mkdir -p $O

for x in $WHICH; do
	d=build/card_ws_$x
	# variantes : thème, mode
	cp build/sdroot/etc/theme.txt $d/etc/theme.txt
	cp build/sdroot/etc/system.ini $d/etc/system.ini
	[ -n "$THEME" ] && sed -i "s/^theme *=.*/theme    = $THEME/" $d/etc/theme.txt
	[ -n "$SHELL_MODE" ] && sed -i "s/^shell=.*/shell=$SHELL_MODE/" $d/etc/system.ini
	# boot.sh : mesures, puis les lignes datées du démarrage, entre deux repères
	mkdir -p $d/bt
	if [ "${BENCH:-1}" = 1 ]; then
		cat > $d/bt/boot.sh <<'EOF'
SD0:/bin/journal > RAM0:/journal_out.txt 2>&1 &
SD0:/bin/bench 30 > RAM0:/bench.txt 2>&1
SD0:/bin/movebench 1 >> RAM0:/bench.txt 2>&1
SD0:/bin/bench 1 >> RAM0:/bench.txt 2>&1
SD0:/bin/movebench 1 >> RAM0:/bench.txt 2>&1
wait
echo @@BT_DEBUT
cat RAM0:/bench.txt
grep -a "elegant\|panel :\|menubar\|voronoy\|clockd\|notifyd\|clipd\|init2 : SD0:/sys/svc/busd" SD0:/journal.txt
echo @@BT_FIN
EOF
	else
		printf 'echo @@BT_DEBUT\necho @@BT_FIN\n' > $d/bt/boot.sh
	fi
	{ cat build/sdroot/etc/startup.txt; echo "SD0:/bin/bash SD0:/bt/boot.sh"; } > $d/etc/startup.txt
	python -I tools/mkdisk.py build/sd_ws_$x.img --fat32 $d --exfat build/exfat_root >/dev/null
	sed "s|^shot \(.*\)|shot $O/${NAME}_${x}_\1|" "$SCRIPT" > build/run_pi_ws_$N.txt
	U=$O/${NAME}_$x.log
	python -I tools/qemu_drive.py build/run_pi_ws_$N.txt --sd build/sd_ws_$x.img --fwd $((2325 + N)):23 \
		--port $((4467 + N)) --vnc $((67 + N)) --uart $U >/dev/null 2>&1 || true
	tr -d '\r' < $U | sed -n '/^@@BT_DEBUT/,/^@@BT_FIN/p' | grep -av '^@@BT_' > $O/${NAME}_$x.txt || true
	echo "== $NAME $x"
	cat $O/${NAME}_$x.txt
done
