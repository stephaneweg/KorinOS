#!/bin/sh
# build.sh -- la suite bureautique par la chaine croisee (WSL), pour aller vite : memes sources, memes options que
# pibuild.mk sous ARMPI, hors du depot et du selfhost, comme Koton. -> build/office/sdroot/apps/<nom>.app/{main,app.txt,data},
# que build.ps1 met sur la carte.
#
#   sh tools/office/build.sh [letters sheet slides cardfile media circuits basic qbasic qbstudio gpiolab turtle cosmozoom irc mail telegram courier paint pdf photos calendar notes vector pocketqi]
#   JOBS=8 sh tools/office/build.sh sheet
#
# A lancer depuis la racine du depot, apres tools/gcc/kits.sh (les relais des kits dans build/gcc/sdroot/gcc/lib).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
GCC=$ROOT/build/gcc/sdroot/gcc
OUT=$ROOT/build/office
JOBS=${JOBS:-8}
[ $# -gt 0 ] || set -- letters sheet slides cardfile media circuits basic qbasic qbstudio gpiolab turtle cosmozoom irc mail telegram courier calendar notes paint pdf photos vector printd archiver iconedit disks pinball critters arabesque pocketqi
mkdir -p "$OUT"
sed -E "s#/[^ ]*/build/posix#$ROOT/build/posix#g" "$ROOT/build/posix/lib/posix.specs" > "$OUT/posix.specs"

BASEFLAGS="-mcpu=cortex-a72 -specs=$OUT/posix.specs -O2 -g0 -ffunction-sections -fdata-sections -MMD -MP -I. -I$GCC/include/kits -I$ROOT/src/kits/uikit/include"
for app; do
	# (OpenSSL, le seul TLS de l'OS, n'est pas sous /gcc : ces apps ne se construisent que sur le PC)
	FLAGS=$BASEFLAGS
	SSL=
	case $app in mail|telegram|courier|pocketqi)
		FLAGS="$FLAGS -I$ROOT/build/openssl/include"
		SSL="-L$ROOT/build/openssl/lib -lssl -lcrypto" ;;
	pdf)	# (MuPDF, hors du depot : tools/pdf/build.sh)
		FLAGS="$FLAGS -I$ROOT/build/pdf/include -DHAVE_SIGSETJMP=0"
		SSL="-L$ROOT/build/pdf -lmupdf" ;;
	vector)	# (Skia, HarfBuzz, libxml2 : le sysroot de tools/webkit/build-deps.sh ; les options de Skia viennent de skia.pc)
		SR=${SR:-$HOME/armpi-webkit-build/sysroot}
		FLAGS="$FLAGS -I$SR/include/skia -I$SR/include/harfbuzz -I$SR/include/libxml2 $(PKG_CONFIG_LIBDIR=$SR/lib/pkgconfig pkg-config --cflags-only-other skia)"
		SSL="-L$SR/lib -lskia -lxml2 -lharfbuzz -lfreetype -lpng16 -ljpeg -lwebpmux -lwebpdemux -lwebp -lsharpyuv -lbrotlidec -lz" ;;
	pinball|arabesque)	# (la physique d'Onyx, le moteur d'Arabesque et son port Windows : memes bits partout)
		FLAGS="$FLAGS -ffp-contract=off" ;;
	esac
	S=$ROOT/src/apps/$app
	O=$OUT/obj/$app
	A=$OUT/sdroot/apps/$app.app
	[ "$app" != basic ] || A=$OUT/sdroot/bin	# (le runtime de BASIC, pibuild bin : docs/BASIC.md)
	if [ "$app" = printd ]; then	# (le service d'impression : docs/IMPRESSION.md ; pdfwrite.h de Letters, stb_image.h de Paint)
		S=$ROOT/src/sys/printd
		A=$OUT/sdroot/sys
		FLAGS="$FLAGS -I$ROOT/src/apps/letters -I$ROOT/src/apps/paint"
	fi
	mkdir -p "$O" "$A"
	cd "$S"
	: > "$O/make.mk"
	{
		echo 'all:'
		find . -name '*.c' -o -name '*.cpp' | sed 's#^\./##' | while read -r f; do
			o=$O/$(echo "$f" | sed 's/\.[a-z]*$//').o
			echo "all: $o"
			echo "$o: $f"
			echo "	@mkdir -p \$(dir \$@)"
			echo "	@echo $app : $f"
			case $f in
			*.cpp) echo "	${TC}g++ -std=gnu++20 -Wno-write-strings $FLAGS -c $f -o \$@" ;;
			*) echo "	${TC}gcc $FLAGS -c $f -o \$@" ;;
			esac
			echo "-include $(echo "$o" | sed 's/\.o$/.d/')"
		done
	} > "$O/make.mk"
	make -s -j"$JOBS" -f "$O/make.mk"
	LD=${TC}gcc
	[ -z "$(find . -name '*.cpp')" ] || LD=${TC}g++
	# (comme pibuild.mk : tous les relais de kits ; ld ne prend que ce qui est appele)
	LIBS=$(ls "$GCC"/lib/lib*.a | sed 's#.*/lib\(.*\)\.a#-l\1#' | tr '\n' ' ')
	$LD -mcpu=cortex-a72 -specs="$OUT/posix.specs" -Wl,--gc-sections $(find "$O" -name '*.o') -L"$GCC/lib" $LIBS $SSL -lm -o "$O/main"
	if [ "$app" = basic ]; then
		${TC}strip -o "$A/basic" "$O/main"
		ls -l "$A/basic"
		# (ce que #import lit : SD0:/lib/<kit>.bi, docs/BASIC.md)
		L=$OUT/sdroot/lib
		mkdir -p "$L"
		cp "$ROOT"/src/lib/basic/bi/*.bi "$L/"
		for k in audiokit netkit gpukit mediakit archivekit documentkit gpiokit printerkit; do python3 -I "$ROOT/tools/kits/abi2bas.py" "$ROOT/src/kits/$k/$k.abi" "$L"; done
		for k in uikit appkit; do python3 -I "$ROOT/tools/kits/abi2bas.py" "$ROOT/src/kits/$k/$k.abi" "$L" --head "$ROOT/src/lib/basic/bi/$k.head"; done
		continue
	fi
	if [ "$app" = printd ]; then
		${TC}strip -o "$A/printd" "$O/main"
		ls -l "$A/printd"
		continue
	fi
	${TC}strip -o "$A/main" "$O/main"
	cp app.txt "$A/"
	[ ! -d data ] || cp -r data/. "$A/"
	ls -l "$A/main"
done
