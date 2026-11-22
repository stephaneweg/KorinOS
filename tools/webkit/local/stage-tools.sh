#!/bin/bash
# stage-tools.sh [ARBRE] -- l'arbre des paquets nasm, ninja et libjpeg-turbo (tools/pkg/packages-pc.ini), à partir de
# build-nasm.sh, build-ninja.sh et build-jpeg.sh (dans ~/jet-build) ; puis, depuis le dépôt :
#   korin-pkg-key run python3 tools/pkg/pub.py nasm --new --tree ARBRE   (et ninja, libjpeg-turbo)
set -e
W=${WORK:-$HOME/jet-build}
T=${1:-$W/pkgtree}
J=${SRC:-$W/src/libjpeg-turbo-3.1.4}
rm -rf "$T"
mkdir -p "$T/devtools/gcc/bin" "$T/devtools/gcc/lib" "$T/devtools/gcc/include"
strip -o "$T/devtools/gcc/bin/nasm" "$W/bin/nasm"
cp "$W/bin/ninja" "$T/devtools/gcc/bin/ninja"
chmod +x "$T/devtools/gcc/bin/nasm" "$T/devtools/gcc/bin/ninja"
cp "$W/build/jpeg/libjpeg.a" "$T/devtools/gcc/lib/"
cp "$J/src/jpeglib.h" "$J/src/jmorecfg.h" "$J/src/jerror.h" "$W/build/jpeg/jconfig.h" "$T/devtools/gcc/include/"
"$T/devtools/gcc/bin/nasm" -v
"$T/devtools/gcc/bin/ninja" --version
find "$T" -type f | sort
