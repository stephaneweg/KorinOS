#!/bin/sh
# pymodules.sh : les paquets Python purs que Blender livre avec lui (versions.cmake : attrs, cattrs, requests…), dans
# site-packages de l'arbre installé ; le module d'extensions (bl_pkg) en a besoin dès le démarrage. Des roues
# « py3-none-any » de PyPI : rien à compiler pour Korin. Appelé par build.sh après l'installation.
#
#   sh tools/blender/pymodules.sh ARBRE_INSTALLÉ
set -e
. "$(dirname "$0")/env.sh"
I=${1:?arbre installé}
SP=$I/${BLENDER_VER%.*}/python/lib/python3.13/site-packages
[ -d "$SP" ] || { echo "pymodules.sh : pas de $SP" >&2; exit 1; }
V=$BSRC/build_files/build_environment/cmake/versions.cmake
ver() { sed -n "s/^set($1_VERSION \(.*\))$/\1/p" "$V" | head -1; }
W=$K/wheels
mkdir -p "$W"
for p in "attrs ATTRS" "cattrs CATTRS" "typing_extensions TYPING_EXTENSIONS" "idna IDNA" \
	"charset_normalizer CHARSET_NORMALIZER" "urllib3 URLLIB3" "certifi CERTIFI" "requests REQUESTS"; do
	set -- $p
	v=$(ver "$2")
	[ -n "$v" ] || { echo "pymodules.sh : pas de version pour $2" >&2; exit 1; }
	ls "$W/$1-$v-"*.whl >/dev/null 2>&1 ||
		python3 -m pip download -q --no-deps --only-binary=:all: --python-version 3.13 --platform any \
			--implementation py -d "$W" "$1==$v"
	python3 -c "import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$W/$1-$v-"*.whl "$SP"
	echo "$1 $v"
done
