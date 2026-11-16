#!/bin/sh
# build-occt.sh : OpenCASCADE (OCCT) en bibliothèques statiques pour Korin, dans ~/korin-cao/$ARCH/occt
# (include/opencascade, lib/libTK*.a). D'abord sh tools/cao/fetch.sh et la libposix de l'architecture
# (ARCH=x86_64 sh tools/posix/build.sh pour le PC, sh tools/posix/build.sh pour le Pi).
#     sh tools/cao/build-occt.sh              (ARCH=aarch64 : pour le Pi)
#
# Boîtes à outils gardées (docs/DESIGN-CAO.md, « Construction ») : Foundation, Modeling (données et algorithmes :
# primitives, booléens, congés, décalages, formes, maillage) et Data Exchange STEP, IGES, STL ; ce qu'elles
# tirent avec elles (TKXCAF demande TKV3d et TKService, sans OpenGL ni FreeType). Pas de Draw, ni Tcl/Tk, ni
# visualisation OpenGL, ni Qt.
set -eu
. "$(dirname "$0")/env.sh"
[ -f "$POSIX/lib/posix.specs" ] || { echo "build-occt : pas de libposix dans $POSIX" >&2; exit 1; }
[ -d "$OCCT_SRC" ] || { echo "build-occt : d'abord sh tools/cao/fetch.sh" >&2; exit 1; }

# le patch de Korin (une fois, marqué dans l'arbre)
if [ -f "$CAO/occt-korin.patch" ] && [ ! -f "$OCCT_SRC/.korin-patched" ]; then
	patch -d "$OCCT_SRC" -p1 < "$CAO/occt-korin.patch"
	touch "$OCCT_SRC/.korin-patched"
fi

B=$OUT/obj/occt
I=$OUT/occt
TK="TKDESTEP;TKDEIGES;TKDESTL;TKFillet;TKBool;TKBO;TKOffset;TKFeat;TKPrim;TKHelix;TKMesh;TKShHealing;TKTopAlgo;TKGeomAlgo;TKHLR"
export ARCH KORIN_POSIX="$POSIX"
mods=""
for m in FoundationClasses ModelingData ModelingAlgorithms Visualization ApplicationFramework DataExchange Draw; do
	mods="$mods -DBUILD_MODULE_$m=OFF"
done
# shellcheck disable=SC2086
cmake -S "$OCCT_SRC" -B "$B" -G Ninja \
	-DCMAKE_TOOLCHAIN_FILE="$CAO/korin.cmake" \
	-DCMAKE_BUILD_TYPE=Release \
	-DBUILD_LIBRARY_TYPE=Static \
	$mods \
	-DBUILD_ADDITIONAL_TOOLKITS="$TK" \
	-DBUILD_RELEASE_DISABLE_EXCEPTIONS=OFF \
	-DBUILD_DOC_Overview=OFF -DBUILD_DOC_RefMan=OFF \
	-DBUILD_USE_PCH=OFF -DBUILD_USE_VCPKG=OFF \
	-DUSE_TK=OFF -DUSE_FREETYPE=OFF -DUSE_FREEIMAGE=OFF -DUSE_FFMPEG=OFF -DUSE_OPENVR=OFF \
	-DUSE_RAPIDJSON=OFF -DUSE_DRACO=OFF -DUSE_TBB=OFF -DUSE_EIGEN=OFF -DUSE_VTK=OFF \
	-DUSE_XLIB=OFF -DUSE_OPENGL=OFF -DUSE_GLES2=OFF -DUSE_D3D=OFF \
	-DCMAKE_CXX_FLAGS_RELEASE="-O2 -g0 -DNDEBUG" -DCMAKE_C_FLAGS_RELEASE="-O2 -g0 -DNDEBUG" \
	-DINSTALL_DIR="$I" -DCMAKE_INSTALL_PREFIX="$I" > "$OUT/occt-cmake.log" 2>&1 ||
	{ tail -40 "$OUT/occt-cmake.log"; exit 1; }
t0=$(date +%s)
# (KEEP=1 : ninja -k 0, toutes les erreurs d'un coup)
ninja -C "$B" -j "$JOBS" ${KEEP:+-k 0} > "$OUT/occt-build.log" 2>&1 || { grep -B2 -A12 "error" "$OUT/occt-build.log" | head -80; exit 1; }
ninja -C "$B" install > "$OUT/occt-install.log" 2>&1 || { tail -20 "$OUT/occt-install.log"; exit 1; }
echo "build-occt : $I en $(( ($(date +%s) - t0) / 60 )) min ; $(ls "$I"/lib*/libTK*.a | wc -l) boîtes à outils, $(du -sh "$I" | cut -f1)"
