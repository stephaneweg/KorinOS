#!/bin/sh
# check_n3ds.sh -- le port du coeur 3DS contre la reference (coeur d'Onyx, Dynarmic pas a pas), image par image :
# meme temps de machine, memes appels systeme, memes CRC des deux ecrans, a l'interprete et au recompilateur.
#     sh tools/emu/check_n3ds.sh <rom> <images> [touches]
# Journaux dans $OUT/log. Dans WSL, apres build_n3ds_bench.sh. Les ROM : des copies, jamais au depot.
set -u
OUT=${OUT:-$HOME/n3ds}
export N3DS_FONT=${N3DS_FONT:-${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx}/user/Emulators/n3ds/data/sysfont.bcfnt}
ROM=$1; N=$2; KEYS=${3:-}
T=$(basename "$ROM"); T=${T%.*}
L=$OUT/log; mkdir -p "$L"
[ -n "${NOREF:-}" ] && [ -f "$L/$T.$N.ref" ] || { qemu-aarch64 "$OUT/ref/run_n3ds" "$ROM" "$N" "$KEYS" > "$L/$T.$N.ref" 2> "$L/$T.$N.ref.err" & }
qemu-aarch64 "$OUT/port/run_n3ds" "$ROM" "$N" "$KEYS" > "$L/$T.$N.jit" 2> "$L/$T.$N.jit.err" &
[ -n "${NOINTERP:-}" ] || { N3DS_NOJIT=1 qemu-aarch64 "$OUT/port/run_n3ds" "$ROM" "$N" "$KEYS" > "$L/$T.$N.int" 2> "$L/$T.$N.int.err" & }
wait
for m in jit int; do
	[ -f "$L/$T.$N.$m" ] || continue
	if cmp -s "$L/$T.$N.ref" "$L/$T.$N.$m"; then echo "$T $N $m : identique ($(tail -1 "$L/$T.$N.$m"))"
	else echo "$T $N $m : DIFFERENT, premiere ligne :"; diff "$L/$T.$N.ref" "$L/$T.$N.$m" | head -4; fi
done
