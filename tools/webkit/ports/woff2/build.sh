#!/bin/sh
# build.sh -- woff2 1.0.2 de Google (MIT) pour WebKit : les polices web WOFF2 (presque toutes aujourd'hui ; sans elles,
# le texte retombait sur DejaVu sans qu'on le voie et les polices d'icones, comme celles de claude.ai, montraient des
# rectangles : 27/11/2026). Le decodage seulement : libwoff2common.a, libwoff2dec.a, include/woff2 et leurs .pc dans le
# sysroot (FindWOFF2 de WebKit, patch 0019 : USE_WOFF2) ; brotli (libbrotlidec) est deja la.
. "$(dirname "$0")/../common.sh"

SRC=$TP/woff2-1.0.2
if [ ! -d "$SRC/src" ]; then
	echo "woff2: git clone v1.0.2 -> $SRC"
	git clone -q --depth 1 --branch v1.0.2 https://github.com/google/woff2.git "$SRC"
fi
B=$PORTS_OUT/build/woff2
rm -rf "$B"
mkdir -p "$B"
for f in woff2_common table_tags variable_length woff2_dec woff2_out; do
	"${ONYX_TOOLCHAIN_PREFIX}g++" $CXXFLAGS -std=gnu++17 -fno-exceptions -w -I"$SRC/include" -c "$SRC/src/$f.cc" -o "$B/$f.o"
done
"${ONYX_TOOLCHAIN_PREFIX}ar" rcs "$ONYX_SYSROOT/lib/libwoff2common.a" "$B/woff2_common.o" "$B/table_tags.o" "$B/variable_length.o"
"${ONYX_TOOLCHAIN_PREFIX}ar" rcs "$ONYX_SYSROOT/lib/libwoff2dec.a" "$B/woff2_dec.o" "$B/woff2_out.o"
mkdir -p "$ONYX_SYSROOT/include/woff2" "$ONYX_SYSROOT/lib/pkgconfig"
cp "$SRC"/include/woff2/*.h "$ONYX_SYSROOT/include/woff2/"
for c in common dec; do
	req=""
	[ $c = dec ] && req="Requires.private: libwoff2common, libbrotlidec"
	cat > "$ONYX_SYSROOT/lib/pkgconfig/libwoff2$c.pc" <<PC
prefix=$ONYX_SYSROOT
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: libwoff2$c
Description: WOFF2 $c library
Version: 1.0.2
$req
Libs: -L\${libdir} -lwoff2$c
Cflags: -I\${includedir}
PC
done
ls -l "$ONYX_SYSROOT/lib/libwoff2common.a" "$ONYX_SYSROOT/lib/libwoff2dec.a" | awk '{print "woff2: " $5 "  " $NF}'
