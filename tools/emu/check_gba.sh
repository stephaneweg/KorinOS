#!/bin/sh
# Le coeur GBA en assembleur contre le coeur C++ d'Onyx : memes images, meme son, memes cycles, meme
# etat, sur des ROM donnees (WSL, racine du depot), apres avoir assemble tools/emu/gba_harness.asm :
#     sh tools/emu/check_gba.sh images rom...
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
N=$1
shift
[ -x $W/ref_gba ] || g++ -O2 -w -Dprivate=public -I$ONYX tools/emu/ref_gba.cpp $ONYX/gba/gba.cpp $ONYX/gba/gba_cpu.cpp $ONYX/gba/gba_ppu.cpp $ONYX/gba/gba_apu.cpp $ONYX/gba/gba_bios.cpp -o $W/ref_gba || exit 1
chmod +x $W/gba_harness
fail=0
for r in "$@"; do
	./$W/ref_gba "$r" $N > $W/ref_a$$.txt
	qemu-aarch64 $W/gba_harness "$r" $N > $W/asm_a$$.txt
	if cmp -s $W/ref_a$$.txt $W/asm_a$$.txt; then echo "ok  $r ($(wc -l < $W/asm_a$$.txt) lignes)"
	else
		fail=1
		echo "DIFF $r : $(diff $W/ref_a$$.txt $W/asm_a$$.txt | sed -n 2p) / $(diff $W/ref_a$$.txt $W/asm_a$$.txt | grep '^>' | head -1)"
	fi
done
rm -f $W/ref_a$$.txt $W/asm_a$$.txt
exit $fail
