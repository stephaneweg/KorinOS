#!/bin/sh
# apps.sh -- les apps C et C++ de Korin pour le PC (x86-64), dans WSL (docs/etude/PC-PILOTES.md, « Chaîne
# x86-64 et apps C/C++ ») : les mêmes sources et options que tools/office/build.sh (le Pi), la chaîne
# x86_64-onyx-elf (tools/pc/toolchain.sh) et libposix du PC (ARCH=x86_64 sh tools/posix/build.sh).
#
#   sh tools/pc/apps.sh                  les kits depuis le C, puis toutes les apps de la liste
#   sh tools/pc/apps.sh letters sheet    seulement celles-ci
#   KEEP_GOING=1 sh tools/pc/apps.sh     une app qui ne se construit pas est notée, les autres continuent
#
# Produit :
#   build/pc/gcc/include/kits, build/pc/gcc/lib   les interfaces C des kits (comme /gcc du Pi, tools/gcc/kits.sh) :
#                                                 <kit>_abi.h, lib<kit>.a (tools/kits/abi2c.py --arch x86_64)
#   build/pc/apps/sdroot/apps/<nom>.app/{main,app.txt,données}   (tools/pc/sysroot_apps.sh les met dans l'arbre)
#   build/pc/apps/sdroot/bin/basic, lib/*.bi     le runtime de BASIC (docs/BASIC.md)
#   build/pc/apps/status.txt                      ce qui est construit, ce qui ne l'est pas et pourquoi
# Les bibliothèques tierces (OpenSSL, MuPDF) : tools/pc/thirdparty.sh, dans build/pc/thirdparty.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/x86_64-onyx-elf-14.2}
TC=$TOOL/bin/x86_64-onyx-elf-
POSIX=$ROOT/build/pc/posix
GCC=$ROOT/build/pc/gcc
OUT=$ROOT/build/pc/apps
TP=$ROOT/build/pc/thirdparty
JOBS=${JOBS:-$(nproc)}
KEEP_GOING=${KEEP_GOING:-0}
[ -f "$POSIX/lib/posix.specs" ] || { echo "apps : d'abord ARCH=x86_64 sh tools/posix/build.sh" >&2; exit 1; }
# (la liste de tools/office/build.sh, avec les apps passées de l'asm au C ; printd est un service de src/sys, vector
# demande Skia : à part ; pkgman vient de tools/pkg/build.sh sur le Pi)
ALL="letters sheet slides cardfile media circuits basic qbasic qbstudio gpiolab turtle cosmozoom irc mail telegram
courier calendar notes paint pdf photos printd archiver iconedit disks pinball critters arabesque pocketqi help imageview
screenshot budget ndsemu terminal files welcome editor viewer calculator uidemo builder mediaplayer mixer music piano
recorder targets bubbles arkanoid pkgman koton"
[ $# -gt 0 ] || set -- $ALL
mkdir -p "$OUT" "$GCC/include/kits" "$GCC/lib"
: > "$OUT/status.new"

# ---- les kits depuis le C (comme tools/gcc/kits.sh) ----
K=$ROOT/src/kits
A=$ROOT/src/inc
W=$OUT/kits
rm -rf "$W" && mkdir -p "$W"
kit () { # nom, puis les .inc des constantes
	n=$1
	shift
	[ -f "$K/$n/$n.abi" ] || return 0
	python3 -I "$ROOT/tools/kits/abi2c.py" "$K/$n/$n.abi" "$W" --arch x86_64 --const "$@" >/dev/null
	${TC}gcc -c "$W/${n}_abi.S" -o "$W/${n}_abi.o"
	${TC}gcc -O2 -g0 -specs="$POSIX/lib/posix.specs" -c "$W/${n}_abi_open.c" -o "$W/${n}_abi_open.o"
	rm -f "$GCC/lib/lib$n.a"
	${TC}ar rcs "$GCC/lib/lib$n.a" "$W/${n}_abi.o" "$W/${n}_abi_open.o"
	cp "$W/${n}_abi.h" "$GCC/include/kits/"
}
kit uikit "$A/armpi.inc" "$A/ws_ext.inc" "$K/uikit/uikit.inc"
kit audiokit "$K/audiokit/audiokit.inc"
kit netkit "$K/netkit/netkit.inc"
kit emukit "$K/emukit/emukit.inc" "$A/pad.inc"
kit gpukit "$K/gpukit/gpukit.inc"
kit mediakit "$K/mediakit/mediakit.inc"
kit archivekit "$K/archivekit/archivekit.inc"
kit documentkit "$K/documentkit/documentkit.inc"
kit gpiokit "$K/gpiokit/gpiokit.inc"
kit printerkit "$K/printerkit/printerkit.inc"
kit buskit "$K/buskit/buskit.inc"
cp "$K/uikit/include/uikit.h" "$K/mediakit/mediakit.h" "$K/emukit/include/emukit.h" "$K/printerkit/include/printerkit.h" \
	"$K/printerkit/include/pdfprint.h" "$GCC/include/kits/"

SPECS=$POSIX/lib/posix.specs
BASEFLAGS="-specs=$SPECS -O2 -g0 -ffunction-sections -fdata-sections -MMD -MP -I. -I$GCC/include/kits -I$ROOT/src/kits/uikit/include"
note () { printf '%s\n' "$*" >> "$OUT/status.new"; }

# KotonStudio (src/apps/koton, README.md du dossier) : options de tools/webkit/build-koton.sh (le Pi) ; un programme
# pour l'app, un par greffon (plugins/<id>, POSIX seul), et bin/kptest ; ses chansons dans home/koton comme sur le Pi
build_koton () {
	S=$ROOT/src/apps/koton
	O=$OUT/obj/koton
	D=$OUT/sdroot/apps/koton.app
	FLAGS="$BASEFLAGS -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wno-write-strings -DFD_SETSIZE=1024 -I$S/ui"
	mkdir -p "$O" "$D" "$OUT/sdroot/bin" "$OUT/sdroot/home/koton/songs" "$OUT/sdroot/home/koton/demos"
	cd "$S"
	{
		echo 'all:'
		ls ui/*.cpp engine/*.cpp main.cpp plug/*.cpp plugins/*/main.cpp | while read -r f; do
			o=$O/${f%.cpp}.o
			printf 'all: %s\n%s: %s\n\t@mkdir -p $(dir $@)\n\t%sg++ %s -c %s -o $@\n-include %s\n' "$o" "$o" "$f" "$TC" "$FLAGS" "$f" "${o%.o}.d"
		done
	} > "$O/make.mk"
	if ! make -s -j"$JOBS" -f "$O/make.mk" > "$O/make.log" 2>&1; then
		note "koton : ECHEC de compilation ($(grep -m1 -E 'error|Error' "$O/make.log" | cut -c1-200))"
		grep -E 'error' "$O/make.log" | head -5 >&2
		return 1
	fi
	KL="-specs=$SPECS -Wl,--gc-sections"
	KITS="-L$GCC/lib -luikit -laudiokit -lm"
	if ! { ${TC}g++ $KL "$O"/ui/*.o "$O"/engine/*.o "$O/plug/plughost.o" "$O/plug/plugctx.o" "$O/main.o" $KITS -o "$O/main" &&
		${TC}g++ $KL "$O"/ui/*.o "$O"/engine/*.o "$O/plug/plughost.o" "$O/plug/plugctx.o" "$O/plug/kptest.o" $KITS -o "$O/kptest"; } > "$O/link.log" 2>&1; then
		note "koton : ECHEC de l'édition de liens ($(grep -m1 -E 'undefined|error' "$O/link.log" | cut -c1-200))"
		grep -E 'undefined|error' "$O/link.log" | sort -u | head -8 >&2
		return 1
	fi
	${TC}strip -o "$D/main" "$O/main"
	${TC}strip -o "$OUT/sdroot/bin/kptest" "$O/kptest"
	rm -rf "$D/plugins"
	for d in plugins/*/; do
		id=$(basename "$d")
		mkdir -p "$D/plugins/$id"
		${TC}g++ $KL "$O/plugins/$id/main.o" -lm -o "$O/plugins/$id/prog" || { note "koton : ECHEC du greffon $id"; return 1; }
		${TC}strip -o "$D/plugins/$id/main" "$O/plugins/$id/prog"
		cp "$d/plugin.json" "$D/plugins/$id/"
	done
	cp app.txt data/drums.json help_*.md "$D/"
	cp -r help "$D/"
	cp data/demo.kson "$OUT/sdroot/home/koton/songs/"
	cp demos/*.kson "$OUT/sdroot/home/koton/demos/"
	note "koton : construite ($(stat -c %s "$D/main") octets, $(ls -d "$D"/plugins/*/ | wc -l) greffons, bin/kptest)"
}

