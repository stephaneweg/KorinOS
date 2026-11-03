#!/bin/sh
# dktest.sh : construit l'essai de DocumentKit, depuis la racine du depot, dans WSL (apres tools/gcc/kits.sh) :
#     python -I tools/gcc/test/dkvec.py build/try/dkvec      (les vecteurs, sur le PC)
#     sh tools/gcc/test/dktest.sh                            -> build/try/dktest, card/fuzz.ref
# Puis build/try/dkvec/card sur la carte en /dkvec, build/try/dktest en /bin/dktest.
set -eu
T=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}/bin/aarch64-onyx-elf-
V=build/try/dkvec
g++ -O1 -g -fsanitize=address -w -I$V/refsrc tools/gcc/test/dkref.cpp -o $V/dkref
$V/dkref $V/card/fuzz.bin $V/card/fuzz.ref
ls -l $V/card/fuzz.*
GCC=build/gcc/sdroot/gcc
${T}gcc -mcpu=cortex-a72 -specs=build/office/posix.specs -O2 -Wall -Wno-pointer-to-int-cast -I$GCC/include/kits \
	tools/gcc/test/dktest.c -L$GCC/lib -ldocumentkit -o build/try/dktest
${T}strip build/try/dktest
ls -l build/try/dktest
