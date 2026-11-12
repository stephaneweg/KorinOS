#!/bin/sh
# Construit les commandes de /bin écrites en C (src/bin/<nom>/*.c), communes au Pi et au PC, dans WSL
# (docs/BIN-C.md) :
#   build/bin/aarch64/<nom>   pour le Pi (copiées sur la carte par build.ps1 à la place de l'asm)
#   build/bin/x86_64/<nom>    pour le PC (initrd de tools/pc/build.sh)
#
#   sh tools/bin/build_c.sh              toutes les commandes qui ont du C
#   sh tools/bin/build_c.sh cat wc       seulement celles-ci
#
# Chaque commande est liée statiquement, sans newlib ni libposix, au runtime src/lib/korinrt
# (build/bin/<arch>/obj/libkorinrt.a) et aux relais d'AppKit (tools/kits/abi2import.py) ;
# ELF64 à adresse fixe, comme les programmes en assembleur : 0x4000000000 sur le Pi, 0x400000 sur
# PC (les chargeurs ne font pas de relocations ; des pointeurs dans les données sont permis).
# Les fichiers lang/<xx>.txt de src/bin/<nom> restent ceux de la commande (tools/lang/merge.py).
set -e
cd "$(dirname "$0")/../.."

OUT=build/bin
GEN=$OUT/gen
A64=${A64:-/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}
RT=src/lib/korinrt
mkdir -p "$GEN" "$OUT/aarch64/obj" "$OUT/x86_64/obj"

# numéros des appels système, tirés de src/inc/syscalls.inc (seule source)
{
	echo "/* Tiré de src/inc/syscalls.inc par tools/bin/build_c.sh : ne pas modifier. */"
	echo "#ifndef KORIN_SYS_NR_H"
	echo "#define KORIN_SYS_NR_H"
	sed -n 's/^\(SYS_[A-Z0-9_]*\)[[:space:]]*=[[:space:]]*\(0x[0-9A-Fa-f]*\).*/#define \1 \2/p' src/inc/syscalls.inc
	echo "#endif"
} >"$GEN/korin_sys_nr.h"

# relais d'AppKit : version minimale = nombre de fonctions de appkit.abi (comme kits_open) ;
# puts et malloc renommés (le runtime a les siens) ; un relais par section (seuls ceux qui servent restent)
NAK=$(grep -c '^kit_fn' src/kits/appkit/appkit.abi)
python3 -I tools/kits/abi2import.py src/kits/appkit/appkit.abi "$GEN" --min "$NAK" \
	--rename puts=ak_puts,malloc=ak_malloc --sections >/dev/null

CFLAGS="-std=gnu11 -O2 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector
-fno-pie -fno-strict-aliasing -fwrapv -fno-asynchronous-unwind-tables -fno-unwind-tables
-ffunction-sections -fdata-sections -Wall -Wextra -Werror -I$GEN -Isrc/kits/common -I$RT
-DKRT_APPKIT_MIN=$NAK"
LDFLAGS="-nostdlib -static -no-pie -s -Wl,-u,_start -Wl,--gc-sections -Wl,-z,noexecstack -Wl,--build-id=none
-Wl,-z,max-page-size=0x1000 -Wl,-T,$RT/bin.ld"

arch_flags() { # arch_flags <arch> : options du compilateur et de l'éditeur de liens
	if [ "$1" = aarch64 ]; then
		echo "-ffixed-x18 -mno-outline-atomics -Wl,--defsym=KRT_BASE=0x4000000000"
	else
		echo "-mno-red-zone -Wl,--defsym=KRT_BASE=0x400000"
	fi
}
cc_of() { [ "$1" = aarch64 ] && echo "${A64}gcc" || echo gcc; }
ar_of() { [ "$1" = aarch64 ] && echo "${A64}ar" || echo ar; }

# le runtime : une bibliothèque statique par architecture (seul ce qui sert est lié)
for arch in aarch64 x86_64; do
	CC=$(cc_of $arch)
	O=$OUT/$arch/obj
	objs=""
	for f in $RT/*.c "$GEN/appkit_import.S"; do
		o=$O/$(basename "$f").o
		$CC $CFLAGS $(arch_flags $arch) -c "$f" -o "$o"
		objs="$objs $o"
	done
	rm -f "$O/libkorinrt.a"
	$(ar_of $arch) rcs "$O/libkorinrt.a" $objs
done

NAMES="$*"
if [ -z "$NAMES" ]; then
	for d in src/bin/*/; do
		n=$(basename "$d")
		ls "$d"*.c >/dev/null 2>&1 || continue
		NAMES="$NAMES $n"
	done
fi

for n in $NAMES; do
	src=$(ls src/bin/"$n"/*.c)
	# src/bin/<nom>/bin.conf (facultatif) : « import = netkit,buskit » : les relais de ces kits
	# (<kit>_import.h, fonctions de type long), et krt_kits pour que _start les ouvre (kits_open)
	IMPORTS=""
	[ -f src/bin/"$n"/bin.conf ] &&
		IMPORTS=$(sed -n 's/^[[:space:]]*import[[:space:]]*=[[:space:]]*//p' src/bin/"$n"/bin.conf | head -1 | tr ',' ' ')
	extra=""
	if [ -n "$IMPORTS" ]; then
		G=$GEN/$n
		mkdir -p "$G"
		{
			echo "/* Généré par tools/bin/build_c.sh depuis src/bin/$n/bin.conf : ne pas modifier. */"
			printf 'const char *const krt_kits[] = {'
			for k in $IMPORTS; do printf ' "%s",' "$k"; done
			echo ' 0 };'
			printf 'const long krt_kits_min[] = {'
			for k in $IMPORTS; do printf ' %s,' "$(grep -c '^kit_fn' src/kits/$k/$k.abi)"; done
			echo ' 0 };'
		} > "$G/krt_kits.c"
		extra="$G/krt_kits.c"
		for k in $IMPORTS; do
			python3 -I tools/kits/abi2import.py src/kits/$k/$k.abi "$G" --sections >/dev/null
			extra="$extra $G/${k}_import.S"
		done
		extra="$extra -I$G"
	fi
	for arch in aarch64 x86_64; do
		CC=$(cc_of $arch)
		O=$OUT/$arch/obj
		# (crt0 en tête : _start ; la bibliothèque après les sources)
		$CC $CFLAGS $(arch_flags $arch) $LDFLAGS -Isrc/bin/"$n" $src $extra "$O/libkorinrt.a" -lgcc -o "$OUT/$arch/$n"
		if readelf -r "$OUT/$arch/$n" | grep -q 'Relocation section'; then
			echo "erreur : $OUT/$arch/$n a des relocations"
			exit 1
		fi
	done
	printf '%-10s %6s octets (aarch64) %6s octets (x86_64)\n' "$n" \
		"$(stat -c %s "$OUT/aarch64/$n")" "$(stat -c %s "$OUT/x86_64/$n")"
done
