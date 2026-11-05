#!/bin/sh
# songcheck.sh -- le banc des morceaux de Koton sur le PC (songcheck.cpp) : construit avec le g++ de WSL sur les sources
# du moteur de Koton, puis lance sur les fichiers donnes (par defaut : src/apps/koton/demos/*.kson).
#     sh tools/koton/songcheck.sh [fichier.kson...]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
K=$ROOT/src/apps/koton
B=${TMPDIR:-/tmp}/koton-songcheck
mkdir -p "$B"
g++ -std=gnu++17 -O1 -g -w -fsanitize=address,undefined -I"$K" -I"$K/engine" "$ROOT/tools/koton/songcheck.cpp" \
	"$K/engine/model.cpp" "$K/engine/theory.cpp" "$K/engine/gen_chords.cpp" "$K/engine/gen_melodic.cpp" \
	"$K/engine/gen_rhythm.cpp" "$K/engine/compile.cpp" -o "$B/songcheck"
[ $# -gt 0 ] || set -- "$K"/demos/*.kson
"$B/songcheck" "$@"
