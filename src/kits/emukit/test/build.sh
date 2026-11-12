#!/bin/sh
# Construit emtest (essai différentiel d'EmuKit), sans bibliothèque, dans WSL :
#   build/kits/test/aarch64/emtest   Korin sur le Pi (lié à 0x4000000000)
#   build/kits/test/x86_64/emtest    Korin sur PC (lié à 0x400000)
#
#   sh src/kits/emukit/test/build.sh      (depuis la racine du dépôt)
set -e
cd "$(dirname "$0")/../../../.."

A64=${A64:-/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}
SRC=src/kits/emukit/test/emtest.c
OUT=build/kits/test
mkdir -p "$OUT/aarch64" "$OUT/x86_64"

CFLAGS="-std=gnu11 -O2 -ffreestanding -nostdlib -static -fno-pie -no-pie -fno-stack-protector
-fno-builtin -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -fno-unwind-tables
-Wall -Wextra -Werror -Wl,-e,_start -Wl,-z,noexecstack -Wl,--build-id=none"

${A64}gcc $CFLAGS -mgeneral-regs-only -ffixed-x18 -mno-outline-atomics \
	-Wl,-Ttext-segment=0x4000000000 -Wl,-z,max-page-size=0x1000 \
	"$SRC" -o "$OUT/aarch64/emtest"
gcc $CFLAGS -mno-red-zone -Wl,-Ttext-segment=0x400000 -Wl,-z,max-page-size=0x1000 \
	"$SRC" -o "$OUT/x86_64/emtest"

for a in aarch64 x86_64; do
	f=$OUT/$a/emtest
	echo "$f : $(stat -c %s "$f") octets, $(readelf -h "$f" | sed -n 's/^ *Type: *\([A-Z]*\).*/\1/p')," \
		"$(readelf -h "$f" | sed -n 's/^ *Machine: *//p'), entrée $(readelf -h "$f" | sed -n 's/^ *Entry point address: *//p')"
done
