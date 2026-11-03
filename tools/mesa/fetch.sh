#!/bin/sh
# fetch : la source de Mesa et les outils de construction (meson, mako) dans ~/armpi-mesa, puis les
# retouches d'ARMPI (armpi.patch).
#     sh tools/mesa/fetch.sh        (depuis la racine du depot, dans WSL)
set -eu
. "$(dirname "$0")/env.sh"
[ -d "$SRC/.git" ] || git -c advice.detachedHead=false clone -q --depth 1 --branch $MESA_TAG \
	https://gitlab.freedesktop.org/mesa/mesa.git "$SRC"
[ -x "$A/venv/bin/meson" ] || {
	python3 -m venv "$A/venv"
	"$A/venv/bin/pip" -q install meson==1.12.1 mako==1.4.3 markupsafe==3.0.4 pyyaml==6.0.3 packaging==26.3 ply==3.11
}
if [ -f "$ARMPI_ROOT/tools/mesa/armpi.patch" ]; then
	git -C "$SRC" checkout -q .
	git -C "$SRC" apply "$ARMPI_ROOT/tools/mesa/armpi.patch"
fi
