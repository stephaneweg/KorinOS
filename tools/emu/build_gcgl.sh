#!/bin/sh
# build_gcgl.sh -- build/emu/gcgl : tools/emu/gcgl.cpp, apres tools/emu/build_gcemu.sh (memes objets, sans EmuKit).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
MAIN=${MAIN:-$(r=$(cd "$(dirname "$0")/../.." && pwd); echo "${r%%/.claude/worktrees/*}")}
GCC=$MAIN/build/gcc/sdroot/gcc
MO=$HOME/armpi-mesa/out
OUT=$ROOT/build/emu
O=$OUT/obj/gcemu
U=$HOME/armpi-webkit-build/uikit
${TC}g++ -mcpu=cortex-a72 -specs="$OUT/posix.specs" -O2 -g0 -w -std=gnu++17 -I"$ROOT/src/apps/gcemu" -I"$ROOT/tools/mesa/armgl" -I"$ROOT/src/kits/uikit/include" -I"$U" \
	-c "$ROOT/tools/emu/gcgl.cpp" -o "$O/gcgl.o"
objs=$(ls "$O"/gc_*.o "$O"/gxes_*.o "$O"/gx_asm.o)
LIBS=$(cat "$MO/armgl/libs.txt")
${TC}g++ -mcpu=cortex-a72 -specs="$OUT/posix.specs" -Wl,--gc-sections "$O/gcgl.o" $objs "$U/libuikit_c.a" \
	-Wl,--start-group "$O/armgl/libarmgl.a" $LIBS "$HOME/armpi-webkit-build/sysroot/lib/libz.a" -Wl,--end-group -o "$OUT/gcgl"
${TC}strip "$OUT/gcgl"
ls -l "$OUT/gcgl"
