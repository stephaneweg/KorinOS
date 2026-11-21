#!/bin/sh
# build-jet.sh -- Jet, le navigateur d'Onyx, sur ARMPI (src/extra/onyxports/jet, docs/WEBKIT.md, etape 5) :
# le moteur d'Onyx (engine_webkit.cpp, WebKit2 par son API C), l'habillage d'ARMPI (main.cpp sur UIKit)
# et la presentation (present.c : la page dans une surface attachee d'Elegant), lies a libWebKit.a
# (sh tools/mesa/build-webkit.sh) comme wk2test, avec ANGLE et Mesa pour WebGL (docs/MESA.md).
# -> $WK_OUT/out/jet.app/{main, app.txt, start.html} ; stage.sh le met sur la carte (/apps/jet.app).
#
#   sh tools/webkit/build-jet.sh
#   (sur ARMPI)  le dock, ou /apps/jet.app/main [adresse]
# Korin PC (docs/etude/PC-PILOTES.md, "Jet pour le PC") : ARCH=x86_64 sh tools/webkit/build-jet.sh, apres
# ARCH=x86_64 sh tools/mesa/build-webkit.sh ; GlKit du PC (build/pc/mesa/lib/libglkit.a, tools/mesa/build-iris.sh).
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
MESA_OUT=$HOME/armpi-mesa/out
GLKIT=$ARMPI/prebuilt/mesa/lib/libglkit.a
if [ "$ONYX_ARCH" = x86_64 ]; then
	: "${BUILD:=$WK_OUT/webcore}"
	GLKIT=$ARMPI/build/pc/mesa/lib/libglkit.a
	[ -f "$GLKIT" ] || { echo "build-jet.sh : pas de $GLKIT : sh tools/mesa/build-iris.sh" >&2; exit 1; }
else
	: "${BUILD:=$MESA_OUT/webcore}"
	[ -f "$ARMPI/prebuilt/mesa/lib/libarmgl.a" ] || { echo "build-jet.sh : pas de prebuilt/mesa : sh tools/mesa/build-armgl.sh" >&2; exit 1; }
fi
unset CFLAGS CXXFLAGS LDFLAGS CC CXX
# (JET_OUT : Jet ailleurs que dans $WK_OUT/out/jet.app, pour un essai à part, avec BUILD)
: "${JET_OUT:=$WK_OUT/out/jet.app}"
S=$ONYX_SYSROOT
HERE=$ARMPI/tools/webkit
J=$ARMPI/src/extra/onyxports/jet
U=$WK_OUT/uikit
[ -f "$BUILD/lib/libWebKit.a" ] || { echo "build-jet.sh : pas de $BUILD/lib/libWebKit.a : sh tools/mesa/build-webkit.sh" >&2; exit 1; }
# (refaite aussi quand UIKit a de nouvelles fonctions : ui_window_place, ui_window_top de webapp.cpp)
[ -f "$U/libuikit_c.a" ] && [ ! "$ARMPI/src/kits/uikit/uikit.abi" -nt "$U/libuikit_c.a" ] || sh "$HERE/build-ui.sh"
# (et la libposix du sysroot quand src/lib/posix est plus récent : Jet la lie en statique ; une copie du 16/11 au matin
# laissait HOME à /home sous un compte, et Jet écrivait dans /home/downloads, /home/spotify -- 20/11)
if [ ! -f "$S/lib/libposix.a" ] || [ -n "$(find "$ARMPI/src/lib/posix" -type f -newer "$S/lib/libposix.a" | head -n 1)" ]; then
	echo "jet : libposix du sysroot refaite (tools/webkit/sysroot.sh)"
	sh "$HERE/sysroot.sh"
fi

