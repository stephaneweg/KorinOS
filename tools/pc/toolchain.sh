#!/bin/sh
# toolchain.sh : la chaîne croisée x86_64-onyx-elf (binutils 2.43, GCC 14.2 C et C++, newlib 4.4,
# libstdc++), pour les programmes C et C++ de Korin sur le PC (docs/etude/PC-PILOTES.md, « Chaîne
# x86-64 et apps C/C++ »). La même recette que aarch64-onyx-elf-14.2 d'Onyx
# (/opt/toolchains/aarch64-onyx-elf-14.2/share/onyx-toolchain/build-onyx-toolchain.sh) : seulement
# la cible change ; newlib et sa glue (libposix, src/lib/posix) font le reste.
#
#   sh tools/pc/toolchain.sh               # -> /opt/toolchains/x86_64-onyx-elf-14.2 (reprend où il en était)
#   STAGES="check" sh tools/pc/toolchain.sh
#
# Variables : PREFIX, WORK (sources, arbres, marques : $HOME/.cache/korin-x86-toolchain, côté Linux),
# JOBS, KEEP_BUILD=1 (garder les arbres), STAGES (« all », ou parmi fetch binutils gcc1 newlib
# overlay gcc final check).
#
# Étapes : sources (sommes SHA-256 vérifiées ; reprises de ~/.cache/armpi-gcc/dl si elles y sont) ->
# binutils -> GCC 1 (C seul, sans en-têtes) -> newlib -> en-têtes d'Onyx (pthread.h,
# sys/_pthreadtypes.h, semaphore.h, sys/dirent.h, sys/features.h, repris de la chaîne AArch64 :
# mêmes dispositions LP64) -> GCC final (C, C++, threads POSIX, TLS par FS) -> final -> check.
# Le seul changement des sources de GCC : patches/gcc-14.2.0-libstdcxx-onyx.patch (cible *-onyx-*).
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
TARGET=x86_64-onyx-elf
: "${PREFIX:=/opt/toolchains/x86_64-onyx-elf-14.2}"
: "${WORK:=$HOME/.cache/korin-x86-toolchain}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
: "${STAGES:=all}"
: "${KEEP_BUILD:=0}"
A64=/opt/toolchains/aarch64-onyx-elf-14.2	# les en-têtes d'Onyx viennent de là
REVISION=korin1

BINUTILS=binutils-2.43
GCC=gcc-14.2.0
NEWLIB=newlib-4.4.0.20231231
GMP=gmp-6.3.0
MPFR=mpfr-4.2.1
MPC=mpc-1.3.1
SOURCES="
https://ftp.gnu.org/gnu/binutils/$BINUTILS.tar.xz b53606f443ac8f01d1d5fc9c39497f2af322d99e14cea5c0b4b124d630379365
https://ftp.gnu.org/gnu/gcc/$GCC/$GCC.tar.xz a7b39bc69cbf9e25826c5a60ab26477001f7c08d85cec04bc0e29cabed6f3cc9
https://sourceware.org/pub/newlib/$NEWLIB.tar.gz 0c166a39e1bf0951dfafcd68949fe0e4b6d3658081d6282f39aeefc6310f2f13
https://ftp.gnu.org/gnu/gmp/$GMP.tar.xz a3c2b80201b89e68616f4ad30bc66aee4927c3ce50e33929ca819d5c43538898
https://ftp.gnu.org/gnu/mpfr/$MPFR.tar.xz 277807353a6726978996945af13e52829e3abd7a9a5b7fb2793894e18f1fcbb2
https://ftp.gnu.org/gnu/mpc/$MPC.tar.gz ab642492f5cf882b74aa0cb730cd410a81edcdbec895183ce930e706c1c759b8
"

