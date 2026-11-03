#!/bin/sh
# Le coeur NES en assembleur contre le coeur C++ d'Onyx : memes images, meme son, memes cycles, sur
# des ROM au hasard (tools/emu/romgen.py). Dans WSL, depuis la racine du depot, apres avoir assemble
# tools/emu/nes_harness.asm en build/emu/nes_harness.
set -u
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
FRAMES=${FRAMES:-400}
mkdir -p $W
[ -x $W/ref_nes ] || g++ -O2 -w -I$ONYX tools/emu/ref_nes.cpp $ONYX/nes/nes.cpp $ONYX/nes/nes_ppu.cpp $ONYX/nes/nes_apu.cpp -o $W/ref_nes || exit 1
fail=0
run () { # nom, fichier, options
	./$W/ref_nes "$2" $FRAMES $3 > $W/ref.txt
	qemu-aarch64 $W/nes_harness "$2" $FRAMES $3 > $W/asm.txt
	if cmp -s $W/ref.txt $W/asm.txt; then echo "ok  $1"
	else
		fail=1
		echo "DIFF $1 : $(diff $W/ref.txt $W/asm.txt | sed -n 2p) / $(diff $W/ref.txt $W/asm.txt | grep '^>' | head -1)"
	fi
}
for m in 0 1 2 3 4 7 66; do
	for s in ${SEEDS:-1 2 3}; do
		python3 -I tools/emu/romgen.py nes-stress $m $s $W/s.nes
		run "mapper $m graine $s" $W/s.nes ""
		python3 -I tools/emu/romgen.py nes-stress $m $s $W/s.nes chr-ram
		run "mapper $m graine $s chr-ram" $W/s.nes ""
	done
done
python3 -I tools/emu/romgen.py nes-stress 4 9 $W/s.nes pal
run "PAL" $W/s.nes "pal"
python3 -I tools/emu/romgen.py nes-demo $W/demo.nes
run "demo" $W/demo.nes ""
exit $fail
