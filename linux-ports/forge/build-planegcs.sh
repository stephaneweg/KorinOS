#!/bin/sh
# build-planegcs.sh : PlaneGCS (le solveur d'esquisses de FreeCAD) pour Linux, dans linux-ports/build/cao, par le
# script de Korin (tools/cao/build-planegcs.sh, HOST=1) : les sources de FreeCAD (version et SHA-256 de
# tools/cao/env.sh, le seul dossier planegcs extrait), l'Eigen du système (eigen3-devel, la même version que Korin).
# Appelé par le Makefile (bin/forge) ; rien n'est copié dans le dépôt.
#     sh forge/build-planegcs.sh       -> build/cao/host/planegcs/{lib/libplanegcs.a,include/planegcs}
set -eu
P=$(cd "$(dirname "$0")/.." && pwd)
export KORIN_CAO="$P/build/cao"
. "$P/../tools/cao/env.sh"
F=$DL/freecad-$FREECAD_VER.tar.gz
if [ ! -f "$F" ] || [ "$(sha256sum "$F" | cut -d' ' -f1)" != "$FREECAD_SHA256" ]; then
	curl -fsSL --retry 3 -o "$F.part" "https://github.com/FreeCAD/FreeCAD/archive/refs/tags/$FREECAD_VER.tar.gz"
	[ "$(sha256sum "$F.part" | cut -d' ' -f1)" = "$FREECAD_SHA256" ] || { rm -f "$F.part"; echo "build-planegcs : sha256 faux" >&2; exit 1; }
	mv "$F.part" "$F"
fi
[ -d "$PGCS_SRC" ] || (cd "$SRC" && tar xzf "$F" "FreeCAD-$FREECAD_VER/src/Mod/Sketcher/App/planegcs")
# l'Eigen du système à la place de l'archive (en-têtes seuls)
E=/usr/include/eigen3
[ -f "$E/Eigen/Core" ] || { echo "build-planegcs : Eigen absent (sudo dnf install eigen3-devel)" >&2; exit 1; }
mkdir -p "$EIGEN_SRC"
[ -e "$EIGEN_SRC/Eigen" ] || ln -s "$E/Eigen" "$EIGEN_SRC/Eigen"
[ -e "$EIGEN_SRC/unsupported" ] || ln -s "$E/unsupported" "$EIGEN_SRC/unsupported"
HOST=1 sh "$P/../tools/cao/build-planegcs.sh"
