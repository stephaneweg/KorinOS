#!/bin/sh
# Construit build/audiotest/refdec.exe (decodeurs de reference sur le PC,
# tools/audiotest/refdec.c) avec le gcc de Windows et les sources d'Onyx.
# A lancer depuis la racine du depot.
ONYX=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx/third_party}
OUT=build/audiotest
mkdir -p $OUT/obj
O=$ONYX/opus-1.5.2
CF="-O2 -w -DHAVE_CONFIG_H -I$O/onyx -I$O/include -I$O/celt -I$O/silk -I$O/silk/float -I$O"
objs=""
for f in $O/celt/*.c $O/silk/*.c $O/silk/float/*.c $O/src/opus.c $O/src/opus_decoder.c $O/src/opus_encoder.c $O/src/analysis.c $O/src/mlp.c $O/src/mlp_data.c $O/src/extensions.c $O/src/repacketizer.c; do
  case $f in *opus_custom_demo*|*/silk/debug.c) continue;; esac
  b=$OUT/obj/$(basename $f .c).o
  [ -f $b ] || gcc $CF -c $f -o $b || exit 1
  objs="$objs $b"
done
gcc -O2 -w -I$ONYX/minimp3 -I$ONYX/dr_libs -I$ONYX/stb_vorbis -I$O/include tools/audiotest/refdec.c $objs -o $OUT/refdec.exe -lm && echo "$OUT/refdec.exe"
