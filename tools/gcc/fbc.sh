#!/bin/sh
# fbc : FreeBASIC 1.10.1 pour ARMPI (docs/GCC.md), dans /gcc, construit sur le PC, hors du selfhost,
# comme GCC. fbc genere du C (-gen gcc) que le GCC d'ARMPI compile. A lancer dans WSL depuis la
# racine du depot, apres tools/gcc/build.sh et tools/gcc/kits.sh :
#     sh tools/gcc/fbc.sh                 # tout (reprend ou il en etait)
#     STAGES="libs stage" sh tools/gcc/fbc.sh    # apres un changement de rtlib ou de gfxlib2
#
# Sources officielles : src/extra/FreeBASIC-1.10.1 (paquet « bootstrap », pas dans le depot). Ce qui
# change pour ARMPI est ici : tools/gcc/fbc/armpi.patch (compilateur, makefile, en-tetes),
# tools/gcc/fbc/{rtlib,gfxlib2,inc} (fichiers nouveaux, poses par-dessus).
#
# Amorce : fbc du PC depuis le C du paquet, puis ce fbc recompile le compilateur modifie, qui sait
# viser armpi-aarch64 ; il traduit alors le compilateur en C pour ARMPI, compile ici par la chaine
# d'Onyx. Produit build/gcc/sdroot/gcc/{bin/fbc,include/freebasic,lib/freebasic/armpi-aarch64}.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
W=${WORK:-$HOME/.cache/armpi-fbc}
JOBS=${JOBS:-$(nproc)}
STAGES=${STAGES:-all}
FB=FreeBASIC-1.10.1
TARBALL=$FB-source-bootstrap.tar.xz
SUM=2fae2c66b435240d4de11bb534843baa188574508a8a5bdaa5020bf729bf0a3e
URL=https://github.com/freebasic/fbc/releases/download/1.10.1/$TARBALL
EXTRA=$ROOT/src/extra/$FB
OUT=$ROOT/build/gcc/sdroot/gcc
SPECS=$ROOT/build/posix/lib/posix.specs
FBT=armpi-aarch64
S=$W/src
mkdir -p "$W/dl" "$W/stamps" "$W/hostlib"
want () { case " $STAGES " in *" all "*|*" $1 "*) return 0;; esac; return 1; }
done_ () { [ -f "$W/stamps/$1" ]; }
mark () { date > "$W/stamps/$1"; }
say () { printf '\n=== %s ===\n' "$*"; }
[ -f "$SPECS" ] || { echo "fbc : lancer d'abord sh tools/posix/build.sh" >&2; exit 1; }
[ -d "$OUT/bin" ] || { echo "fbc : lancer d'abord sh tools/gcc/build.sh" >&2; exit 1; }

