#!/bin/sh
# python : CPython 3.14 pour Korin (docs/PYTHON.md, docs/PYTHON-PORT.md), sur newlib + libposix, par
# la chaine d'Onyx. Hors du selfhost (comme les autres ports C). A lancer dans WSL depuis la racine
# du depot, apres sh tools/posix/build.sh :
#     sh tools/python/build.sh
#
# Produit :
#   build/python/sdroot/bin/python3                    l'interpreteur, hote de dlopen (-rdynamic)
#   build/python/sdroot/python/lib/python314.zip       la bibliotheque standard, precompilee
#   build/python/sdroot/python/lib/python3.14/         lib-dynload (vide), site-packages, et le
#                                                      sysconfig de la carte (gcc de /gcc)
#   build/python/sdroot/python/include/python3.14/     les en-tetes (modules compiles sur la carte)
#   build/python/tests/                                Lib/test et les modules d'essai en .so
#                                                      (_testcapi...), pour la carte d'essai seulement
#                                                      (tools/python/testimage.py)
# recopies sur la carte par build.ps1 (sdroot seulement).
#
# Les modules de la bibliotheque standard sont dans l'interpreteur (statiques) ; ceux d'essai sont
# des .so, charges par dlopen. Dependances : OpenSSL (build/openssl, tools/openssl/build.sh), zlib
# et SQLite (src/extra du depot principal, docs/../src/extra/README.md), libffi (telechargee),
# expat et libmpdec (copies fournies par CPython).
# Sources et arbres de construction : ~/.cache/armpi-python (hors de git). Les archives viennent de
# https://www.python.org/ftp/python/ et https://github.com/libffi/libffi/releases ; leur sha256
# est verifie.
#
# configure voit Linux (--host=aarch64-unknown-linux-gnu, sys.platform == "linux") mais le
# compilateur ne definit pas __linux__ : CPython prend ses chemins POSIX generiques.
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=aarch64-onyx-elf-
WORK=${WORK:-$HOME/.cache/armpi-python}
JOBS=${JOBS:-$(nproc)}
OUT=${OUT:-$ROOT/build/python}
SPECS=${SPECS:-$ROOT/build/posix/lib/posix.specs}
SSL=${SSL:-$ROOT/build/openssl}
EXTRA=${EXTRA:-${ROOT%%/.claude/worktrees/*}/src/extra}
BUILD_PYTHON=${BUILD_PYTHON:-/usr/bin/python3.14}

VER=3.14.10
SHA256=de414a57119abef52784d8993e341d53aed635fc2b05c2607d9f98a2c5b72032
NAME=Python-$VER
URL=https://www.python.org/ftp/python/$VER/$NAME.tar.xz
FFI_VER=3.8.0
FFI_SHA256=7da3e2d9a171eb0a038f592ecad3ff2bb2550f3496d87b3b29ad0cf4430c0db4
FFI_URL=https://github.com/libffi/libffi/releases/download/v$FFI_VER/libffi-$FFI_VER.tar.gz
ZLIB=$EXTRA/zlib-1.3.1
SQLITE=$EXTRA/sqlite-3.50.4
T=$ROOT/tools/python

export PATH="$TOOL/bin:$PATH"
[ -f "$SPECS" ] || { echo "libposix absente : sh tools/posix/build.sh" >&2; exit 1; }
"$BUILD_PYTHON" -c 'import sys; assert sys.version_info[:2] == (3, 14)' ||
	{ echo "il faut un Python 3.14 de construction (BUILD_PYTHON)" >&2; exit 1; }
for d in "$ZLIB" "$SQLITE"; do [ -d "$d" ] || { echo "absent : $d (EXTRA=...)" >&2; exit 1; }; done

CC="${TC}gcc -specs=$SPECS"
CF="-mcpu=cortex-a72 -O2 -g0 -fno-stack-protector"
fetch () {	# fichier url sha256
	[ -f "$WORK/dl/$1" ] || curl -fsSL -o "$WORK/dl/$1" "$2"
	echo "$3  $WORK/dl/$1" | sha256sum -c -
}
mkdir -p "$WORK/dl" "$WORK/src"

# ---- OpenSSL : celui de Korin (docs/OPENSSL.md), construit s'il manque ----
[ -f "$SSL/lib/libssl.a" ] || NO_TESTS=1 OUT="$SSL" WORK="$WORK/openssl" sh "$ROOT/tools/openssl/build.sh"

# ---- zlib, SQLite, libffi : bibliotheques statiques dans $DEPS, vues par pkg-config ----
DEPS=$WORK/deps
rm -rf "$DEPS"
mkdir -p "$DEPS/include" "$DEPS/lib/pkgconfig" "$DEPS/obj/z" "$DEPS/obj/sqlite"
for f in adler32 compress crc32 deflate gzclose gzlib gzread gzwrite infback inffast inflate inftrees trees uncompr zutil; do
	$CC $CF -DHAVE_UNISTD_H -DHAVE_STDARG_H -c "$ZLIB/$f.c" -o "$DEPS/obj/z/$f.o"
done
${TC}ar rcs "$DEPS/lib/libz.a" "$DEPS"/obj/z/*.o
cp "$ZLIB/zlib.h" "$DEPS/include/"
sed 's|^#ifdef HAVE_UNISTD_H .*|#if 1|' "$ZLIB/zconf.h" > "$DEPS/include/zconf.h"	# (comme ./configure)
printf 'Name: zlib\nDescription: zlib\nVersion: 1.3.1\nLibs: -L%s/lib -lz\nCflags: -I%s/include\n' \
	"$DEPS" "$DEPS" > "$DEPS/lib/pkgconfig/zlib.pc"
# SQLite : verrous de fichiers sans effet sous Korin (docs/POSIX.md) : sur pour un process
$CC $CF -DSQLITE_THREADSAFE=1 -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_MATH_FUNCTIONS \
	-DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DEFAULT_MEMSTATUS=0 -DHAVE_USLEEP=1 \
	-c "$SQLITE/sqlite3.c" -o "$DEPS/obj/sqlite/sqlite3.o"
${TC}ar rcs "$DEPS/lib/libsqlite3.a" "$DEPS/obj/sqlite/sqlite3.o"
cp "$SQLITE/sqlite3.h" "$DEPS/include/"
printf 'Name: SQLite\nDescription: SQLite\nVersion: 3.50.4\nLibs: -L%s/lib -lsqlite3\nCflags: -I%s/include\n' \
	"$DEPS" "$DEPS" > "$DEPS/lib/pkgconfig/sqlite3.pc"
# libffi : fermetures dans des pages PROT_EXEC par mmap (permises par Korin ; sans
# FFI_MMAP_EXEC_WRIT, hors de Linux, libffi les prend a malloc : tas non executable), sans les trampolines
# statiques de Linux (/proc/self/maps)
fetch libffi-$FFI_VER.tar.gz "$FFI_URL" "$FFI_SHA256"
rm -rf "$WORK/src/libffi-$FFI_VER" "$WORK/ffi"
tar xzf "$WORK/dl/libffi-$FFI_VER.tar.gz" -C "$WORK/src"
mkdir -p "$WORK/ffi"
( cd "$WORK/ffi" && CC="$CC" CFLAGS="$CF -DFFI_MMAP_EXEC_WRIT=1" AR=${TC}ar RANLIB=${TC}ranlib \
	"$WORK/src/libffi-$FFI_VER/configure" --host=aarch64-unknown-linux-gnu --build=x86_64-pc-linux-gnu \
	--prefix="$DEPS" --disable-shared --enable-static --disable-docs --disable-multi-os-directory \
	--disable-exec-static-tramp >"$WORK/ffi/configure.log" 2>&1 &&
  make -j"$JOBS" >"$WORK/ffi/build.log" 2>&1 && make install >"$WORK/ffi/install.log" 2>&1 ) ||
	{ tail -30 "$WORK/ffi/configure.log" "$WORK/ffi/build.log" 2>/dev/null; exit 1; }

# ---- CPython : les sources, verifiees, puis les retouches de korin.patch ----
fetch $NAME.tar.xz "$URL" "$SHA256"
S=$WORK/src/$NAME
STAMP=$(cat "$T/korin.patch" 2>/dev/null | sha256sum | cut -c1-16)
if [ "$(cat "$S/.korin-patched" 2>/dev/null)" != "$STAMP" ]; then
	rm -rf "$S"
	tar xJf "$WORK/dl/$NAME.tar.xz" -C "$WORK/src"
	[ -f "$T/korin.patch" ] && patch -d "$S" -p1 < "$T/korin.patch"
	echo "$STAMP" > "$S/.korin-patched"
fi

B=$WORK/build
rm -rf "$B"
mkdir -p "$B/Modules"
# Setup.local : les modules d'essai (_testcapi...) en .so, charges par dlopen (la premiere regle
# d'un module gagne : Setup.local passe avant Setup.stdlib)
{ echo "*shared*"
  sed -n -E 's/^@MODULE_(_TEST|XX|_CTYPES_TEST|_XXTEST)[A-Z0-9_]*_TRUE@//p' "$S/Modules/Setup.stdlib.in"
} > "$B/Modules/Setup.local"
cd "$B"
# la spec dans CC (deux -specs se contredisent) ; pkg-config ne voit que nos bibliotheques ;
# MODULE_BUILDTYPE=static : les modules de la bibliotheque standard dans l'interpreteur ;
# LINKFORSHARED=-rdynamic : python3 est un hote de dlopen (docs/POSIX.md « dlopen »)
CONFIG_SITE=$T/config.site MODULE_BUILDTYPE=static LINKFORSHARED=-rdynamic \
CC="$CC" CXX="${TC}g++ -specs=$SPECS" AR=${TC}ar RANLIB=${TC}ranlib READELF=${TC}readelf STRIP=${TC}strip \
CFLAGS="$CF" CPPFLAGS="-DTHREAD_STACK_SIZE=0x400000" LDFLAGS="" \
PKG_CONFIG_LIBDIR=$DEPS/lib/pkgconfig PKG_CONFIG_PATH= \
	"$S/configure" --host=aarch64-unknown-linux-gnu --build=x86_64-pc-linux-gnu \
	--with-build-python="$BUILD_PYTHON" --prefix=/python \
	--disable-shared --disable-ipv6 --without-ensurepip --enable-test-modules \
	--without-readline --without-static-libpython --without-mimalloc \
	--with-openssl="$SSL" --with-ssl-default-suites=openssl \
	>"$B/configure.log" 2>&1 || { tail -30 "$B/configure.log"; exit 1; }
make -j"$JOBS" >"$B/build.log" 2>&1 || { grep -n -A6 -E ": error:|\*\*\*|undefined reference" "$B/build.log" | cut -c1-400 | head -60; exit 1; }
grep "^MODULE_.*_STATE=" Makefile | grep -v -E "=(yes|n/a)$" | sed 's/^MODULE_//; s/_STATE=/ : /' > "$B/modules-absents.txt"
rm -rf "$OUT/stage"
make DESTDIR="$OUT/stage" install >"$B/install.log" 2>&1 || { tail -30 "$B/install.log"; exit 1; }

# ---- la carte ----
L=$OUT/stage/python/lib/python3.14
SD=$OUT/sdroot
rm -rf "$SD" "$OUT/tests"
mkdir -p "$SD/bin" "$SD/python/lib/python3.14/lib-dynload" "$SD/python/lib/python3.14/site-packages" \
	"$SD/python/include" "$OUT/tests/lib-dynload"
${TC}strip "$OUT/stage/python/bin/python3.14" -o "$SD/bin/python3"
# le sysconfig de la carte : les modules s'y compilent par le gcc de /gcc (specs de Korin fondues
# dans les siennes, docs/GCC.md), en -fPIC -shared ; celui de la construction reste dans
# build/Makefile (les .so d'essai ci-dessus y ont ete faits), stage prend celui de la carte
for f in $(cd "$L" && ls _sysconfigdata_*.py _sysconfig_vars_*.json); do
	sed -e "s|${TC}gcc -specs=$SPECS|gcc|g" -e "s|${TC}g++ -specs=$SPECS|g++|g" \
	    -e "s|${TC}\(ar\|ranlib\|readelf\|strip\)|\1|g" -e "s|$DEPS|/python|g" -e "s|$SSL|/python|g" \
	    -e "s|$B|/python/lib/python3.14/config-3.14|g" -e "s|$S|/python/lib/python3.14/config-3.14|g" \
		"$L/$f" > "$WORK/$f"
	! grep -q "$WORK\|/mnt/" "$WORK/$f" || { echo "chemins de construction dans $f :" >&2;
		grep -o "[^ '\"]*\(/mnt/\|$WORK\)[^ '\"]*" "$WORK/$f" | sort -u | head >&2; exit 1; }
	cp "$WORK/$f" "$L/$f"
done
cp "$L"/_sysconfig_vars_*.json "$SD/python/lib/python3.14/"
cp -r "$OUT/stage/python/include/python3.14" "$SD/python/include/"
"$BUILD_PYTHON" -I "$T/mkzip.py" "$L" "$SD/python/lib/python314.zip" --deflate
# les essais : Lib/test et les modules d'essai (seulement sur la carte d'essai)
cp -r "$L/test" "$OUT/tests/test"
find "$OUT/tests/test" -name "*.opt-[12].pyc" -delete
for f in "$L"/lib-dynload/*.so; do ${TC}strip --strip-unneeded "$f" -o "$OUT/tests/lib-dynload/$(basename "$f")"; done
ls -l "$SD/bin" "$SD/python/lib" "$OUT/tests/lib-dynload"
echo "modules absents ou coupes :"; cat "$B/modules-absents.txt"
