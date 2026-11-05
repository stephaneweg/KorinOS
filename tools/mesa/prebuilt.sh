#!/bin/sh
# prebuilt.sh -- Mesa construite, dans le depot (prebuilt/mesa) : les apps qui l'utilisent (SDL, SuperTux,
# SuperTuxKart, n3dsemu, l'hote d'Avalonia, Jet) se lient sans refaire Mesa. Appele a la fin de
# build-armgl.sh ; a relancer seul apres build-libs.sh si armgl n'a pas change.
#     sh tools/mesa/prebuilt.sh        (dans WSL, depuis la racine du depot)
#
# prebuilt/mesa/lib/libmesa_v3d.a  les 30 archives de Mesa (minces chez meson) en une archive pleine
#                   libarmgl.a     winsys et frontal (build-armgl.sh)
#                   libz.a         la zlib dont Mesa a besoin (celle de la sysroot de WebKit)
#               include/           EGL, GLES, GLES2, GLES3, KHR de Mesa
#               VERSION            tag de Mesa, empreintes des patchs et d'armgl
# Lier : -Wl,--start-group libarmgl.a libmesa_v3d.a libz.a -Wl,--end-group
set -eu
. "$(dirname "$0")/env.sh"
P=$ARMPI_ROOT/prebuilt/mesa
G=$OUT/armgl
[ -f "$G/libarmgl.a" ] && [ -f "$G/libs.txt" ] || { echo "prebuilt.sh : lancer d'abord build-armgl.sh" >&2; exit 1; }
mkdir -p "$P/lib" "$P/include"
T=$(mktemp -d)
{ echo "create $T/libmesa_v3d.a"; for a in $(cat "$G/libs.txt"); do echo "addlib $a"; done; echo save; echo end; } | ${TC}ar -M
${TC}strip --strip-debug -D -o "$P/lib/libmesa_v3d.a" "$T/libmesa_v3d.a"
${TC}strip --strip-debug -D -o "$P/lib/libarmgl.a" "$G/libarmgl.a"
cp "$WK/lib/libz.a" "$P/lib/libz.a"
rm -rf "$T"
for d in EGL GLES GLES2 GLES3 KHR; do rm -rf "$P/include/$d"; cp -r "$SRC/include/$d" "$P/include/$d"; done
M=$ARMPI_ROOT/tools/mesa
{
	echo "mesa      $MESA_TAG"
	echo "armpi.patch $(sha256sum < "$M/armpi.patch" | cut -c1-16)"
	echo "armgl     $(cat "$M"/armgl/*.c "$M"/armgl/*.h "$M"/libdrm/*.c | sha256sum | cut -c1-16)"
} > "$P/VERSION"
ls -l "$P/lib"
cat "$P/VERSION"