# les sources C++ avec la commande meme d'une source de WebKit (en-tetes prives, configuration)
ref=UIProcess/API/C/onyx/WKRunLoop.cpp
cmd=$(cd "$BUILD" && ninja -t commands lib/libWebKit.a | grep -F "$ref" | grep -F -- " -c " | head -n 1)
[ -n "$cmd" ] || { echo "build-jet.sh : pas de commande pour $ref dans $BUILD" >&2; exit 1; }
for f in engine_webkit main webapp; do
	c=$(printf '%s' "$cmd" | sed -E 's/ -MD -MT [^ ]+ -MF [^ ]+//; s/ -o [^ ]+/ -o jet_'"$f"'.o/; s| -c [^ ]+| -c '"$J/$f"'.cpp -I'"$J"' -I'"$ARMPI"'/src/kits/uikit/include -I'"$U"'|')
	echo "jet : $f.cpp"
	( cd "$BUILD" && sh -c "$c" )
done
${ONYX_TOOLCHAIN_PREFIX}gcc -specs="$S/lib/onyx-cc.specs" -O2 $ONYX_CPU_FLAGS -I"$ARMPI/src/kits/uikit/include" -I"$U" \
	-c "$J/present.c" -o "$BUILD/jet_present.o"

WK="-Wl,--start-group $BUILD/lib/libWebKit.a $BUILD/lib/libWebCore.a $BUILD/lib/libPAL.a $BUILD/lib/libJavaScriptCore.a $BUILD/lib/libWTF.a $BUILD/lib/libbmalloc.a -Wl,--end-group"
LIBS=""
for l in skia harfbuzz-icu harfbuzz freetype png16 jpeg webpmux webpdemux webp sharpyuv curl ssl crypto \
	nghttp2 brotlidec xml2 sqlite3 z icui18n icuuc icudata; do
	LIBS="$LIBS $S/lib/lib$l.a"
done
# WebGL : ANGLE, puis Mesa par armgl
GL="-Wl,--start-group $BUILD/lib/libANGLE.a $BUILD/lib/libGLESv2.a $GLKIT $S/lib/libz.a -Wl,--end-group"
# la bibliothèque media d'av (src/extra/onyx-libs/av, avec tools/webkit/av-patches) : WebCore l'utilise par ses
# en-têtes (av_packet, av_track : leur forme compte) ; un av.h plus récent que libWebCore.a : WebKit d'abord
# (tools/mesa/build-webkit.sh), sinon Jet lierait deux formes de ces structures ; des sources d'av plus récentes que
# libonyxav.a (ou du client de cdmhost, src/sys/cdmhost) : elle est refaite
if [ "$EXTRA/onyx-libs/av/av.h" -nt "$BUILD/lib/libWebCore.a" ]; then
	echo "build-jet.sh : av.h a changé depuis $BUILD/lib/libWebCore.a : d'abord tools/mesa/build-webkit.sh" >&2
	exit 1
fi
if [ ! -f "$WK_OUT/av/libonyxav.a" ] || [ -n "$(find "$EXTRA/onyx-libs/av" "$ARMPI/src/sys/cdmhost" -name '*.[ch]*' -newer "$WK_OUT/av/libonyxav.a" | head -n 1)" ]; then
	sh "$HERE/build-media.sh"
