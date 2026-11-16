#!/bin/sh
# run.sh : mkexfat (src/lib/exfat) jugé par fsck.exfat sous Linux (WSL, exfatprogs), depuis la racine du dépôt :
#     sh src/lib/exfat/test/run.sh
# Formate des images de plusieurs tailles (clusters par défaut : 4, 32 et 128 Kio ; puis des clusters imposés),
# puis fsck.exfat -n (aucune erreur) et l'étiquette relue par exfatlabel.
set -e
D=$(mktemp -d)
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o "$D/mk" src/lib/exfat/test/hosttest.c src/lib/exfat/mkexfat.c
check() { # taille [cluster]
	rm -f "$D/v.img"
	truncate -s "$1" "$D/v.img"
	"$D/mk" "$D/v.img" $2 >"$D/mk.txt"
	if fsck.exfat -n "$D/v.img" >"$D/fsck.txt" 2>&1; then
		echo "ok : $1 ${2:-} : $(cat "$D/mk.txt")"
	else
		cat "$D/mk.txt" "$D/fsck.txt"
		echo "ECHEC : $1 ${2:-}"
		exit 1
	fi
}
for size in 8M 100M 300M 1G 40G 600G; do check $size; done
check 100M 512
check 1G 1048576
check 2G 4194304
exfatlabel "$D/v.img" 2>/dev/null || true
rm -rf "$D"
echo "tout est bon"
