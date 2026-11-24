#!/bin/sh
# mkpatch.sh : tire tools/blender/patches/blender.patch des retouches faites à la main dans la source de Blender
# ($BSRC, sous git : le premier appel y fait un dépôt avec la source d'origine).
#
#   sh tools/blender/mkpatch.sh init     (une fois, sur une source d'origine sans retouche)
#   … retoucher les fichiers de $BSRC …
#   sh tools/blender/mkpatch.sh          (écrit blender.patch et la marque .korin-patch, que build.sh compare)
set -e
. "$(dirname "$0")/env.sh"
cd "$BSRC"
if [ "$1" = init ]; then
	[ -d .git ] && { echo "mkpatch.sh : $BSRC est déjà sous git" >&2; exit 1; }
	[ -f .korin-patch ] && { echo "mkpatch.sh : la source a déjà une retouche : refaire la source" >&2; exit 1; }
	git init -q && git add -A && git -c user.name=korin -c user.email=korin@localhost commit -q -m "Blender $BLENDER_VER"
	echo "mkpatch.sh : $BSRC sous git"
	exit 0
fi
[ -d .git ] || { echo "mkpatch.sh : d'abord sh tools/blender/mkpatch.sh init" >&2; exit 1; }
git diff --no-color -- . ':(exclude).korin-patch' > "$BL/patches/blender.patch"
cp "$BL/patches/blender.patch" .korin-patch
git diff --stat -- . ':(exclude).korin-patch' | tail -1
