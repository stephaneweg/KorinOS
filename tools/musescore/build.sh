#!/bin/sh
# build.sh : MuseScore Studio pour Korin PC (docs/etude/MUSESCORE.md), lié en statique au Qt de Korin (tools/qt).
#
#   sh tools/musescore/build.sh            (configure, construit, installe dans $MINST)
#   STEP=configure sh tools/musescore/build.sh
#
# Coupés : mises à jour, Muse Sounds (MuseSampler, propriétaire), VST et greffons audio,
# rapports de plantage (crashpad), export vidéo (FFmpeg), import MNX (récupéré sur GitHub à la configuration),
# Learn (vidéos en ligne), les essais. FreeType et HarfBuzz : ceux des paquets -dev, comme Qt (sinon deux versions
# dans le même binaire statique : glyphes faux dans les paroles). Son : le pilote Korin sur AudioKit (korin/korinaudiodriver.cpp).
set -e
. "$(dirname "$0")/env.sh"
[ -f "$PREFIX/lib/libQt6Core5Compat.a" ] || { echo "build.sh : d'abord sh tools/qt/build.sh (avec qt5compat, qtnetworkauth)" >&2; exit 1; }
sh "$MST/fetch.sh" >/dev/null
sh "$MST/mkpatch.sh" apply
# les fichiers nouveaux de Korin (pilote audio, thème de plateforme)
while read -r dst src; do
	[ -n "$dst" ] || continue
	mkdir -p "$MSRC/$(dirname "$dst")"
	cmp -s "$MST/korin/$src" "$MSRC/$dst" || cp "$MST/korin/$src" "$MSRC/$dst"
done < "$MST/korin/FILES"

# (les paquets -dev, /devtools/gcc dans Korin : chemins par défaut du GCC de l'OS ; ici, ajoutés aux drapeaux de la
# chaîne par CFLAGS, CXXFLAGS, LDFLAGS : zlib que MuseScore lie par son nom)
D=$DEVSYS/devtools/gcc
env CFLAGS="-isystem $D/include" CXXFLAGS="-isystem $D/include" LDFLAGS="-L$D/lib" KORIN_POSIX=$POSIX KORIN_QT_DEPS=$DEVSYS/devtools/gcc KORIN_UIKIT_LIBDIR=$UIKIT_LIBDIR \
	PKG_CONFIG_SYSROOT_DIR=$DEVSYS PKG_CONFIG_LIBDIR=$DEVSYS/devtools/gcc/lib/pkgconfig PKG_CONFIG_PATH= \
	cmake -S "$MSRC" -B "$MOBJ" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$QTT/korin.cmake" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="$PREFIX" -DQT_HOST_PATH="$HOSTQT" -DQt6_DIR="$PREFIX/lib/cmake/Qt6" \
	-DCMAKE_INSTALL_PREFIX="$MINST" -DCMAKE_SKIP_RPATH=ON \
	-DBUILD_CONFIGURATION=APP -DMUSE_APP_BUILD_MODE=release \
	-DMUSE_COMPILE_USE_COMPILER_CACHE=OFF -DMUSE_ENABLE_UNIT_TESTS=OFF \
	-DMUSE_MODULE_CLOUD=ON -DMUSE_MODULE_UPDATE=OFF -DMUSE_MODULE_MUSESAMPLER=OFF -DMUSE_MODULE_VST=OFF \
	-DMUSE_MODULE_AUDIOPLUGINS=OFF -DMUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT=OFF -DMUSE_MODULE_LEARN=OFF \
	-DMUSE_MODULE_AUTOBOT=OFF -DMUSE_MODULE_MULTIWINDOWS=OFF \
	-DMUE_BUILD_MUSESOUNDS_MODULE=OFF -DMUE_BUILD_MUSESOUNDS_QML=ON -DMUE_BUILD_IMPEXP_VIDEOEXPORT_MODULE=OFF -DMUE_BUILD_IMPEXP_MNX_MODULE=OFF \
	-DMUE_BUILD_ENGRAVING_TESTS=OFF -DMUE_BUILD_IMPORTEXPORT_TESTS=OFF -DMUE_BUILD_NOTATION_TESTS=OFF \
	-DMUE_BUILD_PROJECT_TESTS=OFF -DMUE_BUILD_PLAYBACK_TESTS=OFF -DMUE_BUILD_CONVERTER_TESTS=OFF \
	-DMUE_BUILD_BRAILLE_TESTS=OFF -DMUE_BUILD_MUSESOUNDS_TESTS=OFF -DMUE_BUILD_NOTATIONSCENE_TESTS=OFF \
	\
	-DMUE_COMPILE_USE_SYSTEM_FREETYPE=ON -DMUE_COMPILE_USE_SYSTEM_HARFBUZZ=ON -DCMAKE_MODULE_PATH="$MST/cmake" \
	-DMUE_DOWNLOAD_SOUNDFONT=OFF -DMUE_RUN_LRELEASE=OFF ${MS_EXTRA:-}
[ "$STEP" = configure ] && exit 0
ninja -C "$MOBJ" -j "$JOBS" ${NINJA_FLAGS:-}
ninja -C "$MOBJ" install
