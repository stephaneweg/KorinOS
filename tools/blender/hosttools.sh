#!/bin/sh
# hosttools.sh : les outils que Blender lance pendant sa propre construction (datatoc, shader_tool, makesdna,
# makesrna), construits pour la machine de construction (Linux de WSL). La construction croisée (build.sh) les appelle
# par CMAKE_CROSSCOMPILING_EMULATOR = hosttool.sh (retouche de Blender : l'émulateur devant chaque appel d'outil) ;
# sans eux, les mêmes outils compilés pour Korin plantent sous Linux.
#
#   CONFIG=headless|gui sh tools/blender/hosttools.sh     (dans WSL, depuis la racine du dépôt ; une fois par
#                                                         configuration, ou après un changement de Blender)
# 1. les dépendances pour l'hôte (HOST=1 sh deps.sh : celles que la configuration de Blender exige) ;
# 2. Blender configuré pour l'hôte avec LE MÊME cache que la cible (config-$CONFIG.cmake), seulement ces cibles :
#    $K/host/blender-$CONFIG/bin. Les mêmes options sont indispensables : makesrna génère les tables de l'API (RNA)
#    selon WITH_CYCLES, WITH_VULKAN_BACKEND, WITH_IMAGE_WEBP… ; des outils d'une autre configuration donnent des
#    tables fausses (24/11 : arguments nommés de UILayout reçus au mauvais rang, plantage de l'interface).
set -e
export HOST=1
. "$(dirname "$0")/env.sh"
sh "$BL/deps.sh" imath deflate openjph openexr tiff fmt robinmap expat yamlcpp pystring minizipng opencolorio \
	openimageio epoxy eigen tbb opensubdiv manifold fftw gmp embree
CONFIG=${CONFIG:-headless}
B=$OUT/blender-$CONFIG
PY=$K/host-python
[ -f "$PY/lib/libpython3.13.a" ] || { echo "hosttools.sh : pas de $PY (sh tools/blender/python.sh)" >&2; exit 1; }

# Vulkan et shaderc (CONFIG=gui) : platform_unix.cmake les demande à pkg-config ; ces outils ne les lient pas, il ne
# leur faut que les en-têtes (ceux de la cible, deps.sh : vulkan_headers, shaderc)
TD=$K/x86_64/deps
PC=$OUT/pkgconfig-$CONFIG
mkdir -p "$PC"
printf 'Name: vulkan\nDescription: en-têtes seulement (hosttools.sh)\nVersion: 1.4.341\nCflags: -I%s/include\nLibs:\n' "$TD" > "$PC/vulkan.pc"
printf 'Name: shaderc\nDescription: en-têtes seulement (hosttools.sh)\nVersion: 2025.4\nCflags: -I%s/include\nLibs:\n' "$TD" > "$PC/shaderc.pc"

# (pas de GHOST de Korin ici : KORIN n'est pas posé ; ni X11 ni Wayland, ces outils n'en ont pas besoin)
PKG_CONFIG_PATH="$PC${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}" cmake -S "$BSRC" -B "$B" -G Ninja \
	-C "$BL/config-$CONFIG.cmake" -DCMAKE_BUILD_TYPE=Release \
	-DWITH_GHOST_X11=OFF -DWITH_GHOST_WAYLAND=OFF -DWITH_GHOST_SDL=OFF \
	-DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_PROJECT_INCLUDE="$BL/host-fix.cmake" \
	-DPYTHON_ROOT_DIR="$PY" -DPYTHON_VERSION=3.13 -DPYTHON_INCLUDE_DIR="$PY/include/python3.13" \
	-DPYTHON_LIBRARY="$PY/lib/libpython3.13.a" -DPYTHON_LIBPATH="$PY/lib" -DPYTHON_EXECUTABLE="$PY/bin/python3.13" \
	-DEPOXY_ROOT_DIR="$DEPS" -DOPENEXR_ROOT_DIR="$DEPS" -DOpenImageIO_ROOT="$DEPS" -DOpenColorIO_ROOT="$DEPS" \
	-Dfmt_ROOT="$DEPS" -Dexpat_ROOT="$DEPS" -Dpystring_ROOT="$DEPS" -DImath_ROOT="$DEPS" -Dminizip-ng_ROOT="$DEPS" \
	-Dminizip-ng_INCLUDE_DIR="$DEPS/include/minizip-ng/minizip" -Dminizip-ng_LIBRARY="$DEPS/lib/libminizip.a" \
	-DOPENSUBDIV_ROOT_DIR="$DEPS" -DFFTW3_ROOT_DIR="$DEPS" -DGMP_ROOT_DIR="$DEPS" -DEMBREE_ROOT_DIR="$DEPS" \
	-Dmanifold_DIR="$DEPS/lib/cmake/manifold" \
	-Dyaml-cpp_DIR="$DEPS/lib/cmake/yaml-cpp" -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -Wno-dev
# les options qui changent les tables de makesrna doivent être celles de la cible
grep -E "^WITH_(CYCLES|VULKAN_BACKEND|OPENGL_BACKEND|IMAGE_WEBP|OPENSUBDIV|PYTHON|BULLET|FREESTYLE|INTERNATIONAL|USD|ALEMBIC|FFTW3|MOD_FLUID|MOD_OCEANSIM|CODEC_FFMPEG|INPUT_NDOF|XR_OPENXR|GMP|IMAGE_OPENJPEG|IMAGE_CINEON|OPENAL|SDL|JACK|PULSEAUDIO|EXPERIMENTAL_FEATURES):BOOL" \
	"$B/CMakeCache.txt" | sort > "$B/rna-options.txt"
ninja -C "$B" -j "$JOBS" datatoc shader_tool makesdna makesrna
ls -l "$B/bin"
