#!/bin/sh
# fetch.sh : les sources de la CAO dans ~/korin-cao/src, versions et SHA-256 fixés dans env.sh :
# OpenCASCADE (OCCT), Eigen (en-têtes seuls, pour PlaneGCS) et le dossier planegcs de FreeCAD.
#     sh tools/cao/fetch.sh
set -eu
. "$(dirname "$0")/env.sh"
get () {	# url, fichier, sha256
	f=$DL/$2
	if [ ! -f "$f" ] || [ "$(sha256sum "$f" | cut -d' ' -f1)" != "$3" ]; then
		curl -fsSL --retry 3 -o "$f.part" "$1"
		[ "$(sha256sum "$f.part" | cut -d' ' -f1)" = "$3" ] || { rm -f "$f.part"; echo "fetch : sha256 faux : $1" >&2; exit 1; }
		mv "$f.part" "$f"
	fi
}
get "https://github.com/Open-Cascade-SAS/OCCT/archive/refs/tags/$OCCT_TAG.tar.gz" "occt-$OCCT_VER.tar.gz" "$OCCT_SHA256"
get "https://gitlab.com/libeigen/eigen/-/archive/$EIGEN_VER/eigen-$EIGEN_VER.tar.gz" "eigen-$EIGEN_VER.tar.gz" "$EIGEN_SHA256"
get "https://github.com/FreeCAD/FreeCAD/archive/refs/tags/$FREECAD_VER.tar.gz" "freecad-$FREECAD_VER.tar.gz" "$FREECAD_SHA256"
cd "$SRC"
[ -d "$OCCT_SRC" ] || tar xzf "$DL/occt-$OCCT_VER.tar.gz"
[ -d "$EIGEN_SRC" ] || tar xzf "$DL/eigen-$EIGEN_VER.tar.gz"
[ -d "$PGCS_SRC" ] || tar xzf "$DL/freecad-$FREECAD_VER.tar.gz" "FreeCAD-$FREECAD_VER/src/Mod/Sketcher/App/planegcs"
echo "fetch : $OCCT_SRC, $EIGEN_SRC, $PGCS_SRC"
