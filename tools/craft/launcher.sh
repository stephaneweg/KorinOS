#!/bin/bash
# launcher : Craft (/apps/craft.app), le lanceur de la suite Craft sur Korin (src/apps/craft, docs/CRAFT.md), pour le PC.
# Dans WSL, depuis la racine du dépôt, après ARCH=x86_64 bash tools/rust/build.sh et tools/pc/apps.sh (libuikit.a) :
#     bash tools/craft/launcher.sh    -> build/craft/x86_64/apps/craft.app (main, app.txt, icon.rgba, icons/<app>.png)
# Les icônes de la suite (128 px, MIT ou Apache-2.0) viennent du lanceur de storytold (github.com/storytold/craft-launcher,
# assets/app-icons), à la révision de LAUNCHER_REV ; l'icône de Craft est celle d'ArtCraft. Chaque app de la suite déjà
# construite (build/craft/x86_64/apps/<paquet>.app) reçoit aussi son icon.rgba.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
LAUNCHER_REV=bbb3844954d90e64c70a89ea493d0e7270478553
K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
W=${KORIN_CRAFT:-$HOME/korin-craft}
[ -f "$K/env.sh" ] || { echo "launcher : pas de chaîne Rust du PC (ARCH=x86_64 bash tools/rust/build.sh)" >&2; exit 1; }
. "$K/env.sh"
L=$W/craft-launcher
[ -d "$L/.git" ] || git clone -q https://github.com/storytold/craft-launcher.git "$L"
(cd "$L" && { git cat-file -e "$LAUNCHER_REV^{commit}" 2>/dev/null || git fetch -q origin; } && git checkout -q --detach "$LAUNCHER_REV")
# le rendu par le CPU avec le correctif de Korin, comme tools/craft/egui.sh
C=hdrshot-egui-backend-0.2.0
P=$ROOT/tools/craft/patches/$C.patch
if [ ! -f "$W/vendor/$C/.korin" ] || [ "$P" -nt "$W/vendor/$C/.korin" ]; then
	mkdir -p "$W/vendor"
	rm -rf "$W/vendor/$C"
	curl -sSfL "https://static.crates.io/crates/hdrshot-egui-backend/$C.crate" | tar xz -C "$W/vendor"
	(cd "$W/vendor/$C" && patch -p1 -s --no-backup-if-mismatch < "$P")
	touch "$W/vendor/$C/.korin"
fi
export CARGO_TARGET_DIR=$W/target-launcher
(cd src/apps/craft && cargo korin build --release --config "patch.crates-io.hdrshot-egui-backend.path='$W/vendor/$C'")
A=$ROOT/build/craft/x86_64/apps
D=$A/craft.app
mkdir -p "$D/icons"
"${TC}strip" -o "$D/main" "$CARGO_TARGET_DIR/x86_64-unknown-korin/release/craft"
cp src/apps/craft/app.txt "$D/"
cp "$L"/assets/app-icons/*.png "$D/icons/"
python3 -E -P tools/craft/icon_rgba.py "$L/assets/app-icons/artcraft.png" "$D/icon.rgba"
for d in "$A"/*.app; do
	n=$(basename "$d" .app)
	[ -f "$L/assets/app-icons/$n.png" ] && python3 -E -P tools/craft/icon_rgba.py "$L/assets/app-icons/$n.png" "$d/icon.rgba"
done
echo "launcher : $D ($(stat -c %s "$D/main") octets)"
