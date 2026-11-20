#!/bin/sh
# host.sh -- win16 construit pour l'hôte (WSL, gcc de Linux) et lancé sur un programme, sans Korin : pour mettre
# au point le processeur, le chargeur, USER et GDI (tools/win16/host_be.c remplace l'affichage).
#
#   sh tools/win16/host.sh build/win16/tests/hello.exe          (W16_TRACE=1 : appels de l'API ; 2 : instructions)
#   W16_SHOT=/tmp sh tools/win16/host.sh …                      (une image PPM de chaque fenêtre à sa fermeture)
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=$ROOT/src/apps/win16
B=$ROOT/build/win16/host
mkdir -p "$B"
OBJS=""
for f in "$S"/*.c "$S"/cpu.cpp "$ROOT/tools/win16/host_be.c"; do
	n=$(basename "$f" | sed 's/\.[a-z]*$//')
	[ "$n" != be_uikit ] || continue
	o=$B/$n.o
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$S/w16.h" -nt "$o" ] || [ "$S/user.h" -nt "$o" ]; then
		case $f in
		*.cpp) g++ -std=gnu++20 -O2 -g -w -DW16_HOST -I"$S" -c "$f" -o "$o" ;;
		*) gcc -O1 -g -Wall -Wno-unused-function -DW16_HOST -I"$S" -c "$f" -o "$o" ;;
		esac
	fi
	OBJS="$OBJS $o"
done
g++ -o "$B/win16" $OBJS -lm
[ $# -gt 0 ] || exit 0
exec "$B/win16" "$@"
