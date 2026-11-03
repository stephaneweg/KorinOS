#!/bin/sh
# Le temps par image du C++ et de l'asm sous qemu-aarch64, sans sommes ni sorties (WSL, racine du depot) :
#     sh tools/emu/time_snes.sh images rom
# Le C++ : memes options que tools/emu/build.sh (-O3 -mcpu=cortex-a72). QEMU ne dit rien de la vitesse
# du Pi : c'est une comparaison entre les deux, pas une mesure de la vitesse reelle.
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
N=$1
[ -x $W/ref_gba_a64 ] || aarch64-linux-gnu-g++ -O3 -mcpu=cortex-a72 -w -static -Dprivate=public -I$ONYX tools/emu/ref_gba.cpp $ONYX/gba/gba.cpp $ONYX/gba/gba_cpu.cpp $ONYX/gba/gba_ppu.cpp $ONYX/gba/gba_apu.cpp $ONYX/gba/gba_bios.cpp -o $W/ref_gba_a64 || exit 1
chmod +x $W/gba_harness
mesure () { # nom, commande...
	n=$1; shift
	t0=$(date +%s.%N)
	"$@" > /dev/null
	t1=$(date +%s.%N)
	echo "$n : $(echo "($t1 - $t0) * 1000 / $N" | bc -l | cut -c1-7) ms par image"
}
for i in 1 2 3; do
	mesure "C++ " qemu-aarch64 $W/ref_gba_a64 "$2" $N time
	mesure "asm " qemu-aarch64 $W/gba_harness "$2" $N time
done