fi
# FFmpeg dans la bibliotheque media (LGPL, build-media.sh) : ses bibliotheques apres libonyxav.a
FF=/tmp/korin-ffmpeg-jet-$ONYX_ARCH
FFLIBS=
[ "${NO_FFMPEG:-0}" = 1 ] || [ ! -f "$FF/libavcodec.a" ] || FFLIBS="$FF/libavformat.a $FF/libavcodec.a $FF/libswscale.a $FF/libavutil.a"
${ONYX_TOOLCHAIN_PREFIX}gcc -specs="$S/lib/onyx-cc.specs" -O2 $ONYX_CPU_FLAGS -c "$HERE/glue/webkit2_armpi.c" -o "$BUILD/webkit2_armpi.o"
# gpucomp d'Onyx tel quel (le compositeur de LayerTreeHostOnyx) sur GpuKit : glue/gpucomp_armpi.c
GC="-specs=$S/lib/onyx-cc.specs -O2 $ONYX_CPU_FLAGS -I$HERE/glue/include -I$EXTRA/onyx-libs/gpucomp"
${ONYX_TOOLCHAIN_PREFIX}gcc $GC -c "$EXTRA/onyx-libs/gpucomp/gpucomp.c" -o "$BUILD/gpucomp.o"
# (Korin PC : ses appels au GPU sur OpenGL ES par GlKit -- Mesa iris --, glue/gpucomp_gl.c ; le Pi : GpuKit, le V3D)
GCGLUE=gpucomp_armpi.c
[ "$ONYX_ARCH" = x86_64 ] && GCGLUE=gpucomp_gl.c
${ONYX_TOOLCHAIN_PREFIX}gcc $GC -c "$HERE/glue/$GCGLUE" -o "$BUILD/gpucomp_armpi.o"
mkdir -p "$BUILD/bin" "$JET_OUT"
echo "jet : edition de liens"
# (Korin PC : les decodeurs video sur plusieurs fils, glue/av_threads_pc.c)
AVWRAP=
[ "$ONYX_ARCH" = x86_64 ] && AVWRAP="-Wl,--wrap=vpx_codec_dec_init_ver -Wl,--wrap=dav1d_open"
${ONYX_TOOLCHAIN_PREFIX}g++ $ONYX_CPU_FLAGS -specs="$S/lib/onyx.specs" -L"$S/lib" -Wl,--gc-sections $AVWRAP \
	"$BUILD/jet_main.o" "$BUILD/jet_engine_webkit.o" "$BUILD/jet_webapp.o" "$BUILD/jet_present.o" "$BUILD/webkit2_armpi.o" "$BUILD/gpucomp.o" "$BUILD/gpucomp_armpi.o" $WK \
	"$U/libuikit_c.a" "$WK_OUT/av/libonyxav.a" $FFLIBS $LIBS $GL -o "$BUILD/bin/jet"
${ONYX_TOOLCHAIN_PREFIX}size "$BUILD/bin/jet"
${STRIP} -o "$JET_OUT/main" "$BUILD/bin/jet"
cp "$J/app.txt" "$J/start.html" "$JET_OUT/"
# (les sondes pour un rapport, en /etc/web-probe.js : frame-probe.js, docs/WEBKIT.md « hCaptcha » ; login-probe.js,
# « Connexion à Facebook »)
cp "$HERE/tests/frame-probe.js" "$HERE/tests/login-probe.js" "$JET_OUT/"
# l'hôte du module Widevine (src/sys/cdmhost, docs/etude/WIDEVINE.md ; PC seulement : le module est Linux x86-64),
# que CDMWidevineOnyx lance (/apps/jet.app/libexec/cdmhost : un dossier de /etc/exec.txt, donc exécutable) ;
# construit par tools/linux/build.sh. Le module n'est pas là : la case des réglages de Jet le fait télécharger chez
# Google par cdmupdate (même dossier ; OpenSSL et zlib du sysroot de Jet), dans HOME:/.jet/widevine (jamais dans nos
# paquets)
if [ "$ONYX_ARCH" = x86_64 ]; then
	if [ -f "$ARMPI/build/pc/linux/cdmhost" ]; then
		mkdir -p "$JET_OUT/libexec"
		rm -f "$JET_OUT/cdmhost"
		${STRIP} -o "$JET_OUT/libexec/cdmhost" "$ARMPI/build/pc/linux/cdmhost"
		CDM=$ARMPI/src/sys/cdmhost
		${ONYX_TOOLCHAIN_PREFIX}g++ -specs="$S/lib/onyx.specs" -O2 $ONYX_CPU_FLAGS -std=gnu++17 -I"$S/include" \
			"$CDM/cdmupdate.cpp" "$CDM/net.cpp" "$S/lib/libssl.a" "$S/lib/libcrypto.a" "$S/lib/libz.a" -o "$BUILD/cdmupdate"
		${STRIP} -o "$JET_OUT/libexec/cdmupdate" "$BUILD/cdmupdate"
	else
		echo "build-jet.sh : pas de build/pc/linux/cdmhost (sh tools/linux/build.sh) : Jet sans Widevine" >&2
	fi
fi
ls -l "$JET_OUT"
