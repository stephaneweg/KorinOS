#!/bin/bash
# build.sh -- Claude Code pour Korin (src/apps/claudecode : le frontal de la CLI officielle, en Rust sur egui, dans le
# style des apps Craft ; docs/etude/LINUX-HOST.md, jalon 4), pour le PC. Dans WSL ou le cloud, depuis la racine, après
# ARCH=x86_64 bash tools/rust/build.sh et tools/pc/apps.sh (libuikit.a), comme tools/craft/egui.sh :
#     bash tools/claudecode/build.sh      -> build/pc/apps/sdroot/apps/claudecode.app (main, app.txt, icon.rgba)
#     TEST=1 bash tools/claudecode/build.sh   d'abord cargo test sur la machine de construction (les modules sans fenêtre,
#                                             ccui-shared et ccui-core)
# L'hôte de développement Linux (la même interface dans une fenêtre eframe, sur le claude installé) :
#     bash tools/claudecode/dev.sh
# Le dossier target est hors du dépôt : ~/korin-craft/target-claudecode (KORIN_CRAFT).
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
[ -f "$K/env.sh" ] || { echo "claudecode : pas de chaîne Rust du PC (ARCH=x86_64 bash tools/rust/build.sh)" >&2; exit 1; }
[ -f "$ROOT/build/pc/gcc/lib/libuikit.a" ] || { echo "claudecode : build/pc/gcc/lib/libuikit.a absent (sh tools/pc/apps.sh)" >&2; exit 1; }
W=${KORIN_CRAFT:-$HOME/korin-craft}
S=src/apps/claudecode
# Deux verrous : Cargo.lock (celui de l'hôte : cargo test, l'hôte de développement eframe) et Cargo.korin.lock (celui
# de Korin : libc, tokio, mio… patchés). La construction pour Korin prend le second à la place du premier, puis remet
# le premier.
if [ -n "${TEST:-}" ]; then
	# les modules sans fenêtre de l'app et les essais de ccui-shared et ccui-core (crates/README.md)
	(cd $S && CARGO_TARGET_DIR=$W/target-claudecode-host cargo test --workspace --quiet)
fi
. "$K/env.sh"
export CARGO_TARGET_DIR=$W/target-claudecode
# le rendu par le CPU avec le correctif de Korin (tools/craft/patches/hdrshot-egui-backend-0.2.0.patch), comme egui.sh
C=hdrshot-egui-backend-0.2.0
P=$ROOT/tools/craft/patches/$C.patch
if [ ! -f "$W/vendor/$C/.korin" ] || [ "$P" -nt "$W/vendor/$C/.korin" ]; then
	mkdir -p "$W/vendor"
	rm -rf "$W/vendor/$C"
	curl -sSfL "https://static.crates.io/crates/hdrshot-egui-backend/$C.crate" | tar xz -C "$W/vendor"
	(cd "$W/vendor/$C" && patch -p1 -s --no-backup-if-mismatch < "$P")
	touch "$W/vendor/$C/.korin"
fi
# les caisses qui connaissent Korin (tokio, mio, socket2, libc 0.2.999… : celles du paquet rust), refaites quand un
# patch change
V=$W/korin-vendor
if [ ! -f "$V/etc/cargo/config.toml" ] || [ -n "$(find tools/craft/patches tools/rust/korin-vendor.sh -newer "$V/etc/cargo/config.toml" 2>/dev/null)" ]; then
	[ -d "$K/vendor/libc-0.2.191" ] || bash tools/rust/libc-projects.sh "$K" >/dev/null
	OUT=$V PREFIX=$V bash tools/rust/korin-vendor.sh >/dev/null
fi
cp $S/Cargo.lock "$W/claudecode.host.lock"
cp $S/Cargo.korin.lock $S/Cargo.lock
set +e
(cd $S && cargo korin build --release --bin claudecode --config "$V/etc/cargo/config.toml" \
	--config "patch.crates-io.hdrshot-egui-backend.path='$W/vendor/$C'")
rc=$?
set -e
cp $S/Cargo.lock $S/Cargo.korin.lock
cp "$W/claudecode.host.lock" $S/Cargo.lock
[ $rc = 0 ] || exit $rc
D=$ROOT/build/pc/apps/sdroot/apps/claudecode.app
mkdir -p "$D"
"${TC}strip" -o "$D/main" "$CARGO_TARGET_DIR/x86_64-unknown-korin/release/claudecode"
cp src/apps/claudecode/app.txt "$D/"
python3 -E -P tools/claudecode/icon.py "$W/claudecode-icon.png" && python3 -E -P tools/craft/icon_rgba.py "$W/claudecode-icon.png" "$D/icon.rgba"
echo "claudecode : $D/main ($(stat -c %s "$D/main") octets)"
