#!/bin/sh
# stage-dev.sh : les bibliothèques de Jet (le sysroot de tools/webkit/build-deps.sh, ARCH=x86_64) en paquets de
# développement <nom>-dev pour le GCC de Korin PC (docs/agents/JET-LOCAL-BUILD.md, point 5) : les .a dans
# /devtools/gcc/lib, leurs en-têtes dans /devtools/gcc/include, leur .pc dans /devtools/gcc/lib/pkgconfig
# (préfixe /devtools/gcc ; pkgconf : paquet pkgconf), leur licence dans /devtools/gcc/share/doc/<nom>.
# Construites comme WebKit : -march=tigerlake -mno-avx512f -O2 (tools/webkit/env.sh, armpi-toolchain.cmake).
#
#     ARCH=x86_64 sh tools/webkit/build-deps.sh      (avant)
#     sh tools/devtools/stage-dev.sh [NOM-dev...]     -> build/pc/devtools/<nom>-dev/
#     python3 tools/pkg/pub.py <nom>-dev --new --tree build/pc/devtools/<nom>-dev
#
# Korin n'a pas de liens symboliques : libpng.a est une copie de libpng16.a.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
S=${ONYX_SYSROOT:-$HOME/korin-webkit-x86_64/sysroot}
X=$ROOT/src/extra
OUT=$ROOT/build/pc/devtools
P=devtools/gcc
[ -f "$S/lib/libz.a" ] || { echo "stage-dev : pas de $S/lib/libz.a : ARCH=x86_64 sh tools/webkit/build-deps.sh" >&2; exit 1; }

# pkg <nom-dev> <libs (sans lib ni .a)> <en-têtes (fichiers ou dossiers de include/)> <.pc> <licences (de src/extra)>
pkg () {
	n=$1 libs=$2 hdrs=$3 pcs=$4 lics=$5
	case " ${WANT:-$n} " in *" $n "*) ;; *) return 0 ;; esac
	T=$OUT/$n
	rm -rf "$T"
	mkdir -p "$T/$P/lib/pkgconfig" "$T/$P/include" "$T/$P/share/doc/${n%-dev}"
	for l in $libs; do cp -L "$S/lib/lib$l.a" "$T/$P/lib/"; done
	for h in $hdrs; do cp -RL "$S/include/$h" "$T/$P/include/"; done
	for pc in $pcs; do
		sed -e "s|$S|/$P|g" "$S/lib/pkgconfig/$pc.pc" > "$T/$P/lib/pkgconfig/$pc.pc"
	done
	for f in $lics; do
		case $f in /*) ;; *) f=$X/$f ;; esac
		[ -f "$f" ] && cp "$f" "$T/$P/share/doc/${n%-dev}/" || echo "stage-dev : $n : pas de licence $f" >&2
	done
	v=$(sed -n 's/^Version: *//p' "$S/lib/pkgconfig/${pcs%% *}.pc" | head -1)
	echo "$n $v : $(find "$T" -type f | wc -l) fichiers, $(du -sk "$T" | cut -f1) Ko"
}

WANT=${*:-}
[ -n "$WANT" ] || WANT="zlib-dev libpng-dev libwebp-dev freetype-dev harfbuzz-dev icu-dev skia-dev sqlite-dev libxml2-dev brotli-dev nghttp2-dev openssl-dev curl-dev"
pkg zlib-dev "z" "zlib.h zconf.h" "zlib" "zlib-1.3.1/LICENSE"
pkg libpng-dev "png16 png" "png.h pngconf.h pnglibconf.h libpng16" "libpng16 libpng" "libpng-1.6.44/LICENSE"
pkg libwebp-dev "webp webpdecoder webpdemux webpmux sharpyuv" "webp" \
	"libwebp libwebpdecoder libwebpdemux libwebpmux libsharpyuv" "libwebp-1.4.0/COPYING libwebp-1.4.0/PATENTS"
pkg freetype-dev "freetype" "freetype2" "freetype2" "freetype-2.14.3/LICENSE.TXT freetype-2.14.3/docs/FTL.TXT"
pkg harfbuzz-dev "harfbuzz harfbuzz-icu" "harfbuzz" "harfbuzz harfbuzz-icu" "harfbuzz-14.5.1/COPYING"
pkg icu-dev "icuuc icui18n icudata" "unicode" "icu-uc icu-i18n" "icu-78.3/LICENSE"
pkg skia-dev "skia" "skia" "skia" "skia-m154/LICENSE"
pkg sqlite-dev "sqlite3" "sqlite3.h sqlite3ext.h" "sqlite3" ""
pkg libxml2-dev "xml2" "libxml2" "libxml-2.0" "libxml2-2.13.8/Copyright"
pkg brotli-dev "brotlidec" "brotli" "libbrotlidec" "brotli-1.1.0/LICENSE"
pkg nghttp2-dev "nghttp2" "nghttp2" "libnghttp2" "nghttp2-1.70.0/COPYING"
pkg openssl-dev "ssl crypto" "openssl" "openssl libssl libcrypto" "${OPENSSL_SRC:-$(dirname "$S")/openssl-work/src/openssl-3.5.9}/LICENSE.txt"
pkg curl-dev "curl" "curl" "libcurl" "curl-8.16.0/COPYING"
