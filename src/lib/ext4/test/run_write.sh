#!/bin/sh
# run_write.sh : ext4w (src/lib/ext4/ext4w.c) jugé sous Linux (WSL), depuis la racine du dépôt :
#     sh src/lib/ext4/test/run_write.sh
# Des séries d'opérations au hasard (writetest : relu et comparé à un modèle), sur des images de mkext4 et de
# mke2fs (avec un arbre déjà là, blocs de 1 Kio, sans flex_bg, un grand volume aux groupes BLOCK_UNINIT), puis
# e2fsck -fn : aucune erreur. Un volume que ext4w refuse (gdt_csum sans metadata_csum) doit rester intact.
set -e
D=$(mktemp -d)
CF="-std=gnu11 -O2 -Wall -Wextra -Werror"
gcc $CF -Wno-format-truncation -o "$D/wt" src/lib/ext4/test/writetest.c src/lib/ext4/ext4w.c src/lib/ext4/ext4r.c src/lib/ext4/mkext4.c
gcc $CF -o "$D/mk" src/lib/ext4/test/hosttest.c src/lib/ext4/mkext4.c
python3 -I src/lib/ext4/test/mktree.py "$D/tree" "$D/expected.txt" --no-links
fsck() { # nom
	if e2fsck -fn "$D/v.img" >"$D/fsck.txt" 2>&1; then
		echo "ok : $1 : $(cat "$D/wt.txt") ; e2fsck : $(grep -o '[0-9/]* files.*blocks' "$D/fsck.txt")"
	else
		cat "$D/wt.txt"
		head -40 "$D/fsck.txt"
		echo "ECHEC : $1 (e2fsck)"
		exit 1
	fi
}
run() { # nom, graine, opérations, [gros Mo]
	if ! "$D/wt" "$D/v.img" "$2" "$3" ${4:-} >"$D/wt.txt"; then
		cat "$D/wt.txt"
		echo "ECHEC : $1"
		exit 1
	fi
	fsck "$1"
}
mke() { # taille, options de mke2fs
	rm -f "$D/v.img"
	s=$1
	shift
	mke2fs -q -F -L TEST "$@" "$D/v.img" "$s" 2>/dev/null
}
for seed in 1 2 3; do
	rm -f "$D/v.img"
	truncate -s 200M "$D/v.img"
	"$D/mk" "$D/v.img" >/dev/null
	run "mkext4 200M, graine $seed" $seed 600
done
mke 160M -t ext4 -d "$D/tree"
run "mke2fs -d (arbre déjà là)" 4 600
"$D/wt" "$D/v.img" 5 300 >"$D/wt.txt" && fsck "le même, une seconde fois"
mke 100M -t ext4 -b 1024
run "mke2fs blocs de 1 Kio" 6 500
mke 160M -t ext4 -O ^flex_bg
run "mke2fs sans flex_bg" 7 500
mke 160M -t ext4 -O ^64bit -I 128
run "mke2fs sans 64bit, inodes de 128 o" 8 400
mke 2G -t ext4
run "mke2fs 2 Gio (BLOCK_UNINIT), un fichier de 600 Mo" 9 300 600
mke 160M -t ext4 -O ^metadata_csum
run "mke2fs sans sommes de contrôle" 11 400
mke 64M -t ext4 -O ^metadata_csum,uninit_bg
cp "$D/v.img" "$D/v0.img"
"$D/wt" "$D/v.img" 10 10 >"$D/wt.txt" || true
if grep -q "lecture seule : -30" "$D/wt.txt" && cmp -s "$D/v.img" "$D/v0.img"; then
	echo "ok : gdt_csum sans metadata_csum : refusé, intact"
else
	cat "$D/wt.txt"
	echo "ECHEC : gdt_csum"
	exit 1
fi
rm -rf "$D"
echo "tout est bon"
