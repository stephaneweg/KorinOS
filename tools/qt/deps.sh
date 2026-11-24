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
# (le .pc dit prefix=/devtools/gcc : PKG_CONFIG_SYSROOT_DIR=$DEVSYS le retrouve ; build.sh le donne)
ls "$DEVSYS/devtools/gcc/lib"
