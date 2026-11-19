#!/bin/sh
# glkit-pc-from-pkg.sh -- GlKit du PC sans construire Mesa : le glkit.so publié dans le paquet « pc »
# (packages/pc/pkgs/pc-*.opk, lib/glkit.so), puis libglkit.a, ce que les programmes lient (relais, chargeur,
# imports), tiré de lui par finish.py comme le fait build-glkit-pc.sh. Les relais se résolvent par nom au premier
# appel (glkit_names) : le programme marche avec ce glkit.so et avec ceux qui exportent les mêmes fonctions.
#   build/pc/mesa/sdroot/lib/glkit.so    le kit (tools/pc/sysroot.sh le met dans /lib)
#   build/pc/mesa/lib/libglkit.a         ce que tools/pc/apps.sh lie (glkit_extra)
# Pour un agent du cloud, ou une machine sans ~/armpi-mesa (Mesa iris : ≈ 1 h et LLVM 21). Ne remplace pas un
# GlKit construit (build/pc/mesa/lib/libglkit.a présent : rien n'est fait, sauf FORCE=1).
#     sh tools/mesa/glkit-pc-from-pkg.sh [paquet.opk]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
POSIX=$ROOT/build/pc/posix
PM=$ROOT/build/pc/mesa
K=$ROOT/tools/mesa/glkit
W=$ROOT/build/pc/glkit-pkg
[ -f "$POSIX/lib/posix.specs" ] || { echo "glkit-pc-from-pkg : d'abord ARCH=x86_64 sh tools/posix/build.sh" >&2; exit 1; }
if [ -f "$PM/lib/libglkit.a" ] && [ "${FORCE:-0}" != 1 ]; then
	echo "glkit-pc-from-pkg : $PM/lib/libglkit.a est déjà là (FORCE=1 pour le refaire)"
	exit 0
fi
PKG=${1:-$(ls "$ROOT"/packages/pc/pkgs/pc-*.opk 2>/dev/null | sort -V | tail -1)}
[ -f "$PKG" ] || { echo "glkit-pc-from-pkg : pas de paquet pc (packages/pc/pkgs/pc-*.opk)" >&2; exit 1; }
rm -rf "$W"
mkdir -p "$W/gen" "$PM/lib" "$PM/sdroot/lib"
python3 -I - "$PKG" "$W/glkit.in" <<'EOF'
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
open(sys.argv[2], "wb").write(z.read("lib/glkit.so"))
EOF
python3 -I "$K/finish.py" "$W/glkit.in" "$W/glkit.so" "$W/gen"
cp "$W/glkit.so" "$PM/sdroot/lib/glkit.so"
H=$K/host
HF="-specs=$POSIX/lib/posix.specs -O2 -g0 -fno-builtin"
${TC}gcc $HF -c "$H/glkit_host.c" -o "$W/glkit_host.o"
${TC}gcc $HF -w -c "$W/gen/glkit_imports.c" -o "$W/glkit_imports.o"
${TC}gcc -c "$H/glkit_lazy_x86_64.S" -o "$W/glkit_lazy.o"
${TC}gcc -c "$W/gen/glkit_stubs.S" -o "$W/glkit_stubs.o"
rm -f "$PM/lib/libglkit.a"
${TC}ar rcsD "$PM/lib/libglkit.a" "$W/glkit_stubs.o" "$W/glkit_lazy.o" "$W/glkit_host.o" "$W/glkit_imports.o"
echo "glkit-pc-from-pkg : depuis $(basename "$PKG")"
ls -l "$PM/sdroot/lib/glkit.so" "$PM/lib/libglkit.a"
