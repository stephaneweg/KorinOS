#!/bin/sh
# hosttools.sh : les outils que Blender lance pendant sa propre construction (datatoc, shader_tool, makesdna,
# makesrna), construits pour la machine de construction (Linux de WSL). La construction croisée (build.sh) les appelle
# par CMAKE_CROSSCOMPILING_EMULATOR = hosttool.sh (retouche de Blender : l'émulateur devant chaque appel d'outil) ;
# sans eux, les mêmes outils compilés pour Korin plantent sous Linux.
#
#   sh tools/blender/hosttools.sh       (dans WSL, depuis la racine du dépôt ; une fois, ou après un changement de
#                                       Blender qui touche ces outils)
# 1. les dépendances pour l'hôte (HOST=1 sh deps.sh : celles que la configuration de Blender exige) ;
# 2. Blender configuré pour l'hôte (même cache initial), seulement ces cibles : $K/host/blender/bin.
set -e
export HOST=1
. "$(dirname "$0")/env.sh"
sh "$BL/deps.sh" imath deflate openjph openexr tiff fmt robinmap expat yamlcpp pystring minizipng opencolorio \
	openimageio epoxy eigen
CONFIG=${CONFIG:-headless}
B=$OUT/blender
PY=$K/host-python
[ -f "$PY/lib/libpython3.13.a" ] || { echo "hosttools.sh : pas de $PY (sh tools/blender/python.sh)" >&2; exit 1; }

cmake -S "$BSRC" -B "$B" -G Ninja -C "$BL/config-$CONFIG.cmake" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_PREFIX_PATH="$DEPS" -DWITH_CYCLES=OFF -DWITH_IMAGE_WEBP=OFF -DCMAKE_PROJECT_INCLUDE="$BL/host-fix.cmake" \
	-DPYTHON_ROOT_DIR="$PY" -DPYTHON_VERSION=3.13 -DPYTHON_INCLUDE_DIR="$PY/include/python3.13" \
	-DPYTHON_LIBRARY="$PY/lib/libpython3.13.a" -DPYTHON_LIBPATH="$PY/lib" -DPYTHON_EXECUTABLE="$PY/bin/python3.13" \
	-DEPOXY_ROOT_DIR="$DEPS" -DOPENEXR_ROOT_DIR="$DEPS" -DOpenImageIO_ROOT="$DEPS" -DOpenColorIO_ROOT="$DEPS" \
	-Dfmt_ROOT="$DEPS" -Dexpat_ROOT="$DEPS" -Dpystring_ROOT="$DEPS" -DImath_ROOT="$DEPS" -Dminizip-ng_ROOT="$DEPS" \
	-Dminizip-ng_INCLUDE_DIR="$DEPS/include/minizip-ng/minizip" -Dminizip-ng_LIBRARY="$DEPS/lib/libminizip.a" \
	-Dyaml-cpp_DIR="$DEPS/lib/cmake/yaml-cpp" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev
ninja -C "$B" -j "$JOBS" datatoc shader_tool makesdna makesrna
ls -l "$B/bin"
