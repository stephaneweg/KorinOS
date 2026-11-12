#!/bin/sh
# Refait tools/python/korin.patch depuis deux arbres de CPython (WSL) :
#     sh tools/python/mkpatch.sh <arbre d'origine> <arbre retouche>
# ex. ~/.cache/armpi-python/orig/Python-3.14.10 et ~/.cache/armpi-python/edit/Python-3.14.10
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
O=$(cd "$1" && pwd)
E=$(cd "$2" && pwd)
cd "$(dirname "$O")"
# en-tetes a/ et b/, pour patch -p1 ; sans dates (le patch ne change pas d'une fois sur l'autre)
diff -ruN -x '*.orig' -x '__pycache__' "$O" "$E" |
	sed -E -e "s|^diff -ruN .*||" \
	       -e "s|^--- $O/([^[:space:]]+).*|--- a/\1|" \
	       -e "s|^\+\+\+ $E/([^[:space:]]+).*|+++ b/\1|" |
	grep -v '^$' > "$ROOT/tools/python/korin.patch" || true
grep -c '^+++ ' "$ROOT/tools/python/korin.patch"
