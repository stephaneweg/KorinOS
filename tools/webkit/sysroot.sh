#!/bin/sh
# sysroot.sh -- le sysroot du portage de WebKit (docs/WEBKIT.md) : libposix (tools/posix/build.sh,
# construite a part, sans les essais) recopiee dans $ONYX_SYSROOT (tools/webkit/env.sh), avec deux
# fichiers de specs aux noms d'Onyx (les scripts et la serie de patchs les attendent) :
#   lib/onyx.specs      edition de liens : crt0posix.o, libposix.a, posix.ld, newlib
#   lib/onyx-cc.specs   preprocesseur : __unix__, __onyx__ (WTF_OS_ONYX), __armpi__, -isystem du sysroot
# A relancer apres toute modification de src/lib/posix (les dependances deja construites restent).
#
#   sh tools/webkit/sysroot.sh
set -e
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
P=$WK_OUT/posix
NO_TESTS=1 SYSROOT=$P OUT=$WK_OUT/posix-obj sh "$ARMPI/tools/posix/build.sh"
mkdir -p "$ONYX_SYSROOT/lib/pkgconfig" "$ONYX_SYSROOT/include"
cp -r "$P/include/." "$ONYX_SYSROOT/include/"
cp "$P/lib/libposix.a" "$P/lib/crt0posix.o" "$P/lib/posix.ld" "$ONYX_SYSROOT/lib/"
L=$ONYX_SYSROOT/lib
cat > "$L/onyx-cc.specs" <<EOF
*cpp:
+ -isystem $ONYX_SYSROOT/include -DFD_SETSIZE=1024 -D__unix__=1 -D__unix=1 -D__onyx__=1 -D__armpi__=1 %{pthread:-D_REENTRANT}

EOF
cat > "$L/onyx.specs" <<EOF
%rename link posix_orig_link
%rename lib posix_orig_lib

*startfile:
$L/crt0posix.o $L/libposix.a

*endfile:


*link:
-T $L/posix.ld -z max-page-size=0x1000 --build-id=none --gc-sections --eh-frame-hdr -u __cxa_guard_acquire -u __posix_pthread_anchor -u abort %(posix_orig_link)

*lib:
--start-group $L/libposix.a -lc -lm --end-group %(posix_orig_lib)

*cpp:
+ -isystem $ONYX_SYSROOT/include -DFD_SETSIZE=1024 -D__unix__=1 -D__unix=1 -D__onyx__=1 -D__armpi__=1 %{pthread:-D_REENTRANT}

EOF
echo "sysroot: $ONYX_SYSROOT"
ls -l "$L/libposix.a"
