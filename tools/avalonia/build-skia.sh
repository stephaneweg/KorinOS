#!/bin/sh
# build-skia : libSkiaSharp et libHarfBuzzSharp en bibliotheques statiques pour ARMPI, a partir de la
# branche de Skia que vise SkiaSharp 3.119.4 (m119, mono/skia) et de HarfBuzz 8.3.1. Rendu logiciel
# seul : ni GPU, ni skottie, ni PDF, ni fontconfig. Les polices viennent de SD0:/fonts.
#     sh tools/avalonia/build-skia.sh        (depuis la racine du depot, dans WSL)
set -eu
. "$(dirname "$0")/env.sh"
S=$SRC/skia
if [ ! -d "$S/.git" ]; then
	mkdir -p "$S"
	(cd "$S" && git init -q && git remote add origin https://github.com/mono/skia.git &&
		git fetch -q --depth 1 origin $SKIA_COMMIT && git checkout -q FETCH_HEAD)
fi
[ -x "$S/bin/gn" ] || (cd "$S" && python3 bin/fetch-gn)
(cd "$S" && git diff --quiet BUILD.gn && git apply "$ARMPI_ROOT/tools/avalonia/skia-armpi.patch" || true)

# seulement les en-tetes des dependances : le reste du sysroot de WebKit cacherait libposix
D=$A/deps
rm -rf "$D"
mkdir -p "$D/include"
for h in ft2build.h freetype2 png.h pngconf.h pnglibconf.h jpeglib.h jconfig.h jerror.h jmorecfg.h webp zlib.h zconf.h; do
	ln -sf "$WK/include/$h" "$D/include/$h"
done

echo '#define SK_FONT_FILE_PREFIX "SD0:/fonts/"' > "$D/include/armpi_skia.h"
FLAGS=""
for f in -mcpu=cortex-a72 -specs=$WK/lib/onyx-cc.specs -D_GNU_SOURCE -DSK_BUILD_FOR_UNIX -DSKIA_C_DLL -w -fpermissive -O2	-ffunction-sections -fdata-sections -I$D/include -I$D/include/freetype2 -include$D/include/armpi_skia.h; do
	FLAGS="$FLAGS\"$f\","
done
ARGS="target_os=\"linux\" target_cpu=\"arm64\" is_official_build=true is_debug=false is_static_skiasharp=true
	cc=\"${TC}gcc\" cxx=\"${TC}g++\" ar=\"${TC}ar\"
	extra_cflags=[$FLAGS] extra_asmflags=[]
	skia_enable_ganesh=false skia_enable_gpu=false skia_use_gl=false skia_use_vulkan=false skia_use_egl=false skia_use_x11=false
	skia_enable_skottie=false skia_enable_svg=false skia_enable_pdf=false skia_use_xps=false skia_enable_tools=false
	skia_use_harfbuzz=false skia_use_icu=false skia_use_piex=false skia_use_sfntly=false skia_use_expat=false
	skia_use_fontconfig=false skia_use_freetype=true skia_use_perfetto=false skia_use_dng_sdk=false skia_use_wuffs=false
	skia_use_system_freetype2=true skia_use_system_libjpeg_turbo=true skia_use_system_libpng=true
	skia_use_system_libwebp=true skia_use_system_zlib=true skia_use_libgrapheme=false"
B=$OUT/skia
mkdir -p "$B"
(cd "$S" && bin/gn gen "$B" --args="$(echo "$ARGS" | tr '	' ' ')")
ninja -C "$B" -j "$JOBS" -k 0 SkiaSharp

# HarfBuzz : le fichier unique, comme HarfBuzzSharp (sans FreeType ni ICU)
H=$SRC/harfbuzz
[ -d "$H" ] || git clone -q --depth 1 --branch $HARFBUZZ_TAG https://github.com/harfbuzz/harfbuzz "$H"
mkdir -p "$OUT/hb"
${TC}g++ -specs="$WK/lib/onyx-cc.specs" -mcpu=cortex-a72 -O2 -w -fno-rtti -fno-exceptions -fno-threadsafe-statics \
	-ffunction-sections -fdata-sections -DHAVE_OT -DHB_NO_FALLBACK_SHAPE \
	-c "$H/src/harfbuzz.cc" -o "$OUT/hb/harfbuzz.o"
rm -f "$OUT/hb/libHarfBuzzSharp.a"
${TC}ar rcs "$OUT/hb/libHarfBuzzSharp.a" "$OUT/hb/harfbuzz.o"
ls -l "$B"/*.a "$OUT/hb/libHarfBuzzSharp.a"
