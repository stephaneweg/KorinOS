#!/bin/sh
# build.sh -- libacp (src/lib/acp) et agent, le client terminal de Vibe (docs/PYTHON-PORT.md « Étape 8 »), par la
# chaîne croisée (WSL), sur libposix et newlib comme rg et bash. À lancer depuis la racine du dépôt, après
# tools/posix/build.sh :
#     sh tools/agent/build.sh
# Produit :
#   build/agent/libacp.a              la bibliothèque (l'app de l'étape 9 compile ses sources elle-même)
#   build/agent/sdroot/bin/agent      pour la carte (recopié par build.ps1)
#   build/agent/host/agent            le même agent pour le PC (Linux), pour l'essayer contre le Vibe de l'étape 0
# ARCH=x86_64 : pour Korin PC (chaîne x86_64-onyx-elf, libposix de build/pc/posix) : build/pc/agent/libacp.a,
# build/pc/agent/sdroot/bin/agent (tools/pc/sysroot_python.sh le pose dans l'arbre du PC) ; pas d'agent Linux.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
ARCH=${ARCH:-aarch64}
case $ARCH in
aarch64) TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2}; OUT=$ROOT/build/agent; SPECS=$ROOT/build/posix/lib/posix.specs
	MCPU=-mcpu=cortex-a72 ;;
x86_64) TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2}; OUT=$ROOT/build/pc/agent; SPECS=$ROOT/build/pc/posix/lib/posix.specs
	MCPU= ;;
*) echo "agent : ARCH=aarch64 ou x86_64" >&2; exit 1 ;;
esac
TC=$TOOL/bin/$ARCH-onyx-elf-
HOSTCC=${HOSTCC:-cc}
[ -f "$SPECS" ] || { echo "agent : lancer d'abord tools/posix/build.sh (ARCH=$ARCH)" >&2; exit 1; }
mkdir -p "$OUT/obj" "$OUT/sdroot/bin" "$OUT/host"
L=$ROOT/src/lib/acp
CFLAGS="-O2 -g0 -Wall -Wextra -Wno-unused-parameter -I$L"

for f in json acp home; do
	${TC}gcc $MCPU -specs="$SPECS" $CFLAGS -ffunction-sections -fdata-sections -c "$L/$f.c" -o "$OUT/obj/$f.o"
done
rm -f "$OUT/libacp.a"
${TC}ar rcs "$OUT/libacp.a" "$OUT/obj/json.o" "$OUT/obj/acp.o" "$OUT/obj/home.o"
${TC}gcc $MCPU -specs="$SPECS" $CFLAGS -c "$ROOT/tools/agent/agent.c" -o "$OUT/obj/agent.o"
${TC}gcc $MCPU -specs="$SPECS" -Wl,--gc-sections "$OUT/obj/agent.o" "$OUT/libacp.a" -o "$OUT/obj/agent.elf"
${TC}strip "$OUT/obj/agent.elf" -o "$OUT/sdroot/bin/agent"
ls -l "$OUT/sdroot/bin/agent"

# le même pour le PC (Linux)
[ "$ARCH" = aarch64 ] || exit 0
$HOSTCC $CFLAGS -D_GNU_SOURCE "$L/json.c" "$L/acp.c" "$L/home.c" "$ROOT/tools/agent/agent.c" -o "$OUT/host/agent"
ls -l "$OUT/host/agent"
