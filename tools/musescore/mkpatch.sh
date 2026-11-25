#!/bin/sh
# mkpatch.sh : la retouche de Korin à MuseScore (tools/musescore/patches/musescore.patch), depuis la source sous git ;
# les fichiers nouveaux de Korin (tools/musescore/korin, recopiés par build.sh) n'y sont pas.
#   sh tools/musescore/mkpatch.sh save     sh tools/musescore/mkpatch.sh apply
set -e
. "$(dirname "$0")/env.sh"
p=$MST/patches/musescore.patch
cd "$MSRC"
EXCL=$(sed -n 's/^\([^ ]*\) .*/:!\1/p' "$MST/korin/FILES")
case $1 in
save)
	mkdir -p "$MST/patches"
	git add -A -- . $EXCL
	git diff --cached --binary HEAD > "$p"
	git reset -q
	echo "$p : $(grep -c '^diff --git' "$p") fichiers" ;;
apply)
	[ -f "$p" ] || exit 0
	if git diff --quiet HEAD -- . $EXCL; then git apply "$p" && echo "musescore : retouche appliquée"; fi ;;
esac
