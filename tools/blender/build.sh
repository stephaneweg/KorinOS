#!/bin/sh
# build.sh : Blender pour Korin PC (docs/etude/BLENDER.md), sur les dépendances de deps.sh et le Python de python.sh.
#
#   sh tools/blender/build.sh               (dans WSL, depuis la racine du dépôt ; jalon 2 : sans écran)
#   CONFIG=headless sh tools/blender/build.sh configure   (seulement la configuration CMake)
#
# Configuration : tools/blender/config-$CONFIG.cmake (cache initial). Sortie : $OUT/blender-$CONFIG (objets) et
# $OUT/install-$CONFIG (l'arbre portable : blender et 5.2/{datafiles,scripts,python}).
# Une longue construction : setsid nohup … & dans WSL ; JOBS au plus 4 (WSL : 9 Go).
set -e
. "$(dirname "$0")/env.sh"
CONFIG=${CONFIG:-headless}
B=$OUT/blender-$CONFIG
I=$OUT/install-$CONFIG
PY=$OUT/python/stage/devtools/python
# ce que les modules intégrés à libpython demandent (python.sh les range dans lib/korin-deps), après libpython
PYDEPS=$(for l in mpdec Hacl_Hash_SHA2 expat sqlite3 ffi ssl crypto; do printf "%s;" "$PY/lib/korin-deps/lib$l.a"; done)
PYDEPS=${PYDEPS%;}
[ -f "$PY/lib/libpython3.13.a" ] || { echo "build.sh : pas de Python (sh tools/blender/python.sh)" >&2; exit 1; }
[ -f "$OBJ/openimageio/.fait" ] || { echo "build.sh : dépendances absentes (sh tools/blender/deps.sh)" >&2; exit 1; }
# les outils de la construction (datatoc, shader_tool, makesdna, makesrna) pour l'hôte : hosttools.sh
HOSTBIN=$K/host/blender/bin
[ -x "$HOSTBIN/makesrna" ] || { echo "build.sh : pas d'outils pour l'hôte (sh tools/blender/hosttools.sh)" >&2; exit 1; }

# Les retouches de Blender (tools/blender/patches/blender.patch), comme celles des dépendances.
p=$BL/patches/blender.patch
if [ -f "$p" ] && ! cmp -s "$p" "$BSRC/.korin-patch"; then
	[ -f "$BSRC/.korin-patch" ] && { echo "build.sh : ancienne retouche dans $BSRC : refaire la source" >&2; exit 1; }
	(cd "$BSRC" && patch -p1 < "$p") && cp "$p" "$BSRC/.korin-patch"
fi

KORIN_POSIX=$POSIX KORIN_BLENDER_DEPS=$DEPS cmake -S "$BSRC" -B "$B" -G Ninja \
	-C "$BL/config-$CONFIG.cmake" \
	-DCMAKE_TOOLCHAIN_FILE="$BL/korin.cmake" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_INSTALL_PREFIX="$I" -DCMAKE_PREFIX_PATH="$DEPS" \
	-DCMAKE_CROSSCOMPILING_EMULATOR="$BL/hosttool.sh;$HOSTBIN" \
	-DPYTHON_ROOT_DIR="$PY" -DPYTHON_VERSION=3.13 \
	-DPYTHON_INCLUDE_DIR="$PY/include/python3.13" -DPYTHON_LIBRARY="$PY/lib/libpython3.13.a;$PYDEPS" \
	-DPYTHON_LIBPATH="$PY/lib" -DPYTHON_EXECUTABLE="$K/host-python/bin/python3.13" \
	-DZLIB_LIBRARY="$DEPS/lib/libz.a" -DZLIB_INCLUDE_DIR="$DEPS/include" \
	-DPNG_LIBRARY="$DEPS/lib/libpng16.a" -DPNG_PNG_INCLUDE_DIR="$DEPS/include" \
	-DJPEG_LIBRARY="$DEPS/lib/libjpeg.a" -DJPEG_INCLUDE_DIR="$DEPS/include" \
	-DFREETYPE_LIBRARY="$DEPS/lib/libfreetype.a" -DBROTLI_ROOT_DIR="$DEPS" -DFREETYPE_INCLUDE_DIRS="$DEPS/include/freetype2" \
	-DEPOXY_ROOT_DIR="$DEPS" -DZSTD_ROOT_DIR="$DEPS" -DTBB_ROOT_DIR="$DEPS" -DWEBP_ROOT_DIR="$DEPS" \
	-DOPENEXR_ROOT_DIR="$DEPS" -DOpenImageIO_ROOT="$DEPS" -DOpenColorIO_ROOT="$DEPS" -Dfmt_ROOT="$DEPS" \
	-Dexpat_ROOT="$DEPS" -Dpystring_ROOT="$DEPS" -DImath_ROOT="$DEPS" -Dminizip-ng_ROOT="$DEPS" \
	-Dminizip-ng_INCLUDE_DIR="$DEPS/include/minizip-ng/minizip" -Dminizip-ng_LIBRARY="$DEPS/lib/libminizip.a" \
	-Dyaml-cpp_DIR="$DEPS/lib/cmake/yaml-cpp" \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev

[ "$1" = configure ] && exit 0
ninja -C "$B" -j "$JOBS" -k "${KEEP:-1}"	# KEEP=50 : continuer après des erreurs pour les voir ensemble
ninja -C "$B" install
sh "$BL/pymodules.sh" "$I"	# attrs, cattrs, requests… (paquets purs livrés avec Blender)
ls -l "$I/blender"
