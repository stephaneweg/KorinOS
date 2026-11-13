#!/bin/sh
# kits : les kits d'ARMPI depuis le C, sous /gcc (docs/GCC.md) : pour chaque kit, ses relais dans
# /gcc/lib/lib<kit>.a et son en-tete dans /gcc/include/kits. Les interfaces sont generees sur le PC
# depuis les .abi (tools/kits/abi2c.py : ARMPI n'a pas Python). A lancer dans WSL depuis la racine du
# depot, apres tools/gcc/build.sh :
#     sh tools/gcc/kits.sh
# Sous ARMPI :
#     gcc -I/gcc/include/kits app.c -luikit -o app
#     fbc app.bas                       (#include "kits/uikit.bi" : la bibliotheque suit, par #inclib)
# AppKit n'y est pas : libposix le prend deja, et ses noms (puts, malloc) sont ceux de la libc.
# ARCH=x86_64 : pour Korin sur PC, dans build/pc/gccroot/gcc (relais x86-64 : abi2c.py --arch x86_64).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCH=${ARCH:-aarch64}
case $ARCH in
aarch64)
	TARGET=aarch64-onyx-elf
	OUT=$ROOT/build/gcc/sdroot/gcc
	SPECS=$ROOT/build/posix/lib/posix.specs
	WORK=${WORK:-$HOME/.cache/armpi-gcc}/kits
	CPUFLAG=-mcpu=cortex-a72
	ABIARCH=""
	;;
x86_64)
	TARGET=x86_64-onyx-elf
	OUT=$ROOT/build/pc/gccroot/gcc
	SPECS=$ROOT/build/pc/posix/lib/posix.specs
	WORK=${WORK:-$HOME/.cache/korin-gcc-x86_64}/kits
	CPUFLAG=""
	ABIARCH="--arch x86_64"
	;;
*) echo "kits : ARCH=aarch64 ou x86_64" >&2; exit 1 ;;
esac
TOOL=${TOOL:-/opt/toolchains/$TARGET-14.2}
TC=$TOOL/bin/$TARGET-
INC=$OUT/include/kits
BI=$OUT/include/freebasic/kits		# (les memes interfaces pour FreeBASIC : #include "kits/uikit.bi")
[ -d "$OUT/include" ] || { echo "kits : lancer d'abord tools/gcc/build.sh" >&2; exit 1; }
mkdir -p "$INC"
K=$ROOT/src/kits
A=$ROOT/src/inc
rm -rf "$WORK"
mkdir -p "$WORK" "$OUT/lib" "$BI"

kit () { # nom, puis les .inc des constantes
	n=$1
	shift
	[ -f "$K/$n/$n.abi" ] || return 0
	python3 -I "$ROOT/tools/kits/abi2c.py" "$K/$n/$n.abi" "$WORK" $ABIARCH --const "$@"
	python3 -I "$ROOT/tools/kits/abi2bi.py" "$K/$n/$n.abi" "$BI" --const "$@"
	${TC}gcc $CPUFLAG -c "$WORK/${n}_abi.S" -o "$WORK/${n}_abi.o"
	${TC}gcc $CPUFLAG -O2 -g0 -specs="$SPECS" -c "$WORK/${n}_abi_open.c" -o "$WORK/${n}_abi_open.o"
	rm -f "$OUT/lib/lib$n.a"
	${TC}ar rcs "$OUT/lib/lib$n.a" "$WORK/${n}_abi.o" "$WORK/${n}_abi_open.o"
	cp "$WORK/${n}_abi.h" "$INC/"
}

kit uikit "$A/armpi.inc" "$A/ws_ext.inc" "$K/uikit/uikit.inc"
kit audiokit "$K/audiokit/audiokit.inc"
kit netkit "$K/netkit/netkit.inc"
kit emukit "$K/emukit/emukit.inc" "$A/pad.inc"
kit gpukit "$K/gpukit/gpukit.inc"
kit mediakit "$K/mediakit/mediakit.inc"
kit archivekit "$K/archivekit/archivekit.inc"
kit documentkit "$K/documentkit/documentkit.inc"
kit gpiokit "$K/gpiokit/gpiokit.inc"
kit printerkit "$K/printerkit/printerkit.inc"
kit buskit "$K/buskit/buskit.inc"
cp "$K/uikit/include/uikit.h" "$K/mediakit/mediakit.h" "$K/emukit/include/emukit.h" "$K/printerkit/include/printerkit.h" "$K/printerkit/include/pdfprint.h" "$INC/"
# (le seul .bi ecrit a la main : les variables de l'interface ; les autres reprennent le genere)
cp "$ROOT/tools/kits/bi/uikit.bi" "$BI/"
for b in "$BI"/*_abi.bi; do
	f=${b%_abi.bi}.bi
	[ -f "$f" ] || printf '#pragma once
#include once "%s"
' "$(basename "$b")" > "$f"
done
ls -l "$OUT"/lib/*.a