build_one () { # nom
	app=$1
	FLAGS=$BASEFLAGS
	EXTRA=
	case $app in
	koton)	build_koton; return ;;
	mail|telegram|courier|pocketqi|pkgman)	# (OpenSSL : tools/pc/thirdparty.sh openssl)
		[ -f "$TP/openssl/lib/libssl.a" ] || { note "$app : pas construite (OpenSSL x86-64 absent : tools/pc/thirdparty.sh openssl)"; return 0; }
		FLAGS="$FLAGS -I$TP/openssl/include"
		[ "$app" != pkgman ] || FLAGS="$FLAGS -I$ROOT/src/lib/pkg"	# (pkglib.h, comme tools/pkg/build.sh)
		EXTRA="-L$TP/openssl/lib -lssl -lcrypto" ;;
	pdf)	# (MuPDF : tools/pc/thirdparty.sh mupdf)
		[ -f "$TP/mupdf/libmupdf.a" ] || { note "pdf : pas construite (MuPDF x86-64 absent : tools/pc/thirdparty.sh mupdf)"; return 0; }
		FLAGS="$FLAGS -I$TP/mupdf/include -DHAVE_SIGSETJMP=0"
		EXTRA="-L$TP/mupdf -lmupdf" ;;
	printd)	# (son main.cpp appelle le noyau par svc 0 en asm : à passer à <armpi/syscall.h>, comme les apps)
		if grep -q '"svc 0"' "$ROOT/src/sys/printd/main.cpp"; then
			note "printd : pas construit (src/sys/printd/main.cpp : appel système en asm AArch64, à passer à <armpi/syscall.h>)"
			return 0
		fi ;;
	pinball|arabesque)	# (la physique d'Onyx, le moteur d'Arabesque : mêmes bits partout)
		FLAGS="$FLAGS -ffp-contract=off" ;;
	ndsemu)	# (comme tools/emu/build.sh ; l'interpréteur seul : le JIT n'est construit que pour AArch64)
		FLAGS="$FLAGS -O3 -w -fno-math-errno -fno-trapping-math" ;;
	esac
	S=$ROOT/src/apps/$app
	O=$OUT/obj/$app
	D=$OUT/sdroot/apps/$app.app
	[ "$app" != basic ] || D=$OUT/sdroot/bin
	if [ "$app" = printd ]; then	# (le service d'impression, docs/IMPRESSION.md)
		S=$ROOT/src/sys/printd
		D=$OUT/sdroot/sys
		FLAGS="$FLAGS -I$ROOT/src/apps/letters -I$ROOT/src/apps/paint"
	fi
	mkdir -p "$O" "$D"
	cd "$S"
	{
		echo 'all:'
		find . -name '*.c' -o -name '*.cpp' | sed 's#^\./##' | grep -v '^data/' | while read -r f; do
			o=$O/$(echo "$f" | sed 's/\.[a-z]*$//').o
			echo "all: $o"
			echo "$o: $f"
			echo "	@mkdir -p \$(dir \$@)"
			case $f in
			*.cpp) echo "	${TC}g++ -std=gnu++20 -Wno-write-strings $FLAGS -c $f -o \$@" ;;
			*) echo "	${TC}gcc $FLAGS -c $f -o \$@" ;;
			esac
			echo "-include $(echo "$o" | sed 's/\.o$/.d/')"
		done
	} > "$O/make.mk"
	if ! make -s -j"$JOBS" -f "$O/make.mk" > "$O/make.log" 2>&1; then
		note "$app : ECHEC de compilation ($(grep -m1 -E 'error|Error' "$O/make.log" | cut -c1-200))"
		cat "$O/make.log" | grep -E 'error' | head -5 >&2
		return 1
	fi
	LD=${TC}gcc
	[ -z "$(find . -name '*.cpp' | grep -v '^\./data/')" ] || LD=${TC}g++
	LIBS=$(ls "$GCC"/lib/lib*.a | sed 's#.*/lib\(.*\)\.a#-l\1#' | tr '\n' ' ')
	if ! $LD -specs="$SPECS" -Wl,--gc-sections $(find "$O" -name '*.o') -L"$GCC/lib" $LIBS $EXTRA -lm -o "$O/main" > "$O/link.log" 2>&1; then
		note "$app : ECHEC de l'édition de liens ($(grep -m1 -E 'undefined|error' "$O/link.log" | cut -c1-200))"
		grep -E 'undefined|error' "$O/link.log" | sort -u | head -8 >&2
		return 1
	fi
	if [ "$app" = basic ]; then
		${TC}strip -o "$D/basic" "$O/main"
		L=$OUT/sdroot/lib
		mkdir -p "$L"
		cp "$ROOT"/src/lib/basic/bi/*.bi "$L/"
		for k in audiokit netkit gpukit mediakit archivekit documentkit gpiokit printerkit; do python3 -I "$ROOT/tools/kits/abi2bas.py" "$ROOT/src/kits/$k/$k.abi" "$L" >/dev/null; done
		for k in uikit appkit; do python3 -I "$ROOT/tools/kits/abi2bas.py" "$ROOT/src/kits/$k/$k.abi" "$L" --head "$ROOT/src/lib/basic/bi/$k.head" >/dev/null; done
		note "basic : construite ($(stat -c %s "$D/basic") octets, bin/basic)"
		return 0
	fi
	if [ "$app" = printd ]; then
		${TC}strip -o "$D/printd" "$O/main"
		note "printd : construit ($(stat -c %s "$D/printd") octets, sys/printd)"
		return 0
	fi
	${TC}strip -o "$D/main" "$O/main"
	cp app.txt "$D/"
	[ ! -d data ] || cp -r data/. "$D/"
	# (l'aide de l'app et ses images, comme build.ps1 sur le Pi : le menu Aide de menubar, help.app)
	for f in help_*.md; do [ ! -f "$f" ] || cp "$f" "$D/"; done
	[ ! -d help ] || cp -r help "$D/"
	[ "$app" != arabesque ] || [ ! -d presets ] || { mkdir -p "$OUT/sdroot/home/arabesque"; cp presets/*.arb "$OUT/sdroot/home/arabesque/"; }
	note "$app : construite ($(stat -c %s "$D/main") octets)"
}

fails=0
for app; do
	if ! (build_one "$app"); then
		fails=$((fails + 1))
		[ "$KEEP_GOING" = 1 ] || { cat "$OUT/status.new"; exit 1; }
	fi
done
# (le tableau d'ensemble : les lignes de ce lancement remplacent celles des mêmes apps)
touch "$OUT/status.txt"
for app; do grep -v "^$app : " "$OUT/status.txt" > "$OUT/status.tmp" || true; mv "$OUT/status.tmp" "$OUT/status.txt"; done
cat "$OUT/status.new" >> "$OUT/status.txt"
sort -o "$OUT/status.txt" "$OUT/status.txt"
cat "$OUT/status.new"
echo "apps : $fails échec(s)"