COMMON_CONF="--target=$TARGET --prefix=$PREFIX --disable-nls --disable-multilib"
BINUTILS_CONF="--disable-werror --disable-gdb --disable-gdbserver --disable-sim --disable-gprofng --disable-readline"
NEWLIB_CONF="--enable-newlib-reent-thread-local --enable-newlib-retargetable-locking --enable-newlib-io-long-long --enable-newlib-io-c99-formats --disable-newlib-supplied-syscalls --enable-newlib-mb --enable-newlib-register-fini"
TARGET_CFLAGS="-O2 -g -ffunction-sections -fdata-sections"
# (pas de --with-cpu : x86-64 de base, comme le noyau et les kits du PC)
GCC_COMMON="--with-sysroot=$PREFIX/$TARGET --with-native-system-header-dir=/include --with-newlib --enable-tls --disable-shared --without-isl --disable-libssp --disable-libgomp --disable-libquadmath --disable-libsanitizer --disable-libvtv --disable-libatomic --disable-libitm --disable-libcc1 --with-pkgversion=Korin-toolchain-$REVISION"
GCC1_CONF="$GCC_COMMON --enable-languages=c --without-headers --disable-threads"
GCC2_CONF="$GCC_COMMON --enable-languages=c,c++ --enable-threads=posix --enable-libstdcxx-filesystem-ts --disable-libstdcxx-pch --enable-libstdcxx-backtrace=no"
# le compilateur de WSL (GCC 15, C23 par défaut) : GMP 6.3 et GCC 14 attendent C17
HOSTCC="gcc -std=gnu17"
HOSTCXX="g++ -std=gnu++17"

DL=$WORK/dl
SRC=$WORK/src
BLD=$WORK/build
STAMPS=$WORK/stamps
LOGS=$WORK/logs
mkdir -p "$DL" "$SRC" "$BLD" "$STAMPS" "$LOGS"
export PATH="$PREFIX/bin:$PATH"

say () { printf '\n=== %s ===\n' "$*"; }
want () { case " $STAGES " in *" all "*|*" $1 "*) return 0;; esac; return 1; }
done_ () { [ -f "$STAMPS/$1" ]; }
mark () { date > "$STAMPS/$1"; }
run () {
	_log=$LOGS/$1.log; shift
	if ! "$@" >>"$_log" 2>&1; then
		echo "ECHEC : $* (journal : $_log)" >&2
		tail -n 40 "$_log" >&2
		exit 1
	fi
}
sha256 () { sha256sum "$1" | cut -d' ' -f1; }

mkdir -p "$PREFIX" 2>/dev/null && [ -w "$PREFIX" ] || { echo "toolchain : $PREFIX n'est pas inscriptible" >&2; exit 1; }
T0=$(date +%s)

if want fetch && ! done_ fetch; then
	say "sources"
	echo "$SOURCES" | while read -r url sum; do
		[ -n "$url" ] || continue
		b=$(basename "$url")
		f=$DL/$b
		if ! { [ -f "$f" ] && [ "$(sha256 "$f")" = "$sum" ]; }; then
			c=$HOME/.cache/armpi-gcc/dl/$b
			if [ -f "$c" ] && [ "$(sha256 "$c")" = "$sum" ]; then cp "$c" "$f"; else curl -fL --retry 3 -o "$f.part" "$url" && mv "$f.part" "$f"; fi
		fi
		[ "$(sha256 "$f")" = "$sum" ] || { echo "sha256 faux : $f" >&2; exit 1; }
		echo "ok  $b"
	done
	for p in $BINUTILS $GCC $NEWLIB $GMP $MPFR $MPC; do
		rm -rf "${SRC:?}/$p"
		tar -C "$SRC" -xf "$DL"/$p.tar.*
	done
	ln -s ../$GMP "$SRC/$GCC/gmp"
	ln -s ../$MPFR "$SRC/$GCC/mpfr"
	ln -s ../$MPC "$SRC/$GCC/mpc"
	(cd "$SRC/$GCC" && patch -p1 < "$HERE/patches/gcc-14.2.0-libstdcxx-onyx.patch")
	# newlib : x86_64-onyx-elf comme aarch64 (libc/syscalls : close () -> _close_r -> _close, que
	# fournit libposix), pas le cas par défaut (-DMISSING_SYSCALL_NAMES : _close_r -> close)
	sed -i 's/^  aarch64\*-\*-\*)$/  x86_64-onyx-*)\n\tsyscall_dir=syscalls\n\t;;\n&/' "$SRC/$NEWLIB/newlib/configure.host"
	grep -q '^  x86_64-onyx-\*)$' "$SRC/$NEWLIB/newlib/configure.host" || { echo "newlib : configure.host" >&2; exit 1; }
	mark fetch
