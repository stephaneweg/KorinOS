#!/bin/sh
# build-jet.sh -- Jet, le navigateur d'Onyx, sur ARMPI (src/extra/onyxports/jet, docs/WEBKIT.md, etape 5) :
# le moteur d'Onyx (engine_webkit.cpp, WebKit2 par son API C), l'habillage d'ARMPI (main.cpp sur UIKit)
# et la presentation (present.c : la page dans une surface attachee d'Elegant), lies a libWebKit.a
# (sh tools/mesa/build-webkit.sh) comme wk2test, avec ANGLE et Mesa pour WebGL (docs/MESA.md).
# -> $WK_OUT/out/jet.app/{main, app.txt, start.html} ; stage.sh le met sur la carte (SD0:/apps/jet.app).
#
#   sh tools/webkit/build-jet.sh
#   (sur ARMPI)  le dock, ou SD0:/apps/jet.app/main [adresse]
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
MESA_OUT=$HOME/armpi-mesa/out
: "${BUILD:=$MESA_OUT/webcore}"
unset CFLAGS CXXFLAGS LDFLAGS CC CXX
S=$ONYX_SYSROOT
HERE=$ARMPI/tools/webkit
J=$ARMPI/src/extra/onyxports/jet
U=$WK_OUT/uikit
[ -f "$BUILD/lib/libWebKit.a" ] || { echo "build-jet.sh : pas de $BUILD/lib/libWebKit.a : sh tools/mesa/build-webkit.sh" >&2; exit 1; }
[ -f "$ARMPI/prebuilt/mesa/lib/libarmgl.a" ] || { echo "build-jet.sh : pas de prebuilt/mesa : sh tools/mesa/build-armgl.sh" >&2; exit 1; }
[ -f "$U/libuikit_c.a" ] || sh "$HERE/build-ui.sh"

# les sources C++ avec la commande meme d'une source de WebKit (en-tetes prives, configuration)
ref=UIProcess/API/C/onyx/WKRunLoop.cpp
cmd=$(cd "$BUILD" && ninja -t commands lib/libWebKit.a | grep -F "$ref" | grep -F -- " -c " | head -n 1)
[ -n "$cmd" ] || { echo "build-jet.sh : pas de commande pour $ref dans $BUILD" >&2; exit 1; }
for f in engine_webkit main; do
	c=$(printf '%s' "$cmd" | sed -E 's/ -MD -MT [^ ]+ -MF [^ ]+//; s/ -o [^ ]+/ -o jet_'"$f"'.o/; s| -c [^ ]+| -c '"$J/$f"'.cpp -I'"$J"' -I'"$ARMPI"'/src/kits/uikit/include -I'"$U"'|')
	echo "jet : $f.cpp"
	( cd "$BUILD" && sh -c "$c" )
done
${ONYX_TOOLCHAIN_PREFIX}gcc -specs="$S/lib/onyx-cc.specs" -O2 -mcpu=cortex-a72 -I"$ARMPI/src/kits/uikit/include" -I"$U" \
	-c "$J/present.c" -o "$BUILD/jet_present.o"

WK="-Wl,--start-group $BUILD/lib/libWebKit.a $BUILD/lib/libWebCore.a $BUILD/lib/libPAL.a $BUILD/lib/libJavaScriptCore.a $BUILD/lib/libWTF.a $BUILD/lib/libbmalloc.a -Wl,--end-group"
LIBS=""
for l in skia harfbuzz-icu harfbuzz freetype png16 jpeg webpmux webpdemux webp sharpyuv curl ssl crypto \
	nghttp2 brotlidec xml2 sqlite3 z icui18n icuuc icudata; do
	LIBS="$LIBS $S/lib/lib$l.a"
done
# WebGL : ANGLE, puis Mesa par armgl
GL="-Wl,--start-group $BUILD/lib/libANGLE.a $BUILD/lib/libGLESv2.a $ARMPI/prebuilt/mesa/lib/libarmgl.a $ARMPI/prebuilt/mesa/lib/libmesa_v3d.a $S/lib/libz.a -Wl,--end-group"
[ -f "$WK_OUT/av/libonyxav.a" ] || sh "$HERE/build-media.sh"
${ONYX_TOOLCHAIN_PREFIX}gcc -specs="$S/lib/onyx-cc.specs" -O2 -mcpu=cortex-a72 -c "$HERE/glue/webkit2_armpi.c" -o "$BUILD/webkit2_armpi.o"
# gpucomp d'Onyx tel quel (le compositeur de LayerTreeHostOnyx) sur GpuKit : glue/gpucomp_armpi.c
GC="-specs=$S/lib/onyx-cc.specs -O2 -mcpu=cortex-a72 -I$HERE/glue/include -I$EXTRA/onyx-libs/gpucomp"
${ONYX_TOOLCHAIN_PREFIX}gcc $GC -c "$EXTRA/onyx-libs/gpucomp/gpucomp.c" -o "$BUILD/gpucomp.o"
${ONYX_TOOLCHAIN_PREFIX}gcc $GC -c "$HERE/glue/gpucomp_armpi.c" -o "$BUILD/gpucomp_armpi.o"
mkdir -p "$BUILD/bin" "$WK_OUT/out/jet.app"
echo "jet : edition de liens"
${ONYX_TOOLCHAIN_PREFIX}g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	"$BUILD/jet_main.o" "$BUILD/jet_engine_webkit.o" "$BUILD/jet_present.o" "$BUILD/webkit2_armpi.o" "$BUILD/gpucomp.o" "$BUILD/gpucomp_armpi.o" $WK \
	"$U/libuikit_c.a" "$WK_OUT/av/libonyxav.a" $LIBS $GL -o "$BUILD/bin/jet"
${ONYX_TOOLCHAIN_PREFIX}size "$BUILD/bin/jet"
${STRIP} -o "$WK_OUT/out/jet.app/main" "$BUILD/bin/jet"
cp "$J/app.txt" "$J/start.html" "$WK_OUT/out/jet.app/"
ls -l "$WK_OUT/out/jet.app"
