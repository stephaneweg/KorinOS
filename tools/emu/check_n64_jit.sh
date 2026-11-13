#!/bin/sh
# Le recompilateur R4300 et ses deux generateurs (src/apps/n64emu/n64/n64_jit*.{cpp,h}) contre l'interprete, sur des
# ROM d'essai (tools/emu/romgen.py n64-stress), dans WSL depuis la racine du depot :
#     sh tools/emu/check_n64_jit.sh [images] [graines...]
#   * x86-64 : le coeur compile pour Linux et lance tel quel ; interprete (i) et blocs (j) : memes sorties par image
#     (cycles, registres, RDRAM, image, son, listes d'affichage).
#   * AArch64 : sous qemu-aarch64, i et j de meme ; puis l'ancien recompilateur d'un seul tenant (tools/emu/n64ref) :
#     memes sorties, et la meme somme du code produit (le generateur AArch64 ecrit les memes octets).
#   -O1 -ffp-contract=off comme tools/emu/check_n64.sh (l'ordre des operandes d'une addition sur deux NaN).
set -u
W=build/emu/n64jit
N=${1:-300}
[ $# -gt 0 ] && shift
SEEDS=${*:-1 2 3 4 5}
NEW=src/apps/n64emu
OLD=tools/emu/n64ref
mkdir -p $W
F="-O1 -w -std=gnu++17 -ffp-contract=off -Dprivate=public"
g++ -static $F -I$NEW tools/emu/ref_n64.cpp $NEW/n64/*.cpp -o $W/ref_x64 || exit 1
aarch64-linux-gnu-g++ -static $F -I$NEW tools/emu/ref_n64.cpp $NEW/n64/*.cpp -o $W/ref_a64 || exit 1
# l'ancien, avec la somme de son code (J y est statique : la fonction est ajoutee a son fichier)
{ cat $OLD/n64/n64_jit.cpp; printf '%s\n' 'namespace n64 { unsigned long n64_jit_code_hash () { unsigned long h = 1469598103934665603ul;' \
	'for (unsigned char *p = (unsigned char *) J.code; p && p < (unsigned char *) J.cp; p++) h = (h ^ *p) * 1099511628211ul; return h; } }'; } > $W/old_jit.cpp
aarch64-linux-gnu-g++ -static $F -I$OLD -I$OLD/n64 tools/emu/ref_n64.cpp $(ls $OLD/n64/*.cpp | grep -v n64_jit.cpp) $W/old_jit.cpp -o $W/ref_old || exit 1
fail=0
for s in $SEEDS; do
	R=$W/s$s.z64
	python3 -I tools/emu/romgen.py n64-stress $s $R
	./$W/ref_x64 $R $N i > $W/xi.txt 2> $W/xi.err
	./$W/ref_x64 $R $N j > $W/xj.txt 2> $W/xj.err
	qemu-aarch64 $W/ref_a64 $R $N i > $W/ai.txt 2> $W/ai.err
	qemu-aarch64 $W/ref_a64 $R $N j > $W/aj.txt 2> $W/aj.err
	qemu-aarch64 $W/ref_old $R $N j > $W/oj.txt 2> $W/oj.err
	r="graine $s :"
	if cmp -s $W/xi.txt $W/xj.txt; then r="$r x86-64 i=j ($(wc -l < $W/xj.txt) images, $(sed 's/.*s, \([0-9]*\) blocs.*/\1/' $W/xj.err) blocs)"
	else fail=1; r="$r x86-64 DIFF ($(diff $W/xi.txt $W/xj.txt | sed -n 2p))"; fi
	if cmp -s $W/ai.txt $W/aj.txt; then r="$r, AArch64 i=j"; else fail=1; r="$r, AArch64 DIFF"; fi
	if cmp -s $W/aj.txt $W/oj.txt; then r="$r, = ancien"; else fail=1; r="$r, ancien DIFF"; fi
	hn=$(sed 's/.*code //' $W/aj.err); ho=$(sed 's/.*code //' $W/oj.err)
	if [ "$hn" = "$ho" ]; then r="$r, code A64 identique ($hn)"; else fail=1; r="$r, code A64 DIFFERENT ($hn / $ho)"; fi
	if cmp -s $W/xi.txt $W/ai.txt; then r="$r, x86-64 = AArch64"; else r="$r, (x86-64 et AArch64 different : $(diff $W/xi.txt $W/ai.txt | sed -n 2p | cut -c1-60))"; fi
	echo "$r"
done
exit $fail
