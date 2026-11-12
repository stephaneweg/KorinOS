#!/bin/sh
# Construit uitest (essai différentiel d'UIKit), sans bibliothèque, dans WSL :
#   build/kits/test/aarch64/uitest   Korin sur le Pi (lié à 0x4000000000)
#   build/kits/test/x86_64/uitest    Korin sur PC (lié à 0x400000)
#
#   sh src/kits/uikit/test/build.sh      (depuis la racine du dépôt)
#
# uitest lie le rasteriseur TrueType du kit en C (src/kits/uikit/uk_ttf.c, en
# flottant, comme fp_files de kit.conf) pour remplir lui-même ses trois polices,
# embarquées par .incbin depuis build/sdroot/fonts (et quatre petits PNG de
# build/sdroot) : il faut avoir construit la carte et les en-têtes du kit
# (wsl sh tools/kits/build_c.sh uikit : build/kits/gen/uikit).
set -e
cd "$(dirname "$0")/../../../.."

A64=${A64:-/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}
SRC=src/kits/uikit/test/uitest.c
TTF=src/kits/uikit/uk_ttf.c
OUT=build/kits/test
mkdir -p "$OUT/aarch64" "$OUT/x86_64"
[ -f build/kits/gen/uikit/appkit_import.h ] || {
	echo "build/kits/gen/uikit absent : lancer d'abord tools/kits/build_c.sh uikit" >&2
	exit 1
}

CFLAGS="-std=gnu11 -O2 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin
-fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror"
LFLAGS="-nostdlib -static -no-pie -Wl,-e,_start -Wl,-z,noexecstack -Wl,--build-id=none -Wl,-z,max-page-size=0x1000"
# uk_ttf.c : comme dans le kit (kit.conf : cflags_*, sans -mgeneral-regs-only)
TTFFLAGS="-fwrapv -fno-strict-aliasing -ffp-contract=off -Isrc/kits/uikit -Ibuild/kits/gen -Ibuild/kits/gen/uikit
-Isrc/kits/common"

${A64}gcc $CFLAGS -mgeneral-regs-only -ffixed-x18 -mno-outline-atomics -c "$SRC" -o "$OUT/aarch64/uitest.o"
${A64}gcc $CFLAGS $TTFFLAGS -ffixed-x18 -mno-outline-atomics -c "$TTF" -o "$OUT/aarch64/uk_ttf.o"
${A64}gcc $CFLAGS $LFLAGS -Wl,-Ttext-segment=0x4000000000 \
	"$OUT/aarch64/uitest.o" "$OUT/aarch64/uk_ttf.o" -o "$OUT/aarch64/uitest"

gcc $CFLAGS -mno-red-zone -c "$SRC" -o "$OUT/x86_64/uitest.o"
gcc $CFLAGS $TTFFLAGS -mno-red-zone -msse4.1 -c "$TTF" -o "$OUT/x86_64/uk_ttf.o"
gcc $CFLAGS $LFLAGS -Wl,-Ttext-segment=0x400000 \
	"$OUT/x86_64/uitest.o" "$OUT/x86_64/uk_ttf.o" -o "$OUT/x86_64/uitest"

for a in aarch64 x86_64; do
	f=$OUT/$a/uitest
	echo "$f : $(stat -c %s "$f") octets, $(readelf -h "$f" | sed -n 's/^ *Type: *\([A-Z]*\).*/\1/p')," \
		"$(readelf -h "$f" | sed -n 's/^ *Machine: *//p'), entrée $(readelf -h "$f" | sed -n 's/^ *Entry point address: *//p')"
done
