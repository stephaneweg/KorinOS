#!/bin/sh
# build-tests.sh -- les programmes Win16 d'essai de win16 (docs/etude/WIN16.md), par Open Watcom v2, dans WSL.
#
#   sh tools/win16/build-tests.sh          -> build/win16/tests/*.exe
#
# Open Watcom : WATCOM=~/win16/watcom par défaut (archive ow-snapshot.tar.xz de la version « Current-build » de
# github.com/open-watcom/open-watcom-v2, extraite telle quelle).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
WATCOM=${WATCOM:-$HOME/win16/watcom}
[ -x "$WATCOM/binl64/wcl" ] || { echo "build-tests : Open Watcom absent ($WATCOM)" >&2; exit 1; }
export WATCOM
export PATH="$WATCOM/binl64:$PATH"
export INCLUDE="$WATCOM/h:$WATCOM/h/win"
OUT=$ROOT/build/win16/tests
mkdir -p "$OUT"
cd "$OUT"
for f in "$ROOT"/tools/win16/tests/*.c; do
	n=$(basename "$f" .c)
	wcl -q -bt=windows -l=windows -ms -zW -fe="$n.exe" "$f" >"$n.log" 2>&1 || { cat "$n.log" >&2; exit 1; }
	rm -f "$n.o" "$n.obj"
	echo "$n.exe : $(stat -c %s "$n.exe") octets"
done
