#!/bin/sh
# build-test.sh : caotest (tools/cao/test/caotest.cpp), l'essai d'OCCT et de PlaneGCS, relié en statique pour Korin :
# ~/korin-cao/$ARCH/test/caotest. D'abord build-occt.sh et build-planegcs.sh de la même architecture.
#     sh tools/cao/build-test.sh          (ARCH=aarch64 : pour le Pi)
set -eu
. "$(dirname "$0")/env.sh"
O=$OUT/test
mkdir -p "$O"
OC=$OUT/occt
OCL=$(dirname "$(ls "$OC"/lib*/libTKernel.a | head -1)")
TKS="TKDESTEP TKDEIGES TKDESTL TKXCAF TKVCAF TKV3d TKService TKCAF TKLCAF TKCDF TKDE TKXSBase TKFillet TKOffset
TKFeat TKBool TKBO TKPrim TKHLR TKMesh TKShHealing TKTopAlgo TKGeomAlgo TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel"
LIBS=""
for t in $TKS; do [ -f "$OCL/lib$t.a" ] && LIBS="$LIBS -l$t"; done
t0=$(date +%s)
# shellcheck disable=SC2086
${TC}g++ $MCPU -specs="$POSIX/lib/posix.specs" -D_GNU_SOURCE -std=gnu++20 -O2 -g0 \
	-I"$OC/include/opencascade" -I"$OUT/planegcs/include" \
	"$CAO/test/caotest.cpp" -o "$O/caotest" \
	-L"$OCL" -L"$OUT/planegcs/lib" -Wl,--gc-sections -Wl,--start-group $LIBS -lplanegcs -Wl,--end-group -lpthread
${TC}strip "$O/caotest"
echo "build-test : $O/caotest ($(du -k "$O/caotest" | cut -f1) Ko) en $(( $(date +%s) - t0 )) s"
