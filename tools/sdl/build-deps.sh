#!/bin/sh
# build-deps.sh -- le sysroot des ports SDL (docs/SDL.md), ~/armpi-sdl/sysroot : celui de WebKit (freetype, harfbuzz,
# png, jpeg, zlib, curl, ssl), libposix de ce depot, SDL, puis ce qu'il faut de plus aux deux jeux.
#     sh tools/sdl/build-deps.sh
set -eu
. "$(dirname "$0")/env.sh"
R=$A/sysroot
B=$OUT/deps
L=$R/lib
mkdir -p "$B" "$L/pkgconfig" "$R/include"

# le sysroot
cp -a "$WK/lib/." "$L/"
cp -a "$WK/include/." "$R/include/"
cp -r "$POSIX/include/." "$R/include/"
cp "$POSIX/lib/libposix.a" "$POSIX/lib/crt0posix.o" "$POSIX/lib/posix.ld" "$L/"
sed "s|$WK|$R|g" "$WK/lib/onyx.specs" > "$L/onyx.specs"
sed "s|$WK|$R|g" "$WK/lib/onyx-cc.specs" > "$L/onyx-cc.specs"
rm -rf "$L/pkgconfig"/*.bak
sed -i "s|$WK|$R|g" "$L"/pkgconfig/*.pc
rm -rf "$L/cmake"

# SDL : libSDL2.a sur libSDL3r.a, les en-tetes de SDL 2 et de SDL 3
cp "$OUT/libSDL3.a" "$OUT/libSDL3r.a" "$OUT/libSDL2.a" "$U/libuikit_c.a" "$L/"
mkdir -p "$R/include/SDL2" "$R/include/SDL3"
cp "$SRC/sdl2-compat/include/SDL2/"*.h "$R/include/SDL2/"
cp "$ARMPI_ROOT/tools/sdl/compat/SDL_config_unix.h" "$R/include/SDL2/"
cp "$SRC/SDL3/include/SDL3/"*.h "$R/include/SDL3/"
cp "$POSIX/lib/libposix.a" "$L/"
cat > "$L/pkgconfig/sdl2.pc" <<EOF
prefix=$R
libdir=$L
includedir=$R/include
Name: sdl2
Description: sdl2-compat sur SDL 3, ARMPI
Version: 2.32.74
Libs: -L$L -lSDL2 -lSDL3r -luikit_c
Cflags: -I$R/include/SDL2 -D_REENTRANT
EOF

mkdir -p "$L/cmake/SDL2"
cat > "$L/cmake/SDL2/SDL2Config.cmake" <<EOF
add_library(SDL2::SDL2 STATIC IMPORTED GLOBAL)
set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION $L/libSDL2.a
	INTERFACE_INCLUDE_DIRECTORIES $R/include/SDL2 INTERFACE_LINK_LIBRARIES "$L/libSDL3r.a;$L/libuikit_c.a")
set(SDL2_FOUND TRUE)
set(SDL2_INCLUDE_DIRS $R/include/SDL2)
set(SDL2_LIBRARIES SDL2::SDL2)
EOF

CM="cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ARMPI_ROOT/tools/sdl/armpi-sdl.cmake -DONYX_SYSROOT=$R -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_INSTALL_PREFIX=$R -DBUILD_SHARED_LIBS=OFF"
port () {	# nom, puis options de CMake
	n=$1
	shift
	[ -f "$B/$n.done" ] && return 0
	rm -rf "$B/$n"
	echo "== $n"
	$CM -S "$SRC/$n" -B "$B/$n" "$@" > "$B/$n.log" 2>&1 || { tail -30 "$B/$n.log"; exit 1; }
	ninja -C "$B/$n" -j"$JOBS" install >> "$B/$n.log" 2>&1 || { grep -E "error|Error" "$B/$n.log" | head -20; exit 1; }
	touch "$B/$n.done"
}
port ogg -DBUILD_TESTING=OFF -DINSTALL_DOCS=OFF
port vorbis -DOGG_ROOT=$R -DBUILD_TESTING=OFF
port fmt -DFMT_TEST=OFF -DFMT_DOC=OFF
port libsamplerate -DBUILD_TESTING=OFF -DLIBSAMPLERATE_EXAMPLES=OFF -DLIBSAMPLERATE_INSTALL=ON
port openal-soft -DLIBTYPE=STATIC -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF \
	-DALSOFT_INSTALL_AMBDEC_PRESETS=OFF -DALSOFT_BACKEND_SDL2=ON -DALSOFT_REQUIRE_SDL2=ON -DALSOFT_BACKEND_WAVE=OFF -DALSOFT_BACKEND_PIPEWIRE=OFF \
	-DALSOFT_BACKEND_PULSEAUDIO=OFF -DALSOFT_BACKEND_ALSA=OFF -DALSOFT_BACKEND_OSS=OFF -DALSOFT_BACKEND_JACK=OFF -DALSOFT_BACKEND_PORTAUDIO=OFF \
	-DALSOFT_BACKEND_SNDIO=OFF -DALSOFT_BACKEND_SOLARIS=OFF -DALSOFT_DLOPEN=OFF -DALSOFT_RTKIT=OFF \
	-DSDL2_DIR=$L/cmake/SDL2

# (newlib : les atomiques sont en ligne, mais OpenAL demande -latomic)
${TC}ar rcs "$L/libatomic.a"

# glm : des en-tetes
[ -d "$R/include/glm" ] || cp -r "$SRC/glm/glm" "$R/include/"
cat > "$L/pkgconfig/glm.pc" <<EOF
Name: glm
Description: glm
Version: 1.0
Cflags: -I$R/include
EOF

CC="${TC}gcc -specs=$L/onyx-cc.specs -mcpu=cortex-a72 -O2 -g0 -ffunction-sections -fdata-sections -D_GNU_SOURCE -isystem $R/include"

# PhysicsFS : sans son CMake, un seul fichier par format
if [ ! -f "$L/libphysfs.a" ]; then
	mkdir -p "$B/physfs"
	for f in "$SRC/physfs/src/"physfs*.c; do
		$CC -DPHYSFS_NO_CDROM_SUPPORT=1 -c "$f" -o "$B/physfs/$(basename "$f" .c).o"
	done
	${TC}ar rcs "$L/libphysfs.a" "$B/physfs/"*.o
	cp "$SRC/physfs/src/physfs.h" "$R/include/"
	cat > "$L/pkgconfig/physfs.pc" <<EOF
Name: physfs
Description: PhysicsFS
Version: 3.2
Libs: -L$L -lphysfs
Cflags: -I$R/include
EOF
fi

# SDL_image : PNG, JPEG et le reste par stb_image, qu'il embarque
if [ ! -f "$L/libSDL2_image.a" ]; then
	mkdir -p "$B/sdlimage"
	for f in "$SRC/SDL2_image/src/"IMG*.c; do
		$CC -I"$R/include/SDL2" -I"$SRC/SDL2_image/include" -I"$SRC/SDL2_image/include/SDL2" -I"$SRC/SDL2_image/src" -DUSE_STBIMAGE -DLOAD_BMP -DLOAD_GIF -DLOAD_JPG -DLOAD_PNG -DLOAD_TGA -DLOAD_XPM \
			-c "$f" -o "$B/sdlimage/$(basename "$f" .c).o"
	done
	${TC}ar rcs "$L/libSDL2_image.a" "$B/sdlimage/"*.o
	cp "$SRC/SDL2_image/include/SDL_image.h" "$R/include/SDL2/" 2>/dev/null || cp "$SRC/SDL2_image/include/SDL2/SDL_image.h" "$R/include/SDL2/"
	cat > "$L/pkgconfig/SDL2_image.pc" <<EOF
Name: SDL2_image
Description: SDL_image
Version: 2.8.8
Requires: sdl2
Libs: -L$L -lSDL2_image
Cflags: -I$R/include/SDL2
EOF
fi
ls -l "$L"/libSDL*.a "$L"/libphysfs.a "$L"/libogg.a "$L"/libvorbis.a "$L"/libopenal.a "$L"/libfmt.a
