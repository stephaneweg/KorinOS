#!/bin/bash
# egui : la démo de l'hôte egui de Korin (src/lib/egui-korin, docs/EGUI.md), pour le PC.
# Dans WSL, depuis la racine du dépôt, après ARCH=x86_64 bash tools/rust/build.sh et tools/pc/apps.sh (libuikit.a) :
#     bash tools/craft/egui.sh       -> build/craft/x86_64/apps/eguidemo.app (main, app.txt)
# Le dossier target est hors du dépôt : ~/korin-craft/target-egui (KORIN_CRAFT).
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
[ -f "$K/env.sh" ] || { echo "egui : pas de chaîne Rust du PC (ARCH=x86_64 bash tools/rust/build.sh)" >&2; exit 1; }
[ -f "$ROOT/build/pc/gcc/lib/libuikit.a" ] || { echo "egui : build/pc/gcc/lib/libuikit.a absent (wsl sh tools/pc/apps.sh)" >&2; exit 1; }
. "$K/env.sh"
W=${KORIN_CRAFT:-$HOME/korin-craft}
export CARGO_TARGET_DIR=$W/target-egui
# le rendu par le CPU, avec le correctif de Korin (tools/craft/patches/hdrshot-egui-backend-0.2.0.patch : le cache
# des primitives oublie les textures qui changent), comme pour les apps Craft (tools/craft/build.sh)
C=hdrshot-egui-backend-0.2.0
P=$ROOT/tools/craft/patches/$C.patch
if [ ! -f "$W/vendor/$C/.korin" ] || [ "$P" -nt "$W/vendor/$C/.korin" ]; then
	mkdir -p "$W/vendor"
	rm -rf "$W/vendor/$C"
	curl -sSfL "https://static.crates.io/crates/hdrshot-egui-backend/$C.crate" | tar xz -C "$W/vendor"
	(cd "$W/vendor/$C" && patch -p1 -s --no-backup-if-mismatch < "$P")
	touch "$W/vendor/$C/.korin"
fi
(cd src/lib/egui-korin && cargo korin build --release --example demo 	--config "patch.crates-io.hdrshot-egui-backend.path='$W/vendor/$C'")
D=$ROOT/build/craft/x86_64/apps/eguidemo.app
mkdir -p "$D"
"${TC}strip" -o "$D/main" "$CARGO_TARGET_DIR/x86_64-unknown-korin/release/examples/demo"
cp src/lib/egui-korin/demo/app.txt "$D/"
echo "egui : $D/main ($(stat -c %s "$D/main") octets)"
