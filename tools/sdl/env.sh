# env.sh -- versions et dossiers de l'essai de SDL (docs/SDL.md). Tout se construit dans WSL, hors du depot
# et du selfhost, sous ~/armpi-sdl. Inclus par les autres scripts.
SDL_TAG=release-3.4.18
COMPAT_TAG=release-2.32.74

ARMPI_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
A=$HOME/armpi-sdl
SRC=$A/src
OUT=$A/out
TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
POSIX=$A/posix				# libposix de ce depot, construite par build-sdl.sh
U=$A/uikit				# le relais C d'UIKit
MESA=$HOME/armpi-mesa/out		# l arbre de Mesa (seulement pour la refaire)
MESA_GL="$ARMPI_ROOT/prebuilt/mesa/lib/libarmgl.a $ARMPI_ROOT/prebuilt/mesa/lib/libmesa_v3d.a $ARMPI_ROOT/prebuilt/mesa/lib/libz.a"	# Mesa construite (tools/mesa/prebuilt.sh)
WK=$HOME/armpi-webkit-build/sysroot	# zlib
JOBS=${JOBS:-$(nproc)}
CFLAGS_ARMPI="-specs=$POSIX/lib/posix.specs -mcpu=cortex-a72 -O2 -g0 -ffunction-sections -fdata-sections -D_GNU_SOURCE"
mkdir -p "$SRC" "$OUT"
