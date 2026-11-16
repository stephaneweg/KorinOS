#!/bin/sh
# sysroot_cao.sh : la CAO dans l'arbre système du PC (docs/DESIGN-CAO.md, §15), dans WSL, APRÈS tools/pc/sysroot_gcc.sh
# (qui refait gcc/ à neuf) :
#     gcc/include/opencascade/, gcc/lib/libTK*.a        OpenCASCADE 8.0.1 pour le GCC de l'OS (paquet occt-dev)
#     gcc/include/planegcs/, gcc/include/Eigen/,         PlaneGCS et Eigen (paquet planegcs-dev)
#     gcc/lib/libplanegcs.a
#     bin/caotest                                        l'essai du moteur (paquet caotest)
# Dans Korin : g++ -std=gnu++20 -I/gcc/include/opencascade -I/gcc/include prog.cpp -lTKDESTEP … -lTKernel -lplanegcs
# (l'ordre des bibliothèques : tools/cao/build-test.sh).
# Construire d'abord : sh tools/cao/fetch.sh, build-planegcs.sh, build-occt.sh, build-test.sh (ARCH=x86_64).
#     sh tools/pc/sysroot_cao.sh [arbre]
set -e
cd "$(dirname "$0")/../.."
OUT=${1:-build/pc/sysroot}
K=${KORIN_CAO:-$HOME/korin-cao}/x86_64
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : sh tools/pc/sysroot.sh)"; exit 1; }
[ -f "$K/occt/include/opencascade/Standard.hxx" ] || { echo "absent : $K/occt (sh tools/cao/build-occt.sh)"; exit 1; }
[ -f "$K/planegcs/lib/libplanegcs.a" ] || { echo "absent : $K/planegcs (sh tools/cao/build-planegcs.sh)"; exit 1; }
mkdir -p "$OUT/gcc/include" "$OUT/gcc/lib"
rm -rf "$OUT/gcc/include/opencascade" "$OUT/gcc/include/planegcs" "$OUT/gcc/include/Eigen"
cp -r "$K/occt/include/opencascade" "$OUT/gcc/include/"
cp "$(dirname "$(ls "$K"/occt/lib*/libTKernel.a | head -1)")"/libTK*.a "$OUT/gcc/lib/"
cp -r "$K/planegcs/include/planegcs" "$K/planegcs/include/Eigen" "$OUT/gcc/include/"
cp "$K/planegcs/lib/libplanegcs.a" "$OUT/gcc/lib/"
[ ! -f "$K/test/caotest" ] || cp "$K/test/caotest" "$OUT/bin/caotest"
echo "sysroot_cao : OCCT ($(ls "$OUT"/devtools/gcc/lib/libTK*.a | wc -l) bibliothèques), PlaneGCS, Eigen$( [ -f "$OUT/bin/caotest" ] && echo ", caotest") -> $OUT"

# les outils de développement sous /devtools (arbre unique)
sh tools/devtools_move.sh "$OUT"
