#!/bin/sh
# Le coeur SNES en assembleur contre le coeur C++ d'Onyx : memes images, meme son, meme horloge, meme
# etat, sur des ROM donnees (WSL, racine du depot), apres avoir assemble tools/emu/snes_harness.asm :
#     sh tools/emu/check_snes.sh images rom...
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
N=$1
shift
[ -x $W/ref_snes ] || g++ -O2 -w -Dprivate=public -I$ONYX tools/emu/ref_snes.cpp $ONYX/snes/snes.cpp $ONYX/snes/snes_cpu.cpp $ONYX/snes/snes_ppu.cpp $ONYX/snes/snes_apu.cpp -o $W/ref_snes || exit 1
chmod +x $W/snes_harness
fail=0
for r in "$@"; do
	./$W/ref_snes "$r" $N > $W/ref_s$$.txt
	qemu-aarch64 $W/snes_harness "$r" $N > $W/asm_s$$.txt
	if cmp -s $W/ref_s$$.txt $W/asm_s$$.txt; then echo "ok  $r ($(wc -l < $W/asm_s$$.txt) lignes)"
	else
		fail=1
		echo "DIFF $r : $(diff $W/ref_s$$.txt $W/asm_s$$.txt | sed -n 2p) / $(diff $W/ref_s$$.txt $W/asm_s$$.txt | grep '^>' | head -1)"
	fi
done
rm -f $W/ref_s$$.txt $W/asm_s$$.txt
exit $fail
