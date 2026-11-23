#!/bin/sh
# fbc : FreeBASIC 1.10.1 pour ARMPI (docs/GCC.md), dans /devtools/gcc, construit sur le PC, hors du selfhost,
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
#
# ARCH=x86_64 : fbc pour Korin sur PC (cible armpi-x86_64), dans build/pc/gccroot/gcc, apres
# ARCH=x86_64 sh tools/gcc/build.sh et ARCH=x86_64 sh tools/gcc/kits.sh ; travail dans
# ~/.cache/korin-fbc-x86_64.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCH=${ARCH:-aarch64}
case $ARCH in
aarch64)
	TARGET=aarch64-onyx-elf
	W=${WORK:-$HOME/.cache/armpi-fbc}
	OUT=$ROOT/build/gcc/sdroot/gcc
	PX=$ROOT/build/posix
	CPUFLAG=-mcpu=cortex-a72
	;;
x86_64)
	TARGET=x86_64-onyx-elf
	W=${WORK:-$HOME/.cache/korin-fbc-x86_64}
	OUT=$ROOT/build/pc/gccroot/gcc
	PX=$ROOT/build/pc/posix
	CPUFLAG=""
	;;
*) echo "fbc : ARCH=aarch64 ou x86_64" >&2; exit 1 ;;
esac
TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/$TARGET-14.2}
TC=$TOOL/bin/$TARGET-
JOBS=${JOBS:-$(nproc)}
STAGES=${STAGES:-all}
FB=FreeBASIC-1.10.1
TARBALL=$FB-source-bootstrap.tar.xz
SUM=2fae2c66b435240d4de11bb534843baa188574508a8a5bdaa5020bf729bf0a3e
URL=https://github.com/freebasic/fbc/releases/download/1.10.1/$TARBALL
EXTRA=$ROOT/src/extra/$FB
SPECS=$PX/lib/posix.specs
FBT=armpi-$ARCH
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
if [ "$ARCH" = x86_64 ]; then
	# (le PC : l'as de l'arbre est celui de x86_64-onyx-elf, et « -target armpi » vise x86_64)
	sed -i -e 's|"aarch64-onyx-elf/bin/as"|"x86_64-onyx-elf/bin/as"|' 		-e 's|(@"armpi", FB_COMPTARGET_ARMPI, FB_DEFAULT_CPUTYPE_AARCH64)|(@"armpi", FB_COMPTARGET_ARMPI, FB_DEFAULT_CPUTYPE_X86_64)|' 		"$W/merged/src/compiler/fbc.bas"
	grep -q '"x86_64-onyx-elf/bin/as"' "$W/merged/src/compiler/fbc.bas" || { echo "fbc : fbc.bas (as)" >&2; exit 1; }
fi
mkdir -p "$S"
rsync -rc --delete --exclude='obj/' --exclude='/bin/' --exclude='/include/' --exclude='/lib/freebasic/' --exclude='/bootstrap/fbc' --exclude='*.o' "$W/merged/" "$S/"
rm -rf "$W/merged"
mkdir -p "$S/include"
ln -sfn ../inc "$S/include/freebasic"		# (ou fbc cherche ses .bi, relatif a bin/fbc)

# le fbc du PC : la machine n'a pas les en-tetes de ncurses, seulement ses .so
HOSTINC=$W/hostinc
mkdir -p "$HOSTINC"
cp "$ROOT/tools/gcc/fbc/rtlib/armpi/termcap.h" "$HOSTINC/"
# (termcap est dans tinfo ; Debian et Ubuntu : /lib/x86_64-linux-gnu, Fedora : /usr/lib64)
TINFO=
for t in /lib/x86_64-linux-gnu/libtinfo.so.6 /usr/lib/x86_64-linux-gnu/libtinfo.so.6 /usr/lib64/libtinfo.so.6; do
	[ -f "$t" ] && { TINFO=$t; break; }
done
[ -n "$TINFO" ] || { echo "fbc : libtinfo.so.6 introuvable (paquet libtinfo6 ou ncurses-libs)" >&2; exit 1; }
ln -sf "$TINFO" "$W/hostlib/libncurses.so"
ln -sf "$TINFO" "$W/hostlib/libtinfo.so"
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
CCT="${TC}gcc $CPUFLAG -specs=$SPECS"
TCF="-O2 -g0 -fno-exceptions -fno-unwind-tables -fno-asynchronous-unwind-tables -D_GNU_SOURCE -DDISABLE_GPM -DDISABLE_FFI -DDISABLE_X11 -DDISABLE_OPENGL -DDISABLE_FBDEV -I$S/src/rtlib/armpi -I$OUT/include/kits"
if want libs; then
	say "rtlib et gfxlib2"
	make -C "$S" -k -j"$JOBS" TARGET=$FBT TARGET_OS=armpi TARGET_ARCH=$ARCH AR=${TC}ar CC="$CCT" FBC="$HOSTFBC" CFLAGS="$TCF" rtlib gfxlib2
fi

# fbc pour ARMPI : le compilateur traduit en C par le fbc du PC
T=$W/target
if want fbc; then
	say "fbc pour ARMPI"
	mkdir -p "$T"
	cd "$S/src/compiler"
	"$HOSTFBC" -i "$S/inc" -target $FBT -e -m fbc -r -d 'ENABLE_PREFIX="/devtools/gcc"' *.bas
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

# dans /devtools/gcc
if want stage; then
	say "dans $OUT"
	L=$S/lib/freebasic/$FBT
	cp "$T/fbc" "$OUT/bin/fbc"
	# (libposix : celle que rtlib attend, sans refaire tout l'arbre de GCC)
	cp "$PX/lib/libposix.a" "$OUT/$TARGET/armpi/"
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
