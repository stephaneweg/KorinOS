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
if [ "$ARCH" = x86_64 ]; then
	# Korin PC : la libposix du PC (ARCH=x86_64 de tools/posix/build.sh), et ses specs telles quelles (-shared,
	# -export-dynamic, *cc1plus : le g++ de x86_64-*-elf ne passe pas %(cpp) a cc1plus), __onyx__ en plus
	ARCH=x86_64 NO_TESTS=1 SYSROOT=$P OUT=$WK_OUT/posix-obj sh "$ARMPI/tools/posix/build.sh"
	mkdir -p "$ONYX_SYSROOT/lib/pkgconfig" "$ONYX_SYSROOT/include"
	cp -r "$P/include/." "$ONYX_SYSROOT/include/"
	cp "$P/lib/libposix.a" "$P/lib/crt0posix.o" "$P/lib/posix.ld" "$P/lib/dlhost.ld" "$P/lib/dlhost-cxx.ld" 		"$P/lib/dlmod.o" "$P/lib/libgcc_mod.a" "$ONYX_SYSROOT/lib/"
	L=$ONYX_SYSROOT/lib
	D="-isystem $ONYX_SYSROOT/include -DFD_SETSIZE=1024 -D__unix__=1 -D__unix=1 -D__onyx__=1 -D__armpi__=1 %{pthread:-D_REENTRANT}"
	printf '*cpp:
+ %s

*cc1plus:
+ %s

' "$D" "$D" > "$L/onyx-cc.specs"
	sed -e "s|$P/lib|$L|g" -e "s|$P/include|$ONYX_SYSROOT/include|g" -e 's|-D__armpi__=1|-D__onyx__=1 -D__armpi__=1|' 		"$P/lib/posix.specs" > "$L/onyx.specs"
	grep -q __onyx__ "$L/onyx.specs" && grep -q cc1plus "$L/onyx.specs" || { echo "sysroot.sh : onyx.specs du PC rate" >&2; exit 1; }
	echo "sysroot: $ONYX_SYSROOT (x86_64)"
	ls -l "$L/libposix.a"
	exit 0
fi
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
