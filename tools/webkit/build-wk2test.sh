#!/bin/sh
# build-wk2test.sh -- wk2test (tools/webkit/wk2test.cpp, d'Onyx : WebKit2 sans fenetre, un programme
# qui est le process UI et, relance par WebKit, le process web et le process reseau) pour ARMPI,
# contre libWebKit.a (TARGET=WebKit sh tools/webkit/build-webcore.sh) : l'edition de liens complete de
# WebKit2 sur libposix, avec la glue d'ARMPI (glue/webkit2_armpi.c : pas de gpucomp, pas d'app cores,
# surfaces sur BUF_*) et le moteur media (libonyxav). -> $WK_OUT/out/wk2test
#
# Sur ARMPI (depuis l'IPC entre process du noyau, 30/10/2026 : docs/WEBKIT.md, « WebKit2 ») :
#   preload SD0:/bin/wk2test ; wk2test /webkit/page1.html /wk2.png
# (une adresse en /chemin : file://SD0:/x n'est pas une URL valide pour WebKit).
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
: "${BUILD:=$WK_OUT/webcore}"
unset CFLAGS CXXFLAGS LDFLAGS CC CXX
S=$ONYX_SYSROOT
HERE=$ARMPI/tools/webkit
[ -f "$BUILD/lib/libWebKit.a" ] || { echo "build-wk2test.sh : pas de $BUILD/lib/libWebKit.a : TARGET=WebKit sh tools/webkit/build-webcore.sh" >&2; exit 1; }

ref=UIProcess/API/C/onyx/WKRunLoop.cpp
cmd=$(cd "$BUILD" && ninja -t commands lib/libWebKit.a | grep -F "$ref" | grep -F -- " -c " | head -n 1)
[ -n "$cmd" ] || { echo "build-wk2test.sh : pas de commande pour $ref dans $BUILD" >&2; exit 1; }
cmd=$(printf '%s' "$cmd" | sed -E 's/ -MD -MT [^ ]+ -MF [^ ]+//; s/ -o [^ ]+/ -o wk2test.o/; s| -c [^ ]+| -c '"$HERE"'/wk2test.cpp|')
echo "wk2test : compilation"
( cd "$BUILD" && sh -c "$cmd" )

WK="-Wl,--start-group $BUILD/lib/libWebKit.a $BUILD/lib/libWebCore.a $BUILD/lib/libPAL.a $BUILD/lib/libJavaScriptCore.a $BUILD/lib/libWTF.a $BUILD/lib/libbmalloc.a -Wl,--end-group"
LIBS=""
for l in skia harfbuzz-icu harfbuzz freetype png16 jpeg webpmux webpdemux webp sharpyuv curl ssl crypto \
	nghttp2 brotlidec xml2 sqlite3 z icui18n icuuc icudata; do
	LIBS="$LIBS $S/lib/lib$l.a"
done
[ -f "$WK_OUT/av/libonyxav.a" ] || sh "$HERE/build-media.sh"
${ONYX_TOOLCHAIN_PREFIX}gcc -specs="$S/lib/onyx-cc.specs" -O2 -mcpu=cortex-a72 -c "$HERE/glue/webkit2_armpi.c" -o "$BUILD/webkit2_armpi.o"
# gpucomp d'Onyx tel quel (le compositeur de LayerTreeHostOnyx), comme build-jet.sh
GC="-specs=$S/lib/onyx-cc.specs -O2 -mcpu=cortex-a72 -I$HERE/glue/include -I$EXTRA/onyx-libs/gpucomp"
${ONYX_TOOLCHAIN_PREFIX}gcc $GC -c "$EXTRA/onyx-libs/gpucomp/gpucomp.c" -o "$BUILD/gpucomp.o"
${ONYX_TOOLCHAIN_PREFIX}gcc $GC -c "$HERE/glue/gpucomp_armpi.c" -o "$BUILD/gpucomp_armpi.o"
mkdir -p "$BUILD/bin" "$WK_OUT/out"
echo "wk2test : edition de liens"
${ONYX_TOOLCHAIN_PREFIX}g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	"$BUILD/wk2test.o" "$BUILD/webkit2_armpi.o" "$BUILD/gpucomp.o" "$BUILD/gpucomp_armpi.o" $WK "$WK_OUT/av/libonyxav.a" $LIBS -o "$BUILD/bin/wk2test"
${ONYX_TOOLCHAIN_PREFIX}size "$BUILD/bin/wk2test"
${STRIP} -o "$WK_OUT/out/wk2test" "$BUILD/bin/wk2test"
ls -l "$WK_OUT/out/wk2test"
