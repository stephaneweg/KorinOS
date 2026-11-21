#!/bin/sh
# cargo dans l'arbre système du PC (docs/CARGO.md), dans WSL ou le cloud, après tools/pc/sysroot.sh (qui refait
# build/pc/sysroot) :
#     devtools/rust/bin/cargo   cargo 0.98.0 (bash tools/cargo/build.sh -> build/pc/cargo/cargo) ; /devtools/rust
#                               recevra rustc quand il tournera dans Korin
#     etc/ca-bundle             les autorités de https (crates.io), si l'arbre ne l'a pas (comme sysroot_git.sh)
#
#   sh tools/pc/sysroot.sh && sh tools/pc/sysroot_cargo.sh [arbre]
# Ce qui n'est pas construit est sauté et dit. Rien ne change pour le Pi.
set -e
cd "$(dirname "$0")/../.."

OUT=${1:-build/pc/sysroot}
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : sh tools/pc/sysroot.sh)"; exit 1; }
C=build/pc/cargo/cargo
if [ ! -f "$C" ]; then
	echo "cargo pas construit ($C : bash tools/cargo/build.sh)"
	exit 0
fi
# un programme du PC, pas celui du Pi
od -An -tx1 -j18 -N2 "$C" | grep -q '3e 00' || { echo "$C n'est pas un programme x86-64"; exit 1; }
mkdir -p "$OUT/devtools/rust/bin"
cp "$C" "$OUT/devtools/rust/bin/cargo"
[ -f "$OUT/etc/ca-bundle" ] || cp skel/sd0/etc/ca-bundle "$OUT/etc/"
echo "$OUT : /devtools/rust/bin/cargo ($(stat -c %s "$C") octets)"
