#!/bin/sh
# gx_asm.asm contre ses equivalents C++, sous qemu-aarch64 : memes sommes par image (enregistreur : sommets, lots,
# uniformes, textures ; machine : VI, cycles, compteurs du GX, son). Le coeur Gekko et le recompilateur restent en C++.
# Dans WSL, depuis la racine du depot :  sh tools/emu/check_gc.sh trames pas iso...
#   GC_PAD, GC_CARD : comme ref_gc. Les deux binaires viennent de build_ref_gc.sh et build_ref_gc.sh asm.
W=build/emu
N=$1; P=$2
shift 2
sh tools/emu/build_ref_gc.sh > /dev/null || exit 1
sh tools/emu/build_ref_gc.sh asm > /dev/null || exit 1
fail=0
for r in "$@"; do
	GC_MEM=0 GC_REC=1 qemu-aarch64 $W/ref_gc "$r" $N $P 2> /dev/null > $W/gcc$$.txt &
	GC_MEM=0 GC_REC=1 qemu-aarch64 $W/ref_gc_asm "$r" $N $P 2> /dev/null > $W/gca$$.txt &
	wait
	if cmp -s $W/gcc$$.txt $W/gca$$.txt; then echo "ok  $r ($(grep -c '^  rec' $W/gca$$.txt) points, $(tail -1 $W/gca$$.txt))"
	else fail=1; echo "DIFF $r : $(diff $W/gcc$$.txt $W/gca$$.txt | sed -n 2p | cut -c1-150) / $(diff $W/gcc$$.txt $W/gca$$.txt | grep '^>' | head -1 | cut -c1-150)"; fi
done
rm -f $W/gcc$$.txt $W/gca$$.txt
exit $fail
