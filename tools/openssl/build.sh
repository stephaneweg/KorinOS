#!/bin/sh
# openssl : libcrypto.a et libssl.a pour les programmes C et C++ d'ARMPI (docs/OPENSSL.md), sur
# newlib + libposix, par la chaine d'Onyx. Hors du selfhost (comme les autres ports C). A lancer
# dans WSL depuis la racine du depot, apres sh tools/posix/build.sh :
#     sh tools/openssl/build.sh
#
# Produit :
#   build/openssl/include/openssl/      les en-tetes
#   build/openssl/lib/libcrypto.a, libssl.a
#   build/openssl/lib/pkgconfig/        libcrypto.pc, libssl.pc, openssl.pc
#   build/openssl/sdroot/bin/           les essais, recopies sur la carte par build.ps1
# Sources et arbre de construction : ~/.cache/armpi-openssl (hors de git).
#
# Une archive locale a la place du telechargement (tools/git/build.sh) :
#     OPENSSL_TAR=<archive .tar.gz> OPENSSL_VER=<version> OPENSSL_PATCH=<patch> sh tools/openssl/build.sh
# (pas de controle sha256 ; le patch par defaut, armpi.patch, est fait pour 3.5.9).
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-/opt/toolchains/aarch64-onyx-elf-14.2}
TC=aarch64-onyx-elf-
WORK=${WORK:-$HOME/.cache/armpi-openssl}
JOBS=${JOBS:-$(nproc)}
OUT=${OUT:-$ROOT/build/openssl}
SPECS=${SPECS:-$ROOT/build/posix/lib/posix.specs}

VER=${OPENSSL_VER:-3.5.9}
SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
PATCH=${OPENSSL_PATCH:-$ROOT/tools/openssl/armpi.patch}
NAME=openssl-$VER
URL=https://github.com/openssl/openssl/releases/download/$NAME/$NAME.tar.gz

export PATH="$TOOL/bin:$PATH"
[ -f "$(dirname "$SPECS")/../include/sys/auxv.h" ] || { echo "libposix absente ou ancienne : sh tools/posix/build.sh" >&2; exit 1; }

# les sources, verifiees, puis les trois retouches d'armpi.patch
mkdir -p "$WORK/dl" "$WORK/src"
if [ -n "${OPENSSL_TAR:-}" ]; then
	A=$OPENSSL_TAR
else
	A=$WORK/dl/$NAME.tar.gz
	[ -f "$A" ] || curl -fsSL -o "$A" "$URL"
	echo "$SHA256  $A" | sha256sum -c -
fi
S=$WORK/src/$NAME
if [ ! -f "$S/.armpi-patched" ]; then
	rm -rf "$S"
	tar xzf "$A" -C "$WORK/src"
	patch -d "$S" -p1 < "$PATCH"
	touch "$S/.armpi-patched"
fi

# no-dso, no-module : pas de dlopen ; le fournisseur « legacy » entre alors dans libcrypto.
# no-async : pas d'ucontext. IPv4 seulement.
# Le processeur : l'assembleur AArch64 est garde, NEON sur le Pi 4 (pas d'extensions AES / SHA).
B=$WORK/build
rm -rf "$B"
mkdir -p "$B"
cd "$B"
perl "$S/Configure" armpi-aarch64 --config="$ROOT/tools/openssl/armpi.conf" \
	--cross-compile-prefix=$TC --prefix=/ --libdir=lib --openssldir=/etc/ssl \
	--with-rand-seed=getrandom \
	no-shared no-dso no-module no-pic no-pie no-async no-engine no-afalgeng \
	no-apps no-tests no-docs -DOPENSSL_USE_IPV6=0 \
	-mcpu=cortex-a72 -g0 -fno-stack-protector -ffunction-sections -fdata-sections \
	-specs="$SPECS"
make -j"$JOBS" build_libs >"$B/build.log" 2>&1 || { grep -n -B2 -A8 "error" "$B/build.log" | head -80; exit 1; }
rm -rf "$OUT"
make DESTDIR="$OUT" install_dev >"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
${TC}strip -g "$OUT/lib/libcrypto.a" "$OUT/lib/libssl.a"
sed -i "s|^prefix=.*|prefix=$OUT|" "$OUT"/lib/pkgconfig/*.pc

# NO_TESTS=1 : les bibliotheques seules
[ "${NO_TESTS:-0}" = 1 ] && exit 0

T=$ROOT/tools/openssl
TF="-mcpu=cortex-a72 -O2 -g0 -Wall -specs=$SPECS -I$OUT/include"
${TC}gcc $TF "$T/openssltest.c" -L"$OUT/lib" -lssl -lcrypto -o "$WORK/openssltest.elf"
${TC}g++ $TF -std=gnu++20 "$T/openssltest-cxx.cpp" -L"$OUT/lib" -lssl -lcrypto -o "$WORK/openssltest-cxx.elf"
mkdir -p "$OUT/sdroot/bin"
${TC}strip "$WORK/openssltest.elf" -o "$OUT/sdroot/bin/openssltest"
${TC}strip "$WORK/openssltest-cxx.elf" -o "$OUT/sdroot/bin/openssltest-cxx"
ls -l "$OUT/lib" "$OUT/sdroot/bin"
