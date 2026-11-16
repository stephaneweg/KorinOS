#!/bin/sh
# Essai par comparaison des apps passées de l'assembleur au C (docs/etude/PC-PILOTES.md, « Apps asm en C ») sur
# le Pi sous QEMU raspi4b, dans Git Bash depuis la racine du dépôt (comme tools/sys/compare.sh) :
#   sh tools/apps/compare.sh cards APP...     deux cartes depuis build/sdroot : build/card_app<TAG>_a (l'asm) et
#                                             build/card_app<TAG>_c (build/office/sdroot/apps/APP.app/main à la
#                                             place de l'asm) ; les kits de build/kits/aarch64 sur les deux
#   sh tools/apps/compare.sh run NOM SCRIPT [a|c] [-- LIGNE]
#                                             chaque carte (ou une seule) démarre dans QEMU avec le script de
#                                             tools/qemu_drive.py SCRIPT (« shot X.png » : capture
#                                             build/appcmp/NOM_<a|c>_X.png) ; LIGNE (programme et arguments)
#                                             s'ajoute à /etc/startup.txt ; journal build/appcmp/NOM_<a|c>.log
#   sh tools/apps/compare.sh clean            cartes et images effacées
# Variables : TAG (cartes à part : plusieurs essais en même temps), THEME="Onyx Noir", KEYB (US par défaut, la
# disposition de qemu_drive.py ; FR : celle de la carte). QEMU : emplacement
# RUN_PI_SLOT (2 par défaut : telnet 2327, QMP 4469, VNC 69) ; un seul QEMU à la fois par emplacement : le
# verrou build/qemu<N>.lock (mkdir) est pris pendant chaque démarrage.
set -e
cd "$(dirname "$0")/../.."
A=build/card_app${TAG}_a
C=build/card_app${TAG}_c
O=build/appcmp
N=${RUN_PI_SLOT:-2}
L=build/qemu$N.lock

if [ "$1" = clean ]; then
	rm -rf $A $C build/sd_app${TAG}_a.img build/sd_app${TAG}_c.img
	exit 0
fi
if [ "$1" = cards ]; then
	shift
	rm -rf $A $C
	cp -a build/sdroot $A
	[ ! -d build/kits/aarch64 ] || cp build/kits/aarch64/*.so $A/lib/
	: > $A/etc/preload.txt # (sans préchargement)
	cp -a $A $C
	for n; do
		f=build/office/sdroot/apps/$n.app/main
		[ -f $f ] || { echo "absent : $f (wsl sh tools/office/build.sh $n)"; exit 1; }
		cp $f $C/apps/$n.app/main
	done
	echo "cartes : $A (asm), $C (C : $*)"
	exit 0
fi
[ "$1" = run ] || { echo "usage : sh tools/apps/compare.sh cards APP... | run NOM SCRIPT [a|c] [-- LIGNE] | clean"; exit 1; }
NAME=$2
SCRIPT=$3
shift 3
WHICH="a c"
if [ $# -gt 0 ] && [ "$1" != -- ]; then WHICH=$1; shift; fi
[ "${1:-}" != -- ] || shift
LINE="$*"
[ -d $A ] && [ -d $C ] || { echo "pas de cartes : sh tools/apps/compare.sh cards APP..."; exit 1; }
mkdir -p $O

for x in $WHICH; do
	d=build/card_app${TAG}_$x
	cp build/sdroot/etc/theme.txt $d/etc/theme.txt
	[ -z "$THEME" ] || sed -i "s/^theme *=.*/theme    = $THEME/" $d/etc/theme.txt
	# (le clavier de qemu_drive.py est américain : keyb=US, sauf KEYB=FR)
	cp build/sdroot/etc/options.txt $d/etc/options.txt 2>/dev/null || : > $d/etc/options.txt
	echo "keyb=${KEYB:-US}" >> $d/etc/options.txt
	{ cat build/sdroot/etc/startup.txt; [ -z "$LINE" ] || echo "$LINE"; } > $d/etc/startup.txt
	python -I tools/mkdisk.py build/sd_app${TAG}_$x.img --fat32 $d --exfat build/exfat_root >/dev/null
	sed "s|^shot \(.*\)|shot $O/${NAME}_${x}_\1|" "$SCRIPT" > build/run_app${TAG}_$x.txt
	U=$O/${NAME}_$x.log
	until mkdir $L 2>/dev/null; do sleep 3; done
	python -I tools/qemu_drive.py build/run_app${TAG}_$x.txt --sd build/sd_app${TAG}_$x.img --fwd $((2325 + N)):23 \
		--port $((4467 + N)) --vnc $((67 + N)) --uart $U >/dev/null 2>&1 || true
	rmdir $L
	echo "== $NAME $x : $(ls $O/${NAME}_${x}_* 2>/dev/null | tr '\n' ' ')"
done
