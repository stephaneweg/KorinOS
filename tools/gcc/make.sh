#!/bin/sh
# make : GNU make 4.4.1 pour ARMPI (docs/GCC.md), construit sur le PC, hors du selfhost, comme GCC.
# A lancer dans WSL depuis la racine du depot, apres tools/gcc/build.sh :
#     sh tools/gcc/make.sh
# Produit build/gcc/sdroot/gcc/bin/make.
# ARCH=x86_64 : pour Korin sur PC (build/pc/gccroot/gcc/bin/make, apres ARCH=x86_64 sh tools/gcc/build.sh).
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCH=${ARCH:-aarch64}
case $ARCH in
aarch64) TARGET=aarch64-onyx-elf; WORK=${WORK:-$HOME/.cache/armpi-gcc}; OUT=$ROOT/build/gcc/sdroot/gcc ;;
x86_64) TARGET=x86_64-onyx-elf; WORK=${WORK:-$HOME/.cache/korin-gcc-x86_64}; OUT=$ROOT/build/pc/gccroot/gcc ;;
*) echo "make : ARCH=aarch64 ou x86_64" >&2; exit 1 ;;
esac
TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/$TARGET-14.2}
TC=$TOOL/bin/$TARGET-
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
"$WORK/src/$MAKEV/configure" --build=x86_64-pc-linux-gnu --host=$TARGET --prefix=/devtools/gcc \
	--disable-nls --disable-dependency-tracking --disable-load --without-guile --enable-posix-spawn \
	CC="$WORK/bin/armpi-gcc" AR=${TC}ar RANLIB=${TC}ranlib CFLAGS="-O2 -g0" > configure.log 2>&1 || { tail -20 configure.log; exit 1; }
grep -n 'USE_POSIX_SPAWN\|HAVE_POSIX_SPAWN\|HAVE_WAITPID\|HAVE_FORK\|HAVE_VFORK\|HAVE_SIGSETMASK\|HAVE_SYS_WAIT' src/config.h || true
make -j8 > make.log 2>&1 || { grep -n 'error\|undefined' make.log | head -30; exit 1; }
mkdir -p "$OUT/bin"
${TC}strip make -o "$OUT/bin/make"
mkdir -p "$OUT/share"
if [ "$ARCH" = x86_64 ]; then
	# (pas de FASMARM sur le PC : /bin/fasm, le fasm x86-64 de tools/pc/fasm.sh)
	sed 's/fasmarm/fasm/g' "$ROOT/tools/gcc/pibuild.mk" > "$OUT/share/pibuild.mk"
else
	cp "$ROOT/tools/gcc/pibuild.mk" "$OUT/share/"
fi
ls -la "$OUT/bin/make"
