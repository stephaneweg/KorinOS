#!/bin/sh
# Le temps par image du C et de l'asm, sous qemu-aarch64 : le meilleur de 3 passages. QEMU ne dit rien de la vitesse du
# Pi : il ne compare que les deux entre eux. Dans WSL, depuis la racine du depot, apres mk_harness.sh (OPT=-O3 comme l'app).
#   sh tools/quake/time.sh [dossier des donnees]
W=build/quake
D=${1:-$W/data}
best () { # binaire images largeur hauteur commande
	b=$1; shift
	for i in 1 2 3; do qemu-aarch64 $W/$b t "$D" "$@" 2>/dev/null | tail -1; done | sort -n | head -1
}
for spec in "2000 320 200 demo1" "2000 320 200 demo2" "1500 800 600 demo1" "1500 800 600 demo2"; do
	set -- $spec
	echo "$2 x $3 $4 ($1 images) : C $(best ref_quake $1 $2 $3 playdemo $4), asm $(best asm_quake $1 $2 $3 playdemo $4)"
done
