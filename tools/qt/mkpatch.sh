#!/bin/sh
# mkpatch.sh : les retouches de Korin aux modules de Qt. La source de chaque module est tenue sous git (commit
# « pristine » à la première fois) :
#   sh tools/qt/mkpatch.sh save  <module>   : git diff de la source -> tools/qt/patches/<module>.patch
#   sh tools/qt/mkpatch.sh apply <module>   : applique la retouche si la source ne l'a pas encore
# (la QPA korin, src/plugins/platforms/korin, et le style korin, src/plugins/styles/korin, ne sont pas dans la
# retouche : build.sh les recopie de tools/qt/qpa et tools/qt/style)
set -e
. "$(dirname "$0")/env.sh"
op=$1; m=$2
s=$(qt_src $m)
p=$QTT/patches/$m.patch
cd "$s"
if [ ! -d .git ]; then
	git init -q && git add -A && git -c user.name=korin -c user.email=korin@localhost commit -qm pristine
fi
case $op in
save)
	mkdir -p "$QTT/patches"
	git add -A -- . ':!src/plugins/platforms/korin' ':!src/plugins/printsupport/korin' ':!src/plugins/styles/korin'
	git diff --cached --binary HEAD > "$p.tmp"
	git reset -q
	mv "$p.tmp" "$p"; echo "$p : $(grep -c '^diff --git' "$p") fichiers" ;;
apply)
	[ -f "$p" ] || exit 0
	if git diff --quiet HEAD -- . ':!src/plugins/platforms/korin' ':!src/plugins/printsupport/korin' ':!src/plugins/styles/korin' && [ -z "$(git ls-files -o --exclude-standard -- . ':!src/plugins/platforms/korin' ':!src/plugins/printsupport/korin' ':!src/plugins/styles/korin')" ]; then
		git apply "$p" && echo "$m : retouche appliquée"
	fi ;;
esac
