#!/bin/sh
# build-skgltest : skgltest, Skia Ganesh (build-skia.sh) relie en statique a Mesa (build-armgl.sh).
#     sh tools/mesa/build-skgltest.sh
set -eu
. "$(dirname "$0")/env.sh"
G=$OUT/armgl
B=$OUT/build
M=$ARMPI_ROOT/tools/mesa
S=$HOME/armpi-avalonia/src/skia
K=$OUT/skia
U=$HOME/armpi-webkit-build/uikit
${TC}g++ -specs=$POSIX/lib/posix.specs -mcpu=cortex-a72 -O2 -std=c++17 -w -fpermissive -DSK_GANESH -DSK_GL -DSK_ASSUME_GL_ES=1 \
	-I"$S" -I"$M/armgl" -I"$ARMPI_ROOT/src/kits/uikit/include" -I"$U" -c "$M/armgl/skgltest.cpp" -o "$G/skgltest.o"

LIBS=$(cat "$G/libs.txt")
if ! ${TC}g++ -specs=$POSIX/lib/posix.specs -Wl,--gc-sections "$G/skgltest.o" "$U/libuikit_c.a" -Wl,--start-group "$G/libarmgl.a" $LIBS \
	"$K/libskia.a" "$K/libskcms.a" "$WK/lib/libfreetype.a" "$WK/lib/libpng16.a" "$WK/lib/libjpeg.a" \
	"$WK/lib/libwebpdemux.a" "$WK/lib/libwebpmux.a" "$WK/lib/libwebp.a" "$WK/lib/libsharpyuv.a" "$WK/lib/libz.a" \
	"$WK/lib/libbrotlidec.a" -Wl,--end-group -o "$G/skgltest" 2> "$G/sklink.log"; then
	grep -E "undefined reference" "$G/sklink.log" | sed 's/.*undefined reference to//' | sort | uniq -c | sort -rn | head -40
	tail -3 "$G/sklink.log" | cut -c1-200
	exit 1
fi
${TC}strip -o "$G/skgltest.s" "$G/skgltest"
ls -l "$G/skgltest.s"
cp "$G/skgltest.s" "$ARMPI_ROOT/build/mesa/sdroot/bin/skgltest"
