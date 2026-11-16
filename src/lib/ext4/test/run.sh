#!/bin/sh
# run.sh : mkext4 (src/lib/ext4) jugé par e2fsck sous Linux (WSL), depuis la racine du dépôt :
#     sh src/lib/ext4/test/run.sh
# Formate des images de plusieurs tailles (petite : journal dans le groupe 0 ; moyennes ; 600 Gio creuse, comme
# le disque du ZBook), puis e2fsck -fn (aucune erreur), dumpe2fs -h, et debugfs (la racine, lost+found).
set -e
D=$(mktemp -d)
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o "$D/mk" src/lib/ext4/test/hosttest.c src/lib/ext4/mkext4.c
for size in 16M 100M 1G 5G 600G; do
	rm -f "$D/v.img"
	truncate -s $size "$D/v.img"
	"$D/mk" "$D/v.img" >/dev/null
	if e2fsck -fn "$D/v.img" >"$D/fsck.txt" 2>&1; then
		echo "ok : $size : $(grep -o '[0-9/]* files, [0-9/]* blocks' "$D/fsck.txt")"
	else
		cat "$D/fsck.txt"
		echo "ECHEC : $size"
		exit 1
	fi
done
dumpe2fs -h "$D/v.img" 2>/dev/null | grep -E "features|Inode count|Block count|Journal size|checksum type"
debugfs -R "ls -l /" "$D/v.img" 2>/dev/null | head -5
rm -rf "$D"
echo "tout est bon"
