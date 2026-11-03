#!/bin/sh
# build-koton.sh -- Koton, le studio d'Onyx, sur ARMPI (src/apps/koton, README.md du dossier) : le moteur et
# l'interface en C++, directement sur UIKit, le kit de l'OS (aucun pont : docs/UIKIT-MANQUES.md),
# la synthese et le son par AudioKit (kit en assembleur) ; ses greffons, un programme chacun
# (src/apps/koton/plugins/<id>). -> $WK_OUT/out/koton.app/{main, app.txt, plugins/<id>/{main,
# plugin.json}}, $WK_OUT/out/kptest (l'essai des greffons) ; stage.sh le met sur la carte
# (SD0:/apps/koton.app, SD0:/bin/kptest).
#
#   sh tools/webkit/build-koton.sh
#   (sur ARMPI)  le dock, ou SD0:/apps/koton.app/main [chanson.kson]
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
unset CFLAGS CXXFLAGS LDFLAGS CC CXX
S=$ONYX_SYSROOT
K=$ARMPI/src/apps/koton
O=$WK_OUT/koton
mkdir -p "$O" "$WK_OUT/out/koton.app"
# UIKit et AudioKit depuis le C : les interfaces generees de leurs .abi
python3 -I "$ARMPI/tools/kits/abi2c.py" "$ARMPI/src/kits/uikit/uikit.abi" "$O" \
	--const "$ARMPI/src/inc/armpi.inc" "$ARMPI/src/inc/ws_ext.inc" "$ARMPI/src/kits/uikit/uikit.inc"
python3 -I "$ARMPI/tools/kits/abi2c.py" "$ARMPI/src/kits/audiokit/audiokit.abi" "$O" \
	--const "$ARMPI/src/kits/audiokit/audiokit.inc"
TC=${ONYX_TOOLCHAIN_PREFIX}
for k in uikit audiokit; do
	${TC}gcc -mcpu=cortex-a72 -c "$O/${k}_abi.S" -o "$O/${k}_abi.o"
	${TC}gcc -mcpu=cortex-a72 -O2 -specs="$S/lib/onyx-cc.specs" -c "$O/${k}_abi_open.c" -o "$O/${k}_abi_open.o"
done

INC="-I$K -I$K/ui -I$ARMPI/src/kits/uikit/include -I$O -I$S/include -I$S/include/freetype2"
COMMON="-mcpu=cortex-a72 -O2 -g0 -ffunction-sections -fdata-sections -fno-pic -fno-pie -DFD_SETSIZE=1024"
CXXF="$COMMON -specs=$S/lib/onyx-cc.specs -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wno-write-strings $INC"

# chaque source -> son objet, en parallele (une source plus recente que son objet est recompilee)
: > "$O/jobs.txt"
add () { # source objet
	if [ ! -f "$O/$2.o" ] || [ "$1" -nt "$O/$2.o" ] || [ -n "$(find "$K" -name '*.h' -newer "$O/$2.o" -print -quit)" ]; then
		printf '%s %s\n' "$1" "$2" >> "$O/jobs.txt"
	fi
}
for f in "$K"/ui/*.cpp; do b=$(basename "$f" .cpp); add "$f" "ui_$b"; done
for f in "$K"/engine/*.cpp; do b=$(basename "$f" .cpp); add "$f" "e_$b"; done
add "$K/main.cpp" main
add "$K/plug/plughost.cpp" plughost
add "$K/plug/plugctx.cpp" plugctx
add "$K/plug/kptest.cpp" kptest
for f in "$K"/plugins/*/main.cpp; do id=$(basename "$(dirname "$f")"); add "$f" "kp_$id"; done
export TC CXXF O
if [ -s "$O/jobs.txt" ]; then
	xargs -P "$JOBS" -L 1 sh -c 'echo "koton : $0"; ${TC}g++ $CXXF -c "$0" -o "$O/$1.o"' < "$O/jobs.txt"
fi

rm -f "$O/libkits_c.a" "$O"/ui_*.o.old
${AR} rcs "$O/libkits_c.a" "$O/uikit_abi.o" "$O/uikit_abi_open.o" "$O/audiokit_abi.o" "$O/audiokit_abi_open.o"
echo "koton : edition de liens"
${TC}g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	"$O"/ui_*.o "$O"/e_*.o "$O/plughost.o" "$O/plugctx.o" "$O/main.o" "$O/libkits_c.a" \
	-lm -o "$O/koton"
${TC}size "$O/koton"
${STRIP} -o "$WK_OUT/out/koton.app/main" "$O/koton"
cp "$K/app.txt" "$K/data/drums.json" "$K/data/demo.kson" "$WK_OUT/out/koton.app/"

# les greffons : un programme par dossier, sans kit (kplug.h : POSIX seul)
rm -rf "$WK_OUT/out/koton.app/plugins"
for f in "$K"/plugins/*/main.cpp; do
	d=$(dirname "$f"); id=$(basename "$d"); P=$WK_OUT/out/koton.app/plugins/$id
	mkdir -p "$P"
	${TC}g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections "$O/kp_$id.o" -lm -o "$O/kp_$id"
	${STRIP} -o "$P/main" "$O/kp_$id"
	cp "$d/plugin.json" "$P/"
done
${TC}g++ -mcpu=cortex-a72 -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections \
	"$O"/ui_view.o "$O"/ui_vpath.o "$O"/e_*.o "$O/plughost.o" "$O/plugctx.o" "$O/kptest.o" "$O/libkits_c.a" \
	-lm -o "$O/kptest"
${STRIP} -o "$WK_OUT/out/kptest" "$O/kptest"
ls -l "$WK_OUT/out/koton.app" "$WK_OUT/out/koton.app/plugins"/*/main
