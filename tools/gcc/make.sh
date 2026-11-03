#!/bin/sh
# make : GNU make 4.4.1 pour ARMPI (docs/GCC.md), construit sur le PC, hors du selfhost, comme GCC.
# A lancer dans WSL depuis la racine du depot, apres tools/gcc/build.sh :
#     sh tools/gcc/make.sh
# Produit build/gcc/sdroot/gcc/bin/make.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=$TOOL/bin/aarch64-onyx-elf-
WORK=${WORK:-$HOME/.cache/armpi-gcc}
MAKEV=make-4.4.1
SUM=dd16fb1d67bfab79a72f5e8390735c49e3e8e70b4945a15ab1f81ddb78658fb3
export PATH="$TOOL/bin:$PATH"
[ -x "$WORK/bin/armpi-gcc" ] || { echo "make : lancer d'abord tools/gcc/build.sh" >&2; exit 1; }
mkdir -p "$WORK/dl" "$WORK/src" "$WORK/build"
f=$WORK/dl/$MAKEV.tar.gz
[ -f "$f" ] && [ "$(sha256sum "$f" | cut -d' ' -f1)" = "$SUM" ] || curl -fsSL --retry 3 -o "$f" "https://ftp.gnu.org/gnu/make/$MAKEV.tar.gz"
[ "$(sha256sum "$f" | cut -d' ' -f1)" = "$SUM" ] || { echo "sha256 faux : $f" >&2; exit 1; }
rm -rf "${WORK:?}/src/$MAKEV" "$WORK/build/make"
tar -C "$WORK/src" -xf "$f"
mkdir -p "$WORK/build/make"
cd "$WORK/build/make"
export ac_cv_func_fork=no ac_cv_func_vfork=no ac_cv_func_fork_works=no ac_cv_func_vfork_works=no
export ac_cv_func_posix_spawn=yes ac_cv_func_posix_spawnattr_setsigmask=yes
"$WORK/src/$MAKEV/configure" --build=x86_64-pc-linux-gnu --host=aarch64-onyx-elf --prefix=/gcc \
	--disable-nls --disable-dependency-tracking --disable-load --without-guile --enable-posix-spawn \
	CC="$WORK/bin/armpi-gcc" AR=${TC}ar RANLIB=${TC}ranlib CFLAGS="-O2 -g0" > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
grep -n 'USE_POSIX_SPAWN\|HAVE_POSIX_SPAWN\|HAVE_WAITPID\|HAVE_FORK\|HAVE_VFORK\|HAVE_SIGSETMASK\|HAVE_SYS_WAIT' src/config.h || true
make -j8 > make.log 2>&1 || { grep -n 'error\|undefined' make.log | head -30; exit 1; }
mkdir -p "$ROOT/build/gcc/sdroot/gcc/bin"
${TC}strip make -o "$ROOT/build/gcc/sdroot/gcc/bin/make"
mkdir -p "$ROOT/build/gcc/sdroot/gcc/share"
cp "$ROOT/tools/gcc/pibuild.mk" "$ROOT/build/gcc/sdroot/gcc/share/"
ls -la "$ROOT/build/gcc/sdroot/gcc/bin/make"
