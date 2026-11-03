#!/bin/sh
# n64gl_check : le rendu N64 par GL sans GPU, sur le PC (WSL, racine du depot, apres avoir assemble n64_harness.asm) :
# le combineur et le blender contre un calcul independant, puis, avec une ROM, de vraies images rejouees par llvmpipe.
#     sh tools/emu/n64gl_check.sh [essais]
#     sh tools/emu/n64gl_check.sh rom images premiere pas sortie     -> build/emu/gl/sortie/f*.ppm
# EGL et GLES : ceux du Mesa de WSL ; les en-tetes : ceux de ~/armpi-mesa.
set -eu
. "$(dirname "$0")/../mesa/env.sh"
W=$ARMPI_ROOT/build/emu/gl
N=$ARMPI_ROOT/src/apps/n64emu/gl
T=$ARMPI_ROOT/tools/emu
EGL=/usr/lib/x86_64-linux-gnu/libEGL.so.1
export LIBGL_ALWAYS_SOFTWARE=1 EGL_PLATFORM=surfaceless
mkdir -p "$W"
if [ $# -lt 5 ]; then
	gcc -O1 -g -w -I"$N" -I"$T" -I"$SRC/include" "$T/n64gl_unit.c" "$N/n64gl.c" $EGL -lm -o "$W/n64gl_unit"
	"$W/n64gl_unit" ${1:-2000}
	exit
fi
gcc -O1 -g -w -I"$N" -I"$SRC/include" "$T/n64gl_replay.c" "$N/n64gl.c" $EGL -o "$W/n64gl_replay"
chmod +x "$ARMPI_ROOT/build/emu/n64_harness"
qemu-aarch64 "$ARMPI_ROOT/build/emu/n64_harness" "$1" "$2" g "$W/$5.dump" "$3" "$4" > /dev/null
mkdir -p "$W/$5"
"$W/n64gl_replay" "$W/$5.dump" "$W/$5"
