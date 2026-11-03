#!/bin/sh
# qpucheck : construit le banc (PC, WSL) avec qpusim d'Onyx et l'execute sur build/mesa/qpu_shaders.h.
#     sh tools/mesa/qpucheck.sh                 (depuis la racine du depot, apres qpucheck.py)
set -eu
. "$(dirname "$0")/env.sh"
Q=${ONYX:-$(cd "$(dirname "$0")/../.." && pwd)/build/onyx}/tools/qpu
G=$OUT/qpucheck
mkdir -p "$G"
for f in mesa/broadcom/qpu/qpu_instr.c mesa/broadcom/qpu/qpu_pack.c mesa/broadcom/qpu/qpu_disasm.c ralloc_stub.c; do
	gcc -O1 -std=gnu11 -w -I"$Q/mesa" -c "$Q/$f" -o "$G/$(basename "$f" .c).o"
done
g++ -O1 -std=c++20 -w -I"$Q" -I"$Q/mesa" -I"$ARMPI_ROOT/build/mesa" \
	"$ARMPI_ROOT/tools/mesa/qpucheck.cpp" "$Q/qpusim.cpp" "$G"/*.o -lm -o "$G/qpucheck"
"$G/qpucheck"
