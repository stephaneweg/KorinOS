#!/bin/bash
# rust : la cible aarch64-unknown-korin (docs/RUST.md). Dans WSL, depuis la racine du dépôt :
#     bash tools/rust/build.sh            tout, depuis zéro : chaîne, patchs, contrôle d'ABI, essais
#     SKIP_TESTS=1 bash tools/rust/build.sh   sans les essais
#
# Rien de Rust dans le dépôt hors de tools/rust : la chaîne et les sources sont dans $KORIN_RUST
# (~/.cache/korin-rust par défaut), refait à chaque lancement (le dossier toolchain est effacé) :
#   rustup/, cargo/   un rustup à part (RUSTUP_HOME, CARGO_HOME) : la chaîne 1.97.1 + rust-src
#   toolchain/        copie de la 1.97.1, liée sous le nom « korin » ; son rust-src reçoit
#                     tools/rust/libc.patch (module unix/korin de libc 0.2.185) et std.patch
#   vendor/           caisses patchées pour « korin » : target-lexicon, getrandom (tools/rust/*.patch)
#   bin/              aarch64-korin-gcc (gcc + posix.specs), cargo-korin (« cargo korin build »)
#   env.sh            les variables à sourcer pour s'en servir
# Résultat des essais : build/rust/tests (korintest, rdlhost, librdl.so, std/*), que
# tools/rust/testimage.py met sur une carte d'essai de QEMU.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
K=${KORIN_RUST:-$HOME/.cache/korin-rust}
RUST_VERSION=1.97.1
HOST=x86_64-unknown-linux-gnu
TC=${TC:-/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}
TARGET_JSON=$ROOT/tools/rust/aarch64-unknown-korin.json
SPECS=$ROOT/build/posix/lib/posix.specs
mkdir -p "$K/dl" "$K/bin"

# 1. libposix (posix.specs, libgcc_mod.a, __errno, relais du dérouleur)
if [ ! -f "$SPECS" ] || [ ! -f "$ROOT/build/posix/lib/libgcc_mod.a" ]; then
	NO_TESTS=1 sh "$ROOT/tools/posix/build.sh"
fi

# 2. rustup à part, puis la chaîne $RUST_VERSION avec rust-src
export RUSTUP_HOME=$K/rustup CARGO_HOME=$K/cargo
RUSTUP=$CARGO_HOME/bin/rustup
if [ ! -x "$RUSTUP" ]; then
	U=https://static.rust-lang.org/rustup/dist/$HOST/rustup-init
	curl -sSfL "$U" -o "$K/dl/rustup-init"
	curl -sSfL "$U.sha256" -o "$K/dl/rustup-init.sha256"
	(cd "$K/dl" && echo "$(cut -d' ' -f1 rustup-init.sha256)  rustup-init" | sha256sum -c -)
	chmod +x "$K/dl/rustup-init"
	"$K/dl/rustup-init" -y --no-modify-path --profile minimal --default-toolchain none >/dev/null
fi
"$RUSTUP" toolchain install "$RUST_VERSION" --profile minimal -c rust-src --no-self-update
SRC_TC=$RUSTUP_HOME/toolchains/$RUST_VERSION-$HOST

# 3. la chaîne « korin » : une copie, son rust-src patché
rm -rf "$K/toolchain"
cp -a "$SRC_TC" "$K/toolchain"
"$RUSTUP" toolchain link korin "$K/toolchain"
L=$K/toolchain/lib/rustlib/src/rust/library
(cd "$L" && patch -p1 -s < "$ROOT/tools/rust/libc.patch" && patch -p1 -s < "$ROOT/tools/rust/std.patch")
# les fichiers de libc changent : la somme de contrôle du vendeur ne vaut plus
python3 - "$L/vendor/libc-0.2.185/.cargo-checksum.json" <<'EOF'
import json, sys
p = sys.argv[1]
j = json.load(open(p))
json.dump({"files": {}, "package": j["package"]}, open(p, "w"))
EOF

# des caisses de crates.io qui ne connaissent pas le système « korin », patchées et prises par
# toutes les caisses ([patch.crates-io] plus bas ; docs/RUST.md, « Caisses patchées ») :
#   target-lexicon  pyo3-build-config et d'autres scripts de construction lisent $TARGET avec elle
#   getrandom 0.3   (pydantic-core, par ahash) : backend getrandom () de libposix, errno par __errno
VENDOR="target-lexicon 0.13.5 adb6935a6f5c20170eeceb1a3835a49e12e19d792f6dd344ccc76a985ca5a6ca
getrandom 0.3.3 26145e563e54f2cadc477553f1ec5ee650b00862f0a58bcd12cbdc5f0ea2d2f4"
mkdir -p "$K/vendor"
PATCHES=""
while read -r c v sha; do
	[ -f "$K/dl/$c-$v.crate" ] || curl -sSfL "https://static.crates.io/crates/$c/$c-$v.crate" -o "$K/dl/$c-$v.crate"
	(cd "$K/dl" && echo "$sha  $c-$v.crate" | sha256sum -c - >/dev/null)
	rm -rf "$K/vendor/$c-$v"
	tar xzf "$K/dl/$c-$v.crate" -C "$K/vendor"
	(cd "$K/vendor/$c-$v" && patch -p1 -s < "$ROOT/tools/rust/$c.patch")
	PATCHES="$PATCHES$c = { path = \"$K/vendor/$c-$v\" }
"
done <<< "$VENDOR"

