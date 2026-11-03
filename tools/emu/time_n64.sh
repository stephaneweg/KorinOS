#!/bin/sh
# Le temps par image du C++ et de l'asm sous qemu-aarch64, sans sommes ni sorties (WSL, racine du depot) :
#     sh tools/emu/time_n64.sh images rom [i|j]
# Le C++ : -O3 -mcpu=cortex-a72, comme les apps C++. QEMU ne dit rien de la vitesse du Pi : c'est une
# comparaison entre les deux, pas une mesure de la vitesse reelle. Un recompilateur l'est moins encore :
# QEMU traduit a son tour le code qu'il produit.
W=build/emu
N=$1
M=${3:-j}t
S=tools/emu/n64ref
[ -x $W/ref_n64_a64 ] || aarch64-linux-gnu-g++ -O3 -mcpu=cortex-a72 -w -static -std=gnu++17 -ffp-contract=off -Dprivate=public -I$S tools/emu/ref_n64.cpp $S/n64/*.cpp -o $W/ref_n64_a64 || exit 1
chmod +x $W/n64_harness
mesure () { # nom, commande...
	n=$1; shift
	t0=$(date +%s.%N)
	"$@" > /dev/null 2>&1
	t1=$(date +%s.%N)
	echo "$n : $(echo "($t1 - $t0) * 1000 / $N" | bc -l | cut -c1-7) ms par image"
}
for i in 1 2 3; do
	mesure "C++ " qemu-aarch64 $W/ref_n64_a64 "$2" $N $M
	mesure "asm " qemu-aarch64 $W/n64_harness "$2" $N $M
done
