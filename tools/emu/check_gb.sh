#!/bin/sh
# Le coeur Game Boy en assembleur contre le coeur C++ d'Onyx, sur des ROM donnees (WSL, racine du depot) :
#     sh tools/emu/check_gb.sh images rom...
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
N=$1
shift
[ -x $W/ref_gb ] || g++ -O2 -w -I$ONYX tools/emu/ref_gb.cpp $ONYX/gb/gb.cpp -o $W/ref_gb || exit 1
chmod +x $W/gb_harness
for r in "$@"; do
	./$W/ref_gb "$r" $N > $W/ref.txt
	qemu-aarch64 $W/gb_harness "$r" $N > $W/asm.txt
	if cmp -s $W/ref.txt $W/asm.txt; then echo "ok  $r"
	else echo "DIFF $r : $(diff $W/ref.txt $W/asm.txt | sed -n 2p) / $(diff $W/ref.txt $W/asm.txt | grep '^>' | head -1)"; fi
done
