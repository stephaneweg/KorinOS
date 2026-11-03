#!/bin/sh
# build-ui.sh -- UIKit depuis le C (docs/WEBKIT.md, etape 5) : l'interface generee depuis
# src/kits/uikit/uikit.abi (tools/kits/abi2c.py : uikit_abi.h, uikit_abi.S, uikit_abi_open.c) et
# compilee dans $WK_OUT/uikit/libuikit_c.a ; l'en-tete a inclure est src/kits/uikit/include/uikit.h
# (-I src/kits/uikit/include -I $WK_OUT/uikit). Puis l'essai uitest (-> $WK_OUT/out/uitest).
#
#   sh tools/webkit/build-ui.sh
#   (sur ARMPI)  uitest          une fenetre UIKit ouverte, dessinee et fermee depuis le C
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
U=$WK_OUT/uikit
mkdir -p "$U" "$WK_OUT/out"
python3 -I "$ARMPI/tools/kits/abi2c.py" "$ARMPI/src/kits/uikit/uikit.abi" "$U" \
	--const "$ARMPI/src/inc/armpi.inc" "$ARMPI/src/inc/ws_ext.inc" "$ARMPI/src/kits/uikit/uikit.inc"
${CC} -mcpu=cortex-a72 -c "$U/uikit_abi.S" -o "$U/uikit_abi.o"
${CC} -mcpu=cortex-a72 -O2 -specs="$ONYX_SYSROOT/lib/onyx-cc.specs" -c "$U/uikit_abi_open.c" -o "$U/uikit_abi_open.o"
rm -f "$U/libuikit_c.a"
${AR} rcs "$U/libuikit_c.a" "$U/uikit_abi.o" "$U/uikit_abi_open.o"
UIF="-I$ARMPI/src/kits/uikit/include -I$U"
${CC} -mcpu=cortex-a72 -O2 -specs="$ONYX_SYSROOT/lib/onyx.specs" $UIF "$ARMPI/tools/webkit/uitest.c" \
	"$U/libuikit_c.a" -o "$U/uitest"
${STRIP} -o "$WK_OUT/out/uitest" "$U/uitest"
ls -l "$U/libuikit_c.a" "$WK_OUT/out/uitest"
