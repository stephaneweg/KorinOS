#!/bin/sh
# Construit les services écrits en C (src/svc/<nom>/*.c), dans WSL, comme les commandes de
# tools/bin/build_c.sh (même runtime src/lib/korinrt, mêmes adresses fixes) :
#   build/svc/aarch64/<nom>   pour le Pi
#   build/svc/x86_64/<nom>    pour le PC (initrd de tools/pc/build.sh)
#
#   sh tools/svc/build_c.sh              tous les services qui ont du C
#   sh tools/svc/build_c.sh pcid usbd    seulement ceux-ci
#
# Dans src/svc/<nom> : les *.c communs ; *_aarch64.c et *_x86_64.c seulement pour leur machine
# (couche propre au Pi ou au PC). svc.conf (facultatif) :
#   import = buskit              relais de ces kits (comme bin.conf des commandes)
#   sources = ../pcid/pcilib.c   sources de plus (relatives au dossier du service)
#   arch = x86_64                une seule machine (sinon les deux)
# src/svc/pcid (pci.h) et src/svc/common (fssvc.h, fsowner.c, blkserv.c : services de fichiers) sont
# toujours dans le chemin des en-têtes.
# Le runtime (build/bin/<arch>/obj/libkorinrt.a) vient de tools/bin/build_c.sh, lancé ici s'il manque.
set -e
cd "$(dirname "$0")/../.."

OUT=build/svc
BGEN=build/bin/gen
A64=${A64:-/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}
RT=src/lib/korinrt
mkdir -p "$OUT/aarch64" "$OUT/x86_64" "$OUT/gen"

if [ ! -f build/bin/x86_64/obj/libkorinrt.a ] || [ ! -f build/bin/aarch64/obj/libkorinrt.a ] ||
	[ ! -f "$BGEN/appkit_import.h" ]; then
	sh tools/bin/build_c.sh >/dev/null
fi
# les numéros des appels système peuvent avoir changé depuis le runtime
{
	echo "/* Tiré de src/inc/syscalls.inc par tools/svc/build_c.sh : ne pas modifier. */"
	echo "#ifndef KORIN_SYS_NR_H"
	echo "#define KORIN_SYS_NR_H"
	sed -n 's/^\(SYS_[A-Z0-9_]*\)[[:space:]]*=[[:space:]]*\(0x[0-9A-Fa-f]*\).*/#define \1 \2/p' src/inc/syscalls.inc
	echo "#endif"
} >"$OUT/gen/korin_sys_nr.h"

NAK=$(grep -c '^kit_fn' src/kits/appkit/appkit.abi)
CFLAGS="-std=gnu11 -O2 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector
-fno-pie -fno-strict-aliasing -fwrapv -fno-asynchronous-unwind-tables -fno-unwind-tables
-ffunction-sections -fdata-sections -Wall -Wextra -Werror -I$OUT/gen -I$BGEN -Isrc/kits/common -I$RT
-Isrc/svc/pcid -Isrc/svc/common -DKRT_APPKIT_MIN=$NAK"
LDFLAGS="-nostdlib -static -no-pie -s -Wl,-u,_start -Wl,--gc-sections -Wl,-z,noexecstack -Wl,--build-id=none
-Wl,-z,max-page-size=0x1000 -Wl,-T,$RT/bin.ld"

arch_flags() {
	if [ "$1" = aarch64 ]; then
		echo "-ffixed-x18 -mno-outline-atomics -Wl,--defsym=KRT_BASE=0x4000000000"
	else
		echo "-mno-red-zone -Wl,--defsym=KRT_BASE=0x400000"
	fi
}
cc_of() { [ "$1" = aarch64 ] && echo "${A64}gcc" || echo gcc; }
conf() { # conf <dossier> <clé>
	[ -f "$1/svc.conf" ] || return 0
	sed -n "s/^[[:space:]]*$2[[:space:]]*=[[:space:]]*//p" "$1/svc.conf" | head -1
}

NAMES="$*"
if [ -z "$NAMES" ]; then
	for d in src/svc/*/; do
		ls "$d"*.c >/dev/null 2>&1 || continue
		grep -l 'int main' "$d"*.c >/dev/null 2>&1 || continue
		NAMES="$NAMES $(basename "$d")"
	done
fi

for n in $NAMES; do
	D=src/svc/$n
	IMPORTS=$(conf "$D" import | tr ',' ' ')
	EXTRA_SRC=""
	for s in $(conf "$D" sources | tr ',' ' '); do EXTRA_SRC="$EXTRA_SRC $D/$s"; done
	ARCHS=$(conf "$D" arch)
	[ -n "$ARCHS" ] || ARCHS="aarch64 x86_64"
	extra=""
	if [ -n "$IMPORTS" ]; then
		G=$OUT/gen/$n
		mkdir -p "$G"
		{
			echo "/* Généré par tools/svc/build_c.sh depuis $D/svc.conf : ne pas modifier. */"
			printf 'const char *const krt_kits[] = {'
			for k in $IMPORTS; do printf ' "%s",' "$k"; done
			echo ' 0 };'
			printf 'const long krt_kits_min[] = {'
			for k in $IMPORTS; do printf ' %s,' "$(grep -c '^kit_fn' src/kits/$k/$k.abi)"; done
			echo ' 0 };'
		} >"$G/krt_kits.c"
		extra="$G/krt_kits.c"
		for k in $IMPORTS; do
			python3 -I tools/kits/abi2import.py src/kits/$k/$k.abi "$G" --sections >/dev/null
			extra="$extra $G/${k}_import.S"
		done
		extra="$extra -I$G"
	fi
	for arch in $ARCHS; do
		CC=$(cc_of $arch)
		src=""
		for f in "$D"/*.c; do
			case "$f" in
			*_aarch64.c) [ $arch = aarch64 ] && src="$src $f" ;;
			*_x86_64.c) [ $arch = x86_64 ] && src="$src $f" ;;
			*) src="$src $f" ;;
			esac
		done
		$CC $CFLAGS $(arch_flags $arch) $LDFLAGS -I"$D" $src $EXTRA_SRC $extra \
			build/bin/$arch/obj/libkorinrt.a -lgcc -o "$OUT/$arch/$n"
		if readelf -r "$OUT/$arch/$n" | grep -q 'Relocation section'; then
			echo "erreur : $OUT/$arch/$n a des relocations"
			exit 1
		fi
		printf '%-10s %-8s %7s octets\n' "$n" "$arch" "$(stat -c %s "$OUT/$arch/$n")"
	done
done
