#!/bin/sh
# build.sh -- les programmes du gestionnaire de paquets par la chaine croisee (WSL), hors du selfhost comme les apps
# C++ de tools/office (OpenSSL n'est pas sous /gcc) :
#   src/bin/pkg -> bin/pkg, src/sys/pkgd -> sys/pkgd, src/apps/pkgman -> apps/pkgman.app
# dans build/office/sdroot, que build.ps1 met sur la carte.
#
#   sh tools/pkg/build.sh [pkg pkgd pkgman]
#
# A lancer depuis la racine du depot, apres tools/gcc/kits.sh (les relais des kits) et tools/openssl/build.sh.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
GCC=$ROOT/build/gcc/sdroot/gcc
OUT=$ROOT/build/office
OBJ=$OUT/obj/pkgtools
[ $# -gt 0 ] || set -- pkg pkgd pkgman setup
mkdir -p "$OBJ" "$OUT/sdroot/bin" "$OUT/sdroot/sys"
sed -E "s#/[^ ]*/build/posix#$ROOT/build/posix#g" "$ROOT/build/posix/lib/posix.specs" > "$OBJ/posix.specs"

FLAGS="-mcpu=cortex-a72 -specs=$OBJ/posix.specs -O2 -g0 -std=gnu++20 -Wno-write-strings -ffunction-sections -fdata-sections \
 -I$ROOT/src/lib/pkg -I$GCC/include/kits -I$ROOT/src/kits/uikit/include -I$ROOT/build/openssl/include"
for p; do
	case $p in
	pkg)	S=$ROOT/src/bin/pkg; D=$OUT/sdroot/bin/pkg; KITS="-larchivekit -lnetkit" ;;
	pkgd)	S=$ROOT/src/sys/pkgd; D=$OUT/sdroot/sys/pkgd; KITS="-larchivekit -lnetkit" ;;
	pkgman)	S=$ROOT/src/apps/pkgman; D=$OUT/sdroot/apps/pkgman.app/main; KITS="-luikit -larchivekit -lnetkit" ;;
	setup)	S=$ROOT/src/sys/setup; D=$OUT/sdroot/sys/setup; KITS="-luikit -larchivekit -lnetkit" ;;
	*)	echo "pkg/build.sh : $p ?" >&2; exit 1 ;;
	esac
	echo "pkg : $p"
	${TC}g++ $FLAGS -c "$S/main.cpp" -o "$OBJ/$p.o"
	${TC}g++ -mcpu=cortex-a72 -specs="$OBJ/posix.specs" -Wl,--gc-sections "$OBJ/$p.o" -L"$GCC/lib" $KITS \
		-L"$ROOT/build/openssl/lib" -lssl -lcrypto -lm -o "$OBJ/$p"
	mkdir -p "$(dirname "$D")"
	${TC}strip -o "$D" "$OBJ/$p"
	[ "$p" != pkgman ] || cp "$S/app.txt" "$OUT/sdroot/apps/pkgman.app/"
	ls -l "$D"
done