# les sources : le paquet officiel, verifie, puis patch et fichiers d'ARMPI ; seul ce qui a change
# est retouche (rsync -c : make ne refait que cela)
if [ ! -d "$EXTRA" ]; then
	f=$W/dl/$TARBALL
	[ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$SUM" ] || curl -fL --retry 3 -o "$f" "$URL"
	[ "$(sha256sum "$f" | cut -d' ' -f1)" = "$SUM" ] || { echo "sha256 faux : $f" >&2; exit 1; }
	mkdir -p "$ROOT/src/extra"
	tar -C "$ROOT/src/extra" -xf "$f"
	mv "$ROOT/src/extra/$FB-source-bootstrap" "$EXTRA"
fi
if [ ! -d "$W/pristine" ]; then
	rsync -a --exclude='/bootstrap/' "$EXTRA/" "$W/pristine/"
	mkdir -p "$W/pristine/bootstrap"
	rsync -a "$EXTRA/bootstrap/linux-x86_64" "$W/pristine/bootstrap/"
fi
rm -rf "$W/merged"
cp -r "$W/pristine" "$W/merged"
(cd "$W/merged" && patch -s -p1 < "$ROOT/tools/gcc/fbc/armpi.patch")
cp -r "$ROOT/tools/gcc/fbc/rtlib/." "$W/merged/src/rtlib/"
cp -r "$ROOT/tools/gcc/fbc/gfxlib2/." "$W/merged/src/gfxlib2/"
cp -r "$ROOT/tools/gcc/fbc/inc/." "$W/merged/inc/"
mkdir -p "$S"
rsync -rc --delete --exclude='obj/' --exclude='/bin/' --exclude='/include/' --exclude='/lib/freebasic/' --exclude='/bootstrap/fbc' --exclude='*.o' "$W/merged/" "$S/"
rm -rf "$W/merged"
mkdir -p "$S/include"
ln -sfn ../inc "$S/include/freebasic"		# (ou fbc cherche ses .bi, relatif a bin/fbc)

# le fbc du PC : la machine n'a pas les en-tetes de ncurses, seulement ses .so
HOSTINC=$W/hostinc
mkdir -p "$HOSTINC"
cp "$ROOT/tools/gcc/fbc/rtlib/armpi/termcap.h" "$HOSTINC/"
ln -sf /lib/x86_64-linux-gnu/libtinfo.so.6 "$W/hostlib/libncurses.so"	# (termcap est dans tinfo)
ln -sf /lib/x86_64-linux-gnu/libtinfo.so.6 "$W/hostlib/libtinfo.so"
HOSTCC="gcc -L$W/hostlib -I$HOSTINC"
HOSTCF="-O2 -DDISABLE_GPM -DDISABLE_FFI -DDISABLE_X11 -DDISABLE_OPENGL -DDISABLE_FBDEV"
if want host && ! done_ host0; then
	say "fbc du PC : amorce"
	make -C "$S" -j"$JOBS" CC="$HOSTCC" CFLAGS="$HOSTCF" bootstrap-minimal
	mark host0
fi
if want host; then
	say "fbc du PC : compilateur modifie"
	make -C "$S" -j"$JOBS" CC="$HOSTCC" CFLAGS="$HOSTCF" FBC="$S/bin/fbc" FBLFLAGS="-p $W/hostlib" compiler
fi
HOSTFBC=$S/bin/fbc

# rtlib et gfxlib2 pour ARMPI
CCT="${TC}gcc -mcpu=cortex-a72 -specs=$SPECS"
TCF="-O2 -g0 -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -D_GNU_SOURCE -DDISABLE_GPM -DDISABLE_FFI -DDISABLE_X11 -DDISABLE_OPENGL -DDISABLE_FBDEV -I$S/src/rtlib/armpi -I$OUT/include/kits"
if want libs; then
	say "rtlib et gfxlib2"
	make -C "$S" -k -j"$JOBS" TARGET=$FBT TARGET_OS=armpi TARGET_ARCH=aarch64 AR=${TC}ar CC="$CCT" FBC="$HOSTFBC" CFLAGS="$TCF" rtlib gfxlib2
fi

# fbc pour ARMPI : le compilateur traduit en C par le fbc du PC
T=$W/target
if want fbc; then
	say "fbc pour ARMPI"
	mkdir -p "$T"
	cd "$S/src/compiler"
	"$HOSTFBC" -i "$S/inc" -target $FBT -e -m fbc -r -d 'ENABLE_PREFIX="/gcc"' *.bas
	for f in *.c; do
		if ! cmp -s "$f" "$T/$f"; then mv "$f" "$T/$f"; else rm -f "$f"; fi
	done
	cd "$T"
	ls *.c | sed 's/\.c$//' | xargs -P "$JOBS" -I@ sh -c \
		"[ @.o -nt @.c ] || $CCT -O2 -g0 -nostdinc -fno-strict-aliasing -frounding-math -fwrapv -fno-math-errno -w -c @.c -o @.o"
	L=$S/lib/freebasic/$FBT
	$CCT -o fbc.elf "$L/fbrt0.o" *.o "$L/libfb.a" -lm
	${TC}strip fbc.elf -o fbc
	ls -l fbc
fi

# dans /gcc
if want stage; then
	say "dans $OUT"
	L=$S/lib/freebasic/$FBT
	cp "$T/fbc" "$OUT/bin/fbc"
	# (libposix : celle que rtlib attend, sans refaire tout l'arbre de GCC)
	cp "$ROOT/build/posix/lib/libposix.a" "$OUT/aarch64-onyx-elf/armpi/"
	# (include/freebasic/kits vient de tools/gcc/kits.sh)
	rm -rf "$OUT/lib/freebasic" "$OUT/include/freebasic/crt"
	mkdir -p "$OUT/lib/freebasic/$FBT" "$OUT/include/freebasic" "$OUT/share/freebasic"
	cp "$L"/*.a "$L"/*.o "$OUT/lib/freebasic/$FBT/"
	cp "$S"/inc/*.bi "$OUT/include/freebasic/"
	cp -r "$S/inc/crt" "$OUT/include/freebasic/"
	rm -rf "$OUT/include/freebasic/crt/win32" "$OUT/include/freebasic/crt/dos" "$OUT/include/freebasic/crt/sys/win32" "$OUT/include/freebasic/crt/sys/dos"
	cp "$S/src/compiler/license.txt" "$OUT/share/freebasic/compiler-license.txt"
	cp "$S/src/rtlib/license.txt" "$OUT/share/freebasic/rtlib-license.txt"
	mkdir -p "$OUT/test/basic"
	cp -r "$ROOT"/tools/gcc/test/basic/* "$OUT/test/basic/"
	du -sh "$OUT/lib/freebasic" "$OUT/include/freebasic"
fi
say fini
