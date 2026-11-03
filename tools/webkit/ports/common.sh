# common.sh -- partage par les scripts des dependances (tools/webkit/ports/<nom>/build.sh) : repris
# de tools/ports/common.sh d'Onyx, adapte a ARMPI (sysroot de libposix fait par tools/webkit/sysroot.sh,
# sources dans src/extra, sorties hors du depot : tools/webkit/env.sh). Source, pas lance.
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
# hereby granted, free of charge, to any person obtaining a copy of this software and associated
# documentation files (the "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
# do so, subject to the following conditions: The above copyright notice and this permission
# notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
# IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.

set -e
. "$(dirname "$0")/../../env.sh"
PORTS=$ARMPI/tools/webkit/ports
TP=$EXTRA
mkdir -p "$PORTS_OUT/build" "$PORTS_OUT/bin"
[ -f "$ONYX_SYSROOT/lib/onyx.specs" ] || { echo "pas de sysroot dans $ONYX_SYSROOT : sh tools/webkit/sysroot.sh" >&2; exit 1; }

# CMake avec le fichier de chaine (les drapeaux sont les siens, pas ceux de l'environnement)
onyx_cmake ()
{
	env -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CC -u CXX cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
		-DONYX_SYSROOT="$ONYX_SYSROOT" -DCMAKE_BUILD_TYPE=Release "$@"
}

# onyx_dep_lib <lib.a> <dir> <cflags> <sources...> : compilees une fois dans $PORTS_OUT/build/deps
onyx_dep_lib ()
{
	_a=$1; _dir=$2; _cf=$3; shift 3
	_b=$PORTS_OUT/build/deps/${_a%.a}
	if [ ! -f "$_b/$_a" ]; then
		mkdir -p "$_b"
		for _s in "$@"; do
			"${ONYX_TOOLCHAIN_PREFIX}gcc" $CFLAGS -O2 -w $_cf -c "$_s" -o "$_b/$(basename "$_s" .c).o"
		done
		"${ONYX_TOOLCHAIN_PREFIX}ar" rcs "$_b/$_a" "$_b"/*.o
	fi
	cp "$_b/$_a" "$ONYX_SYSROOT/lib/"
}

# zlib, nghttp2, brotli (decodeur) : sources dans src/extra, archives et .pc dans le sysroot
onyx_install_deps ()
{
	inc=$ONYX_SYSROOT/include
	lib=$ONYX_SYSROOT/lib
	pc=$lib/pkgconfig
	mkdir -p "$inc" "$lib" "$pc"
	cp "$TP/zlib-1.3.1/zlib.h" "$TP/zlib-1.3.1/zconf.h" "$inc/"
	onyx_dep_lib libz.a "$TP/zlib-1.3.1" "-DHAVE_UNISTD_H -DZ_HAVE_UNISTD_H -DHAVE_STDARG_H" "$TP"/zlib-1.3.1/*.c
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: zlib\nDescription: zlib compression library\nVersion: 1.3.1\nLibs: -L${libdir} -lz\nCflags: -I${includedir}\n' "$ONYX_SYSROOT" > "$pc/zlib.pc"
	mkdir -p "$inc/nghttp2"
	cp "$TP/nghttp2-1.70.0/lib/includes/nghttp2/nghttp2.h" "$TP/nghttp2-1.70.0/lib/includes/nghttp2/nghttp2ver.h" "$inc/nghttp2/"
	onyx_dep_lib libnghttp2.a "$TP/nghttp2-1.70.0" "-std=gnu99 -DHAVE_CONFIG_H -DNGHTTP2_STATICLIB -I$TP/nghttp2-1.70.0/lib -I$TP/nghttp2-1.70.0/lib/includes" "$TP"/nghttp2-1.70.0/lib/*.c
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: libnghttp2\nDescription: HTTP/2 C library\nVersion: 1.70.0\nLibs: -L${libdir} -lnghttp2\nCflags: -I${includedir} -DNGHTTP2_STATICLIB\n' "$ONYX_SYSROOT" > "$pc/libnghttp2.pc"
	mkdir -p "$inc/brotli"
	cp "$TP/brotli-1.1.0/c/include/brotli/"*.h "$inc/brotli/"
	onyx_dep_lib libbrotlidec.a "$TP/brotli-1.1.0" "-I$TP/brotli-1.1.0/c/include" "$TP"/brotli-1.1.0/c/common/*.c "$TP"/brotli-1.1.0/c/dec/*.c
	printf 'prefix=%s\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: libbrotlidec\nDescription: Brotli decoder library (with brotlicommon)\nVersion: 1.1.0\nLibs: -L${libdir} -lbrotlidec\nCflags: -I${includedir}\n' "$ONYX_SYSROOT" > "$pc/libbrotlidec.pc"
}

# un outil d'essai construit : garde en <nom>.elf dans $PORTS_OUT/bin
onyx_tool_done ()
{
	cp "$1" "$PORTS_OUT/bin/$2.elf"
	"${ONYX_TOOLCHAIN_PREFIX}size" "$PORTS_OUT/bin/$2.elf"
}
