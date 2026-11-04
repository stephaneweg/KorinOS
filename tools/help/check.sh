#!/bin/sh
# check.sh -- compile et lie les exemples complets du guide du developpeur (src/apps/help/data/api/*.md, extraits par
# tools/help/extract.py) et le squelette d'app (data/api/skeleton), avec les options de pibuild.mk et de
# tools/office/build.sh : un exemple du guide se compile tel quel sous Korin OS. Rien n'est installe.
#
#   sh tools/help/check.sh                 toutes les pages
#   sh tools/help/check.sh uikit_fr netkit_en   ces pages seulement
#
# A lancer dans WSL depuis la racine du depot, apres tools/gcc/kits.sh (les relais des kits dans build/gcc/sdroot/gcc/lib).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
GCC=$ROOT/build/gcc/sdroot/gcc
API=$ROOT/src/apps/help/data/api
# (un dossier par verification : plusieurs peuvent tourner a la fois)
OUT=$ROOT/build/helpcheck/$$
rm -rf "$OUT"; mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT
sed -E "s#/[^ ]*/build/posix#$ROOT/build/posix#g" "$ROOT/build/posix/lib/posix.specs" > "$OUT/posix.specs"
FLAGS="-mcpu=cortex-a72 -specs=$OUT/posix.specs -O2 -g0 -ffunction-sections -fdata-sections -Wall -Wno-unused-function -I$GCC/include -I$GCC/include/kits"
LIBS=$(ls "$GCC"/lib/lib*.a | sed 's#.*/lib\(.*\)\.a#-l\1#' | tr '\n' ' ')
if [ $# -gt 0 ]; then
	PAGES=$(for p; do echo "$API/$p.md"; done)
else
	PAGES=$(ls "$API"/*.md)
fi
rm -rf "$OUT/ex"
python3 -I "$ROOT/tools/help/extract.py" "$OUT/ex" $PAGES || exit 1
ok=0; bad=0
# un exemple : compile, lie (tous les relais des kits, comme pibuild ; ld ne prend que ce qui est appele)
build () { # source, nom
	src=$1; obj=${src%.*}.o; exe=${src%.*}.elf
	case $src in
	*.cpp) cc="${TC}g++ -std=gnu++20 -Wno-write-strings" ;;
	*) cc="${TC}gcc" ;;
	esac
	if $cc $FLAGS -I"$(dirname "$src")" -c "$src" -o "$obj" 2> "$obj.log" && \
	   ${TC}g++ -mcpu=cortex-a72 -specs="$OUT/posix.specs" -Wl,--gc-sections "$obj" -L"$GCC/lib" $LIBS -lm -o "$exe" 2>> "$obj.log"; then
		[ -s "$obj.log" ] && { echo "avertissements : $2"; cat "$obj.log"; }
		echo "ok    : $2"; ok=$((ok + 1))
	else
		echo "ECHEC : $2"; cat "$obj.log"; bad=$((bad + 1))
	fi
}
while IFS="	" read -r src page line; do
	[ -n "$src" ] || continue
	build "$src" "$(basename "$page"):$line"
done < "$OUT/ex/liste.txt"
# le squelette : tous ses .cpp ensemble
if [ $# -eq 0 ] && [ -d "$API/skeleton" ]; then
	S=$OUT/skeleton; rm -rf "$S"; mkdir -p "$S"
	fail=0
	for f in "$API"/skeleton/*.cpp; do
		${TC}g++ -std=gnu++20 -Wno-write-strings $FLAGS -I"$API/skeleton" -c "$f" -o "$S/$(basename "${f%.cpp}").o" 2>> "$S/log" || fail=1
	done
	if [ $fail = 0 ] && ${TC}g++ -mcpu=cortex-a72 -specs="$OUT/posix.specs" -Wl,--gc-sections "$S"/*.o -L"$GCC/lib" $LIBS -lm -o "$S/main" 2>> "$S/log"; then
		[ -s "$S/log" ] && cat "$S/log"
		echo "ok    : skeleton"; ok=$((ok + 1))
	else
		echo "ECHEC : skeleton"; cat "$S/log"; bad=$((bad + 1))
	fi
fi
echo "exemples : $ok ok, $bad en echec"
[ $bad = 0 ]
