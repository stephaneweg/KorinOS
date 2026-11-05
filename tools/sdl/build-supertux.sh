#!/bin/sh
# build-supertux.sh -- SuperTux 0.7 pour ARMPI (docs/SDL.md), rendu SDL_Renderer. tools/sdl/supertux.patch : les touches A et B
# du PocketTerm, rien d'autre.
# Resultat : ~/armpi-sdl/out/supertux/supertux2.
#     sh tools/sdl/build-supertux.sh
set -eu
. "$(dirname "$0")/env.sh"
R=$A/sysroot
B=$OUT/supertux
S=$SRC/supertux
# GPU=1 : relie aussi armgl et Mesa, SDL_Renderer prend alors opengles2
GLLIBS=""
[ -z "${GPU:-}" ] || GLLIBS="-Wl,-u,armgl_init -Wl,--start-group $MESA_GL -Wl,--end-group"
git -C "$S" apply --reverse --check "$ARMPI_ROOT/tools/sdl/supertux.patch" 2>/dev/null || git -C "$S" apply "$ARMPI_ROOT/tools/sdl/supertux.patch"
PKG_CONFIG_LIBDIR=$R/lib/pkgconfig PKG_CONFIG_PATH= cmake -G Ninja -S "$S" -B "$B" \
	-DCMAKE_TOOLCHAIN_FILE="$ARMPI_ROOT/tools/sdl/armpi-sdl.cmake" -DONYX_SYSROOT="$R" -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_PREFIX_PATH="$R;$R/lib/cmake" -DSDL2_DIR="$R/lib/cmake/SDL2" \
	-DENABLE_OPENGL=OFF -DENABLE_NETWORKING=OFF -DENABLE_DISCORD=OFF -DBUILD_TESTING=OFF -DUSE_STATIC_SIMPLESQUIRREL=ON \
	-DCMAKE_PROJECT_INCLUDE="$ARMPI_ROOT/tools/sdl/shared-ok.cmake" -DSUPERTUX_PCH=OFF -DIS_SUPERTUX_RELEASE=ON -DCMAKE_EXE_LINKER_FLAGS="-specs=$R/lib/onyx.specs -L$R/lib" -DCMAKE_CXX_STANDARD_LIBRARIES="-lvorbisfile -lvorbis -logg -lfreetype -lharfbuzz -lpng -lbrotlidec -lz $GLLIBS" > "$B.cfg.log" 2>&1 || { tail -25 "$B.cfg.log"; exit 1; }
ninja -C "$B" -j"$JOBS" -k 0 supertux2 > "$B.log" 2>&1 || true
grep -E "FAILED|error:|undefined reference" "$B.log" | sed 's/.*undefined reference to/undef/' | sort | uniq -c | sort -rn | head -${1:-40}
ls -l "$B/supertux2" 2>/dev/null || true
