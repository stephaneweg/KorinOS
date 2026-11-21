#!/bin/bash
# rustc-build : un rustc 1.97.1 qui connaît les cibles de Korin sans fichier JSON (docs/RUST.md, « La cible dans le
# compilateur »). Dans WSL ou le cloud, depuis la racine du dépôt, après ARCH=x86_64 bash tools/rust/build.sh (pour
# libposix et l'éditeur de liens du PC) :
#     bash tools/rust/rustc-build.sh            stage 1 : rustc de l'hôte + la std de x86_64-unknown-korin
#     TARGETS="x86_64-unknown-korin aarch64-unknown-korin" bash tools/rust/rustc-build.sh   les deux
# Les sources de rust-lang/rust à la 1.97.1 (rustc-1.97.1-src.tar.xz, sha256 vérifié), avec :
#   tools/rust/rustc.patch   le compilateur : Os::Korin, spec/base/korin.rs, les deux cibles, la doc de plateforme,
#                            l'essai d'assembleur, la liste des cibles inconnues du compilateur d'amorçage (bootstrap)
#   tools/rust/std.patch, tools/rust/libc.patch   la std et la libc (les mêmes que pour rust-src)
#   tools/rust/llvm.patch    LLVM (src/llvm-project) pour Korin comme hôte (tools/rust/llvm-build.sh)
# SRC_ONLY=1 : seulement les sources patchées et le LLVM de la CI (tools/rust/llvm-build.sh).
# LLVM de l'hôte : celui de la CI de Rust pour ce même commit (rust-dev, LLVM 22.1.6), au lieu de le construire.
# Travail dans ~/korin-rustc (KORIN_RUSTC) : src/ (refait si un patch change), ci-llvm/, build/ (gardé).
# Résultat : $KORIN_RUSTC/src/build/x86_64-unknown-linux-gnu/stage1 (lié à rustup sous le nom « korin-stage1 »).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=${KORIN_RUSTC:-$HOME/korin-rustc}
V=1.97.1
COMMIT=8bab26f4f68e0e26f0bb7960be334d5b520ea452
SRC_SHA=0ed06fdaffd4722a7702e0b4eebfafc897ab8f513e8e1b247cdd7e5c6df6ded2
HOST=x86_64-unknown-linux-gnu
TARGETS=${TARGETS:-x86_64-unknown-korin}
mkdir -p "$W/dl"

# 1. les sources, patchées (refaites quand un des patchs a changé)
[ -f "$W/dl/rustc-$V-src.tar.xz" ] || curl -sSfL "https://static.rust-lang.org/dist/rustc-$V-src.tar.xz" -o "$W/dl/rustc-$V-src.tar.xz"
(cd "$W/dl" && echo "$SRC_SHA  rustc-$V-src.tar.xz" | sha256sum -c - >/dev/null)
S=$W/src
STAMP=$(cat "$ROOT/tools/rust/rustc.patch" "$ROOT/tools/rust/std.patch" "$ROOT/tools/rust/libc.patch" \
	"$ROOT/tools/rust/llvm.patch" | sha256sum | cut -c1-16)
if [ "$(cat "$S/.korin" 2>/dev/null)" != "$STAMP" ]; then
	# (build/ est gardé : il ne dépend que des sources, que cargo et bootstrap suivent)
	[ -d "$S/build" ] && mv "$S/build" "$W/build.keep"
	rm -rf "$S"
	mkdir -p "$S"
	tar xJf "$W/dl/rustc-$V-src.tar.xz" -C "$S" --strip-components=1
	[ -d "$W/build.keep" ] && mv "$W/build.keep" "$S/build"
	(cd "$S" && patch -p1 -s < "$ROOT/tools/rust/rustc.patch")
	(cd "$S/src/llvm-project" && patch -p1 -s < "$ROOT/tools/rust/llvm.patch")
	(cd "$S/library" && patch -p1 -s < "$ROOT/tools/rust/libc.patch" && patch -p1 -s < "$ROOT/tools/rust/std.patch")
	# les fichiers de libc changent : la somme de contrôle du vendeur ne vaut plus
	python3 -I - "$S/library/vendor/libc-0.2.185/.cargo-checksum.json" <<'EOF'
