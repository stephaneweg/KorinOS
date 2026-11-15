#!/bin/sh
# build.sh -- les programmes du gestionnaire de paquets par la chaine croisee (WSL), hors du selfhost comme les apps
# C++ de tools/office (OpenSSL n'est pas sous /devtools/gcc) :
#   src/bin/pkg -> bin/pkg, src/sys/pkgd -> sys/pkgd, src/apps/pkgman -> apps/pkgman.app
# dans build/office/sdroot, que build.ps1 met sur la carte.
#
#   sh tools/pkg/build.sh [pkg pkgd pkgman]
#
# A lancer depuis la racine du depot, apres tools/gcc/kits.sh (les relais des kits) et tools/openssl/build.sh.
# Le PC (ARCH=x86_64 ou BOARD=pc) : la chaine x86_64-onyx-elf et libposix du PC, par tools/pc/apps.sh (bin/pkg,
# sys/pkgd, apps/pkgman.app dans build/pc/apps/sdroot, que tools/pc/sysroot_apps.sh met dans l'arbre du PC).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
if [ "${ARCH:-}" = x86_64 ] || [ "${BOARD:-}" = pc ]; then
	[ $# -gt 0 ] || set -- pkg pkgd pkgman
	exec sh "$ROOT/tools/pc/apps.sh" "$@"
fi
TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
GCC=$ROOT/build/gcc/sdroot/gcc
OUT=${PKG_OUT:-$ROOT/build/office}	# (PKG_OUT : un autre dossier, pour un essai)
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