fi

if want binutils && ! done_ binutils; then
	say "binutils"
	rm -rf "$BLD/binutils" && mkdir -p "$BLD/binutils" && cd "$BLD/binutils"
	run binutils env CC="$HOSTCC" CXX="$HOSTCXX" "$SRC/$BINUTILS/configure" $COMMON_CONF $BINUTILS_CONF
	run binutils make -j"$JOBS" MAKEINFO=true
	run binutils make install-strip MAKEINFO=true
	mark binutils
fi

if want gcc1 && ! done_ gcc1; then
	say "GCC 1 (C, sans en-têtes)"
	rm -rf "$BLD/gcc1" && mkdir -p "$BLD/gcc1" && cd "$BLD/gcc1"
	run gcc1 env CC="$HOSTCC" CXX="$HOSTCXX" "$SRC/$GCC/configure" $COMMON_CONF $GCC1_CONF
	run gcc1 make -j"$JOBS" all-gcc all-target-libgcc MAKEINFO=true
	run gcc1 make install-gcc install-target-libgcc MAKEINFO=true
	mark gcc1
fi

if want newlib && ! done_ newlib; then
	say "newlib"
	rm -rf "$BLD/newlib" && mkdir -p "$BLD/newlib" && cd "$BLD/newlib"
	run newlib env CC="$HOSTCC" CFLAGS_FOR_TARGET="$TARGET_CFLAGS" "$SRC/$NEWLIB/configure" $COMMON_CONF $NEWLIB_CONF
	run newlib make -j"$JOBS" all-target-newlib MAKEINFO=true
	run newlib make install-target-newlib MAKEINFO=true
	rm -f "$STAMPS/overlay"
	mark newlib
fi

# les en-têtes d'Onyx (threads, sémaphores, dirent, features) : ceux de la chaîne AArch64, qui ont
# servi à libstdc++ et à libposix ; LP64 des deux côtés, aucun n'en dépend
INC=$PREFIX/$TARGET/include
if want overlay && ! done_ overlay; then
	say "en-têtes d'Onyx -> $INC"
	[ -f "$INC/sys/_newlib_features.h" ] || mv "$INC/sys/features.h" "$INC/sys/_newlib_features.h"
	for h in pthread.h sys/_pthreadtypes.h semaphore.h sys/dirent.h sys/features.h; do
		cp "$A64/aarch64-onyx-elf/include/$h" "$INC/$h"
	done
	grep -q '_newlib_features.h' "$INC/sys/features.h" || { echo "overlay : sys/features.h" >&2; exit 1; }
	mark overlay
fi

if want gcc && ! done_ gcc; then
	say "GCC final (C, C++, threads POSIX, TLS)"
	rm -rf "$BLD/gcc" && mkdir -p "$BLD/gcc" && cd "$BLD/gcc"
	run gcc env CC="$HOSTCC" CXX="$HOSTCXX" CFLAGS_FOR_TARGET="$TARGET_CFLAGS" CXXFLAGS_FOR_TARGET="$TARGET_CFLAGS" \
		"$SRC/$GCC/configure" $COMMON_CONF $GCC2_CONF
	run gcc make -j"$JOBS" MAKEINFO=true
	run gcc make install-strip MAKEINFO=true
	mark gcc
fi