import json, sys
p = sys.argv[1]
j = json.load(open(p))
json.dump({"files": {}, "package": j["package"]}, open(p, "w"))
EOF
	# libc devient un chemin ([patch] de std.patch) : library/Cargo.lock le dit, x.py le tient figé (--frozen)
	(cd "$S/library" && RUSTC_BOOTSTRAP=1 cargo update -q -p libc --offline)
	echo "$STAMP" > "$S/.korin"
fi

# 2. LLVM de l'hôte : rust-dev de la CI de Rust (le LLVM avec lequel la 1.97.1 a été publiée)
L=$W/ci-llvm
if [ ! -x "$L/bin/llvm-config" ]; then
	f=rust-dev-$V-$HOST.tar.xz
	[ -f "$W/dl/$f" ] || curl -sSfL "https://ci-artifacts.rust-lang.org/rustc-builds/$COMMIT/$f" -o "$W/dl/$f"
	rm -rf "$L" && mkdir -p "$L"
	tar xJf "$W/dl/$f" -C "$L" --strip-components=2
fi
[ "${SRC_ONLY:-0}" = 1 ] && exit 0

# 3. bootstrap.toml : l'hôte, les cibles de Korin et leur chaîne C (gcc + posix.specs)
TL=""
for t in $TARGETS; do
	a=${t%%-*}
	case $a in
	x86_64)  K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}; TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
		SPECS=$ROOT/build/pc/posix/lib/posix.specs ;;
	aarch64) K=${KORIN_RUST:-$HOME/.cache/korin-rust}; TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
		SPECS=$ROOT/build/posix/lib/posix.specs ;;
	*) echo "rustc-build : cible inconnue $t" >&2; exit 1 ;;
	esac
	[ -f "$SPECS" ] || { echo "rustc-build : $SPECS absent (ARCH=$a sh tools/posix/build.sh)" >&2; exit 1; }
	mkdir -p "$W/bin"
	for d in gcc g++; do
		printf '#!/bin/sh\nexec %s%s -specs=%s "$@"\n' "$TC" "$d" "$SPECS" > "$W/bin/$a-korin-$d"
		chmod +x "$W/bin/$a-korin-$d"
	done
	TL="$TL
[target.$t]
cc = \"$W/bin/$a-korin-gcc\"
cxx = \"$W/bin/$a-korin-g++\"
ar = \"${TC}ar\"
ranlib = \"${TC}ranlib\"
linker = \"$W/bin/$a-korin-gcc\"
"
done
T="\"$HOST\""
for t in $TARGETS; do T="$T, \"$t\""; done
cat > "$S/bootstrap.toml" <<EOF
# tools/rust/rustc-build.sh (Korin)
change-id = "ignore"

[llvm]
download-ci-llvm = false
# le LLVM de la CI est une bibliothèque partagée (libLLVM.so, link-type.txt)
link-shared = true

[build]
build = "$HOST"
host = ["$HOST"]
target = [$T]
docs = false
extended = false
optimized-compiler-builtins = false

[rust]
lld = false
llvm-tools = false
debuginfo-level = 0

[target.$HOST]
llvm-config = "$L/bin/llvm-config"
$TL
EOF

# 4. stage 1 : rustc de l'hôte et la std des cibles de Korin
A=$HOST
for t in $TARGETS; do A="$A,$t"; done
# (le rustc de stage 1 charge libLLVM.so, qui est dans le LLVM de la CI : pendant la construction par
# LD_LIBRARY_PATH, puis copiée à côté de lui, dans stage1/lib, son rpath)
(cd "$S" && LD_LIBRARY_PATH=$L/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH} python3 x.py build --stage 1 library --target "$A" -j "$(nproc)")
ST=$S/build/$HOST/stage1
cp "$L"/lib/libLLVM*.so* "$ST/lib/"
"${CARGO_HOME:-$HOME/.cargo}/bin/rustup" toolchain link korin-stage1 "$ST" 2>/dev/null || true
"$ST/bin/rustc" --print target-list | grep korin
echo "rustc-build : $ST"
