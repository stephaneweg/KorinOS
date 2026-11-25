#!/bin/sh
# deps.sh : les dépendances de Qt qui ont déjà leur paquet <nom>-dev publié (packages/pc/pkgs/*.opk : zlib, libpng,
# brotli, freetype, harfbuzz, openssl ; tools/devtools/stage-dev.sh), dépaquetées dans $DEVSYS (préfixe
# /devtools/gcc, comme dans Korin). Qt s'y lie (FEATURE_system_*), au lieu de ses copies embarquées : une app Qt
# construite dans Korin prendra les mêmes paquets. Ce qui n'a pas de paquet (pcre2, libjpeg, double-conversion, md4c)
# reste embarqué dans qtbase (Qt6Bundled*.a, livrés dans qtbase-dev).
set -e
. "$(dirname "$0")/env.sh"
PK=$ROOT/packages/pc/pkgs
rm -rf "$DEVSYS"
mkdir -p "$DEVSYS"
for n in zlib-dev libpng-dev brotli-dev freetype-dev harfbuzz-dev openssl-dev; do
	f=$(ls "$PK"/$n-[0-9]*.opk 2>/dev/null | sort -V | tail -1)
	[ -f "$f" ] || { echo "deps.sh : pas de paquet $n dans $PK" >&2; exit 1; }
	python3 -I -c 'import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
for m in z.namelist():
    if not m.startswith("PKG/"):
        z.extract(m, sys.argv[2])' "$f" "$DEVSYS"
	echo "$n : ${f##*/}"
done
# FreeType 2.14.3 retouché (tools/webkit/ports/freetype/korin-transform-ppem.patch) tant que le paquet publié ne
# l'a pas : sa limite « 10 x ppem » refuse les glyphes rendus sous une matrice, ce que Qt fait pour le texte agrandi
# (MuseScore : paroles fausses, plantage dans QFontEngineFT::loadGlyph). Même recette que le paquet, libposix.
FT=freetype-2.14.3
FT_SHA=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f
[ -f "$DL/$FT.tar.xz" ] || curl -sSfL -o "$DL/$FT.tar.xz" "https://download.savannah.gnu.org/releases/freetype/$FT.tar.xz"
echo "$FT_SHA  $DL/$FT.tar.xz" | sha256sum -c -
B=$OUT/obj/freetype
mkdir -p "$B"
[ -d "$B/$FT" ] || tar xf "$DL/$FT.tar.xz" -C "$B" --no-same-owner
patch -N -p1 -s -d "$B/$FT" < "$ROOT/tools/webkit/ports/freetype/korin-transform-ppem.patch" || true
FD=$DEVSYS/devtools/gcc
for s in base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbbox.c base/ftbitmap.c \
	base/ftfstype.c base/ftgasp.c base/ftglyph.c base/ftmm.c base/ftstroke.c base/ftsynth.c base/ftotval.c \
	base/ftpatent.c base/fttype1.c base/ftcid.c base/ftfntfmt.c base/ftlcdfil.c autofit/autofit.c \
	truetype/truetype.c cff/cff.c sfnt/sfnt.c psaux/psaux.c psnames/psnames.c pshinter/pshinter.c \
	smooth/smooth.c raster/raster.c svg/svg.c gzip/ftgzip.c; do
	"${TC}gcc" $KORIN_CPU -specs="$POSIX/lib/posix.specs" -O2 -w -ffunction-sections -fdata-sections \
		-DFT2_BUILD_LIBRARY "-DFT_CONFIG_MODULES_H=<onyx_ftmodule.h>" -DFT_CONFIG_OPTION_SYSTEM_ZLIB \
		-DFT_CONFIG_OPTION_USE_PNG -DFT_CONFIG_OPTION_USE_BROTLI -DHAVE_UNISTD_H -DHAVE_FCNTL_H \
		-I"$ROOT/tools/webkit/ports/freetype" -I"$B/$FT/include" -I"$FD/include" -I"$FD/include/libpng16" \
		-c "$B/$FT/src/$s" -o "$B/$(basename "$s" .c).o"
done
rm -f "$FD/lib/libfreetype.a"
"${TC}ar" rcs "$FD/lib/libfreetype.a" "$B"/*.o
echo "freetype : $FT retouché (korin-transform-ppem.patch)"

# (le .pc dit prefix=/devtools/gcc : PKG_CONFIG_SYSROOT_DIR=$DEVSYS le retrouve ; build.sh le donne)
ls "$DEVSYS/devtools/gcc/lib"
