#!/bin/bash
# spotify : librespot 0.7.1 pour Korin PC, en bibliothèque C (src/lib/korinspot ; docs/etude/MUSIQUE.md §1, voie B).
# Dans WSL, depuis la racine du dépôt, après tools/rust/build.sh (cible x86_64-unknown-korin) et OpenSSL du PC
# (tools/pc/thirdparty.sh openssl) :
#
#     bash tools/spotify/build.sh          -> build/pc/spotify/libkorinspot.a (et korinspot.h)
#
# Travail dans ~/korin-spotify (KORIN_SPOTIFY) : le projet (copie de src/lib/korinspot), vendor/ (les caisses du
# registre qui ne connaissaient pas « korin », avec tools/spotify/patches/*.patch : socket2, mio, tokio, getrandom 0.2
# et 0.4, open, tempfile). mio prend son repli poll(2) et son réveil par tube (--cfg mio_unsupported_force_*).
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
W=${KORIN_SPOTIFY:-$HOME/korin-spotify}
OUT=$ROOT/build/pc/spotify
. "${KORIN_RUST_ENV:-$HOME/.cache/korin-rust-x86_64/env.sh}"
OSSL=$ROOT/build/pc/thirdparty/openssl
[ -f "$OSSL/lib/libssl.a" ] || { echo "spotify : pas d'OpenSSL du PC (tools/pc/thirdparty.sh openssl)" >&2; exit 1; }
mkdir -p "$W/vendor" "$W/korinspot" "$OUT"
rm -rf "$W/korinspot/src"
cp -r "$ROOT/src/lib/korinspot/src" "$ROOT/src/lib/korinspot/Cargo.toml" "$ROOT/src/lib/korinspot/Cargo.lock" "$W/korinspot/"
cd "$W/korinspot"
cargo fetch --target x86_64-unknown-linux-gnu >/dev/null 2>&1 || true	# (le registre : les sources à patcher)
REG=$(ls -d "$CARGO_HOME"/registry/src/index.crates.io-* | head -1)
for p in "$ROOT"/tools/spotify/patches/*.patch; do
	c=$(basename "$p" .patch)
	if [ ! -f "$W/vendor/$c/.korin" ] || [ "$p" -nt "$W/vendor/$c/.korin" ]; then
		[ -d "$REG/$c" ] || { echo "spotify : $c absent du registre (cargo fetch)" >&2; exit 1; }
		rm -rf "$W/vendor/$c"
		cp -r "$REG/$c" "$W/vendor/$c"
		(cd "$W/vendor/$c" && patch -p1 -s --no-backup-if-mismatch < "$p")
		touch "$W/vendor/$c/.korin"
	fi
done
export OPENSSL_DIR=$OSSL OPENSSL_STATIC=1
export RUSTFLAGS="${RUSTFLAGS:-} --cfg mio_unsupported_force_poll_poll --cfg mio_unsupported_force_waker_pipe"
cargo korin build --release --locked
cp target/x86_64-unknown-korin/release/libkorinspot.a "$OUT/"
cp "$ROOT/src/lib/korinspot/korinspot.h" "$OUT/"
echo "spotify : $OUT/libkorinspot.a ($(stat -c %s "$OUT/libkorinspot.a") octets)"
