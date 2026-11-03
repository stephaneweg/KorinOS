#!/bin/sh
# build-webcore.sh -- PAL + WebCore (+ WebKit : TARGET=WebKit) pour ARMPI (PORT=Onyx avec ONYX_WEBCORE et
# ONYX_WEBKIT -- ce dernier donne aussi les en-tetes de la glue : av.h --, docs/WEBKIT.md) : Skia sur
# le processeur (sans GL, sans compositeur), curl + OpenSSL, sur le sysroot de libposix et toutes les
# dependances (build-deps.sh). Repris de tools/webkit/build-webcore.sh d'Onyx.
#
#   sh tools/webkit/build-webcore.sh               # -> $BUILD/lib/libWebCore.a
#   KEEP_GOING=1 sh tools/webkit/build-webcore.sh  # ninja -k 0 : toutes les erreurs
#   TARGET=PAL sh tools/webkit/build-webcore.sh    # une autre cible
#
# Variables : celles d'env.sh, BUILD (defaut $WK_OUT/webcore), JOBS (defaut 12 : environ 1,5 Go par
# travail pour les sources unifiees de WebCore), CMAKE_EXTRA. Une premiere construction : de l'ordre
# de 30 minutes sur 16 coeurs.
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
: "${BUILD:=$WK_OUT/webcore}"
: "${CMAKE_EXTRA:=}"
: "${TARGET:=WebCore}"
[ "$JOBS" -gt 12 ] && JOBS=12
unset CFLAGS CXXFLAGS LDFLAGS CC CXX

sh "$ARMPI/tools/webkit/fetch.sh"
for l in icuuc skia harfbuzz curl xml2 sqlite3 ssl; do
	[ -f "$ONYX_SYSROOT/lib/lib$l.a" ] || { echo "build-webcore.sh : pas de lib$l.a dans $ONYX_SYSROOT : sh tools/webkit/build-deps.sh" >&2; exit 1; }
done
for t in cmake ninja perl python3 ruby gperf pkg-config unifdef; do
	command -v $t >/dev/null 2>&1 || { echo "build-webcore.sh : outil $t absent" >&2; exit 1; }
done
# la glue d'Onyx dont WebKit lit les en-tetes (gpucomp.h, av.h) : tools/webkit/glue.sh
sh "$ARMPI/tools/webkit/glue.sh"

if [ ! -f "$BUILD/build.ninja" ]; then
	mkdir -p "$BUILD"
	cmake -S "$WEBKIT_DIR" -B "$BUILD" -G Ninja \
		-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" -DONYX_SYSROOT="$ONYX_SYSROOT" \
		-DONYX_SOURCE_DIR="$WK_OUT/glue" \
		-DPORT=Onyx -DONYX_WEBCORE=ON -DONYX_WEBKIT=ON -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG -g0" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG -g0" \
		-DDEVELOPER_MODE=OFF $CMAKE_EXTRA
fi
[ "${CONFIGURE_ONLY:-0}" = 1 ] && exit 0
start=$(date +%s)
if [ "${KEEP_GOING:-0}" = 1 ]; then
	ninja -C "$BUILD" -j"$JOBS" -k 0 "$TARGET"
else
	cmake --build "$BUILD" --target "$TARGET" -- -j"$JOBS"
fi
echo "build-webcore.sh : $TARGET construit en $(( $(date +%s) - start )) s dans $BUILD"
