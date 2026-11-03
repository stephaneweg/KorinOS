#!/bin/sh
# build-sdl.sh -- libSDL3.a pour ARMPI : les sources de SDL sans son CMake, qui ne sait pas ce systeme, plus les
# pilotes de tools/sdl/armpi, par les crochets PRIVATE de SDL (SDL_build_config.h). Resultat dans ~/armpi-sdl/out.
#     sh tools/sdl/build-sdl.sh
set -eu
. "$(dirname "$0")/env.sh"
S=$SRC/SDL3
M=$ARMPI_ROOT/tools/sdl/armpi
O=$OUT/sdl3
mkdir -p "$O" "$POSIX" "$U"

# libposix de ce depot
SYSROOT=$POSIX OUT_POSIX=$A/posix-obj
[ -f "$POSIX/lib/libposix.a" ] && [ -z "$(find "$ARMPI_ROOT/src/lib/posix" -newer "$POSIX/lib/libposix.a" -type f | head -1)" ] || \
	SYSROOT=$POSIX OUT=$OUT_POSIX sh "$ARMPI_ROOT/tools/posix/build.sh" > "$A/posix.log" 2>&1

# le relais C d'UIKit
python3 -I "$ARMPI_ROOT/tools/kits/abi2c.py" "$ARMPI_ROOT/src/kits/uikit/uikit.abi" "$U" \
	--const "$ARMPI_ROOT/src/inc/armpi.inc" "$ARMPI_ROOT/src/inc/ws_ext.inc" "$ARMPI_ROOT/src/inc/pad.inc" "$ARMPI_ROOT/src/kits/uikit/uikit.inc"
${TC}gcc -mcpu=cortex-a72 -c "$U/uikit_abi.S" -o "$U/uikit_abi.o"
${TC}gcc $CFLAGS_ARMPI -c "$U/uikit_abi_open.c" -o "$U/uikit_abi_open.o"
rm -f "$U/libuikit_c.a"
${TC}ar rcs "$U/libuikit_c.a" "$U/uikit_abi.o" "$U/uikit_abi_open.o"

# ce que SDL met dans sa liste, moins ce qu'ARMPI n'a pas
cd "$S"
LIST=$(ls src/*.c src/atomic/*.c src/audio/*.c src/camera/*.c src/core/*.c src/cpuinfo/*.c src/dynapi/*.c src/events/*.c \
	src/io/*.c src/io/generic/*.c src/filesystem/*.c src/gpu/*.c src/joystick/*.c src/haptic/*.c src/hidapi/*.c src/locale/*.c \
	src/main/*.c src/misc/*.c src/power/*.c src/render/*.c src/render/*/*.c src/sensor/*.c src/stdlib/*.c src/storage/*.c \
	src/storage/generic/*.c src/thread/*.c src/time/*.c src/timer/*.c src/video/*.c src/video/yuv2rgb/*.c src/tray/*.c \
	src/dialog/*.c src/dialog/dummy/*.c src/tray/dummy/*.c src/process/*.c src/process/dummy/*.c src/camera/dummy/*.c \
	src/thread/pthread/*.c src/timer/unix/*.c src/time/unix/*.c src/filesystem/unix/*.c src/filesystem/posix/*.c \
	src/loadso/dummy/*.c src/sensor/dummy/*.c src/haptic/dummy/*.c src/locale/dummy/*.c src/misc/dummy/*.c \
	src/audio/dummy/*.c src/joystick/virtual/*.c src/video/dummy/*.c src/main/generic/*.c 2>/dev/null | grep -v 'src/render/\(direct3d\|metal\|ps2\|psp\|vitagxm\|ngage\|gpu\|vulkan\|opengl/\)')

INC="-I$M -I$S/include -I$S/src -I$S/src/video/khronos -I$S/src/hidapi/hidapi -I$ARMPI_ROOT/src/kits/uikit/include -I$U -I$ARMPI_ROOT/tools/mesa/armgl"
DEF="-DUSING_GENERATED_CONFIG_H -DSDL_BUILD_MAJOR_VERSION=3 -DSDL_BUILD_MINOR_VERSION=4 -DSDL_BUILD_MICRO_VERSION=18"

fails=$O/fails.txt
: > "$fails"
compile () {	# source, objet
	extra=""
	case $1 in src/time/unix/SDL_systime.c) extra="-Dsun=1 -Dtimezone=_timezone" ;; esac	# newlib : pas de tm_gmtoff
	if ! ${TC}gcc $CFLAGS_ARMPI $INC $DEF $extra -c "$1" -o "$2" 2>> "$O/cc.log"; then
		echo "$1" >> "$fails"
	fi
}
: > "$O/cc.log"
n=0
OBJS=""
for f in $LIST; do
	o=$O/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')
	OBJS="$OBJS $o"
	if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ "$M/SDL_build_config.h" -nt "$o" ]; then
		compile "$f" "$o" &
		n=$((n + 1))
		[ $((n % JOBS)) -eq 0 ] && wait
	fi
done
for f in "$M"/*.c; do
	o=$O/armpi_$(basename "$f" .c).o
	OBJS="$OBJS $o"
	compile "$f" "$o" &
done
wait
if [ -s "$fails" ]; then
	echo "echecs :"; cat "$fails"
	grep -E "error|Error" "$O/cc.log" | head -40
	exit 1
fi
rm -f "$OUT/libSDL3.a"
${TC}ar rcs "$OUT/libSDL3.a" $OBJS
ls -l "$OUT/libSDL3.a"
