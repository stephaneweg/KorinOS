#!/bin/sh
# fetch.sh : télécharge la source de Blender ($BLENDER_VER) et les archives des dépendances de classe A (env.sh :
# DEPS_A), avec les URL et les sommes de versions.cmake de Blender ; dépaquette le tout dans $SRC.
#
#   sh tools/blender/fetch.sh            (dans WSL, depuis la racine du dépôt)
#   sh tools/blender/fetch.sh NOM…       (seulement ces dépendances, ex. TBB SHADERC)
set -e
. "$(dirname "$0")/env.sh"

get() {	# get <fichier> <url> : télécharge si absent
	[ -s "$DL/$1" ] && return 0
	echo "téléchargement de $1"
	curl -fL --retry 3 -o "$DL/$1.part" "$2"
	mv "$DL/$1.part" "$DL/$1"
}

check() {	# check <fichier> <somme> <type>
	case $3 in
	MD5) s=$(md5sum "$DL/$1" | cut -d' ' -f1) ;;
	SHA256) s=$(sha256sum "$DL/$1" | cut -d' ' -f1) ;;
	*) echo "fetch.sh : type de somme inconnu $3 pour $1" >&2; exit 1 ;;
	esac
	if [ "$s" != "$2" ]; then
		echo "fetch.sh : somme fausse pour $1 ($s au lieu de $2)" >&2
		exit 1
	fi
}

unpack() {	# unpack <fichier> <dossier> : dépaquette l'archive dans $SRC/<dossier> (un seul niveau ôté)
	[ -d "$SRC/$2" ] && return 0
	mkdir -p "$SRC/$2.part"
	tar -xf "$DL/$1" -C "$SRC/$2.part" --strip-components=1
	mv "$SRC/$2.part" "$SRC/$2"
}

# 1. Blender lui-même : la somme vient du fichier .md5sum publié à côté de l'archive (le seul publié).
f=blender-$BLENDER_VER.tar.xz
get "$f" "$BLENDER_URL"
get "$f.md5sum" "$BLENDER_URL.md5sum"
check "$f" "$(cut -d' ' -f1 "$DL/$f.md5sum")" MD5
unpack "$f" "blender-$BLENDER_VER"

# 2. Les dépendances.
names=${*:-$DEPS_A}
list=$(python3 -I "$BL/versions.py" "$BSRC/build_files/build_environment/cmake/versions.cmake" $names)
echo "$list" | while read -r name file url sum type; do
	get "$file" "$url"
	check "$file" "$sum" "$type"
	unpack "$file" "$(echo "$name" | tr 'A-Z' 'a-z')"
done
echo "fetch.sh : sources prêtes dans $SRC"
