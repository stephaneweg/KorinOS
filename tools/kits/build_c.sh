#!/bin/sh
# Construit les kits écrits en C (src/kits/<kit>/*.c), communs au Pi et au PC, dans WSL :
#   build/kits/aarch64/<kit>.so   pour le Pi (copié sur la carte par build.ps1 à la place de l'asm)
#   build/kits/x86_64/<kit>.so    pour le PC (initrd de tools/pc/build.sh)
#
#   sh tools/kits/build_c.sh            tous les kits qui ont du C
#   sh tools/kits/build_c.sh buskit     un seul
#
# Un kit est un ELF64 lié à 0 (src/kits/common/kit.ld), table d'exportation générée depuis son
# .abi (tools/kits/abi2table.py), code relatif au compteur ordinal (-fPIC, symboles cachés).
# Les chargeurs de kits des deux noyaux ne font pas de relocations : un kit qui en demanderait
# (un pointeur rangé dans ses données) est refusé ici.
#
# src/kits/<kit>/kit.conf (facultatif), une clé par ligne :
#   keep = net_,wifi_          fonctions qui gardent tous les registres sauf x0 (relais AArch64)
#   cflags_aarch64 = ...       options de plus pour AArch64 (ex. -mgeneral-regs-only)
#   cflags_x86_64 = ...        options de plus pour x86-64
set -e
cd "$(dirname "$0")/../.."

OUT=build/kits
GEN=$OUT/gen
A64=${A64:-/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}
mkdir -p "$GEN" "$OUT/aarch64" "$OUT/x86_64"

# numéros des appels système, tirés de src/inc/syscalls.inc (seule source)
{
	echo "/* Tiré de src/inc/syscalls.inc par tools/kits/build_c.sh : ne pas modifier. */"
	echo "#ifndef KORIN_SYS_NR_H"
	echo "#define KORIN_SYS_NR_H"
	sed -n 's/^\(SYS_[A-Z0-9_]*\)[[:space:]]*=[[:space:]]*\(0x[0-9A-Fa-f]*\).*/#define \1 \2/p' src/inc/syscalls.inc
	echo "#endif"
} >"$GEN/korin_sys_nr.h"

KITS="$*"
if [ -z "$KITS" ]; then
	for d in src/kits/*/; do
		k=$(basename "$d")
		ls "$d"*.c >/dev/null 2>&1 && [ -f "$d$k.abi" ] && KITS="$KITS $k"
	done
fi

CFLAGS="-std=gnu11 -O2 -ffreestanding -fPIC -fvisibility=hidden -fno-stack-protector -fno-builtin
-fno-asynchronous-unwind-tables -fno-unwind-tables -Wall -Wextra -Werror -I$GEN -Isrc/kits/common"

conf() { # conf <kit> <clé> : la valeur dans src/kits/<kit>/kit.conf
	[ -f "src/kits/$1/kit.conf" ] || return 0
	sed -n "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*//p" "src/kits/$1/kit.conf" | head -1
}

for k in $KITS; do
	KEEP=$(conf "$k" keep)
	python3 -I tools/kits/abi2table.py "src/kits/$k/$k.abi" "$GEN/${k}_table.S" ${KEEP:+--keep "$KEEP"}
	for arch in aarch64 x86_64; do
		if [ "$arch" = aarch64 ]; then
			CC=${A64}gcc
			LD=${A64}ld
			AFLAGS="-mno-outline-atomics -ffixed-x18 $(conf "$k" cflags_aarch64)"
		else
			CC=gcc
			LD=ld
			AFLAGS="-mno-red-zone $(conf "$k" cflags_x86_64)"
		fi
		O=$OUT/obj/$arch/$k
		mkdir -p "$O"
		OBJS=""
		for f in src/kits/$k/*.c src/kits/common/kitrt.c; do
			o="$O/$(basename "$f" .c).o"
			$CC $CFLAGS $AFLAGS -c "$f" -o "$o"
			OBJS="$OBJS $o"
		done
		$CC -c "$GEN/${k}_table.S" -o "$O/table.o"
		$LD -nostdlib -static -z max-page-size=0x1000 -z noexecstack --no-warn-rwx-segments \
			-T src/kits/common/kit.ld -o "$OUT/$arch/$k.so" "$O/table.o" $OBJS
		# aucune relocation ni table d'adresses absolues (GOT) dans le kit
		if readelf -S "$OUT/$arch/$k.so" | grep -Eq '\.(rela?\.|got)'; then
			echo "$k ($arch) : relocations ou GOT, refusé (un pointeur dans les données ?)" >&2
			readelf -S "$OUT/$arch/$k.so" | grep -E '\.(rela?\.|got)' >&2
			exit 1
		fi
		echo "$OUT/$arch/$k.so : $(stat -c %s "$OUT/$arch/$k.so") octets"
	done
done
