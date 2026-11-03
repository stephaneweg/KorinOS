#!/bin/sh
# Le temps par image du C++ et de l'asm sous qemu-aarch64, sans sommes ni sorties (WSL, racine du depot) :
#     sh tools/emu/time_snes.sh images rom
# Le C++ : memes options que tools/emu/build.sh (-O3 -mcpu=cortex-a72). QEMU ne dit rien de la vitesse
# du Pi : c'est une comparaison entre les deux, pas une mesure de la vitesse reelle.
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/user/Emulators}
W=build/emu
N=$1
[ -x $W/ref_snes_a64 ] || aarch64-linux-gnu-g++ -O3 -mcpu=cortex-a72 -w -static -Dprivate=public -I$ONYX tools/emu/ref_snes.cpp $ONYX/snes/snes.cpp $ONYX/snes/snes_cpu.cpp $ONYX/snes/snes_ppu.cpp $ONYX/snes/snes_apu.cpp -o $W/ref_snes_a64 || exit 1
chmod +x $W/snes_harness
mesure () { # nom, commande...
	n=$1; shift
	t0=$(date +%s.%N)
	"$@" > /dev/null
	t1=$(date +%s.%N)
	echo "$n : $(echo "($t1 - $t0) * 1000 / $N" | bc -l | cut -c1-7) ms par image"
}
for i in 1 2 3; do
	mesure "C++ " qemu-aarch64 $W/ref_snes_a64 "$2" $N time
	mesure "asm " qemu-aarch64 $W/snes_harness "$2" $N time
done
