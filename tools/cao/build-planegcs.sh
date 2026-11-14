#!/bin/sh
# build-planegcs.sh : PlaneGCS, le solveur d'esquisses de FreeCAD (src/Mod/Sketcher/App/planegcs), en
# bibliothèque statique à part pour Korin : ~/korin-cao/$ARCH/planegcs/{include/planegcs,include/Eigen…,lib}.
# Ni FreeCAD ni Boost : les quelques en-têtes qu'il en prend sont remplacés par tools/cao/planegcs/compat
# (journal, et Boost.Graph réduit à connected_components) ; <cassert> et <cmath>, que FreeCAD donne par
# PreCompiled.h, par -include. Eigen n'a que des en-têtes : copiés à côté.
#     sh tools/cao/build-planegcs.sh          (ARCH=aarch64 : pour le Pi ; HOST=1 : pour Linux, essai rapide)
set -eu
. "$(dirname "$0")/env.sh"
[ -d "$PGCS_SRC" ] || { echo "build-planegcs : d'abord sh tools/cao/fetch.sh" >&2; exit 1; }
C=$CAO/planegcs/compat
if [ "${HOST:-0}" = 1 ]; then
	CXX=g++ AR=ar FLAGS="" O=$K/host/obj/planegcs I=$K/host/planegcs
else
	[ -f "$POSIX/lib/posix.specs" ] || { echo "build-planegcs : pas de libposix dans $POSIX" >&2; exit 1; }
	CXX=${TC}g++ AR=${TC}ar FLAGS="$MCPU -specs=$POSIX/lib/posix.specs -D_GNU_SOURCE -ffunction-sections -fdata-sections"
	O=$OUT/obj/planegcs I=$OUT/planegcs
fi
rm -rf "$O" "$I"
mkdir -p "$O" "$I/lib" "$I/include/planegcs"
# « ../../SketcherGlobal.h » des sources : à sa place dans l'arbre de FreeCAD (hors du dépôt)
cp "$C/SketcherGlobal.h" "$PGCS_SRC/../../SketcherGlobal.h"
CXXFLAGS="$FLAGS -std=gnu++20 -O2 -g0 -include cassert -include cmath -DEIGEN_NO_DEBUG -DNDEBUG -I$C -I$EIGEN_SRC -Wno-deprecated-declarations"
t0=$(date +%s)
pids=""
for f in GCS Constraints Geo SubSystem qp_eq; do
	# shellcheck disable=SC2086
	$CXX $CXXFLAGS -c "$PGCS_SRC/$f.cpp" -o "$O/$f.o" 2> "$O/$f.log" &
	pids="$pids $!"
done
st=0
for p in $pids; do wait "$p" || st=1; done
[ $st = 0 ] || { cat "$O"/*.log | grep -A6 error | head -60; exit 1; }
$AR rcs "$I/lib/libplanegcs.a" "$O"/*.o
# les en-têtes, avec SketcherGlobal.h à côté d'eux et sans l'include inutile de Boost ; Eigen (en-têtes seuls) dans include/
for h in GCS Constraints Geo SubSystem Util qp_eq; do
	sed -e 's#"\.\./\.\./SketcherGlobal\.h"#"SketcherGlobal.h"#' -e '/boost\/math\/constants/d' "$PGCS_SRC/$h.h" > "$I/include/planegcs/$h.h"
done
cp "$C/SketcherGlobal.h" "$I/include/planegcs/"
cp -r "$EIGEN_SRC/Eigen" "$I/include/"
echo "build-planegcs : $I/lib/libplanegcs.a ($(du -k "$I/lib/libplanegcs.a" | cut -f1) Ko) en $(( $(date +%s) - t0 )) s"
