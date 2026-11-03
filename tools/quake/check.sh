#!/bin/sh
# Le C d'id contre les routines A64, sous qemu-aarch64 : memes sommes pour chaque image, avec le son et la palette
# (tools/quake/harness.c). Dans WSL, depuis la racine du depot, apres tools/quake/mk_harness.sh :
#   sh tools/quake/check.sh [dossier des donnees]
# Une ligne par essai : images, taille, commande. -d : la premiere image qui diverge.
W=build/quake
D=${1:-$W/data}
fail=0
run () { # images largeur hauteur commande...
	n=$1; w=$2; h=$3
	shift 3
	qemu-aarch64 $W/ref_quake "$D" $n $w $h "$@" 2>/dev/null > $W/ref_$$.txt
	qemu-aarch64 $W/asm_quake "$D" $n $w $h "$@" 2>/dev/null > $W/asm_$$.txt
	l=$(wc -l < $W/asm_$$.txt)
	if [ "$l" = "$n" ] && cmp -s $W/ref_$$.txt $W/asm_$$.txt; then
		echo "ok   $n images ${w}x$h $*"
	else
		fail=1
		echo "DIFF $n images ${w}x$h $* : $(wc -l < $W/ref_$$.txt) / $l lignes, $(cmp $W/ref_$$.txt $W/asm_$$.txt | head -1) ; $(diff $W/ref_$$.txt $W/asm_$$.txt | sed -n 2p) / $(diff $W/ref_$$.txt $W/asm_$$.txt | grep '^>' | head -1)"
	fi
}
run 3000 320 200 playdemo demo1
run 3000 320 200 playdemo demo2
run 2000 320 200 playdemo demo3
run 2000 800 600 playdemo demo1
run 1500 640 480 playdemo demo2
run 1500 320 200 map lq_e0m1
run 1500 800 600 map lq_e0m5
# tir, particules, son ecrete (volume 8)
run 2000 320 200 'volume 8; map lq_e0m1; +attack; +right; +forward'
run 1500 640 480 'volume 8; map lq_e0m3; +attack; +left; +forward'
rm -f $W/ref_$$.txt $W/asm_$$.txt
exit $fail
