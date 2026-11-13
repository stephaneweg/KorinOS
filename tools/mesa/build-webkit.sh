#!/bin/sh
# build-webkit : le WebKit de Jet, avec WebGL, dans sa propre copie et son propre dossier (~/armpi-mesa) : la copie
# de tools/webkit (~/armpi-webkit-build/webkit) et sa construction restent pour wk2test, wctest et Koton.
#     sh tools/mesa/build-webkit.sh           (apres build-angle.sh et build-armgl.sh)
# Korin PC (docs/etude/PC-PILOTES.md, "Jet pour le PC") : ARCH=x86_64 sh tools/mesa/build-webkit.sh, apres
# ARCH=x86_64 sh tools/webkit/build-deps.sh et tools/mesa/build-iris.sh (GlKit du PC) : la copie et l'arbre dans
# ~/korin-webkit-x86_64 (webkit-gl, webcore), avec le JIT de JSC (de base, DFG, FTL).
set -eu
. "$(dirname "$0")/env.sh"
M=$ARMPI_ROOT/tools/mesa
BASE=$HOME/armpi-webkit-build/webkit
W=$A/src/webkit
WKOUT=$OUT/webcore
JITFLAGS=
if [ "${ARCH:-aarch64}" = x86_64 ]; then
	K=${WK_OUT:-$HOME/korin-webkit-x86_64}
	BASE=$K/webkit
	W=$K/webkit-gl
	WKOUT=$K/webcore
	[ -f "$BASE/Source/cmake/OptionsOnyx.cmake" ] || ARCH=x86_64 sh "$ARMPI_ROOT/tools/webkit/fetch.sh"
	JITFLAGS="-DENABLE_JIT=ON -DENABLE_DFG_JIT=ON -DENABLE_FTL_JIT=ON -DENABLE_C_LOOP=OFF"
fi
if [ ! -f "$W/Source/cmake/OptionsOnyx.cmake" ]; then
	mkdir -p "$W"
	(cd "$BASE" && tar cf - .) | (cd "$W" && tar xf -)
fi
mkdir -p "$W/Source/ThirdParty"
[ -d "$W/Source/ThirdParty/ANGLE/src" ] || cp -a "$A/src/angle-wk/Source/ThirdParty/ANGLE" "$W/Source/ThirdParty/ANGLE"
cp "$M/webkit/PlatformOnyx-ANGLE.cmake" "$W/Source/ThirdParty/ANGLE/PlatformOnyx.cmake"
cmp -s "$M/webkit/GraphicsContextGLOnyx.cpp" "$W/Source/WebCore/platform/graphics/onyx/GraphicsContextGLOnyx.cpp" ||
	cp "$M/webkit/GraphicsContextGLOnyx.cpp" "$W/Source/WebCore/platform/graphics/onyx/"
python3 -I "$M/webkit/enable-webgl.py" "$W"
WEBKIT_DIR="$W" BUILD="$WKOUT" TARGET=${TARGET:-WebKit} KEEP_GOING=1 JOBS=${JOBS:-12} CMAKE_EXTRA="$JITFLAGS ${CMAKE_EXTRA:-}" \
	sh "$ARMPI_ROOT/tools/webkit/build-webcore.sh"
