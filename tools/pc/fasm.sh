#!/bin/sh
# fasm : flat assembler 1.73 (x86, x86-64) qui tourne DANS Korin sur PC, sous le nom « fasm »
# (docs/etude/PC-PILOTES.md, « GCC, FreeBASIC et fasm dans Korin PC »). Le coeur de fasm
# (ref/fasm/SOURCE, le même que FASM.EXE de tools/pc/build.sh) et l'interface de Linux x64
# (LINUX/X64/MODES.INC) tels quels ; l'interface avec Korin (src/bin/fasm : korin.asm,
# system.inc, c/fasm_sys.c) ; assemblé en objet ELF64 par FASM.EXE (interop de WSL), lié au
# runtime des commandes en C (korinrt, à 0x400000, comme les commandes du PC). Dans WSL :
#     sh tools/bin/build_c.sh true      (une fois : build/bin/x86_64/obj/libkorinrt.a, build/bin/gen)
#     sh tools/pc/fasm.sh
# Produit build/pc/fasm/fasm (pour /bin/fasm de l'arbre du PC : tools/pc/sysroot_gcc.sh) et
# build/pc/fasm/source (le source assemblable dans Korin : fasm korin.asm fasm.o).
set -e
cd "$(dirname "$0")/../.."

FASM=${KORIN_FASM:-$(pwd)/ref/fasm/FASM.EXE}	# (KORIN_FASM : un fasm pour Linux, tools/cloud/setup.sh)
OUT=build/pc/fasm
W=$OUT/source
RT=src/lib/korinrt
G=build/bin/gen
O=build/bin/x86_64/obj
[ -f "$O/libkorinrt.a" ] && [ -f "$G/korin_sys_nr.h" ] || { echo "fasm : lancer d'abord sh tools/bin/build_c.sh true" >&2; exit 1; }

rm -rf "$W"
mkdir -p "$W"
cp ref/fasm/SOURCE/*.INC "$W/"
cp ref/fasm/SOURCE/LINUX/X64/MODES.INC "$W/modes.inc"
cp src/bin/fasm/korin.asm src/bin/fasm/system.inc "$W/"
(cd "$W" && "$FASM" korin.asm ../fasm.o) >"$OUT/fasm.log" 2>&1 || { cat "$OUT/fasm.log"; exit 1; }
tail -n 1 "$OUT/fasm.log"

# (les options de tools/bin/build_c.sh pour x86_64)
NAK=$(grep -c '^kit_fn' src/kits/appkit/appkit.abi)
CFLAGS="-std=gnu11 -O2 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector
-fno-pie -fno-strict-aliasing -fwrapv -fno-asynchronous-unwind-tables -fno-unwind-tables
-ffunction-sections -fdata-sections -Wall -Wextra -Werror -I$G -Isrc/kits/common -I$RT
-DKRT_APPKIT_MIN=$NAK -mno-red-zone"
LDFLAGS="-nostdlib -static -no-pie -s -Wl,-u,_start -Wl,--gc-sections -Wl,-z,noexecstack -Wl,--build-id=none
-Wl,-z,max-page-size=0x1000 -Wl,-T,$RT/bin.ld -Wl,--defsym=KRT_BASE=0x400000"
gcc $CFLAGS $LDFLAGS src/bin/fasm/c/fasm_sys.c "$OUT/fasm.o" "$O/libkorinrt.a" -lgcc -o "$OUT/fasm"
if readelf -r "$OUT/fasm" | grep -q 'Relocation section'; then
	echo "erreur : $OUT/fasm a des relocations"
	exit 1
fi
# (le source dans l'arbre : se réassemble dans Korin, la liaison demande gcc et korinrt)
cp src/bin/fasm/c/fasm_sys.c "$W/"
cp ref/fasm/LICENSE.TXT "$W/"
ls -l "$OUT/fasm"
