#!/bin/sh
# build_gcemu.sh -- gcemu.app : le coeur et l'app en C++ par la chaine croisee (WSL), Mesa relie en statique.
# -> build/emu/sdroot/apps/gcemu.app/{main,app.txt}. Depuis la racine du depot :  sh tools/emu/build_gcemu.sh
# Il faut : prebuilt/mesa (Mesa et armgl construits, tools/mesa/prebuilt.sh), les arbres GCC et POSIX de la copie principale.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
MAIN=${MAIN:-$(r=$(cd "$(dirname "$0")/../.." && pwd); echo "${r%%/.claude/worktrees/*}")}
GCC=${GCC:-$MAIN/build/gcc/sdroot/gcc}
POSIX=${POSIX:-$MAIN/build/posix}
P=$ROOT/prebuilt/mesa
OUT=$ROOT/build/emu
K=$OUT/kits
O=$OUT/obj/gcemu
A=$OUT/sdroot/apps/gcemu.app
JOBS=${JOBS:-12}
mkdir -p "$K" "$O" "$A"
sed -E "s#/[^ ]*/build/posix#$POSIX#g" "$POSIX/lib/posix.specs" > "$OUT/posix.specs"
python3 -I "$ROOT/tools/kits/abi2c.py" "$ROOT/src/kits/emukit/emukit.abi" "$K" --const "$ROOT/src/kits/emukit/emukit.inc" "$ROOT/src/inc/pad.inc" > /dev/null
${TC}gcc -mcpu=cortex-a72 -c "$K/emukit_abi.S" -o "$K/emukit_abi.o"
${TC}gcc -mcpu=cortex-a72 -O2 -g0 -specs="$OUT/posix.specs" -I"$GCC/include/kits" -c "$K/emukit_abi_open.c" -o "$K/emukit_abi_open.o"
rm -f "$K/libemukit.a"
${TC}ar rcs "$K/libemukit.a" "$K/emukit_abi.o" "$K/emukit_abi_open.o"
cp "$ROOT/src/kits/emukit/include/emukit.h" "$K/"

FLAGS="-mcpu=cortex-a72 -specs=$OUT/posix.specs -O3 -g0 -w -ffunction-sections -fdata-sections -MMD -MP -ffp-contract=off -DGX_ASM -I. -I$K -I$GCC/include/kits -I$ROOT/tools/mesa/armgl -I$P/include -std=gnu++17"
cd "$ROOT/src/apps/gcemu"
: > "$O/make.mk"
objs=""
for f in main.cpp gxes_rec.cpp gxes_gl.cpp soft.cpp gc/*.cpp; do
	o=$O/$(echo "$f" | tr / _).o
	objs="$objs $o"
	printf '%s: %s\n\t%s %s -c %s -o %s\n-include %s\n' "$o" "$f" "${TC}g++" "$FLAGS" "$f" "$o" "${o%.o}.d" >> "$O/make.mk"
done
{ printf 'all: %s\n' "$objs"; cat "$O/make.mk"; } > "$O/all.mk"
make -s -j"$JOBS" -f "$O/all.mk" all
"$ROOT/tools/fasmarm/fasmarm.exe" gx_asm.asm "$(wslpath -w "$O/gx_asm.o")" | tail -1
objs="$objs $O/gx_asm.o"
${TC}g++ -mcpu=cortex-a72 -specs="$OUT/posix.specs" -Wl,--gc-sections $objs -L"$K" -L"$GCC/lib" -lemukit \
	-Wl,--start-group "$P/lib/libarmgl.a" "$P/lib/libmesa_v3d.a" "$P/lib/libz.a" -Wl,--end-group -o "$A/main"
${TC}strip "$A/main"
cp app.txt "$A/"
echo "gcemu : $(stat -c %s "$A/main") octets"
