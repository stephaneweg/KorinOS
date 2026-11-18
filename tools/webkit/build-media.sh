#!/bin/sh
# build-media.sh -- la bibliotheque media du moteur de WebKit pour ARMPI (av.mk -> $WK_OUT/av/libonyxav.a :
# son par AudioKit, video VP8 / VP9 par libvpx et AV1 par dav1d) et son essai avplay
# (-> $WK_OUT/out/avplay ; stage.sh : /bin/avplay). docs/WEBKIT.md, « Son et video ».
#
#   sh tools/webkit/build-media.sh
#   (sur ARMPI)  avplay /webkit/tone.wav ; avplay --decode HOME:/video/test-av1.mp4 ; avplay --types
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
O=$WK_OUT/av
AVARCH=aarch64
AVWRAP=
[ "$ONYX_ARCH" = x86_64 ] && AVARCH=generic	# (Korin PC : codecs.mk sans le NEON)
[ "$ONYX_ARCH" = x86_64 ] && AVWRAP="-Wl,--wrap=vpx_codec_dec_init_ver -Wl,--wrap=dav1d_open"	# (decodeurs sur plusieurs fils)
# FFmpeg (LGPL : H.264, H.265, AAC, Vorbis ; tools/mediakit/ffmpeg.sh FLAVOR=jet), sauf NO_FFMPEG=1
FF=/tmp/korin-ffmpeg-jet-$ONYX_ARCH
FFLIBS=
if [ "${NO_FFMPEG:-0}" != 1 ]; then
	FLAVOR=jet ARCH=$ONYX_ARCH ONYX_SYSROOT=$ONYX_SYSROOT JOBS=${FF_JOBS:-4} sh "$ARMPI/tools/mediakit/ffmpeg.sh"
	FFLIBS="$FF/libavformat.a $FF/libavcodec.a $FF/libswscale.a $FF/libavutil.a"
else
	FF=
fi
make -s -f "$ARMPI/tools/webkit/av.mk" FF="$FF" EXTRA="$EXTRA" GLUE="$ARMPI/tools/webkit/glue" S="$ONYX_SYSROOT" O="$O" \
	TC="$TC_BIN/$ONYX_TOOLCHAIN_PREFIX" AV_ARCH="$AVARCH" CPU="$ONYX_CPU_FLAGS" -j"$JOBS"
mkdir -p "$WK_OUT/out"
${CC} $ONYX_CPU_FLAGS -O2 -specs="$ONYX_SYSROOT/lib/onyx.specs" -I"$EXTRA/onyx-libs/av" \
	"$ARMPI/tools/webkit/avplay.c" "$O/libonyxav.a" $FFLIBS $AVWRAP -lm -o "$O/avplay"
${STRIP} -o "$WK_OUT/out/avplay" "$O/avplay"
ls -l "$O/libonyxav.a" "$WK_OUT/out/avplay"
