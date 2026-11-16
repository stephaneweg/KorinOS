#!/bin/sh
# run.sh : la bibliothèque GPT (src/lib/gpt) jugée par sgdisk sous Linux (WSL), depuis la racine du dépôt :
#     sh src/lib/gpt/test/run.sh
# 1. une table neuve sur une image de 1 Gio ; 2. une partition ajoutée à une table de sgdisk (l'imitation du ZBook
# de tools/pc/mkkorindisk.py, si build/pc/installtarget.img existe) ; 3. l'en-tête principal abîmé : la copie de
# secours doit servir. sgdisk -v ne doit trouver aucun problème.
set -e
D=$(mktemp -d)
gcc -std=gnu11 -O2 -Wall -Wextra -Werror -o "$D/hosttest" src/lib/gpt/test/hosttest.c src/lib/gpt/gpt.c
ok() { sgdisk -v "$1" | grep -q "No problems found" || { sgdisk -v "$1"; echo "ECHEC : $2"; exit 1; }; echo "ok : $2"; }
truncate -s 1G "$D/a.img"
"$D/hosttest" new "$D/a.img"
ok "$D/a.img" "table neuve"
sgdisk -p "$D/a.img" | tail -4
"$D/hosttest" add "$D/a.img" >/dev/null
ok "$D/a.img" "partition ajoutée"
if [ -f build/pc/installtarget.img ]; then
	cp build/pc/installtarget.img "$D/z.img"
	"$D/hosttest" show "$D/z.img"
	"$D/hosttest" add "$D/z.img" | tail -2
	ok "$D/z.img" "ajout dans la table du faux ZBook"
fi
"$D/hosttest" breakprimary "$D/a.img" | head -1
sgdisk -v "$D/a.img" | grep -qi "header" && echo "ok : sgdisk voit l'en-tête abîmé"
rm -rf "$D"
echo "tout est bon"
