#!/bin/sh
# build-deps.sh -- les dependances de WebKit, construites contre libposix (docs/WEBKIT.md) : les
# scripts de tools/webkit/ports (repris de tools/ports d'Onyx), sources dans src/extra, archives et
# en-tetes dans le sysroot ($WK_OUT/sysroot), outils d'essai dans $WK_OUT/ports/bin.
#
#   sh tools/webkit/build-deps.sh [dep...]    deps : sqlite openssl libxml2 curl icu libpng freetype
#                                             harfbuzz libjpeg-turbo libwebp skia ; groupes « c » (les
#                                             quatre premieres) et « webkit » (les autres) ; defaut : tout
# Le sysroot est refait d'abord (sysroot.sh). Environ 10 minutes sur 16 coeurs.
set -e
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
here=$ARMPI/tools/webkit
ports=
for p in ${*:-c webkit}; do
	case $p in
	c) ports="$ports sqlite openssl libxml2 curl";;
	webkit) ports="$ports icu libpng freetype harfbuzz libjpeg-turbo libwebp skia";;
	*) ports="$ports $p";;
	esac
done
sh "$here/sysroot.sh"
start=$(date +%s)
for p in $ports; do
	echo "=== $p"
	sh "$here/ports/$p/build.sh"
done
echo "build-deps: $ports en $(( $(date +%s) - start )) s ; sysroot $ONYX_SYSROOT"
