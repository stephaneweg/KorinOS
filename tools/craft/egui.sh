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
export CARGO_TARGET_DIR=${KORIN_CRAFT:-$HOME/korin-craft}/target-egui
(cd src/lib/egui-korin && cargo korin build --release --example demo)
D=$ROOT/build/craft/x86_64/apps/eguidemo.app
mkdir -p "$D"
"${TC}strip" -o "$D/main" "$CARGO_TARGET_DIR/x86_64-unknown-korin/release/examples/demo"
cp src/lib/egui-korin/demo/app.txt "$D/"
echo "egui : $D/main ($(stat -c %s "$D/main") octets)"
