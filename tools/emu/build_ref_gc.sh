#!/bin/sh
# Construit build/emu/ref_gc : le coeur GameCube de src/apps/gcemu/gc pour qemu-aarch64 (Linux statique).
# Dans WSL, depuis la racine du depot :  sh tools/emu/build_ref_gc.sh [asm]
# asm : build/emu/ref_gc_asm, avec gx_asm.asm a la place de ses equivalents C++. -ffp-contract=off : l'asm fait fmul puis fadd.
W=build/emu
O=$W/gcref$1
mkdir -p $O
S=src/apps/gcemu
CF="-O2 -w -std=gnu++17 -ffp-contract=off -Dprivate=public -I$S -mcpu=cortex-a72"
[ -z "$1" ] || CF="$CF -DGX_ASM"
OUT=$W/ref_gc${1:+_$1}
[ -z "$1" ] || tools/fasmarm/fasmarm.exe $S/gx_asm.asm $O/gx_asm.o | tail -1
for f in $S/gc/*.cpp $S/gxes_rec.cpp tools/emu/ref_gc.cpp; do
	o=$O/$(basename $f .cpp).o
	[ $o -nt $f ] && [ $o -nt $S/gc/gc.h ] && continue
	aarch64-linux-gnu-g++ $CF -c $f -o $o &
done
wait
aarch64-linux-gnu-g++ -static $O/*.o -o $OUT && ls -l $OUT
