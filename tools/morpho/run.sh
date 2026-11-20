#!/bin/sh
# run.sh -- Morpho sur l'hôte (Linux), sans Korin : le même code de la créature et de la scène, GpuKit du PC compilé
# pour l'hôte (son rastériseur du processeur), UIKit et AudioKit en bouchons, le texte par FreeType et les polices de
# Korin. Pour voir des images sans démarrer Korin :
#
#   sh tools/morpho/run.sh gallery <stade 0..5> <graine> <col> <lig> out.png [heure]   des créatures au hasard
#   sh tools/morpho/run.sh shot out.png [VAR=valeur ...]                               une image du jeu (host.cpp)
#
# Il faut le GCC de l'hôte, FreeType (libfreetype-dev), python3 et PIL (le .png).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
B=$ROOT/build/morpho-host
S=$ROOT/src/apps/morpho
mkdir -p "$B/inc" "$B/w"
K=$ROOT/src/kits
A=$ROOT/src/inc
if [ ! -f "$B/inc/uikit_abi.h" ] || [ "$K/uikit/uikit.abi" -nt "$B/inc/uikit_abi.h" ]; then
	python3 -I "$ROOT/tools/kits/abi2c.py" "$K/uikit/uikit.abi" "$B/w" --arch x86_64 --const "$A/armpi.inc" "$A/ws_ext.inc" "$K/uikit/uikit.inc" >/dev/null
	python3 -I "$ROOT/tools/kits/abi2c.py" "$K/audiokit/audiokit.abi" "$B/w" --arch x86_64 --const "$K/audiokit/audiokit.inc" >/dev/null
	python3 -I "$ROOT/tools/kits/abi2c.py" "$K/gpukit/gpukit.abi" "$B/w" --arch x86_64 --const "$K/gpukit/gpukit.inc" >/dev/null
	cp "$B/w/uikit_abi.h" "$B/w/audiokit_abi.h" "$B/w/gpukit_abi.h" "$K/uikit/include/uikit.h" "$B/inc/"
	python3 -I "$ROOT/tools/relics/mkstubs.py" "$B/stubs.c" "$B/inc/uikit_abi.h" "$B/inc/audiokit_abi.h"
fi
CXX="g++ -O2 -g -std=gnu++20 -Wall -Wno-unused-function -I$B/inc"
gcc -O2 -g -w -c "$ROOT/tools/morpho/host_gk.c" -o "$B/gk.o"
gcc -O2 -w -I"$B/inc" -c "$B/stubs.c" -o "$B/stubs.o"
pngout () { python3 -c "import sys; from PIL import Image; Image.open(sys.argv[1]).save(sys.argv[2])" "$1" "$2"; }
case ${1:-gallery} in
gallery)
	$CXX -o "$B/gallery" "$ROOT/tools/morpho/gallery.cpp" "$S/creature.cpp" "$S/render.cpp" "$S/scene.cpp" "$B/gk.o" -lm
	"$B/gallery" "${2:-4}" "${3:-1}" "${4:-4}" "${5:-3}" "$B/gallery.ppm" "${7:-11}"
	pngout "$B/gallery.ppm" "${6:-$B/gallery.png}"
	;;
shot)
	SRCS=$(ls "$S"/*.cpp | grep -v '/main.cpp$')
	$CXX -I/usr/include/freetype2 -DMORPHO_HOST -o "$B/host" "$ROOT/tools/morpho/host.cpp" $SRCS "$B/stubs.o" "$B/gk.o" -lfreetype -lm
	out=$2; shift 2
	(cd "$B" && env MORPHO_FONTS="${MORPHO_FONTS:-$ROOT/sdcard/fonts}" "$@" "$B/host" "$B/shot.ppm")
	pngout "$B/shot.ppm" "$out"
	;;
esac
