#!/bin/sh
# Paquet testing_prep_nodejs : son arbre build/testing/sdroot (packages.ini : root), depuis tools/testing/sdroot
# (run.sh, LISEZMOI.txt) et les essais C de prebuilt/ (posixtest, posixtest-cxx : leur paquet system-tests n'est pas
# publie, publish = 0). A lancer avant mkrepo.py / publish.sh, apres sh tools/posix/build.sh.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$ROOT/build/testing/sdroot
rm -rf "$OUT"; mkdir -p "$OUT"
cp -r "$ROOT/tools/testing/sdroot/." "$OUT/"
mkdir -p "$OUT/test/prep_nodejs/bin"
cp "$ROOT/prebuilt/posixtest" "$ROOT/prebuilt/posixtest-cxx" "$OUT/test/prep_nodejs/bin/"
echo "build/testing/sdroot : pret"
