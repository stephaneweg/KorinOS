#!/bin/sh
# build_gcemu.sh -- gcemu.app : le coeur et l'app en C++ par la chaine croisee (WSL), Mesa relie en statique.
# -> build/emu/sdroot/apps/gcemu.app/{main,app.txt}. Depuis la racine du depot :  sh tools/emu/build_gcemu.sh
# Il faut : ~/armpi-mesa/out (tools/mesa/build-libs.sh), les arbres GCC et POSIX de la copie principale. armgl vient
# de ce worktree, compile a part : ~/armpi-mesa/out/armgl est aux autres sessions.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
MAIN=${MAIN:-$(r=$(cd "$(dirname "$0")/../.." && pwd); echo "${r%%/.claude/worktrees/*}")}
GCC=${GCC:-$MAIN/build/gcc/sdroot/gcc}
POSIX=${POSIX:-$MAIN/build/posix}
MO=$HOME/armpi-mesa/out
MS=$HOME/armpi-mesa/src/mesa-26.2.0
OUT=$ROOT/build/emu
K=$OUT/kits
O=$OUT/obj/gcemu
A=$OUT/sdroot/apps/gcemu.app
G=$O/armgl
JOBS=${JOBS:-12}
mkdir -p "$K" "$O" "$A" "$G"
sed -E "s#/[^ ]*/build/posix#$POSIX#g" "$POSIX/lib/posix.specs" > "$OUT/posix.specs"
python3 -I "$ROOT/tools/kits/abi2c.py" "$ROOT/src/kits/emukit/emukit.abi" "$K" --const "$ROOT/src/kits/emukit/emukit.inc" "$ROOT/src/inc/pad.inc" > /dev/null
${TC}gcc -mcpu=cortex-a72 -c "$K/emukit_abi.S" -o "$K/emukit_abi.o"
${TC}gcc -mcpu=cortex-a72 -O2 -g0 -specs="$OUT/posix.specs" -I"$GCC/include/kits" -c "$K/emukit_abi_open.c" -o "$K/emukit_abi_open.o"
rm -f "$K/libemukit.a"
${TC}ar rcs "$K/libemukit.a" "$K/emukit_abi.o" "$K/emukit_abi_open.o"
cp "$ROOT/src/kits/emukit/include/emukit.h" "$K/"

# armgl : les memes options que Mesa (voir tools/mesa/build-armgl.sh)
M=$ROOT/tools/mesa
B=$MO/build
CF=$(python3 -I - "$B/compile_commands.json" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if e['file'].endswith('v3d_screen.c'):
        a = shlex.split(e['command'])[1:]
        o = []
        skip = False
        for x in a:
            if skip:
                skip = False
            elif x in ('-o', '-MF', '-MQ', '-c'):
                skip = x != '-c'
            elif x != '-MD' and not x.endswith('v3d_screen.c') and not x.startswith('-W') and ' ' not in x and '"' not in x:
                o.append(x)
        print(' '.join(o))
        break
PY
)
CF=$(echo "$CF" | sed "s#[^ ]*/build/posix#$POSIX#g; s#$ROOT/build/posix#$POSIX#g")
INC="-I$M/armgl -I$M/libdrm -I$MS/src/mesa -I$MS/src/mesa/glapi -I$B/src/mesa -I$B/src/compiler -I$B/src/compiler/nir"
(cd "$B"; ${TC}gcc $INC $CF -c "$M/libdrm/armpi_drm.c" -o "$G/armpi_drm.o"; ${TC}gcc $INC $CF -c "$M/armgl/armgl.c" -o "$G/armgl.o")
rm -f "$G/libarmgl.a"
${TC}ar rcs "$G/libarmgl.a" "$G/armpi_drm.o" "$G/armgl.o"

FLAGS="-mcpu=cortex-a72 -specs=$OUT/posix.specs -O3 -g0 -w -ffunction-sections -fdata-sections -MMD -MP -ffp-contract=off -DGX_ASM -I. -I$K -I$GCC/include/kits -I$M/armgl -I$MS/include -std=gnu++17"
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
LIBS=$(cat "$MO/armgl/libs.txt")
${TC}g++ -mcpu=cortex-a72 -specs="$OUT/posix.specs" -Wl,--gc-sections $objs -L"$K" -L"$GCC/lib" -lemukit \
	-Wl,--start-group "$G/libarmgl.a" $LIBS "$HOME/armpi-webkit-build/sysroot/lib/libz.a" -Wl,--end-group -o "$A/main"
${TC}strip "$A/main"
cp app.txt "$A/"
echo "gcemu : $(stat -c %s "$A/main") octets"
