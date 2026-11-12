#!/bin/sh
# Lance le banc différentiel d'AudioKit (voir cmp.h), dans WSL, depuis la racine du dépôt :
#   sh src/kits/audiokit/test/cmp/run.sh NOM...
# 1) aarch64 (qemu-aarch64) : asm contre C ; 2) x86-64 : C, comparé au C d'aarch64.
# Sorties : build/audiokit_cmp/out_NOM_{a64,x86}.txt ; code 0 si tout est égal.
cd "$(dirname "$0")/../../../../.."
OUT=build/audiokit_cmp
rc=0
for n in "$@"; do
	timeout 1200 qemu-aarch64 $OUT/a64/cmp_$n > $OUT/out_${n}_a64.txt 2>&1
	ra=$?
	timeout 1200 $OUT/x86/cmp_$n > $OUT/out_${n}_x86.txt 2>&1
	rx=$?
	grep -v '^!!\|^== .* écarts$\|^== .* (C seulement)$' $OUT/out_${n}_a64.txt > $OUT/out_${n}_a64.c.txt
	grep -v '^== .* (C seulement)$' $OUT/out_${n}_x86.txt > $OUT/out_${n}_x86.c.txt
	nd=$(grep -c '^!!' $OUT/out_${n}_a64.txt)
	if cmp -s $OUT/out_${n}_a64.c.txt $OUT/out_${n}_x86.c.txt; then px="x86-64 = aarch64"; else px="x86-64 DIFFÉRENT"; rc=1; fi
	[ $ra = 0 ] && [ $nd = 0 ] || rc=1
	echo "$n : aarch64 code $ra, $nd lignes d'écart asm/C ; x86-64 code $rx, $px ($(grep -c . $OUT/out_${n}_a64.c.txt) lignes)"
	[ $nd = 0 ] || grep '^!!' $OUT/out_${n}_a64.txt | head -20
	[ "$px" = "x86-64 = aarch64" ] || diff $OUT/out_${n}_a64.c.txt $OUT/out_${n}_x86.c.txt | head -10
done
exit $rc
