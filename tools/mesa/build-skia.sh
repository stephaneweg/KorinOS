#!/bin/sh
# build-skia : Skia m119 avec Ganesh sur OpenGL ES, variante de tools/avalonia/build-skia.sh : meme source
# (~/armpi-avalonia/src/skia, seulement lue), memes options, sortie dans ~/armpi-mesa/out/skia.
#     sh tools/mesa/build-skia.sh
set -eu
. "$(dirname "$0")/env.sh"
AV=$HOME/armpi-avalonia
S=$AV/src/skia
D=$AV/deps
FLAGS=""
for f in -mcpu=cortex-a72 -specs=$WK/lib/onyx-cc.specs -D_GNU_SOURCE -DSK_BUILD_FOR_UNIX -DSKIA_C_DLL -w -fpermissive -O2 -ffunction-sections -fdata-sections -I$D/include -I$D/include/freetype2 -include$D/include/armpi_skia.h; do
	FLAGS="$FLAGS\"$f\","
done
ARGS="target_os=\"linux\" target_cpu=\"arm64\" is_official_build=true is_debug=false is_static_skiasharp=true
	cc=\"${TC}gcc\" cxx=\"${TC}g++\" ar=\"${TC}ar\"
	extra_cflags=[$FLAGS] extra_asmflags=[]
	skia_enable_ganesh=true skia_enable_graphite=false skia_use_gl=true skia_gl_standard=\"gles\" skia_use_vulkan=false
	skia_use_egl=false skia_use_x11=false
	skia_enable_skottie=false skia_enable_svg=false skia_enable_pdf=false skia_use_xps=false skia_enable_tools=false
	skia_use_harfbuzz=false skia_use_icu=false skia_use_piex=false skia_use_sfntly=false skia_use_expat=false
	skia_use_fontconfig=false skia_use_freetype=true skia_use_perfetto=false skia_use_dng_sdk=false skia_use_wuffs=false
	skia_use_system_freetype2=true skia_use_system_libjpeg_turbo=true skia_use_system_libpng=true
	skia_use_system_libwebp=true skia_use_system_zlib=true skia_use_libgrapheme=false"
B=$OUT/skia
mkdir -p "$B"
(cd "$S" && bin/gn gen "$B" --args="$(echo "$ARGS" | tr '	' ' ')")
ninja -C "$B" -j "$JOBS" -k 0 SkiaSharp
ls -l "$B"/*.a
