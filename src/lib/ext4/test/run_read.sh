#!/bin/sh
# run_read.sh : ext4r (src/lib/ext4/ext4r.c) jugé sous Linux (WSL), depuis la racine du dépôt :
#     sh src/lib/ext4/test/run_read.sh
# Un arbre d'essai (mktree.py), mis dans des images par mke2fs -d (ext4 en blocs de 4 Kio, 1 Kio et 64 Kio,
# inline_data, ext3, ext2), puis relu par readtest : l'arbre entier doit être identique (noms, tailles, crc32,
# liens), et des chemins sont cherchés (casse, liens suivis, « .. »). Enfin une image de mkext4 (vide).
set -e
D=$(mktemp -d)
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o "$D/rt" src/lib/ext4/test/readtest.c src/lib/ext4/ext4r.c
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o "$D/mk" src/lib/ext4/test/hosttest.c src/lib/ext4/mkext4.c
python3 -I src/lib/ext4/test/mktree.py "$D/tree" "$D/expected.txt"
check() { # nom, options de mke2fs
	n=$1
	shift
	rm -f "$D/v.img"
	mke2fs -q -F -L TEST "$@" -d "$D/tree" "$D/v.img" $SIZE 2>"$D/mke2fs.txt" || { cat "$D/mke2fs.txt"; exit 1; }
	"$D/rt" "$D/v.img" 2>"$D/info.txt" | sort >"$D/got.txt"
	if cmp -s "$D/got.txt" "$D/expected.txt"; then
		echo "ok : $n : $(cat "$D/info.txt")"
	else
		diff "$D/expected.txt" "$D/got.txt" | head -20
		echo "ECHEC : $n"
		exit 1
	fi
	"$D/rt" "$D/v.img" "a/b/c/d/e/f/profond.txt" "CASSE/fichier.txt" "lien_dossier/d/e/f/profond.txt" \
		"lien_long_qui_depasse_soixante_octets" "Casse/lien_absolu/un fichier.txt" "a/b/../b/c/./d" \
		"Été à Noël/café crème.txt" "absent" "tailles/f1.bin/x" 2>/dev/null >"$D/look.txt"
	if ! grep -q "profond.txt -> F 13" "$D/look.txt" || ! grep -q "CASSE/fichier.txt -> F 6" "$D/look.txt" ||
		! grep -q "lien_dossier/d/e/f/profond.txt -> F 13" "$D/look.txt" ||
		! grep -q "soixante_octets -> F 13" "$D/look.txt" || ! grep -q "lien_absolu/un fichier.txt -> F 8" "$D/look.txt" ||
		! grep -q "a/b/../b/c/./d -> D" "$D/look.txt" || ! grep -q "crème.txt -> F 22" "$D/look.txt" ||
		! grep -q "absent -> erreur -2" "$D/look.txt" || ! grep -q "f1.bin/x -> erreur -20" "$D/look.txt"; then
		cat "$D/look.txt"
		echo "ECHEC : $n (chemins)"
		exit 1
	fi
}
SIZE=160M
check "ext4 4K" -t ext4
check "ext4 1K" -t ext4 -b 1024
SIZE=512M
check "ext4 64K" -t ext4 -b 65536
SIZE=160M
check "ext4 inline_data" -t ext4 -O inline_data
check "ext4 sans 64bit, 128 o" -t ext4 -O ^64bit -I 128
check "ext3" -t ext3
check "ext2" -t ext2
rm -f "$D/v.img"
truncate -s 64M "$D/v.img"
"$D/mk" "$D/v.img" >/dev/null
"$D/rt" "$D/v.img" 2>/dev/null >"$D/got.txt"
[ ! -s "$D/got.txt" ] && echo "ok : mkext4 (vide)" || { cat "$D/got.txt"; echo "ECHEC : mkext4"; exit 1; }
rm -rf "$D"
echo "tout est bon"
