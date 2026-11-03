#!/bin/sh
# fetch.sh -- la copie de travail de WebKit dans WSL ($WEBKIT_DIR, defaut ~/armpi-webkit-build/webkit) :
# recopiee de src/extra/webkit (WebKit d'Onyx, serie de patchs d'Onyx deja appliquee, revision dans
# ONYX-REVISION.txt), puis les patchs propres a ARMPI de tools/webkit/patches/*.patch s'il y en a
# (appliques une fois, notes dans .armpi-patches). Le disque de WSL : des milliers de fichiers lus a
# chaque construction, bien plus vite que sur /mnt/c.
#
#   sh tools/webkit/fetch.sh            # ne fait rien si la copie existe
#   FORCE=1 sh tools/webkit/fetch.sh    # la refait
set -eu
WK_ENV_SELF=$0
. "$(dirname "$0")/env.sh"
[ -f "$EXTRA/webkit/Source/cmake/OptionsOnyx.cmake" ] || { echo "fetch.sh : pas de WebKit dans $EXTRA/webkit (src/extra/README.md)" >&2; exit 1; }
if [ "${FORCE:-0}" = 1 ]; then rm -rf "$WEBKIT_DIR"; fi
if [ ! -f "$WEBKIT_DIR/Source/cmake/OptionsOnyx.cmake" ]; then
	echo "fetch.sh : copie de $EXTRA/webkit vers $WEBKIT_DIR"
	mkdir -p "$WEBKIT_DIR"
	(cd "$EXTRA/webkit" && tar cf - .) | (cd "$WEBKIT_DIR" && tar xf -)
fi
for p in "$ARMPI"/tools/webkit/patches/*.patch; do
	[ -f "$p" ] || continue
	n=$(basename "$p")
	grep -qx "$n" "$WEBKIT_DIR/.armpi-patches" 2>/dev/null && continue
	echo "fetch.sh : $n"
	patch -d "$WEBKIT_DIR" -p1 --forward < "$p"
	echo "$n" >> "$WEBKIT_DIR/.armpi-patches"
done
