#!/bin/sh
# build-media.sh -- la bibliotheque media du moteur de WebKit pour ARMPI (av.mk -> $WK_OUT/av/libonyxav.a :
# son par AudioKit, video au point de branchement de MediaKit) et son essai avplay
# (-> $WK_OUT/out/avplay ; stage.sh : SD0:/bin/avplay). docs/WEBKIT.md, « Son ».
#
#   sh tools/webkit/build-media.sh
#   (sur ARMPI)  avplay SD0:/webkit/tone.wav
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
O=$WK_OUT/av
make -s -f "$ARMPI/tools/webkit/av.mk" EXTRA="$EXTRA" GLUE="$ARMPI/tools/webkit/glue" S="$ONYX_SYSROOT" O="$O" -j"$JOBS"
mkdir -p "$WK_OUT/out"
${CC} -mcpu=cortex-a72 -O2 -specs="$ONYX_SYSROOT/lib/onyx.specs" -I"$EXTRA/onyx-libs/av" \
	"$ARMPI/tools/webkit/avplay.c" "$O/libonyxav.a" -lm -o "$O/avplay"
${STRIP} -o "$WK_OUT/out/avplay" "$O/avplay"
ls -l "$O/libonyxav.a" "$WK_OUT/out/avplay"
