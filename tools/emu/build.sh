#!/bin/sh
# build.sh -- les emulateurs en C++ par la chaine croisee (WSL), pour aller vite : memes sources que
# pibuild sous ARMPI, hors du depot. -> build/emu/sdroot/apps/<nom>.app/{main,app.txt}
#
#   sh tools/emu/build.sh [ndsemu] [n3dsemu]
#
# A lancer depuis la racine du depot. GCC et POSIX : l'arbre de tools/gcc/build.sh (par defaut celui
# de la copie principale, qu'un worktree n'a pas).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
MAIN=${MAIN:-$(r=$(cd "$(dirname "$0")/../.." && pwd); echo "${r%%/.claude/worktrees/*}")}
GCC=${GCC:-$MAIN/build/gcc/sdroot/gcc}
POSIX=${POSIX:-$MAIN/build/posix}
OUT=$ROOT/build/emu
JOBS=${JOBS:-8}
[ $# -gt 0 ] || set -- ndsemu n3dsemu
K=$OUT/kits
mkdir -p "$K"
sed -E "s#/[^ ]*/build/posix#$POSIX#g" "$POSIX/lib/posix.specs" > "$OUT/posix.specs"
# les relais d'EmuKit
python3 -I "$ROOT/tools/kits/abi2c.py" "$ROOT/src/kits/emukit/emukit.abi" "$K" --const "$ROOT/src/kits/emukit/emukit.inc" "$ROOT/src/inc/pad.inc"
${TC}gcc -mcpu=cortex-a72 -c "$K/emukit_abi.S" -o "$K/emukit_abi.o"
${TC}gcc -mcpu=cortex-a72 -O2 -g0 -specs="$OUT/posix.specs" -I"$GCC/include/kits" -c "$K/emukit_abi_open.c" -o "$K/emukit_abi_open.o"
rm -f "$K/libemukit.a"
${TC}ar rcs "$K/libemukit.a" "$K/emukit_abi.o" "$K/emukit_abi_open.o"
cp "$ROOT/src/kits/emukit/include/emukit.h" "$K/"
FLAGS="-mcpu=cortex-a72 -specs=$OUT/posix.specs -O3 -g0 -w -ffunction-sections -fdata-sections -MMD -MP -I. -I$K -I$GCC/include/kits -DN3DS_ARMPI -I$ROOT/tools/mesa/armgl -I$ROOT/prebuilt/mesa/include -fno-math-errno -fno-trapping-math"
for app; do
	S=$ROOT/src/apps/$app
	O=$OUT/obj/$app
	A=$OUT/sdroot/apps/$app.app
	mkdir -p "$O" "$A"
	cd "$S"
	: > "$O/make.mk"
	objs=""
	for f in $(find . -name '*.cpp' -o -name '*.S' | sed 's#^\./##'); do
		o=$O/$(echo "$f" | tr / _).o
		objs="$objs $o"
		case $f in
		*.S) printf '%s: %s\n\t%s -mcpu=cortex-a72 -c %s -o %s\n' "$o" "$f" "${TC}gcc" "$f" "$o" >> "$O/make.mk" ;;
		*) printf '%s: %s\n\t%s %s -std=gnu++17 -c %s -o %s\n-include %s\n' "$o" "$f" "${TC}g++" "$FLAGS" "$f" "$o" "${o%.o}.d" >> "$O/make.mk" ;;
		esac
	done
	printf 'all: %s\n' "$objs" > "$O/all.mk"
	cat "$O/make.mk" >> "$O/all.mk"
	make -s -j"$JOBS" -f "$O/all.mk" all
	# (n3dsemu : Mesa, relie en statique, tools/mesa/build-armgl.sh)
	MESA=
	if [ "$app" = n3dsemu ]; then
		P=$ROOT/prebuilt/mesa/lib
		MESA="$P/libglkit.a"	# (Mesa : GlKit, le kit partage)
		cp sysfont.bcfnt "$A/"
	fi
	${TC}g++ -mcpu=cortex-a72 -specs="$OUT/posix.specs" -Wl,--gc-sections $objs $MESA -L"$K" -L"$GCC/lib" -lemukit -lgpukit -o "$A/main"
	cp app.txt "$A/"
	echo "$app : $(stat -c %s "$A/main") octets"
	# (le banc du Pi : le meme coeur sans fenetre, hors de la carte : tools/emu/run_n3ds.cpp)
	if [ "$app" = n3dsemu ]; then
		${TC}g++ $FLAGS -std=gnu++17 -DN3DS_PORT -DN3DS_PI -c "$ROOT/tools/emu/run_n3ds.cpp" -o "$O/run_n3ds.o"
		${TC}g++ -mcpu=cortex-a72 -specs="$OUT/posix.specs" -Wl,--gc-sections $(echo "$objs" | tr ' ' '\n' | grep -v 'main.cpp.o') "$O/run_n3ds.o" $MESA -o "$OUT/run_n3ds"
		${TC}strip "$OUT/run_n3ds"
		echo "run_n3ds : $(stat -c %s "$OUT/run_n3ds") octets"
	fi
	cd "$ROOT"
done
