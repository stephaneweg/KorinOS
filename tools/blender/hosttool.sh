#!/bin/sh
# hosttool.sh : l'« émulateur » de la construction croisée de Blender (CMAKE_CROSSCOMPILING_EMULATOR, posé par
# build.sh) : appelé « hosttool.sh DOSSIER_HÔTE OUTIL_KORIN arguments… », il lance à la place l'outil de même nom
# construit pour l'hôte par hosttools.sh.
d=$1
t=$(basename "$2")
shift 2
[ -x "$d/$t" ] || { echo "hosttool.sh : pas de $d/$t (sh tools/blender/hosttools.sh)" >&2; exit 1; }
exec "$d/$t" "$@"
