#!/bin/sh
# build-n64 : n64emu relie a Mesa -> build/emu/sdroot/apps/n64emu.app/main, que build.ps1 met sur la carte a la place
# de l'assembleur seul (docs/EMULATEURS.md). L'emulateur reste un programme FASMARM entier, assemble au-dessus du C :
# ld n'a ni a le comprendre ni a le reloger, il pose ses segments tels quels (n64link.py).
#     sh tools/emu/build-n64.sh              (dans WSL, depuis la racine du depot, apres tools/mesa/build-armgl.sh)
# Sans Mesa construit, build.ps1 garde l'assembleur seul, sur GpuKit.
set -eu
. "$(dirname "$0")/../mesa/env.sh"
MAIN=${MAIN:-$(r=$(cd "$(dirname "$0")/../.." && pwd); echo "${r%%/.claude/worktrees/*}")}
[ -f "$POSIX/lib/libposix.a" ] || POSIX=$MAIN/build/posix
G=$OUT/n64gl
B=$OUT/build
M=$ARMPI_ROOT/tools/mesa
N=$ARMPI_ROOT/src/apps/n64emu
BASE=0x4004000000		# 64 Mo au-dessus du C : Mesa et ses donnees y tiennent, ld refuse sinon
mkdir -p "$G" "$ARMPI_ROOT/build/emu/sdroot/apps/n64emu.app" "$ARMPI_ROOT/build/mesa/sdroot/bin"

sed -E "s#/[^ ]*/build/posix#$POSIX#g" "$POSIX/lib/posix.specs" > "$G/base.specs"

# les memes options que Mesa pour armgl et le winsys ; ces objets-ci, pour ne pas gener un autre chantier
CF=$(python3 - "$B/compile_commands.json" <<'PY'
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
INC="-I$M/armgl -I$M/libdrm -I$SRC/src/mesa -I$SRC/src/mesa/glapi -I$B/src/mesa -I$B/src/compiler -I$B/src/compiler/nir"
(cd "$B"
${TC}gcc $INC $CF -c "$M/libdrm/armpi_drm.c" -o "$G/armpi_drm.o"
${TC}gcc $INC $CF -c "$M/armgl/armgl.c" -o "$G/armgl.o")
rm -f "$G/libarmgl.a"
${TC}ar rcs "$G/libarmgl.a" "$G/armpi_drm.o" "$G/armgl.o"

CC="${TC}gcc -specs=$G/base.specs -mcpu=cortex-a72 -O2 -w -I$M/armgl -I$N/gl -I$SRC/include"
$CC -c "$N/gl/n64gl.c" -o "$G/n64gl.o"
$CC -c "$N/gl/n64gl_armpi.c" -o "$G/n64gl_armpi.o"
$CC -DARMPI_DRY -c "$ARMPI_ROOT/tools/emu/n64gl_replay.c" -o "$G/n64gltest.o"

# l'assembleur, a son adresse (tools/fasmarm : .exe sous Windows, WSL le lance)
(cd "$N" && "$ARMPI_ROOT/tools/fasmarm/fasmarm.exe" -d N64_BASE=$BASE n64emu.asm "$(wslpath -m "$G/n64emu.elf")" | tail -1)
python3 -I "$ARMPI_ROOT/tools/emu/n64link.py" "$G/n64emu.elf" "$G/n64asm.S" "$G/base.specs" "$G/n64.ld" "$G/posix.specs"
${TC}gcc -c "$G/n64asm.S" -o "$G/n64asm.o"

LIBS=$(cat "$OUT/armgl/libs.txt")
${TC}g++ -specs="$G/posix.specs" -Wl,--gc-sections "$G/n64gl_armpi.o" "$G/n64gl.o" "$G/n64asm.o" \
	-Wl,--start-group "$G/libarmgl.a" $LIBS "$WK/lib/libz.a" -Wl,--end-group -o "$G/n64emu"
${TC}strip -o "$G/n64emu.s" "$G/n64emu"
cp "$G/n64emu.s" "$ARMPI_ROOT/build/emu/sdroot/apps/n64emu.app/main"
cp "$N/app.txt" "$ARMPI_ROOT/build/emu/sdroot/apps/n64emu.app/"
mkdir -p "$ARMPI_ROOT/build/mesa/sdroot/apps/n64emu.app"	# (la carte d'essai de tools/mesa/testimage.py)
cp "$G/n64emu.s" "$ARMPI_ROOT/build/mesa/sdroot/apps/n64emu.app/main"

# le banc : le meme n64gl.c sur des images videes sur le PC
${TC}g++ -specs="$G/base.specs" -Wl,--gc-sections "$G/n64gltest.o" "$G/n64gl.o" \
	-Wl,--start-group "$G/libarmgl.a" $LIBS "$WK/lib/libz.a" -Wl,--end-group -o "$G/n64gltest"
${TC}strip -o "$G/n64gltest.s" "$G/n64gltest"
cp "$G/n64gltest.s" "$ARMPI_ROOT/build/mesa/sdroot/bin/n64gltest"
ls -l "$G/n64emu.s" "$G/n64gltest.s"
