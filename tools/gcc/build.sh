#!/bin/sh
# gcc : GCC 14.2 et binutils 2.43 qui tournent SOUS ARMPI (docs/GCC.md). Compilation croisee
# Â« canadienne Â» : construits sur le PC par la chaine d'Onyx (aarch64-onyx-elf-14.2), lies a
# libposix ; les bibliotheques cibles (newlib, libstdc++, libgcc) et les en-tetes viennent tels
# quels de cette chaine. Hors du selfhost (comme WebKit). A lancer dans WSL depuis la racine du
# depot, apres sh tools/posix/build.sh :
#     sh tools/gcc/build.sh              # tout (reprend ou il en etait)
#     STAGES="stage" sh tools/gcc/build.sh     # seulement l'arbre final
#     RELINK=1 sh tools/gcc/build.sh     # apres un changement de libposix : refait les liens
#
# Produit build/gcc/sdroot/gcc/ : copie sur la carte en SD0:/gcc, vu des programmes comme /gcc
# (SD0:/etc/paths.txt : docs/FICHIERS.md, Â« Chemins virtuels Â»).
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
WORK=${WORK:-$HOME/.cache/armpi-gcc}
JOBS=${JOBS:-$(nproc)}
STAGES=${STAGES:-all}
RELINK=${RELINK:-0}
TARGET=aarch64-onyx-elf
PREFIX=/gcc				# le chemin vu sous ARMPI
OUT=$ROOT/build/gcc/sdroot/gcc
SPECS=$ROOT/build/posix/lib/posix.specs

BINUTILS=binutils-2.43
GCC=gcc-14.2.0
GMP=gmp-6.3.0
MPFR=mpfr-4.2.1
MPC=mpc-1.3.1
SOURCES="
https://ftp.gnu.org/gnu/binutils/$BINUTILS.tar.xz b53606f443ac8f01d1d5fc9c39497f2af322d99e14cea5c0b4b124d630379365
https://ftp.gnu.org/gnu/gcc/$GCC/$GCC.tar.xz a7b39bc69cbf9e25826c5a60ab26477001f7c08d85cec04bc0e29cabed6f3cc9
https://ftp.gnu.org/gnu/gmp/$GMP.tar.xz a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
https://ftp.gnu.org/gnu/mpfr/$MPFR.tar.xz 277807353a6726978996945af13e52829e3abd7a9a5b7fb2793894e18f1fcbb2
https://ftp.gnu.org/gnu/mpc/$MPC.tar.gz ab642492f5cf882b74aa0cb730cd410a81edcdbec895183ce930e706c1c759b8
"
DL=$WORK/dl
SRC=$WORK/src
BLD=$WORK/build
STAMPS=$WORK/stamps
LOGS=$WORK/logs
INST=$WORK/inst
mkdir -p "$DL" "$SRC" "$BLD" "$STAMPS" "$LOGS"
export PATH="$TOOL/bin:$PATH"

want () { case " $STAGES " in *" all "*|*" $1 "*) return 0;; esac; return 1; }
done_ () { [ -f "$STAMPS/$1" ]; }
mark () { date > "$STAMPS/$1"; }
say () { printf '\n=== %s ===\n' "$*"; }
run () {
	_log=$LOGS/$1.log; shift
	if ! "$@" >>"$_log" 2>&1; then
		echo "ECHEC : $* (journal : $_log)" >&2
		tail -n 40 "$_log" >&2
		exit 1
	fi
}

[ -f "$SPECS" ] || { echo "gcc : lancer d'abord sh tools/posix/build.sh" >&2; exit 1; }

# le compilateur de la machine hote (ARMPI) : la chaine d'Onyx + libposix
# (des enveloppes : les drapeaux de l'ARM une seule fois, jamais chez les outils de generation
# du PC, a qui le Makefile de GCC transmet CXXFLAGS)
HOSTFLAGS="-O2 -g0"
WRAPFLAGS="-mcpu=cortex-a72 -D_GNU_SOURCE -DFD_SETSIZE=1024 -specs=$SPECS"
export CC_FOR_BUILD=gcc CXX_FOR_BUILD=g++ CFLAGS_FOR_BUILD=-O2 CXXFLAGS_FOR_BUILD=-O2
WRAP=$WORK/bin
mkdir -p "$WRAP"
for t in gcc g++; do
	printf '#!/bin/sh\nexec %s%s %s "$@"\n' "$TC" "$t" "$WRAPFLAGS" > "$WRAP/armpi-$t"
	chmod +x "$WRAP/armpi-$t"
