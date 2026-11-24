#!/bin/sh
# stage.sh : les modules de Qt en paquets de développement <module>-dev (docs/etude/QT.md § 5) pour le GCC de Korin
# PC : tout dans /devtools/qt (lib/*.a, include/, lib/cmake/, mkspecs/, plugins/, qml/, libexec/ et bin/ : les outils
# construits POUR Korin, moc, rcc, uic, qsb, qmlcachegen…), leurs licences dans /devtools/qt/share/doc/<module>.
# Les fichiers d'un module sont ceux de son install_manifest.txt (build.sh l'installe dans $PREFIX).
#
#     sh tools/qt/build.sh                          (avant)
#     sh tools/qt/stage.sh [module...]               -> build/pc/devtools/<module>-dev/
#     python3 tools/pkg/pub.py <module>-dev --new --tree build/pc/devtools/<module>-dev
#
# Korin n'a pas de liens symboliques : ils sont recopiés (cp -L).
set -e
. "$(dirname "$0")/env.sh"
DEST=$ROOT/build/pc/devtools
STAGE=${PREFIX%$QT_KORIN_PREFIX}	# la racine de l'arbre installé ($OUT/stage)
for m in ${*:-$QT_MODULES}; do
	man=$OBJ/$m/install_manifest.txt
	[ -f "$man" ] || { echo "stage.sh : pas de $man (sh tools/qt/build.sh $m)" >&2; exit 1; }
	T=$DEST/$m-dev
	rm -rf "$T"
	n=0
	while IFS= read -r f; do
		case $f in "$STAGE"/*) ;; *) echo "stage.sh : $m : hors de l'arbre : $f" >&2; exit 1 ;; esac
		r=${f#"$STAGE"/}
		mkdir -p "$T/$(dirname "$r")"
		cp -L "$f" "$T/$r"
		n=$((n + 1))
	done < "$man"
	d=$T${QT_KORIN_PREFIX}/share/doc/$m
	mkdir -p "$d"
	s=$(qt_src $m)
	[ -d "$s/LICENSES" ] && cp "$s"/LICENSES/* "$d/"
	echo "$m-dev $QT_VER : $n fichiers, $(du -sk "$T" | cut -f1) Ko"
done
