#!/bin/sh
# build.sh -- Godot pour Korin PC (x86-64), dans WSL, depuis la racine du depot (docs/GODOT.md).
#     sh tools/godot/build.sh                   l'editeur (target=editor)
#     sh tools/godot/build.sh template_release  le modele d'export d'un jeu (et template_debug)
# Avant : sh tools/godot/fetch.sh ; la chaine x86_64-onyx-elf (tools/pc/toolchain.sh) ; ARCH=x86_64 sh
# tools/posix/build.sh ; sh tools/pc/apps.sh (libuikit.a) ; Mesa du PC (tools/mesa/build-iris.sh : libglkit.a).
# Produit build/pc/godot/sdroot/apps/godot.app/main (l'editeur) ou build/pc/godot/templates/<cible>.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TARGET=${1:-editor}
W=${KORIN_GODOT:-$HOME/korin-godot}
S=$W/src
TOOL=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2
OUT=$ROOT/build/pc/godot
JOBS=${JOBS:-$(nproc)}

export KORIN_TC=$TOOL/bin/x86_64-onyx-elf-
export KORIN_POSIX=$ROOT/build/pc/posix
export KORIN_KITS=$ROOT/build/pc/gcc
export KORIN_GL=$ROOT/build/pc/mesa/lib/libglkit.a
export KORIN_ROOT=$ROOT

[ -d "$S/.git" ] || { echo "build.sh : d'abord sh tools/godot/fetch.sh" >&2; exit 1; }
[ -x "${KORIN_TC}g++" ] || { echo "build.sh : manque la chaine $TOOL (tools/pc/toolchain.sh)" >&2; exit 1; }
[ -f "$KORIN_POSIX/lib/posix.specs" ] || { echo "build.sh : d'abord ARCH=x86_64 sh tools/posix/build.sh" >&2; exit 1; }
[ -f "$KORIN_KITS/lib/libuikit.a" ] || { echo "build.sh : d'abord sh tools/pc/apps.sh (libuikit.a)" >&2; exit 1; }
[ -f "$KORIN_GL" ] || { echo "build.sh : manque $KORIN_GL (tools/mesa/build-iris.sh)" >&2; exit 1; }

# les seules retouches des sources de Godot (tools/godot/korin.patch : un nom de manifold, _S, est une macro de
# ctype.h dans newlib)
git -C "$S" apply --check -R "$ROOT/tools/godot/korin.patch" 2>/dev/null || git -C "$S" apply "$ROOT/tools/godot/korin.patch"
# la plateforme vient du depot : copiee dans l'arbre de Godot (pas de lien : SCons et /mnt/c)
mkdir -p "$S/platform/korin"
cp "$ROOT"/tools/godot/platform/korin/* "$S/platform/korin/"
command -v scons >/dev/null || { echo "build.sh : scons manque (apt-get install scons)" >&2; exit 1; }

cd "$S"
scons ${SCONS_FLAGS:-} -j"$JOBS" platform=korin arch=x86_64 target="$TARGET" production=yes debug_symbols=no \
	> "$W/scons-$TARGET.log" 2>&1 || { grep -E "error|Error" "$W/scons-$TARGET.log" | head -30; exit 1; }

BIN=$(ls -t bin/godot.korin.*"$TARGET"*.x86_64 2>/dev/null | head -1)
[ -n "$BIN" ] || { echo "build.sh : pas de binaire dans $S/bin" >&2; exit 1; }
if [ "$TARGET" = editor ]; then
	D=$OUT/sdroot/apps/godot.app
	mkdir -p "$D"
	"${KORIN_TC}strip" -o "$D/main" "$BIN"
	cp "$ROOT/tools/godot/app.txt" "$D/app.txt"
	ls -l "$D/main"
else
	mkdir -p "$OUT/templates"
	"${KORIN_TC}strip" -o "$OUT/templates/$TARGET.x86_64" "$BIN"
	ls -l "$OUT/templates/$TARGET.x86_64"
fi
