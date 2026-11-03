#!/bin/sh
# Le coeur N64 en assembleur contre le coeur C++, sous qemu-aarch64 : memes cycles, registres, RDRAM, images, son,
# listes d'affichage, avec l'interprete puis avec le recompilateur, apres avoir assemble tools/emu/n64_harness.asm.
# Dans WSL, depuis la racine du depot :  sh tools/emu/check_n64.sh images rom...
#   MODES="i j" par defaut ; s : aussi le rendu logiciel et son ecriture en RDRAM, comme l'application (js, ...).
#   -ffp-contract=off : sinon GCC contracte a*b+c en fmadd, l'asm fait fmul puis fadd.
#   -O1 : en -O2, GCC echange parfois les operandes d'une addition ; sur deux NaN, le resultat depend de cet ordre.
W=build/emu
N=$1
shift
S=tools/emu/n64ref
MODES=${MODES:-i j}
[ -x $W/ref_n64 ] || aarch64-linux-gnu-g++ -static -O1 -w -std=gnu++17 -ffp-contract=off -Dprivate=public -I$S tools/emu/ref_n64.cpp $S/n64/*.cpp -o $W/ref_n64 || exit 1
chmod +x $W/n64_harness
fail=0
for r in "$@"; do
	for mode in $MODES; do
		case $mode in *s*) k=s ;; *) k=i ;; esac
		[ -f $W/ref_n$$.$k ] || qemu-aarch64 $W/ref_n64 "$r" $N $k > $W/ref_n$$.$k 2> /dev/null
		qemu-aarch64 $W/n64_harness "$r" $N $mode > $W/asm_n$$.txt
		if cmp -s $W/ref_n$$.$k $W/asm_n$$.txt; then echo "ok  $r $mode ($(wc -l < $W/asm_n$$.txt) images)"
		else
			fail=1
			echo "DIFF $r $mode : $(diff $W/ref_n$$.$k $W/asm_n$$.txt | sed -n 2p) / $(diff $W/ref_n$$.$k $W/asm_n$$.txt | grep '^>' | head -1) ; lignes : $(wc -l < $W/asm_n$$.txt)"
		fi
	done
	rm -f $W/ref_n$$.i $W/ref_n$$.s
done
rm -f $W/asm_n$$.txt
exit $fail