done
HOSTENV="CC=$WRAP/armpi-gcc CXX=$WRAP/armpi-g++ AR=${TC}ar RANLIB=${TC}ranlib NM=${TC}nm AS=${TC}as LD=${TC}ld STRIP=${TC}strip OBJCOPY=${TC}objcopy OBJDUMP=${TC}objdump READELF=${TC}readelf"

# les reponses que configure ne peut pas trouver seul (pas de fork, pas d'execution)
export ac_cv_func_fork=no ac_cv_func_vfork=no ac_cv_func_fork_works=no ac_cv_func_vfork_works=no
export ac_cv_func_posix_spawn=yes ac_cv_func_posix_spawnp=yes
export gcc_cv_as_gnu_unique_object=no

COMMON="--build=x86_64-pc-linux-gnu --host=$TARGET --target=$TARGET --prefix=$PREFIX --disable-nls --disable-multilib"
COMMON="$COMMON --disable-shared --enable-static --without-zstd --without-debuginfod --disable-plugin --disable-lto"

if want fetch && ! done_ fetch; then
	say "sources"
	echo "$SOURCES" | while read -r url sum; do
		[ -n "$url" ] || continue
		f=$DL/$(basename "$url")
		[ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$sum" ] || curl -fL --retry 3 -o "$f" "$url"
		[ "$(sha256sum "$f" | cut -d' ' -f1)" = "$sum" ] || { echo "sha256 faux : $f" >&2; exit 1; }
		echo "ok  $(basename "$f")"
	done
	for p in $BINUTILS $GCC $GMP $MPFR $MPC; do
		rm -rf "${SRC:?}/$p"
		tar -C "$SRC" -xf "$DL"/$p.tar.*
	done
	ln -s ../$GMP "$SRC/$GCC/gmp"
	ln -s ../$MPFR "$SRC/$GCC/mpfr"
	ln -s ../$MPC "$SRC/$GCC/mpc"
	for p in "$ROOT"/tools/gcc/patches/*.patch; do
		[ -f "$p" ] || continue
		case $(basename "$p") in
		gcc-*)      (cd "$SRC/$GCC" && patch -p1 < "$p") ;;
		binutils-*) (cd "$SRC/$BINUTILS" && patch -p1 < "$p") ;;
		esac
	done
	mark fetch
fi

# ARMPI n'a pas de mode binaire : O_BINARY de newlib est sans effet, comme sous BeOS
for t in "$SRC/$BINUTILS" "$SRC/$GCC"; do
	[ -f "$t/include/binary-io.h" ] && sed -i 's/^#ifdef __BEOS__$/#if defined __BEOS__ || defined __armpi__/' "$t/include/binary-io.h"
done

# modules C++20 : lecture et ecriture ordinaires (pas de MAP_SHARED en ecriture sous ARMPI)
[ -f "$SRC/$GCC/gcc/cp/module.cc" ] && sed -i 's|^#if 0 // 1 for testing no mmap$|#if 1 // ARMPI : pas de mmap|' "$SRC/$GCC/gcc/cp/module.cc"

# -mcpu=native : seulement sous Linux (/proc/cpuinfo) ; GCC le suppose pour tout hote AArch64
[ -f "$SRC/$GCC/gcc/config/aarch64/aarch64.h" ] && sed -i '/^#if defined(__aarch64__)$/{N;s/^#if defined(__aarch64__)\(\nextern const char \*host_detect_local_cpu\)/#if defined(__aarch64__) \&\& !defined(__armpi__)\1/}' "$SRC/$GCC/gcc/config/aarch64/aarch64.h"

if [ "$RELINK" = 1 ]; then
	# (libposix a change : les bibliotheques des outils restent, seuls les executables se refont)
	find "$BLD/gcc/gcc" -maxdepth 1 -type f \( -name cc1 -o -name cc1plus -o -name xgcc -o -name xg++ -o -name collect2 -o -name cpp \) -delete 2>/dev/null || true
	rm -f "$BLD"/binutils/gas/as-new "$BLD"/binutils/ld/ld-new "$BLD"/binutils/binutils/ar "$BLD"/binutils/binutils/ranlib "$BLD"/binutils/binutils/nm-new "$BLD"/binutils/binutils/objcopy "$BLD"/binutils/binutils/strip-new "$BLD"/binutils/binutils/objdump "$BLD"/binutils/binutils/readelf "$BLD"/binutils/binutils/size "$BLD"/binutils/binutils/strings
	rm -f "$STAMPS/binutils.make" "$STAMPS/gcc.make" "$STAMPS/stage"
fi

if want binutils && ! done_ binutils.conf; then
	say "binutils : configure"
	rm -rf "$BLD/binutils" && mkdir -p "$BLD/binutils" && cd "$BLD/binutils"
	: > "$LOGS/binutils.log"
	run binutils env $HOSTENV CFLAGS="$HOSTFLAGS" CXXFLAGS="$HOSTFLAGS" \
		"$SRC/$BINUTILS/configure" $COMMON --with-sysroot=$PREFIX/$TARGET \
		--disable-werror --disable-gdb --disable-gdbserver --disable-sim --disable-gprofng --disable-readline --disable-libdecnumber --enable-gold=no --disable-plugins
	mark binutils.conf
fi
if want binutils && ! done_ binutils.make; then
	say "binutils : make"
	cd "$BLD/binutils"
	run binutils make -j"$JOBS" MAKEINFO=true all-binutils all-gas all-ld
	mark binutils.make
fi

if want gcc && ! done_ gcc.conf; then
	say "gcc : configure"
	rm -rf "$BLD/gcc" && mkdir -p "$BLD/gcc" && cd "$BLD/gcc"
	: > "$LOGS/gcc.log"
	run gcc env $HOSTENV CFLAGS="$HOSTFLAGS" CXXFLAGS="$HOSTFLAGS" \
		CC_FOR_TARGET=${TC}gcc CXX_FOR_TARGET=${TC}g++ GCC_FOR_TARGET=${TC}gcc \
		AR_FOR_TARGET=${TC}ar AS_FOR_TARGET=${TC}as LD_FOR_TARGET=${TC}ld NM_FOR_TARGET=${TC}nm \
		RANLIB_FOR_TARGET=${TC}ranlib STRIP_FOR_TARGET=${TC}strip OBJDUMP_FOR_TARGET=${TC}objdump \
		READELF_FOR_TARGET=${TC}readelf OBJCOPY_FOR_TARGET=${TC}objcopy \
		"$SRC/$GCC/configure" $COMMON --enable-languages=c,c++ --disable-bootstrap \
		--with-sysroot=$PREFIX/$TARGET --with-build-sysroot="$TOOL/$TARGET" \
		--with-native-system-header-dir=/include --with-cpu=cortex-a72 --with-newlib --enable-tls \
		--enable-threads=posix --without-isl --disable-libcc1 --disable-libssp --disable-libgomp \
		--disable-libquadmath --disable-libsanitizer --disable-libvtv --disable-libatomic --disable-libitm \
		--disable-libstdcxx-pch --enable-libstdcxx-backtrace=no --with-pkgversion=ARMPI-gcc
	mark gcc.conf
fi
if want gcc && ! done_ gcc.make; then
	say "gcc : make"
	cd "$BLD/gcc"
	# (-k : gcov-tool veut ftw et nftw, absents de libposix ; il ne sert pas ici)
	make -k -j"$JOBS" MAKEINFO=true all-gcc >>"$LOGS/gcc.log" 2>&1 || true
	for f in cc1 cc1plus collect2 xgcc xg++ cpp; do
		[ -x "$BLD/gcc/gcc/$f" ] || { echo "ECHEC : $f absent (journal : $LOGS/gcc.log)" >&2; tail -n 30 "$LOGS/gcc.log" >&2; exit 1; }
	done
	mark gcc.make
fi

# l'arbre final : les binaires construits ici + les en-tetes et bibliotheques de la chaine d'Onyx
if want stage && ! done_ stage; then
	say "l'arbre final : $OUT"
	V=14.2.0
	rm -rf "$OUT" && mkdir -p "$OUT/bin" "$OUT/lib/gcc/$TARGET/$V" "$OUT/libexec/gcc/$TARGET/$V" "$OUT/$TARGET/bin"
	cp -r "$TOOL/$TARGET/include" "$OUT/$TARGET/"
	mkdir -p "$OUT/include"				# (ou cc1plus les cherche : $PREFIX/include/c++)
	mv "$OUT/$TARGET/include/c++" "$OUT/include/c++"
	cp -r "$TOOL/$TARGET/lib" "$OUT/$TARGET/"
	rm -f "$OUT/$TARGET"/lib/*.la "$OUT/$TARGET"/lib/*-gdb.py "$OUT/$TARGET"/lib/libg.a
	cp -r "$TOOL/lib/gcc/$TARGET/$V/." "$OUT/lib/gcc/$TARGET/$V/"
	rm -rf "$OUT/lib/gcc/$TARGET/$V/plugin" "$OUT/lib/gcc/$TARGET/$V/install-tools"
	# (sans leur debogage : l'edition de liens lit et recopie moins sur le Pi, libc.a passe de 6,8 a 1,5 Mo)
	for f in "$OUT/$TARGET/lib/libc.a" "$OUT/$TARGET/lib/libm.a" "$OUT/lib/gcc/$TARGET/$V/libgcc.a"; do ${TC}strip --strip-debug "$f"; done
	G=$BLD/gcc/gcc
	B=$BLD/binutils
	cp "$G/cc1" "$G/cc1plus" "$G/collect2" "$OUT/libexec/gcc/$TARGET/$V/"
	cp "$G/xgcc" "$OUT/bin/gcc"
	cp "$G/xg++" "$OUT/bin/g++"
	cp "$G/cpp" "$OUT/bin/cpp"
	cp "$B/gas/as-new" "$OUT/$TARGET/bin/as"
	cp "$B/ld/ld-new" "$OUT/$TARGET/bin/ld"
	cp "$B/binutils/ar" "$B/binutils/ranlib" "$B/binutils/objcopy" "$B/binutils/objdump" "$B/binutils/readelf" "$B/binutils/size" "$OUT/bin/"
	cp "$B/binutils/nm-new" "$OUT/bin/nm"
	cp "$B/binutils/strip-new" "$OUT/bin/strip"
	for f in "$OUT"/bin/* "$OUT/$TARGET"/bin/* "$OUT/libexec/gcc/$TARGET/$V"/*; do ${TC}strip "$f"; done
	mkdir -p "$OUT/test"
	cp -r "$ROOT"/tools/gcc/test/* "$ROOT"/src/lib/posix/test/posixtest-cxx.cpp "$OUT/test/"
	du -sh "$OUT"
	mark stage
fi

# libposix et ses en-tetes, puis les specs : gcc hello.c suffit, sans -specs. A chaque lancement
# (sans marque), et seul par STAGES="posix" : apres un changement de libposix ou de posix.specs,
# sans refaire l'arbre (les kits et FreeBASIC de /gcc restent). Avec dlopen (docs/POSIX.md) :
# dlmod.o pour gcc -shared, dlhost.ld et dlhost-cxx.ld pour gcc -rdynamic.
if { want stage || want posix; } && [ -d "$OUT/lib/gcc/$TARGET" ]; then
	say "libposix et specs : $OUT"
	V=14.2.0
	L=$OUT/$TARGET/armpi
	rm -rf "$L"
	mkdir -p "$L"
	for f in libposix.a crt0posix.o posix.ld dlmod.o dlhost.ld dlhost-cxx.ld libdl.a libpthread.a librt.a libutil.a; do
		[ -f "$ROOT/build/posix/lib/$f" ] && cp "$ROOT/build/posix/lib/$f" "$L/"
	done
	cp -r "$ROOT"/build/posix/include "$L/include"
	# (-L$PREFIX/lib : les kits depuis le C, tools/gcc/kits.sh)
	sed -e "s|$ROOT/build/posix/lib|$PREFIX/$TARGET/armpi|g" -e "s|$ROOT/build/posix/include|$PREFIX/$TARGET/armpi/include|g" -e "s|^-T |-L$PREFIX/lib -T |" -e "s|-L $PREFIX/$TARGET/armpi|-L$PREFIX/lib -L $PREFIX/$TARGET/armpi|" "$SPECS" > "$WORK/armpi.specs"
	${TC}gcc -dumpspecs > "$WORK/dump.specs"
	awk -f "$ROOT/tools/gcc/mkspecs.awk" "$WORK/armpi.specs" "$WORK/dump.specs" > "$OUT/lib/gcc/$TARGET/$V/specs"
fi
say "fini"
ls -la "$OUT/bin" 2>/dev/null | head -20 || true
