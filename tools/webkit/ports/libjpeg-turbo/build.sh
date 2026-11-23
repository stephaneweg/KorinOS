#!/bin/sh
# build.sh -- libjpeg-turbo 3.1.4 for the POSIX ports (third_party/libjpeg-turbo-3.1.4; the IJG licence +
# the Modified BSD licence + zlib's, LICENSE.md): libjpeg.a (the libjpeg API, version 6b ABI, with
# libjpeg-turbo's extensions -- jpeg_skip_scanlines, jpeg_crop_scanline, JCS_EXT_*: what Skia's JPEG codec
# and WebKit's JPEG decoder use; the in-tree jpeg-9f has none of them) + its headers + libjpeg.pc into the
# sysroot. libjpeg-turbo's CMake, with the NEON SIMD (simd/arm, intrinsics: GCC 14 builds them) on the Pi and
# the x86-64 SIMD (simd/x86_64, NASM: SSE2 + AVX2) on Korin PC; no
# TurboJPEG API, no tools, no tests, no shared library.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (this script).
. "$(dirname "$0")/../common.sh"

SRC=$TP/libjpeg-turbo-3.1.4
B=$PORTS_OUT/build/libjpeg-turbo
mkdir -p "$B"

SIMD="-DWITH_SIMD=ON -DREQUIRE_SIMD=ON"
if [ "$ONYX_ARCH" = x86_64 ]; then
	# Korin PC (22/11/2026, docs/agents/JET-LOCAL-BUILD.md) : le SIMD x86-64 (SSE2 et AVX2, choisis au
	# lancement), assemblé par nasm. La copie de src/extra n'a que simd/arm : la source officielle 3.1.4
	# (même version, SHA-256 fixé), dans ~/.cache/korin-src
	command -v nasm >/dev/null || { echo "libjpeg-turbo: nasm manque (paquet nasm : apt install nasm, dnf install nasm)" >&2; exit 1; }
	JT=libjpeg-turbo-3.1.4.tar.gz
	JSUM=e23d3ebb2c6ee4d0e2a5823dbb55b614845df5ea3435c956fed5cf04041a87ad
	JD=${KORIN_SRC_CACHE:-$HOME/.cache/korin-src}
	mkdir -p "$JD"
	[ -f "$JD/$JT" ] && [ "$(sha256sum "$JD/$JT" | cut -d' ' -f1)" = "$JSUM" ] ||
		curl -fsSL --retry 3 -o "$JD/$JT" "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.1.4/$JT"
	[ "$(sha256sum "$JD/$JT" | cut -d' ' -f1)" = "$JSUM" ] || { echo "libjpeg-turbo: sha256 faux : $JD/$JT" >&2; exit 1; }
	SRC=$PORTS_OUT/build/libjpeg-turbo-3.1.4-src
	[ -d "$SRC/simd/x86_64" ] || { rm -rf "$SRC"; mkdir -p "$SRC"; tar -C "$SRC" --strip-components=1 -xzf "$JD/$JT"; }
	rm -rf "$B"
	mkdir -p "$B"
	SIMD="$SIMD -DCMAKE_ASM_NASM_COMPILER=$(command -v nasm)"
fi
echo "libjpeg-turbo: configure (CMake, the Onyx toolchain file)"
onyx_cmake -S "$SRC" -B "$B" -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF -DWITH_TOOLS=OFF \
	-DWITH_TESTS=OFF -DWITH_JAVA=OFF -DWITH_FUZZ=OFF $SIMD -DWITH_ARITH_DEC=ON \
	-DWITH_ARITH_ENC=ON -DCMAKE_INSTALL_PREFIX="$ONYX_SYSROOT" -DCMAKE_INSTALL_LIBDIR=lib \
	-DCMAKE_INSTALL_INCLUDEDIR=include \
	>"$B/configure.log" || { tail -30 "$B/configure.log"; exit 1; }
echo "libjpeg-turbo: build"
cmake --build "$B" -j "$JOBS" >"$B/build.log" || { grep -B2 -A8 "error" "$B/build.log" | head -60; exit 1; }
# the library and its headers (the "doc" component wants doc/, not vendored)
for c in lib include; do
	cmake --install "$B" --component $c >>"$B/install.log" 2>&1 || { tail -20 "$B/install.log"; exit 1; }
done
ls -l "$ONYX_SYSROOT/lib/libjpeg.a" | awk '{print "libjpeg-turbo: " $5 "  " $NF}'
