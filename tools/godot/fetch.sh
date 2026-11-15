#!/bin/sh
# fetch.sh -- les sources de Godot pour Korin (docs/GODOT.md), hors du depot, dans WSL. git verifie chaque fichier
# par son empreinte : le commit fixe ici vaut somme de controle.
#     sh tools/godot/fetch.sh            -> ~/korin-godot/src
set -eu
GODOT_TAG=4.7.2-stable
GODOT_COMMIT=ed1daf0bf001b61586d9930840f2f1394092c079
W=${KORIN_GODOT:-$HOME/korin-godot}
S=$W/src
mkdir -p "$W"
if [ ! -d "$S/.git" ]; then
	git -c advice.detachedHead=false clone -q --depth 1 --branch "$GODOT_TAG" https://github.com/godotengine/godot "$S"
fi
[ "$(git -C "$S" rev-parse HEAD)" = "$GODOT_COMMIT" ] || { echo "fetch.sh : $S n'est pas a $GODOT_COMMIT" >&2; exit 1; }
echo "Godot $GODOT_TAG dans $S"