if want final && ! done_ final; then
	say "final"
	{
		echo "x86_64-onyx-elf toolchain, revision $REVISION (Korin, tools/pc/toolchain.sh)"
		echo "built $(date -u +%Y-%m-%dT%H:%MZ) on $(uname -sm)"
		echo "sources:"
		echo "$SOURCES" | while read -r url sum; do if [ -n "$url" ]; then echo "  $url  sha256 $sum"; fi; done
		echo "patch: gcc-14.2.0-libstdcxx-onyx.patch"
		echo "binutils: $COMMON_CONF $BINUTILS_CONF"
		echo "newlib:   $COMMON_CONF $NEWLIB_CONF (CFLAGS_FOR_TARGET=$TARGET_CFLAGS)"
		echo "gcc:      $COMMON_CONF $GCC2_CONF"
		echo "overlay:  pthread.h sys/_pthreadtypes.h semaphore.h sys/dirent.h sys/features.h (de $A64)"
	} > "$PREFIX/KORIN-TOOLCHAIN.txt"
	mkdir -p "$PREFIX/share/korin-toolchain"
	cp "$HERE/patches/gcc-14.2.0-libstdcxx-onyx.patch" "$HERE/toolchain.sh" "$PREFIX/share/korin-toolchain/"
	[ "$KEEP_BUILD" = 1 ] || { rm -rf "${BLD:?}"/* "${SRC:?}"/*; rm -f "$STAMPS/fetch"; }
	mark final
fi

if want check; then
	say "vérifications"
	fail=0
	"$PREFIX/bin/$TARGET-gcc" -v 2>&1 | grep -q '^Thread model: posix' && echo "ok    Thread model: posix" || { echo "ECHEC Thread model"; fail=1; }
	CXXCONFIG=$(ls "$PREFIX/$TARGET"/include/c++/*/"$TARGET"/bits/c++config.h 2>/dev/null | head -n 1)
	for m in _GLIBCXX_HAS_GTHREADS _GLIBCXX_USE_CLOCK_MONOTONIC _GLIBCXX_USE_NANOSLEEP _GLIBCXX_USE_SCHED_YIELD \
		 _GLIBCXX_USE_PTHREAD_COND_CLOCKWAIT _GLIBCXX_HAVE_TLS _GLIBCXX_USE_CLOCK_REALTIME _GLIBCXX_HAVE_DIRENT_H; do
		if [ -n "$CXXCONFIG" ] && grep -q "^#define $m 1" "$CXXCONFIG"; then echo "ok    $m"; else echo "ECHEC $m"; fail=1; fi
	done
	grep -q '_REENT_THREAD_LOCAL\|_WANT_REENT_THREAD_LOCAL' "$PREFIX/$TARGET/include/newlib.h" && echo "ok    newlib : _REENT_THREAD_LOCAL" || { echo "ECHEC newlib sans _REENT_THREAD_LOCAL"; fail=1; }
	tmp=$(mktemp -d)
	printf '__thread int v; int f (void) { return v; }\n' > "$tmp/t.c"
	"$PREFIX/bin/$TARGET-gcc" -O2 -S -o "$tmp/t.s" "$tmp/t.c"
	grep -q '%fs:' "$tmp/t.s" && ! grep -q emutls "$tmp/t.s" && echo "ok    TLS natif (%fs)" || { echo "ECHEC TLS émulé"; fail=1; }
	printf '#include <limits.h>\n#include <pthread.h>\nint a[PATH_MAX > 0 && _POSIX_THREADS > 0 ? 1 : -1]; int f (pthread_mutex_t *m) { return pthread_mutex_clocklock (m, CLOCK_MONOTONIC, 0); }\n' > "$tmp/h.c"
	"$PREFIX/bin/$TARGET-gcc" -c -o "$tmp/h.o" "$tmp/h.c" && echo "ok    <limits.h>, <pthread.h>" || { echo "ECHEC <limits.h> / <pthread.h>"; fail=1; }
	rm -rf "$tmp"
	echo "installé : $PREFIX ($(du -sh "$PREFIX" 2>/dev/null | cut -f1)) ; $(( ($(date +%s) - T0) / 60 )) min"
	[ $fail = 0 ] && echo "RECETTE : OK" || { echo "RECETTE : ECHEC"; exit 1; }
fi
