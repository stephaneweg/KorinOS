# env.sh -- versions et dossiers du portage de Mesa (docs/MESA.md). Tout se construit dans WSL, hors du
# depot et du selfhost, sous ~/armpi-mesa. Inclus par les autres scripts.
MESA_TAG=mesa-26.2.0

ARMPI_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
A=$HOME/armpi-mesa
SRC=$A/src/$MESA_TAG
OUT=$A/out
TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
POSIX=$ARMPI_ROOT/build/posix
WK=$HOME/armpi-webkit-build/sysroot			# zlib
JOBS=${JOBS:-$(nproc)}
PATH=$A/venv/bin:$PATH
mkdir -p "$A/src" "$OUT"
