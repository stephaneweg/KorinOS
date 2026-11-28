#!/bin/sh
# aicompose.sh -- construit tools/koton/aicompose.cpp (le moteur de Koton : IA, theorie, generateurs, compilation) pour
# Linux, puis le lance avec les arguments donnes.     sh tools/koton/aicompose.sh prompt|apply ...
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
K=$ROOT/src/apps/koton
B=${TMPDIR:-/tmp}/koton-aicompose
mkdir -p "$B"
if [ ! -x "$B/aicompose" ] || [ -n "$(find "$K/engine" "$ROOT/tools/koton/aicompose.cpp" -newer "$B/aicompose" 2>/dev/null | head -1)" ]; then
	g++ -std=gnu++17 -O1 -w -I"$K" -I"$K/engine" -I"$K/ui" "$ROOT/tools/koton/aicompose.cpp" \
		"$K/engine/model.cpp" "$K/engine/theory.cpp" "$K/engine/gen_chords.cpp" "$K/engine/gen_melodic.cpp" \
		"$K/engine/gen_rhythm.cpp" "$K/engine/compile.cpp" "$K/engine/ai.cpp" "$K/engine/ai_place.cpp" -o "$B/aicompose"
fi
exec "$B/aicompose" "$@"
