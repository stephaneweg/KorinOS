#!/bin/bash
# cargo : le gestionnaire de projets de Rust, construit pour Korin (compilation croisée), docs/CARGO.md.
# Le PC d'abord (x86_64) ; ARCH=aarch64 pour le Pi, sans essai. Dans WSL (ou le cloud), depuis la racine du dépôt,
# après ARCH=x86_64 bash tools/rust/build.sh :
#     bash tools/cargo/build.sh            -> build/pc/cargo/cargo (strippé) ; le Pi : build/cargo/cargo
#
# cargo 0.98.0 (celui de Rust 1.97.1, la chaîne de tools/rust), de crates.io (son Cargo.lock), avec :
#   - OpenSSL du système (ARCH=x86_64 sh tools/openssl/build.sh, lancé s'il manque), en statique ;
#   - libcurl (HTTP/2 par nghttp2), libgit2, libssh2, zlib et SQLite compilés de leurs sources par leurs caisses -sys ;
#   - les caisses de crates.io qui ne connaissent pas « korin » : tools/craft/patches (communes avec Craft) et
#     tools/cargo/patches, branchées par le .cargo/config.toml de la source ; la libc 0.2.191 des projets
#     (tools/rust/libc-projects.sh).
# Travail dans ~/korin-cargo (KORIN_CARGO) : cargo-<v>/ (la source, refaite à chaque lancement), vendor/ (les
# caisses patchées), target/ (gardé d'un lancement à l'autre).
set -e
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
ARCH=${ARCH:-x86_64}
CV=0.98.0
CSHA=7bcc52d107e10f9695543bbdc9128c7798abafdd36c6ba5adfb35e0abe87bf6e
W=${KORIN_CARGO:-$HOME/korin-cargo}
case $ARCH in
aarch64)
	K=${KORIN_RUST:-$HOME/.cache/korin-rust}
	TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
	PLIB=$ROOT/build/posix/lib
	SSL=$ROOT/build/openssl
	CPU="-mcpu=cortex-a72"
	OUT=$ROOT/build/cargo ;;
x86_64)
	K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}
	TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
	PLIB=$ROOT/build/pc/posix/lib
	SSL=$ROOT/build/pc/thirdparty/openssl
	CPU=""
	OUT=$ROOT/build/pc/cargo ;;
*)	echo "ARCH : aarch64 ou x86_64" >&2; exit 1 ;;
esac
T=$ARCH-unknown-korin
T_=$(echo "$T" | tr - _)
[ -f "$K/env.sh" ] || { echo "cargo : pas de chaîne Rust dans $K (ARCH=$ARCH bash tools/rust/build.sh)" >&2; exit 1; }
. "$K/env.sh"
mkdir -p "$W/vendor" "$OUT" "$K/dl"

# 1. OpenSSL du système (en-têtes et bibliothèques statiques)
[ -f "$SSL/lib/libssl.a" ] || [ -f "$SSL/lib64/libssl.a" ] || ARCH=$ARCH sh "$ROOT/tools/openssl/build.sh"
SSLLIB=$SSL/lib; [ -f "$SSLLIB/libssl.a" ] || SSLLIB=$SSL/lib64

# 2. la libc des projets (0.2.191)
LV=0.2.191
bash "$ROOT/tools/rust/libc-projects.sh" "$K"

# 3. la source de cargo, de crates.io (avec son Cargo.lock)
[ -f "$K/dl/cargo-$CV.crate" ] || curl -sSfL "https://static.crates.io/crates/cargo/cargo-$CV.crate" -o "$K/dl/cargo-$CV.crate"
(cd "$K/dl" && echo "$CSHA  cargo-$CV.crate" | sha256sum -c - >/dev/null)
S=$W/cargo-$CV
rm -rf "$S"
tar xzf "$K/dl/cargo-$CV.crate" -C "$W"
cargo +korin fetch -q --manifest-path "$S/Cargo.toml"

