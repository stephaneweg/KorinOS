#!/bin/sh
# build_n3ds_bench.sh -- le banc du coeur 3DS sous qemu-aarch64 (docs/EMULATEURS.md) : run_n3ds relie a la reference
# (coeur d'Onyx, Dynarmic pas a pas) et au port. Sortie dans $OUT (par defaut ~/n3ds) :
#   $OUT/ref/run_n3ds     la reference
#   $OUT/port/run_n3ds    le port
# Dans WSL, depuis la racine du depot. Dynarmic : la construction de tools/tests/run_n3ds_test.sh d'Onyx, dans
# $OUT/ref/dynarmic (make -C $ONYX/user/Libs/dynarmic OUT=...).
#     sh tools/emu/build_n3ds_bench.sh [ref] [port]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx}
OUT=${OUT:-$HOME/n3ds}
export PATH=$HOME/toolchains/arm-gnu-toolchain-14.2.rel1-x86_64-aarch64-none-elf/bin:$PATH
ARCH="-mcpu=cortex-a72 -mno-outline-atomics -ffunction-sections -fdata-sections -fno-math-errno -fno-trapping-math"
FLAGS="-std=c++17 -O2 -w -fno-exceptions -fno-rtti $ARCH"
S=$ONYX/tools/tests
D=$ONYX/user/Libs/dynarmic
DYN=$ONYX/third_party/dynarmic-a466015
SHIM="$OUT/shim.o $OUT/t0shim.o"
mkdir -p "$OUT/ref/core" "$OUT/port/core"
[ -f "$OUT/shim.o" ] || aarch64-none-elf-gcc -O2 -c "$S/basic/a64/linux_shim.c" -o "$OUT/shim.o"
[ -f "$OUT/t0shim.o" ] || aarch64-none-elf-gcc -O2 -I"$D/onyx" -c "$S/n3ds/t0_shim.c" -o "$OUT/t0shim.o"
[ $# -gt 0 ] || set -- ref port
for what; do
case $what in
ref)
	[ -f "$OUT/ref/dynarmic/libdynarmic.a" ] || make -C "$D" -j16 OUT="$OUT/ref/dynarmic" > "$OUT/dyn.log" 2>&1
	INC="-I$DYN/src -I$DYN/externals/mcl/include -I$DYN/externals/fmt/include -isystem $ONYX/third_party/boost-1.86.0 -I$D/onyx -include $D/onyx/onyx_std_mutex.h"
	objs=""
	for f in "$ONYX"/user/Emulators/n3ds/*.cpp; do
		case $f in *n3ds_cpu.cpp) continue;; esac
		o=$OUT/ref/core/$(basename "$f" .cpp).o
		[ "$o" -nt "$f" ] || aarch64-none-elf-g++ $FLAGS -I"$ONYX/user/Emulators" -I"$ONYX/user/Libs" -I"$ONYX/tools/qpu/mesa" -c "$f" -o "$o" &
		objs="$objs $o"
	done
	wait
	aarch64-none-elf-g++ -std=c++20 -O2 -w $ARCH $INC -I"$ONYX/user/Emulators" -c "$ROOT/tools/emu/ref_n3ds_cpu.cpp" -o "$OUT/ref/core/refcpu.o"
	aarch64-none-elf-g++ $FLAGS -I"$ONYX/user/Emulators" -c "$ROOT/tools/emu/run_n3ds.cpp" -o "$OUT/ref/core/run.o"
	# (le generateur de shaders d'Onyx : le chemin du GPU n'y sert pas, mais le coeur s'y relie)
	V3D=$ONYX/user/Libs/v3d
	vo=""
	for src in "$V3D/picatev.cpp" "$V3D/picavs.cpp" "$V3D/qpu.cpp" "$V3D/shaders.cpp"; do
		o=$OUT/ref/core/v3d_$(basename "$src" .cpp).o
		[ -f "$o" ] || aarch64-none-elf-g++ $FLAGS -I"$ONYX/user/Libs" -I"$ONYX/tools/qpu/mesa" -c "$src" -o "$o"
		vo="$vo $o"
	done
	for src in "$ONYX/tools/qpu/mesa/broadcom/qpu/qpu_pack.c" "$ONYX/tools/qpu/mesa/broadcom/qpu/qpu_instr.c" "$V3D/shim.c"; do
		o=$OUT/ref/core/v3d_$(basename "$src" .c).o
		[ -f "$o" ] || aarch64-none-elf-gcc -std=gnu11 -O2 $ARCH -DNDEBUG -w -I"$ONYX/tools/qpu/mesa" -c "$src" -o "$o"
		vo="$vo $o"
	done
	aarch64-none-elf-g++ $ARCH -static -nostartfiles --specs=nosys.specs -Wl,--gc-sections $objs $vo "$OUT/ref/core/refcpu.o" "$OUT/ref/core/run.o" \
		"$OUT/ref/dynarmic/libdynarmic.a" $SHIM -o "$OUT/ref/run_n3ds" 2>&1 | grep -v "is not implemented and will always fail\|in function" || true
	echo "reference : $(stat -c %s "$OUT/ref/run_n3ds") octets"
	;;
port)
	objs=""
	for f in "$ROOT"/src/apps/n3dsemu/n3ds/*.cpp; do
		o=$OUT/port/core/$(basename "$f" .cpp).o
		[ "$o" -nt "$f" ] && [ "$o" -nt "$ROOT/src/apps/n3dsemu/n3ds/n3ds.h" ] && [ "$o" -nt "$ROOT/src/apps/n3dsemu/n3ds/n3ds_arm.h" ] || aarch64-none-elf-g++ $FLAGS -I"$ROOT/src/apps/n3dsemu" -I"$D/onyx" -I"$HOME/armpi-mesa/src/mesa-26.2.0/include" -c "$f" -o "$o" &
		objs="$objs $o"
	done
	wait
	aarch64-none-elf-g++ $FLAGS -DN3DS_PORT -I"$ROOT/src/apps/n3dsemu" -c "$ROOT/tools/emu/run_n3ds.cpp" -o "$OUT/port/core/run.o"
	aarch64-none-elf-gcc -mcpu=cortex-a72 -c "$ROOT/src/apps/n3dsemu/n3ds/n3ds_kern.S" -o "$OUT/port/core/kern.o"
	aarch64-none-elf-g++ $ARCH -static -nostartfiles --specs=nosys.specs -Wl,--gc-sections $objs "$OUT/port/core/kern.o" "$OUT/port/core/run.o" $SHIM -o "$OUT/port/run_n3ds" 2>&1 | grep -v "is not implemented and will always fail\|in function" || true
	echo "port : $(stat -c %s "$OUT/port/run_n3ds") octets"
	;;
replay)								# (le rejeu sur le PC, par llvmpipe : le Mesa du systeme)
	M=$HOME/armpi-mesa
	g++ -std=gnu++17 -O1 -g -w -I"$ROOT/src/apps/n3dsemu" -I"$M/src/mesa-26.2.0/include" "$ROOT/tools/emu/n3ds_replay.cpp" "$ROOT/src/apps/n3dsemu/n3ds/n3ds_gl.cpp" "$ROOT/src/apps/n3dsemu/n3ds/n3ds_glsl.cpp" /usr/lib/x86_64-linux-gnu/libEGL.so.1 -ldl -o "$OUT/n3ds_replay"
	echo "rejeu : $(stat -c %s "$OUT/n3ds_replay") octets"
	;;
esac
done
