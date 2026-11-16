#!/bin/sh
# fetch-angle : seulement la source d'ANGLE (WebKit amont, la révision de la copie d'Onyx) et le correctif d'ARMPI,
# dans ~/armpi-mesa/src/angle-wk, que tools/mesa/build-webkit.sh copie dans WebKit. build-angle.sh fait la même
# chose puis construit ANGLE pour le Pi ; le PC (ARCH=x86_64) n'a besoin que de la source.
set -e
. "$(dirname "$0")/env.sh"
REV=b8a7a626127c0010a557c9d6466fefd38d9477c1
W=$A/src/angle-wk
S=$W/Source/ThirdParty/ANGLE
if [ ! -d "$S/src" ]; then
	mkdir -p "$W"
	git -C "$W" init -q .
	git -C "$W" remote add origin https://github.com/WebKit/WebKit.git 2>/dev/null || true
	git -C "$W" sparse-checkout set Source/ThirdParty/ANGLE
	git -C "$W" fetch -q --depth 1 --filter=blob:none origin $REV
	git -C "$W" -c advice.detachedHead=false checkout -q FETCH_HEAD
fi
git -C "$W" checkout -q .
git -C "$W" apply "$ARMPI_ROOT/tools/mesa/angle/armpi.patch"
echo "fetch-angle : $S"
