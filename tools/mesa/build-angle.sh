#!/bin/sh
# build-angle : ANGLE seul pour ARMPI, a la revision amont de WebKit (docs/MESA.md).
#     sh tools/mesa/build-angle.sh
set -eu
. "$(dirname "$0")/env.sh"
REV=b8a7a626127c0010a557c9d6466fefd38d9477c1		# le WebKit amont de la copie d'Onyx
W=$A/src/angle-wk
S=$W/Source/ThirdParty/ANGLE
M=$ARMPI_ROOT/tools/mesa
if [ ! -d "$S/src" ]; then
	mkdir -p "$W"
	git -C "$W" init -q .
	git -C "$W" remote add origin https://github.com/WebKit/WebKit.git
	git -C "$W" sparse-checkout set Source/ThirdParty/ANGLE
	git -C "$W" fetch -q --depth 1 --filter=blob:none origin $REV
	git -C "$W" -c advice.detachedHead=false checkout -q FETCH_HEAD
fi
git -C "$W" checkout -q .
git -C "$W" apply "$M/angle/armpi.patch"
B=$OUT/angle
mkdir -p "$B/gen"
cat > "$OUT/angle-toolchain.cmake" <<TC
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER ${TC}gcc)
set(CMAKE_CXX_COMPILER ${TC}g++)
set(CMAKE_AR ${TC}ar)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_FLAGS "-specs=$POSIX/lib/posix.specs -mcpu=cortex-a72 -D_GNU_SOURCE -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS "-specs=$POSIX/lib/posix.specs -mcpu=cortex-a72 -D_GNU_SOURCE -ffunction-sections -fdata-sections")
TC
cmake -S "$M/angle" -B "$B" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$OUT/angle-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release \
	-DANGLE_SRC="$S" -DZLIB_INC="$OUT/zlib" -DGEN="$B/gen" > "$B/cmake.log"
ninja -C "$B" -k 0 -j "$JOBS"
ls -l "$B/libarmangle.a"

# angletest : relie a Mesa par libs.txt de build-armgl.sh
G=$OUT/armgl
${TC}gcc -specs=$POSIX/lib/posix.specs -mcpu=cortex-a72 -O2 -DEGL_EGLEXT_PROTOTYPES -DEGL_NO_PLATFORM_SPECIFIC_TYPES 	-DGL_GLEXT_PROTOTYPES -I"$S/include" -I"$M/armgl" -c "$M/angle/angletest.c" -o "$G/angletest.o"
LIBS=$(cat "$G/libs.txt")
if ! ${TC}g++ -specs=$POSIX/lib/posix.specs -Wl,--gc-sections "$G/angletest.o" -Wl,--start-group "$B/libarmangle.a" 	"$G/libarmgl.a" $LIBS "$WK/lib/libz.a" -Wl,--end-group -o "$G/angletest" 2> "$G/anglelink.log"; then
	grep -E "undefined reference|multiple definition" "$G/anglelink.log" | sed -E 's/.*(undefined reference to|multiple definition of)//' | sort | uniq -c | sort -rn | head -40
	tail -3 "$G/anglelink.log" | cut -c1-200
	exit 1
fi
${TC}strip -o "$G/angletest.s" "$G/angletest"
ls -l "$G/angletest.s"
cp "$G/angletest.s" "$ARMPI_ROOT/build/mesa/sdroot/bin/angletest"