# 4. les caisses que Korin patche à une version : celles du Cargo.lock d'une autre version de la même série y sont
#    ramenées (cargo update --precise ; sinon « patch was not used », et la caisse du registre ne connaît pas « korin »)
PATCHES=$(ls "$ROOT"/tools/craft/patches/*-[0-9]*.patch "$ROOT"/tools/cargo/patches/*-[0-9]*.patch 2>/dev/null || true)
for cv in libc-$LV $(for p in $PATCHES; do basename "$p" .patch; done); do
	n=${cv%-*}; v=${cv##*-}
	for old in $(python3 -I - "$S/Cargo.lock" "$n" "$v" <<'PY'
import sys, tomllib
lock, n, v = tomllib.load(open(sys.argv[1], 'rb')), sys.argv[2], sys.argv[3]
series = lambda x: x.split('.')[:2] if x.startswith('0.') else x.split('.')[:1]
print(' '.join(p['version'] for p in lock['package'] if p['name'] == n and 'registry' in p.get('source', '')
               and p['version'] != v and series(p['version']) == series(v)))
PY
	); do
		(cd "$S" && cargo +korin update -q -p "$n@$old" --precise "$v" 2>/dev/null) || echo "cargo : $n $old -> $v refusé" >&2
	done
done

# 5. les caisses patchées (dans $W/vendor), branchées par .cargo/config.toml
REG=$(ls -d "$CARGO_HOME"/registry/src/index.crates.io-* | head -1)
CFG="[patch.crates-io]"
for p in $PATCHES; do
	c=$(basename "$p" .patch)
	name=${c%-*}
	# (seulement les caisses que cargo prend)
	grep -q "^name = \"$name\"" "$S/Cargo.lock" || continue
	if [ ! -f "$W/vendor/$c/.korin" ] || [ "$p" -nt "$W/vendor/$c/.korin" ]; then
		[ -d "$REG/$c" ] || curl -sSfL "https://static.crates.io/crates/$name/$c.crate" | tar xz -C "$REG"
		rm -rf "$W/vendor/$c"
		cp -r "$REG/$c" "$W/vendor/$c"
		(cd "$W/vendor/$c" && patch -p1 -s --no-backup-if-mismatch < "$p")
		touch "$W/vendor/$c/.korin"
	fi
	CFG="$CFG
$(echo "$c" | tr . -) = { path = \"$W/vendor/$c\", package = \"$name\" }"
done
mkdir -p "$S/.cargo"
printf '# tools/cargo/build.sh (Korin)\n%s\n' "$CFG" > "$S/.cargo/config.toml"

# 6. la construction
#    C des caisses -sys : gcc de la chaîne avec posix.specs (en-têtes de libposix et de newlib) ;
#    OpenSSL : celui du système, en statique (openssl-sys) ; curl, libgit2, libssh2 : leurs sources (static-curl,
#    vendored-libgit2 ; libssh2-sys et libz-sys compilent les leurs quand pkg-config ne trouve rien)
export CC_$T_="${TC}gcc -specs=$PLIB/posix.specs" CXX_$T_="${TC}g++ -specs=$PLIB/posix.specs" AR_$T_=${TC}ar \
	CFLAGS_$T_="$CPU -O2 -fno-stack-protector" CXXFLAGS_$T_="$CPU -O2 -fno-stack-protector"
export OPENSSL_DIR=$SSL OPENSSL_INCLUDE_DIR=$SSL/include OPENSSL_LIB_DIR=$SSLLIB OPENSSL_STATIC=1 OPENSSL_NO_VENDOR=1
export PKG_CONFIG_ALLOW_CROSS=0 LIBSSH2_SYS_USE_PKG_CONFIG=0 LIBZ_SYS_STATIC=1 LIBGIT2_NO_VENDOR=0
export CARGO_TARGET_DIR=$W/target
(cd "$S" && cargo korin build --release --bin cargo --features curl/static-curl,vendored-libgit2)
UNUSED=$(python3 -I - "$S/Cargo.lock" "$W/vendor" <<'EOF'
import os, sys, tomllib
lock = tomllib.load(open(sys.argv[1], 'rb'))
mine = {d.rsplit('-', 1)[0] for d in os.listdir(sys.argv[2])} | {'libc'}
here = {p['name'] for p in lock['package'] if 'registry' in p.get('source', '')}
print(' '.join(sorted('%s %s' % (u['name'], u['version']) for u in lock.get('patch', {}).get('unused', [])
                      if u['name'] in mine and u['name'] in here and u['version'] != '0.2.185')))
EOF
)
[ -z "$UNUSED" ] || { echo "cargo : patchs non pris (une autre version dans le Cargo.lock) : $UNUSED" >&2; exit 1; }
"${TC}strip" -o "$OUT/cargo" "$CARGO_TARGET_DIR/$T/release/cargo"
echo "cargo : $OUT/cargo ($(stat -c %s "$OUT/cargo") octets)"
