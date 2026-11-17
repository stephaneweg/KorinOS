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
echo "-> build/pc/fakebat.aml"
