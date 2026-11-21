#!/bin/bash
# KorinStudio pour Korin PC (src/apps/korinstudio, docs/KORINSTUDIO.md), dans WSL ou le cloud, depuis la racine du dépôt,
# après ARCH=x86_64 bash tools/rust/build.sh (la chaîne Rust du PC) et tools/pc/apps.sh (libuikit.a) :
#     bash tools/korinstudio/build.sh     -> build/korinstudio/x86_64/apps/korinstudio.app (main, app.txt, icon.rgba)
# Sous Korin, la fenêtre est celle d'egui-korin (sans eframe, main.rs) ; le rendu par le
# CPU prend le correctif de Korin (tools/craft/patches/hdrshot-egui-backend-0.2.0.patch), comme la démo d'egui.
# Le dossier target est hors du dépôt : ~/korin-craft/target-korinstudio (KORIN_CRAFT). Sur l'hôte (Linux, Windows) :
# cargo run --release dans src/apps/korinstudio.
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
[ -f "$K/env.sh" ] || { echo "korinstudio : pas de chaîne Rust du PC (ARCH=x86_64 bash tools/rust/build.sh)" >&2; exit 1; }
[ -f "$ROOT/build/pc/gcc/lib/libuikit.a" ] || { echo "korinstudio : build/pc/gcc/lib/libuikit.a absent (wsl sh tools/pc/apps.sh)" >&2; exit 1; }
. "$K/env.sh"
W=${KORIN_CRAFT:-$HOME/korin-craft}
export CARGO_TARGET_DIR=$W/target-korinstudio
C=hdrshot-egui-backend-0.2.0
P=$ROOT/tools/craft/patches/$C.patch
if [ ! -f "$W/vendor/$C/.korin" ] || [ "$P" -nt "$W/vendor/$C/.korin" ]; then
	mkdir -p "$W/vendor"
	rm -rf "$W/vendor/$C"
	curl -sSfL "https://static.crates.io/crates/hdrshot-egui-backend/$C.crate" | tar xz -C "$W/vendor"
	(cd "$W/vendor/$C" && patch -p1 -s --no-backup-if-mismatch < "$P")
	touch "$W/vendor/$C/.korin"
fi
# le Cargo.lock du dépôt est celui de l'hôte : celui de Korin (avec les [patch]) vit à côté du dossier target
S=src/apps/korinstudio
cp "$S/Cargo.lock" "$CARGO_TARGET_DIR.host.lock" 2>/dev/null || true
if [ -f "$CARGO_TARGET_DIR.korin.lock" ]; then cp "$CARGO_TARGET_DIR.korin.lock" "$S/Cargo.lock"; else rm -f "$S/Cargo.lock"; fi
set +e
(cd "$S" && cargo korin build --release \
	--config "patch.crates-io.hdrshot-egui-backend.path='$W/vendor/$C'")
rc=$?
set -e
# un [patch] non pris (une autre version dans le Cargo.lock) : le rendu par le CPU sans le correctif de Korin
if grep -A1 '^\[\[patch.unused\]\]' "$S/Cargo.lock" | grep -q hdrshot; then echo "korinstudio : patch non pris" >&2; rc=1; fi
cp "$S/Cargo.lock" "$CARGO_TARGET_DIR.korin.lock"
[ -f "$CARGO_TARGET_DIR.host.lock" ] && cp "$CARGO_TARGET_DIR.host.lock" "$S/Cargo.lock"
[ $rc = 0 ] || exit $rc
D=$ROOT/build/korinstudio/x86_64/apps/korinstudio.app
mkdir -p "$D"
"${TC}strip" -o "$D/main" "$CARGO_TARGET_DIR/x86_64-unknown-korin/release/korinstudio"
cp "$S/app.txt" "$D/"
python3 -E -P "$ROOT/tools/korinstudio/icon.py" "$D/icon.rgba"
echo "korinstudio : $D/main ($(stat -c %s "$D/main") octets)"
