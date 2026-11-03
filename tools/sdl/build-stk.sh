#!/bin/sh
# build-stk.sh -- SuperTuxKart 1.5 pour ARMPI (docs/SDL.md) : le chemin OpenGL ES, sans Vulkan ni shaderc, sans
# enregistreur, sans wiimote. Resultat : ~/armpi-sdl/out/stk/bin/supertuxkart.
#     sh tools/sdl/build-stk.sh
set -eu
. "$(dirname "$0")/env.sh"
R=$A/sysroot
B=$OUT/stk
S=$SRC/stk-code
# les en-tetes de Khronos, ceux de Mesa
for d in GLES2 GLES3 EGL KHR; do mkdir -p "$R/include/$d"; cp "$HOME/armpi-mesa/src/mesa-26.2.0/include/$d/"*.h "$R/include/$d/"; done
GL="-Wl,-u,armgl_init -Wl,--start-group $MESA/armgl/libarmgl.a $(cat "$MESA/armgl/libs.txt") -Wl,--end-group"
PKG_CONFIG_LIBDIR=$R/lib/pkgconfig PKG_CONFIG_PATH= cmake -G Ninja -S "$S" -B "$B" \
	-DCMAKE_TOOLCHAIN_FILE="$ARMPI_ROOT/tools/sdl/armpi-sdl.cmake" -DONYX_SYSROOT="$R" -DCMAKE_BUILD_TYPE=STKRelease \
	-DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_PREFIX_PATH="$R;$R/lib/cmake" -DSDL2_DIR="$R/lib/cmake/SDL2" \
	-DCMAKE_PROJECT_INCLUDE="$ARMPI_ROOT/tools/sdl/shared-ok.cmake" \
	-DUSE_GLES2=ON -DNO_SHADERC=ON -DBUILD_RECORDER=OFF -DUSE_WIIUSE=OFF -DUSE_SQLITE3=OFF -DCHECK_ASSETS=OFF \
	-DUSE_DNS_C=ON -DUSE_IPV6=ON -DUSE_SYSTEM_ENET=OFF -DUSE_SYSTEM_SQUISH=OFF -DUSE_SYSTEM_MCPP=OFF -DUSE_SYSTEM_ANGELSCRIPT=OFF \
	-DLIBRESOLV_LIBRARY="$R/lib/libatomic.a" -DPTHREAD_LIBRARY="$R/lib/libatomic.a" -DSDL2_LIBRARY="$R/lib/libSDL2.a" -DSDL2_INCLUDEDIR="$R/include/SDL2" \
	-DCMAKE_EXE_LINKER_FLAGS="-specs=$R/lib/onyx.specs -L$R/lib" \
	-DCMAKE_CXX_STANDARD_LIBRARIES="-lSDL3r -luikit_c -lcurl -lnghttp2 -lssl -lcrypto -lvorbisfile -lvorbis -logg -lfreetype -lharfbuzz -lpng -lbrotlidec -lz $GL" \
	> "$B.cfg.log" 2>&1 || { tail -25 "$B.cfg.log"; exit 1; }
ninja -C "$B" -j"$JOBS" -k 0 supertuxkart > "$B.log" 2>&1 || true
grep -E "error:|undefined reference" "$B.log" | sed 's/.*undefined reference to/undef/' | sed 's/.*error: /error: /' | sort | uniq -c | sort -rn | head -${1:-60}
grep -c "^FAILED" "$B.log" || true
ls -l "$B/bin/supertuxkart" 2>/dev/null || true
