#!/bin/sh
# glue.sh -- l'arborescence « sources d'Onyx » que la serie de patchs de WebKit attend
# (ONYX_SOURCE_DIR : user/Libs/gpucomp/gpucomp.h, user/Libs/av/av.h), faite de liens vers
# src/extra/onyx-libs (copies de user/Libs/gpucomp et user/Libs/av d'Onyx) : $WK_OUT/glue.
# Sur ARMPI, gpucomp (le compositeur sur le V3D d'Onyx) est lie tel quel, ses appels au GPU d'Onyx
# faits par GpuKit (tools/webkit/glue/gpucomp_armpi.c, build-jet.sh), et la sortie son
# de av par AudioKit (tools/webkit/glue/av_sound_armpi.c).
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
L=$EXTRA/onyx-libs
[ -f "$L/gpucomp/gpucomp.h" ] && [ -f "$L/av/av.h" ] || { echo "glue.sh : pas de $L/{gpucomp,av} (src/extra/README.md)" >&2; exit 1; }
mkdir -p "$WK_OUT/glue/user/Libs"
ln -sfn "$L/gpucomp" "$WK_OUT/glue/user/Libs/gpucomp"
ln -sfn "$L/av" "$WK_OUT/glue/user/Libs/av"
