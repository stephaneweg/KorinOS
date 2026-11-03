#!/bin/sh
# build-webkit : le WebKit de Jet, avec WebGL, dans sa propre copie et son propre dossier (~/armpi-mesa) : la copie
# de tools/webkit (~/armpi-webkit-build/webkit) et sa construction restent pour wk2test, wctest et Koton.
#     sh tools/mesa/build-webkit.sh           (apres build-angle.sh et build-armgl.sh)
set -eu
. "$(dirname "$0")/env.sh"
M=$ARMPI_ROOT/tools/mesa
BASE=$HOME/armpi-webkit-build/webkit
W=$A/src/webkit
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
WEBKIT_DIR="$W" BUILD="$OUT/webcore" TARGET=${TARGET:-WebKit} KEEP_GOING=1 JOBS=${JOBS:-12} \
	sh "$ARMPI_ROOT/tools/webkit/build-webcore.sh"
