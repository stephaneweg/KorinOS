#!/bin/sh
# Le harnais (harness.c) sous Linux aarch64 : build/quake/ref_quake, le C d'id seul, et build/quake/asm_quake,
# les routines A64 en plus. Dans WSL, depuis la racine du depot : sh tools/quake/mk_harness.sh
set -eu
Q=third_party/quake
W=build/quake
LIST=$(sed -n 's/^QUAKE_C = //p' tools/quake/sources.mk)
OPT=${OPT:--O2}
BASE="-static $OPT -g0 -mcpu=cortex-a72 -std=gnu89 -fcommon -fno-strict-aliasing -fwrapv -ffp-contract=off -w -Werror=implicit-function-declaration -Werror=int-conversion -I$Q"
GCC=aarch64-linux-gnu-gcc
mkdir -p $W/ref $W/asm
one () { # dossier, binaire, options, objets en plus
	d=$1; bin=$2; opt=$3; more=$4
	objs=""
	{
		printf 'all: %s\n' "$W/$bin"
		for f in $LIST; do
			printf '%s: %s\n\t%s %s %s -c $< -o $@\n' "$W/$d/$f.o" "$Q/$f.c" "$GCC" "$BASE" "$opt"
			objs="$objs $W/$d/$f.o"
		done
		printf '%s: %s\n\t%s %s %s -c $< -o $@\n' "$W/$d/harness.o" tools/quake/harness.c "$GCC" "$BASE" "$opt"
		objs="$objs $W/$d/harness.o $more"
		printf '%s:%s\n\t%s -static %s -lm -o $@\n' "$W/$bin" "$objs" "$GCC" "$objs"
	} > "$W/$d.mk"
	make -s -j8 -f "$W/$d.mk" all
}
one ref ref_quake "" ""
sh tools/quake/mk_asm.sh
$GCC $BASE -std=gnu11 -Isrc/apps/quake -c src/apps/quake/asm_check.c -o $W/asm/asm_check.o
one asm asm_quake "-DARMPI_ASM=1" "$W/quake_a64.o"
ls -la $W/ref_quake $W/asm_quake
