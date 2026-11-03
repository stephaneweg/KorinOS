#!/bin/sh
# build.sh -- Quake par la chaine croisee (WSL) -> build/quake/sdroot/apps/quake.app/{main,app.txt} (docs/QUAKE.md)
#
#   sh tools/quake/build.sh
#
# A lancer depuis la racine du depot. GCC et POSIX : l'arbre de tools/gcc/build.sh (par defaut celui de la copie
# principale, qu'un worktree n'a pas), comme tools/emu/build.sh. build.ps1 copie build/quake/sdroot sur la carte.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
MAIN=${MAIN:-$(r=$(cd "$(dirname "$0")/../.." && pwd); echo "${r%%/.claude/worktrees/*}")}
GCC=${GCC:-$MAIN/build/gcc/sdroot/gcc}
POSIX=${POSIX:-$MAIN/build/posix}
OUT=$ROOT/build/quake
Q=third_party/quake
K=$OUT/kits
O=$OUT/obj
A=$OUT/sdroot/apps/quake.app
JOBS=${JOBS:-8}
cd "$ROOT"
mkdir -p "$K" "$O" "$A"
sed -E "s#/[^ ]*/build/posix#$POSIX#g" "$POSIX/lib/posix.specs" > "$OUT/posix.specs"
# les relais d'EmuKit
python3 -I tools/kits/abi2c.py src/kits/emukit/emukit.abi "$K" --const src/kits/emukit/emukit.inc src/inc/pad.inc
${TC}gcc -mcpu=cortex-a72 -c "$K/emukit_abi.S" -o "$K/emukit_abi.o"
${TC}gcc -mcpu=cortex-a72 -O2 -g0 -specs="$OUT/posix.specs" -I"$GCC/include/kits" -c "$K/emukit_abi_open.c" -o "$K/emukit_abi_open.o"
rm -f "$K/libemukit.a"
${TC}ar rcs "$K/libemukit.a" "$K/emukit_abi.o" "$K/emukit_abi_open.o"
cp src/kits/emukit/include/emukit.h "$K/"

LIST=$(sed -n 's/^QUAKE_C = //p' tools/quake/sources.mk)
sh tools/quake/mk_asm.sh
ASM=${ASM:-1}			# 0 : le C d'id seul
BASE="-DARMPI_ASM=$ASM -mcpu=cortex-a72 -specs=$OUT/posix.specs -O3 -g0 -w -fcommon -fno-strict-aliasing -fwrapv -ffp-contract=off -ffunction-sections -fdata-sections -Werror=implicit-function-declaration -Werror=int-conversion -I$Q -I$K -I$GCC/include/kits"
objs=""
{
	printf 'all: %s\n' "$A/main"
	for f in $LIST; do
		printf '%s: %s\n\t%sgcc %s -std=gnu89 -c $< -o $@\n' "$O/$f.o" "$Q/$f.c" "$TC" "$BASE"
		objs="$objs $O/$f.o"
	done
	for f in main vid snd in asm_check; do
		printf '%s: src/apps/quake/%s.c src/apps/quake/armpi.h\n\t%sgcc %s -std=gnu11 -Isrc/apps/quake -c $< -o $@\n' "$O/a_$f.o" "$f" "$TC" "$BASE"
		objs="$objs $O/a_$f.o"
	done
	[ "$ASM" = 0 ] || objs="$objs $OUT/quake_a64.o"
	printf '%s:%s\n\t%sgcc -mcpu=cortex-a72 -specs=%s -Wl,--gc-sections %s -L%s -L%s/lib -lemukit -lm -o $@\n' "$A/main" "$objs" "$TC" "$OUT/posix.specs" "$objs" "$K" "$GCC"
} > "$OUT/app.mk"
make -s -j"$JOBS" -f "$OUT/app.mk" all
cp src/apps/quake/app.txt "$A/"
echo "quake : $(stat -c %s "$A/main") octets"