# 4. l'éditeur de liens et « cargo korin »
cat > "$K/bin/aarch64-korin-gcc" <<EOF
#!/bin/sh
# éditeur de liens de aarch64-unknown-korin (tools/rust/build.sh) : gcc de la chaîne + posix.specs
exec ${TC}gcc -specs=$SPECS "\$@"
EOF
cat > "$K/bin/cargo-korin" <<EOF
#!/bin/sh
# cargo korin build|test|run... : cargo de la chaîne korin, pour aarch64-unknown-korin, std reconstruite
shift
sub=\$1; shift
exec cargo +korin "\$sub" -Zjson-target-spec -Zbuild-std=std,panic_unwind --target $TARGET_JSON "\$@"
EOF
chmod +x "$K/bin/aarch64-korin-gcc" "$K/bin/cargo-korin"
# les caisses des projets prennent la même libc (patch) et cet éditeur de liens
cat > "$CARGO_HOME/config.toml" <<EOF
[patch.crates-io]
libc = { path = "$L/vendor/libc-0.2.185" }
$PATCHES
[target.aarch64-unknown-korin]
linker = "$K/bin/aarch64-korin-gcc"
EOF
cat > "$K/env.sh" <<EOF
# . $K/env.sh ; puis : cargo korin build --release
export RUSTUP_HOME=$RUSTUP_HOME CARGO_HOME=$CARGO_HOME RUSTC_BOOTSTRAP=1
export PATH=$K/bin:$CARGO_HOME/bin:\$PATH
EOF
. "$K/env.sh"

# 5. contrôle d'ABI : libc (module korin) contre les en-têtes C
O=$K/work/abicheck
python3 "$ROOT/tools/rust/abicheck.py" "$L/vendor/libc-0.2.185/src/unix/korin/mod.rs" "${TC}gcc" "$SPECS" "$O"
printf '[package]\nname = "abicheck"\nversion = "0.1.0"\nedition = "2021"\npublish = false\n\n[dependencies]\nlibc = { path = "%s", default-features = false }\n' \
	"$L/vendor/libc-0.2.185" > "$O/Cargo.toml"
(cd "$O" && CARGO_TARGET_DIR=$K/target-abi cargo +korin check -q -Zjson-target-spec -Zbuild-std=core --target "$TARGET_JSON")
echo "abicheck : ok"
# les fonctions déclarées existent-elles ? (newlib + libposix ; rapport seulement)
NL=$(dirname "$(${TC}gcc -print-file-name=libc.a)")
${TC}nm -g --defined-only "$ROOT/build/posix/lib/libposix.a" "$NL/libc.a" "$NL/libm.a" 2>/dev/null |
	awk 'NF == 3 { print $3 }' | sort -u > "$O/defined.txt"
grep -o 'pub fn [a-z_0-9]*' "$L/vendor/libc-0.2.185/src/unix/korin/mod.rs" | cut -d' ' -f3 | sort -u > "$O/declared.txt"
miss=$(comm -23 "$O/declared.txt" "$O/defined.txt" | grep -v '^CMSG_\|^CPU_\|^FD_\|^W[A-Z]' | xargs)
[ -n "$miss" ] && echo "abicheck : déclarées, absentes de libposix et newlib : $miss"

[ "${SKIP_TESTS:-0}" = 1 ] && exit 0

# 6. les essais (docs/RUST.md « Essais »)
export CARGO_TARGET_DIR=$K/target
R=$CARGO_TARGET_DIR/aarch64-unknown-korin/release
OUT=$ROOT/build/rust/tests
mkdir -p "$OUT/std"
for p in korintest rdl; do
	cargo korin build --release --manifest-path "$ROOT/tools/rust/tests/$p/Cargo.toml"
done
${TC}gcc -O2 -Wall -specs="$SPECS" -rdynamic "$ROOT/tools/rust/tests/rdlhost.c" -o "$R/rdlhost"
for f in korintest librdl.so rdlhost; do ${TC}strip "$R/$f" -o "$OUT/$f"; done

# une sélection des essais d'intégration de la std (library/std/tests), pris dans rust-src
S=$K/work/stdtests
STD_TESTS="env env_modify process_spawning thread time path panic create_dir_all_bare istr num error"
mkdir -p "$S/src"
: > "$S/src/lib.rs"
{
	printf '[package]\nname = "stdtests"\nversion = "0.1.0"\nedition = "2024"\npublish = false\n\n'
	printf '[dev-dependencies]\nrand = { version = "0.9", default-features = false, features = ["alloc"] }\n'
	printf 'rand_xorshift = "0.4"\n\n'
	for t in $STD_TESTS; do printf '[[test]]\nname = "%s"\npath = "%s"\n\n' $t "$L/std/tests/$t.rs"; done
	printf '[[test]]\nname = "pipe_subprocess"\npath = "%s"\nharness = false\n\n' "$L/std/tests/pipe_subprocess.rs"
	printf '[[test]]\nname = "sync"\npath = "%s"\n\n' "$L/std/tests/sync/lib.rs"
	printf '[[test]]\nname = "thread_local"\npath = "%s"\n' "$L/std/tests/thread_local/lib.rs"
} > "$S/Cargo.toml"
A="--release -Zjson-target-spec -Zbuild-std=std,panic_unwind,test --target $TARGET_JSON --manifest-path $S/Cargo.toml"
T=""
for t in $STD_TESTS pipe_subprocess thread_local; do T="$T --test $t"; done
cargo +korin test --no-run $A $T
# sync : ses essais demandent une fonction instable que x.py active (-Zcrate-attr)
cargo +korin rustc --test sync $A -- -Zcrate-attr="feature(sync_poison_mod)"
for t in $STD_TESTS pipe_subprocess thread_local sync; do
	b=$(ls -t "$R/deps/$t"-* | grep -v '\.d$' | head -1)
	${TC}strip "$b" -o "$OUT/std/$t"
done
ls -l "$OUT" "$OUT/std"
