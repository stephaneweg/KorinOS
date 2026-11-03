#!/bin/sh
# build-wctest.sh -- wctest (tools/webkit/wctest.cpp, d'Onyx : une page HTML chargee, mise en page et
# peinte dans un PNG par WebCore seul -- sans WebKit2, sans reseau) pour ARMPI, contre la construction
# de build-webcore.sh. -> $WK_OUT/out/wctest (depouille ; stage.sh le met sur la carte : SD0:/bin/wctest)
#
#   sh tools/webkit/build-webcore.sh              # libWebCore.a d'abord
#   sh tools/webkit/build-wctest.sh
#   (sur ARMPI)  wctest SD0:/webkit/page1.html RAM0:/page1.png
#                wctest --view SD0:/webkit/page1.html     (la page dans une fenetre : src/extra/onyxports/jet/present.h)
#
# Compile avec la commande meme des sources de WebCore (prise a ninja) : en-tetes prives et
# configuration de WebCore.
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
: "${BUILD:=$WK_OUT/webcore}"
unset CFLAGS CXXFLAGS LDFLAGS CC CXX
S=$ONYX_SYSROOT
HERE=$ARMPI/tools/webkit

[ -f "$BUILD/lib/libWebCore.a" ] || { echo "build-wctest.sh : pas de $BUILD/lib/libWebCore.a : sh tools/webkit/build-webcore.sh" >&2; exit 1; }

ref=platform/onyx/PlatformScreenOnyx.cpp
cmd=$(cd "$BUILD" && ninja -t commands lib/libWebCore.a | grep -F "$ref" | grep -F -- " -c " | head -n 1)
[ -n "$cmd" ] || { echo "build-wctest.sh : pas de commande pour $ref dans $BUILD" >&2; exit 1; }
cmd=$(printf '%s' "$cmd" | sed -E 's/ -MD -MT [^ ]+ -MF [^ ]+//; s/ -o [^ ]+/ -o wctest.o/; s| -c [^ ]+| -c '"$HERE"'/wctest.cpp -I'"$ARMPI"'/src/extra/onyxports/jet|')
echo "wctest : compilation"
( cd "$BUILD" && sh -c "$cmd" )

# les bibliotheques de WebKit, puis celles du sysroot dans l'ordre des dependances
WK="-Wl,--start-group $BUILD/lib/libWebCore.a $BUILD/lib/libPAL.a $BUILD/lib/libJavaScriptCore.a $BUILD/lib/libWTF.a $BUILD/lib/libbmalloc.a -Wl,--end-group"
LIBS=""
for l in skia harfbuzz-icu harfbuzz freetype png16 jpeg webpmux webpdemux webp sharpyuv curl ssl crypto \
	nghttp2 brotlidec xml2 sqlite3 z icui18n icuuc icudata; do
	LIBS="$LIBS $S/lib/lib$l.a"
done
# la fenetre (--view) : src/extra/onyxports/jet/present.c sur UIKit (build-ui.sh) ; le moteur media : libonyxav
# (build-media.sh : son par AudioKit, video au point de branchement de MediaKit)
[ -f "$WK_OUT/uikit/libuikit_c.a" ] || sh "$HERE/build-ui.sh"
[ -f "$WK_OUT/av/libonyxav.a" ] || sh "$HERE/build-media.sh"
${ONYX_TOOLCHAIN_PREFIX}gcc -specs="$S/lib/onyx-cc.specs" -O2 -mcpu=cortex-a72 -I"$ARMPI/src/kits/uikit/include" -I"$WK_OUT/uikit" -c "$ARMPI/src/extra/onyxports/jet/present.c" -o "$BUILD/present.o"
EXTRA_O="$BUILD/present.o $WK_OUT/uikit/libuikit_c.a $WK_OUT/av/libonyxav.a"
mkdir -p "$BUILD/bin" "$WK_OUT/out"
echo "wctest : edition de liens"
${ONYX_TOOLCHAIN_PREFIX}g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	"$BUILD/wctest.o" $WK $EXTRA_O $LIBS -o "$BUILD/bin/wctest"
${ONYX_TOOLCHAIN_PREFIX}size "$BUILD/bin/wctest"
${STRIP} -o "$WK_OUT/out/wctest" "$BUILD/bin/wctest"
ls -l "$WK_OUT/out/wctest"
