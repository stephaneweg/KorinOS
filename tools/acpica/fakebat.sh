#!/bin/sh
# Compile tools/acpica/fakebat.asl (fausse batterie d'essai) en build/pc/fakebat.aml, avec l'iasl d'ACPICA
# construit dans ~/.cache/korin-acpica (tools/acpica/build.sh a déjà les sources). Dans WSL :
#     sh tools/acpica/fakebat.sh
# puis : python3 -I tools/pc/qemu_nvme.py IMAGE … --qemu=-acpitable --qemu=file=build/pc/fakebat.aml
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
VER=${ACPICA_VER:-20260930}
SRC=${CACHE:-$HOME/.cache/korin-acpica}/acpica-unix-$VER
[ -d "$SRC" ] || sh "$ROOT/tools/acpica/build.sh"
IASL=$SRC/generate/unix/bin/iasl
[ -x "$IASL" ] || make -C "$SRC" -j"$(nproc)" iasl >/dev/null
mkdir -p "$ROOT/build/pc"
"$IASL" -p "$ROOT/build/pc/fakebat" "$ROOT/tools/acpica/fakebat.asl"
# la même en charge (FAKE = 1 : _BST dit « en charge », _PSR branché)
sed 's/Name (FAKE, 0)/Name (FAKE, 1)/' "$ROOT/tools/acpica/fakebat.asl" >"$ROOT/build/pc/fakebat_ac.asl"
"$IASL" -p "$ROOT/build/pc/fakebat_ac" "$ROOT/build/pc/fakebat_ac.asl"
rm -f "$ROOT/build/pc/fakebat_ac.asl"
echo "-> build/pc/fakebat.aml, build/pc/fakebat_ac.aml"
